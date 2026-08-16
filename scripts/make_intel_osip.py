#!/usr/bin/env python3
"""Wrap an Intel MID OS payload in Lenovo-compatible unsigned OSIP metadata."""

import argparse
import pathlib
import struct

OSIP_SIZE = 0x200
HEADER_SIZE = 0x38
OSII_OFFSET = 0x20


def integer(value: str) -> int:
    return int(value, 0)


def make_header(payload_size: int, load: int, entry: int, attribute: int) -> bytes:
    blocks = (payload_size + 0x1FF) // 0x200
    header = bytearray(b"\xff" * OSIP_SIZE)
    struct.pack_into(
        "<4sBBBBBBH",
        header,
        0,
        b"$OS$",
        0,
        0,
        1,
        0,
        1,
        1,
        HEADER_SIZE,
    )
    header[0x0C:OSII_OFFSET] = bytes(OSII_OFFSET - 0x0C)
    struct.pack_into(
        "<HHIIIIB",
        header,
        OSII_OFFSET,
        0,
        0,
        1,
        load,
        entry,
        blocks,
        attribute,
    )
    header[0x35:HEADER_SIZE] = bytes(HEADER_SIZE - 0x35)

    # Lenovo's S149 images use an otherwise empty MBR area plus 0x55aa.
    header[0x1B8:0x1FE] = bytes(0x1FE - 0x1B8)
    struct.pack_into("<H", header, 0x1FE, 0xAA55)

    checksum = 0
    for byte in header[:HEADER_SIZE]:
        checksum ^= byte
    header[7] = checksum
    return bytes(header)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--load-address", required=True, type=integer)
    parser.add_argument("--entry-point", required=True, type=integer)
    parser.add_argument("--attribute", required=True, type=integer)
    args = parser.parse_args()

    payload = args.input.read_bytes()
    padding = (-len(payload)) % OSIP_SIZE
    args.output.write_bytes(
        make_header(len(payload) + padding, args.load_address, args.entry_point, args.attribute)
        + payload
        + b"\xff" * padding
    )


if __name__ == "__main__":
    main()
