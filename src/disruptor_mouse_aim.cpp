/*
 * Native mouse aiming and modern keyboard/mouse controls for Disruptor
 * (SLUS-00224).
 *
 * Dynamic correlation plus static instruction tracing identified 0x80077624
 * as the game's 8-bit wrapping player/camera yaw.  The original turn routine
 * adds a signed controller-derived step to this byte; renderer, collision and
 * camera code then consume it through the game's own sine/cosine tables.
 *
 * This opt-in mod applies relative host mouse X motion to the same yaw byte
 * after each guest input/update frame.  Disruptor has no retail pitch state,
 * so independently enabled mouse Y motion drives a bounded host-side camera
 * and weapon-aim extension.  A third opt-in path merges a conventional
 * WASD/mouse layout into the emulated port-1 digital pad after the runtime's
 * low-latency input sample.  The runtime keeps a dormant frame hook available
 * for live menu changes; normal play pays only one immediate return unless one
 * of the control features is enabled.
 */

#include "psx_sdl.h"
#include "disruptor_mouse_aim.h"
#include "disruptor_vertical_camera.h"
#include "../psxrecomp-overlay/runtime/include/gpu.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

extern "C" void mod_register_frame_hook(void (*hook)(void));
extern "C" uint8_t psx_read_byte(uint32_t address);
extern "C" void psx_write_byte(uint32_t address, uint8_t value);
extern "C" uint16_t sio_get_pad_buttons_slot(int slot);
extern "C" void sio_set_pad_state_slot(int slot, uint16_t buttons);
extern "C" int32_t psx_mod_widescreen_x_margin(void);
extern "C" int gpu_ws_present_native_43(void);
extern "C" int ws_native_wide_active(void);
extern "C" int ws_nw_extra(void);
extern "C" void gpu_geometry_camera_yaw_residual_set(double yaw_units);
extern "C" SDL_Window *sdl_window;
extern "C" int psx_host_ui_game_input_captured(void);
extern "C" int psx_netplay_active(void);

namespace {

constexpr uint32_t kPlayerYawAddress = 0x80077624u;
constexpr double kDefaultHorizontalSensitivity = 0.080;
constexpr double kDefaultVerticalSensitivity = 0.080;
constexpr double kMinimumSensitivity = 0.005;
constexpr double kMaximumSensitivity = 2.000;
constexpr double kMaximumMotionPerFrame = 1024.0;
/* The native projectile vertical component is bounded to a slope of 160/256.
 * Twenty-two angular units (30.94 degrees) remain just inside that envelope
 * and keep the initial presentation horizon on-screen. */
constexpr double kMaximumPitchUnits = 22.0;

/* PS1 digital-pad bits are active-low: clearing a bit presses it. */
constexpr uint16_t kPadSelect   = 1u << 0;
constexpr uint16_t kPadStart    = 1u << 3;
constexpr uint16_t kPadUp       = 1u << 4;
constexpr uint16_t kPadRight    = 1u << 5;
constexpr uint16_t kPadDown     = 1u << 6;
constexpr uint16_t kPadLeft     = 1u << 7;
constexpr uint16_t kPadL2       = 1u << 8;
constexpr uint16_t kPadR2       = 1u << 9;
constexpr uint16_t kPadL1       = 1u << 10;
constexpr uint16_t kPadR1       = 1u << 11;
constexpr uint16_t kPadTriangle = 1u << 12;
constexpr uint16_t kPadCircle   = 1u << 13;
constexpr uint16_t kPadCross    = 1u << 14;
constexpr uint16_t kPadSquare   = 1u << 15;

struct MouseAimState {
    bool enabled = false;
    bool mouse_aim_enabled = false;
    bool vertical_look_enabled = false;
    bool modern_controls_enabled = false;
    bool high_precision_camera = false;
    bool configured = false;
    bool captured = false;
    bool middle_was_down = false;
    bool escape_was_down = false;
    bool invert_horizontal = false;
    bool invert_vertical = false;
    double horizontal_sensitivity = kDefaultHorizontalSensitivity;
    double vertical_sensitivity = kDefaultVerticalSensitivity;
    double fractional_yaw = 0.0;
    double vertical_pitch = 0.0;
    uint64_t frame = 0;
    uint64_t motion_samples = 0;
    double interval_mouse_x = 0.0;
    double interval_mouse_y = 0.0;
    int64_t interval_yaw_steps = 0;
    double interval_pitch_delta = 0.0;
    uint16_t interval_modern_press_mask = 0;
    std::FILE *log = nullptr;
};

MouseAimState g_mouse;

std::string trim_copy(const std::string& value) {
    size_t first = 0;
    while (first < value.size() &&
           (value[first] == ' ' || value[first] == '\t' ||
            value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    size_t last = value.size();
    while (last > first &&
           (value[last - 1] == ' ' || value[last - 1] == '\t' ||
            value[last - 1] == '\r' || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

bool parse_bool(const std::string& value, bool fallback) {
    std::string lowered = trim_copy(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
                   });
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on")
        return true;
    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off")
        return false;
    return fallback;
}

double parse_sensitivity(const std::string& value, double fallback) {
    const std::string trimmed = trim_copy(value);
    if (trimmed.empty()) return fallback;
    char *tail = nullptr;
    const double parsed = std::strtod(trimmed.c_str(), &tail);
    if (tail == trimmed.c_str() || !std::isfinite(parsed)) return fallback;
    while (*tail == ' ' || *tail == '\t' || *tail == '\r' || *tail == '\n') ++tail;
    if (*tail != '\0') return fallback;
    return std::clamp(parsed, kMinimumSensitivity, kMaximumSensitivity);
}

bool env_enabled(const char *name) {
    const char *value = std::getenv(name);
    return value && value[0] != '\0' && !(
        std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 ||
        std::strcmp(value, "FALSE") == 0 || std::strcmp(value, "off") == 0 ||
        std::strcmp(value, "OFF") == 0);
}

void load_configuration() {
    if (g_mouse.configured) return;
    g_mouse.configured = true;

    std::ifstream input("mouse-aim.ini");
    std::string line;
    while (std::getline(input, line)) {
        const size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) line.erase(comment);
        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim_copy(line.substr(0, equals));
        const std::string value = trim_copy(line.substr(equals + 1));
        if (key == "horizontal_sensitivity") {
            g_mouse.horizontal_sensitivity =
                parse_sensitivity(value, g_mouse.horizontal_sensitivity);
        } else if (key == "invert_horizontal") {
            g_mouse.invert_horizontal = parse_bool(value, g_mouse.invert_horizontal);
        } else if (key == "vertical_sensitivity") {
            g_mouse.vertical_sensitivity =
                parse_sensitivity(value, g_mouse.vertical_sensitivity);
        } else if (key == "invert_vertical") {
            g_mouse.invert_vertical = parse_bool(value, g_mouse.invert_vertical);
        }
    }

    if (const char *value = std::getenv("PSX_DISRUPTOR_MOUSE_SENSITIVITY")) {
        g_mouse.horizontal_sensitivity =
            parse_sensitivity(value, g_mouse.horizontal_sensitivity);
    }
    if (const char *value = std::getenv("PSX_DISRUPTOR_MOUSE_INVERT_X")) {
        g_mouse.invert_horizontal = parse_bool(value, g_mouse.invert_horizontal);
    }
    if (const char *value = std::getenv("PSX_DISRUPTOR_MOUSE_SENSITIVITY_Y")) {
        g_mouse.vertical_sensitivity =
            parse_sensitivity(value, g_mouse.vertical_sensitivity);
    }
    if (const char *value = std::getenv("PSX_DISRUPTOR_MOUSE_INVERT_Y")) {
        g_mouse.invert_vertical = parse_bool(value, g_mouse.invert_vertical);
    }
}

void set_title(const char *message) {
    if (!sdl_window) return;
    const char *mode = g_mouse.modern_controls_enabled
        ? "Modern Controls" : "Mouse Aim";
    const std::string title = std::string("Disruptor Recompiled - ") +
                              mode + ": " + message;
    SDL_SetWindowTitle(sdl_window, title.c_str());
}

void open_log() {
    if (g_mouse.log) return;
    g_mouse.log = std::fopen("disruptor-mouse-aim.log", "wb");
    if (!g_mouse.log) return;
    std::fprintf(g_mouse.log,
                 "DISRUPTOR MODERN CONTROLS AND MOUSE CAMERA v3\n"
                 "yaw_address=0x%08X\n"
                 "mouse_aim=%s\n"
                 "vertical_look=%s\n"
                 "modern_controls=%s\n"
                 "high_precision_camera=%s\n"
                 "horizontal_sensitivity=%.6f\n"
                 "invert_horizontal=%s\n"
                 "vertical_sensitivity=%.6f\n"
                 "invert_vertical=%s\n"
                 "vertical_pitch_limit=%.6f\n"
                 "capture=middle-click release=middle-click-or-escape\n"
                 "keyboard=W/S:forward/back A/D:strafe Space:jump E:use "
                 "F:psionic Q:weapon R:psionic-select Tab:map P:pause\n"
                 "mouse=left:fire right:psionic X1:weapon X2:psionic-select\n",
                 kPlayerYawAddress,
                 g_mouse.mouse_aim_enabled ? "true" : "false",
                 g_mouse.vertical_look_enabled ? "true" : "false",
                 g_mouse.modern_controls_enabled ? "true" : "false",
                 g_mouse.high_precision_camera ? "true" : "false",
                 g_mouse.horizontal_sensitivity,
                 g_mouse.invert_horizontal ? "true" : "false",
                 g_mouse.vertical_sensitivity,
                 g_mouse.invert_vertical ? "true" : "false",
                 kMaximumPitchUnits);
    std::fflush(g_mouse.log);
}

void log_event(const char *event) {
    if (!g_mouse.log) return;
    std::fprintf(g_mouse.log, "event=%s frame=%llu yaw=0x%02X\n", event,
                 static_cast<unsigned long long>(g_mouse.frame),
                 static_cast<unsigned>(psx_read_byte(kPlayerYawAddress)));
    std::fflush(g_mouse.log);
}

bool set_relative_mouse_mode(bool enabled) {
    if (!sdl_window) return false;
#if defined(PSX_SDL3)
    return SDL_SetWindowRelativeMouseMode(sdl_window, enabled);
#else
    return SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE) == 0;
#endif
}

uint32_t get_mouse_buttons() {
#if defined(PSX_SDL3)
    float x = 0.0f;
    float y = 0.0f;
    return static_cast<uint32_t>(SDL_GetMouseState(&x, &y));
#else
    int x = 0;
    int y = 0;
    return static_cast<uint32_t>(SDL_GetMouseState(&x, &y));
#endif
}

void discard_relative_motion() {
#if defined(PSX_SDL3)
    float x = 0.0f;
    float y = 0.0f;
    (void)SDL_GetRelativeMouseState(&x, &y);
#else
    int x = 0;
    int y = 0;
    (void)SDL_GetRelativeMouseState(&x, &y);
#endif
}

struct RelativeMotion {
    double x = 0.0;
    double y = 0.0;
};

RelativeMotion take_relative_motion() {
#if defined(PSX_SDL3)
    float x = 0.0f;
    float y = 0.0f;
    (void)SDL_GetRelativeMouseState(&x, &y);
    return {static_cast<double>(x), static_cast<double>(y)};
#else
    int x = 0;
    int y = 0;
    (void)SDL_GetRelativeMouseState(&x, &y);
    return {static_cast<double>(x), static_cast<double>(y)};
#endif
}

void set_capture(bool captured, const char *reason) {
    if (captured && !g_mouse.mouse_aim_enabled &&
        !g_mouse.vertical_look_enabled &&
        !g_mouse.modern_controls_enabled) {
        return;
    }
    if (captured == g_mouse.captured) return;
    if (!set_relative_mouse_mode(captured)) {
        set_title("ERROR enabling relative mode; see log");
        if (g_mouse.log) {
            std::fprintf(g_mouse.log, "event=relative-mode-error requested=%s error=%s\n",
                         captured ? "capture" : "release", SDL_GetError());
            std::fflush(g_mouse.log);
        }
        return;
    }

    g_mouse.captured = captured;
    g_mouse.fractional_yaw = 0.0;
    gpu_geometry_camera_yaw_residual_set(0.0);
    discard_relative_motion();
    log_event(reason);
    if (captured) {
        set_title(g_mouse.modern_controls_enabled
            ? "CAPTURED - LMB fire, RMB psionic; middle-click or Esc releases"
            : "CAPTURED - move to aim; middle-click or Esc releases");
    } else {
        set_title("released - middle-click in live gameplay to capture");
    }
}

bool key_down(const Uint8 *keys, int scancode) {
    return keys && scancode >= 0 && keys[scancode] != 0;
}

/* Merge host controls into the already sampled port-1 pad word.  This runs
 * after the runtime's final low-latency sample, so releases are naturally
 * refreshed on the next frame and no synthetic button can stick.  Keyboard
 * controls remain live while uncaptured; mouse actions require capture so a
 * desktop click cannot fire into gameplay accidentally. */
void apply_modern_controls(const Uint8 *keys, uint32_t mouse_buttons) {
    if (!g_mouse.modern_controls_enabled) return;

    uint16_t press = 0;
    if (key_down(keys, SDL_SCANCODE_W) || key_down(keys, SDL_SCANCODE_UP))
        press |= kPadUp;
    if (key_down(keys, SDL_SCANCODE_S) || key_down(keys, SDL_SCANCODE_DOWN))
        press |= kPadDown;
    if (key_down(keys, SDL_SCANCODE_LEFT))  press |= kPadLeft;
    if (key_down(keys, SDL_SCANCODE_RIGHT)) press |= kPadRight;
    if (key_down(keys, SDL_SCANCODE_A))     press |= kPadL2;
    if (key_down(keys, SDL_SCANCODE_D))     press |= kPadR2;
    if (key_down(keys, SDL_SCANCODE_SPACE)) press |= kPadCircle;
    if (key_down(keys, SDL_SCANCODE_E))     press |= kPadTriangle;
    if (key_down(keys, SDL_SCANCODE_F))     press |= kPadSquare;
    if (key_down(keys, SDL_SCANCODE_Q))     press |= kPadL1;
    if (key_down(keys, SDL_SCANCODE_R))     press |= kPadR1;
    if (key_down(keys, SDL_SCANCODE_TAB))   press |= kPadSelect;
    if (key_down(keys, SDL_SCANCODE_P))     press |= kPadStart;
    if (key_down(keys, SDL_SCANCODE_RETURN)) press |= kPadCross;

    if (g_mouse.captured) {
        if ((mouse_buttons & SDL_BUTTON_LMASK) != 0)  press |= kPadCross;
        if ((mouse_buttons & SDL_BUTTON_RMASK) != 0)  press |= kPadSquare;
        if ((mouse_buttons & SDL_BUTTON_X1MASK) != 0) press |= kPadL1;
        if ((mouse_buttons & SDL_BUTTON_X2MASK) != 0) press |= kPadR1;
    }

    if (press != 0) {
        const uint16_t sampled = sio_get_pad_buttons_slot(0);
        sio_set_pad_state_slot(0, static_cast<uint16_t>(sampled & ~press));
        g_mouse.interval_modern_press_mask |= press;
    }
}

void apply_mouse_x(double raw_x) {
    if (raw_x == 0.0) return;
    raw_x = std::clamp(raw_x, -kMaximumMotionPerFrame, kMaximumMotionPerFrame);
    const double direction = g_mouse.invert_horizontal ? 1.0 : -1.0;
    g_mouse.fractional_yaw +=
        raw_x * g_mouse.horizontal_sensitivity * direction;

    /* Quantize to the nearest retail yaw byte and carry the signed error into
     * the next sample.  The previous toward-zero extraction could retain
     * almost one whole angular unit, making very small movements wait longer
     * and then arrive as a visible byte-sized jump.  Nearest-step error
     * diffusion bounds that discrepancy to half a unit without averaging,
     * predicting future input, or changing the cumulative mouse distance.
     * The carried error remains the exact sub-byte presentation correction. */
    const double whole = std::round(g_mouse.fractional_yaw);
    if (whole == 0.0) return;
    int yaw_steps = static_cast<int>(whole);
    yaw_steps = std::clamp(yaw_steps, -64, 64);
    g_mouse.fractional_yaw -= static_cast<double>(yaw_steps);
    if (std::abs(whole) > 64.0) g_mouse.fractional_yaw = 0.0;

    const uint8_t old_yaw = psx_read_byte(kPlayerYawAddress);
    const uint8_t new_yaw = static_cast<uint8_t>(
        static_cast<int>(old_yaw) + yaw_steps);
    psx_write_byte(kPlayerYawAddress, new_yaw);
    ++g_mouse.motion_samples;
    g_mouse.interval_mouse_x += raw_x;
    g_mouse.interval_yaw_steps += yaw_steps;
}

void reset_vertical_pitch(const char *event) {
    g_mouse.vertical_pitch = 0.0;
    disruptor_vertical_camera_recenter();
    if (event) log_event(event);
}

void apply_mouse_y(double raw_y) {
    if (raw_y == 0.0) return;
    raw_y = std::clamp(raw_y, -kMaximumMotionPerFrame,
                       kMaximumMotionPerFrame);
    const double direction = g_mouse.invert_vertical ? 1.0 : -1.0;
    const double old_pitch = g_mouse.vertical_pitch;
    const double new_pitch = std::clamp(
        old_pitch + raw_y * g_mouse.vertical_sensitivity * direction,
        -kMaximumPitchUnits, kMaximumPitchUnits);

    g_mouse.interval_mouse_y += raw_y;
    g_mouse.interval_pitch_delta += new_pitch - old_pitch;
    if (new_pitch == old_pitch) return;
    g_mouse.vertical_pitch = new_pitch;
    disruptor_vertical_camera_set_requested_pitch(new_pitch);
}

void log_u64_array(std::FILE *log, const char *key,
                   const uint64_t *values, size_t count) {
    std::fprintf(log, " %s=", key);
    for (size_t i = 0; i < count; ++i) {
        std::fprintf(log, "%s%llu", i == 0 ? "" : ",",
                     static_cast<unsigned long long>(values[i]));
    }
}

void log_geometry_diagnostics_scope(
        const char *scope, uint64_t frame,
        const GpuGeometryCorrectionStats& snapshot,
        const GpuGeometryCorrectionCounters& counters) {
    std::fprintf(
        g_mouse.log,
        "geometry_diag version=%u size=%u frame=%llu scope=%s "
        "presentation=%llu corrected_presentations=%llu "
        "latest_live_presentation=%llu latest_live_valid=%u "
        "triangle_candidates=%llu triangle_accepted=%llu "
        "polygon_candidates=%llu polygon_accepted=%llu "
        "vertex_candidates=%llu vertex_accepted=%llu "
        "partial_polygon_rejections=%llu partial_quad_rejections=%llu "
        "correction_vertices=%llu "
        "correction_abs_x16_sum=%llu correction_abs_y16_sum=%llu "
        "correction_magnitude16_sum=%llu correction_magnitude16_max=%llu "
        "correction_x16_squared_sum=%llu correction_y16_squared_sum=%llu "
        "correction_accumulators_saturated=%llu "
        "provenance_store_collisions=%llu provenance_store_evictions=%llu "
        "provenance_lookup_collisions=%llu "
        "provenance_store_uncommitted_rejections=%llu "
        "provenance_registered_store_attempts=%llu "
        "provenance_registered_store_accepts=%llu "
        "provenance_registered_store_packed_rejections=%llu "
        "provenance_copy_load_attempts=%llu "
        "provenance_copy_load_accepts=%llu "
        "provenance_copy_store_attempts=%llu "
        "provenance_copy_store_accepts=%llu "
        "provenance_copy_store_packed_rejections=%llu",
        snapshot.version, snapshot.size,
        static_cast<unsigned long long>(frame), scope,
        static_cast<unsigned long long>(snapshot.presentation_sequence),
        static_cast<unsigned long long>(snapshot.corrected_presentations),
        static_cast<unsigned long long>(snapshot.latest_live_presentation),
        snapshot.latest_live_valid,
        static_cast<unsigned long long>(counters.triangle_candidates),
        static_cast<unsigned long long>(counters.triangle_accepted),
        static_cast<unsigned long long>(counters.polygon_candidates),
        static_cast<unsigned long long>(counters.polygon_accepted),
        static_cast<unsigned long long>(counters.vertex_candidates),
        static_cast<unsigned long long>(counters.vertex_accepted),
        static_cast<unsigned long long>(counters.partial_polygon_rejections),
        static_cast<unsigned long long>(counters.partial_quad_rejections),
        static_cast<unsigned long long>(counters.correction_vertices),
        static_cast<unsigned long long>(counters.correction_abs_x16_sum),
        static_cast<unsigned long long>(counters.correction_abs_y16_sum),
        static_cast<unsigned long long>(counters.correction_magnitude16_sum),
        static_cast<unsigned long long>(counters.correction_magnitude16_max),
        static_cast<unsigned long long>(counters.correction_x16_squared_sum),
        static_cast<unsigned long long>(counters.correction_y16_squared_sum),
        static_cast<unsigned long long>(
            counters.correction_accumulators_saturated),
        static_cast<unsigned long long>(snapshot.provenance_store_collisions),
        static_cast<unsigned long long>(snapshot.provenance_store_evictions),
        static_cast<unsigned long long>(snapshot.provenance_lookup_collisions),
        static_cast<unsigned long long>(
            snapshot.provenance_store_uncommitted_rejections),
        static_cast<unsigned long long>(
            snapshot.provenance_registered_store_attempts),
        static_cast<unsigned long long>(
            snapshot.provenance_registered_store_accepts),
        static_cast<unsigned long long>(
            snapshot.provenance_registered_store_packed_rejections),
        static_cast<unsigned long long>(
            snapshot.provenance_copy_load_attempts),
        static_cast<unsigned long long>(
            snapshot.provenance_copy_load_accepts),
        static_cast<unsigned long long>(
            snapshot.provenance_copy_store_attempts),
        static_cast<unsigned long long>(
            snapshot.provenance_copy_store_accepts),
        static_cast<unsigned long long>(
            snapshot.provenance_copy_store_packed_rejections));
    log_u64_array(g_mouse.log, "reject", counters.vertex_rejections,
                  GPU_GEOMETRY_REJECT_REASON_COUNT);
    log_u64_array(g_mouse.log, "primitive_candidates",
                  counters.primitive_candidates,
                  GPU_GEOMETRY_PRIMITIVE_CLASS_COUNT);
    log_u64_array(g_mouse.log, "primitive_accepted",
                  counters.primitive_accepted,
                  GPU_GEOMETRY_PRIMITIVE_CLASS_COUNT);
    log_u64_array(g_mouse.log, "opcode_candidates",
                  counters.opcode_candidates, GPU_GEOMETRY_OPCODE_COUNT);
    log_u64_array(g_mouse.log, "opcode_accepted",
                  counters.opcode_accepted, GPU_GEOMETRY_OPCODE_COUNT);
    log_u64_array(g_mouse.log, "depth_vertices", counters.depth_vertices,
                  GPU_GEOMETRY_DEPTH_BUCKET_COUNT);
    log_u64_array(g_mouse.log, "screen_vertices", counters.screen_vertices,
                  GPU_GEOMETRY_SCREEN_BUCKET_COUNT);
    log_u64_array(g_mouse.log, "correction_buckets",
                  counters.correction_magnitude_buckets,
                  GPU_GEOMETRY_CORRECTION_BUCKET_COUNT);
    std::fputc('\n', g_mouse.log);
}

void mouse_aim_frame() {
    if (!g_mouse.enabled || !sdl_window) return;
    ++g_mouse.frame;
    load_configuration();
    open_log();

    const bool vertical_netplay_blocked = psx_netplay_active() != 0;
    if (vertical_netplay_blocked && g_mouse.vertical_pitch != 0.0)
        reset_vertical_pitch("netplay-vertical-recenter");

    /* The host menu consumes polled state as well as SDL events.  Keep the
     * edge detectors current so a button held while closing cannot toggle
     * capture or inject a guest action on the following frame. */
    if (psx_host_ui_game_input_captured()) {
        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        const uint32_t buttons = get_mouse_buttons();
        g_mouse.escape_was_down =
            keys && keys[SDL_SCANCODE_ESCAPE] != 0;
        g_mouse.middle_was_down = (buttons & SDL_BUTTON_MMASK) != 0;
        discard_relative_motion();
        gpu_geometry_camera_yaw_residual_set(0.0);
        return;
    }

    static bool announced = false;
    if (!announced) {
        announced = true;
        set_title("middle-click in live gameplay to capture");
        log_event("ready");
    }

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    const bool escape_down = keys && keys[SDL_SCANCODE_ESCAPE] != 0;
    const uint32_t buttons = get_mouse_buttons();
    const bool middle_down = (buttons & SDL_BUTTON_MMASK) != 0;

    if (g_mouse.captured &&
        (SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_INPUT_FOCUS) == 0) {
        set_capture(false, "focus-lost-release");
    } else if (middle_down && !g_mouse.middle_was_down) {
        set_capture(!g_mouse.captured,
                    g_mouse.captured ? "middle-release" : "middle-capture");
    } else if (g_mouse.captured && escape_down && !g_mouse.escape_was_down) {
        set_capture(false, "escape-release");
    }

    g_mouse.middle_was_down = middle_down;
    g_mouse.escape_was_down = escape_down;

    apply_modern_controls(keys, buttons);
    if (g_mouse.captured &&
        (g_mouse.mouse_aim_enabled || g_mouse.vertical_look_enabled)) {
        /* SDL drains both relative axes in one call.  Keep this a single
         * coherent sample so enabling one axis can never consume the other. */
        const RelativeMotion motion = take_relative_motion();
        if (g_mouse.mouse_aim_enabled) apply_mouse_x(motion.x);
        if (g_mouse.vertical_look_enabled && !vertical_netplay_blocked &&
            disruptor_vertical_camera_input_allowed())
            apply_mouse_y(motion.y);
    } else if (g_mouse.captured) {
        discard_relative_motion();
    }

    /* The retail yaw byte remains authoritative for gameplay.  The sub-byte
     * remainder is presentation metadata only, consumed by the independent
     * geometry-correction surface; releasing capture immediately restores the
     * exact retail camera. */
    gpu_geometry_camera_yaw_residual_set(
        g_mouse.high_precision_camera && g_mouse.captured &&
                g_mouse.mouse_aim_enabled
            ? g_mouse.fractional_yaw
            : 0.0);

    if (g_mouse.log && g_mouse.frame % 60u == 0u) {
        uint64_t geometry_world = 0;
        uint64_t geometry_precise = 0;
        const uint32_t texture_perspective = gpu_texture_correction_hits();
        GpuGeometryCorrectionStats geometry_diagnostics{};
        gpu_geometry_correction_stats(&geometry_world, &geometry_precise);
        gpu_geometry_correction_stats_detailed(
            &geometry_diagnostics,
            static_cast<uint32_t>(sizeof(geometry_diagnostics)));
        std::fprintf(g_mouse.log,
                     "sample frame=%llu captured=%s mouse_x=%.3f mouse_y=%.3f "
                     "yaw_steps=%lld pitch_delta=%.6f pitch=%.6f "
                     "yaw=0x%02X yaw_residual=%.6f motion_samples=%llu "
                      "modern_press_mask=0x%04X geometry_world=%llu "
                      "geometry_precise=%llu "
                      "texture_perspective=%u "
                      "ws_margin=%d ws_native_active=%d ws_pinned_43=%d "
                     "ws_extra=%d\n",
                     static_cast<unsigned long long>(g_mouse.frame),
                     g_mouse.captured ? "true" : "false",
                     g_mouse.interval_mouse_x,
                     g_mouse.interval_mouse_y,
                     static_cast<long long>(g_mouse.interval_yaw_steps),
                     g_mouse.interval_pitch_delta,
                     g_mouse.vertical_pitch,
                     static_cast<unsigned>(psx_read_byte(kPlayerYawAddress)),
                     g_mouse.fractional_yaw,
                     static_cast<unsigned long long>(g_mouse.motion_samples),
                     static_cast<unsigned>(g_mouse.interval_modern_press_mask),
                      static_cast<unsigned long long>(geometry_world),
                      static_cast<unsigned long long>(geometry_precise),
                      static_cast<unsigned>(texture_perspective),
                     static_cast<int>(psx_mod_widescreen_x_margin()),
                     ws_native_wide_active(), gpu_ws_present_native_43(),
                     ws_nw_extra());
        log_geometry_diagnostics_scope(
            "cumulative", g_mouse.frame, geometry_diagnostics,
            geometry_diagnostics.cumulative);
        log_geometry_diagnostics_scope(
            "latest_live", g_mouse.frame, geometry_diagnostics,
            geometry_diagnostics.latest_live);
        std::fflush(g_mouse.log);
        g_mouse.interval_mouse_x = 0.0;
        g_mouse.interval_mouse_y = 0.0;
        g_mouse.interval_yaw_steps = 0;
        g_mouse.interval_pitch_delta = 0.0;
        g_mouse.interval_modern_press_mask = 0;
    }
}

struct MouseAimRegistration {
    MouseAimRegistration() {
        g_mouse.mouse_aim_enabled =
            env_enabled("PSX_DISRUPTOR_MOUSE_AIM");
        g_mouse.vertical_look_enabled =
            env_enabled("PSX_DISRUPTOR_VERTICAL_LOOK");
        g_mouse.modern_controls_enabled =
            env_enabled("PSX_DISRUPTOR_MODERN_CONTROLS");
        g_mouse.high_precision_camera =
            env_enabled("PSX_DISRUPTOR_HIGH_PRECISION_CAMERA");
        g_mouse.enabled = g_mouse.mouse_aim_enabled ||
                          g_mouse.vertical_look_enabled ||
                          g_mouse.modern_controls_enabled ||
                          g_mouse.high_precision_camera;
        /* Keep one dormant hook installed even when every launch-time option
         * is off.  The in-game settings menu can then enable controls without
         * restarting; mouse_aim_frame() immediately returns while dormant. */
        mod_register_frame_hook(mouse_aim_frame);
    }
};

MouseAimRegistration g_registration;

}  // namespace

namespace {

void reset_presentation_residual() {
    g_mouse.fractional_yaw = 0.0;
    gpu_geometry_camera_yaw_residual_set(0.0);
}

void refresh_live_control_state(const char *event) {
    g_mouse.enabled = g_mouse.mouse_aim_enabled ||
                      g_mouse.vertical_look_enabled ||
                      g_mouse.modern_controls_enabled ||
                      g_mouse.high_precision_camera;

    if ((!g_mouse.mouse_aim_enabled || !g_mouse.high_precision_camera) &&
        g_mouse.fractional_yaw != 0.0) {
        reset_presentation_residual();
    } else if (!g_mouse.mouse_aim_enabled ||
               !g_mouse.high_precision_camera) {
        gpu_geometry_camera_yaw_residual_set(0.0);
    }

    /* Relative capture is only meaningful for the two input features.  Do
     * not leave the desktop pointer trapped when both are disabled live. */
    if (!g_mouse.mouse_aim_enabled &&
        !g_mouse.vertical_look_enabled &&
        !g_mouse.modern_controls_enabled && g_mouse.captured) {
        set_capture(false, "settings-disabled-release");
    }

    if (event) log_event(event);
}

}  // namespace

extern "C" int disruptor_mouse_aim_enabled(void) {
    return g_mouse.mouse_aim_enabled ? 1 : 0;
}

extern "C" void disruptor_mouse_aim_set_enabled(int enabled) {
    load_configuration();
    const bool next = enabled != 0;
    if (g_mouse.mouse_aim_enabled == next) return;
    g_mouse.mouse_aim_enabled = next;
    reset_presentation_residual();
    refresh_live_control_state(next ? "settings-mouse-aim-on"
                                    : "settings-mouse-aim-off");
}

extern "C" int disruptor_modern_controls_enabled(void) {
    return g_mouse.modern_controls_enabled ? 1 : 0;
}

extern "C" void disruptor_modern_controls_set_enabled(int enabled) {
    load_configuration();
    const bool next = enabled != 0;
    if (g_mouse.modern_controls_enabled == next) return;
    g_mouse.modern_controls_enabled = next;
    refresh_live_control_state(next ? "settings-modern-controls-on"
                                    : "settings-modern-controls-off");
}

extern "C" int disruptor_high_precision_camera_enabled(void) {
    return g_mouse.high_precision_camera ? 1 : 0;
}

extern "C" void disruptor_high_precision_camera_set_enabled(int enabled) {
    load_configuration();
    const bool next = enabled != 0;
    if (g_mouse.high_precision_camera == next) return;
    g_mouse.high_precision_camera = next;
    reset_presentation_residual();
    refresh_live_control_state(next ? "settings-high-precision-on"
                                    : "settings-high-precision-off");
}

extern "C" double disruptor_mouse_horizontal_sensitivity(void) {
    load_configuration();
    return g_mouse.horizontal_sensitivity;
}

extern "C" double disruptor_mouse_set_horizontal_sensitivity(
        double sensitivity) {
    load_configuration();
    if (!std::isfinite(sensitivity)) return g_mouse.horizontal_sensitivity;
    const double next = std::clamp(sensitivity,
                                   kMinimumSensitivity,
                                   kMaximumSensitivity);
    if (g_mouse.horizontal_sensitivity == next)
        return g_mouse.horizontal_sensitivity;
    g_mouse.horizontal_sensitivity = next;
    reset_presentation_residual();
    log_event("settings-sensitivity");
    return g_mouse.horizontal_sensitivity;
}

extern "C" int disruptor_mouse_invert_horizontal(void) {
    load_configuration();
    return g_mouse.invert_horizontal ? 1 : 0;
}

extern "C" void disruptor_mouse_set_invert_horizontal(int inverted) {
    load_configuration();
    const bool next = inverted != 0;
    if (g_mouse.invert_horizontal == next) return;
    g_mouse.invert_horizontal = next;
    reset_presentation_residual();
    log_event(next ? "settings-invert-x-on" : "settings-invert-x-off");
}

extern "C" int disruptor_mouse_vertical_look_enabled(void) {
    return g_mouse.vertical_look_enabled ? 1 : 0;
}

extern "C" void disruptor_mouse_set_vertical_look_enabled(int enabled) {
    load_configuration();
    const bool next = enabled != 0;
    if (g_mouse.vertical_look_enabled == next) return;
    g_mouse.vertical_look_enabled = next;
    if (next) {
        if (!psx_netplay_active())
            disruptor_vertical_camera_set_requested_pitch(
                g_mouse.vertical_pitch);
    } else {
        reset_vertical_pitch("settings-vertical-look-recenter");
    }
    refresh_live_control_state(next ? "settings-vertical-look-on"
                                    : "settings-vertical-look-off");
}

extern "C" double disruptor_mouse_vertical_sensitivity(void) {
    load_configuration();
    return g_mouse.vertical_sensitivity;
}

extern "C" double disruptor_mouse_set_vertical_sensitivity(
        double sensitivity) {
    load_configuration();
    if (!std::isfinite(sensitivity)) return g_mouse.vertical_sensitivity;
    const double next = std::clamp(sensitivity,
                                   kMinimumSensitivity,
                                   kMaximumSensitivity);
    if (g_mouse.vertical_sensitivity == next)
        return g_mouse.vertical_sensitivity;
    g_mouse.vertical_sensitivity = next;
    log_event("settings-vertical-sensitivity");
    return g_mouse.vertical_sensitivity;
}

extern "C" int disruptor_mouse_invert_vertical(void) {
    load_configuration();
    return g_mouse.invert_vertical ? 1 : 0;
}

extern "C" void disruptor_mouse_set_invert_vertical(int inverted) {
    load_configuration();
    const bool next = inverted != 0;
    if (g_mouse.invert_vertical == next) return;
    g_mouse.invert_vertical = next;
    log_event(next ? "settings-invert-y-on" : "settings-invert-y-off");
}

extern "C" double disruptor_mouse_vertical_pitch(void) {
    return g_mouse.vertical_pitch;
}

extern "C" void disruptor_mouse_recenter_vertical(void) {
    load_configuration();
    reset_vertical_pitch("settings-vertical-recenter");
}

extern "C" int disruptor_mouse_captured(void) {
    return g_mouse.captured ? 1 : 0;
}

extern "C" int disruptor_mouse_set_captured(int captured) {
    load_configuration();
    const bool next = captured != 0;
    if (next && !g_mouse.mouse_aim_enabled &&
        !g_mouse.vertical_look_enabled &&
        !g_mouse.modern_controls_enabled) {
        return 0;
    }
    set_capture(next, next ? "settings-capture" : "settings-release");
    if (next && g_mouse.captured) {
        /* A host UI can restore capture from inside its SDL event callback.
         * Synchronise the polled edge detectors with that same event so a
         * still-held Escape or middle button cannot immediately release the
         * newly restored capture in mouse_aim_frame(). */
        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        const uint32_t buttons = get_mouse_buttons();
        g_mouse.escape_was_down =
            keys && keys[SDL_SCANCODE_ESCAPE] != 0;
        g_mouse.middle_was_down = (buttons & SDL_BUTTON_MMASK) != 0;
    }
    return g_mouse.captured == next ? 1 : 0;
}
