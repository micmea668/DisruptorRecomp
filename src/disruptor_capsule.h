#ifndef DISRUPTOR_CAPSULE_H
#define DISRUPTOR_CAPSULE_H

#include <stdint.h>

#ifdef __cplusplus
#include <string>
#include <string_view>
#include <vector>

namespace disruptor::capsule {

/* What went into the game at one VBlank, and what its memory was then. */
struct Frame {
    uint16_t pad[2] = {0xFFFF, 0xFFFF}; /* active low, as the game reads them */
    uint32_t byte_at = 0;               /* a byte a module wrote into the game this frame, 0 for none */
    uint8_t byte = 0;
    float pitch = 0.0f;                 /* the vertical look, which lives outside the game */
    uint64_t memory = 0;
};

/* first_cycle: the emulated clock at the first frame's VBlank, which places every later one. */
std::string encode(const std::vector<Frame> &frames, uint64_t first_cycle);
bool decode(std::string_view bytes, std::vector<Frame> &frames, uint64_t &first_cycle);
uint64_t digest(const uint8_t *bytes, size_t size);
/* A 24-bit bottom-up BMP of `width` by `height` from rows of RGB. */
std::string bitmap(const std::vector<uint8_t> &rgb, uint32_t width, uint32_t height);

}  /* namespace disruptor::capsule */

extern "C" {
#endif

/* Once per VBlank, before the pads are sampled: a recording takes what the last VBlank left in them and the game's memory now. */
void disruptor_capsule_before_input(void);
/* Once per VBlank, after the pads are sampled: a replay puts the recorded frame in their place. */
void disruptor_capsule_after_input(void);
void disruptor_capsule_note_byte(uint32_t address, uint8_t value);
/* The frontend calls this when a save state has been loaded, before the game runs on. */
void disruptor_capsule_state_loaded(void);
/* A word to show for a second when a recording starts, ends or is refused, or null. A visible overlay holds the in-between frames off, so it is not shown longer. */
const char *disruptor_capsule_notice(void);
/* The key: the first press saves a state into a new folder under capsules/ and records from there, the second ends it. */
void disruptor_capsule_toggle(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* DISRUPTOR_CAPSULE_H */
