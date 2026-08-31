#include "config_loader.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using PSXRecompV4::UserSettings;

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

static bool has_temp_sibling(const fs::path& directory) {
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.path().filename().string().find(".tmp.") != std::string::npos)
            return true;
    }
    return false;
}

int main() {
    const auto stamp = std::chrono::steady_clock::now()
                           .time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("disruptor-user-settings-test-" + std::to_string(stamp));
    fs::create_directories(root);

    try {
        const fs::path path = root / fs::u8path(u8"settings-unicode-ü.toml");
        UserSettings missing = PSXRecompV4::load_user_settings(path);
        require(!missing.parse_error && !missing.has_mouse_aim &&
                    !missing.has_vsync,
                "missing settings must fall through without overrides");

        UserSettings written;
        written.has_vsync = true;
        written.vsync = -1;
        written.has_supersampling = true;
        written.supersampling = 3;
        written.has_frame_interpolation_fps = true;
        written.frame_interpolation_fps = 144;
        written.has_frame_interpolation_blend = true;
        written.frame_interpolation_blend = 1;
        written.has_aspect_ratio = true;
        written.aspect_num = 32;
        written.aspect_den = 9;
        written.has_adaptive_view = true;
        written.adaptive_view = false;
        written.has_mouse_aim = true;
        written.mouse_aim = true;
        written.has_modern_controls = true;
        written.modern_controls = true;
        written.has_horizontal_sensitivity = true;
        written.horizontal_sensitivity = 0.125;
        written.has_invert_horizontal = true;
        written.invert_horizontal = true;
        written.has_vertical_look = true;
        written.vertical_look = true;
        written.has_vertical_sensitivity = true;
        written.vertical_sensitivity = 0.2;
        written.has_invert_vertical = true;
        written.invert_vertical = true;
        written.has_high_precision_camera = true;
        written.high_precision_camera = true;
        written.has_geometry_correction = true;
        written.geometry_correction = true;
        written.has_perspective_textures = true;
        written.perspective_textures = true;
        written.has_language = true;
        written.language = "en";

        require(PSXRecompV4::save_user_settings(path, written),
                "initial atomic save failed");
        require(fs::is_regular_file(path), "settings file was not published");
        require(!has_temp_sibling(root), "successful save leaked a temp file");

        UserSettings loaded = PSXRecompV4::load_user_settings(path);
        require(!loaded.parse_error, "saved settings failed to parse");
        require(loaded.has_vsync && loaded.vsync == -1,
                "vsync did not round-trip");
        require(loaded.has_supersampling && loaded.supersampling == 3,
                "internal resolution scale did not round-trip");
        require(loaded.has_frame_interpolation_fps &&
                    loaded.frame_interpolation_fps == 144 &&
                    loaded.has_frame_interpolation_blend &&
                    loaded.frame_interpolation_blend == 1,
                "interpolation preferences did not round-trip");
        require(loaded.has_aspect_ratio && loaded.aspect_num == 32 &&
                    loaded.aspect_den == 9 && loaded.has_adaptive_view &&
                    !loaded.adaptive_view,
                "fixed display aspect did not round-trip");
        require(loaded.has_mouse_aim && loaded.mouse_aim &&
                    loaded.has_modern_controls && loaded.modern_controls &&
                    loaded.has_horizontal_sensitivity &&
                    loaded.horizontal_sensitivity == 0.125 &&
                    loaded.has_invert_horizontal && loaded.invert_horizontal &&
                    loaded.has_vertical_look && loaded.vertical_look &&
                    loaded.has_vertical_sensitivity &&
                    loaded.vertical_sensitivity == 0.2 &&
                    loaded.has_invert_vertical && loaded.invert_vertical &&
                    loaded.has_high_precision_camera &&
                    loaded.high_precision_camera &&
                    loaded.has_geometry_correction &&
                    loaded.geometry_correction &&
                    loaded.has_perspective_textures &&
                    loaded.perspective_textures,
                "Disruptor preferences did not round-trip");
        require(loaded.has_language && loaded.language == "en",
                "merge-save dropped an unrelated known setting");

        loaded.mouse_aim = false;
        require(PSXRecompV4::save_user_settings(path, loaded),
                "atomic replacement of an existing file failed");
        UserSettings replaced = PSXRecompV4::load_user_settings(path);
        require(replaced.has_mouse_aim && !replaced.mouse_aim &&
                    replaced.has_language && replaced.has_aspect_ratio &&
                    replaced.aspect_num == 32 && replaced.aspect_den == 9 &&
                    replaced.has_supersampling &&
                    replaced.supersampling == 3 &&
                    replaced.has_adaptive_view && !replaced.adaptive_view,
                "replacement did not preserve the merged settings");
        require(!has_temp_sibling(root), "replacement leaked a temp file");

        const fs::path invalid = root / "invalid.toml";
        {
            std::ofstream out(invalid, std::ios::trunc);
            out << "[disruptor\nmouse_aim = true\n";
        }
        require(PSXRecompV4::load_user_settings(invalid).parse_error,
                "whole-file TOML corruption was not surfaced");

        const fs::path partial = root / "partial.toml";
        {
            std::ofstream out(partial, std::ios::trunc);
            out << "[disruptor]\n"
                   "mouse_aim = false\n"
                   "horizontal_sensitivity = 99.0\n"
                   "vertical_look = true\n"
                   "vertical_sensitivity = -1.0\n"
                   "invert_vertical = true\n"
                   "geometry_correction = false\n"
                   "perspective_textures = true\n";
        }
        UserSettings tolerant = PSXRecompV4::load_user_settings(partial);
        require(!tolerant.parse_error && tolerant.has_mouse_aim &&
                    !tolerant.mouse_aim &&
                    !tolerant.has_horizontal_sensitivity &&
                    tolerant.has_vertical_look && tolerant.vertical_look &&
                    !tolerant.has_vertical_sensitivity &&
                    tolerant.has_invert_vertical && tolerant.invert_vertical &&
                    tolerant.has_geometry_correction &&
                    !tolerant.geometry_correction &&
                    tolerant.has_perspective_textures &&
                    tolerant.perspective_textures,
                "field validation must be tolerant and dependency-neutral");

        const fs::path blocked = root / "blocked.toml";
        fs::create_directory(blocked);
        {
            std::ofstream marker(blocked / "keep.txt", std::ios::trunc);
            marker << "original";
        }
        require(!PSXRecompV4::save_user_settings(blocked, written),
                "publication over a directory unexpectedly succeeded");
        require(fs::is_directory(blocked) &&
                    fs::is_regular_file(blocked / "keep.txt"),
                "failed publication damaged the original target");
        require(!has_temp_sibling(root), "failed publication leaked a temp file");

        fs::remove_all(root);
        std::cout << "user settings persistence: PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "user settings persistence: FAIL: " << e.what() << "\n";
        std::error_code ignored;
        fs::remove_all(root, ignored);
        return 1;
    }
}
