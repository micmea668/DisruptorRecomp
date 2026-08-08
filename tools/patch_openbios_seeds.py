#!/usr/bin/env python3
"""Apply the project-local OpenBIOS framework fixes.

The pinned PSXRecomp revision's ELF seed list marks osDbgPrintf's entry but
not the verified first data byte after it. Whole-function discovery can then
follow an unconditional branch into the adjacent data and omit osDbgPrintf.

The same revision also emits a const-qualified object as a file-scope C
initializer. That is not a C constant expression and MSVC rejects the generated
OpenBIOS dispatch table. Emit the count as an enum constant instead.

Both patches are deliberately small, checked, and idempotent so a fresh
framework checkout produces the same BIOS translation used by this project.
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
EMITTER_OLD = (
    '        out += fmt::format("static const uint32_t '
    '{}psx_bios_kernel_body_count = {}u;\\n\\n",\n'
    '                           g_sym_prefix, kb_count);'
)
EMITTER_NEW = (
    '        out += fmt::format("enum {{ '
    '{}psx_bios_kernel_body_count = {}u }};\\n\\n",\n'
    '                           g_sym_prefix, kb_count);'
)


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


def prepare_emitter_patch(framework: Path):
    emitter_path = framework / "recompiler" / "src" / "full_function_emitter.cpp"
    if not emitter_path.is_file():
        raise SystemExit(f"OpenBIOS emitter source not found: {emitter_path}")

    source = emitter_path.read_text(encoding="utf-8")
    old_count = source.count(EMITTER_OLD)
    new_count = source.count(EMITTER_NEW)
    if old_count == 0 and new_count == 1:
        return emitter_path, None
    if old_count != 1 or new_count != 0:
        raise SystemExit(
            "Unexpected OpenBIOS kernel-body count emitter; refusing to patch"
        )

    return emitter_path, source.replace(EMITTER_OLD, EMITTER_NEW)


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
        seed_update = False
    else:
        if len(seeds) != 646:
            raise SystemExit(
                f"Expected the pinned 646-seed baseline, found {len(seeds)}; "
                "refusing to guess"
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
        seed_update = True

    # Validate both pinned inputs before writing either one. A layout mismatch
    # must never leave a fresh checkout with only half of this patch applied.
    emitter_path, emitter_update = prepare_emitter_patch(args.framework)

    if seed_update:
        with seed_path.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(document, stream, indent=2)
            stream.write("\n")
        print(f"Applied OpenBIOS boundary seed: {ADDRESS}")
    else:
        print(f"OpenBIOS boundary seed already present: {ADDRESS}")

    if emitter_update is not None:
        with emitter_path.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(emitter_update)
        print("Applied OpenBIOS constant-expression emitter fix")
    else:
        print("OpenBIOS constant-expression emitter fix already present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
