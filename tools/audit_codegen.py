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

PRECISION_MFC2_SITES = {
    0x8004633C: 0x480A7000,
    0x8004795C: 0x480A7000,
}

PRECISION_STORE_SITES = {
    0x8004641C: 0xACAA0004,
    0x800464BC: 0xACAA0004,
    0x80046524: 0xACAA0004,
    0x80046540: 0xACAA0004,
    0x80046594: 0xACAA0004,
    0x800465D8: 0xACAA0004,
}

PRECISION_COPY_LOAD_SITES = {
    0x80046C4C: (0x8D0C0004, 12),
    0x80046C50: (0x8D2D0004, 13),
    0x80046C54: (0x8D4E0004, 14),
    0x80046C60: (0x8D6F0004, 15),
    0x80047A80: (0x8CA80084, 8),
    0x80047A84: (0x8CA900A4, 9),
    0x80047A88: (0x8CAA00C4, 10),
    0x80047A8C: (0x8CAB00BC, 11),
    0x80047B40: (0x8CA800A4, 8),
    0x80047B44: (0x8CA9008C, 9),
    0x80047B48: (0x8CAA00AC, 10),
    0x80047B4C: (0x8CAB00C4, 11),
    0x80047C00: (0x8CA800C4, 8),
    0x80047C04: (0x8CA900AC, 9),
    0x80047C08: (0x8CAA0094, 10),
    0x80047C0C: (0x8CAB00B4, 11),
    0x80047CC4: (0x8CA800BC, 8),
    0x80047CC8: (0x8CA900C4, 9),
    0x80047CCC: (0x8CAA00B4, 10),
    0x80047CD0: (0x8CAB009C, 11),
    0x80047D6C: (0x8CA80084, 8),
    0x80047D70: (0x8CA9008C, 9),
    0x80047D74: (0x8CAA0094, 10),
    0x80047D78: (0x8CAB009C, 11),
}

PRECISION_COPY_STORE_SITES = {
    0x80046D04: (0xAF2C0008, 12),
    0x80046D08: (0xAF2D0014, 13),
    0x80046D10: (0xAF2E002C, 14),
    0x80046D0C: (0xAF2F0020, 15),
    0x80047A90: (0xAF280008, 8),
    0x80047A94: (0xAF290014, 9),
    0x80047A9C: (0xAF2A002C, 10),
    0x80047A98: (0xAF2B0020, 11),
    0x80047B50: (0xAF280008, 8),
    0x80047B54: (0xAF290014, 9),
    0x80047B5C: (0xAF2A002C, 10),
    0x80047B58: (0xAF2B0020, 11),
    0x80047C10: (0xAF280008, 8),
    0x80047C14: (0xAF290014, 9),
    0x80047C1C: (0xAF2A002C, 10),
    0x80047C18: (0xAF2B0020, 11),
    0x80047CD4: (0xAF280008, 8),
    0x80047CD8: (0xAF290014, 9),
    0x80047CE0: (0xAF2A002C, 10),
    0x80047CDC: (0xAF2B0020, 11),
    0x80047D7C: (0xAF280008, 8),
    0x80047D80: (0xAF290014, 9),
    0x80047D88: (0xAF2A002C, 10),
    0x80047D84: (0xAF2B0020, 11),
}

PRECISION_SCRATCH_STORE_SITES = {
    0x80047A0C: 0xACAA0004,
}

VERTICAL_CAMERA_SITES = {
    0x8003AD54: 0x34020078,
    0x8003ADE4: 0x34020078,
    0x8003B140: 0x34020078,
    0x8003B1EC: 0x34020078,
    0x8003B764: 0x34020078,
    0x8003B990: 0x34020078,
    0x8003BDA4: 0x34020078,
    0x8003C4B4: 0x34020078,
    0x8003C99C: 0x34020078,
    0x8003D07C: 0x34020078,
    0x8003D398: 0x34030078,
    0x8003F6B0: 0x34020078,
    0x8003B900: 0x0205102A,
    0x8003B90C: 0x0202102A,
    0x8003BD14: 0x0206102A,
    0x8003BD20: 0x0202102A,
    0x8003CDB8: 0x0065102A,
    0x8003CDC4: 0x0062102A,
    0x8002E4B8: 0x92640022,
    0x8002EAF0: 0x92430020,
    0x8004279C: 0x8FBF016C,
}

FAR_RENDERING_SITES = {
    0x8003A0F8: 0x93B300C0,
    0x8003A390: 0xA082001F,
    0x8003A92C: 0x00005812,
    0x8003A964: 0x00681021,
    0x8003AA74: 0x00AC1021,
    0x8003A914: 0x8F8C04A0,
    0x8003B8C8: 0x8F8304A0,
    0x8003BCE0: 0x8F8204A0,
    0x8003C168: 0x8F8304A0,
    0x8003CD64: 0x8F8304A0,
    0x8003B8D8: 0x0072182A,
    0x8003BCEC: 0x0053102A,
    0x8003C178: 0x0077182A,
    0x8003CD70: 0x0077182A,
    0x8003B2CC: 0x8F820494,
    0x8003BA30: 0x8F820494,
    0x8003BE40: 0x8F820494,
    0x8003C628: 0x8F820494,
    0x8003C878: 0x8F820494,
    0x8003D28C: 0x8F820494,
    0x8003FB0C: 0x8F840494,
    0x80043184: 0x8F820494,
    0x800410E0: 0x93830310,
    0x800410F8: 0x8F820680,
    0x800411D8: 0x8F8301D4,
    0x8004279C: 0x8FBF016C,
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

    vertical_camera_matches = re.findall(
        r"disruptor_vertical_camera_instruction_hook\(cpu, "
        r"0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, 1\)",
        shard_text,
    )
    generated_vertical_camera_sites = {
        int(address, 16): int(word, 16)
        for address, word in vertical_camera_matches
    }
    if (generated_vertical_camera_sites != VERTICAL_CAMERA_SITES or
            len(vertical_camera_matches) != len(VERTICAL_CAMERA_SITES)):
        failures.append(
            "reviewed vertical-camera hooks differ: expected "
            + str(VERTICAL_CAMERA_SITES)
            + ", found "
            + str(generated_vertical_camera_sites)
        )
    renderer_entry_hook = (
        "psx_mod_function_entry(cpu, 0x80040E68u)"
    )
    if shard_text.count(renderer_entry_hook) != 1:
        failures.append(
            "expected exactly one reviewed renderer function-entry hook"
        )

    far_rendering_raw_matches = re.findall(
        r"disruptor_far_rendering_instruction_hook\(cpu, "
        r"0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, 1\)",
        shard_text,
    )
    far_rendering_matches = re.findall(
        r"(?m)^[ \t]+disruptor_far_rendering_instruction_hook\(cpu, "
        r"0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, 1\);"
        r"(?:[ \t]+/\*[^\r\n]*\*/)?[ \t]*$",
        shard_text,
    )
    far_rendering_directive_lines = re.findall(
        r"(?m)^[ \t]*#[^\r\n]*"
        r"disruptor_far_rendering_instruction_hook\(",
        shard_text,
    )
    generated_far_rendering_sites = {
        int(address, 16): int(word, 16)
        for address, word in far_rendering_matches
    }
    if far_rendering_directive_lines:
        failures.append(
            "far-rendering hook shares a preprocessor-directive line"
        )
    if len(far_rendering_raw_matches) != len(far_rendering_matches):
        failures.append(
            "far-rendering hooks must be separate generated statements"
        )
    if (generated_far_rendering_sites != FAR_RENDERING_SITES or
            len(far_rendering_matches) != len(FAR_RENDERING_SITES)):
        failures.append(
            "reviewed far-rendering hooks differ: expected "
            + str(FAR_RENDERING_SITES)
            + ", found "
            + str(generated_far_rendering_sites)
        )

    precision_mfc2_matches = re.findall(
        r"gte_precision_mfc2_pc_read\(0x([0-9A-F]{8})u, "
        r"0x([0-9A-F]{8})u, 14,",
        shard_text,
    )
    generated_precision_mfc2 = {
        int(address, 16): int(word, 16)
        for address, word in precision_mfc2_matches
    }
    if (generated_precision_mfc2 != PRECISION_MFC2_SITES or len(
        precision_mfc2_matches
    ) != len(PRECISION_MFC2_SITES)):
        failures.append(
            "reviewed precision MFC2 hooks differ: expected "
            + str(PRECISION_MFC2_SITES)
            + ", found "
            + str(generated_precision_mfc2)
        )

    precision_store_matches = re.findall(
        r"gte_precision_store_pc_word\(0x([0-9A-F]{8})u, "
        r"0x([0-9A-F]{8})u,",
        shard_text,
    )
    generated_precision_stores = {
        int(address, 16): int(word, 16)
        for address, word in precision_store_matches
    }
    if (generated_precision_stores != PRECISION_STORE_SITES or len(
        precision_store_matches
    ) != len(PRECISION_STORE_SITES)):
        failures.append(
            "reviewed precision SW hooks differ: expected "
            + str(PRECISION_STORE_SITES)
            + ", found "
            + str(generated_precision_stores)
        )

    precision_scratch_store_matches = re.findall(
        r"gte_precision_scratch_store_pc_word\(0x([0-9A-F]{8})u, "
        r"0x([0-9A-F]{8})u,",
        shard_text,
    )
    generated_precision_scratch_stores = {
        int(address, 16): int(word, 16)
        for address, word in precision_scratch_store_matches
    }
    if (generated_precision_scratch_stores != PRECISION_SCRATCH_STORE_SITES or
            len(precision_scratch_store_matches) !=
            len(PRECISION_SCRATCH_STORE_SITES)):
        failures.append(
            "reviewed precision scratchpad SW hooks differ: expected "
            + str(PRECISION_SCRATCH_STORE_SITES)
            + ", found "
            + str(generated_precision_scratch_stores)
        )

    precision_copy_load_matches = re.findall(
        r"gte_precision_copy_pc_read\(0x([0-9A-F]{8})u,\s*"
        r"0x([0-9A-F]{8})u,\s*(\d+),",
        shard_text,
    )
    generated_precision_copy_loads = {
        int(address, 16): (int(word, 16), int(gpr))
        for address, word, gpr in precision_copy_load_matches
    }
    if (generated_precision_copy_loads != PRECISION_COPY_LOAD_SITES or len(
        precision_copy_load_matches
    ) != len(PRECISION_COPY_LOAD_SITES)):
        failures.append(
            "reviewed precision packet-copy LW hooks differ: expected "
            + str(PRECISION_COPY_LOAD_SITES)
            + ", found "
            + str(generated_precision_copy_loads)
        )

    precision_copy_store_matches = re.findall(
        r"gte_precision_copy_pc_word\(0x([0-9A-F]{8})u,\s*"
        r"0x([0-9A-F]{8})u,\s*(\d+),",
        shard_text,
    )
    generated_precision_copy_stores = {
        int(address, 16): (int(word, 16), int(gpr))
        for address, word, gpr in precision_copy_store_matches
    }
    if (generated_precision_copy_stores != PRECISION_COPY_STORE_SITES or len(
        precision_copy_store_matches
    ) != len(PRECISION_COPY_STORE_SITES)):
        failures.append(
            "reviewed precision packet-copy SW hooks differ: expected "
            + str(PRECISION_COPY_STORE_SITES)
            + ", found "
            + str(generated_precision_copy_stores)
        )

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
