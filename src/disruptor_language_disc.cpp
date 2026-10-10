/*
 * The game disc of Disruptor (SLUS-00224) in another language.
 *
 * The US code and data stay. From the player's disc of another region come
 * the movie files, whole, and the pieces of WAD.IN that carry the language.
 * WAD.IN starts with a table of offsets whose indexes mean the same on every
 * disc, so the file is laid out again piece by piece, each from one disc or
 * the other, and the table points at where each lands. The directory, the
 * volume size and a few tables in the executable are rewritten to match.
 * The strings of the other executable, and the x of each menu string it
 * draws elsewhere, go to the language module as a pack.
 * Nothing is written to either image: a sector is taken from one of them, or
 * from a small set of rewritten ones, when the drive asks for it.
 *
 * The other disc's large menu font may have signs the US one lacks. Their
 * pictures go where the US routine can reach them: into a glyph the US game
 * never draws, or past the font's last glyph, onto letters of the small font
 * that the menu does not draw in that language. The pack tells the language
 * module which character to give the routine for each.
 *
 * A disc whose words are not strings of this kind (the Japanese one) gives
 * its movies, its speech and its hints. Those hints are twice as tall, which
 * the layout says and the hint module acts on.
 *
 * A whole piece of a PAL disc cannot stand in where the US code knows a
 * piece's inside. The menu piece is used at fixed offsets, so it stays and
 * takes the other disc's strings and the signs its fonts lack. A level piece
 * is read as 0x73800 bytes of header and pictures and then as much as the
 * header says, so it takes the other disc's header and that second part.
 */

#include "disruptor_language_disc.h"

#include "disruptor_language_pack.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace disruptor {
namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint32_t kDataAt = 24;
constexpr uint32_t kHeadWords = 7680;
constexpr uint32_t kTableWords = 405;
constexpr uint32_t kSpeechFirst = 316, kSpeechLast = 365;
constexpr uint32_t kHintFirst = 386, kHintLast = 400;  /* a level's hint, for the loading screen after a death */
constexpr uint32_t kClipSizeFirst = 320, kClipSizeLast = 348;  /* every second entry: the byte size of a clip */
constexpr uint32_t kMenuFirst = 306, kMenuLast = 310;
constexpr uint32_t kLevelFirst = 163, kLevelLast = 177;
constexpr uint32_t kLevelHeader = 0x800;
constexpr uint32_t kLevelPictures = 0x73800;  /* the US loader reads this much before the part the header sizes */
constexpr size_t kMenuStrings = 79;  /* the menus on every disc, the credits after them differ in count and order */
constexpr size_t kFontC = 0x5F400, kGlyphC = 168, kGlyphsC = 46, kSignsC = 10;  /* menu font of the US menu piece */
constexpr size_t kGlyphB = 64, kGlyphsB = 62, kLettersB = 26;  /* the 8x8 font right after it */
constexpr size_t kWidthsC = pack_rules::kLastDrawn - 'a' + 1;  /* the large font's 46 widths and the two bytes of padding after them */
constexpr uint32_t kNamePlaces = 0x800569F8u;  /* where the ten control names are drawn beside the picture of the pad, an x and a y each */
constexpr int kMenuWidth = 320;
constexpr int kNameRaster = 256;  /* a control's name is drawn into a raster this wide before it goes on the screen */
constexpr size_t kSmallFontFirst = 10, kSmallFontLast = 19;  /* the menu strings drawn in the small font */
constexpr int kTextRaster = 256, kGlyphWide = 12, kSpaceWide = 6;  /* func_8001A46C clears a raster this wide, func_80044BDC writes each glyph's twelve bytes into it unchecked */
constexpr uint32_t kLoad = 0x80010000u, kExeHeader = 0x800u;
constexpr uint32_t kMostFileSectors = 0x1FFFFFu;  /* a file's bytes, rounded up to a sector, stay within 32 bits */
constexpr uint32_t kMovieTables = 0x8005701Cu, kMovieTable = 0x28u;  /* sound sectors, pitch, frames, top row */
constexpr std::array<uint32_t, 3> kWidths = {0x80056E40u, 0x80056E80u, 0x80056EC4u};
constexpr uint32_t kWidthsA = 0x40u, kWidthsB = 0x44u;
constexpr int kPalBottom = 252;  /* a PAL movie's top row plus its height */
constexpr char kHomeBoot[] = "SLUS_002.24";
constexpr uint32_t kMenuDraw = 0x8001A46Cu;  /* (string, x, y, colour): 0 centres, a negative x is the right edge */
constexpr size_t kCallWindow = 14;
using pack_rules::kAnyX;
constexpr size_t kTraceWindow = 32;
constexpr uint32_t kReturn = 0x03E00008u;  /* jr $ra */

struct Pair {
    uint32_t home;
    uint32_t other;
};

struct Names {
    uint32_t home, home_slot, count, other, other_slot;
};

struct Sign {
    char written;  /* the small letter the other disc writes for it in the large menu font */
    std::string drawn;  /* what the US routine is given: one character whose glyph takes the sign's picture, or letters */
};

enum class Hints { kSame, kTall };  /* as long as the US ones, or twice as tall */

struct Language {
    const char *boot;
    const char *name;
    uint32_t movie_tables;
    std::array<uint32_t, 3> widths;
    bool pal;
    bool text;  /* false: the disc's words are no strings of the US kind, so its menus, levels and executable strings are left */
    Hints hints;
    size_t more_signs;  /* how many signs the other disc's large menu font has after the ten of the US one */
    std::vector<Sign> signs;
    std::vector<Pair> strings;
    std::vector<Names> names;
    std::vector<uint32_t> second_number;  /* US formats with two numbers whose other words take only the second */
    std::map<size_t, std::string> menu_stand_ins;  /* shorter words where the US routine's 256 pixels are too few */
    std::map<uint32_t, std::string> string_stand_ins;  /* the same for strings of the executable, by US address */
    uint32_t menu_draw;
};

const std::vector<Language> &languages() {
    static const std::vector<Language> known = {
        {"SLES_005.64", "French", 0x80057A68u, {0x8005786Cu, 0x800578ACu, 0x800578F0u}, true,
         true, Hints::kSame, 1, {{'k', "j"}},
         {{0x80010078u, 0x80010068u}, {0x80010098u, 0x8001008Cu}, {0x800100B8u, 0x800100ACu}, {0x800100D8u, 0x800100CCu},
          {0x80010104u, 0x80010100u}, {0x80010118u, 0x80010118u}, {0x80010130u, 0x8001012Cu}, {0x80010140u, 0x80010140u},
          {0x80010154u, 0x80010160u}, {0x80010160u, 0x80010174u}, {0x80010180u, 0x80010190u}, {0x8001018Cu, 0x8001019Cu},
          {0x800101A8u, 0x800101BCu}, {0x800101D0u, 0x800101E4u}, {0x800101E8u, 0x8001020Cu}, {0x800101F4u, 0x80010218u},
          {0x8001020Cu, 0x80010234u}, {0x80010228u, 0x8001024Cu}, {0x80010244u, 0x80010260u}, {0x80010260u, 0x80010274u},
          {0x80010270u, 0x8001028Cu}, {0x80010280u, 0x800102A0u}, {0x80010290u, 0x80010274u}, {0x800102ACu, 0x800102C0u},
          {0x800102D0u, 0x800102ECu}, {0x800102E0u, 0x800102F8u}, {0x800102F0u, 0x80010304u}, {0x80010360u, 0x80010374u},
          {0x8001036Cu, 0x80010380u}, {0x8001037Cu, 0x80010394u}, {0x8001038Cu, 0x800103ACu}, {0x80010398u, 0x800103C4u},
          {0x800103A8u, 0x800103B8u}, {0x800103B8u, 0x800103D0u}, {0x800103C8u, 0x800103DCu}, {0x800711F0u, 0x80071CF4u},
          {0x800711F4u, 0x80071CF8u}, {0x80071200u, 0x800101D8u}, {0x80071210u, 0x80010200u}, {0x80071220u, 0x80071D14u},
          {0x80071224u, 0x80071D18u}, {0x8007123Cu, 0x80071D34u}, {0x80071260u, 0x800103B8u}},
         {{0x8005725Cu, 16, 10, 0x80057CF4u, 24}, {0x800572FCu, 10, 6, 0x80057DE4u, 16}},
         {0x80010098u, 0x800100B8u},
         {{53, "SELECTIONNER FICHIER A EFFACER"}, {61, "LECTURE DU FICHIER IMPOSSIBLE"}, {62, "CREATION FICHIER IMPOSSIBLE"},
          {63, "LECTURE DU FICHIER IMPOSSIBLE"}, {64, "SAUVEGARDE NON DISPONIBLE"}, {72, "SAUVEGARDES INACCESSIBLES"},
          {73, "SELECTIONNER LA PARTIE"}, {75, "SAUVEGARDE AUTO EN COURS"}, {76, "SAUVEGARDE AUTO REUSSIE"}, {77, "SAUVEGARDE AUTO ECHOUEE"}},
         {{0x80010098u, "REMPLACER PAR M%d f  NON"}, {0x800100B8u, "REMPLACER PAR M%d f  OUI"}, {0x80010140u, "ESSAYEZ LE MODE DUR"},
          {0x8001018Cu, "ECRASER CETTE PARTIE f"}},
         0x8001A4E0u},
        {"SLES_005.65", "German", 0x80057548u, {0x80057344u, 0x80057384u, 0x800573C8u}, true,
         true, Hints::kSame, 4, {{'k', "j"}, {'l', "\x8F"}, {'m', "\x90"}, {'n', "SS"}},
         {{0x80010078u, 0x80010068u}, {0x80010098u, 0x8001008Cu}, {0x800100B8u, 0x800100ACu}, {0x800100D8u, 0x800100C8u},
          {0x80010104u, 0x80010100u}, {0x80010118u, 0x80010114u}, {0x80010130u, 0x80010130u}, {0x80010140u, 0x80010150u},
          {0x80010154u, 0x8007173Cu}, {0x80010160u, 0x80010170u}, {0x80010180u, 0x80010194u}, {0x8001018Cu, 0x800101A0u},
          {0x800101A8u, 0x800101BCu}, {0x800101D0u, 0x800101E8u}, {0x800101F4u, 0x80010220u}, {0x8001020Cu, 0x80010220u},
          {0x80010228u, 0x80010244u}, {0x80010244u, 0x80010260u}, {0x80010260u, 0x8001027Cu}, {0x80010270u, 0x8001028Cu},
          {0x80010280u, 0x800102A4u}, {0x80010290u, 0x8001027Cu}, {0x800102ACu, 0x800102C8u}, {0x800102D0u, 0x80010314u},
          {0x800102E0u, 0x80010340u}, {0x800102F0u, 0x80010354u}, {0x80010324u, 0x80010388u}, {0x80010360u, 0x800103C4u},
          {0x8001036Cu, 0x800103D0u}, {0x8001037Cu, 0x800103E0u}, {0x8001038Cu, 0x8007179Cu}, {0x80010398u, 0x800103FCu},
          {0x800103A8u, 0x800103F0u}, {0x800103B8u, 0x80010408u}, {0x800103C8u, 0x80010414u}, {0x800711F0u, 0x80071744u},
          {0x800711F4u, 0x8007174Cu}, {0x80071200u, 0x80071758u}, {0x80071210u, 0x80010208u}, {0x80071220u, 0x80071770u},
          {0x80071224u, 0x800102D8u}, {0x8007123Cu, 0x800102FCu}, {0x80071260u, 0x800103F0u}},
         {{0x8005725Cu, 16, 10, 0x800577B0u, 16}, {0x800572FCu, 10, 6, 0x80057850u, 10}},
         {0x80010098u, 0x800100B8u},
         {{61, "MEMORY CARD NICHT LESBAR"}, {62, "KANN SPIEL NICHT SICHERN"}, {64, "SPIELSTkNDE NICHT GESICHERT"},
          {72, "KEIN ZUGRIFF AUF SPIELSTkNDE"}, {76, "AUTOSICHERN ERFOLGREICH"}, {77, "AUTOSICHERN MInLUNGEN"}},
         {{0x80010140u, "JETZT DEN SCHWEREREN MODUS"}, {0x800101F4u, "KARTE IST NICHT FORMATIERT"}, {0x8001020Cu, " "},
          {0x8007123Cu, "%s mznitiyn"}},
         0x8001A518u},
        {"SLPS_008.04", "Japanese", 0x800543DCu, {}, false, false, Hints::kTall, 0, {}, {}, {}, {}, {}, {}, 0},
    };
    return known;
}

uint32_t le32(const uint8_t *at) {
    return static_cast<uint32_t>(at[0]) | static_cast<uint32_t>(at[1]) << 8 | static_cast<uint32_t>(at[2]) << 16 |
           static_cast<uint32_t>(at[3]) << 24;
}

void put_le32(uint8_t *at, uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) at[byte] = static_cast<uint8_t>(value >> (8 * byte));
}

void put_be32(uint8_t *at, uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) at[byte] = static_cast<uint8_t>(value >> (8 * (3 - byte)));
}

bool user_data(DiscImage &disc, uint32_t lba, size_t bytes, Bytes &out) {
    std::array<uint8_t, kRawSector> raw{};
    out.clear();
    out.reserve(bytes + kSectorData);
    while (out.size() < bytes) {
        if (!disc.raw_sector(lba++, raw.data())) return false;
        out.insert(out.end(), raw.begin() + kDataAt, raw.begin() + kDataAt + kSectorData);
    }
    out.resize(bytes);
    return true;
}

struct File {
    uint32_t lba = 0;
    uint32_t sectors = 0;
    uint32_t bytes = 0;
    size_t record = 0;
};

struct Volume {
    uint32_t root_lba = 0;
    Bytes descriptor;
    Bytes root;
    std::map<std::string, File> files;
};

bool open_volume(DiscImage &disc, Volume &volume) {
    if (!user_data(disc, 16, kSectorData, volume.descriptor) || std::memcmp(volume.descriptor.data() + 1, "CD001", 5) != 0)
        return false;
    volume.root_lba = le32(volume.descriptor.data() + 158);
    const uint32_t root_bytes = le32(volume.descriptor.data() + 166);
    if (root_bytes == 0 || root_bytes > 16 * kSectorData) return false;
    /* Whole sectors, so that a rewritten directory sector keeps what follows a directory that ends inside it. */
    if (!user_data(disc, volume.root_lba, (root_bytes + kSectorData - 1) / kSectorData * kSectorData, volume.root)) return false;
    const uint64_t image_sectors = disc.sectors();
    for (size_t at = 0; at < root_bytes;) {
        const size_t length = volume.root[at];
        if (length == 0) {
            at = (at / kSectorData + 1) * kSectorData;
            continue;
        }
        const uint8_t *record = volume.root.data() + at;
        if (length < 34 || at + length > root_bytes || 33u + record[32] > length) return false;
        std::string name(reinterpret_cast<const char *>(record + 33), record[32]);
        if ((record[25] & 2) == 0 && name != std::string(1, '\0') && name != "\x01") {
            name = name.substr(0, name.find(';'));
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char letter) { return static_cast<char>(std::toupper(letter)); });
            const uint32_t lba = le32(record + 2), bytes = le32(record + 10);
            const uint32_t sectors = bytes / kSectorData + (bytes % kSectorData != 0 ? 1 : 0);
            if (sectors > kMostFileSectors || uint64_t{lba} + sectors > image_sectors) return false;
            volume.files[name] = {lba, sectors, bytes, at};
        }
        at += length;
    }
    return volume.files.count("WAD.IN") != 0;
}

struct Piece {
    uint32_t start = 0;
    uint32_t end = 0;
    std::vector<uint32_t> indexes;
};

bool clip_size(uint32_t index) {
    return index >= kClipSizeFirst && index <= kClipSizeLast && (index - kClipSizeFirst) % 2 == 0;
}

/* The file cut at every offset of the table, in file order, with the entries that point at each cut. */
std::vector<Piece> pieces(const std::vector<uint32_t> &head, uint32_t wad_bytes) {
    std::map<uint32_t, std::vector<uint32_t>> by_offset;
    for (uint32_t index = 0; index < kTableWords; ++index) {
        const uint32_t word = head[index];
        if (!clip_size(index) && word >= kHeadWords * 4 && word < wad_bytes && word % kSectorData == 0)
            by_offset[word].push_back(index);
    }
    std::vector<Piece> out;
    for (auto &[offset, indexes] : by_offset) {
        if (!out.empty()) out.back().end = offset;
        out.push_back({offset, wad_bytes, std::move(indexes)});
    }
    return out;
}

bool carries_language(uint32_t index) {
    return (index >= kLevelFirst && index <= kLevelLast) || (index >= kMenuFirst && index <= kMenuLast) || (index >= kSpeechFirst && index <= kSpeechLast) ||
           (index >= kHintFirst && index <= kHintLast);
}

bool within(const Piece &piece, uint32_t first, uint32_t last) {
    return std::any_of(piece.indexes.begin(), piece.indexes.end(), [=](uint32_t index) { return index >= first && index <= last; });
}

bool only_within(const Piece &piece, uint32_t first, uint32_t last) {
    return std::all_of(piece.indexes.begin(), piece.indexes.end(), [=](uint32_t index) { return index >= first && index <= last; });
}

struct StringTable {
    size_t at = 0;
    size_t count = 0;
};

/* String tables of a piece: offsets from the piece's start, the first string right after the last offset. */
std::vector<StringTable> string_tables(const Bytes &piece) {
    std::vector<StringTable> found;
    const size_t size = piece.size();
    for (size_t at = 0; at + 16 < size;) {
        const size_t first = le32(piece.data() + at);
        const size_t count = first > at ? (first - at) / 4 : 0;
        bool good = first > at && (first - at) % 4 == 0 && count >= 4 && first < size;
        size_t last = 0;
        for (size_t entry = 0; good && entry < count; ++entry) {
            const size_t offset = le32(piece.data() + at + 4 * entry);
            good = offset < size && (entry == 0 || (offset > last && piece[offset - 1] == 0));
            last = offset;
        }
        const auto end = good ? std::find(piece.begin() + static_cast<std::ptrdiff_t>(last), piece.end(), 0) : piece.end();
        if (!good || end == piece.end() || end == piece.begin()) {
            at += 4;
            continue;
        }
        found.push_back({at, count});
        at = static_cast<size_t>(end - piece.begin()) + 1;
        at += (4 - at % 4) % 4;
    }
    return found;
}

std::vector<std::string> strings_of(const Bytes &piece, const StringTable &table) {
    std::vector<std::string> out;
    for (size_t entry = 0; entry < table.count; ++entry)
        out.emplace_back(reinterpret_cast<const char *>(piece.data() + le32(piece.data() + table.at + 4 * entry)));
    return out;
}

/* Whether a string can be one for the large menu font: a small letter is a sign there, so one the font has no sign for rules it out. */
bool for_large_font(std::string_view text, const Language &language) {
    const char signs_end = static_cast<char>('a' + kSignsC + language.more_signs);
    for (size_t at = 0; at < text.size(); ++at) {
        if (text[at] == '%') ++at;  /* the letter of a part is no letter of the string */
        else if (text[at] >= signs_end && text[at] <= 'z') return false;
    }
    return true;
}

/* The glyph of the US large font that a sign's picture is put in, or npos for a sign spelled with letters. */
size_t home_glyph(const Sign &sign) {
    const bool pictured = sign.drawn.size() == 1 && (sign.drawn[0] < 'A' || sign.drawn[0] > 'Z');
    return pictured ? static_cast<size_t>(static_cast<uint8_t>(sign.drawn[0]) - 'a') : std::string::npos;
}

/* How long a string for the large font is once its routine is given the signs' characters. */
size_t spelled_out(std::string_view text, const Language &language) {
    size_t length = text.size();
    if (!for_large_font(text, language)) return length;
    for (const Sign &sign : language.signs) length += (sign.drawn.size() - 1) * static_cast<size_t>(std::count(text.begin(), text.end(), sign.written));
    return length;
}

/* Whether a string for the large font already writes a character that now draws one of the other disc's signs. */
bool writes_taken_glyph(std::string_view text, const Language &language) {
    return for_large_font(text, language) && std::any_of(language.signs.begin(), language.signs.end(), [&](const Sign &sign) {
        return home_glyph(sign) != std::string::npos && text.find(sign.drawn[0]) != std::string_view::npos;
    });
}

Bytes shape(const uint8_t *glyph, size_t size) {
    Bytes out(size);
    std::transform(glyph, glyph + size, out.begin(), [](uint8_t value) { return static_cast<uint8_t>(value != 0); });
    return out;
}

/* A glyph of the other disc in the US piece's pixel values, learned from glyphs both discs draw alike. */
Bytes in_home_colours(const uint8_t *glyph, size_t size, const std::vector<std::pair<const uint8_t *, const uint8_t *>> &alike) {
    std::array<std::vector<std::pair<uint8_t, uint32_t>>, 256> votes;
    for (const auto &[mine, theirs] : alike) {
        if (shape(mine, size) != shape(theirs, size)) continue;
        for (size_t at = 0; at < size; ++at) {
            auto &seen = votes[theirs[at]];
            const auto same = std::find_if(seen.begin(), seen.end(), [&](const auto &vote) { return vote.first == mine[at]; });
            if (same == seen.end()) seen.emplace_back(mine[at], 1u);
            else ++same->second;
        }
    }
    Bytes out(size);
    for (size_t at = 0; at < size; ++at) {
        const auto &seen = votes[glyph[at]];
        const auto best = std::max_element(seen.begin(), seen.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
        out[at] = seen.empty() ? glyph[at] : best->first;
    }
    return out;
}

/* Puts the other disc's own signs into the US menu fonts: changed small letters of the small one, then the large
 * font's signs, each into the glyph the language names for it. */
bool carry_fonts(Bytes &out, const Bytes &home, const Bytes &other, const Language &language) {
    const size_t home_b = kFontC + kGlyphsC * kGlyphC, fonts_end = home_b + kGlyphsB * kGlyphB;
    if (home.size() < fonts_end) return false;
    const Bytes other_shape = shape(other.data(), other.size());
    const Bytes signs = shape(home.data() + kFontC, kSignsC * kGlyphC);
    const auto found = std::search(other_shape.begin(), other_shape.end(), signs.begin(), signs.end());
    if (found == other_shape.end()) return false;
    const size_t other_c = static_cast<size_t>(found - other_shape.begin()), extra = language.more_signs;
    const size_t other_b = other_c + (kGlyphsC + extra) * kGlyphC;
    if (other.size() < other_b + kGlyphsB * kGlyphB) return false;
    std::vector<std::pair<const uint8_t *, const uint8_t *>> large, small;
    for (size_t slot = 0; slot < kGlyphsC; ++slot)
        large.emplace_back(home.data() + kFontC + slot * kGlyphC, other.data() + other_c + (slot + (slot >= kSignsC ? extra : 0)) * kGlyphC);
    const auto differs = [](const auto &pair) { return shape(pair.first, kGlyphC) != shape(pair.second, kGlyphC); };
    if (std::any_of(large.begin() + static_cast<std::ptrdiff_t>(kSignsC), large.end(), differs)) return false;
    size_t same = 0;
    for (size_t slot = 0; slot < kGlyphsB; ++slot) {
        small.emplace_back(home.data() + home_b + slot * kGlyphB, other.data() + other_b + slot * kGlyphB);
        same += shape(small.back().first, kGlyphB) == shape(small.back().second, kGlyphB);
    }
    if (same + 8 < kGlyphsB) return false;
    for (size_t slot = 0; slot < kLettersB; ++slot) {
        if (shape(small[slot].first, kGlyphB) == shape(small[slot].second, kGlyphB)) continue;
        const Bytes letter = in_home_colours(small[slot].second, kGlyphB, small);
        std::copy(letter.begin(), letter.end(), out.begin() + static_cast<std::ptrdiff_t>(home_b + slot * kGlyphB));
    }
    for (const Sign &sign : language.signs) {
        const size_t slot = home_glyph(sign), theirs = static_cast<size_t>(sign.written - 'a');
        if (slot == std::string::npos) continue;
        if (theirs < kSignsC || theirs >= kSignsC + extra || kFontC + (slot + 1) * kGlyphC > fonts_end) return false;
        const Bytes picture = in_home_colours(other.data() + other_c + theirs * kGlyphC, kGlyphC, large);
        std::copy(picture.begin(), picture.end(), out.begin() + static_cast<std::ptrdiff_t>(kFontC + slot * kGlyphC));
    }
    return true;
}

/* The US menu piece with the other disc's menu strings and signs. */
bool menu_piece(const Bytes &home, const Bytes &other, const Language &language, Bytes &out, std::string &why, std::vector<std::string> *home_names = nullptr,
                std::vector<std::string> *names = nullptr) {
    const std::vector<StringTable> mine = string_tables(home), theirs = string_tables(other);
    if (mine.size() != 1 || theirs.size() != 1 || std::min(mine[0].count, theirs[0].count) < kMenuStrings) {
        why = "the menu's strings were not found";
        return false;
    }
    const size_t at = mine[0].at, count = mine[0].count;
    const std::vector<std::string> old = strings_of(home, mine[0]);
    std::vector<std::string> texts = strings_of(other, theirs[0]);
    texts.resize(kMenuStrings);
    texts.insert(texts.end(), old.begin() + static_cast<std::ptrdiff_t>(kMenuStrings), old.end());
    for (const auto &[index, text] : language.menu_stand_ins) texts[index] = text;
    for (size_t index = 0; index < kMenuStrings; ++index) {
        if (writes_taken_glyph(texts[index], language)) {
            why = "a menu string uses a sign whose glyph another sign is put in";
            return false;
        }
    }
    /* A sign past the large font's last glyph lies on the small font's first letters: strings 10..19, all the menu draws in that font, must do without them. */
    for (const Sign &sign : language.signs) {
        const size_t slot = home_glyph(sign);
        if (slot == std::string::npos || slot < kGlyphsC) continue;
        const char first = static_cast<char>('a' + (slot - kGlyphsC) * kGlyphC / kGlyphB), last = static_cast<char>('a' + ((slot - kGlyphsC + 1) * kGlyphC - 1) / kGlyphB);
        for (size_t index = kSmallFontFirst; index <= kSmallFontLast; ++index) {
            if (std::any_of(texts[index].begin(), texts[index].end(), [=](char letter) { return letter >= first && letter <= last; })) {
                why = "a control's name needs a letter of the small menu font that a sign is put on";
                return false;
            }
        }
    }
    const size_t first = at + 4 * count;
    const size_t old_end = le32(home.data() + at + 4 * (count - 1)) + old.back().size() + 1;
    if (std::any_of(home.begin() + static_cast<std::ptrdiff_t>(old_end), home.end(), [](uint8_t byte) { return byte != 0 && byte != 0xFF; })) {
        why = "data follows the menu's strings";
        return false;
    }
    out = home;
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(), uint8_t{0});
    size_t need = 0;
    for (const std::string &text : texts) need += text.size() + 1;
    const bool share = need + 1 > home.size() - first;
    std::map<std::string, size_t> placed;
    size_t cursor = first;
    for (size_t index = 0; index < texts.size(); ++index) {
        const std::string &text = texts[index];
        if (const auto same = placed.find(text); share && same != placed.end()) {
            put_le32(out.data() + at + 4 * index, static_cast<uint32_t>(same->second));
            continue;
        }
        if (cursor + text.size() + 2 > home.size()) {
            why = "the menu's strings do not fit";
            return false;
        }
        placed[text] = cursor;
        put_le32(out.data() + at + 4 * index, static_cast<uint32_t>(cursor));
        std::copy(text.begin(), text.end(), out.begin() + static_cast<std::ptrdiff_t>(cursor));
        cursor += text.size() + 1;
    }
    out[cursor] = 0xFF;
    if (!carry_fonts(out, home, other, language)) {
        why = "the other disc's menu fonts were not found";
        return false;
    }
    if (home_names) *home_names = old;
    if (names) *names = texts;
    return true;
}

/* How far func_80044BDC writes into its raster for a string, with this executable's widths and the signs spelled out: -1 for a character it has no width for or a glyph that starts before the raster. A number counts as two of the widest digit, a format's other parts as nothing. */
int large_font_reach(const Bytes &exe, std::string_view text, const Language &language) {
    const size_t widths = kWidths[2] - kLoad + kExeHeader;
    if (widths + kWidthsC > exe.size()) return -1;
    const uint8_t *digits = exe.data() + widths + kSignsC + 26;
    const auto widest_digit = static_cast<uint8_t>('0' + (std::max_element(digits, digits + 10, [](uint8_t one, uint8_t other) { return static_cast<int8_t>(one) < static_cast<int8_t>(other); }) - digits));
    int at = 0, reach = 0;
    const auto draw = [&](uint8_t letter) {
        if (letter == ' ') {
            at += kSpaceWide;
            return true;
        }
        const int glyph = letter >= 'a' ? letter - 'a' : letter >= 'A' ? letter - 'A' + static_cast<int>(kSignsC) : letter - 12;
        if (glyph < 0 || glyph >= static_cast<int>(kWidthsC) || at < 0) return false;
        reach = std::max(reach, at + kGlyphWide);
        at += static_cast<int8_t>(exe[widths + static_cast<size_t>(glyph)]);
        return true;
    };
    for (size_t index = 0; index < text.size(); ++index) {
        const auto letter = static_cast<uint8_t>(text[index]);
        if (letter < ' ') continue;  /* a part of a pack's string, numbered */
        if (letter == '%' && index + 1 < text.size()) {
            if (text[++index] == 'd' && !(draw(widest_digit) && draw(widest_digit))) return -1;
            continue;
        }
        const auto sign = std::find_if(language.signs.begin(), language.signs.end(), [&](const Sign &one) { return static_cast<uint8_t>(one.written) == letter; });
        const std::string drawn = sign != language.signs.end() ? sign->drawn : std::string(1, static_cast<char>(letter));
        for (const char one : drawn) {
            if (!draw(static_cast<uint8_t>(one))) return -1;
        }
    }
    return reach;
}

/* A string past the raster's edge comes out wrapped round onto its own start. */
bool fits_text_raster(const Bytes &exe, std::string_view text, const Language &language) {
    const int reach = large_font_reach(exe, text, language);
    return reach >= 0 && reach <= kTextRaster;
}

/* Whether every menu string of the large font ends inside its raster: the first that does not is named. */
bool menu_strings_fit(const Bytes &exe, const std::vector<std::string> &texts, const Language &language, std::string &why) {
    for (size_t index = 0; index < std::min(texts.size(), kMenuStrings); ++index) {
        const bool small = index >= kSmallFontFirst && index <= kSmallFontLast;
        if (small || !for_large_font(texts[index], language) || fits_text_raster(exe, texts[index], language)) continue;
        why = "menu string " + std::to_string(index) + ", \"" + texts[index] + "\", is wider than the raster the menu draws it in";
        return false;
    }
    return true;
}

std::string exe_string(const Bytes &exe, uint32_t address) {
    const size_t at = address - kLoad + kExeHeader;
    if (address < kLoad || at >= exe.size()) return {};
    const auto end = std::find(exe.begin() + static_cast<std::ptrdiff_t>(at), exe.end(), 0);
    return std::string(exe.begin() + static_cast<std::ptrdiff_t>(at), end);
}

std::string parts_of(std::string_view text) {
    std::string kinds;
    for (size_t at = 0; at + 1 < text.size(); ++at)
        if (text[at] == '%' && (text[at + 1] == 's' || text[at + 1] == 'd' || text[at + 1] == 'c')) kinds.push_back(text[at + 1]);
    return kinds;
}

struct Place {
    uint32_t returns_to;
    int32_t home_x;
    int32_t other_x;
};

struct MenuCall {
    uint32_t at = 0;
    uint32_t routine = 0;  /* how many returns precede the call: the same for calls of one routine */
    std::vector<uint32_t> shape;
    uint32_t x_word = 0;  /* the instruction that last wrote $a1, 0 when none is near */
    bool has_x = false;
    int32_t x = 0;
    uint32_t origin_word = 0;  /* where x_word copies a register: what filled it, 0 when none is near. A call's own link into $ra is not seen */
    std::vector<uint32_t> origin_shape;  /* the instructions from x_word back to that one */
};

/* An instruction with what a translation of the same code may change masked out: immediates, jump targets, and
 * which of the three ways to put a constant in a register was used. */
uint32_t shape_of(uint32_t word) {
    const uint32_t opcode = word >> 26, source = word >> 21 & 31, target = word >> 16 & 31;
    if (opcode == 2 || opcode == 3) return opcode << 26;
    if (opcode == 0 && source == 0 && target == 0 && ((word & 0x7FF) == 0x21 || (word & 0x7FF) == 0x25)) return 0x0Du << 26 | (word >> 11 & 31) << 16;
    if ((opcode == 0x09 || opcode == 0x0D) && source == 0) return 0x0Du << 26 | target << 16;
    return opcode == 0 ? word : word & 0xFFFF0000u;
}

/* jal, jalr, bltzal, bgezal: after its delay slot, what a callee may change is unknown. */
bool is_call(uint32_t word) {
    const uint32_t opcode = word >> 26;
    return opcode == 3 || (opcode == 0 && (word & 0x3F) == 9) || (opcode == 1 && (word >> 16 & 0x1E) == 0x10);
}

bool writes(uint32_t word, uint32_t reg) {
    const uint32_t opcode = word >> 26, function = word & 0x3F;
    if (opcode == 0) return (word >> 11 & 31) == reg && word != 0 && function != 8 && (function < 0x18 || function > 0x1B);
    const bool from_coprocessor = (opcode == 0x10 || opcode == 0x12) && ((word >> 21 & 31) == 0 || (word >> 21 & 31) == 2);
    const bool sets_target = (opcode >= 0x08 && opcode <= 0x0F) || (opcode >= 0x20 && opcode <= 0x26) || from_coprocessor;
    return sets_target && (word >> 16 & 31) == reg;
}

/* A constant put into a register out of nothing, one a pack's sixteen bits hold. */
bool constant_load(uint32_t word, int32_t &value) {
    const uint32_t opcode = word >> 26, source = word >> 21 & 31;
    if (source != 0) return false;
    if (opcode == 0x09) value = static_cast<int16_t>(word & 0xFFFF);
    else if (opcode == 0x0D) value = static_cast<int32_t>(word & 0xFFFF);
    else if (opcode == 0 && (word >> 16 & 31) == 0 && ((word & 0x7FF) == 0x21 || (word & 0x7FF) == 0x25)) value = 0;
    else return false;
    return value > kAnyX && value <= 32767;
}

/* The register a plain copy into $a1 takes, 0 for any other instruction. */
uint32_t copied_into_a1(uint32_t word) {
    const uint32_t source = word >> 21 & 31, target = word >> 16 & 31;
    const bool copy = word >> 26 == 0 && (word >> 11 & 31) == 5 && ((word & 0x7FF) == 0x21 || (word & 0x7FF) == 0x25) && (source == 0) != (target == 0);
    return copy ? source | target : 0;
}

/* An x made by adding a constant to a register, the same register in both executables: how far apart they are. */
bool relative_x(uint32_t mine, uint32_t theirs, int32_t &step) {
    if (mine >> 26 != 0x09 || mine >> 16 != theirs >> 16 || (mine >> 16 & 31) != 5 || (mine >> 21 & 31) == 0) return false;
    step = static_cast<int16_t>(theirs & 0xFFFF) - static_cast<int16_t>(mine & 0xFFFF);
    return step > kAnyX && step <= 32767;
}

/* A register copied into $a1 that the same code fills with a constant in both executables: how far apart they are. */
bool relative_origin(const MenuCall &mine, const MenuCall &theirs, int32_t &step) {
    int32_t from = 0, to = 0;
    if (mine.origin_shape != theirs.origin_shape || !constant_load(mine.origin_word, from) || !constant_load(theirs.origin_word, to)) return false;
    step = to - from;
    return step > kAnyX && step <= 32767;
}

/* Every call of the menu's string routine, with its shape and the x it is given when that is a constant. Where
 * the x is a copy of a register, the instruction that filled the register is looked for further back. Neither
 * search goes into the delay slot of an earlier return, nor of an earlier call unless a call keeps the register.
 * Both follow addresses, not paths: a constant set before an if and its else is found from the call after them,
 * which is how the compiler lays out the menus' own calls. */
std::vector<MenuCall> menu_calls(const Bytes &exe, uint32_t routine) {
    std::vector<MenuCall> calls;
    const uint32_t jal = 0x0C000000u | (routine >> 2 & 0x03FFFFFFu);
    const size_t words = exe.size() > kExeHeader ? (exe.size() - kExeHeader) / 4 : 0;
    const auto word = [&](size_t index) { return le32(exe.data() + kExeHeader + 4 * index); };
    uint32_t returns = 0;
    for (size_t index = kCallWindow; index + 1 < words; ++index) {
        returns += word(index) == kReturn;
        if (word(index) != jal) continue;
        MenuCall call;
        call.at = kLoad + static_cast<uint32_t>(4 * index);
        call.routine = returns;
        for (size_t at = index - kCallWindow; at <= index + 1; ++at) call.shape.push_back(shape_of(word(at)));
        for (size_t step = 0; step <= kCallWindow; ++step) {
            const size_t at = step == 0 ? index + 1 : index - step;
            if (step != 0 && (at == 0 || is_call(word(at - 1)) || word(at - 1) == kReturn)) break;
            if (writes(word(at), 5)) {
                call.x_word = word(at);
                call.has_x = constant_load(word(at), call.x);
                const uint32_t copied = copied_into_a1(word(at));
                const bool kept_by_calls = (copied >= 16 && copied <= 23) || copied == 30;
                for (size_t back = 1; copied != 0 && back <= kTraceWindow && back < at; ++back) {
                    const uint32_t earlier = word(at - back), before = word(at - back - 1);
                    if (before == kReturn || (is_call(before) && !kept_by_calls)) break;
                    call.origin_shape.push_back(shape_of(earlier));
                    if (writes(earlier, copied)) {
                        call.origin_word = earlier;
                        break;
                    }
                }
                break;
            }
        }
        calls.push_back(std::move(call));
    }
    return calls;
}

/* Where the other executable draws a menu string at another x: its calls are lined up with the US ones by shape.
 * Between two stretches of calls that pair up, each executable may have calls the other lacks. A stretch is
 * trusted only when both have as many of those on either side of it, so that order pairs them too. Within
 * it the calls of one routine move together or not at all: where one of them takes an x that is neither a
 * constant, nor a constant added to a register, nor a copy of a register the same code fills with a constant,
 * the two executables may make it from other numbers, and moving its neighbours alone would pull a screen apart. */
std::vector<Place> other_places(const Bytes &home, const Bytes &other, uint32_t other_routine) {
    const std::vector<MenuCall> mine = menu_calls(home, kMenuDraw), theirs = menu_calls(other, other_routine);
    std::vector<std::vector<uint16_t>> longest(mine.size() + 1, std::vector<uint16_t>(theirs.size() + 1, 0));
    for (size_t i = mine.size(); i-- > 0;)
        for (size_t j = theirs.size(); j-- > 0;)
            longest[i][j] = mine[i].shape == theirs[j].shape ? static_cast<uint16_t>(longest[i + 1][j + 1] + 1) : std::max(longest[i + 1][j], longest[i][j + 1]);
    std::vector<Place> places, stretch, routine;
    size_t unpaired_mine = 0, unpaired_theirs = 0;
    uint32_t in_routine = 0;
    bool trusted = true, whole = true;
    const auto end_routine = [&] {
        if (whole) stretch.insert(stretch.end(), routine.begin(), routine.end());
        routine.clear();
        whole = true;
    };
    const auto close = [&](bool gap_is_even) {
        end_routine();
        if (trusted && gap_is_even) places.insert(places.end(), stretch.begin(), stretch.end());
        stretch.clear();
        trusted = gap_is_even;
    };
    for (size_t i = 0, j = 0; i < mine.size() || j < theirs.size();) {
        const bool pair = i < mine.size() && j < theirs.size() && mine[i].shape == theirs[j].shape;
        if (pair && (unpaired_mine != 0 || unpaired_theirs != 0)) {
            close(unpaired_mine == unpaired_theirs);
            unpaired_mine = unpaired_theirs = 0;
        }
        if (pair) {
            if (mine[i].routine != in_routine) end_routine();
            in_routine = mine[i].routine;
            int32_t step = 0;
            if (mine[i].has_x && theirs[j].has_x) {
                if (mine[i].x != theirs[j].x) routine.push_back({mine[i].at + 8, mine[i].x, theirs[j].x});
            } else if (relative_x(mine[i].x_word, theirs[j].x_word, step) || relative_origin(mine[i], theirs[j], step)) {
                if (step != 0) routine.push_back({mine[i].at + 8, kAnyX, step});
            } else {
                whole = false;
            }
            ++i;
            ++j;
        } else if (j == theirs.size() || (i < mine.size() && longest[i + 1][j] >= longest[i][j + 1])) {
            ++i;
            ++unpaired_mine;
        } else {
            ++j;
            ++unpaired_theirs;
        }
    }
    close(unpaired_mine == unpaired_theirs);
    return places;
}

/* The other executable's strings for the strings of the US one, in the pack the language module takes. */
bool build_pack(const Bytes &home, const Bytes &other, const Language &language, Bytes &pack, std::string &why, const Bytes *drawn_with = nullptr) {
    std::map<uint32_t, uint32_t> pairs;
    for (const Pair &pair : language.strings) pairs[pair.home] = pair.other;
    for (const Names &names : language.names) {
        for (uint32_t slot = 0; slot < names.count; ++slot) {
            const uint32_t mine = names.home + names.home_slot * slot, theirs = names.other + names.other_slot * slot;
            if (!exe_string(home, mine).empty() && !exe_string(other, theirs).empty()) pairs[mine] = theirs;
        }
    }
    std::map<std::string, std::string> said;
    Bytes body;
    uint32_t count = 0;
    for (const auto &[address, other_address] : pairs) {
        const std::string text = exe_string(home, address);
        const auto stand_in = language.string_stand_ins.find(address);
        std::string words = stand_in != language.string_stand_ins.end() ? stand_in->second : exe_string(other, other_address);
        if (text.empty() || words.empty() || writes_taken_glyph(words, language)) {
            why = "a string of one of the executables is not where it is expected";
            return false;
        }
        if (drawn_with && for_large_font(words, language) && !fits_text_raster(*drawn_with, words, language)) {
            why = "the string \"" + words + "\" is wider than the raster the menu draws it in";
            return false;
        }
        const std::string kinds = parts_of(text);
        if (!kinds.empty()) {
            const std::string other_kinds = parts_of(words);
            const bool second_only = std::find(language.second_number.begin(), language.second_number.end(), address) != language.second_number.end();
            std::string choice;
            for (size_t part = 0; part < other_kinds.size(); ++part) choice.push_back(static_cast<char>(second_only ? 2 : part + 1));
            bool good = std::count(kinds.begin(), kinds.end(), 's') <= 1 && choice.size() == other_kinds.size();
            for (size_t part = 0; good && part < choice.size(); ++part)
                good = static_cast<size_t>(choice[part]) <= kinds.size() && kinds[static_cast<size_t>(choice[part]) - 1] == other_kinds[part];
            if (!good) {
                why = "a format of the two executables does not take the same parts";
                return false;
            }
            std::string numbered;
            size_t part = 0;
            for (size_t at = 0; at < words.size(); ++at) {
                const bool specifier = words[at] == '%' && at + 1 < words.size() && (words[at + 1] == 's' || words[at + 1] == 'd' || words[at + 1] == 'c');
                numbered.push_back(specifier ? choice[part++] : words[at]);
                at += specifier ? 1 : 0;
            }
            words = numbered;
        }
        if (const auto [earlier, fresh] = said.emplace(text, words); !fresh && earlier->second != words) {
            why = "one US string is given two different strings";
            return false;
        }
        if (text == words) continue;
        pack_rules::Format format;
        if (!pack_rules::entry_fits(text, words, format) || spelled_out(words, language) > static_cast<size_t>(pack_rules::kLongestString)) {
            why = "a string is not one the language module takes";
            return false;
        }
        const size_t at = body.size();
        body.resize(at + 10);
        put_le32(body.data() + at, address);
        put_le32(body.data() + at + 4, pack_rules::hash_of(text));
        body[at + 8] = static_cast<uint8_t>(words.size());
        body[at + 9] = static_cast<uint8_t>(words.size() >> 8);
        body.insert(body.end(), words.begin(), words.end());
        ++count;
    }
    const std::vector<Place> places = other_places(home, other, language.menu_draw);
    if (count > pack_rules::kMostStrings || places.size() > pack_rules::kMostPlaces) {
        why = "the executables differ in more strings or places than a pack holds";
        return false;
    }
    std::set<char> written;
    const auto taken = [&](const Sign &sign) {
        const size_t theirs = static_cast<size_t>(sign.written - 'a');
        return theirs >= kSignsC && theirs < kSignsC + language.more_signs && written.insert(sign.written).second &&
               pack_rules::sign_fits(static_cast<uint8_t>(sign.written), sign.drawn);
    };
    if (language.signs.size() > pack_rules::kMostSigns || !std::all_of(language.signs.begin(), language.signs.end(), taken)) {
        why = "a sign of the large font is not one the language module takes";
        return false;
    }
    pack.clear();
    if (count == 0 && (!language.signs.empty() || !places.empty())) {
        why = "the other disc has signs or places of its own and no string for a pack to carry them";
        return false;
    }
    if (count == 0) return true;  /* the module takes no pack without strings */
    pack.assign(8, 0);
    put_le32(pack.data(), pack_rules::kMagic);
    put_le32(pack.data() + 4, count);
    pack.insert(pack.end(), body.begin(), body.end());
    pack.resize(pack.size() + 4 + 8 * places.size());
    uint8_t *at = pack.data() + pack.size() - 4 - 8 * places.size();
    put_le32(at, static_cast<uint32_t>(places.size()));
    for (const Place &place : places) {
        at += 8;
        put_le32(at - 4, place.returns_to);
        put_le32(at, static_cast<uint32_t>(place.home_x & 0xFFFF) | static_cast<uint32_t>(place.other_x & 0xFFFF) << 16);
    }
    const size_t signs_at = pack.size();
    pack.resize(signs_at + 4 + 4 * language.signs.size());
    put_le32(pack.data() + signs_at, static_cast<uint32_t>(language.signs.size()));
    for (size_t index = 0; index < language.signs.size(); ++index) {
        const Sign &sign = language.signs[index];
        uint8_t *entry = pack.data() + signs_at + 4 + 4 * index;
        entry[0] = static_cast<uint8_t>(sign.written);
        entry[1] = static_cast<uint8_t>(sign.drawn.size());
        std::copy(sign.drawn.begin(), sign.drawn.end(), entry + 2);
    }
    return true;
}

/* The tables of the other executable that go with its movies and fonts, written into the US executable. */
bool patch_executable(Bytes &home, const Bytes &other, const Language &language) {
    const auto at = [](uint32_t address) { return static_cast<size_t>(address - kLoad + kExeHeader); };
    const auto copy = [&](uint32_t to, uint32_t from, uint32_t size) {
        if (at(to) + size > home.size() || at(from) + size > other.size()) return false;
        std::copy_n(other.begin() + static_cast<std::ptrdiff_t>(at(from)), size, home.begin() + static_cast<std::ptrdiff_t>(at(to)));
        return true;
    };
    if (!copy(kMovieTables, language.movie_tables, 4 * kMovieTable)) return false;
    if (language.text) {
        if (!copy(kWidths[0], language.widths[0], kWidthsA) || !copy(kWidths[1], language.widths[1], kWidthsB)) return false;
        for (const Sign &sign : language.signs) {
            const size_t slot = home_glyph(sign);
            if (slot == std::string::npos) continue;
            const uint32_t width = kWidths[2] + static_cast<uint32_t>(slot);
            /* A glyph past the font's last has a width only where the US table is padded, and only while that is padding. */
            const bool room = slot < kGlyphsC || (slot < kWidthsC && home[at(width)] == 0);
            if (!room || !copy(width, language.widths[2] + static_cast<uint32_t>(sign.written - 'a'), 1)) return false;
        }
    }
    if (language.pal) {
        uint8_t *tops = home.data() + at(kMovieTables + 3 * kMovieTable);
        for (size_t movie = 0; movie < kMovieTable / 2; ++movie) {
            const int top = tops[2 * movie] | tops[2 * movie + 1] << 8;
            const int centred = top != 0 ? (top - kPalBottom + 240) / 2 : 0;
            tops[2 * movie] = static_cast<uint8_t>(centred);
            tops[2 * movie + 1] = static_cast<uint8_t>(centred >> 8);
        }
    }
    return true;
}

/* How wide func_80044AE8 draws a string in the small menu font with this executable's widths, or -1 for one it has no glyph or no room for. */
int small_font_width(const Bytes &exe, std::string_view text) {
    const size_t widths = kWidths[1] - kLoad + kExeHeader;
    if (widths + kWidthsB > exe.size()) return -1;
    int width = 0;
    for (const char letter : text) {
        const int glyph = letter >= 'a' && letter <= 'z' ? letter - 'a' : letter >= 'A' && letter <= 'Z' ? 26 + letter - 'A' : letter >= '0' && letter <= '9' ? 52 + letter - '0' : -1;
        if (glyph < 0 && letter != ' ') return -1;
        width += glyph < 0 ? 4 : exe[widths + static_cast<size_t>(glyph)];
        if (width > kNameRaster) return -1;
    }
    return width;
}

/* The control names stand beside a picture of the pad, which stays the US one. A name left of it keeps the edge the
 * US name ends at, one right of it starts where the US name does, or as far left as it needs to end on the screen. */
bool place_control_names(Bytes &patched, const Bytes &home, const std::vector<std::string> &home_names, const std::vector<std::string> &names) {
    static_assert(kWidths[1] > kNamePlaces + 80, "an executable that holds the small font's widths holds the places before them");
    const size_t places = kNamePlaces - kLoad + kExeHeader;
    if (home_names.size() <= kSmallFontLast || names.size() <= kSmallFontLast) return false;
    for (size_t name = kSmallFontFirst; name <= kSmallFontLast; ++name) {
        const size_t at = places + 8 * (name - kSmallFontFirst);
        const int home_width = small_font_width(home, home_names[name]), width = small_font_width(patched, names[name]);
        if (home_width < 0 || width < 0) return false;
        const int64_t home_x = static_cast<int32_t>(le32(home.data() + at));
        const int64_t x = home_x < kMenuWidth / 2 ? std::max<int64_t>(0, home_x + home_width - width) : std::min<int64_t>(home_x, kMenuWidth - width);
        put_le32(patched.data() + at, static_cast<uint32_t>(x));
    }
    return true;
}

bool is_movie(const std::string &name) {
    return name.size() > 4 && name.compare(name.size() - 4, 4, ".STR") == 0;
}

}  // namespace

void LanguageDisc::add(bool from_other, uint32_t source, uint32_t count) {
    if (count == 0) return;
    if (!runs_.empty() && runs_.back().from_other == from_other && runs_.back().source + runs_.back().count == source) {
        runs_.back().count += count;
    } else {
        runs_.push_back({sectors_, count, from_other, source});
    }
    sectors_ += count;
}

void LanguageDisc::set_data(uint32_t lba, const uint8_t *data) {
    data_[lba].assign(data, data + kSectorData);
}

bool known_disc(DiscImage &other, KnownDisc &known) {
    Volume theirs;
    if (!open_volume(other, theirs)) return false;
    for (const Language &one : languages()) {
        if (theirs.files.count(one.boot) == 0) continue;
        known = {one.name, one.text};
        return true;
    }
    return false;
}

bool LanguageDisc::build(DiscImage &home, DiscImage &other, std::string &why) {
    home_ = &home;
    other_ = &other;
    runs_.clear();
    data_.clear();
    pack_.clear();
    sectors_ = 0;
    tall_hints_ = false;
    language_ = "";

    Volume mine, theirs;
    if (!open_volume(home, mine) || !mine.files.count(kHomeBoot) || !open_volume(other, theirs)) {
        why = "one of the discs has no Disruptor files";
        return false;
    }
    const std::vector<Language> &known = languages();
    const auto language = std::find_if(known.begin(), known.end(), [&](const Language &one) { return theirs.files.count(one.boot) != 0; });
    if (language == known.end()) {
        why = "the other disc is not a version this build knows";
        return false;
    }

    const File home_wad = mine.files["WAD.IN"], other_wad = theirs.files["WAD.IN"];
    const File home_boot = mine.files[kHomeBoot], other_boot = theirs.files[language->boot];
    Bytes home_head, other_head, home_exe, other_exe;
    if (!user_data(home, home_wad.lba, kHeadWords * 4, home_head) || !user_data(other, other_wad.lba, kHeadWords * 4, other_head) ||
        !user_data(home, home_boot.lba, home_boot.sectors * kSectorData, home_exe) ||
        !user_data(other, other_boot.lba, other_boot.sectors * kSectorData, other_exe)) {
        why = "a disc could not be read";
        return false;
    }
    std::vector<uint32_t> head(kHeadWords), their_head(kHeadWords);
    for (uint32_t index = 0; index < kHeadWords; ++index) {
        head[index] = le32(home_head.data() + 4 * index);
        their_head[index] = le32(other_head.data() + 4 * index);
    }
    const std::vector<Piece> home_pieces = pieces(head, home_wad.sectors * kSectorData);
    std::map<std::vector<uint32_t>, Piece> their_pieces;
    std::set<uint32_t> cut, their_cut;
    for (const Piece &piece : home_pieces) cut.insert(piece.indexes.begin(), piece.indexes.end());
    for (const Piece &piece : pieces(their_head, other_wad.sectors * kSectorData)) {
        their_pieces[piece.indexes] = piece;
        their_cut.insert(piece.indexes.begin(), piece.indexes.end());
    }
    for (uint32_t index = 0; index < kTableWords; ++index) {
        if (carries_language(index) && cut.count(index) != their_cut.count(index)) {
            why = "WAD.IN is cut differently on the other disc";
            return false;
        }
    }

    Bytes patched_exe = home_exe;
    if (!patch_executable(patched_exe, other_exe, *language) || (language->text && !build_pack(home_exe, other_exe, *language, pack_, why, &patched_exe))) {
        if (why.empty()) why = "the executables do not have the tables this build expects";
        return false;
    }

    uint32_t hints = 0;
    add(false, 0, home_wad.lba);
    const uint32_t wad_first = sectors_;
    add(false, home_wad.lba, kHeadWords * 4 / kSectorData);
    for (const Piece &piece : home_pieces) {
        const uint32_t landed = (sectors_ - wad_first) * kSectorData;
        for (const uint32_t index : piece.indexes) head[index] = landed;
        const uint32_t first = home_wad.lba + piece.start / kSectorData, count = (piece.end - piece.start) / kSectorData;
        const bool menu = language->text && within(piece, kMenuFirst, kMenuLast), level = language->text && within(piece, kLevelFirst, kLevelLast);
        const bool speech = within(piece, kSpeechFirst, kSpeechLast), hint = within(piece, kHintFirst, kHintLast);
        if (!menu && !level && !speech && !hint) {
            add(false, first, count);
            continue;
        }
        const auto same = their_pieces.find(piece.indexes);
        if (same == their_pieces.end() || (speech && !only_within(piece, kSpeechFirst, kSpeechLast)) || (hint && !only_within(piece, kHintFirst, kHintLast))) {
            why = "WAD.IN is cut differently on the other disc";
            return false;
        }
        const uint32_t other_first = other_wad.lba + same->second.start / kSectorData;
        const uint32_t other_count = (same->second.end - same->second.start) / kSectorData;
        const uint64_t other_hint = language->hints == Hints::kTall ? kTallHintBytes : kHintBytes;
        if (hint && (uint64_t{count} * kSectorData != kHintBytes || uint64_t{other_count} * kSectorData != other_hint)) {
            why = "a hint is not the size the game loads";
            return false;
        }
        if (speech || hint) {
            hints += hint ? 1u : 0u;
            add(true, other_first, other_count);
        } else if (menu) {
            Bytes ours, their_piece, reworked;
            std::vector<std::string> home_names, names;
            if (!user_data(home, first, count * kSectorData, ours) || !user_data(other, other_first, other_count * kSectorData, their_piece) ||
                !menu_piece(ours, their_piece, *language, reworked, why, &home_names, &names)) {
                if (why.empty()) why = "a disc could not be read";
                return false;
            }
            if (!place_control_names(patched_exe, home_exe, home_names, names)) {
                why = "a control's name cannot be placed beside the picture of the pad";
                return false;
            }
            if (!menu_strings_fit(patched_exe, names, *language, why)) return false;
            for (uint32_t sector = 0; sector < count; ++sector) {
                const uint8_t *data = reworked.data() + sector * kSectorData;
                if (std::memcmp(data, ours.data() + sector * kSectorData, kSectorData) != 0) set_data(sectors_ + sector, data);
            }
            add(false, first, count);
        } else {
            Bytes header, other_header;
            if (!user_data(home, first, 8, header) || !user_data(other, other_first, 8, other_header)) {
                why = "a disc could not be read";
                return false;
            }
            /* Each piece: header and pictures, the part its header sizes, then later parts as long on both discs. */
            const uint64_t bytes = uint64_t{count} * kSectorData, other_bytes = uint64_t{other_count} * kSectorData;
            const uint64_t second = le32(header.data() + 4), other_second = le32(other_header.data() + 4);
            const bool sized = second % kSectorData == 0 && other_second % kSectorData == 0 && kLevelPictures + second <= bytes;
            const uint64_t later = sized ? bytes - kLevelPictures - second : 0;
            if (!sized || kLevelPictures + other_second + later > other_bytes) {
                why = "a level of the other disc is not laid out as expected";
                return false;
            }
            const uint64_t other_pictures = other_bytes - other_second - later;
            add(true, other_first, kLevelHeader / kSectorData);
            add(false, first + kLevelHeader / kSectorData, (kLevelPictures - kLevelHeader) / kSectorData);
            add(true, other_first + static_cast<uint32_t>(other_pictures / kSectorData), static_cast<uint32_t>(other_second / kSectorData));
            add(false, first + static_cast<uint32_t>((kLevelPictures + second) / kSectorData), static_cast<uint32_t>(later / kSectorData));
        }
    }
    for (uint32_t index = kClipSizeFirst; index <= kClipSizeLast; index += 2) head[index] = their_head[index];
    const uint32_t wad_sectors = sectors_ - wad_first;
    for (uint32_t index = 0; index < kTableWords; ++index) put_le32(home_head.data() + 4 * index, head[index]);
    set_data(wad_first, home_head.data());

    std::vector<std::pair<uint32_t, std::string>> order;
    uint32_t last_end = 0;
    for (const auto &[name, file] : mine.files) {
        order.emplace_back(file.lba, name);
        last_end = std::max(last_end, file.lba + file.sectors);
    }
    std::sort(order.begin(), order.end());
    for (const auto &[lba, name] : order) {
        const File &file = mine.files[name];
        uint32_t placed = lba, count = file.sectors, bytes = file.bytes;
        if (name == "WAD.IN") {
            placed = wad_first;
            count = wad_sectors;
            bytes = count * kSectorData;
        } else if (lba > home_wad.lba) {
            placed = sectors_;
            if (is_movie(name)) {
                const auto movie = theirs.files.find(name);
                if (movie == theirs.files.end()) {
                    why = "the other disc lacks a movie";
                    return false;
                }
                count = movie->second.sectors;
                bytes = count * kSectorData;
                add(true, movie->second.lba, count);
            } else {
                add(false, lba, count);
            }
        }
        uint8_t *record = mine.root.data() + file.record;
        put_le32(record + 2, placed);
        put_be32(record + 6, placed);
        put_le32(record + 10, bytes);
        put_be32(record + 14, bytes);
    }
    add(false, last_end, home.sectors() > last_end ? home.sectors() - last_end : 0);

    put_le32(mine.descriptor.data() + 80, sectors_);
    put_be32(mine.descriptor.data() + 84, sectors_);
    set_data(16, mine.descriptor.data());
    for (uint32_t sector = 0; sector * kSectorData < mine.root.size(); ++sector) set_data(mine.root_lba + sector, mine.root.data() + sector * kSectorData);
    for (uint32_t sector = 0; sector < home_boot.sectors; ++sector) {
        const uint8_t *data = patched_exe.data() + sector * kSectorData;
        if (std::memcmp(data, home_exe.data() + sector * kSectorData, kSectorData) != 0) set_data(home_boot.lba + sector, data);
    }
    language_ = language->name;
    tall_hints_ = language->hints == Hints::kTall && hints != 0;
    return true;
}

bool LanguageDisc::raw_sector(uint32_t lba, uint8_t *raw) {
    if (lba >= sectors_) return false;
    const auto after = std::upper_bound(runs_.begin(), runs_.end(), lba, [](uint32_t value, const Run &run) { return value < run.first; });
    const Run &run = *(after - 1);
    DiscImage *source = run.from_other ? other_ : home_;
    if (!source->raw_sector(run.source + (lba - run.first), raw)) return false;
    const uint32_t frame = lba + 150;
    const auto bcd = [](uint32_t value) { return static_cast<uint8_t>(value / 10 << 4 | value % 10); };
    raw[12] = bcd(frame / (75 * 60));
    raw[13] = bcd(frame / 75 % 60);
    raw[14] = bcd(frame % 75);
    if (const auto data = data_.find(lba); data != data_.end()) std::copy(data->second.begin(), data->second.end(), raw + kDataAt);
    return true;
}

}  // namespace disruptor
