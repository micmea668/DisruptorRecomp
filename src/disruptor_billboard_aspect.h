#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CPUState;

enum DisruptorBillboardAspectInstructionPhase {
    DISRUPTOR_BILLBOARD_ASPECT_BEFORE_INSTRUCTION = 0,
    DISRUPTOR_BILLBOARD_ASPECT_AFTER_INSTRUCTION = 1,
};

/* Version-pinned host-presentation seam for SLUS-00224's CPU-built world
 * billboards.  The hook only reads a completed POLY_FT4 packet and records its
 * projected centre in the GPU widescreen provenance table. */
void disruptor_billboard_aspect_instruction_hook(
    struct CPUState *cpu, uint32_t pc, uint32_t instruction, int phase);

/* Session-only isolation seam for the developer menu.  Bit N enables the Nth
 * reviewed packet-builder site in address order; unsupported bits are ignored.
 * Every process starts with all reviewed sites enabled. */
uint32_t disruptor_billboard_aspect_get_site_mask(void);
void disruptor_billboard_aspect_set_site_mask(uint32_t mask);
uint32_t disruptor_billboard_aspect_all_site_mask(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif
