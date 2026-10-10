#include "disruptor_language_discs.h"

#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

disruptor::Language plain_language() {
    disruptor::Language language = disruptor::languages().front();
    language.menu_stand_ins.clear();
    return language;
}

void test_the_file_is_cut_at_the_table() {
    std::vector<uint32_t> head(disruptor::kHeadWords, 0);
    head[1] = 0x7800;
    head[5] = 0x9000;
    head[6] = 0x9000;
    head[7] = 0xFFFFFFFFu;
    head[8] = 0x9100;
    head[9] = 0x20000;
    head[320] = 0x8000;
    head[404] = 0xA000;
    head[405] = 0x8800;
    const std::vector<disruptor::Piece> cut = disruptor::pieces(head, 0x10000);
    expect(cut.size() == 3 && cut[0].start == 0x7800 && cut[0].end == 0x9000 && cut[1].indexes == std::vector<uint32_t>{5, 6} &&
               cut[1].end == 0xA000 && cut[2].indexes == std::vector<uint32_t>{404} && cut[2].end == 0x10000,
           "pieces run from each table offset to the next, entries that share an offset share a piece");
}

void test_the_menu_takes_the_other_strings() {
    std::vector<std::string> us = numbered("US STRING ", 90), other = numbered("AUTRE CHAINE ", 92);
    other[3] = "LkAPOSTROPHE";
    other[4] = "petite kyrielle";
    other[5] = "LABEL TROP LONG f";
    disruptor::Language language = plain_language();
    language.menu_stand_ins = {{5, "COURT f"}};
    const Bytes home = menu_like(us, kTableAt, 0x10, false, ""), theirs = menu_like(other, kTableAt + 0x800, 0x40, true, "yz");
    Bytes out;
    std::string why;
    expect(disruptor::menu_piece(home, theirs, language, out, why) && out.size() == home.size(), "the menu piece must be reworked in place");
    const std::vector<disruptor::StringTable> tables = disruptor::string_tables(out);
    expect(tables.size() == 1 && tables[0].at == kTableAt && tables[0].count == 90, "the table stays where the US code looks for it");
    const std::vector<std::string> texts = disruptor::strings_of(out, tables[0]);
    expect(texts[0] == "AUTRE CHAINE 0" && texts[78] == "AUTRE CHAINE 78" && texts[79] == "US STRING 79" && texts[89] == "US STRING 89",
           "the first 79 strings come from the other disc, the credits stay");
    expect(texts[3] == "LkAPOSTROPHE" && texts[4] == "petite kyrielle" && texts[5] == "COURT f",
           "a sign stays as the other disc writes it, and a stand-in replaces its label");
    size_t end = disruptor::le32(out.data() + kTableAt + 4 * 89) + texts[89].size() + 1;
    expect(out[end] == 0xFF && std::all_of(out.begin() + static_cast<std::ptrdiff_t>(end) + 1, out.end(), [](uint8_t byte) { return byte == 0; }),
           "the strings end with the mark the US piece has, and zeros");
    expect(std::equal(home.begin(), home.begin() + static_cast<std::ptrdiff_t>(disruptor::kFontC), out.begin()), "nothing before the fonts changes");

    const auto glyph = [](const Bytes &piece, size_t at, size_t size) { return Bytes(piece.begin() + static_cast<std::ptrdiff_t>(at), piece.begin() + static_cast<std::ptrdiff_t>(at + size)); };
    const size_t c = disruptor::kFontC, g = disruptor::kGlyphC, b = c + disruptor::kGlyphsC * g;
    const size_t question_mark = 9;
    const Bytes sign = glyph(out, c + question_mark * g, g), theirs_sign = glyph(theirs, c + disruptor::kSignsC * g, g);
    expect(disruptor::shape(sign.data(), g) == disruptor::shape(theirs_sign.data(), g) && sign != theirs_sign &&
               std::all_of(sign.begin(), sign.end(), [](uint8_t value) { return value == 0 || (value >= 0x10 && value < 0x13); }),
           "the apostrophe takes the other disc's shape in the US piece's values");
    for (size_t slot = 0; slot < disruptor::kGlyphsC; ++slot)
        if (slot != question_mark) expect(glyph(out, c + slot * g, g) == glyph(home, c + slot * g, g), "every other large glyph stays");
    for (size_t slot = 0; slot < disruptor::kGlyphsB; ++slot) {
        const Bytes drawn = glyph(out, b + slot * 64, 64), ours = glyph(home, b + slot * 64, 64), other_glyph = glyph(theirs, b + 168 + slot * 64, 64);
        if (slot == 24 || slot == 25)
            expect(disruptor::shape(drawn.data(), 64) == disruptor::shape(other_glyph.data(), 64) && drawn != ours, "a changed small letter is carried over");
        else
            expect(drawn == ours, "an unchanged small letter stays");
    }

    other[7] = "UN j DE TROP";
    expect(!disruptor::menu_piece(home, menu_like(other, kTableAt + 0x800, 0x40, true, "yz"), plain_language(), out, why),
           "a menu string that uses the sign the apostrophe is put in refuses the piece");
    other[7] = "un j en petit";
    expect(disruptor::menu_piece(home, menu_like(other, kTableAt + 0x800, 0x40, true, "yz"), plain_language(), out, why),
           "a string for another font may use that letter");
    other[7] = std::string(3000, 'A');
    expect(!disruptor::menu_piece(home, menu_like(other, kTableAt + 0x800, 0x40, true, "yz"), plain_language(), out, why),
           "strings that do not fit refuse the piece");
    Bytes followed = home;
    followed[followed.size() - 4] = 0x55;
    expect(!disruptor::menu_piece(followed, theirs, plain_language(), out, why), "data after the US strings refuses the piece");
    Bytes fontless = theirs;
    std::fill(fontless.begin() + static_cast<std::ptrdiff_t>(disruptor::kFontC), fontless.begin() + static_cast<std::ptrdiff_t>(kTableAt), uint8_t{0});
    expect(!disruptor::menu_piece(home, fontless, plain_language(), out, why), "a piece without the fonts refuses");
    expect(!disruptor::menu_piece(home, Bytes(0x1000, 0), plain_language(), out, why), "a piece without strings refuses");
    Bytes carried = home, cut = theirs;
    cut.resize(disruptor::kFontC + disruptor::kSignsC * disruptor::kGlyphC);
    expect(!disruptor::carry_fonts(carried, home, cut, plain_language()), "a piece that ends after the signs of its large font is refused");
    cut = theirs;
    cut.resize(disruptor::kFontC + (disruptor::kGlyphsC + 1) * disruptor::kGlyphC + disruptor::kGlyphsB * disruptor::kGlyphB - 1);
    expect(!disruptor::carry_fonts(carried, home, cut, plain_language()) && disruptor::carry_fonts(carried, home, theirs, plain_language()),
           "a piece that ends inside its small font is refused");

    std::vector<std::string> same = other, home_names, names;
    std::fill(same.begin(), same.begin() + 79, std::string(60, 'X'));
    expect(disruptor::menu_piece(home, menu_like(same, kTableAt + 0x800, 0x40, true, "yz"), plain_language(), out, why, &home_names, &names) &&
               disruptor::le32(out.data() + kTableAt) == disruptor::le32(out.data() + kTableAt + 4 * 78) && disruptor::string_tables(out)[0].count != 90,
           "strings that fit only when equal ones share their bytes are written so, and a scan of the piece then finds another table than the one written");
    expect(home_names == us && names.size() == 90 && names[0] == std::string(60, 'X') && names[78] == names[0] && names[79] == "US STRING 79",
           "so the piece hands out the strings it was given and the ones it wrote");
}

void test_signs_go_where_the_us_routine_reaches() {
    std::vector<std::string> us = numbered("US STRING ", 90), other = numbered("ANDERE KETTE ", 92);
    other[3] = "lBUNG mL kRGER GROn";
    disruptor::Language language = plain_language();
    language.more_signs = 4;
    language.signs = {{'k', "j"}, {'l', "\x8F"}, {'m', "\x90"}, {'n', "SS"}};
    const Bytes home = menu_like(us, kTableAt, 0x10, 0, ""), theirs = menu_like(other, kTableAt + 0x800, 0x40, 4, "");
    Bytes out;
    std::string why;
    expect(disruptor::menu_piece(home, theirs, language, out, why), "a font with four more signs must be carried");
    const size_t c = disruptor::kFontC, g = disruptor::kGlyphC, b = c + disruptor::kGlyphsC * g;
    const auto shape_at = [&](const Bytes &piece, size_t at) { return disruptor::shape(piece.data() + at, g); };
    expect(shape_at(out, c + 9 * g) == shape_at(theirs, c + 10 * g) && shape_at(out, c + 46 * g) == shape_at(theirs, c + 11 * g) &&
               shape_at(out, c + 47 * g) == shape_at(theirs, c + 12 * g),
           "each pictured sign lands in the glyph its character names: the question mark's, and two past the font's last");
    expect(std::all_of(out.begin() + static_cast<std::ptrdiff_t>(c + 46 * g), out.begin() + static_cast<std::ptrdiff_t>(c + 48 * g),
                       [](uint8_t value) { return value == 0 || (value >= 0x10 && value < 0x13); }),
           "in the US piece's values");
    for (size_t slot = 0; slot < disruptor::kGlyphsC; ++slot)
        if (slot != 9) expect(Bytes(out.begin() + static_cast<std::ptrdiff_t>(c + slot * g), out.begin() + static_cast<std::ptrdiff_t>(c + (slot + 1) * g)) ==
                                  Bytes(home.begin() + static_cast<std::ptrdiff_t>(c + slot * g), home.begin() + static_cast<std::ptrdiff_t>(c + (slot + 1) * g)),
                              "the US font's own glyphs but the question mark stay");
    expect(std::equal(home.begin() + static_cast<std::ptrdiff_t>(b + 2 * g), home.begin() + static_cast<std::ptrdiff_t>(kTableAt), out.begin() + static_cast<std::ptrdiff_t>(b + 2 * g)),
           "and the small font past the two glyphs stays");
    expect(disruptor::strings_of(out, disruptor::string_tables(out)[0])[3] == "lBUNG mL kRGER GROn", "the strings keep the letters the other disc writes");

    const auto refused = [&](size_t index, const std::string &text) {
        std::vector<std::string> changed = other;
        changed[index] = text;
        Bytes into;
        return !disruptor::menu_piece(home, menu_like(changed, kTableAt + 0x800, 0x40, 4, ""), language, into, why);
    };
    expect(refused(10, "Feuer") && refused(19, "drehen") && refused(14, "a") && refused(12, "f"),
           "a control's name with a small letter the signs lie on refuses the piece");
    expect(!refused(9, "feuer") && !refused(20, "feuer") && !refused(15, "SPRINGEN") && !refused(15, "springt nun"),
           "other strings may use those letters, and a control's name the letters after them");
    expect(refused(30, "WAS j") && refused(30, std::string("WAS ") + '\x8F') && !refused(30, "WAS n"),
           "a string that writes a character a sign's picture is put under refuses the piece, one spelled with letters does not");
    const auto carried_with = [&](size_t more_signs, const std::vector<disruptor::Sign> &signs, const Bytes &from) {
        disruptor::Language one = language;
        one.more_signs = more_signs;
        one.signs = signs;
        Bytes into = home;
        return disruptor::carry_fonts(into, home, from, one);
    };
    expect(carried_with(4, language.signs, theirs) && !carried_with(3, {}, theirs) && !carried_with(5, {}, theirs),
           "the font is carried with the number of signs it has, and with no other");
    expect(!carried_with(4, {{'a', "j"}}, theirs) && !carried_with(4, {{'j', "j"}}, theirs) && !carried_with(4, {{'o', "j"}}, theirs) && carried_with(4, {{'n', "j"}}, theirs),
           "a sign must be one of those the other font has more");
    expect(carried_with(4, {{'k', "\xA5"}}, theirs) && !carried_with(4, {{'k', "\xA6"}}, theirs),
           "a sign's picture must land inside the two fonts");
    {
        disruptor::Language spelled = language;
        spelled.signs = {{'n', "jj"}, {'k', "S"}};
        Bytes into = home;
        expect(disruptor::carry_fonts(into, home, theirs, spelled) && into == home, "a sign drawn with two characters, or with a capital, brings no picture");
    }
    const auto names_refused = [&](const char *drawn, const std::string &name) {
        disruptor::Language one = language;
        one.signs = {{'k', drawn}};
        std::vector<std::string> changed = other;
        changed[3] = "KETTE";
        changed[10] = name;
        Bytes into;
        return !disruptor::menu_piece(home, menu_like(changed, kTableAt + 0x800, 0x40, 4, ""), one, into, why);
    };
    expect(names_refused("\x8F", "a") && names_refused("\x8F", "c") && !names_refused("\x8F", "d") && !names_refused("\x8F", "z"),
           "the first glyph past the large font lies on the small font's first three letters");
    expect(!names_refused("\x90", "b") && names_refused("\x90", "c") && names_refused("\x90", "f") && !names_refused("\x90", "g"),
           "the second on its third to sixth");
    expect(!names_refused("j", "a") && !names_refused("j", "y z"), "and a glyph of the large font on none");
    Bytes another = theirs;
    for (size_t pixel = 0; pixel < g; ++pixel) another[c + (disruptor::kSignsC + 4 + 20) * g + pixel] ^= 0x40;
    expect(!carried_with(14, language.signs, another), "a font whose letters have another shape is not the game's");
}

/* A stretch of code that ends in a call of `routine`, its fourteen instructions before the call told apart by `id`. */
void add_call(Bytes &exe, uint32_t id, uint32_t x_instruction, uint32_t routine, size_t x_before = 2, const std::map<size_t, uint32_t> &others = {}) {
    /* The fillers write $t0..$t7 only. */
    for (size_t at = 14; at > 0; --at) {
        const uint32_t filler = (8u + (id & 15u)) << 21 | (8u + (id >> 4 & 15u)) << 16 | (8u + (static_cast<uint32_t>(at) & 7u)) << 11 | 0x21u;
        const uint32_t word = at == x_before ? x_instruction : others.count(at) ? others.at(at) : filler;
        exe.resize(exe.size() + 4);
        disruptor::put_le32(exe.data() + exe.size() - 4, word);
    }
    exe.resize(exe.size() + 8, 0);
    disruptor::put_le32(exe.data() + exe.size() - 8, 0x0C000000u | (routine >> 2 & 0x03FFFFFFu));
}

uint32_t ori_x(int x) { return 0x34050000u | static_cast<uint32_t>(x & 0xFFFF); }
uint32_t addiu_x(int x) { return 0x24050000u | static_cast<uint32_t>(x & 0xFFFF); }
constexpr uint32_t kMoveZero = 0x00002821u;  /* addu $a1, $zero, $zero */
constexpr uint32_t kLoadX = 0x8FA50010u;  /* lw $a1, 16($sp) */
constexpr uint32_t kOtherDraw = 0x8001A4E0u;

struct PackEntry {
    uint32_t address;
    uint32_t hash;
    std::string words;
};

struct PackPlace {
    uint32_t returns_to;
    int from;
    int to;
};

std::vector<PackPlace> g_unpacked_places;
std::vector<disruptor::Sign> g_unpacked_signs;

std::vector<PackEntry> unpack(const Bytes &pack) {
    std::vector<PackEntry> out;
    g_unpacked_places.clear();
    g_unpacked_signs.clear();
    if (pack.size() < 8 || std::memcmp(pack.data(), "DLP4", 4) != 0) return out;
    size_t at = 8;
    for (uint32_t index = 0; index < disruptor::le32(pack.data() + 4); ++index) {
        const size_t length = pack[at + 8] | pack[at + 9] << 8;
        out.push_back({disruptor::le32(pack.data() + at), disruptor::le32(pack.data() + at + 4), std::string(pack.begin() + static_cast<std::ptrdiff_t>(at + 10), pack.begin() + static_cast<std::ptrdiff_t>(at + 10 + length))});
        at += 10 + length;
    }
    const uint32_t places = disruptor::le32(pack.data() + at);
    for (uint32_t index = 0; index < places; ++index) {
        const uint8_t *place = pack.data() + at + 4 + 8 * index;
        g_unpacked_places.push_back({disruptor::le32(place), static_cast<int16_t>(place[4] | place[5] << 8), static_cast<int16_t>(place[6] | place[7] << 8)});
    }
    const size_t signs_at = at + 4 + 8 * places;
    const uint32_t signs = disruptor::le32(pack.data() + signs_at);
    for (uint32_t index = 0; index < signs; ++index) {
        const uint8_t *sign = pack.data() + signs_at + 4 + 4 * index;
        g_unpacked_signs.push_back({static_cast<char>(sign[0]), std::string(reinterpret_cast<const char *>(sign + 2), sign[1])});
        if (sign[1] == 1 && sign[3] != 0) out.clear();
    }
    if (signs_at + 4 + 4 * signs != pack.size()) out.clear();
    return out;
}

void test_the_pack_pairs_the_two_executables() {
    Bytes home(0x2000, 0), other(0x2000, 0), pack;
    disruptor::Language language{};
    language.strings = {{0x80010100u, 0x80010200u}, {0x80010120u, 0x80010220u}, {0x80010140u, 0x80010240u},
                        {0x80010160u, 0x80010260u}, {0x80010180u, 0x80010280u}};
    language.names = {{0x80010400u, 16, 3, 0x80010500u, 24}};
    language.second_number = {0x80010160u};
    language.more_signs = 4;
    language.signs = {{'k', "j"}, {'n', "SS"}};
    put_string(home, 0x80010100u, "RESUME");
    put_string(other, 0x80010200u, "REPRENDRE");
    put_string(home, 0x80010120u, "SAME");
    put_string(other, 0x80010220u, "SAME");
    put_string(home, 0x80010140u, "PICKED UP %s");
    put_string(other, 0x80010240u, "%s RECUPERE");
    put_string(home, 0x80010160u, "OVER M%d WITH M%d");
    put_string(other, 0x80010260u, "PAR MISSION %d");
    put_string(home, 0x80010180u, "CARD");
    put_string(other, 0x80010280u, "LkEMPLACEMENT");
    put_string(home, 0x80010400u, "Rifle");
    put_string(other, 0x80010500u, "Fusil");
    put_string(home, 0x80010420u, "Blast");
    put_string(other, 0x80010530u, "Blast");
    std::string why;
    expect(disruptor::build_pack(home, other, language, pack, why), "the pack must be built");
    const std::vector<PackEntry> entries = unpack(pack);
    expect(entries.size() == 5 && entries[0].address == 0x80010100u && entries[0].words == "REPRENDRE" && entries[0].hash == disruptor::pack_rules::hash_of("RESUME"),
           "a string is named by its US address and the hash of its US text, an unchanged one is left out");
    expect(entries[1].words == "\x01 RECUPERE" && entries[2].address == 0x80010160u && entries[2].words == "PAR MISSION \x02",
           "the parts of a format are numbered, and a listed format takes the second number");
    expect(entries[3].words == "LkEMPLACEMENT" && entries[4].address == 0x80010400u && entries[4].words == "Fusil",
           "a sign stays as it is written, name slots are paired where both hold a name and differ");
    expect(g_unpacked_places.empty(), "executables without menu calls bring no places");
    expect(g_unpacked_signs.size() == 2 && g_unpacked_signs[0].written == 'k' && g_unpacked_signs[0].drawn == "j" && g_unpacked_signs[1].written == 'n' &&
               g_unpacked_signs[1].drawn == "SS" && pack[pack.size() - 5] == 0,
           "the pack ends with the signs: the letter written, how many characters it is drawn with, and those");
    language.string_stand_ins = {{0x80010160u, "PAR M%d"}, {0x80010100u, "LkAUTRE"}};
    expect(disruptor::build_pack(home, other, language, pack, why) && unpack(pack)[2].words == "PAR M\x02" && unpack(pack)[0].words == "LkAUTRE",
           "a stand-in takes the place of the other executable's string and is treated like one");
    language.string_stand_ins.clear();
    for (uint32_t id = 1; id <= 3; ++id) {
        add_call(home, id, ori_x(static_cast<int>(id) * 10), disruptor::kMenuDraw);
        add_call(other, id, id == 2 ? addiu_x(-300) : ori_x(static_cast<int>(id) * 10), kOtherDraw);
    }
    language.menu_draw = kOtherDraw;
    expect(disruptor::build_pack(home, other, language, pack, why) && unpack(pack).size() == 5 && g_unpacked_places.size() == 1 &&
               g_unpacked_places[0].returns_to == 0x80010000u + 0x1800u + 64 + 56 + 8 && g_unpacked_places[0].from == 20 && g_unpacked_places[0].to == -300,
           "the pack ends with the places, each a return address and two x in 16 bits");
    home.resize(0x2000);
    other.resize(0x2000);

    const auto refused = [&](uint32_t address, const std::string &text, bool in_home) {
        Bytes mine = home, theirs = other;
        std::fill_n((in_home ? mine : theirs).begin() + static_cast<std::ptrdiff_t>(address - 0x80010000u + 0x800u), 32, uint8_t{0});
        put_string(in_home ? mine : theirs, address, text);
        Bytes out;
        return !disruptor::build_pack(mine, theirs, language, out, why);
    };
    expect(refused(0x80010100u, "", true) && refused(0x80010200u, "", false), "a missing string refuses the pack");
    {
        Bytes mine = home, theirs = other, out;
        std::fill_n(mine.begin() + 0x900, 32, uint8_t{0});
        std::fill_n(theirs.begin() + 0xA00, 32, uint8_t{0});
        expect(!disruptor::build_pack(mine, theirs, language, out, why), "so does a string missing from both executables");
    }
    expect(refused(0x80010240u, "%d RECUPERE", false) && refused(0x80010240u, "%s %s", false),
           "a format whose parts do not match refuses the pack");
    expect(refused(0x80010140u, "A %s B %s", true), "a US format with two text parts refuses the pack");
    expect(refused(0x80010280u, "UN j", false) && !refused(0x80010280u, "un j", false), "words for the large font that use the character a sign is put under refuse the pack");
    expect(!refused(0x80010280u, std::string(86, 'A') + std::string(5, 'n'), false) && refused(0x80010280u, std::string(86, 'A') + std::string(6, 'n'), false),
           "so do words longer than the module writes once their signs are spelled out");
    expect(!refused(0x80010280u, std::string(86, 'x') + std::string(10, 'n'), false), "which another font's words are not");
    expect(refused(0x80010280u, std::string(49, 'n'), false) && refused(0x80010280u, "j", false) && refused(0x80010280u, "a j b", false) && refused(0x80010280u, "%s j", false),
           "words of nothing but signs and parts are words for the large font");
    expect(!refused(0x80010280u, "o j", false) && refused(0x80010280u, "n j", false) && !refused(0x80010280u, "z j", false) && !refused(0x80010280u, "%d j s", false),
           "and a small letter past the font's last sign makes them words for another");
    expect(refused(0x80010280u, "A|j", false) && refused(0x80010280u, "{j}", false), "which a character above the letters does not");
    const std::vector<std::vector<disruptor::Sign>> wrong_signs = {
        {{'K', "j"}}, {{'k', ""}}, {{'k', "SSS"}}, {{'k', " "}}, {{'k', "!"}}, {{'k', "\x91"}}, {{'j', "S"}}, {{'o', "S"}}, {{'k', "j"}, {'k', "S"}},
        {{'k', "S"}, {'l', "S"}, {'m', "S"}, {'n', "S"}, {'k', "S"}}};
    for (const std::vector<disruptor::Sign> &signs : wrong_signs) {
        disruptor::Language wrong = language;
        wrong.signs = signs;
        Bytes out;
        expect(!disruptor::build_pack(home, other, wrong, out, why), "a sign the module would not take refuses the pack");
    }
    expect(refused(0x80010180u, "RESUME", true), "one US text with two different strings refuses the pack");
    expect(refused(0x80010180u, "SAME", true), "so does one US text that one string keeps and another changes");

    const auto one_pair = [&](const std::string &text, const std::string &words) {
        Bytes mine(0x2000, 0), theirs(0x2000, 0), out(3, 0x55);
        disruptor::Language only{};
        only.strings = {{0x80010100u, 0x80010900u}};
        put_string(mine, 0x80010100u, text);
        put_string(theirs, 0x80010900u, words);
        const bool built = disruptor::build_pack(mine, theirs, only, out, why);
        return std::pair<bool, size_t>{built, out.size()};
    };
    expect(one_pair("A", std::string(96, 'B')).first && !one_pair("A", std::string(97, 'B')).first && !one_pair(std::string(97, 'A'), "B").first,
           "words or a US string longer than the module reads refuse the pack");
    expect(one_pair("%s %d", "%s , %d").first && !one_pair("%s%d", "%s %d").first && !one_pair("%d1", "%d").first,
           "a US format the module cannot take apart refuses the pack");
    expect(one_pair("SAME", "SAME") == std::pair<bool, size_t>{true, 0}, "executables that say the same bring no pack");
    {
        Bytes mine(0x2000, 0), theirs(0x2000, 0), out;
        disruptor::Language only{};
        only.strings = {{0x80010100u, 0x80010900u}};
        only.menu_draw = kOtherDraw;
        put_string(mine, 0x80010100u, "SAME");
        put_string(theirs, 0x80010900u, "SAME");
        only.more_signs = 1;
        only.signs = {{'k', "j"}};
        expect(!disruptor::build_pack(mine, theirs, only, out, why), "signs with no string for a pack to carry them refuse it");
        only.signs.clear();
        add_call(mine, 1, ori_x(10), disruptor::kMenuDraw);
        add_call(theirs, 1, ori_x(10), kOtherDraw);
        expect(disruptor::build_pack(mine, theirs, only, out, why) && out.empty(), "calls at the same places bring no pack either");
        add_call(mine, 2, ori_x(20), disruptor::kMenuDraw);
        add_call(theirs, 2, ori_x(30), kOtherDraw);
        expect(!disruptor::build_pack(mine, theirs, only, out, why), "and a place with no string for a pack to carry it refuses it");
    }
    const auto many = [&](uint32_t strings, uint32_t calls) {
        Bytes mine(0x4000, 0), theirs(0x4000, 0), out;
        disruptor::Language all{};
        all.menu_draw = kOtherDraw;
        for (uint32_t index = 0; index < strings; ++index) {
            all.strings.push_back({0x80010100u + 16 * index, 0x80010100u + 16 * index});
            put_string(mine, 0x80010100u + 16 * index, "U" + std::to_string(index));
            put_string(theirs, 0x80010100u + 16 * index, "F" + std::to_string(index));
        }
        for (uint32_t id = 1; id <= calls; ++id) {
            add_call(mine, id, ori_x(1), disruptor::kMenuDraw);
            add_call(theirs, id, ori_x(2), kOtherDraw);
        }
        return disruptor::build_pack(mine, theirs, all, out, why) && unpack(out).size() == strings && g_unpacked_places.size() == calls;
    };
    expect(many(512, 512) && !many(513, 1) && !many(1, 513), "a pack holds 512 strings and 512 places and no more");
}

void test_places_come_from_calls_that_pair_up() {
    const auto places = [](const std::vector<std::pair<uint32_t, uint32_t>> &mine, const std::vector<std::pair<uint32_t, uint32_t>> &theirs) {
        Bytes home(0x800, 0), other(0x800, 0);
        for (const auto &[id, x] : mine) add_call(home, id, x, disruptor::kMenuDraw);
        for (const auto &[id, x] : theirs) add_call(other, id, x, kOtherDraw);
        return disruptor::other_places(home, other, kOtherDraw);
    };
    const auto returns_to = [](size_t call) { return 0x80010000u + static_cast<uint32_t>(call * 64 + 56 + 8); };
    std::vector<disruptor::Place> found = places({{1, ori_x(48)}, {2, ori_x(28)}, {3, ori_x(252)}, {4, ori_x(48)}, {5, addiu_x(-40)}},
                                                 {{1, ori_x(48)}, {2, ori_x(14)}, {3, addiu_x(270)}, {4, kMoveZero}, {5, addiu_x(-60)}});
    expect(found.size() == 4 && found[0].returns_to == returns_to(1) && found[0].home_x == 28 && found[0].other_x == 14 && found[1].other_x == 270 &&
               found[2].home_x == 48 && found[2].other_x == 0 && found[3].home_x == -40 && found[3].other_x == -60,
           "a call with another constant x is a place, whichever way the constant is loaded");
    expect(places({{1, kLoadX}, {2, ori_x(1)}}, {{1, kLoadX}, {2, ori_x(2)}}).empty(), "an x read from memory keeps the calls of its routine where they are");
    constexpr uint32_t kMixX = 0x01092826u;  /* xor $a1, $t0, $t1 */
    const auto with_input = [](uint32_t input, uint32_t other_input) {
        Bytes home(0x800, 0), other(0x800, 0);
        add_call(home, 1, kMixX, disruptor::kMenuDraw, 2, {{5, input}});
        add_call(other, 1, kMixX, kOtherDraw, 2, {{5, other_input}});
        add_call(home, 2, ori_x(1), disruptor::kMenuDraw);
        add_call(other, 2, ori_x(2), kOtherDraw);
        return disruptor::other_places(home, other, kOtherDraw).size();
    };
    const auto behind = [](uint32_t last_write, const std::map<size_t, uint32_t> &also) {
        Bytes home(0x800, 0), other(0x800, 0);
        std::map<size_t, uint32_t> mine = also, theirs = also;
        mine[6] = ori_x(1);
        theirs[6] = ori_x(2);
        add_call(home, 1, last_write, disruptor::kMenuDraw, 2, mine);
        add_call(other, 1, last_write, kOtherDraw, 2, theirs);
        add_call(home, 2, ori_x(1), disruptor::kMenuDraw);
        add_call(other, 2, ori_x(2), kOtherDraw);
        return disruptor::other_places(home, other, kOtherDraw).size();
    };
    expect(behind(0x48850000u, {}) == 2, "a move of $a1 to a coprocessor leaves the constant in it");
    expect(behind(0x01095021u, {{4, 0x03E00008u}}) == 0, "a constant from before a return is not the x: it is another routine's");
    expect(behind(0x01095021u, {{4, 0x08004000u}}) == 2 && behind(0x01095021u, {{4, 0x01000008u}}) == 2,
           "a constant from before a jump inside the routine is the x: the search follows addresses, as an if and its else are laid out");
    expect(behind(0x01095021u, {{7, 0x0C004000u}}) == 0 && behind(0x01095021u, {{7, 0x0320F809u}}) == 0 && behind(0x01095021u, {{7, 0x04110001u}}) == 0 &&
               behind(0x01095021u, {{7, 0x04100001u}}) == 0 && behind(0x01095021u, {{7, 0x04010001u}}) == 2,
           "a constant in the delay slot of an earlier call, of any of the four kinds, is not the x, one in the delay slot of a branch is");
    constexpr uint32_t kCopyRa = 0x03E02821u, kCopyS1Early = 0x02202821u;  /* addu $a1, $ra, $zero and addu $a1, $s1, $zero */
    const auto linked = [](uint32_t copy, uint32_t fill, uint32_t other_fill, uint32_t between) {
        Bytes home(0x800, 0), other(0x800, 0);
        add_call(home, 1, copy, disruptor::kMenuDraw, 2, {{9, fill}, {5, between}});
        add_call(other, 1, copy, kOtherDraw, 2, {{9, other_fill}, {5, between}});
        add_call(home, 2, ori_x(1), disruptor::kMenuDraw);
        add_call(other, 2, ori_x(2), kOtherDraw);
        return disruptor::other_places(home, other, kOtherDraw).size();
    };
    expect(linked(kCopyRa, 0x341F003Fu, 0x341F002Bu, 0x01095021u) == 2 && linked(kCopyRa, 0x341F003Fu, 0x341F002Bu, 0x04110001u) == 0 &&
               linked(kCopyRa, 0x341F003Fu, 0x341F002Bu, 0x04100001u) == 0,
           "a branch that links ends the search for what filled a register calls do not keep");
    expect(linked(kCopyS1Early, 0x3411003Fu, 0x3411002Bu, 0x0320F809u) == 2 && linked(kCopyS1Early, 0x3411003Fu, 0x3411002Bu, 0x03208809u) == 0 &&
               linked(kCopyS1Early, 0x3411003Fu, 0x3411002Bu, 0x03E00008u) == 0 && linked(kCopyS1Early, 0x3411003Fu, 0x3411002Bu, 0x08004000u) == 2,
           "a call that links into $ra leaves a kept register, one that links into the register fills it, a return ends the search and a jump does not");
    expect(behind(0x01095021u, {}) == 2 && behind(0x9BA50000u, {{3, 0x8BA50003u}}) == 0 && behind(0x8BA50003u, {}) == 0 && behind(0x48050000u, {}) == 0 &&
               behind(0x40056000u, {}) == 0 && behind(0x48450000u, {}) == 0,
           "a constant is not the x when an unaligned load or a move from a coprocessor writes $a1 after it");
    expect(with_input(0x34080001u, 0x34080002u) == 0 && with_input(0x34080001u, 0x34080001u) == 0,
           "an x made in a way that is not carried keeps the calls of its routine, whether or not what it is made from is seen to differ");
    found = places({{1, 0x36050005u}, {2, ori_x(1)}}, {{1, 0x36050009u}, {2, ori_x(2)}});
    expect(found.empty(), "an x the executables compute differently is none, and keeps the calls of its routine where they are");
    found = places({{1, 0x26050005u}, {2, ori_x(1)}, {3, 0x24A5003Eu}, {4, 0x24A50007u}},
                   {{1, 0x26050005u}, {2, ori_x(2)}, {3, 0x24A5002Au}, {4, 0x24A50007u}});
    expect(found.size() == 2 && found[0].home_x == 1 && found[1].returns_to == returns_to(2) && found[1].home_x == -32768 && found[1].other_x == -20,
           "an x that is a sum ending in another constant is a step, a sum ending in the same constant is nothing");
    expect(places({{1, ori_x(1)}, {2, 0x00B12821u}}, {{1, ori_x(2)}, {2, 0x00B12821u}}).empty(), "a sum of two registers keeps the calls of its routine");
    found = places({{1, 0x24058000u}, {2, ori_x(1)}}, {{1, addiu_x(7)}, {2, ori_x(2)}});
    expect(found.empty(), "an x of -32768 stands for any x in a pack, so as a constant it is not carried");
    found = places({{1, 0x34058000u}, {2, ori_x(1)}}, {{1, ori_x(7)}, {2, ori_x(2)}});
    expect(found.empty(), "an x past what a pack's sixteen bits hold is not carried");
    found = places({{1, 0x24A57FFFu}, {2, ori_x(1)}}, {{1, 0x24A58000u}, {2, ori_x(2)}});
    expect(found.empty(), "a step past what a pack's sixteen bits hold is not carried");
    std::int32_t step = 0;
    expect(disruptor::relative_x(0x24A5003Eu, 0x24A5002Au, step) && step == -20 && disruptor::relative_x(0x26050005u, 0x26050009u, step) && step == 4,
           "a sum into $a1 from one register in both executables gives the step between the two constants");
    expect(!disruptor::relative_x(0x24A50005u, 0x26050009u, step) && !disruptor::relative_x(0x24A40005u, 0x24A40009u, step) &&
               !disruptor::relative_x(0x24050005u, 0x24050009u, step) && !disruptor::relative_x(0x34A50005u, 0x34A50009u, step),
           "a sum from two registers, into another register, from zero, or no sum at all gives no step");
    constexpr uint32_t kCopyS1 = 0x02202821u, kCopyV1 = 0x00602821u;  /* addu $a1, $s1, $zero and addu $a1, $v1, $zero */
    /* A call with a constant x whose stretch fills a register, then a call that copies the register into $a1, then one with a constant x. */
    const auto traced = [](uint32_t copy, uint32_t fill, uint32_t other_fill, bool returns_between = false, uint32_t other_first = 1) {
        Bytes home(0x800, 0), other(0x800, 0);
        add_call(home, 1, ori_x(5), disruptor::kMenuDraw, 2, {{10, fill}});
        add_call(other, other_first, ori_x(5), kOtherDraw, 2, {{10, other_fill}});
        for (Bytes *exe : {&home, &other}) {
            if (!returns_between) continue;
            exe->resize(exe->size() + 8, 0);
            disruptor::put_le32(exe->data() + exe->size() - 8, 0x03E00008u);
        }
        add_call(home, 2, copy, disruptor::kMenuDraw);
        add_call(other, 2, copy, kOtherDraw);
        add_call(home, 3, ori_x(1), disruptor::kMenuDraw);
        add_call(other, 3, ori_x(2), kOtherDraw);
        return disruptor::other_places(home, other, kOtherDraw);
    };
    found = traced(kCopyS1, 0x3411003Fu, 0x3411002Bu);
    expect(found.size() == 2 && found[0].returns_to == returns_to(1) && found[0].home_x == -32768 && found[0].other_x == -20 && found[1].home_x == 1,
           "an x copied from a register the same code fills with another constant is a step, also past a call for a register calls keep");
    expect(traced(kCopyS1, 0x3411003Fu, 0x3411003Fu).size() == 1, "a copied register filled alike is nothing");
    expect(traced(kCopyS1, 0x8FB10010u, 0x8FB10014u).empty(), "a copied register filled differently, and not by constants, keeps its routine's calls");
    expect(traced(kCopyS1, 0x34117FFFu, 0x24118001u).empty(), "a copied register whose constants are too far apart for a pack keeps its routine's calls");
    expect(traced(kCopyS1, 0x3411003Fu, 0x3411002Bu, true).empty(), "what fills a copied register is not looked for past a return, and a copy that is not traced keeps its routine's calls");
    expect(traced(kCopyV1, 0x3403003Fu, 0x3403002Bu).empty(), "nor past a call for a register calls do not keep");
    expect(traced(kCopyS1, 0x3411003Fu, 0x3411002Bu, false, 9).empty(), "other code between the constant and the copy is no step");
    const auto in_one_stretch = [&](uint32_t between) {
        Bytes home(0x800, 0), other(0x800, 0);
        add_call(home, 1, kCopyV1, disruptor::kMenuDraw, 2, {{9, 0x3403003Fu}, {5, between}});
        add_call(other, 1, kCopyV1, kOtherDraw, 2, {{9, 0x3403002Bu}, {5, between}});
        return disruptor::other_places(home, other, kOtherDraw);
    };
    expect(in_one_stretch(0x01095021u).size() == 1 && in_one_stretch(0x0320F809u).empty(),
           "a register calls do not keep is traced where no call lies between, and a call through a register is a call");
    expect(disruptor::copied_into_a1(kCopyS1) == 17 && disruptor::copied_into_a1(0x00112821u) == 17 && disruptor::copied_into_a1(0x02202825u) == 17,
           "a copy into $a1 is an addu or an or with $zero on either side");
    expect(disruptor::copied_into_a1(kMoveZero) == 0 && disruptor::copied_into_a1(0x02302821u) == 0 && disruptor::copied_into_a1(0x02202021u) == 0 &&
               disruptor::copied_into_a1(0x02202823u) == 0 && disruptor::copied_into_a1(0x34112821u) == 0,
           "zero, a sum of two registers, a copy into another register, a difference and an ori whose constant reads as a copy are none");
    Bytes two(0x800, 0), other_two(0x800, 0);
    add_call(two, 1, 0x36050005u, disruptor::kMenuDraw);
    add_call(other_two, 1, 0x36050009u, kOtherDraw);
    for (Bytes *exe : {&two, &other_two}) {
        exe->resize(exe->size() + 8, 0);
        disruptor::put_le32(exe->data() + exe->size() - 8, 0x03E00008u);
    }
    add_call(two, 2, ori_x(1), disruptor::kMenuDraw);
    add_call(other_two, 2, ori_x(2), kOtherDraw);
    found = disruptor::other_places(two, other_two, kOtherDraw);
    expect(found.size() == 1 && found[0].home_x == 1, "a call of another routine moves all the same");
    found = places({{1, ori_x(1)}, {8, ori_x(5)}, {2, ori_x(2)}, {3, ori_x(3)}}, {{1, ori_x(10)}, {9, ori_x(5)}, {2, ori_x(20)}, {3, ori_x(30)}});
    expect(found.size() == 3 && found[1].home_x == 2 && found[2].other_x == 30, "one unpaired call on each side leaves the calls around it trusted");
    found = places({{1, ori_x(1)}, {2, ori_x(2)}, {3, ori_x(3)}, {8, ori_x(5)}, {4, ori_x(4)}, {5, ori_x(5)}},
                   {{1, ori_x(10)}, {2, ori_x(20)}, {3, ori_x(30)}, {4, ori_x(40)}, {5, ori_x(50)}});
    expect(found.empty(), "a call only one executable has makes the calls on both sides of it untrusted");
    found = places({{1, ori_x(1)}, {8, ori_x(5)}, {2, ori_x(2)}, {9, ori_x(5)}, {10, ori_x(5)}, {3, ori_x(3)}},
                   {{1, ori_x(10)}, {11, ori_x(5)}, {2, ori_x(20)}, {12, ori_x(5)}, {3, ori_x(30)}});
    expect(found.size() == 1 && found[0].home_x == 1, "trust ends where the unpaired calls stop being as many on both sides");
    Bytes home(0x800, 0), other(0x800, 0);
    const auto second_call_close_behind = [](Bytes &exe, int x, uint32_t routine) {
        add_call(exe, 1, ori_x(x), routine);
        for (uint32_t word : {0x01095021u, 0x012A5821u, 0x014B6021u, 0x0C000000u | (routine >> 2 & 0x03FFFFFFu), 0u}) {
            exe.resize(exe.size() + 4);
            disruptor::put_le32(exe.data() + exe.size() - 4, word);
        }
    };
    second_call_close_behind(home, 7, disruptor::kMenuDraw);
    second_call_close_behind(other, 8, kOtherDraw);
    found = disruptor::other_places(home, other, kOtherDraw);
    expect(found.empty(), "an x from before the call before is not this call's, and a call with no x of its own keeps its routine's calls");
    expect(disruptor::other_places(Bytes(0x700, 0), other, kOtherDraw).empty() && disruptor::other_places(home, Bytes(), kOtherDraw).empty(),
           "an executable too short to hold code gives no places");
    const auto with_between = [](uint32_t between) {
        Bytes mine(0x800, 0), theirs(0x800, 0);
        add_call(mine, 1, ori_x(1), disruptor::kMenuDraw, 6, {{3, between}});
        add_call(theirs, 1, ori_x(2), kOtherDraw, 6, {{3, between}});
        return disruptor::other_places(mine, theirs, kOtherDraw).size();
    };
    expect(with_between(0x01095021u) == 1 && with_between(0x0320F809u) == 0, "a constant from before a call through a register is not this call's x");
}

void test_the_executable_takes_the_other_tables() {
    const disruptor::Language language = disruptor::languages().front();
    Bytes home(0x62000, 0x11), other(0x62800, 0x22);
    const auto at = [](uint32_t address) { return static_cast<size_t>(address - 0x80010000u + 0x800u); };
    for (size_t movie = 0; movie < 20; ++movie) {
        other[at(language.movie_tables) + 0x78 + 2 * movie] = movie == 0 ? 60 : movie < 3 ? 0 : 44;
        other[at(language.movie_tables) + 0x78 + 2 * movie + 1] = 0;
    }
    other[at(language.widths[2]) + 10] = 6;
    Bytes patched = home;
    expect(disruptor::patch_executable(patched, other, language), "the executable must be patched");
    const auto same = [&](uint32_t address, size_t size, uint8_t value) {
        return std::all_of(patched.begin() + static_cast<std::ptrdiff_t>(at(address)), patched.begin() + static_cast<std::ptrdiff_t>(at(address) + size), [=](uint8_t byte) { return byte == value; });
    };
    expect(same(0x8005701Cu, 0x78, 0x22) && same(0x80056E40u, 0x40, 0x22) && same(0x80056E80u, 0x44, 0x22), "movie and width tables come from the other executable");
    const uint8_t *tops = patched.data() + at(0x8005701Cu + 0x78);
    expect(tops[0] == 24 && tops[2] == 0 && tops[6] == 16 && tops[38] == 16 && tops[1] == 0, "PAL picture tops are centred in 240 rows, an absent movie stays absent");
    expect(patched[at(0x80056EC4u) + 9] == 6 && patched[at(0x80056EC4u) + 8] == 0x11 && patched[at(0x80056EC4u) + 10] == 0x11, "one width of the large font changes: the apostrophe's");
    size_t changed = 0;
    for (size_t index = 0; index < home.size(); ++index) changed += patched[index] != home[index];
    expect(changed == 0x78 + 0x28 + 0x40 + 0x44 + 1, "nothing else in the executable changes");
    Bytes small(0x1000, 0);
    expect(!disruptor::patch_executable(small, other, language), "an executable without the tables refuses");

    disruptor::Language german = language;
    german.signs = {{'k', "j"}, {'l', "\x8F"}, {'m', "\x90"}, {'n', "SS"}};
    for (size_t sign = 10; sign < 14; ++sign) other[at(german.widths[2]) + sign] = static_cast<uint8_t>(0x30 + sign);
    patched = home;
    expect(!disruptor::patch_executable(patched, other, german), "a US table with a width where its padding should be has no room for a sign's");
    home[at(0x80056EC4u) + 46] = home[at(0x80056EC4u) + 47] = 0;
    patched = home;
    patched[at(0x80056EC4u) + 47] = 1;
    expect(!disruptor::patch_executable(patched, other, german), "nor one with a single byte of padding");
    patched = home;
    expect(disruptor::patch_executable(patched, other, german) && patched[at(0x80056EC4u) + 9] == 0x3A && patched[at(0x80056EC4u) + 46] == 0x3B &&
               patched[at(0x80056EC4u) + 47] == 0x3C && patched[at(0x80056EC4u) + 45] == 0x11 && patched[at(0x80056EC4u) + 48] == 0x11,
           "each pictured sign brings its width to the glyph it is put in, the two past the font's last into the bytes after the table");
    german.signs = {{'k', "\x91"}};
    home[at(0x80056EC4u) + 48] = 0;
    patched = home;
    expect(!disruptor::patch_executable(patched, other, german), "a sign whose glyph has no byte for a width refuses");
    disruptor::Language spoken = language;
    spoken.text = false;
    spoken.pal = false;
    patched = home;
    changed = 0;
    expect(disruptor::patch_executable(patched, other, spoken), "a disc that gives no words still gives its movie tables");
    for (size_t index = 0; index < home.size(); ++index) changed += patched[index] != home[index];
    expect(changed == 0xA0 && same(0x8005701Cu, 0x78, 0x22) && patched[at(0x8005701Cu) + 0x78] == 60, "and nothing else, the picture tops as they are on an NTSC disc");
}

void test_control_names_stand_beside_the_us_pad() {
    const auto at = [](uint32_t address) { return static_cast<size_t>(address - 0x80010000u + 0x800u); };
    Bytes home(0x62000, 0), patched(0x62000, 0);
    for (size_t glyph = 0; glyph < 62; ++glyph) {
        home[at(0x80056E80u) + glyph] = 5;
        patched[at(0x80056E80u) + glyph] = glyph < 26 ? 6 : glyph < 52 ? 7 : 8;
    }
    expect(disruptor::small_font_width(patched, "ab AB 09") == 2 * 6 + 4 + 2 * 7 + 4 + 2 * 8 && disruptor::small_font_width(patched, "") == 0 &&
               disruptor::small_font_width(patched, "z Z") == 6 + 4 + 7,
           "a name is as wide as its glyphs' widths, small letters first, then capitals, then digits, and four for a space");
    expect(disruptor::small_font_width(patched, "a-b") < 0 && disruptor::small_font_width(patched, "a{") < 0 && disruptor::small_font_width(patched, "`") < 0 &&
               disruptor::small_font_width(patched, "[") < 0 && disruptor::small_font_width(patched, "@") < 0 && disruptor::small_font_width(patched, "/") < 0 &&
               disruptor::small_font_width(patched, ":") < 0 && disruptor::small_font_width(Bytes(at(0x80056E80u) + 0x43, 0), "a") < 0,
           "and has none with a character the font lacks, or in an executable without the table");
    std::vector<std::string> us(20, "US"), other(20, "other");
    const std::array<int, 10> us_x = {37, 228, 228, 228, 228, 228, 24, 228, 159, 160};
    for (size_t name = 0; name < 10; ++name) {
        disruptor::put_le32(home.data() + at(0x800569F8u) + 8 * name, static_cast<uint32_t>(us_x[name]));
        disruptor::put_le32(home.data() + at(0x800569F8u) + 8 * name + 4, static_cast<uint32_t>(100 + name));
    }
    patched.assign(home.begin(), home.end());
    for (size_t glyph = 0; glyph < 62; ++glyph) patched[at(0x80056E80u) + glyph] = glyph < 26 ? 6 : glyph < 52 ? 7 : 8;
    us[10] = "Strafe Left";  /* 10 glyphs of 5 and a space: 54 wide, so it ends at 91 */
    other[10] = "links";  /* 30 wide */
    other[11] = "rechts";  /* 36 wide: it fits from 228 */
    other[12] = std::string(16, 'A');  /* 112 wide: it must start at 208 to end on the screen */
    us[16] = "Weapon Select";  /* 64 wide, ends at 88 */
    other[16] = std::string(15, 'B');  /* 105 wide: wider than the room left of the pad */
    other[13] = std::string(13, 'A') + "a";  /* 97 wide: it ends at the screen's edge from 223 */
    us[18] = "abc";
    other[18] = "ab";
    us[19] = "abc";
    other[19] = "ab";
    expect(disruptor::place_control_names(patched, home, us, other), "the names must be placed");
    const auto x_of = [&](size_t name) { return static_cast<int32_t>(disruptor::le32(patched.data() + at(0x800569F8u) + 8 * name)); };
    expect(x_of(0) == 37 + 54 - 30 && x_of(6) == 0, "a name left of the pad ends where the US one does, and starts no further left than the screen");
    expect(x_of(1) == 228 && x_of(2) == 320 - 112 && x_of(3) == 223 && x_of(4) == 228,
           "a name right of it starts where the US one does, or as far left as it needs to end on the screen");
    expect(x_of(8) == 159 + 15 - 12 && x_of(9) == 160, "the middle of the screen parts the two sides");
    for (size_t name = 0; name < 10; ++name)
        expect(disruptor::le32(patched.data() + at(0x800569F8u) + 8 * name + 4) == 100 + name, "no name changes its row");
    size_t changed = 0;
    for (size_t index = 0; index < home.size(); ++index) changed += patched[index] != home[index] && (index < at(0x80056E80u) || index >= at(0x80056E80u) + 62);
    expect(changed == 5, "and nothing but the five places that move changes");
    other[15] = std::string(36, 'A');  /* 252 wide */
    disruptor::put_le32(home.data() + at(0x800569F8u) + 8 * 7, 0x80000000u);
    disruptor::put_le32(home.data() + at(0x800569F8u) + 8 * 4, 0x7FFFFFFFu);
    expect(disruptor::place_control_names(patched, home, us, other) && x_of(5) == 320 - 252 && x_of(7) == 0 && x_of(4) == 320 - 30,
           "a name as wide as the raster it is drawn into is placed, and a US place at either end of what 32 bits hold is taken without a sum that wraps");
    other[15] = std::string(36, 'A') + "a";  /* 258 wide */
    expect(!disruptor::place_control_names(patched, home, us, other), "a name wider than the raster it is drawn into is not placed");
    other[15] = std::string(35, 'A') + "aa";  /* 257 wide */
    expect(!disruptor::place_control_names(patched, home, us, other), "by one pixel either");
    other[15] = std::string(36, 'A') + " ";  /* 256 wide */
    expect(disruptor::place_control_names(patched, home, us, other) && x_of(5) == 320 - 256, "one exactly as wide is");
    other[15] = "sp-ringen";
    expect(!disruptor::place_control_names(patched, home, us, other), "a name with a character the small font lacks is not placed");
    other[15] = "springen";
    us[15] = "Jump!";
    expect(!disruptor::place_control_names(patched, home, us, other), "nor is one whose US name has such a character");
    us[15] = "Jump";
    Bytes cut(at(0x800569F8u) + 3, 0);
    expect(!disruptor::place_control_names(patched, cut, us, other) && !disruptor::place_control_names(patched, home, std::vector<std::string>(19, "a"), other) &&
               !disruptor::place_control_names(patched, home, us, std::vector<std::string>(19, "a")) && disruptor::place_control_names(patched, home, us, other),
           "an executable without the places, or a menu with fewer than twenty strings, places nothing");
}

struct Read {
    uint8_t tag;
    Bytes data;
    uint32_t minute_second_frame;
};

Read read(disruptor::LanguageDisc &disc, uint32_t lba) {
    uint8_t raw[kRawSector];
    if (!disc.raw_sector(lba, raw)) return {0, {}, 0};
    return {raw[16], Bytes(raw + 24, raw + 24 + kSectorData), static_cast<uint32_t>(raw[12]) << 16 | static_cast<uint32_t>(raw[13]) << 8 | raw[14]};
}

void test_strings_fit_the_menu_raster() {
    const size_t table = 0x80056EC4u - 0x80010000u + 0x800u;
    Bytes exe(0x62000, 0);
    std::fill_n(exe.begin() + static_cast<std::ptrdiff_t>(table), 48, uint8_t{10});
    exe[table + 9] = 4;                /* the glyph a sign is put in */
    exe[table + 10 + ('I' - 'A')] = 6;
    exe[table + 36 + 7] = 11;          /* the widest digit */
    disruptor::Language language{};
    language.more_signs = 4;
    language.signs = {{'k', "j"}, {'n', "SS"}};
    const auto reach = [&](std::string_view text) { return disruptor::large_font_reach(exe, text, language); };
    expect(reach("") == 0 && reach("   ") == 0 && reach("A") == 12 && reach("AB") == 22 && reach("A B") == 28,
           "a glyph is written twelve bytes wide where the widths before it end, and a space is six");
    expect(reach("I") == 12 && reach("IA") == 18 && reach("A7") == 22 && reach("7A") == 23 && reach("!") == 12,
           "a narrow glyph still writes twelve bytes, a digit and a mark below the capitals have glyphs of their own");
    expect(reach("kA") == 16 && reach("n") == 22 && reach("\x90") == 12 && reach("\x91") == -1,
           "a sign is measured as it is drawn, and a character past the last glyph has no width");
    expect(reach("A%d") == 33 && reach("A%sB") == 22 && reach("A\x01" "B") == 22 && reach("A%") == 22,
           "a number counts as two of the widest digit, a format's other parts as nothing, and a last % is a character");
    expect(disruptor::large_font_reach(Bytes(0x1000, 0), "A", language) == -1, "an executable without the widths measures nothing");
    const std::string full = std::string(24, 'A') + "kA", past = std::string(23, 'A') + "7kA";
    expect(reach(full) == 256 && reach(past) == 257 && disruptor::fits_text_raster(exe, full, language) &&
               !disruptor::fits_text_raster(exe, past, language) && !disruptor::fits_text_raster(exe, "\x91", language),
           "a string fits when its last glyph ends inside the 256 bytes of the raster, and not a byte later");
    exe[table + 10 + ('N' - 'A')] = 0xF8;
    expect(reach("AANA") == 32 && reach("NA") == -1 && reach("N A") == -1 && reach("N") == 12,
           "a glyph that steps back does not take the reach back with it, and one that would start before the raster has no place");
    exe[table + 10 + ('N' - 'A')] = 10;

    Bytes home(0x2000, 0), other(0x2000, 0), pack;
    disruptor::Language two{};
    two.strings = {{0x80010100u, 0x80010200u}, {0x80010140u, 0x80010240u}};
    two.more_signs = 4;
    put_string(home, 0x80010100u, "SMALL");
    put_string(other, 0x80010200u, std::string(40, 'x'));
    put_string(home, 0x80010140u, "LARGE");
    put_string(other, 0x80010240u, std::string(20, 'A'));
    std::string refusal;
    expect(disruptor::build_pack(home, other, two, pack, refusal, &exe), "a string of another font is not measured, however long");
    put_string(other, 0x80010240u, std::string(26, 'A'));
    expect(!disruptor::build_pack(home, other, two, pack, refusal, &exe) && refusal.find("the string \"" + std::string(26, 'A') + "\" is wider") == 0,
           "a string of the large font that would wrap round is refused and named");
    expect(disruptor::build_pack(home, other, two, pack, refusal), "and is built where no executable is given to measure with");

    std::vector<std::string> texts(79, "A");
    std::string why;
    texts[12] = std::string(40, 'A');  /* drawn in the small font */
    texts[30] = "zzzz" + std::string(40, 'A');  /* a small letter the large font has no sign for */
    expect(disruptor::menu_strings_fit(exe, texts, language, why) && why.empty(), "strings of another font are not measured");
    texts[60] = " " + full;
    texts[61] = " " + full;
    expect(!disruptor::menu_strings_fit(exe, texts, language, why) && why.find("menu string 60, \" AAAA") == 0 && why.find("is wider") != std::string::npos,
           "the first menu string that would wrap round is named");
    texts.resize(60);
    expect(disruptor::menu_strings_fit(exe, texts, language, why), "a shorter list is measured as far as it goes");

    /* The French entry's longest menu string has 24 letters and its longest executable string 18. */
    Discs fitting, menu_wide, string_wide;
    const size_t in_home = 24 * kSectorData + table + 10;
    make_discs(fitting);
    make_discs(menu_wide);
    make_discs(string_wide);
    std::fill_n(fitting.home.data.begin() + static_cast<std::ptrdiff_t>(in_home), 26, uint8_t{8});
    std::fill_n(menu_wide.home.data.begin() + static_cast<std::ptrdiff_t>(in_home), 26, uint8_t{11});
    std::fill_n(string_wide.home.data.begin() + static_cast<std::ptrdiff_t>(in_home), 26, uint8_t{127});
    disruptor::LanguageDisc disc;
    expect(disc.build(fitting.home, fitting.other, why), "a disc whose strings fit their raster is laid out");
    expect(!disc.build(menu_wide.home, menu_wide.other, why) && why.find("menu string ") == 0 && why.find("is wider") != std::string::npos,
           "a menu string that would wrap round refuses the disc");
    expect(!disc.build(string_wide.home, string_wide.other, why) && why.find("the string \"FR") == 0 && why.find("is wider") != std::string::npos,
           "so does a string of the executable, with the widths the laid out executable has");
}

void test_the_two_discs_become_one() {
    Discs discs;
    make_discs(discs);
    disruptor::LanguageDisc disc;
    std::string why;
    expect(disc.build(discs.home, discs.other, why), "two fitting discs must be laid out");
    if (disc.sectors() == 0) return;
    const uint32_t home_sectors = static_cast<uint32_t>(discs.home_wad.size() / kSectorData);
    const uint32_t wad_sectors = home_sectors - 1 /* level */ + 1 /* speech */ - 1 /* movie sound */;
    const uint32_t licence = 220 + wad_sectors, movie0 = licence + 1, movie3 = movie0 + 5, tail = movie3 + 3;
    expect(disc.sectors() == tail + 2, "the disc is as long as its files and the US disc's tail");
    expect(read(disc, 0).tag == 'U' && read(disc, 219).tag == 'U' && read(disc, tail).data[0] == 0x7E && !read(disc, tail + 2).tag,
           "what is before WAD.IN and after the last file is the US disc's, and nothing is past the end");
    expect(read(disc, 0).minute_second_frame == 0x000200 && read(disc, 74).minute_second_frame == 0x000274 && read(disc, 300).minute_second_frame == 0x000600,
           "each sector carries the address of where it lands");

    discs.home.put(discs.home.sectors(), Bytes(4 * kSectorData, 0x33));
    expect(!read(disc, disc.sectors()).tag && read(disc, disc.sectors() - 1).tag == 'U', "the disc ends where it was laid out to end, whatever the images hold after");
    const Bytes descriptor = read(disc, 16).data, root = read(disc, 22).data;
    expect(disruptor::le32(descriptor.data() + 80) == disc.sectors() && descriptor[84 + 3] == static_cast<uint8_t>(disc.sectors()), "the volume has the new length, both ways round");
    const auto big = [](const uint8_t *at) { return static_cast<uint32_t>(at[0]) << 24 | static_cast<uint32_t>(at[1]) << 16 | static_cast<uint32_t>(at[2]) << 8 | at[3]; };
    const auto record = [&](const char *name) {
        const std::string wanted = std::string(name) + ";1";
        for (size_t at = 0; root[at] != 0; at += root[at])
            if (std::string(reinterpret_cast<const char *>(root.data() + at + 33), root[at + 32]) == wanted)
                return std::array<uint32_t, 4>{disruptor::le32(root.data() + at + 2), disruptor::le32(root.data() + at + 10), big(root.data() + at + 6), big(root.data() + at + 14)};
        return std::array<uint32_t, 4>{0, 0, 0, 0};
    };
    expect(record("SLUS_002.24")[0] == 24 && record("WAD.IN") == std::array<uint32_t, 4>{220, wad_sectors * kSectorData, 220, wad_sectors * kSectorData},
           "the executable stays, WAD.IN has its new size");
    expect(record("LICENSEA.DAT")[0] == licence && record("LICENSEA.DAT")[1] == 0x700 && record("MOVIE0.STR")[0] == movie0 && record("MOVIE0.STR")[1] == 0x2800 &&
               record("MOVIE3.STR")[0] == movie3 && record("MOVIE3.STR")[1] == 0x1800,
           "the files after it move, the movies with the other disc's lengths");
    expect(read(disc, licence).tag == 'U' && read(disc, movie0).tag == 'F' && read(disc, movie0 + 4).data[0] == 0xA9 && read(disc, movie3).data[0] == 0xAA && read(disc, movie3 + 2).tag == 'F',
           "the licence is the US disc's, every sector of a movie the other disc's");

    const Bytes head = read(disc, 220).data;
    const auto entry = [&](uint32_t index) { return disruptor::le32(head.data() + 4 * index); };
    const auto piece_sector = [&](uint32_t index, uint32_t sector) { return read(disc, 220 + entry(index) / kSectorData + sector); };
    expect(entry(1) == 0x7800 && piece_sector(1, 0).tag == 'U' && piece_sector(1, 0).data[0] == 0x51 && entry(350) == 0xFFFFFFFFu, "a piece no language touches is the US disc's");
    expect(piece_sector(163, 0).tag == 'F' && disruptor::le32(piece_sector(163, 0).data.data() + 4) == 0x1000 && piece_sector(163, 1).tag == 'U' &&
               piece_sector(163, 0x73800 / 0x800 - 1).data[0] == 0x60,
           "a level has the other disc's header and the US pictures");
    expect(piece_sector(163, 0x73800 / 0x800).tag == 'F' && piece_sector(163, 0x73800 / 0x800).data[0] == 0xB1 && piece_sector(163, 0x73800 / 0x800 + 1).data[0] == 0xB1 &&
               piece_sector(163, 0x73800 / 0x800 + 2).tag == 'U' && piece_sector(163, 0x73800 / 0x800 + 2).data[0] == 0x62,
           "then the other disc's second part, as long as its header says, then the US disc's later parts");
    expect(entry(306) == entry(163) + 0x73800 + 0x1000 + 0x800 && piece_sector(306, 0).tag == 'U' &&
               entry(311) == entry(306) + disruptor::le32(discs.home_wad.data() + 4 * 311) - discs.home_menu,
           "the menu piece keeps its US size and place in the order");
    Bytes menu;
    for (uint32_t sector = 0; sector * kSectorData < disruptor::le32(discs.home_wad.data() + 4 * 311) - discs.home_menu; ++sector) {
        const Bytes data = piece_sector(306, sector).data;
        menu.insert(menu.end(), data.begin(), data.end());
    }
    const std::vector<disruptor::StringTable> tables = disruptor::string_tables(menu);
    expect(tables.size() == 1 && disruptor::strings_of(menu, tables[0])[0] == "FR MENU 0" && disruptor::strings_of(menu, tables[0])[67] == "FR MENU 67" && disruptor::strings_of(menu, tables[0])[73] == "SELECTIONNER LA PARTIE" &&
               disruptor::strings_of(menu, tables[0])[80] == "US MENU 80",
           "the menu read from the laid-out disc has the other strings, the stand-in for string 73 and the US credits");
    expect(piece_sector(386, 0).tag == 'F' && piece_sector(386, 0).data[0] == 0xA6 && piece_sector(386, 9).data[0] == 0xA6 && piece_sector(400, 0).data[0] == 0xA9 &&
               piece_sector(400, 9).tag == 'F',
           "a level's hint is the other disc's, whole");
    expect(piece_sector(385, 0).data[0] == 0x57 && piece_sector(401, 0).data[0] == 0x58, "the pieces around the hints stay the US disc's");
    expect(!disc.tall_hints(), "and hints of the US size are not called tall");
    const size_t second_place = disruptor::kNamePlaces - 0x80010000u + 0x800u + 8;
    expect(disruptor::le32(read(disc, 24 + static_cast<uint32_t>(second_place / kSectorData)).data.data() + second_place % kSectorData) == 320 - 100 - 4 - 4,
           "a control's name too wide to start where the US one does is moved to end on the screen");
    expect(piece_sector(311, 0).data[0] == 0x52 && piece_sector(316, 0).tag == 'F' && piece_sector(316, 2).data[0] == 0xA3 && entry(319) == entry(316) + 0x1800 &&
               piece_sector(319, 0).data[0] == 0xA4 && entry(320) == 0x6F0 && piece_sector(352, 0).data[0] == 0xA5,
           "speech is the other disc's, with its own lengths and clip sizes");
    const Bytes tables_sector = read(disc, 24 + (0x8005701Cu - 0x80010000u + 0x800u) / kSectorData).data;
    expect(tables_sector[(0x8005701Cu - 0x80010000u + 0x800u) % kSectorData] == 0x2C && read(disc, 24).tag == 'U' && read(disc, 124).data == Bytes(kSectorData, 0),
           "the executable is the US one with the other movie tables, sectors without a change untouched");
    const std::vector<PackEntry> pack = unpack(disc.pack());
    expect(pack.size() == disruptor::languages().front().strings.size() && pack[0].words.rfind("FR", 0) == 0, "the pack pairs the two executables' strings");
    expect(g_unpacked_signs.size() == 1 && g_unpacked_signs[0].written == 'k', "and names the apostrophe's sign");
}

void test_a_german_disc_is_laid_out() {
    Discs discs;
    make_discs(discs, true, "SLES_005.65");
    disruptor::LanguageDisc disc;
    std::string why;
    expect(disc.build(discs.home, discs.other, why), "a German disc must be laid out");
    expect(!unpack(disc.pack()).empty() && g_unpacked_signs.size() == 4 && g_unpacked_signs[3].drawn == "SS", "with a pack that names its four signs");
}

void test_a_disc_without_words_gives_movies_and_speech() {
    Discs discs;
    make_discs(discs, true, "SLPS_008.04");
    disruptor::LanguageDisc disc;
    std::string why;
    expect(disc.build(discs.home, discs.other, why) && disc.pack().empty(), "a Japanese disc must be laid out, with no pack");
    if (disc.sectors() == 0) return;
    const Bytes head = read(disc, 220).data;
    const auto piece_sector = [&](uint32_t index, uint32_t sector) { return read(disc, 220 + disruptor::le32(head.data() + 4 * index) / kSectorData + sector); };
    expect(piece_sector(163, 0).tag == 'U' && piece_sector(306, 0).tag == 'U' && piece_sector(1, 0).tag == 'U' && piece_sector(385, 0).data[0] == 0x57 && piece_sector(401, 0).data[0] == 0x58,
           "its levels and menus stay the US disc's");
    expect(piece_sector(386, 0).data[0] == 0xA6 && piece_sector(386, 19).data[0] == 0xA6 && piece_sector(400, 0).tag == 'F' && piece_sector(400, 19).data[0] == 0xA9 &&
               disruptor::le32(head.data() + 4 * 400) == disruptor::le32(head.data() + 4 * 386) + 0xA000 && disc.tall_hints(),
           "its hints are taken, twice as long as the US ones, and the disc says so");
    bool menu_untouched = true;
    for (uint32_t sector = 0; sector * kSectorData < disruptor::le32(discs.home_wad.data() + 4 * 311) - discs.home_menu; ++sector) {
        uint8_t raw[kRawSector];
        discs.home.raw_sector(220 + discs.home_menu / kSectorData + sector, raw);
        menu_untouched = menu_untouched && piece_sector(306, sector).data == Bytes(raw + 24, raw + 24 + kSectorData);
    }
    expect(menu_untouched, "with not a byte of the menu changed");
    expect(piece_sector(316, 0).tag == 'F' && piece_sector(316, 2).data[0] == 0xA3 && piece_sector(352, 0).data[0] == 0xA5 && disruptor::le32(head.data() + 4 * 320) == 0x6F0,
           "its speech is taken, with its own lengths and clip sizes");
    const size_t tables = 0x8005701Cu - 0x80010000u + 0x800u, second_place = disruptor::kNamePlaces - 0x80010000u + 0x800u + 8;
    expect(read(disc, 24 + static_cast<uint32_t>(tables / kSectorData)).data[tables % kSectorData] == 0x2C, "and its movie tables");
    expect(disruptor::le32(read(disc, 24 + static_cast<uint32_t>(second_place / kSectorData)).data.data() + second_place % kSectorData) == 228, "the control names stay where they are");
    const uint32_t home_sectors = static_cast<uint32_t>(discs.home_wad.size() / kSectorData);
    expect(read(disc, 220 + home_sectors + 1 /* speech */ - 1 /* movie sound */ + 20 /* two hints */ + 1 /* licence */).tag == 'F', "and every movie");
    Discs bare;
    make_discs(bare, true, "SLPS_008.04");
    for (uint8_t *bare_head : {bare.home.data.data() + 220 * kSectorData, bare.other.data.data() + 230 * kSectorData})
        for (const uint32_t index : {386u, 400u}) disruptor::put_le32(bare_head + 4 * index, 0);
    expect(disc.build(bare.home, bare.other, why) && !disc.tall_hints(), "discs that both have no hint in their tables lay out, with no tall hints to show");
    expect(disc.build(discs.home, discs.other, why) && disc.tall_hints(), "the discs with hints have them tall again");
    Discs unknown;
    make_discs(unknown, true, "SLES_999.99");
    expect(!disc.build(unknown.home, unknown.other, why) && !disc.tall_hints(), "a refused layout after it has no tall hints");
    expect(disc.build(discs.home, discs.other, why) && disc.tall_hints(), "the same discs laid out again have them again");
    Discs french;
    make_discs(french);
    expect(disc.build(french.home, french.other, why) && !disc.tall_hints(), "a layout after it forgets that its hints were tall");
}

size_t record_of(const MemoryImage &image, const std::string &name);

void test_discs_that_do_not_fit_are_refused() {
    std::string why;
    disruptor::LanguageDisc disc;
    {
        Discs discs;
        make_discs(discs, true, "SLES_999.99");
        expect(!disc.build(discs.home, discs.other, why) && disc.pack().empty(), "a disc of an unknown version is refused");
    }
    {
        Discs discs;
        make_discs(discs, false);
        expect(!disc.build(discs.home, discs.other, why), "a disc without one of the movies is refused");
    }
    {
        Discs discs;
        make_discs(discs);
        disruptor::put_le32(discs.other.data.data() + 230 * kSectorData + 4 * 319, 0);
        expect(!disc.build(discs.home, discs.other, why), "a WAD.IN cut differently is refused");
    }
    {
        Discs discs;
        make_discs(discs);
        disruptor::put_le32(discs.other.data.data() + 230 * kSectorData + discs.other_level + 4, 0x1001);
        expect(!disc.build(discs.home, discs.other, why), "a level whose second part is not whole sectors is refused");
    }
    {
        Discs discs;
        make_discs(discs);
        MemoryImage empty('E');
        empty.put(40, Bytes(kSectorData, 0));
        expect(!disc.build(discs.home, empty, why) && !disc.build(empty, discs.other, why), "an image that is no disc of the game is refused");
    }
    {
        Discs discs;
        make_discs(discs);
        const size_t record = record_of(discs.other, "WAD.IN");
        disruptor::put_le32(discs.other.data.data() + record + 10, static_cast<uint32_t>(discs.other_wad.size()) + kSectorData);
        expect(!disc.build(discs.home, discs.other, why) && why.find("hint") != std::string::npos, "a hint that is not the size the US code loads is refused");
    }
    for (const size_t hint_bytes : {size_t{0x4800}, size_t{0x5800}, size_t{0x800}}) {
        Discs discs;
        make_discs(discs, true, "SLES_005.64", hint_bytes);
        expect(!disc.build(discs.home, discs.other, why) && why.find("hint") != std::string::npos, "hints that are as long on both discs but not what the US code loads are refused");
    }
    for (const auto &[boot, other_hint_bytes] : {std::pair<const char *, size_t>{"SLPS_008.04", 0x5000}, {"SLPS_008.04", 0xA800}, {"SLES_005.64", 0xA000}}) {
        Discs discs;
        make_discs(discs, true, boot, 0x5000, other_hint_bytes);
        expect(!disc.build(discs.home, discs.other, why) && why.find("hint") != std::string::npos && !disc.tall_hints(),
               "a disc known for tall hints with others, and one known for US-sized hints with tall ones, are refused");
    }
    {
        Discs discs;
        make_discs(discs);
        uint8_t *home_head = discs.home.data.data() + 220 * kSectorData, *other_head = discs.other.data.data() + 230 * kSectorData;
        disruptor::put_le32(home_head + 4 * 401, disruptor::le32(home_head + 4 * 386));
        disruptor::put_le32(other_head + 4 * 401, disruptor::le32(other_head + 4 * 386));
        expect(!disc.build(discs.home, discs.other, why), "a hint that shares its piece with an entry that is none is refused");
    }
    for (const uint32_t index : {163u, 306u, 319u, 386u}) {
        Discs discs;
        make_discs(discs);
        disruptor::put_le32(discs.home.data.data() + 220 * kSectorData + 4 * index, 0);
        expect(!disc.build(discs.home, discs.other, why), "a US disc that lacks the offset of a level, a menu or speech the other disc has is refused");
    }
    {
        Discs discs;
        make_discs(discs);
        disruptor::put_le32(discs.home.data.data() + 220 * kSectorData + 4 * 163, 0);
        disruptor::put_le32(discs.other.data.data() + 230 * kSectorData + 4 * 163, 0);
        expect(disc.build(discs.home, discs.other, why), "two discs that both lack a level still lay out");
        disruptor::put_le32(discs.home.data.data() + 220 * kSectorData + 4 * 306, 0);
        expect(!disc.build(discs.home, discs.other, why), "a US disc that lacks the menu's offset is refused, whatever piece its bytes fall into");
    }
    {
        Discs discs;
        make_discs(discs);
        disruptor::put_le32(discs.other.data.data() + 230 * kSectorData + 4 * 311, 0);
        expect(disc.build(discs.home, discs.other, why), "a piece no language touches may be cut differently: it is the US disc's own");
    }
    for (const uint32_t second : {0x1801u, 0x2800u, 0x10000000u, 0xFFF8C800u}) {
        Discs discs;
        make_discs(discs);
        disruptor::put_le32(discs.home.data.data() + 220 * kSectorData + discs.home_level + 4, second);
        expect(!disc.build(discs.home, discs.other, why), "a US level whose second part is not whole sectors or longer than the piece is refused, also by a size that wraps");
    }
    for (const uint32_t second : {0x2800u, 0xFFFFF800u}) {
        Discs discs;
        make_discs(discs);
        disruptor::put_le32(discs.other.data.data() + 230 * kSectorData + discs.other_level + 4, second);
        expect(!disc.build(discs.home, discs.other, why), "so is a level of the other disc whose second part leaves no room for its pictures");
    }
}

size_t record_of(const MemoryImage &image, const std::string &name) {
    const uint8_t *root = image.data.data() + 22 * kSectorData;
    for (size_t at = 0; root[at] != 0; at += root[at])
        if (std::string(reinterpret_cast<const char *>(root + at + 33), root[at + 32]) == name + ";1") return 22 * kSectorData + at;
    return 0;
}

/* An image that says it never ends. */
class Endless final : public disruptor::DiscImage {
  public:
    explicit Endless(MemoryImage &inner) : inner_(inner) {}
    bool raw_sector(uint32_t lba, uint8_t *raw) override { return inner_.raw_sector(lba, raw); }
    uint32_t sectors() override { return 0xFFFFFFFFu; }

  private:
    MemoryImage &inner_;
};

void test_malformed_discs_are_read_within_their_bounds() {
    std::string why;
    disruptor::LanguageDisc disc;
    {
        Discs discs;
        make_discs(discs);
        discs.other.data.resize(700 * kSectorData);
        expect(!disc.build(discs.home, discs.other, why), "an image that ends before one of its files is refused");
    }
    {
        Discs discs;
        make_discs(discs);
        disruptor::put_le32(discs.other.data.data() + record_of(discs.other, "MOVIE0.STR") + 10, 0xFFFFF801u);
        Endless endless(discs.other);
        expect(!disc.build(discs.home, endless, why), "a file whose size does not round up to a sector within 32 bits is refused");
    }
    {
        Discs discs;
        make_discs(discs);
        discs.home.data[record_of(discs.home, "LICENSEA.DAT") + 32] = 60;
        expect(!disc.build(discs.home, discs.other, why), "a directory record whose name runs past it is refused");
    }
    {
        Discs discs;
        make_discs(discs);
        uint32_t used = 0;
        while (discs.home.data[22 * kSectorData + used] != 0) used += discs.home.data[22 * kSectorData + used];
        disruptor::put_le32(discs.home.data.data() + 16 * kSectorData + 166, used);
        discs.home.data[22 * kSectorData + 0x7F0] = 0x5C;
        expect(disc.build(discs.home, discs.other, why) && read(disc, 22).data[0x7F0] == 0x5C &&
                   disruptor::le32(read(disc, 22).data.data() + (record_of(discs.home, "WAD.IN") - 22 * kSectorData) + 10) != discs.home_wad.size(),
               "a directory that ends inside a sector is rewritten, and the rest of the sector kept");
        disruptor::put_le32(discs.home.data.data() + 16 * kSectorData + 166, used - 4);
        expect(!disc.build(discs.home, discs.other, why), "a directory that ends inside a record is refused");
        disruptor::put_le32(discs.home.data.data() + 16 * kSectorData + 166, used + 20);
        discs.home.data[22 * kSectorData + used] = 20;
        expect(!disc.build(discs.home, discs.other, why), "a record too short to hold a name is refused");
    }
}

}  // namespace

int main() {
    test_the_file_is_cut_at_the_table();
    test_the_menu_takes_the_other_strings();
    test_the_pack_pairs_the_two_executables();
    test_places_come_from_calls_that_pair_up();
    test_the_executable_takes_the_other_tables();
    test_the_two_discs_become_one();
    test_strings_fit_the_menu_raster();
    test_signs_go_where_the_us_routine_reaches();
    test_control_names_stand_beside_the_us_pad();
    test_a_german_disc_is_laid_out();
    test_a_disc_without_words_gives_movies_and_speech();
    test_discs_that_do_not_fit_are_refused();
    test_malformed_discs_are_read_within_their_bounds();
    if (g_failures != 0) return 1;
    std::cout << "disruptor language disc: PASS\n";
    return 0;
}
