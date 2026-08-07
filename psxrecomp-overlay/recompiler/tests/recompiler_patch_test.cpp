#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "code_generator.h"
#include "config_loader.h"
#include "control_flow.h"
#include "fmt/format.h"
#include "gte_register_classification.h"
#include "recompiler_patch.h"

namespace fs = std::filesystem;
using PSXRecompV4::RecompilerPatch;

namespace {

int failures = 0;

void check(bool condition, const std::string& name) {
    if (condition) {
        fmt::print("PASS  {}\n", name);
    } else {
        fmt::print(stderr, "FAIL  {}\n", name);
        ++failures;
    }
}

template <typename Fn>
void check_throws(Fn&& fn, const std::string& needle,
                  const std::string& name) {
    try {
        fn();
        check(false, name + " (did not throw)");
    } catch (const std::exception& e) {
        check(std::string(e.what()).find(needle) != std::string::npos,
              name + " (" + e.what() + ")");
    }
}

void append_word(std::vector<uint8_t>& bytes, uint32_t word) {
    bytes.push_back(static_cast<uint8_t>(word));
    bytes.push_back(static_cast<uint8_t>(word >> 8));
    bytes.push_back(static_cast<uint8_t>(word >> 16));
    bytes.push_back(static_cast<uint8_t>(word >> 24));
}

void write_word(PSXRecomp::PS1Executable& exe, uint32_t address,
                uint32_t word) {
    const size_t offset = static_cast<size_t>(address - exe.header.load_address);
    exe.code_data[offset + 0] = static_cast<uint8_t>(word);
    exe.code_data[offset + 1] = static_cast<uint8_t>(word >> 8);
    exe.code_data[offset + 2] = static_cast<uint8_t>(word >> 16);
    exe.code_data[offset + 3] = static_cast<uint8_t>(word >> 24);
}

std::string generate_first_instruction(uint32_t first_word,
                                       std::vector<RecompilerPatch> patches,
                                       bool overlay_mode,
                                       PSXRecomp::CodeGenConfig config = {}) {
    constexpr uint32_t base = 0x80010000u;
    PSXRecomp::PS1Executable exe{};
    exe.header.load_address = base;
    exe.header.initial_pc = base;
    exe.header.file_size = 20;
    append_word(exe.code_data, first_word);
    append_word(exe.code_data, 0x00000000u); // branch/nonbranch delay-slot nop
    append_word(exe.code_data, 0x24030001u); // addiu v1, zero, 1
    append_word(exe.code_data, 0x03E00008u); // jr ra
    append_word(exe.code_data, 0x00000000u); // return delay-slot nop

    PSXRecompV4::apply_recompiler_patches_to_executable(
        exe, patches, overlay_mode);

    PSXRecomp::Function function{};
    function.start_addr = base;
    function.end_addr = base + 20;
    function.size = 20;
    function.name = "patch_test";

    PSXRecomp::ControlFlowAnalyzer analyzer(exe);
    const auto cfg = analyzer.analyze_function(function);
    config.overlay_mode = overlay_mode;
    PSXRecomp::CodeGenerator generator(exe, config);
    return generator.generate_function(function, cfg).full_code;
}

std::string base_config() {
    return R"toml([game]
name = "Patch Test"
id = "TEST-00000"
exe = "TEST.EXE"
load_address = "0x80010000"
entry_pc = "0x80010000"
text_size = "0x1000"
stack_base = "0x801FFFF0"

[recompiler]
seeds = "seeds.txt"
out_dir = "generated"
)toml";
}

fs::path write_config(const fs::path& root, const std::string& suffix,
                      const std::string& patch_tables) {
    const fs::path path = root / ("game-" + suffix + ".toml");
    std::ofstream file(path, std::ios::binary);
    file << base_config() << patch_tables;
    file.close();
    return path;
}

void parser_tests(const fs::path& root) {
    const auto valid = write_config(root, "valid", R"toml(
[[recompiler.patch]]
id = "gameplay-rate"
address = "0x80012340"
expected = "0x24020002"
replacement = "0x24020001"
note = "Test-only fixture"
)toml");
    const auto config = PSXRecompV4::load_game_config(valid);
    check(config.recompiler_patches.size() == 1,
          "parser accepts one guarded patch");
    check(config.recompiler_patches[0].id == "gameplay-rate" &&
          config.recompiler_patches[0].address == 0x80012340u &&
          config.recompiler_patches[0].expected == 0x24020002u &&
          config.recompiler_patches[0].replacement == 0x24020001u,
          "parser preserves patch fields");

    const auto audio_buffer = write_config(root, "audio-buffer", R"toml(
[runtime]

[audio]
buffer_ms = 60
)toml");
    const auto audio_config = PSXRecompV4::load_game_config(audio_buffer);
    check(audio_config.runtime.audio_buffer_ms == 60,
          "parser preserves per-game audio buffer target");

    const auto bad_audio_buffer = write_config(root, "bad-audio-buffer", R"toml(
[runtime]

[audio]
buffer_ms = 20
)toml");
    check_throws(
        [&] { (void)PSXRecompV4::load_game_config(bad_audio_buffer); },
        "[audio] buffer_ms out of range (30..500)",
        "parser rejects unsafe audio buffer target");

    const auto duplicate_address = write_config(root, "duplicate-address", R"toml(
[[recompiler.patch]]
id = "first"
address = "0x80012340"
expected = "0x24020002"
replacement = "0x24020001"

[[recompiler.patch]]
id = "alias"
address = "0xA0012340"
expected = "0x24020002"
replacement = "0x24020001"
)toml");
    check_throws(
        [&] { (void)PSXRecompV4::load_game_config(duplicate_address); },
        "physical address", "parser rejects duplicate aliased addresses");

    const auto duplicate_id = write_config(root, "duplicate-id", R"toml(
[[recompiler.patch]]
id = "same-id"
address = "0x80012340"
expected = "0x24020002"
replacement = "0x24020001"

[[recompiler.patch]]
id = "same-id"
address = "0x80012344"
expected = "0x24030002"
replacement = "0x24030001"
)toml");
    check_throws([&] { (void)PSXRecompV4::load_game_config(duplicate_id); },
                 "duplicate [[recompiler.patch]] id",
                 "parser rejects duplicate IDs");

    const auto unaligned = write_config(root, "unaligned", R"toml(
[[recompiler.patch]]
id = "unaligned"
address = "0x80012342"
expected = "0x24020002"
replacement = "0x24020001"
)toml");
    check_throws([&] { (void)PSXRecompV4::load_game_config(unaligned); },
                 "instruction-aligned", "parser rejects unaligned sites");

    const auto negsub = write_config(root, "negsub", R"toml(
[widescreen.cull]
negsub_sites = ["0x80012340"]
)toml");
    const auto negsub_config = PSXRecompV4::load_game_config(negsub);
    check(negsub_config.ws_cull_negsub_sites ==
              std::vector<uint32_t>{0x80012340u},
          "parser preserves negsub cull sites");

    const auto manual_window = write_config(root, "manual-window", R"toml(
[widescreen.cull]
lower_branch_sites = ["0x80012340"]
lower_value_sites = ["0x80012344"]
upper_value_sites = ["0x80012348"]
span_value_sites = ["0x8001234C"]
)toml");
    const auto manual_window_config =
        PSXRecompV4::load_game_config(manual_window);
    check(manual_window_config.ws_cull_lower_branch_sites ==
              std::vector<uint32_t>{0x80012340u} &&
              manual_window_config.ws_cull_lower_value_sites ==
              std::vector<uint32_t>{0x80012344u} &&
              manual_window_config.ws_cull_upper_value_sites ==
              std::vector<uint32_t>{0x80012348u} &&
              manual_window_config.ws_cull_span_value_sites ==
              std::vector<uint32_t>{0x8001234Cu},
          "parser preserves manual screen-window site families");
    auto changed_manual_window_config = manual_window_config;
    changed_manual_window_config.ws_cull_span_value_sites.push_back(
        0x80012350u);
    check(PSXRecompV4::overlay_codegen_config_hash(manual_window_config) !=
              PSXRecompV4::overlay_codegen_config_hash(
                  changed_manual_window_config),
          "manual screen-window sites change overlay cache identity");

    const auto vxrange = write_config(root, "vxrange", R"toml(
[widescreen.cull]
vxrange_sites = ["0x80012340"]
)toml");
    const auto vxrange_config = PSXRecompV4::load_game_config(vxrange);
    check(vxrange_config.ws_cull_vxrange_sites ==
              std::vector<uint32_t>{0x80012340u},
          "parser preserves masked-u16 X-window sites");

    const auto depth = write_config(root, "depth", R"toml(
[widescreen.cull]
depth_sites = ["0x80012340"]
)toml");
    const auto depth_config = PSXRecompV4::load_game_config(depth);
    check(depth_config.ws_cull_depth_sites ==
              std::vector<uint32_t>{0x80012340u},
          "parser preserves depth-bound sites");

    const auto plane_nx = write_config(root, "plane-nx", R"toml(
[widescreen.cull]
plane_nx_sites = ["0x80012340"]
)toml");
    const auto plane_nx_config = PSXRecompV4::load_game_config(plane_nx);
    check(plane_nx_config.ws_cull_plane_nx_sites ==
              std::vector<uint32_t>{0x80012340u},
          "parser preserves side-plane nx load sites");

    const auto xclip_load = write_config(root, "xclip-load", R"toml(
[widescreen.cull]
xclip_load_sites = ["0x80012340"]
)toml");
    const auto xclip_load_config = PSXRecompV4::load_game_config(xclip_load);
    check(xclip_load_config.ws_cull_xclip_load_sites ==
              std::vector<uint32_t>{0x80012340u},
          "parser preserves per-prim bound-load sites");

    const auto keep = write_config(root, "cull-keep", R"toml(
[[widescreen.cull.keep]]
address = "0x8002B310"
expected = "0x28A21C01"
result = 1

[[widescreen.cull.keep]]
address = "0x8002B368"
expected = "0x0082202A"
result = 0
)toml");
    const auto keep_config = PSXRecompV4::load_game_config(keep);
    check(keep_config.ws_cull_keep_sites.size() == 2 &&
              keep_config.ws_cull_keep_sites[0].address == 0x8002B310u &&
              keep_config.ws_cull_keep_sites[0].expected == 0x28A21C01u &&
              keep_config.ws_cull_keep_sites[0].result == 1u &&
              keep_config.ws_cull_keep_sites[1].address == 0x8002B368u &&
              keep_config.ws_cull_keep_sites[1].expected == 0x0082202Au &&
              keep_config.ws_cull_keep_sites[1].result == 0u,
          "parser preserves full-word-guarded maximal-participation sites");

    const auto bad_keep = write_config(root, "cull-keep-bad", R"toml(
[[widescreen.cull.keep]]
address = "0x8002B310"
expected = "0x24820001"
result = 1
)toml");
    check_throws([&] { (void)PSXRecompV4::load_game_config(bad_keep); },
                 "expected must be SLT/SLTU/SLTI/SLTIU",
                 "parser rejects non-comparison maximal-participation sites");

    const auto range = write_config(root, "range-cull", R"toml(
[widescreen.cull]
range_sites = ["0x80012340"]
activation_guard_pixels = 256
)toml");
    const auto range_config = PSXRecompV4::load_game_config(range);
    check(range_config.ws_cull_range_sites ==
              std::vector<uint32_t>{0x80012340u} &&
              range_config.ws_cull_activation_guard_pixels == 256,
          "parser preserves explicit range sites and activation guard");

    const auto bad_activation_guard =
        write_config(root, "range-cull-bad-activation-guard", R"toml(
[widescreen.cull]
activation_guard_pixels = 257
)toml");
    check_throws(
        [&] {
            (void)PSXRecompV4::load_game_config(bad_activation_guard);
        },
        "activation_guard_pixels must be in [0, 256]",
        "parser rejects an oversized activation-only guard");

    const auto angle = write_config(root, "angle-cull", R"toml(
[[widescreen.cull.angle]]
address = "0x8013F138"
expected = "0x24020155"
)toml");
    const auto angle_config = PSXRecompV4::load_game_config(angle);
    check(angle_config.ws_cull_angle_sites.size() == 1 &&
              angle_config.ws_cull_angle_sites[0].address ==
                  0x8013F138u &&
              angle_config.ws_cull_angle_sites[0].expected ==
                  0x24020155u,
          "parser preserves exact terrain-frustum angle sites");
    auto changed_angle_config = angle_config;
    changed_angle_config.ws_cull_angle_sites[0].expected =
        0x24020156u;
    check(PSXRecompV4::overlay_codegen_config_hash(angle_config) !=
              PSXRecompV4::overlay_codegen_config_hash(
                  changed_angle_config),
          "terrain angle sites change overlay cache identity");

    const auto bad_angle = write_config(root, "angle-cull-bad", R"toml(
[[widescreen.cull.angle]]
address = "0x8013F138"
expected = "0x24420155"
)toml");
    check_throws([&] { (void)PSXRecompV4::load_game_config(bad_angle); },
                 "expected must be ADDI/ADDIU rt,zero,imm",
                 "parser rejects non-constant terrain-angle instructions");

    const auto yaw_cone = write_config(root, "yaw-cone", R"toml(
[[widescreen.cull.yaw_cone]]
address = "0x80040FE8"
expected = "0x24710020"
full_turn = 256

[[widescreen.cull.yaw_cone]]
address = "0x80040FF0"
expected = "0x2472FFE0"
full_turn = 256

[[widescreen.cull.horizontal_slt]]
address = "0x8003B8EC"
expected = "0x0067102A"
)toml");
    const auto yaw_cone_config = PSXRecompV4::load_game_config(yaw_cone);
    check(yaw_cone_config.ws_cull_yaw_cone_sites.size() == 2 &&
              yaw_cone_config.ws_cull_yaw_cone_sites[0].address ==
                  0x80040FE8u &&
              yaw_cone_config.ws_cull_yaw_cone_sites[0].expected ==
                  0x24710020u &&
              yaw_cone_config.ws_cull_yaw_cone_sites[0].full_turn == 256u &&
              yaw_cone_config.ws_cull_yaw_cone_sites[1].expected ==
                  0x2472FFE0u,
          "parser preserves signed wrapping-yaw cone sites");
    check(yaw_cone_config.ws_cull_horizontal_slt_sites.size() == 1 &&
              yaw_cone_config.ws_cull_horizontal_slt_sites[0].address ==
                  0x8003B8ECu &&
              yaw_cone_config.ws_cull_horizontal_slt_sites[0].expected ==
                  0x0067102Au,
          "parser preserves signed horizontal side-plane sites");
    auto changed_yaw_cone_config = yaw_cone_config;
    changed_yaw_cone_config.ws_cull_yaw_cone_sites[0].full_turn = 512u;
    check(PSXRecompV4::overlay_codegen_config_hash(yaw_cone_config) !=
              PSXRecompV4::overlay_codegen_config_hash(
                  changed_yaw_cone_config),
          "yaw-cone metadata changes overlay cache identity");

    const auto bad_yaw_cone = write_config(root, "yaw-cone-bad", R"toml(
[[widescreen.cull.yaw_cone]]
address = "0x80040FE8"
expected = "0x24110020"
full_turn = 256
)toml");
    check_throws([&] { (void)PSXRecompV4::load_game_config(bad_yaw_cone); },
                 "rs != zero",
                 "parser rejects yaw-cone constants without a live base yaw");

    const auto bad_horizontal_slt = write_config(
        root, "horizontal-slt-bad", R"toml(
[[widescreen.cull.horizontal_slt]]
address = "0x8003B8EC"
expected = "0x0067102B"
)toml");
    check_throws(
        [&] { (void)PSXRecompV4::load_game_config(bad_horizontal_slt); },
        "signed register-register SLT",
        "parser rejects unsigned horizontal side-plane compares");

    const auto aspect_cone = write_config(root, "aspect-cone", R"toml(
[widescreen.cull.aspect_cone]
forward_addr = "0x1F8000E8"
object_type_offset = 12
object_reg = 19
x_reg = 16
z_reg = 17
y_reg = 18
hysteresis_pixels = 24
queue_reserve = 4
queue_count_addrs = ["0x1F800144", "0x1F800150", "0x1F80015C"]
queue_capacities = [24, 40, 28]
queue_type_masks = ["0x00000204", "0x00000010", "0x00000020"]

[[widescreen.cull.aspect_cone.sites]]
address = "0x80010000"
expected = "0x28620370"

[[widescreen.cull.aspect_cone.sites]]
address = "0x80010004"
expected = "0x0082202A"
cosine_threshold = 856
object_reg = 20
x_reg = 19
z_reg = 18
y_reg = 17
queue_guard = false
)toml");
    const auto aspect_config =
        PSXRecompV4::load_game_config(aspect_cone);
    check(aspect_config.ws_aspect_cone.sites.size() == 2 &&
              aspect_config.ws_aspect_cone.sites[0].address ==
                  0x80010000u &&
              aspect_config.ws_aspect_cone.sites[0].expected ==
                  0x28620370u &&
              aspect_config.ws_aspect_cone.sites[0].cosine_threshold ==
                  0x370u &&
              aspect_config.ws_aspect_cone.sites[1].expected ==
                  0x0082202Au &&
              aspect_config.ws_aspect_cone.sites[1].cosine_threshold ==
                  856u &&
              aspect_config.ws_aspect_cone.sites[1].object_reg == 20u &&
              aspect_config.ws_aspect_cone.sites[1].x_reg == 19u &&
              aspect_config.ws_aspect_cone.sites[1].z_reg == 18u &&
              aspect_config.ws_aspect_cone.sites[1].y_reg == 17u &&
              !aspect_config.ws_aspect_cone.sites[1].queue_guard &&
              aspect_config.ws_aspect_cone.forward_addr ==
                  0x1F8000E8u &&
              aspect_config.ws_aspect_cone.object_reg == 19u &&
              aspect_config.ws_aspect_cone.x_reg == 16u &&
              aspect_config.ws_aspect_cone.z_reg == 17u &&
              aspect_config.ws_aspect_cone.y_reg == 18u &&
              aspect_config.ws_aspect_cone.hysteresis_pixels == 24u &&
              aspect_config.ws_aspect_cone.queue_reserve == 4u &&
              aspect_config.ws_aspect_cone.queue_capacities ==
                  std::array<uint32_t, 3>{24u, 40u, 28u},
          "parser preserves aspect-aware participation policy");

    const auto aspect_hash = PSXRecompV4::overlay_codegen_config_hash(
        aspect_config);
    auto changed_aspect_config = aspect_config;
    changed_aspect_config.ws_aspect_cone.hysteresis_pixels++;
    check(aspect_hash != PSXRecompV4::overlay_codegen_config_hash(
                             changed_aspect_config),
          "aspect participation policy changes overlay cache identity");
    changed_aspect_config = aspect_config;
    changed_aspect_config.ws_cull_guard_pixels++;
    check(aspect_hash != PSXRecompV4::overlay_codegen_config_hash(
                             changed_aspect_config),
          "widescreen guard changes overlay cache identity");
    changed_aspect_config = aspect_config;
    changed_aspect_config.ws_cull_activation_guard_pixels++;
    check(aspect_hash != PSXRecompV4::overlay_codegen_config_hash(
                             changed_aspect_config),
          "activation-only guard changes overlay cache identity");
    changed_aspect_config = aspect_config;
    changed_aspect_config.ws_aspect_cone.sites[1].queue_guard = true;
    check(aspect_hash != PSXRecompV4::overlay_codegen_config_hash(
                             changed_aspect_config),
          "per-site aspect participation policy changes cache identity");

    const auto bad_aspect_opcode =
        write_config(root, "aspect-cone-bad-opcode", R"toml(
[widescreen.cull.aspect_cone]
forward_addr = "0x1F8000E8"
object_type_offset = 12
object_reg = 19
x_reg = 16
z_reg = 17
y_reg = 18
queue_count_addrs = ["0x1F800144", "0x1F800150", "0x1F80015C"]
queue_capacities = [24, 40, 28]
queue_type_masks = ["0x00000204", "0x00000010", "0x00000020"]
[[widescreen.cull.aspect_cone.sites]]
address = "0x80010000"
expected = "0x2C620370"
)toml");
    check_throws(
        [&] {
            (void)PSXRecompV4::load_game_config(bad_aspect_opcode);
        },
        "expected must be signed SLTI or SLT",
        "parser rejects unsigned aspect-cone guards");

    const auto bad_aspect_slt_threshold =
        write_config(root, "aspect-cone-bad-slt-threshold", R"toml(
[widescreen.cull.aspect_cone]
forward_addr = "0x1F8000E8"
object_type_offset = 12
object_reg = 19
x_reg = 16
z_reg = 17
y_reg = 18
queue_count_addrs = ["0x1F800144", "0x1F800150", "0x1F80015C"]
queue_capacities = [24, 40, 28]
queue_type_masks = ["0x00000204", "0x00000010", "0x00000020"]
[[widescreen.cull.aspect_cone.sites]]
address = "0x80010000"
expected = "0x0082202A"
)toml");
    check_throws(
        [&] {
            (void)PSXRecompV4::load_game_config(
                bad_aspect_slt_threshold);
        },
        "cosine_threshold must be in [1, 1023]",
        "parser requires an explicit Q10 threshold for SLT cones");

    const auto bad_aspect_arrays =
        write_config(root, "aspect-cone-bad-arrays", R"toml(
[widescreen.cull.aspect_cone]
forward_addr = "0x1F8000E8"
object_type_offset = 12
object_reg = 19
x_reg = 16
z_reg = 17
y_reg = 18
queue_count_addrs = ["0x1F800144", "0x1F800150"]
queue_capacities = [24, 40, 28]
queue_type_masks = ["0x00000204", "0x00000010", "0x00000020"]
[[widescreen.cull.aspect_cone.sites]]
address = "0x80010000"
expected = "0x28620370"
)toml");
    check_throws(
        [&] {
            (void)PSXRecompV4::load_game_config(bad_aspect_arrays);
        },
        "must contain exactly three values",
        "parser rejects incomplete aspect-cone queue metadata");
}

void capture_history_config_tests(const fs::path& root) {
    const auto valid = write_config(root, "capture-history", R"toml(
[runtime]
overlay_cache = true
overlay_capture_history = true
overlay_capture_persist_dir = ".aot_capture_history/TEST-00000"
)toml");
    const auto cfg = PSXRecompV4::load_game_config(valid);
    check(cfg.runtime.overlay_capture_history,
          "parser enables durable overlay capture history");
    check(cfg.runtime.overlay_capture_persist_dir ==
              ".aot_capture_history/TEST-00000",
          "parser preserves project-relative capture history directory");

    const auto escaping = write_config(root, "capture-history-escape", R"toml(
[runtime]
overlay_capture_persist_dir = "../outside"
)toml");
    check_throws([&] { (void)PSXRecompV4::load_game_config(escaping); },
                 "must stay inside the project",
                 "parser rejects escaping capture history directory");

    const auto absolute = write_config(root, "capture-history-absolute", R"toml(
[runtime]
overlay_capture_persist_dir = "C:/outside"
)toml");
    check_throws([&] { (void)PSXRecompV4::load_game_config(absolute); },
                 "must be project-relative",
                 "parser rejects absolute capture history directory");

    // A game.toml travels between hosts, so each of these must be rejected on
    // every host, not only on the one whose path grammar it is written in.
    // TOML literal strings ('...') are used because a backslash is an escape
    // inside a TOML basic string. Cases marked POSIX-visible are the ones a
    // host-native std::filesystem check silently accepted under POSIX.
    struct RejectedDir {
        const char* value;   // as written in game.toml
        const char* needle;  // expected message fragment
        const char* name;
    };
    static constexpr RejectedDir rejected[] = {
        {"'/outside'", "must be project-relative",
         "parser rejects rooted capture history directory"},
        {R"('\outside')", "must be project-relative",
         "parser rejects backslash-rooted capture history directory"},
        {R"('C:\outside')", "must be project-relative",
         "parser rejects backslash drive-qualified capture history directory"},
        {"'C:outside'", "must be project-relative",
         "parser rejects drive-relative capture history directory"},
        {R"('\\server\share\outside')", "must be project-relative",
         "parser rejects UNC capture history directory"},
        {R"('..\outside')", "must stay inside the project",
         "parser rejects backslash-escaping capture history directory"},
        {R"('nested\..\..\outside')", "must stay inside the project",
         "parser rejects nested backslash-escaping capture history directory"},
        {"'nested/..'", "must stay inside the project",
         "parser rejects a trailing .. component"},
        // A basic string, not a literal one: TOML forbids a raw NUL byte but
        // allows the \u0000 escape that produces it. The runtime passes this
        // value on as a C string, so a NUL would truncate it after validation.
        {R"("..\u0000keep")", "must not contain a NUL byte",
         "parser rejects an embedded NUL escape"},
    };
    for (size_t i = 0; i < std::size(rejected); ++i) {
        const auto& c = rejected[i];
        const auto cfg_path = write_config(
            root, fmt::format("capture-history-rejected-{}", i),
            fmt::format("[runtime]\noverlay_capture_persist_dir = {}\n",
                        c.value));
        check_throws([&] { (void)PSXRecompV4::load_game_config(cfg_path); },
                     c.needle, c.name);
    }

    // A dotfile is not a `..` component; the guard must not over-reject.
    const auto dotted = write_config(root, "capture-history-dotted", R"toml(
[runtime]
overlay_capture_history = true
overlay_capture_persist_dir = ".aot_capture_history/..keep/TEST-00000"
)toml");
    const auto dotted_cfg = PSXRecompV4::load_game_config(dotted);
    check(dotted_cfg.runtime.overlay_capture_persist_dir ==
              ".aot_capture_history/..keep/TEST-00000",
          "parser accepts a capture history component that merely starts with dots");
}

void codegen_tests() {
    constexpr uint32_t original = 0x24020002u;    // addiu v0, zero, 2
    constexpr uint32_t replacement = 0x24020001u; // addiu v0, zero, 1
    const RecompilerPatch physical_alias{
        "gameplay-rate", 0x00010000u, original, replacement, ""};

    const std::string applied =
        generate_first_instruction(original, {physical_alias}, false);
    check(applied.find("0x24020001") != std::string::npos &&
          applied.find("cpu->gpr[2] = 1;") != std::string::npos,
          "codegen applies exact patch through physical alias");

    check_throws(
        [&] { (void)generate_first_instruction(0x24020003u,
                                               {physical_alias}, false); },
        "wrong game revision or stale patch",
        "main codegen fails on stale opcode guard");

    const std::string overlay =
        generate_first_instruction(0x24020003u, {physical_alias}, true);
    check(overlay.find("0x24020003") != std::string::npos &&
          overlay.find("0x24020001") == std::string::npos,
          "overlay nonmatching variant remains unchanged");

    constexpr uint32_t beq_v0_zero = 0x10400002u;
    constexpr uint32_t bne_v0_zero = 0x14400002u;
    const RecompilerPatch branch_patch{
        "feature-branch", 0x80010000u, beq_v0_zero, bne_v0_zero, ""};
    const std::string branch =
        generate_first_instruction(beq_v0_zero, {branch_patch}, false);
    check(branch.find("cpu->gpr[2] != cpu->gpr[0]") != std::string::npos,
          "patch is applied before control-flow analysis");

    std::vector<RecompilerPatch> merged{physical_alias};
    PSXRecompV4::merge_recompiler_patches(merged, {physical_alias});
    check(merged.size() == 1,
          "config merge deduplicates an identical patch");

    const RecompilerPatch conflicting_id{
        "gameplay-rate", 0x00010004u, original, replacement, ""};
    check_throws(
        [&] {
            PSXRecompV4::merge_recompiler_patches(merged, {conflicting_id});
        },
        "conflicting recompiler patches",
        "config merge rejects cross-config ID conflicts");

    PSXRecomp::CodeGenConfig negsub_config;
    negsub_config.ws_cull_negsub_sites.insert(0x80010000u);
    const std::string negsub = generate_first_instruction(
        0x00041023u, {}, false, negsub_config); // subu v0,zero,a0
    check(negsub.find("cpu->gpr[2] = 0u - cpu->gpr[4] - "
                      "(uint32_t)psx_ws_x_margin()") != std::string::npos,
          "codegen emits configured negsub horizontal low-edge widen");

    const std::string overlay_mismatch = generate_first_instruction(
        0x00041021u, {}, true, negsub_config); // addu v0,zero,a0
    check(overlay_mismatch.find("ws cull negsub") == std::string::npos,
          "overlay nonmatching negsub variant remains unchanged");

    PSXRecomp::CodeGenConfig cpu_projection_config;
    cpu_projection_config.ws_backdrop_x_sites.insert(0x80010000u);
    const std::string cpu_projection = generate_first_instruction(
        0x248200A0u, {}, false, cpu_projection_config); // addiu v0,a0,160
    check(cpu_projection.find(
              "psx_ws_project_x((int32_t)(cpu->gpr[4] + (uint32_t)160))") !=
              std::string::npos,
          "codegen squashes exact CPU-projected screen-X sites");

    const std::string cpu_projection_overlay_mismatch =
        generate_first_instruction(
            0x288200A0u, {}, true, cpu_projection_config); // slti at same VA
    check(cpu_projection_overlay_mismatch.find("psx_ws_project_x") ==
              std::string::npos,
          "overlay nonmatching CPU-projection variant remains unchanged");

    PSXRecomp::CodeGenConfig lower_branch_config;
    lower_branch_config.ws_cull_lower_branch_sites.insert(0x80010000u);
    const std::string lower_bgez = generate_first_instruction(
        0x04810002u, {}, false, lower_branch_config); // bgez a0,+2
    check(lower_bgez.find(
              "(int32_t)cpu->gpr[4] >= -psx_ws_x_margin()") !=
              std::string::npos,
          "codegen widens a manual bgez lower screen edge");
    const std::string lower_blez = generate_first_instruction(
        0x18800002u, {}, false, lower_branch_config); // blez a0,+2
    check(lower_blez.find(
              "(int32_t)cpu->gpr[4] <= -psx_ws_x_margin()") !=
              std::string::npos,
          "codegen preserves manual lower-edge branch sense");

    PSXRecomp::CodeGenConfig lower_value_config;
    lower_value_config.ws_cull_lower_value_sites.insert(0x80010000u);
    const std::string lower_zero = generate_first_instruction(
        0x00001021u, {}, false, lower_value_config); // addu v0,zero,zero
    check(lower_zero.find("psx_ws_cull_lower_value(0)") !=
              std::string::npos,
          "codegen widens a register lower clamp");
    const std::string lower_store = generate_first_instruction(
        0xAFA0002Cu, {}, false, lower_value_config); // sw zero,44(sp)
    check(lower_store.find("cpu->write_word(cpu->gpr[29] + 44, "
                           "(uint32_t)psx_ws_cull_lower_value(0))") !=
              std::string::npos,
          "codegen widens a stored lower clamp");

    PSXRecomp::CodeGenConfig upper_value_config;
    upper_value_config.ws_cull_upper_value_sites.insert(0x80010000u);
    const std::string upper_value = generate_first_instruction(
        0x34020140u, {}, false, upper_value_config); // ori v0,zero,320
    check(upper_value.find("psx_ws_cull_upper_value(320)") !=
              std::string::npos,
          "codegen widens a manual upper bound constant");

    PSXRecomp::CodeGenConfig span_value_config;
    span_value_config.ws_cull_span_value_sites.insert(0x80010000u);
    const std::string span_value = generate_first_instruction(
        0x34020140u, {}, false, span_value_config); // packed [0,320]
    check(span_value.find("psx_ws_cull_span_value(0x00000140u)") !=
              std::string::npos,
          "codegen widens both halves of a packed screen span");

    PSXRecomp::CodeGenConfig vxrange_config;
    vxrange_config.ws_cull_vxrange_sites.insert(0x80010000u);
    const std::string vxrange = generate_first_instruction(
        0x2C820140u, {}, false, vxrange_config); // sltiu v0,a0,0x140
    check(vxrange.find("cpu->gpr[2] = psx_ws_cull_vxrange(cpu->gpr[4], 320)") !=
              std::string::npos,
          "codegen routes masked-u16 X-window sites through shared helper");

    const std::string vxrange_overlay_mismatch = generate_first_instruction(
        0x24820140u, {}, true, vxrange_config); // addiu v0,a0,0x140
    check(vxrange_overlay_mismatch.find("ws cull masked-u16") == std::string::npos,
          "overlay nonmatching masked-u16 variant remains unchanged");

    PSXRecomp::CodeGenConfig depth_config;
    depth_config.ws_cull_depth_sites.insert(0x80010000u);
    const std::string signed_depth = generate_first_instruction(
        0x28827FFFu, {}, false, depth_config); // slti v0,a0,0x7fff
    check(signed_depth.find("psx_ws_depth_bound(32767)") != std::string::npos,
          "codegen emits signed aspect-scaled depth bound");

    const std::string unsigned_depth = generate_first_instruction(
        0x2C82FFFFu, {}, false, depth_config); // sltiu v0,a0,-1
    check(unsigned_depth.find("psx_ws_depth_bound(-1)") != std::string::npos,
          "unsigned depth emit preserves MIPS immediate sign extension");

    const std::string depth_overlay_mismatch = generate_first_instruction(
        0x24827FFFu, {}, true, depth_config); // addiu v0,a0,0x7fff
    check(depth_overlay_mismatch.find("ws cull depth") == std::string::npos,
          "overlay nonmatching depth variant remains unchanged");

    PSXRecomp::CodeGenConfig range_config;
    range_config.ws_cull_range_sites.insert(0x80010000u);
    const std::string range = generate_first_instruction(
        0x2C8201C1u, {}, false, range_config); // sltiu v0,a0,0x1c1
    check(range.find("2*psx_ws_x_margin()") != std::string::npos,
          "native range emit widens by both horizontal margins");

    auto activation_range_config = range_config;
    activation_range_config.ws_cull_activation_guard_pixels = 256;
    const std::string activation_range = generate_first_instruction(
        0x2C8201C1u, {}, false,
        activation_range_config); // sltiu v0,a0,0x1c1
    check(activation_range.find(
              "2*(psx_ws_x_margin() > 0 ? psx_ws_x_margin() + 256 : 0)") !=
              std::string::npos,
          "activation range gains an isolated resident-object lead");

    PSXRecomp::CodeGenConfig activation_bias_config;
    activation_bias_config.ws_cull_bias_sites.insert(0x80010000u);
    activation_bias_config.ws_cull_activation_guard_pixels = 256;
    const std::string activation_bias = generate_first_instruction(
        0x248200E6u, {}, false,
        activation_bias_config); // addiu v0,a0,230
    check(activation_bias.find(
              "(psx_ws_x_margin() > 0 ? psx_ws_x_margin() + 256 : 0)") !=
              std::string::npos,
          "activation bias gains the same isolated resident-object lead");

    PSXRecomp::CodeGenConfig plane_nx_config;
    plane_nx_config.ws_cull_plane_nx_sites.insert(0x80010000u);
    const std::string plane_nx = generate_first_instruction(
        0x8C84C828u, {}, false, plane_nx_config); // lw a0,-0x37d8(a0)
    check(plane_nx.find("psx_ws_plane_nx((int32_t)psx_cyc_load_word") !=
              std::string::npos,
          "codegen routes side-plane nx loads through shared helper");

    const std::string plane_nx_overlay_mismatch = generate_first_instruction(
        0x9484C828u, {}, true, plane_nx_config); // lhu a0,-0x37d8(a0)
    check(plane_nx_overlay_mismatch.find("ws cull plane nx") == std::string::npos,
          "overlay nonmatching plane-nx variant remains unchanged");

    PSXRecomp::CodeGenConfig xclip_config;
    xclip_config.ws_cull_xclip_load_sites.insert(0x80010000u);
    const std::string xclip = generate_first_instruction(
        0x8C6200F8u, {}, false, xclip_config); // lw v0,0xf8(v1)
    check(xclip.find("psx_ws_xclip_bound(psx_cyc_load_word") != std::string::npos,
          "codegen routes per-prim bound loads through shared helper");

    const std::string xclip_overlay_mismatch = generate_first_instruction(
        0x246200F8u, {}, true, xclip_config); // addiu v0,v1,0xf8
    check(xclip_overlay_mismatch.find("ws cull xclip") == std::string::npos,
          "overlay nonmatching xclip variant remains unchanged");

    PSXRecomp::CodeGenConfig keep_config;
    keep_config.ws_cull_keep_sites.push_back(
        {0x80010000u, 0x28821C01u, 1u}); // slti v0,a0,0x1c01
    const std::string keep_true = generate_first_instruction(
        0x28821C01u, {}, false, keep_config);
    check(keep_true.find("psx_ws_cull_keep_result") != std::string::npos &&
              keep_true.find(", 1u)") != std::string::npos,
          "codegen emits guarded widescreen keep-true comparison");

    keep_config.ws_cull_keep_sites[0] =
        {0x80010000u, 0x0082202Au, 0u}; // slt a0,a0,v0
    const std::string keep_false = generate_first_instruction(
        0x0082202Au, {}, false, keep_config);
    check(keep_false.find("psx_ws_cull_keep_result") != std::string::npos &&
              keep_false.find(", 0u)") != std::string::npos,
          "codegen emits guarded widescreen keep-false comparison");

    const std::string keep_overlay_mismatch = generate_first_instruction(
        0x0082202Bu, {}, true, keep_config); // sltu at same overlay VA
    check(keep_overlay_mismatch.find("maximal object/model participation") ==
              std::string::npos,
          "overlay full-word mismatch leaves keep site unchanged");

    PSXRecomp::CodeGenConfig angle_config;
    angle_config.ws_cull_angle_sites.push_back(
        {0x80010000u, 0x24020155u});
    const std::string angle = generate_first_instruction(
        0x24020155u, {}, false, angle_config);
    check(angle.find("psx_ws_angle_widen(341u)") !=
              std::string::npos,
          "codegen emits exact terrain-frustum angle helper");
    auto activation_isolated_angle_config = angle_config;
    activation_isolated_angle_config.ws_cull_activation_guard_pixels = 256;
    const std::string activation_isolated_angle = generate_first_instruction(
        0x24020155u, {}, false, activation_isolated_angle_config);
    check(activation_isolated_angle == angle,
          "activation guard leaves terrain-frustum emission byte-identical");
    const std::string angle_overlay_mismatch =
        generate_first_instruction(0x240201C7u, {}, true, angle_config);
    check(angle_overlay_mismatch.find("psx_ws_angle_widen") ==
              std::string::npos,
          "overlay full-word mismatch leaves terrain angle unchanged");

    PSXRecomp::CodeGenConfig yaw_config;
    yaw_config.ws_cull_yaw_cone_sites.push_back(
        {0x80010000u, 0x24710020u, 256u});
    const std::string yaw = generate_first_instruction(
        0x24710020u, {}, false, yaw_config);
    check(yaw.find("cpu->gpr[17] = cpu->gpr[3] + (uint32_t)"
                   "psx_ws_yaw_cone_offset(32, 256u)") !=
              std::string::npos,
          "codegen emits aspect-scaled wrapping-yaw offset");
    const std::string yaw_overlay_mismatch = generate_first_instruction(
        0x2471001Fu, {}, true, yaw_config);
    check(yaw_overlay_mismatch.find("psx_ws_yaw_cone_offset") ==
              std::string::npos,
          "overlay full-word mismatch leaves yaw cone unchanged");

    PSXRecomp::CodeGenConfig horizontal_slt_config;
    horizontal_slt_config.ws_cull_horizontal_slt_sites.push_back(
        {0x80010000u, 0x0067102Au});
    const std::string horizontal_slt = generate_first_instruction(
        0x0067102Au, {}, false, horizontal_slt_config);
    check(horizontal_slt.find(
              "cpu->gpr[2] = psx_ws_horizontal_slt(cpu->gpr[3], "
              "cpu->gpr[7])") != std::string::npos,
          "codegen emits signed camera-horizontal side-plane helper");
    const std::string horizontal_slt_overlay_mismatch =
        generate_first_instruction(
            0x0067102Bu, {}, true, horizontal_slt_config);
    check(horizontal_slt_overlay_mismatch.find("psx_ws_horizontal_slt") ==
              std::string::npos,
          "overlay full-word mismatch leaves horizontal side plane unchanged");

    PSXRecomp::CodeGenConfig aspect_config;
    aspect_config.ws_aspect_cone.sites.push_back(
        {0x80010000u, 0x28620370u}); // slti v0,v1,0x370 reject predicate
    aspect_config.ws_aspect_cone.object_reg = 19u;
    aspect_config.ws_aspect_cone.x_reg = 16u;
    aspect_config.ws_aspect_cone.z_reg = 17u;
    aspect_config.ws_aspect_cone.y_reg = 18u;
    const std::string aspect = generate_first_instruction(
        0x28620370u, {}, false, aspect_config);
    check(aspect.find(
              "psx_ws_aspect_cone_result(0x80010000u") !=
              std::string::npos &&
              aspect.find("cpu->gpr[19]") != std::string::npos &&
              aspect.find("(int32_t)(int16_t)cpu->gpr[16]") !=
                  std::string::npos &&
              aspect.find("(int32_t)(int16_t)cpu->gpr[17]") !=
                  std::string::npos &&
              aspect.find("(int32_t)(int16_t)cpu->gpr[18]") !=
                  std::string::npos,
          "codegen emits full-word-guarded aspect-cone helper");

    const std::string aspect_overlay_mismatch =
        generate_first_instruction(0x28620358u, {}, true, aspect_config);
    check(aspect_overlay_mismatch.find("psx_ws_aspect_cone_result") ==
              std::string::npos,
          "overlay full-word mismatch leaves aspect-cone site unchanged");

    PSXRecomp::CodeGenConfig part_config;
    PSXRecompV4::WidescreenAspectConeSite part_site;
    part_site.address = 0x80010000u;
    part_site.expected = 0x0082202Au; // slt a0,a0,v0
    part_site.cosine_threshold = 856u;
    part_site.object_reg = 20u;
    part_site.x_reg = 19u;
    part_site.z_reg = 18u;
    part_site.y_reg = 17u;
    part_site.queue_guard = false;
    part_config.ws_aspect_cone.sites.push_back(part_site);
    const std::string part = generate_first_instruction(
        0x0082202Au, {}, false, part_config);
    check(part.find("cpu->gpr[4] = psx_ws_aspect_cone_result") !=
              std::string::npos &&
              part.find("cpu->gpr[20]") != std::string::npos &&
              part.find("(int32_t)(int16_t)cpu->gpr[19]") !=
                  std::string::npos &&
              part.find("(int32_t)(int16_t)cpu->gpr[18]") !=
                  std::string::npos &&
              part.find("(int32_t)(int16_t)cpu->gpr[17]") !=
                  std::string::npos,
          "codegen supports exact per-child SLT cone metadata");
}

void gte_codegen_classification_tests() {
    bool all_ok = true;
    for (uint8_t reg = 0; reg < 32; ++reg) {
        const auto expect = [&](uint32_t word, const std::string& call,
                                bool needs_helper, const char *kind) {
            const std::string code = generate_first_instruction(word, {}, false);
            const bool has_helper = code.find(call) != std::string::npos;
            if (has_helper != needs_helper) {
                fmt::print(stderr,
                           "FAIL  full-game GTE {} reg {} helper={} expected={}\n",
                           kind, reg, has_helper, needs_helper);
                all_ok = false;
            }
        };

        const uint32_t cop2 = 0x12u << 26;
        expect(cop2 | (0x00u << 21) | (2u << 16) | (uint32_t(reg) << 11),
               fmt::format("gte_read_data(cpu, {})", reg),
               PSXRecompGTERegisters::data_read_needs_helper(reg), "MFC2");
        expect(cop2 | (0x02u << 21) | (2u << 16) | (uint32_t(reg) << 11),
               fmt::format("gte_read_ctrl(cpu, {})", reg),
               PSXRecompGTERegisters::ctrl_read_needs_helper(reg), "CFC2");
        expect(cop2 | (0x04u << 21) | (2u << 16) | (uint32_t(reg) << 11),
               fmt::format("gte_write_data(cpu, {}", reg),
               PSXRecompGTERegisters::data_write_needs_helper(reg), "MTC2");
        expect(cop2 | (0x06u << 21) | (2u << 16) | (uint32_t(reg) << 11),
               fmt::format("gte_write_ctrl(cpu, {}", reg),
               PSXRecompGTERegisters::ctrl_write_needs_helper(reg), "CTC2");
        expect((0x32u << 26) | (3u << 21) | (uint32_t(reg) << 16),
               fmt::format("gte_write_data(cpu, {}", reg),
               PSXRecompGTERegisters::data_write_needs_helper(reg), "LWC2");
        expect((0x3Au << 26) | (3u << 21) | (uint32_t(reg) << 16),
               fmt::format("gte_read_data(cpu, {})", reg),
               PSXRecompGTERegisters::data_read_needs_helper(reg), "SWC2");
    }
    check(all_ok, "full-game GTE helper classification covers all registers");
}

void jump_table_producer_codegen_test() {
    constexpr uint32_t base = 0x80010000u;
    constexpr uint32_t entry = base + 0x500u;
    PSXRecomp::PS1Executable exe{};
    exe.header.load_address = base;
    exe.header.initial_pc = entry;
    exe.header.file_size = 0x1000u;
    exe.code_data.resize(0x1000u, 0u);

    write_word(exe, base + 0x500u, 0x3C088001u); // lui t0,0x8001
    write_word(exe, base + 0x504u, 0x25100A00u); // addiu s0,t0,0x0a00
    write_word(exe, base + 0x50Cu, 0x2C620003u); // sltiu v0,v1,3
    write_word(exe, base + 0x510u, 0x1040001Bu); // beq v0,zero,+0x580
    write_word(exe, base + 0x518u, 0x00031080u); // sll v0,v1,2
    write_word(exe, base + 0x51Cu, 0x00501021u); // addu v0,v0,s0
    write_word(exe, base + 0x520u, 0x8C420000u); // lw v0,0(v0)
    write_word(exe, base + 0x528u, 0x00400008u); // jr v0
    const uint32_t cases[] = {
        base + 0x540u, base + 0x550u, base + 0x560u};
    for (size_t i = 0; i < 3u; i++) {
        write_word(exe, cases[i], 0x24020001u + static_cast<uint32_t>(i));
        write_word(exe, cases[i] + 4u, 0x03E00008u);
        write_word(exe, base + 0xA00u + static_cast<uint32_t>(i * 4u),
                   cases[i]);
    }
    write_word(exe, base + 0x580u, 0x03E00008u);
    write_word(exe, base + 0x590u,
               0x08000000u | ((entry >> 2) & 0x03FFFFFFu));

    PSXRecomp::Function function{};
    function.start_addr = entry;
    function.end_addr = base + 0x588u;
    function.size = function.end_addr - function.start_addr;
    function.name = "producer_switch";
    function.producer_lo = base + 0x400u;
    function.producer_hi = base + 0x900u; // table belongs to adjacent producer

    PSXRecomp::ControlFlowAnalyzer analyzer(exe);
    const auto bounded_cfg = analyzer.analyze_function(function);
    PSXRecomp::CodeGenerator bounded_generator(exe);
    const std::string bounded =
        bounded_generator.generate_function(function, bounded_cfg).full_code;
    check(bounded.find("/* jump table") == std::string::npos,
          "codegen cannot resurrect an adjacent-producer jump table");

    function.producer_lo = 0u;
    function.producer_hi = 0u;
    const auto single_image_cfg = analyzer.analyze_function(function);
    PSXRecomp::CodeGenerator single_image_generator(exe);
    const std::string single_image = single_image_generator
        .generate_function(function, single_image_cfg).full_code;
    check(single_image.find("/* jump table") != std::string::npos,
          "codegen still emits the same table for a single owned image");

    // Exercise the actual overlapping-alias emitter path. The alias begins at
    // a later block which jumps back through the host's complete table setup,
    // so an unbounded alias really would expose the adjacent producer bytes.
    // Producer ownership must survive Function -> alias CFG -> shared body.
    PSXRecomp::Function alias = function;
    alias.start_addr = base + 0x590u;
    alias.end_addr = base + 0x598u;
    alias.size = alias.end_addr - alias.start_addr;
    alias.name = "producer_switch_alias";
    alias.alias_walk_lo = entry;
    alias.alias_group_entries = {alias.start_addr};
    alias.producer_lo = base + 0x400u;
    alias.producer_hi = base + 0x900u;

    const auto alias_cfg = analyzer.analyze_function(alias);
    check(alias_cfg.producer_lo == alias.producer_lo &&
          alias_cfg.producer_hi == alias.producer_hi,
          "alias CFG retains its host producer bounds");
    PSXRecomp::CodeGenerator alias_generator(exe);
    const auto alias_generated = alias_generator.generate_alias_group(
        {&alias}, alias_cfg, "");
    check(!alias_generated.empty() &&
          alias_generated.front().full_code.find("/* jump table") ==
              std::string::npos,
          "alias emitter cannot expose an adjacent producer jump table");

    alias.producer_lo = 0u;
    alias.producer_hi = 0u;
    const auto unbounded_alias_cfg = analyzer.analyze_function(alias);
    PSXRecomp::CodeGenerator unbounded_alias_generator(exe);
    const auto unbounded_alias_generated =
        unbounded_alias_generator.generate_alias_group(
            {&alias}, unbounded_alias_cfg, "");
    check(!unbounded_alias_generated.empty() &&
          unbounded_alias_generated.front().full_code.find("/* jump table") !=
              std::string::npos,
          "alias regression fixture reaches the table when ownership is absent");
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() /
        fmt::format("psxrecomp-patch-test-{}", reinterpret_cast<uintptr_t>(&failures));
    fs::remove_all(root);
    fs::create_directories(root);

    try {
        parser_tests(root);
        capture_history_config_tests(root);
        codegen_tests();
        gte_codegen_classification_tests();
        jump_table_producer_codegen_test();
    } catch (const std::exception& e) {
        fmt::print(stderr, "FAIL  unexpected exception: {}\n", e.what());
        ++failures;
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    fmt::print("\nGuarded recompiler patches: {}\n",
               failures == 0 ? "all tests passed" : "failures detected");
    return failures == 0 ? 0 : 1;
}
