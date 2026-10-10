#!/usr/bin/env python3
"""Version and source contract for the range of Disruptor's world sprites under the widescreen squash."""

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800

# Per funnel: the step aside taken from the projection's X operand, three eighths of it added to the depth,
# and the range read again for the fade and for the sort slot after the projection.
RETAIL_INSTRUCTIONS = {
    0x8003B8B0: 0x00E09021,
    0x8003B8C4: 0x00121040,
    0x8003B8CC: 0x00521021,
    0x8003B8D0: 0x000210C3,
    0x8003B8D4: 0x02029021,
    0x8003BA3C: 0x02428823,
    0x8003BB78: 0x001220C3,
    0x8003BCAC: 0x00E09821,
    0x8003BCC8: 0x00131040,
    0x8003BCCC: 0x00531021,
    0x8003BCD0: 0x000210C3,
    0x8003BCD4: 0x02029821,
    0x8003BE4C: 0x02628823,
    0x8003BF94: 0x001320C3,
    0x8003C12C: 0xAFA20018,
    0x8003C140: 0x0040B821,
    0x8003C160: 0x02E03021,
    0x8003C164: 0x00061040,
    0x8003C16C: 0x00461021,
    0x8003C170: 0x000210C3,
    0x8003C174: 0x02A2B821,
    0x8003C634: 0x02E28823,
    0x8003C82C: 0x001720C3,
    0x8003CAE0: 0x001720C3,
    0x8003CD3C: 0x0100B821,
    0x8003CD48: 0x02E03021,
    0x8003CD5C: 0x00061040,
    0x8003CD60: 0x00461021,
    0x8003CD68: 0x000210C3,
    0x8003CD6C: 0x0282B821,
    0x8003CEC8: 0x26F7FFE8,
    0x8003CEE0: 0x2AE20008,
    0x8003CEEC: 0x34170008,
    0x8003D1B4: 0xA4379EA0,
    0x8003D294: 0x02E21023,
    0x8003D4B4: 0x001720C3,
    # The world renderer's side of it: a vertex's column from 160 picks the table entry its depth grows by.
    0x80046360: 0x216FFF60,
    0x8004637C: 0x95EF0000,
    0x800463CC: 0x01AF6820,
}
# Per funnel: its first instruction, where the step aside is taken from X, where the range is made, the projection
# seam, where a culled sprite leaves to, the register of X (None: the word at 0x18 of the stack), of the depth and
# of the range, and the game's own leads and floors that write the range register before and after the projection.
EFFECT_LEADS = {0x8003C68C, 0x8003C69C, 0x8003C6F8}
FUNNELS = (
    (0x8003B78C, 0x8003B8B0, 0x8003B8D4, 0x8003B98C, 0x8003BB94, 7, 16, 18, set(), {0x8003BA78}),
    (0x8003BBE0, 0x8003BCAC, 0x8003BCD4, 0x8003BDA0, 0x8003BFBC, 7, 16, 19, set(), {0x8003BE90, 0x8003BEA0}),
    (0x8003C000, 0x8003C12C, 0x8003C174, 0x8003C4B0, 0x8003CAF4, None, 21, 23, set(), EFFECT_LEADS),
    (0x8003CB38, 0x8003CD3C, 0x8003CD6C, 0x8003D078, 0x8003D4C8, 8, 20, 23, {0x8003CEC8, 0x8003CEEC}, set()),
)
LOADS = range(0x20, 0x27)
STORES = {0x28: 1, 0x29: 2, 0x2A: 4, 0x2B: 4, 0x2E: 4, 0x3A: 4}
STACK = 29
GLOBALS = 28
X_SLOT = range(0x18, 0x1C)
GUEST_WRITE = re.compile(r"cpu(?:\.|->)gpr\[[^\]]+\] [-+*/|&^]?=(?!=)")
NO_SPECIAL_TARGET = {0x08, 0x0C, 0x0D, 0x11, 0x13, 0x18, 0x19, 0x1A, 0x1B}


def written(word: int) -> int | None:
    """The general register an instruction writes, from the R3000's opcode table."""
    operation, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
    if operation == 0x00:
        return None if word & 0x3F in NO_SPECIAL_TARGET else rd
    if operation == 0x01:
        return 31 if rt & 0x10 else None
    if operation == 0x03:
        return 31
    if 0x08 <= operation <= 0x0F or operation in LOADS:
        return rt
    if operation in (0x10, 0x12) and rs in (0x00, 0x02):
        return rt
    return None


def signed(half: int) -> int:
    return half - 0x10000 if half & 0x8000 else half


def target(address: int, word: int) -> int | None:
    """Where a jump or a branch goes, when the instruction itself says so."""
    operation = word >> 26
    if operation in (0x02, 0x03):
        return (address + 4) & 0xF0000000 | (word & 0x03FFFFFF) << 2
    if 0x04 <= operation <= 0x07 or (operation == 0x01 and (word >> 16 & 31) in (0x00, 0x01, 0x10, 0x11)):
        return address + 4 + (signed(word & 0xFFFF) << 2)
    return None


def calls(word: int) -> bool:
    return word >> 26 == 0x03 or (word >> 26 == 0x00 and word & 0x3F == 0x09)


def stores_x(word: int) -> bool:
    """A store into the stack word that holds the third funnel's X."""
    size = STORES.get(word >> 26)
    if size is None or word >> 21 & 31 != STACK:
        return False
    first = signed(word & 0xFFFF)
    return first < X_SLOT.stop and first + size > X_SLOT.start


def aliases_stack(word: int) -> bool:
    """An instruction that makes another register out of the stack pointer."""
    operation = word >> 26
    reads = word >> 21 & 31 == STACK or (operation == 0x00 and word >> 16 & 31 == STACK)
    return reads and operation not in STORES and operation not in LOADS and written(word) not in (None, STACK)


def faults(words: list[int]) -> list[str]:
    """What in the funnels' code would let a range be squashed that is not the game's own of that depth and step."""

    def at(address: int) -> int:
        return words[(address - LOAD_ADDRESS) // 4]

    jumps = [
        (LOAD_ADDRESS + 4 * index, goes)
        for index, word in enumerate(words)
        if (goes := target(LOAD_ADDRESS + 4 * index, word)) is not None
    ]
    found = []
    for start, taken, made, projection, leaves, x, depth, held, expected, later in FUNNELS:
        name = f"the funnel projecting at 0x{projection:08X}"
        if written(at(made)) != held:
            found.append(f"{name}: its range is not made in register {held}")
        writers = {address for address in range(made + 4, projection, 4) if written(at(address)) == held}
        if writers != expected:
            found.append(f"{name}: its range register is written at {sorted(map(hex, writers))} before the projection")
        writers = {address for address in range(projection + 4, leaves, 4) if written(at(address)) == held}
        if writers != later:
            found.append(f"{name}: its range register is written at {sorted(map(hex, writers))} after the projection")
        if any(written(at(address)) == depth for address in range(made, projection, 4)):
            found.append(f"{name}: its depth is written between the range and the projection")
        before = [at(address) for address in range(taken + 4, projection, 4)]
        if x is None and any(written(word) == STACK or stores_x(word) for word in before):
            found.append(f"{name}: the stack word of its X is stored again or the stack moves")
        if x is None and (
            any(word >> 26 in STORES and word >> 21 & 31 not in (STACK, GLOBALS) for word in before)
            or any(aliases_stack(at(address)) for address in range(start, projection, 4))
        ):
            found.append(f"{name}: a store may reach the stack word of its X through another register")
        if x is not None and any(written(word) == x or calls(word) for word in before):
            found.append(f"{name}: the register of its X is written, or a call may write it")
        leaving = {
            goes
            for address in range(made, projection, 4)
            if (goes := target(address, at(address))) is not None
            and not calls(at(address))
            and not made <= goes <= projection
        }
        if leaving != {leaves}:
            found.append(
                f"{name}: a sprite leaves for {sorted(map(hex, leaving))} between the range and the projection"
            )
        if any(projection < goes < leaves and not projection <= address < leaves for address, goes in jumps):
            found.append(f"{name}: the code after the projection is entered from elsewhere")
    return found


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
source = read("src/disruptor_sprite_depth.cpp")
gte = read("psxrecomp-overlay/runtime/src/gte.cpp")

require(
    'tests/test_sprite_range_contract.py"\n            --require-artifacts)' in cmake
    and "    set_tests_properties(disruptor_sprite_range_contract\n        PROPERTIES LABELS retail)\n" in cmake
    and "    add_test(NAME disruptor_sprite_depth\n" in cmake,
    "this contract with its input and the unit test of the range must stay registered",
)
require(
    "    {0x8003B98Cu, 16u, {0x8003BB88u, 0u}, {7u, 0u}, {5u, 0u}, Size::Words, 18u, false},\n"
    "    {0x8003BDA0u, 16u, {0x8003BFB0u, 0u}, {7u, 0u}, {6u, 0u}, Size::Bytes, 19u, false},\n"
    "    {0x8003C4B0u, 21u, {kEffectPacketSite, 0x8003CAD4u}, {0u, 0x18u}, {0u, 0x20u}, Size::Effect, 23u, false},\n"
    "    {kDeferredProjection, 20u, {kShadowPacketSite, 0u}, {8u, 0u}, {5u, 0u}, Size::Body, 23u, true},\n"
    in source
    and "constexpr uint32_t kDeferredProjection = 0x8003D078u;" in source
    and "constexpr int64_t kBodyLead = 0x18;" in source
    and "constexpr int64_t kNearest = 8;\n" in source,
    "each funnel must name the registers of its depth, its step aside and its range, and the fourth its lead",
)
require(
    "    const int64_t range = depth + (3 * aside >> 3);\n"
    "    return funnel.led ? std::max(range - kBodyLead, kNearest) : range;\n" in source,
    "the range must be the game's: the depth and three eighths of the step aside, the fourth funnel's led and floored",
)
require(
    "    gte_ws_x_squash(&num, &den);\n    const std::optional<int32_t> x = operand(cpu, funnel.x);\n"
    "    const auto depth = static_cast<int32_t>(cpu.gpr[funnel.depth_gpr]);\n"
    "    if (num <= 0 || den <= 0 || !x || depth <= 0) return;\n"
    in source
    and "    const int64_t aside = *x < 0 ? -static_cast<int64_t>(*x) : *x;\n"
    "    if (static_cast<int32_t>(cpu.gpr[funnel.range_gpr]) != range_of(funnel, depth, aside)) return;\n"
    "    cpu.gpr[funnel.range_gpr] = static_cast<uint32_t>(range_of(funnel, depth, aside * num / den));\n"
    in source
    and GUEST_WRITE.findall(source) == ["cpu.gpr[funnel.range_gpr] ="]
    and all(
        name not in source for name in ("psx_mod_write", "write_word", "write_half", "write_byte", "cpu.pc", "cpu->pc")
    ),
    "only while the GTE squashes, only a register that holds the game's own range, and nothing else of the guest is written",
)
require(
    "        g_pending = projected(*cpu, funnel);\n        squash_range(*cpu, funnel);\n        return;\n" in source
    and source.count("squash_range(") == 2,
    "the range is brought under the squash at a funnel's projection and nowhere else",
)
require(
    "static bool ws_x_squashed() { return s_ws_xnum != s_ws_xden && !gpu_ws_present_native_43(); }\n" in gte
    and "    const bool squashed = ws_x_squashed();\n    *num = squashed ? s_ws_xnum : 1;\n    *den = squashed ? s_ws_xden : 1;\n"
    in gte
    and "    bool do_squash = ws_x_squashed();\n" in gte
    and gte.count("s_ws_xnum != s_ws_xden") == 1,
    "the ratio handed to the funnels must be the one RTPS squashes the world by, decided in one place",
)
require(
    [
        written(word)
        for word in (0x26F7FFE8, 0x34170008, 0xA4379EA0, 0x0000B012, 0x8FB7004C, 0x0C011346, 0x480D9800, 0x00E09021)
    ]
    == [23, 23, None, 22, 23, 31, 13, 18]
    and [written(word) for word in (0x01000008, 0x0062001A, 0x0411FFFF, 0x1440FFFF, 0x8AE20000, 0x9AE20000, 0x4A180001)]
    == [None, None, 31, None, 2, 2, None],
    "the scan below must know which register an instruction writes",
)

image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()
    retail = list(struct.unpack_from(f"<{(len(image) - ROM_TEXT_OFFSET) // 4}I", image, ROM_TEXT_OFFSET))

    def changed(address: int, word: int) -> list[int]:
        copy = list(retail)
        copy[(address - LOAD_ADDRESS) // 4] = word
        return copy

    def spare(first: int, last: int) -> int:
        """A nop of the retail code to put a seeded instruction in."""
        return next(address for address in range(first, last, 4) if retail[(address - LOAD_ADDRESS) // 4] == 0)

    def branch(address: int, goes: int) -> int:
        return 0x10000000 | (goes - address - 4) >> 2 & 0xFFFF

    for address, instruction in RETAIL_INSTRUCTIONS.items():
        require(
            retail[(address - LOAD_ADDRESS) // 4] == instruction, f"retail sprite range code changed at 0x{address:08X}"
        )
    require(not faults(retail), f"the retail funnels do not hold their operands to the projection: {faults(retail)}")
    first, second, third, fourth = ((made + 4, projection) for _, _, made, projection, *_ in FUNNELS)
    seeded = {
        "X of the first funnel lowered before its projection": (0x8003B960, 0x24E7FFFF, "register of its X is written"),
        "the first funnel's depth changed": (spare(*first), 0x26100001, "its depth is written"),
        "the first funnel's range changed": (spare(*first), 0x26520001, "before the projection"),
        "the first funnel's range overwritten before its sort": (0x8003BB74, 0x02009021, "after the projection"),
        "the second funnel's lead gone": (0x8003BE90, 0x00000000, "after the projection"),
        "a pointer made to the third funnel's stack": (spare(*third), 0x27A80018, "through another register"),
        "a pointer made to its stack before its step aside": (
            spare(*FUNNELS[2][:2]),
            0x27A80018,
            "through another register",
        ),
        "a store through a register in the third funnel": (spare(*third), 0xAD000000, "through another register"),
        "a call while the first funnel's X is in a register": (spare(*first), 0x0C01124E, "or a call may write it"),
        "a call through a register in the second funnel": (spare(*second), 0x0040F809, "or a call may write it"),
        "the third funnel's X stored again": (spare(*third), 0xAFA20018, "stored again or the stack moves"),
        "one byte of the third funnel's X stored": (spare(*third), 0xA3A2001B, "stored again or the stack moves"),
        "the third funnel's stack moved": (spare(*third), 0x27BDFFF8, "stored again or the stack moves"),
        "X of the fourth funnel changed": (spare(*fourth), 0x25080001, "register of its X is written"),
        "the fourth funnel's lead gone": (0x8003CEC8, 0x00000000, "before the projection"),
        "the fourth funnel's range made elsewhere": (0x8003CD6C, 0x0282B021, "its range is not made in register"),
        "a way from the second funnel's range past its projection": (
            spare(*second),
            branch(spare(*second), FUNNELS[1][3] + 8),
            "a sprite leaves for",
        ),
        "a way into the code after the first projection": (
            spare(FUNNELS[0][0], FUNNELS[0][1]),
            0x08000000 | (FUNNELS[0][3] + 8 & 0x0FFFFFFF) >> 2,
            "is entered from elsewhere",
        ),
    }
    for name, (address, word, complaint) in seeded.items():
        said = faults(changed(address, word))
        require(any(complaint in fault for fault in said), f"the scan must refuse {name}, it said {said}")

checked = f"retail image {'checked' if image_path.exists() else 'absent'}"
require(
    "--require-artifacts" not in sys.argv[1:] or image_path.exists(),
    f"the build gate must check the retail image: {checked}",
)
print(f"Disruptor sprite range source contract: PASS ({checked})")
