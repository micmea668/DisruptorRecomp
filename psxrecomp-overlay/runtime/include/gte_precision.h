/* Exact GTE projection provenance shared by the GTE and GPU paths.
 *
 * SWC2 records the projection associated with a guest RAM word.  Some games,
 * including Disruptor, instead read SXY through MFC2 and later use an ordinary
 * SW.  Those routes are admitted only through opcode/dataflow-reviewed MFC2
 * and store PCs registered by the game runtime; a separately reviewed route
 * may seed a finite set of scratchpad words for a later exact packet copy.
 * This is a bounded audit of known instruction paths, not a general GPR
 * lineage tracker.  GPU packet decoding itself still queries exact main-RAM
 * addresses; these result codes deliberately distinguish an absent store from
 * an address collision or stale/changed packet data. */

#ifndef PSXRECOMP_GTE_PRECISION_H
#define PSXRECOMP_GTE_PRECISION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GtePrecisionLookupResult {
    GTE_PRECISION_LOOKUP_ACCEPTED = 0,
    GTE_PRECISION_LOOKUP_TRACKING_DISABLED,
    GTE_PRECISION_LOOKUP_SPECULATIVE,
    GTE_PRECISION_LOOKUP_SOURCE_NOT_RAM,
    GTE_PRECISION_LOOKUP_SOURCE_UNALIGNED,
    GTE_PRECISION_LOOKUP_STORE_MISS,
    GTE_PRECISION_LOOKUP_STORE_COLLISION,
    GTE_PRECISION_LOOKUP_PROJECTION_INVALID,
    GTE_PRECISION_LOOKUP_PACKED_MISMATCH,
    GTE_PRECISION_LOOKUP_ZERO_DEPTH,
    GTE_PRECISION_LOOKUP_SATURATED,
    GTE_PRECISION_LOOKUP_PERSPECTIVE_INVALID,
    GTE_PRECISION_LOOKUP_RESULT_COUNT
} GtePrecisionLookupResult;

GtePrecisionLookupResult gte_precision_load_word_ex(
    uint32_t addr, uint32_t packed,
    int32_t *x16, int32_t *y16, uint16_t *z);

/* Texture interpolation needs an unclamped projective depth in addition to
 * the exact packet-address/SXY identity required by geometry correction.  A
 * projection may remain useful for subpixel geometry while failing this
 * stricter near/far-depth test, so keep the two lookups separate. */
GtePrecisionLookupResult gte_precision_load_perspective_word_ex(
    uint32_t addr, uint32_t packed, uint16_t *z);

/* Retained boolean ABI used by older runtime code and out-of-tree modules. */
int gte_precision_load_word(uint32_t addr, uint32_t packed,
                            int32_t *x16, int32_t *y16, uint16_t *z);

/* memory.c owns the authoritative write result.  Every public 32-bit write
 * attempt clears the transient token, and only a completed main-RAM or
 * scratchpad write arms it with an explicit, non-aliasing address domain.
 * Projection callbacks consume the token exactly once before deciding whether
 * provenance may enter the cache. */
void gte_precision_word_write_begin(void);
void gte_precision_main_ram_word_committed(uint32_t physical);
void gte_precision_scratch_word_committed(uint32_t physical);
void gte_precision_store_word(uint32_t addr, uint8_t reg);
void gte_precision_invalidate_word(uint32_t addr);

/* Register reviewed MFC2 and ordinary-SW instruction identities. The MFC2
 * callback snapshots the precise projection actually read by guest code. The
 * post-write callback is emitted only after the matching guest SW returns, and
 * the independent memory token proves that its RAM word really committed.
 * Both callbacks require the registered raw instruction word; dirty or changed
 * code therefore fails closed. Clipped/repacked values must still equal the
 * captured packed SXY. Registration is idempotent; reset is intended for
 * process/game configuration, not per-frame use. */
void gte_precision_store_pc_routes_reset(void);
int gte_precision_mfc2_pc_route_add(uint32_t mfc2_pc, uint32_t instruction,
                                    uint8_t gte_reg);
int gte_precision_store_pc_route_add(uint32_t store_pc, uint32_t instruction,
                                     uint8_t gte_reg);
uint32_t gte_precision_mfc2_pc_read(uint32_t mfc2_pc, uint32_t instruction,
                                    uint8_t gte_reg, uint32_t packed);
void gte_precision_store_pc_word(uint32_t store_pc, uint32_t instruction,
                                 uint32_t addr, uint32_t packed);

/* Register a reviewed ordinary-SW route whose destination must be one of a
 * finite set of aligned scratchpad words.  The range is described explicitly
 * by the game configuration; no arbitrary scratchpad store is admitted.  The
 * callback is post-write and therefore also requires memory.c's matching
 * scratchpad commit token. */
int gte_precision_scratch_store_pc_route_add(
    uint32_t store_pc, uint32_t instruction, uint8_t gte_reg,
    uint32_t scratch_first, uint32_t scratch_stride,
    uint32_t scratch_count);
void gte_precision_scratch_store_pc_word(
    uint32_t store_pc, uint32_t instruction,
    uint32_t addr, uint32_t packed);

/* Register a finite set of reviewed projected-word copies into GP0 packets.
 * Each route pairs one exact LW with one exact SW and one unchanged GPR. The
 * read snapshots only an already-proven main-RAM or reviewed-scratch source;
 * the post-write callback publishes that snapshot only after the destination
 * main-RAM word commits. */
int gte_precision_copy_pc_route_add(
    uint32_t load_pc, uint32_t load_instruction,
    uint32_t store_pc, uint32_t store_instruction, uint8_t gpr);
uint32_t gte_precision_copy_pc_read(
    uint32_t load_pc, uint32_t instruction, uint8_t gpr,
    uint32_t addr, uint32_t packed);
void gte_precision_copy_pc_word(
    uint32_t store_pc, uint32_t instruction, uint8_t gpr,
    uint32_t addr, uint32_t packed);

typedef struct GtePrecisionDiagnostics {
    uint64_t store_collisions;
    uint64_t store_evictions;
    uint64_t lookup_collisions;
    uint64_t store_uncommitted_rejections;
    uint64_t registered_store_attempts;
    uint64_t registered_store_accepts;
    uint64_t registered_store_packed_rejections;
    uint64_t copy_load_attempts;
    uint64_t copy_load_accepts;
    uint64_t copy_store_attempts;
    uint64_t copy_store_accepts;
    uint64_t copy_store_packed_rejections;
} GtePrecisionDiagnostics;

void gte_precision_diagnostics(GtePrecisionDiagnostics *out);
void gte_precision_diagnostics_reset(void);

/* Bracket diagnostic interpreter replays so they cannot replace authoritative
 * GTE-side provenance or mutate runtime-only projection diagnostics. */
int gte_replay_side_effects_begin(void);
void gte_replay_side_effects_end(void);

#ifdef __cplusplus
}
#endif

#endif /* PSXRECOMP_GTE_PRECISION_H */
