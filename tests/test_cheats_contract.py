#!/usr/bin/env python3
"""Version and codegen contract for Disruptor's reviewed gameplay cheats."""

from __future__ import annotations

import re
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
source = read("src/disruptor_cheats.cpp")
header = read("src/disruptor_cheats.h")
game = read("game.toml")
wide = read("game-widescreen.toml")

require("disruptor_cheats_test.cpp" in cmake and
        "add_test(NAME disruptor_cheats" in cmake,
        "behavioral cheat test must remain registered")
for config, name in ((game, "game.toml"), (wide, "game-widescreen.toml")):
    entry_match = re.search(
        r"mod_function_entry_funcs\s*=\s*\[([^\]]*)\]", config)
    require(entry_match is not None and
            '"0x80020DD8"' in entry_match.group(1),
            f"{name} must retain the audited damage-function hook")

for token in (
    "DISRUPTOR_CHEAT_GAME_NOT_READY",
    "DISRUPTOR_CHEAT_NETPLAY_BLOCKED",
    "DISRUPTOR_CHEAT_UNVERIFIED_STATE",
    "disruptor_cheats_reset_session",
):
    require(token in header and token in source,
            f"cheat API is missing fail-closed token {token}")

for address in (
    "0x80020DD8u", "0x80077660u", "0x80077664u",
    "0x80056A90u", "0x80056AB0u",
    "0x800770FCu", "0x80077100u", "0x80077104u", "0x80077108u",
    "0x8007710Cu", "0x80077110u", "0x80077114u", "0x80077118u",
    "0x80077B5Cu", "0x80077B60u", "0x80077B64u",
    "0x80077B68u", "0x80077B6Cu",
    "0x80077627u", "0x80077629u", "0x80077668u", "0x8007766Cu",
    "0x80071690u",
):
    require(address in source, f"reviewed cheat identity missing: {address}")

require("kExpectedMaximumAmmo" in source and
        "0u, 200u, 100u, 50u, 50u, 10u, 100u, 1u" in source,
        "All Weapons must validate and copy the retail resource maxima")
require(source.index("maximum_ammo_table_matches()") <
        source.index("psx_mod_write_word(kLiveAmmoBase"),
        "All Weapons must validate the executable state before its first write")
require("psx_mod_write_byte(kCheatedMarker, 1u)" in source,
        "retail save/endgame cheat consequence must not be bypassed")

# Private retail input is intentionally absent from clean source checkouts.
# When present, pin the exact instructions that justify the host contract.
image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()

    def word(pc: int) -> int:
        offset = ROM_TEXT_OFFSET + pc - LOAD_ADDRESS
        require(0 <= offset <= len(image) - 4,
                f"retail instruction outside image: 0x{pc:08X}")
        return struct.unpack_from("<I", image, offset)[0]

    expected = {
        0x80020DD8: 0x27BDFFE8,  # central damage function entry
        0x80020E44: 0x8C427660,  # load current health
        0x80020E4C: 0x00501023,  # subtract $s0 damage
        0x80020E54: 0xAC227660,  # store current health
        0x8002152C: 0x0C008376,  # player/environment damage call
        0x8002315C: 0x0C008376,  # collision damage call
        0x800353D0: 0x0C008376,  # stage/hazard damage call
        0x8004153C: 0xAC2370FC,
        0x80041544: 0xAC237100,
        0x8004154C: 0xAC237104,
        0x80041554: 0xAC237108,
        0x8004155C: 0xAC23710C,
        0x80041564: 0xAC237110,
        0x8004156C: 0xAC237114,
        0x80041574: 0xAC237118,
        0x800415B0: 0xA3820544,  # gp+0x544 cheated marker
    }
    for pc, instruction in expected.items():
        require(word(pc) == instruction,
                f"retail cheat identity changed at 0x{pc:08X}")

generated = list((ROOT / "generated").glob("SLUS_002.24.code_full_*.c"))
if generated:
    hook = "psx_mod_function_entry(cpu, 0x80020DD8u)"
    hook_count = sum(path.read_text(encoding="utf-8").count(hook)
                     for path in generated)
    require(hook_count == 1,
            "generated guest code must contain exactly one God-mode entry hook")

print("Disruptor cheat source/codegen contract: PASS")
