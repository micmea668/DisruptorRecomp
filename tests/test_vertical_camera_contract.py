#!/usr/bin/env python3
"""Source-only contract for Disruptor's version-pinned vertical camera."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"vertical camera contract: FAIL: {message}", file=sys.stderr)
        raise SystemExit(1)


camera = read("src/disruptor_vertical_camera.cpp")
camera_h = read("src/disruptor_vertical_camera.h")
mouse = read("src/disruptor_mouse_aim.cpp")
codegen = read("psxrecomp-overlay/recompiler/src/code_generator.cpp")
dirty = read("psxrecomp-overlay/runtime/src/dirty_ram_interp.c")
gpu = read("psxrecomp-overlay/runtime/src/gpu.c")
cmake = read("CMakeLists.txt")
configs = read("game.toml") + read("game-widescreen.toml")

require("0x80040E68u" in camera and
        "disruptor.vertical_camera.renderer" in camera,
        "renderer entry must stay pinned to the audited game function")
require(configs.count('mod_function_entry_funcs = ["0x80020DD8", "0x80040E68"]') == 2,
        "both game configs must emit the renderer function-entry hook")

expected_sites = {
    0x8003AD54: 0x34020078, 0x8003ADE4: 0x34020078,
    0x8003B140: 0x34020078, 0x8003B1EC: 0x34020078,
    0x8003B764: 0x34020078, 0x8003B990: 0x34020078,
    0x8003BDA4: 0x34020078, 0x8003C4B4: 0x34020078,
    0x8003C99C: 0x34020078, 0x8003D07C: 0x34020078,
    0x8003D398: 0x34030078, 0x8003F6B0: 0x34020078,
    0x8003B900: 0x0205102A, 0x8003B90C: 0x0202102A,
    0x8003BD14: 0x0206102A, 0x8003BD20: 0x0202102A,
    0x8003CDB8: 0x0065102A, 0x8003CDC4: 0x0062102A,
    0x8002E4B8: 0x92640022, 0x8002EAF0: 0x92430020,
    0x8004279C: 0x8FBF016C,
}
codegen_table = codegen[
    codegen.index("kDisruptorVerticalCameraSites"):
    codegen.index("struct DisruptorFarRenderingSite")
]
found_sites = {
    int(pc, 16): int(word, 16)
    for pc, word in re.findall(
        r"\{0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u\}", codegen_table)
}
require(found_sites == expected_sites,
        "code generation must retain exactly the 21 reviewed PC/opcode sites")
require("disruptor_vertical_camera_instruction_hook(cpu" in codegen and
        "addr, instr);" not in codegen_table,
        "generated sites must call the guarded camera dispatcher")

require("#ifdef PSX_HAS_DISRUPTOR_VERTICAL_CAMERA" in dirty and
        "disruptor_vertical_camera_instruction_hook(cpu, pc, insn, 1)" in dirty,
        "dirty interpreter parity must be project-gated and post-instruction")
require("PSX_HAS_DISRUPTOR_VERTICAL_CAMERA=1" in cmake,
        "the Disruptor target must opt into dirty-interpreter camera hooks")

require("g_ls_mode != 0 || g_ls_replay_active != 0" in camera,
        "record and replay halves of the lockstep comparator must be neutral")
projectile = camera[camera.index("void adjust_projectile"):
                    camera.index("PSX_MOD_CONSTRUCTOR")]
require(projectile.index("if (projectile_slope == 0.0) return") <
        projectile.index("plausible_live_gameplay()"),
        "zero pitch must not add diagnostic guest-memory reads")
require("psx_mod_read_word(kAimTargetPointer) != 0u" in projectile,
        "native target-assisted aim must remain authoritative")

require("disruptor_vertical_camera_input_allowed()" in mouse,
        "mouse Y must not accumulate during unsupported game states")
require("gpu_geometry_camera_projection_center_y_set" in camera and
        "gpu_geometry_camera_projection_center_y_set" in gpu,
        "guest and presentation horizons must share one renderer setter")
require("disruptor-vertical-camera-test" in cmake and
        "disruptor_vertical_look_registration" in cmake,
        "behavior and vertical-only registration tests must remain enabled")
require("disruptor_vertical_camera_instruction_hook" in camera_h,
        "the exact-instruction dispatcher must remain a declared C ABI")

print("vertical camera source/codegen contract: PASS")
