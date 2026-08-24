#ifndef PSX_HOST_UI_H
#define PSX_HOST_UI_H

/*
 * Optional in-game host UI seam.
 *
 * A statically linked game module registers one callback table before main()
 * (normally from a C++ static constructor).  The runtime forwards SDL events,
 * brackets the SDL/renderer lifetime, and invokes the OpenGL draw callback on
 * the main GL context immediately before SwapWindow.  With no registered
 * provider every function is a cheap no-op and the normal frontend is
 * unchanged.
 *
 * Callbacks and the live video setting functions are main-thread only.  The
 * runtime copies the table during registration, so the caller does not need to
 * keep the PsxHostUiHooks object alive.  `userdata` remains caller-owned.
 */

#include <stddef.h>
#include <stdint.h>

#include "psx_sdl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PSX_HOST_UI_ABI_VERSION 1u

enum PsxHostUiBackend {
    PSX_HOST_UI_BACKEND_NONE     = 0,
    PSX_HOST_UI_BACKEND_SOFTWARE = 1,
    PSX_HOST_UI_BACKEND_OPENGL   = 2,
    PSX_HOST_UI_BACKEND_VULKAN   = 3,
};

enum PsxHostUiFlags {
    /* Suppress the corresponding polled host input before it reaches SIO or a
     * cooperating game mod.  A menu will normally request all three. */
    PSX_HOST_UI_CAPTURE_KEYBOARD = 1u << 0,
    PSX_HOST_UI_CAPTURE_MOUSE    = 1u << 1,
    PSX_HOST_UI_CAPTURE_GAMEPAD  = 1u << 2,

    /* Stop high-refresh interpolation while the UI is open.  This guarantees
     * render_gl runs only on the emulation thread's main GL context. */
    PSX_HOST_UI_SUSPEND_INTERPOLATION = 1u << 3,

    /* The provider has visible GL output.  This defeats unchanged-frame and
     * display-disabled present elision so an interactive UI keeps redrawing. */
    PSX_HOST_UI_VISIBLE = 1u << 4,
};

typedef struct PsxHostUiHooks {
    uint32_t abi_version; /* PSX_HOST_UI_ABI_VERSION */
    uint32_t struct_size; /* sizeof(PsxHostUiHooks)   */
    void *userdata;

    /* Called once after the window and selected renderer are ready.  The GL
     * context is current for PSX_HOST_UI_BACKEND_OPENGL. */
    void (*on_runtime_ready)(void *userdata, SDL_Window *window, int backend);

    /* Called at most once for a ready session and always before its renderer
     * context and SDL window are destroyed. */
    void (*on_runtime_shutdown)(void *userdata);

    /* Called before runtime hotkey handling.  Return non-zero to consume the
     * event.  SDL_QUIT and controller hotplug bookkeeping are never suppressed
     * by consumption. */
    int (*on_sdl_event)(void *userdata, const SDL_Event *event);

    /* Current PsxHostUiFlags.  Queried on the emulation/main thread. */
    uint32_t (*flags)(void *userdata);

    /* Called on the main GL context after the game image is complete and
     * immediately before SwapWindow.  The provider owns/restores any GL state
     * it needs across its draw. */
    void (*render_gl)(void *userdata, int drawable_width, int drawable_height);
} PsxHostUiHooks;

/* Register the sole host-UI provider. Returns 1 on success, 0 for an invalid
 * table, ABI mismatch, or a second distinct provider. */
int psx_host_ui_register(const PsxHostUiHooks *hooks);

/* Public cooperation queries for game-side input modules. */
uint32_t psx_host_ui_capture_flags(void);
int psx_host_ui_game_input_captured(void);

/* UTF-8 path to the runtime's canonical user settings file.  The pointer is
 * process-owned and remains valid for the active runtime session.  This keeps
 * a game-owned UI on the same executable-relative settings.toml as the
 * launcher without making it rediscover platform/AppImage path rules. */
const char *psx_host_user_settings_path(void);

/* Live, session-only video controls.  Persistence belongs to the game UI.
 * Display aspect changes are queued for the next presentation boundary; the
 * fixed choices are 4:3, 16:9, 21:9, and 32:9.  Vsync accepts -1 (adaptive),
 * 0 (immediate), or 1 (synchronised).  Interpolation target accepts -1
 * (uncapped), 0 (display refresh), or 60..1000 FPS; blend accepts 0 (linear)
 * or 1 (motion-adaptive). */
void psx_host_video_get_display_aspect(int *numerator, int *denominator);
int psx_host_video_set_display_aspect(int numerator, int denominator);
int psx_host_video_get_vsync(void);
int psx_host_video_set_vsync(int mode);
void psx_host_video_get_interpolation(int *enabled, int *target_fps,
                                      int *blend_mode);
int psx_host_video_set_interpolation(int enabled, int target_fps,
                                     int blend_mode);

/* Runtime-internal dispatch entry points.  Renderer/front-end code uses these
 * to keep the optional provider out of the core presentation implementation. */
int psx_host_ui_handle_event(const SDL_Event *event);
int psx_host_ui_gl_visible(void);
void psx_host_ui_render_gl(int drawable_width, int drawable_height);

#ifdef __cplusplus
}
#endif

#endif /* PSX_HOST_UI_H */
