#!/usr/bin/env python3
"""Host helper for Cardputer SD serial file transfer (see src/sd_serial_xfer.cpp).

Requires: pip install pyserial

Examples:
  python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 ls /boards/meme
  python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 get /boards/meme/a.mp3 ./a.mp3
  python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 put ./local.mp3 /mp3/local.mp3
  python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 rm /mp3/local.mp3
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    print("Install pyserial: pip install pyserial", file=sys.stderr)
    sys.exit(1)

PREFIX = "SD>"
READ_TIMEOUT = 300
CHUNK_SIZE = 4096


def _fmt_size(n: int) -> str:
    if n < 1024:
        return f"{n} B"
    if n < 1_048_576:
        return f"{n / 1024:.1f} KB"
    return f"{n / 1_048_576:.1f} MB"


def _print_bar(done: int, total: int, width: int = 30) -> None:
    frac = done / total if total else 1.0
    filled = int(width * frac)
    bar = "█" * filled + "░" * (width - filled)
    print(
        f"\r  [{bar}] {int(frac * 100):3d}%  {_fmt_size(done)} / {_fmt_size(total)}   ",
        end="",
        flush=True,
    )


def read_line(ser: serial.Serial) -> str:
    buf = bytearray()
    while True:
        b = ser.read(1)
        if not b:
            raise TimeoutError("serial read timeout")
        if b == b"\n":
            return buf.decode("utf-8", errors="replace").rstrip("\r")
        buf.extend(b)


def cmd_line(ser: serial.Serial, line: str) -> None:
    ser.write((line + "\n").encode("utf-8"))
    ser.flush()


def do_ls(ser: serial.Serial, path: str) -> None:
    cmd_line(ser, f"{PREFIX} LS {path}")
    while True:
        ln = read_line(ser)
        if ln.startswith("-ERR"):
            print(ln, file=sys.stderr)
            sys.exit(1)
        if ln == "+LIST":
            break
    while True:
        ln = read_line(ser)
        if ln == ".":
            break
        print(ln)


def do_stat(ser: serial.Serial, path: str) -> None:
    cmd_line(ser, f"{PREFIX} STAT {path}")
    ln = read_line(ser)
    if ln.startswith("-ERR"):
        print(ln, file=sys.stderr)
        sys.exit(1)
    print(ln)


def do_get(ser: serial.Serial, remote: str, local: str) -> None:
    cmd_line(ser, f"{PREFIX} GET {remote}")
    ln = read_line(ser)
    if ln.startswith("-ERR"):
        print(ln, file=sys.stderr)
        sys.exit(1)
    if not ln.startswith("+BIN "):
        print("unexpected:", ln, file=sys.stderr)
        sys.exit(1)
    size = int(ln.split()[1])
    with open(local, "wb") as f:
        left = size
        while left > 0:
            chunk = ser.read(min(left, 65536))
            if not chunk:
                raise TimeoutError("short read on GET body")
            f.write(chunk)
            left -= len(chunk)
    print("+OK saved", local, size, "bytes")


def do_put(ser: serial.Serial, local: str, remote: str) -> None:
    with open(local, "rb") as f:
        data = f.read()
    size = len(data)
    cmd_line(ser, f"{PREFIX} PUT {remote} {size}")
    ln = read_line(ser)
    if ln.startswith("-ERR"):
        print(ln, file=sys.stderr)
        sys.exit(1)
    if ln != "+GO":
        print("expected +GO, got:", ln, file=sys.stderr)
        sys.exit(1)
    sent = 0
    _print_bar(0, size)
    while sent < size:
        chunk = data[sent : sent + CHUNK_SIZE]
        ser.write(chunk)
        ser.flush()
        sent += len(chunk)
        _print_bar(sent, size)
    print(flush=True)  # newline after progress bar
    ln = read_line(ser)
    if ln != "+OK":
        print(ln, file=sys.stderr)
        sys.exit(1)


def do_rm(ser: serial.Serial, path: str) -> None:
    cmd_line(ser, f"{PREFIX} RM {path}")
    ln = read_line(ser)
    print(ln)
    if ln.startswith("-ERR"):
        sys.exit(1)


def do_mkdir(ser: serial.Serial, path: str) -> None:
    cmd_line(ser, f"{PREFIX} MKDIR {path}")
    ln = read_line(ser)
    print(ln)
    if ln.startswith("-ERR"):
        sys.exit(1)


def list_dir_entries(ser: serial.Serial, path: str) -> list[tuple[bool, str, int]]:
    """Return list of (is_directory, full_path, size). Size is 0 for directories."""
    cmd_line(ser, f"{PREFIX} LS {path}")
    while True:
        ln = read_line(ser)
        if ln.startswith("-ERR"):
            raise OSError(ln)
        if ln == "+LIST":
            break
    out: list[tuple[bool, str, int]] = []
    while True:
        ln = read_line(ser)
        if ln == ".":
            break
        parts = ln.split("\t")
        if len(parts) < 2:
            continue
        kind, full = parts[0], parts[1]
        if kind == "D":
            out.append((True, full, 0))
        elif kind == "F" and len(parts) >= 3:
            out.append((False, full, int(parts[2])))
    return out


def remote_all_files(ser: serial.Serial) -> dict[str, int]:
    """Map absolute remote file path -> size in bytes."""
    files: dict[str, int] = {}
    dirs: list[str] = ["/"]
    while dirs:
        d = dirs.pop()
        try:
            for is_dir, p, sz in list_dir_entries(ser, d):
                if is_dir:
                    dirs.append(p)
                else:
                    files[p] = sz
        except OSError as e:
            print("skip dir", d, e, file=sys.stderr)
    return files


def local_manifest(content_root: Path) -> dict[str, tuple[Path, int]]:
    """Map remote absolute path -> (local Path, size). Skips .DS_Store."""
    desired: dict[str, tuple[Path, int]] = {}
    for p in content_root.rglob("*"):
        if not p.is_file() or p.name == ".DS_Store":
            continue
        rel = p.relative_to(content_root).as_posix()
        remote = "/" + rel
        desired[remote] = (p, p.stat().st_size)
    return desired


def stat_remote_size(ser: serial.Serial, path: str) -> int | None:
    cmd_line(ser, f"{PREFIX} STAT {path}")
    ln = read_line(ser)
    if ln.startswith("-ERR"):
        return None
    if ln.startswith("+STAT "):
        return int(ln.split()[1])
    return None


def rm_remote(ser: serial.Serial, path: str) -> bool:
    cmd_line(ser, f"{PREFIX} RM {path}")
    ln = read_line(ser)
    ok = ln == "+OK"
    if not ok:
        print(ln, file=sys.stderr)
    return ok


def do_sync(ser: serial.Serial, content_root: Path) -> None:
    """Remove remote files not in content_root tree, then PUT missing or size-mismatched files."""
    content_root = content_root.resolve()
    if not content_root.is_dir():
        print("not a directory:", content_root, file=sys.stderr)
        sys.exit(1)

    desired = local_manifest(content_root)

    print("Scanning SD card...", end="", flush=True)
    remote = remote_all_files(ser)
    print(f" {len(remote)} file(s) found.")

    extra = sorted(set(remote) - set(desired))
    if extra:
        print(f"Removing {len(extra)} extra file(s):")
        for i, rpath in enumerate(extra, 1):
            print(f"  [{i}/{len(extra)}] RM {rpath}")
            if not rm_remote(ser, rpath):
                sys.exit(1)

    desired_list = sorted(desired.items())
    n_check = len(desired_list)
    w = len(str(n_check))
    to_put: list[tuple[Path, str, int]] = []
    for idx, (rpath, (lp, want_sz)) in enumerate(desired_list, 1):
        display = rpath if len(rpath) <= 55 else "..." + rpath[-52:]
        print(f"\rChecking {idx:{w}d}/{n_check}  {display:<55}", end="", flush=True)
        got = stat_remote_size(ser, rpath)
        if got != want_sz:
            to_put.append((lp, rpath, want_sz))
    print()  # newline after checking phase

    n = len(to_put)
    if n == 0:
        print("All files up to date.")
        print("+OK sync complete")
        return

    total_bytes = sum(sz for _, _, sz in to_put)
    nw = len(str(n))
    print(f"Uploading {n} file(s)  ({_fmt_size(total_bytes)} total):")
    for i, (lp, rpath, want_sz) in enumerate(to_put, 1):
        print(f"  [{i:{nw}d}/{n}] {rpath}  ({_fmt_size(want_sz)})")
        do_put(ser, str(lp), rpath)

    print("+OK sync complete")


def main() -> None:
    ap = argparse.ArgumentParser(description="SD card file ops over Cardputer USB serial")
    ap.add_argument("-p", "--port", required=True, help="Serial device e.g. /dev/tty.usbmodem201101")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    sub = ap.add_subparsers(dest="action", required=True)

    p_ls = sub.add_parser("ls", help="List directory")
    p_ls.add_argument("path", help="Absolute path e.g. /boards/meme")

    p_stat = sub.add_parser("stat", help="File size")
    p_stat.add_argument("path")

    p_get = sub.add_parser("get", help="Download file from SD")
    p_get.add_argument("remote")
    p_get.add_argument("local")

    p_put = sub.add_parser("put", help="Upload file to SD")
    p_put.add_argument("local")
    p_put.add_argument("remote")

    p_rm = sub.add_parser("rm", help="Delete file on SD")
    p_rm.add_argument("path")

    p_mk = sub.add_parser("mkdir", help="Create directory (and parents)")
    p_mk.add_argument("path")

    p_sync = sub.add_parser(
        "sync",
        help="Mirror repo sd_card_content to SD: delete extra remote files, upload missing/changed",
    )
    p_sync.add_argument(
        "content_dir",
        nargs="?",
        default=None,
        help="Local folder to mirror (default: <repo>/sd_card_content)",
    )

    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=READ_TIMEOUT)
    time.sleep(0.15)
    ser.reset_input_buffer()

    if args.action == "ls":
        do_ls(ser, args.path)
    elif args.action == "stat":
        do_stat(ser, args.path)
    elif args.action == "get":
        do_get(ser, args.remote, args.local)
    elif args.action == "put":
        do_put(ser, args.local, args.remote)
    elif args.action == "rm":
        do_rm(ser, args.path)
    elif args.action == "mkdir":
        do_mkdir(ser, args.path)
    elif args.action == "sync":
        repo_sd = Path(__file__).resolve().parent.parent / "sd_card_content"
        root = Path(args.content_dir) if args.content_dir else repo_sd
        do_sync(ser, root)


if __name__ == "__main__":
    main()
