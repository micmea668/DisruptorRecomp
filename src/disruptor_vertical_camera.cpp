/*
 * Game-aware vertical camera and weapon aim for Disruptor (SLUS-00224).
 *
 * The retail renderer is yaw-only, but its projection consistently treats
 * screen Y=120 as the horizon and uses GTE H=160.  Moving that horizon by
 * H*tan(pitch) therefore gives the game one coherent, bounded vertical-look
 * model without altering its world transform.  Exact resident instruction
 * hooks carry that same center through CPU-side projection, conservatively
 * widen the original vertical cull planes, and add the matching slope to
 * freshly-created projectiles.
 *
 * Every site is pinned by both PC and opcode.  At zero pitch, during lockstep
 * replay, in netplay, or outside verified live gameplay, the original guest
 * results and memory are left byte-for-byte untouched.
 */

#include "disruptor_vertical_camera.h"

#include "cpu_state.h"
#include "lockstep.h"
#include "mod_plugins.h"
#include "psx_netplay.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

extern "C" void gpu_geometry_camera_projection_center_y_set(double center_y);

namespace {

constexpr uint32_t kRendererEntry = 0x80040E68u;
constexpr uint32_t kRendererRestore = 0x8004279Cu;
constexpr uint32_t kRendererRestoreOpcode = 0x8FBF016Cu;

constexpr uint32_t kCurrentHealth = 0x80077660u;
constexpr uint32_t kMaximumHealth = 0x80077664u;
constexpr uint32_t kSceneMode = 0x8007145Cu;
constexpr uint32_t kScriptedCamera = 0x800714E0u;
constexpr uint32_t kPlayerPointer = 0x80071714u;
constexpr uint32_t kAimTargetPointer = 0x80071560u;
constexpr uint32_t kPlayerInactiveOffset = 0x7Fu;

constexpr int kNeutralCenterY = 120;
constexpr double kProjectionH = 160.0;
constexpr double kFullTurnUnits = 256.0;
constexpr double kMaximumPitchUnits = 22.0;
constexpr double kPi = 3.14159265358979323846264338327950288;

struct CenterSite {
    uint32_t pc;
    uint32_t opcode;
};

constexpr std::array<CenterSite, 12> kCenterSites = {{
    {0x8003AD54u, 0x34020078u},
    {0x8003ADE4u, 0x34020078u},
    {0x8003B140u, 0x34020078u},
    {0x8003B1ECu, 0x34020078u},
    {0x8003B764u, 0x34020078u},
    {0x8003B990u, 0x34020078u},
    {0x8003BDA4u, 0x34020078u},
    {0x8003C4B4u, 0x34020078u},
    {0x8003C99Cu, 0x34020078u},
    {0x8003D07Cu, 0x34020078u},
    {0x8003D398u, 0x34030078u},
    {0x8003F6B0u, 0x34020078u},
}};

struct CullSite {
    uint32_t pc;
    uint32_t opcode;
    uint8_t vertical_register;
    uint8_t depth_register;
    bool negative_side;
};

constexpr std::array<CullSite, 6> kCullSites = {{
    {0x8003B900u, 0x0205102Au, 5u, 16u, false},
    {0x8003B90Cu, 0x0202102Au, 5u, 16u, true},
    {0x8003BD14u, 0x0206102Au, 6u, 16u, false},
    {0x8003BD20u, 0x0202102Au, 6u, 16u, true},
    /* This primitive path compares (depth+64), retained in $v1, with Y.
     * Use the unpadded depth in $s4 when calculating only the pitch margin. */
    {0x8003CDB8u, 0x0065102Au, 5u, 20u, false},
    {0x8003CDC4u, 0x0062102Au, 5u, 20u, true},
}};

constexpr uint32_t kNormalProjectileSite = 0x8002E4B8u;
constexpr uint32_t kNormalProjectileOpcode = 0x92640022u;
constexpr uint32_t kPsionicProjectileSite = 0x8002EAF0u;
constexpr uint32_t kPsionicProjectileOpcode = 0x92430020u;
constexpr uint32_t kVelocityXOffset = 0x10u;
constexpr uint32_t kVelocityZOffset = 0x12u;
constexpr uint32_t kVelocityYOffset = 0x14u;

std::atomic<double> g_requested_pitch{0.0};
std::atomic<double> g_effective_pitch{0.0};
std::atomic<double> g_effective_slope{0.0};
std::atomic<int> g_effective_center_y{kNeutralCenterY};
std::atomic<int> g_effective_active{0};

bool comparator_active() {
    /* The optional differential checker runs a compiled RECORD pass followed
     * by an interpreter REPLAY.  Camera hooks are deliberately outside the
     * architectural model being compared, so they must be neutral in both
     * halves; suppressing replay alone would manufacture a false divergence. */
    return g_ls_mode != 0 || g_ls_replay_active != 0;
}

bool valid_main_ram_pointer(uint32_t pointer, uint32_t trailing_bytes) {
    /* The verified player/entity pointers are KSEG0 aliases of 2 MiB main
     * RAM.  Requiring that exact region avoids host reads through MMIO,
     * scratchpad, BIOS, or a corrupt pointer when the game is transitioning. */
    constexpr uint32_t kRamFirst = 0x80000000u;
    constexpr uint32_t kRamLast = 0x801FFFFFu;
    return pointer >= kRamFirst && pointer <= kRamLast &&
           trailing_bytes <= kRamLast - pointer;
}

bool plausible_live_gameplay() {
    if (comparator_active() || psx_netplay_active() ||
        !psx_mod_game_started()) {
        return false;
    }

    const uint32_t maximum = psx_mod_read_word(kMaximumHealth);
    const uint32_t current = psx_mod_read_word(kCurrentHealth);
    if (maximum == 0u || maximum > 10000u ||
        current == 0u || current > maximum) {
        return false;
    }

    /* Scene mode 1 is the renderer's special non-gameplay presentation path.
     * Scripted camera movement and inactive player objects retain the retail
     * horizon so cutscenes, death sequences, and level transitions cannot be
     * steered by a stale host pitch. */
    if (psx_mod_read_byte(kSceneMode) == 1u ||
        psx_mod_read_byte(kScriptedCamera) != 0u) {
        return false;
    }

    const uint32_t player = psx_mod_read_word(kPlayerPointer);
    return valid_main_ram_pointer(player, kPlayerInactiveOffset) &&
           psx_mod_read_byte(player + kPlayerInactiveOffset) == 0u;
}

double clamp_pitch(double pitch) {
    if (!std::isfinite(pitch)) return 0.0;
    return std::clamp(pitch, -kMaximumPitchUnits, kMaximumPitchUnits);
}

int center_for_pitch(double pitch) {
    const double radians = pitch * (2.0 * kPi / kFullTurnUnits);
    return static_cast<int>(std::lround(
        static_cast<double>(kNeutralCenterY) +
        kProjectionH * std::tan(radians)));
}

void publish_neutral(CPUState *cpu) {
    if (comparator_active()) return;

    const int was_active = g_effective_active.load(std::memory_order_relaxed);
    const int old_center =
        g_effective_center_y.load(std::memory_order_relaxed);
    g_effective_pitch.store(0.0, std::memory_order_relaxed);
    g_effective_slope.store(0.0, std::memory_order_relaxed);
    g_effective_center_y.store(kNeutralCenterY, std::memory_order_relaxed);
    g_effective_active.store(0, std::memory_order_release);

    /* Zero pitch is an exact no-op once neutral.  A prior non-zero frame must
     * explicitly restore both guest GTE state and host presentation state. */
    if (!was_active && old_center == kNeutralCenterY) return;
    if (cpu) {
        gte_write_ctrl(cpu, 25u,
                       static_cast<uint32_t>(kNeutralCenterY << 16));
    }
    gpu_geometry_camera_projection_center_y_set(
        static_cast<double>(kNeutralCenterY));
}

void publish_pitch(CPUState *cpu, double pitch) {
    if (!cpu || comparator_active()) return;

    pitch = clamp_pitch(pitch);
    const int center = center_for_pitch(pitch);
    if (center == kNeutralCenterY) {
        publish_neutral(cpu);
        return;
    }
    const double slope =
        static_cast<double>(center - kNeutralCenterY) / kProjectionH;

    g_effective_pitch.store(pitch, std::memory_order_relaxed);
    g_effective_slope.store(slope, std::memory_order_relaxed);
    g_effective_center_y.store(center, std::memory_order_relaxed);
    g_effective_active.store(1, std::memory_order_release);

    gte_write_ctrl(cpu, 25u, static_cast<uint32_t>(center << 16));
    gpu_geometry_camera_projection_center_y_set(static_cast<double>(center));
}

void renderer_entry(CPUState *cpu, uint32_t address) {
    if (!cpu || address != kRendererEntry || comparator_active()) return;
    if (!plausible_live_gameplay()) {
        publish_neutral(cpu);
        return;
    }
    publish_pitch(cpu,
                  g_requested_pitch.load(std::memory_order_acquire));
}

const CenterSite *find_center_site(uint32_t pc, uint32_t opcode) {
    for (const CenterSite& site : kCenterSites) {
        if (site.pc == pc && site.opcode == opcode) return &site;
    }
    return nullptr;
}

const CullSite *find_cull_site(uint32_t pc, uint32_t opcode) {
    for (const CullSite& site : kCullSites) {
        if (site.pc == pc && site.opcode == opcode) return &site;
    }
    return nullptr;
}

void apply_center_literal(CPUState *cpu, uint32_t opcode) {
    if (!g_effective_active.load(std::memory_order_acquire) ||
        !plausible_live_gameplay()) {
        return;
    }
    const uint32_t destination = (opcode >> 16u) & 31u;
    if (destination == 0u) return;
    cpu->gpr[destination] = static_cast<uint32_t>(
        g_effective_center_y.load(std::memory_order_relaxed));
}

int64_t signed_register(const CPUState *cpu, uint8_t index) {
    return static_cast<int64_t>(static_cast<int32_t>(cpu->gpr[index]));
}

void widen_vertical_cull(CPUState *cpu, const CullSite& site) {
    if (!g_effective_active.load(std::memory_order_acquire) ||
        !plausible_live_gameplay()) {
        return;
    }
    const double slope =
        std::abs(g_effective_slope.load(std::memory_order_relaxed));
    if (slope == 0.0) return;

    const int64_t depth = signed_register(cpu, site.depth_register);
    const int64_t native_bound = depth +
        (site.depth_register == 20u ? 64 : 0);
    const int64_t margin = static_cast<int64_t>(std::llround(
        std::abs(static_cast<double>(depth)) * slope));
    const int64_t widened_bound = native_bound + margin;
    const int64_t vertical = signed_register(cpu, site.vertical_register);
    const int64_t compared_vertical = site.negative_side ? -vertical : vertical;

    /* Preserve the SLT result contract in $v0: one still means reject. */
    cpu->gpr[2] = widened_bound < compared_vertical ? 1u : 0u;
}

int16_t load_signed_half(uint32_t address) {
    return static_cast<int16_t>(psx_mod_read_half(address));
}

int16_t saturated_half(int64_t value) {
    value = std::clamp<int64_t>(
        value, std::numeric_limits<int16_t>::min(),
        std::numeric_limits<int16_t>::max());
    return static_cast<int16_t>(value);
}

void adjust_projectile(CPUState *cpu) {
    const int projectile_center = center_for_pitch(
        g_requested_pitch.load(std::memory_order_acquire));
    const double projectile_slope =
        static_cast<double>(projectile_center - kNeutralCenterY) /
        kProjectionH;
    if (projectile_slope == 0.0) return;

    if (!plausible_live_gameplay() ||
        psx_mod_read_word(kAimTargetPointer) != 0u) {
        return;
    }

    const uint32_t entity = cpu->gpr[16];  /* $s0 at both verified seams. */
    if (!valid_main_ram_pointer(entity, kVelocityYOffset + 1u)) return;

    const int16_t velocity_x = load_signed_half(entity + kVelocityXOffset);
    const int16_t velocity_z = load_signed_half(entity + kVelocityZOffset);
    const int16_t velocity_y = load_signed_half(entity + kVelocityYOffset);
    const double horizontal_speed = std::hypot(
        static_cast<double>(velocity_x), static_cast<double>(velocity_z));
    const int64_t pitch_velocity = static_cast<int64_t>(std::llround(
        horizontal_speed * projectile_slope));
    if (pitch_velocity == 0) return;

    const int16_t adjusted = saturated_half(
        static_cast<int64_t>(velocity_y) + pitch_velocity);
    psx_mod_write_half(entity + kVelocityYOffset,
                       static_cast<uint16_t>(adjusted));
}

PSX_MOD_CONSTRUCTOR(register_disruptor_vertical_camera) {
    if (!psx_mod_register_function_entry_plugin(
            "disruptor.vertical_camera.renderer", kRendererEntry,
            renderer_entry)) {
        std::fprintf(stderr,
                     "disruptor: failed to register vertical-camera renderer hook\n");
    }
}

}  // namespace

extern "C" void disruptor_vertical_camera_set_requested_pitch(
        double pitch_units) {
    if (comparator_active() || !std::isfinite(pitch_units)) return;
    if (psx_netplay_active() && pitch_units != 0.0) return;
    g_requested_pitch.store(clamp_pitch(pitch_units),
                            std::memory_order_release);
}

extern "C" void disruptor_vertical_camera_recenter(void) {
    if (comparator_active()) return;
    g_requested_pitch.store(0.0, std::memory_order_release);
}

extern "C" double disruptor_vertical_camera_requested_pitch(void) {
    return g_requested_pitch.load(std::memory_order_acquire);
}

extern "C" double disruptor_vertical_camera_effective_pitch(void) {
    return g_effective_pitch.load(std::memory_order_acquire);
}

extern "C" int disruptor_vertical_camera_effective_center_y(void) {
    return g_effective_center_y.load(std::memory_order_acquire);
}

extern "C" double disruptor_vertical_camera_effective_slope(void) {
    return g_effective_slope.load(std::memory_order_acquire);
}

extern "C" int disruptor_vertical_camera_active(void) {
    return g_effective_active.load(std::memory_order_acquire);
}

extern "C" int disruptor_vertical_camera_input_allowed(void) {
    return plausible_live_gameplay() ? 1 : 0;
}

extern "C" void disruptor_vertical_camera_instruction_hook(
        CPUState *cpu, uint32_t pc, uint32_t instruction, int phase) {
    if (!cpu || comparator_active()) return;

    if (phase == DISRUPTOR_VERTICAL_CAMERA_BEFORE_INSTRUCTION) return;
    if (phase != DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION) return;

    if (pc == kRendererRestore && instruction == kRendererRestoreOpcode) {
        publish_neutral(cpu);
        return;
    }

    if (find_center_site(pc, instruction)) {
        apply_center_literal(cpu, instruction);
        return;
    }
    if (const CullSite *site = find_cull_site(pc, instruction)) {
        widen_vertical_cull(cpu, *site);
        return;
    }
    if ((pc == kNormalProjectileSite &&
         instruction == kNormalProjectileOpcode) ||
        (pc == kPsionicProjectileSite &&
         instruction == kPsionicProjectileOpcode)) {
        adjust_projectile(cpu);
    }
}
