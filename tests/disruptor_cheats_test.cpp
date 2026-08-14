#include "cpu_state.h"
#include "mod_plugins.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::array<std::uint8_t, 2 * 1024 * 1024> g_ram{};
PSXModFunctionEntryCallback g_damage_callback = nullptr;
std::uint32_t g_registered_address = 0;
bool g_game_started = false;
bool g_netplay = false;
std::vector<std::uint32_t> g_write_addresses;
int g_failures = 0;

std::size_t physical(std::uint32_t address) {
    return static_cast<std::size_t>(address & 0x001FFFFFu);
}

void seed_byte(std::uint32_t address, std::uint8_t value) {
    g_ram[physical(address)] = value;
}

void seed_word(std::uint32_t address, std::uint32_t value) {
    const std::size_t p = physical(address);
    g_ram[p + 0] = static_cast<std::uint8_t>(value >> 0);
    g_ram[p + 1] = static_cast<std::uint8_t>(value >> 8);
    g_ram[p + 2] = static_cast<std::uint8_t>(value >> 16);
    g_ram[p + 3] = static_cast<std::uint8_t>(value >> 24);
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

int psx_mod_register_function_entry_plugin(
        const char *id, std::uint32_t address,
        PSXModFunctionEntryCallback callback) {
    if (!id || std::string(id) != "disruptor.god_mode.damage" ||
        !address || !callback || g_damage_callback)
        return 0;
    g_registered_address = address;
    g_damage_callback = callback;
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
    g_write_addresses.push_back(address);
    seed_byte(address, value);
}

std::uint16_t psx_mod_read_half(std::uint32_t address) {
    const std::size_t p = physical(address);
    return static_cast<std::uint16_t>(g_ram[p + 0]) |
           (static_cast<std::uint16_t>(g_ram[p + 1]) << 8);
}

void psx_mod_write_half(std::uint32_t address, std::uint16_t value) {
    g_write_addresses.push_back(address);
    seed_byte(address + 0u, static_cast<std::uint8_t>(value >> 0));
    seed_byte(address + 1u, static_cast<std::uint8_t>(value >> 8));
}

std::uint32_t psx_mod_read_word(std::uint32_t address) {
    return load_word(address);
}

void psx_mod_write_word(std::uint32_t address, std::uint32_t value) {
    g_write_addresses.push_back(address);
    seed_word(address, value);
}

}  // extern "C"

// Include the implementation to exercise its version-pinned internal guards.
#include "../src/disruptor_cheats.cpp"

namespace {

constexpr std::array<std::uint32_t, 8> kAmmo = {
    0u, 200u, 100u, 50u, 50u, 10u, 100u, 1u,
};

void reset_state() {
    g_ram.fill(0);
    g_game_started = false;
    g_netplay = false;
    g_write_addresses.clear();
    g_god_mode = false;
}

void seed_live_game() {
    g_game_started = true;
    seed_word(kMaximumHealth, 100u);
    seed_word(kCurrentHealth, 80u);
    for (std::size_t i = 0; i < kAmmo.size(); ++i)
        seed_word(kMaximumAmmoBase + static_cast<std::uint32_t>(i * 4u),
                  kAmmo[i]);
    seed_byte(kCurrentWeapon, 2u);
    seed_byte(kCurrentPsionic, 1u);
    seed_word(kMaximumPsionicEnergy, 250u);
    seed_word(kPsionicEnergy, 12u);
}

void test_registration_and_guards() {
    expect(g_damage_callback != nullptr, "damage callback registered");
    expect(g_registered_address == kDamageFunction,
           "damage callback pinned to central function");

    reset_state();
    expect(disruptor_cheats_set_god_mode(1) ==
               DISRUPTOR_CHEAT_GAME_NOT_READY,
           "God mode rejects before live gameplay");
    expect(disruptor_cheats_grant_all_weapons() ==
               DISRUPTOR_CHEAT_GAME_NOT_READY,
           "inventory grant rejects before live gameplay");
    expect(g_write_addresses.empty(), "pre-game rejection performs no writes");

    seed_live_game();
    seed_word(kCurrentHealth, 0u);
    expect(disruptor_cheats_set_god_mode(1) ==
               DISRUPTOR_CHEAT_GAME_NOT_READY,
           "God mode does not resurrect a dead player");

    reset_state();
    seed_live_game();
    g_netplay = true;
    expect(disruptor_cheats_set_god_mode(1) ==
               DISRUPTOR_CHEAT_NETPLAY_BLOCKED,
           "God mode rejects netplay");
    expect(disruptor_cheats_grant_all_weapons() ==
               DISRUPTOR_CHEAT_NETPLAY_BLOCKED,
           "inventory grant rejects netplay");
    expect(g_write_addresses.empty(), "netplay rejection performs no writes");
}

void test_god_mode_is_cpu_local_and_reversible() {
    reset_state();
    seed_live_game();
    expect(disruptor_cheats_set_god_mode(1) == DISRUPTOR_CHEAT_OK,
           "God mode enables in live gameplay");

    CPUState cpu{};
    cpu.gpr[4] = 37u;
    g_damage_callback(&cpu, kDamageFunction);
    expect(cpu.gpr[4] == 0u, "positive damage is neutralised");
    const std::uint32_t retail_health_after =
        load_word(kCurrentHealth) - cpu.gpr[4];
    expect(retail_health_after == 80u,
           "emulated retail subtraction leaves health unchanged");
    expect(load_word(kCurrentHealth) == 80u,
           "entry callback does not edit persistent health");
    expect(g_write_addresses.empty(), "God callback performs no guest writes");

    cpu.gpr[4] = static_cast<std::uint32_t>(-5);
    g_damage_callback(&cpu, kDamageFunction);
    expect(static_cast<std::int32_t>(cpu.gpr[4]) == -5,
           "non-positive arguments remain untouched");

    expect(disruptor_cheats_set_god_mode(0) == DISRUPTOR_CHEAT_OK,
           "God mode disables idempotently");
    cpu.gpr[4] = 9u;
    g_damage_callback(&cpu, kDamageFunction);
    expect(cpu.gpr[4] == 9u, "damage resumes immediately after disable");

    (void)disruptor_cheats_set_god_mode(1);
    g_netplay = true;
    cpu.gpr[4] = 11u;
    g_damage_callback(&cpu, kDamageFunction);
    expect(cpu.gpr[4] == 11u && !disruptor_cheats_god_mode_enabled(),
           "entering netplay clears God mode without mutating damage");
}

void test_retail_all_weapons_action() {
    reset_state();
    seed_live_game();
    seed_word(0x800770F4u, 0x11223344u);
    seed_word(0x800770F8u, 0x55667788u);
    for (std::size_t i = 0; i < kAmmo.size(); ++i)
        seed_word(kLiveAmmoBase + static_cast<std::uint32_t>(i * 4u), 0u);

    expect(disruptor_cheats_grant_all_weapons() == DISRUPTOR_CHEAT_OK,
           "retail All Weapons action succeeds");
    for (std::size_t i = 0; i < kAmmo.size(); ++i)
        expect(load_word(kLiveAmmoBase + static_cast<std::uint32_t>(i * 4u)) ==
                   kAmmo[i],
               "live ammunition copied from retail maxima");
    for (std::uint32_t address : kPsionicOwnership)
        expect(load_word(address) == 1u, "psionic unlocked");
    for (std::uint32_t address : kWeaponOwnership)
        expect(load_word(address) == 1u, "weapon unlocked");
    expect(load_word(0x800770F4u) == 0x11223344u &&
           load_word(0x800770F8u) == 0x55667788u,
           "two base ownership slots remain untouched");
    expect(psx_mod_read_byte(kCurrentWeapon) == 2u &&
           psx_mod_read_byte(kCurrentPsionic) == 1u,
           "valid current selectors are preserved");
    expect(load_word(kPsionicEnergy) == 250u, "psionic energy refilled");
    expect(psx_mod_read_byte(kCheatedMarker) == 1u,
           "native save-affecting cheat marker set");

    g_write_addresses.clear();
    expect(disruptor_cheats_grant_all_weapons() == DISRUPTOR_CHEAT_OK,
           "repeated grant is idempotent");

    seed_byte(kCurrentWeapon, 0xFFu);
    seed_byte(kCurrentPsionic, 0xFFu);
    expect(disruptor_cheats_grant_all_weapons() == DISRUPTOR_CHEAT_OK,
           "unset selector fallback succeeds");
    expect(psx_mod_read_byte(kCurrentWeapon) == 6u &&
           psx_mod_read_byte(kCurrentPsionic) == 0u,
           "retail selector fallbacks applied");
}

void test_inventory_validation_is_atomic() {
    reset_state();
    seed_live_game();
    seed_word(kMaximumAmmoBase + 8u, 999u);
    expect(disruptor_cheats_grant_all_weapons() ==
               DISRUPTOR_CHEAT_UNVERIFIED_STATE,
           "unexpected retail table fails closed");
    expect(g_write_addresses.empty(), "validation completes before any write");
}

}  // namespace

int main() {
    test_registration_and_guards();
    test_god_mode_is_cpu_local_and_reversible();
    test_retail_all_weapons_action();
    test_inventory_validation_is_atomic();
    disruptor_cheats_reset_session();

    if (g_failures) {
        std::cerr << g_failures << " cheat test(s) failed\n";
        return 1;
    }
    std::cout << "Disruptor cheat contracts: PASS\n";
    return 0;
}
