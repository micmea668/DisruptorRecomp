/*
 * Camera depth of Disruptor's CPU-projected world sprites (SLUS-00224).
 *
 * Pickups, enemies and effects are projected by the game's own code as
 * x = 160 + 160 * X / Z, then built into a POLY_FT4 around that point. The
 * GTE never sees them, so the renderer has no depth for them and cannot
 * move them with the camera between two game frames. An instruction seam
 * at each projection reads Z, and the packet seam of the same funnel hands
 * it over with the packet. The fourth funnel can defer an actor
 * into a sorted record instead: its Z waits under the record's index.
 * The same seam reads X and Y, so the remainders of the two divisions go
 * along: the sprite stands between pixels like the walls around it. Its
 * size goes along too, before the game cuts it to whole pixels. The third
 * funnel sizes an effect after its projection: the size is worked out
 * ahead and holds only if the game's registers agree at the packet.
 * Guest state is only read, but for the range a funnel sorts and fades by:
 * under the widescreen squash it is made the squashed world's.
 */

#include "cpu_state.h"
#include "gpu.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

extern "C" void gte_ws_x_squash(int32_t *num, int32_t *den);

namespace {

constexpr uint32_t kProjectWord = 0xAFA20010u;  /* sw $v0, 16($sp): the projected x is stored */
constexpr uint32_t kRecordSite = 0x8003D1B4u;
constexpr uint32_t kRecordWord = 0xA4379EA0u;  /* sh $s7, record.key */
constexpr uint32_t kRecordBytes = 20u;
constexpr uint32_t kDeferredPacketSite = 0x800433A0u;
constexpr uint32_t kDeferredProjection = 0x8003D078u;
/* Measured on a strafe: without this factor the camera puts no moving sprite face within 1.5 px of where it was. */
constexpr int32_t kGteDepthPerSpriteDepth = 8;
constexpr uint32_t kFocal = 160u;
constexpr int64_t kOne = 65536;
constexpr int32_t kFarColumn = 4096;  /* past this a 16.16 column no longer fits */
constexpr int32_t kHorizon = 120;
constexpr uint32_t kReciprocals = 0x80057E98u;  /* 65536 / n in 16 bits, a table the game builds at start */
constexpr uint32_t kCameraHeight = 0x800775D0u;
constexpr uint32_t kShadowPacketSite = 0x8003D488u;
constexpr uint32_t kEffectPacketSite = 0x8003C848u;
constexpr uint32_t kWarped = 1u;
constexpr int32_t kLargest = 0x1FF;  /* the game caps a side here and rescales the other */
constexpr int64_t kTaken = 3 * kOne / 2;  /* the renderer takes a size this far over its packet's, an effect's two cuts can leave more */
constexpr int64_t kBodyLead = 0x18;  /* 0x8003CEC8..0x8003CEEC: the fourth funnel sorts this much nearer, down to kNearest */
constexpr int64_t kNearest = 8;

/* Where a funnel keeps the size it scales by 160 / Z. */
enum class Size : uint8_t { Words, Bytes, Effect, Body };

/* A register, or a stack slot when gpr is 0. */
struct Operand {
    uint8_t gpr;
    uint8_t stack;
};

struct Funnel {
    uint32_t projection;
    uint8_t depth_gpr;
    std::array<uint32_t, 2> packets;
    Operand x;
    Operand y;
    Size size;
    uint8_t range_gpr;
    bool led;  /* the range has had its lead by the time of the projection */
};

constexpr auto kFunnels = std::array<Funnel, 4>{{
    {0x8003B98Cu, 16u, {0x8003BB88u, 0u}, {7u, 0u}, {5u, 0u}, Size::Words, 18u, false},
    {0x8003BDA0u, 16u, {0x8003BFB0u, 0u}, {7u, 0u}, {6u, 0u}, Size::Bytes, 19u, false},
    {0x8003C4B0u, 21u, {kEffectPacketSite, 0x8003CAD4u}, {0u, 0x18u}, {0u, 0x20u}, Size::Effect, 23u, false},
    {kDeferredProjection, 20u, {kShadowPacketSite, 0u}, {8u, 0u}, {5u, 0u}, Size::Body, 23u, true},
}};

/* 16.16: the unrounded column of the projection, and how far it stands from the row the game stored. */
struct Place {
    int32_t x;
    int32_t dy;
};

/* A size in the game's units, before 160 / Z. */
struct Extent {
    int32_t wide;
    int32_t tall;
};

/* 16.16: the unrounded row the rectangle is centred on, and its unrounded sides. */
struct Shape {
    int32_t row;
    int32_t width;
    int32_t height;
    bool shadow;
};

struct Sprite {
    int32_t depth = 0;
    std::optional<Place> place;
    std::optional<Shape> shape;
    /* What an effect's first packet has to show in $s6 and $s2 for the shape to be the game's. */
    std::optional<Extent> cut;
};

uint32_t g_pending_projection = 0;
Sprite g_pending{};
std::array<Sprite, 256> g_record{};

[[nodiscard]] bool readable(const CPUState& cpu) {
    return cpu.read_word && cpu.read_half && cpu.read_byte;
}

[[nodiscard]] std::optional<int32_t> operand(const CPUState& cpu, Operand source) {
    if (source.gpr != 0u) return static_cast<int32_t>(cpu.gpr[source.gpr]);
    if (!cpu.read_word) return std::nullopt;
    return static_cast<int32_t>(cpu.read_word(cpu.gpr[29] + source.stack));
}

[[nodiscard]] constexpr int32_t scaled(int64_t units, int32_t depth) {
    return static_cast<int32_t>(static_cast<int64_t>(kFocal) * units * kOne / depth);
}

[[nodiscard]] constexpr int32_t low_product(int32_t left, int32_t right) {
    return static_cast<int32_t>(static_cast<uint32_t>(left) * static_cast<uint32_t>(right));
}

/* The fourth funnel's own steps from 0x8003CE60 to 0x8003CF88. Nothing when they do not end on the game's $s2 and $s3. */
[[nodiscard]] std::optional<Extent> body_extent(const CPUState& cpu, int32_t depth) {
    const uint32_t actor = cpu.gpr[21], body = cpu.gpr[22];
    const uint32_t kind = cpu.read_byte(actor + 0x3Au);
    const uint32_t frame = cpu.read_byte(actor + 0x3Bu) + (static_cast<uint8_t>(kind - 7u) < 2u ? 0u : cpu.gpr[16] & 0xFFu);
    const uint32_t picture = cpu.read_word(body) + 20u * frame;
    const int32_t scale = cpu.read_half(kReciprocals + 2u * cpu.read_word(body + 0xCu));
    const int32_t near = static_cast<int32_t>(kFocal) * cpu.read_half(kReciprocals + 2u * static_cast<uint32_t>(depth));
    const Extent extent{
        .wide = low_product(low_product(static_cast<int32_t>(cpu.read_word(body + 0x1Cu)), cpu.read_byte(picture + 8u)), scale) >> 16,
        .tall = low_product(low_product(static_cast<int32_t>(cpu.read_word(body + 0x18u)), cpu.read_byte(picture + 9u)), scale) >> 16,
    };
    const int32_t width = low_product(extent.wide, near) >> 16, height = low_product(extent.tall, near) >> 16;
    if (width <= 0 || height <= 0 || width > kLargest || height > kLargest ||
        width != static_cast<int32_t>(cpu.gpr[18]) || height != static_cast<int32_t>(cpu.gpr[19])) {
        return std::nullopt;
    }
    return extent;
}

/* One side of an effect: 16.16 before the game's two cuts, and what those cuts leave of it. */
struct Side {
    int32_t exact;
    int32_t cut;
};

struct Effect {
    Side wide;
    Side tall;
};

/* units * part / whole over the depth, cut once to whole pixels and once more after the ratio. */
[[nodiscard]] constexpr std::optional<Side> effect_side(int32_t units, int32_t part, int32_t whole, int32_t depth) {
    const int32_t cut = low_product(static_cast<int32_t>(kFocal) * units / depth, part) / whole;
    if (cut <= 0 || cut > kLargest) return std::nullopt;
    const int64_t exact = static_cast<int64_t>(kFocal) * units * part * kOne / (static_cast<int64_t>(depth) * whole);
    return Side{.exact = static_cast<int32_t>(std::min(exact, cut * kOne + kTaken)), .cut = cut};
}

/* The third funnel's own steps from 0x8003C4D4 to 0x8003C5F4, which run after its projection. */
[[nodiscard]] std::optional<Effect> effect_size(const CPUState& cpu, int32_t depth) {
    const uint32_t object = cpu.gpr[19], picture = cpu.gpr[20], kind = cpu.read_word(cpu.gpr[29] + 0x28u);
    const uint32_t which = cpu.read_byte(object + 0x1Du);
    const auto units = static_cast<int32_t>(cpu.read_word(kind + (which != 0u ? 0x18u : 0x14u)));
    const int32_t across = cpu.read_byte(kind + which + 0x30u), down = cpu.read_byte(kind + which + 0x34u);
    if (units <= 0 || units > 0xFFFF || across == 0 || down == 0) return std::nullopt;
    std::optional<Side> wide = effect_side(units, cpu.read_byte(picture + 8u), across, depth);
    const std::optional<Side> tall = effect_side(units, cpu.read_byte(picture + 9u), down, depth);
    if (!wide || !tall) return std::nullopt;
    if ((cpu.gpr[30] & 0xFFu) != 0u) {
        /* A mirrored packet runs back from x + (w + 1) / 2 over w + 1 columns, two more than one that is not. */
        wide = Side{.exact = wide->exact + 2 * static_cast<int32_t>(kOne), .cut = -wide->cut};
    }
    return Effect{.wide = *wide, .tall = *tall};
}

[[nodiscard]] std::optional<Extent> extent_of(const CPUState& cpu, const Funnel& funnel, int32_t depth) {
    if (!readable(cpu)) return std::nullopt;
    std::optional<Extent> extent;
    if (funnel.size == Size::Words) {
        extent = Extent{.wide = static_cast<int32_t>(cpu.read_word(cpu.gpr[21] + 4u)),
                        .tall = static_cast<int32_t>(cpu.read_word(cpu.gpr[21] + 8u))};
    } else if (funnel.size == Size::Bytes) {
        extent = Extent{.wide = cpu.read_byte(cpu.gpr[18] + 8u), .tall = cpu.read_byte(cpu.gpr[18] + 9u)};
    } else {
        extent = body_extent(cpu, depth);
    }
    if (!extent || extent->wide <= 0 || extent->tall <= 0 || extent->wide > 0xFFFF || extent->tall > 0xFFFF) return std::nullopt;
    return extent;
}

/* The shadow block 0x8003D22C..0x8003D488 hangs its own rectangle on the caster's column. Nothing when its numbers do not give the packet. */
[[nodiscard]] std::optional<Shape> shadow_shape(const CPUState& cpu, uint32_t packet, int32_t depth) {
    const uint32_t actor = cpu.gpr[21], body = cpu.gpr[22];
    const Extent extent{.wide = static_cast<int32_t>(cpu.read_word(body + 0xF8u)),
                        .tall = static_cast<int32_t>(cpu.read_word(body + 0xFCu))};
    const int32_t drop = static_cast<int32_t>(cpu.read_half(actor + 0x28u)) - static_cast<int32_t>(cpu.read_word(body + 0xF4u)) -
                         static_cast<int32_t>(cpu.read_word(kCameraHeight));
    const int32_t width = low_product(static_cast<int32_t>(kFocal), extent.wide) / depth;
    const int32_t height = low_product(static_cast<int32_t>(kFocal), extent.tall) / depth;
    const int32_t fall = low_product(static_cast<int32_t>(kFocal), drop) / depth;
    const uint32_t first = cpu.read_word(packet + 8u);
    const auto left = static_cast<int16_t>(first), top = static_cast<int16_t>(first >> 16);
    const auto right = static_cast<int16_t>(cpu.read_word(packet + 16u)), bottom = static_cast<int16_t>(cpu.read_word(packet + 24u) >> 16);
    if (width <= 0 || height <= 0 || right - left + 1 != width || bottom - top + 1 != height ||
        top != kHorizon - fall - (height >> 1)) {
        return std::nullopt;
    }
    return Shape{.row = static_cast<int32_t>(kHorizon * kOne) - scaled(drop, depth),
                 .width = scaled(extent.wide, depth),
                 .height = scaled(extent.tall, depth),
                 .shadow = true};
}

[[nodiscard]] Sprite projected(const CPUState& cpu, const Funnel& funnel) {
    Sprite sprite{.depth = static_cast<int32_t>(cpu.gpr[funnel.depth_gpr])};
    const std::optional<int32_t> x = operand(cpu, funnel.x), y = operand(cpu, funnel.y);
    /* $a0 is the screen warp's switch in every funnel: on, 0x8003B6E0 goes on to move the stored point. */
    if (sprite.depth <= 0 || !x || !y || cpu.gpr[4] == kWarped) return sprite;
    const auto across = static_cast<int32_t>(kFocal * static_cast<uint32_t>(*x));
    const auto down = static_cast<int32_t>(kFocal * static_cast<uint32_t>(*y));
    const int32_t column = static_cast<int32_t>(kFocal) + across / sprite.depth;
    if (column < -kFarColumn || column > kFarColumn) return sprite;
    /* $v1 and $v0 still hold what the game made of them: anything else and these are not its operands. */
    if (static_cast<int32_t>(cpu.gpr[3]) != down / sprite.depth ||
        static_cast<int32_t>(cpu.gpr[2]) != psx_ws_project_x(column)) {
        return sprite;
    }
    const auto fraction = static_cast<int32_t>(across % sprite.depth * kOne / sprite.depth);
    const Place place{.x = psx_ws_project_x16(column, fraction),
                      .dy = static_cast<int32_t>(-(down % sprite.depth) * kOne / sprite.depth)};
    sprite.place = place;
    const int32_t row = static_cast<int32_t>((kHorizon - down / sprite.depth) * kOne) + place.dy;
    if (funnel.size == Size::Effect) {
        if (const std::optional<Effect> effect = readable(cpu) ? effect_size(cpu, sprite.depth) : std::nullopt) {
            sprite.shape = Shape{.row = row, .width = effect->wide.exact, .height = effect->tall.exact, .shadow = false};
            sprite.cut = Extent{.wide = effect->wide.cut, .tall = effect->tall.cut};
        }
    } else if (const std::optional<Extent> extent = extent_of(cpu, funnel, sprite.depth)) {
        sprite.shape = Shape{.row = row,
                             .width = scaled(extent->wide, sprite.depth),
                             .height = scaled(extent->tall, sprite.depth),
                             .shadow = false};
    }
    return sprite;
}

/* A funnel's range, as at 0x8003B8C4..0x8003B8D4: the depth and three eighths of the step aside. */
[[nodiscard]] constexpr int64_t range_of(const Funnel& funnel, int32_t depth, int64_t aside) {
    const int64_t range = depth + (3 * aside >> 3);
    return funnel.led ? std::max(range - kBodyLead, kNearest) : range;
}

/* The world renderer ranges a vertex by its squashed column (0x80046360): a sprite's step aside has to count for as little, or the floor under it sorts in front. */
void squash_range(CPUState& cpu, const Funnel& funnel) {
    int32_t num = 1, den = 1;
    gte_ws_x_squash(&num, &den);
    const std::optional<int32_t> x = operand(cpu, funnel.x);
    const auto depth = static_cast<int32_t>(cpu.gpr[funnel.depth_gpr]);
    if (num <= 0 || den <= 0 || !x || depth <= 0) return;
    const int64_t aside = *x < 0 ? -static_cast<int64_t>(*x) : *x;
    if (static_cast<int32_t>(cpu.gpr[funnel.range_gpr]) != range_of(funnel, depth, aside)) return;
    cpu.gpr[funnel.range_gpr] = static_cast<uint32_t>(range_of(funnel, depth, aside * num / den));
}

[[nodiscard]] Sprite take_pending(uint32_t projection) {
    const Sprite sprite = g_pending_projection == projection ? g_pending : Sprite{};
    g_pending_projection = 0;
    g_pending = Sprite{};
    return sprite;
}

}  // namespace

extern "C" void disruptor_sprite_depth_instruction_hook(
        CPUState *cpu, uint32_t address, uint32_t instruction, int phase) {
    if (!cpu || phase != 1) return;
    if (address == kRecordSite) {
        const uint32_t offset = cpu->gpr[3];
        const std::size_t index = offset / kRecordBytes;
        if (instruction != kRecordWord || offset % kRecordBytes != 0u ||
            index >= g_record.size()) {
            return;
        }
        g_record[index] =
            g_pending_projection == kDeferredProjection ? g_pending : Sprite{};
        return;
    }
    if (instruction != kProjectWord) return;
    for (const Funnel& funnel : kFunnels) {
        if (funnel.projection != address) continue;
        g_pending_projection = address;
        g_pending = projected(*cpu, funnel);
        squash_range(*cpu, funnel);
        return;
    }
}

extern "C" void disruptor_sprite_depth_packet(
        CPUState *cpu, uint32_t packet_site, uint32_t packet) {
    if (!cpu) return;
    Sprite sprite{};
    if (packet_site == kDeferredPacketSite) {
        const std::size_t index = cpu->gpr[22];
        if (index < g_record.size()) {
            sprite = g_record[index];
            g_record[index] = Sprite{};
        }
    } else {
        for (const Funnel& funnel : kFunnels) {
            if (funnel.packets[0] == packet_site || funnel.packets[1] == packet_site) {
                sprite = take_pending(funnel.projection);
                break;
            }
        }
    }
    if (sprite.cut && (packet_site != kEffectPacketSite || static_cast<int32_t>(cpu->gpr[22]) != sprite.cut->wide ||
                       static_cast<int32_t>(cpu->gpr[18]) != sprite.cut->tall)) {
        sprite.shape.reset();
    }
    if (packet_site == kShadowPacketSite && sprite.place) {
        /* The row and the size so far are the caster's. */
        sprite.place->dy = 0;
        sprite.shape = readable(*cpu) ? shadow_shape(*cpu, packet, sprite.depth) : std::nullopt;
    }
    if (sprite.depth <= 0 || sprite.depth > INT32_MAX / kGteDepthPerSpriteDepth) return;
    gpu_temporal_note_sprite(cpu, packet, sprite.depth * kGteDepthPerSpriteDepth);
    if (!sprite.place) return;
    gpu_temporal_place_sprite(packet, sprite.place->x, sprite.place->dy);
    if (sprite.shape) {
        gpu_temporal_size_sprite(packet, sprite.shape->row, sprite.shape->width, sprite.shape->height,
                                 sprite.shape->shadow, packet_site == kDeferredPacketSite);
    }
}
