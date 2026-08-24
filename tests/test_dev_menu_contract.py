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
cheats = read("src/disruptor_cheats.cpp")
cheats_h = read("src/disruptor_cheats.h")
far_rendering_h = read("src/disruptor_far_rendering.h")
config_h = read("psxrecomp-overlay/recompiler/src/config_loader.h")
config_cpp = read("psxrecomp-overlay/recompiler/src/config_loader.cpp")
run_ps1 = read("run.ps1")
run_sh = read("run.sh")
game_toml = read("game.toml")
game_wide_toml = read("game-widescreen.toml")

require("option(DISRUPTOR_DEV_MENU" in cmake,
        "developer menu must remain independently build-selectable")
require("v1.92.6.tar.gz" in cmake and
        "5b17c01f69545bde732b14936d89ce0f508adb83e8b56fa82448371845172bc3"
        in cmake.lower(),
        "Dear ImGui source must remain versioned and integrity-pinned")
require("src/disruptor_dev_menu.cpp" in cmake and
        "target_link_libraries(psx-runtime PRIVATE disruptor-imgui)" in cmake,
        "menu implementation must be linked only into the game runtime")
require("src/disruptor_cheats.cpp" in cmake and
        "tests/disruptor_cheats_test.cpp" in cmake,
        "version-pinned cheats and their behavioral test must be built")
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
    "psx_host_video_get_display_aspect",
    "psx_host_video_set_display_aspect",
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

aspect_setter = function_body(main_cpp,
                              "psx_host_video_set_display_aspect")
for token in (
    "is_host_fixed_display_aspect",
    "g_pending_video_aspect.store",
    "std::memory_order_release",
):
    require(token in aspect_setter,
            f"live aspect setter is missing {token}")
fixed_aspects = function_body(main_cpp, "is_host_fixed_display_aspect")
for token in ("numerator == 4", "numerator == 16", "numerator == 21",
              "numerator == 32"):
    require(token in fixed_aspects,
            f"fixed aspect validation is missing {token}")
aspect_apply = function_body(main_cpp, "apply_live_display_aspect")
for token in (
    "gl_renderer_set_display_aspect",
    "SDL_RenderSetLogicalSize",
    "reshape_window_for_fixed_aspect",
    "gte_set_display_aspect",
    "gpu_ws_configure",
    "gl_renderer_invalidate_present",
):
    require(token in aspect_apply,
            f"live aspect transition is missing {token}")
vblank_aspect_start = main_cpp.index("static void sdl_vblank_present")
vblank_aspect_prefix = main_cpp[
    vblank_aspect_start:
    main_cpp.index("#ifndef PSX_NO_DEBUG_TOOLS", vblank_aspect_start)
]
require("PendingAspectBoundary" in vblank_aspect_prefix and
        "~PendingAspectBoundary()" in vblank_aspect_prefix and
        "apply_pending_fixed_display_aspect" in vblank_aspect_prefix,
        "fixed aspect requests must apply on vblank-present scope exit")
for config_name, config in (
    ("game.toml", game_toml),
    ("game-widescreen.toml", game_wide_toml),
):
    require("offer_ultrawide = true" in config,
            f"{config_name} would clamp a saved 21:9 aspect on restart")

for token in (
    "SDL_SCANCODE_GRAVE",
    "SDL_SCANCODE_ESCAPE",
    'BeginTabItem("Controls")',
    'BeginTabItem("Enhancements")',
    'BeginTabItem("Cheats")',
    'BeginTabItem("Diagnostics")',
    'BeginTabItem("System")',
    "gpu_geometry_correction_set",
    "gpu_texture_correction_set",
    "psx_host_video_set_interpolation",
    "psx_host_video_set_vsync",
    "psx_host_video_set_display_aspect",
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
    "disruptor_cheats_set_god_mode",
    "disruptor_cheats_grant_all_weapons",
    "Grant all weapons + psionics",
    "marked as cheated",
):
    require(token in menu or token in cheats_h,
            f"Cheats tab is missing {token}")
require("psx_mod_register_function_entry_plugin" in cheats and
        "0x80020DD8u" in cheats and "cpu->gpr[4] = 0u" in cheats,
        "God mode must neutralise the audited central damage invocation")
require("psx_netplay_active()" in cheats and
        "psx_mod_game_started()" in cheats,
        "cheats must fail closed outside gameplay and during netplay")
require("SDL_" not in cheats and "sio_" not in cheats,
        "cheat semantics must not synthesize host or guest input")
for forbidden in ("has_god_mode", "has_all_weapons", "PREF_GOD", "PREF_CHEAT"):
    require(forbidden not in config_h and forbidden not in config_cpp and
            forbidden not in menu,
            f"cheat setting must remain session-only: {forbidden}")
require("disruptor_cheats_reset_session();" in
        function_body(menu, "on_runtime_ready") and
        "disruptor_cheats_reset_session();" in
        function_body(menu, "on_runtime_shutdown"),
        "cheat toggles must reset at both session boundaries")

for token in (
    "DISRUPTOR_FAR_RENDERING_RETAIL",
    "DISRUPTOR_FAR_RENDERING_EXTENDED",
    "DISRUPTOR_FAR_RENDERING_FAR",
    "DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL",
    "DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED",
    "DisruptorFarRenderingDiagnostics",
    "disruptor_far_rendering_set_preset",
    "disruptor_far_rendering_set_depth_fade_mode",
    "disruptor_far_rendering_get_depth_fade_mode",
    "disruptor_far_rendering_get_diagnostics",
    "disruptor_far_rendering_reset_session",
):
    require(token in far_rendering_h,
            f"far-rendering session ABI is missing {token}")

far_controls = function_body(menu, "draw_far_rendering_controls")
for token in (
    'SeparatorText("Experimental: Draw distance")',
    '"Retail", "1.25x", "1.5x"',
    '"Retail palette ramp", "Nearest CLUT row (diagnostic)"',
    'ImGui::Combo("Distance shading"',
    "diagnostics.netplay_blocked",
    "diagnostics.gameplay_ready",
    "ImGui::BeginDisabled()",
    "disruptor_far_rendering_set_preset",
    "Restore Retail distance and fade",
    "disruptor_far_rendering_set_depth_fade_mode",
    "reveal the void",
    "portal or ",
    "culling errors",
    "distant actors frozen",
    "primitive ",
    "pressure or frame time",
    "selects the nearest palette row",
    "world and billboard paths",
    "level-authored DRAWENV background",
    "add geometry, or bypass portal/content culling",
    "Applied far / shading hooks",
    "Session-only",
):
    require(token in far_controls,
            f"far-rendering menu control is missing {token}")

diagnostics_body = function_body(menu, "draw_diagnostics_tab")
for token in (
    "source_far_distance",
    "effective_far_distance",
    "effective_fade_start",
    "depth_fade_mode",
    "substituted_loads",
    "far_load_substitutions",
    "fade_load_substitutions",
    "completed_frames",
    "primitive_samples",
    "primitive_high_water",
    "render_wall_us_p50",
    "render_wall_us_p95",
    "render_wall_us_max",
    "portal_final_decision_flips",
    "portal_final_decisions",
    "object_decision_flips",
    "object_far_decisions",
    "traversal_rooms_last",
    "traversal_rooms_high_water",
    "submitted_spans_last",
    "submitted_spans_high_water",
    "traversal_max_depth_last",
    "traversal_max_depth_high_water",
    "traversal_cap_hits",
    "portal_shortcut_relaxations",
    "packet_entry_to_traversal_end_last",
    "packet_traversal_to_visible_start_last",
    "packet_visible_loop_last",
    "packet_post_visible_loop_last",
    "observer_sequence_errors",
    "New valid-room marks last / high",
    "Visible room spans last / high",
):
    require(token in diagnostics_body,
            f"far-rendering diagnostics are missing {token}")

require("disruptor_far_rendering_reset_session();" in
        function_body(menu, "on_runtime_ready") and
        "disruptor_far_rendering_reset_session();" in
        function_body(menu, "on_runtime_shutdown"),
        "far-rendering presets must reset at both session boundaries")
for forbidden in (
    "has_draw_distance",
    "has_far_rendering",
    "has_depth_fade",
    "draw_distance_preset",
    "far_rendering_preset",
    "depth_fade_mode",
):
    require(forbidden not in config_h and forbidden not in config_cpp,
            f"experimental draw distance must remain outside persistence: {forbidden}")
require("mark_draw_distance" not in menu and
        "mark_far_rendering" not in menu and
        "mark_depth_fade" not in menu and
        "PREF_DRAW_DISTANCE" not in menu and
        "PREF_FAR_RENDERING" not in menu and
        "PREF_DEPTH_FADE" not in menu,
        "experimental draw distance must not enter the menu preference layer")

for token in (
    "has_frame_interpolation_blend",
    "has_mouse_aim",
    "has_modern_controls",
    "has_horizontal_sensitivity",
    "has_invert_horizontal",
    "has_vertical_look",
    "has_vertical_sensitivity",
    "has_invert_vertical",
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
for token in (
    "Vertical mouse look",
    "Vertical sensitivity",
    "Invert vertical mouse",
    "Recenter vertical view",
    "disruptor_mouse_set_vertical_look_enabled",
    "disruptor_mouse_set_vertical_sensitivity",
    "disruptor_mouse_set_invert_vertical",
):
    require(token in menu, f"vertical-look menu is missing {token}")
for token in (
    "PSX_DISRUPTOR_VERTICAL_LOOK",
    "PSX_DISRUPTOR_MOUSE_SENSITIVITY_Y",
    "PSX_DISRUPTOR_MOUSE_INVERT_Y",
):
    require(token in menu or token in mouse,
            f"vertical settings precedence is missing {token}")
require("take_relative_motion" in mouse and
        mouse.count("take_relative_motion()") == 2,
        "mouse X/Y must come from exactly one relative sample per frame")
require("kMaximumPitchUnits = 22.0" in mouse and
        "disruptor_vertical_camera_set_requested_pitch" in mouse and
        "disruptor_vertical_camera_recenter" in mouse and
        "disruptor_vertical_camera_input_allowed()" in mouse,
        "vertical input must use the bounded camera/weapon-aim sink")
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

aspect_controls = function_body(menu, "draw_aspect_controls")
for token in (
    'Checkbox("Widescreen"',
    'Combo("Aspect ratio"',
    '"16:9", "21:9", "32:9"',
    "psx_host_video_get_display_aspect",
    "psx_host_video_set_display_aspect",
    "mark_aspect",
    'draw_status_badge("LIVE"',
):
    require(token in aspect_controls,
            f"live aspect controls are missing {token}")
require("PREF_ASPECT" in function_body(menu, "merge_dirty_preferences") and
        "settings.has_aspect_ratio = true" in
        function_body(menu, "merge_dirty_preferences") and
        "settings.has_adaptive_view = true" in
        function_body(menu, "merge_dirty_preferences") and
        "settings.adaptive_view = false" in
        function_body(menu, "merge_dirty_preferences"),
        "fixed live aspect choices must atomically persist and disable adaptive view")
require("PREF_ASPECT" in function_body(menu, "apply_pending_preferences") and
        "psx_host_video_set_display_aspect" in
        function_body(menu, "apply_pending_preferences"),
        "a pending aspect choice must survive a soft runtime session")
require("g_frame_interpolation = 0;" in main_cpp and
        "does not auto-restore it" in main_cpp,
        "legacy settings.toml must not auto-restore blurry interpolation")

main_settings = main_cpp.index("load_user_settings(settings_path)")
main_env = main_cpp.index('std::getenv("PSX_GEOMETRY_CORRECTION")')
require(main_settings < main_cpp.index("us.has_geometry_correction") < main_env and
        main_settings < main_cpp.index("us.has_perspective_textures") < main_env,
        "saved enhancements must load before explicit environment overrides")
aspect_env = main_cpp.index('std::getenv("PSX_VIDEO_ASPECT")')
require(main_settings < main_cpp.index("us.has_aspect_ratio") < aspect_env and
        "is_host_fixed_display_aspect" in main_cpp[aspect_env:main_env],
        "explicit aspect override must be validated after saved settings load")
require("UserSettings seed =" in main_cpp and
        "load_user_settings(" in
        main_cpp[main_cpp.index("UserSettings seed ="):main_cpp.index("seed.renderer =")],
        "a generic launcher save must merge rather than drop game-owned fields")

require("PSX_DISRUPTOR_MODERN_CONTROLS = if" not in run_ps1 and
        "PSX_TEXTURE_CORRECTION = if" not in run_ps1 and
        "$env:PSX_DISRUPTOR_MOUSE_AIM = '0'" not in run_ps1,
        "PowerShell must not synthesize disabling overrides for absent switches")
require("[switch]$VerticalLook" in run_ps1 and
        "PSX_DISRUPTOR_VERTICAL_LOOK" in run_ps1 and
        "--vertical-look" in run_sh and
        "PSX_DISRUPTOR_VERTICAL_LOOK=1" in run_sh,
        "launchers must expose an explicit vertical-look opt-in")
require("PSX_DISRUPTOR_VERTICAL_LOOK = if" not in run_ps1 and
        "export PSX_DISRUPTOR_VERTICAL_LOOK=0" not in run_sh,
        "launchers must not synthesize a disabling vertical override")
require("export PSX_DISRUPTOR_MODERN_CONTROLS=$MODERN_CONTROLS" not in run_sh and
        "export PSX_DISRUPTOR_MOUSE_AIM=0" not in run_sh,
        "POSIX launcher must not synthesize disabling overrides")
require("--perspective-textures" in run_sh and
        "PERSPECTIVE_TEXTURES=1; GEOMETRY_CORRECTION=1" in run_sh,
        "POSIX perspective-texture launch must explicitly imply geometry")
require("PSX_VIDEO_ASPECT'] = '16:9'" in run_ps1 and
        "export PSX_VIDEO_ASPECT=16:9" in run_sh,
        "explicit widescreen launch switches must override a saved aspect")

print("developer menu contract: PASS")
