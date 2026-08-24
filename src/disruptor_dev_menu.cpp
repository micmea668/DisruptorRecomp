/*
 * Host-side in-game settings and diagnostics menu for Disruptor.
 *
 * This overlay is deliberately outside the emulated PlayStation.  It draws
 * after the completed host presentation. Normal settings never write guest
 * VRAM/RAM; the Cheats tab calls only the narrow, version-pinned Disruptor
 * gameplay APIs whose save consequences are stated in the UI.
 */

#include "disruptor_cheats.h"
#include "disruptor_far_rendering.h"
#include "disruptor_mouse_aim.h"
#include "config_loader.h"
#include "gpu.h"
#include "gpu_gl_renderer.h"
#include "host_ui.h"
#include "psx_keybinds.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#if defined(PSX_SDL3)
#include "imgui_impl_sdl3.h"
#else
#include "imgui_impl_sdl2.h"
#endif

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace {

struct DevMenuState {
    SDL_Window *window = nullptr;
    int backend = PSX_HOST_UI_BACKEND_NONE;
    bool imgui_ready = false;
    bool open = false;
    bool restore_mouse_capture = false;
    bool interpolation_apply_failed = false;
    bool aspect_apply_failed = false;
    bool far_rendering_apply_failed = false;
    bool base_keybinds_valid = false;
    int selected_wide_aspect = 0;
    std::string cheat_status;
    std::array<SDL_Scancode, PSX_KB_COUNT> base_keybinds{};
};

DevMenuState g_menu;

namespace fs = std::filesystem;

enum PreferenceDirty : uint32_t {
    PREF_MOUSE_AIM         = 1u << 0,
    PREF_MODERN_CONTROLS   = 1u << 1,
    PREF_SENSITIVITY       = 1u << 2,
    PREF_INVERT_X          = 1u << 3,
    PREF_PRECISE_CAMERA    = 1u << 4,
    PREF_GEOMETRY          = 1u << 5,
    PREF_TEXTURES          = 1u << 6,
    PREF_VSYNC             = 1u << 7,
    PREF_INTERP_TARGET     = 1u << 8,
    PREF_INTERP_BLEND      = 1u << 9,
    PREF_VERTICAL_LOOK     = 1u << 10,
    PREF_VERTICAL_SENS     = 1u << 11,
    PREF_INVERT_Y          = 1u << 12,
    PREF_ASPECT            = 1u << 13,
};

struct PreferenceState {
    fs::path path;
    PSXRecompV4::UserSettings pending;
    uint32_t dirty = 0;
    bool loaded = false;
    bool save_failed = false;
    std::string status;
};

PreferenceState g_preferences;

bool env_override_present(const char *name) {
    return name && std::getenv(name) != nullptr;
}

static constexpr std::array<SDL_Scancode, PSX_KB_COUNT> kModernKeybinds = {
    SDL_SCANCODE_W, SDL_SCANCODE_S,
    SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_RETURN, SDL_SCANCODE_SPACE,
    SDL_SCANCODE_F, SDL_SCANCODE_E,
    SDL_SCANCODE_Q, SDL_SCANCODE_R,
    SDL_SCANCODE_A, SDL_SCANCODE_D,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_P, SDL_SCANCODE_TAB,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
};

static constexpr std::array<SDL_Scancode, PSX_KB_COUNT> kOriginalKeybinds = {
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
    SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_X, SDL_SCANCODE_S,
    SDL_SCANCODE_Z, SDL_SCANCODE_A,
    SDL_SCANCODE_Q, SDL_SCANCODE_W,
    SDL_SCANCODE_E, SDL_SCANCODE_R,
    SDL_SCANCODE_T, SDL_SCANCODE_Y,
    SDL_SCANCODE_RETURN, SDL_SCANCODE_RSHIFT,
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
    SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
};

void capture_base_keybinds() {
    if (g_menu.base_keybinds_valid) return;
    bool is_packaged_modern_preset = true;
    for (int i = 0; i < PSX_KB_COUNT; ++i) {
        g_menu.base_keybinds[static_cast<size_t>(i)] =
            psx_keybinds_get_button(1, i);
        if (g_menu.base_keybinds[static_cast<size_t>(i)] !=
            kModernKeybinds[static_cast<size_t>(i)])
            is_packaged_modern_preset = false;
    }
    /* run.ps1/run.sh install the packaged modern map for an explicit launch
     * flag.  Treat that exact map as an overlay, not the restore baseline, so
     * turning Modern controls off live really returns to the packaged classic
     * mapping.  Any custom/non-exact map is preserved verbatim instead. */
    if (is_packaged_modern_preset)
        g_menu.base_keybinds = kOriginalKeybinds;
    g_menu.base_keybinds_valid = true;
}

void apply_modern_keybinds(bool enabled) {
    capture_base_keybinds();
    if (!enabled) {
        for (int i = 0; i < PSX_KB_COUNT; ++i)
            psx_keybinds_set_button(
                1, i, g_menu.base_keybinds[static_cast<size_t>(i)]);
        return;
    }

    for (int i = 0; i < PSX_KB_COUNT; ++i)
        psx_keybinds_set_button(
            1, i, kModernKeybinds[static_cast<size_t>(i)]);
}

void set_modern_controls_live(bool enabled) {
    apply_modern_keybinds(enabled);
    disruptor_modern_controls_set_enabled(enabled ? 1 : 0);
}

void mark_mouse_aim(bool value) {
    g_preferences.pending.has_mouse_aim = true;
    g_preferences.pending.mouse_aim = value;
    g_preferences.dirty |= PREF_MOUSE_AIM;
}

void mark_modern_controls(bool value) {
    g_preferences.pending.has_modern_controls = true;
    g_preferences.pending.modern_controls = value;
    g_preferences.dirty |= PREF_MODERN_CONTROLS;
}

void mark_sensitivity(double value) {
    g_preferences.pending.has_horizontal_sensitivity = true;
    g_preferences.pending.horizontal_sensitivity = value;
    g_preferences.dirty |= PREF_SENSITIVITY;
}

void mark_invert_x(bool value) {
    g_preferences.pending.has_invert_horizontal = true;
    g_preferences.pending.invert_horizontal = value;
    g_preferences.dirty |= PREF_INVERT_X;
}

void mark_vertical_look(bool value) {
    g_preferences.pending.has_vertical_look = true;
    g_preferences.pending.vertical_look = value;
    g_preferences.dirty |= PREF_VERTICAL_LOOK;
}

void mark_vertical_sensitivity(double value) {
    g_preferences.pending.has_vertical_sensitivity = true;
    g_preferences.pending.vertical_sensitivity = value;
    g_preferences.dirty |= PREF_VERTICAL_SENS;
}

void mark_invert_y(bool value) {
    g_preferences.pending.has_invert_vertical = true;
    g_preferences.pending.invert_vertical = value;
    g_preferences.dirty |= PREF_INVERT_Y;
}

void mark_precise_camera(bool value) {
    g_preferences.pending.has_high_precision_camera = true;
    g_preferences.pending.high_precision_camera = value;
    g_preferences.dirty |= PREF_PRECISE_CAMERA;
}

void mark_geometry(bool value) {
    g_preferences.pending.has_geometry_correction = true;
    g_preferences.pending.geometry_correction = value;
    g_preferences.dirty |= PREF_GEOMETRY;
}

void mark_textures(bool value) {
    g_preferences.pending.has_perspective_textures = true;
    g_preferences.pending.perspective_textures = value;
    g_preferences.dirty |= PREF_TEXTURES;
}

void mark_vsync(int value) {
    g_preferences.pending.has_vsync = true;
    g_preferences.pending.vsync = value;
    g_preferences.dirty |= PREF_VSYNC;
}

void mark_interpolation_target(int value) {
    g_preferences.pending.has_frame_interpolation_fps = true;
    g_preferences.pending.frame_interpolation_fps = value;
    g_preferences.dirty |= PREF_INTERP_TARGET;
}

void mark_interpolation_blend(int value) {
    g_preferences.pending.has_frame_interpolation_blend = true;
    g_preferences.pending.frame_interpolation_blend = value;
    g_preferences.dirty |= PREF_INTERP_BLEND;
}

void mark_aspect(int numerator, int denominator) {
    g_preferences.pending.has_aspect_ratio = true;
    g_preferences.pending.aspect_num = numerator;
    g_preferences.pending.aspect_den = denominator;
    /* Every menu entry is a fixed aspect. Clear a launcher-era adaptive
     * preference as part of the same atomic settings update. */
    g_preferences.pending.has_adaptive_view = true;
    g_preferences.pending.adaptive_view = false;
    g_preferences.dirty |= PREF_ASPECT;
}

void merge_dirty_preferences(PSXRecompV4::UserSettings &settings) {
    const auto &pending = g_preferences.pending;
    if (g_preferences.dirty & PREF_MOUSE_AIM) {
        settings.has_mouse_aim = true;
        settings.mouse_aim = pending.mouse_aim;
    }
    if (g_preferences.dirty & PREF_MODERN_CONTROLS) {
        settings.has_modern_controls = true;
        settings.modern_controls = pending.modern_controls;
    }
    if (g_preferences.dirty & PREF_SENSITIVITY) {
        settings.has_horizontal_sensitivity = true;
        settings.horizontal_sensitivity = pending.horizontal_sensitivity;
    }
    if (g_preferences.dirty & PREF_INVERT_X) {
        settings.has_invert_horizontal = true;
        settings.invert_horizontal = pending.invert_horizontal;
    }
    if (g_preferences.dirty & PREF_VERTICAL_LOOK) {
        settings.has_vertical_look = true;
        settings.vertical_look = pending.vertical_look;
    }
    if (g_preferences.dirty & PREF_VERTICAL_SENS) {
        settings.has_vertical_sensitivity = true;
        settings.vertical_sensitivity = pending.vertical_sensitivity;
    }
    if (g_preferences.dirty & PREF_INVERT_Y) {
        settings.has_invert_vertical = true;
        settings.invert_vertical = pending.invert_vertical;
    }
    if (g_preferences.dirty & PREF_PRECISE_CAMERA) {
        settings.has_high_precision_camera = true;
        settings.high_precision_camera = pending.high_precision_camera;
    }
    if (g_preferences.dirty & PREF_GEOMETRY) {
        settings.has_geometry_correction = true;
        settings.geometry_correction = pending.geometry_correction;
    }
    if (g_preferences.dirty & PREF_TEXTURES) {
        settings.has_perspective_textures = true;
        settings.perspective_textures = pending.perspective_textures;
    }
    if (g_preferences.dirty & PREF_VSYNC) {
        settings.has_vsync = true;
        settings.vsync = pending.vsync;
    }
    if (g_preferences.dirty & PREF_INTERP_TARGET) {
        settings.has_frame_interpolation_fps = true;
        settings.frame_interpolation_fps = pending.frame_interpolation_fps;
    }
    if (g_preferences.dirty & PREF_INTERP_BLEND) {
        settings.has_frame_interpolation_blend = true;
        settings.frame_interpolation_blend = pending.frame_interpolation_blend;
    }
    if (g_preferences.dirty & PREF_ASPECT) {
        settings.has_aspect_ratio = true;
        settings.aspect_num = pending.aspect_num;
        settings.aspect_den = pending.aspect_den;
        settings.has_adaptive_view = true;
        settings.adaptive_view = false;
    }
}

bool flush_preferences() {
    if (g_preferences.dirty == 0) return true;
    if (g_preferences.path.empty()) {
        g_preferences.save_failed = true;
        g_preferences.status = "Runtime settings path is unavailable.";
        return false;
    }

    PSXRecompV4::UserSettings settings =
        PSXRecompV4::load_user_settings(g_preferences.path);
    if (settings.parse_error) {
        g_preferences.save_failed = true;
        g_preferences.status =
            "settings.toml is invalid; it was left untouched.";
        return false;
    }
    merge_dirty_preferences(settings);
    if (!PSXRecompV4::save_user_settings(g_preferences.path, settings)) {
        g_preferences.save_failed = true;
        g_preferences.status =
            "Could not save settings; the previous file is intact.";
        return false;
    }

    g_preferences.dirty = 0;
    g_preferences.save_failed = false;
    g_preferences.status = "Settings saved.";
    std::fprintf(stdout, "disruptor: saved in-game settings to %s\n",
                 g_preferences.path.u8string().c_str());
    return true;
}

void apply_saved_preferences(const PSXRecompV4::UserSettings &settings) {
    /* Force the legacy INI + environment fallback to load before the saved
     * layer is applied.  Presence (including an explicit zero) makes an
     * environment value authoritative for this launch. */
    (void)disruptor_mouse_horizontal_sensitivity();
    (void)disruptor_mouse_invert_horizontal();

    if (settings.has_mouse_aim &&
        !env_override_present("PSX_DISRUPTOR_MOUSE_AIM"))
        disruptor_mouse_aim_set_enabled(settings.mouse_aim ? 1 : 0);
    if (settings.has_modern_controls &&
        !env_override_present("PSX_DISRUPTOR_MODERN_CONTROLS"))
        set_modern_controls_live(settings.modern_controls);
    if (settings.has_horizontal_sensitivity &&
        !env_override_present("PSX_DISRUPTOR_MOUSE_SENSITIVITY"))
        (void)disruptor_mouse_set_horizontal_sensitivity(
            settings.horizontal_sensitivity);
    if (settings.has_invert_horizontal &&
        !env_override_present("PSX_DISRUPTOR_MOUSE_INVERT_X"))
        disruptor_mouse_set_invert_horizontal(
            settings.invert_horizontal ? 1 : 0);
    if (settings.has_vertical_look &&
        !env_override_present("PSX_DISRUPTOR_VERTICAL_LOOK"))
        disruptor_mouse_set_vertical_look_enabled(
            settings.vertical_look ? 1 : 0);
    if (settings.has_vertical_sensitivity &&
        !env_override_present("PSX_DISRUPTOR_MOUSE_SENSITIVITY_Y"))
        (void)disruptor_mouse_set_vertical_sensitivity(
            settings.vertical_sensitivity);
    if (settings.has_invert_vertical &&
        !env_override_present("PSX_DISRUPTOR_MOUSE_INVERT_Y"))
        disruptor_mouse_set_invert_vertical(
            settings.invert_vertical ? 1 : 0);
    if (settings.has_high_precision_camera &&
        !env_override_present("PSX_DISRUPTOR_HIGH_PRECISION_CAMERA"))
        disruptor_high_precision_camera_set_enabled(
            settings.high_precision_camera ? 1 : 0);

    /* An inherited explicit environment override can enable Modern controls
     * without a run-script switch (and therefore without its preset copy).
     * Always reconcile the live mode with the in-memory keyboard overlay so
     * S/A do not retain conflicting classic face-button bindings. */
    apply_modern_keybinds(disruptor_modern_controls_enabled() != 0);

    /* main.cpp applies these fields before the environment layer.  Repeat the
     * live application here so a host-UI soft session also gets the saved
     * state, while still respecting explicit per-launch overrides. */
    if (settings.has_geometry_correction &&
        !env_override_present("PSX_GEOMETRY_CORRECTION"))
        gpu_geometry_correction_set(settings.geometry_correction ? 1 : 0);
    if (settings.has_perspective_textures &&
        !env_override_present("PSX_TEXTURE_CORRECTION")) {
        gpu_texture_correction_set(
            settings.perspective_textures &&
                    gpu_geometry_correction_enabled()
                ? 1 : 0);
    }
}

void apply_pending_preferences() {
    const auto &pending = g_preferences.pending;
    if (g_preferences.dirty & PREF_MOUSE_AIM)
        disruptor_mouse_aim_set_enabled(pending.mouse_aim ? 1 : 0);
    if (g_preferences.dirty & PREF_MODERN_CONTROLS)
        set_modern_controls_live(pending.modern_controls);
    if (g_preferences.dirty & PREF_SENSITIVITY)
        (void)disruptor_mouse_set_horizontal_sensitivity(
            pending.horizontal_sensitivity);
    if (g_preferences.dirty & PREF_INVERT_X)
        disruptor_mouse_set_invert_horizontal(
            pending.invert_horizontal ? 1 : 0);
    if (g_preferences.dirty & PREF_VERTICAL_LOOK)
        disruptor_mouse_set_vertical_look_enabled(
            pending.vertical_look ? 1 : 0);
    if (g_preferences.dirty & PREF_VERTICAL_SENS)
        (void)disruptor_mouse_set_vertical_sensitivity(
            pending.vertical_sensitivity);
    if (g_preferences.dirty & PREF_INVERT_Y)
        disruptor_mouse_set_invert_vertical(
            pending.invert_vertical ? 1 : 0);
    if (g_preferences.dirty & PREF_PRECISE_CAMERA)
        disruptor_high_precision_camera_set_enabled(
            pending.high_precision_camera ? 1 : 0);
    if (g_preferences.dirty & PREF_GEOMETRY)
        gpu_geometry_correction_set(pending.geometry_correction ? 1 : 0);
    if (g_preferences.dirty & PREF_TEXTURES)
        gpu_texture_correction_set(
            pending.perspective_textures &&
                    gpu_geometry_correction_enabled()
                ? 1 : 0);
    if (g_preferences.dirty & PREF_VSYNC)
        (void)psx_host_video_set_vsync(pending.vsync);
    if (g_preferences.dirty & (PREF_INTERP_TARGET | PREF_INTERP_BLEND)) {
        int enabled = 0;
        int target = 0;
        int blend = 0;
        psx_host_video_get_interpolation(&enabled, &target, &blend);
        if (g_preferences.dirty & PREF_INTERP_TARGET)
            target = pending.frame_interpolation_fps;
        if (g_preferences.dirty & PREF_INTERP_BLEND)
            blend = pending.frame_interpolation_blend;
        (void)psx_host_video_set_interpolation(enabled, target, blend);
    }
    if (g_preferences.dirty & PREF_ASPECT)
        (void)psx_host_video_set_display_aspect(
            pending.aspect_num, pending.aspect_den);
}

void load_preferences_for_session() {
    const char *path = psx_host_user_settings_path();
    if (!path || !path[0]) {
        g_preferences.loaded = false;
        g_preferences.status = "Runtime settings path is unavailable.";
        return;
    }
    g_preferences.path = fs::u8path(path);
    const PSXRecompV4::UserSettings settings =
        PSXRecompV4::load_user_settings(g_preferences.path);
    if (settings.parse_error) {
        g_preferences.loaded = false;
        g_preferences.status =
            "settings.toml is invalid; saved menu values were not applied.";
        return;
    }
    capture_base_keybinds();
    apply_saved_preferences(settings);
    apply_pending_preferences();
    g_preferences.loaded = true;
    if (!g_preferences.save_failed)
        g_preferences.status = "Settings loaded.";
}

bool imgui_sdl_init(SDL_Window *window) {
#if defined(PSX_SDL3)
    return ImGui_ImplSDL3_InitForOpenGL(window, SDL_GL_GetCurrentContext());
#else
    return ImGui_ImplSDL2_InitForOpenGL(window, SDL_GL_GetCurrentContext());
#endif
}

void imgui_sdl_shutdown() {
#if defined(PSX_SDL3)
    ImGui_ImplSDL3_Shutdown();
#else
    ImGui_ImplSDL2_Shutdown();
#endif
}

void imgui_sdl_new_frame() {
#if defined(PSX_SDL3)
    ImGui_ImplSDL3_NewFrame();
#else
    ImGui_ImplSDL2_NewFrame();
#endif
}

void imgui_sdl_process_event(const SDL_Event *event) {
#if defined(PSX_SDL3)
    (void)ImGui_ImplSDL3_ProcessEvent(event);
#else
    (void)ImGui_ImplSDL2_ProcessEvent(event);
#endif
}

void apply_disruptor_style() {
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 3.0f;

    const ImVec4 green(0.20f, 0.78f, 0.34f, 1.00f);
    const ImVec4 green_hover(0.28f, 0.90f, 0.43f, 1.00f);
    const ImVec4 green_active(0.14f, 0.62f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = green_hover;
    style.Colors[ImGuiCol_SliderGrab] = green;
    style.Colors[ImGuiCol_SliderGrabActive] = green_hover;
    style.Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.38f, 0.18f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered] = green_active;
    style.Colors[ImGuiCol_HeaderActive] = green;
    style.Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.34f, 0.17f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = green_active;
    style.Colors[ImGuiCol_ButtonActive] = green;
}

bool initialize_imgui() {
    if (g_menu.imgui_ready) return true;
    if (!g_menu.window || g_menu.backend != PSX_HOST_UI_BACKEND_OPENGL)
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    /* Persist only the reviewed gameplay/presentation fields below.  ImGui's
     * ambient layout file is deliberately disabled: it is relative-path,
     * independently written, and outside our atomic settings contract. */
    io.IniFilename = nullptr;
    apply_disruptor_style();

    if (!imgui_sdl_init(g_menu.window)) {
        ImGui::DestroyContext();
        std::fprintf(stderr,
                     "disruptor: developer menu SDL backend initialization failed\n");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        imgui_sdl_shutdown();
        ImGui::DestroyContext();
        std::fprintf(stderr,
                     "disruptor: developer menu OpenGL initialization failed\n");
        return false;
    }

    g_menu.imgui_ready = true;
    std::fprintf(stdout,
                 "disruptor: in-game settings menu ready (press ` to open)\n");
    return true;
}

void set_menu_open(bool open) {
    if (open == g_menu.open) return;
    if (open) {
        g_menu.restore_mouse_capture = disruptor_mouse_captured() != 0;
        if (g_menu.restore_mouse_capture)
            (void)disruptor_mouse_set_captured(0);
        g_menu.open = true;
    } else {
        g_menu.open = false;
        (void)flush_preferences();
        if (g_menu.restore_mouse_capture)
            (void)disruptor_mouse_set_captured(1);
        g_menu.restore_mouse_capture = false;
    }
}

bool scancode_event(const SDL_Event *event, SDL_Scancode scancode) {
    if (!event || event->type != SDL_KEYDOWN || event->key.repeat) return false;
#if defined(PSX_SDL3)
    return event->key.scancode == scancode;
#else
    return event->key.keysym.scancode == scancode;
#endif
}

void draw_status_badge(const char *label, const ImVec4 &colour) {
    ImGui::SameLine();
    ImGui::TextColored(colour, "[%s]", label);
}

void draw_controls_tab() {
    bool mouse_aim = disruptor_mouse_aim_enabled() != 0;
    if (ImGui::Checkbox("Horizontal mouse aim", &mouse_aim)) {
        disruptor_mouse_aim_set_enabled(mouse_aim ? 1 : 0);
        mark_mouse_aim(mouse_aim);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool modern = disruptor_modern_controls_enabled() != 0;
    if (ImGui::Checkbox("Modern keyboard and mouse controls", &modern)) {
        set_modern_controls_live(modern);
        mark_modern_controls(modern);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    float sensitivity =
        static_cast<float>(disruptor_mouse_horizontal_sensitivity());
    if (ImGui::SliderFloat("Horizontal sensitivity", &sensitivity,
                           0.005f, 2.0f, "%.3f",
                           ImGuiSliderFlags_Logarithmic)) {
        const double applied =
            disruptor_mouse_set_horizontal_sensitivity(sensitivity);
        mark_sensitivity(applied);
    }

    bool invert_x = disruptor_mouse_invert_horizontal() != 0;
    if (ImGui::Checkbox("Invert horizontal mouse", &invert_x)) {
        disruptor_mouse_set_invert_horizontal(invert_x ? 1 : 0);
        mark_invert_x(invert_x);
    }

    const bool exact_geometry = gpu_geometry_correction_enabled() != 0;
    ImGui::SeparatorText("Vertical look");
    bool vertical_look = disruptor_mouse_vertical_look_enabled() != 0;
    if (ImGui::Checkbox("Vertical mouse look", &vertical_look)) {
        disruptor_mouse_set_vertical_look_enabled(vertical_look ? 1 : 0);
        mark_vertical_look(vertical_look);
    }
    draw_status_badge("EXPERIMENTAL", ImVec4(1.0f, 0.73f, 0.25f, 1.0f));

    float vertical_sensitivity =
        static_cast<float>(disruptor_mouse_vertical_sensitivity());
    if (ImGui::SliderFloat("Vertical sensitivity", &vertical_sensitivity,
                           0.005f, 2.0f, "%.3f",
                           ImGuiSliderFlags_Logarithmic)) {
        const double applied =
            disruptor_mouse_set_vertical_sensitivity(vertical_sensitivity);
        mark_vertical_sensitivity(applied);
    }

    bool invert_y = disruptor_mouse_invert_vertical() != 0;
    if (ImGui::Checkbox("Invert vertical mouse", &invert_y)) {
        disruptor_mouse_set_invert_vertical(invert_y ? 1 : 0);
        mark_invert_y(invert_y);
    }

    const double pitch_units = disruptor_mouse_vertical_pitch();
    if (pitch_units == 0.0) ImGui::BeginDisabled();
    if (ImGui::Button("Recenter vertical view"))
        disruptor_mouse_recenter_vertical();
    if (pitch_units == 0.0) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("Requested pitch: %+.2f deg", pitch_units * (360.0 / 256.0));
    ImGui::TextDisabled(
        "Range: +/-30.94 degrees. The original game has no pitch state.");
    ImGui::SeparatorText("Camera presentation");
    bool high_precision = disruptor_high_precision_camera_enabled() != 0;
    if (!exact_geometry) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Sub-byte camera presentation", &high_precision)) {
        disruptor_high_precision_camera_set_enabled(high_precision ? 1 : 0);
        mark_precise_camera(high_precision);
    }
    if (!exact_geometry) ImGui::EndDisabled();
    draw_status_badge("EXPERIMENTAL", ImVec4(1.0f, 0.73f, 0.25f, 1.0f));
    if (!exact_geometry)
        ImGui::TextDisabled("Requires exact-provenance geometry.");

    ImGui::Separator();
    ImGui::TextWrapped(
        "The menu releases relative mouse capture while open and restores the "
        "previous state when closed. Gameplay input is neutralised while you "
        "interact with the menu.");
    ImGui::Text("Capture before opening: %s",
                g_menu.restore_mouse_capture ? "yes" : "no");
}

void apply_geometry_enabled(bool enabled) {
    if (!enabled && gpu_texture_correction_enabled()) {
        gpu_texture_correction_set(0);
        mark_textures(false);
    }
    gpu_geometry_correction_set(enabled ? 1 : 0);
    mark_geometry(enabled);
}

const char *far_rendering_preset_label(int preset) {
    switch (preset) {
        case DISRUPTOR_FAR_RENDERING_EXTENDED:
            return "1.25x";
        case DISRUPTOR_FAR_RENDERING_FAR:
            return "1.5x";
        case DISRUPTOR_FAR_RENDERING_RETAIL:
        default:
            return "Retail";
    }
}

const char *far_rendering_depth_fade_label(int mode) {
    switch (mode) {
        case DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED:
            return "Nearest CLUT row (diagnostic)";
        case DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL:
        default:
            return "Retail palette ramp";
    }
}

void draw_far_rendering_controls() {
    DisruptorFarRenderingDiagnostics diagnostics{};
    const bool diagnostics_available =
        disruptor_far_rendering_get_diagnostics(
            &diagnostics,
            static_cast<uint32_t>(sizeof(diagnostics))) != 0;
    const bool netplay = diagnostics_available
        ? diagnostics.netplay_blocked != 0
        : disruptor_far_rendering_netplay_blocked() != 0;
    const bool gameplay_ready = diagnostics_available
        ? diagnostics.gameplay_ready != 0
        : disruptor_far_rendering_gameplay_ready() != 0;

    ImGui::SeparatorText("Experimental: Draw distance");
    int preset = disruptor_far_rendering_get_preset();
    preset = std::clamp(
        preset, static_cast<int>(DISRUPTOR_FAR_RENDERING_RETAIL),
        static_cast<int>(DISRUPTOR_FAR_RENDERING_PRESET_COUNT) - 1);
    static const char *kPresetLabels[] = {"Retail", "1.25x", "1.5x"};
    const bool extension_controls_blocked = netplay || !gameplay_ready;
    if (extension_controls_blocked) ImGui::BeginDisabled();
    if (ImGui::Combo("Draw distance preset", &preset, kPresetLabels,
                     DISRUPTOR_FAR_RENDERING_PRESET_COUNT)) {
        const int result = disruptor_far_rendering_set_preset(preset);
        g_menu.far_rendering_apply_failed =
            result != DISRUPTOR_FAR_RENDERING_OK;
    }
    int depth_fade_mode = disruptor_far_rendering_get_depth_fade_mode();
    depth_fade_mode = std::clamp(
        depth_fade_mode,
        static_cast<int>(DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL),
        static_cast<int>(DISRUPTOR_FAR_RENDERING_DEPTH_FADE_MODE_COUNT) - 1);
    static const char *kDepthFadeLabels[] = {
        "Retail palette ramp", "Nearest CLUT row (diagnostic)"};
    if (ImGui::Combo("Distance shading", &depth_fade_mode, kDepthFadeLabels,
                     DISRUPTOR_FAR_RENDERING_DEPTH_FADE_MODE_COUNT)) {
        const int result =
            disruptor_far_rendering_set_depth_fade_mode(depth_fade_mode);
        g_menu.far_rendering_apply_failed =
            result != DISRUPTOR_FAR_RENDERING_OK;
    }
    if (extension_controls_blocked) ImGui::EndDisabled();
    draw_status_badge("SESSION", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    if (extension_controls_blocked &&
        (preset != DISRUPTOR_FAR_RENDERING_RETAIL ||
         depth_fade_mode != DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL) &&
        ImGui::Button("Restore Retail distance and fade")) {
        const int preset_result = disruptor_far_rendering_set_preset(
            DISRUPTOR_FAR_RENDERING_RETAIL);
        const int fade_result = disruptor_far_rendering_set_depth_fade_mode(
            DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL);
        g_menu.far_rendering_apply_failed =
            preset_result != DISRUPTOR_FAR_RENDERING_OK ||
            fade_result != DISRUPTOR_FAR_RENDERING_OK;
    }

    if (netplay) {
        ImGui::TextDisabled(
            "Distance/fade experiments are unavailable during netplay.");
    } else if (!gameplay_ready) {
        ImGui::TextDisabled(
            "Enter verified live gameplay to change distance or fade.");
    }
    if (g_menu.far_rendering_apply_failed) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
            "The runtime rejected the requested distance/fade setting.");
    }

    if (diagnostics_available && diagnostics.source_far_distance != 0) {
        ImGui::Text("Source / effective far: %u / %u",
                    static_cast<unsigned>(diagnostics.source_far_distance),
                    static_cast<unsigned>(diagnostics.effective_far_distance));
        ImGui::Text("Source / effective fade start: %d / %d",
                    static_cast<int>(diagnostics.source_fade_start),
                    static_cast<int>(diagnostics.effective_fade_start));
        ImGui::Text("Applied far / shading hooks: %llu / %llu",
                    static_cast<unsigned long long>(
                        diagnostics.far_load_substitutions),
                    static_cast<unsigned long long>(
                        diagnostics.fade_load_substitutions));
    } else {
        ImGui::TextDisabled(
            "Distance diagnostics become available after a verified render.");
    }
    ImGui::TextWrapped(
        "Warning: extending distance can reveal the void, expose portal or "
        "culling errors, leave distant actors frozen, and increase primitive "
        "pressure or frame time.");
    ImGui::TextWrapped(
        "The shading diagnostic selects the nearest palette row in reviewed "
        "world and billboard paths. It does not alter the "
        "level-authored DRAWENV background, add geometry, or bypass portal/content culling.");
    ImGui::TextDisabled(
        "Session-only: every runtime start and shutdown restores Retail "
        "distance and shading.");
}

void draw_aspect_controls() {
    static constexpr int kAspectNumerators[] = {16, 21, 32};
    static constexpr int kAspectDenominators[] = {9, 9, 9};
    static const char *kAspectLabels[] = {"16:9", "21:9", "32:9"};
    static constexpr int kAspectCount = 3;

    int numerator = 4;
    int denominator = 3;
    psx_host_video_get_display_aspect(&numerator, &denominator);
    bool widescreen = numerator * 3 != denominator * 4;
    if (widescreen) {
        for (int index = 0; index < kAspectCount; ++index) {
            if (numerator * kAspectDenominators[index] ==
                denominator * kAspectNumerators[index]) {
                g_menu.selected_wide_aspect = index;
                break;
            }
        }
    }
    g_menu.selected_wide_aspect = std::clamp(
        g_menu.selected_wide_aspect, 0, kAspectCount - 1);

    ImGui::SeparatorText("Display aspect");
    if (ImGui::Checkbox("Widescreen", &widescreen)) {
        const int requested_num = widescreen
            ? kAspectNumerators[g_menu.selected_wide_aspect] : 4;
        const int requested_den = widescreen
            ? kAspectDenominators[g_menu.selected_wide_aspect] : 3;
        g_menu.aspect_apply_failed =
            psx_host_video_set_display_aspect(requested_num, requested_den) == 0;
        if (!g_menu.aspect_apply_failed)
            mark_aspect(requested_num, requested_den);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    int selected = g_menu.selected_wide_aspect;
    if (ImGui::Combo("Aspect ratio", &selected, kAspectLabels,
                     kAspectCount)) {
        g_menu.selected_wide_aspect = selected;
        if (widescreen) {
            const int requested_num = kAspectNumerators[selected];
            const int requested_den = kAspectDenominators[selected];
            g_menu.aspect_apply_failed =
                psx_host_video_set_display_aspect(
                    requested_num, requested_den) == 0;
            if (!g_menu.aspect_apply_failed)
                mark_aspect(requested_num, requested_den);
        }
    }
    if (g_menu.aspect_apply_failed) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
            "The runtime rejected the requested display aspect.");
    }
    ImGui::TextDisabled(
        "4:3 disables widescreen. Fixed 16:9, 21:9 and 32:9 choices "
        "apply after the current frame and are saved.");
}

void draw_enhancements_tab() {
    draw_aspect_controls();

    bool geometry = gpu_geometry_correction_enabled() != 0;
    if (ImGui::Checkbox("Exact-provenance geometry", &geometry))
        apply_geometry_enabled(geometry);
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool textures = gpu_texture_correction_enabled() != 0;
    if (!geometry) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Perspective-correct world textures", &textures)) {
        gpu_texture_correction_set(textures ? 1 : 0);
        mark_textures(textures);
    }
    if (!geometry) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    if (!geometry)
        ImGui::TextDisabled("Enable exact geometry before perspective textures.");

    draw_far_rendering_controls();

    ImGui::SeparatorText("High-refresh presentation");
    int interpolation = 0;
    int target_fps = 0;
    int blend = 1;
    psx_host_video_get_interpolation(&interpolation, &target_fps, &blend);

    bool interpolation_enabled = interpolation != 0;
    if (ImGui::Checkbox("Frame interpolation", &interpolation_enabled)) {
        g_menu.interpolation_apply_failed =
            psx_host_video_set_interpolation(
                interpolation_enabled ? 1 : 0, target_fps, blend) == 0;
    }
    draw_status_badge("EXPERIMENTAL", ImVec4(1.0f, 0.73f, 0.25f, 1.0f));

    static const int kTargets[] = {0, 120, 144, 165, 240};
    static const char *kTargetLabels[] = {
        "Display refresh", "120 FPS", "144 FPS", "165 FPS", "240 FPS"};
    int target_index = 0;
    for (int i = 0; i < 5; ++i) {
        if (target_fps == kTargets[i]) target_index = i;
    }
    if (ImGui::Combo("Presentation target", &target_index,
                     kTargetLabels, 5)) {
        target_fps = kTargets[target_index];
        g_menu.interpolation_apply_failed =
            psx_host_video_set_interpolation(
                interpolation_enabled ? 1 : 0, target_fps, blend) == 0;
        if (!g_menu.interpolation_apply_failed)
            mark_interpolation_target(target_fps);
    }
    static const char *kBlendLabels[] = {
        "Linear crossfade", "Motion-adaptive clarity"};
    if (ImGui::Combo("Blend mode", &blend, kBlendLabels, 2)) {
        g_menu.interpolation_apply_failed =
            psx_host_video_set_interpolation(
                interpolation_enabled ? 1 : 0, target_fps, blend) == 0;
        if (!g_menu.interpolation_apply_failed)
            mark_interpolation_blend(blend);
    }

    ImGui::TextWrapped(
        "Interpolation blends completed host images. Guest simulation, audio "
        "and input remain at the original cadence; ghosting and additional "
        "visual latency are possible.");
    ImGui::TextDisabled(
        "Activation is session-only and starts disabled next launch; target "
        "and blend preferences are remembered.");
    if (g_menu.interpolation_apply_failed)
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                           "The renderer rejected the requested interpolation setting.");

    ImGui::SeparatorText("Synchronisation");
    int vsync = psx_host_video_get_vsync();
    int vsync_index = std::clamp(vsync + 1, 0, 2);
    static const char *kVsyncLabels[] = {
        "Adaptive", "Immediate", "Synchronised"};
    if (interpolation_enabled) ImGui::BeginDisabled();
    if (ImGui::Combo("VSync", &vsync_index, kVsyncLabels, 3)) {
        const int requested = vsync_index - 1;
        if (psx_host_video_set_vsync(requested)) mark_vsync(requested);
    }
    if (interpolation_enabled) ImGui::EndDisabled();
    if (interpolation_enabled)
        ImGui::TextDisabled("Interpolation owns presentation cadence and uses immediate swaps.");
}

const char *cheat_result_message(int result, const char *success) {
    switch (result) {
        case DISRUPTOR_CHEAT_OK:
            return success;
        case DISRUPTOR_CHEAT_GAME_NOT_READY:
            return "Enter live gameplay before using this cheat.";
        case DISRUPTOR_CHEAT_NETPLAY_BLOCKED:
            return "Cheats are disabled during netplay.";
        case DISRUPTOR_CHEAT_UNVERIFIED_STATE:
            return "The game state did not match the verified USA executable; no changes were made.";
        default:
            return "The cheat request was rejected.";
    }
}

void draw_cheats_tab() {
    const bool netplay = disruptor_cheats_netplay_blocked() != 0;
    const bool gameplay_ready = disruptor_cheats_gameplay_ready() != 0;

    ImGui::TextWrapped(
        "These are version-pinned gameplay cheats for testing and casual "
        "play. They are never saved as port settings and start off on every "
        "launch.");
    if (netplay) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                           "Cheats are unavailable while netplay is active.");
    } else if (!gameplay_ready) {
        ImGui::TextDisabled("Enter live gameplay to enable cheats.");
    }

    ImGui::SeparatorText("Player");
    bool god_mode = disruptor_cheats_god_mode_enabled() != 0;
    const bool disable_god_control = !god_mode && (netplay || !gameplay_ready);
    if (disable_god_control) ImGui::BeginDisabled();
    if (ImGui::Checkbox("God mode", &god_mode)) {
        const int result = disruptor_cheats_set_god_mode(god_mode ? 1 : 0);
        g_menu.cheat_status = cheat_result_message(
            result, god_mode ? "God mode enabled for this session."
                             : "God mode disabled.");
    }
    if (disable_god_control) ImGui::EndDisabled();
    draw_status_badge("SESSION", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "Neutralises player damage at the game's central damage routine. "
        "A light hit cue remains, but saved health is not altered and the "
        "game is not marked as cheated.");

    ImGui::SeparatorText("Inventory");
    if (netplay || !gameplay_ready) ImGui::BeginDisabled();
    if (ImGui::Button("Grant all weapons + psionics"))
        ImGui::OpenPopup("Confirm All Weapons");
    if (netplay || !gameplay_ready) ImGui::EndDisabled();
    ImGui::TextWrapped(
        "This reproduces the retail All Weapons cheat: all weapons and "
        "psionics are unlocked, ammunition and psionic energy are refilled, "
        "and the current game is marked as cheated.");
    ImGui::TextColored(ImVec4(1.0f, 0.73f, 0.25f, 1.0f),
        "Important: the cheated marker affects the ending and is carried into "
        "memory-card/password saves.");

    bool popup_open = true;
    if (ImGui::BeginPopupModal("Confirm All Weapons", &popup_open,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Grant every weapon and psionic, refill their resources, and "
            "mark this game (including later saves) as cheated?");
        if (ImGui::Button("Grant and mark cheated")) {
            const int result = disruptor_cheats_grant_all_weapons();
            g_menu.cheat_status = cheat_result_message(
                result, "All weapons, psionics and resources granted. Game marked as cheated.");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (!g_menu.cheat_status.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", g_menu.cheat_status.c_str());
    }
}

double coverage_percent(uint64_t accepted, uint64_t candidates) {
    return candidates ? (100.0 * static_cast<double>(accepted) /
                         static_cast<double>(candidates)) : 0.0;
}

void draw_diagnostics_tab() {
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::Text("Developer UI: %.1f FPS", io.Framerate);

    GpuGeometryCorrectionStats stats{};
    gpu_geometry_correction_stats_detailed(
        &stats, static_cast<uint32_t>(sizeof(stats)));
    const GpuGeometryCorrectionCounters &latest =
        stats.latest_live_valid ? stats.latest_live : stats.cumulative;
    const double coverage = coverage_percent(latest.vertex_accepted,
                                             latest.vertex_candidates);

    ImGui::SeparatorText("Exact geometry");
    ImGui::Text("Corrected presentations: %llu",
                static_cast<unsigned long long>(stats.corrected_presentations));
    ImGui::Text("Vertices: %llu / %llu (%.2f%%)",
                static_cast<unsigned long long>(latest.vertex_accepted),
                static_cast<unsigned long long>(latest.vertex_candidates),
                coverage);
    ImGui::ProgressBar(static_cast<float>(coverage / 100.0), ImVec2(-1.0f, 0.0f));
    ImGui::Text("Store misses: %llu",
                static_cast<unsigned long long>(
                    latest.vertex_rejections[GPU_GEOMETRY_REJECT_STORE_MISS]));
    ImGui::Text("Partial quad fallbacks: %llu",
                static_cast<unsigned long long>(latest.partial_quad_rejections));
    ImGui::Text("Perspective-correct triangles: %u",
                static_cast<unsigned>(gpu_texture_correction_hits()));

    ImGui::SeparatorText("Frame interpolation");
    int interp_enabled = 0;
    int interp_suspended = 0;
    int history_frames = 0;
    double host_hz = 0.0;
    double target_hz = 0.0;
    uint64_t swaps = 0;
    gl_renderer_interpolation_diag(&interp_enabled, &interp_suspended,
                                   &history_frames, &host_hz, &target_hz,
                                   &swaps);
    ImGui::Text("State: %s%s", interp_enabled ? "enabled" : "disabled",
                interp_suspended ? " (suspended)" : "");
    ImGui::Text("Display %.1f Hz, target %.1f Hz, history %d",
                host_hz, target_hz, history_frames);
    ImGui::Text("Interpolated swaps: %llu",
                static_cast<unsigned long long>(swaps));

    DisruptorFarRenderingDiagnostics far{};
    const bool far_diagnostics = disruptor_far_rendering_get_diagnostics(
        &far, static_cast<uint32_t>(sizeof(far))) != 0;
    ImGui::SeparatorText("Experimental draw distance");
    if (far_diagnostics) {
        ImGui::Text("Preset: %s", far_rendering_preset_label(far.preset));
        ImGui::Text("Distance shading: %s",
                    far_rendering_depth_fade_label(far.depth_fade_mode));
        ImGui::Text("Status: %s",
                    far.netplay_blocked
                        ? "blocked by netplay"
                        : (far.gameplay_ready
                               ? "verified live gameplay"
                               : "waiting for verified gameplay"));
        ImGui::Text("Source / effective far: %u / %u",
                    static_cast<unsigned>(far.source_far_distance),
                    static_cast<unsigned>(far.effective_far_distance));
        ImGui::Text("Source / effective fade start: %d / %d",
                    static_cast<int>(far.source_fade_start),
                    static_cast<int>(far.effective_fade_start));
        ImGui::Text("Far / fade load substitutions: %llu / %llu",
                    static_cast<unsigned long long>(
                        far.far_load_substitutions),
                    static_cast<unsigned long long>(
                        far.fade_load_substitutions));
        ImGui::Text("Total substitutions / completed frames: %llu / %llu",
                    static_cast<unsigned long long>(far.substituted_loads),
                    static_cast<unsigned long long>(far.completed_frames));
        ImGui::Text("Portal final flips / decisions: %llu / %llu",
                    static_cast<unsigned long long>(
                        far.portal_final_decision_flips),
                    static_cast<unsigned long long>(
                        far.portal_final_decisions));
        ImGui::Text("Object far flips / decisions: %llu / %llu",
                    static_cast<unsigned long long>(
                        far.object_decision_flips),
                    static_cast<unsigned long long>(far.object_far_decisions));
        ImGui::Text("New valid-room marks last / high: %u / %u",
                    static_cast<unsigned>(far.traversal_rooms_last),
                    static_cast<unsigned>(far.traversal_rooms_high_water));
        ImGui::Text("Visible room spans last / high: %u / %u",
                    static_cast<unsigned>(far.submitted_spans_last),
                    static_cast<unsigned>(far.submitted_spans_high_water));
        ImGui::Text("Traversal depth last / high, cap hits: %u / %u, %llu",
                    static_cast<unsigned>(far.traversal_max_depth_last),
                    static_cast<unsigned>(far.traversal_max_depth_high_water),
                    static_cast<unsigned long long>(far.traversal_cap_hits));
        ImGui::Text("Portal shortcut relaxations: %llu",
                    static_cast<unsigned long long>(
                        far.portal_shortcut_relaxations));
        ImGui::Text("Packet bytes by stage: %u + %u + %u + %u",
                    static_cast<unsigned>(
                        far.packet_entry_to_traversal_end_last),
                    static_cast<unsigned>(
                        far.packet_traversal_to_visible_start_last),
                    static_cast<unsigned>(far.packet_visible_loop_last),
                    static_cast<unsigned>(far.packet_post_visible_loop_last));
        ImGui::Text("Renderer entries / rejected: %llu / %llu",
                    static_cast<unsigned long long>(far.renderer_entries),
                    static_cast<unsigned long long>(far.rejected_entries));
        ImGui::Text("Packet bytes/frame high-water: %u (%llu samples)",
                    static_cast<unsigned>(far.primitive_high_water),
                    static_cast<unsigned long long>(far.primitive_samples));
        ImGui::Text("Renderer wall span: p50 %.3f ms, p95 %.3f ms, max %.3f ms",
                    far.render_wall_us_p50 / 1000.0,
                    far.render_wall_us_p95 / 1000.0,
                    far.render_wall_us_max / 1000.0);
        if (far.nested_entries || far.missed_epilogues ||
            far.invalid_globals || far.observer_sequence_errors) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                "Guard events: nested %llu, missed epilogues %llu, "
                "invalid globals %llu, observer sequence %llu",
                static_cast<unsigned long long>(far.nested_entries),
                static_cast<unsigned long long>(far.missed_epilogues),
                static_cast<unsigned long long>(far.invalid_globals),
                static_cast<unsigned long long>(
                    far.observer_sequence_errors));
        }
    } else {
        ImGui::TextDisabled("Far-rendering diagnostics are unavailable.");
    }

    GpuWsDebug widescreen{};
    gpu_ws_get_debug(&widescreen);
    ImGui::SeparatorText("Presentation state");
    ImGui::Text("Widescreen: %s, mode %d, margin %d px",
                widescreen.active ? "active" : "inactive",
                widescreen.mode, widescreen.x_margin);
    ImGui::Text("Native 4:3 presentation: %s",
                widescreen.present_native_43 ? "yes" : "no");
    ImGui::TextWrapped(
        "Diagnostics are read-only. Raw RAM, overlay and timing controls are "
        "intentionally excluded; the separate Cheats tab exposes only audited "
        "Disruptor-specific actions.");
}

void draw_system_tab() {
    ImGui::TextWrapped(
        "Live menu choices are merged into the runtime's user-owned "
        "settings.toml. Writes use an atomic replacement, so a failed save "
        "leaves the previous file intact. Explicit launch flags still win "
        "for that run.");
    ImGui::SeparatorText("Settings persistence");
    ImGui::TextWrapped("Path: %s",
        g_preferences.path.empty()
            ? "unavailable" : g_preferences.path.u8string().c_str());
    if (g_preferences.save_failed)
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                           "Not saved");
    else if (g_preferences.dirty)
        ImGui::TextColored(ImVec4(1.0f, 0.73f, 0.25f, 1.0f),
                           "Unsaved changes");
    else
        ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.45f, 1.0f), "Saved");
    if (!g_preferences.status.empty())
        ImGui::TextWrapped("%s", g_preferences.status.c_str());
    if (g_preferences.dirty && ImGui::Button("Save now"))
        (void)flush_preferences();
    ImGui::TextDisabled(
        "Frame-interpolation activation, current vertical pitch, menu layout, "
        "mouse capture, experimental draw distance/depth fade and diagnostic "
        "counters are intentionally not persisted. Cheat controls also start "
        "off each launch.");
    ImGui::SeparatorText("Restart required");
    ImGui::BulletText("Renderer backend");
    ImGui::BulletText("Supersampling/internal framebuffer allocation");
    ImGui::BulletText("Audio backend and buffer configuration");
    ImGui::BulletText("BIOS, disc and memory-card wiring");
    ImGui::Separator();
    if (ImGui::Button("Close menu")) set_menu_open(false);
}

void draw_menu() {
    bool keep_open = g_menu.open;
    ImGui::SetNextWindowSize(ImVec2(760.0f, 560.0f),
                             ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Disruptor Settings & Developer Tools", &keep_open,
                     ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled("Press ` to toggle; Escape closes the menu.");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.73f, 0.25f, 1.0f),
                           "Developer build");
        ImGui::TextColored(ImVec4(1.0f, 0.73f, 0.25f, 1.0f),
                           "The game is not paused while this menu is open.");

        if (ImGui::BeginTabBar("DisruptorDevTabs")) {
            if (ImGui::BeginTabItem("Controls")) {
                draw_controls_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Enhancements")) {
                draw_enhancements_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Cheats")) {
                draw_cheats_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Diagnostics")) {
                draw_diagnostics_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("System")) {
                draw_system_tab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    if (!keep_open && g_menu.open) set_menu_open(false);
}

void on_runtime_ready(void *, SDL_Window *window, int backend) {
    disruptor_cheats_reset_session();
    disruptor_far_rendering_reset_session();
    disruptor_mouse_recenter_vertical();
    g_menu.window = window;
    g_menu.backend = backend;
    load_preferences_for_session();
    if (backend == PSX_HOST_UI_BACKEND_OPENGL) (void)initialize_imgui();
}

void on_runtime_shutdown(void *) {
    disruptor_cheats_reset_session();
    disruptor_far_rendering_reset_session();
    disruptor_mouse_recenter_vertical();
    (void)flush_preferences();
    if (disruptor_mouse_captured())
        (void)disruptor_mouse_set_captured(0);
    if (g_menu.base_keybinds_valid)
        apply_modern_keybinds(false);
    g_menu.open = false;
    g_menu.restore_mouse_capture = false;
    if (g_menu.imgui_ready) {
        ImGui_ImplOpenGL3_Shutdown();
        imgui_sdl_shutdown();
        ImGui::DestroyContext();
    }
    g_menu = DevMenuState{};
}

int on_sdl_event(void *, const SDL_Event *event) {
    const bool toggle = scancode_event(event, SDL_SCANCODE_GRAVE);
    const bool close =
        g_menu.open && scancode_event(event, SDL_SCANCODE_ESCAPE);
    if (toggle || close) {
        set_menu_open(toggle ? !g_menu.open : false);
        return 1;
    }
    if (!g_menu.open || !g_menu.imgui_ready) return 0;
    imgui_sdl_process_event(event);
    return 1;
}

uint32_t current_flags(void *) {
    if (!g_menu.open || !g_menu.imgui_ready) return 0;
    return PSX_HOST_UI_CAPTURE_KEYBOARD |
           PSX_HOST_UI_CAPTURE_MOUSE |
           PSX_HOST_UI_CAPTURE_GAMEPAD |
           PSX_HOST_UI_SUSPEND_INTERPOLATION |
           PSX_HOST_UI_VISIBLE;
}

void render_gl(void *, int, int) {
    if (!g_menu.open || !initialize_imgui()) return;
    ImGui_ImplOpenGL3_NewFrame();
    imgui_sdl_new_frame();
    ImGui::NewFrame();
    draw_menu();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

struct MenuRegistration {
    MenuRegistration() {
        PsxHostUiHooks hooks{};
        hooks.abi_version = PSX_HOST_UI_ABI_VERSION;
        hooks.struct_size = static_cast<uint32_t>(sizeof(hooks));
        hooks.on_runtime_ready = on_runtime_ready;
        hooks.on_runtime_shutdown = on_runtime_shutdown;
        hooks.on_sdl_event = on_sdl_event;
        hooks.flags = current_flags;
        hooks.render_gl = render_gl;
        if (!psx_host_ui_register(&hooks)) {
            std::fprintf(stderr,
                         "disruptor: developer menu registration failed\n");
        }
    }
};

MenuRegistration g_registration;

}  // namespace
