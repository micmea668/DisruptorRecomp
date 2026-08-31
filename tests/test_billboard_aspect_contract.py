#!/usr/bin/env python3
"""Source/codegen contract for Disruptor world-billboard aspect repair."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"billboard aspect contract: FAIL: {message}", file=sys.stderr)
        raise SystemExit(1)


source = read("src/disruptor_billboard_aspect.cpp")
header = read("src/disruptor_billboard_aspect.h")
codegen = read("psxrecomp-overlay/recompiler/src/code_generator.cpp")
dirty = read("psxrecomp-overlay/runtime/src/dirty_ram_interp.c")
gpu = read("psxrecomp-overlay/runtime/src/gpu.c")
gpu_h = read("psxrecomp-overlay/runtime/include/gpu.h")
tag_match_h = read("psxrecomp-overlay/runtime/include/gpu_ws_tag_match.h")
audit = read("tools/audit_codegen.py")
cmake = read("CMakeLists.txt")

expected_sites = {
    0x8003BB88: 0xA6030016,
    0x8003BFB0: 0xA6030016,
    0x8003C848: 0xA6030016,
    0x8003CAD4: 0xA6020016,
    0x8003D488: 0xA2060025,
    0x800433A0: 0x02A02821,
}

codegen_table = codegen[
    codegen.index("kDisruptorBillboardAspectSites"):
    codegen.index("static bool codegen_cycle_per_insn")
]
found_sites = {
    int(pc, 16): int(word, 16)
    for pc, word in re.findall(
        r"\{0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, \d+u\}",
        codegen_table)
}
require(found_sites == expected_sites,
        "code generation must retain exactly six reviewed packet completions")

source_table = source[
    source.index("kBillboardPacketSites"):
    source.index("constexpr uint32_t kRamFirst")
]
for pc, word in expected_sites.items():
    require(f"0x{pc:08X}u" in source_table and
            f"0x{word:08X}u" in source_table,
            f"host hook table is missing 0x{pc:08X}")
    require(f"0x{pc:08X}u" in dirty and f"0x{word:08X}u" in dirty,
            f"dirty interpreter is missing 0x{pc:08X}")

require("disruptor_billboard_aspect_instruction_hook(cpu" in codegen and
        '"\\n{}disruptor_billboard_aspect_instruction_hook(cpu, "' in codegen,
        "compiled hooks must be separate post-instruction statements")
require("#ifdef PSX_HAS_DISRUPTOR_BILLBOARD_ASPECT" in dirty and
        "disruptor_billboard_aspect_instruction_hook(cpu, pc, insn, 1)" in dirty,
        "dirty-interpreter parity must be project-gated and post-instruction")
require("PSX_HAS_DISRUPTOR_BILLBOARD_ASPECT=1" in cmake and
        "src/disruptor_billboard_aspect.cpp" in cmake and
        "tests/disruptor_billboard_aspect_test.cpp" in cmake,
        "runtime source, dirty seam, and focused behavior test must be enabled")

require("gpu_ws_tag_primitive" in gpu_h and
        "void gpu_ws_tag_primitive" in gpu and
        "if (!ws_active() || !cpu || !cpu->read_word) return" in gpu and
        "gpu_ws_tag_primitive(cpu, packet, anchor_x)" in source,
        "audited packets must reuse the active classic-squash tag table")
for token in ("content_validated", "psx_ws_tag_match_result",
              "tag->key = 0"):
    require(token in gpu,
            f"packet-reuse-safe provenance is missing {token}")
for token in ("now - stamp > 2u", "PSX_WS_TAG_CONTENT_MISMATCH",
              "psx_ws_ft4_signature_words"):
    require(token in tag_match_h,
            f"packet-reuse matching contract is missing {token}")
require("cpu->write_" not in source and "psx_mod_write" not in source,
        "billboard repair must never mutate guest RAM")
for token in ("packet + 8u", "packet + 16u", "packet + 24u", "packet + 32u",
              "x0 != x2", "x1 != x3", "std::min(x0, x1)",
              "std::max(x0, x1)", "left + width / 2"):
    require(token in source, f"completed FT4 guard is missing {token}")
require("x1 < x0" not in source,
        "mirrored FT4 winding must not be rejected")
require("g_ls_mode != 0 || g_ls_replay_active != 0" in source,
        "lockstep record and replay halves must not mutate host tags")
require("0x800433A0u, 0x02A02821u, 5u" in source and
        "cpu->gpr[packet_gpr]" in source,
        "deferred actor pass must use its live $a1 packet register")
require("disruptor_billboard_aspect_instruction_hook" in header,
        "the exact-instruction hook must remain a declared C ABI")
require("BILLBOARD_ASPECT_SITES" in audit and
        "disruptor_billboard_aspect_instruction_hook" in audit,
        "generated shards must audit the exact billboard hook set")

print("billboard aspect source/codegen contract: PASS")
