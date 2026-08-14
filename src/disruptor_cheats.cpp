/*
 * Narrow, version-pinned host controls for Disruptor (SLUS-00224) cheats.
 *
 * God mode is implemented at the one resident damage-function entry rather
 * than by repeatedly editing health.  The callback changes only the current
 * CPUState argument, so disabling it leaves no health override or save data.
 *
 * The inventory action deliberately mirrors the retail All Weapons cheat,
 * including its persistent "cheated" marker.  All addresses and table values
 * below are guarded by source/codegen contract tests for this exact executable.
 */

#include "disruptor_cheats.h"

#include "cpu_state.h"
#include "mod_plugins.h"
#include "psx_netplay.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {

constexpr uint32_t kDamageFunction = 0x80020DD8u;
constexpr uint32_t kCurrentHealth = 0x80077660u;
constexpr uint32_t kMaximumHealth = 0x80077664u;

constexpr uint32_t kLiveAmmoBase = 0x80056A90u;
constexpr uint32_t kMaximumAmmoBase = 0x80056AB0u;
constexpr std::array<uint32_t, 8> kExpectedMaximumAmmo = {
    0u, 200u, 100u, 50u, 50u, 10u, 100u, 1u,
};

constexpr std::array<uint32_t, 5> kPsionicOwnership = {
    0x80077B5Cu, 0x80077B60u, 0x80077B64u,
    0x80077B68u, 0x80077B6Cu,
};

constexpr std::array<uint32_t, 8> kWeaponOwnership = {
    0x800770FCu, 0x80077100u, 0x80077104u, 0x80077108u,
    0x8007710Cu, 0x80077110u, 0x80077114u, 0x80077118u,
};

constexpr uint32_t kCurrentWeapon = 0x80077627u;
constexpr uint32_t kCurrentPsionic = 0x80077629u;
constexpr uint32_t kPsionicEnergy = 0x80077668u;
constexpr uint32_t kMaximumPsionicEnergy = 0x8007766Cu;
constexpr uint32_t kCheatedMarker = 0x80071690u;

bool g_god_mode = false;

bool plausible_live_gameplay() {
    if (!psx_mod_game_started()) return false;
    const uint32_t maximum = psx_mod_read_word(kMaximumHealth);
    const uint32_t current = psx_mod_read_word(kCurrentHealth);
    return maximum > 0u && maximum <= 10000u &&
           current > 0u && current <= maximum;
}

bool maximum_ammo_table_matches() {
    for (size_t i = 0; i < kExpectedMaximumAmmo.size(); ++i) {
        if (psx_mod_read_word(kMaximumAmmoBase +
                              static_cast<uint32_t>(i * 4u)) !=
            kExpectedMaximumAmmo[i])
            return false;
    }
    return true;
}

void damage_function_entry(CPUState *cpu, uint32_t address) {
    if (!cpu || address != kDamageFunction || !g_god_mode) return;
    if (psx_netplay_active()) {
        g_god_mode = false;
        return;
    }
    if (!plausible_live_gameplay()) return;

    /* Every verified player-damage call reaches func_80020DD8 with a positive
     * amount in $a0.  Zeroing only this invocation makes the subtraction an
     * exact no-op while retaining the routine's lightest hit cue.  Non-positive
     * values are left untouched defensively. */
    if (static_cast<int32_t>(cpu->gpr[4]) > 0)
        cpu->gpr[4] = 0u;
}

PSX_MOD_CONSTRUCTOR(register_disruptor_cheats) {
    if (!psx_mod_register_function_entry_plugin(
            "disruptor.god_mode.damage", kDamageFunction,
            damage_function_entry)) {
        std::fprintf(stderr,
                     "disruptor: failed to register God-mode damage hook\n");
    }
}

}  // namespace

extern "C" int disruptor_cheats_gameplay_ready(void) {
    return plausible_live_gameplay() ? 1 : 0;
}

extern "C" int disruptor_cheats_netplay_blocked(void) {
    return psx_netplay_active() ? 1 : 0;
}

extern "C" int disruptor_cheats_god_mode_enabled(void) {
    if (g_god_mode && psx_netplay_active()) g_god_mode = false;
    return g_god_mode ? 1 : 0;
}

extern "C" int disruptor_cheats_set_god_mode(int enabled) {
    if (!enabled) {
        g_god_mode = false;
        return DISRUPTOR_CHEAT_OK;
    }
    if (psx_netplay_active()) {
        g_god_mode = false;
        return DISRUPTOR_CHEAT_NETPLAY_BLOCKED;
    }
    if (!plausible_live_gameplay())
        return DISRUPTOR_CHEAT_GAME_NOT_READY;
    g_god_mode = true;
    return DISRUPTOR_CHEAT_OK;
}

extern "C" int disruptor_cheats_grant_all_weapons(void) {
    if (psx_netplay_active()) return DISRUPTOR_CHEAT_NETPLAY_BLOCKED;
    if (!plausible_live_gameplay()) return DISRUPTOR_CHEAT_GAME_NOT_READY;
    if (!maximum_ammo_table_matches())
        return DISRUPTOR_CHEAT_UNVERIFIED_STATE;

    const uint8_t current_weapon = psx_mod_read_byte(kCurrentWeapon);
    const uint8_t current_psionic = psx_mod_read_byte(kCurrentPsionic);
    if (!((current_weapon <= 9u) || current_weapon == 0xFFu) ||
        !((current_psionic <= 4u) || current_psionic == 0xFFu))
        return DISRUPTOR_CHEAT_UNVERIFIED_STATE;

    for (size_t i = 0; i < kExpectedMaximumAmmo.size(); ++i) {
        const uint32_t offset = static_cast<uint32_t>(i * 4u);
        psx_mod_write_word(kLiveAmmoBase + offset,
                           psx_mod_read_word(kMaximumAmmoBase + offset));
    }
    for (uint32_t address : kPsionicOwnership)
        psx_mod_write_word(address, 1u);
    for (uint32_t address : kWeaponOwnership)
        psx_mod_write_word(address, 1u);

    if (current_weapon == 0xFFu) psx_mod_write_byte(kCurrentWeapon, 6u);
    if (current_psionic == 0xFFu) psx_mod_write_byte(kCurrentPsionic, 0u);
    psx_mod_write_word(kPsionicEnergy,
                       psx_mod_read_word(kMaximumPsionicEnergy));

    /* This is the game's own marker.  It affects the ending and is serialized
     * into memory-card/password state, matching the retail cheat consequence. */
    psx_mod_write_byte(kCheatedMarker, 1u);
    return DISRUPTOR_CHEAT_OK;
}

extern "C" void disruptor_cheats_reset_session(void) {
    g_god_mode = false;
}
