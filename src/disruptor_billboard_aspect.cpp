/*
 * Aspect correction for Disruptor's CPU-built world billboards.
 *
 * The game's GTE and reviewed CPU projection sites already squash a world
 * sprite's centre X for classic widescreen.  Five resident render funnels then
 * build a 40-byte POLY_FT4 around that centre using an unsquashed retail pixel
 * width, while a sixth deferred submission path rebuilds the base actor quad
 * from a sorted record.  The final wide presentation consequently stretches
 * enemies, pickups, and related billboard effects horizontally.
 *
 * At each audited completion/submission seam, derive the packet's inclusive
 * horizontal midpoint and tag it through the renderer's existing provenance
 * path. The GP0 textured-quad executor then squashes X around that midpoint.
 * Guest RAM, Y, UVs, HUD SPRTs, and the first-person weapon path are untouched.
 */

#include "disruptor_billboard_aspect.h"

#include "cpu_state.h"
#include "gpu.h"
#include "lockstep.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

struct BillboardPacketSite {
    uint32_t pc;
    uint32_t instruction;
    uint8_t packet_gpr;
};

/* Completed POLY_FT4 packet seams in the audited CPU-projected world-billboard
 * funnels.  func_8003C000 has two construction branches; func_8004300C is the
 * deferred/sorted base-actor pass and submits its completed packet through $a1. */
constexpr auto kBillboardPacketSites = std::array<BillboardPacketSite, 6>{{
    {0x8003BB88u, 0xA6030016u, 16u},
    {0x8003BFB0u, 0xA6030016u, 16u},
    {0x8003C848u, 0xA6030016u, 16u},
    {0x8003CAD4u, 0xA6020016u, 16u},
    {0x8003D488u, 0xA2060025u, 16u},
    {0x800433A0u, 0x02A02821u, 5u},
}};
constexpr uint32_t kAllBillboardPacketSites =
    (1u << kBillboardPacketSites.size()) - 1u;
uint32_t g_billboard_packet_site_mask = kAllBillboardPacketSites;

constexpr uint32_t kRamFirst = 0x80000000u;
constexpr uint32_t kRamLast = 0x801FFFFFu;
constexpr uint32_t kPacketLastByte = 39u;
constexpr int32_t kMaximumBillboardWidth = 2048;

bool comparator_active() {
    return g_ls_mode != 0 || g_ls_replay_active != 0;
}

const BillboardPacketSite *billboard_packet_site(
        uint32_t pc, uint32_t instruction) {
    for (const BillboardPacketSite& site : kBillboardPacketSites) {
        if (site.pc == pc && site.instruction == instruction) return &site;
    }
    return nullptr;
}

uint32_t billboard_packet_site_bit(const BillboardPacketSite *site) {
    if (!site) return 0;
    const auto index = static_cast<std::size_t>(
        site - kBillboardPacketSites.data());
    return index < kBillboardPacketSites.size() ? (1u << index) : 0u;
}

bool valid_main_ram_packet(uint32_t packet) {
    return packet >= kRamFirst && packet <= kRamLast - kPacketLastByte;
}

int32_t signed_x(CPUState *cpu, uint32_t address) {
    return static_cast<int32_t>(static_cast<int16_t>(cpu->read_half(address)));
}

void tag_completed_billboard(CPUState *cpu, uint8_t packet_gpr) {
    const uint32_t packet = cpu->gpr[packet_gpr];
    if (!valid_main_ram_packet(packet) ||
        !cpu->read_byte || !cpu->read_half || !cpu->read_word) {
        return;
    }

    /* P_TAG is four bytes; the GP0 command byte is the high byte of the colour
     * word.  POLY_FT4 occupies 0x2C..0x2F depending on raw/semitrans flags. */
    const uint8_t command = cpu->read_byte(packet + 7u);
    if ((command & 0xFCu) != 0x2Cu) return;

    const int32_t x0 = signed_x(cpu, packet + 8u);
    const int32_t x1 = signed_x(cpu, packet + 16u);
    const int32_t x2 = signed_x(cpu, packet + 24u);
    const int32_t x3 = signed_x(cpu, packet + 32u);

    /* These audited packets are axis-aligned billboards.  Mirrored animation
     * frames legitimately reverse the horizontal winding (right/left instead
     * of left/right), while preserving both duplicated vertical edges.  Fail
     * closed only when those duplicate edges no longer describe a rectangle. */
    if (x0 != x2 || x1 != x3) return;
    const int32_t left = std::min(x0, x1);
    const int32_t right = std::max(x0, x1);
    const int32_t width = right - left + 1;
    if (width <= 0 || width > kMaximumBillboardWidth) return;

    /* The right coordinate is inclusive.  This exactly recovers the source
     * centre for both odd and even widths (left = centre - floor(width/2)). */
    const int32_t anchor_x = left + width / 2;
    /* The renderer fingerprints the completed command through CPUState.  This
     * lets an explicit tag outlive a short DMA delay while still failing closed
     * the instant the packet-pool slot is reused for different contents. */
    gpu_ws_tag_primitive(cpu, packet, anchor_x);
}

}  // namespace

extern "C" void disruptor_billboard_aspect_instruction_hook(
        CPUState *cpu, uint32_t pc, uint32_t instruction, int phase) {
    if (!cpu || comparator_active() ||
        phase != DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION) {
        return;
    }
    const BillboardPacketSite *site = billboard_packet_site(pc, instruction);
    if (!site ||
        (g_billboard_packet_site_mask & billboard_packet_site_bit(site)) == 0) {
        return;
    }
    tag_completed_billboard(cpu, site->packet_gpr);
}

extern "C" uint32_t disruptor_billboard_aspect_get_site_mask(void) {
    return g_billboard_packet_site_mask;
}

extern "C" void disruptor_billboard_aspect_set_site_mask(uint32_t mask) {
    g_billboard_packet_site_mask = mask & kAllBillboardPacketSites;
}

extern "C" uint32_t disruptor_billboard_aspect_all_site_mask(void) {
    return kAllBillboardPacketSites;
}
