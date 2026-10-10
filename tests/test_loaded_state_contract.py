#!/usr/bin/env python3
"""Source contract: a save state loaded into a fresh process, with a window or without, runs on as the game that saved it.

Each rule was a cause of a replay leaving its recording. The first 16 bytes and the cost divider
were found by comparing the memory of a booted session with that of the same state loaded.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


host = read("psxrecomp-overlay/runtime/src/main.cpp")
memory = read("psxrecomp-overlay/runtime/src/memory.c")
state = read("psxrecomp-overlay/runtime/src/boot_state.c")
cycles = read("psxrecomp-overlay/runtime/include/psx_cyc.h")
latch = read("psxrecomp/runtime/src/fntrace.c")
menu = read("src/disruptor_dev_menu.cpp")
rate = read("src/disruptor_frame_rate.cpp")

require(
    "static void engage_widescreen_at_game_entry(void) {\n"
    "    extern int fntrace_is_game_started(void);\n"
    "    if (!g_ws_engaged && fntrace_is_game_started()) engage_widescreen();\n"
    "}\n"
    in host
    and host.count("engage_widescreen_at_game_entry();") == 2
    and "    if (g_headless) engage_widescreen_at_game_entry();" in host
    and host.index("    if (g_headless) engage_widescreen_at_game_entry();")
    < host.index("    if (g_headless || module_fast_forward) {")
    < host.index("    mod_call_frame_hooks();")
    < host.index("    engage_widescreen_at_game_entry();\n\n    /* ---- Display from our VRAM ---- */"),
    "a run without a window leaves the VBlank before a windowed one engages widescreen, so it engages it on its way out",
)
require(
    "  if (g_headless) {\n"
    '    std::fprintf(stdout, "psxrecomp: headless frontend enabled\\n");\n'
    "    host_ui_runtime_ready(nullptr, PSX_HOST_UI_BACKEND_NONE);"
    in host
    and host.count("    host_ui_runtime_ready(") == 2
    and "    load_preferences_for_session();\n    if (backend == PSX_HOST_UI_BACKEND_OPENGL) (void)initialize_imgui();"
    in menu
    and "disruptor_frame_rate_set_unlocked(settings.frame_unlock ? 1 : 0);" in menu,
    "a run without a window tells the game module the runtime is ready: its saved settings, frame unlock among them, apply there",
)
require(
    "static void sdl_audio_pump(bool discard_output = false) {\n    if (!sdl_audio_device) discard_output = true;"
    in host
    and "static void sdl_audio_update(int hard_mute_active, int turbo_sink_active) {\n    {   /* Tag audio events"
    in host
    and "            if (sdl_audio_device && audio_legacy_mode()) {" in host
    and "            } else if (sdl_audio_device && s_drc_ready) {\n                psx_sdl_audio_lock(sdl_audio_device);"
    in host
    and "if (!sdl_audio_device) return;" not in host
    and host.index("    if (discard_output) {\n        /* Host-only sink")
    < host.index("    spu_render(sdl_audio_buf, frames);\n    if (discard_output) {"),
    "the SPU is rendered on the guest's clock with no device to play it: the game reads the SPU back",
)
require(
    "static void take_loaded_state_for_started(void) {\n"
    "    uint32_t bios = 0, entry = 0;\n"
    "    savestate_get_integrity(&bios, &entry);\n"
    "    if (!dirty_ram_text_image_registered() || !psx_game_text_native_ok(entry)) return;\n"
    "    memory_keep_low_boot_scratch();"
    in host
    and "    if (!g_ws_engaged) engage_widescreen();  /* at the next VBlank" in host
    and "    gl_renderer_invalidate_present();\n    take_loaded_state_for_started();\n}\n" in host
    and host.count("take_loaded_state_for_started();") == 1
    and "psx_game_address_in_text(target) && psx_game_text_native_ok(target))) {" in latch,
    "a loaded state of a started game is taken for one before the game runs on, by the test the entry latch itself goes by",
)
require(
    "static int s_low_scratch_is_the_games;" in memory
    and "void memory_keep_low_boot_scratch(void) { s_low_scratch_is_the_games = 1; }\n" in memory
    and "void memory_clear_low_boot_scratch(void) {\n    if (s_low_scratch_is_the_games) return;\n    memset(ram, 0, 0x10u);\n}\n"
    in memory
    and "    s_low_scratch_is_the_games = 0;\n    memset(ram, 0, sizeof(ram));\n" in memory
    and memory.count("s_low_scratch_is_the_games") == 4
    and "            memory_clear_low_boot_scratch();\n" in latch,
    "the entry latch of a fresh process must not clear the first 16 bytes of a loaded game, and a new session clears them again",
)
require(
    "#define BS_SEC_CPU_COST 0x10u\n" in state
    and "    h.section_count = 16;\n" in state
    and "        ok = pst_w_u32(&w, g_psx_cyc_overclock_shift) &&\n"
    "             pst_w_u32(&w, g_psx_cyc_overclock_carry) &&\n"
    "             write_section(f, BS_SEC_CPU_COST, cost, sizeof cost);\n"
    in state
    and "        if (!pst_r_u32(&r, &shift) || !pst_r_u32(&r, &carry) || shift > 31u || (carry >> 31) != 0u) return 0;\n"
    "        g_psx_cyc_overclock_shift = shift;\n"
    "        g_psx_cyc_overclock_carry = carry;\n"
    in state
    and "BS_SEC_CPU_COST" not in state.split("const uint32_t required =")[1].split(";")[0]
    and "        const uint32_t total = g_psx_cyc_overclock_carry + cycles;\n" in cycles
    and "        cycles = total >> g_psx_cyc_overclock_shift;\n" in cycles
    and "            g_psx_cyc_overclock_shift =\n                g_cpu_shift.load(std::memory_order_relaxed);" in rate
    and "    if (waits) g_psx_cyc_overclock_shift = 0u;\n" in rate
    and "g_psx_cyc_overclock_carry" not in rate,
    "a state holds the instruction cost divider frame unlock sets and its carry, a carry a wait left without a divider "
    "loads, and a state without the two still loads",
)

print("Disruptor loaded state source contract: PASS")
