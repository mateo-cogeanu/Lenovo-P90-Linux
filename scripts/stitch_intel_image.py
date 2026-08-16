#!/usr/bin/env python3
"""Build the Intel Droidboot image format used by Lenovo's P90 release."""

from __future__ import annotations

import argparse
import pathlib
import struct


BLOCK = 4096
HEADER_KERNEL_SIZE = 1024
HEADER_INITRD_SIZE = 1028
HEADER_SPI_SUPPRESS = 1032
HEADER_SPI_TYPE = 1036


def padded_block(data: bytes, name: str) -> bytes:
    if len(data) > BLOCK:
        raise ValueError(f"{name} is {len(data)} bytes; maximum is {BLOCK}")
    return data + bytes(BLOCK - len(data))


def bootstub_block(data: bytes) -> bytes:
    """Match Lenovo stitch.sh: copy exactly the first 4 KiB, padding if short."""
    return (data + bytes(BLOCK))[:BLOCK]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmdline", required=True, type=pathlib.Path)
    parser.add_argument("--bootstub", required=True, type=pathlib.Path)
    parser.add_argument("--kernel", required=True, type=pathlib.Path)
    parser.add_argument("--initrd", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--spi-suppress", type=int, choices=(0, 1), default=0)
    parser.add_argument("--spi-type", type=int, choices=(0, 1, 2), default=0)
    args = parser.parse_args()

    cmdline = args.cmdline.read_bytes()
    bootstub = args.bootstub.read_bytes()
    kernel = args.kernel.read_bytes()
    initrd = args.initrd.read_bytes()

    image = bytearray(padded_block(cmdline, "command line"))
    image.extend(bootstub_block(bootstub))
    image.extend(kernel)
    image.extend(initrd)

    struct.pack_into("<I", image, HEADER_KERNEL_SIZE, len(kernel))
    struct.pack_into("<I", image, HEADER_INITRD_SIZE, len(initrd))
    struct.pack_into("<I", image, HEADER_SPI_SUPPRESS, args.spi_suppress)
    struct.pack_into("<I", image, HEADER_SPI_TYPE, args.spi_type)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(f"wrote {args.output} ({len(image)} bytes)")
    print(f"kernel={len(kernel)} initrd={len(initrd)}")


if __name__ == "__main__":
    main()
