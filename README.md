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
Press **TAB** to cycle: **MP3 Player** → each **soundboard panel** (subfolder of `/boards/` on the SD card, `meme` first) → back to MP3. When you switch to a panel, its folder name appears briefly (e.g. `MEME BOARD`).

---

## SD Card Setup

Format the SD card as FAT32, then create these folders:

### `/boards/` — Soundboard panels
Each **immediate subfolder** of `/boards/` is a separate panel (TAB cycles through them after MP3). Put the same key layout in each folder: one MP3 + one image per key (`a.mp3`, `a.jpg`, …).

The **`meme`** folder is the default / standard set (first after MP3). If a key has no file on the **current** panel, the firmware uses sound + image from **`/boards/meme/`** if present.

```
/boards/meme/a.mp3
/boards/meme/a.jpg
/boards/mykit/b.mp3   ← custom panel "mykit"
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

### Copying files over USB (no card reader)

The firmware exposes the SD card over USB serial (`SD> …` commands) — no need to remove the card. The repo includes `sd_card_content/` with all required files. To mirror it to the card in one step:

```bash
pip install pyserial          # one-time
./tools/sync_sd_card_content.sh
```

This deletes extra files on the card and uploads anything missing or changed. See **[docs/SD_SERIAL_TRANSFER.md](docs/SD_SERIAL_TRANSFER.md)** for the full protocol and individual file commands.

---

## Building & Flashing

### Requirements
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB-C cable connected to M5 Cardputer

### First-time: build, flash, and SD card

From the repository root:

```bash
# Build firmware
pio run

# List serial ports (pick the Cardputer / CP210x / USB JTAG port)
pio device list

# Flash (auto-detect port when only one device is connected)
pio run -t upload

# Or specify the port explicitly (examples; yours will differ)
pio run -t upload --upload-port /dev/tty.usbmodem1101    # macOS / Linux
pio run -t upload --upload-port COM5                     # Windows
```

After flashing, prepare the SD card while the Cardputer is still connected:

```bash
./tools/sync_sd_card_content.sh
```

Optional serial monitor:

```bash
pio device monitor
pio device monitor --port /dev/tty.usbmodem1101
```

---

## Soundboard — Default Sound Map

All 36 sounds + images are in `sd_card_content/boards/meme/` — copy `boards/` (and `mp3/`) to the SD card root.
Without SD card, built-in synthesized sounds play instead.

| Key | Sound | Key | Sound | Key | Sound |
|-----|-------|-----|-------|-----|-------|
| `A` | 📣 Air Horn | `M` | 🟫 Minecraft Oof | `Y` | ✨ Anime Wow |
| `B` | 💥 Vine Boom | `N` | 😱 Oh No No No | `Z` | ✗ Wrong Buzzer |
| `C` | 🐿 Dramatic Chipmunk | `O` | 🔵 Roblox Oof | `0` | 🎮 GameCube Startup |
| `D` | ⏩ To Be Continued... | `P` | 😱 Patrick Star Scream | `1` | 🏆 Victory Fanfare |
| `E` | 😤 Emotional Damage | `Q` | 🦆 Quack | `2` | 👁 FNAF Jumpscare |
| `F` | 🎬 Directed by R.B.Weide | `R` | 🎵 Rickroll | `3` | ⏱ 3…2…1 Countdown |
| `G` | 💀 GTA Wasted | `S` | 🎺 Sad Trombone | `4` | 😨 FNAF 2 Hallway |
| `H` | 🧙 Hello There | `T` | ☢️ Tactical Nuke | `5` | 🚨 Red Alert |
| `I` | 🎵 Imperial March | `U` | 🚀 Among Us Ejected | `6` | ⚡ It's Pikachu! |
| `J` | 💪 John Cena! | `V` | 😐 Bruh | `7` | 💰 Ka-Ching! |
| `K` | 😎 SHEEEESH | `W` | ⚠️ Windows XP Error | `8` | 🕷 Spider-Man Meme |
| `L` | 😂 Laugh Track | `X` | 🪟 Windows XP Startup | `9` | 💢 It's Over 9000! |

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
