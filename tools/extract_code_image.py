#!/usr/bin/env python3
"""Create the local code-only PS-X EXE consumed by the recompiler.

The retail executable places non-code tables/strings before and after one
contiguous resident-code range. Feeding those tables to whole-image discovery
creates false functions. The runtime still boots the untouched retail EXE from
the user's disc; this derived image only defines the bytes translated to C and
the range guarded for native dispatch.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


SOURCE_SHA256 = "48e8c3143b7f5de10340c9d4a9bac8cb7e97c15eda7a0897d3cf337ad96cb2c4"
CODE_START = 0x80011200
CODE_END = 0x80056938


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", nargs="?", type=Path, default=Path("input/SLUS_002.24"))
    parser.add_argument(
        "output", nargs="?", type=Path, default=Path("input/SLUS_002.24.code")
    )
    args = parser.parse_args()

    source = args.source.read_bytes()
    digest = hashlib.sha256(source).hexdigest()
    if digest != SOURCE_SHA256:
        raise SystemExit(
            "error: unsupported source executable\n"
            f"expected SHA-256 {SOURCE_SHA256}\n"
            f"actual   SHA-256 {digest}"
        )
    if source[:8] != b"PS-X EXE":
        raise SystemExit("error: source is not a PS-X EXE")

    original_load = u32(source, 0x18)
    original_size = u32(source, 0x1C)
    original_end = original_load + original_size
    if not (original_load <= CODE_START < CODE_END <= original_end):
        raise SystemExit("error: configured code range is outside the source image")

    begin = 0x800 + (CODE_START - original_load)
    end = 0x800 + (CODE_END - original_load)
    header = bytearray(source[:0x800])
    struct.pack_into("<I", header, 0x18, CODE_START)
    struct.pack_into("<I", header, 0x1C, CODE_END - CODE_START)
    derived = bytes(header) + source[begin:end]

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(derived)
    print(
        f"Wrote code image {args.output}: "
        f"0x{CODE_START:08X}-0x{CODE_END:08X} "
        f"({CODE_END - CODE_START} bytes, SHA-256 "
        f"{hashlib.sha256(derived).hexdigest()})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
