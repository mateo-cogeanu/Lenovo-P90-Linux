#!/usr/bin/env python3
"""Replace only the engineering command line in a signed Intel OSIP image."""

from __future__ import annotations

import argparse
import pathlib
import struct


OSIP_SECTOR = 0x200
SIGNED_HEADER_SIZE = 0x2D8
CMDLINE_CAPACITY = 0x400
SIGNED_ATTRIBUTES = {0x00, 0x0A, 0x0C, 0x0E, 0x12, 0x18}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--append", required=True)
    args = parser.parse_args()

    image = bytearray(args.input.read_bytes())
    if image[:4] != b"$OS$":
        raise SystemExit("not an Intel OSIP image")

    attribute = image[0x34]
    if attribute not in SIGNED_ATTRIBUTES or attribute & 1:
        raise SystemExit(f"OSIP attribute 0x{attribute:02x} is not a signed-image attribute")

    payload_blocks = struct.unpack_from("<I", image, 0x30)[0]
    expected_size = OSIP_SECTOR + payload_blocks * OSIP_SECTOR
    if len(image) != expected_size:
        raise SystemExit(f"OSIP size mismatch: {len(image)} != {expected_size}")

    signed_header = bytes(image[OSIP_SECTOR + 0x80 : OSIP_SECTOR + 0x85])
    if signed_header not in {b"$MOSS", b"$ROSS"}:
        raise SystemExit("signed MOSS/ROSS header not found")

    start = OSIP_SECTOR + SIGNED_HEADER_SIZE
    end = start + CMDLINE_CAPACITY
    old_raw = bytes(image[start:end])
    old = old_raw.split(b"\0", 1)[0].rstrip(b"\r\n")
    addition = args.append.encode("ascii")
    new = old + b" " + addition + b"\n"
    if len(new) + 1 > CMDLINE_CAPACITY:
        raise SystemExit("new command line exceeds the 0x400-byte engineering field")

    image[start:end] = new + bytes(CMDLINE_CAPACITY - len(new))
    args.output.write_bytes(image)

    # The authenticated MOSS/ROSS header and all bytes after the command-line
    # field must remain exactly unchanged.
    original = args.input.read_bytes()
    assert image[:start] == original[:start]
    assert image[end:] == original[end:]

    print(f"attribute=0x{attribute:02x}")
    print(f"signed_header={signed_header.decode('ascii')}")
    print(f"old_cmdline={old.decode('ascii')}")
    print(f"new_cmdline={new.rstrip().decode('ascii')}")
    print(f"changed_range=0x{start:x}-0x{end - 1:x}")


if __name__ == "__main__":
    main()
