#include "gpu_ws_frame_kind.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

constexpr std::uint32_t kLeast = 3;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

/* One frame of the game: what it projects, then its drawing area. */
void frame(PsxWsFrameKinds &kinds, std::uint32_t vertices, std::uint32_t y) {
    if (vertices) psx_ws_frame_kinds_project(&kinds, vertices);
    psx_ws_frame_kinds_area(&kinds, 0, y, kLeast);
}

void test_nothing_is_known_at_the_start() {
    PsxWsFrameKinds kinds{};
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == -1 && psx_ws_frame_kinds_displayed(&kinds, 0, 0) == -1,
           "before the first drawing area neither frame is known");
    psx_ws_frame_kinds_project(&kinds, 1500);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 0, "a frame being built with a world is not flat even then");
    psx_ws_frame_kinds_area(&kinds, 0, 240, kLeast);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 0 && psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 0,
           "the first drawing area makes its buffer known");
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 0) == -1 && psx_ws_frame_kinds_displayed(&kinds, 320, 240) == -1,
           "and no other");
}

void test_the_map_opens_and_closes() {
    PsxWsFrameKinds kinds{};
    frame(kinds, 1500, 240);
    frame(kinds, 1200, 0);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 0 && psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 0,
           "in play the frame being drawn and the frame on display both hold a world");

    frame(kinds, 0, 240);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 1, "the first frame of the map is flat as soon as its drawing area is set");
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 0) == 0, "while the frame on display is still the world drawn before it");
    frame(kinds, 0, 0);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 1 && psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 1,
           "one frame later the map is drawn and displayed");

    psx_ws_frame_kinds_project(&kinds, 2);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 1, "a vertex or two do not make a world");
    psx_ws_frame_kinds_project(&kinds, 1);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 0, "the frame being built is a world's from its third vertex on");
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 1 && psx_ws_frame_kinds_displayed(&kinds, 0, 0) == 1,
           "and both buffers still hold the map");
    psx_ws_frame_kinds_project(&kinds, 900);
    psx_ws_frame_kinds_area(&kinds, 0, 240, kLeast);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 0 && psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 0,
           "its drawing area makes that buffer a world's");
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 0) == 1, "while the frame on display is still the map");
    frame(kinds, 900, 0);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 0) == 0 && psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 0, "and then the world is back in both");
}

void test_the_count_is_per_drawing_area() {
    PsxWsFrameKinds kinds{};
    frame(kinds, kLeast, 0);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 0) == 0, "the least number of vertices makes a world");
    frame(kinds, kLeast - 1, 240);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 1, "one fewer does not");
    frame(kinds, 0, 0);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 0) == 1, "what was projected for one frame does not count for the next");
    psx_ws_frame_kinds_project(&kinds, 1);
    psx_ws_frame_kinds_project(&kinds, 1);
    psx_ws_frame_kinds_project(&kinds, 1);
    psx_ws_frame_kinds_area(&kinds, 0, 240, kLeast);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 0, "vertices projected one at a time add up");
}

void test_buffers_are_told_by_their_origin() {
    PsxWsFrameKinds kinds{};
    frame(kinds, 1500, 0);
    frame(kinds, 0, 240);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 0) == 0 && psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 1, "each origin keeps its own answer");
    expect(psx_ws_frame_kinds_displayed(&kinds, 240, 0) == -1 && psx_ws_frame_kinds_displayed(&kinds, 0, 241) == -1,
           "the two coordinates are not interchangeable and a near miss is another buffer");
    psx_ws_frame_kinds_project(&kinds, 1500);
    psx_ws_frame_kinds_area(&kinds, 320, 0, kLeast);
    expect(psx_ws_frame_kinds_displayed(&kinds, 320, 0) == 0 && psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 1,
           "a third origin takes the place of the buffer that was not drawn last");
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 0) == -1, "which is then forgotten");

    PsxWsFrameKinds beside{};
    psx_ws_frame_kinds_project(&beside, 1500);
    psx_ws_frame_kinds_area(&beside, 0, 0, kLeast);
    psx_ws_frame_kinds_area(&beside, 320, 0, kLeast);
    expect(psx_ws_frame_kinds_displayed(&beside, 0, 0) == 0 && psx_ws_frame_kinds_displayed(&beside, 320, 0) == 1,
           "two buffers side by side are two buffers");
    PsxWsFrameKinds stacked{};
    psx_ws_frame_kinds_project(&stacked, 1500);
    psx_ws_frame_kinds_area(&stacked, 0, 0, kLeast);
    psx_ws_frame_kinds_area(&stacked, 0, 240, kLeast);
    expect(psx_ws_frame_kinds_displayed(&stacked, 0, 0) == 0 && psx_ws_frame_kinds_displayed(&stacked, 0, 240) == 1,
           "and so are two one above the other");

    PsxWsFrameKinds single{};
    frame(single, 1500, 0);
    frame(single, 0, 0);
    expect(psx_ws_frame_kinds_displayed(&single, 0, 0) == 1 && psx_ws_frame_kinds_drawing(&single, kLeast) == 1,
           "a game with one buffer redraws the same one");
    frame(single, 1500, 240);
    expect(psx_ws_frame_kinds_displayed(&single, 0, 0) == 1 && psx_ws_frame_kinds_displayed(&single, 0, 240) == 0,
           "and its first other origin takes the free place");
}

/* The game says what it begins to build, as Disruptor does at its renderer's entry. */
void test_the_game_tells_the_frame_it_begins() {
    PsxWsFrameKinds kinds{};
    frame(kinds, 1500, 240);
    frame(kinds, 1200, 0);
    psx_ws_frame_kinds_tell(&kinds, 1);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 1, "a frame told flat is flat before anything is drawn of it");
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 0) == 0 && psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 0,
           "while both buffers still hold a world");
    psx_ws_frame_kinds_area(&kinds, 0, 240, kLeast);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 1, "and its drawing area keeps that");

    psx_ws_frame_kinds_tell(&kinds, 0);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 0, "a frame told a world's is one before its first vertex");
    psx_ws_frame_kinds_area(&kinds, 0, 0, kLeast);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 0) == 0, "and stays one though nothing was projected for it");

    psx_ws_frame_kinds_tell(&kinds, 1);
    psx_ws_frame_kinds_project(&kinds, 1500);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 1, "what the game told stands over the count");
    psx_ws_frame_kinds_area(&kinds, 0, 240, kLeast);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 1, "at the drawing area as well");

    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 1, "after it the buffer drawn last answers");
    frame(kinds, 1500, 0);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 0) == 0, "and a frame the game says nothing about is counted again");
    psx_ws_frame_kinds_tell(&kinds, 7);
    psx_ws_frame_kinds_area(&kinds, 0, 240, kLeast);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 1, "any other number than zero tells a flat frame");
}

/* The loading screen: the game copies its pictures into both buffers and sets no drawing area. */
void test_a_picture_put_into_a_buffer_is_what_it_shows() {
    PsxWsFrameKinds kinds{};
    psx_ws_frame_kinds_pictured(&kinds, 96, 124, 64, 12, 320, 240);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 0) == -1, "a picture makes no buffer known");

    frame(kinds, 1500, 0);
    frame(kinds, 1500, 240);
    psx_ws_frame_kinds_pictured(&kinds, 96, 364, 64, 12, 320, 240);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 1 && psx_ws_frame_kinds_displayed(&kinds, 0, 0) == 0,
           "a picture copied over a world makes that buffer's frame flat, and no other's");
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 1, "the buffer drawn last answers so too");
    frame(kinds, 1500, 240);
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 0, "until a world is drawn there again");

    PsxWsFrameKinds edges{};
    frame(edges, 1500, 0);
    frame(edges, 1500, 240);
    for (const auto &[x, y, w, h] : {std::array<std::uint32_t, 4>{320, 0, 160, 240}, {0, 480, 320, 32}, {768, 0, 128, 14}, {320, 239, 8, 8}}) {
        psx_ws_frame_kinds_pictured(&edges, x, y, w, h, 320, 240);
        expect(psx_ws_frame_kinds_displayed(&edges, 0, 0) == 0 && psx_ws_frame_kinds_displayed(&edges, 0, 240) == 0,
               "a picture beside the buffers or below them, a texture or the backdrop, changes neither");
    }
    psx_ws_frame_kinds_pictured(&edges, 319, 239, 1, 1, 320, 240);
    expect(psx_ws_frame_kinds_displayed(&edges, 0, 0) == 1 && psx_ws_frame_kinds_displayed(&edges, 0, 240) == 0, "the last word of a buffer is in it");
    frame(edges, 1500, 0);
    psx_ws_frame_kinds_pictured(&edges, 0, 236, 320, 4, 320, 240);
    expect(psx_ws_frame_kinds_displayed(&edges, 0, 0) == 1 && psx_ws_frame_kinds_displayed(&edges, 0, 240) == 0, "a picture that ends where the next buffer begins stays out of it");
    frame(edges, 1500, 0);
    psx_ws_frame_kinds_pictured(&edges, 0, 240, 1, 1, 320, 240);
    expect(psx_ws_frame_kinds_displayed(&edges, 0, 0) == 0 && psx_ws_frame_kinds_displayed(&edges, 0, 240) == 1, "and so is its first");
    frame(edges, 1500, 240);
    psx_ws_frame_kinds_pictured(&edges, 300, 230, 40, 20, 320, 240);
    expect(psx_ws_frame_kinds_displayed(&edges, 0, 0) == 1 && psx_ws_frame_kinds_displayed(&edges, 0, 240) == 1, "a picture across both lands in both");

    PsxWsFrameKinds beside{};
    psx_ws_frame_kinds_project(&beside, 1500);
    psx_ws_frame_kinds_area(&beside, 0, 0, kLeast);
    psx_ws_frame_kinds_project(&beside, 1500);
    psx_ws_frame_kinds_area(&beside, 256, 0, kLeast);
    psx_ws_frame_kinds_pictured(&beside, 250, 10, 6, 4, 256, 240);
    expect(psx_ws_frame_kinds_displayed(&beside, 0, 0) == 1 && psx_ws_frame_kinds_displayed(&beside, 256, 0) == 0,
           "a buffer is as wide and as tall as the display, whatever that is");
    psx_ws_frame_kinds_pictured(&beside, 250, 10, 8, 4, 256, 240);
    expect(psx_ws_frame_kinds_displayed(&beside, 256, 0) == 1, "and the next one begins where it ends");
}

/* Lists of the frame before are still being drawn while the next one is built. */
void test_a_list_asks_about_the_buffer_drawn_last() {
    PsxWsFrameKinds kinds{};
    frame(kinds, 1500, 240);
    frame(kinds, 1200, 0);
    psx_ws_frame_kinds_tell(&kinds, 1);
    psx_ws_frame_kinds_list(&kinds, 1);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 0, "a late list of the world is a world's though the map is being built");
    psx_ws_frame_kinds_list(&kinds, 0);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 1, "and after it the map is being built again");

    psx_ws_frame_kinds_list(&kinds, 5);
    psx_ws_frame_kinds_area(&kinds, 0, 240, kLeast);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 1, "a list that sets the drawing area draws the frame told before it");
    psx_ws_frame_kinds_list(&kinds, 0);
    frame(kinds, 0, 0);

    psx_ws_frame_kinds_project(&kinds, 1500);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 0, "outside a list the counted world is being built");
    psx_ws_frame_kinds_list(&kinds, 1);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 1, "a late list of the map is flat though a world has been projected");
    psx_ws_frame_kinds_tell(&kinds, 0);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 1, "and though the game has told one");
    psx_ws_frame_kinds_list(&kinds, 0);
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 0, "which it is, outside the list");

    PsxWsFrameKinds opening{};
    frame(opening, 1500, 240);
    frame(opening, 1200, 0);
    psx_ws_frame_kinds_tell(&opening, 1);
    psx_ws_frame_kinds_list(&opening, 1);
    expect(psx_ws_frame_kinds_drawing(&opening, kLeast) == 0,
           "inside the map's own first list, ahead of its drawing area, the answer is still the world's: nothing may be drawn there");
    psx_ws_frame_kinds_area(&opening, 0, 240, kLeast);
    expect(psx_ws_frame_kinds_drawing(&opening, kLeast) == 1, "and from its drawing area on it is the map's");
    psx_ws_frame_kinds_list(&opening, 0);
    psx_ws_frame_kinds_tell(&opening, 0);
    psx_ws_frame_kinds_list(&opening, 1);
    expect(psx_ws_frame_kinds_drawing(&opening, kLeast) == 1,
           "and the same the other way: ahead of the world's drawing area its first list is still answered for the map");
    psx_ws_frame_kinds_area(&opening, 0, 0, kLeast);
    expect(psx_ws_frame_kinds_drawing(&opening, kLeast) == 0, "until that drawing area");

    PsxWsFrameKinds unknown{};
    psx_ws_frame_kinds_tell(&unknown, 1);
    psx_ws_frame_kinds_list(&unknown, 1);
    expect(psx_ws_frame_kinds_drawing(&unknown, kLeast) == -1, "a list drawn before any drawing area is of no known frame");
}

void test_a_front_end_screen_is_flat_and_shown_wide() {
    PsxWsFrameKinds kinds{};
    psx_ws_frame_kinds_menu_begin(&kinds);
    expect(!psx_ws_frame_kinds_menu(&kinds) && !psx_ws_frame_kinds_displayed_menu(&kinds, 0, 0), "before any drawing area no frame is the front end's");
    frame(kinds, 0, 240);
    expect(!psx_ws_frame_kinds_menu(&kinds), "a frame is not the front end's until it draws the backdrop");
    psx_ws_frame_kinds_menu_begin(&kinds);
    expect(psx_ws_frame_kinds_menu(&kinds) && psx_ws_frame_kinds_displayed_menu(&kinds, 0, 240) && !psx_ws_frame_kinds_displayed_menu(&kinds, 0, 0),
           "the buffer it is drawn into is, and the other buffer is not");
    expect(psx_ws_frame_kinds_drawing(&kinds, kLeast) == 1 && psx_ws_frame_kinds_displayed(&kinds, 0, 240) == 1, "and it is a flat frame still");
    frame(kinds, 0, 0);
    expect(!psx_ws_frame_kinds_menu(&kinds) && psx_ws_frame_kinds_displayed_menu(&kinds, 0, 240), "the next frame starts as none, the one on display stays");
    frame(kinds, 1500, 240);
    expect(!psx_ws_frame_kinds_menu(&kinds) && !psx_ws_frame_kinds_displayed_menu(&kinds, 0, 240), "a world drawn into that buffer takes its place");
}

/* A new game goes from the front end to the loading screen with no drawing area between them. */
void test_a_picture_ends_a_front_end_frame() {
    PsxWsFrameKinds kinds{};
    frame(kinds, 0, 0);
    psx_ws_frame_kinds_menu_begin(&kinds);
    frame(kinds, 0, 240);
    psx_ws_frame_kinds_menu_begin(&kinds);
    psx_ws_frame_kinds_pictured(&kinds, 160, 124, 64, 12, 320, 240);
    expect(!psx_ws_frame_kinds_displayed_menu(&kinds, 0, 0) && psx_ws_frame_kinds_displayed_menu(&kinds, 0, 240),
           "a picture copied over a front-end frame ends it there, and leaves the other buffer's");
    expect(psx_ws_frame_kinds_displayed(&kinds, 0, 0) == 1 && psx_ws_frame_kinds_menu(&kinds), "which stays a flat frame, and the frame being drawn stays the front end's");
}

}  // namespace

int main() {
    test_nothing_is_known_at_the_start();
    test_the_map_opens_and_closes();
    test_the_count_is_per_drawing_area();
    test_buffers_are_told_by_their_origin();
    test_the_game_tells_the_frame_it_begins();
    test_a_picture_put_into_a_buffer_is_what_it_shows();
    test_a_list_asks_about_the_buffer_drawn_last();
    test_a_front_end_screen_is_flat_and_shown_wide();
    test_a_picture_ends_a_front_end_frame();
    if (g_failures) return 1;
    std::cout << "GPU widescreen frame kind tests passed\n";
    return 0;
}
