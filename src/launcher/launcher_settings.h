#pragma once

#include "config_loader.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace disruptor::launcher {

using Settings = PSXRecompV4::UserSettings;

enum class Page { display, enhancements, controls, audio };
enum class Kind { toggle, choice, slider };
enum class Unit { none, percent, frames, thousandths };

// A value is 0 or 1 for a toggle, an index into `choices` for a choice and a whole number of `unit` for a slider.
struct Option {
    const char* key; // "section.key" in settings.toml
    Page page;
    Kind kind;
    const wchar_t* label;
    bool (*saved)(const Settings&);
    int (*get)(const Settings&);
    void (*set)(Settings&, int);
    int initial; // what a fresh installation runs with
    int lowest;
    int highest;
    std::span<const wchar_t* const> choices;
    const char* needs; // the toggle that must be on for this row to apply
    Unit unit;
};

struct PresetValue { const char* key; int value; };
struct Preset { const wchar_t* label; std::span<const PresetValue> values; };

std::span<const Option> options();
std::span<const Preset> presets();
const Option* find_option(const char* key);
// Whether a release's settings.toml names the row: for these the game's own value is not the one a release runs with.
bool shipped(const Option& option);
std::span<const wchar_t* const> page_labels();

std::string utf8(const std::filesystem::path& path);
std::filesystem::path from_utf8(const std::string& text);
// The PSX_ variables of this process, which the game it starts lets replace settings for that run.
std::wstring environment_overrides();

// settings.toml through the game's own reader and writer: a change rereads the file, so a key the game saved since is kept.
class SettingsFile {
public:
    explicit SettingsFile(std::filesystem::path path);
    // False when the file is not valid TOML: nothing is written then, a save would replace it with defaults.
    bool load();
    bool readable() const { return readable_; }

    void set_display_rate(double hertz) { display_rate_ = hertz; }
    int highest(const Option& option) const;
    int shown(const Option& option) const;
    bool applies(const Option& option) const;

    bool set(const Option& option, int value);
    bool apply(const Preset& preset);
    bool reset();
    // Writes each shipped row the file does not name: for those the game's own value is not the one shown here.
    bool complete();

    // UTF-8 paths. An empty language disc means the US disc alone.
    const std::string& language_disc() const { return settings_.language_disc; }
    std::vector<std::string> language_discs() const;
    bool choose_language_disc(const std::string& path);
    bool forget_language_disc(const std::string& path);

    std::filesystem::path saves_folder() const;

private:
    template <typename Change> bool change(Change&& alter);
    static void put(Settings& settings, const Option& option, int value);

    std::filesystem::path path_;
    Settings settings_;
    bool readable_ = true;
    double display_rate_ = 0.0;
};

} // namespace disruptor::launcher
