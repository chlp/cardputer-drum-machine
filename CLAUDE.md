# Cardputer Meme Soundboard — CLAUDE.md

## Project Overview
Firmware for **M5 Cardputer ADV** (ESP32-S3): a dual-mode meme soundboard + MP3 player.

## Hardware
- Board: M5Stack Cardputer ADV (ESP32-S3, 240×135 ST7789 display, physical QWERTY keyboard)
- Audio: built-in I2S speaker via M5Unified
- Storage: microSD card via SPI (SCK=40, MISO=39, MOSI=14, CS=12)

## Build System
- **PlatformIO** + Arduino framework
- Board target: `m5stack-stamps3`
- Flash with: `pio run -t upload`
- Monitor: `pio device monitor`

## Key Libraries
- `m5stack/M5Cardputer` — hardware abstraction
- `earlephilhower/ESP8266Audio` — MP3 decoding (AudioGeneratorMP3, AudioFileSourceSD, AudioOutputM5Speaker)
- LovyanGFX (bundled with M5Cardputer) — display + JPEG/PNG rendering from SD

## SD Card Structure
```
/sounds/
  a.mp3   ← meme sound for key 'a'
  a.jpg   ← image shown when 'a' is pressed (240×110 px recommended)
  b.mp3
  b.jpg
  ...     ← one pair per key (a-z, 0-9)
/mp3/
  song1.mp3
  song2.mp3
  ...     ← MP3 player playlist
```

## Controls
| Key | Soundboard mode | MP3 Player mode |
|-----|-----------------|-----------------|
| a–z, 0–9 | Play sound + show image | — |
| ESC (`` ` ``) | Stop playback | Stop playback |
| TAB | Switch to MP3 Player | Switch to Soundboard |
| j / `,` | — | Next track |
| k / `;` | — | Previous track |
| ENTER | — | Play selected |

## Source Layout
```
src/
  main.cpp   ← all firmware logic
platformio.ini
CLAUDE.md
README.md
```

## Coding Notes
- Audio is non-blocking: `gen->loop()` called every frame
- Delete + recreate AudioGenerator/AudioFileSource on each new sound (avoids state issues)
- Mode switch always stops current playback first
- No sound/image file = graceful fallback (text label shown instead)
- Respond in Russian to the user
