#!/usr/bin/env python3
"""Generate conservative function seeds from direct MIPS JAL targets."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "exe", nargs="?", type=Path, default=Path("input/SLUS_002.24.code")
    )
    parser.add_argument("--output", type=Path, default=Path("seeds/functions.txt"))
    args = parser.parse_args()

    data = args.exe.read_bytes()
    if len(data) < 0x800 or data[:8] != b"PS-X EXE":
        raise SystemExit(f"error: {args.exe} is not a PS-X EXE")

    entry = u32(data, 0x10)
    load = u32(data, 0x18)
    text_size = u32(data, 0x1C)
    payload = data[0x800:0x800 + text_size]
    end = load + len(payload)

    seeds = {entry}
    for offset in range(0, len(payload) - 3, 4):
        word = u32(payload, offset)
        if (word >> 26) != 0x03:  # JAL
            continue
        pc = load + offset
        target = (pc & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
        if load <= target < end and (target & 3) == 0:
            seeds.add(target)

    digest = hashlib.sha256(data).hexdigest()
    lines = [
        "# Auto-generated MIPS JAL targets for Disruptor SLUS-00224.",
        f"# Source SHA-256: {digest}",
        f"# Static range: 0x{load:08X}-0x{end:08X}",
        "",
        *(f"0x{address:08X}" for address in sorted(seeds)),
        "",
    ]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="ascii", newline="\n")
    print(f"Wrote {len(seeds)} seeds to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
