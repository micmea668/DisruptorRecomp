#include "launcher_settings.h"
#include "startup_log.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace disruptor::launcher;
namespace {
const std::string french = "C:\\Games \"old\"\\Disruptor (France).cue";
const std::string german = "D:\\\xD0\x94\xD0\xB8\xD1\x81\xD0\xBA\xD0\xB8\\Disruptor (Germany).cue";

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void write(const fs::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary);
    out << contents;
    out.close();
    require(static_cast<bool>(out), "fixture write failed");
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
bool has(const fs::path& path, std::string_view text) { return read(path).find(text) != std::string::npos; }
const Option& option(const char* key) {
    const Option* found = find_option(key);
    require(found != nullptr, "an option the test names is not in the table");
    return *found;
}
int other_value(const SettingsFile& settings, const Option& one) {
    if (one.kind == Kind::toggle) return 1 - one.initial;
    if (one.kind == Kind::choice) return (one.initial + 1) % static_cast<int>(one.choices.size());
    return one.initial == one.lowest + 7 ? one.lowest + 8 : std::min(one.lowest + 7, settings.highest(one));
}

void table() {
    std::set<std::string> keys;
    for (const auto& one : options()) {
        require(keys.insert(one.key).second, "two options have one key");
        require(std::string_view(one.key).find('.') != std::string_view::npos, "a key lacks its section");
        if (one.kind == Kind::choice)
            require(!one.choices.empty() && one.initial >= 0 && one.initial < static_cast<int>(one.choices.size()) &&
                    one.highest == static_cast<int>(one.choices.size()) - 1, "a choice starts outside its list");
        if (one.kind == Kind::toggle) require(one.initial == 0 || one.initial == 1, "a toggle starts as neither");
        if (one.needs) require(option(one.needs).kind == Kind::toggle && &option(one.needs) != &one, "a row needs no toggle");
        require(static_cast<size_t>(one.page) < page_labels().size(), "a row is on no page");
    }
    require(presets().size() == 2, "the two presets are expected");
    for (const auto& preset : presets())
        for (const auto& value : preset.values)
            require(value.value >= option(value.key).lowest && value.value <= option(value.key).highest,
                    "a preset sets a value outside its option");
}

void absent(const fs::path& folder) {
    SettingsFile settings(folder / "none" / "settings.toml");
    require(settings.load() && settings.readable(), "a missing file must read as defaults");
    for (const auto& one : options()) {
        if (std::string_view(one.key) == "video.frame_interpolation_fps") continue;
        require(settings.shown(one) == one.initial, "a missing key must show what a new installation has");
    }
    const Option& rate = option("video.frame_interpolation_fps");
    require(settings.highest(rate) == 120 && settings.shown(rate) == 120, "an unknown display must cap the rate at 120");
    settings.set_display_rate(143.9);
    require(settings.highest(rate) == 144 && settings.shown(rate) == 144, "the rate must reach the display's own");
    require(!fs::exists(folder / "none"), "reading must write nothing");
    require(settings.saves_folder() == folder / "none", "saves are beside the settings by default");
}

void one_change(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    write(file, "[video]\nrenderer = \"opengl\"\n\n[controller]\np1_device = \"keyboard\"\n\n"
                "[memcard]\ndir = \"cards\"\n\n[disruptor]\nhigh_precision_camera = true\n");
    SettingsFile settings(file);
    require(settings.load(), "the fixture must load");
    require(settings.saves_folder() == folder / "cards", "a relative saves folder is beside the settings");
    write(file, "[memcard]\ndir = \"D:/Saves\"\n");
    require(settings.load() && settings.saves_folder() == fs::path("D:/Saves"), "an absolute saves folder is itself");
    write(file, "[video]\nrenderer = \"opengl\"\n\n[controller]\np1_device = \"keyboard\"\n\n"
                "[memcard]\ndir = \"cards\"\n\n[disruptor]\nhigh_precision_camera = true\n");
    require(settings.load(), "the fixture must load again");
    require(settings.set(option("disruptor.hud_scale"), 70), "a change must be saved");
    require(has(file, "hud_scale = 70") && !has(file, "mouse_aim") && !has(file, "supersampling"),
            "one change must write one key");
    require(has(file, "renderer          = \"opengl\"") && has(file, "p1_device = \"keyboard\"") &&
            has(file, "high_precision_camera = true") && has(file, "dir     = \"cards\""),
            "keys the launcher does not show must be kept");

    write(file, read(file) + "frame_unlock = true\n");
    require(settings.set(option("audio.muted"), 1) && settings.shown(option("disruptor.frame_unlock")) == 1,
            "a change must start from the file as it is now");
    fs::remove(file);
}

void round_trip(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    for (const auto& one : options()) {
        SettingsFile settings(file);
        settings.set_display_rate(240.0);
        require(settings.load(), "the file must load");
        const int value = other_value(settings, one);
        require(value != settings.shown(one), "the test value must differ from the one shown");
        require(settings.set(one, value) && settings.shown(one) == value, "a set value must be the one shown");
        SettingsFile again(file);
        again.set_display_rate(240.0);
        require(again.load() && again.shown(one) == value, "a set value must be read back");
        fs::remove(file);
    }
}

void whole_range(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    SettingsFile settings(file);
    settings.set_display_rate(1000.0);
    require(settings.set(option("video.frame_interpolation"), 1), "in-between frames must switch on");
    for (const auto& one : options()) {
        const int highest = settings.highest(one);
        const int step = one.kind == Kind::slider ? highest - one.lowest : 1;
        for (int value = one.lowest; value <= highest; value += step) {
            SettingsFile again(file);
            again.set_display_rate(1000.0);
            const bool saved = settings.set(one, value);
            const DWORD save_error = GetLastError();
            const bool loaded = again.load();
            if (!saved || !loaded || again.shown(one) != value)
                throw std::runtime_error(std::string("settings round-trip failed for ") + one.key +
                    "=" + std::to_string(value) + ": saved=" + std::to_string(saved) +
                    ", loaded=" + std::to_string(loaded) + ", actual=" + std::to_string(again.shown(one)) +
                    ", Windows error=" + std::to_string(save_error));
        }
    }
    fs::remove(file);
}

void dependants(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    SettingsFile settings(file);
    const Option& geometry = option("disruptor.geometry_correction");
    const Option& textures = option("disruptor.perspective_textures");
    const Option& frames = option("video.frame_interpolation");
    const Option& rate = option("video.frame_interpolation_fps");
    require(settings.set(geometry, 1) && settings.shown(textures) == 1, "switching a toggle on must leave its dependants");
    require(settings.set(frames, 1) && settings.applies(textures) && settings.applies(frames) && settings.applies(rate),
            "rows apply while what they need is on");
    require(settings.set(geometry, 0), "geometry must switch off");
    require(settings.shown(textures) == 0 && settings.shown(frames) == 0 && !settings.applies(textures) &&
            !settings.applies(frames) && !settings.applies(rate), "what needs geometry must go off with it");
    require(has(file, "perspective_textures = false") && has(file, "frame_interpolation = false"),
            "a switched off dependant must be written, its default is on");
    require(settings.set(geometry, 1) && settings.shown(textures) == 0 && settings.applies(textures) &&
            !settings.applies(rate), "switching geometry back on must switch nothing else");
    write(file, "[video]\nframe_interpolation = true\n\n[disruptor]\ngeometry_correction = false\n");
    require(settings.load() && settings.shown(frames) == 1 && !settings.applies(frames) && !settings.applies(rate),
            "a row whose need does not apply itself does not apply either");
    const std::string before = read(file);
    fs::permissions(file, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write, fs::perm_options::remove);
    const bool written = settings.set(geometry, 1);
    fs::permissions(file, fs::perms::owner_write, fs::perm_options::add);
    require(!written && settings.readable() && settings.shown(geometry) == 0 && read(file) == before,
            "a save that fails must be reported and must not be shown as done");
    fs::remove(file);
}

void presets_and_reset(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    SettingsFile settings(file);
    require(settings.apply(presets()[1]), "the enhanced preset must be saved");
    for (const char* key : {"disruptor.geometry_correction", "disruptor.perspective_textures", "disruptor.improved_shadows",
                            "disruptor.frame_unlock", "video.frame_interpolation", "video.aspect_ratio"})
        require(settings.shown(option(key)) == 1, "the enhanced preset must switch its rows on");
    require(has(file, "aspect_ratio      = \"16:9\"") && has(file, "adaptive_view     = false"), "enhanced is 16:9");
    require(settings.set(option("disruptor.hud_scale"), 60) && settings.set(option("audio.master_volume"), 30),
            "changes must be saved");
    require(settings.apply(presets()[0]), "the original preset must be saved");
    for (const char* key : {"disruptor.geometry_correction", "disruptor.perspective_textures", "disruptor.improved_shadows",
                            "disruptor.frame_unlock", "video.frame_interpolation", "video.aspect_ratio"})
        require(settings.shown(option(key)) == 0, "the original preset must switch its rows off");
    require(settings.shown(option("disruptor.hud_scale")) == 100 && settings.shown(option("audio.master_volume")) == 30 &&
            has(file, "aspect_ratio      = \"4:3\""), "the original preset is 4:3 with a full HUD and leaves the volume");

    require(settings.choose_language_disc(french) && settings.reset(), "a reset must be saved");
    for (const auto& one : options())
        require(settings.shown(one) == (one.kind == Kind::slider && one.initial == 0 ? settings.highest(one) : one.initial),
                "a reset must show what a new installation has");
    require(has(file, "mouse_aim = true") && has(file, "master_volume = 100") && has(file, "frame_interpolation_fps = 0") &&
            settings.language_disc() == french, "a reset writes every row and leaves the language");
    fs::remove(file);
}

void unreadable(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    const std::string broken = "[video\nsupersampling = 2\n";
    write(file, broken);
    SettingsFile settings(file);
    require(!settings.load() && !settings.readable(), "a file that is not TOML must be reported");
    require(!settings.set(option("audio.muted"), 1) && !settings.reset() && !settings.apply(presets()[0]) &&
            !settings.choose_language_disc(french) && !settings.forget_language_disc(french),
            "nothing may be saved over a file that could not be read");
    require(read(file) == broken, "the unreadable file must be left as it is");
    write(file, "[video]\nsupersampling = 2\n");
    require(settings.set(option("audio.muted"), 1) && settings.readable() &&
            settings.shown(option("video.supersampling")) == 1, "a mended file must be taken again");
    fs::remove(file);
}

void aspects(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    const Option& aspect = option("video.aspect_ratio");
    write(file, "[video]\naspect_ratio = \"16:10\"\n");
    SettingsFile settings(file);
    require(settings.load() && settings.shown(aspect) == -1, "a ratio outside the list must select nothing");
    write(file, "[video]\naspect_ratio = \"64:27\"\n");
    require(settings.load() && settings.shown(aspect) == -1, "64:27 is not 21:9");
    write(file, "[video]\naspect_ratio = \"8:6\"\n");
    require(settings.load() && settings.shown(aspect) == 0, "a ratio is matched by its value");
    write(file, "[video]\naspect_ratio = \"32:9\"\nadaptive_view = true\n");
    require(settings.load() && settings.shown(aspect) == 4, "the adaptive view is the last choice");
    require(settings.set(aspect, 2) && has(file, "aspect_ratio      = \"21:9\"") && has(file, "adaptive_view     = false"),
            "a fixed ratio must switch the adaptive view off");
    require(settings.set(aspect, 4) && has(file, "aspect_ratio      = \"32:9\"") && has(file, "adaptive_view     = true"),
            "matching the window is 32:9 with the adaptive view on");
    write(file, "[video]\nadaptive_view = true\n");
    require(settings.load() && settings.shown(aspect) == 4, "the adaptive view alone is the game matching its window");
    write(file, "[video]\nadaptive_view = false\n");
    require(settings.load() && settings.shown(aspect) == 0, "without a ratio and the adaptive view the game is 4:3");
    write(file, "[video]\nsupersampling = 9\nvsync = \"immediate\"\n");
    require(settings.load() && settings.shown(option("video.supersampling")) == 3 && settings.shown(option("video.vsync")) == 1,
            "a scale the game refuses is shown as the game's own");
    fs::remove(file);
}

// The row shows what the in-game control shows: both go through disruptor_present_rate_shown.
void rates(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    const Option& rate = option("video.frame_interpolation_fps");
    write(file, "[video]\nframe_interpolation_fps = 300\n");
    SettingsFile settings(file);
    settings.set_display_rate(144.0);
    require(settings.load() && settings.shown(rate) == 144, "a rate over the display's is shown as the display's");
    settings.set_display_rate(360.0);
    require(settings.shown(rate) == 300 && settings.highest(rate) == 360, "a rate the display reaches is shown as it is");
    write(file, "[video]\nframe_interpolation_fps = 0\n");
    settings.set_display_rate(60.0);
    require(settings.load() && settings.shown(rate) == 120, "the display's own rate is shown as the top of the control");
    fs::remove(file);
}

void overrides() {
    SetEnvironmentVariableW(L"PSX_LAUNCHER_TEST_ONE", nullptr);
    SetEnvironmentVariableW(L"psx_launcher_test_two", nullptr);
    const std::wstring before = environment_overrides();
    require(before.find(L"LAUNCHER_TEST") == std::wstring::npos, "a variable that is not set is not named");
    SetEnvironmentVariableW(L"PSX_LAUNCHER_TEST_ONE", L"1");
    SetEnvironmentVariableW(L"psx_launcher_test_two", L"a=b");
    SetEnvironmentVariableW(L"NOT_PSX_LAUNCHER_TEST", L"1");
    const std::wstring named = environment_overrides();
    SetEnvironmentVariableW(L"PSX_LAUNCHER_TEST_ONE", nullptr);
    SetEnvironmentVariableW(L"psx_launcher_test_two", nullptr);
    SetEnvironmentVariableW(L"NOT_PSX_LAUNCHER_TEST", nullptr);
    require(named.find(L"PSX_LAUNCHER_TEST_ONE") != std::wstring::npos &&
            named.find(L"psx_launcher_test_two") != std::wstring::npos && named.find(L"NOT_PSX") == std::wstring::npos &&
            named.find(L'=') == std::wstring::npos && named.find(L", ") != std::wstring::npos,
            "the PSX_ variables are named, whatever their case, and nothing else");
}

void languages(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    write(file, "[disruptor]\nlanguage_disc = 'C:\\Games \"old\"\\Disruptor (France).cue'\n");
    SettingsFile settings(file);
    require(settings.load() && settings.language_disc() == french && settings.language_discs() == std::vector{french},
            "a disc named by the in-game menu must be offered");
    require(settings.choose_language_disc(german) && settings.language_disc() == german &&
            settings.language_discs() == (std::vector{french, german}) && has(file, "language_discs = [\"C:"),
            "choosing another disc must keep the one the in-game menu named");
    require(settings.choose_language_disc(french) && settings.choose_language_disc(french) &&
            settings.language_discs() == (std::vector{french, german}), "a disc is listed once");
    require(settings.choose_language_disc({}) && settings.language_disc().empty() &&
            settings.language_discs() == (std::vector{french, german}), "English keeps the discs for later");
    SettingsFile again(file);
    require(again.load() && again.language_disc().empty() && again.language_discs() == (std::vector{french, german}),
            "the discs must be read back, letters outside ASCII included");
    require(again.choose_language_disc(german) && again.forget_language_disc(french) &&
            again.language_disc() == german && again.language_discs() == std::vector{german},
            "forgetting another disc leaves the language");
    require(again.forget_language_disc(german) && again.language_disc().empty() && again.language_discs().empty() &&
            has(file, "language_disc = \"\"") && has(file, "language_discs = []"), "forgetting the disc in use is English");
    require(utf8(from_utf8(german)) == german && from_utf8(german).filename() == L"Disruptor (Germany).cue",
            "paths are UTF-8 in the file");
    fs::remove(file);
}

void log(const fs::path& folder) {
    const fs::path file = folder / "startup.log";
    require(log_tail(file, 3).empty(), "no log is no tail");
    write(file, "one\ntwo\r\n\r\nthree\nfour\n");
    require(log_tail(file, 3) == L"two\r\nthree\r\nfour\r\n", "the tail is the last lines that say something");
    require(log_tail(file, 9) == L"one\r\ntwo\r\nthree\r\nfour\r\n", "a short log is shown whole");
    write(file, "caf\xC3\xA9\n");
    require(log_tail(file, 1) == L"caf\u00E9\r\n", "the log is UTF-8");
    write(file, "caf\xE9\n");
    require(log_tail(file, 1) == L"caf�\r\n", "a log that is not UTF-8 is still shown");
    write(file, std::string(20000, 'x') + "\xD0\x94\nlast\n");
    require(log_tail(file, 5) == L"last\r\n", "a line cut by the read is not shown");
    fs::remove(file);
}

void completed(const fs::path& folder) {
    const fs::path file = folder / "complete.toml";
    for (const char* start : {"", "# nothing named\n", "[video]\n\n[disruptor]\n"}) {
        fs::remove(file);
        if (*start) write(file, start);
        SettingsFile fresh(file);
        require(fresh.complete() && fs::exists(file), "Play must write the shipped rows a missing or empty file does not name");
        const Settings written = PSXRecompV4::load_user_settings(file);
        for (const auto& one : options()) {
            require(one.saved(written) == shipped(one), "only the rows a release ships are written, the others stay the game's");
            require(!shipped(one) || one.get(written) == one.initial, "a shipped row is written with the value a release has");
            if (std::string_view(one.key) != "video.frame_interpolation_fps")
                require(fresh.shown(one) == one.initial, "and the window shows every row as before");
        }
    }

    const std::string before = read(file);
    fs::permissions(file, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write, fs::perm_options::remove);
    SettingsFile whole(file);
    require(whole.complete() && read(file) == before, "a file that names every shipped row is not written again");
    fs::permissions(file, fs::perms::owner_write, fs::perm_options::add);

    write(file, "[disruptor]\nmouse_aim = false\nhud_scale = 70\nfuture_key = 3\n\n[video]\nsupersampling = 2\n");
    SettingsFile partly(file);
    require(partly.complete(), "a file that names some rows is completed");
    const Settings kept = PSXRecompV4::load_user_settings(file);
    require(!kept.mouse_aim && kept.hud_scale == 70 && kept.supersampling == 2, "what the file named stays as it was");
    for (const auto& one : options()) {
        const std::string_view key = one.key;
        const bool named = key == "disruptor.mouse_aim" || key == "disruptor.hud_scale" || key == "video.supersampling";
        require(one.saved(kept) == (named || shipped(one)), "the absent shipped rows are named, and no other row is added");
        if (shipped(one) && !named) require(one.get(kept) == one.initial, "each with the value a release has");
    }
    require(partly.shown(option("disruptor.mouse_aim")) == 0 && partly.shown(option("disruptor.geometry_correction")) == 1,
            "the window shows the completed file");

    write(file, "[disruptor]\nmouse_aim = true\nmodern_controls = true\nvertical_look = false\ngeometry_correction = false\n"
                "perspective_textures = true\nframe_unlock = false\nimproved_shadows = false\n\n[video]\nframe_interpolation = false\n");
    const std::string chosen = read(file);
    SettingsFile theirs(file);
    require(theirs.complete() && read(file) == chosen && theirs.shown(option("disruptor.geometry_correction")) == 0,
            "a shipped row the player turned off is left off");

    write(file, "[video\nbroken");
    SettingsFile broken(file);
    require(!broken.complete() && read(file) == "[video\nbroken", "a file that is not TOML is left as it is");
    fs::remove(file);
}

// The game's own readers say what a new installation runs with: the table must start from the same values.
void shipped_values() {
    const fs::path source = DISRUPTOR_SOURCE_DIR;
    const Settings file = PSXRecompV4::load_user_settings(source / "release/windows/settings.toml");
    require(!file.parse_error, "the shipped settings.toml must be readable");
    size_t named = 0;
    for (const auto& one : options()) {
        require(one.saved(file) == shipped(one), "the rows Play restores are not the rows the shipped settings.toml names");
        if (!one.saved(file)) continue;
        ++named;
        require(one.get(file) == one.initial, "a row starts from another value than the shipped settings.toml gives it");
    }
    require(named == 8, "the shipped settings.toml names eight rows");
    const auto game = PSXRecompV4::load_game_config(source / "game.toml");
    Settings as_game;
    as_game.has_supersampling = as_game.has_aspect_ratio = true;
    as_game.supersampling = game.runtime.video_supersampling;
    as_game.aspect_num = game.runtime.video_aspect_num;
    as_game.aspect_den = game.runtime.video_aspect_den;
    for (const char* key : {"video.supersampling", "video.aspect_ratio"})
        require(option(key).get(as_game) == option(key).initial, "a row starts from another value than game.toml gives the game");
}
} // namespace

int main() {
    const fs::path folder = fs::temp_directory_path() / ("disruptor-launcher-settings-" + std::to_string(GetCurrentProcessId()));
    int result = 0;
    try {
        fs::remove_all(folder);
        fs::create_directories(folder);
        table();
        absent(folder);
        one_change(folder);
        round_trip(folder);
        whole_range(folder);
        dependants(folder);
        presets_and_reset(folder);
        unreadable(folder);
        aspects(folder);
        rates(folder);
        overrides();
        languages(folder);
        log(folder);
        completed(folder);
        shipped_values();
        std::cout << "Launcher settings, presets, language discs and log tail tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << "\n";
        result = 1;
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    return result;
}
