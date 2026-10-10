#!/usr/bin/env python3
"""Replay a capsule the game recorded, without a window or sound, and say whether it stayed in step.

A capsule is a folder under `capsules/` beside the game: a save state, the input of every
frame after it, the settings and the build it was made with. Ctrl+F12 in the game starts a
recording and ends it.

The game's exit code is passed on: 0 when the replay stayed in step with the recording,
3 when it left it, 2 when the capsule could not be played.
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def build_directory() -> Path:
    return ROOT / os.environ.get("DISRUPTOR_BUILD_DIRECTORY", "build")


def find_game(build: Path) -> Path:
    for candidate in (
        build / "DisruptorRecompiled.exe",
        build / "Release" / "DisruptorRecompiled.exe",
        build / "DisruptorRecompiled",
    ):
        if candidate.is_file():
            return candidate
    raise SystemExit(f"No game executable in {build}. Build first, or pass --game.")


def bitmap_to_png(source: Path) -> None:
    data = source.read_bytes()
    (start,) = struct.unpack_from("<I", data, 10)
    width, height = struct.unpack_from("<ii", data, 18)
    row = (width * 3 + 3) & ~3
    lines = bytearray()
    for y in range(height - 1, -1, -1):
        line = data[start + y * row : start + y * row + width * 3]
        lines.append(0)
        for at in range(0, len(line), 3):
            lines += bytes((line[at + 2], line[at + 1], line[at]))

    def chunk(tag: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(bytes(lines)))
        + chunk(b"IEND", b"")
    )
    source.with_suffix(".png").write_bytes(png)
    source.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("capsule", type=Path, help="the capsule's folder")
    parser.add_argument("--pictures", type=Path, help="write every replayed frame here as a PNG, with memory.txt")
    parser.add_argument("--game", type=Path, help="the game executable (default: the build directory's)")
    parser.add_argument(
        "--disc", type=Path, default=ROOT / "input" / "Disruptor (USA).cue", help="the US disc's cue sheet"
    )
    args = parser.parse_args()

    capsule = args.capsule.resolve()
    if not (capsule / "input.bin").is_file():
        parser.error(f"not a capsule: {capsule}")
    game = (args.game or find_game(build_directory())).resolve()
    theirs, ours = capsule / "settings.toml", game.parent / "settings.toml"
    if theirs.is_file() and (not ours.is_file() or theirs.read_bytes() != ours.read_bytes()):
        print(
            f"note: {ours} is not the capsule's settings.toml. Copy the capsule's there if the replay leaves the recording."
        )

    environment = dict(os.environ)
    environment["PSX_DISRUPTOR_CAPSULE_REPLAY"] = str(capsule)
    for name in ("PSX_LOAD_SLOT", "PSX_DISRUPTOR_CAPSULE_RECORD", "PSX_DISRUPTOR_CAPSULE_FRAMES"):
        environment.pop(name, None)
    if args.pictures:
        args.pictures.mkdir(parents=True, exist_ok=True)
        environment["PSX_DISRUPTOR_CAPSULE_FRAMES"] = str(args.pictures.resolve())
    command = [str(game), "--headless", "--no-launcher", "--game", "game.toml", "--disc", str(args.disc.resolve())]
    finished = subprocess.run(
        command,
        cwd=game.parent,
        env=environment,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        errors="replace",
    )
    said = [line for line in finished.stderr.splitlines() if "capsule:" in line]
    print("\n".join(said) if said else f"The game said nothing of the capsule. Its exit code is {finished.returncode}.")
    if args.pictures:
        for picture in sorted(args.pictures.glob("*.bmp")):
            bitmap_to_png(picture)
    return finished.returncode


if __name__ == "__main__":
    sys.exit(main())
