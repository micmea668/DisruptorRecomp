#!/usr/bin/env python3
"""Audit structural invariants in PSXRecomp's generated game code."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
GENERATED = ROOT / "generated"
CODE_START = 0x80011200
CODE_END = 0x80056938
EXPECTED_SHARDS = 22
EXPECTED_FUNCTIONS = 579
EXPECTED_DISPATCH_ENTRIES = 12_797

FUNCTION_DEFINITION = re.compile(
    r"^void func_([0-9A-F]{8})\(CPUState\* cpu\)$", re.MULTILINE
)
DIRECT_CALL = re.compile(r"\bfunc_([0-9A-F]{8})\(cpu\);")
DISPATCH_ENTRY = re.compile(
    r"\{0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, \d+u, \d+u, "
    r"func_([0-9A-F]{8})\}"
)
CPS_STATIC_TARGET = re.compile(
    r"cpu->pc = 0x([0-9A-F]{8})u; return;\s+"
    r"/\* CPS (?:jal|j) -> 0x[0-9A-F]{8} \*/"
)

UNSIGNED_PORTAL_LEFT = re.compile(
    r"cpu->gpr\[2\] = psx_cyc_load_half\("
    r"cpu, cpu->gpr\[17\], 2, 0x20000u\);\s+"
    r"/\* 0x80041184: 0x96220000 \*/"
)
UNSIGNED_PORTAL_RIGHT = re.compile(
    r"cpu->gpr\[2\] = psx_cyc_load_half\("
    r"cpu, cpu->gpr\[17\] \+ 2, 2, 0x20000u\);\s+"
    r"/\* 0x80041178: 0x96220002 \*/"
)

CPU_PROJECTED_X_SITES = {
    "0x8003A578": "0x244400A0",
    "0x8003A650": "0x244300A2",
    "0x8003B244": "0x244200A0",
    "0x8003B2A0": "0x244200A0",
    "0x8003B988": "0x244200A0",
    "0x8003BD9C": "0x244200A0",
    "0x8003C4AC": "0x244200A0",
    "0x8003D074": "0x244200A0",
    "0x8003F6B4": "0x248700A0",
}

YAW_CONE_SITES = {
    "0x80040FE8": ("0x24710020", "32"),
    "0x80040FF0": ("0x2472FFE0", "-32"),
}

HORIZONTAL_SIDE_PLANE_SITES = {
    "0x8003B8EC": "0x0067102A",
    "0x8003B8F8": "0x0062102A",
    "0x8003BD00": "0x0067102A",
    "0x8003BD0C": "0x0062102A",
}

# Tests 4-6 widened Disruptor's portal spans into negative X.  Its static-world
# renderer was authored around unsigned 0..320 spans, and yaw-dependent portal
# selection became unstable.  Test 7 instead keeps every portal/outcode bound
# vanilla and squashes the final CPU projection at the exact sites above.
VANILLA_PORTAL_AND_POINT_SITES = (
    "cpu->gpr[2] = 0x0140;  /* 0x8003A430: 0x34020140 */",
    "cpu->write_word(cpu->gpr[29] + 44, cpu->gpr[0]);  "
    "/* 0x8003A434: 0xAFA0002C */",
    "cpu->gpr[2] = ((int32_t)cpu->gpr[4] < 321) ? 1 : 0;  "
    "/* 0x8003A580: 0x28820141 */",
    "cpu->gpr[4] = cpu->gpr[0];  /* move */  "
    "/* 0x8003A584: 0x00002021 */",
    "cpu->gpr[2] = ((int32_t)cpu->gpr[4] < 320) ? 1 : 0;  "
    "/* 0x8003A69C: 0x28820140 */",
    "cpu->gpr[2] = (cpu->gpr[6] < (uint32_t)321) ? 1 : 0;  "
    "/* 0x8003B6E8: 0x2CC20141 */",
    "cpu->gpr[2] = ((int32_t)cpu->gpr[7] < -160) ? 1 : 0;  "
    "/* 0x8003F6C0: 0x28E2FF60 */",
    "cpu->gpr[7] = -160;  /* 0x8003F6D0: 0x2407FF60 */",
    "cpu->gpr[2] = ((int32_t)cpu->gpr[7] < 480) ? 1 : 0;  "
    "/* 0x8003F6DC: 0x28E201E0 */",
    "cpu->gpr[7] = 0x01E0;  /* 0x8003F6E8: 0x340701E0 */",
)


def format_addresses(addresses: set[int]) -> str:
    values = sorted(addresses)
    shown = ", ".join(f"0x{value:08X}" for value in values[:12])
    if len(values) > 12:
        shown += f", ... ({len(values)} total)"
    return shown


def main() -> int:
    shards = sorted(GENERATED.glob("SLUS_002.24.code_full_*.c"))
    dispatch_path = GENERATED / "SLUS_002.24.code_dispatch.c"
    failures: list[str] = []

    if len(shards) != EXPECTED_SHARDS:
        failures.append(
            f"expected {EXPECTED_SHARDS} generated shards, found {len(shards)}"
        )
    if not dispatch_path.is_file():
        failures.append(f"missing {dispatch_path}")
    if failures:
        for failure in failures:
            print(f"error: {failure}")
        return 1

    shard_text = "\n".join(path.read_text(encoding="utf-8") for path in shards)
    dispatch_text = dispatch_path.read_text(encoding="utf-8")

    definition_matches = FUNCTION_DEFINITION.findall(shard_text)
    definitions = {int(value, 16) for value in definition_matches}
    if len(definition_matches) != len(definitions):
        failures.append("duplicate generated function definitions")
    if len(definitions) != EXPECTED_FUNCTIONS:
        failures.append(
            f"expected {EXPECTED_FUNCTIONS} functions, found {len(definitions)}"
        )

    direct_calls = {int(value, 16) for value in DIRECT_CALL.findall(shard_text)}
    missing_direct_calls = direct_calls - definitions
    if missing_direct_calls:
        failures.append(
            "direct calls without definitions: "
            + format_addresses(missing_direct_calls)
        )

    dispatch_matches = DISPATCH_ENTRY.findall(dispatch_text)
    if len(dispatch_matches) != EXPECTED_DISPATCH_ENTRIES:
        failures.append(
            "expected "
            f"{EXPECTED_DISPATCH_ENTRIES} dispatch entries, "
            f"found {len(dispatch_matches)}"
        )

    dispatch_addresses = {int(address, 16) for address, _, _ in dispatch_matches}
    dispatch_functions = {int(function, 16) for _, _, function in dispatch_matches}
    missing_dispatch_functions = dispatch_functions - definitions
    if missing_dispatch_functions:
        failures.append(
            "dispatch rows reference missing functions: "
            + format_addresses(missing_dispatch_functions)
        )

    function_entries = {
        int(address, 16)
        for address, resume, function in dispatch_matches
        if int(resume, 16) == 0 and address == function
    }
    missing_function_entries = definitions - function_entries
    extra_function_entries = function_entries - definitions
    if missing_function_entries:
        failures.append(
            "functions without primary dispatch entries: "
            + format_addresses(missing_function_entries)
        )
    if extra_function_entries:
        failures.append(
            "primary dispatch entries without functions: "
            + format_addresses(extra_function_entries)
        )

    static_targets = {int(value, 16) for value in CPS_STATIC_TARGET.findall(shard_text)}
    resident_targets = {
        value for value in static_targets if CODE_START <= value < CODE_END
    }
    missing_static_targets = resident_targets - dispatch_addresses
    if missing_static_targets:
        failures.append(
            "resident CPS targets without dispatch entries: "
            + format_addresses(missing_static_targets)
        )

    # The projection-and-stretch path deliberately restores the retail
    # unsigned portal representation. Both halves must therefore remain LHU.
    if not UNSIGNED_PORTAL_LEFT.search(shard_text):
        failures.append(
            "portal left edge is not the retail unsigned load at 0x80041184"
        )
    if not UNSIGNED_PORTAL_RIGHT.search(shard_text):
        failures.append(
            "portal right edge load changed unexpectedly at 0x80041178"
        )

    projection_tags = re.findall(
        r"/\* ws CPU-projected screen-x \*/\s*"
        r"/\* (0x[0-9A-F]{8}): (0x[0-9A-F]{8}) \*/",
        shard_text,
    )
    projected_sites = {address: word for address, word in projection_tags}
    if projected_sites != CPU_PROJECTED_X_SITES:
        failures.append(
            "CPU-projected widescreen sites differ: expected "
            + str(CPU_PROJECTED_X_SITES)
            + ", found "
            + str(projected_sites)
        )
    if shard_text.count("psx_ws_project_x(") != len(CPU_PROJECTED_X_SITES):
        failures.append("unexpected CPU-projection helper call count")

    yaw_tags = re.findall(
        r"psx_ws_yaw_cone_offset\((-?\d+), 256u\);\s*"
        r"/\* aspect-scaled wrapping yaw half-FOV \*/\s*"
        r"/\* (0x[0-9A-F]{8}): (0x[0-9A-F]{8}) \*/",
        shard_text,
    )
    generated_yaw_sites = {
        address: (word, offset) for offset, address, word in yaw_tags
    }
    if generated_yaw_sites != YAW_CONE_SITES:
        failures.append(
            "yaw-cone sites differ: expected "
            + str(YAW_CONE_SITES)
            + ", found "
            + str(generated_yaw_sites)
        )
    if shard_text.count("psx_ws_yaw_cone_offset(") != len(YAW_CONE_SITES):
        failures.append("unexpected yaw-cone helper call count")

    side_plane_tags = re.findall(
        r"psx_ws_horizontal_slt\([^;]+;\s*"
        r"/\* aspect-scaled camera-horizontal side plane \*/\s*"
        r"/\* (0x[0-9A-F]{8}): (0x[0-9A-F]{8}) \*/",
        shard_text,
    )
    generated_side_plane_sites = {
        address: word for address, word in side_plane_tags
    }
    if generated_side_plane_sites != HORIZONTAL_SIDE_PLANE_SITES:
        failures.append(
            "horizontal side-plane sites differ: expected "
            + str(HORIZONTAL_SIDE_PLANE_SITES)
            + ", found "
            + str(generated_side_plane_sites)
        )
    if shard_text.count("psx_ws_horizontal_slt(") != len(
        HORIZONTAL_SIDE_PLANE_SITES
    ):
        failures.append("unexpected horizontal side-plane helper call count")

    for site in VANILLA_PORTAL_AND_POINT_SITES:
        if site not in shard_text:
            failures.append(
                "retail portal/outcode invariant changed unexpectedly: " + site
            )
    for tag in (
        "/* ws manual lower",
        "/* ws manual upper",
        "/* ws manual packed",
        "/* ws explicit screen-x cull */",
        "/* ws cull slti site",
    ):
        if tag in shard_text:
            failures.append("unsafe native-wide portal emit remains: " + tag)

    print(f"Generated shards:       {len(shards)}")
    print(f"Compiled functions:     {len(definitions)}")
    print(f"Dispatch addresses:     {len(dispatch_addresses)}")
    print(f"Static resident targets:{len(resident_targets):6d}")
    if failures:
        for failure in failures:
            print(f"error: {failure}")
        return 1
    print("Code-generation audit: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
