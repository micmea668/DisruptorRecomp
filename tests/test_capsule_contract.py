#!/usr/bin/env python3
"""Source contract for the replay capsule: where the game calls it, and what its glue promises."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


host = read("psxrecomp-overlay/runtime/src/main.cpp")
capsule = read("src/disruptor_capsule.cpp")
mouse = read("src/disruptor_mouse_aim.cpp")
menu = read("src/disruptor_dev_menu.cpp")
cmake = read("CMakeLists.txt")

require(
    "#ifdef PSX_HAS_DISRUPTOR_CAPSULE\n"
    "    if (!psx_netplay_active()) disruptor_capsule_before_input();\n"
    "#endif\n"
    "    if (psx_netplay_active()) {\n"
    "        psx_netplay_finish_frame();\n"
    "    } else if (g_headless) {\n"
    "        sample_headless_pad_into_sio(override);\n"
    "    } else {\n"
    "        sample_pad_into_sio(override);\n"
    "    }\n"
    "#ifdef PSX_HAS_DISRUPTOR_CAPSULE\n"
    "    if (!psx_netplay_active()) disruptor_capsule_after_input();\n"
    "#endif\n"
    in host
    and host.count("disruptor_capsule_before_input();") == host.count("disruptor_capsule_after_input();") == 1,
    "the capsule is called once on either side of the pad sampling of every VBlank, and never in a network game",
)
sampling = host.index("    if (!psx_netplay_active()) disruptor_capsule_after_input();")
require(
    sampling < host.index("    if (g_headless || module_fast_forward) {")
    and sampling < host.index("    mod_call_frame_hooks();"),
    "a replay must put its frame in before the frame loop's early ways out: module hooks never run without a window",
)
require(
    'extern "C" void psx_frontend_on_savestate_loaded(void) {\n'
    "#ifdef PSX_HAS_DISRUPTOR_CAPSULE\n"
    "    disruptor_capsule_state_loaded();\n"
    "#endif\n" in host,
    "the capsule must hear of a loaded state before the game runs on from it",
)
require(
    "    if (g_capsule.replay != Replay::kLoading) return;\n    g_capsule.loaded = true;\n" in capsule
    and "    interrupts_advance_cycles(left - static_cast<uint32_t>(first - now));\n" in capsule
    and "    if (left == 0u || first <= now || first - now > kVBlankCycles) {" in capsule
    and "constexpr uint32_t kVBlankCycles = 564480u;" in capsule
    and "#define VBLANK_CYCLES   564480u" in read("psxrecomp/runtime/src/interrupts.c"),
    "a replay must put the first VBlank at the recording's cycle, with the framework's own VBlank period",
)
require(
    '        capsule.recording = Recording::kOn;\n        say("REC");\n        capsule.first_cycle = psx_get_cycle_count();\n        open_frame();\n'
    in capsule
    and "    capsule.open.pad[0] = sio_get_pad_buttons_slot(0);\n    capsule.open.pad[1] = sio_get_pad_buttons_slot(1);\n"
    in capsule
    and "    last.memory = memory_now();\n    capsule.recorded.push_back(last);\n" in capsule,
    "a recording takes the pads as the last VBlank left them, and ends with the memory its last input led to",
)
require(
    "    g_capsule.notice_until = std::chrono::steady_clock::now() + std::chrono::seconds(1);\n" in capsule
    and "    return std::chrono::steady_clock::now() < g_capsule.notice_until ? g_capsule.notice : nullptr;\n"
    in capsule
    and '    say(stored ? "REC saved" : "REC NOT saved");\n' in capsule
    and '    g_capsule.recording = Recording::kOff;\n    say("REC refused");\n' in capsule
    and "    if (!g_menu.open || !g_menu.imgui_ready) return disruptor_capsule_notice() ? PSX_HOST_UI_VISIBLE : 0u;\n"
    in menu
    and "    if (const char *word = g_menu.open ? nullptr : disruptor_capsule_notice()) {\n"
    "        if (initialize_imgui()) render_notice(word, width, height);\n        return;\n    }\n"
    in menu
    and "imgui_sdl_new_frame" not in menu[menu.index("void render_notice(") : menu.index("void render_gl(")]
    and "ImGuiWindowFlags_NoInputs" in menu[menu.index("void render_notice(") : menu.index("void render_gl(")],
    "a recording shows a word for one second when it starts, ends or is refused, takes no input for it, "
    "and never holds the in-between frames off for longer",
)
replay = capsule[capsule.index("void replay_frame() {") : capsule.index('extern "C" void disruptor_capsule_note_byte(')]
require(
    replay.index("capsule.seen.push_back(memory_now());") < replay.index("    sio_set_pad_state_slot(0, frame.pad[0]);")
    and replay.index("if (++capsule.at == capsule.replayed.size()) finish_replay();")
    < replay.index("    sio_set_pad_state_slot(0, frame.pad[0]);")
    and "        psx_write_byte(frame.byte_at, frame.byte);\n        disruptor_capsule_note_byte(frame.byte_at, frame.byte);"
    in replay
    and "    disruptor_mouse_set_vertical_pitch(static_cast<double>(frame.pitch));\n" in replay,
    "a replay compares the memory before it puts a frame's input in, puts in all a frame holds, "
    "and notes the byte it writes for a capsule recorded inside it",
)
require(
    "    const bool in_step = capsule.same != 0 && capsule.last_same + kInStep >= capsule.replayed.size();\n" in capsule
    and "constexpr size_t kSettle = 120, kInStep = 60, kLongest = 60 * 60 * 10;" in capsule,
    "a replay's exit code must say whether it still had the recording's memory in its last second",
)
require(
    "    if (capsule.recording != Recording::kOff || capsule.replay == Replay::kAsked || capsule.replay == Replay::kLoading || savestate_pending()) return;\n"
    in capsule
    and "    if (psx_netplay_active()) return refuse_recording(" in capsule
    and capsule.index("    if (psx_netplay_active()) return refuse_recording(")
    < capsule.index("turn_states_to(capsule.made)")
    and "    if (!savestate_request_save(0)) {\n        turn_states_back();\n" in capsule
    and "        if (const bool saved = savestate_slot_exists(0) != 0; turn_states_back(), !saved) {\n" in capsule
    and capsule.count("turn_states_back()") == 4
    and "        made = fs::create_directory(capsule.made, failed);\n" in capsule
    and "    if (failed || !made || !turn_states_to(capsule.made)) return refuse_recording(" in capsule,
    "the save state folder is the capsule's for one save or load only, and is turned back whatever came of it",
)
require(
    "    psx_write_byte(kPlayerYawAddress, new_yaw);\n    disruptor_capsule_note_byte(kPlayerYawAddress, new_yaw);\n"
    in mouse
    and 'extern "C" void disruptor_mouse_set_vertical_pitch(double pitch) {\n    g_mouse.vertical_pitch = pitch;\n}\n'
    in mouse,
    "the heading the mouse writes into the game must be told to a recording, and a replay must be able to put the pitch back",
)
require(
    "    if (!g_menu.open && scancode_event(event, SDL_SCANCODE_F12) && (SDL_GetModState() & KMOD_CTRL) != 0) {\n"
    "        disruptor_capsule_toggle();\n"
    "        return 1;\n"
    "    }\n" in menu,
    "Ctrl+F12 must start and end a recording and must not reach the save state keys",
)
require(
    '    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_capsule.cpp"\n' in cmake
    and "    PSX_HAS_DISRUPTOR_CAPSULE=1\n" in cmake
    and '#define DISRUPTOR_CAPSULE_NO_HOST 1\n#include "../src/disruptor_capsule.cpp"\n'
    in read("tests/disruptor_capsule_test.cpp")
    and "    add_test(NAME disruptor_capsule\n        COMMAND disruptor-capsule-test)\n" in cmake
    and 'tests/test_capsule_contract.py")' in cmake,
    "the capsule and its tests must remain built and registered",
)
require(
    "    out.close();\n    return !out.fail();\n" in capsule
    and '    const bool stored = write(capsule.made / "capsule.toml", about) && input && !failed;\n' in capsule
    and "    std::_Exit(stored ? 0 : 2);\n" in capsule
    and "    if (g_capsule.scripted == 0) return;\n    std::fflush(nullptr);\n    std::_Exit(2);" in capsule
    and "    std::_Exit(!in_step ? 3 : capsule.unwritten != 0 ? 2 : 0);" in capsule,
    "a capsule that was not written whole must not be called recorded, and a script must never be left waiting",
)
require(
    "    if (g_capsule.recording == Recording::kOn) {" in capsule
    and '        else store_recording("a state was loaded");\n' in capsule
    and capsule.index("    if (g_capsule.recording == Recording::kOn) {")
    < capsule.index("    if (g_capsule.replay != Replay::kLoading) return;"),
    "a recording must end when another state is loaded: a capsule holds one state",
)
guide = read("docs/BUILD.md")
require(
    "Ctrl+F12" in guide and "tools/replay_capsule.py" in guide and "PSX_DISRUPTOR_CAPSULE_REPLAY" in capsule,
    "the build guide must say how a capsule is made and replayed",
)

print("Disruptor replay capsule source contract: PASS")
