/*
 * Host-side in-game settings and diagnostics menu for Disruptor.
 *
 * This overlay is deliberately outside the emulated PlayStation.  It draws
 * after the completed host presentation, never writes guest VRAM/RAM, and
 * calls only the narrow live-setting APIs exposed by the runtime and the
 * Disruptor input module.
 */

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
    bool base_keybinds_valid = false;
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

    bool high_precision = disruptor_high_precision_camera_enabled() != 0;
    const bool exact_geometry = gpu_geometry_correction_enabled() != 0;
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

void draw_enhancements_tab() {
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
        "intentionally excluded from the normal in-game menu.");
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
        "Frame-interpolation activation, menu layout, mouse capture and "
        "diagnostic counters are intentionally not persisted.");
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
    g_menu.window = window;
    g_menu.backend = backend;
    load_preferences_for_session();
    if (backend == PSX_HOST_UI_BACKEND_OPENGL) (void)initialize_imgui();
}

void on_runtime_shutdown(void *) {
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
