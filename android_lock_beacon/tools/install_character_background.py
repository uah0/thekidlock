#!/usr/bin/env python3
"""Install the approved character background as a normal Android drawable.

Usage from repository root:
    python android_lock_beacon/tools/install_character_background.py /path/to/ble_lock_home_background2.jpg

The final repository resource is a normal binary JPEG under drawable-nodpi.
There is no Base64 runtime decoding mechanism.
"""

from __future__ import annotations

import hashlib
import shutil
import struct
import sys
from pathlib import Path

EXPECTED_BYTES = 812_030
EXPECTED_SHA256 = "9a7fdb22aff298d1c562c297d9f085daae99999dc898cd373880890f1f44d9e1"
EXPECTED_SIZE = (1080, 2400)
DESTINATION = Path(__file__).resolve().parents[1] / "app/src/main/res/drawable-nodpi/home_background.jpg"


def jpeg_size(data: bytes) -> tuple[int, int]:
    if len(data) < 4 or data[:2] != b"\xff\xd8" or data[-2:] != b"\xff\xd9":
        raise ValueError("file is not a complete JPEG (SOI/EOI missing)")

    index = 2
    while index + 4 <= len(data):
        if data[index] != 0xFF:
            index += 1
            continue
        while index < len(data) and data[index] == 0xFF:
            index += 1
        if index >= len(data):
            break
        marker = data[index]
        index += 1
        if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:
            continue
        if index + 2 > len(data):
            break
        segment_length = struct.unpack(">H", data[index:index + 2])[0]
        if segment_length < 2 or index + segment_length > len(data):
            raise ValueError("truncated JPEG segment")
        if marker in {
            0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
            0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF,
        }:
            if segment_length < 7:
                raise ValueError("invalid JPEG SOF segment")
            height = struct.unpack(">H", data[index + 3:index + 5])[0]
            width = struct.unpack(">H", data[index + 5:index + 7])[0]
            return width, height
        index += segment_length
    raise ValueError("JPEG dimensions not found")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: install_character_background.py <approved-background.jpg>", file=sys.stderr)
        return 2

    source = Path(sys.argv[1]).expanduser().resolve()
    if not source.is_file():
        print(f"source not found: {source}", file=sys.stderr)
        return 2

    data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    size = jpeg_size(data)

    if len(data) != EXPECTED_BYTES:
        raise SystemExit(f"unexpected byte length: {len(data)} != {EXPECTED_BYTES}")
    if digest != EXPECTED_SHA256:
        raise SystemExit(f"unexpected SHA-256: {digest}")
    if size != EXPECTED_SIZE:
        raise SystemExit(f"unexpected dimensions: {size} != {EXPECTED_SIZE}")

    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, DESTINATION)

    copied = DESTINATION.read_bytes()
    if hashlib.sha256(copied).hexdigest() != EXPECTED_SHA256:
        raise SystemExit("copy verification failed")

    print(f"installed: {DESTINATION}")
    print(f"bytes: {len(copied)}")
    print(f"size: {size[0]}x{size[1]}")
    print(f"sha256: {EXPECTED_SHA256}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
