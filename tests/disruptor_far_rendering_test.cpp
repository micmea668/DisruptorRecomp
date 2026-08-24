#include "cpu_state.h"
#include "mod_plugins.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::array<std::uint8_t, 2 * 1024 * 1024> g_ram{};
PSXModFunctionEntryCallback g_renderer_callback = nullptr;
std::uint32_t g_registered_address = 0u;
bool g_game_started = false;
bool g_netplay = false;
std::vector<std::pair<std::uint32_t, std::uint32_t>> g_writes;
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

int g_ls_replay_active = 0;
int g_ls_mode = 0;

int psx_mod_register_function_entry_plugin(
        const char *id, std::uint32_t address,
        PSXModFunctionEntryCallback callback) {
    if (!id || std::string(id) != "disruptor.far_rendering.renderer" ||
        address == 0u || !callback || g_renderer_callback) {
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
    seed_byte(address, value);
}

std::uint16_t psx_mod_read_half(std::uint32_t address) {
    const std::size_t p = physical(address);
    return static_cast<std::uint16_t>(g_ram[p + 0]) |
           (static_cast<std::uint16_t>(g_ram[p + 1]) << 8);
}

void psx_mod_write_half(std::uint32_t address, std::uint16_t value) {
    const std::size_t p = physical(address);
    g_ram[p + 0] = static_cast<std::uint8_t>(value >> 0);
    g_ram[p + 1] = static_cast<std::uint8_t>(value >> 8);
}

std::uint32_t psx_mod_read_word(std::uint32_t address) {
    return load_word(address);
}

/* Any call is a test failure: the feature must be savestate-safe and may only
 * substitute destination GPRs after exact renderer loads. */
void psx_mod_write_word(std::uint32_t address, std::uint32_t value) {
    g_writes.emplace_back(address, value);
    seed_word(address, value);
}

}  // extern "C"

#include "../src/disruptor_far_rendering.cpp"

namespace {

constexpr std::uint32_t kTestPlayer = 0x80010000u;
constexpr std::uint32_t kRendererContext = 0x80020000u;
constexpr std::uint32_t kTestStack = 0x80030000u;
constexpr std::uint32_t kPrimitiveBuffer = 0x80040000u;
constexpr std::uint32_t kReviewedVisibleSpanBase = 0x80079D18u;

static_assert(kVisibleSpanBase == kReviewedVisibleSpanBase,
              "runtime visible-span base must match the reviewed guest arena");

struct DiagnosticsV1Prefix {
    std::uint32_t abi_version;
    std::uint32_t struct_size;
    std::int32_t preset;
    std::int32_t active;
    std::int32_t gameplay_ready;
    std::int32_t netplay_blocked;
    std::uint64_t renderer_entries;
    std::uint64_t completed_frames;
    std::uint64_t substituted_loads;
    std::uint64_t rejected_entries;
    std::uint64_t nested_entries;
    std::uint64_t missed_epilogues;
    std::uint64_t invalid_globals;
    std::uint64_t primitive_samples;
    std::uint32_t source_far_distance;
    std::int32_t source_fade_start;
    std::uint32_t effective_far_distance;
    std::int32_t effective_fade_start;
    std::uint32_t primitive_start;
    std::uint32_t primitive_end;
    std::uint32_t primitive_last_delta;
    std::uint32_t primitive_high_water;
    double render_wall_us_p50;
    double render_wall_us_p95;
    double render_wall_us_max;
};

static_assert(sizeof(DiagnosticsV1Prefix) ==
                  offsetof(DisruptorFarRenderingDiagnostics,
                           depth_fade_mode),
              "diagnostics ABI v2 must append to the complete v1 prefix");

struct DiagnosticsV2Prefix {
    DiagnosticsV1Prefix v1;
    std::int32_t depth_fade_mode;
    std::uint32_t reserved_v2;
    std::uint64_t far_load_substitutions;
    std::uint64_t fade_load_substitutions;
};

static_assert(sizeof(DiagnosticsV2Prefix) ==
                  offsetof(DisruptorFarRenderingDiagnostics,
                           traversal_recursion_entries),
              "diagnostics ABI v3 must append to the complete v2 prefix");

DisruptorFarRenderingDiagnostics diagnostics() {
    DisruptorFarRenderingDiagnostics value{};
    expect(disruptor_far_rendering_get_diagnostics(
               &value, static_cast<std::uint32_t>(sizeof(value))) == 1,
           "diagnostics snapshot succeeds");
    return value;
}

void reset_state() {
    disruptor_far_rendering_reset_session();
    g_ram.fill(0u);
    g_game_started = false;
    g_netplay = false;
    g_ls_mode = 0;
    g_ls_replay_active = 0;
    g_writes.clear();
}

void seed_live_game(CPUState *cpu, std::uint32_t far_distance = 1024u,
                    std::int32_t fade_start = 256) {
    g_game_started = true;
    seed_word(kMaximumHealth, 100u);
    seed_word(kCurrentHealth, 80u);
    seed_byte(kSceneMode, 0u);
    seed_byte(kScriptedCamera, 0u);
    seed_word(kPlayerPointer, kTestPlayer);
    seed_byte(kTestPlayer + kPlayerInactiveOffset, 0u);
    seed_word(kFarDistance, far_distance);
    seed_word(kFadeStart, static_cast<std::uint32_t>(fade_start));
    seed_word(kRendererContext + kRendererPrimitiveStartOffset,
              kPrimitiveBuffer);
    seed_word(kPrimitivePointer, kPrimitiveBuffer);
    if (cpu) {
        cpu->gpr[4] = kRendererContext;
        cpu->gpr[29] = kTestStack;
    }
}

void enter(CPUState *cpu) {
    g_renderer_callback(cpu, kRendererEntry);
}

void exit_renderer(CPUState *cpu) {
    disruptor_far_rendering_instruction_hook(
        cpu, kRendererRestore, kRendererRestoreOpcode,
        DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
}

void hook_after(CPUState *cpu, std::uint32_t pc, std::uint32_t opcode) {
    disruptor_far_rendering_instruction_hook(
        cpu, pc, opcode, DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
}

void invoke_load(CPUState *cpu, const DistanceLoadSite& site,
                 std::uint32_t saved_ra) {
    seed_word(cpu->gpr[29] + site.saved_ra_offset, saved_ra);
    const std::uint32_t destination = (site.opcode >> 16u) & 31u;
    cpu->gpr[destination] =
        site.kind == DistanceKind::Far
            ? load_word(kFarDistance)
            : load_word(kFadeStart);
    disruptor_far_rendering_instruction_hook(
        cpu, site.pc, site.opcode,
        DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
}

void invoke_first_far(CPUState *cpu) {
    invoke_load(cpu, kDistanceLoadSites[0], 0x800410E0u);
}

void invoke_first_fade(CPUState *cpu) {
    invoke_load(cpu, kDistanceLoadSites[5], 0x8004106Cu);
}

void test_registration_and_stateless_substitution() {
    expect(g_renderer_callback != nullptr, "renderer callback registered");
    expect(g_registered_address == kRendererEntry,
           "renderer entry is address pinned");

    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    expect(disruptor_far_rendering_set_preset(
               DISRUPTOR_FAR_RENDERING_EXTENDED) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "1.25x preset accepted");
    enter(&cpu);
    expect(load_word(kFarDistance) == 1024u && load_word(kFadeStart) == 256u,
           "renderer entry never changes guest distance globals");

    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1280u,
           "exact far load receives the 1.25x value in its destination GPR");
    invoke_first_fade(&cpu);
    expect(static_cast<std::int32_t>(cpu.gpr[2]) == 512,
           "fade load shifts by the same delta and preserves retail ramp");
    expect(g_writes.empty(), "load substitution performs zero guest writes");

    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x240u);
    exit_renderer(&cpu);
    const auto d = diagnostics();
    expect(d.completed_frames == 1u && d.substituted_loads == 2u,
           "completed frame and exact substituted loads are counted");
    expect(d.far_load_substitutions == 1u &&
               d.fade_load_substitutions == 1u,
           "far and fade substitutions are counted separately");
    expect(d.primitive_samples == 1u &&
               d.primitive_last_delta == 0x240u &&
               d.primitive_high_water == 0x240u,
           "primitive pressure uses context start and epilogue end");
    expect(d.render_wall_us_p50 >= 0.0 &&
               d.render_wall_us_p95 >= d.render_wall_us_p50 &&
               d.render_wall_us_max >= d.render_wall_us_p95,
           "renderer wall-span percentiles are ordered");
}

void test_independent_depth_fade_modes() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);

    expect(disruptor_far_rendering_set_depth_fade_mode(
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "no-depth-fade diagnostic mode is accepted");
    expect(disruptor_far_rendering_get_preset() ==
               DISRUPTOR_FAR_RENDERING_RETAIL &&
               disruptor_far_rendering_get_depth_fade_mode() ==
                   DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED,
           "fade mode is independent of the draw-distance preset");

    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1024u,
           "retail draw distance leaves far loads untouched");
    invoke_first_fade(&cpu);
    expect(cpu.gpr[2] == 1024u,
           "no-depth-fade moves fade start to the retail far plane");
    auto d = diagnostics();
    expect(d.depth_fade_mode ==
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED &&
               d.source_fade_start == 256 &&
               d.effective_far_distance == 1024u &&
               d.effective_fade_start == 1024,
           "diagnostics expose the zero-width fade interval");
    expect(d.substituted_loads == 1u &&
               d.far_load_substitutions == 0u &&
               d.fade_load_substitutions == 1u,
           "fade-only diagnostic counts only the audited fade load");

    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    expect(disruptor_far_rendering_get_depth_fade_mode() ==
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED,
           "distance changes preserve the independently selected fade mode");
    invoke_first_far(&cpu);
    invoke_first_fade(&cpu);
    d = diagnostics();
    expect(cpu.gpr[12] == 1536u && cpu.gpr[2] == 1536u,
           "no-depth-fade follows the selected effective far plane");
    expect(d.substituted_loads == 2u &&
               d.far_load_substitutions == 1u &&
               d.fade_load_substitutions == 1u,
           "combined extension reports one far and one fade substitution");
    expect(load_word(kFarDistance) == 1024u &&
               load_word(kFadeStart) == 256u && g_writes.empty(),
           "no-depth-fade and extended distance perform zero guest writes");

    expect(disruptor_far_rendering_set_depth_fade_mode(
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "retail fade ramp remains the safe escape hatch");
    invoke_first_fade(&cpu);
    expect(cpu.gpr[2] == 768u,
           "retail ramp still shifts with an extended far plane");
    d = diagnostics();
    expect(d.depth_fade_mode ==
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL &&
               d.substituted_loads == 1u &&
               d.far_load_substitutions == 0u &&
               d.fade_load_substitutions == 1u,
           "fade-mode changes reset comparison metrics without changing preset");
}

void test_live_billboard_fade_site() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);

    const DistanceLoadSite& site = kDistanceLoadSites.back();
    expect(site.pc == 0x80043184u && site.opcode == 0x8F820494u &&
               site.kind == DistanceKind::Fade &&
               site.saved_ra_offset == 68u &&
               site.allowed_return_count == 1u &&
               site.allowed_return_addresses[0] == 0x80043FE0u,
           "live billboard fade site retains its exact ancestry contract");

    invoke_load(&cpu, site, 0x80043FE0u);
    auto d = diagnostics();
    expect(cpu.gpr[2] == 256u && d.substituted_loads == 0u &&
               d.fade_load_substitutions == 0u,
           "Retail leaves the live billboard fade load unchanged");

    expect(disruptor_far_rendering_set_preset(
               DISRUPTOR_FAR_RENDERING_FAR) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "1.5x preset is accepted for the billboard fade test");
    invoke_load(&cpu, site, 0x80043FE0u);
    d = diagnostics();
    expect(cpu.gpr[2] == 768u && d.substituted_loads == 1u &&
               d.far_load_substitutions == 0u &&
               d.fade_load_substitutions == 1u,
           "1.5x shifts the live billboard retail fade with one fade count");

    invoke_load(&cpu, site, 0x80001234u);
    d = diagnostics();
    expect(cpu.gpr[2] == 256u && d.substituted_loads == 1u &&
               d.fade_load_substitutions == 1u,
           "wrong billboard saved RA fails closed without incrementing counts");

    expect(disruptor_far_rendering_set_depth_fade_mode(
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "nearest-row mode is accepted for the billboard fade test");
    invoke_load(&cpu, site, 0x80043FE0u);
    d = diagnostics();
    expect(cpu.gpr[2] == 1536u && d.substituted_loads == 1u &&
               d.far_load_substitutions == 0u &&
               d.fade_load_substitutions == 1u,
           "nearest-row mode moves the live billboard fade to the 1.5x far plane");
    expect(load_word(kFarDistance) == 1024u &&
               load_word(kFadeStart) == 256u && g_writes.empty(),
           "billboard fade substitution performs zero guest writes");
}

void test_all_sites_and_ancestry_contracts() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);

    for (const DistanceLoadSite& site : kDistanceLoadSites) {
        const std::uint32_t destination = (site.opcode >> 16u) & 31u;
        const std::uint32_t expected =
            site.kind == DistanceKind::Far ? 1536u : 768u;
        for (std::uint8_t i = 0; i < site.allowed_return_count; ++i) {
            invoke_load(&cpu, site, site.allowed_return_addresses[i]);
            expect(cpu.gpr[destination] == expected,
                   "every audited saved-RA ancestry substitutes exactly");
        }

        seed_word(cpu.gpr[29] + site.saved_ra_offset, 0x80001234u);
        cpu.gpr[destination] = site.kind == DistanceKind::Far ? 1024u : 256u;
        disruptor_far_rendering_instruction_hook(
            &cpu, site.pc, site.opcode,
            DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
        expect(cpu.gpr[destination] ==
                   (site.kind == DistanceKind::Far ? 1024u : 256u),
               "same helper outside renderer ancestry remains retail");
    }

    cpu.gpr[2] = 1024u;
    disruptor_far_rendering_instruction_hook(
        &cpu, 0x800247C4u, 0x8F8204A0u,
        DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 1024u,
           "unreviewed global load outside renderer remains untouched");

    cpu.gpr[3] = 1024u;
    disruptor_far_rendering_instruction_hook(
        &cpu, 0x8004030Cu, 0x8F8304A0u,
        DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
    cpu.gpr[2] = 256u;
    disruptor_far_rendering_instruction_hook(
        &cpu, 0x80040464u, 0x8F820494u,
        DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
    expect(cpu.gpr[3] == 1024u && cpu.gpr[2] == 256u &&
               load_word(kFarDistance) == 1024u &&
               load_word(kFadeStart) == 256u,
           "setup loads which feed guest stores are explicitly excluded");
    expect(g_writes.empty(), "all audited load routes remain RAM-read-only");
    const auto d = diagnostics();
    expect(d.substituted_loads == 16u &&
               d.far_load_substitutions == 7u &&
               d.fade_load_substitutions == 9u,
           "all exact sites contribute to the correct split counter");
}

void test_retail_baseline_and_metric_reset() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    enter(&cpu);
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1024u,
           "Retail preset leaves exact load destination unchanged");
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x500u);
    exit_renderer(&cpu);
    auto d = diagnostics();
    expect(d.completed_frames == 1u && d.substituted_loads == 0u &&
               d.far_load_substitutions == 0u &&
               d.fade_load_substitutions == 0u &&
               d.primitive_high_water == 0x500u,
           "Retail collects a non-substituted A/B baseline");

    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_EXTENDED);
    d = diagnostics();
    expect(d.completed_frames == 0u && d.substituted_loads == 0u &&
               d.far_load_substitutions == 0u &&
               d.fade_load_substitutions == 0u &&
               d.primitive_samples == 0u && d.primitive_high_water == 0u &&
               d.render_wall_us_max == 0.0,
           "preset change resets comparison metrics");
    expect(d.source_far_distance == 0u &&
               d.effective_far_distance == 0u &&
               d.renderer_entries == 1u,
           "preset switch clears old labels but retains safety counters");
}

void test_current_globals_and_signed_fade() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu, 1200u, 432);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_EXTENDED);
    invoke_first_far(&cpu);
    invoke_first_fade(&cpu);
    expect(cpu.gpr[12] == 1500u &&
               static_cast<std::int32_t>(cpu.gpr[2]) == 732,
           "stateless hooks scale current script-provided globals");
    expect(load_word(kFarDistance) == 1200u && load_word(kFadeStart) == 432u,
           "script globals stay authoritative in RAM");

    seed_word(kFarDistance, 512u);
    seed_word(kFadeStart, static_cast<std::uint32_t>(-256));
    invoke_first_far(&cpu);
    invoke_first_fade(&cpu);
    expect(cpu.gpr[12] == 640u &&
               static_cast<std::int32_t>(cpu.gpr[2]) == -128,
           "negative fade remains signed while shifting with far");
    const auto d = diagnostics();
    expect(d.source_fade_start == -256 &&
               d.effective_fade_start == -128,
           "signed fade is preserved in public diagnostics");
}

void test_guards_and_exact_dispatch() {
    reset_state();
    CPUState cpu{};
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_EXTENDED);
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 0u, "pre-game load hook is side-effect free");

    seed_live_game(&cpu);
    seed_byte(kSceneMode, 1u);
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1024u, "map/cutscene scene mode fails closed");
    seed_byte(kSceneMode, 0u);
    seed_byte(kScriptedCamera, 1u);
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1024u, "scripted camera fails closed");
    seed_byte(kScriptedCamera, 0u);

    seed_word(kFarDistance, 0u);
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 0u, "invalid distance global fails closed");
    seed_word(kFarDistance, 1024u);
    seed_word(kFadeStart, 2048u);
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1024u, "fade beyond far fails closed");
    seed_word(kFadeStart, 256u);

    seed_word(kTestStack + 164u, 0x800410E0u);
    cpu.gpr[12] = 999u;
    disruptor_far_rendering_instruction_hook(
        &cpu, 0x8003A914u, 0x8F8C04A0u,
        DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
    expect(cpu.gpr[12] == 999u,
           "post-load source mismatch fails closed before substitution");

    seed_word(kTestStack + 164u, 0x800410E0u);
    cpu.gpr[12] = 1024u;
    disruptor_far_rendering_instruction_hook(
        &cpu, 0x8003A914u, 0x8F8C04A0u,
        DISRUPTOR_FAR_RENDERING_BEFORE_INSTRUCTION);
    disruptor_far_rendering_instruction_hook(
        &cpu, 0x8003A914u, 0x8F8C04A1u,
        DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
    expect(cpu.gpr[12] == 1024u,
           "phase/opcode mismatch is an exact no-op");
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1280u, "exact AFTER hook substitutes");
    expect(diagnostics().invalid_globals >= 2u,
           "invalid global values remain visible in diagnostics");
}

void test_nested_toggle_netplay_and_lockstep() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_EXTENDED);
    enter(&cpu);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 24u);
    enter(&cpu);
    auto d = diagnostics();
    expect(d.nested_entries == 1u && d.missed_epilogues == 1u &&
               d.rejected_entries == 1u,
           "nested/missed entry fails closed for diagnostics");
    expect(g_writes.empty(), "nested cleanup never writes guest RAM");

    enter(&cpu);
    expect(disruptor_far_rendering_set_preset(
               DISRUPTOR_FAR_RENDERING_RETAIL) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "Retail toggle always succeeds");
    expect(diagnostics().active == 0 && g_writes.empty(),
           "mid-frame toggle abandons metrics without guest mutation");

    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    disruptor_far_rendering_set_depth_fade_mode(
        DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED);
    enter(&cpu);
    g_netplay = true;
    expect(disruptor_far_rendering_get_preset() ==
               DISRUPTOR_FAR_RENDERING_RETAIL &&
               disruptor_far_rendering_get_depth_fade_mode() ==
                   DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL,
           "netplay transition forces distance and fade controls to Retail");
    expect(disruptor_far_rendering_set_preset(
               DISRUPTOR_FAR_RENDERING_EXTENDED) ==
               DISRUPTOR_FAR_RENDERING_NETPLAY_BLOCKED,
           "netplay rejects non-Retail preset");
    expect(disruptor_far_rendering_set_preset(
               DISRUPTOR_FAR_RENDERING_RETAIL) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "Retail remains selectable during netplay");
    expect(disruptor_far_rendering_set_depth_fade_mode(
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED) ==
               DISRUPTOR_FAR_RENDERING_NETPLAY_BLOCKED,
           "netplay rejects no-depth-fade diagnostic mode");
    expect(disruptor_far_rendering_set_depth_fade_mode(
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "retail fade ramp remains selectable during netplay");

    g_netplay = false;
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_EXTENDED);
    enter(&cpu);
    g_ls_mode = 1;
    expect(disruptor_far_rendering_set_depth_fade_mode(
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED) ==
               DISRUPTOR_FAR_RENDERING_INVALID_PRESET &&
               disruptor_far_rendering_get_depth_fade_mode() ==
                   DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL,
           "lockstep comparator rejects no-depth-fade mode");
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1024u, "lockstep record suppresses substitution");
    exit_renderer(&cpu);
    expect(diagnostics().completed_frames == 0u,
           "comparator transition abandons incomplete wall-span metrics");
    g_ls_mode = 0;
    g_ls_replay_active = 1;
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1024u, "lockstep replay suppresses substitution");
    expect(g_writes.empty(), "netplay/lockstep paths perform zero guest writes");
}

void test_savestate_abandon_is_stateless() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    enter(&cpu);
    expect(diagnostics().active == 1, "metrics window starts at entry");
    disruptor_far_rendering_abandon_metrics();
    expect(diagnostics().active == 0,
           "savestate load abandons the stale wall-span window");

    invoke_first_far(&cpu);
    invoke_first_fade(&cpu);
    expect(cpu.gpr[12] == 1536u && cpu.gpr[2] == 768u,
           "exact loads still substitute without an entry metrics window");
    exit_renderer(&cpu);
    const auto d = diagnostics();
    expect(d.completed_frames == 0u && d.substituted_loads == 2u,
           "resumed mid-render loads are counted without false frame timing");
    expect(d.far_load_substitutions == 1u &&
               d.fade_load_substitutions == 1u,
           "savestate-safe substitutions retain split diagnostics");
    expect(g_writes.empty(), "savestate-safe path never mutates guest RAM");
}

void test_visibility_recursion_and_packet_stages() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(
        DISRUPTOR_FAR_RENDERING_EXTENDED);
    enter(&cpu);

    cpu.gpr[31] = 0x800410E0u;
    cpu.gpr[19] = 0u;
    hook_after(&cpu, kTraversalDepthLoad, kTraversalDepthLoadOpcode);
    cpu.gpr[31] = 0x8003AE54u;
    cpu.gpr[19] = 10u;
    hook_after(&cpu, kTraversalDepthLoad, kTraversalDepthLoadOpcode);
    cpu.gpr[31] = 0x8003AF1Cu;
    cpu.gpr[19] = 11u;
    hook_after(&cpu, kTraversalDepthLoad, kTraversalDepthLoadOpcode);
    cpu.gpr[31] = 0x80001234u;
    cpu.gpr[19] = 99u;
    hook_after(&cpu, kTraversalDepthLoad, kTraversalDepthLoadOpcode);

    hook_after(&cpu, kTraversalRoomMark, kTraversalRoomMarkOpcode);
    hook_after(&cpu, kTraversalRoomMark, kTraversalRoomMarkOpcode);
    hook_after(&cpu, kTraversalRoomMark, kTraversalRoomMarkOpcode);

    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x20u);
    hook_after(&cpu, kAfterTraversal, kAfterTraversalOpcode);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x40u);
    cpu.gpr[2] = kReviewedVisibleSpanBase + 3u * kVisibleSpanStride;
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0xA0u);
    hook_after(&cpu, kAfterVisibleLoop, kAfterVisibleLoopOpcode);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x120u);
    exit_renderer(&cpu);

    auto d = diagnostics();
    expect(d.traversal_recursion_entries == 3u &&
               d.traversal_cap_hits == 1u &&
               d.traversal_max_depth_last == 11u &&
               d.traversal_max_depth_high_water == 11u,
           "recursion observer records valid ancestry, attempted max and cap");
    expect(d.visibility_samples == 1u &&
               d.traversal_rooms_last == 3u &&
               d.traversal_rooms_high_water == 3u &&
               d.submitted_spans_last == 3u &&
               d.submitted_spans_high_water == 3u,
           "completed visibility sample reports reached rooms and span records");
    expect(d.packet_stage_samples == 1u &&
               d.packet_entry_to_traversal_end_last == 0x20u &&
               d.packet_traversal_to_visible_start_last == 0x20u &&
               d.packet_visible_loop_last == 0x60u &&
               d.packet_post_visible_loop_last == 0x80u,
           "ordered primitive boundaries publish exact stage deltas");
    expect(d.observer_sequence_errors == 0u,
           "valid observer sequence has no errors");

    enter(&cpu);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x10u);
    cpu.gpr[2] = kReviewedVisibleSpanBase + kVisibleSpanStride;
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x30u);
    hook_after(&cpu, kAfterVisibleLoop, kAfterVisibleLoopOpcode);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x40u);
    exit_renderer(&cpu);
    d = diagnostics();
    expect(d.packet_stage_samples == 1u &&
               d.visibility_samples == 1u &&
               d.traversal_rooms_last == 0u &&
               d.submitted_spans_last == 0u &&
               d.observer_sequence_errors >= 1u,
           "out-of-order boundaries fail closed without publishing stale data");
}

void test_portal_and_object_decision_observers() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(
        DISRUPTOR_FAR_RENDERING_EXTENDED);
    enter(&cpu);

    constexpr std::uint32_t kSourceSquared = 1024u * 1024u;
    constexpr std::uint32_t kEffectiveSquared = 1280u * 1280u;
    static_assert(kSourceSquared < 1200000u &&
                      1200000u < kEffectiveSquared,
                  "portal flip candidate must lie between both far planes");

    invoke_first_far(&cpu);
    cpu.gpr[11] = kEffectiveSquared;
    cpu.gpr[2] = 1200000u;
    hook_after(&cpu, kPortalFirstCandidate,
               kPortalFirstCandidateOpcode);

    invoke_first_far(&cpu);
    cpu.gpr[11] = kEffectiveSquared;
    cpu.gpr[2] = 1700000u;
    hook_after(&cpu, kPortalFirstCandidate,
               kPortalFirstCandidateOpcode);
    cpu.gpr[2] = 1700000u;
    hook_after(&cpu, kPortalSecondCandidate,
               kPortalSecondCandidateOpcode);
    cpu.gpr[2] = 1200000u;
    hook_after(&cpu, kPortalFinalCandidate,
               kPortalFinalCandidateOpcode);

    invoke_first_far(&cpu);
    cpu.gpr[11] = kEffectiveSquared;
    cpu.gpr[2] = 1700000u;
    hook_after(&cpu, kPortalFirstCandidate,
               kPortalFirstCandidateOpcode);
    hook_after(&cpu, kPortalSecondCandidate,
               kPortalSecondCandidateOpcode);
    hook_after(&cpu, kPortalFinalCandidate,
               kPortalFinalCandidateOpcode);

    for (std::size_t i = 0u; i < kObjectDecisionSites.size(); ++i) {
        const ObjectDecisionSite& object = kObjectDecisionSites[i];
        const DistanceLoadSite& load = kDistanceLoadSites[i + 1u];
        invoke_load(&cpu, load, load.allowed_return_addresses[0]);
        cpu.gpr[object.threshold_register] = 1100u;
        cpu.gpr[object.result_register] = 0u;
        hook_after(&cpu, object.decision_pc, object.decision_opcode);
    }

    invoke_load(&cpu, kDistanceLoadSites[1], 0x800411ACu);
    cpu.gpr[18] = 1400u;
    cpu.gpr[3] = 1u;
    hook_after(&cpu, 0x8003B8D8u, 0x0072182Au);

    invoke_load(&cpu, kDistanceLoadSites[1], 0x800411ACu);
    cpu.gpr[18] = 1100u;
    cpu.gpr[3] = 1u; /* impossible result: token must fail closed */
    hook_after(&cpu, 0x8003B8D8u, 0x0072182Au);

    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x80u);
    exit_renderer(&cpu);
    const auto d = diagnostics();
    expect(d.portal_far_tests == 7u &&
               d.portal_shortcut_relaxations == 1u &&
               d.portal_final_decisions == 2u &&
               d.portal_final_effective_rejections == 1u &&
               d.portal_final_decision_flips == 1u,
           "portal observer separates shortcut relaxations from final flips");
    expect(d.object_far_decisions == 5u &&
               d.object_effective_rejections == 1u &&
               d.object_decision_flips == 4u,
           "all object observers report direct retail-reject/effective-accept flips");
    expect(d.observer_sequence_errors == 1u,
           "impossible post-SLT result invalidates exactly one pending token");
    expect(g_writes.empty(),
           "visibility decision observers never write guest state");
}

void test_observer_safety_guards_and_abandon() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    cpu.gpr[31] = 0x800410E0u;
    cpu.gpr[19] = 11u;
    hook_after(&cpu, kTraversalDepthLoad, kTraversalDepthLoadOpcode);
    expect(diagnostics().traversal_recursion_entries == 0u,
           "observer is inert outside an active renderer frame");

    disruptor_far_rendering_set_preset(
        DISRUPTOR_FAR_RENDERING_EXTENDED);
    enter(&cpu);
    invoke_load(&cpu, kDistanceLoadSites[1], 0x800411ACu);
    disruptor_far_rendering_abandon_metrics();
    cpu.gpr[18] = 1100u;
    cpu.gpr[3] = 0u;
    hook_after(&cpu, 0x8003B8D8u, 0x0072182Au);
    auto d = diagnostics();
    expect(d.object_far_decisions == 0u &&
               d.observer_sequence_errors == 0u,
           "savestate abandon clears pending decisions without false errors");

    enter(&cpu);
    g_ls_mode = 1;
    hook_after(&cpu, kTraversalRoomMark, kTraversalRoomMarkOpcode);
    g_ls_mode = 0;
    g_netplay = true;
    hook_after(&cpu, kTraversalRoomMark, kTraversalRoomMarkOpcode);
    g_netplay = false;
    exit_renderer(&cpu);
    d = diagnostics();
    expect(d.visibility_samples == 0u &&
               d.traversal_rooms_high_water == 0u,
           "comparator and netplay transitions suppress passive observers");
}

void test_reset_and_versioned_snapshot() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    expect(disruptor_far_rendering_set_preset(99) ==
               DISRUPTOR_FAR_RENDERING_INVALID_PRESET,
           "invalid preset rejected");
    expect(disruptor_far_rendering_set_depth_fade_mode(99) ==
               DISRUPTOR_FAR_RENDERING_INVALID_PRESET,
           "invalid depth-fade mode rejected");
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    disruptor_far_rendering_set_depth_fade_mode(
        DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED);
    enter(&cpu);
    disruptor_far_rendering_reset_session();
    const auto d = diagnostics();
    expect(disruptor_far_rendering_get_preset() ==
               DISRUPTOR_FAR_RENDERING_RETAIL &&
               disruptor_far_rendering_get_depth_fade_mode() ==
                   DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL &&
               d.renderer_entries == 0u && g_writes.empty(),
           "session reset clears both host-only controls without writes");
    expect(d.abi_version ==
               DISRUPTOR_FAR_RENDERING_DIAGNOSTICS_ABI_VERSION &&
               d.struct_size == sizeof(d),
           "diagnostics report versioned ABI size");

    struct SmallSnapshot {
        std::uint32_t abi_version;
        std::uint32_t struct_size;
        std::uint32_t canary;
    } small{0u, 0u, 0xDEADBEEFu};
    expect(disruptor_far_rendering_get_diagnostics(
               reinterpret_cast<DisruptorFarRenderingDiagnostics *>(&small),
               8u) == 1 &&
               small.abi_version ==
                   DISRUPTOR_FAR_RENDERING_DIAGNOSTICS_ABI_VERSION &&
               small.struct_size == sizeof(DisruptorFarRenderingDiagnostics) &&
               small.canary == 0xDEADBEEFu,
           "versioned diagnostics honor caller capacity");
    expect(disruptor_far_rendering_get_diagnostics(
               reinterpret_cast<DisruptorFarRenderingDiagnostics *>(&small),
               7u) == 0 &&
               small.canary == 0xDEADBEEFu,
           "diagnostics reject buffers too small for ABI version and size");

    struct V1SnapshotWithCanary {
        DiagnosticsV1Prefix snapshot;
        std::uint32_t canary;
    } v1{{}, 0xA5A55A5Au};
    expect(disruptor_far_rendering_get_diagnostics(
               reinterpret_cast<DisruptorFarRenderingDiagnostics *>(
                   &v1.snapshot),
               static_cast<std::uint32_t>(sizeof(v1.snapshot))) == 1 &&
               v1.snapshot.abi_version ==
                   DISRUPTOR_FAR_RENDERING_DIAGNOSTICS_ABI_VERSION &&
               v1.snapshot.struct_size ==
                   sizeof(DisruptorFarRenderingDiagnostics) &&
               v1.canary == 0xA5A55A5Au,
           "ABI v3 preserves and honors a complete v1 caller capacity");

    struct V2SnapshotWithCanary {
        DiagnosticsV2Prefix snapshot;
        std::uint32_t canary;
    } v2{{}, 0x5AA5A55Au};
    expect(disruptor_far_rendering_get_diagnostics(
               reinterpret_cast<DisruptorFarRenderingDiagnostics *>(
                   &v2.snapshot),
               static_cast<std::uint32_t>(sizeof(v2.snapshot))) == 1 &&
               v2.snapshot.v1.abi_version ==
                   DISRUPTOR_FAR_RENDERING_DIAGNOSTICS_ABI_VERSION &&
               v2.snapshot.v1.struct_size ==
                   sizeof(DisruptorFarRenderingDiagnostics) &&
               v2.canary == 0x5AA5A55Au,
           "ABI v3 appends fields and honors a complete v2 caller capacity");
}

}  // namespace

int main() {
    test_registration_and_stateless_substitution();
    test_independent_depth_fade_modes();
    test_live_billboard_fade_site();
    test_all_sites_and_ancestry_contracts();
    test_retail_baseline_and_metric_reset();
    test_current_globals_and_signed_fade();
    test_guards_and_exact_dispatch();
    test_nested_toggle_netplay_and_lockstep();
    test_savestate_abandon_is_stateless();
    test_visibility_recursion_and_packet_stages();
    test_portal_and_object_decision_observers();
    test_observer_safety_guards_and_abandon();
    test_reset_and_versioned_snapshot();

    expect(g_writes.empty(), "far rendering must perform zero guest RAM writes");
    if (g_failures != 0) {
        std::cerr << g_failures << " far-rendering test(s) failed\n";
        return 1;
    }
    std::cout << "Disruptor far-rendering contracts: PASS\n";
    return 0;
}
