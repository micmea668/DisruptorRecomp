#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum DisruptorCheatResult {
    DISRUPTOR_CHEAT_OK = 1,
    DISRUPTOR_CHEAT_GAME_NOT_READY = 0,
    DISRUPTOR_CHEAT_NETPLAY_BLOCKED = -1,
    DISRUPTOR_CHEAT_UNVERIFIED_STATE = -2,
};

/* Cheats are deliberately session-owned and are never written to settings.toml. */
int disruptor_cheats_gameplay_ready(void);
int disruptor_cheats_netplay_blocked(void);

int disruptor_cheats_god_mode_enabled(void);
int disruptor_cheats_set_god_mode(int enabled);

/*
 * Reproduces the retail All Weapons cheat as a one-shot action.  The retail
 * action grants weapons, psionics and ammunition and marks the current game
 * (and any subsequent save) as cheated.
 */
int disruptor_cheats_grant_all_weapons(void);

void disruptor_cheats_reset_session(void);

#ifdef __cplusplus
}
#endif
