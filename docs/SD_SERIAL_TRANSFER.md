# USB serial — file operations on the SD card

While the Cardputer is connected over USB, the firmware accepts **text commands** on the serial port (same port as `pio device monitor`, default **115200** baud). You can list directories, read and delete files, create folders, and **upload files from your computer** to the microSD card without removing it.

Commands must start with the prefix **`SD>`** so random typing in a serial monitor does not touch the card.

On boot the device may print a short banner such as `+SDX ready (prefix SD> …)`.

## Before you start

1. **Close the serial monitor** (PlatformIO, Arduino IDE, `screen`, etc.) before running the Python helper — only one program can open the port at a time.
2. Paths are **absolute**, Unix-style, starting with `/` (for example `/boards/meme/a.mp3`).
3. Invalid or unsafe paths are rejected: no `..`, no `\`, length at most 128 characters.
4. **`RM` removes files only**, not directories.

## Command reference

Send each command as **one line** terminated by **LF** (`\n`). Lines may optionally use CRLF.

### `SD> PING`

Checks that the handler is alive.

**Response:** `+OK pong`

### `SD> LS <directory>`

Lists a directory.

**Response:**

- `+LIST` on its own line
- One line per entry:
  - Directory: `D	<full path>`
  - File: `F	<full path>	<size in bytes>`
- A single `.` on its own line marks the end of the listing

**Example:** `SD> LS /boards/meme`

### `SD> STAT <file>`

Returns the size of a file (not for directories).

**Response:** `+STAT <bytes>`

**Example:** `SD> STAT /mp3/classic/01-Ode_to_Joy.mp3`

### `SD> GET <file>`

Downloads a file from the SD card as raw bytes over serial.

**Response:**

1. Line `+BIN <size>` where `<size>` is the number of bytes that follow.
2. Immediately after the newline ending that line, exactly `<size>` raw bytes (binary).

**Example:** `SD> GET /boards/meme/a.mp3` — then read the binary payload on the host.

### `SD> RM <file>`

Deletes a file.

**Response:** `+OK` on success, or `-ERR <reason>` on failure.

**Example:** `SD> RM /mp3/temp.mp3`

### `SD> MKDIR <directory>`

Creates a directory. Parent directories are created as needed.

**Response:** `+OK` or `-ERR …`

**Example:** `SD> MKDIR /boards/my_panel`

### `SD> PUT <file> <size>`

Uploads a file **from the host** to the SD card.

1. Host sends one line: `SD> PUT /path/to/file.mp3 123456` (path, space, size in bytes).
2. Device responds with `+GO` when it is ready to accept the payload (or `-ERR …` if it cannot open/write).
3. Host sends exactly **`<size>`** raw bytes (no encoding).
4. Device responds with `+OK` when the transfer finished, or `-ERR …` on failure.

If the file already exists, it is replaced. Parent folders are created automatically.

Maximum size per transfer: **512 MiB** (536870912 bytes).

**Example:** see the Python tool below.

### `SD> HELP`

Prints a one-line summary of supported commands.

## Python helper (`tools/sd_xfer.py`)

Install [pyserial](https://pyserial.readthedocs.io/):

```bash
pip install pyserial
# macOS (Homebrew-managed Python):
python3 -m pip install --break-system-packages pyserial
```

Replace the port with yours (macOS often `/dev/tty.usbmodem…`, Linux `/dev/ttyACM0`, Windows `COM3`).

```bash
# Mirror repo sd_card_content/ to the SD card — delete extra files, upload missing/changed
./tools/sync_sd_card_content.sh                          # uses /dev/tty.usbmodem201101 by default
./tools/sync_sd_card_content.sh -p /dev/tty.usbmodem101 # explicit port

# Or call sd_xfer.py directly:
python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 sync

# List a folder on the SD card
python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 ls /boards/meme

# Remote file size
python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 stat /boards/meme/a.mp3

# Download from SD → local file
python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 get /boards/meme/a.mp3 ./a.mp3

# Upload local file → SD (creates parent dirs if needed)
python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 put ./track.mp3 /mp3/track.mp3

# Delete a file on the SD card
python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 rm /mp3/track.mp3

# Create a directory (and parents)
python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 mkdir /boards/custom
```

Optional: `-b 115200` sets baud rate (default is 115200, matching `monitor_speed` in `platformio.ini`).

### `sync` — mirror `sd_card_content/` to the SD card

`sync` is the easiest way to prepare or update the SD card after flashing:

1. Scans all files currently on the SD card via `LS`.
2. Deletes files that are not in `sd_card_content/` (using `RM`).
3. Uploads files that are missing or whose size differs (using `PUT`).

`.DS_Store` files are ignored. The default content directory is `<repo>/sd_card_content`; pass a path to override.

## Manual use (serial monitor)

You can type `SD> PING` in a terminal that sends a newline after each line. **Do not** use this for binary `PUT`/`GET` payloads — use the Python script or another tool that can send and receive raw bytes.

## Implementation notes

- Logic lives in `src/sd_serial_xfer.cpp`; `setup()` increases the USB serial RX buffer before USB init to help large uploads.
- The soundboard and MP3 player keep running while commands are processed; avoid heavy transfers during critical use if you notice stutter.
