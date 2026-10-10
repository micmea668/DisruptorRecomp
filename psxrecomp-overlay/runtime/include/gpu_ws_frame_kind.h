#ifndef PSXRECOMP_GPU_WS_FRAME_KIND_H
#define PSXRECOMP_GPU_WS_FRAME_KIND_H

#include <stdint.h>

/* Which of a double-buffered game's two frames is a full-2D screen.
 *
 * A game that draws its world through the GTE projects vertices for every
 * gameplay frame before it sets that frame's drawing area, and none for a
 * map or a menu. So the count since the last drawing area says what the frame
 * about to be drawn is, without waiting, and the buffer it is drawn into
 * keeps that answer until it has been displayed.
 *
 * Two frames are in hand at once: the game builds one while display lists of
 * the one before are still being drawn. A question asked inside a list is
 * about the buffer drawn last, a question asked outside one is about the
 * frame being built. And a game culls before it projects, so one that knows
 * what it is about to build says so first. */
typedef struct {
    uint32_t x, y;
    int known, flat;
    int menu; /* a front-end screen whose backdrop fills a wide screen: flat, and shown wide all the same */
} PsxWsFrameBuffer;

typedef struct {
    PsxWsFrameBuffer buffers[2];
    int drawing;
    uint32_t projected;
    int told, told_flat;
    int in_list;
} PsxWsFrameKinds;

static inline void psx_ws_frame_kinds_project(PsxWsFrameKinds *kinds, uint32_t vertices) {
    kinds->projected += vertices;
}

/* The game begins to build a frame and says what it is. That stands over the
 * count until the frame's drawing area. */
static inline void psx_ws_frame_kinds_tell(PsxWsFrameKinds *kinds, int flat) {
    kinds->told = 1;
    kinds->told_flat = flat != 0;
}

static inline void psx_ws_frame_kinds_list(PsxWsFrameKinds *kinds, int inside) {
    kinds->in_list = inside != 0;
}

/* The game set a drawing area at (x, y): the frame it draws there is what the
 * game told, or else flat when fewer than `least` vertices were projected for
 * it. A third origin takes the place of the buffer that was not drawn last. */
static inline void psx_ws_frame_kinds_area(PsxWsFrameKinds *kinds, uint32_t x, uint32_t y, uint32_t least) {
    int slot = kinds->drawing ^ 1;
    for (int i = 0; i < 2; ++i)
        if (kinds->buffers[i].known && kinds->buffers[i].x == x && kinds->buffers[i].y == y) slot = i;
    kinds->buffers[slot].x = x;
    kinds->buffers[slot].y = y;
    kinds->buffers[slot].known = 1;
    kinds->buffers[slot].flat = kinds->told ? kinds->told_flat : kinds->projected < least;
    kinds->buffers[slot].menu = 0;
    kinds->drawing = slot;
    kinds->projected = 0;
    kinds->told = 0;
}

/* The game uploaded or copied a picture to (x, y): a buffer it lands in shows that from now on, a flat frame and no front-end one. */
static inline void psx_ws_frame_kinds_pictured(PsxWsFrameKinds *kinds, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                               uint32_t wide, uint32_t tall) {
    for (int i = 0; i < 2; ++i) {
        PsxWsFrameBuffer *buffer = &kinds->buffers[i];
        if (x >= buffer->x + wide || x + w <= buffer->x || y >= buffer->y + tall || y + h <= buffer->y) continue;
        buffer->flat = 1;
        buffer->menu = 0;
    }
}

/* The frame a question is about: 1 flat, 0 not, -1 not known yet. Outside a
 * display list that is the frame being built, once the game has told its kind
 * or projected `least` vertices for it. Otherwise it is the buffer drawn last. */
static inline int psx_ws_frame_kinds_drawing(const PsxWsFrameKinds *kinds, uint32_t least) {
    if (!kinds->in_list) {
        if (kinds->told) return kinds->told_flat;
        if (kinds->projected >= least) return 0;
    }
    return kinds->buffers[kinds->drawing].known ? kinds->buffers[kinds->drawing].flat : -1;
}

/* The frame in the buffer displayed from (x, y): 1 flat, 0 not, -1 not known. */
static inline int psx_ws_frame_kinds_displayed(const PsxWsFrameKinds *kinds, uint32_t x, uint32_t y) {
    for (int i = 0; i < 2; ++i)
        if (kinds->buffers[i].known && kinds->buffers[i].x == x && kinds->buffers[i].y == y)
            return kinds->buffers[i].flat;
    return -1;
}

/* The frame being drawn has drawn the front end's backdrop. */
static inline void psx_ws_frame_kinds_menu_begin(PsxWsFrameKinds *kinds) {
    if (kinds->buffers[kinds->drawing].known) kinds->buffers[kinds->drawing].menu = 1;
}

static inline int psx_ws_frame_kinds_menu(const PsxWsFrameKinds *kinds) {
    return kinds->buffers[kinds->drawing].known && kinds->buffers[kinds->drawing].menu;
}

static inline int psx_ws_frame_kinds_displayed_menu(const PsxWsFrameKinds *kinds, uint32_t x, uint32_t y) {
    for (int i = 0; i < 2; ++i)
        if (kinds->buffers[i].known && kinds->buffers[i].x == x && kinds->buffers[i].y == y) return kinds->buffers[i].menu;
    return 0;
}

#endif /* PSXRECOMP_GPU_WS_FRAME_KIND_H */
