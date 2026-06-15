#!/usr/bin/env python3
"""
snapshot_recv.py — receive a JPEG snapshot from the AI Compass over USB-Serial/JTAG.

Protocol (see doc/SNAPSHOT_USAGE.md and main/display/snapshot/snapshot_service.cc):

    ===SCREENSHOT_START===
    <base64 JPEG, ~17 KB>
    ===SCREENSHOT_END===

Baud: 115200, 8N1. The device sends one shot on TakeSnapshot(); main/main.cc schedules
3 shots 2 s apart, 2 s after boot.

Usage:
    snapshot_recv.py -p /dev/cu.usbmodem101 -o screenshots/screenshot.jpg
    snapshot_recv.py -p /dev/cu.usbmodem101            # default out path
    snapshot_recv.py -p /dev/cu.usbmodem101 -n 3        # capture 3 frames
    snapshot_recv.py -p /dev/cu.usbmodem101 --reset     # pulse DTR to reboot device first

Why this exists instead of scripts/save_screenshot.py:
    - argparse + sensible defaults
    - progress dots while waiting
    - recovers from partial reads
    - explicit error messages instead of silent exits
    - writes a sidecar .json with the timestamp + size for regression tracking
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial  # type: ignore
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial", file=sys.stderr)
    sys.exit(2)


START = b"===SCREENSHOT_START==="
END = b"===SCREENSHOT_END==="


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-p", "--port", required=True, help="serial port, e.g. /dev/cu.usbmodem101")
    p.add_argument("-b", "--baud", type=int, default=115200, help="baud rate (default 115200)")
    p.add_argument("-o", "--out", default="screenshots/screenshot.jpg", help="output JPEG path")
    p.add_argument("-n", "--count", type=int, default=1, help="number of snapshots to capture")
    p.add_argument("--timeout", type=float, default=15.0, help="seconds to wait per snapshot")
    p.add_argument("--reset", action="store_true", help="pulse DTR to reboot device before capture")
    p.add_argument("--quiet", action="store_true", help="suppress progress output")
    return p.parse_args()


def log(msg: str, quiet: bool) -> None:
    if not quiet:
        print(msg, file=sys.stderr, flush=True)


def pulse_dtr(ser: "serial.Serial") -> None:
    ser.setDTR(False)
    time.sleep(0.1)
    ser.setDTR(True)
    time.sleep(0.05)
    ser.setDTR(False)


def read_one(ser: "serial.Serial", timeout: float, quiet: bool) -> bytes | None:
    """Read a single screenshot. Returns JPEG bytes or None on timeout."""
    deadline = time.monotonic() + timeout
    buf = bytearray()
    state = "SEEK_START"
    log(f"  waiting for {START.decode()} ...", quiet)

    while time.monotonic() < deadline:
        chunk = ser.read(1024)
        if not chunk:
            continue
        if state == "SEEK_START":
            idx = chunk.find(START)
            if idx >= 0:
                buf.extend(chunk[idx + len(START):])
                state = "READ_BODY"
                log("  start marker found, reading body ...", quiet)
        elif state == "READ_BODY":
            buf.extend(chunk)
            if END in buf:
                end = buf.index(END)
                jpeg = bytes(buf[:end]).strip()
                log(f"  end marker found, jpeg={len(jpeg)} bytes", quiet)
                try:
                    return base64.b64decode(jpeg, validate=False)
                except Exception as e:
                    log(f"  base64 decode failed: {e}", quiet)
                    return None
    log("  TIMEOUT", quiet)
    return None


def main() -> int:
    args = parse_args()
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.5)
    except serial.SerialException as e:
        print(f"ERROR: cannot open {args.port}: {e}", file=sys.stderr)
        return 1

    try:
        if args.reset:
            log("pulsing DTR to reboot device ...", args.quiet)
            pulse_dtr(ser)
            time.sleep(3.0)  # let boot + snapshot task start
        else:
            ser.reset_input_buffer()

        successes = 0
        for i in range(1, args.count + 1):
            log(f"[{i}/{args.count}] capture ...", args.quiet)
            jpeg = read_one(ser, args.timeout, args.quiet)
            if jpeg is None:
                log(f"  FAILED on attempt {i}", args.quiet)
                continue

            # If capturing multiple, suffix the path
            target = out_path
            if args.count > 1:
                stem, suf = out_path.stem, out_path.suffix
                target = out_path.with_name(f"{stem}_{i:03d}{suf}")

            target.write_bytes(jpeg)

            meta = {
                "path": str(target),
                "size_bytes": len(jpeg),
                "captured_at": datetime.now().isoformat(timespec="seconds"),
                "port": args.port,
                "baud": args.baud,
            }
            target.with_suffix(".json").write_text(json.dumps(meta, indent=2))
            log(f"  saved {target} ({len(jpeg)} bytes) + {target.with_suffix('.json')}", args.quiet)
            successes += 1

        log(f"done: {successes}/{args.count} captured", args.quiet)
        return 0 if successes > 0 else 3
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
