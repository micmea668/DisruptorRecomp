#include "launcher_settings.h"

#include "disruptor_present_rate.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fs = std::filesystem;
namespace disruptor::launcher {
namespace {
using Saved = bool (*)(const Settings&);
using Get = int (*)(const Settings&);
using Set = void (*)(Settings&, int);

template <auto Has> bool saved(const Settings& settings) { return settings.*Has; }
template <auto Value> int read(const Settings& settings) { return static_cast<int>(settings.*Value); }
template <auto Has, auto Value> void write(Settings& settings, int value) {
    settings.*Has = true;
    settings.*Value = static_cast<std::remove_reference_t<decltype(settings.*Value)>>(value);
}

constexpr int aspect_widths[] = {4, 16, 21, 32};
constexpr int aspect_heights[] = {3, 9, 9, 9};
constexpr int widest_aspect = 3, window_aspect = 4;

bool aspect_saved(const Settings& settings) { return settings.has_aspect_ratio || settings.has_adaptive_view; }

int aspect(const Settings& settings) {
    if (settings.has_adaptive_view && settings.adaptive_view) return window_aspect;
    for (int index = 0; index <= widest_aspect; ++index)
        if (settings.aspect_num * aspect_heights[index] == settings.aspect_den * aspect_widths[index]) return index;
    return -1;
}

void set_aspect(Settings& settings, int index) {
    const int fixed = index == window_aspect ? widest_aspect : index;
    settings.has_aspect_ratio = settings.has_adaptive_view = true;
    settings.adaptive_view = index == window_aspect;
    settings.aspect_num = aspect_widths[fixed];
    settings.aspect_den = aspect_heights[fixed];
}

int scale(const Settings& settings) { return settings.supersampling - 1; }
void set_scale(Settings& settings, int index) { settings.has_supersampling = true; settings.supersampling = index + 1; }
int vsync(const Settings& settings) { return std::clamp(settings.vsync + 1, 0, 2); }
void set_vsync(Settings& settings, int index) { settings.has_vsync = true; settings.vsync = index - 1; }

template <auto Value> int thousandths(const Settings& settings) {
    return static_cast<int>(std::lround(settings.*Value * 1000.0));
}
template <auto Has, auto Value> void set_thousandths(Settings& settings, int value) {
    settings.*Has = true;
    settings.*Value = value / 1000.0;
}

constexpr const wchar_t* display_modes[] = {L"Windowed", L"Borderless fullscreen", L"Exclusive fullscreen"};
constexpr const wchar_t* aspects[] = {L"4:3", L"16:9", L"21:9", L"32:9", L"Match window (up to 32:9)"};
constexpr const wchar_t* scales[] = {L"1x (native)", L"2x", L"3x", L"4x", L"5x", L"6x", L"7x", L"8x"};
constexpr const wchar_t* vsyncs[] = {L"Adaptive", L"Immediate", L"Synchronised"};
constexpr const wchar_t* shadows[] = {L"Vanilla", L"Improved"};
constexpr const wchar_t* pages[] = {L"Display", L"Enhancements", L"Controls", L"Audio"};

constexpr const char* geometry_key = "disruptor.geometry_correction";
constexpr const char* frames_key = "video.frame_interpolation";
constexpr const char* rate_key = "video.frame_interpolation_fps";

template <auto Has, auto Value>
constexpr Option toggle(const char* key, Page page, const wchar_t* label, int initial, const char* needs = nullptr) {
    return {key, page, Kind::toggle, label, saved<Has>, read<Value>, write<Has, Value>, initial, 0, 1, {}, needs, Unit::none};
}

constexpr Option choice(const char* key, Page page, const wchar_t* label, int initial,
                        std::span<const wchar_t* const> choices, Saved saved, Get get, Set set) {
    return {key, page, Kind::choice, label, saved, get, set, initial, 0, static_cast<int>(choices.size()) - 1,
            choices, nullptr, Unit::none};
}

constexpr Option slider(const char* key, Page page, const wchar_t* label, int initial, int lowest, int highest,
                        Unit unit, Saved saved, Get get, Set set, const char* needs = nullptr) {
    return {key, page, Kind::slider, label, saved, get, set, initial, lowest, highest, {}, needs, unit};
}

constexpr Option option_list[] = {
    choice("video.fullscreen", Page::display, L"Display mode", 0, display_modes,
           saved<&Settings::has_fullscreen>, read<&Settings::fullscreen>,
           write<&Settings::has_fullscreen, &Settings::fullscreen>),
    choice("video.aspect_ratio", Page::display, L"Aspect ratio", 0, aspects,
           aspect_saved, aspect, set_aspect),
    choice("video.supersampling", Page::display, L"Internal resolution scale", 3, scales,
           saved<&Settings::has_supersampling>, scale, set_scale),
    choice("video.vsync", Page::display, L"VSync", 2, vsyncs, saved<&Settings::has_vsync>, vsync, set_vsync),
    slider("disruptor.hud_scale", Page::display, L"HUD size", 100, 50, 100, Unit::percent,
           saved<&Settings::has_hud_scale>, read<&Settings::hud_scale>,
           write<&Settings::has_hud_scale, &Settings::hud_scale>),

    toggle<&Settings::has_geometry_correction, &Settings::geometry_correction>(
        "disruptor.geometry_correction", Page::enhancements, L"Exact-provenance geometry", 1),
    toggle<&Settings::has_perspective_textures, &Settings::perspective_textures>(
        "disruptor.perspective_textures", Page::enhancements, L"Perspective-correct world textures", 1, geometry_key),
    choice("disruptor.improved_shadows", Page::enhancements, L"Shadows", 0, shadows,
           saved<&Settings::has_improved_shadows>, read<&Settings::improved_shadows>,
           write<&Settings::has_improved_shadows, &Settings::improved_shadows>),
    toggle<&Settings::has_frame_unlock, &Settings::frame_unlock>(
        "disruptor.frame_unlock", Page::enhancements, L"60 FPS gameplay (experimental)", 0),
    toggle<&Settings::has_frame_interpolation, &Settings::frame_interpolation>(
        "video.frame_interpolation", Page::enhancements, L"In-between frames (experimental)", 0, geometry_key),
    slider("video.frame_interpolation_fps", Page::enhancements, L"In-between frame rate", 0,
           kDisruptorPresentRateLowest, kDisruptorPresentRateHighest, Unit::frames,
           saved<&Settings::has_frame_interpolation_fps>, read<&Settings::frame_interpolation_fps>,
           write<&Settings::has_frame_interpolation_fps, &Settings::frame_interpolation_fps>, frames_key),
    toggle<&Settings::has_skip_intro, &Settings::skip_intro>(
        "disruptor.skip_intro", Page::enhancements, L"Skip the logos and the title movie", 0),

    toggle<&Settings::has_mouse_aim, &Settings::mouse_aim>(
        "disruptor.mouse_aim", Page::controls, L"Horizontal mouse aim", 1),
    toggle<&Settings::has_modern_controls, &Settings::modern_controls>(
        "disruptor.modern_controls", Page::controls, L"Modern keyboard and mouse controls", 1),
    slider("disruptor.horizontal_sensitivity", Page::controls, L"Horizontal sensitivity", 80, 5, 2000, Unit::thousandths,
           saved<&Settings::has_horizontal_sensitivity>, thousandths<&Settings::horizontal_sensitivity>,
           set_thousandths<&Settings::has_horizontal_sensitivity, &Settings::horizontal_sensitivity>),
    toggle<&Settings::has_invert_horizontal, &Settings::invert_horizontal>(
        "disruptor.invert_horizontal", Page::controls, L"Invert horizontal mouse", 0),
    toggle<&Settings::has_vertical_look, &Settings::vertical_look>(
        "disruptor.vertical_look", Page::controls, L"Vertical mouse look", 0),
    slider("disruptor.vertical_sensitivity", Page::controls, L"Vertical sensitivity", 80, 5, 2000, Unit::thousandths,
           saved<&Settings::has_vertical_sensitivity>, thousandths<&Settings::vertical_sensitivity>,
           set_thousandths<&Settings::has_vertical_sensitivity, &Settings::vertical_sensitivity>),
    toggle<&Settings::has_invert_vertical, &Settings::invert_vertical>(
        "disruptor.invert_vertical", Page::controls, L"Invert vertical mouse", 0),

    slider("audio.master_volume", Page::audio, L"Master volume", 100, 0, 100, Unit::percent,
           saved<&Settings::has_master_volume>, read<&Settings::master_volume>,
           write<&Settings::has_master_volume, &Settings::master_volume>),
    toggle<&Settings::has_audio_muted, &Settings::audio_muted>("audio.muted", Page::audio, L"Mute all audio", 0),
};

constexpr PresetValue original_values[] = {
    {"video.aspect_ratio", 0}, {"disruptor.hud_scale", 100}, {"disruptor.geometry_correction", 0},
    {"disruptor.perspective_textures", 0}, {"disruptor.improved_shadows", 0}, {"disruptor.frame_unlock", 0},
    {"video.frame_interpolation", 0}};
constexpr PresetValue enhanced_values[] = {
    {"video.aspect_ratio", 1}, {"disruptor.geometry_correction", 1}, {"disruptor.perspective_textures", 1},
    {"disruptor.improved_shadows", 1}, {"disruptor.frame_unlock", 1}, {"video.frame_interpolation", 1}};
constexpr Preset preset_list[] = {{L"Original", original_values}, {L"Enhanced (experimental)", enhanced_values}};

constexpr const char* shipped_keys[] = {
    "disruptor.mouse_aim", "disruptor.modern_controls", "disruptor.vertical_look", "disruptor.geometry_correction",
    "disruptor.perspective_textures", "disruptor.frame_unlock", "disruptor.improved_shadows", "video.frame_interpolation"};

int stored(const Settings& settings, const Option& option) {
    return option.saved(settings) ? option.get(settings) : option.initial;
}

bool listed(const std::vector<std::string>& discs, const std::string& path) {
    return std::find(discs.begin(), discs.end(), path) != discs.end();
}
} // namespace

std::span<const Option> options() { return option_list; }
std::span<const Preset> presets() { return preset_list; }
std::span<const wchar_t* const> page_labels() { return pages; }

const Option* find_option(const char* key) {
    for (const auto& option : option_list)
        if (std::string_view(option.key) == key) return &option;
    return nullptr;
}

std::string utf8(const fs::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

fs::path from_utf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

std::wstring environment_overrides() {
    std::wstring names;
    wchar_t* const block = GetEnvironmentStringsW();
    for (const wchar_t* entry = block; entry && *entry; entry += wcslen(entry) + 1) {
        const std::wstring_view variable(entry);
        if (_wcsnicmp(entry, L"PSX_", 4) != 0) continue;
        if (!names.empty()) names += L", ";
        names += variable.substr(0, variable.find(L'='));
    }
    if (block) FreeEnvironmentStringsW(block);
    return names;
}

SettingsFile::SettingsFile(fs::path path) : path_(std::move(path)) {}

bool SettingsFile::load() {
    settings_ = PSXRecompV4::load_user_settings(path_);
    readable_ = !settings_.parse_error;
    if (!readable_) settings_ = Settings{};
    return readable_;
}

int SettingsFile::highest(const Option& option) const {
    return std::string_view(option.key) == rate_key ? disruptor_present_rate_top(display_rate_) : option.highest;
}

int SettingsFile::shown(const Option& option) const {
    const int value = stored(settings_, option);
    if (option.kind == Kind::toggle) return value != 0;
    if (option.kind == Kind::choice) return value >= 0 && value <= option.highest ? value : -1;
    if (std::string_view(option.key) == rate_key) return disruptor_present_rate_shown(value, highest(option));
    return std::clamp(value, option.lowest, highest(option));
}

bool SettingsFile::applies(const Option& option) const {
    const Option* needed = option.needs ? find_option(option.needs) : nullptr;
    return !needed || (shown(*needed) != 0 && applies(*needed));
}

void SettingsFile::put(Settings& settings, const Option& option, int value) {
    option.set(settings, value);
    if (option.kind != Kind::toggle || value != 0) return;
    for (const auto& other : option_list) {
        if (other.kind == Kind::toggle && other.needs && std::string_view(other.needs) == option.key &&
            stored(settings, other) != 0)
            put(settings, other, 0);
    }
}

template <typename Change> bool SettingsFile::change(Change&& alter) {
    Settings fresh = PSXRecompV4::load_user_settings(path_);
    readable_ = !fresh.parse_error;
    if (!readable_) return false;
    alter(fresh);
    if (!PSXRecompV4::save_user_settings(path_, fresh)) return false;
    settings_ = std::move(fresh);
    return true;
}

bool SettingsFile::set(const Option& option, int value) {
    return change([&](Settings& settings) { put(settings, option, value); });
}

bool SettingsFile::apply(const Preset& preset) {
    return change([&](Settings& settings) {
        for (const auto& value : preset.values) put(settings, *find_option(value.key), value.value);
    });
}

bool SettingsFile::reset() {
    return change([](Settings& settings) {
        for (const auto& option : option_list) option.set(settings, option.initial);
    });
}

bool shipped(const Option& option) {
    const auto same = [&](const char* key) { return std::string_view(key) == option.key; };
    return std::any_of(std::begin(shipped_keys), std::end(shipped_keys), same);
}

bool SettingsFile::complete() {
    if (!load()) return false;
    const auto whole = [this](const Option& option) { return !shipped(option) || option.saved(settings_); };
    if (std::all_of(std::begin(option_list), std::end(option_list), whole)) return true;
    return change([](Settings& settings) {
        for (const auto& option : option_list)
            if (shipped(option) && !option.saved(settings)) option.set(settings, option.initial);
    });
}

std::vector<std::string> SettingsFile::language_discs() const {
    auto discs = settings_.language_discs;
    if (!settings_.language_disc.empty() && !listed(discs, settings_.language_disc))
        discs.push_back(settings_.language_disc);
    return discs;
}

bool SettingsFile::choose_language_disc(const std::string& path) {
    return change([&](Settings& settings) {
        for (const std::string& disc : {settings.language_disc, path}) {
            if (disc.empty() || listed(settings.language_discs, disc)) continue;
            settings.has_language_discs = true;
            settings.language_discs.push_back(disc);
        }
        settings.has_language_disc = true;
        settings.language_disc = path;
    });
}

bool SettingsFile::forget_language_disc(const std::string& path) {
    return change([&](Settings& settings) {
        std::erase(settings.language_discs, path);
        settings.has_language_discs = true;
        if (settings.language_disc != path) return;
        settings.has_language_disc = true;
        settings.language_disc.clear();
    });
}

fs::path SettingsFile::saves_folder() const {
    const fs::path beside = path_.parent_path();
    return settings_.has_memcard_dir ? beside / settings_.memcard_dir : beside;
}

} // namespace disruptor::launcher
