#ifndef PSXRECOMP_HOST_BORDERLESS_H
#define PSXRECOMP_HOST_BORDERLESS_H

/* A borderless OpenGL window that covers its screen exactly is taken by the driver for exclusive fullscreen, and Alt+Tab then behaves as it does there. */
/* Measured on Windows 11 with the window in front: the shell reports "running D3D full screen" for it, and a plain full-screen window once it is one row taller. */

typedef struct PsxHostRect {
    int left, top, right, bottom; /* right and bottom are past the last pixel, as the system gives them */
} PsxHostRect;

/* Where a window that fits its screen exactly must be put instead: one row higher, that row above the screen. Returns 0 when it need not move. */
static inline int psx_borderless_wanted(PsxHostRect window, PsxHostRect screen, PsxHostRect *wanted) {
    if (window.left != screen.left || window.right != screen.right || window.top != screen.top || window.bottom != screen.bottom) return 0;
    *wanted = screen;
    wanted->top = screen.top - 1;
    return 1;
}

/* How many rows of the window's pixels are above the screen, for the renderer to leave out: none unless the window is the screen and more above it. */
static inline int psx_borderless_hidden_rows(PsxHostRect window, PsxHostRect screen, int pixels_high) {
    const int screen_high = screen.bottom - screen.top;
    if (window.left != screen.left || window.right != screen.right || window.bottom != screen.bottom || window.top >= screen.top) return 0;
    return pixels_high > screen_high ? pixels_high - screen_high : 0;
}

#endif /* PSXRECOMP_HOST_BORDERLESS_H */
