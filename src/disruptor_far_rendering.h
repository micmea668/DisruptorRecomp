#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CPUState;

/*
 * Session-only draw-distance presets.  They are intentionally not part of the
 * persisted user-settings ABI while the primitive pressure and scene coverage
 * of the extended modes are still being measured.
 */
enum DisruptorFarRenderingPreset {
    DISRUPTOR_FAR_RENDERING_RETAIL = 0,
    DISRUPTOR_FAR_RENDERING_EXTENDED = 1, /* 1.25x */
    DISRUPTOR_FAR_RENDERING_FAR = 2,      /* 1.50x */
    DISRUPTOR_FAR_RENDERING_PRESET_COUNT = 3,
};

/* Independent, session-only control for the game's depth-fade ramp.  The
 * disabled mode moves the beginning of the ramp to the effective far plane,
 * leaving a zero-width fade interval without changing the clip distance. */
enum DisruptorFarRenderingDepthFadeMode {
    DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL = 0,
    DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED = 1,
    DISRUPTOR_FAR_RENDERING_DEPTH_FADE_MODE_COUNT = 2,
};

enum DisruptorFarRenderingResult {
    DISRUPTOR_FAR_RENDERING_OK = 1,
    DISRUPTOR_FAR_RENDERING_INVALID_PRESET = 0,
    DISRUPTOR_FAR_RENDERING_NETPLAY_BLOCKED = -1,
};

enum DisruptorFarRenderingInstructionPhase {
    DISRUPTOR_FAR_RENDERING_BEFORE_INSTRUCTION = 0,
    DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION = 1,
};

enum { DISRUPTOR_FAR_RENDERING_DIAGNOSTICS_ABI_VERSION = 3 };

typedef struct DisruptorFarRenderingDiagnostics {
    uint32_t abi_version;
    uint32_t struct_size;

    int32_t preset;
    /* True only while the entry-to-epilogue diagnostics window is active.
     * Exact load substitution itself is intentionally stateless so it also
     * works after a savestate resumes in the middle of the renderer. */
    int32_t active;
    int32_t gameplay_ready;
    int32_t netplay_blocked;

    uint64_t renderer_entries;
    uint64_t completed_frames;
    uint64_t substituted_loads;
    uint64_t rejected_entries;
    uint64_t nested_entries;
    uint64_t missed_epilogues;
    uint64_t invalid_globals;
    uint64_t primitive_samples;

    uint32_t source_far_distance;
    int32_t source_fade_start;
    uint32_t effective_far_distance;
    int32_t effective_fade_start;

    uint32_t primitive_start;
    uint32_t primitive_end;
    uint32_t primitive_last_delta;
    uint32_t primitive_high_water;

    /* Wall-clock renderer span, not thread CPU time. */
    double render_wall_us_p50;
    double render_wall_us_p95;
    double render_wall_us_max;

    /* ABI v2 append-only fields.  substituted_loads above remains the total
     * for compatibility with v1 readers. */
    int32_t depth_fade_mode;
    uint32_t reserved_v2;
    uint64_t far_load_substitutions;
    uint64_t fade_load_substitutions;

    /* ABI v3 append-only fields.  These are passive renderer observations:
     * they never recreate guest visibility decisions or write guest state.
     * A portal shortcut relaxation is an earlier far-test result, whereas a
     * portal final-decision flip reaches the helper's final distance test. */
    uint64_t traversal_recursion_entries;
    uint64_t traversal_cap_hits;
    uint64_t portal_far_tests;
    uint64_t portal_shortcut_relaxations;
    uint64_t portal_final_decisions;
    uint64_t portal_final_effective_rejections;
    uint64_t portal_final_decision_flips;
    uint64_t object_far_decisions;
    uint64_t object_effective_rejections;
    uint64_t object_decision_flips;
    uint64_t observer_sequence_errors;
    uint64_t visibility_samples;
    uint64_t packet_stage_samples;

    uint32_t traversal_rooms_last;
    uint32_t traversal_rooms_high_water;
    uint32_t submitted_spans_last;
    uint32_t submitted_spans_high_water;
    uint32_t traversal_max_depth_last;
    uint32_t traversal_max_depth_high_water;

    /* Last complete frame's primitive-arena growth at four renderer stages.
     * A sample is published only when every boundary is seen in order and all
     * five pointers (including entry and epilogue) are valid and monotonic. */
    uint32_t packet_entry_to_traversal_end_last;
    uint32_t packet_traversal_to_visible_start_last;
    uint32_t packet_visible_loop_last;
    uint32_t packet_post_visible_loop_last;
} DisruptorFarRenderingDiagnostics;

int disruptor_far_rendering_set_preset(int preset);
int disruptor_far_rendering_get_preset(void);
int disruptor_far_rendering_set_depth_fade_mode(int mode);
int disruptor_far_rendering_get_depth_fade_mode(void);
int disruptor_far_rendering_gameplay_ready(void);
int disruptor_far_rendering_netplay_blocked(void);

/* Writes only the caller-provided capacity and returns one on success.
 * Timing/primitive comparison fields are scoped to the current distance/fade
 * combination and reset when either setting changes; safety counters remain
 * session-wide. */
int disruptor_far_rendering_get_diagnostics(
    DisruptorFarRenderingDiagnostics *out, uint32_t capacity);

/* Clears an in-flight diagnostics window before resetting all session state. */
void disruptor_far_rendering_reset_session(void);

/* Abandons only an in-flight entry-to-epilogue timing window.  Savestate load
 * calls this because wall-span/primitive deltas cannot cross a restored guest
 * timeline.  Exact load substitution is stateless and remains available. */
void disruptor_far_rendering_abandon_metrics(void);

/* Version-pinned runtime seams for SLUS-00224. */
void disruptor_far_rendering_renderer_entry(struct CPUState *cpu,
                                            uint32_t address);
void disruptor_far_rendering_instruction_hook(
    struct CPUState *cpu, uint32_t pc, uint32_t instruction, int phase);

#ifdef __cplusplus
}  /* extern "C" */
#endif
