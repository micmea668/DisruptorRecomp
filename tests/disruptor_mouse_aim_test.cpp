#include "psx_sdl.h"
#include "../psxrecomp-overlay/runtime/include/gpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

SDL_Window g_window;
std::array<std::uint8_t, 2 * 1024 * 1024> g_ram{};
std::array<Uint8, 512> g_keys{};
void (*g_registered_hook)(void) = nullptr;
std::uint32_t g_buttons = 0;
std::uint16_t g_pad_buttons = 0xFFFFu;
float g_relative_x = 0.0f;
float g_relative_y = 0.0f;
SDL_WindowFlags g_window_flags = SDL_WINDOW_INPUT_FOCUS;
bool g_relative_mode = false;
bool g_relative_mode_succeeds = true;
std::string g_title;
int g_failures = 0;
int g_ws_margin = 0;
int g_ws_pinned_43 = 1;
int g_ws_native_active = 0;
int g_ws_extra = 0;
double g_camera_yaw_residual = 0.0;
int g_camera_yaw_residual_calls = 0;
int g_host_ui_captured = 0;
double g_requested_vertical_pitch = 0.0;
int g_vertical_pitch_calls = 0;
int g_vertical_recenter_calls = 0;
int g_netplay_active = 0;
int g_vertical_input_allowed = 1;

constexpr std::uint32_t kTestYawAddress = 0x80077624u;

}  // namespace

extern "C" {

SDL_Window *sdl_window = &g_window;

void mod_register_frame_hook(void (*hook)(void)) {
    g_registered_hook = hook;
}

int psx_host_ui_game_input_captured(void) {
    return g_host_ui_captured;
}

std::uint8_t psx_read_byte(std::uint32_t address) {
    return g_ram[address & 0x001fffffu];
}

void psx_write_byte(std::uint32_t address, std::uint8_t value) {
    g_ram[address & 0x001fffffu] = value;
}

std::uint16_t sio_get_pad_buttons_slot(int slot) {
    return slot == 0 ? g_pad_buttons : 0xFFFFu;
}

void sio_set_pad_state_slot(int slot, std::uint16_t buttons) {
    if (slot == 0) g_pad_buttons = buttons;
}

std::int32_t psx_mod_widescreen_x_margin() {
    return g_ws_margin;
}

int gpu_ws_present_native_43() {
    return g_ws_pinned_43;
}

int ws_native_wide_active() {
    return g_ws_native_active;
}

int ws_nw_extra() {
    return g_ws_extra;
}

void gpu_geometry_camera_yaw_residual_set(double yaw_units) {
    g_camera_yaw_residual = yaw_units;
    ++g_camera_yaw_residual_calls;
}

void disruptor_vertical_camera_set_requested_pitch(double pitch_units) {
    g_requested_vertical_pitch = pitch_units;
    ++g_vertical_pitch_calls;
}

void disruptor_vertical_camera_recenter() {
    g_requested_vertical_pitch = 0.0;
    ++g_vertical_recenter_calls;
}

int disruptor_vertical_camera_input_allowed() {
    return g_vertical_input_allowed;
}

int psx_netplay_active() {
    return g_netplay_active;
}

void gpu_geometry_correction_stats(std::uint64_t *world_triangles,
                                   std::uint64_t *precise_triangles) {
    if (world_triangles) *world_triangles = 0;
    if (precise_triangles) *precise_triangles = 0;
}

std::uint32_t gpu_texture_correction_hits() {
    return 0;
}

void gpu_geometry_correction_stats_detailed(
        GpuGeometryCorrectionStats *stats, std::uint32_t out_size) {
    if (!stats || out_size == 0) return;
    GpuGeometryCorrectionStats snapshot{};
    snapshot.version = GPU_GEOMETRY_DIAGNOSTICS_VERSION;
    snapshot.size = static_cast<std::uint32_t>(sizeof(snapshot));
    const std::size_t copy_size = std::min<std::size_t>(out_size,
                                                        sizeof(snapshot));
    std::memcpy(stats, &snapshot, copy_size);
}

}  // extern "C"

bool SDL_SetWindowRelativeMouseMode(SDL_Window *, bool enabled) {
    if (!g_relative_mode_succeeds) return false;
    g_relative_mode = enabled;
    return true;
}

std::uint32_t SDL_GetMouseState(float *x, float *y) {
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    return g_buttons;
}

std::uint32_t SDL_GetRelativeMouseState(float *x, float *y) {
    if (x) *x = g_relative_x;
    if (y) *y = g_relative_y;
    g_relative_x = 0.0f;
    g_relative_y = 0.0f;
    return g_buttons;
}

const Uint8 *SDL_GetKeyboardState(int *count) {
    if (count) *count = static_cast<int>(g_keys.size());
    return g_keys.data();
}

SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *) {
    return g_window_flags;
}

bool SDL_SetWindowTitle(SDL_Window *, const char *title) {
    g_title = title ? title : "";
    return true;
}

const char *SDL_GetError() {
    return "mock relative-mode failure";
}

// Include the implementation so its internal pure helpers and state can be
// exercised against the mocks above without exposing a test-only production ABI.
#include "../src/disruptor_mouse_aim.cpp"

namespace {

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

void expect_close(double actual, double expected, const char *message) {
    expect(std::abs(actual - expected) < 0.000001, message);
}

void reset_state() {
    if (g_mouse.log) std::fclose(g_mouse.log);
    g_mouse = MouseAimState{};
    g_ram.fill(0);
    g_keys.fill(0);
    g_buttons = 0;
    g_pad_buttons = 0xFFFFu;
    g_relative_x = 0.0f;
    g_relative_y = 0.0f;
    g_window_flags = SDL_WINDOW_INPUT_FOCUS;
    g_relative_mode = false;
    g_relative_mode_succeeds = true;
    g_title.clear();
    g_ws_margin = 0;
    g_ws_pinned_43 = 1;
    g_ws_native_active = 0;
    g_ws_extra = 0;
    g_camera_yaw_residual = 0.0;
    g_camera_yaw_residual_calls = 0;
    g_host_ui_captured = 0;
    g_requested_vertical_pitch = 0.0;
    g_vertical_pitch_calls = 0;
    g_vertical_recenter_calls = 0;
    g_netplay_active = 0;
    g_vertical_input_allowed = 1;
    sdl_window = &g_window;
}

void test_parsing() {
    expect(parse_bool(" YES ", false), "boolean true parsing");
    expect(!parse_bool("off", true), "boolean false parsing");
    expect(parse_bool("invalid", true), "invalid boolean fallback");
    expect_close(parse_sensitivity("0.125", 0.08), 0.125,
                 "decimal sensitivity parsing");
    expect_close(parse_sensitivity("0", 0.08), 0.005,
                 "minimum sensitivity clamp");
    expect_close(parse_sensitivity("99", 0.08), 2.0,
                 "maximum sensitivity clamp");
    expect_close(parse_sensitivity("bad", 0.08), 0.08,
                 "invalid sensitivity fallback");
}

void test_ini_configuration() {
    reset_state();
    std::string original;
    bool had_original = false;
    {
        std::ifstream input("mouse-aim.ini", std::ios::binary);
        if (input) {
            had_original = true;
            original.assign(std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>());
        }
    }
    {
        std::ofstream output("mouse-aim.ini", std::ios::trunc);
        output << "# test configuration\n"
                  "horizontal_sensitivity = 0.375\n"
                  "invert_horizontal = yes\n"
                  "vertical_sensitivity = 0.250\n"
                  "invert_vertical = on\n";
    }
    load_configuration();
    expect_close(g_mouse.horizontal_sensitivity, 0.375,
                 "INI sensitivity load");
    expect(g_mouse.invert_horizontal, "INI inversion load");
    expect_close(g_mouse.vertical_sensitivity, 0.250,
                 "INI vertical sensitivity load");
    expect(g_mouse.invert_vertical, "INI vertical inversion load");
    if (had_original) {
        std::ofstream output("mouse-aim.ini",
                             std::ios::binary | std::ios::trunc);
        output << original;
    } else {
        std::remove("mouse-aim.ini");
    }
}

void test_direction_fraction_and_wrap() {
    reset_state();
    g_mouse.horizontal_sensitivity = 0.08;

    psx_write_byte(kTestYawAddress, 100);
    apply_mouse_x(25.0);
    expect(psx_read_byte(kTestYawAddress) == 98,
           "rightward motion decreases Disruptor yaw");
    apply_mouse_x(-25.0);
    expect(psx_read_byte(kTestYawAddress) == 100,
           "leftward motion increases Disruptor yaw");

    psx_write_byte(kTestYawAddress, 100);
    g_mouse.fractional_yaw = 0.0;
    apply_mouse_x(6.0);
    expect(psx_read_byte(kTestYawAddress) == 100,
           "sub-step motion is retained without premature yaw change");
    apply_mouse_x(7.0);
    expect(psx_read_byte(kTestYawAddress) == 99,
           "fractional motion accumulates into one yaw step");
    expect_close(g_mouse.fractional_yaw, -0.04,
                 "fractional remainder is preserved");

    g_mouse.horizontal_sensitivity = 1.0;
    g_mouse.fractional_yaw = 0.0;
    psx_write_byte(kTestYawAddress, 1);
    apply_mouse_x(4.0);
    expect(psx_read_byte(kTestYawAddress) == 253,
           "negative yaw wraps through zero");
    psx_write_byte(kTestYawAddress, 254);
    apply_mouse_x(-4.0);
    expect(psx_read_byte(kTestYawAddress) == 2,
           "positive yaw wraps through 255");

    g_mouse.invert_horizontal = true;
    psx_write_byte(kTestYawAddress, 10);
    apply_mouse_x(3.0);
    expect(psx_read_byte(kTestYawAddress) == 13,
           "horizontal inversion reverses direction");
}

void test_motion_safety_clamp() {
    reset_state();
    g_mouse.horizontal_sensitivity = 2.0;
    psx_write_byte(kTestYawAddress, 10);
    apply_mouse_x(5000.0);
    expect(psx_read_byte(kTestYawAddress) == 202,
           "single-frame yaw delta is clamped to 64 steps");
    expect_close(g_mouse.fractional_yaw, 0.0,
                 "pathological motion does not leave a large backlog");
}

void test_vertical_direction_inversion_and_clamp() {
    reset_state();
    g_mouse.configured = true;
    g_mouse.vertical_sensitivity = 0.08;

    apply_mouse_y(-25.0);
    expect_close(g_mouse.vertical_pitch, 2.0,
                 "mouse up requests positive look pitch");
    expect_close(g_requested_vertical_pitch, 2.0,
                 "vertical sink receives positive look pitch");
    apply_mouse_y(25.0);
    expect_close(g_mouse.vertical_pitch, 0.0,
                 "mouse down returns pitch toward centre");

    g_mouse.invert_vertical = true;
    apply_mouse_y(25.0);
    expect_close(g_mouse.vertical_pitch, 2.0,
                 "vertical inversion reverses direction");

    g_mouse.invert_vertical = false;
    apply_mouse_y(-5000.0);
    expect_close(g_mouse.vertical_pitch, 22.0,
                 "positive pitch is clamped to the safe weapon envelope");
    apply_mouse_y(-5000.0);
    expect_close(g_mouse.vertical_pitch, 22.0,
                 "motion into the pitch stop creates no backlog");
    apply_mouse_y(1.0);
    expect_close(g_mouse.vertical_pitch, 21.92,
                 "reversing at the pitch stop responds immediately");

    g_mouse.vertical_pitch = 0.0;
    apply_mouse_y(5000.0);
    expect_close(g_mouse.vertical_pitch, -22.0,
                 "negative pitch is clamped symmetrically");
}

void test_simultaneous_relative_axes_sample() {
    reset_state();
    g_mouse.enabled = true;
    g_mouse.mouse_aim_enabled = true;
    g_mouse.vertical_look_enabled = true;
    g_mouse.captured = true;
    g_mouse.configured = true;
    g_mouse.horizontal_sensitivity = 0.08;
    g_mouse.vertical_sensitivity = 0.08;
    g_mouse.log = std::tmpfile();
    psx_write_byte(kTestYawAddress, 100);
    g_relative_x = 25.0f;
    g_relative_y = -25.0f;

    mouse_aim_frame();

    expect(psx_read_byte(kTestYawAddress) == 98,
           "one relative sample applies horizontal motion");
    expect_close(g_mouse.vertical_pitch, 2.0,
                 "the same relative sample applies vertical motion");
    expect_close(g_relative_x, 0.0,
                 "relative X is drained once");
    expect_close(g_relative_y, 0.0,
                 "relative Y is drained once");

    reset_state();
    g_mouse.enabled = true;
    g_mouse.mouse_aim_enabled = true;
    g_mouse.captured = true;
    g_mouse.configured = true;
    g_mouse.log = std::tmpfile();
    g_relative_y = -25.0f;
    mouse_aim_frame();
    expect_close(g_mouse.vertical_pitch, 0.0,
                 "relative Y is inert while vertical look is disabled");
    expect_close(g_relative_y, 0.0,
                 "disabled vertical motion is still drained");
}

void test_capture_release_and_frame_motion() {
    reset_state();
    g_mouse.enabled = true;
    g_mouse.mouse_aim_enabled = true;
    g_mouse.configured = true;
    g_mouse.horizontal_sensitivity = 0.08;
    g_mouse.log = std::tmpfile();
    psx_write_byte(kTestYawAddress, 100);

    g_buttons = SDL_BUTTON_MMASK;
    mouse_aim_frame();
    expect(g_mouse.captured && g_relative_mode,
           "middle-click captures the relative mouse");

    g_buttons = 0;
    g_relative_x = 25.0f;
    mouse_aim_frame();
    expect(psx_read_byte(kTestYawAddress) == 98,
           "captured frame applies relative mouse motion");

    g_mouse.vertical_look_enabled = true;
    apply_mouse_y(-25.0);

    g_buttons = SDL_BUTTON_MMASK;
    mouse_aim_frame();
    expect(!g_mouse.captured && !g_relative_mode,
           "second middle-click releases the mouse");
    expect_close(g_mouse.vertical_pitch, 2.0,
                 "capture release preserves the requested vertical view");

    g_buttons = 0;
    mouse_aim_frame();
    g_buttons = SDL_BUTTON_MMASK;
    mouse_aim_frame();
    g_buttons = 0;
    mouse_aim_frame();
    g_keys[SDL_SCANCODE_ESCAPE] = 1;
    mouse_aim_frame();
    expect(!g_mouse.captured && !g_relative_mode,
           "Escape releases a captured mouse");

    g_keys[SDL_SCANCODE_ESCAPE] = 0;
    g_buttons = SDL_BUTTON_MMASK;
    mouse_aim_frame();
    g_buttons = 0;
    mouse_aim_frame();
    g_window_flags = 0;
    mouse_aim_frame();
    expect(!g_mouse.captured && !g_relative_mode,
           "focus loss releases a captured mouse");
}

void test_high_precision_camera_residual() {
    reset_state();
    g_mouse.enabled = true;
    g_mouse.mouse_aim_enabled = true;
    g_mouse.high_precision_camera = true;
    g_mouse.configured = true;
    g_mouse.horizontal_sensitivity = 0.08;
    g_mouse.log = std::tmpfile();
    psx_write_byte(kTestYawAddress, 100);

    g_buttons = SDL_BUTTON_MMASK;
    mouse_aim_frame();
    expect_close(g_camera_yaw_residual, 0.0,
                 "capture starts with zero presentation residual");

    g_buttons = 0;
    g_relative_x = 6.0f;
    mouse_aim_frame();
    expect(psx_read_byte(kTestYawAddress) == 100,
           "sub-step high-precision motion leaves retail yaw unchanged");
    expect_close(g_camera_yaw_residual, -0.48,
                 "sub-step remainder reaches the presentation camera");

    g_relative_x = 7.0f;
    mouse_aim_frame();
    expect(psx_read_byte(kTestYawAddress) == 99,
           "crossing the byte boundary still advances retail yaw");
    expect_close(g_camera_yaw_residual, -0.04,
                 "presentation residual is continuous across byte boundary");

    g_buttons = SDL_BUTTON_MMASK;
    mouse_aim_frame();
    expect(!g_mouse.captured, "high-precision test releases capture");
    expect_close(g_camera_yaw_residual, 0.0,
                 "release restores the exact retail presentation camera");
}

void expect_pressed(std::uint16_t mask, const char *message) {
    expect((g_pad_buttons & mask) == 0, message);
}

void expect_released(std::uint16_t mask, const char *message) {
    expect((g_pad_buttons & mask) == mask, message);
}

void test_modern_keyboard_mapping_and_merge() {
    reset_state();
    g_mouse.modern_controls_enabled = true;

    /* Preserve an action already supplied by the normal keyboard/controller
     * sampler while adding all of the modern keyboard actions. */
    g_pad_buttons = static_cast<std::uint16_t>(0xFFFFu & ~kPadSelect);
    g_keys[SDL_SCANCODE_W] = 1;
    g_keys[SDL_SCANCODE_S] = 1;
    g_keys[SDL_SCANCODE_A] = 1;
    g_keys[SDL_SCANCODE_D] = 1;
    g_keys[SDL_SCANCODE_SPACE] = 1;
    g_keys[SDL_SCANCODE_E] = 1;
    g_keys[SDL_SCANCODE_F] = 1;
    g_keys[SDL_SCANCODE_Q] = 1;
    g_keys[SDL_SCANCODE_R] = 1;
    g_keys[SDL_SCANCODE_P] = 1;
    g_keys[SDL_SCANCODE_RETURN] = 1;
    apply_modern_controls(g_keys.data(), 0);

    expect_pressed(kPadSelect, "existing sampled controller action is preserved");
    expect_pressed(kPadUp, "W maps to walk forward");
    expect_pressed(kPadDown, "S maps to walk backward");
    expect_pressed(kPadL2, "A maps to strafe left");
    expect_pressed(kPadR2, "D maps to strafe right");
    expect_pressed(kPadCircle, "Space maps to jump");
    expect_pressed(kPadTriangle, "E maps to use");
    expect_pressed(kPadSquare, "F maps to psionic attack");
    expect_pressed(kPadL1, "Q maps to weapon selection");
    expect_pressed(kPadR1, "R maps to psionic selection");
    expect_pressed(kPadStart, "P maps to pause");
    expect_pressed(kPadCross, "Return maps to confirm/fire");

    reset_state();
    g_mouse.modern_controls_enabled = true;
    g_keys[SDL_SCANCODE_UP] = 1;
    g_keys[SDL_SCANCODE_DOWN] = 1;
    g_keys[SDL_SCANCODE_LEFT] = 1;
    g_keys[SDL_SCANCODE_RIGHT] = 1;
    g_keys[SDL_SCANCODE_TAB] = 1;
    apply_modern_controls(g_keys.data(), 0);
    expect_pressed(kPadUp, "Up arrow remains a menu/gameplay fallback");
    expect_pressed(kPadDown, "Down arrow remains a menu/gameplay fallback");
    expect_pressed(kPadLeft, "Left arrow retains original turn/menu behavior");
    expect_pressed(kPadRight, "Right arrow retains original turn/menu behavior");
    expect_pressed(kPadSelect, "Tab maps to the automap");
}

void test_modern_mouse_buttons_require_capture() {
    reset_state();
    g_mouse.modern_controls_enabled = true;
    const std::uint32_t actions = SDL_BUTTON_LMASK | SDL_BUTTON_RMASK |
                                  SDL_BUTTON_X1MASK | SDL_BUTTON_X2MASK;

    apply_modern_controls(g_keys.data(), actions);
    expect_released(kPadCross | kPadSquare | kPadL1 | kPadR1,
                    "desktop mouse clicks do not reach the game while released");

    g_mouse.captured = true;
    apply_modern_controls(g_keys.data(), actions);
    expect_pressed(kPadCross, "left mouse maps to fire");
    expect_pressed(kPadSquare, "right mouse maps to psionic attack");
    expect_pressed(kPadL1, "mouse X1 maps to weapon selection");
    expect_pressed(kPadR1, "mouse X2 maps to psionic selection");
}

void test_live_settings_api() {
    reset_state();
    g_mouse.configured = true;

    expect(!disruptor_mouse_aim_enabled(),
           "mouse aim starts disabled in live settings test");
    expect(!disruptor_modern_controls_enabled(),
           "modern controls start disabled in live settings test");
    expect(!disruptor_mouse_set_captured(1),
           "capture is rejected while input features are disabled");

    disruptor_mouse_aim_set_enabled(1);
    expect(disruptor_mouse_aim_enabled() && g_mouse.enabled,
           "live mouse-aim enable wakes the dormant frame hook");
    expect(disruptor_mouse_set_captured(1) && g_relative_mode,
           "live API captures after mouse aim is enabled");

    expect_close(disruptor_mouse_set_horizontal_sensitivity(0.25), 0.25,
                 "live sensitivity accepts an in-range value");
    expect_close(disruptor_mouse_set_horizontal_sensitivity(99.0), 2.0,
                 "live sensitivity clamps to its safe maximum");
    expect_close(disruptor_mouse_set_horizontal_sensitivity(NAN), 2.0,
                 "live sensitivity rejects non-finite values");

    disruptor_mouse_set_vertical_look_enabled(1);
    expect(disruptor_mouse_vertical_look_enabled(),
           "live vertical-look enable is observable");
    expect_close(disruptor_mouse_set_vertical_sensitivity(0.25), 0.25,
                 "live vertical sensitivity accepts an in-range value");
    expect_close(disruptor_mouse_set_vertical_sensitivity(99.0), 2.0,
                 "live vertical sensitivity clamps to its safe maximum");
    expect_close(disruptor_mouse_set_vertical_sensitivity(NAN), 2.0,
                 "live vertical sensitivity rejects non-finite values");
    disruptor_mouse_set_invert_vertical(1);
    expect(disruptor_mouse_invert_vertical(),
           "live vertical inversion is observable");
    apply_mouse_y(-1.0);
    disruptor_mouse_recenter_vertical();
    expect_close(disruptor_mouse_vertical_pitch(), 0.0,
                 "live recenter clears requested vertical pitch");

    disruptor_mouse_set_invert_horizontal(1);
    expect(disruptor_mouse_invert_horizontal(),
           "live horizontal inversion is observable");
    disruptor_high_precision_camera_set_enabled(1);
    expect(disruptor_high_precision_camera_enabled(),
           "live high-precision camera is observable");

    g_mouse.fractional_yaw = 0.375;
    g_camera_yaw_residual = 0.375;
    disruptor_high_precision_camera_set_enabled(0);
    expect_close(g_mouse.fractional_yaw, 0.0,
                 "disabling high precision clears fractional yaw");
    expect_close(g_camera_yaw_residual, 0.0,
                 "disabling high precision clears presentation residual");

    disruptor_modern_controls_set_enabled(1);
    disruptor_mouse_aim_set_enabled(0);
    expect(disruptor_mouse_captured(),
           "modern controls keep an existing capture after mouse aim is disabled");
    disruptor_modern_controls_set_enabled(0);
    expect(disruptor_mouse_captured(),
           "vertical look keeps capture after other input features are disabled");
    disruptor_mouse_set_vertical_look_enabled(0);
    expect(!disruptor_mouse_captured() && !g_relative_mode,
           "disabling the last input feature releases capture");
    expect_close(g_requested_vertical_pitch, 0.0,
                 "disabling vertical look recenters the camera sink");
}

void test_vertical_only_capture() {
    reset_state();
    g_mouse.configured = true;
    disruptor_mouse_set_vertical_look_enabled(1);
    expect(disruptor_mouse_set_captured(1) && g_relative_mode,
           "vertical look alone permits relative mouse capture");
    disruptor_mouse_set_vertical_look_enabled(0);
    expect(!g_mouse.captured && !g_relative_mode,
           "disabling vertical-only input releases capture");
}

void test_host_ui_blocks_polled_mod_input() {
    reset_state();
    g_mouse.configured = true;
    g_mouse.enabled = true;
    g_mouse.mouse_aim_enabled = true;
    g_mouse.vertical_look_enabled = true;
    g_mouse.modern_controls_enabled = true;
    g_mouse.captured = true;
    g_relative_mode = true;
    g_mouse.fractional_yaw = 0.5;
    g_camera_yaw_residual = 0.5;
    psx_write_byte(kTestYawAddress, 77);
    g_keys[SDL_SCANCODE_W] = 1;
    g_buttons = SDL_BUTTON_LMASK;
    g_relative_x = 12.0f;
    g_relative_y = -12.0f;
    g_host_ui_captured = 1;

    mouse_aim_frame();

    expect(psx_read_byte(kTestYawAddress) == 77,
           "host UI blocks mouse yaw writes");
    expect(g_pad_buttons == 0xFFFFu,
           "host UI blocks modern-control button injection");
    expect_close(g_relative_x, 0.0,
                 "host UI drains relative motion while captured");
    expect_close(g_relative_y, 0.0,
                 "host UI drains relative vertical motion while captured");
    expect_close(g_mouse.vertical_pitch, 0.0,
                 "host UI blocks vertical camera changes");
    expect_close(g_camera_yaw_residual, 0.0,
                 "host UI clears presentation residual while captured");
}

void test_host_ui_escape_close_restores_capture() {
    reset_state();
    g_mouse.configured = true;
    g_mouse.enabled = true;
    g_mouse.mouse_aim_enabled = true;
    g_mouse.log = std::tmpfile();

    /* Closing the host UI happens inside the Escape key event callback.  The
     * key is still down when its frame hook next polls SDL state. */
    g_keys[SDL_SCANCODE_ESCAPE] = 1;
    expect(disruptor_mouse_set_captured(1) && g_relative_mode,
           "host UI restores relative capture while Escape is held");
    mouse_aim_frame();
    expect(g_mouse.captured && g_relative_mode,
           "held close key does not immediately release restored capture");

    g_keys[SDL_SCANCODE_ESCAPE] = 0;
    mouse_aim_frame();
    g_keys[SDL_SCANCODE_ESCAPE] = 1;
    mouse_aim_frame();
    expect(!g_mouse.captured && !g_relative_mode,
           "a later Escape press still releases capture normally");
}

void test_netplay_recenters_and_blocks_vertical_motion() {
    reset_state();
    g_mouse.configured = true;
    g_mouse.enabled = true;
    g_mouse.vertical_look_enabled = true;
    g_mouse.captured = true;
    g_mouse.vertical_pitch = 4.0;
    g_requested_vertical_pitch = 4.0;
    g_relative_y = -25.0f;
    g_netplay_active = 1;
    g_mouse.log = std::tmpfile();

    mouse_aim_frame();

    expect_close(g_mouse.vertical_pitch, 0.0,
                 "netplay transition clears host vertical pitch");
    expect_close(g_requested_vertical_pitch, 0.0,
                 "netplay transition recenters the camera sink");
    expect_close(g_relative_y, 0.0,
                 "netplay still drains relative vertical motion");
    expect(disruptor_mouse_vertical_look_enabled(),
           "netplay guard preserves the user's vertical preference");
}

void test_gameplay_gate_discards_new_vertical_motion() {
    reset_state();
    g_mouse.configured = true;
    g_mouse.enabled = true;
    g_mouse.vertical_look_enabled = true;
    g_mouse.captured = true;
    g_mouse.vertical_pitch = 3.0;
    g_requested_vertical_pitch = 3.0;
    g_vertical_input_allowed = 0;
    g_relative_y = -25.0f;
    g_mouse.log = std::tmpfile();

    mouse_aim_frame();

    expect_close(g_mouse.vertical_pitch, 3.0,
                 "scripted/non-gameplay gate preserves existing pitch");
    expect_close(g_requested_vertical_pitch, 3.0,
                 "scripted/non-gameplay gate preserves the sink request");
    expect_close(g_relative_y, 0.0,
                 "scripted/non-gameplay gate discards new mouse Y");
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--registration") == 0) {
        expect(g_registered_hook != nullptr,
               "environment opt-in registers the frame hook");
    } else {
        expect(g_registered_hook != nullptr,
               "normal launch installs a dormant hook for live settings");
        test_parsing();
        test_ini_configuration();
        test_direction_fraction_and_wrap();
        test_motion_safety_clamp();
        test_vertical_direction_inversion_and_clamp();
        test_simultaneous_relative_axes_sample();
        test_capture_release_and_frame_motion();
        test_high_precision_camera_residual();
        test_modern_keyboard_mapping_and_merge();
        test_modern_mouse_buttons_require_capture();
        test_live_settings_api();
        test_vertical_only_capture();
        test_host_ui_blocks_polled_mod_input();
        test_host_ui_escape_close_restores_capture();
        test_netplay_recenters_and_blocks_vertical_motion();
        test_gameplay_gate_discards_new_vertical_motion();
    }

    if (g_mouse.log) {
        std::fclose(g_mouse.log);
        g_mouse.log = nullptr;
    }
    if (g_failures != 0) return 1;
    std::cout << "Disruptor mouse-aim tests passed\n";
    return 0;
}
