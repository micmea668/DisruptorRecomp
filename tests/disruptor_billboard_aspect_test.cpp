#include "cpu_state.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

std::array<std::uint8_t, 2 * 1024 * 1024> g_ram{};
std::vector<std::pair<std::uint32_t, std::int32_t>> g_tags;
int g_failures = 0;

std::size_t physical(std::uint32_t address) {
    return static_cast<std::size_t>(address & 0x001FFFFFu);
}

void seed_byte(std::uint32_t address, std::uint8_t value) {
    g_ram[physical(address)] = value;
}

void seed_half(std::uint32_t address, std::uint16_t value) {
    const std::size_t p = physical(address);
    g_ram[p + 0] = static_cast<std::uint8_t>(value);
    g_ram[p + 1] = static_cast<std::uint8_t>(value >> 8);
}

std::uint8_t read_byte(std::uint32_t address) {
    return g_ram[physical(address)];
}

std::uint16_t read_half(std::uint32_t address) {
    const std::size_t p = physical(address);
    return static_cast<std::uint16_t>(g_ram[p + 0]) |
           (static_cast<std::uint16_t>(g_ram[p + 1]) << 8);
}

std::uint32_t read_word(std::uint32_t address) {
    const std::size_t p = physical(address);
    return static_cast<std::uint32_t>(g_ram[p + 0]) |
           (static_cast<std::uint32_t>(g_ram[p + 1]) << 8) |
           (static_cast<std::uint32_t>(g_ram[p + 2]) << 16) |
           (static_cast<std::uint32_t>(g_ram[p + 3]) << 24);
}

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

extern "C" {

int g_ls_replay_active = 0;
int g_ls_mode = 0;

void gpu_ws_tag_primitive(CPUState *, std::uint32_t primitive_addr,
                          std::int32_t anchor_x) {
    g_tags.emplace_back(primitive_addr, anchor_x);
}

}  // extern "C"

// Include the implementation to exercise its exact private packet guards.
#include "../src/disruptor_billboard_aspect.cpp"

namespace {

constexpr std::uint32_t kPacket = 0x80012000u;

void seed_billboard(std::int16_t left, std::int32_t width,
                    std::uint8_t command = 0x2Cu) {
    seed_byte(kPacket + 7u, command);
    const std::int16_t right = static_cast<std::int16_t>(
        static_cast<std::int32_t>(left) + width - 1);
    seed_half(kPacket + 8u, static_cast<std::uint16_t>(left));
    seed_half(kPacket + 16u, static_cast<std::uint16_t>(right));
    seed_half(kPacket + 24u, static_cast<std::uint16_t>(left));
    seed_half(kPacket + 32u, static_cast<std::uint16_t>(right));
}

void seed_mirrored_billboard(std::int16_t right, std::int32_t width,
                             std::uint8_t command = 0x2Cu) {
    seed_byte(kPacket + 7u, command);
    const std::int16_t left = static_cast<std::int16_t>(
        static_cast<std::int32_t>(right) - width + 1);
    seed_half(kPacket + 8u, static_cast<std::uint16_t>(right));
    seed_half(kPacket + 16u, static_cast<std::uint16_t>(left));
    seed_half(kPacket + 24u, static_cast<std::uint16_t>(right));
    seed_half(kPacket + 32u, static_cast<std::uint16_t>(left));
}

CPUState make_cpu() {
    CPUState cpu{};
    cpu.gpr[16] = kPacket;
    cpu.gpr[5] = kPacket;
    cpu.read_byte = read_byte;
    cpu.read_half = read_half;
    cpu.read_word = read_word;
    return cpu;
}

void reset_state() {
    g_ram.fill(0);
    g_tags.clear();
    g_ls_mode = 0;
    g_ls_replay_active = 0;
    disruptor_billboard_aspect_set_site_mask(
        disruptor_billboard_aspect_all_site_mask());
}

void test_all_audited_sites() {
    struct Site { std::uint32_t pc, opcode; std::uint8_t packet_gpr; };
    constexpr std::array<Site, 6> sites{{
        {0x8003BB88u, 0xA6030016u, 16u},
        {0x8003BFB0u, 0xA6030016u, 16u},
        {0x8003C848u, 0xA6030016u, 16u},
        {0x8003CAD4u, 0xA6020016u, 16u},
        {0x8003D488u, 0xA2060025u, 16u},
        {0x800433A0u, 0x02A02821u, 5u},
    }};

    reset_state();
    seed_billboard(168, 64);
    CPUState cpu = make_cpu();
    for (const Site& site : sites) {
        cpu.gpr[site.packet_gpr] = kPacket;
        disruptor_billboard_aspect_instruction_hook(
            &cpu, site.pc, site.opcode,
            DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    }
    expect(g_tags.size() == sites.size(),
           "all and only audited packet-completion sites tag");
    for (const auto& tag : g_tags) {
        expect(tag.first == kPacket, "tag preserves P_TAG packet address");
        expect(tag.second == 200, "even-width inclusive midpoint is exact");
    }
}

void test_deferred_enemy_uses_a1_packet() {
    reset_state();
    seed_billboard(168, 64);
    CPUState cpu = make_cpu();
    cpu.gpr[16] = 0x1F801810u;  // stale $s0 must not be consulted here
    cpu.gpr[5] = kPacket;
    disruptor_billboard_aspect_instruction_hook(
        &cpu, 0x800433A0u, 0x02A02821u,
        DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    expect(g_tags.size() == 1 && g_tags[0].first == kPacket,
           "deferred enemy submission tags the live $a1 packet, not stale $s0");
}

void test_mirrored_deferred_enemy_keeps_the_same_anchor() {
    reset_state();
    seed_mirrored_billboard(231, 64);
    CPUState cpu = make_cpu();
    disruptor_billboard_aspect_instruction_hook(
        &cpu, 0x800433A0u, 0x02A02821u,
        DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    expect(g_tags.size() == 1 && g_tags[0].second == 200,
           "mirrored enemy winding preserves the billboard midpoint");
}

void test_session_site_mask_is_exact_and_fail_closed() {
    reset_state();
    seed_billboard(168, 64);
    CPUState cpu = make_cpu();

    disruptor_billboard_aspect_set_site_mask(1u << 5);
    disruptor_billboard_aspect_instruction_hook(
        &cpu, 0x8003BB88u, 0xA6030016u,
        DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    disruptor_billboard_aspect_instruction_hook(
        &cpu, 0x800433A0u, 0x02A02821u,
        DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    expect(g_tags.size() == 1,
           "site isolation enables only the selected packet builder");

    disruptor_billboard_aspect_set_site_mask(0xFFFFFFFFu);
    expect(disruptor_billboard_aspect_get_site_mask() ==
               disruptor_billboard_aspect_all_site_mask(),
           "unsupported isolation bits are discarded");
}

void test_fixed_screen_compositor_is_excluded() {
    constexpr std::array<std::uint32_t, 4> screen_compositor_sites{{
        0x8003DD8Cu, 0x8003DEF0u, 0x8003E518u, 0x8003E654u,
    }};
    reset_state();
    seed_billboard(18, 102);
    CPUState cpu = make_cpu();
    cpu.gpr[16] = 0x1F801810u;
    cpu.gpr[5] = 0x1F801810u;
    cpu.gpr[23] = kPacket;
    for (const std::uint32_t pc : screen_compositor_sites) {
        disruptor_billboard_aspect_instruction_hook(
            &cpu, pc, 0xA6E20016u,
            DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    }
    expect(g_tags.empty(),
           "fixed-screen D6B0 FT4 branches must never receive world tags");
}

void test_midpoints_and_packet_flags() {
    reset_state();
    seed_billboard(170, 63, 0x2Fu);
    CPUState cpu = make_cpu();
    disruptor_billboard_aspect_instruction_hook(
        &cpu, 0x8003BB88u, 0xA6030016u,
        DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    expect(g_tags.size() == 1 && g_tags[0].second == 201,
           "odd-width raw/semitrans FT4 midpoint is exact");

    reset_state();
    seed_billboard(-200, 64);
    cpu = make_cpu();
    disruptor_billboard_aspect_instruction_hook(
        &cpu, 0x8003BFB0u, 0xA6030016u,
        DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    expect(g_tags.size() == 1 && g_tags[0].second == -168,
           "signed offscreen midpoint is preserved");
}

void test_fail_closed_guards() {
    reset_state();
    seed_billboard(10, 64);
    CPUState cpu = make_cpu();

    disruptor_billboard_aspect_instruction_hook(
        &cpu, 0x8003BB88u, 0xA6030016u,
        DISRUPTOR_BILLBOARD_ASPECT_BEFORE_INSTRUCTION);
    disruptor_billboard_aspect_instruction_hook(
        &cpu, 0x8003BB8Cu, 0xA6030016u,
        DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    disruptor_billboard_aspect_instruction_hook(
        &cpu, 0x8003BB88u, 0xA6030017u,
        DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    expect(g_tags.empty(), "phase, PC, and opcode mismatches are no-ops");

    seed_byte(kPacket + 7u, 0x24u);
    disruptor_billboard_aspect_instruction_hook(
        &cpu, 0x8003BB88u, 0xA6030016u,
        DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    expect(g_tags.empty(), "non-FT4 packet is rejected");

    seed_billboard(10, 64);
    seed_half(kPacket + 24u, 11u);
    disruptor_billboard_aspect_instruction_hook(
        &cpu, 0x8003BB88u, 0xA6030016u,
        DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    expect(g_tags.empty(), "non-axis-aligned packet is rejected");

    seed_billboard(10, 64);
    g_ls_mode = 1;
    disruptor_billboard_aspect_instruction_hook(
        &cpu, 0x8003BB88u, 0xA6030016u,
        DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    expect(g_tags.empty(), "lockstep record half cannot mutate host tags");
    g_ls_mode = 0;

    cpu.gpr[16] = 0x1F801810u;
    disruptor_billboard_aspect_instruction_hook(
        &cpu, 0x8003BB88u, 0xA6030016u,
        DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION);
    expect(g_tags.empty(), "non-RAM packet pointer is rejected before reads");
}

}  // namespace

int main() {
    test_all_audited_sites();
    test_deferred_enemy_uses_a1_packet();
    test_mirrored_deferred_enemy_keeps_the_same_anchor();
    test_session_site_mask_is_exact_and_fail_closed();
    test_fixed_screen_compositor_is_excluded();
    test_midpoints_and_packet_flags();
    test_fail_closed_guards();
    if (g_failures != 0) return 1;
    std::cout << "disruptor billboard aspect tests passed\n";
    return 0;
}
