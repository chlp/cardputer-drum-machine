# Cardputer Meme Soundboard

Firmware for [M5 Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer) that turns it into a portable meme soundboard with a built-in MP3 player.

## Features

### Mode 1 — Soundboard
- Press any key (a–z, 0–9) to instantly play the assigned meme sound
- The previous sound stops immediately when a new key is pressed
- A full-screen image associated with the sound is displayed
- **ESC** (`` ` `` key) stops playback and returns to idle screen

### Mode 2 — MP3 Player
- Scrollable list of all `.mp3` files from the `/mp3` folder on the SD card
- Navigate with `j`/`k` (or `,`/`;`)
- Press **ENTER** to play the selected track
- Tracks auto-advance when finished
- **ESC** stops playback

### Switching Modes
Press **TAB** at any time to switch between Soundboard and MP3 Player.

---

## SD Card Setup

Format the SD card as FAT32, then create these folders:

### `/sounds/` — Meme sounds
Each key gets one MP3 file + one image file with the same base name:

```
/sounds/a.mp3    ← plays when 'a' is pressed
/sounds/a.jpg    ← shown when 'a' is pressed (recommended 240×110 px)
/sounds/b.mp3
/sounds/b.jpg
/sounds/1.mp3
/sounds/1.jpg
...
```

Supported image formats: **JPEG** (`.jpg`). PNG also works.
Supported audio format: **MP3** (`.mp3`).

### `/mp3/` — MP3 Player playlist
Put any MP3 files here:
```
/mp3/lofi-beat.mp3
/mp3/rickroll.mp3
...
```

---

## Building & Flashing

### Requirements
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB-C cable connected to M5 Cardputer

### Steps
```bash
# Install dependencies + build
pio run

# Flash to device
pio run -t upload
pio run -t upload --upload-port /dev/tty.usbmodem201101

# Open serial monitor
pio device monitor
```

---

## Controls Reference

| Key | Soundboard | MP3 Player |
|-----|-----------|------------|
| `a`–`z`, `0`–`9` | Play meme sound | — |
| TAB | → MP3 Player | → Soundboard |
| ESC (`` ` ``) | Stop sound | Stop playback |
| ENTER | — | Play selected track |
| `j` or `,` | — | Next track |
| `k` or `;` | — | Previous track |

---

## Hardware

- **Device**: M5 Cardputer ADV
- **CPU**: ESP32-S3
- **Display**: 240×135 ST7789 (landscape)
- **Audio**: I2S speaker
- **Storage**: microSD via SPI

## License
MIT
