#include "../psxrecomp-overlay/runtime/include/host_borderless.h"

#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool same(PsxHostRect a, PsxHostRect b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }

/* What the window is told to become, or the untouched marker when it is left alone. */
PsxHostRect wanted_for(PsxHostRect window, PsxHostRect screen, int *moved) {
    PsxHostRect wanted = {7, 7, 7, 7};
    *moved = psx_borderless_wanted(window, screen, &wanted);
    return wanted;
}

void test_a_window_that_is_its_screen_goes_one_row_up() {
    int moved = 0;
    const PsxHostRect screen = {0, 0, 2560, 1440}, second = {2560, 200, 4480, 1280}, untouched = {7, 7, 7, 7};
    expect(same(wanted_for(screen, screen, &moved), {0, -1, 2560, 1440}) && moved == 1, "a window that fits its screen exactly is given one row more, above the screen");
    expect(same(wanted_for(second, second, &moved), {2560, 199, 4480, 1280}) && moved == 1, "on a screen that is not at the origin too");
    expect(same(wanted_for({0, -1, 2560, 1440}, screen, &moved), untouched) && moved == 0, "one that already has its row is left alone");
    expect(same(wanted_for({100, 100, 1380, 820}, screen, &moved), untouched) && moved == 0, "an ordinary window is left alone");
    expect(same(wanted_for({-32000, -32000, -31840, -31972}, screen, &moved), untouched) && moved == 0, "a minimised one is left alone");
    for (const PsxHostRect near : {PsxHostRect{1, 0, 2560, 1440}, PsxHostRect{0, 1, 2560, 1440}, PsxHostRect{0, 0, 2559, 1440}, PsxHostRect{0, 0, 2560, 1439}})
        expect(same(wanted_for(near, screen, &moved), untouched) && moved == 0, "a window one pixel short of the screen on any side is not the screen");
}

void test_the_rows_above_the_screen_are_hidden() {
    const PsxHostRect screen = {0, 0, 2560, 1440}, raised = {0, -1, 2560, 1440};
    expect(psx_borderless_hidden_rows(raised, screen, 1441) == 1, "the row above the screen is left out of the picture");
    expect(psx_borderless_hidden_rows({0, -3, 2560, 1440}, screen, 1443) == 3, "as many rows as there are above it");
    expect(psx_borderless_hidden_rows({2560, 199, 4480, 1280}, {2560, 200, 4480, 1280}, 1081) == 1, "on a screen that is not at the origin too");
    expect(psx_borderless_hidden_rows(raised, screen, 1440) == 0 && psx_borderless_hidden_rows(raised, screen, 1080) == 0,
           "none when the window's pixels are no higher than the screen: the picture then fits as it is");
    expect(psx_borderless_hidden_rows(screen, screen, 1441) == 0, "none for a window that starts where the screen does");
    expect(psx_borderless_hidden_rows({0, 4, 2560, 1440}, screen, 1441) == 0, "or below its top");
    for (const PsxHostRect other : {PsxHostRect{1, -1, 2560, 1440}, PsxHostRect{0, -1, 2559, 1440}, PsxHostRect{0, -1, 2560, 1441}})
        expect(psx_borderless_hidden_rows(other, screen, 1441) == 0, "none for a window that is not the screen from side to side and down to its last row");
}

}  // namespace

int main() {
    test_a_window_that_is_its_screen_goes_one_row_up();
    test_the_rows_above_the_screen_are_hidden();
    if (g_failures) return 1;
    std::cout << "Host borderless window tests passed\n";
    return 0;
}
