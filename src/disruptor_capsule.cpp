/* PSX_DISRUPTOR_CAPSULE_REPLAY=<folder> replays a capsule and ends the run: 0 in step with the recording to the end, 3 not, 2 unplayable. */
/* PSX_DISRUPTOR_CAPSULE_FRAMES=<folder> writes each replayed frame as a picture, which is whole only without a window. */
/* PSX_DISRUPTOR_CAPSULE_RECORD=<frames> is the key for a script: two seconds in, it records that many frames and ends the run. */
/* Measured: a loaded state takes its interrupts a few instructions apart, so one frame in four differs in a few bytes, nine in a row at most. */
#include "disruptor_capsule.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#ifndef DISRUPTOR_CAPSULE_NO_HOST
#include "gpu.h"
#include "interrupts.h"
#include "overlay_api.h"
#include "psx_cycles.h"
#include "savestate.h"

extern "C" uint8_t *memory_get_ram_ptr(void);
extern "C" void psx_write_byte(uint32_t address, uint8_t value);
extern "C" uint16_t sio_get_pad_buttons_slot(int slot);
extern "C" void sio_set_pad_state_slot(int slot, uint16_t buttons);
extern "C" double disruptor_mouse_vertical_pitch(void);
extern "C" void disruptor_mouse_set_vertical_pitch(double pitch);
extern "C" const char *psx_host_user_settings_path(void);
extern "C" int psx_netplay_active(void);
#endif

namespace disruptor::capsule {
namespace {

constexpr char kMagic[8] = {'D', 'C', 'A', 'P', 'S', 'U', 'L', '1'};
constexpr size_t kFrameBytes = 21, kHeadBytes = sizeof(kMagic) + 4 + 8;

void put(std::string &out, uint64_t value, int bytes) {
    for (int at = 0; at < bytes; ++at) out.push_back(static_cast<char>(value >> (8 * at)));
}

uint64_t take(std::string_view bytes, size_t at, int count) {
    uint64_t value = 0;
    for (int index = 0; index < count; ++index) value |= static_cast<uint64_t>(static_cast<uint8_t>(bytes[at + static_cast<size_t>(index)])) << (8 * index);
    return value;
}

}  /* namespace */

std::string encode(const std::vector<Frame> &frames, uint64_t first_cycle) {
    std::string out(kMagic, sizeof(kMagic));
    put(out, frames.size(), 4);
    put(out, first_cycle, 8);
    for (const Frame &frame : frames) {
        uint32_t pitch = 0;
        std::memcpy(&pitch, &frame.pitch, sizeof(pitch));
        put(out, frame.pad[0], 2);
        put(out, frame.pad[1], 2);
        put(out, frame.byte_at, 4);
        put(out, frame.byte, 1);
        put(out, pitch, 4);
        put(out, frame.memory, 8);
    }
    return out;
}

bool decode(std::string_view bytes, std::vector<Frame> &frames, uint64_t &first_cycle) {
    frames.clear();
    first_cycle = 0;
    if (bytes.size() < kHeadBytes || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) return false;
    const uint64_t count = take(bytes, sizeof(kMagic), 4);
    if (bytes.size() != kHeadBytes + count * kFrameBytes) return false;
    first_cycle = take(bytes, sizeof(kMagic) + 4, 8);
    frames.resize(static_cast<size_t>(count));
    for (size_t index = 0; index < frames.size(); ++index) {
        const size_t at = kHeadBytes + index * kFrameBytes;
        Frame &frame = frames[index];
        const auto pitch = static_cast<uint32_t>(take(bytes, at + 9, 4));
        frame.pad[0] = static_cast<uint16_t>(take(bytes, at, 2));
        frame.pad[1] = static_cast<uint16_t>(take(bytes, at + 2, 2));
        frame.byte_at = static_cast<uint32_t>(take(bytes, at + 4, 4));
        frame.byte = static_cast<uint8_t>(take(bytes, at + 8, 1));
        std::memcpy(&frame.pitch, &pitch, sizeof(pitch));
        frame.memory = take(bytes, at + 13, 8);
    }
    return true;
}

uint64_t digest(const uint8_t *bytes, size_t size) {
    uint64_t hash = 0xCBF29CE484222325ull;
    size_t at = 0;
    for (; at + 8 <= size; at += 8) {
        uint64_t word = 0;
        std::memcpy(&word, bytes + at, 8);
        hash = (hash ^ word) * 0x100000001B3ull;
        hash ^= hash >> 29;
    }
    for (; at < size; ++at) hash = (hash ^ bytes[at]) * 0x100000001B3ull;
    return hash;
}

std::string bitmap(const std::vector<uint8_t> &rgb, uint32_t width, uint32_t height) {
    const uint32_t row = (width * 3 + 3) & ~3u, pixels = row * height;
    std::string out = "BM";
    put(out, 54 + pixels, 4);
    put(out, 0, 4);
    put(out, 54, 4);
    put(out, 40, 4);
    put(out, width, 4);
    put(out, height, 4);
    put(out, 1, 2);
    put(out, 24, 2);
    put(out, 0, 4);
    put(out, pixels, 4);
    put(out, 2835, 4);
    put(out, 2835, 4);
    put(out, 0, 8);
    for (uint32_t y = height; y-- > 0;) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t *pixel = rgb.data() + (static_cast<size_t>(y) * width + x) * 3;
            out.push_back(static_cast<char>(pixel[2]));
            out.push_back(static_cast<char>(pixel[1]));
            out.push_back(static_cast<char>(pixel[0]));
        }
        out.append(row - width * 3, '\0');
    }
    return out;
}

}  /* namespace disruptor::capsule */

#ifndef DISRUPTOR_CAPSULE_NO_HOST
namespace {

namespace fs = std::filesystem;
using disruptor::capsule::Frame;

constexpr size_t kRamBytes = 0x200000;
constexpr uint32_t kVBlankCycles = 564480u;  /* VBLANK_CYCLES of the framework's interrupts.c */
constexpr size_t kSettle = 120, kInStep = 60, kLongest = 60 * 60 * 10;  /* ten minutes at 60 VBlanks a second, then the recording ends itself */

fs::path from_utf8(const char *text) { return fs::path(std::u8string(text, text + std::strlen(text))); }

std::string utf8(const fs::path &path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

enum class Recording { kOff, kSaving, kOn };
enum class Replay { kOff, kAsked, kLoading, kOn };

struct Capsule {
    Recording recording = Recording::kOff;
    Replay replay = Replay::kOff;
    fs::path made, played, pictures;
    std::string states_before;
    uint32_t bios = 0, entry = 0;
    std::vector<Frame> recorded, replayed;
    std::vector<uint64_t> seen;
    Frame open;  /* the VBlank being recorded: its memory is known, its input is not yet */
    size_t at = 0, same = 0, last_same = 0, unwritten = 0;
    uint64_t first_cycle = 0, played_first_cycle = 0;
    uint32_t noted_at = 0;
    uint8_t noted = 0;
    size_t scripted = 0, waited = 0;
    const char *notice = nullptr;
    std::chrono::steady_clock::time_point notice_until{};
    bool loaded = false;

    Capsule() {
        if (const char *record = std::getenv("PSX_DISRUPTOR_CAPSULE_RECORD"); record && record[0])
            scripted = static_cast<size_t>(std::strtoull(record, nullptr, 10));
        const char *replay_of = std::getenv("PSX_DISRUPTOR_CAPSULE_REPLAY");
        if (!replay_of || !replay_of[0]) return;
        played = from_utf8(replay_of);
        std::ifstream input(played / "input.bin", std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(input)), {});
        if (!disruptor::capsule::decode(bytes, replayed, played_first_cycle) || replayed.empty()) {
            std::fprintf(stderr, "disruptor: capsule: %s holds no readable input.bin\n", replay_of);
            std::exit(2);
        }
        if (const char *out = std::getenv("PSX_DISRUPTOR_CAPSULE_FRAMES"); out && out[0]) pictures = from_utf8(out);
        replay = Replay::kAsked;
    }
};

Capsule g_capsule;

/* A state goes through the framework's slot 0 with the slot folder turned to the capsule for that one save or load. */
bool turn_states_to(const fs::path &folder) {
    Capsule &capsule = g_capsule;
    capsule.states_before = savestate_dir();
    savestate_get_integrity(&capsule.bios, &capsule.entry);
    if (capsule.states_before.empty() || capsule.entry == 0u) return false;
    savestate_configure(utf8(folder).c_str(), capsule.bios, capsule.entry);
    return true;
}

void turn_states_back() {
    savestate_configure(g_capsule.states_before.c_str(), g_capsule.bios, g_capsule.entry);
}

bool write(const fs::path &path, const std::string &bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return !out.fail();
}

uint64_t memory_now() { return disruptor::capsule::digest(memory_get_ram_ptr(), kRamBytes); }

/* The open frame gets what the last VBlank left in the pads and wrote into the game. */
void close_frame() {
    Capsule &capsule = g_capsule;
    capsule.open.pad[0] = sio_get_pad_buttons_slot(0);
    capsule.open.pad[1] = sio_get_pad_buttons_slot(1);
    capsule.open.byte_at = capsule.noted_at;
    capsule.open.byte = capsule.noted;
    capsule.open.pitch = static_cast<float>(disruptor_mouse_vertical_pitch());
    capsule.recorded.push_back(capsule.open);
    capsule.noted_at = 0;
}

void open_frame() {
    g_capsule.open = Frame{};
    g_capsule.open.memory = memory_now();
    g_capsule.noted_at = 0;
}

void say(const char *word) {
    g_capsule.notice = word;
    g_capsule.notice_until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
}

void refuse_recording(const char *why) {
    std::fprintf(stderr, "disruptor: capsule: no recording, %s\n", why);
    g_capsule.recording = Recording::kOff;
    say("REC refused");
    if (g_capsule.scripted == 0) return;
    std::fflush(nullptr);
    std::_Exit(2);  /* a script waits for the run to end */
}

/* Writes what was recorded. Its last frame is there for its memory alone: a replay ends on it. */
void store_recording(const char *why) {
    Capsule &capsule = g_capsule;
    char about[256];
    std::snprintf(about, sizeof(about), "frames = %zu\ncodegen_hash = \"0x%08X\"\nentry_pc = \"0x%08X\"\nended = \"%s\"\n",
                  capsule.recorded.size(), static_cast<unsigned>(PSX_OVERLAY_CODEGEN_HASH), static_cast<unsigned>(capsule.entry), why);
    std::error_code failed;
    const char *settings = psx_host_user_settings_path();
    if (settings && fs::exists(from_utf8(settings), failed))
        fs::copy_file(from_utf8(settings), capsule.made / "settings.toml", fs::copy_options::overwrite_existing, failed);
    const bool input = write(capsule.made / "input.bin", disruptor::capsule::encode(capsule.recorded, capsule.first_cycle));
    const bool stored = write(capsule.made / "capsule.toml", about) && input && !failed;
    if (stored)
        std::fprintf(stderr, "disruptor: capsule: %zu frames in %s (%s)\n", capsule.recorded.size(), utf8(capsule.made).c_str(), why);
    else
        std::fprintf(stderr, "disruptor: capsule: the capsule in %s could NOT be written whole (%s)\n", utf8(capsule.made).c_str(), why);
    capsule.recorded.clear();
    capsule.recording = Recording::kOff;
    say(stored ? "REC saved" : "REC NOT saved");
    if (capsule.scripted == 0) return;
    std::fflush(nullptr);
    std::_Exit(stored ? 0 : 2);
}

/* Called before the pads are sampled, so the game's memory is that of this VBlank and the pads are still the last one's. */
void finish_recording(const char *why) {
    Capsule &capsule = g_capsule;
    close_frame();
    Frame last;
    last.pitch = capsule.open.pitch;
    last.memory = memory_now();
    capsule.recorded.push_back(last);
    store_recording(why);
}

void write_picture(size_t number) {
    GpuDisplayInfo display{};
    gpu_get_display_info(&display);
    if (display.width == 0 || display.height == 0) return;
    std::vector<uint8_t> rgb(static_cast<size_t>(display.width) * display.height * 3);
    for (uint32_t y = 0; y < display.height; ++y)
        for (uint32_t x = 0; x < display.width; ++x) {
            uint8_t *pixel = rgb.data() + (static_cast<size_t>(y) * display.width + x) * 3;
            gpu_display_pixel_rgb(&display, x, y, pixel, pixel + 1, pixel + 2);
        }
    char name[32];
    std::snprintf(name, sizeof(name), "%05zu.bmp", number);
    if (!write(g_capsule.pictures / name, disruptor::capsule::bitmap(rgb, display.width, display.height))) ++g_capsule.unwritten;
}

[[noreturn]] void finish_replay() {
    Capsule &capsule = g_capsule;
    const bool in_step = capsule.same != 0 && capsule.last_same + kInStep >= capsule.replayed.size();
    std::fprintf(stderr, "disruptor: capsule: replayed %zu frames and %s: the game's memory is the recording's at %zu of them, last at frame %zu\n",
                 capsule.replayed.size(), in_step ? "stayed in step with the recording" : "LEFT the recording", capsule.same, capsule.last_same);
    if (!capsule.pictures.empty()) {
        std::string lines;
        char line[64];
        for (size_t index = 0; index < capsule.seen.size(); ++index) {
            std::snprintf(line, sizeof(line), "%zu %016llx %016llx\n", index, static_cast<unsigned long long>(capsule.replayed[index].memory),
                          static_cast<unsigned long long>(capsule.seen[index]));
            lines += line;
        }
        if (!write(capsule.pictures / "memory.txt", lines)) ++capsule.unwritten;
        if (capsule.unwritten != 0)
            std::fprintf(stderr, "disruptor: capsule: %zu of the files asked for in %s could NOT be written\n", capsule.unwritten, utf8(capsule.pictures).c_str());
    }
    std::fflush(nullptr);
    std::_Exit(!in_step ? 3 : capsule.unwritten != 0 ? 2 : 0);  /* the run ends inside a VBlank on the game's thread: nothing is left to save */
}

void replay_frame() {
    Capsule &capsule = g_capsule;
    const Frame &frame = capsule.replayed[capsule.at];
    capsule.seen.push_back(memory_now());
    if (capsule.seen.back() == frame.memory) {
        ++capsule.same;
        capsule.last_same = capsule.at;
    }
    if (!capsule.pictures.empty()) write_picture(capsule.at);
    if (++capsule.at == capsule.replayed.size()) finish_replay();
    sio_set_pad_state_slot(0, frame.pad[0]);
    sio_set_pad_state_slot(1, frame.pad[1]);
    if (frame.byte_at != 0u) {
        psx_write_byte(frame.byte_at, frame.byte);
        disruptor_capsule_note_byte(frame.byte_at, frame.byte);  /* a recording made inside a replay holds the byte too */
    }
    disruptor_mouse_set_vertical_pitch(static_cast<double>(frame.pitch));
}

}  /* namespace */

extern "C" const char *disruptor_capsule_notice(void) {
    return std::chrono::steady_clock::now() < g_capsule.notice_until ? g_capsule.notice : nullptr;
}

extern "C" void disruptor_capsule_note_byte(uint32_t address, uint8_t value) {
    g_capsule.noted_at = address;
    g_capsule.noted = value;
}

extern "C" void disruptor_capsule_state_loaded(void) {
    if (g_capsule.recording == Recording::kOn) {  /* a capsule holds one state: the recording ends at the last VBlank before this one */
        if (g_capsule.recorded.empty()) refuse_recording("a state was loaded before its first frame");
        else store_recording("a state was loaded");
    }
    if (g_capsule.replay != Replay::kLoading) return;
    g_capsule.loaded = true;
    /* A state does not hold how far the game is into its VBlank period: it would run on from wherever this session was. The capsule holds it. */
    const uint64_t now = psx_get_cycle_count(), first = g_capsule.played_first_cycle;
    const uint32_t left = interrupts_cycles_to_vblank();
    if (left == 0u || first <= now || first - now > kVBlankCycles) {
        std::fprintf(stderr, "disruptor: capsule: the first VBlank is not within a period of the state, so the replay keeps this session's\n");
        return;
    }
    interrupts_advance_cycles(left - static_cast<uint32_t>(first - now));
}

extern "C" void disruptor_capsule_toggle(void) {
    Capsule &capsule = g_capsule;
    if (capsule.recording == Recording::kOn) {
        finish_recording("the key");
        return;
    }
    if (capsule.recording != Recording::kOff || capsule.replay == Replay::kAsked || capsule.replay == Replay::kLoading || savestate_pending()) return;
    if (psx_netplay_active()) return refuse_recording("a network game is not recorded");  /* the frame loop does not call the capsule there */
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::error_code failed;
    const char *settings = psx_host_user_settings_path();
    const fs::path all = fs::absolute((settings ? from_utf8(settings).parent_path() : fs::path()) / "capsules", failed);
    fs::create_directories(all, failed);
    bool made = false;  /* a folder of its own: two recordings may start within one second */
    for (int copy = 1; copy < 100 && !failed && !made; ++copy) {
        capsule.made = all / (std::to_string(now) + (copy > 1 ? "-" + std::to_string(copy) : ""));
        made = fs::create_directory(capsule.made, failed);
    }
    if (failed || !made || !turn_states_to(capsule.made)) return refuse_recording("the folder or the save states are not there");
    if (!savestate_request_save(0)) {
        turn_states_back();
        return refuse_recording("a save state is refused now");
    }
    capsule.recorded.clear();
    capsule.recording = Recording::kSaving;
}

extern "C" void disruptor_capsule_before_input(void) {
    Capsule &capsule = g_capsule;
    switch (capsule.recording) {
    case Recording::kOff:
        if (capsule.scripted != 0 && capsule.replay != Replay::kAsked && capsule.replay != Replay::kLoading && ++capsule.waited == kSettle)
            disruptor_capsule_toggle();
        break;
    case Recording::kSaving:
        if (savestate_pending()) break;
        if (const bool saved = savestate_slot_exists(0) != 0; turn_states_back(), !saved) {
            refuse_recording("the state was not written");
            break;
        }
        std::fprintf(stderr, "disruptor: capsule: recording into %s\n", utf8(capsule.made).c_str());
        capsule.recording = Recording::kOn;
        say("REC");
        capsule.first_cycle = psx_get_cycle_count();
        open_frame();
        break;
    case Recording::kOn:
        if (capsule.recorded.size() + 2 >= (capsule.scripted != 0 ? capsule.scripted : kLongest)) {
            finish_recording(capsule.scripted != 0 ? "the asked length" : "the longest recording");
            break;
        }
        close_frame();
        open_frame();
        break;
    }
}

extern "C" void disruptor_capsule_after_input(void) {
    Capsule &capsule = g_capsule;
    if (capsule.replay == Replay::kAsked) {
        if (!turn_states_to(capsule.played) || !savestate_request_load(0)) {
            std::fprintf(stderr, "disruptor: capsule: the state of %s cannot be loaded\n", utf8(capsule.played).c_str());
            std::fflush(nullptr);
            std::_Exit(2);
        }
        capsule.replay = Replay::kLoading;
        return;
    }
    if (capsule.replay == Replay::kLoading) {
        if (savestate_pending()) return;
        turn_states_back();
        if (!capsule.loaded) {
            std::fprintf(stderr, "disruptor: capsule: the state of %s was refused: it is of another build or disc\n", utf8(capsule.played).c_str());
            std::fflush(nullptr);
            std::_Exit(2);
        }
        capsule.replay = Replay::kOn;
    }
    if (capsule.replay == Replay::kOn) replay_frame();
}
#endif
