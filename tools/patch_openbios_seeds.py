#!/usr/bin/env python3
"""Apply the project-local OpenBIOS discovery boundary fix.

The pinned PSXRecomp revision's ELF seed list marks osDbgPrintf's entry but
not the verified first data byte after it. Whole-function discovery can then
follow an unconditional branch into the adjacent data and omit osDbgPrintf.
This patch is deliberately small, checked, and idempotent so a fresh framework
checkout produces the same BIOS translation used by this project.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


ADDRESS = "0xBFC095F8"
PATCH_SEED = {
    "address": ADDRESS,
    "label": "data_after_osDbgPrintf",
    "rationale": (
        "verified exclusive end of osDbgPrintf; prevents unconditional-branch "
        "fallthrough analysis from entering adjacent data"
    ),
}


def parse_args() -> argparse.Namespace:
    project_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--framework",
        type=Path,
        default=project_root / "psxrecomp",
        help="path to the pinned PSXRecomp checkout",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    seed_path = (
        args.framework
        / "recompiler"
        / "seeds"
        / "openbios_elf_seeds.json"
    )
    if not seed_path.is_file():
        raise SystemExit(f"OpenBIOS seed file not found: {seed_path}")

    with seed_path.open("r", encoding="utf-8") as stream:
        document = json.load(stream)

    seeds = document.get("seeds")
    if document.get("schema") != "psxrecomp phase2 seeds" or not isinstance(seeds, list):
        raise SystemExit(f"Unexpected OpenBIOS seed schema: {seed_path}")
    if document.get("seed_count") != len(seeds):
        raise SystemExit(
            "OpenBIOS seed_count does not match the seed array; refusing to patch"
        )

    target = int(ADDRESS, 16)
    matches = [
        seed for seed in seeds
        if int(seed.get("address", "0"), 16) == target
    ]
    if matches:
        if len(matches) != 1 or matches[0] != PATCH_SEED:
            raise SystemExit(f"Conflicting OpenBIOS seed already exists at {ADDRESS}")
        print(f"OpenBIOS boundary seed already present: {ADDRESS}")
        return 0

    if len(seeds) != 646:
        raise SystemExit(
            f"Expected the pinned 646-seed baseline, found {len(seeds)}; refusing to guess"
        )
    dbg_printf = [
        seed for seed in seeds
        if int(seed.get("address", "0"), 16) == 0xBFC091EC
    ]
    if len(dbg_printf) != 1 or dbg_printf[0].get("label") != "osDbgPrintf":
        raise SystemExit("Pinned osDbgPrintf seed was not found at 0xBFC091EC")

    insert_at = next(
        (index for index, seed in enumerate(seeds)
         if int(seed["address"], 16) > int(ADDRESS, 16)),
        len(seeds),
    )
    seeds.insert(insert_at, PATCH_SEED)
    document["seed_count"] = len(seeds)

    with seed_path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(document, stream, indent=2)
        stream.write("\n")

    print(f"Applied OpenBIOS boundary seed: {ADDRESS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
