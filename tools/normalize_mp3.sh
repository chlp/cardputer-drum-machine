#!/usr/bin/env bash
# Normalize MP3 files in sd_card_content/ for Cardputer playback.
#
# Rules (tuned for the built-in mono speaker, Helix MP3 decoder on ESP32-S3):
#   /boards/**  — meme / soundboard clips  → mono, 44100 Hz, 96 kb/s
#   /mp3/**     — music player tracks      → stereo, 44100 Hz, 128 kb/s max
#
# A file is only re-encoded when it deviates from the target:
#   wrong sample rate, wrong channel count, or bitrate above the ceiling.
#
# For the music folder the encode bitrate is min(original, 128 kb/s) to avoid
# inflating files that were originally encoded below the target.
#
# Conversion is done in-place (temp file → atomic rename).
#
# Requirements: ffmpeg + ffprobe must be on PATH.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="$REPO_ROOT/sd_card_content"

# ── targets ──────────────────────────────────────────────────────────────────
BOARDS_RATE=44100
BOARDS_CHAN=1        # mono
BOARDS_KBPS=96
# Re-encode if bitrate exceeds this ceiling.  LAME ABR can report the bitrate
# ~30 % above the nominal target, so the ceiling is set well above the output
# (~96 kb/s) but still catches files genuinely too large (192+ kb/s originals).
BOARDS_BIT=150000    # 150 kb/s

MUSIC_RATE=44100
MUSIC_CHAN=1         # mono — speaker is mono; halves decode work
MUSIC_KBPS=128
# Same logic: allows 128 kb/s ABR output (probes up to ~175 kb/s), but still
# catches 256 / 320 kb/s originals.
MUSIC_BIT=220000     # 220 kb/s

# ── helpers ──────────────────────────────────────────────────────────────────
check_deps() {
  local missing=()
  command -v ffprobe >/dev/null 2>&1 || missing+=(ffprobe)
  command -v ffmpeg  >/dev/null 2>&1 || missing+=(ffmpeg)
  if [[ ${#missing[@]} -gt 0 ]]; then
    echo "ERROR: required tools not found: ${missing[*]}" >&2
    echo "Install via: brew install ffmpeg  (macOS) / apt install ffmpeg  (Linux)" >&2
    exit 1
  fi
}

# probe_audio <file>
# Prints three lines: sample_rate  channels  bit_rate
probe_audio() {
  ffprobe -v quiet -print_format json -show_streams -select_streams a:0 "$1" 2>/dev/null \
    | python3 -c "
import sys, json
s = json.load(sys.stdin)['streams'][0]
print(s.get('sample_rate',''))
print(s.get('channels',''))
print(s.get('bit_rate',''))
"
}

# convert_inplace <file> <channels> <rate> <enc_kbps>
convert_inplace() {
  local file="$1" chan="$2" rate="$3" kbps="$4"
  local tmp="${file}.norm_tmp.mp3"

  ffmpeg -y -loglevel error \
    -i "$file" \
    -ac "$chan" -ar "$rate" \
    -codec:a libmp3lame -b:a "${kbps}k" \
    -map_metadata -1 \
    "$tmp"

  mv "$tmp" "$file"
}

# ── main ─────────────────────────────────────────────────────────────────────
main() {
  check_deps

  [[ -d "$SRC" ]] || { echo "ERROR: sd_card_content not found: $SRC" >&2; exit 1; }

  local converted=0 skipped=0

  # Collect all paths into an array up-front so the loop body can freely
  # spawn subprocesses without any risk of them consuming the find pipe's stdin.
  local -a files=()
  while IFS= read -r -d '' f; do
    files+=("$f")
  done < <(find "$SRC" -name "*.mp3" -print0 | sort -z)

  for file in "${files[@]}"; do
    local rel="${file#"$SRC/"}"
    local trate tchan tceil tkbps

    if [[ "$rel" == boards/* ]]; then
      trate=$BOARDS_RATE; tchan=$BOARDS_CHAN; tceil=$BOARDS_BIT; tkbps=$BOARDS_KBPS
    elif [[ "$rel" == mp3/* ]]; then
      trate=$MUSIC_RATE;  tchan=$MUSIC_CHAN;  tceil=$MUSIC_BIT;  tkbps=$MUSIC_KBPS
    else
      continue
    fi

    # Single ffprobe call per file — pipe goes directly to python3, no sed.
    local rate chan brate
    read -r rate chan brate < <(
      ffprobe -v quiet -print_format json -show_streams -select_streams a:0 "$file" 2>/dev/null \
        | python3 -c "
import sys, json
s = json.load(sys.stdin)['streams'][0]
print(s.get('sample_rate',''), s.get('channels',''), s.get('bit_rate',''))
"
    )

    if [[ -z "$rate" || -z "$chan" ]]; then
      echo "  [?] cannot probe — skipping: $rel" >&2
      continue
    fi

    local needs=0
    [[ "$rate" != "$trate" ]] && needs=1
    [[ "$chan"  != "$tchan" ]] && needs=1
    if [[ -n "$brate" && "$brate" -gt "$tceil" ]]; then
      needs=1
    fi

    if [[ $needs -eq 1 ]]; then
      local bkbps=$(( ${brate:-0} / 1000 ))

      # For music/meme tracks: cap encode bitrate at the original to avoid
      # inflating low-bitrate files that only needed a sample-rate or channel fix.
      local enc_kbps=$tkbps
      if [[ $bkbps -gt 0 && $bkbps -lt $tkbps ]]; then
        enc_kbps=$bkbps
      fi

      printf "  [convert] %-40s  %s Hz / %sch / %d kb/s  →  %s Hz / %sch / %d kb/s\n" \
        "$rel" "$rate" "$chan" "$bkbps" "$trate" "$tchan" "$enc_kbps"
      convert_inplace "$file" "$tchan" "$trate" "$enc_kbps"
      (( converted++ )) || true
    else
      (( skipped++ )) || true
    fi
  done

  echo "  Normalized: $converted file(s) converted, $skipped already OK."
}

echo "==> Normalizing MP3 files in sd_card_content/ …"
main
