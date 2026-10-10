#include "cpu_state.h"
#include "mod_plugins.h"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <map>
#include <string>

namespace {

constexpr std::uint32_t kTestRow = 0x80071488u, kTestCursor = 0x80071684u, kTestName = 0x8007175Cu;
constexpr std::uint32_t kPressed = 0x4040u;
constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";  /* the game's own, at 0x80056F08 */

std::map<std::uint32_t, std::uint8_t> g_memory;
std::uint32_t g_entry_address = 0;
PSXModFunctionEntryCallback g_entry = nullptr;
PSXModVBlankCallback g_tick = nullptr;
int g_netplay = 0;
int g_strays = 0;
int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool ours(std::uint32_t address, std::uint32_t bytes) {
    return (address == kTestRow && bytes == 4u) || (address == kTestCursor && bytes == 4u) ||
           (address >= kTestName && address < kTestName + 8u && bytes == 1u);
}

}  // namespace

extern "C" {
int g_ls_mode = 0;
int g_ls_replay_active = 0;
int psx_netplay_active(void) { return g_netplay; }
std::uint32_t psx_mod_read_word(std::uint32_t address) {
    if (!ours(address, 4u)) ++g_strays;
    return g_memory[address] | g_memory[address + 1u] << 8 | g_memory[address + 2u] << 16 |
           static_cast<std::uint32_t>(g_memory[address + 3u]) << 24;
}
void psx_mod_write_word(std::uint32_t address, std::uint32_t value) {
    if (!ours(address, 4u)) ++g_strays;
    for (std::uint32_t byte = 0; byte < 4u; ++byte) g_memory[address + byte] = static_cast<std::uint8_t>(value >> (8u * byte));
}
void psx_mod_write_byte(std::uint32_t address, std::uint8_t value) {
    if (!ours(address, 1u)) ++g_strays;
    g_memory[address] = value;
}
int psx_mod_register_function_entry_plugin(const char *, std::uint32_t address, PSXModFunctionEntryCallback callback) {
    g_entry_address = address;
    g_entry = callback;
    return 1;
}
int psx_mod_register_vblank_plugin(const char *, PSXModVBlankCallback callback) {
    g_tick = callback;
    return 1;
}
}

#include "../src/disruptor_name_entry.cpp"

namespace {

void screen(std::uint32_t row, std::uint32_t cursor, const std::string &name = "        ") {
    psx_mod_write_word(kTestRow, row);
    psx_mod_write_word(kTestCursor, cursor);
    for (std::uint32_t slot = 0; slot < 8u; ++slot)
        g_memory[kTestName + slot] = static_cast<std::uint8_t>(std::string(kTable).find(name[slot]));
}

std::string name() {
    std::string text;
    for (std::uint32_t slot = 0; slot < 8u; ++slot) text += g_memory[kTestName + slot] < 37u ? kTable[g_memory[kTestName + slot]] : '?';
    return text;
}

std::uint32_t row() { return psx_mod_read_word(kTestRow); }
std::uint32_t cursor() { return psx_mod_read_word(kTestCursor); }

/* The routine is entered with this frame's presses: what is left of them afterwards. */
std::uint32_t frame(std::uint32_t address = 0x8001C484u) {
    CPUState cpu{};
    cpu.gpr[4] = kPressed;
    g_entry(&cpu, address);
    g_tick();
    for (int gpr = 0; gpr < 32; ++gpr) expect(gpr == 4 || cpu.gpr[gpr] == 0u, "the routine's entry may change its first argument and no other register");
    return cpu.gpr[4];
}

/* A key struck: down, a frame of the game, up. Returns whether it was taken. */
bool strike(int scancode, std::uint32_t keycode, int plain = 1) {
    const bool taken = disruptor_name_entry_key(scancode, keycode, 1, plain) != 0;
    frame();
    disruptor_name_entry_key(scancode, keycode, 0, 1);
    return taken;
}

void fresh(std::uint32_t at_row, std::uint32_t at_cursor, const std::string &text = "        ") {
    for (int tick = 0; tick < 200; ++tick) g_tick();
    screen(at_row, at_cursor, text);
    frame();
}

void test_keys_become_the_name() {
    expect(g_entry_address == 0x8001C484u && g_entry && g_tick, "the module must hook the save screen's routine and the VBlank");
    fresh(5, 0);
    expect(strike(4, 'a') && name() == "A       " && cursor() == 1u && row() == 5u, "a letter goes where the cursor is and the cursor moves on");
    expect(strike(5, 'B') && strike(38, '9') && strike(39, '0') && strike(44, 0xA0u) && strike(29, 'z'),
           "letters of either case, digits and the space bar, whatever a layout calls it, are text");
    expect(name() == "AB90 Z  " && cursor() == 6u, "each in the game's own code: letters, digits, blank");
    expect(strike(4, 'q') && name() == "AB90 ZQ ", "the layout's letter counts, not the key's place: A on a French board types Q");
    expect(strike(4, 0x444u) && name() == "AB90 ZQA" && cursor() == 8u, "a letter key a Cyrillic layout names gives the letter of its place, and the eighth letter leaves the cursor on DONE");
    expect(strike(5, 'c') && name() == "AB90 ZQA" && cursor() == 8u && row() == 5u, "a full name takes no more");

    fresh(5, 0);
    for (int key = 30; key <= 38; ++key) expect(strike(key, static_cast<std::uint32_t>("&~\"'(-`_^"[key - 30])), "a key of the digit row is a digit in every layout");
    expect(name() == "12345678" && cursor() == 8u, "which gives 1 to 8 here, and the ninth finds the name full");
    fresh(5, 0);
    expect(strike(39, 0xE0u) && strike(98, 0x40000062u) && strike(89, 0x40000059u) && strike(97, 0x40000061u) && name() == "0019    ",
           "0 of the digit row, and the keypad's 0, 1 and 9");
    fresh(5, 0);
    expect(strike(12, 0x131u) && strike(26, 0xE9u) && !strike(47, 0xFCu) && !strike(51, 0xF1u) && name() == "IW      ",
           "so does any letter key with a sign past ASCII, a Turkish dotless i for one, and a key that is no letter's place gives nothing whatever its sign");
    fresh(5, 2, "AB      ");
    expect(!strike(20, '\'') && !strike(51, ';') && !strike(58, 0x4000003Au) && !strike(82, 0x40000052u) && name() == "AB      " && cursor() == 2u,
           "a sign, a function key and an arrow are not text");
    expect(!strike(-1, 'a') && !strike(512, 'a') && !strike(100000, 'a') && name() == "AB      ", "a key without a place on the board is not taken");
}

void test_erasing() {
    fresh(5, 3, "ABC     ");
    expect(strike(42, 8) && name() == "AB      " && cursor() == 2u, "Backspace blanks the letter before the cursor and steps back");
    expect(strike(76, 127) && name() == "AB      " && cursor() == 2u, "Delete blanks the letter under the cursor");
    fresh(5, 1, "ABC     ");
    expect(strike(76, 127) && name() == "A C     " && cursor() == 1u, "and leaves the cursor where it is");
    fresh(5, 0, "ABC     ");
    expect(strike(42, 8) && name() == "ABC     " && cursor() == 0u, "Backspace at the first letter does nothing");
    fresh(5, 8, "ABCDEFGH");
    expect(strike(76, 127) && name() == "ABCDEFGH" && cursor() == 8u, "Delete on DONE does nothing");
    expect(strike(42, 8) && name() == "ABCDEFG " && cursor() == 7u, "Backspace on DONE takes the last letter");
    fresh(6, 8, "ABCDEFGH");
    expect(strike(4, 'a') && row() == 6u && name() == "ABCDEFGH", "a letter typed on ACCEPT NAME with the name full changes nothing");
    expect(strike(42, 8) && row() == 5u && cursor() == 7u && name() == "ABCDEFG ", "a key that changes the name brings the cursor back to the letters' row");
    fresh(7, 2, "AB      ");
    expect(strike(4, 'x') && row() == 5u && name() == "ABX     " && cursor() == 3u, "from RETURN TO MAIN MENU as well");
    fresh(5, 1000, "AB      ");
    expect(strike(4, 'x') && name() == "AB      " && cursor() == 1000u, "a cursor the game never has is taken for DONE and left alone");
    fresh(5, 1000, "ABCDEFGH");
    expect(strike(42, 8) && name() == "ABCDEFG " && cursor() == 7u, "and Backspace from it takes the last letter, not a byte far past the name");
}

/* The game's own confirm moves the cursor on, and from DONE goes to ACCEPT NAME: Enter is that confirm on the default keys. */
void test_enter_ends_the_typing() {
    for (const int enter : {40, 88}) {
        fresh(5, 3, "ABC     ");
        expect(disruptor_name_entry_key(enter, '\r', 1, 1) == 0, "Enter is never taken from the host");
        expect(frame() == kPressed && cursor() == 8u && row() == 5u && name() == "ABC     ",
               "it puts the cursor on DONE and leaves the frame's presses to the game");
        disruptor_name_entry_key(enter, '\r', 0, 1);
        expect(frame() == kPressed, "and holds nothing back afterwards");
    }
    fresh(5, 8, "ABC     ");
    disruptor_name_entry_key(40, '\r', 1, 1);
    expect(frame() == kPressed && cursor() == 8u && row() == 5u, "on DONE already it changes nothing");
    for (const std::uint32_t at : {6u, 7u}) {
        fresh(at, 2, "AB      ");
        disruptor_name_entry_key(40, '\r', 1, 1);
        expect(frame() == kPressed && cursor() == 2u && row() == at, "on ACCEPT NAME and RETURN TO MAIN MENU it is the game's confirm and nothing more");
    }
    fresh(5, 2, "AB      ");
    disruptor_name_entry_key(40, '\r', 1, 0);
    expect(frame() == kPressed && cursor() == 2u, "Alt+Enter is the host's");
    fresh(4, 2, "AB      ");
    disruptor_name_entry_key(40, '\r', 1, 1);
    expect(frame() == kPressed && cursor() == 2u, "and on another row of the save screen Enter moves nothing");
    for (const std::uint32_t before : {4u, 6u, 7u}) {
        fresh(before, 2, "AB      ");
        disruptor_name_entry_key(40, '\r', 1, 1);
        psx_mod_write_word(kTestRow, 5);
        expect(frame() == kPressed && cursor() == 2u, "Enter that came while another row was on moves nothing though the letters' row is on when it is looked at");
    }
    fresh(5, 2, "AB      ");
    disruptor_name_entry_key(40, '\r', 1, 1);
    psx_mod_write_word(kTestRow, 6);
    expect(frame() == kPressed && cursor() == 2u && row() == 6u, "nor does Enter that came on the letters' row once another row is on");
    fresh(5, 2, "AB      ");
    for (int tick = 0; tick < 7; ++tick) g_tick();
    disruptor_name_entry_key(40, '\r', 1, 1);
    expect(frame() == kPressed && cursor() == 2u, "and Enter after the screen was left is no Enter of the letters' row");
    fresh(5, 2, "AB      ");
    disruptor_name_entry_key(40, '\r', 0, 1);
    expect(frame() == kPressed && cursor() == 2u, "the release of Enter is no press");
    fresh(5, 1, "A       ");
    disruptor_name_entry_key(5, 'b', 1, 1);
    disruptor_name_entry_key(40, '\r', 1, 1);
    expect(frame() == 0u && name() == "AB      " && cursor() == 8u, "a letter and Enter in one frame: the letter is put, the cursor ends on DONE, the presses are the letter's");
    disruptor_name_entry_key(5, 'b', 0, 1);
}

void test_only_on_the_name_rows() {
    for (const std::uint32_t at : {0u, 1u, 4u, 8u, 0xFFFFFFFFu}) {
        fresh(at, 0);
        expect(!strike(4, 'a') && name() == "        " && row() == at && cursor() == 0u, "on another row of the save screen a key is not text");
        expect(frame() == kPressed, "and the frame's presses stand");
    }
    fresh(5, 0);
    expect(disruptor_name_entry_key(4, 'a', 1, 1) == 1, "on the name rows a key is taken");
    disruptor_name_entry_key(4, 'a', 0, 1);
    for (int tick = 0; tick < 7; ++tick) g_tick();
    expect(disruptor_name_entry_key(5, 'b', 1, 1) == 0, "at the eighth VBlank without the routine the screen has been left");
    expect(frame() == kPressed && name() == "        ", "and what was typed and not yet put is forgotten");
    fresh(5, 0);
    for (int tick = 0; tick < 6; ++tick) g_tick();
    expect(disruptor_name_entry_key(5, 'b', 1, 1) == 1, "seven are a slow frame");
    disruptor_name_entry_key(5, 'b', 0, 1);

    fresh(5, 0);
    disruptor_name_entry_key(4, 'a', 1, 1);
    disruptor_name_entry_key(4, 'a', 0, 1);
    CPUState cpu{};
    cpu.gpr[4] = kPressed;
    g_entry(&cpu, 0x8001C488u);
    g_entry(nullptr, 0x8001C484u);
    expect(cpu.gpr[4] == kPressed && name() == "        ", "another routine's entry and an entry without a CPU put nothing");
    expect(frame() == 0u && name() == "A       ", "the routine's own entry does");

    int *const blocks[3] = {&g_ls_mode, &g_ls_replay_active, &g_netplay};
    for (int *block : blocks) {
        *block = 1;
        fresh(5, 0);
        expect(!strike(4, 'a') && name() == "        ", "in lockstep, in its replay and in a net game the keyboard types nothing: text is not part of their input");
        *block = 0;
    }
}

void test_a_letter_key_is_no_button_that_frame() {
    fresh(5, 0);
    expect(frame() == kPressed, "with no key the frame's presses reach the routine");
    disruptor_name_entry_key(26, 'w', 1, 1);
    expect(frame() == 0u && name() == "W       ", "the frame a letter is put, the presses are dropped: W is also a way up");
    expect(frame() == 0u && frame() == 0u && name() == "W       ", "and while the key is held");
    disruptor_name_entry_key(26, 'w', 0, 1);
    expect(frame() == kPressed, "until it is let go");

    disruptor_name_entry_key(26, 'w', 1, 0);
    expect(frame() == kPressed && name() == "W       ", "a key with Ctrl, Alt or the system key is not text and holds nothing back");
    disruptor_name_entry_key(26, 'w', 0, 1);

    disruptor_name_entry_key(7, 'd', 1, 1);
    frame();
    for (int tick = 0; tick < 119; ++tick) expect(frame() == 0u, "a key whose release never came holds the presses back for a while");
    expect(frame() == kPressed, "and no longer than 120 VBlanks without a key event");
    disruptor_name_entry_key(7, 'd', 1, 1);
    for (int tick = 0; tick < 100; ++tick) frame();
    disruptor_name_entry_key(7, 'd', 1, 1);
    for (int tick = 0; tick < 100; ++tick) expect(frame() == 0u, "a key that repeats is still held");
    disruptor_name_entry_key(7, 'd', 0, 1);
    expect(frame() == kPressed, "its release ends that");

    fresh(5, 0);
    for (int key = 0; key < 40; ++key) disruptor_name_entry_key(4, 'a', 1, 1);
    disruptor_name_entry_key(4, 'a', 0, 1);
    expect(frame() == 0u && name() == "AAAAAAAA" && cursor() == 8u, "more keys than a frame can hold are dropped, not written past the name");
}

}  // namespace

int main() {
    test_keys_become_the_name();
    test_erasing();
    test_enter_ends_the_typing();
    test_only_on_the_name_rows();
    test_a_letter_key_is_no_button_that_frame();
    expect(g_strays == 0, "nothing but the row, the cursor and the name's eight codes may be read or written");
    if (g_failures) return 1;
    std::cout << "Disruptor name entry tests passed\n";
    return 0;
}
