#!/usr/bin/env python3
"""Validate and describe the supported Disruptor PS-X executable."""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path


EXPECTED = {
    "sha256": "48e8c3143b7f5de10340c9d4a9bac8cb7e97c15eda7a0897d3cf337ad96cb2c4",
    "size": 0x62000,
    "entry_pc": 0x80048CE4,
    "load_address": 0x80010000,
    "text_size": 0x61800,
    "stack_base": 0x801FFFF0,
}


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def inspect(path: Path) -> list[str]:
    data = path.read_bytes()
    if len(data) < 0x800 or data[:8] != b"PS-X EXE":
        raise ValueError(f"{path} is not a PS-X EXE")

    actual = {
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
        "entry_pc": u32(data, 0x10),
        "load_address": u32(data, 0x18),
        "text_size": u32(data, 0x1C),
        "stack_base": u32(data, 0x30),
    }
    errors = [
        f"{name}: expected {expected!r}, got {actual[name]!r}"
        for name, expected in EXPECTED.items()
        if actual[name] != expected
    ]

    region = data[0x4C:0x800].split(b"\0", 1)[0].decode("ascii", "replace")
    print(f"File:         {path}")
    print(f"SHA-256:      {actual['sha256']}")
    print(f"Size:         {actual['size']} bytes (0x{actual['size']:X})")
    print(f"Entry PC:     0x{actual['entry_pc']:08X}")
    print(f"Load address: 0x{actual['load_address']:08X}")
    print(f"Text size:    0x{actual['text_size']:X}")
    print(f"Text end:     0x{actual['load_address'] + actual['text_size']:08X}")
    print(f"Stack base:   0x{actual['stack_base']:08X}")
    print(f"Region:       {region}")
    return errors


def validate_system_cnf(path: Path) -> list[str]:
    text = path.read_text(encoding="ascii", errors="replace").replace("\r", "")
    required = {
        "BOOT = cdrom:\\slus_002.24;1",
        "STACK = 801FFFF0",
    }
    missing = sorted(line for line in required if line.lower() not in text.lower())
    if missing:
        return [f"SYSTEM.CNF is missing expected line: {line}" for line in missing]
    print(f"SYSTEM.CNF:   {path} (matches SLUS-00224)")
    return []


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", nargs="?", type=Path, default=Path("input/SLUS_002.24"))
    parser.add_argument("--system-cnf", type=Path)
    args = parser.parse_args()

    try:
        errors = inspect(args.exe)
        if args.system_cnf:
            errors.extend(validate_system_cnf(args.system_cnf))
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    if errors:
        print("\nUnsupported or modified executable:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("Validation:   supported executable")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
