#include "cpu_state.h"
#include "gte_precision.h"

#include <cstdint>
#include <cstdio>

extern "C" void gte_precision_tracking_set(int enabled);
extern "C" void gte_precision_speculative_begin(void);
extern "C" void gte_precision_speculative_end(void);
extern "C" void gte_precision_timeline_invalidate(void);
extern "C" void gte_test_seed_precise_projection(
    uint32_t index, uint32_t packed, int32_t x16, int32_t y16, uint16_t z);
extern "C" void gte_test_set_precise_perspective_valid(
    uint32_t index, int valid);

/* gte.cpp runtime dependencies that are irrelevant to provenance storage. */
extern "C" int gpu_ws_present_native_43(void) { return 0; }
extern "C" void psx_ws_note_gte_project(int) {}
extern "C" {
uint64_t s_frame_count = 0;
}

extern "C" uint32_t psx_gte_cmd_latency(uint32_t) { return 0; }
extern "C" void psx_gte_set(CPUState *, uint32_t) {}

namespace {

constexpr uint32_t kMfc2Pc = 0x8004633Cu;
constexpr uint32_t kOtherMfc2Pc = 0x8004795Cu;
constexpr uint32_t kMfc2Insn = 0x480A7000u;
constexpr uint32_t kStorePc = 0x8004641Cu;
constexpr uint32_t kOtherStorePc = 0x800464BCu;
constexpr uint32_t kScratchStorePc = 0x80047A0Cu;
constexpr uint32_t kStoreInsn = 0xACAA0004u;
constexpr uint32_t kScratchFirst = 0x1F800084u;
constexpr uint32_t kScratchStride = 8u;
constexpr uint32_t kScratchCount = 9u;
constexpr uint32_t kPacked = 0xFFDA0123u;
constexpr int32_t kX16 = 0x00123456;
constexpr int32_t kY16 = -0x00034567;
constexpr uint16_t kZ = 0x4567u;

struct CopyRoute {
    uint32_t load_pc;
    uint32_t load_insn;
    uint32_t store_pc;
    uint32_t store_insn;
    uint8_t gpr;
};

constexpr CopyRoute kCopyRoutes[] = {
    {0x80046C4Cu, 0x8D0C0004u, 0x80046D04u, 0xAF2C0008u, 12u},
    {0x80046C50u, 0x8D2D0004u, 0x80046D08u, 0xAF2D0014u, 13u},
    {0x80046C54u, 0x8D4E0004u, 0x80046D10u, 0xAF2E002Cu, 14u},
    {0x80046C60u, 0x8D6F0004u, 0x80046D0Cu, 0xAF2F0020u, 15u},
};

constexpr CopyRoute kScratchCopyRoutes[] = {
    {0x80047A80u, 0x8CA80084u, 0x80047A90u, 0xAF280008u, 8u},
    {0x80047A84u, 0x8CA900A4u, 0x80047A94u, 0xAF290014u, 9u},
    {0x80047A88u, 0x8CAA00C4u, 0x80047A9Cu, 0xAF2A002Cu, 10u},
    {0x80047A8Cu, 0x8CAB00BCu, 0x80047A98u, 0xAF2B0020u, 11u},
    {0x80047B40u, 0x8CA800A4u, 0x80047B50u, 0xAF280008u, 8u},
    {0x80047B44u, 0x8CA9008Cu, 0x80047B54u, 0xAF290014u, 9u},
    {0x80047B48u, 0x8CAA00ACu, 0x80047B5Cu, 0xAF2A002Cu, 10u},
    {0x80047B4Cu, 0x8CAB00C4u, 0x80047B58u, 0xAF2B0020u, 11u},
    {0x80047C00u, 0x8CA800C4u, 0x80047C10u, 0xAF280008u, 8u},
    {0x80047C04u, 0x8CA900ACu, 0x80047C14u, 0xAF290014u, 9u},
    {0x80047C08u, 0x8CAA0094u, 0x80047C1Cu, 0xAF2A002Cu, 10u},
    {0x80047C0Cu, 0x8CAB00B4u, 0x80047C18u, 0xAF2B0020u, 11u},
    {0x80047CC4u, 0x8CA800BCu, 0x80047CD4u, 0xAF280008u, 8u},
    {0x80047CC8u, 0x8CA900C4u, 0x80047CD8u, 0xAF290014u, 9u},
    {0x80047CCCu, 0x8CAA00B4u, 0x80047CE0u, 0xAF2A002Cu, 10u},
    {0x80047CD0u, 0x8CAB009Cu, 0x80047CDCu, 0xAF2B0020u, 11u},
    {0x80047D6Cu, 0x8CA80084u, 0x80047D7Cu, 0xAF280008u, 8u},
    {0x80047D70u, 0x8CA9008Cu, 0x80047D80u, 0xAF290014u, 9u},
    {0x80047D74u, 0x8CAA0094u, 0x80047D88u, 0xAF2A002Cu, 10u},
    {0x80047D78u, 0x8CAB009Cu, 0x80047D84u, 0xAF2B0020u, 11u},
};

static_assert(sizeof(kScratchCopyRoutes) / sizeof(kScratchCopyRoutes[0]) == 20,
              "the audited scratchpad path has exactly twenty copy routes");

struct Projection {
    uint32_t packed;
    int32_t x16;
    int32_t y16;
    uint16_t z;
};

int fail(const char *what) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

void reset_fixture() {
    gte_precision_tracking_set(0);
    gte_precision_tracking_set(1);
    gte_precision_diagnostics_reset();
    gte_precision_store_pc_routes_reset();
    gte_test_seed_precise_projection(2, kPacked, kX16, kY16, kZ);
}

bool add_capture(uint32_t pc = kMfc2Pc) {
    return gte_precision_mfc2_pc_route_add(pc, kMfc2Insn, 14) != 0;
}

bool add_store(uint32_t pc = kStorePc) {
    return gte_precision_store_pc_route_add(pc, kStoreInsn, 14) != 0;
}

bool add_scratch_store() {
    return gte_precision_scratch_store_pc_route_add(
               kScratchStorePc, kStoreInsn, 14,
               kScratchFirst, kScratchStride, kScratchCount) != 0;
}

void capture(uint32_t pc = kMfc2Pc, uint32_t packed = kPacked) {
    (void)gte_precision_mfc2_pc_read(pc, kMfc2Insn, 14, packed);
}

bool lookup(uint32_t addr, uint32_t packed = kPacked,
            int32_t expected_x16 = kX16, int32_t expected_y16 = kY16,
            uint16_t expected_z = kZ) {
    int32_t x16 = 0;
    int32_t y16 = 0;
    uint16_t z = 0;
    return gte_precision_load_word(addr, packed, &x16, &y16, &z) != 0 &&
           x16 == expected_x16 && y16 == expected_y16 && z == expected_z;
}

GtePrecisionLookupResult perspective_lookup(uint32_t addr,
                                             uint32_t packed = kPacked,
                                             uint16_t *z = nullptr) {
    return gte_precision_load_perspective_word_ex(addr, packed, z);
}

void commit(uint32_t physical) {
    gte_precision_word_write_begin();
    gte_precision_main_ram_word_committed(physical);
}

void scratch_commit(uint32_t physical) {
    gte_precision_word_write_begin();
    gte_precision_scratch_word_committed(physical);
}

void store(uint32_t pc, uint32_t addr, uint32_t packed = kPacked) {
    gte_precision_store_pc_word(pc, kStoreInsn, addr, packed);
}

void scratch_store(uint32_t addr, uint32_t packed = kPacked) {
    gte_precision_scratch_store_pc_word(
        kScratchStorePc, kStoreInsn, addr, packed);
}

bool add_copy(const CopyRoute &route) {
    return gte_precision_copy_pc_route_add(
               route.load_pc, route.load_insn,
               route.store_pc, route.store_insn, route.gpr) != 0;
}

void copy_read(const CopyRoute &route, uint32_t addr,
               uint32_t packed = kPacked) {
    (void)gte_precision_copy_pc_read(
        route.load_pc, route.load_insn, route.gpr, addr, packed);
}

void copy_store(const CopyRoute &route, uint32_t addr,
                uint32_t packed = kPacked) {
    gte_precision_copy_pc_word(
        route.store_pc, route.store_insn, route.gpr, addr, packed);
}

void seed_source(uint32_t addr, uint32_t packed, int32_t x16, int32_t y16,
                 uint16_t z) {
    gte_test_seed_precise_projection(2, packed, x16, y16, z);
    capture(kMfc2Pc, packed);
    commit(addr & 0x1FFFFCu);
    store(kStorePc, addr, packed);
}

Projection scratch_projection(uint32_t index) {
    return {
        0x10200100u + index * 0x00010001u,
        kX16 + static_cast<int32_t>(index * 0x1111u),
        kY16 - static_cast<int32_t>(index * 0x2222u),
        static_cast<uint16_t>(kZ + index),
    };
}

void seed_scratch_source(uint32_t slot, const Projection &projection) {
    gte_test_seed_precise_projection(
        2, projection.packed, projection.x16, projection.y16, projection.z);
    capture(kOtherMfc2Pc, projection.packed);
    scratch_commit(slot);
    scratch_store(slot, projection.packed);
}

const CopyRoute *scratch_route_for_slot(uint32_t slot) {
    const uint32_t offset = slot - 0x1F800000u;
    for (const CopyRoute &route : kScratchCopyRoutes) {
        if ((route.load_insn & 0xFFFFu) == offset) return &route;
    }
    return nullptr;
}

GtePrecisionDiagnostics diagnostics() {
    GtePrecisionDiagnostics value{};
    gte_precision_diagnostics(&value);
    return value;
}

int test_registration_and_committed_store() {
    reset_fixture();
    if (!add_capture() || !add_capture() || !add_store() || !add_store())
        return fail("route registration is not idempotent");
    if (gte_precision_mfc2_pc_route_add(kMfc2Pc, kMfc2Insn, 13) ||
        gte_precision_mfc2_pc_route_add(kMfc2Pc + 2u, kMfc2Insn, 14) ||
        gte_precision_mfc2_pc_route_add(kOtherMfc2Pc, kStoreInsn, 14) ||
        gte_precision_store_pc_route_add(kStorePc, kStoreInsn, 13) ||
        gte_precision_store_pc_route_add(kStorePc + 2u, kStoreInsn, 14) ||
        gte_precision_store_pc_route_add(kOtherStorePc, kMfc2Insn, 14))
        return fail("conflicting or invalid route registration was accepted");

    constexpr uint32_t addr = 0x00123450u;
    commit(addr);
    store(kStorePc, addr);
    if (lookup(addr)) return fail("store without a reviewed MFC2 capture was accepted");

    capture();
    commit(addr);
    store(kStorePc, addr);
    if (!lookup(addr)) return fail("committed captured SW was not recorded");

    GtePrecisionDiagnostics d{};
    gte_precision_diagnostics(&d);
    if (d.registered_store_attempts != 2 ||
        d.registered_store_accepts != 1 ||
        d.registered_store_packed_rejections != 0)
        return fail("registered-store acceptance diagnostics are wrong");
    return 0;
}

int test_captured_snapshot_and_multiple_stores() {
    reset_fixture();
    if (!add_capture() || !add_store() || !add_store(kOtherStorePc))
        return fail("could not register reviewed capture/store PCs");
    capture();

    /* A later GTE projection with the same rounded SXY must not replace the
     * projection that the audited MFC2 actually read. */
    constexpr int32_t newer_x16 = kX16 + 0x1234;
    constexpr int32_t newer_y16 = kY16 - 0x2345;
    constexpr uint16_t newer_z = kZ + 1;
    gte_test_seed_precise_projection(
        2, kPacked, newer_x16, newer_y16, newer_z);

    constexpr uint32_t phys_a = 0x00045670u;
    constexpr uint32_t phys_b = 0x00145670u;
    commit(phys_a);
    store(kStorePc, 0x80045670u);
    commit(phys_b);
    store(kOtherStorePc, 0xA0145670u);
    if (!lookup(phys_a) || !lookup(0x80445670u) ||
        !lookup(phys_b) || !lookup(0xA0545670u))
        return fail("captured projection did not survive later GTE state or multiple stores");
    if (lookup(phys_a, kPacked, newer_x16, newer_y16, newer_z))
        return fail("store used current GTE state instead of the captured MFC2 snapshot");
    return 0;
}

int test_fail_closed_paths() {
    reset_fixture();
    if (!add_capture() || !add_store())
        return fail("could not register fail-closed routes");
    capture();

    constexpr uint32_t addr = 0x00080000u;
    commit(addr);
    store(kOtherStorePc, addr);
    if (lookup(addr)) return fail("unregistered store PC created provenance");

    commit(addr);
    gte_precision_store_pc_word(kStorePc, kStoreInsn ^ 1u, addr, kPacked);
    if (lookup(addr)) return fail("changed store instruction created provenance");

    gte_precision_word_write_begin();
    store(kStorePc, addr);
    if (lookup(addr)) return fail("uncommitted registered store created provenance");

    commit(addr + 4u);
    store(kStorePc, addr);
    if (lookup(addr)) return fail("mismatched commit address created provenance");

    commit(addr);
    store(kStorePc, addr, kPacked ^ 1u);
    if (lookup(addr, kPacked ^ 1u))
        return fail("clipped/repacked coordinate created provenance");

    commit(addr);
    store(kStorePc, 0xC0080000u);
    if (lookup(addr)) return fail("KSEG2 store aliased main-RAM provenance");

    GtePrecisionDiagnostics d{};
    gte_precision_diagnostics(&d);
    if (d.store_uncommitted_rejections != 2 ||
        d.registered_store_packed_rejections != 1 ||
        d.registered_store_accepts != 0)
        return fail("fail-closed diagnostics are wrong");
    return 0;
}

int test_speculation_and_timeline() {
    reset_fixture();
    if (!add_capture() || !add_store())
        return fail("could not register speculation routes");
    constexpr uint32_t addr = 0x00100000u;
    capture();

    const GtePrecisionDiagnostics before_replay_store = diagnostics();
    (void)gte_replay_side_effects_begin();
    commit(addr + 8u);
    store(kStorePc, addr + 8u);
    gte_replay_side_effects_end();
    const GtePrecisionDiagnostics after_replay_store = diagnostics();
    if (after_replay_store.registered_store_attempts !=
        before_replay_store.registered_store_attempts)
        return fail("replayed registered SW changed diagnostics");
    if (lookup(addr + 8u))
        return fail("replayed registered SW escaped into provenance");

    gte_precision_speculative_begin();
    gte_test_seed_precise_projection(2, kPacked, kX16 + 1, kY16 + 1, kZ + 1);
    capture();
    commit(addr);
    store(kStorePc, addr);
    gte_precision_speculative_end();
    if (lookup(addr)) return fail("speculative store escaped into provenance");

    commit(addr);
    store(kStorePc, addr);
    if (!lookup(addr)) return fail("authoritative captured store failed after speculation");

    gte_precision_timeline_invalidate();
    if (lookup(addr)) return fail("timeline restore retained stale provenance");
    commit(addr + 4u);
    store(kStorePc, addr + 4u);
    if (lookup(addr + 4u)) return fail("timeline restore retained stale MFC2 capture");
    return 0;
}

int test_copy_registration_and_four_word_packet() {
    reset_fixture();
    if (!add_capture() || !add_store())
        return fail("could not register source projection route");
    for (const CopyRoute &route : kCopyRoutes) {
        if (!add_copy(route) || !add_copy(route))
            return fail("copy-route registration is not idempotent");
    }

    const CopyRoute &first = kCopyRoutes[0];
    if (gte_precision_copy_pc_route_add(
            first.load_pc + 2u, first.load_insn,
            first.store_pc, first.store_insn, first.gpr) ||
        gte_precision_copy_pc_route_add(
            first.load_pc, first.store_insn,
            first.store_pc, first.store_insn, first.gpr) ||
        gte_precision_copy_pc_route_add(
            first.load_pc, first.load_insn,
            first.store_pc + 2u, first.store_insn, first.gpr) ||
        gte_precision_copy_pc_route_add(
            first.load_pc, first.load_insn,
            first.store_pc, first.load_insn, first.gpr) ||
        gte_precision_copy_pc_route_add(
            first.load_pc, first.load_insn,
            first.store_pc, first.store_insn, first.gpr + 1u))
        return fail("invalid or conflicting copy route was accepted");

    constexpr uint32_t source_base = 0x00090000u;
    constexpr uint32_t destination_base = 0x00110000u;
    uint32_t packed[4]{};
    int32_t x16[4]{};
    int32_t y16[4]{};
    uint16_t z[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
        packed[i] = kPacked + i * 0x00010001u;
        x16[i] = kX16 + static_cast<int32_t>(i * 0x1111u);
        y16[i] = kY16 - static_cast<int32_t>(i * 0x2222u);
        z[i] = static_cast<uint16_t>(kZ + i);
        seed_source(source_base + i * 4u, packed[i], x16[i], y16[i], z[i]);
    }

    /* Disruptor loads all four words before copying them to the GPU packet.
     * Distinct source values prove that the captures are kept per GPR. */
    for (uint32_t i = 0; i < 4; ++i)
        copy_read(kCopyRoutes[i], source_base + i * 4u, packed[i]);

    constexpr uint32_t store_order[] = {0u, 1u, 3u, 2u};
    for (uint32_t i : store_order) {
        const uint32_t destination = destination_base + i * 4u;
        commit(destination);
        copy_store(kCopyRoutes[i], destination, packed[i]);
    }
    for (uint32_t i = 0; i < 4; ++i) {
        if (!lookup(destination_base + i * 4u, packed[i], x16[i], y16[i], z[i]))
            return fail("four-word packet copy lost its per-GPR provenance");
    }
    GtePrecisionDiagnostics d{};
    gte_precision_diagnostics(&d);
    if (d.copy_load_attempts != 4 || d.copy_load_accepts != 4 ||
        d.copy_store_attempts != 4 || d.copy_store_accepts != 4 ||
        d.copy_store_packed_rejections != 0)
        return fail("successful packet-copy diagnostics are wrong");
    return 0;
}

int test_copy_snapshot_and_fail_closed_paths() {
    constexpr uint32_t source = 0x000A0000u;
    constexpr uint32_t destination = 0x00120000u;
    const CopyRoute &route = kCopyRoutes[0];

    reset_fixture();
    if (!add_capture() || !add_store() || !add_copy(route))
        return fail("could not register copy fixture routes");
    seed_source(source, kPacked, kX16, kY16, kZ);
    copy_read(route, source);

    /* The reviewed LW captures the projection carried by the loaded word.
     * A later source overwrite must not change that already-loaded GPR value. */
    constexpr int32_t newer_x16 = kX16 + 0x3333;
    constexpr int32_t newer_y16 = kY16 - 0x4444;
    constexpr uint16_t newer_z = kZ + 7;
    seed_source(source, kPacked, newer_x16, newer_y16, newer_z);
    commit(destination);
    copy_store(route, destination);
    if (!lookup(destination))
        return fail("copy store did not use the projection captured at LW");
    if (lookup(destination, kPacked, newer_x16, newer_y16, newer_z))
        return fail("copy store re-read changed source provenance");
    commit(destination + 4u);
    copy_store(route, destination + 4u);
    if (lookup(destination + 4u))
        return fail("one reviewed LW capture was reused by two SWs");

    copy_read(route, source);
    copy_read(route, source + 0x100u);
    commit(destination + 8u);
    copy_store(route, destination + 8u);
    if (lookup(destination + 8u))
        return fail("failed authoritative LW retained an older capture");

    copy_read(route, source);
    gte_precision_word_write_begin();
    copy_store(route, destination + 0xCu);
    commit(destination + 0xCu);
    copy_store(route, destination + 0xCu);
    if (lookup(destination + 0xCu))
        return fail("rejected authoritative SW did not consume its capture");

    struct RejectedCase {
        const char *name;
        uint32_t load_pc;
        uint32_t load_insn;
        uint8_t load_gpr;
        uint32_t source_addr;
        uint32_t load_packed;
        uint32_t store_pc;
        uint32_t store_insn;
        uint8_t store_gpr;
        uint32_t destination_addr;
        uint32_t store_packed;
        uint32_t committed_addr;
        bool commit_word;
    };
    const RejectedCase rejected[] = {
        {"missing reviewed LW", 0, 0, 0, source, kPacked,
         route.store_pc, route.store_insn, route.gpr,
         destination + 0x10u, kPacked, destination + 0x10u, true},
        {"wrong LW PC", route.load_pc + 4u, route.load_insn, route.gpr,
         source, kPacked, route.store_pc, route.store_insn, route.gpr,
         destination + 0x20u, kPacked, destination + 0x20u, true},
        {"wrong LW word", route.load_pc, route.load_insn ^ 1u, route.gpr,
         source, kPacked, route.store_pc, route.store_insn, route.gpr,
         destination + 0x30u, kPacked, destination + 0x30u, true},
        {"wrong LW GPR", route.load_pc, route.load_insn, 13u,
         source, kPacked, route.store_pc, route.store_insn, route.gpr,
         destination + 0x40u, kPacked, destination + 0x40u, true},
        {"missing source", route.load_pc, route.load_insn, route.gpr,
         source + 0x100u, kPacked, route.store_pc, route.store_insn, route.gpr,
         destination + 0x50u, kPacked, destination + 0x50u, true},
        {"changed source word", route.load_pc, route.load_insn, route.gpr,
         source, kPacked ^ 1u, route.store_pc, route.store_insn, route.gpr,
         destination + 0x60u, kPacked ^ 1u, destination + 0x60u, true},
        {"unaligned source", route.load_pc, route.load_insn, route.gpr,
         source + 2u, kPacked, route.store_pc, route.store_insn, route.gpr,
         destination + 0x68u, kPacked, destination + 0x68u, true},
        {"wrong SW PC", route.load_pc, route.load_insn, route.gpr,
         source, kPacked, route.store_pc + 4u, route.store_insn, route.gpr,
         destination + 0x70u, kPacked, destination + 0x70u, true},
        {"wrong SW word", route.load_pc, route.load_insn, route.gpr,
         source, kPacked, route.store_pc, route.store_insn ^ 1u, route.gpr,
         destination + 0x80u, kPacked, destination + 0x80u, true},
        {"wrong SW GPR", route.load_pc, route.load_insn, route.gpr,
         source, kPacked, route.store_pc, route.store_insn, 13u,
         destination + 0x90u, kPacked, destination + 0x90u, true},
        {"uncommitted SW", route.load_pc, route.load_insn, route.gpr,
         source, kPacked, route.store_pc, route.store_insn, route.gpr,
         destination + 0xA0u, kPacked, 0, false},
        {"mismatched commit", route.load_pc, route.load_insn, route.gpr,
         source, kPacked, route.store_pc, route.store_insn, route.gpr,
         destination + 0xB0u, kPacked, destination + 0xB4u, true},
        {"changed destination word", route.load_pc, route.load_insn, route.gpr,
         source, kPacked, route.store_pc, route.store_insn, route.gpr,
         destination + 0xC0u, kPacked ^ 1u, destination + 0xC0u, true},
        {"unaligned destination", route.load_pc, route.load_insn, route.gpr,
         source, kPacked, route.store_pc, route.store_insn, route.gpr,
         destination + 0xC6u, kPacked, destination + 0xC4u, true},
        {"KSEG2 source", route.load_pc, route.load_insn, route.gpr,
         0xC00A0000u, kPacked, route.store_pc, route.store_insn, route.gpr,
         destination + 0xD0u, kPacked, destination + 0xD0u, true},
        {"KSEG2 destination", route.load_pc, route.load_insn, route.gpr,
         source, kPacked, route.store_pc, route.store_insn, route.gpr,
         0xC0120000u, kPacked, destination + 0xE0u, true},
    };

    for (const RejectedCase &item : rejected) {
        reset_fixture();
        if (!add_capture() || !add_store() || !add_copy(route))
            return fail("could not reset copy rejection fixture");
        seed_source(source, kPacked, kX16, kY16, kZ);
        if (item.load_pc != 0) {
            (void)gte_precision_copy_pc_read(
                item.load_pc, item.load_insn, item.load_gpr,
                item.source_addr, item.load_packed);
        }
        if (item.commit_word)
            commit(item.committed_addr);
        else
            gte_precision_word_write_begin();
        gte_precision_copy_pc_word(
            item.store_pc, item.store_insn, item.store_gpr,
            item.destination_addr, item.store_packed);
        const uint32_t probe_addr = item.destination_addr >= 0xC0000000u
            ? item.destination_addr & 0x1FFFFCu
            : item.destination_addr & ~3u;
        if (lookup(probe_addr, item.store_packed))
            return fail(item.name);
    }
    return 0;
}

int test_copy_speculation_replay_and_timeline() {
    constexpr uint32_t source = 0x000B0000u;
    constexpr uint32_t destination = 0x00130000u;
    const CopyRoute &route = kCopyRoutes[0];

    reset_fixture();
    if (!add_capture() || !add_store() || !add_copy(route))
        return fail("could not register copy transaction fixture");
    seed_source(source, kPacked, kX16, kY16, kZ);

    gte_precision_speculative_begin();
    copy_read(route, source);
    gte_precision_speculative_end();
    commit(destination);
    copy_store(route, destination);
    if (lookup(destination))
        return fail("speculative copy LW escaped into provenance");

    const int replay_token = gte_replay_side_effects_begin();
    const GtePrecisionDiagnostics before_replay_load = diagnostics();
    copy_read(route, source);
    gte_replay_side_effects_end();
    (void)replay_token;
    const GtePrecisionDiagnostics after_replay_load = diagnostics();
    if (after_replay_load.copy_load_attempts !=
        before_replay_load.copy_load_attempts)
        return fail("replayed copy LW changed diagnostics");
    commit(destination + 4u);
    copy_store(route, destination + 4u);
    if (lookup(destination + 4u))
        return fail("replayed copy LW escaped into provenance");

    copy_read(route, source);
    gte_precision_speculative_begin();
    commit(destination + 8u);
    copy_store(route, destination + 8u);
    gte_precision_speculative_end();
    if (lookup(destination + 8u))
        return fail("speculative copy SW escaped into provenance");
    commit(destination + 0xCu);
    copy_store(route, destination + 0xCu);
    if (!lookup(destination + 0xCu))
        return fail("speculative SW consumed an authoritative copy capture");

    copy_read(route, source);
    (void)gte_replay_side_effects_begin();
    const GtePrecisionDiagnostics before_replay_store = diagnostics();
    commit(destination + 0x10u);
    copy_store(route, destination + 0x10u);
    gte_replay_side_effects_end();
    const GtePrecisionDiagnostics after_replay_store = diagnostics();
    if (after_replay_store.copy_store_attempts !=
        before_replay_store.copy_store_attempts)
        return fail("replayed copy SW changed diagnostics");
    if (lookup(destination + 0x10u))
        return fail("replayed copy SW escaped into provenance");
    commit(destination + 0x14u);
    copy_store(route, destination + 0x14u);
    if (!lookup(destination + 0x14u))
        return fail("replayed SW consumed an authoritative copy capture");

    copy_read(route, source);
    gte_precision_timeline_invalidate();
    commit(destination + 0x18u);
    copy_store(route, destination + 0x18u);
    if (lookup(destination + 0x18u))
        return fail("timeline restore retained stale copy capture");
    return 0;
}

int test_scratch_nine_slots_and_fail_closed_domains() {
    reset_fixture();
    if (!add_capture(kOtherMfc2Pc) || !add_scratch_store() ||
        !add_scratch_store())
        return fail("scratch store registration is not idempotent");
    if (gte_precision_scratch_store_pc_route_add(
            kScratchStorePc, kStoreInsn ^ 4u, 14u,
            kScratchFirst, kScratchStride, kScratchCount) ||
        gte_precision_scratch_store_pc_route_add(
            kScratchStorePc, kStoreInsn, 13u,
            kScratchFirst, kScratchStride, kScratchCount) ||
        gte_precision_scratch_store_pc_route_add(
            kScratchStorePc, kStoreInsn, 14u,
            kScratchFirst + 4u, kScratchStride, kScratchCount) ||
        gte_precision_scratch_store_pc_route_add(
            kScratchStorePc, kStoreInsn, 14u,
            kScratchFirst, kScratchStride, kScratchCount - 1u))
        return fail("conflicting scratch store registration was accepted");

    Projection projections[kScratchCount]{};
    for (uint32_t i = 0; i < kScratchCount; ++i) {
        const uint32_t slot = kScratchFirst + i * kScratchStride;
        projections[i] = scratch_projection(i);
        seed_scratch_source(slot, projections[i]);
        if (lookup(slot, projections[i].packed, projections[i].x16,
                   projections[i].y16, projections[i].z))
            return fail("generic GPU precision lookup admitted scratchpad");
    }

    constexpr uint32_t destination_base = 0x00160000u;
    for (uint32_t i = 0; i < kScratchCount; ++i) {
        const uint32_t slot = kScratchFirst + i * kScratchStride;
        const CopyRoute *route = scratch_route_for_slot(slot);
        if (!route || !add_copy(*route))
            return fail("a reviewed scratch slot has no copy route");
        copy_read(*route, slot, projections[i].packed);
        const uint32_t destination = destination_base + i * 4u;
        commit(destination);
        copy_store(*route, destination, projections[i].packed);
        if (!lookup(destination, projections[i].packed, projections[i].x16,
                    projections[i].y16, projections[i].z))
            return fail("one of the nine exact scratch slots lost provenance");
    }

    GtePrecisionDiagnostics d = diagnostics();
    if (d.registered_store_attempts != kScratchCount ||
        d.registered_store_accepts != kScratchCount ||
        d.copy_load_accepts != kScratchCount ||
        d.copy_store_accepts != kScratchCount)
        return fail("nine-slot scratch acceptance diagnostics are wrong");

    const CopyRoute &probe_route = kScratchCopyRoutes[0];
    constexpr uint32_t destination = 0x00161000u;

    reset_fixture();
    if (!add_capture(kOtherMfc2Pc) || !add_scratch_store() ||
        !add_copy(probe_route))
        return fail("could not configure wrong-slot scratch rejection");
    gte_test_seed_precise_projection(2, kPacked, kX16, kY16, kZ);
    capture(kOtherMfc2Pc);
    scratch_commit(kScratchFirst + 4u);
    scratch_store(kScratchFirst + 4u);
    copy_read(probe_route, kScratchFirst + 4u);
    commit(destination);
    copy_store(probe_route, destination);
    if (lookup(destination))
        return fail("scratch store outside the nine exact slots was accepted");

    reset_fixture();
    if (!add_capture(kOtherMfc2Pc) || !add_scratch_store() ||
        !add_copy(probe_route))
        return fail("could not configure uncommitted scratch rejection");
    capture(kOtherMfc2Pc);
    gte_precision_word_write_begin();
    scratch_store(kScratchFirst);
    copy_read(probe_route, kScratchFirst);
    commit(destination + 4u);
    copy_store(probe_route, destination + 4u);
    if (lookup(destination + 4u))
        return fail("uncommitted scratch store was accepted");

    reset_fixture();
    if (!add_capture(kOtherMfc2Pc) || !add_scratch_store() ||
        !add_copy(probe_route))
        return fail("could not configure scratch domain rejection");
    capture(kOtherMfc2Pc);
    commit(kScratchFirst & 0x1FFFFCu);
    scratch_store(kScratchFirst);
    copy_read(probe_route, kScratchFirst);
    commit(destination + 8u);
    copy_store(probe_route, destination + 8u);
    if (lookup(destination + 8u))
        return fail("main-RAM commit token authorized a scratch store");

    reset_fixture();
    if (!add_capture(kOtherMfc2Pc) || !add_scratch_store() ||
        !add_copy(probe_route))
        return fail("could not configure mismatched scratch commit rejection");
    capture(kOtherMfc2Pc);
    scratch_commit(kScratchFirst + kScratchStride);
    scratch_store(kScratchFirst);
    copy_read(probe_route, kScratchFirst);
    commit(destination + 0xCu);
    copy_store(probe_route, destination + 0xCu);
    if (lookup(destination + 0xCu))
        return fail("a different scratch word's commit token was accepted");

    reset_fixture();
    if (!add_capture(kOtherMfc2Pc) || !add_store())
        return fail("could not configure reverse domain rejection");
    capture(kOtherMfc2Pc);
    scratch_commit(kScratchFirst);
    store(kStorePc, destination + 0x10u);
    if (lookup(destination + 0x10u))
        return fail("scratch commit token authorized a main-RAM store");
    return 0;
}

int test_scratch_twenty_routes_are_independent_one_shot() {
    reset_fixture();
    if (!add_capture(kOtherMfc2Pc) || !add_scratch_store())
        return fail("could not configure scratch copy fixture");
    for (const CopyRoute &route : kCopyRoutes) {
        if (!add_copy(route))
            return fail("could not register an existing main-RAM copy route");
    }
    for (const CopyRoute &route : kScratchCopyRoutes) {
        if (!add_copy(route))
            return fail("copy route table cannot hold all 4+20 routes");
    }

    Projection projections[kScratchCount]{};
    for (uint32_t i = 0; i < kScratchCount; ++i) {
        projections[i] = scratch_projection(i);
        seed_scratch_source(
            kScratchFirst + i * kScratchStride, projections[i]);
    }

    constexpr uint32_t destination_base = 0x00180000u;
    uint32_t route_index = 0;
    for (const CopyRoute &route : kScratchCopyRoutes) {
        const uint32_t source =
            0x1F800000u + (route.load_insn & 0xFFFFu);
        const uint32_t slot_index =
            (source - kScratchFirst) / kScratchStride;
        if (slot_index >= kScratchCount ||
            source != kScratchFirst + slot_index * kScratchStride)
            return fail("scratch copy route names a non-slot source");
        const Projection &projection = projections[slot_index];
        copy_read(route, source, projection.packed);

        const uint32_t destination = destination_base + route_index * 8u;
        commit(destination);
        copy_store(route, destination, projection.packed);
        if (!lookup(destination, projection.packed, projection.x16,
                    projection.y16, projection.z))
            return fail("one of twenty scratch copy routes lost provenance");

        commit(destination + 4u);
        copy_store(route, destination + 4u, projection.packed);
        if (lookup(destination + 4u, projection.packed,
                   projection.x16, projection.y16, projection.z))
            return fail("scratch copy route capture was not one-shot");
        ++route_index;
    }

    const GtePrecisionDiagnostics d = diagnostics();
    if (d.registered_store_attempts != kScratchCount ||
        d.registered_store_accepts != kScratchCount ||
        d.copy_load_attempts != 20u || d.copy_load_accepts != 20u ||
        d.copy_store_attempts != 40u || d.copy_store_accepts != 20u ||
        d.copy_store_packed_rejections != 0u)
        return fail("twenty-route one-shot diagnostics are wrong");
    return 0;
}

int test_scratch_replay_speculation_and_timeline() {
    const CopyRoute &route = kScratchCopyRoutes[0];
    const Projection projection = scratch_projection(0);
    constexpr uint32_t source = kScratchFirst;
    constexpr uint32_t destination = 0x00190000u;

    reset_fixture();
    if (!add_capture(kOtherMfc2Pc) || !add_scratch_store() || !add_copy(route))
        return fail("could not configure speculative scratch fixture");
    gte_test_seed_precise_projection(
        2, projection.packed, projection.x16, projection.y16, projection.z);
    capture(kOtherMfc2Pc, projection.packed);
    gte_precision_speculative_begin();
    scratch_commit(source);
    scratch_store(source, projection.packed);
    gte_precision_speculative_end();
    copy_read(route, source, projection.packed);
    commit(destination);
    copy_store(route, destination, projection.packed);
    if (lookup(destination, projection.packed, projection.x16,
               projection.y16, projection.z))
        return fail("speculative scratch store escaped into provenance");
    scratch_commit(source);
    scratch_store(source, projection.packed);
    copy_read(route, source, projection.packed);
    commit(destination + 4u);
    copy_store(route, destination + 4u, projection.packed);
    if (!lookup(destination + 4u, projection.packed, projection.x16,
                projection.y16, projection.z))
        return fail("speculative scratch store consumed authoritative capture");

    reset_fixture();
    if (!add_capture(kOtherMfc2Pc) || !add_scratch_store() || !add_copy(route))
        return fail("could not configure replay scratch fixture");
    gte_test_seed_precise_projection(
        2, projection.packed, projection.x16, projection.y16, projection.z);
    capture(kOtherMfc2Pc, projection.packed);
    const GtePrecisionDiagnostics before_replay = diagnostics();
    (void)gte_replay_side_effects_begin();
    scratch_commit(source);
    scratch_store(source, projection.packed);
    gte_replay_side_effects_end();
    const GtePrecisionDiagnostics after_replay = diagnostics();
    if (after_replay.registered_store_attempts !=
        before_replay.registered_store_attempts)
        return fail("replayed scratch store changed diagnostics");
    copy_read(route, source, projection.packed);
    commit(destination + 8u);
    copy_store(route, destination + 8u, projection.packed);
    if (lookup(destination + 8u, projection.packed, projection.x16,
               projection.y16, projection.z))
        return fail("replayed scratch store escaped into provenance");
    scratch_commit(source);
    scratch_store(source, projection.packed);
    copy_read(route, source, projection.packed);
    commit(destination + 0xCu);
    copy_store(route, destination + 0xCu, projection.packed);
    if (!lookup(destination + 0xCu, projection.packed, projection.x16,
                projection.y16, projection.z))
        return fail("replayed scratch store consumed authoritative capture");

    copy_read(route, source, projection.packed);
    gte_precision_speculative_begin();
    commit(destination + 0x10u);
    copy_store(route, destination + 0x10u, projection.packed);
    gte_precision_speculative_end();
    if (lookup(destination + 0x10u, projection.packed, projection.x16,
               projection.y16, projection.z))
        return fail("speculative scratch copy store escaped into provenance");
    commit(destination + 0x14u);
    copy_store(route, destination + 0x14u, projection.packed);
    if (!lookup(destination + 0x14u, projection.packed, projection.x16,
                projection.y16, projection.z))
        return fail("speculative copy store consumed scratch source capture");

    copy_read(route, source, projection.packed);
    (void)gte_replay_side_effects_begin();
    commit(destination + 0x18u);
    copy_store(route, destination + 0x18u, projection.packed);
    gte_replay_side_effects_end();
    if (lookup(destination + 0x18u, projection.packed, projection.x16,
               projection.y16, projection.z))
        return fail("replayed scratch copy store escaped into provenance");
    commit(destination + 0x1Cu);
    copy_store(route, destination + 0x1Cu, projection.packed);
    if (!lookup(destination + 0x1Cu, projection.packed, projection.x16,
                projection.y16, projection.z))
        return fail("replayed copy store consumed scratch source capture");

    copy_read(route, source, projection.packed);
    gte_precision_timeline_invalidate();
    commit(destination + 0x20u);
    copy_store(route, destination + 0x20u, projection.packed);
    if (lookup(destination + 0x20u, projection.packed, projection.x16,
               projection.y16, projection.z))
        return fail("timeline restore retained scratch copy capture");
    copy_read(route, source, projection.packed);
    commit(destination + 0x24u);
    copy_store(route, destination + 0x24u, projection.packed);
    if (lookup(destination + 0x24u, projection.packed, projection.x16,
               projection.y16, projection.z))
        return fail("timeline restore retained scratch source provenance");
    return 0;
}

int test_perspective_depth_validity_propagates() {
    constexpr uint32_t source = 0x001A0000u;
    constexpr uint32_t destination = 0x001A1000u;

    reset_fixture();
    if (!add_capture() || !add_store())
        return fail("could not configure perspective-valid fixture");
    capture();
    commit(source);
    store(kStorePc, source);
    uint16_t z = 0;
    if (perspective_lookup(source, kPacked, &z) !=
            GTE_PRECISION_LOOKUP_ACCEPTED || z != kZ)
        return fail("strict perspective lookup rejected an unclamped depth");

    reset_fixture();
    if (!add_capture() || !add_store())
        return fail("could not configure perspective-invalid fixture");
    gte_test_set_precise_perspective_valid(2, 0);
    capture();
    commit(source);
    store(kStorePc, source);
    if (!lookup(source))
        return fail("perspective-invalid depth incorrectly disabled geometry");
    if (perspective_lookup(source) !=
            GTE_PRECISION_LOOKUP_PERSPECTIVE_INVALID)
        return fail("strict perspective lookup accepted clamped depth");

    const CopyRoute &route = kScratchCopyRoutes[0];
    reset_fixture();
    if (!add_capture(kOtherMfc2Pc) || !add_scratch_store() || !add_copy(route))
        return fail("could not configure perspective scratch-copy fixture");
    gte_test_set_precise_perspective_valid(2, 0);
    capture(kOtherMfc2Pc);
    scratch_commit(kScratchFirst);
    scratch_store(kScratchFirst);
    copy_read(route, kScratchFirst);
    commit(destination);
    copy_store(route, destination);
    if (!lookup(destination))
        return fail("scratch copy lost geometry-valid projection metadata");
    if (perspective_lookup(destination) !=
            GTE_PRECISION_LOOKUP_PERSPECTIVE_INVALID)
        return fail("scratch copy lost strict perspective-validity metadata");
    return 0;
}

}  // namespace

int main() {
    if (int rc = test_registration_and_committed_store()) return rc;
    if (int rc = test_captured_snapshot_and_multiple_stores()) return rc;
    if (int rc = test_fail_closed_paths()) return rc;
    if (int rc = test_speculation_and_timeline()) return rc;
    if (int rc = test_copy_registration_and_four_word_packet()) return rc;
    if (int rc = test_copy_snapshot_and_fail_closed_paths()) return rc;
    if (int rc = test_copy_speculation_replay_and_timeline()) return rc;
    if (int rc = test_scratch_nine_slots_and_fail_closed_domains()) return rc;
    if (int rc = test_scratch_twenty_routes_are_independent_one_shot()) return rc;
    if (int rc = test_scratch_replay_speculation_and_timeline()) return rc;
    if (int rc = test_perspective_depth_validity_propagates()) return rc;
    std::puts("PASS: reviewed RAM/scratch MFC2/SW, strict depth, and 24 copy routes fail closed");
    return 0;
}
