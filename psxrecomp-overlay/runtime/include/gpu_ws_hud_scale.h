#ifndef PSXRECOMP_GPU_WS_HUD_SCALE_H
#define PSXRECOMP_GPU_WS_HUD_SCALE_H

#include <stdint.h>

#define PSX_WS_HUD_SCALE_MIN 50
#define PSX_WS_HUD_SCALE_MAX 100

static inline int psx_ws_hud_scale_clamp(int percent) {
    if (percent < PSX_WS_HUD_SCALE_MIN) return PSX_WS_HUD_SCALE_MIN;
    if (percent > PSX_WS_HUD_SCALE_MAX) return PSX_WS_HUD_SCALE_MAX;
    return percent;
}

/* One edge of a widget, moved toward `pivot` by numerator/denominator and
 * rounded to nearest. Two widgets with one pivot that share an edge share it
 * afterwards, unless one of them is held at its one pixel minimum. */
static inline int32_t psx_ws_hud_scale_edge(int32_t value, int32_t pivot,
                                            int64_t numerator,
                                            int64_t denominator) {
    const int64_t scaled = (int64_t)(value - pivot) * numerator;
    const int64_t half = denominator / 2;
    return pivot + (int32_t)((scaled >= 0 ? scaled + half : scaled - half) /
                             denominator);
}

/* Twice the column the thirds rule takes for the middle of [x, x + w). A game's line of text, one picture `strip` wide
 * with its words at the left end, is not in the right third until it begins there. */
static inline int32_t psx_ws_hud_thirds_centre(int32_t x, int32_t w, int32_t strip, int32_t screen_w) {
    const int32_t centre = 2 * x + w;
    return w == strip && 3 * centre > 4 * screen_w && 3 * x <= 2 * screen_w ? screen_w : centre;
}

/* Shrinks a screen-space widget toward the edge or corner of the screen it
 * sits at: thirds of the width and of the height pick the pivot, as the stock
 * horizontal squash does. A counter whose pieces all centre in one third keeps
 * one pivot. Pieces in different thirds are pulled apart. The
 * horizontal factor includes the squash. The middle column below the top
 * third is left alone: the first-person weapon and its effects are drawn
 * there with the same primitives. Returns 0 when nothing was changed.
 * `strip` is how wide the game's lines of text are where one may be meant, else 0. */
static inline int psx_ws_hud_scale_rect_strip(int32_t *x, int32_t *y,
                                              int32_t *w, int32_t *h,
                                              int32_t screen_w, int32_t screen_h,
                                              int32_t squash_num, int32_t squash_den,
                                              int percent, int32_t strip) {
    int32_t centre_x, centre_y, pivot_x, pivot_y, x0, x1, y0, y1;
    int left, right, top, bottom;
    if (percent >= PSX_WS_HUD_SCALE_MAX || percent < PSX_WS_HUD_SCALE_MIN ||
        *w <= 0 || *h <= 0 || screen_w <= 0 || screen_h <= 0 ||
        squash_num <= 0 || squash_den <= 0)
        return 0;
    centre_x = psx_ws_hud_thirds_centre(*x, *w, strip, screen_w);
    centre_y = 2 * *y + *h;
    left = 3 * centre_x < 2 * screen_w;
    right = 3 * centre_x > 4 * screen_w;
    top = 3 * centre_y < 2 * screen_h;
    bottom = 3 * centre_y > 4 * screen_h;
    if (!left && !right && !top) return 0;
    pivot_x = left ? 0 : right ? screen_w : screen_w / 2;
    pivot_y = top ? 0 : bottom ? screen_h : screen_h / 2;
    x0 = psx_ws_hud_scale_edge(*x, pivot_x, (int64_t)squash_num * percent,
                               (int64_t)squash_den * 100);
    x1 = psx_ws_hud_scale_edge(*x + *w, pivot_x, (int64_t)squash_num * percent,
                               (int64_t)squash_den * 100);
    y0 = psx_ws_hud_scale_edge(*y, pivot_y, percent, 100);
    y1 = psx_ws_hud_scale_edge(*y + *h, pivot_y, percent, 100);
    *x = x0;
    *w = x1 > x0 ? x1 - x0 : 1;
    *y = y0;
    *h = y1 > y0 ? y1 - y0 : 1;
    return 1;
}

static inline int psx_ws_hud_scale_rect(int32_t *x, int32_t *y,
                                        int32_t *w, int32_t *h,
                                        int32_t screen_w, int32_t screen_h,
                                        int32_t squash_num, int32_t squash_den,
                                        int percent) {
    return psx_ws_hud_scale_rect_strip(x, y, w, h, screen_w, screen_h, squash_num, squash_den, percent, 0);
}

#endif /* PSXRECOMP_GPU_WS_HUD_SCALE_H */
