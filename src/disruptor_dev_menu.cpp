/*
 * Host-side in-game settings menu for Disruptor.
 *
 * This overlay is deliberately outside the emulated PlayStation.  It draws
 * after the completed host presentation. Normal settings never write guest
 * VRAM/RAM; the Cheats tab calls only the narrow, version-pinned Disruptor
 * gameplay APIs whose save consequences are stated in the UI.
 */

#include "disruptor_cheats.h"
#include "disruptor_billboard_aspect.h"
#include "disruptor_far_rendering.h"
#include "disruptor_frame_rate.h"
#include "disruptor_capsule.h"
#include "disruptor_intro_skip.h"
#include "disruptor_mouse_aim.h"
#include "disruptor_present_rate.h"
#include "disruptor_restart.h"
#include "config_loader.h"
#include "gpu.h"
#include "host_ui.h"
#include "psx_keybinds.h"
#include "psx_netplay.h"

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
#include <string_view>

extern "C" void gte_ws_set_far_threshold(int threshold);
extern "C" void gte_ws_set_backdrop_repair_enabled(int enabled);

namespace {

struct DevMenuState {
    SDL_Window *window = nullptr;
    int backend = PSX_HOST_UI_BACKEND_NONE;
    bool imgui_ready = false;
    bool open = false;
    bool restore_mouse_capture = false;
    bool aspect_apply_failed = false;
    bool internal_scale_apply_failed = false;
    bool fullscreen_apply_failed = false;
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
    PREF_GEOMETRY          = 1u << 5,
    PREF_TEXTURES          = 1u << 6,
    PREF_VSYNC             = 1u << 7,
    PREF_VERTICAL_LOOK     = 1u << 10,
    PREF_VERTICAL_SENS     = 1u << 11,
    PREF_INVERT_Y          = 1u << 12,
    PREF_ASPECT            = 1u << 13,
    PREF_SUPERSAMPLING     = 1u << 14,
    PREF_FULLSCREEN        = 1u << 15,
    PREF_MASTER_VOLUME     = 1u << 16,
    PREF_AUDIO_MUTED       = 1u << 17,
    PREF_HUD_SCALE         = 1u << 18,
    PREF_FRAME_UNLOCK      = 1u << 19,
    PREF_SHADOW_SHAPE      = 1u << 20,
    PREF_PRESENT_RATE      = 1u << 21,
    PREF_SKIP_INTRO        = 1u << 22,
    PREF_PRESENT_ENABLED   = 1u << 23,
    PREF_LANGUAGE_DISC     = 1u << 24,
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
std::string g_language_disc;

std::string utf8(const fs::path &path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

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

void mark_aspect(int numerator, int denominator, bool adaptive) {
    g_preferences.pending.has_aspect_ratio = true;
    g_preferences.pending.aspect_num = numerator;
    g_preferences.pending.aspect_den = denominator;
    /* Keep the adaptive flag and its fixed 32:9 cap in the same atomic
     * settings update as the fallback/initial aspect. */
    g_preferences.pending.has_adaptive_view = true;
    g_preferences.pending.adaptive_view = adaptive;
    g_preferences.dirty |= PREF_ASPECT;
}

void mark_supersampling(int value) {
    g_preferences.pending.has_supersampling = true;
    g_preferences.pending.supersampling = value;
    g_preferences.dirty |= PREF_SUPERSAMPLING;
}

void mark_fullscreen(int value) {
    g_preferences.pending.has_fullscreen = true;
    g_preferences.pending.fullscreen = value;
    g_preferences.dirty |= PREF_FULLSCREEN;
}

void mark_master_volume(int value) {
    g_preferences.pending.has_master_volume = true;
    g_preferences.pending.master_volume = value;
    g_preferences.dirty |= PREF_MASTER_VOLUME;
}

void mark_audio_muted(bool value) {
    g_preferences.pending.has_audio_muted = true;
    g_preferences.pending.audio_muted = value;
    g_preferences.dirty |= PREF_AUDIO_MUTED;
}

void mark_skip_intro(bool value) {
    g_preferences.pending.has_skip_intro = true;
    g_preferences.pending.skip_intro = value;
    g_preferences.dirty |= PREF_SKIP_INTRO;
}

void mark_language_disc(const std::string &path) {
    g_language_disc = path;
    g_preferences.pending.has_language_disc = true;
    g_preferences.pending.language_disc = path;
    g_preferences.dirty |= PREF_LANGUAGE_DISC;
}

void mark_hud_scale(int value) {
    g_preferences.pending.has_hud_scale = true;
    g_preferences.pending.hud_scale = value;
    g_preferences.dirty |= PREF_HUD_SCALE;
}

void mark_frame_unlock(bool value) {
    g_preferences.pending.has_frame_unlock = true;
    g_preferences.pending.frame_unlock = value;
    g_preferences.dirty |= PREF_FRAME_UNLOCK;
}

void mark_shadow_shape(bool improved) {
    g_preferences.pending.has_improved_shadows = true;
    g_preferences.pending.improved_shadows = improved;
    g_preferences.dirty |= PREF_SHADOW_SHAPE;
}

void mark_present_rate(int value) {
    g_preferences.pending.has_frame_interpolation_fps = true;
    g_preferences.pending.frame_interpolation_fps = value;
    g_preferences.dirty |= PREF_PRESENT_RATE;
}

void mark_present_enabled(bool value) {
    g_preferences.pending.has_frame_interpolation = true;
    g_preferences.pending.frame_interpolation = value;
    g_preferences.dirty |= PREF_PRESENT_ENABLED;
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
    if (g_preferences.dirty & PREF_ASPECT) {
        settings.has_aspect_ratio = true;
        settings.aspect_num = pending.aspect_num;
        settings.aspect_den = pending.aspect_den;
        settings.has_adaptive_view = true;
        settings.adaptive_view = pending.adaptive_view;
    }
    if (g_preferences.dirty & PREF_SUPERSAMPLING) {
        settings.has_supersampling = true;
        settings.supersampling = pending.supersampling;
    }
    if (g_preferences.dirty & PREF_FULLSCREEN) {
        settings.has_fullscreen = true;
        settings.fullscreen = pending.fullscreen;
    }
    if (g_preferences.dirty & PREF_MASTER_VOLUME) {
        settings.has_master_volume = true;
        settings.master_volume = pending.master_volume;
    }
    if (g_preferences.dirty & PREF_AUDIO_MUTED) {
        settings.has_audio_muted = true;
        settings.audio_muted = pending.audio_muted;
    }
    if (g_preferences.dirty & PREF_SKIP_INTRO) {
        settings.has_skip_intro = true;
        settings.skip_intro = pending.skip_intro;
    }
    if (g_preferences.dirty & PREF_LANGUAGE_DISC) {
        settings.has_language_disc = true;
        settings.language_disc = pending.language_disc;
    }
    if (g_preferences.dirty & PREF_HUD_SCALE) {
        settings.has_hud_scale = true;
        settings.hud_scale = pending.hud_scale;
    }
    if (g_preferences.dirty & PREF_FRAME_UNLOCK) {
        settings.has_frame_unlock = true;
        settings.frame_unlock = pending.frame_unlock;
    }
    if (g_preferences.dirty & PREF_SHADOW_SHAPE) {
        settings.has_improved_shadows = true;
        settings.improved_shadows = pending.improved_shadows;
    }
    if (g_preferences.dirty & PREF_PRESENT_RATE) {
        settings.has_frame_interpolation_fps = true;
        settings.frame_interpolation_fps = pending.frame_interpolation_fps;
    }
    if (g_preferences.dirty & PREF_PRESENT_ENABLED) {
        settings.has_frame_interpolation = true;
        settings.frame_interpolation = pending.frame_interpolation;
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
                 utf8(g_preferences.path).c_str());
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

    /* An inherited explicit environment override can enable Modern controls
     * without a run-script switch (and therefore without its preset copy).
     * Always reconcile the live mode with the in-memory keyboard overlay so
     * S/A do not retain conflicting classic face-button bindings. */
    apply_modern_keybinds(disruptor_modern_controls_enabled() != 0);

    /* main.cpp applies these fields before the environment layer. Repeat the
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
    if (settings.has_supersampling)
        (void)psx_host_video_set_internal_scale(settings.supersampling);
    if (settings.has_fullscreen)
        g_menu.fullscreen_apply_failed =
            psx_host_video_set_fullscreen_mode(settings.fullscreen) == 0;
    if (settings.has_master_volume)
        (void)psx_host_audio_set_master_volume(settings.master_volume);
    if (settings.has_audio_muted)
        (void)psx_host_audio_set_muted(settings.audio_muted ? 1 : 0);
    if (settings.has_hud_scale) gpu_ws_set_hud_scale(settings.hud_scale);
    if (settings.has_frame_unlock &&
        !env_override_present("PSX_DISRUPTOR_FRAME_UNLOCK"))
        disruptor_frame_rate_set_unlocked(settings.frame_unlock ? 1 : 0);
    if (settings.has_skip_intro &&
        !env_override_present("PSX_DISRUPTOR_SKIP_INTRO"))
        disruptor_intro_skip_set_enabled(settings.skip_intro ? 1 : 0);
    if (settings.has_language_disc) g_language_disc = settings.language_disc;
    if (settings.has_improved_shadows && !env_override_present("PSX_DISRUPTOR_IMPROVED_SHADOWS"))
        gpu_set_shadow_shape(settings.improved_shadows ? 1 : 0);
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
    if (g_preferences.dirty & PREF_GEOMETRY)
        gpu_geometry_correction_set(pending.geometry_correction ? 1 : 0);
    if (g_preferences.dirty & PREF_TEXTURES)
        gpu_texture_correction_set(
            pending.perspective_textures &&
                    gpu_geometry_correction_enabled()
                ? 1 : 0);
    if (g_preferences.dirty & PREF_VSYNC)
        (void)psx_host_video_set_vsync(pending.vsync);
    if (g_preferences.dirty & PREF_ASPECT) {
        if (pending.adaptive_view)
            (void)psx_host_video_set_adaptive_view(1);
        else
            (void)psx_host_video_set_display_aspect(
                pending.aspect_num, pending.aspect_den);
    }
    if (g_preferences.dirty & PREF_SUPERSAMPLING)
        (void)psx_host_video_set_internal_scale(pending.supersampling);
    if (g_preferences.dirty & PREF_FULLSCREEN)
        g_menu.fullscreen_apply_failed =
            psx_host_video_set_fullscreen_mode(pending.fullscreen) == 0;
    if (g_preferences.dirty & PREF_MASTER_VOLUME)
        (void)psx_host_audio_set_master_volume(pending.master_volume);
    if (g_preferences.dirty & PREF_AUDIO_MUTED)
        (void)psx_host_audio_set_muted(pending.audio_muted ? 1 : 0);
    if (g_preferences.dirty & PREF_HUD_SCALE)
        gpu_ws_set_hud_scale(pending.hud_scale);
    if (g_preferences.dirty & PREF_FRAME_UNLOCK)
        disruptor_frame_rate_set_unlocked(pending.frame_unlock ? 1 : 0);
    if (g_preferences.dirty & PREF_SKIP_INTRO)
        disruptor_intro_skip_set_enabled(pending.skip_intro ? 1 : 0);
    if (g_preferences.dirty & PREF_SHADOW_SHAPE)
        gpu_set_shadow_shape(pending.improved_shadows ? 1 : 0);
#ifndef PSX_DISABLE_FRAME_INTERPOLATION
    if (g_preferences.dirty & PREF_PRESENT_ENABLED)
        (void)disruptor_present_rate_enable(pending.frame_interpolation ? 1 : 0);
    if (g_preferences.dirty & PREF_PRESENT_RATE)
        (void)disruptor_present_rate_apply(pending.frame_interpolation_fps);
#endif
}

void load_preferences_for_session() {
    const char *path = psx_host_user_settings_path();
    if (!path || !path[0]) {
        g_preferences.loaded = false;
        g_preferences.status = "Runtime settings path is unavailable.";
        return;
    }
    const std::string_view text(path);
    g_preferences.path = fs::path(std::u8string(text.begin(), text.end()));
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
#ifndef PSX_DISABLE_FRAME_INTERPOLATION
    if (!enabled) {
        (void)disruptor_present_rate_enable(0);
        mark_present_enabled(false);
    }
#endif
    gpu_geometry_correction_set(enabled ? 1 : 0);
    mark_geometry(enabled);
}

void draw_aspect_controls() {
    static constexpr int kAspectNumerators[] = {16, 21, 32, 32};
    static constexpr int kAspectDenominators[] = {9, 9, 9, 9};
    static const char *kAspectLabels[] = {
        "16:9", "21:9", "32:9", "Match window (up to 32:9)"};
    static constexpr int kFixedWideAspectCount = 3;
    static constexpr int kAdaptiveAspectIndex = 3;
    static constexpr int kAspectCount = 4;

    int numerator = 4;
    int denominator = 3;
    psx_host_video_get_display_aspect(&numerator, &denominator);
    const bool adaptive = psx_host_video_get_adaptive_view() != 0;
    bool widescreen = adaptive || numerator * 3 != denominator * 4;
    if (adaptive) {
        g_menu.selected_wide_aspect = kAdaptiveAspectIndex;
    } else if (widescreen) {
        for (int index = 0; index < kFixedWideAspectCount; ++index) {
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
        if (widescreen &&
            g_menu.selected_wide_aspect == kAdaptiveAspectIndex) {
            g_menu.aspect_apply_failed =
                psx_host_video_set_adaptive_view(1) == 0;
            if (!g_menu.aspect_apply_failed)
                mark_aspect(32, 9, true);
        } else {
            const int requested_num = widescreen
                ? kAspectNumerators[g_menu.selected_wide_aspect] : 4;
            const int requested_den = widescreen
                ? kAspectDenominators[g_menu.selected_wide_aspect] : 3;
            g_menu.aspect_apply_failed =
                psx_host_video_set_display_aspect(
                    requested_num, requested_den) == 0;
            if (!g_menu.aspect_apply_failed)
                mark_aspect(requested_num, requested_den, false);
        }
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    int selected = g_menu.selected_wide_aspect;
    if (ImGui::Combo("Aspect ratio", &selected, kAspectLabels,
                     kAspectCount)) {
        g_menu.selected_wide_aspect = selected;
        if (widescreen) {
            if (selected == kAdaptiveAspectIndex) {
                g_menu.aspect_apply_failed =
                    psx_host_video_set_adaptive_view(1) == 0;
                if (!g_menu.aspect_apply_failed)
                    mark_aspect(32, 9, true);
            } else {
                const int requested_num = kAspectNumerators[selected];
                const int requested_den = kAspectDenominators[selected];
                g_menu.aspect_apply_failed =
                    psx_host_video_set_display_aspect(
                        requested_num, requested_den) == 0;
                if (!g_menu.aspect_apply_failed)
                    mark_aspect(requested_num, requested_den, false);
            }
        }
    }
    if (g_menu.aspect_apply_failed) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
            "The runtime rejected the requested display aspect.");
    }
    ImGui::TextDisabled(
        "4:3 disables widescreen. Fixed 16:9, 21:9 and 32:9 choices or "
        "live window matching (capped at 32:9) apply after the current "
        "frame and are saved.");

    if (gpu_sprite_placement_available()) {
        int shadow_shape = gpu_shadow_shape();
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::Combo("Shadows", &shadow_shape, "Vanilla\0Improved\0")) {
            gpu_set_shadow_shape(shadow_shape);
            mark_shadow_shape(shadow_shape != 0);
        }
        draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
        ImGui::TextDisabled(
            "Improved lays a shadow flat on the floor: as wide as the game makes it and as tall "
            "as the floor's perspective gives. Works with geometry correction on.");
    }

    int hud_scale = gpu_ws_hud_scale();
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::SliderInt("HUD size", &hud_scale, 50, 100, "%d%%",
                         ImGuiSliderFlags_AlwaysClamp)) {
        gpu_ws_set_hud_scale(hud_scale);
        mark_hud_scale(hud_scale);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "Widescreen only. Shrinks the health and ammunition displays toward "
        "their screen edges. The weapon keeps its size.");
}

void draw_internal_resolution_controls() {
    static const char *kScaleLabels[] = {
        "1x (native)", "2x", "3x", "4x", "5x", "6x", "7x", "8x"};
    static constexpr int kScaleCount = 8;
    int selected = std::clamp(
        psx_host_video_get_internal_scale(), 1, kScaleCount) - 1;

    ImGui::SeparatorText("Rendering resolution");
    if (ImGui::Combo("Internal resolution scale", &selected,
                     kScaleLabels, kScaleCount)) {
        const int requested = selected + 1;
        g_menu.internal_scale_apply_failed =
            psx_host_video_set_internal_scale(requested) == 0;
        if (!g_menu.internal_scale_apply_failed)
            mark_supersampling(requested);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    if (g_menu.internal_scale_apply_failed) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
            "The renderer could not allocate that internal resolution.");
    }
    ImGui::TextDisabled(
        "Supersamples geometry and shading before presentation. GPU fill and "
        "memory cost grow roughly with the square of this value; changes "
        "apply immediately and are saved.");
}

void draw_enhancements_tab() {
    draw_aspect_controls();
    draw_internal_resolution_controls();

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

    ImGui::SeparatorText("Synchronisation");
    int vsync = psx_host_video_get_vsync();
    int vsync_index = std::clamp(vsync + 1, 0, 2);
    static const char *kVsyncLabels[] = {
        "Adaptive", "Immediate", "Synchronised"};
    if (ImGui::Combo("VSync", &vsync_index, kVsyncLabels, 3)) {
        const int requested = vsync_index - 1;
        if (psx_host_video_set_vsync(requested)) mark_vsync(requested);
    }

    bool unlocked = disruptor_frame_rate_unlocked() != 0;
    if (ImGui::Checkbox("60 FPS gameplay (experimental)", &unlocked)) {
        disruptor_frame_rate_set_unlocked(unlocked ? 1 : 0);
        mark_frame_unlock(unlocked);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    DisruptorFrameRateWindow window{};
    if (disruptor_frame_rate_last_window(&window) && window.vblanks != 0u) {
        ImGui::TextDisabled(
            "Game frames: %.1f per second. Frame work: %.2f VBlank average, "
            "%.2f peak, %u of %u frames over one VBlank.",
            59.94 * window.frames / window.vblanks,
            window.average_work_permille / 1000.0,
            window.peak_work_permille / 1000.0,
            window.late_frames, window.frames);
    }

#ifndef PSX_DISABLE_FRAME_INTERPOLATION
    int present_enabled = 0;
    if (disruptor_present_rate_switch(geometry ? 1 : 0, &present_enabled))
        mark_present_enabled(present_enabled != 0);
    int present_rate = 0;
    if (disruptor_present_rate_control(&present_rate)) mark_present_rate(present_rate);
#endif

    ImGui::SeparatorText("Startup");
    bool skip_intro = disruptor_intro_skip_enabled() != 0;
    if (ImGui::Checkbox("Skip the logos and the title movie", &skip_intro)) {
        disruptor_intro_skip_set_enabled(skip_intro ? 1 : 0);
        mark_skip_intro(skip_intro);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled("Escape skips the logo or movie on screen.");
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

void draw_fullscreen_controls() {
    static const char *kFullscreenLabels[] = {
        "Windowed", "Borderless fullscreen", "Exclusive fullscreen"};
    int mode = std::clamp(psx_host_video_get_fullscreen_mode(), 0, 2);

    ImGui::SeparatorText("Window presentation");
    if (ImGui::Combo("Display mode", &mode, kFullscreenLabels, 3)) {
        g_menu.fullscreen_apply_failed =
            psx_host_video_set_fullscreen_mode(mode) == 0;
        if (!g_menu.fullscreen_apply_failed)
            mark_fullscreen(mode);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    if (g_menu.fullscreen_apply_failed) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
            "The runtime rejected the requested display mode.");
    }
    ImGui::TextDisabled(
        "Menu selections apply immediately and are saved. Alt+Enter remains "
        "a transient window/fullscreen toggle and does not overwrite the "
        "saved mode.");
}

void draw_audio_controls() {
    ImGui::SeparatorText("Audio output");
    int volume = psx_host_audio_get_master_volume();
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::SliderInt("Master volume", &volume, 0, 100, "%d%%",
                         ImGuiSliderFlags_AlwaysClamp) &&
        psx_host_audio_set_master_volume(volume)) {
        mark_master_volume(volume);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool muted = psx_host_audio_get_muted() != 0;
    if (ImGui::Checkbox("Mute all audio", &muted) &&
        psx_host_audio_set_muted(muted ? 1 : 0)) {
        mark_audio_muted(muted);
    }
    ImGui::TextDisabled(
        "Host-side output only: game audio state and saves are unchanged. "
        "Unmuting restores the selected volume.");
}

void draw_system_tab() {
    draw_fullscreen_controls();
    draw_audio_controls();
    ImGui::TextWrapped(
        "Live menu choices are merged into the runtime's user-owned "
        "settings.toml. Writes use an atomic replacement, so a failed save "
        "leaves the previous file intact. Explicit launch flags still win "
        "for that run.");
    ImGui::SeparatorText("Settings persistence");
    ImGui::TextWrapped("Path: %s",
        g_preferences.path.empty()
            ? "unavailable" : utf8(g_preferences.path).c_str());
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
        "Current vertical pitch, menu layout, "
        "and mouse capture are not saved. Cheat controls start "
        "off each launch.");
    ImGui::SeparatorText("Language");
    ImGui::TextWrapped("Disc of another region: %s", g_language_disc.empty() ? "none, the game is in English" : g_language_disc.c_str());
    if (ImGui::Button("Choose a disc image")) (void)psx_host_pick_disc_image_begin();
    ImGui::SameLine();
    if (ImGui::Button("English") && !g_language_disc.empty()) mark_language_disc(std::string());
    ImGui::SameLine();
    if (ImGui::Button("Restart now") && !psx_netplay_active() && flush_preferences() && disruptor_restart_arm()) {
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
    }
    ImGui::TextDisabled("Movies, speech and words come from that disc at the next start. The French, German and Japanese discs are known.");
    ImGui::TextDisabled("The Japanese disc gives movies, speech and hints: its menus are in English on the disc itself.");
    ImGui::SeparatorText("Restart required");
    ImGui::BulletText("Renderer backend");
    ImGui::BulletText("Audio backend and buffer configuration");
    ImGui::BulletText("BIOS, disc and memory-card wiring");
    ImGui::Separator();
    if (ImGui::Button("Close menu")) set_menu_open(false);
}

void draw_menu() {
    char picked[4096];
    if (psx_host_pick_disc_image_poll(picked, static_cast<int>(sizeof picked)) == 1) mark_language_disc(picked);
    bool keep_open = g_menu.open;
    ImGui::SetNextWindowSize(ImVec2(760.0f, 560.0f),
                             ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Disruptor Settings", &keep_open,
                     ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled("Press ` to toggle; Escape closes the menu.");
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
    disruptor_billboard_aspect_set_site_mask(
        disruptor_billboard_aspect_all_site_mask());
    disruptor_mouse_recenter_vertical();
    disruptor_high_precision_camera_set_enabled(0);
    gte_ws_set_far_threshold(900);
    gte_ws_set_backdrop_repair_enabled(0);
    g_menu.window = window;
    g_menu.backend = backend;
    load_preferences_for_session();
    if (backend == PSX_HOST_UI_BACKEND_OPENGL) (void)initialize_imgui();
}

void on_runtime_shutdown(void *) {
    disruptor_cheats_reset_session();
    disruptor_far_rendering_reset_session();
    disruptor_billboard_aspect_set_site_mask(
        disruptor_billboard_aspect_all_site_mask());
    disruptor_mouse_recenter_vertical();
    disruptor_high_precision_camera_set_enabled(0);
    gte_ws_set_far_threshold(900);
    gte_ws_set_backdrop_repair_enabled(0);
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
    if (!g_menu.open && scancode_event(event, SDL_SCANCODE_ESCAPE))
        disruptor_intro_skip_request();
    if (!g_menu.open && scancode_event(event, SDL_SCANCODE_F12) && (SDL_GetModState() & KMOD_CTRL) != 0) {
        disruptor_capsule_toggle();
        return 1;
    }
    if (!g_menu.open || !g_menu.imgui_ready) return 0;
    imgui_sdl_process_event(event);
    return 1;
}

uint32_t current_flags(void *) {
    if (!g_menu.open || !g_menu.imgui_ready) return disruptor_capsule_notice() ? PSX_HOST_UI_VISIBLE : 0u;
    return PSX_HOST_UI_CAPTURE_KEYBOARD |
           PSX_HOST_UI_CAPTURE_MOUSE |
           PSX_HOST_UI_CAPTURE_GAMEPAD |
           PSX_HOST_UI_VISIBLE;
}

/* Drawn without the SDL backend's frame: that one reads and captures the mouse, which is the game's while the menu is closed. */
void render_notice(const char *word, int width, int height) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f));
    ImGui::Begin("##capsule", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::SetWindowFontScale(std::max(1.0f, static_cast<float>(height) / 360.0f));
    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.2f, 1.0f), "%s", word);
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void render_gl(void *, int width, int height) {
    if (const char *word = g_menu.open ? nullptr : disruptor_capsule_notice()) {
        if (initialize_imgui()) render_notice(word, width, height);
        return;
    }
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
                         "disruptor: settings menu registration failed\n");
        }
    }
};

MenuRegistration g_registration;

}  // namespace
