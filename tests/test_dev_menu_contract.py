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


def host_ui_gl_restore_helper(source: str) -> tuple[str, str, str, str]:
    definitions = re.finditer(
        r"\b(?:static\s+)?void\s+(\w+)\s*\(\s*int\s+(\w+)\s*,\s*"
        r"int\s+(\w+)\s*\)\s*\{",
        source,
    )
    candidates: list[tuple[str, str, str, str]] = []
    for definition in definitions:
        name, width, height = definition.groups()
        body = function_body(source, name)
        if "psx_host_ui_render_gl" in body:
            candidates.append((name, width, height, body))
    require(len(candidates) == 1,
            "host UI GL rendering must be centralised in one state-restoring helper")
    return candidates[0]


cmake = read("CMakeLists.txt")
manifest = read("PSXRECOMP_OVERLAY_FILES.txt")
host_ui = read("psxrecomp-overlay/runtime/include/host_ui.h")
main_cpp = read("psxrecomp-overlay/runtime/src/main.cpp")
gl = read("psxrecomp-overlay/runtime/src/gpu_gl_renderer.c")
menu = read("src/disruptor_dev_menu.cpp")
mouse = read("src/disruptor_mouse_aim.cpp")
config_h = read("psxrecomp-overlay/recompiler/src/config_loader.h")
config_cpp = read("psxrecomp-overlay/recompiler/src/config_loader.cpp")
run_ps1 = read("run.ps1")
run_sh = read("run.sh")

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
    "psx_host_user_settings_path",
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

host_ui_helper, drawable_width, drawable_height, host_ui_helper_body = (
    host_ui_gl_restore_helper(gl)
)
callback_pos = host_ui_helper_body.index("psx_host_ui_render_gl")
default_fbo = re.search(
    r"p_glBindFramebuffer\s*\(\s*PSXGL_(?:DRAW_)?FRAMEBUFFER\s*,\s*0\s*\)",
    host_ui_helper_body[callback_pos:],
)
drawable_viewport = re.search(
    rf"glViewport\s*\(\s*0\s*,\s*0\s*,\s*"
    rf"{re.escape(drawable_width)}\s*,\s*{re.escape(drawable_height)}\s*\)",
    host_ui_helper_body[callback_pos:],
)
require(default_fbo is not None and drawable_viewport is not None and
        default_fbo.start() < drawable_viewport.start(),
        "host UI helper must restore default FBO then the current drawable viewport")

for present_name in (
    "gl_renderer_present",
    "gl_renderer_present_blank",
    "gl_renderer_present_vram",
    "gl_renderer_present_wide_fbo",
):
    body = function_body(gl, present_name)
    helper_call = f"{host_ui_helper}("
    require(helper_call in body and "psx_host_ui_render_gl" not in body and
            "SDL_GL_SwapWindow" in body,
            f"{present_name} must use the state-restoring host UI helper")
    require(body.index(helper_call) <
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

for token in (
    "has_frame_interpolation_blend",
    "has_mouse_aim",
    "has_modern_controls",
    "has_horizontal_sensitivity",
    "has_invert_horizontal",
    "has_high_precision_camera",
    "has_geometry_correction",
    "has_perspective_textures",
):
    require(token in config_h and token in config_cpp,
            f"settings.toml schema is missing {token}")

save_body = function_body(config_cpp, "save_user_settings")
require("temporary" in save_body and
        "atomic_replace_user_settings" in save_body and
        "std::ofstream f(temporary" in save_body,
        "settings must be serialized to a sibling temporary file")
require("MoveFileExW" in config_cpp and "MOVEFILE_REPLACE_EXISTING" in config_cpp and
        "std::rename" in config_cpp,
        "settings publication must atomically replace on Windows and POSIX")

require("load_preferences_for_session();" in
        function_body(menu, "on_runtime_ready"),
        "preferences must load independently of opening the ImGui menu")
require("apply_modern_keybinds(disruptor_modern_controls_enabled() != 0)" in
        function_body(menu, "apply_saved_preferences"),
        "saved or environment-enabled modern controls must install conflict-free binds")
shutdown_body = function_body(menu, "on_runtime_shutdown")
require("flush_preferences();" in shutdown_body and
        shutdown_body.index("flush_preferences();") <
        shutdown_body.index("ImGui_ImplOpenGL3_Shutdown"),
        "dirty preferences must flush before UI/renderer teardown")
require("io.IniFilename = nullptr" in menu,
        "unreviewed ImGui layout state must remain outside persistence")
require("mark_interpolation_target" in menu and
        "mark_interpolation_blend" in menu and
        "mark_interpolation_enabled" not in menu,
        "blurry interpolation activation must remain session-only")
require("g_frame_interpolation = 0;" in main_cpp and
        "does not auto-restore it" in main_cpp,
        "legacy settings.toml must not auto-restore blurry interpolation")

main_settings = main_cpp.index("load_user_settings(settings_path)")
main_env = main_cpp.index('std::getenv("PSX_GEOMETRY_CORRECTION")')
require(main_settings < main_cpp.index("us.has_geometry_correction") < main_env and
        main_settings < main_cpp.index("us.has_perspective_textures") < main_env,
        "saved enhancements must load before explicit environment overrides")
require("UserSettings seed =" in main_cpp and
        "load_user_settings(" in
        main_cpp[main_cpp.index("UserSettings seed ="):main_cpp.index("seed.renderer =")],
        "a generic launcher save must merge rather than drop game-owned fields")

require("PSX_DISRUPTOR_MODERN_CONTROLS = if" not in run_ps1 and
        "PSX_TEXTURE_CORRECTION = if" not in run_ps1 and
        "$env:PSX_DISRUPTOR_MOUSE_AIM = '0'" not in run_ps1,
        "PowerShell must not synthesize disabling overrides for absent switches")
require("export PSX_DISRUPTOR_MODERN_CONTROLS=$MODERN_CONTROLS" not in run_sh and
        "export PSX_DISRUPTOR_MOUSE_AIM=0" not in run_sh,
        "POSIX launcher must not synthesize disabling overrides")
require("--perspective-textures" in run_sh and
        "PERSPECTIVE_TEXTURES=1; GEOMETRY_CORRECTION=1" in run_sh,
        "POSIX perspective-texture launch must explicitly imply geometry")

print("developer menu contract: PASS")
