#!/usr/bin/env python3
"""Source contract for the borderless window that must not cover its screen exactly: where the host and the renderer use it."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


host = read("psxrecomp-overlay/runtime/src/main.cpp")
renderer = read("psxrecomp-overlay/runtime/src/gpu_gl_renderer.c")
cmake = read("CMakeLists.txt")

keep = host[
    host.index("static void psx_borderless_keep_windowed(void) {") : host.index(
        "static void psx_borderless_keep_windowed(void) {}"
    )
]
require(
    "#if defined(_WIN32) && defined(PSX_SDL3)\n/* Keeps a borderless OpenGL window" in host
    and "    int hidden = 0;\n    if (sdl_window && g_video_renderer == 1 && psx_window_fullscreen_mode(sdl_window) == 1) {\n"
    in keep
    and "GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &info) && GetWindowRect(handle, &now)"
    in keep
    and "            if (psx_borderless_wanted(window, screen, &wanted) &&\n"
    "                SetWindowPos(handle, nullptr, wanted.left, wanted.top, wanted.right - wanted.left, wanted.bottom - wanted.top,\n"
    "                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {\n                window = wanted;\n"
    in keep,
    "only a borderless window of the OpenGL renderer is moved, to where the rule says, and by nothing but its place and size",
)
require(
    "            SDL_GetWindowSizeInPixels(sdl_window, &pixels_wide, &pixels_high);\n"
    "            hidden = psx_borderless_hidden_rows(window, screen, pixels_high);\n"
    in keep
    and keep.rstrip().endswith("    gl_renderer_set_hidden_rows(hidden);\n}\n#else")
    and "static void sdl_vblank_present(void) {\n    psx_borderless_keep_windowed();\n" in host
    and "#endif\n    psx_borderless_keep_windowed(); /* at once: a present may come before the next vblank */\n"
    "    return psx_window_fullscreen_mode(window) == mode ? 1 : 0;\n"
    in host,
    "every frame the renderer is told how many rows are above the screen, none in any other mode: SDL fits the window "
    "to the screen again when it is restored",
)
require(
    "void gl_renderer_set_hidden_rows(int rows) { s_hidden_rows = rows > 0 ? rows : 0; }\n"
    "static void gl_visible_size(int *w, int *h) {\n    SDL_GL_GetDrawableSize(s_win, w, h);\n"
    "    if (*h > s_hidden_rows) *h -= s_hidden_rows;\n}\n"
    in renderer
    and renderer.count("    int ww = 0, wh = 0; gl_visible_size(&ww, &wh);\n") == 5
    and renderer.count("SDL_GL_GetDrawableSize(") == 1
    and "void gl_renderer_set_hidden_rows(int rows) { (void)rows; }\n" in renderer,
    "every present of the OpenGL renderer fits its picture to the rows that are on the screen",
)
require(
    "+runtime/include/host_borderless.h" in read("PSXRECOMP_OVERLAY_FILES.txt").splitlines()
    and "    add_test(NAME host_borderless\n        COMMAND host-borderless-test)\n" in cmake
    and 'tests/test_borderless_contract.py")' in cmake,
    "the header is part of the overlay and its tests remain registered",
)

print("Host borderless window source contract: PASS")
