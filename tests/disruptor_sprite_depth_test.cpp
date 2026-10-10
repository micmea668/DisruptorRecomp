#include "cpu_state.h"
#include "gpu_temporal_sprite.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

namespace {

struct Note {
    std::uint32_t packet;
    std::int32_t depth;
};

struct Placed {
    std::uint32_t packet;
    std::int32_t x;
    std::int32_t dy;
};

/* x_gpr and y_gpr are 0 where the game keeps the operand on its stack. */
struct TestFunnel {
    std::uint32_t projection;
    int depth_gpr;
    std::array<std::uint32_t, 2> packets;
    int x_gpr;
    int y_gpr;
};

constexpr std::array<TestFunnel, 4> kTestFunnels{{
    {0x8003B98Cu, 16, {0x8003BB88u, 0u}, 7, 5},
    {0x8003BDA0u, 16, {0x8003BFB0u, 0u}, 7, 6},
    {0x8003C4B0u, 21, {0x8003C848u, 0x8003CAD4u}, 0, 0},
    {0x8003D078u, 20, {0x8003D488u, 0u}, 8, 5},
}};
constexpr std::uint32_t kStack = 0x801FF000u;
constexpr std::uint32_t kStoreWord = 0xAFA20010u;
constexpr std::uint32_t kTestRecordSite = 0x8003D1B4u;
constexpr std::uint32_t kTestRecordWord = 0xA4379EA0u;
constexpr std::uint32_t kTestDeferredSite = 0x800433A0u;
constexpr std::uint32_t kPacket = 0x800C4000u;
constexpr std::int32_t kUnits = 8;

std::vector<Note> g_notes;
std::vector<Placed> g_places;
std::uint32_t g_stack_x = 0;
std::uint32_t g_stack_y = 0;
int g_failures = 0;
bool g_squash = true;

struct Sized {
    std::uint32_t packet;
    std::int32_t row;
    std::int32_t width;
    std::int32_t height;
    int shadow;
    int whole;
};
std::vector<Sized> g_sizes;
std::map<std::uint32_t, std::uint8_t> g_memory;

std::uint8_t read_byte(std::uint32_t address) {
    const auto found = g_memory.find(address);
    return found == g_memory.end() ? 0x5Au : found->second;
}

std::uint16_t read_half(std::uint32_t address) {
    return static_cast<std::uint16_t>(read_byte(address) | read_byte(address + 1u) << 8);
}

std::uint32_t read_stack(std::uint32_t address) {
    if (address == kStack + 0x18u) return g_stack_x;
    if (address == kStack + 0x20u) return g_stack_y;
    if (g_memory.count(address) == 0u) return 0x7FFFFFFFu;
    return read_half(address) | static_cast<std::uint32_t>(read_half(address + 2u)) << 16;
}

void put(std::uint32_t address, std::uint32_t value, int bytes) {
    for (int byte = 0; byte < bytes; ++byte) g_memory[address + static_cast<std::uint32_t>(byte)] = static_cast<std::uint8_t>(value >> (8 * byte));
}

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

extern "C" void gpu_temporal_note_sprite(CPUState *, std::uint32_t packet, std::int32_t depth) {
    g_notes.push_back({packet, depth});
}

extern "C" void gpu_temporal_place_sprite(std::uint32_t packet, std::int32_t x, std::int32_t dy) {
    g_places.push_back({packet, x, dy});
}

extern "C" void gpu_temporal_size_sprite(std::uint32_t packet, std::int32_t row, std::int32_t width, std::int32_t height,
                                         int shadow, int whole) {
    g_sizes.push_back({packet, row, width, height, shadow, whole});
}

/* Classic wide as the runtime does it: three quarters about column 160, rounded and unrounded. 4:3 without g_squash. */
extern "C" int psx_ws_project_x(int x) {
    if (!g_squash) return x;
    const int scaled = (x - 160) * 3;
    return 160 + (scaled + (scaled >= 0 ? 2 : -2)) / 4;
}

extern "C" std::int32_t psx_ws_project_x16(int x, std::int32_t fraction16) {
    if (!g_squash) return x * 65536 + fraction16;
    return 160 * 65536 + static_cast<std::int32_t>((static_cast<std::int64_t>(x - 160) * 65536 + fraction16) * 3 / 4);
}

std::int32_t g_range_num = 1, g_range_den = 1;
extern "C" void gte_ws_x_squash(std::int32_t *num, std::int32_t *den) {
    *num = g_range_num;
    *den = g_range_den;
}

#include "../src/disruptor_sprite_depth.cpp"

namespace {

void project(CPUState &cpu, const TestFunnel &funnel, std::int32_t depth) {
    cpu.gpr[funnel.depth_gpr] = static_cast<std::uint32_t>(depth);
    disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
}

bool noted_once(std::uint32_t packet, std::int32_t depth) {
    const bool right = g_notes.size() == 1 && g_notes[0].packet == packet && g_notes[0].depth == depth;
    g_notes.clear();
    return right;
}

void test_every_funnel_hands_its_depth_over() {
    CPUState cpu{};
    std::int32_t depth = 100;
    for (const TestFunnel &funnel : kTestFunnels)
        for (std::uint32_t site : funnel.packets) {
            if (site == 0u) continue;
            depth += 37;
            project(cpu, funnel, depth);
            disruptor_sprite_depth_packet(&cpu, site, kPacket + static_cast<std::uint32_t>(depth));
            expect(noted_once(kPacket + static_cast<std::uint32_t>(depth), depth * kUnits),
                   "a funnel's packet carries the depth of its own projection in GTE units");
            disruptor_sprite_depth_packet(&cpu, site, kPacket);
            expect(g_notes.empty(), "a depth is handed over once");
        }
}

void test_a_depth_stays_in_its_funnel() {
    CPUState cpu{};
    project(cpu, kTestFunnels[0], 300);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[1].packets[0], kPacket);
    expect(g_notes.empty(), "another funnel's packet does not take a depth left behind by a culled sprite");
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[0].packets[0], kPacket);
    expect(g_notes.empty(), "and the depth it refused is gone");

    project(cpu, kTestFunnels[0], 300);
    project(cpu, kTestFunnels[2], 500);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[2].packets[1], kPacket);
    expect(noted_once(kPacket, 500 * kUnits), "the latest projection is the pending one");
}

void test_only_the_reviewed_instructions_count() {
    CPUState cpu{};
    cpu.gpr[16] = 300;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[0].projection, kStoreWord ^ 1u, 1);
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[0].projection, kStoreWord, 0);
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[0].projection + 4u, kStoreWord, 1);
    disruptor_sprite_depth_instruction_hook(nullptr, kTestFunnels[0].projection, kStoreWord, 1);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[0].packets[0], kPacket);
    expect(g_notes.empty(), "a changed word, another phase or address and a missing CPU record nothing");

    for (std::int32_t depth : {0, -5, INT32_MAX / kUnits + 1}) {
        project(cpu, kTestFunnels[0], depth);
        disruptor_sprite_depth_packet(&cpu, kTestFunnels[0].packets[0], kPacket);
        expect(g_notes.empty(), "a depth that is not positive or does not fit is not passed on");
    }
    project(cpu, kTestFunnels[0], 300);
    disruptor_sprite_depth_packet(&cpu, 0x80010000u, kPacket);
    disruptor_sprite_depth_packet(nullptr, kTestFunnels[0].packets[0], kPacket);
    expect(g_notes.empty(), "an unknown packet site and a missing CPU hand nothing over");
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[0].packets[0], kPacket);
    expect(noted_once(kPacket, 300 * kUnits), "and neither of them spends the pending depth");
}

/* What the game holds at the seam for a point (x, y, depth): its operands, and its own two quotients. */
void at_the_seam(CPUState &cpu, const TestFunnel &funnel, std::int32_t x, std::int32_t y, std::int32_t depth) {
    cpu.gpr[29] = kStack;
    cpu.read_word = read_stack;
    cpu.gpr[funnel.depth_gpr] = static_cast<std::uint32_t>(depth);
    if (funnel.x_gpr) cpu.gpr[funnel.x_gpr] = static_cast<std::uint32_t>(x); else g_stack_x = static_cast<std::uint32_t>(x);
    if (funnel.y_gpr) cpu.gpr[funnel.y_gpr] = static_cast<std::uint32_t>(y); else g_stack_y = static_cast<std::uint32_t>(y);
    cpu.gpr[2] = static_cast<std::uint32_t>(psx_ws_project_x(160 + 160 * x / depth));
    cpu.gpr[3] = static_cast<std::uint32_t>(160 * y / depth);
}

bool placed_once(std::uint32_t packet, std::int32_t x, std::int32_t dy) {
    const bool right = g_places.size() == 1 && g_places[0].packet == packet && g_places[0].x == x && g_places[0].dy == dy;
    g_places.clear();
    g_notes.clear();
    g_sizes.clear();
    return right;
}

bool sized_once(std::int32_t row, std::int32_t width, std::int32_t height, int shadow, int whole) {
    const bool right = g_sizes.size() == 1 && g_sizes[0].packet == kPacket && g_sizes[0].row == row && g_sizes[0].width == width &&
                       g_sizes[0].height == height && g_sizes[0].shadow == shadow && g_sizes[0].whole == whole;
    g_sizes.clear();
    g_places.clear();
    g_notes.clear();
    return right;
}

bool unsized() {
    const bool right = g_sizes.empty() && g_places.size() == 1;
    g_sizes.clear();
    g_places.clear();
    g_notes.clear();
    return right;
}

constexpr std::uint32_t kActor = 0x80120000u;
constexpr std::uint32_t kBody = 0x80130000u;
constexpr std::uint32_t kPictures = 0x80140000u;
constexpr std::uint32_t kTable = 0x80057E98u;
/* The point of every size case: 160 * 100 / 640 and 160 * 37 / 640, row 120 - 9.25 = 110.75. */
constexpr std::int32_t kRow = 7258112;

CPUState ready(const TestFunnel &funnel) {
    CPUState cpu{};
    at_the_seam(cpu, funnel, 100, 37, 640);
    cpu.read_half = read_half;
    cpu.read_byte = read_byte;
    return cpu;
}

void run(CPUState &cpu, const TestFunnel &funnel, std::uint32_t site) {
    disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
    disruptor_sprite_depth_packet(&cpu, site, kPacket);
}

/* An actor of the fourth funnel: picture 28 x 7 at scales 1024 and 2048 over 64, which is 448 x 224 before the depth. */
void fourth_actor(CPUState &cpu, std::uint32_t kind, std::uint32_t frame, std::uint32_t turned) {
    g_memory.clear();
    cpu.gpr[21] = kActor;
    cpu.gpr[22] = kBody;
    cpu.gpr[16] = 0xABCD00u | turned;
    put(kActor + 0x3Au, kind, 1);
    put(kActor + 0x3Bu, frame, 1);
    put(kBody, kPictures, 4);
    put(kPictures + 20u * (frame + (kind == 7u || kind == 8u ? 0u : turned)) + 8u, 28u | 7u << 8, 2);
    put(kBody + 0x1Cu, 1024u, 4);
    put(kBody + 0x18u, 2048u, 4);
    put(kBody + 0xCu, 64u, 4);
    put(kTable + 2u * 64u, 1024u, 2);
    put(kTable + 2u * 640u, 102u, 2);
    cpu.gpr[18] = 111u;  /* (448 * 160 * 102) >> 16 */
    cpu.gpr[19] = 55u;   /* (224 * 160 * 102) >> 16 */
}

constexpr std::uint32_t kKind = 0x80150000u;

/* An effect of the third funnel: 401 units, a picture 38 x 21 over a reference 40 x 40, with what the game's two cuts leave of it. */
void effect(CPUState &cpu, std::uint32_t which, bool mirrored, std::int32_t depth, std::int32_t x = 100, std::int32_t y = 37) {
    g_memory.clear();
    at_the_seam(cpu, kTestFunnels[2], x, y, depth);
    cpu.gpr[19] = kActor;
    cpu.gpr[20] = kPictures;
    cpu.gpr[30] = 0xABCD00u | (mirrored ? 1u : 0u);
    put(kStack + 0x28u, kKind, 4);
    put(kActor + 0x1Du, which, 1);
    put(kKind + 0x14u, which ? 9999u : 401u, 4);
    put(kKind + 0x18u, which ? 401u : 9999u, 4);
    put(kPictures + 8u, 38u | 21u << 8, 2);
    put(kKind + which + 0x30u, 40u, 1);
    put(kKind + which + 0x34u, 40u, 1);
    const std::int32_t base = 160 * 401 / depth, wide = base * 38 / 40;
    cpu.gpr[22] = static_cast<std::uint32_t>(mirrored ? -wide : wide);
    cpu.gpr[18] = static_cast<std::uint32_t>(base * 21 / 40);
}

void run_effect(CPUState &cpu) {
    run(cpu, kTestFunnels[2], kTestFunnels[2].packets[0]);
}

void test_an_effect_keeps_its_size_before_both_cuts() {
    /* At depth 640 the base is 100.25 px: 95.2375 x 52.63125 exactly, 95 x 52 after the cuts. */
    constexpr std::int32_t wide = 6241484, tall = 3449241;
    for (const std::uint32_t which : {0u, 3u}) {
        CPUState cpu = ready(kTestFunnels[2]);
        effect(cpu, which, false, 640);
        run_effect(cpu);
        expect(sized_once(kRow, wide, tall, 0, 0), "an effect's size is its units over the depth times its picture over its reference");
    }
    CPUState cpu = ready(kTestFunnels[2]);
    effect(cpu, 0u, true, 640);
    run_effect(cpu);
    expect(sized_once(kRow, wide + 2 * 65536, tall, 0, 0), "a mirrored effect's packet spans two columns more, and so does its size");
    effect(cpu, 0u, true, 640);
    cpu.gpr[22] = 95u;
    run_effect(cpu);
    expect(unsized(), "a width the game did not mirror is not a mirrored effect's");
    effect(cpu, 0u, false, 640);
    cpu.gpr[22] = 3u * 95u;
    cpu.gpr[18] = 3u * 52u;
    run_effect(cpu);
    expect(unsized(), "an effect the game went on to grow threefold keeps its packet's size");
    effect(cpu, 0u, false, 640);
    ++cpu.gpr[18];
    run_effect(cpu);
    expect(unsized(), "nor is a height the steps do not end on handed over");
    for (const std::uint32_t reference : {0x30u, 0x34u}) {
        effect(cpu, 0u, false, 640);
        put(kKind + reference, 0u, 1);
        run_effect(cpu);
        expect(unsized(), "an effect without a reference has no size");
    }
    effect(cpu, 0u, false, 640);
    put(kKind + 0x14u, 0u, 4);
    cpu.gpr[22] = cpu.gpr[18] = 0u;
    run_effect(cpu);
    expect(unsized(), "nor one without units");
    effect(cpu, 0u, false, 640);
    cpu.read_byte = nullptr;
    run_effect(cpu);
    expect(unsized(), "nor one that cannot be read");
    cpu.read_byte = read_byte;
    effect(cpu, 0u, false, 60);
    run_effect(cpu);
    expect(unsized(), "nor one with a side over 511 pixels");

    effect(cpu, 0u, false, 640);
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[2].projection, kStoreWord, 1);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[2].packets[1], kPacket);
    expect(unsized(), "the funnel's second packet is another rectangle on the same point: it takes the place and no size");
}

/* Every effect at 4:3: the packet as the game centres it, the place, row and size as the module hands them, through the
 * renderer's own arithmetic. The sides it returns must put the rectangle where the numbers before any cut say. */
void test_the_renderer_takes_every_effect_size() {
    constexpr std::array<std::array<std::int32_t, 2>, 2> points{{{100, 37}, {-77, -45}}};
    int seen = 0, refused = 0, held = 0;
    g_squash = false;
    for (const bool mirrored : {false, true})
        for (const auto &point : points)
            for (std::int32_t depth = 140; depth <= 900; ++depth) {
                CPUState cpu = ready(kTestFunnels[2]);
                effect(cpu, 0u, mirrored, depth, point[0], point[1]);
                const auto s6 = static_cast<std::int32_t>(cpu.gpr[22]), s2 = static_cast<std::int32_t>(cpu.gpr[18]);
                run_effect(cpu);
                ++seen;
                if (g_sizes.size() != 1 || g_places.size() != 1) {
                    ++refused;
                    g_sizes.clear(); g_places.clear(); g_notes.clear();
                    continue;
                }
                const Sized size = g_sizes[0];
                const Placed place = g_places[0];
                g_sizes.clear(); g_places.clear(); g_notes.clear();
                /* 0x8003C628..0x8003C7D8: x0 = x - ($s6 >> 1), x1 = x0 + $s6 - 1, y0 = y - ($s2 >> 1), y2 = y0 + $s2 - 1. */
                const int x0 = 160 + 160 * point[0] / depth - (s6 >> 1), x1 = x0 + s6 - 1;
                const int y0 = 120 - 160 * point[1] / depth - (s2 >> 1), y2 = y0 + s2 - 1;
                const int left = std::min(x0, x1), w = std::abs(x1 - x0), h = y2 - y0;
                const GpuTemporalSprite sprite = gpu_temporal_sprite_from_fixed(1, 1, 0, 0, place.x, place.dy, size.row, size.width, size.height);
                float sides[4];
                gpu_temporal_sprite_sides(&sprite, static_cast<float>(left), static_cast<float>(y0), w, h, w + 1, h + 1, 1.0f, 0, sides);
                const double cut_wide = std::abs(s6), exact_wide = 160.0 * 401.0 * 38.0 / (40.0 * depth);
                const double wide = std::min(exact_wide, cut_wide + 1.5) + (mirrored ? 2.0 : 0.0) - 1.5;
                const double tall = std::min(160.0 * 401.0 * 21.0 / (40.0 * depth), s2 + 1.5) - 1.5;
                const double column = 160.0 + 160.0 * point[0] / depth - 0.25, row = 120.0 - 160.0 * point[1] / depth - 0.25;
                const std::array<double, 4> wanted{column - wide / 2, row - tall / 2, column + wide / 2, row + tall / 2};
                const std::array<double, 4> shown{left + sides[0], y0 + sides[1], left + w + sides[2], y0 + h + sides[3]};
                bool right = true;
                for (std::size_t side = 0; side < 4; ++side)
                    right = right && std::isfinite(sides[side]) && std::fabs(shown[side] - wanted[side]) <= 0.003;
                refused += !right;
                held += exact_wide > cut_wide + 1.5;
            }
    g_squash = true;
    expect(seen == 2 * 2 * 761 && refused == 0,
           "the renderer puts every effect from depth 140 to 900 where its numbers before any cut say, mirrored or not");
    expect(held > 0 && held * 5 < seen, "a side the two cuts left more than a pixel and a half behind is held there, and few are");
}

void test_a_sprite_keeps_its_size_before_the_cut() {
    {
        CPUState cpu = ready(kTestFunnels[0]);
        g_memory.clear();
        cpu.gpr[21] = kActor;
        put(kActor + 4u, 36u, 4);
        put(kActor + 8u, 52u, 4);
        run(cpu, kTestFunnels[0], kTestFunnels[0].packets[0]);
        expect(sized_once(kRow, 9 * 65536, 13 * 65536, 0, 0), "the first funnel's size is two words of its definition times 160 over the depth");
        put(kActor + 4u, 0u, 4);
        run(cpu, kTestFunnels[0], kTestFunnels[0].packets[0]);
        expect(unsized(), "a size of nothing is not handed over");
        cpu.read_half = nullptr;
        put(kActor + 4u, 36u, 4);
        run(cpu, kTestFunnels[0], kTestFunnels[0].packets[0]);
        expect(unsized(), "nor a size that cannot be read");
    }
    {
        CPUState cpu = ready(kTestFunnels[1]);
        g_memory.clear();
        cpu.gpr[18] = kActor;
        put(kActor + 8u, 24u | 48u << 8, 2);
        run(cpu, kTestFunnels[1], kTestFunnels[1].packets[0]);
        expect(sized_once(kRow, 6 * 65536, 12 * 65536, 0, 0), "the second funnel's size is two bytes of its definition");
    }
    const TestFunnel &fourth = kTestFunnels[3];
    const auto deferred = [&fourth](CPUState &cpu) {
        at_the_seam(cpu, fourth, 100, 37, 640);
        disruptor_sprite_depth_instruction_hook(&cpu, fourth.projection, kStoreWord, 1);
        cpu.gpr[3] = 20u * 6u;
        disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
        const std::uint32_t body = cpu.gpr[22];
        cpu.gpr[22] = 6u;
        disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
        cpu.gpr[22] = body;
    };
    for (const std::uint32_t kind : {0u, 6u, 7u, 8u, 9u}) {
        CPUState cpu = ready(fourth);
        fourth_actor(cpu, kind, 5u, 3u);
        deferred(cpu);
        expect(sized_once(kRow, 112 * 65536, 56 * 65536, 0, 1),
               "an actor's size follows the game's own steps and is marked as spanning its packet whole");
    }
    {
        CPUState cpu = ready(fourth);
        fourth_actor(cpu, 0u, 5u, 3u);
        put(kActor + 0x3Au, 7u, 1);
        deferred(cpu);
        expect(unsized(), "a picture taken without the actor's turn is another picture");
        fourth_actor(cpu, 0u, 5u, 3u);
        cpu.gpr[18] = 112u;
        deferred(cpu);
        expect(unsized(), "a width the steps do not end on leaves the place and no size");
        fourth_actor(cpu, 0u, 5u, 3u);
        cpu.gpr[19] = 56u;
        deferred(cpu);
        expect(unsized(), "and so does a height");
        fourth_actor(cpu, 0u, 5u, 3u);
        put(kTable + 2u * 640u, 1024u, 2);
        cpu.gpr[18] = 1120u;
        cpu.gpr[19] = 560u;
        deferred(cpu);
        expect(unsized(), "nor does a side past the game's cap, where it rescales the other");
    }    for (const int broken : {0, 1, 2, 3}) {
        /* A shadow 60 x 32 units, 320 units under the eye, at depth 640: 15 x 8 pixels, centred on row 120 - 80. */
        CPUState cpu = ready(fourth);
        fourth_actor(cpu, 0u, 5u, 3u);
        put(kBody + 0xF8u, 60u, 4);
        put(kBody + 0xFCu, 32u, 4);
        put(kBody + 0xF4u, 100u, 4);
        put(kActor + 0x28u, 500u, 2);
        put(0x800775D0u, 80u, 4);
        put(kPacket + 8u, 200u | (36u + (broken == 3)) << 16, 4);
        put(kPacket + 16u, (214u + (broken == 1)) | 36u << 16, 4);
        put(kPacket + 24u, 200u | (43u + (broken == 2) + (broken == 3)) << 16, 4);
        run(cpu, fourth, fourth.packets[0]);
        if (broken) {
            expect(g_sizes.empty() && g_places.size() == 1 && g_places[0].dy == 0,
                   "a shadow whose numbers do not give its packet keeps the caster's column and no row or size");
            g_places.clear();
            g_notes.clear();
        } else {
            expect(g_places.size() == 1 && g_places[0].dy == 0, "a shadow drops the caster's row");
            expect(sized_once(40 * 65536, 15 * 65536, 8 * 65536, 1, 0), "and takes its own row and size, marked as a shadow");
        }
    }
    g_memory.clear();
}


void test_a_projection_keeps_what_the_divisions_dropped();

void test_sizes_run_after_places() {
    test_a_projection_keeps_what_the_divisions_dropped();
    test_a_sprite_keeps_its_size_before_the_cut();
    test_an_effect_keeps_its_size_before_both_cuts();
    test_the_renderer_takes_every_effect_size();
}

void test_a_projection_keeps_what_the_divisions_dropped() {
    for (const TestFunnel &funnel : kTestFunnels) {
        CPUState cpu{};
        /* 160 * 100 / 640 = 25 exactly, column 185, 178.75 after the squash. 160 * 37 / 640 = 9.25: a quarter pixel up. */
        at_the_seam(cpu, funnel, 100, 37, 640);
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        /* The fourth funnel's own packet site is its shadow, which does not keep the caster's row. */
        const bool shadow = funnel.packets[0] == 0x8003D488u;
        expect(placed_once(kPacket, 11714560, shadow ? 0 : -16384), "the unrounded column and the dropped part of the row go with the packet");
        /* 160 * -77 / 1000 = -12.32: column 148 and -0.32 more, 150.76 after the squash. 160 * -45 / 1000 = -7.2: 0.2 down. */
        at_the_seam(cpu, funnel, -77, -45, 1000);
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(placed_once(kPacket, 9880208, shadow ? 0 : 13107), "a remainder left of the centre and above it keeps its sign");

        at_the_seam(cpu, funnel, 100, 37, 640);
        ++cpu.gpr[3];
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a row quotient the operands do not give leaves the depth and no place");
        at_the_seam(cpu, funnel, 100, 37, 640);
        ++cpu.gpr[2];
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a stored column the operands do not give leaves the depth and no place");
        at_the_seam(cpu, funnel, 26000, 37, 640);
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a column too far out for 16.16 is not placed");
        at_the_seam(cpu, funnel, 100, 37, 640);
        cpu.gpr[4] = 1u;
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a point the game goes on to warp keeps its depth and no place");
        cpu.gpr[4] = 2u;
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(placed_once(kPacket, 11714560, funnel.packets[0] == 0x8003D488u ? 0 : -16384), "any other value of the switch leaves the point alone");
        cpu.gpr[4] = 0u;
    }
    CPUState cpu{};
    at_the_seam(cpu, kTestFunnels[2], 100, 37, 640);
    cpu.read_word = nullptr;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[2].projection, kStoreWord, 1);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[2].packets[0], kPacket);
    expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a stack operand that cannot be read leaves the depth and no place");

    const TestFunnel &deferring = kTestFunnels[3];
    at_the_seam(cpu, deferring, -77, -45, 1000);
    disruptor_sprite_depth_instruction_hook(&cpu, deferring.projection, kStoreWord, 1);
    cpu.gpr[3] = 20u * 9u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
    at_the_seam(cpu, deferring, 100, 37, 640);
    disruptor_sprite_depth_instruction_hook(&cpu, deferring.projection, kStoreWord, 1);
    cpu.gpr[22] = 9u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket + 80u);
    expect(placed_once(kPacket + 80u, 9880208, 13107), "a deferred actor's place waits under its record with its depth");
    cpu.gpr[22] = 9u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket + 80u);
    expect(g_places.empty() && g_notes.empty(), "and is handed over once");
    disruptor_sprite_depth_packet(&cpu, deferring.packets[0], kPacket);
    g_places.clear();
    g_notes.clear();
}

void test_deferred_actor_waits_under_its_record() {
    CPUState cpu{};
    const TestFunnel &deferring = kTestFunnels[3];
    project(cpu, deferring, 410);
    cpu.gpr[3] = 20u * 7u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
    project(cpu, deferring, 520);
    cpu.gpr[3] = 20u * 2u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);

    cpu.gpr[22] = 2u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket + 40u);
    expect(noted_once(kPacket + 40u, 520 * kUnits), "a deferred packet takes the depth stored under its record");
    cpu.gpr[22] = 7u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
    expect(noted_once(kPacket, 410 * kUnits), "records are told apart by their index, in any order");
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
    expect(g_notes.empty(), "a record's depth is handed over once");

    project(cpu, kTestFunnels[0], 300);
    cpu.gpr[3] = 20u * 4u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
    cpu.gpr[22] = 4u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
    expect(g_notes.empty(), "a record does not take a depth projected by another funnel");

    project(cpu, deferring, 410);
    for (std::uint32_t offset : {20u * 4u + 1u, 20u * 256u}) {
        cpu.gpr[3] = offset;
        disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
    }
    cpu.gpr[3] = 20u * 4u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord ^ 1u, 1);
    for (std::uint32_t index : {4u, 256u, 0xFFFFFFFFu}) {
        cpu.gpr[22] = index;
        disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
    }
    expect(g_notes.empty(), "a misaligned or out-of-range record and a changed word store nothing");
}

/* Where each funnel holds its range at the projection, and whether the fourth's lead is in it by then. */
struct TestRange {
    int gpr;
    bool led;
};
constexpr std::array<TestRange, 4> kTestRanges{{{18, false}, {19, false}, {23, false}, {23, true}}};

/* The game's steps: the depth and three eighths of the step aside, then the fourth funnel's lead of 0x18 down to 8. */
std::int32_t game_range(bool led, std::int32_t depth, std::int32_t aside) {
    const std::int32_t range = depth + (3 * aside >> 3);
    return led ? std::max(range - 0x18, 8) : range;
}

/* The registers as a funnel has them at its projection, and its range after the seam. */
std::int32_t ranged(std::size_t which, std::int32_t depth, std::int32_t x, std::int32_t off_by = 0,
                    std::uint32_t word = kStoreWord, int phase = 1, bool stack = true) {
    const TestFunnel &funnel = kTestFunnels[which];
    CPUState cpu{};
    cpu.read_word = stack ? read_stack : nullptr;
    cpu.gpr[29] = kStack;
    for (int gpr = 1; gpr < 32; ++gpr)
        if (gpr != 29) cpu.gpr[gpr] = 0x40000000u + static_cast<std::uint32_t>(gpr);
    if (funnel.x_gpr != 0) cpu.gpr[funnel.x_gpr] = static_cast<std::uint32_t>(x);
    g_stack_x = static_cast<std::uint32_t>(x);
    g_stack_y = 0u;
    cpu.gpr[funnel.depth_gpr] = static_cast<std::uint32_t>(depth);
    cpu.gpr[kTestRanges[which].gpr] = static_cast<std::uint32_t>(game_range(kTestRanges[which].led, depth, std::abs(x)) + off_by);
    const CPUState before = cpu;
    disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, word, phase);
    for (int gpr = 0; gpr < 32; ++gpr)
        expect(gpr == kTestRanges[which].gpr || cpu.gpr[gpr] == before.gpr[gpr], "the seam may change the range register and no other");
    return static_cast<std::int32_t>(cpu.gpr[kTestRanges[which].gpr]);
}

void test_a_range_comes_under_the_squash() {
    for (std::size_t which = 0; which < kTestFunnels.size(); ++which) {
        const bool led = kTestRanges[which].led;
        g_range_num = g_range_den = 1;
        expect(ranged(which, 1000, 400) == game_range(led, 1000, 400), "at 4:3 the game's range stands");
        g_range_num = g_range_den = 3;
        expect(ranged(which, 1000, 400) == game_range(led, 1000, 400), "a ratio of one in other numbers is no squash");

        g_range_num = 3;
        g_range_den = 4;
        expect(ranged(which, 1000, 400) == game_range(led, 1000, 300) && ranged(which, 1000, -400) == game_range(led, 1000, 300),
               "at 16:9 a step aside counts for three quarters of itself, to either side");
        expect(game_range(led, 1000, 400) - game_range(led, 1000, 300) == 38, "which is 38 nearer here: over four sort slots");
        expect(ranged(which, 1000, 7) == game_range(led, 1000, 5), "the squashed step is cut to a whole unit before the eighths");
        expect(ranged(which, 1000, 0) == game_range(led, 1000, 0), "a sprite straight ahead keeps its range");
        expect(ranged(which, 1000, 400, 1) == game_range(led, 1000, 400) + 1 && ranged(which, 1000, 400, -1) == game_range(led, 1000, 400) - 1,
               "a register that does not hold the game's range of this depth and step is left alone");
        expect(ranged(which, 1000, 400, 0, kStoreWord ^ 1u) == game_range(led, 1000, 400) &&
                   ranged(which, 1000, 400, 0, kStoreWord, 0) == game_range(led, 1000, 400),
               "a changed word and the phase before the store change nothing");

        g_range_num = 4;
        g_range_den = 7;
        expect(ranged(which, 1000, 400) == game_range(led, 1000, 228), "at 21:9 it counts for four sevenths");
        g_range_num = 16;
        g_range_den = 15;
        expect(ranged(which, 1000, 400) == game_range(led, 1000, 426), "a screen narrower than 4:3 stretches it");
        for (const auto &[num, den] : {std::pair{0, 4}, std::pair{3, 0}, std::pair{-3, -4}}) {
            g_range_num = num;
            g_range_den = den;
            expect(ranged(which, 1000, 400) == game_range(led, 1000, 400), "a ratio that is not two positive numbers changes nothing");
        }
    }

    g_range_num = 3;
    g_range_den = 4;
    expect(ranged(3, 20, 40) == 8 && game_range(true, 20, 40) == 11 && game_range(true, 20, 30) == 8,
           "the fourth funnel's range keeps its lead and its floor");
    expect(ranged(3, 10, 8) == 8, "a range already on its floor stays there");
    for (std::size_t which = 0; which < kTestFunnels.size(); ++which)
        for (const std::int32_t depth : {0, -1000})
            expect(ranged(which, depth, 400) == game_range(kTestRanges[which].led, depth, 400), "a depth the game does not draw is not a sprite's: the range stands");
    expect(ranged(0, 20, 40) == game_range(false, 20, 30) && game_range(false, 20, 30) == 31, "the other funnels have no lead at the projection");
    expect(ranged(2, 1000, 400, 0, kStoreWord, 1, false) == game_range(false, 1000, 400),
           "the third funnel's step is on the stack: unreadable, the range stands");
    g_range_num = g_range_den = 1;
}

}  // namespace

int main() {
    test_a_range_comes_under_the_squash();
    test_every_funnel_hands_its_depth_over();
    test_a_depth_stays_in_its_funnel();
    test_only_the_reviewed_instructions_count();
    test_deferred_actor_waits_under_its_record();
    test_sizes_run_after_places();
    expect(g_places.empty() && g_sizes.empty(), "no test leaves a place or a size behind");
    if (g_failures) return 1;
    std::cout << "Disruptor sprite depth tests passed\n";
    return 0;
}
