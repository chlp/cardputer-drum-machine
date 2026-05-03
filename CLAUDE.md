# Cardputer Meme Soundboard — CLAUDE.md

## Project Overview
Firmware for **M5 Cardputer ADV** (ESP32-S3): dual-mode device.
- **Mode 1 — Soundboard**: polyphonic musical keyboard (36 notes, C3–B5). Panels are first-level subfolders under `/boards/`; default memes live in `/boards/meme/`. If the active panel has no `.mp3` for a key, sound and image are taken from `/boards/meme/`. TAB cycles MP3 ↔ boards (`meme` first).
- **Mode 2 — MP3 Player**: file manager with navigation over SD card folders.

## Hardware
- Board: M5Stack Cardputer ADV (ESP32-S3, 240×135 ST7789 display, physical QWERTY keyboard)
- Audio: built-in I2S speaker via M5Unified
- Storage: microSD via SPI — SCK=40, MISO=39, MOSI=14, CS=12
- Partition: `default_8MB.csv` (device is 8MB, not 16MB!)

## Build System
- **PlatformIO** + Arduino framework, board: `m5stack-stamps3`
- `pio run` — build
- `pio run -t upload --upload-port /dev/tty.usbmodem201101` — flash firmware
- `pio device monitor` — serial monitor

## Key Libraries
- `m5stack/M5Cardputer` — hardware abstraction (keyboard, display, I2S)
- ESP8266Audio — MP3 decoding, **local copy** in `lib/ESP8266AudioLocal/`
  - AudioOutputI2S*, AudioOutputPDM*, AudioOutputSPDIF*, AudioOutputULP* were removed — they require `driver/i2s_std.h` (IDF5), while Arduino-ESP32 2.x uses IDF4
- `src/AudioOutputM5Speaker.h` — custom bridge ESP8266Audio → M5Unified Speaker (double buffer, `playRaw()` on channel 0)
- LovyanGFX (bundled with M5Cardputer) — display, JPEG/PNG from SD

## SD Card Structure
```
/boards/
  meme/    ← default panel: a.mp3, a.jpg, … (a-z, 0-9)
  <other>/  ← custom panel — same filenames; missing file → fallback from meme/
/mp3/
  classic/
    01-Ode_to_Joy.mp3
    ...
  background/
    01-Lofi_Chill.mp3
    ...
  ← arbitrary folder nesting is supported
```
Bundled content: `sd_card_content/` — copy the whole tree to the SD card.

## Soundboard — Note Mapping
Keyboard rows bottom → top map to low → high notes:

| Row | Keys | Notes |
|-----|------|-------|
| Bottom | `z x c v b n m` | C3 C#3 D3 D#3 E3 F3 F#3 |
| Middle | `a s d f g h j k l` | G3 G#3 A3 A#3 B3 C4 C#4 D4 D#4 |
| Top | `q w e r t y u i o p` | E4 F4 F#4 G4 G#4 A4 A#4 B4 C5 C#5 |
| Digits | `1 2 3 4 5 6 7 8 9 0` | D5 D#5 E5 F5 F#5 G5 G#5 A5 A#5 B5 |

- Up to 7 simultaneous voices (M5.Speaker channels 1–7)
- A note sounds while the key is held (press → `noteOn`, release → `noteOff`)
- A mini piano is drawn on screen; pressed keys are highlighted in yellow
- If a key has an `.mp3` on the active board or in `/boards/meme/`, the meme sound plays (tones stop)

## MP3 Player — Controls
| Key | Action |
|-----|--------|
| `j` / `,` | Next item |
| `k` / `;` | Previous item |
| `l` or ENTER on folder | Enter folder |
| `h` | Go up one level |
| ENTER on file | Play / pause / resume |
| `` ` `` (ESC) | Stop |
| TAB | Next soundboard panel or MP3 Player (cycle) |

## Universal Controls
| Key | Soundboard | MP3 Player |
|-----|------------|------------|
| TAB | → next panel or MP3 Player | → first panel in `/boards/` (usually `meme`) |
| `` ` `` | Stop + reset notes | Stop playback |

## Source Layout
```
src/
  main.cpp               ← all firmware logic
  AudioOutputM5Speaker.h ← ESP8266Audio → M5Unified bridge
lib/
  ESP8266AudioLocal/     ← ESP8266Audio without I2S output files
sd_card_content/
  boards/meme/           ← default memes + images; boards/<name>/ — custom panels
  mp3/
    classic/             ← 10 classical pieces
    background/          ← 10 background tracks
platformio.ini
```

## Critical Implementation Notes
- **IDF4/IDF5**: `driver/i2s_std.h` is not available on IDF4 → I2S output files removed from ESP8266Audio
- **Images**: `drawJpgFile(SD, path)` does not work (no `DataWrapperT<fs::SDFS>`). Workaround: read the file into heap with `malloc`, then `drawJpg(buf, len)`
- **Audio**: `gen->loop()` is called every frame (non-blocking). Each new sound → `delete gen/src`, recreate
- **Polyphony**: M5.Speaker channel 0 is used by AudioOutputM5Speaker (MP3). Notes use channels 1–7
- **`M5.Speaker.tone(freq, 0, ch, false)`**: duration=0 → infinite, stop_current=false → does not cut other channels
- **Keyboard**: press and release both go through `isChange()` (not only `isPressed()`). MP3 Player only needs `isPressed()`
- **Partition**: must use `default_8MB.csv` or the device will not boot

## Language
Communicate with the user in English.
