#!/usr/bin/env python3
"""Source-only contract for the host-side Disruptor developer menu."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;{{]*\)\s*\{{", source)
    require(match is not None, f"missing function {name}")
    start = match.end()
    depth = 1
    index = start
    while index < len(source) and depth:
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
        index += 1
    require(depth == 0, f"unterminated function {name}")
    return source[start : index - 1]


cmake = read("CMakeLists.txt")
manifest = read("PSXRECOMP_OVERLAY_FILES.txt")
host_ui = read("psxrecomp-overlay/runtime/include/host_ui.h")
main_cpp = read("psxrecomp-overlay/runtime/src/main.cpp")
gl = read("psxrecomp-overlay/runtime/src/gpu_gl_renderer.c")
menu = read("src/disruptor_dev_menu.cpp")
mouse = read("src/disruptor_mouse_aim.cpp")

require("option(DISRUPTOR_DEV_MENU" in cmake,
        "developer menu must remain independently build-selectable")
require("v1.92.6.tar.gz" in cmake and
        "5b17c01f69545bde732b14936d89ce0f508adb83e8b56fa82448371845172bc3"
        in cmake.lower(),
        "Dear ImGui source must remain versioned and integrity-pinned")
require("src/disruptor_dev_menu.cpp" in cmake and
        "target_link_libraries(psx-runtime PRIVATE disruptor-imgui)" in cmake,
        "menu implementation must be linked only into the game runtime")
require("+runtime/include/host_ui.h" in manifest,
        "new host UI ABI header must remain reviewed in the overlay manifest")

for token in (
    "PSX_HOST_UI_ABI_VERSION",
    "PSX_HOST_UI_CAPTURE_KEYBOARD",
    "PSX_HOST_UI_CAPTURE_MOUSE",
    "PSX_HOST_UI_CAPTURE_GAMEPAD",
    "PSX_HOST_UI_SUSPEND_INTERPOLATION",
    "PSX_HOST_UI_VISIBLE",
    "psx_host_video_set_vsync",
    "psx_host_video_set_interpolation",
):
    require(token in host_ui, f"host UI ABI is missing {token}")

vblank_start = main_cpp.index("static void sdl_vblank_present")
event_start = main_cpp.index("while (SDL_PollEvent(&ev))", vblank_start)
event_end = main_cpp.index("SDL_GameControllerUpdate", event_start)
event_body = main_cpp[event_start:event_end]
require("psx_host_ui_handle_event(&ev)" in event_body,
        "SDL events must reach the menu before runtime hotkeys")
require(event_body.index("psx_host_ui_handle_event(&ev)") <
        event_body.index("key >= SDLK_F1"),
        "menu event consumption must precede save-state hotkeys")
require("psx_host_ui_capture_flags()" in
        function_body(main_cpp, "sample_pad_into_sio"),
        "polled keyboard/gamepad input must be neutralised while the menu is open")
require("host_ui_runtime_shutdown();" in
        function_body(main_cpp, "shutdown_runtime"),
        "menu GL resources must be destroyed before renderer/window teardown")

for present_name in (
    "gl_renderer_present",
    "gl_renderer_present_blank",
    "gl_renderer_present_vram",
    "gl_renderer_present_wide_fbo",
):
    body = function_body(gl, present_name)
    require("psx_host_ui_render_gl" in body and "SDL_GL_SwapWindow" in body,
            f"{present_name} must render the host UI before swapping")
    require(body.index("psx_host_ui_render_gl") <
            body.index("SDL_GL_SwapWindow"),
            f"{present_name} renders the menu after SwapWindow")

require("s_interp_content_suspended" in gl and
        "s_interp_host_ui_suspended" in gl,
        "FMV and menu interpolation suspension reasons must remain independent")

interpolation_setter = function_body(main_cpp,
                                     "psx_host_video_set_interpolation")
require(interpolation_setter.count("gl_renderer_interpolation_diag") >= 2 and
        "actual != enabled" in interpolation_setter and
        "return 0" in interpolation_setter,
        "live interpolation must report renderer setup failure")

for token in (
    "SDL_SCANCODE_GRAVE",
    "SDL_SCANCODE_ESCAPE",
    'BeginTabItem("Controls")',
    'BeginTabItem("Enhancements")',
    'BeginTabItem("Diagnostics")',
    'BeginTabItem("System")',
    "gpu_geometry_correction_set",
    "gpu_texture_correction_set",
    "psx_host_video_set_interpolation",
    "psx_host_video_set_vsync",
    "gpu_geometry_correction_stats_detailed",
):
    require(token in menu, f"developer menu is missing {token}")

for forbidden in (
    "psx_write_byte",
    "psx_write_word",
    "psx_mod_set_native_vblank_rate",
    "PSX_FORCE_INTERP",
):
    require(forbidden not in menu,
            f"normal developer menu must not expose unsafe guest control {forbidden}")

flags_body = function_body(menu, "current_flags")
for flag in (
    "PSX_HOST_UI_CAPTURE_KEYBOARD",
    "PSX_HOST_UI_CAPTURE_MOUSE",
    "PSX_HOST_UI_CAPTURE_GAMEPAD",
    "PSX_HOST_UI_SUSPEND_INTERPOLATION",
    "PSX_HOST_UI_VISIBLE",
):
    require(flag in flags_body, f"visible menu must request {flag}")

require("psx_host_ui_game_input_captured()" in
        function_body(mouse, "mouse_aim_frame"),
        "mouse mod must cooperate with host menu input capture")

print("developer menu contract: PASS")
