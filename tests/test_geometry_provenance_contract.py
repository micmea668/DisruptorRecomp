#!/usr/bin/env python3
"""Regression audit for Test 12's presentation-geometry provenance gate."""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
GPU = ROOT / "psxrecomp-overlay" / "runtime" / "src" / "gpu.c"
GTE = ROOT / "psxrecomp-overlay" / "runtime" / "src" / "gte.cpp"
MEMORY = ROOT / "psxrecomp-overlay" / "runtime" / "src" / "memory.c"
DIRTY_INTERP = (
    ROOT / "psxrecomp-overlay" / "runtime" / "src" / "dirty_ram_interp.c"
)
GTE_HEADER = (
    ROOT / "psxrecomp-overlay" / "runtime" / "include" / "gte_precision.h"
)
MOUSE = ROOT / "src" / "disruptor_mouse_aim.cpp"
MAIN = ROOT / "psxrecomp-overlay" / "runtime" / "src" / "main.cpp"
CODEGEN = (
    ROOT / "psxrecomp-overlay" / "recompiler" / "src" / "code_generator.cpp"
)
GAME_CODE = ROOT / "input" / "SLUS_002.24.code"

EXPECTED_RAM_COPY_ROUTES = (
    (0x80046C4C, 0x8D0C0004, 0x80046D04, 0xAF2C0008, 12),
    (0x80046C50, 0x8D2D0004, 0x80046D08, 0xAF2D0014, 13),
    (0x80046C54, 0x8D4E0004, 0x80046D10, 0xAF2E002C, 14),
    (0x80046C60, 0x8D6F0004, 0x80046D0C, 0xAF2F0020, 15),
)

EXPECTED_SCRATCH_COPY_ROUTES = (
    (0x80047A80, 0x8CA80084, 0x80047A90, 0xAF280008, 8),
    (0x80047A84, 0x8CA900A4, 0x80047A94, 0xAF290014, 9),
    (0x80047A88, 0x8CAA00C4, 0x80047A9C, 0xAF2A002C, 10),
    (0x80047A8C, 0x8CAB00BC, 0x80047A98, 0xAF2B0020, 11),
    (0x80047B40, 0x8CA800A4, 0x80047B50, 0xAF280008, 8),
    (0x80047B44, 0x8CA9008C, 0x80047B54, 0xAF290014, 9),
    (0x80047B48, 0x8CAA00AC, 0x80047B5C, 0xAF2A002C, 10),
    (0x80047B4C, 0x8CAB00C4, 0x80047B58, 0xAF2B0020, 11),
    (0x80047C00, 0x8CA800C4, 0x80047C10, 0xAF280008, 8),
    (0x80047C04, 0x8CA900AC, 0x80047C14, 0xAF290014, 9),
    (0x80047C08, 0x8CAA0094, 0x80047C1C, 0xAF2A002C, 10),
    (0x80047C0C, 0x8CAB00B4, 0x80047C18, 0xAF2B0020, 11),
    (0x80047CC4, 0x8CA800BC, 0x80047CD4, 0xAF280008, 8),
    (0x80047CC8, 0x8CA900C4, 0x80047CD8, 0xAF290014, 9),
    (0x80047CCC, 0x8CAA00B4, 0x80047CE0, 0xAF2A002C, 10),
    (0x80047CD0, 0x8CAB009C, 0x80047CDC, 0xAF2B0020, 11),
    (0x80047D6C, 0x8CA80084, 0x80047D7C, 0xAF280008, 8),
    (0x80047D70, 0x8CA9008C, 0x80047D80, 0xAF290014, 9),
    (0x80047D74, 0x8CAA0094, 0x80047D88, 0xAF2A002C, 10),
    (0x80047D78, 0x8CAB009C, 0x80047D84, 0xAF2B0020, 11),
)

EXPECTED_COPY_ROUTES = EXPECTED_RAM_COPY_ROUTES + EXPECTED_SCRATCH_COPY_ROUTES
EXPECTED_SCRATCH_STORE = (
    0x80047A0C, 0xACAA0004, 14, 0x1F800084, 8, 9
)


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.S)
    if not match:
        raise AssertionError(f"missing function: {name}")
    start = source.index("{", match.start())
    depth = 0
    for pos in range(start, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1 : pos]
    raise AssertionError(f"unterminated function: {name}")


def block_body_after(source: str, marker: str) -> str:
    marker_pos = source.index(marker)
    start = source.index("{", marker_pos + len(marker))
    depth = 0
    for pos in range(start, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1 : pos]
    raise AssertionError(f"unterminated block after: {marker}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    source = GPU.read_text(encoding="utf-8")
    gte_source = GTE.read_text(encoding="utf-8")
    memory_source = MEMORY.read_text(encoding="utf-8")
    dirty_interp_source = DIRTY_INTERP.read_text(encoding="utf-8")
    gte_header = GTE_HEADER.read_text(encoding="utf-8")
    mouse_source = MOUSE.read_text(encoding="utf-8")
    main_source = MAIN.read_text(encoding="utf-8")
    codegen_source = CODEGEN.read_text(encoding="utf-8")

    resolve = function_body(source, "resolve_precise_vertices")
    require("gte_precision_load_word" in resolve,
            "precise vertices must use packet-address provenance")
    require("gp0_cmd_source_addr == 0xFFFFFFFFu" in resolve,
            "commands without a source address must be rejected")
    require("gte_geometry_correction_lookup" not in resolve,
            "rounded packed-SXY fallback must never return")
    require("gte_geometry_correction_lookup" not in source,
            "gpu.c must not call or declare the ambiguous cache lookup")

    legacy_lookup = function_body(gte_source,
                                  "gte_geometry_correction_lookup")
    require("return 0;" in legacy_lookup and
            "s_geom_cache" not in legacy_lookup,
            "legacy packed-SXY ABI must remain hard-disabled")
    require("geom_note" not in gte_source and
            "GEOM_CACHE_SIZE" not in gte_source and
            "s_geom_cache" not in gte_source,
            "rounded packed-SXY storage must not exist")

    # A syntactically RAM-looking SWC2 address is not proof that memory.c
    # accepted the write (IsC, lockstep replay, and narrow runtime suppressions
    # all discard writes).  Provenance therefore consumes a one-write token
    # which only the completed main-RAM path may arm.
    write_word = function_body(memory_source, "psx_write_word")
    flush_pos = write_word.index("if (g_overlay_flush_pending_cycles)")
    begin_pos = write_word.index("gte_precision_word_write_begin()")
    replay_pos = write_word.index("g_ls_mode == 2")
    require(flush_pos < begin_pos < replay_pos,
            "each word attempt must clear commit proof after cycle flush and "
            "before replay can discard it")

    write_raw = function_body(memory_source, "psx_write_word_raw")
    suppress_pos = write_raw.index("g_debug_last_store_pc == 0xBFC117E4u")
    invalidate_pos = write_raw.index("gte_precision_invalidate_word(addr)")
    ram_write_pos = write_raw.index("ram[phys + 3]")
    commit_pos = write_raw.index(
        "gte_precision_main_ram_word_committed(phys)")
    require(suppress_pos < invalidate_pos < ram_write_pos < commit_pos,
            "only the completed main-RAM word path may invalidate and then "
            "publish commit proof")
    require(write_raw.count("gte_precision_main_ram_word_committed") == 1,
            "non-RAM and discarded writes must never arm commit proof")
    scratch_region_pos = write_raw.index(
        "if (phys >= 0x1F800000u && phys <= 0x1F8003FFu)")
    scratch_invalidate_pos = write_raw.index(
        "gte_precision_invalidate_word(addr)", scratch_region_pos)
    scratch_write_pos = write_raw.index("scratchpad[off + 3]", scratch_region_pos)
    scratch_commit_pos = write_raw.index(
        "gte_precision_scratch_word_committed(phys)", scratch_region_pos)
    require(scratch_region_pos < scratch_invalidate_pos < scratch_write_pos <
            scratch_commit_pos,
            "scratch provenance must be invalidated before replacement bytes "
            "and armed only after all four bytes commit")
    require(write_raw.count("gte_precision_scratch_word_committed") == 1,
            "only the completed scratchpad word path may arm scratch proof")
    require("gte_precision_store_pc_word" not in write_raw,
            "generic RAM/DMA/HLE writes must not inherit a sticky guest store PC")

    store_word = function_body(gte_source, "gte_precision_store_word")
    consume_pos = store_word.index("s_precision_word_commit.pending = 0")
    speculative_pos = store_word.index("s_speculative_depth != 0")
    reject_pos = store_word.index("store_uncommitted_rejections")
    cache_pos = store_word.index("precision_store_projection")
    require(consume_pos < speculative_pos < reject_pos < cache_pos,
            "SWC2 commit proof must be consumed before every early return and "
            "validated before caching")
    require("committed_physical == physical" in store_word,
            "commit proof must match the exact canonical RAM word")
    require("#ifdef PSX_GTE_REGISTER_TEST" in store_word and
            "if (!committed) commit_matches = 1" in store_word,
            "the pinned standalone GTE fixture needs its explicit test-only "
            "direct-store seam")
    require("store_uncommitted_rejections" in gte_header and
            "provenance_store_uncommitted_rejections" in mouse_source,
            "uncommitted SWC2 rejection must remain visible in diagnostics")
    require('#include "gte_precision.h"' in dirty_interp_source,
            "the dirty-RAM SWC2 path must use the shared provenance API")

    registered_store = function_body(
        gte_source, "gte_precision_store_pc_word")
    route_pos = registered_store.index(
        "precision_store_pc_route(store_pc, instruction)")
    consume_pos = registered_store.index(
        "s_precision_word_commit.pending = 0")
    commit_match_pos = registered_store.index(
        "committed_physical != physical")
    packed_pos = registered_store.index("projection.packed != packed")
    cache_pos = registered_store.index("precision_store_projection")
    require(route_pos < consume_pos < commit_match_pos < packed_pos < cache_pos,
            "reviewed MFC2/SW routes must consume exact commit proof and reject "
            "changed packed coordinates before caching")
    require("if (!route) return" in registered_store,
            "ordinary SWs outside the reviewed route list must remain inert")
    require("s_precision_mfc2_capture[index]" in registered_store and
            "s_precise_sxy[index]" not in registered_store,
            "ordinary SW must use the projection captured by its reviewed MFC2")
    require("registered_store_attempts" in gte_header and
            "provenance_registered_store_accepts" in mouse_source,
            "reviewed MFC2/SW coverage must remain visible in diagnostics")

    require("gte_precision_scratch_word_committed" in gte_header and
            "gte_precision_scratch_store_pc_route_add" in gte_header and
            "gte_precision_scratch_store_pc_word" in gte_header,
            "the bounded scratch commit/store API must remain public")
    scratch_store = function_body(
        gte_source, "gte_precision_scratch_store_pc_word")
    scratch_route_pos = scratch_store.index(
        "precision_scratch_store_pc_route(store_pc, instruction)")
    scratch_consume_pos = scratch_store.index(
        "s_precision_word_commit.pending = 0")
    scratch_spec_pos = scratch_store.index("s_speculative_depth != 0")
    scratch_address_pos = scratch_store.index("precision_scratch_address")
    scratch_slot_pos = scratch_store.index("precision_scratch_route_contains")
    scratch_domain_pos = scratch_store.index(
        "committed_domain != PRECISION_WORD_DOMAIN_SCRATCHPAD")
    scratch_commit_match_pos = scratch_store.index(
        "committed_physical != physical")
    scratch_packed_pos = scratch_store.index("projection.packed != packed")
    scratch_cache_pos = scratch_store.index("precision_store_projection")
    require(scratch_route_pos < scratch_consume_pos < scratch_spec_pos <
            scratch_address_pos < scratch_slot_pos < scratch_domain_pos <
            scratch_commit_match_pos < scratch_packed_pos < scratch_cache_pos,
            "scratch store must consume proof, enforce its exact slot/domain/"
            "word and only then cache the captured projection")
    require("PRECISION_WORD_DOMAIN_MAIN_RAM" not in scratch_store and
            "s_precision_mfc2_capture[index]" in scratch_store,
            "scratch stores must neither alias main RAM nor sample live GTE state")

    captured_mfc2 = function_body(
        gte_source, "gte_precision_mfc2_pc_read")
    require("precision_mfc2_pc_route(mfc2_pc, instruction)" in captured_mfc2 and
            "s_precision_mfc2_capture[index]" in captured_mfc2 and
            "projection.packed == packed" in captured_mfc2,
            "reviewed MFC2 must snapshot the exact projection read by the game")

    require("gte_precision_mfc2_pc_read(0x{:08X}u" in codegen_source and
            "gte_precision_store_pc_word(0x{:08X}u" in codegen_source,
            "resident codegen must emit literal-PC capture and post-write hooks")
    copy_load = function_body(gte_source, "gte_precision_copy_pc_read")
    copy_load_route_pos = copy_load.index("precision_copy_load_pc_route(")
    copy_load_clear_pos = copy_load.index("route->capture.valid = 0")
    copy_load_lookup_pos = copy_load.index(
        "s_precision_store[precision_hash(physical)]")
    copy_load_capture_pos = copy_load.index("route->capture = entry.projection")
    require(copy_load_route_pos < copy_load_clear_pos < copy_load_lookup_pos <
            copy_load_capture_pos,
            "reviewed packet-copy LW must clear stale state then snapshot only "
            "the exact proven source word")
    require("entry.addr != physical" in copy_load and
            "entry.projection.packed != packed" in copy_load,
            "packet-copy LW must match the exact RAM address and packed word")
    require("precision_copy_source_address(addr, &physical)" in copy_load and
            "precision_ram_address(addr, &physical)" not in copy_load,
            "reviewed copy LW alone may resolve RAM or scratchpad sources")

    generic_lookup = function_body(gte_source, "precision_lookup_projection")
    require("precision_ram_address(addr, &physical)" in generic_lookup and
            "precision_copy_source_address" not in generic_lookup and
            "precision_scratch_address" not in generic_lookup,
            "generic GPU lookup must remain RAM-only; scratch is copy-route-only")

    timeline = function_body(gte_source, "gte_precision_timeline_invalidate")
    require("s_precision_mfc2_capture" in timeline and
            "precision_copy_captures_clear()" in timeline and
            "s_precision_word_commit.pending = 0" in timeline and
            "gte_precision_generation_advance()" in timeline,
            "timeline restore must clear scratch source, capture and commit state")

    copy_store = function_body(gte_source, "gte_precision_copy_pc_word")
    copy_store_route_pos = copy_store.index("precision_copy_store_pc_route(")
    copy_store_consume_pos = copy_store.index(
        "s_precision_word_commit.pending = 0")
    copy_store_capture_pos = copy_store.index(
        "const PreciseProjection projection = route->capture")
    copy_store_clear_pos = copy_store.index("route->capture.valid = 0")
    copy_store_commit_pos = copy_store.index(
        "committed_physical != physical")
    copy_store_packed_pos = copy_store.index("projection.packed != packed")
    copy_store_cache_pos = copy_store.index("precision_store_projection")
    require(copy_store_route_pos < copy_store_consume_pos <
            copy_store_capture_pos < copy_store_clear_pos <
            copy_store_commit_pos < copy_store_packed_pos <
            copy_store_cache_pos,
            "reviewed packet-copy SW must consume exact commit proof and its "
            "one-shot LW snapshot before publishing destination provenance")
    require("if (!route) return" in copy_store and
            "s_precise_sxy" not in copy_store,
            "unreviewed packet stores must remain inert and may not guess from "
            "current GTE state")
    require("copy_load_attempts" in gte_header and
            "copy_store_accepts" in gte_header and
            "copy_store_packed_rejections" in gte_header,
            "packet-copy coverage and rejection counts must be diagnosable")

    codegen_copy_arrays = {}
    for array_name in ("kDisruptorPrecisionCopyLoads",
                       "kDisruptorPrecisionCopyStores"):
        array = re.search(
            rf"{array_name}\[\]\s*=\s*\{{(.*?)\}};",
            codegen_source, re.S)
        require(array is not None,
                f"resident codegen must declare exact {array_name} sites")
        codegen_copy_arrays[array_name] = tuple(
            (int(pc, 16), int(word, 16), int(gpr))
            for pc, word, gpr in re.findall(
                r"\{0x([0-9A-Fa-f]{8})u,\s*0x([0-9A-Fa-f]{8})u,\s*"
                r"(\d+)u\}", array.group(1))
        )
    require(codegen_copy_arrays["kDisruptorPrecisionCopyLoads"] == tuple(
                (load_pc, load_word, gpr)
                for load_pc, load_word, _, _, gpr in EXPECTED_COPY_ROUTES
            ) and
            codegen_copy_arrays["kDisruptorPrecisionCopyStores"] == tuple(
                (store_pc, store_word, gpr)
                for _, _, store_pc, store_word, gpr in EXPECTED_COPY_ROUTES
            ),
            "resident codegen packet-copy identities must exactly match the "
            "ordered four main-RAM plus twenty scratchpad pairs")

    translate = function_body(codegen_source,
                              "CodeGenerator::translate_instruction")
    require("gte_precision_copy_pc_read(" in translate and
            "gte_precision_copy_pc_word(" in translate and
            "gte_precision_scratch_store_pc_word(" in translate,
            "resident codegen must emit reviewed packet-copy hooks")
    require(translate.index("{} = psx_cyc_load_word(cpu, "
                            "_precision_copy_addr") <
            translate.index("{} = gte_precision_copy_pc_read("),
            "resident packet-copy callback must run after the exact LW")
    require(translate.index("cpu->write_word(_precision_copy_addr") <
            translate.index("gte_precision_copy_pc_word(0x{:08X}u"),
            "resident packet-copy callback must run after the exact SW")
    require(translate.index("translate_sw(instr)") <
            translate.index('" gte_precision_scratch_store_pc_word("'),
            "resident scratch callback must be appended after the exact SW")
    require("uint32_t _precision_copy_addr" in translate and
            "uint32_t _precision_copy_value" in translate,
            "resident SW must snapshot its address and value across the write")

    dirty_sw = dirty_interp_source.index("case 0x2B: { /* SW */")
    dirty_swc2 = dirty_interp_source.index("case 0x3A: { /* SWC2 */")
    dirty_sw_body = dirty_interp_source[dirty_sw:dirty_swc2]
    require(dirty_sw_body.index("cpu->write_word(addr, cpu->gpr[rt])") <
            dirty_sw_body.index("gte_precision_scratch_store_pc_word("),
            "dirty interpreter must publish scratch provenance after SW")
    require(dirty_sw_body.index("cpu->write_word(addr, cpu->gpr[rt])") <
            dirty_sw_body.index("gte_precision_copy_pc_word("),
            "dirty interpreter must publish packet provenance after SW")
    require(dirty_sw_body.index("cpu->write_word(addr, cpu->gpr[rt])") <
            dirty_sw_body.index("gte_precision_store_pc_word(pc, insn, addr"),
            "dirty interpreter must publish a literal-PC callback after SW")
    require("gte_precision_mfc2_pc_read(" in dirty_interp_source,
            "dirty interpreter must snapshot reviewed MFC2 reads")
    dirty_lw = dirty_interp_source.index("case 0x23: { /* LW */")
    dirty_lbu = dirty_interp_source.index("case 0x24: { /* LBU */")
    dirty_lw_body = dirty_interp_source[dirty_lw:dirty_lbu]
    require(dirty_lw_body.index("psx_cyc_load_word(cpu, addr") <
            dirty_lw_body.index("gte_precision_copy_pc_read("),
            "dirty interpreter must snapshot packet provenance after LW")

    for replay_name in ("ls_replay_and_compare", "lsf_replay_and_compare"):
        replay = function_body(dirty_interp_source, replay_name)
        require("gte_replay_side_effects_begin()" in replay and
                "gte_replay_side_effects_end()" in replay,
                f"{replay_name} must sandbox replayed GTE projections")

    route_config = function_body(
        main_source, "configure_disruptor_precision_store_routes")
    require('game_id != "SLUS-00224"' in route_config,
            "reviewed resident routes must be gated to Disruptor's serial")
    mfc2_array = re.search(
        r"mfc2_pcs\[\]\s*=\s*\{(.*?)\};", route_config, re.S)
    store_array = re.search(
        r"store_pcs\[\]\s*=\s*\{(.*?)\};", route_config, re.S)
    copy_array = re.search(
        r"copy_routes\[\]\s*=\s*\{(.*?)\};", route_config, re.S)
    scratch_route = re.search(
        r"gte_precision_scratch_store_pc_route_add\(\s*"
        r"0x([0-9A-Fa-f]{8})u,\s*0x([0-9A-Fa-f]{8})u,\s*(\d+)u,\s*"
        r"0x([0-9A-Fa-f]{8})u,\s*(\d+)u,\s*(\d+)u\)",
        route_config, re.S)
    require(mfc2_array is not None and store_array is not None and
            copy_array is not None and scratch_route is not None,
            "reviewed capture/store/scratch/copy routes must be explicit")
    configured_mfc2_pcs = {
        int(value, 16)
        for value in re.findall(r"0x([0-9A-Fa-f]{8})u", mfc2_array.group(1))
    }
    configured_store_pcs = {
        int(value, 16)
        for value in re.findall(r"0x([0-9A-Fa-f]{8})u", store_array.group(1))
    }
    expected_mfc2_pcs = {0x8004633C, 0x8004795C}
    expected_pcs = {
        0x8004641C, 0x800464BC, 0x80046524, 0x80046540,
        0x80046594, 0x800465D8,
    }
    configured_copy_routes = tuple(
        (int(load_pc, 16), int(load_word, 16),
         int(store_pc, 16), int(store_word, 16), int(gpr))
        for load_pc, load_word, store_pc, store_word, gpr in re.findall(
            r"\{0x([0-9A-Fa-f]{8})u,\s*0x([0-9A-Fa-f]{8})u,\s*"
            r"0x([0-9A-Fa-f]{8})u,\s*0x([0-9A-Fa-f]{8})u,\s*"
            r"(\d+)u\}", copy_array.group(1))
    )
    configured_scratch_route = (
        int(scratch_route.group(1), 16),
        int(scratch_route.group(2), 16),
        int(scratch_route.group(3)),
        int(scratch_route.group(4), 16),
        int(scratch_route.group(5)),
        int(scratch_route.group(6)),
    )
    require(configured_mfc2_pcs == expected_mfc2_pcs,
            "the MFC2 capture list must exactly match the audited SXY2 reads")
    require(configured_store_pcs == expected_pcs,
            "the registered route list must exactly match the audited SXY2 SWs")
    require(configured_scratch_route == EXPECTED_SCRATCH_STORE,
            "scratch provenance must accept exactly nine stride-eight words "
            "starting at 0x1F800084 from the audited SW")
    require(configured_copy_routes == EXPECTED_COPY_ROUTES,
            "packet-copy routes must preserve the exact audited 4+20 order")
    require("pc, 0x480A7000u, 14u" in route_config and
            "pc, 0xACAA0004u, 14u" in route_config,
            "reviewed routes must pin raw MFC2/SW words and SXY2")

    # Pin the retail-derived instruction identities without embedding or
    # publishing any additional game data.  The six early stores preserve the
    # MFC2 word; the final store is a bounded unpack/repack whose changed values
    # are rejected by the runtime packed-word check above.
    if GAME_CODE.exists():
        code = GAME_CODE.read_bytes()
        load_address = 0x80011200
        psx_exe_header_size = 0x800

        def game_word(address: int) -> int:
            offset = psx_exe_header_size + address - load_address
            require(0 <= offset <= len(code) - 4,
                    f"audited instruction 0x{address:08X} is outside game image")
            return int.from_bytes(code[offset:offset + 4], "little")

        for pc in expected_mfc2_pcs:
            require(game_word(pc) == 0x480A7000,
                    f"audited MFC2 0x{pc:08X} must remain MFC2 r10,SXY2")
        for pc in expected_pcs | {EXPECTED_SCRATCH_STORE[0]}:
            require(game_word(pc) == 0xACAA0004,
                    f"reviewed route 0x{pc:08X} must remain SW r10,4(r5)")
        for load_pc, load_word, store_pc, store_word, _ in EXPECTED_COPY_ROUTES:
            require(game_word(load_pc) == load_word,
                    f"reviewed packet-copy LW changed at 0x{load_pc:08X}")
            require(game_word(store_pc) == store_word,
                    f"reviewed packet-copy SW changed at 0x{store_pc:08X}")

    queue = function_body(source, "queue_precise_triangle")
    reject_pos = queue.index("gr_set_world_triangle(0)")
    accept_pos = queue.index("gr_set_world_triangle(1)")
    gate_pos = queue.index("!exact")
    require(reject_pos < gate_pos < accept_pos,
            "fractional yaw must activate only after exact provenance")

    triangle = function_body(source, "prepare_precise_triangle")
    require("resolve_precise_vertices" in triangle and
            "queue_precise_triangle" in triangle,
            "triangles must resolve exactly before queueing metadata")

    quad_names = (
        "gp0_exec_mono_quad",
        "gp0_exec_shaded_quad",
        "gp0_exec_textured_quad",
        "gp0_exec_shaded_textured_quad",
    )
    for name in quad_names:
        body = function_body(source, name)
        require(body.count("prepare_precise_quad_vertices") == 1,
                f"{name} must preflight all four vertices exactly once")
        require(body.count("queue_precise_triangle(precise_quad") == 2,
                f"{name} must give both raster triangles the same verdict")
        require("prepare_precise_triangle(" not in body,
                f"{name} must not fall back to per-triangle acceptance")

    geometry_set = function_body(source, "gpu_geometry_correction_set")
    texture_set = function_body(source, "gpu_texture_correction_set")
    require("s_texture_correction_enabled" in geometry_set,
            "geometry toggle must preserve texture provenance tracking")
    require("ws_geometry_correction" in texture_set,
            "texture toggle must preserve geometry provenance tracking")

    sync_target = function_body(source, "ws_nw_sync_target")
    sync_target_code = re.sub(
        r"/\*.*?\*/|//[^\n]*", "", sync_target, flags=re.S)
    require(re.search(
                r"\bvisual_only\s*=\s*ws_geometry_correction\s*;",
                sync_target_code) is not None,
            "exact geometry must provision its visual-only mirror at 4:3")
    require(re.search(r"\bws_engaged\s*\(", sync_target_code) is None,
            "the 4:3 correction mirror must not depend on widescreen engagement")
    require("!native_wide && !visual_only" in sync_target_code and
            "gr_wide_configure" in sync_target_code and
            re.search(r"native_wide\s*\?\s*ws_nw_offset\(\)\s*:\s*0",
                      sync_target_code) is not None,
            "visual-only correction must use the same-width, zero-offset mirror")

    fill_rect = function_body(source, "gp0_exec_fill_rect")
    fill_rect_code = re.sub(
        r"/\*.*?\*/|//[^\n]*", "", fill_rect, flags=re.S)
    require("gr_wide_clear" in fill_rect_code and
            re.search(
                r"ws_native_wide_active\s*\(\s*\)\s*\|\|\s*"
                r"ws_geometry_correction",
                fill_rect_code) is not None,
            "canonical framebuffer clears must also clear the 4:3 correction mirror")
    require(re.search(
                r"ws_geometry_correction\s*&&\s*ws_engaged\s*\(",
                fill_rect_code) is None,
            "4:3 correction clears must not depend on widescreen engagement")
    require("ws_is_fb_base" in fill_rect_code,
            "mirror clears must remain limited to known display framebuffers")

    gl_fbo_start = main_source.index(
        "if (g_gl_active && g_gl_fbo_present && !di.depth24)")
    cpu_hires_pos = main_source.index(
        "gl_renderer_sync_cpu", gl_fbo_start)
    gl_fbo = main_source[gl_fbo_start:cpu_hires_pos]
    wide_branch = block_body_after(gl_fbo, "if (wide_present)")
    wide_fbo_pos = wide_branch.index("gl_renderer_present_wide_fbo")
    vram_fallback_pos = wide_branch.find(
        "gl_renderer_present_vram", wide_fbo_pos)
    fallback_return_pos = wide_branch.find("return;", vram_fallback_pos)
    require(vram_fallback_pos >= 0 and fallback_return_pos > vram_fallback_pos,
            "a failed GL correction/wide FBO present must fall back directly "
            "to the VRAM presenter")
    require(main_source.index("gl_renderer_present_wide_fbo", gl_fbo_start) <
            cpu_hires_pos,
            "the direct GL fallback must remain ahead of CPU-hires readback")

    print("geometry provenance contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"geometry provenance contract: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
