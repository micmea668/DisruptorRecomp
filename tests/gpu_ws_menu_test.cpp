#include "gpu_ws_menu.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

namespace {

constexpr int kStride = PSX_WS_MENU_WORDS;
constexpr int kGreen = 2, kRed = 1, kBlack = 3, kDim = 4;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

constexpr std::uint16_t colour(int red, int green, int blue) { return static_cast<std::uint16_t>(red | (green << 5) | (blue << 10)); }

/* A palette with a red, a green, a black and a dim grey among greens. */
std::vector<std::uint16_t> palette() {
    std::vector<std::uint16_t> colours(256, colour(3, 6, 3));
    colours[kRed] = colour(28, 4, 4);
    colours[kGreen] = colour(5, 9, 5);
    colours[kBlack] = 0;
    colours[kDim] = colour(6, 7, 6);
    return colours;
}

std::vector<std::uint16_t> picture_of(int index) { return std::vector<std::uint16_t>(kStride * PSX_WS_MENU_HIGH, static_cast<std::uint16_t>(index | (index << 8))); }

/* Box coordinates to the picture's. */
void put(std::vector<std::uint16_t> &picture, int x, int y, int index) {
    psx_ws_menu_set_texel(picture.data(), kStride, PSX_WS_MENU_LOGO_LEFT + x, PSX_WS_MENU_LOGO_TOP + y, index);
}

int at(const std::vector<std::uint16_t> &picture, int x, int y) {
    return psx_ws_menu_texel(picture.data(), kStride, PSX_WS_MENU_LOGO_LEFT + x, PSX_WS_MENU_LOGO_TOP + y);
}

void test_the_backdrop_is_two_rectangles() {
    expect(psx_ws_menu_backdrop_half(0, 0, 256, 240) == 1 && psx_ws_menu_backdrop_half(256, 0, 64, 240) == 2, "the left half is 256 wide, the right the other 64");
    expect(psx_ws_menu_backdrop_half(0, 1, 256, 240) == 0 && psx_ws_menu_backdrop_half(0, 0, 256, 239) == 0 && psx_ws_menu_backdrop_half(0, 0, 255, 240) == 0 &&
               psx_ws_menu_backdrop_half(256, 0, 63, 240) == 0 && psx_ws_menu_backdrop_half(64, 0, 256, 240) == 0 && psx_ws_menu_backdrop_half(256, 0, 256, 240) == 0,
           "a rectangle elsewhere or of another size is no half of it");
}

void test_a_word_holds_two_texels() {
    std::vector<std::uint16_t> picture(kStride * 2, 0x2211);
    expect(psx_ws_menu_texel(picture.data(), kStride, 0, 0) == 0x11 && psx_ws_menu_texel(picture.data(), kStride, 1, 0) == 0x22, "the low byte is the left texel");
    psx_ws_menu_set_texel(picture.data(), kStride, 4, 1, 0xAB);
    psx_ws_menu_set_texel(picture.data(), kStride, 7, 1, 0xCD);
    expect(picture[kStride + 2] == 0x22AB && picture[kStride + 3] == 0xCD11 && picture[kStride + 1] == 0x2211, "a texel is set without its neighbour");
}

void test_the_logo_is_told_by_its_colours() {
    expect(psx_ws_menu_logo_colour(colour(28, 4, 4)) && psx_ws_menu_logo_colour(colour(31, 31, 31)) && psx_ws_menu_logo_colour(colour(10, 10, 10)) &&
               psx_ws_menu_logo_colour(colour(3, 1, 1)) && psx_ws_menu_logo_colour(colour(10, 10, 9)),
           "red, white, grey and dark red are the logo's");
    expect(!psx_ws_menu_logo_colour(colour(5, 6, 4)) && !psx_ws_menu_logo_colour(colour(15, 19, 14)) && !psx_ws_menu_logo_colour(colour(4, 4, 4)) &&
               !psx_ws_menu_logo_colour(0) && !psx_ws_menu_logo_colour(colour(10, 10, 8)),
           "green, dark, and a grey gone yellow are the scene's");

    const std::vector<std::uint16_t> colours = palette();
    std::vector<std::uint16_t> picture = picture_of(kGreen);
    expect(!psx_ws_menu_has_logo(picture.data(), kStride, colours.data()), "a picture without red where the logo stands is not the front end's");
    for (int count = 0; count < PSX_WS_MENU_LOGO_LEAST - 1; ++count) put(picture, count % PSX_WS_MENU_LOGO_WIDE, count / PSX_WS_MENU_LOGO_WIDE, kRed);
    expect(!psx_ws_menu_has_logo(picture.data(), kStride, colours.data()), "one texel short of the least is not it either");
    put(picture, (PSX_WS_MENU_LOGO_LEAST - 1) % PSX_WS_MENU_LOGO_WIDE, (PSX_WS_MENU_LOGO_LEAST - 1) / PSX_WS_MENU_LOGO_WIDE, kRed);
    expect(psx_ws_menu_has_logo(picture.data(), kStride, colours.data()), "the least is");
    std::vector<std::uint16_t> elsewhere = picture_of(kGreen);
    for (int y = 60; y < 120; ++y)
        for (int x = 0; x < PSX_WS_MENU_WIDE; ++x) psx_ws_menu_set_texel(elsewhere.data(), kStride, x, y, kRed);
    expect(!psx_ws_menu_has_logo(elsewhere.data(), kStride, colours.data()), "red below the logo's rows does not count");
}

void test_the_logo_is_lifted_with_what_is_beside_it() {
    const std::vector<std::uint16_t> colours = palette();
    std::vector<std::uint16_t> picture = picture_of(kGreen);
    std::vector<std::uint8_t> mask(PSX_WS_MENU_LOGO_ROWS * PSX_WS_MENU_LOGO_WIDE, 9);
    const auto marked = [&](int x, int y) { return mask[y * PSX_WS_MENU_LOGO_WIDE + x]; };
    put(picture, 20, 10, kRed);
    put(picture, 21, 11, kBlack);
    put(picture, 20 - PSX_WS_MENU_HALO, 10, kDim);
    put(picture, 20 + PSX_WS_MENU_HALO + 1, 10, kBlack);
    put(picture, 0, 0, kRed);
    put(picture, PSX_WS_MENU_LOGO_WIDE - 1, PSX_WS_MENU_LOGO_ROWS - 1, kRed);
    psx_ws_menu_logo_mask(picture.data(), kStride, colours.data(), mask.data());
    expect(marked(20, 10) == 1 && marked(0, 0) == 1 && marked(PSX_WS_MENU_LOGO_WIDE - 1, PSX_WS_MENU_LOGO_ROWS - 1) == 1, "a texel of the logo's colours is the logo, in the box's corners too");
    expect(marked(21, 11) == 2 && marked(1, 1) == 2, "the texel one down and one right of it is its shadow, whatever its colour");
    expect(marked(20 + PSX_WS_MENU_HALO, 10 + PSX_WS_MENU_HALO) == 3 && marked(20 - PSX_WS_MENU_HALO, 10) == 3 && marked(20, 10 - PSX_WS_MENU_HALO) == 3 &&
               marked(20 + PSX_WS_MENU_HALO, 10 - PSX_WS_MENU_HALO) == 3 && marked(21, 10) == 3 && marked(20, 11) == 3 && marked(19, 9) == 3 && marked(19, 11) == 3 && marked(21, 9) == 3 &&
               marked(PSX_WS_MENU_HALO, PSX_WS_MENU_HALO) == 3 && marked(PSX_WS_MENU_LOGO_WIDE - 1 - PSX_WS_MENU_HALO, PSX_WS_MENU_LOGO_ROWS - 1) == 3,
           "the rest within the halo is lifted with it and is not its shadow: its outline is not told by a colour");
    expect(marked(20 + PSX_WS_MENU_HALO + 1, 10) == 0 && marked(20, 10 + PSX_WS_MENU_HALO + 1) == 0 && marked(20 - PSX_WS_MENU_HALO - 1, 10) == 0 &&
               marked(20 + PSX_WS_MENU_HALO + 1, 10 + PSX_WS_MENU_HALO) == 0 && marked(100, 20) == 0 && marked(22, 12) == 3,
           "one past the halo is scene, dark as it may be, and neither the halo nor the shadow is passed on");
    expect(std::count(mask.begin(), mask.end(), std::uint8_t{9}) == 0, "every texel of the box is decided");

    std::vector<std::uint16_t> kept(PSX_WS_MENU_LOGO_ROWS * PSX_WS_MENU_LOGO_WIDE, 0x1234);
    psx_ws_menu_keep_logo(picture.data(), kStride, colours.data(), mask.data(), kept.data());
    expect(kept[10 * PSX_WS_MENU_LOGO_WIDE + 20] == colours[kRed] && kept[1 * PSX_WS_MENU_LOGO_WIDE + 1] == colours[kGreen], "the kept logo has its own colours and its shadow's");
    expect(kept[11 * PSX_WS_MENU_LOGO_WIDE + 21] == 0x8000, "its black is kept with the top bit, or a rectangle would not draw it");
    expect(kept[10 * PSX_WS_MENU_LOGO_WIDE + 20 - PSX_WS_MENU_HALO] == 0 && kept[10 * PSX_WS_MENU_LOGO_WIDE + 21] == 0 && kept[9 * PSX_WS_MENU_LOGO_WIDE + 19] == 0,
           "the rest of the halo is lifted and not kept: scene carried round the letters showed as a fringe");
    expect(kept[10 * PSX_WS_MENU_LOGO_WIDE + 20 + PSX_WS_MENU_HALO + 1] == 0 && kept[20 * PSX_WS_MENU_LOGO_WIDE + 100] == 0, "and nothing where the scene is");
}

void test_the_streak_is_lifted_to_the_end_of_its_tail() {
    const std::vector<std::uint16_t> colours = palette();
    std::vector<std::uint16_t> picture = picture_of(kGreen);
    std::vector<std::uint8_t> mask(PSX_WS_MENU_LOGO_ROWS * PSX_WS_MENU_LOGO_WIDE, 9);
    const auto marked = [&](int x, int y) { return mask[y * PSX_WS_MENU_LOGO_WIDE + x]; };
    const int first = PSX_WS_MENU_TAIL_TOP - PSX_WS_MENU_LOGO_TOP, last = first + PSX_WS_MENU_TAIL_ROWS - 1;
    put(picture, 100, first, kRed);
    put(picture, 150, first, kRed);
    put(picture, 60, last, kRed);
    put(picture, 150, first - 4, kRed);
    put(picture, 150, last + 4, kRed);
    put(picture, 180, first - 1, kRed);
    put(picture, 170, last + 1, kRed);
    psx_ws_menu_logo_mask(picture.data(), kStride, colours.data(), mask.data());
    bool whole_tail = true;
    for (int x = 151; x < PSX_WS_MENU_LOGO_WIDE; ++x) whole_tail = whole_tail && marked(x, first) == 2 && marked(x, last) == 2;
    expect(whole_tail, "in the streak's rows everything from the last texel of the logo's colours to the box's right edge goes with the logo, whatever else is lifted on the way");
    expect(marked(125, first) == 0 && marked(40, last) == 0, "nothing between two such texels or left of the first does");
    expect(marked(190, first - 4) == 0 && marked(190, last + 4) == 0 && marked(190, first - 1) == 0 && marked(190, last + 1) == 0, "nor in any other row");
    expect(marked(150, first) == 1 && marked(60, last) == 1, "and the logo's own texels stay its own");
    std::vector<std::uint16_t> kept(PSX_WS_MENU_LOGO_ROWS * PSX_WS_MENU_LOGO_WIDE, 0x1234);
    psx_ws_menu_keep_logo(picture.data(), kStride, colours.data(), mask.data(), kept.data());
    expect(kept[first * PSX_WS_MENU_LOGO_WIDE + 190] == colours[kGreen] && kept[(first - 4) * PSX_WS_MENU_LOGO_WIDE + 152] == 0, "the tail is kept with the logo, the halo elsewhere is not");

    std::vector<std::uint16_t> bare = picture_of(kGreen);
    put(bare, 150, first - 4, kRed);
    psx_ws_menu_logo_mask(bare.data(), kStride, colours.data(), mask.data());
    expect(marked(0, first) == 0 && marked(190, first) == 0 && marked(190, last) == 0, "a streak row with none of the logo's colours has no tail");
}

void test_the_kept_logo_is_squashed_about_the_centre() {
    expect(PSX_WS_MENU_KEPT_PAGE == 0x108, "the kept logo is read from the page at 512 as colours, not as palette entries");
    std::int32_t left = 0, right = 0;
    psx_ws_menu_logo_span(160, 3, 4, &left, &right);
    expect(left == 84 && right == 240, "at 16:9 the logo's 207 columns are drawn in 156, about the centre");
    psx_ws_menu_logo_span(160, 4, 7, &left, &right);
    expect(left == 102 && right == 221, "at 21:9 in 119");
    psx_ws_menu_logo_span(160, 1, 1, &left, &right);
    expect(left == PSX_WS_MENU_LOGO_LEFT && right == PSX_WS_MENU_LOGO_RIGHT, "and where they were when nothing is squashed");
}

/* Where the fill takes each texel from. One picture cannot name 76800 places in a byte, so three are filled alike: a column's low byte, its high bits, and a row. */
struct Filled {
    std::vector<std::uint16_t> low, high, row, before;
    std::pair<int, int> from(int x, int y) const {
        return {(at(low, x, y) | (at(high, x, y) << 8)) - PSX_WS_MENU_LOGO_LEFT, at(row, x, y) - PSX_WS_MENU_LOGO_TOP};
    }
};

Filled fill(const std::vector<std::uint8_t> &mask) {
    Filled filled;
    filled.low = filled.high = filled.row = std::vector<std::uint16_t>(kStride * PSX_WS_MENU_HIGH);
    for (int y = 0; y < PSX_WS_MENU_HIGH; ++y)
        for (int x = 0; x < PSX_WS_MENU_WIDE; ++x) {
            psx_ws_menu_set_texel(filled.low.data(), kStride, x, y, x & 0xFF);
            psx_ws_menu_set_texel(filled.high.data(), kStride, x, y, x >> 8);
            psx_ws_menu_set_texel(filled.row.data(), kStride, x, y, y);
        }
    filled.before = filled.low;
    psx_ws_menu_paint(filled.low.data(), kStride, mask.data());
    psx_ws_menu_paint(filled.high.data(), kStride, mask.data());
    psx_ws_menu_paint(filled.row.data(), kStride, mask.data());
    return filled;
}

void test_what_the_logo_leaves_is_the_scene_mirrored() {
    std::vector<std::uint8_t> mask(PSX_WS_MENU_LOGO_ROWS * PSX_WS_MENU_LOGO_WIDE, 0);
    const auto lift = [&](int left, int top, int right, int bottom) {
        for (int y = top; y <= bottom; ++y)
            for (int x = left; x <= right; ++x) mask[y * PSX_WS_MENU_LOGO_WIDE + x] = static_cast<std::uint8_t>(1 + (x + y) % 3);
    };
    lift(40, 10, 99, 13);
    lift(40, 20, 99, 22);
    lift(120, 5, 139, 24);
    mask[14 * PSX_WS_MENU_LOGO_WIDE + 130] = 0;
    lift(160, 0, 199, PSX_WS_MENU_LOGO_ROWS - 1);
    const Filled filled = fill(mask);
    const auto from = [&](int x, int y) { return filled.from(x, y); };
    const auto spot = [](int x, int y) { return std::pair<int, int>{x, y}; };

    expect(from(50, 10) == spot(50, 9) && from(50, 13) == spot(50, 14), "a texel at the end of its run takes the scene texel next to it");
    expect(from(50, 11) == spot(50, 8) && from(50, 12) == spot(50, 15), "one further in takes the one as far out, from the nearer end");
    expect(from(40, 11) == spot(39, 11) && from(99, 12) == spot(100, 12), "sideways where that end is the nearest");
    std::vector<int> below;
    bool either = true;
    for (int x = 45; x < 95; ++x) {
        either = either && (from(x, 21) == spot(x, 18) || from(x, 21) == spot(x, 24));
        below.push_back(from(x, 21) == spot(x, 24));
    }
    const int from_below = static_cast<int>(std::count(below.begin(), below.end(), 1));
    expect(either && from_below >= 15 && from_below <= 35, "between two ends as near as each other it takes either, about as often");
    bool striped = false;
    for (std::size_t period = 1; period <= 8; ++period) {
        bool repeats = true;
        for (std::size_t index = 0; index + period < below.size(); ++index) repeats = repeats && below[index] == below[index + period];
        striped = striped || repeats;
    }
    expect(!striped, "and in no stripes: the draw does not repeat every few columns");

    expect(from(130, 13) == spot(130, 14), "a speck of scene inside a letter is the scene for the texel beside it");
    expect(from(130, 12) == spot(130, -3) && from(130, 16) == spot(130, 33), "but one further off has nothing to mirror in it and goes to the next nearest end");
    expect(from(180, 2) == spot(180, -3), "the scene above the logo's rows is mirrored too");
    expect(from(180, 17) == spot(180, 54), "but not from above the picture: then the scene below is");

    bool rest_untouched = true;
    for (int y = 0; y < PSX_WS_MENU_HIGH; ++y)
        for (int x = 0; x < PSX_WS_MENU_WIDE; ++x)
            if (!psx_ws_menu_lifted(mask.data(), x, y) && psx_ws_menu_texel(filled.low.data(), kStride, x, y) != psx_ws_menu_texel(filled.before.data(), kStride, x, y)) rest_untouched = false;
    expect(rest_untouched, "no texel of the scene is touched");
    expect(psx_ws_menu_lifted(mask.data(), PSX_WS_MENU_LOGO_LEFT + 40, PSX_WS_MENU_LOGO_TOP + 10) && !psx_ws_menu_lifted(mask.data(), PSX_WS_MENU_LOGO_LEFT + 39, PSX_WS_MENU_LOGO_TOP + 10) &&
               !psx_ws_menu_lifted(mask.data(), PSX_WS_MENU_LOGO_LEFT - 1, PSX_WS_MENU_LOGO_TOP) && !psx_ws_menu_lifted(mask.data(), PSX_WS_MENU_LOGO_RIGHT, PSX_WS_MENU_LOGO_TOP) &&
               !psx_ws_menu_lifted(mask.data(), PSX_WS_MENU_LOGO_LEFT + 160, PSX_WS_MENU_LOGO_TOP - 1) && !psx_ws_menu_lifted(mask.data(), PSX_WS_MENU_LOGO_LEFT + 160, PSX_WS_MENU_LOGO_BOTTOM),
           "a texel is lifted where the mask says so, and never outside the logo's box");
}

void test_what_was_put_is_given_back_word_by_word() {
    const std::vector<std::uint16_t> put = {11, 12, 13, 14, 15, 16}, before = {1, 2, 3, 4, 5, 6};
    std::vector<std::uint16_t> memory = {11, 12, 13, 77, 78, 14, 99, 16, 79, 80};
    expect(psx_ws_menu_return(memory.data(), 5, 3, 2, put.data(), before.data()) == 5, "every word still as it was put is counted");
    expect(memory == std::vector<std::uint16_t>({1, 2, 3, 77, 78, 4, 99, 6, 79, 80}),
           "it goes back to what it was, a word written since stays, and nothing beside the rectangle is touched");
    expect(psx_ws_menu_return(memory.data(), 5, 3, 2, put.data(), before.data()) == 0, "and nothing is given back twice");
}

void test_the_picture_and_the_kept_logo_are_reached() {
    expect(psx_ws_menu_reaches(PSX_WS_MENU_X, 0, PSX_WS_MENU_WORDS, PSX_WS_MENU_HIGH) && psx_ws_menu_reaches(PSX_WS_MENU_X + PSX_WS_MENU_WORDS - 1, PSX_WS_MENU_HIGH - 1, 1, 1) &&
               psx_ws_menu_reaches(0, 0, PSX_WS_MENU_X + 1, 1),
           "the picture is reached by its own rectangle, by its last word and from the left");
    expect(!psx_ws_menu_reaches(0, 0, PSX_WS_MENU_X, 480) && !psx_ws_menu_reaches(PSX_WS_MENU_X, PSX_WS_MENU_HIGH, PSX_WS_MENU_WORDS, 16) &&
               !psx_ws_menu_reaches(PSX_WS_MENU_X + PSX_WS_MENU_WORDS, 0, PSX_WS_MENU_KEPT_X - PSX_WS_MENU_X - PSX_WS_MENU_WORDS, PSX_WS_MENU_HIGH),
           "the frame buffers beside it, the rows under it and the gap before the kept logo are not it");
    expect(psx_ws_menu_reaches(PSX_WS_MENU_KEPT_X, 0, 1, 1) && psx_ws_menu_reaches(PSX_WS_MENU_KEPT_X + PSX_WS_MENU_LOGO_WIDE - 1, PSX_WS_MENU_LOGO_ROWS - 1, 1, 1) &&
               !psx_ws_menu_reaches(PSX_WS_MENU_KEPT_X + PSX_WS_MENU_LOGO_WIDE, 0, 64, 64) && !psx_ws_menu_reaches(PSX_WS_MENU_KEPT_X, PSX_WS_MENU_LOGO_ROWS, PSX_WS_MENU_LOGO_WIDE, 64),
           "the kept logo is reached in its own rows and columns only");
    expect(psx_ws_menu_is_picture(PSX_WS_MENU_X, 0, PSX_WS_MENU_WORDS, PSX_WS_MENU_HIGH) && !psx_ws_menu_is_picture(PSX_WS_MENU_X, 0, PSX_WS_MENU_WORDS, PSX_WS_MENU_HIGH - 1) &&
               !psx_ws_menu_is_picture(PSX_WS_MENU_X, 1, PSX_WS_MENU_WORDS, PSX_WS_MENU_HIGH) && !psx_ws_menu_is_picture(PSX_WS_MENU_X + 1, 0, PSX_WS_MENU_WORDS, PSX_WS_MENU_HIGH) &&
               !psx_ws_menu_is_picture(PSX_WS_MENU_X, 0, PSX_WS_MENU_WORDS - 1, PSX_WS_MENU_HIGH),
           "the whole picture is its own rectangle and no other");
    expect(psx_ws_menu_reaches(1000, 300, 100, 8) && psx_ws_menu_reaches(900, 500, 8, 40) && !psx_ws_menu_reaches(1000, 300, 24, 8) && !psx_ws_menu_reaches(900, 500, 8, 12),
           "and a rectangle that runs off video memory wraps round, so it may reach either");
}

void test_a_rectangle_keeps_its_shape_on_the_stretched_screen() {
    std::int32_t x = 110;
    expect(psx_ws_menu_squash_rect(&x, 24, 3, 4) == 18 && x == 122, "a rectangle is squashed about the centre, edge by edge");
    x = 110;
    expect(psx_ws_menu_squash_rect(&x, 24, 4, 7) == 14 && x == 131, "by the screen's own ratio");
    x = 0;
    expect(psx_ws_menu_squash_rect(&x, PSX_WS_MENU_WIDE, 3, 4) == PSX_WS_MENU_WIDE && x == 0, "one from side to side lies over the whole screen and is left");
    x = -8;
    expect(psx_ws_menu_squash_rect(&x, PSX_WS_MENU_WIDE + 16, 3, 4) == PSX_WS_MENU_WIDE + 16 && x == -8, "and so is one wider than the screen");
    x = 0;
    expect(psx_ws_menu_squash_rect(&x, PSX_WS_MENU_WIDE - 1, 3, 4) == 239 && x == 40, "one that stops short of the right edge is not");
    x = 1;
    expect(psx_ws_menu_squash_rect(&x, PSX_WS_MENU_WIDE, 3, 4) == 240 && x == 41, "nor one that starts after the left");
    x = 101;
    expect(psx_ws_menu_squash_rect(&x, 1, 3, 4) == 1 && x == 116, "a dot stays a dot where its edges fall on one column");
    x = 101;
    expect(psx_ws_menu_squash_rect(&x, 0, 3, 4) == 0 && x == 101, "and nothing stays nothing");
}

void test_a_polygon_is_squashed_away_from_its_own_middle() {
    std::int32_t frame[4] = {109, 135, 109, 135};
    psx_ws_menu_squash_corners(frame, 4, 3, 4);
    expect(frame[0] == 121 && frame[1] == 142 && frame[2] == 121 && frame[3] == 142, "a frame behind a picture squashed to 122..140 shows on both sides of it");
    std::int32_t wider[4] = {109, 135, 109, 135};
    psx_ws_menu_squash_corners(wider, 4, 4, 7);
    expect(wider[0] == 130 && wider[1] == 146, "and at another ratio, behind 131..145");
    std::int32_t right[3] = {201, 221, 211};
    psx_ws_menu_squash_corners(right, 3, 3, 4);
    expect(right[0] == 190 && right[1] == 206 && right[2] == 198, "right of the centre too, and a corner at its own middle goes to the nearest column");
    std::int32_t across[4] = {0, PSX_WS_MENU_WIDE, 0, PSX_WS_MENU_WIDE}, beyond[2] = {-4, PSX_WS_MENU_WIDE + 10}, short_of[2] = {0, PSX_WS_MENU_WIDE - 1};
    psx_ws_menu_squash_corners(across, 4, 3, 4);
    psx_ws_menu_squash_corners(beyond, 2, 3, 4);
    psx_ws_menu_squash_corners(short_of, 2, 3, 4);
    expect(across[0] == 0 && across[1] == PSX_WS_MENU_WIDE && beyond[0] == -4 && beyond[1] == PSX_WS_MENU_WIDE + 10, "one from side to side lies over the whole screen and is left");
    expect(short_of[0] == 40 && short_of[1] == 280, "one that stops short of an edge is not");
    std::int32_t alone[1] = {110};
    psx_ws_menu_squash_corners(alone, 1, 3, 4);
    expect(alone[0] == 122, "a single corner goes to the nearest column");
}

}  // namespace

int main() {
    test_the_backdrop_is_two_rectangles();
    test_a_word_holds_two_texels();
    test_the_logo_is_told_by_its_colours();
    test_the_logo_is_lifted_with_what_is_beside_it();
    test_the_streak_is_lifted_to_the_end_of_its_tail();
    test_the_kept_logo_is_squashed_about_the_centre();
    test_what_the_logo_leaves_is_the_scene_mirrored();
    test_what_was_put_is_given_back_word_by_word();
    test_the_picture_and_the_kept_logo_are_reached();
    test_a_rectangle_keeps_its_shape_on_the_stretched_screen();
    test_a_polygon_is_squashed_away_from_its_own_middle();
    if (g_failures) return 1;
    std::cout << "GPU widescreen front-end backdrop tests passed\n";
    return 0;
}
