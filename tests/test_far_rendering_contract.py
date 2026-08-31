#!/usr/bin/env python3
"""Source/codegen contract for Disruptor's experimental far rendering."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"far rendering contract: FAIL: {message}", file=sys.stderr)
        raise SystemExit(1)


source = read("src/disruptor_far_rendering.cpp")
header = read("src/disruptor_far_rendering.h")
codegen = read("psxrecomp-overlay/recompiler/src/code_generator.cpp")
dirty = read("psxrecomp-overlay/runtime/src/dirty_ram_interp.c")
main = read("psxrecomp-overlay/runtime/src/main.cpp")
audit = read("tools/audit_codegen.py")
cmake = read("CMakeLists.txt")
configs = read("game.toml") + read("game-widescreen.toml")

require("src/disruptor_far_rendering.cpp" in cmake and
        "tests/disruptor_far_rendering_test.cpp" in cmake and
        "add_test(NAME disruptor_far_rendering" in cmake,
        "runtime source and focused behavior test must remain enabled")
require("PSX_HAS_DISRUPTOR_FAR_RENDERING=1" in cmake,
        "the Disruptor target must opt into dirty-interpreter far hooks")

# The far renderer shares the already-generated renderer-entry fan-out.  A
# second config entry would emit a duplicate guest callback.
require(configs.count(
    'mod_function_entry_funcs = ["0x80020DD8", "0x80040E68"]') == 2,
    "both configs must retain exactly one audited renderer-entry seam")
require("disruptor.far_rendering.renderer" in source and
        "psx_mod_register_function_entry_plugin" in source and
        "0x80040E68u" in source,
        "far rendering must register independently at the reviewed renderer")

distance_sites = {
    0x8003A914: 0x8F8C04A0,
    0x8003B8C8: 0x8F8304A0,
    0x8003BCE0: 0x8F8204A0,
    0x8003C168: 0x8F8304A0,
    0x8003CD64: 0x8F8304A0,
    0x8003B2CC: 0x8F820494,
    0x8003BA30: 0x8F820494,
    0x8003BE40: 0x8F820494,
    0x8003C628: 0x8F820494,
    0x8003C878: 0x8F820494,
    0x8003D28C: 0x8F820494,
    0x8003FB0C: 0x8F840494,
    0x80043184: 0x8F820494,  # billboard post-pass CLUT-row selection
}
diagnostic_sites = {
    0x8003A0F8: 0x93B300C0,  # recursion-depth load
    0x8003A390: 0xA082001F,  # completed unique-room mark
    0x8003A92C: 0x00005812,  # first portal-distance candidate
    0x8003A964: 0x00681021,  # second portal-distance candidate
    0x8003AA74: 0x00AC1021,  # third portal-distance candidate
    0x8003B8D8: 0x0072182A,  # object distance decision, pass one
    0x8003BCEC: 0x0053102A,  # object distance decision, pass two
    0x8003C178: 0x0077182A,  # object distance decision, pass three
    0x8003CD70: 0x0077182A,  # object distance decision, pass four
    0x800410E0: 0x93830310,  # visible-room count boundary
    0x800410F8: 0x8F820680,  # visible-list end boundary
    0x800411D8: 0x8F8301D4,  # final packet boundary
}
expected_sites = {
    **distance_sites,
    **diagnostic_sites,
    0x8004279C: 0x8FBF016C,
}
excluded_sites = {
    0x8004030C,  # feeds a later store back to the fade global
    0x80040464,  # feeds a later store back to the fade global
    0x800247C4, 0x80027104, 0x80035538,
}
codegen_table = codegen[
    codegen.index("kDisruptorFarRenderingSites"):
    codegen.index("struct DisruptorBillboardAspectSite")
]
found_sites = {
    int(pc, 16): int(word, 16)
    for pc, word in re.findall(
        r"\{0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u\}", codegen_table)
}
require(found_sites == expected_sites,
        "code generation must retain exactly the reviewed far/diagnostic opcodes")
require(not (set(found_sites) & excluded_sites),
        "guest-state writers and unrelated loads must never be substituted")
source_table_start = source.index("constexpr auto kDistanceLoadSites")
source_table = source[source_table_start:
                      source.index("struct SessionDiagnostics",
                                   source_table_start)]
require(re.search(
    r"kDistanceLoadSites\s*=\s*std::array<DistanceLoadSite,\s*13>",
    source) is not None,
        "the host substitution table must contain exactly 13 reviewed loads")
for pc, instruction in distance_sites.items():
    require(f"0x{pc:08X}u" in source_table and
            f"0x{instruction:08X}u" in source_table,
            f"host substitution table is missing 0x{pc:08X}")
require("Disruptor far-rendering site" in codegen and
        "disruptor_far_rendering_instruction_hook(cpu" in codegen,
        "compiled code must fail closed and emit the dedicated AFTER hook")
require('"\\n{}disruptor_far_rendering_instruction_hook(cpu, "' in codegen and
        "config_.indent, addr, instr" in codegen,
        "compiled AFTER hooks must begin a separately indented statement line")

require("#ifdef PSX_HAS_DISRUPTOR_FAR_RENDERING" in dirty and
        "disruptor_far_rendering_instruction_hook(cpu, pc, insn, 1)" in dirty,
        "dirty interpreter parity must be project-gated and opcode-pinned")
for pc, instruction in expected_sites.items():
    require(f"0x{pc:08X}u" in dirty and f"0x{instruction:08X}u" in dirty,
            f"dirty interpreter is missing reviewed site 0x{pc:08X}")
require("disruptor_far_rendering_instruction_hook(cpu, pc, insn, 1)" in dirty and
        '"0x{:08X}u, 0x{:08X}u, 1);"' in codegen,
        "all reviewed diagnostic sites must remain AFTER-instruction")
dirty_table_start = dirty.index("static int disruptor_far_rendering_site")
dirty_table = dirty[dirty_table_start:dirty.index("#endif", dirty_table_start)]
audit_table_start = audit.index("FAR_RENDERING_SITES")
audit_table = audit[audit_table_start:audit.index("# Tests 4-6", audit_table_start)]
for pc in excluded_sites:
    token = f"0x{pc:08X}"
    require(token not in codegen_table and token not in dirty_table and
            token not in audit_table and token not in source_table,
            f"excluded far-rendering site leaked into hook tables: {token}")
require("FAR_RENDERING_SITES" in audit and
        "disruptor_far_rendering_instruction_hook" in audit,
        "generated shards must audit the exact far-rendering hook count")
require("far_rendering_raw_matches" in audit and
        "far_rendering_directive_lines" in audit and
        "far-rendering hooks must be separate generated statements" in audit and
        "far-rendering hook shares a preprocessor-directive line" in audit,
        "the generated audit must reject swallowed or coalesced hook calls")

require("psx_mod_write_word" not in source,
        "far rendering must substitute loaded registers without guest writes")
for token in (
    "DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL",
    "DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED",
    "disruptor_far_rendering_set_depth_fade_mode",
    "disruptor_far_rendering_get_depth_fade_mode",
    "far_load_substitutions",
    "fade_load_substitutions",
):
    require(token in header and token in source,
            f"depth-fade diagnostic ABI is missing {token}")
require("DISRUPTOR_FAR_RENDERING_DIAGNOSTICS_ABI_VERSION = 3" in header,
        "portal and packet observations require diagnostics ABI version 3")
for token in (
    "traversal_recursion_entries",
    "traversal_cap_hits",
    "portal_final_decision_flips",
    "object_decision_flips",
    "visibility_samples",
    "packet_stage_samples",
    "packet_entry_to_traversal_end_last",
    "packet_post_visible_loop_last",
):
    require(token in header and token in source,
            f"portal diagnostics ABI is missing {token}")
require("disruptor_far_rendering_abandon_metrics" in header and
        "disruptor_far_rendering_abandon_metrics" in source,
        "the module must expose savestate-abandon cleanup")
require("#ifdef PSX_HAS_DISRUPTOR_FAR_RENDERING" in main and
        "disruptor_far_rendering_abandon_metrics();" in main,
        "successful savestate loads must abandon host measurement spans")

for token in (
    "disruptor_far_rendering_renderer_entry",
    "disruptor_far_rendering_instruction_hook",
):
    require(token in header and token in source,
            f"far-rendering seam is missing declared implementation: {token}")
require("DISRUPTOR_FAR_RENDERING_BEFORE_INSTRUCTION" in header and
        "DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION" in header and
        "DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION" in source,
        "the hook ABI must declare phases and restore only after the opcode")

print("far rendering source/codegen contract: PASS")
