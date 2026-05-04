#!/usr/bin/env bash
# Mirror repo sd_card_content/ to the SD card inside the Cardputer via USB serial.
# Uses sd_xfer.py sync: deletes remote files not in sd_card_content/, uploads
# missing or size-mismatched files. Requires: pip install pyserial
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
XFER="$SCRIPT_DIR/sd_xfer.py"
SRC="$REPO_ROOT/sd_card_content"
NORMALIZE="$SCRIPT_DIR/normalize_mp3.sh"

DEFAULT_PORT="/dev/tty.usbmodem201101"
DEFAULT_BAUD=115200

usage() {
  echo "Usage: $0 [-p <port>] [-b <baud>]" >&2
  echo "  -p  Serial port (default: $DEFAULT_PORT)" >&2
  echo "  -b  Baud rate   (default: $DEFAULT_BAUD)" >&2
  echo "" >&2
  echo "Example: $0" >&2
  echo "Example: $0 -p /dev/tty.usbmodem101" >&2
  exit 1
}

PORT="$DEFAULT_PORT"
BAUD="$DEFAULT_BAUD"

while getopts "p:b:h" opt; do
  case "$opt" in
    p) PORT="$OPTARG" ;;
    b) BAUD="$OPTARG" ;;
    h) usage ;;
    *) usage ;;
  esac
done

[[ -d "$SRC" ]]        || { echo "Missing source: $SRC" >&2; exit 1; }
[[ -f "$XFER" ]]       || { echo "Missing tool: $XFER" >&2; exit 1; }
[[ -f "$NORMALIZE" ]]  || { echo "Missing tool: $NORMALIZE" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "python3 not found" >&2; exit 1; }
[[ -e "$PORT" ]]  || { echo "Serial port not found: $PORT" >&2; echo "Is the Cardputer connected?" >&2; exit 1; }

bash "$NORMALIZE"
echo

echo "Port:    $PORT  (baud $BAUD)"
echo "Source:  $SRC/"
echo "This will DELETE files on the SD card that are not in sd_card_content/."
if [[ "${SYNC_SD_CONFIRM:-}" != "yes" ]]; then
  read -r -p "Continue? [y/N] " ok
  case "${ok:-}" in
    y|Y|yes|YES) ;;
    *) echo "Aborted." >&2; exit 1 ;;
  esac
fi

python3 "$XFER" -p "$PORT" -b "$BAUD" sync "$SRC"
