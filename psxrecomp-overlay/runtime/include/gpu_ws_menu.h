#ifndef PSXRECOMP_GPU_WS_MENU_H
#define PSXRECOMP_GPU_WS_MENU_H

#include <stdint.h>

#include "gpu_ws_screen_tile.h"

/* The front end's backdrop on a wide screen: a 320x240 picture of 256 colours that the game uploads to (320,0) every frame, logo and all, and draws as two rectangles. */
/* It is stretched to the screen's width with the text squashed first. The logo is lifted out of it and drawn over it squashed, so the logo is as wide as it was. */

enum {
    PSX_WS_MENU_X = 320, /* where the picture is in video memory, in words */
    PSX_WS_MENU_WORDS = 160,
    PSX_WS_MENU_WIDE = 320,
    PSX_WS_MENU_HIGH = 240,
    PSX_WS_MENU_LOGO_LEFT = 59, /* measured on the US disc over twelve scenes: the logo's own colours stand in columns 61..263 and rows 16..48 */
    PSX_WS_MENU_LOGO_RIGHT = 266,
    PSX_WS_MENU_LOGO_TOP = 15,
    PSX_WS_MENU_LOGO_BOTTOM = 51,
    PSX_WS_MENU_LOGO_WIDE = PSX_WS_MENU_LOGO_RIGHT - PSX_WS_MENU_LOGO_LEFT,
    PSX_WS_MENU_LOGO_ROWS = PSX_WS_MENU_LOGO_BOTTOM - PSX_WS_MENU_LOGO_TOP,
    PSX_WS_MENU_LOGO_LEAST = 2500, /* of the 3767..3902 texels of its own colours the logo has there, as its streak glints */
    PSX_WS_MENU_KEPT_X = 512,      /* empty in the front end and in play: measured in both */
    PSX_WS_MENU_KEPT_PAGE = (PSX_WS_MENU_KEPT_X / 64) | (2 << 7), /* colours as they are, no palette */
    PSX_WS_MENU_HALO = 2,          /* the logo's shadow and outline are within two texels of its own colours in every scene */
    PSX_WS_MENU_TAIL_TOP = 29,     /* the three rows where the streak and its shadow run on past the letters, half clear, to the box's right edge */
    PSX_WS_MENU_TAIL_ROWS = 3,
};

/* Which half of the backdrop a textured rectangle is: 1 the left, 2 the right, 0 neither. */
static inline int psx_ws_menu_backdrop_half(int32_t x, int32_t y, int w, int h) {
    if (y != 0 || h != PSX_WS_MENU_HIGH) return 0;
    if (x == 0 && w == 256) return 1;
    return x == 256 && w == PSX_WS_MENU_WIDE - 256 ? 2 : 0;
}

static inline int psx_ws_menu_texel(const uint16_t *picture, int stride, int x, int y) {
    const uint16_t word = picture[y * stride + (x >> 1)];
    return (x & 1) ? word >> 8 : word & 0xFF;
}

static inline void psx_ws_menu_set_texel(uint16_t *picture, int stride, int x, int y, int index) {
    uint16_t *word = &picture[y * stride + (x >> 1)];
    *word = (x & 1) ? (uint16_t)((*word & 0x00FF) | (index << 8)) : (uint16_t)((*word & 0xFF00) | index);
}

/* The logo is red, white and grey. The scenes behind it are green and dark in each of their three palettes: no texel of twelve scenes outside the logo's box passes this. */
static inline int psx_ws_menu_logo_colour(uint16_t colour) {
    const int red = colour & 31, green = (colour >> 5) & 31, blue = (colour >> 10) & 31;
    return red > green || (red == green && blue >= green - 1 && red + green + blue > 12);
}

/* Whether the picture is the front end's: the logo stands where it always stands. */
static inline int psx_ws_menu_has_logo(const uint16_t *picture, int stride, const uint16_t *palette) {
    int count = 0;
    for (int y = PSX_WS_MENU_LOGO_TOP; y < PSX_WS_MENU_LOGO_BOTTOM; ++y)
        for (int x = PSX_WS_MENU_LOGO_LEFT; x < PSX_WS_MENU_LOGO_RIGHT; ++x)
            count += psx_ws_menu_logo_colour(palette[psx_ws_menu_texel(picture, stride, x, y)]);
    return count >= PSX_WS_MENU_LOGO_LEAST;
}

/* Marks the texels the logo is lifted from in its box, PSX_WS_MENU_LOGO_WIDE a row: 1 for its own colours, 2 for its shadow and the streak's tail, 3 for the rest of the halo, lifted and not kept. */
static inline void psx_ws_menu_logo_mask(const uint16_t *picture, int stride, const uint16_t *palette, uint8_t *mask) {
    for (int y = 0; y < PSX_WS_MENU_LOGO_ROWS; ++y)
        for (int x = 0; x < PSX_WS_MENU_LOGO_WIDE; ++x)
            mask[y * PSX_WS_MENU_LOGO_WIDE + x] =
                (uint8_t)psx_ws_menu_logo_colour(palette[psx_ws_menu_texel(picture, stride, PSX_WS_MENU_LOGO_LEFT + x, PSX_WS_MENU_LOGO_TOP + y)]);
    for (int y = 0; y < PSX_WS_MENU_LOGO_ROWS; ++y)
        for (int x = 0; x < PSX_WS_MENU_LOGO_WIDE; ++x) {
            uint8_t *one = &mask[y * PSX_WS_MENU_LOGO_WIDE + x];
            for (int dy = -PSX_WS_MENU_HALO; dy <= PSX_WS_MENU_HALO && !*one; ++dy)
                for (int dx = -PSX_WS_MENU_HALO; dx <= PSX_WS_MENU_HALO; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= PSX_WS_MENU_LOGO_WIDE || ny >= PSX_WS_MENU_LOGO_ROWS) continue;
                    if (mask[ny * PSX_WS_MENU_LOGO_WIDE + nx] == 1) *one = 3;
                }
            if (*one == 3 && x > 0 && y > 0 && mask[(y - 1) * PSX_WS_MENU_LOGO_WIDE + x - 1] == 1) *one = 2; /* the shadow falls one down and one right: that takes 458 of its 559 texels and 32 of the scene's */
        }
    for (int y = PSX_WS_MENU_TAIL_TOP - PSX_WS_MENU_LOGO_TOP; y < PSX_WS_MENU_TAIL_TOP - PSX_WS_MENU_LOGO_TOP + PSX_WS_MENU_TAIL_ROWS; ++y) {
        int last = PSX_WS_MENU_LOGO_WIDE;
        for (int x = 0; x < PSX_WS_MENU_LOGO_WIDE; ++x)
            if (mask[y * PSX_WS_MENU_LOGO_WIDE + x] == 1) last = x;
        for (int x = last + 1; x < PSX_WS_MENU_LOGO_WIDE; ++x) mask[y * PSX_WS_MENU_LOGO_WIDE + x] = 2;
    }
}

/* The logo alone, a word a texel in its own colours: nothing where it is not kept, which a textured rectangle then leaves undrawn. Scene kept round the letters showed as a fringe. */
static inline void psx_ws_menu_keep_logo(const uint16_t *picture, int stride, const uint16_t *palette, const uint8_t *mask, uint16_t *kept) {
    for (int y = 0; y < PSX_WS_MENU_LOGO_ROWS; ++y)
        for (int x = 0; x < PSX_WS_MENU_LOGO_WIDE; ++x) {
            const uint16_t colour = palette[psx_ws_menu_texel(picture, stride, PSX_WS_MENU_LOGO_LEFT + x, PSX_WS_MENU_LOGO_TOP + y)];
            const int mark = mask[y * PSX_WS_MENU_LOGO_WIDE + x];
            kept[y * PSX_WS_MENU_LOGO_WIDE + x] = mark != 1 && mark != 2 ? 0 : colour ? colour : 0x8000; /* black is drawn only with its top bit set */
        }
}

/* Where the kept logo is drawn: its columns squashed about the screen's centre. *right is exclusive. */
static inline void psx_ws_menu_logo_span(int32_t centre, int32_t numerator, int32_t denominator, int32_t *left, int32_t *right) {
    *left = psx_ws_squash_about(PSX_WS_MENU_LOGO_LEFT, centre, numerator, denominator);
    *right = psx_ws_squash_about(PSX_WS_MENU_LOGO_RIGHT, centre, numerator, denominator);
}

/* Whether the logo is lifted from a texel of the picture. */
static inline int psx_ws_menu_lifted(const uint8_t *mask, int x, int y) {
    return x >= PSX_WS_MENU_LOGO_LEFT && x < PSX_WS_MENU_LOGO_RIGHT && y >= PSX_WS_MENU_LOGO_TOP && y < PSX_WS_MENU_LOGO_BOTTOM &&
           mask[(y - PSX_WS_MENU_LOGO_TOP) * PSX_WS_MENU_LOGO_WIDE + (x - PSX_WS_MENU_LOGO_LEFT)];
}

typedef char psx_ws_menu_scene_below[PSX_WS_MENU_LOGO_BOTTOM + PSX_WS_MENU_LOGO_ROWS <= PSX_WS_MENU_HIGH ? 1 : -1]; /* so the way down always has a texel to mirror */

/* Fills what the logo leaves with the scene beside it, or the stretched logo shows behind the kept one. A texel takes the scene texel as far past the end of its run as it is inside it, */
/* in the nearest of four directions that has one. A blend of the rows above and below was tried: it smeared on light scenes and greyed where dark met light. */
static inline void psx_ws_menu_paint(uint16_t *picture, int stride, const uint8_t *mask) {
    static const int8_t across[4] = {0, 0, -1, 1}, down[4] = {-1, 1, 0, 0};
    for (int y = PSX_WS_MENU_LOGO_TOP; y < PSX_WS_MENU_LOGO_BOTTOM; ++y)
        for (int x = PSX_WS_MENU_LOGO_LEFT; x < PSX_WS_MENU_LOGO_RIGHT; ++x) {
            if (!psx_ws_menu_lifted(mask, x, y)) continue;
            uint32_t draw = (uint32_t)x * 0x9E3779B1u + (uint32_t)y * 0x85EBCA77u; /* which of two ends as near as each other. Mixed well: a weaker one striped a row of ties */
            draw = (draw ^ (draw >> 15)) * 0x2C1B3C6Du;
            draw ^= draw >> 12;
            int from_x = x, from_y = y, least = INT32_MAX;
            for (int way = 0; way < 4; ++way) {
                int distance = 1, end_x = x + across[way], end_y = y + down[way];
                while (psx_ws_menu_lifted(mask, end_x, end_y)) {
                    ++distance;
                    end_x += across[way];
                    end_y += down[way];
                }
                const int mirror_x = end_x + across[way] * (distance - 1), mirror_y = end_y + down[way] * (distance - 1);
                if (mirror_x < 0 || mirror_x >= PSX_WS_MENU_WIDE || mirror_y < 0 || psx_ws_menu_lifted(mask, mirror_x, mirror_y)) continue; /* a speck of scene inside a letter has nothing behind it to mirror */
                if (distance > least || (distance == least && !(draw & 1))) continue;
                least = distance;
                from_x = mirror_x;
                from_y = mirror_y;
            }
            psx_ws_menu_set_texel(picture, stride, x, y, psx_ws_menu_texel(picture, stride, from_x, from_y));
        }
}

/* On a front-end frame a rectangle keeps its shape on the stretched screen: its columns are squashed about the centre. Returns its width, 1 at least. */
static inline int psx_ws_menu_squash_rect(int32_t *x, int w, int32_t numerator, int32_t denominator) {
    if (w <= 0 || (*x <= 0 && *x + w >= PSX_WS_MENU_WIDE)) return w; /* one from side to side lies over the whole screen */
    const int32_t right = psx_ws_squash_about(*x + w, PSX_WS_MENU_WIDE / 2, numerator, denominator);
    *x = psx_ws_squash_about(*x, PSX_WS_MENU_WIDE / 2, numerator, denominator);
    return right > *x ? (int)(right - *x) : 1;
}

/* The same for a polygon's or a line's corners, rounded away from its own middle: it backs what is drawn over it and must show on both sides. */
static inline void psx_ws_menu_squash_corners(int32_t *xs, int count, int32_t numerator, int32_t denominator) {
    int32_t low = xs[0], high = xs[0];
    for (int i = 1; i < count; ++i) {
        if (xs[i] < low) low = xs[i];
        if (xs[i] > high) high = xs[i];
    }
    if (low <= 0 && high >= PSX_WS_MENU_WIDE) return;
    for (int i = 0; i < count; ++i) {
        const int32_t scaled = (xs[i] - PSX_WS_MENU_WIDE / 2) * numerator;
        const int32_t down = scaled >= 0 ? scaled / denominator : -((-scaled + denominator - 1) / denominator);
        const int32_t up = scaled >= 0 ? (scaled + denominator - 1) / denominator : -(-scaled / denominator);
        if (2 * xs[i] == low + high) xs[i] = psx_ws_squash_about(xs[i], PSX_WS_MENU_WIDE / 2, numerator, denominator);
        else xs[i] = PSX_WS_MENU_WIDE / 2 + (2 * xs[i] < low + high ? down : up);
    }
}

/* Gives a rectangle of video memory back word by word: a word that still is what was put there becomes what it was before, one written since stays. */
static inline int psx_ws_menu_return(uint16_t *memory, int stride, int w, int h, const uint16_t *put, const uint16_t *before) {
    int returned = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (memory[y * stride + x] != put[y * w + x]) continue;
            memory[y * stride + x] = before[y * w + x];
            ++returned;
        }
    return returned;
}

/* Whether a rectangle of video memory, in words, is the whole picture. */
static inline int psx_ws_menu_is_picture(int x, int y, int w, int h) {
    return x == PSX_WS_MENU_X && y == 0 && w == PSX_WS_MENU_WORDS && h == PSX_WS_MENU_HIGH;
}

/* Whether a rectangle of video memory, in words, reaches the picture or the kept logo. One that runs off video memory wraps round, so it may. */
static inline int psx_ws_menu_reaches(int x, int y, int w, int h) {
    if (x + w > 1024 || y + h > 512) return 1;
    if (x < PSX_WS_MENU_X + PSX_WS_MENU_WORDS && x + w > PSX_WS_MENU_X && y < PSX_WS_MENU_HIGH) return 1;
    return x < PSX_WS_MENU_KEPT_X + PSX_WS_MENU_LOGO_WIDE && x + w > PSX_WS_MENU_KEPT_X && y < PSX_WS_MENU_LOGO_ROWS;
}

#endif /* PSXRECOMP_GPU_WS_MENU_H */
