#include "cpu_state.h"
#include "mod_plugins.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

std::array<std::uint8_t, 2 * 1024 * 1024> g_ram{};
PSXModFunctionEntryCallback g_renderer_callback = nullptr;
std::uint32_t g_registered_address = 0;
bool g_game_started = false;
bool g_netplay = false;
int g_gte_writes = 0;
int g_projection_writes = 0;
double g_projection_center = 120.0;
std::vector<std::uint32_t> g_guest_writes;
int g_failures = 0;

std::size_t physical(std::uint32_t address) {
    return static_cast<std::size_t>(address & 0x001FFFFFu);
}

void seed_byte(std::uint32_t address, std::uint8_t value) {
    g_ram[physical(address)] = value;
}

void seed_half(std::uint32_t address, std::uint16_t value) {
    const std::size_t p = physical(address);
    g_ram[p + 0] = static_cast<std::uint8_t>(value >> 0);
    g_ram[p + 1] = static_cast<std::uint8_t>(value >> 8);
}

void seed_word(std::uint32_t address, std::uint32_t value) {
    const std::size_t p = physical(address);
    g_ram[p + 0] = static_cast<std::uint8_t>(value >> 0);
    g_ram[p + 1] = static_cast<std::uint8_t>(value >> 8);
    g_ram[p + 2] = static_cast<std::uint8_t>(value >> 16);
    g_ram[p + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::uint16_t load_half(std::uint32_t address) {
    const std::size_t p = physical(address);
    return static_cast<std::uint16_t>(g_ram[p + 0]) |
           (static_cast<std::uint16_t>(g_ram[p + 1]) << 8);
}

std::uint32_t load_word(std::uint32_t address) {
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

int psx_mod_register_function_entry_plugin(
        const char *id, std::uint32_t address,
        PSXModFunctionEntryCallback callback) {
    if (!id || std::string(id) != "disruptor.vertical_camera.renderer" ||
        !address || !callback || g_renderer_callback) {
        return 0;
    }
    g_registered_address = address;
    g_renderer_callback = callback;
    return 1;
}

int psx_mod_game_started(void) {
    return g_game_started ? 1 : 0;
}

int psx_netplay_active(void) {
    return g_netplay ? 1 : 0;
}

std::uint8_t psx_mod_read_byte(std::uint32_t address) {
    return g_ram[physical(address)];
}

void psx_mod_write_byte(std::uint32_t address, std::uint8_t value) {
    g_guest_writes.push_back(address);
    seed_byte(address, value);
}

std::uint16_t psx_mod_read_half(std::uint32_t address) {
    return load_half(address);
}

void psx_mod_write_half(std::uint32_t address, std::uint16_t value) {
    g_guest_writes.push_back(address);
    seed_half(address, value);
}

std::uint32_t psx_mod_read_word(std::uint32_t address) {
    return load_word(address);
}

void psx_mod_write_word(std::uint32_t address, std::uint32_t value) {
    g_guest_writes.push_back(address);
    seed_word(address, value);
}

void gte_write_ctrl(CPUState *cpu, std::uint8_t reg, std::uint32_t value) {
    ++g_gte_writes;
    if (cpu && reg < 32u) cpu->gte_ctrl[reg] = value;
}

void gpu_geometry_camera_projection_center_y_set(double center_y) {
    ++g_projection_writes;
    g_projection_center = center_y;
}

}  // extern "C"

// Include the implementation to exercise the version-pinned internal guards.
#include "../src/disruptor_vertical_camera.cpp"

namespace {

constexpr std::uint32_t kTestPlayer = 0x80010000u;
constexpr std::uint32_t kTestProjectile = 0x80012000u;

void reset_state() {
    g_ram.fill(0);
    g_game_started = false;
    g_netplay = false;
    g_ls_replay_active = 0;
    g_ls_mode = 0;
    g_gte_writes = 0;
    g_projection_writes = 0;
    g_projection_center = 120.0;
    g_guest_writes.clear();
    g_requested_pitch.store(0.0);
    g_effective_pitch.store(0.0);
    g_effective_slope.store(0.0);
    g_effective_center_y.store(kNeutralCenterY);
    g_effective_active.store(0);
}

void seed_live_game() {
    g_game_started = true;
    seed_word(kMaximumHealth, 100u);
    seed_word(kCurrentHealth, 80u);
    seed_byte(kSceneMode, 0u);
    seed_byte(kScriptedCamera, 0u);
    seed_word(kPlayerPointer, kTestPlayer);
    seed_byte(kTestPlayer + kPlayerInactiveOffset, 0u);
    seed_word(kAimTargetPointer, 0u);
}

void activate(CPUState *cpu, double pitch) {
    disruptor_vertical_camera_set_requested_pitch(pitch);
    g_renderer_callback(cpu, kRendererEntry);
}

void test_registration_and_projection() {
    expect(g_renderer_callback != nullptr, "renderer callback registered");
    expect(g_registered_address == kRendererEntry,
           "renderer callback pinned to main body entry");

    reset_state();
    seed_live_game();
    CPUState cpu{};
    activate(&cpu, 10.0);
    const int expected_center = center_for_pitch(10.0);
    expect(disruptor_vertical_camera_active() == 1,
           "live non-zero pitch activates");
    expect(disruptor_vertical_camera_effective_center_y() == expected_center,
           "projection center derives from pitch");
    expect(std::abs(disruptor_vertical_camera_effective_slope() -
                    static_cast<double>(expected_center - 120) / 160.0) <
               1e-12,
           "aim slope derives from integer projection center");
    expect(cpu.gte_ctrl[25] ==
               static_cast<std::uint32_t>(expected_center << 16),
           "renderer entry writes GTE OFY");
    expect(g_projection_center == expected_center,
           "renderer entry publishes presentation center");

    cpu.gpr[2] = 120u;  // vanilla LI result
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003AD54u, 0x34020078u,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == static_cast<std::uint32_t>(expected_center),
           "verified CPU projection literal follows active center");

    cpu.gpr[3] = 120u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003D398u, 0x34030078u,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[3] == static_cast<std::uint32_t>(expected_center),
           "LI destination is decoded rather than assumed");

    cpu.gpr[2] = 120u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003AD54u, 0x34020079u,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 120u, "opcode mismatch is an exact no-op");

    g_netplay = true;
    cpu.gpr[2] = 120u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003AD54u, 0x34020078u,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 120u,
           "mid-frame netplay transition blocks CPU projection mutation");
}

void test_restore_and_zero_identity() {
    reset_state();
    seed_live_game();
    CPUState cpu{};
    activate(&cpu, -12.0);
    const double requested = disruptor_vertical_camera_requested_pitch();
    disruptor_vertical_camera_instruction_hook(
        &cpu, kRendererRestore, kRendererRestoreOpcode,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(!disruptor_vertical_camera_active() &&
               disruptor_vertical_camera_effective_center_y() == 120,
           "renderer exit restores neutral effective projection");
    expect(cpu.gte_ctrl[25] == (120u << 16) &&
               g_projection_center == 120.0,
           "renderer exit restores GTE and presentation centers");
    expect(disruptor_vertical_camera_requested_pitch() == requested,
           "temporary renderer restore preserves requested pitch");

    const int writes_before = g_gte_writes + g_projection_writes;
    disruptor_vertical_camera_recenter();
    g_renderer_callback(&cpu, kRendererEntry);
    expect(g_gte_writes + g_projection_writes == writes_before,
           "already-neutral zero pitch performs no projection writes");

    cpu.gpr[2] = 120u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003AD54u, 0x34020078u,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 120u, "zero pitch preserves vanilla LI exactly");
}

void test_gameplay_guards() {
    reset_state();
    CPUState cpu{};
    disruptor_vertical_camera_set_requested_pitch(10.0);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(!disruptor_vertical_camera_active(),
           "renderer fails closed before live gameplay");

    seed_live_game();
    expect(disruptor_vertical_camera_input_allowed() == 1,
           "verified live gameplay accepts vertical input");
    seed_byte(kScriptedCamera, 1u);
    expect(disruptor_vertical_camera_input_allowed() == 0,
           "scripted camera blocks hidden vertical input accumulation");
    g_renderer_callback(&cpu, kRendererEntry);
    expect(!disruptor_vertical_camera_active(),
           "scripted camera suspends pitch");

    seed_byte(kScriptedCamera, 0u);
    seed_byte(kSceneMode, 1u);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(!disruptor_vertical_camera_active(),
           "special renderer mode suspends pitch");

    seed_byte(kSceneMode, 0u);
    seed_byte(kTestPlayer + kPlayerInactiveOffset, 1u);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(!disruptor_vertical_camera_active(),
           "inactive player object suspends pitch");

    seed_byte(kTestPlayer + kPlayerInactiveOffset, 0u);
    g_netplay = true;
    const double before = disruptor_vertical_camera_requested_pitch();
    disruptor_vertical_camera_set_requested_pitch(20.0);
    expect(disruptor_vertical_camera_requested_pitch() == before,
           "netplay rejects non-zero input mutation");
    g_renderer_callback(&cpu, kRendererEntry);
    expect(!disruptor_vertical_camera_active(),
           "netplay never activates vertical camera");
}

void test_cull_widening() {
    reset_state();
    seed_live_game();
    CPUState cpu{};
    activate(&cpu, 22.0);

    cpu.gpr[16] = 100u;  // depth/native vertical bound
    cpu.gpr[5] = 130u;   // +Y: vanilla 100 < 130 rejects
    cpu.gpr[2] = 1u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003B900u, 0x0205102Au,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 0u,
           "positive vertical plane is conservatively widened");

    cpu.gpr[5] = static_cast<std::uint32_t>(-130);
    cpu.gpr[2] = 1u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003B90Cu, 0x0202102Au,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 0u,
           "negative vertical plane is conservatively widened");

    cpu.gpr[16] = static_cast<std::uint32_t>(-999);  // unrelated in this path
    cpu.gpr[20] = 100u;
    cpu.gpr[5] = 190u;  // vanilla (100+64) < 190 rejects
    cpu.gpr[2] = 1u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003CDB8u, 0x0065102Au,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 0u,
           "padded primitive plane uses its verified $s4 depth");
}

void seed_projectile(std::int16_t x, std::int16_t z, std::int16_t y) {
    seed_half(kTestProjectile + kVelocityXOffset,
              static_cast<std::uint16_t>(x));
    seed_half(kTestProjectile + kVelocityZOffset,
              static_cast<std::uint16_t>(z));
    seed_half(kTestProjectile + kVelocityYOffset,
              static_cast<std::uint16_t>(y));
}

std::int16_t projectile_y() {
    return static_cast<std::int16_t>(
        load_half(kTestProjectile + kVelocityYOffset));
}

void test_projectile_alignment() {
    reset_state();
    seed_live_game();
    CPUState cpu{};
    cpu.gpr[16] = kTestProjectile;
    disruptor_vertical_camera_set_requested_pitch(10.0);
    seed_projectile(300, 400, 10);
    const double slope = static_cast<double>(center_for_pitch(10.0) - 120) /
                         160.0;
    const std::int16_t expected = static_cast<std::int16_t>(
        10 + std::lround(500.0 * slope));
    disruptor_vertical_camera_instruction_hook(
        &cpu, kNormalProjectileSite, kNormalProjectileOpcode,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(projectile_y() == expected,
           "normal projectile follows the exact visible horizon slope");

    seed_projectile(300, 400, 10);
    seed_word(kAimTargetPointer, 0x80013000u);
    g_guest_writes.clear();
    disruptor_vertical_camera_instruction_hook(
        &cpu, kPsionicProjectileSite, kPsionicProjectileOpcode,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(projectile_y() == 10 && g_guest_writes.empty(),
           "native target-assisted aim remains authoritative");

    seed_word(kAimTargetPointer, 0u);
    disruptor_vertical_camera_set_requested_pitch(22.0);
    seed_projectile(32767, 32767, 30000);
    disruptor_vertical_camera_instruction_hook(
        &cpu, kPsionicProjectileSite, kPsionicProjectileOpcode,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(projectile_y() == std::numeric_limits<std::int16_t>::max(),
           "psionic vertical velocity saturates instead of wrapping");
}

void test_lockstep_replay_is_side_effect_free() {
    reset_state();
    seed_live_game();
    CPUState cpu{};
    activate(&cpu, 10.0);
    cpu.gpr[16] = kTestProjectile;
    seed_projectile(300, 400, 10);
    g_guest_writes.clear();
    const double requested = disruptor_vertical_camera_requested_pitch();
    const int center = disruptor_vertical_camera_effective_center_y();
    const int gte_writes = g_gte_writes;
    const int projection_writes = g_projection_writes;

    g_ls_replay_active = 1;
    disruptor_vertical_camera_set_requested_pitch(20.0);
    disruptor_vertical_camera_recenter();
    g_renderer_callback(&cpu, kRendererEntry);
    cpu.gpr[2] = 77u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003B900u, 0x0205102Au,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    disruptor_vertical_camera_instruction_hook(
        &cpu, kNormalProjectileSite, kNormalProjectileOpcode,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);

    expect(disruptor_vertical_camera_requested_pitch() == requested &&
               disruptor_vertical_camera_effective_center_y() == center,
           "lockstep replay cannot mutate camera globals");
    expect(cpu.gpr[2] == 77u && projectile_y() == 10,
           "lockstep replay preserves guest registers and RAM");
    expect(g_guest_writes.empty() && g_gte_writes == gte_writes &&
               g_projection_writes == projection_writes,
           "lockstep replay emits no host or guest writes");

    g_ls_replay_active = 0;
    g_ls_mode = 1;
    cpu.gpr[2] = 55u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003B900u, 0x0205102Au,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(cpu.gpr[2] == 55u &&
               disruptor_vertical_camera_effective_center_y() == center &&
               g_guest_writes.empty() && g_gte_writes == gte_writes &&
               g_projection_writes == projection_writes,
           "lockstep record is neutral as well as replay");
    g_ls_mode = 0;
}

void test_requested_pitch_validation() {
    reset_state();
    disruptor_vertical_camera_set_requested_pitch(100.0);
    expect(disruptor_vertical_camera_requested_pitch() == 22.0,
           "requested pitch clamps to verified positive limit");
    disruptor_vertical_camera_set_requested_pitch(-100.0);
    expect(disruptor_vertical_camera_requested_pitch() == -22.0,
           "requested pitch clamps to verified negative limit");
    disruptor_vertical_camera_set_requested_pitch(
        std::numeric_limits<double>::quiet_NaN());
    expect(disruptor_vertical_camera_requested_pitch() == -22.0,
           "non-finite pitch is rejected rather than recentering");
}

}  // namespace

int main() {
    test_registration_and_projection();
    test_restore_and_zero_identity();
    test_gameplay_guards();
    test_cull_widening();
    test_projectile_alignment();
    test_lockstep_replay_is_side_effect_free();
    test_requested_pitch_validation();

    if (g_failures) {
        std::cerr << g_failures << " vertical-camera test(s) failed\n";
        return 1;
    }
    std::cout << "Disruptor vertical camera contracts: PASS\n";
    return 0;
}
