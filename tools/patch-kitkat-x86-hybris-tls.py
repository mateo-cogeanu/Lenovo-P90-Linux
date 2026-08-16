#!/usr/bin/env python3
"""Patch the exact Lenovo P90 KitKat x86 libraries for a staged libhybris test.

This never modifies files on Android's /system partition.  It accepts pulled
copies with known SHA-256 sums, checks every expected instruction byte, writes
new files, and prints their hashes.  The slot mapping is specific to the
staged Debian i386 glibc runtime after a live read-only TCB inventory showed
that slots 0--6 are occupied:

    errno: 2 -> 7, OpenGL API: 3 -> 8

The Android libraries first load the thread pointer from %gs:0 and then use a
byte displacement.  Moving these displacements avoids overwriting glibc's
low x86 thread-control-block fields.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


KNOWN = {
    "libc": {
        "sha256": "53b8b23f11a45ea4dadd19e8b7096955b8543c9000fe52739ade2f1acfd5d542",
        "patches": {
            # __libc_preinit: TLS_SLOT_BIONIC_PREINIT aliases OPENGL_API.
            0x1BA0F: (b"\x0c", b"\x20"),
            0x1BA12: (b"\x0c", b"\x20"),
            # __errno: return thread_pointer + 4 * TLS_SLOT_ERRNO.
            0x2C28B: (b"\x08", b"\x1c"),
        },
    },
    "libEGL": {
        "sha256": "057e6a74688d5a4fc5090a5652db7659ea428be6fa3afc6b82f71d751f816f66",
        "patches": {
            # All are stores to thread_pointer[TLS_SLOT_OPENGL_API].
            0xD723: (b"\x0c", b"\x20"),
            0xD790: (b"\x0c", b"\x20"),
            0xD7D8: (b"\x0c", b"\x20"),
            0xD800: (b"\x0c", b"\x20"),
            0xDB67: (b"\x0c", b"\x20"),
            0xDB90: (b"\x0c", b"\x20"),
            0xDBC0: (b"\x0c", b"\x20"),
            0xDBF0: (b"\x0c", b"\x20"),
            0xDC1E: (b"\x0c", b"\x20"),
        },
    },
    "libGLESv2": {
        "sha256": "29818fa50f6bd6e552aacfa9b9ac27558261d8af824e0409a019069c520f3813",
        "scan_gl_api": 366,
        "patches": {},
    },
    "libGLESv1_CM": {
        "sha256": "374ce8a6ca59e36ca4cb5c5dc2794778c070946db2d0dc59139215d7fc4828a1",
        "scan_gl_api": 292,
        "patches": {},
    },
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def patch(kind: str, source: Path, output: Path) -> None:
    spec = KNOWN[kind]
    data = bytearray(source.read_bytes())
    actual = digest(data)
    if actual != spec["sha256"]:
        raise SystemExit(f"refusing unknown {kind}: expected {spec['sha256']}, got {actual}")

    patches = dict(spec["patches"])

    # The generated GLES entry-point libraries contain hundreds of identical
    # dispatch stubs.  Each loads the thread pointer from %gs:0 and, within the
    # next few instructions, loads TLS_SLOT_OPENGL_API at byte offset 0x0c.
    # Discover them structurally, but require the exact known file hash and
    # exact expected count before changing anything.
    expected_scan_count = spec.get("scan_gl_api")
    if expected_scan_count is not None:
        gs_load = b"\x65\xa1\x00\x00\x00\x00"
        scanned = []
        for start in range(len(data)):
            if not data.startswith(gs_load, start):
                continue
            for cursor in range(start + len(gs_load), min(start + 18, len(data) - 2)):
                # mov r32, [eax + disp8]
                if (
                    data[cursor] == 0x8B
                    and data[cursor + 2] == 0x0C
                    and data[cursor + 1] & 0xC7 == 0x40
                ):
                    scanned.append(cursor + 2)
                    break
        if len(scanned) != expected_scan_count:
            raise SystemExit(
                f"refusing {kind}: expected {expected_scan_count} GL stubs, got {len(scanned)}"
            )
        patches.update({offset: (b"\x0c", b"\x20") for offset in scanned})

    for offset, (old, new) in patches.items():
        found = bytes(data[offset : offset + len(old)])
        if found != old:
            raise SystemExit(
                f"refusing {kind}: offset 0x{offset:x} expected {old.hex()}, got {found.hex()}"
            )
        data[offset : offset + len(old)] = new

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    print(f"{kind}: {len(patches)} checked patches")
    print(f"input  sha256={actual}")
    print(f"output sha256={digest(data)}")
    print(f"output={output}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=KNOWN)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    patch(args.kind, args.source, args.output)


if __name__ == "__main__":
    main()
