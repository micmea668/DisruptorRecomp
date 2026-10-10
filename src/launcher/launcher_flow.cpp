#include "launcher_flow.h"

#include "launcher_settings.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <exception>
#include <stdexcept>
#include <utility>

namespace disruptor::launcher {

Outcome run(Request request, const Discs& discs, const std::atomic_bool& cancelled) {
    Outcome outcome;
    outcome.task = request.task;
    outcome.launch = request.launch;
    outcome.image = request.source;
    try {
        if (request.task == Task::take && discs.look(request.source)) {
            if (request.home.empty())
                throw std::runtime_error("That is a disc of another region. Install the USA disc first: it is the game, "
                                         "and the other disc gives it a language.");
            outcome.task = Task::language;
        }
        if (outcome.task == Task::language) {
            outcome.language = discs.check(request.home, request.source);
            request.listed.push_back(utf8(request.source));
        } else if (outcome.task == Task::import || outcome.task == Task::verify) {
            outcome.disc = outcome.task == Task::import ? discs.import(request.source) : discs.verify(request.source);
            discs.check_package(outcome.disc);
            if (cancelled) {
                outcome.disc = {};
                outcome.error = L"Cancelled. Any completed import is kept; reopen the launcher to verify it again.";
            } else if (request.launch && !request.spoken.empty()) {
                if (discs.report) discs.report(L"Checking the language disc...", 100);
                try {
                    discs.check(outcome.disc.data, from_utf8(request.spoken));
                } catch (const std::exception& error) {
                    outcome.language_error = L"The language disc cannot be used. " + error_text(error);
                }
            }
        }
    } catch (const std::exception& error) {
        outcome.disc = {};
        outcome.language.reset();
        outcome.error = error_text(error);
    }
    for (const std::string& path : request.listed) {
        std::optional<RegionDisc> seen;
        try {
            seen = discs.look(from_utf8(path));
        } catch (const std::exception&) {
            seen.reset(); // a path that cannot be read is a disc that cannot be named
        }
        outcome.regions.emplace(path, std::move(seen));
    }
    return outcome;
}

Step after(const Outcome& outcome, bool cancelled) {
    Step step;
    const bool failed = !outcome.error.empty();
    if (outcome.task == Task::look || (outcome.task == Task::take && !failed)) {
        step.import_next = outcome.task == Task::take && !cancelled;
        if (cancelled) step.status = L"Cancelled.";
    } else if (outcome.task == Task::language || outcome.task == Task::take) {
        if (failed) {
            step.status = outcome.error;
        } else if (cancelled || !outcome.language) {
            step.status = L"Cancelled. The language is as it was.";
        } else {
            step.save_language = true;
            step.status = describe(*outcome.language) + L"\nThe image is used where it is, so keep it there.";
        }
    } else if (failed) {
        // A cancellation can leave an already committed installation.
        // Browsing again or reopening the launcher checks it afresh.
        step.disc = DiscAfter::cleared;
        step.status = outcome.error;
    } else {
        step.disc = DiscAfter::taken;
        if (!outcome.language_error.empty()) {
            step.status = outcome.language_error + L"\nChoose English or another disc, then select Play game.";
        } else {
            step.status = L"Game data is installed and ready. Select Play game to start.";
            step.launch = outcome.launch && !cancelled;
        }
    }
    return step;
}

std::wstring running_text(const std::wstring& overriding, bool unwritten) {
    std::wstring text = L"Disruptor is running. You can close this launcher and keep playing.";
    if (unwritten)
        text += L"\nsettings.toml cannot be written, so for the settings it does not name the game uses its own "
                L"values, which are not always the ones shown here.";
    if (!overriding.empty())
        text += L"\nPSX_ environment variables are set, and the game lets some of them replace settings for this "
                L"run: " + overriding;
    return text;
}

LanguageList language_list(const std::vector<std::string>& discs, const Regions& regions, const std::string& spoken) {
    LanguageList list{{L"English (the US disc alone)"}, 0, L"Add a French, German or Japanese disc image to play in that language."};
    for (size_t index = 0; index < discs.size(); ++index) {
        const auto looked = regions.find(discs[index]);
        const RegionDisc* disc = looked != regions.end() && looked->second ? &*looked->second : nullptr;
        const std::wstring kind = disc ? std::wstring(disc->language.begin(), disc->language.end())
            : looked == regions.end() ? L"Disc image" : L"Unknown or missing";
        list.labels.push_back(kind + L" (" + from_utf8(discs[index]).filename().wstring() + L")");
        if (discs[index] != spoken) continue;
        list.selected = static_cast<int>(index) + 1;
        list.note = disc ? describe(*disc) + L" The image is used where it is."
            : looked == regions.end() ? L""
            : L"This image is missing or is not a known disc. Choose another one or English.";
    }
    return list;
}

std::wstring quote(const std::wstring& argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto ch : argument) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}

std::wstring game_command(const std::filesystem::path& root, const VerifiedDisc& disc) {
    // Keep disc/config arguments relative and ASCII for the game's path loader.
    return quote((root / "DisruptorRecompiled.exe").wstring()) + L" --no-launcher --game " +
        quote(disc.profile->game_config) + L" --disc " + quote(disc.cue.lexically_relative(root).wstring());
}

std::wstring value_text(const Option& option, int value) {
    wchar_t text[32] = L"";
    if (option.unit == Unit::percent) swprintf(text, 32, L"%d%%", value);
    else if (option.unit == Unit::frames) swprintf(text, 32, L"%d FPS", value);
    else if (option.unit == Unit::thousandths) swprintf(text, 32, L"%.3f", value / 1000.0);
    return text;
}

int to_position(const Option& option, int value, int highest) {
    if (option.unit != Unit::thousandths) return value;
    const double span = std::log(static_cast<double>(highest) / option.lowest);
    return static_cast<int>(std::lround(std::log(static_cast<double>(value) / option.lowest) / span * slider_steps));
}

int from_position(const Option& option, int position, int highest) {
    if (option.unit != Unit::thousandths) return position;
    const double ratio = static_cast<double>(highest) / option.lowest;
    const double value = option.lowest * std::pow(ratio, static_cast<double>(position) / slider_steps);
    return std::clamp(static_cast<int>(std::lround(value)), option.lowest, highest);
}

} // namespace disruptor::launcher
