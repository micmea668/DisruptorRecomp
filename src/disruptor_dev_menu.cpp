/*
 * Host-side in-game settings and diagnostics menu for Disruptor.
 *
 * This overlay is deliberately outside the emulated PlayStation.  It draws
 * after the completed host presentation, never writes guest VRAM/RAM, and
 * calls only the narrow live-setting APIs exposed by the runtime and the
 * Disruptor input module.
 */

#include "disruptor_mouse_aim.h"
#include "gpu.h"
#include "gpu_gl_renderer.h"
#include "host_ui.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#if defined(PSX_SDL3)
#include "imgui_impl_sdl3.h"
#else
#include "imgui_impl_sdl2.h"
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace {

struct DevMenuState {
    SDL_Window *window = nullptr;
    int backend = PSX_HOST_UI_BACKEND_NONE;
    bool imgui_ready = false;
    bool open = false;
    bool restore_mouse_capture = false;
    bool interpolation_apply_failed = false;
};

DevMenuState g_menu;

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
    io.IniFilename = nullptr;  // persistence is an explicit later milestone
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
    if (ImGui::Checkbox("Horizontal mouse aim", &mouse_aim))
        disruptor_mouse_aim_set_enabled(mouse_aim ? 1 : 0);
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool modern = disruptor_modern_controls_enabled() != 0;
    if (ImGui::Checkbox("Modern keyboard and mouse controls", &modern))
        disruptor_modern_controls_set_enabled(modern ? 1 : 0);
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    float sensitivity =
        static_cast<float>(disruptor_mouse_horizontal_sensitivity());
    if (ImGui::SliderFloat("Horizontal sensitivity", &sensitivity,
                           0.005f, 2.0f, "%.3f",
                           ImGuiSliderFlags_Logarithmic)) {
        (void)disruptor_mouse_set_horizontal_sensitivity(sensitivity);
    }

    bool invert_x = disruptor_mouse_invert_horizontal() != 0;
    if (ImGui::Checkbox("Invert horizontal mouse", &invert_x))
        disruptor_mouse_set_invert_horizontal(invert_x ? 1 : 0);

    bool high_precision = disruptor_high_precision_camera_enabled() != 0;
    const bool exact_geometry = gpu_geometry_correction_enabled() != 0;
    if (!exact_geometry) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Sub-byte camera presentation", &high_precision))
        disruptor_high_precision_camera_set_enabled(high_precision ? 1 : 0);
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
    if (!enabled && gpu_texture_correction_enabled())
        gpu_texture_correction_set(0);
    gpu_geometry_correction_set(enabled ? 1 : 0);
}

void draw_enhancements_tab() {
    bool geometry = gpu_geometry_correction_enabled() != 0;
    if (ImGui::Checkbox("Exact-provenance geometry", &geometry))
        apply_geometry_enabled(geometry);
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool textures = gpu_texture_correction_enabled() != 0;
    if (!geometry) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Perspective-correct world textures", &textures))
        gpu_texture_correction_set(textures ? 1 : 0);
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
    if (!interpolation_enabled) ImGui::BeginDisabled();
    if (ImGui::Combo("Presentation target", &target_index,
                     kTargetLabels, 5)) {
        target_fps = kTargets[target_index];
        g_menu.interpolation_apply_failed =
            psx_host_video_set_interpolation(1, target_fps, blend) == 0;
    }
    static const char *kBlendLabels[] = {
        "Linear crossfade", "Motion-adaptive clarity"};
    if (ImGui::Combo("Blend mode", &blend, kBlendLabels, 2)) {
        g_menu.interpolation_apply_failed =
            psx_host_video_set_interpolation(1, target_fps, blend) == 0;
    }
    if (!interpolation_enabled) ImGui::EndDisabled();

    ImGui::TextWrapped(
        "Interpolation blends completed host images. Guest simulation, audio "
        "and input remain at the original cadence; ghosting and additional "
        "visual latency are possible.");
    if (g_menu.interpolation_apply_failed)
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                           "The renderer rejected the requested interpolation setting.");

    ImGui::SeparatorText("Synchronisation");
    int vsync = psx_host_video_get_vsync();
    int vsync_index = std::clamp(vsync + 1, 0, 2);
    static const char *kVsyncLabels[] = {
        "Adaptive", "Immediate", "Synchronised"};
    if (interpolation_enabled) ImGui::BeginDisabled();
    if (ImGui::Combo("VSync", &vsync_index, kVsyncLabels, 3))
        (void)psx_host_video_set_vsync(vsync_index - 1);
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
        "This first developer-menu milestone keeps changes for the current "
        "session only. Safe, atomic settings persistence will be added after "
        "the live controls have been validated in gameplay.");
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
                           "Session-only developer build");
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
    if (backend == PSX_HOST_UI_BACKEND_OPENGL) (void)initialize_imgui();
}

void on_runtime_shutdown(void *) {
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
