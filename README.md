# Meme Board

Firmware for [M5 Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer) that turns it into a portable meme soundboard with a built-in MP3 player.

## Features

### Mode 1 — Soundboard
Two sub-modes, selected automatically based on SD card state:

**Browse UI** (SD card with `/boards/` present):
- `,` / `/` step through sounds; the current sound's image fills the screen
- Any letter/digit key (`a`–`z`, `0`–`9`) jumps directly to that sound
- **ENTER** plays the selected sound
- **ESC** (`` ` `` key) stops playback

**Piano** (no SD boards, or PIANO board active):
- Hold any key to play the corresponding note (polyphonic, up to 7 voices)
- If a key has an `.mp3` file on the active board, the meme sound plays instead of a tone
- Pressed keys are highlighted yellow on the on-screen piano
- **ESC** stops all notes

### Mode 2 — MP3 Player
- Scrollable file browser starting at the `/mp3` folder on the SD card; arbitrary sub-folder nesting is supported
- Navigate with `j`/`.` (next) and `k`/`;` (previous)
- `l`/`/` to enter a folder; `h`/`,` to go up
- Press **ENTER** to play the selected track (or enter a folder)
- Tracks auto-advance when finished
- **ESC** while playing/paused scrolls the list to the current file; **ESC** while stopped goes up one level

### Switching Modes
Press **TAB** to cycle through all modes in order:

**Soundboard panels** (one per subfolder of `/boards/`, `meme` first) → **PIANO** (pure synthesiser, no SD sounds) → **MP3 Player** → back to the first soundboard panel.

When you switch to a panel, its name appears briefly on screen (e.g. `MEME BOARD`, `PIANO BOARD`).

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

### Diagnostic logs

The firmware emits structured diagnostic lines on the same USB serial port so you can debug crashes / reboots without extra hardware:

```
[123][BOOT] reset=PANIC code=3
[124][BOOT] idf=v4.4.7-dirty arduino=2.0.17
[125][BOOT] heap free=210432 min=210432 largest=110592 psram_free=0
[2034][AUDIO] play path=/mp3/long.mp3 size=8421376
[2035][AUDIO] heap free=204812 min=200800 largest=98304 psram_free=0
[2036][AUDIO] task_stack_min_free=12288 bytes
[2891][AUDIO] stop
```

- The first `[BOOT] reset=…` line tells you why the previous run terminated (`PANIC`, `TASK_WDT`, `BROWNOUT`, `POWERON`, `SW`, …) — the single most useful clue when chasing random reboots.
- `task_stack_min_free` is the lowest free stack ever observed for the audio task. If it drops below ~2 KB while playing a particular MP3, bump the `xTaskCreatePinnedToCore` stack size in `src/audio.cpp`.
- Lines all start with `[<millis>][<TAG>]`, which `tools/sd_xfer.py` already filters out, so the SD serial protocol keeps working with logging on.

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

| Key | Soundboard (Browse) | Soundboard (Piano) | MP3 Player |
|-----|--------------------|--------------------|------------|
| `a`–`z`, `0`–`9` | Jump to & play sound | Play note / meme | — |
| `,` | Previous sound | — | Go up one level |
| `/` | Next sound | — | Enter folder |
| ENTER | Play selected sound | — | Play / pause / enter folder |
| `j` or `.` | — | — | Next item |
| `k` or `;` | — | — | Previous item |
| `h` | — | — | Go up one level |
| `l` | — | — | Enter folder |
| TAB | → next panel / PIANO / MP3 | → next panel / PIANO / MP3 | → first soundboard panel (meme) |
| `+` or `=` | Volume up | Volume up | Volume up |
| `-` | Volume down | Volume down | Volume down |
| ESC (`` ` ``) | Stop sound | Stop all notes | If playing/paused: show current file; if stopped: go up |

---

## Hardware

- **Device**: M5 Cardputer ADV
- **CPU**: ESP32-S3
- **Display**: 240×135 ST7789 (landscape)
- **Audio**: I2S speaker
- **Storage**: microSD via SPI

## License
MIT
