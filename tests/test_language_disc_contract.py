#!/usr/bin/env python3
"""Version and wiring contract for Disruptor's language disc."""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800

# What the layout of the language disc takes from the US code: how much of WAD.IN's head is the table, how much
# of a level and of a menu piece the loaders read, where the movie player and the three text routines keep their
# tables, the glyph sizes, where the large font's letters start, and which table entry a movie's sound comes from.
# Then what the signs, the control names and the hints rest on: the large font's routine takes every character from
# 'a' up as a glyph with no upper bound, the small font's is called twice, once by the menu for strings 10 to 19,
# and a hint is 0x5000 bytes from head entry 385 + level.
RETAIL_INSTRUCTIONS = {
    0x80012930: 0x34060654,  # ori $a2, $zero, 0x654: the head table is 405 words
    0x80015400: 0x3C060007,  # lui $a2, 7
    0x80015420: 0x34C63800,  # ori $a2, $a2, 0x3800: a level's header and pictures
    0x800154A0: 0x8E300000,  # lw $s0, ($s1): its second part's size, header word 1
    0x800154CC: 0x02003021,  # move $a2, $s0
    0x8001FC6C: 0x3C060009,  # lui $a2, 9
    0x8001FC70: 0x34C62000,  # ori $a2, $a2, 0x2000: a menu piece, read whole
    0x80014530: 0x3C168005,  # lui $s6, 0x8005
    0x80014534: 0x26D6701C,  # addiu $s6, $s6, 0x701c: the movie tables
    0x80014674: 0x8C275DBC,  # lw $a3, 0x5dbc($at): head entry 349 + movie
    0x80044A1C: 0x3C0C8005,  # lui $t4, 0x8005
    0x80044A20: 0x258C6E40,  # addiu $t4, $t4, 0x6e40: glyph widths of the first font
    0x80044AF8: 0x3C0C8005,  # lui $t4, 0x8005
    0x80044AFC: 0x258C6E80,  # addiu $t4, $t4, 0x6e80: of the second
    0x80044B64: 0x00021180,  # sll $v0, $v0, 6: its glyphs are 64 bytes
    0x80044BF0: 0x3C0C8005,  # lui $t4, 0x8005
    0x80044BF4: 0x258C6EC4,  # addiu $t4, $t4, 0x6ec4: of the large font
    0x80044C28: 0x2042000A,  # addi $v0, $v0, 0xa: its letters follow ten signs
    0x80044C38: 0x200300A8,  # addi $v1, $zero, 0xa8: its glyphs are 168 bytes
    0x8001A4B8: 0x34060100,  # ori $a2, $zero, 0x100: a menu string is drawn 256 pixels wide at most
    0x80044C14: 0x2042FF9F,  # addi $v0, $v0, -0x61: the large font's glyph is the character less 'a'
    0x80044C18: 0x04410005,  # bgez $v0, 0x80044c30: for every character from 'a' up
    0x8001EEFC: 0x3C1E8005,  # lui $fp, 0x8005
    0x8001EF00: 0x27DE69F8,  # addiu $fp, $fp, 0x69f8: where the control names are drawn
    0x8001EF50: 0x2A02000A,  # slti $v0, $s0, 0xa: ten of them
    0x8001EF7C: 0x8C440028,  # lw $a0, 0x28($v0): the menu's strings from the tenth
    0x8001EF84: 0x34060100,  # ori $a2, $zero, 0x100: into a raster 256 pixels wide
    0x8001EF8C: 0x0C0112BA,  # jal 0x80044ae8: in the small font
    0x80042438: 0x0C0112BA,  # jal 0x80044ae8: its other caller, in a level
    0x80044B2C: 0x20A50004,  # addi $a1, $a1, 4: a space of the small font
    0x80044B34: 0x2042FF9F,  # addi $v0, $v0, -0x61: its small letters come first
    0x80044B48: 0x2042001A,  # addi $v0, $v0, 0x1a: then its capitals
    0x80044B54: 0x20420034,  # addi $v0, $v0, 0x34: then its digits
    0x8002091C: 0x8C275E4C,  # lw $a3, 0x5e4c($at): head entry 385 + level, the level's hint
    0x80020924: 0x34065000,  # ori $a2, $zero, 0x5000: as much of it as is loaded
}


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
overlay_files = read("PSXRECOMP_OVERLAY_FILES.txt")
disc = read("src/disruptor_language_disc.cpp")
disc_header = read("src/disruptor_language_disc.h")
rules = read("src/disruptor_language_pack.h")
source = read("src/disruptor_language_source.cpp")
header = read("psxrecomp-overlay/runtime/include/iso_overlay.h")
host = read("psxrecomp-overlay/runtime/src/main.cpp")
host_header = read("psxrecomp-overlay/runtime/include/host_ui.h")
pick_slot = read("psxrecomp-overlay/runtime/include/host_pick_slot.h")
restart = read("src/disruptor_restart.cpp")
settings_header = read("psxrecomp-overlay/recompiler/src/config_loader.h")
settings_loader = read("psxrecomp-overlay/recompiler/src/config_loader.cpp")
menu = read("src/disruptor_dev_menu.cpp")
reader = read("psxrecomp-overlay/runtime/src/iso_reader_c.cpp")

require(
    '/src/disruptor_language_disc.cpp"' in cmake
    and '/src/disruptor_language_source.cpp"' in cmake
    and "add_test(NAME disruptor_language_disc\n" in cmake
    and "add_test(NAME disruptor_language_overlay\n" in cmake
    and "add_test(NAME disruptor_language_overlay_tall\n        COMMAND disruptor-language-overlay-test tall)" in cmake
    and "        tests/disruptor_language_overlay_test.cpp)" in cmake
    and 'tests/test_language_disc_contract.py"\n            --require-artifacts)' in cmake,
    "the language disc sources and their three tests must remain registered, the contract with its inputs required",
)
require(
    "\n+runtime/include/iso_overlay.h\n" in overlay_files and "\nruntime/src/iso_reader_c.cpp\n" in overlay_files and "\n+runtime/include/host_pick_slot.h\n" in overlay_files,
    "the reader with the overlay hook and its header must be applied to the framework",
)
require(
    "    int (*read)(void *handle, uint32_t lba, uint8_t *raw);\n    uint32_t (*sectors)(void *handle);\n    void (*closed)(void *handle);" in header
    and all(name in header and name in reader for name in ("iso_set_overlay", "iso_open_plain", "iso_read_raw_sector_plain", "iso_sector_count_plain")),
    "the overlay's shape is shared by the reader and the module",
)
require(
    "    return g_overlay && std::find(g_plain.begin(), g_plain.end(), handle) == g_plain.end();" in reader
    and reader.count("overlaid(handle) && g_overlay->read ? g_overlay->read(handle, lba, ") == 2
    and reader.count("    if (stood_in < 0) return 0;\n") == 2
    and "    if (stood_in > 0) std::memcpy(buffer, raw + 24, 2048);\n    else if (!reader->ReadSector(lba, buffer)) return 0;" in reader
    and "    if (stood_in == 0 && !reader->ReadRawSector(lba, buffer)) return 0;" in reader
    and "    return handle && static_cast<PS1::ISOReader*>(handle)->ReadRawSector(lba, buffer) ? 1 : 0;" in reader
    and "    if (handle) g_plain.push_back(handle);" in reader
    and "    g_plain.erase(std::remove(g_plain.begin(), g_plain.end(), handle), g_plain.end());" in reader
    and "    if (!handle) return;\n    if (overlaid(handle) && g_overlay->closed) g_overlay->closed(handle);" in reader,
    "the overlay stands in front of every image but the ones opened plain, a sector it cannot give is an error, "
    "one it leaves comes from the image, and a plain read never asks it",
)
require(
    "    if (handle == g_game && g_state != State::kUnbuilt) {\n"
    "        if (g_state == State::kOn) keep_states_apart();\n"
    "        return g_state == State::kOn;\n"
    "    }\n"
    "    g_game = handle;\n"
    "    g_state = State::kRefused;\n" in source
    and "    if (g_other && g_disc.build(*g_home, *g_other, why)) {\n        g_state = State::kOn;\n" in source
    and "    if (!laid_out(handle)) return 0;\n    return g_disc.raw_sector(lba, raw) ? 1 : -1;" in source
    and "uint32_t sectors(void *handle) { return laid_out(handle) ? g_disc.sectors() : 0u; }" in source
    and "    bool raw_sector(uint32_t lba, uint8_t *raw) override { return iso_read_raw_sector_plain(handle_, lba, raw) != 0; }" in source
    and "        if (void *other = iso_open_plain(g_path.c_str())) g_other = std::make_unique<IsoImage>(other);" in source
    and "    if (handle != g_game) return;\n    g_game = nullptr;\n    g_state = State::kUnbuilt;\n}" in source
    and "const PsxIsoOverlay kOverlay = {read, sectors, closed};" in source,
    "a refused language disc leaves the game disc as it is, a laid-out one answers every sector or fails, "
    "both images are read past the overlay, and a layout ends with the image it was made for",
)
require(
    "    if (!path || path[0] == '\\0' || !g_path.empty()) return;\n    g_path = path;\n    iso_set_overlay(&kOverlay);" in source
    and source.count("iso_set_overlay(") == 1
    and "    if (g_state != State::kOn || g_disc.pack().empty()) return 0;" in source,
    "the overlay is installed only when a language disc is named, once, and its strings are handed out only when it is laid out",
)
require(
    "        if (length < 34 || at + length > root_bytes || 33u + record[32] > length) return false;" in disc
    and "            if (sectors > kMostFileSectors || uint64_t{lba} + sectors > image_sectors) return false;" in disc
    and "    if (!user_data(disc, volume.root_lba, (root_bytes + kSectorData - 1) / kSectorData * kSectorData, volume.root)) return false;" in disc
    and "    if (other.size() < other_b + kGlyphsB * kGlyphB) return false;" in disc,
    "a directory record, a file and a font are checked against what holds them before they are read",
)
require(
    "        if (carries_language(index) && cut.count(index) != their_cut.count(index)) {" in disc
    and "        if (same == their_pieces.end() || (speech && !only_within(piece, kSpeechFirst, kSpeechLast)) || (hint && !only_within(piece, kHintFirst, kHintLast))) {" in disc
    and "        const uint64_t other_hint = language->hints == Hints::kTall ? kTallHintBytes : kHintBytes;\n"
    "        if (hint && (uint64_t{count} * kSectorData != kHintBytes || uint64_t{other_count} * kSectorData != other_hint)) {" in disc
    and "constexpr uint32_t kHintBytes = 0x5000;" in disc_header
    and "constexpr uint32_t kTallHintBytes = 0xA000;" in disc_header
    and "    tall_hints_ = language->hints == Hints::kTall && hints != 0;\n    return true;" in disc
    and "            hints += hint ? 1u : 0u;" in disc
    and "    sectors_ = 0;\n    tall_hints_ = false;" in disc
    and 'extern "C" int disruptor_language_disc_tall_hints(void) { return g_state == State::kOn && g_disc.tall_hints() ? 1 : 0; }' in source
    and "            const uint64_t bytes = uint64_t{count} * kSectorData, other_bytes = uint64_t{other_count} * kSectorData;\n"
    "            const uint64_t second = le32(header.data() + 4), other_second = le32(other_header.data() + 4);\n"
    "            const bool sized = second % kSectorData == 0 && other_second % kSectorData == 0 && kLevelPictures + second <= bytes;\n"
    "            const uint64_t later = sized ? bytes - kLevelPictures - second : 0;\n"
    "            if (!sized || kLevelPictures + other_second + later > other_bytes) {" in disc,
    "an entry that carries a language is an offset on both discs or on neither, its piece holds the same entries on both, "
    "and a level's parts are sized in 64 bits before any of them is taken",
)
for constant in (
    "constexpr uint32_t kTableWords = 405;",
    "constexpr uint32_t kLevelPictures = 0x73800;",
    "constexpr uint32_t kSpeechFirst = 316, kSpeechLast = 365;",
    "constexpr size_t kFontC = 0x5F400, kGlyphC = 168, kGlyphsC = 46, kSignsC = 10;",
    "constexpr size_t kGlyphB = 64, kGlyphsB = 62, kLettersB = 26;",
    "constexpr size_t kWidthsC = pack_rules::kLastDrawn - 'a' + 1;",
    "constexpr int kNameRaster = 256;",
    "constexpr size_t kSmallFontFirst = 10, kSmallFontLast = 19;",
    "constexpr uint32_t kHintFirst = 386, kHintLast = 400;",
    "constexpr uint32_t kNamePlaces = 0x800569F8u;",
    "constexpr int kMenuWidth = 320;",
    "constexpr uint32_t kMovieTables = 0x8005701Cu, kMovieTable = 0x28u;",
    "constexpr std::array<uint32_t, 3> kWidths = {0x80056E40u, 0x80056E80u, 0x80056EC4u};",
    "constexpr uint32_t kMenuDraw = 0x8001A46Cu;",
    "constexpr size_t kCallWindow = 14;",
    'constexpr char kHomeBoot[] = "SLUS_002.24";',
):
    require(constant in disc, f"the layout must keep the audited constant: {constant}")
require(
    "    if (!open_volume(home, mine) || !mine.files.count(kHomeBoot) || !open_volume(other, theirs)) {" in disc
    and "    if (language == known.end()) {" in disc
    and '        {"SLES_005.64", "French", 0x80057A68u, {0x8005786Cu, 0x800578ACu, 0x800578F0u}, true,' in disc
    and "         0x8001A4E0u}," in disc
    and "         true, Hints::kSame, 1, {{'k', \"j\"}}," in disc
    and '        {"SLES_005.65", "German", 0x80057548u, {0x80057344u, 0x80057384u, 0x800573C8u}, true,' in disc
    and "         true, Hints::kSame, 4, {{'k', \"j\"}, {'l', \"\\x8F\"}, {'m', \"\\x90\"}, {'n', \"SS\"}}," in disc
    and "         0x8001A518u}," in disc
    and '        {"SLPS_008.04", "Japanese", 0x800543DCu, {}, false, false, Hints::kTall, 0, {}, {}, {}, {}, {}, {}, 0},' in disc,
    "the game disc must be the US one and the other disc one of the versions the build has tables for",
)
require(
    "    const size_t other_c = static_cast<size_t>(found - other_shape.begin()), extra = language.more_signs;" in disc
    and "    if (std::any_of(large.begin() + static_cast<std::ptrdiff_t>(kSignsC), large.end(), differs)) return false;" in disc
    and "        if (theirs < kSignsC || theirs >= kSignsC + extra || kFontC + (slot + 1) * kGlyphC > fonts_end) return false;" in disc
    and "        if (writes_taken_glyph(texts[index], language)) {" in disc
    and "        if (text.empty() || words.empty() || writes_taken_glyph(words, language)) {" in disc
    and "        const char first = static_cast<char>('a' + (slot - kGlyphsC) * kGlyphC / kGlyphB), last = static_cast<char>('a' + ((slot - kGlyphsC + 1) * kGlyphC - 1) / kGlyphB);\n"
    "        for (size_t index = kSmallFontFirst; index <= kSmallFontLast; ++index) {" in disc
    and "            const bool room = slot < kGlyphsC || (slot < kWidthsC && home[at(width)] == 0);" in disc
    and "    if (language.signs.size() > pack_rules::kMostSigns || !std::all_of(language.signs.begin(), language.signs.end(), taken)) {" in disc
    and "        return theirs >= kSignsC && theirs < kSignsC + language.more_signs && written.insert(sign.written).second &&" in disc
    and "        if (text[at] == '%') ++at;" in disc
    and "        else if (text[at] >= signs_end && text[at] <= 'z') return false;" in disc
    and "    return for_large_font(text, language) && std::any_of(language.signs.begin(), language.signs.end(), [&](const Sign &sign) {" in disc
    and "    if (!for_large_font(text, language)) return length;" in disc
    and "capitals(" not in disc
    and "stand_in_apostrophe" not in disc,
    "a sign's picture goes only into a glyph the US routine reaches and no string it draws already uses, over letters of the "
    "small font only where no control's name needs them, with its width only where the US table has room, and the pack "
    "names no sign the module would refuse",
)
require(
    "    if (!patch_executable(patched_exe, other_exe, *language) || (language->text && !build_pack(home_exe, other_exe, *language, pack_, why, &patched_exe))) {" in disc
    and "    if (count == 0 && (!language.signs.empty() || !places.empty())) {" in disc
    and "        const bool menu = language->text && within(piece, kMenuFirst, kMenuLast), level = language->text && within(piece, kLevelFirst, kLevelLast);\n"
    "        const bool speech = within(piece, kSpeechFirst, kSpeechLast), hint = within(piece, kHintFirst, kHintLast);" in disc
    and "    if (language.text) {\n" in disc
    and disc.count("language.text") == 1
    and disc.count("language->text") == 3,
    "a disc whose words are not strings of the US kind gives its movies, their tables, its speech and its hints, and nothing else",
)
require(
    "        const int64_t x = home_x < kMenuWidth / 2 ? std::max<int64_t>(0, home_x + home_width - width) : std::min<int64_t>(home_x, kMenuWidth - width);" in disc
    and "        if (width > kNameRaster) return -1;" in disc
    and "        if (home_width < 0 || width < 0) return false;" in disc
    and "        if (glyph < 0 && letter != ' ') return -1;\n        width += glyph < 0 ? 4 : exe[widths + static_cast<size_t>(glyph)];" in disc
    and "            if (!place_control_names(patched_exe, home_exe, home_names, names)) {" in disc
    and "            if (!menu_strings_fit(patched_exe, names, *language, why)) return false;" in disc
    and "                !menu_piece(ours, their_piece, *language, reworked, why, &home_names, &names)) {" in disc
    and "    if (home_names) *home_names = old;\n    if (names) *names = texts;" in disc
    and "    static_assert(kWidths[1] > kNamePlaces + 80, " in disc
    and disc.index("        if (home_width < 0 || width < 0) return false;") < disc.index("        const int64_t home_x = static_cast<int32_t>(le32(home.data() + at));"),
    "a control's name stays beside the US picture of the pad: it ends where the US name does on the left, and on the "
    "right starts there or as far left as it needs to end on the screen",
)
require(
    "        if (trusted && gap_is_even) places.insert(places.end(), stretch.begin(), stretch.end());" in disc
    and "            close(unpaired_mine == unpaired_theirs);\n            unpaired_mine = unpaired_theirs = 0;" in disc
    and "    close(unpaired_mine == unpaired_theirs);\n    return places;" in disc
    and "        if (whole) stretch.insert(stretch.end(), routine.begin(), routine.end());" in disc
    and "            if (mine[i].routine != in_routine) end_routine();" in disc
    and "        returns += word(index) == kReturn;" in disc
    and "constexpr uint32_t kReturn = 0x03E00008u;" in disc
    and "                const bool kept_by_calls = (copied >= 16 && copied <= 23) || copied == 30;" in disc
    and "                    if (before == kReturn || (is_call(before) && !kept_by_calls)) break;" in disc
    and "            if (step != 0 && (at == 0 || is_call(word(at - 1)) || word(at - 1) == kReturn)) break;" in disc
    and "    return opcode == 3 || (opcode == 0 && (word & 0x3F) == 9) || (opcode == 1 && (word >> 16 & 0x1E) == 0x10);" in disc
    and "    if (opcode == 0) return (word >> 11 & 31) == reg && word != 0 && function != 8 && (function < 0x18 || function > 0x1B);" in disc
    and "    const bool sets_target = (opcode >= 0x08 && opcode <= 0x0F) || (opcode >= 0x20 && opcode <= 0x26) || from_coprocessor;" in disc
    and "    if (mine.origin_shape != theirs.origin_shape || !constant_load(mine.origin_word, from) || !constant_load(theirs.origin_word, to)) return false;" in disc
    and "    step = to - from;\n    return step > kAnyX && step <= 32767;" in disc
    and "            if (mine[i].has_x && theirs[j].has_x) {\n"
    "                if (mine[i].x != theirs[j].x) routine.push_back({mine[i].at + 8, mine[i].x, theirs[j].x});\n"
    "            } else if (relative_x(mine[i].x_word, theirs[j].x_word, step) || relative_origin(mine[i], theirs[j], step)) {\n"
    "                if (step != 0) routine.push_back({mine[i].at + 8, kAnyX, step});\n"
    "            } else {\n"
    "                whole = false;" in disc
    and "    if (mine >> 26 != 0x09 || mine >> 16 != theirs >> 16 || (mine >> 16 & 31) != 5 || (mine >> 21 & 31) == 0) return false;" in disc
    and "    return step > kAnyX && step <= 32767;" in disc
    and "    return value > kAnyX && value <= 32767;" in disc
    and "using pack_rules::kAnyX;" in disc
    and "constexpr int32_t kAnyX = -32768;" in rules,
    "a menu string moves only at calls that pair up in a trusted stretch, by a constant x both executables give, by the "
    "constant both add to one register or by the constant the same code fills a copied register with, and the calls of a "
    "routine stay when one of them takes an x made in any other way",
)
require(
    "        if (const auto [earlier, fresh] = said.emplace(text, words); !fresh && earlier->second != words) {\n"
    "            why = \"one US string is given two different strings\";\n"
    "            return false;\n"
    "        }\n"
    "        if (text == words) continue;" in disc
    and disc.count("if (text == words) continue;") == 1
    and "            bool good = std::count(kinds.begin(), kinds.end(), 's') <= 1 && choice.size() == other_kinds.size();" in disc
    and "        put_le32(body.data() + at + 4, pack_rules::hash_of(text));" in disc
    and "        if (!pack_rules::entry_fits(text, words, format) || spelled_out(words, language) > static_cast<size_t>(pack_rules::kLongestString)) {" in disc
    and "    if (count > pack_rules::kMostStrings || places.size() > pack_rules::kMostPlaces) {" in disc
    and "    put_le32(pack.data(), pack_rules::kMagic);" in disc,
    "the pack names each string with the hash of its US text and holds nothing the text module would refuse",
)

require(
    "    if (g_states_apart) return;\n" in source
    and "    if (entry == 0u || !folder || folder[0] == '\\0') return;\n"
    '    savestate_configure((std::string(folder) + "/" + g_disc.language()).c_str(), bios, entry);\n'
    "    g_states_apart = true;" in source
    and "        if (g_state == State::kOn) keep_states_apart();\n        return g_state == State::kOn;" in source
    and "        g_state = State::kOn;\n        keep_states_apart();" in source
    and source.count("keep_states_apart();") == 2
    and "    g_game_disc = reader;\n    return reader;" in reader
    and "    void* const game = g_game_disc;\n    void* handle = iso_open(path);\n    g_game_disc = game;" in reader
    and "    if (handle == g_game_disc) g_game_disc = nullptr;" in reader
    and 'extern "C" void disruptor_language_settle(void) {\n'
    "    if (void *game = iso_game_disc(); game && !g_path.empty()) (void)laid_out(game);" in source
    and "        savestate_configure(memcard_dir.string().c_str(),\n"
    "                            memory_get_bios_checksum(), game_entry_pc);\n"
    "#ifdef PSX_HAS_DISRUPTOR_LANGUAGE\n"
    "        /* Before any state can be asked for: a language disc's states have a folder of their own. */\n"
    "        disruptor_language_settle();\n"
    "#endif" in host
    and host.index("    cdrom_init(disc_path_str.empty()") < host.index("        disruptor_language_settle();")
    < host.index('        if (const char *ls = std::getenv("PSX_LOAD_SLOT")) {')
    and "    language_ = language->name;\n    tall_hints_ = language->hints == Hints::kTall && hints != 0;\n    return true;" in disc
    and '    tall_hints_ = false;\n    language_ = "";' in disc,
    "a save state holds the game's copy of the disc's table, so a laid out disc's states must live in a folder of "
    "their own, and a refused disc's must stay with the game disc's",
)
require(
    "    bool has_language_disc = false; std::string language_disc;" in settings_header
    and '            s.language_disc = toml::find<std::string>(d, "language_disc");\n            s.has_language_disc = true;' in settings_loader
    and "        s.has_language_disc || s.has_skip_intro ||\n        s.has_hud_scale) {" in settings_loader
    and '            f << "language_disc = " << quoted(s.language_disc) << "\\n";' in settings_loader
    and "            if (letter == '\\\\' || letter == '\"') std::snprintf(escape, sizeof(escape), \"\\\\%c\", letter);\n"
    '            else if (code < 0x20 || code == 0x7F) std::snprintf(escape, sizeof(escape), "\\\\u%04X", code);' in settings_loader,
    "settings.toml must read and write [disruptor] language_disc whole: backslashes, quotes and control characters",
)
require(
    "PSX_HAS_DISRUPTOR_LANGUAGE=1" in cmake
    and '#ifdef PSX_HAS_DISRUPTOR_LANGUAGE\nextern "C" void disruptor_language_set_disc(const char* path);\n'
    'extern "C" void disruptor_language_settle(void);\n#endif' in host
    and '        if (us.has_language_disc && !std::getenv("PSX_DISRUPTOR_LANGUAGE_DISC"))\n'
    "            disruptor_language_set_disc(us.language_disc.c_str());" in host
    and host.index("disruptor_language_set_disc(us.language_disc.c_str());") < host.index("if (us.has_bios_hle)       bios_hle_requested = us.bios_hle;"),
    "the saved language disc must be named while the settings are read, before anything boots, unless the environment names one",
)
require(
    "int psx_host_pick_disc_image_begin(void);" in host_header
    and "int psx_host_pick_disc_image_poll(char *out, int size);" in host_header
    and 'extern "C" int psx_host_pick_disc_image_begin(void) {' in host
    and 'extern "C" int psx_host_pick_disc_image_poll(char* out, int size) {' in host
    and "        s_disc_worker = std::thread([] {" in host
    and "static std::thread& s_disc_worker = *new std::thread;" in host
    and "        if (s_disc_worker.joinable()) s_disc_worker.join();" in host
    and ".detach()" not in host[host.index("static PsxHostPickSlot& s_disc_pick"):host.index('extern "C" int psx_host_pick_disc_image_poll')]
    and "static PsxHostPickSlot& s_disc_pick = *new PsxHostPickSlot;" in host
    and "    if (g_headless || !s_disc_pick.open()) return 0;" in host
    and "            } catch (...) {" in host
    and "            s_disc_pick.close(std::move(path));" in host
    and "    } catch (const std::system_error&) {\n        s_disc_pick.abandon();\n        return 0;" in host
    and "    return s_disc_pick.take(out, size);" in host
    and "        if (state_ != State::kNone) return false;\n        state_ = State::kOpen;" in pick_slot
    and "        if (state_ != State::kClosed) return 0;\n        state_ = State::kNone;" in pick_slot
    and "add_test(NAME host_pick_slot\n" in cmake
    and "CLSID_FileOpenDialog" in host
    and "FOS_NOCHANGEDIR" in host
    and "GetOpenFileName" not in host.replace("IFileOpenDialog, not GetOpenFileName:", "")
    and "OPENFILENAME" not in host
    and "        if (!out || size <= 0 || picked_.empty() || picked_.size() >= static_cast<std::size_t>(size)) return -1;" in pick_slot
    and '    if (ImGui::Button("Choose a disc image")) (void)psx_host_pick_disc_image_begin();' in menu
    and "    if (psx_host_pick_disc_image_poll(picked, static_cast<int>(sizeof picked)) == 1) mark_language_disc(picked);" in menu
    and '    if (ImGui::Button("Restart now") && !psx_netplay_active() && flush_preferences() && disruptor_restart_arm()) {' in menu
    and "        quit.type = SDL_QUIT;\n        SDL_PushEvent(&quit);" in menu
    and '/src/disruptor_restart.cpp"' in cmake
    and "add_test(NAME disruptor_restart\n" in cmake
    and "    if (!before) return GetLastError() == ERROR_INVALID_PARAMETER;" in restart
    and "    return waited == WAIT_OBJECT_0;" in restart
    and "    if (!earlier_game_ended(std::strtoul(earlier, nullptr, 10), kLongestWait)) {" in restart
    and "        std::exit(1);" in restart
    and "    if (program.empty() || !CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &started)) {" in restart
    and "    program.resize(GetModuleFileNameW(nullptr, program.data(), static_cast<DWORD>(program.size())));" in restart
    and '    if (ImGui::Button("English") && !g_language_disc.empty()) mark_language_disc(std::string());' in menu
    and "    g_preferences.pending.has_language_disc = true;\n    g_preferences.pending.language_disc = path;\n    g_preferences.dirty |= PREF_LANGUAGE_DISC;" in menu
    and "    if (g_preferences.dirty & PREF_LANGUAGE_DISC) {\n        settings.has_language_disc = true;\n        settings.language_disc = pending.language_disc;" in menu
    and "    if (settings.has_language_disc) g_language_disc = settings.language_disc;" in menu,
    "the menu must show the saved disc, take a new one from a file dialog that neither stops the game nor moves its "
    "working directory, or none, stage it for saving, and offer a restart that saves first and is refused under netplay",
)

image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()
    require(
        sum(word == 0x0C0112BA for (word,) in struct.iter_unpack("<I", image[: len(image) // 4 * 4])) == 2,
        "the small font's routine must have its two callers and no other",
    )
    for address, instruction in RETAIL_INSTRUCTIONS.items():
        offset = ROM_TEXT_OFFSET + address - LOAD_ADDRESS
        require(0 <= offset <= len(image) - 4, f"retail instruction outside image: 0x{address:08X}")
        require(
            struct.unpack_from("<I", image, offset)[0] == instruction,
            f"the retail code the disc layout relies on changed at 0x{address:08X}",
        )

require(
    "--require-artifacts" not in sys.argv[1:] or image_path.exists(),
    "the build gate must check the retail image",
)
print(f"Disruptor language-disc source contract: PASS (retail image {'checked' if image_path.exists() else 'absent'})")
