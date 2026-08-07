/*
 * Disruptor mouse-aim discovery probe.
 *
 * The retail game exposes only digital turn-left/turn-right controls.  A good
 * mouse implementation must write the player's real yaw state, rather than
 * pulse those digital buttons.  This opt-in probe compares main RAM while a
 * tester performs a short neutral/left/right sequence and ranks halfwords that
 * behave like an angle.  Normal gameplay pays no cost: the hook immediately
 * returns unless PSX_DISRUPTOR_CONTROL_PROBE=1 is present in the environment.
 */

#include "psx_sdl.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

extern "C" void mod_register_frame_hook(void (*hook)(void));
extern "C" uint8_t *memory_get_ram_ptr(void);
extern "C" SDL_Window *sdl_window;

namespace {

constexpr uint32_t kScanStart = 0x00056938u;
constexpr uint32_t kScanEnd = 0x001ff000u;
constexpr int kInitialNeutralFrames = 120;
constexpr int kTurnFrames = 240;
constexpr int kMiddleNeutralFrames = 120;
constexpr size_t kResultLimit = 300;

enum class Phase {
    WaitForStart,
    InitialNeutral,
    TurnLeft,
    MiddleNeutral,
    TurnRight,
    Complete,
};

enum class SampleKind {
    Neutral,
    Left,
    Right,
};

struct HalfwordStats {
    uint16_t neutral_changes = 0;
    uint16_t left_positive = 0;
    uint16_t left_negative = 0;
    uint16_t right_positive = 0;
    uint16_t right_negative = 0;
    uint16_t left_min_abs = std::numeric_limits<uint16_t>::max();
    uint16_t left_max_abs = 0;
    uint16_t right_min_abs = std::numeric_limits<uint16_t>::max();
    uint16_t right_max_abs = 0;
    uint32_t left_abs_sum = 0;
    uint32_t right_abs_sum = 0;
};

struct Candidate {
    size_t index = 0;
    double score = 0.0;
    uint32_t left_changes = 0;
    uint32_t right_changes = 0;
    uint32_t left_major = 0;
    uint32_t right_major = 0;
    char left_sign = '?';
    char right_sign = '?';
};

struct ProbeState {
    bool enabled = false;
    bool f8_was_down = false;
    Phase phase = Phase::WaitForStart;
    int phase_frames = 0;
    int neutral_samples = 0;
    int left_samples = 0;
    int right_samples = 0;
    uint64_t total_frames = 0;
    std::vector<uint16_t> previous;
    std::vector<HalfwordStats> stats;
    std::FILE *log = nullptr;
};

ProbeState g_probe;

bool env_enabled(const char *name) {
    const char *value = std::getenv(name);
    return value && value[0] != '\0' && std::strcmp(value, "0") != 0 &&
           std::strcmp(value, "false") != 0 && std::strcmp(value, "FALSE") != 0;
}

void set_title(const std::string& message) {
    if (!sdl_window) return;
    const std::string title = "Disruptor Recompiled - Mouse-Aim Discovery: " + message;
    SDL_SetWindowTitle(sdl_window, title.c_str());
}

void log_line(const char *line) {
    if (!g_probe.log) return;
    std::fprintf(g_probe.log, "%s\n", line);
    std::fflush(g_probe.log);
}

void sync_previous() {
    uint8_t *ram = memory_get_ram_ptr();
    if (!ram || g_probe.previous.empty()) return;
    std::memcpy(g_probe.previous.data(), ram + kScanStart,
                g_probe.previous.size() * sizeof(uint16_t));
}

uint16_t saturating_increment(uint16_t value) {
    return value == std::numeric_limits<uint16_t>::max() ? value
                                                         : static_cast<uint16_t>(value + 1);
}

void collect_sample(SampleKind kind) {
    uint8_t *ram = memory_get_ram_ptr();
    if (!ram) return;

    const auto *current = reinterpret_cast<const uint16_t *>(ram + kScanStart);
    const size_t count = g_probe.previous.size();

    for (size_t i = 0; i < count; ++i) {
        const uint16_t old_value = g_probe.previous[i];
        const uint16_t new_value = current[i];
        g_probe.previous[i] = new_value;
        if (old_value == new_value) continue;

        HalfwordStats& s = g_probe.stats[i];
        if (kind == SampleKind::Neutral) {
            s.neutral_changes = saturating_increment(s.neutral_changes);
            continue;
        }

        /* Modular signed subtraction makes a wrapping 16-bit angle retain its
         * direction as long as a single-frame step is less than half a turn. */
        const int16_t delta = static_cast<int16_t>(new_value - old_value);
        const uint16_t magnitude = delta == std::numeric_limits<int16_t>::min()
                                       ? 32768u
                                       : static_cast<uint16_t>(std::abs(static_cast<int>(delta)));

        if (kind == SampleKind::Left) {
            if (delta > 0)
                s.left_positive = saturating_increment(s.left_positive);
            else
                s.left_negative = saturating_increment(s.left_negative);
            s.left_min_abs = std::min(s.left_min_abs, magnitude);
            s.left_max_abs = std::max(s.left_max_abs, magnitude);
            s.left_abs_sum += magnitude;
        } else {
            if (delta > 0)
                s.right_positive = saturating_increment(s.right_positive);
            else
                s.right_negative = saturating_increment(s.right_negative);
            s.right_min_abs = std::min(s.right_min_abs, magnitude);
            s.right_max_abs = std::max(s.right_max_abs, magnitude);
            s.right_abs_sum += magnitude;
        }
    }
}

void announce_phase(const char *name) {
    if (g_probe.log) {
        std::fprintf(g_probe.log, "phase=%s runtime_frame=%llu\n", name,
                     static_cast<unsigned long long>(g_probe.total_frames));
        std::fflush(g_probe.log);
    }
}

void begin_probe() {
    const size_t count = (kScanEnd - kScanStart) / sizeof(uint16_t);
    g_probe.previous.assign(count, 0);
    g_probe.stats.assign(count, HalfwordStats{});
    sync_previous();

    g_probe.log = std::fopen("disruptor-control-probe.log", "wb");
    if (!g_probe.log) {
        set_title("ERROR - could not create disruptor-control-probe.log");
        g_probe.phase = Phase::Complete;
        return;
    }

    std::fprintf(g_probe.log,
                 "DISRUPTOR MOUSE-AIM DISCOVERY PROBE v1\n"
                 "scan_physical=0x%08X..0x%08X halfwords=%zu\n"
                 "sequence=neutral:%d,left:%d,neutral:%d,right:%d\n",
                 kScanStart, kScanEnd, count, kInitialNeutralFrames, kTurnFrames,
                 kMiddleNeutralFrames, kTurnFrames);
    std::fflush(g_probe.log);
    g_probe.phase = Phase::InitialNeutral;
    g_probe.phase_frames = 0;
    announce_phase("initial-neutral");
}

void write_results() {
    std::vector<Candidate> candidates;
    candidates.reserve(4096);

    for (size_t i = 0; i < g_probe.stats.size(); ++i) {
        const HalfwordStats& s = g_probe.stats[i];
        const uint32_t left_changes = static_cast<uint32_t>(s.left_positive) + s.left_negative;
        const uint32_t right_changes = static_cast<uint32_t>(s.right_positive) + s.right_negative;
        const uint32_t minimum_turn_changes = static_cast<uint32_t>(kTurnFrames / 5);
        if (left_changes < minimum_turn_changes || right_changes < minimum_turn_changes)
            continue;

        const bool left_is_positive = s.left_positive >= s.left_negative;
        const bool right_is_positive = s.right_positive >= s.right_negative;
        if (left_is_positive == right_is_positive) continue;

        const uint32_t left_major = std::max<uint32_t>(s.left_positive, s.left_negative);
        const uint32_t right_major = std::max<uint32_t>(s.right_positive, s.right_negative);
        const double left_consistency = static_cast<double>(left_major) / left_changes;
        const double right_consistency = static_cast<double>(right_major) / right_changes;
        if (left_consistency < 0.70 || right_consistency < 0.70) continue;

        const double left_coverage = static_cast<double>(left_changes) /
                                     std::max(1, g_probe.left_samples);
        const double right_coverage = static_cast<double>(right_changes) /
                                      std::max(1, g_probe.right_samples);
        const double neutral_rate = static_cast<double>(s.neutral_changes) /
                                    std::max(1, g_probe.neutral_samples);
        const double left_average = static_cast<double>(s.left_abs_sum) / left_changes;
        const double right_average = static_cast<double>(s.right_abs_sum) / right_changes;
        const double magnitude_match = std::min(left_average, right_average) /
                                       std::max(left_average, right_average);

        Candidate candidate;
        candidate.index = i;
        candidate.left_changes = left_changes;
        candidate.right_changes = right_changes;
        candidate.left_major = left_major;
        candidate.right_major = right_major;
        candidate.left_sign = left_is_positive ? '+' : '-';
        candidate.right_sign = right_is_positive ? '+' : '-';
        candidate.score = 1800.0 * std::min(left_coverage, right_coverage) +
                          1000.0 * left_consistency +
                          1000.0 * right_consistency +
                          500.0 * magnitude_match -
                          2200.0 * neutral_rate;
        candidates.push_back(candidate);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  if (a.score != b.score) return a.score > b.score;
                  return a.index < b.index;
              });

    const size_t emitted = std::min(kResultLimit, candidates.size());
    std::fprintf(g_probe.log,
                 "RESULTS candidates_total=%zu candidates_emitted=%zu "
                 "neutral_samples=%d left_samples=%d right_samples=%d\n",
                 candidates.size(), emitted, g_probe.neutral_samples,
                 g_probe.left_samples, g_probe.right_samples);
    std::fprintf(g_probe.log,
                 "rank psx_address score current neutral_changes "
                 "left_changes left_major left_sign left_avg_abs left_min_abs left_max_abs "
                 "right_changes right_major right_sign right_avg_abs right_min_abs right_max_abs\n");

    uint8_t *ram = memory_get_ram_ptr();
    const auto *current = reinterpret_cast<const uint16_t *>(ram + kScanStart);
    for (size_t rank = 0; rank < emitted; ++rank) {
        const Candidate& candidate = candidates[rank];
        const HalfwordStats& s = g_probe.stats[candidate.index];
        const uint32_t address = 0x80000000u + kScanStart +
                                 static_cast<uint32_t>(candidate.index * sizeof(uint16_t));
        const double left_average = static_cast<double>(s.left_abs_sum) /
                                    candidate.left_changes;
        const double right_average = static_cast<double>(s.right_abs_sum) /
                                     candidate.right_changes;
        std::fprintf(g_probe.log,
                     "%zu 0x%08X %.3f 0x%04X %u %u %u %c %.3f %u %u "
                     "%u %u %c %.3f %u %u\n",
                     rank + 1, address, candidate.score, current[candidate.index],
                     s.neutral_changes, candidate.left_changes, candidate.left_major,
                     candidate.left_sign, left_average, s.left_min_abs, s.left_max_abs,
                     candidate.right_changes, candidate.right_major, candidate.right_sign,
                     right_average, s.right_min_abs, s.right_max_abs);
    }

    if (emitted == 0) {
        std::fprintf(g_probe.log,
                     "NO_CANDIDATES: repeat the probe while stationary in live gameplay; "
                     "hold each requested arrow continuously.\n");
    }
    std::fprintf(g_probe.log, "END_RESULTS\n");
    std::fflush(g_probe.log);
    std::fclose(g_probe.log);
    g_probe.log = nullptr;
}

void advance_phase(Phase next, const char *name) {
    g_probe.phase = next;
    g_probe.phase_frames = 0;
    sync_previous();
    announce_phase(name);
}

void probe_frame() {
    if (!g_probe.enabled) return;
    ++g_probe.total_frames;

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    if (!keys) return;
    const bool f8_down = keys[SDL_SCANCODE_F8] != 0;
    const bool left_down = keys[SDL_SCANCODE_LEFT] != 0;
    const bool right_down = keys[SDL_SCANCODE_RIGHT] != 0;

    if (g_probe.phase == Phase::WaitForStart) {
        set_title("enter a level, stand still, then press F8");
        if (f8_down && !g_probe.f8_was_down) begin_probe();
        g_probe.f8_was_down = f8_down;
        return;
    }
    g_probe.f8_was_down = f8_down;

    switch (g_probe.phase) {
    case Phase::InitialNeutral:
        if (left_down || right_down) {
            sync_previous();
            set_title("release LEFT and RIGHT");
            break;
        }
        collect_sample(SampleKind::Neutral);
        ++g_probe.neutral_samples;
        ++g_probe.phase_frames;
        set_title("stand still - " +
                  std::to_string(kInitialNeutralFrames - g_probe.phase_frames) +
                  " frames");
        if (g_probe.phase_frames >= kInitialNeutralFrames)
            advance_phase(Phase::TurnLeft, "turn-left");
        break;

    case Phase::TurnLeft:
        if (!left_down || right_down) {
            sync_previous();
            set_title("HOLD keyboard LEFT - " +
                      std::to_string(kTurnFrames - g_probe.phase_frames) + " frames");
            break;
        }
        collect_sample(SampleKind::Left);
        ++g_probe.left_samples;
        ++g_probe.phase_frames;
        set_title("keep holding LEFT - " +
                  std::to_string(kTurnFrames - g_probe.phase_frames) + " frames");
        if (g_probe.phase_frames >= kTurnFrames)
            advance_phase(Phase::MiddleNeutral, "middle-neutral");
        break;

    case Phase::MiddleNeutral:
        if (left_down || right_down) {
            sync_previous();
            set_title("release LEFT and RIGHT - " +
                      std::to_string(kMiddleNeutralFrames - g_probe.phase_frames) +
                      " frames");
            break;
        }
        collect_sample(SampleKind::Neutral);
        ++g_probe.neutral_samples;
        ++g_probe.phase_frames;
        set_title("stand still - " +
                  std::to_string(kMiddleNeutralFrames - g_probe.phase_frames) +
                  " frames");
        if (g_probe.phase_frames >= kMiddleNeutralFrames)
            advance_phase(Phase::TurnRight, "turn-right");
        break;

    case Phase::TurnRight:
        if (!right_down || left_down) {
            sync_previous();
            set_title("HOLD keyboard RIGHT - " +
                      std::to_string(kTurnFrames - g_probe.phase_frames) + " frames");
            break;
        }
        collect_sample(SampleKind::Right);
        ++g_probe.right_samples;
        ++g_probe.phase_frames;
        set_title("keep holding RIGHT - " +
                  std::to_string(kTurnFrames - g_probe.phase_frames) + " frames");
        if (g_probe.phase_frames >= kTurnFrames) {
            write_results();
            g_probe.phase = Phase::Complete;
            set_title("COMPLETE - close game, then collect results");
        }
        break;

    case Phase::Complete:
        set_title("COMPLETE - close game, then collect results");
        break;

    case Phase::WaitForStart:
        break;
    }
}

struct ProbeRegistration {
    ProbeRegistration() {
        g_probe.enabled = env_enabled("PSX_DISRUPTOR_CONTROL_PROBE");
        if (g_probe.enabled) mod_register_frame_hook(probe_frame);
    }
};

ProbeRegistration g_registration;

}  // namespace
