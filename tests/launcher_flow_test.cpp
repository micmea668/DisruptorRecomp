#include "launcher_flow.h"

#include <atomic>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace disruptor::launcher;
namespace {
const fs::path kRoot = L"C:\\Games\\Disruptor";
const fs::path kHome = kRoot / "disc";
const fs::path kImage = L"D:\\\u0414\u0438\u0441\u043A\u0438\\Disruptor (France).cue";
const std::string kGerman = "E:\\Disruptor (Germany).cue";

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool starts(const std::wstring& text, const wchar_t* with) { return text.rfind(with, 0) == 0; }
bool ends(const std::wstring& text, const std::wstring& with) {
    return text.size() >= with.size() && text.compare(text.size() - with.size(), with.size(), with) == 0;
}

// Stands in for the discs and writes down what a task asked of them.
struct Shelf {
    std::vector<std::string> calls;
    std::vector<std::wstring> reports;
    std::optional<RegionDisc> kind;       // what an image turns out to be
    std::string unreadable;               // a path whose look throws
    const char* refuse_import = nullptr;
    const char* refuse_package = nullptr;
    const char* refuse_check = nullptr;
    std::atomic_bool cancelled{false};
    bool cancel_in_import = false;
    DiscProfile profile{"SLUS-00224", L"Disruptor (USA)", 0, "", L"game.toml"};

    VerifiedDisc read(const char* how, const fs::path& source) {
        calls.push_back(std::string(how) + " " + utf8(source));
        if (refuse_import) throw std::runtime_error(refuse_import);
        if (cancel_in_import) cancelled = true;
        return {&profile, kHome, kHome / "Disruptor.cue", nullptr};
    }
    Discs discs() {
        Discs discs;
        discs.import = [this](const fs::path& source) { return read("import", source); };
        discs.verify = [this](const fs::path& source) { return read("verify", source); };
        discs.check_package = [this](const VerifiedDisc& disc) {
            calls.push_back("package");
            require(disc.profile == &profile, "the package is checked for the disc that was just read");
            if (refuse_package) throw std::runtime_error(refuse_package);
        };
        discs.look = [this](const fs::path& image) {
            calls.push_back("look " + utf8(image));
            if (utf8(image) == unreadable) throw std::runtime_error("The share is gone.");
            return kind;
        };
        discs.check = [this](const fs::path& home, const fs::path& image) {
            calls.push_back("check " + utf8(home) + " | " + utf8(image));
            if (refuse_check) throw std::runtime_error(refuse_check);
            return RegionDisc{"French", true};
        };
        discs.report = [this](const std::wstring& text, unsigned percent) { reports.push_back(text + L" " + std::to_wstring(percent)); };
        return discs;
    }
    Outcome run(Request request) { return disruptor::launcher::run(std::move(request), discs(), cancelled); }
    bool asked(const std::vector<std::string>& expected) const { return calls == expected; }
};

void test_the_game_disc_is_read_and_checked() {
    Shelf verify;
    Outcome outcome = verify.run({Task::verify, kHome / "Disruptor.cue", false, {}, kGerman, {kGerman}});
    require(outcome.task == Task::verify && outcome.error.empty() && outcome.disc.profile == &verify.profile && !outcome.launch,
            "an installed disc must come back verified");
    require(verify.asked({"verify " + utf8(kHome / "Disruptor.cue"), "package", "look " + kGerman}) && verify.reports.empty(),
            "without Play the language disc is described and not laid out");
    require(outcome.regions.size() == 1 && outcome.regions.count(kGerman) == 1, "every listed disc is described on the worker");

    Shelf import;
    outcome = import.run({Task::import, kImage, true, {}, "", {}});
    require(outcome.disc.profile && outcome.launch && import.asked({"import " + utf8(kImage), "package"}),
            "an import copies the image, and English needs no second disc");

    Shelf incomplete;
    incomplete.refuse_package = "The build is incomplete.";
    outcome = incomplete.run({Task::verify, kHome, true, {}, kGerman, {}});
    require(outcome.error == L"The build is incomplete." && !outcome.disc.profile && outcome.language_error.empty() &&
                incomplete.asked({"verify " + utf8(kHome), "package"}),
            "an incomplete package must refuse the disc before any language disc is read");

    Shelf unreadable;
    unreadable.refuse_import = "That is not the USA disc.";
    outcome = unreadable.run({Task::import, kImage, true, {}, kGerman, {kGerman}});
    require(outcome.error == L"That is not the USA disc." && !outcome.disc.profile &&
                unreadable.asked({"import " + utf8(kImage), "look " + kGerman}) && outcome.regions.size() == 1,
            "a refused image leaves no disc, and the listed discs are still described");
}

void test_play_checks_the_language_disc_again() {
    Shelf fitting;
    Outcome outcome = fitting.run({Task::verify, kHome, true, {}, kGerman, {kGerman}});
    require(outcome.error.empty() && outcome.language_error.empty() && outcome.disc.profile,
            "a fitting language disc must let the game start");
    require(fitting.asked({"verify " + utf8(kHome), "package", "check " + utf8(kHome) + " | " + kGerman, "look " + kGerman}),
            "before Play the language disc is laid over the disc that was just verified");
    require(fitting.reports == std::vector<std::wstring>{L"Checking the language disc... 100"}, "and the window is told so");

    Shelf moved;
    moved.refuse_check = "The image is gone.";
    outcome = moved.run({Task::verify, kHome, true, {}, kGerman, {}});
    require(outcome.error.empty() && outcome.disc.profile && outcome.language_error == L"The language disc cannot be used. The image is gone.",
            "a language disc that no longer fits keeps the game disc and is reported apart");

    Shelf cancelled;
    cancelled.cancel_in_import = true;
    outcome = cancelled.run({Task::import, kImage, true, {}, kGerman, {}});
    require(starts(outcome.error, L"Cancelled.") && !outcome.disc.profile && cancelled.asked({"import " + utf8(kImage), "package"}),
            "a cancelled import gives no disc and reads no language disc");
}

void test_an_image_is_taken_for_what_it_is() {
    Shelf game;
    Outcome outcome = game.run({Task::take, kImage, true, kHome, "", {kGerman}});
    require(outcome.task == Task::take && outcome.error.empty() && outcome.image == kImage && outcome.launch && !outcome.language &&
                game.asked({"look " + utf8(kImage), "look " + kGerman}),
            "an image that is no disc of another region is left for the import, untouched");

    Shelf early;
    early.kind = RegionDisc{"French", true};
    outcome = early.run({Task::take, kImage, false, {}, "", {}});
    require(outcome.task == Task::take && starts(outcome.error, L"That is a disc of another region.") && early.asked({"look " + utf8(kImage)}),
            "a language disc before the game disc is refused and is not laid out");

    Shelf language;
    language.kind = RegionDisc{"French", true};
    outcome = language.run({Task::take, kImage, false, kHome, "", {kGerman}});
    require(outcome.task == Task::language && outcome.error.empty() && outcome.language && outcome.language->language == "French",
            "a disc of another region becomes the language disc");
    require(language.asked({"look " + utf8(kImage), "check " + utf8(kHome) + " | " + utf8(kImage), "look " + kGerman, "look " + utf8(kImage)}) &&
                outcome.regions.count(utf8(kImage)) == 1 && outcome.regions.count(kGerman) == 1,
            "it is laid over the installed disc and described with the listed ones");

    Shelf unfit;
    unfit.refuse_check = "The disc does not fit.";
    outcome = unfit.run({Task::language, kImage, false, kHome, "", {kGerman}});
    require(outcome.task == Task::language && outcome.error == L"The disc does not fit." && !outcome.language &&
                unfit.asked({"check " + utf8(kHome) + " | " + utf8(kImage), "look " + kGerman}) && outcome.regions.count(utf8(kImage)) == 0,
            "a disc that does not fit is not named and not listed");

    Shelf look;
    outcome = look.run({Task::look, {}, false, kHome, kGerman, {kGerman, utf8(kImage)}});
    require(outcome.task == Task::look && outcome.error.empty() && look.asked({"look " + kGerman, "look " + utf8(kImage)}) &&
                outcome.regions.size() == 2 && !outcome.regions.at(kGerman),
            "a look only describes the listed discs, and an image it cannot name has no description");

    Shelf gone;
    gone.kind = RegionDisc{"French", true};
    gone.unreadable = kGerman;
    outcome = gone.run({Task::look, {}, false, kHome, kGerman, {kGerman, utf8(kImage)}});
    require(outcome.error.empty() && outcome.regions.size() == 2 && !outcome.regions.at(kGerman) && outcome.regions.at(utf8(kImage)),
            "a listed disc whose look fails is one that cannot be named, and the others are still described");
}

Outcome made(Task task, bool launch = false, const wchar_t* error = L"", const wchar_t* language_error = L"") {
    Outcome outcome;
    outcome.task = task;
    outcome.launch = launch;
    outcome.error = error;
    outcome.language_error = language_error;
    return outcome;
}
bool same(const Step& step, DiscAfter disc, bool import_next, bool save_language, bool launch) {
    return step.disc == disc && step.import_next == import_next && step.save_language == save_language && step.launch == launch;
}

void test_the_window_acts_on_an_outcome() {
    Step step = after(made(Task::look), false);
    require(same(step, DiscAfter::kept, false, false, false) && step.status.empty(), "a look changes nothing and says nothing");
    step = after(made(Task::look), true);
    require(same(step, DiscAfter::kept, false, false, false) && step.status == L"Cancelled.", "a cancelled look says so");

    step = after(made(Task::take, true), false);
    require(same(step, DiscAfter::kept, true, false, false) && step.status.empty(), "an image that is no language disc goes on to the import");
    step = after(made(Task::take, true), true);
    require(same(step, DiscAfter::kept, false, false, false) && step.status == L"Cancelled.", "unless the look was cancelled");
    step = after(made(Task::take, true, L"Install the USA disc first."), false);
    require(same(step, DiscAfter::kept, false, false, false) && step.status == L"Install the USA disc first.",
            "a refused image keeps the installed disc and is not imported");

    Outcome language = made(Task::language);
    language.language = RegionDisc{"German", true};
    step = after(language, false);
    require(same(step, DiscAfter::kept, false, true, false) && starts(step.status, L"German disc: ") &&
                ends(step.status, L"\nThe image is used where it is, so keep it there."),
            "a checked language disc is named in the settings and described");
    step = after(language, true);
    require(same(step, DiscAfter::kept, false, false, false) && step.status == L"Cancelled. The language is as it was.",
            "a cancelled check changes nothing");
    step = after(made(Task::language), false);
    require(same(step, DiscAfter::kept, false, false, false), "an outcome without a disc names none");
    step = after(made(Task::language, false, L"The disc does not fit."), false);
    require(same(step, DiscAfter::kept, false, false, false) && step.status == L"The disc does not fit.", "a refused language disc changes nothing");

    step = after(made(Task::verify, true, L"The image is damaged."), false);
    require(same(step, DiscAfter::cleared, false, false, false) && step.status == L"The image is damaged.", "a failed check forgets the disc");
    step = after(made(Task::import, true), false);
    require(same(step, DiscAfter::taken, false, false, true) && step.status == L"Game data is installed and ready. Select Play game to start.",
            "a verified disc is taken and the game is started when that was asked");
    step = after(made(Task::verify, false), false);
    require(same(step, DiscAfter::taken, false, false, false), "and is not started when it was not");
    step = after(made(Task::verify, true), true);
    require(same(step, DiscAfter::taken, false, false, false), "nor after Cancel");
    step = after(made(Task::verify, true, L"", L"The language disc cannot be used."), false);
    require(same(step, DiscAfter::taken, false, false, false) &&
                step.status == L"The language disc cannot be used.\nChoose English or another disc, then select Play game.",
            "a language disc that no longer fits keeps the game from starting");
}

void test_the_running_status() {
    const std::wstring plain = running_text(L"", false);
    require(plain == L"Disruptor is running. You can close this launcher and keep playing.", "a plain start says the game runs");
    const std::wstring unwritten = running_text(L"", true), both = running_text(L"PSX_VSYNC", true);
    require(starts(unwritten, plain.c_str()) && unwritten.find(L"settings.toml cannot be written") != std::wstring::npos &&
                running_text(L"PSX_VSYNC", false).find(L"settings.toml") == std::wstring::npos,
            "a settings file that could not be completed is named, and only then");
    require(ends(both, L"run: PSX_VSYNC") && both.find(L"settings.toml cannot be written") != std::wstring::npos &&
                unwritten.find(L"PSX_") == std::wstring::npos,
            "the variables that replace settings are named, and only when there are some");
}

void test_the_language_box() {
    const std::string french = utf8(kImage), missing = "F:\\gone.cue";
    LanguageList list = language_list({}, {}, "");
    require(list.labels == std::vector<std::wstring>{L"English (the US disc alone)"} && list.selected == 0 && starts(list.note, L"Add a French"),
            "without discs the box offers English and says how to add one");

    Regions regions{{french, RegionDisc{"French", true}}, {missing, std::nullopt}};
    list = language_list({kGerman, french, missing}, regions, french);
    require(list.labels == std::vector<std::wstring>{L"English (the US disc alone)", L"Disc image (Disruptor (Germany).cue)",
                                                     L"French (Disruptor (France).cue)", L"Unknown or missing (gone.cue)"},
            "a disc is listed by its language and file name, one not looked at yet as a disc image");
    require(list.selected == 2 && starts(list.note, L"French disc: ") && ends(list.note, L" The image is used where it is."),
            "the disc in use is selected and described");
    list = language_list({kGerman, french, missing}, regions, missing);
    require(list.selected == 3 && starts(list.note, L"This image is missing"), "a missing disc in use is called missing");
    list = language_list({kGerman, french, missing}, regions, kGerman);
    require(list.selected == 1 && list.note.empty(), "a disc not looked at yet has no note");
    list = language_list({kGerman}, regions, "G:\\unlisted.cue");
    require(list.selected == 0 && starts(list.note, L"Add a French"), "a disc in use that is not listed leaves English selected");
}

void test_the_command_line() {
    require(quote(L"a b") == L"\"a b\"" && quote(L"") == L"\"\"" && quote(L"C:\\a\\b") == L"\"C:\\a\\b\"",
            "an argument is quoted, a backslash inside it stays single");
    require(quote(L"C:\\a\\") == L"\"C:\\a\\\\\"" && quote(L"a\"b") == L"\"a\\\"b\"" && quote(L"a\\\"b") == L"\"a\\\\\\\"b\"",
            "backslashes before a quote are doubled and the quote escaped");
    DiscProfile profile{"SLUS-00224", L"Disruptor (USA)", 0, "", L"game.toml"};
    const fs::path root = L"C:\\Games folder\\Disruptor";
    const VerifiedDisc disc{&profile, root / "disc", root / "disc" / "Disruptor (USA).cue", nullptr};
    require(game_command(root, disc) == L"\"C:\\Games folder\\Disruptor\\DisruptorRecompiled.exe\" --no-launcher --game \"game.toml\" "
                                        L"--disc \"disc\\Disruptor (USA).cue\"",
            "the game is started without its own launcher, with the disc named relative to the package");
}

void test_the_sliders() {
    size_t logarithmic = 0, linear = 0;
    for (const Option& option : options()) {
        if (option.kind != Kind::slider) continue;
        if (option.unit != Unit::thousandths) {
            ++linear;
            require(to_position(option, option.lowest, option.highest) == option.lowest &&
                        from_position(option, option.highest, option.highest) == option.highest,
                    "a plain slider's position is its value");
            continue;
        }
        ++logarithmic;
        require(to_position(option, option.lowest, option.highest) == 0 && to_position(option, option.highest, option.highest) == slider_steps &&
                    from_position(option, 0, option.highest) == option.lowest && from_position(option, slider_steps, option.highest) == option.highest,
                "a sensitivity slider runs from its lowest value to its highest");
        int before = option.lowest - 1;
        for (int position = 0; position <= slider_steps; ++position) {
            const int value = from_position(option, position, option.highest);
            require(value >= before && value >= option.lowest && value <= option.highest, "a later position is never a lower value");
            require(position < 60 || to_position(option, value, option.highest) == position, "a position past the crowded low end keeps its value");
            before = value;
        }
        require(to_position(option, option.initial, option.highest) > 0 && to_position(option, option.initial, option.highest) < slider_steps,
                "the start value lies inside the slider");
    }
    require(logarithmic == 2 && linear >= 3, "the table must hold the sliders this test measures");
    const Option* sensitivity = find_option("disruptor.horizontal_sensitivity");
    const Option* volume = find_option("audio.master_volume");
    const Option* rate = find_option("video.frame_interpolation_fps");
    require(sensitivity && volume && rate && value_text(*sensitivity, 80) == L"0.080" && value_text(*volume, 55) == L"55%" &&
                value_text(*rate, 120) == L"120 FPS",
            "a value is shown in its unit");
}
} // namespace

int main() {
    try {
        test_the_game_disc_is_read_and_checked();
        test_play_checks_the_language_disc_again();
        test_an_image_is_taken_for_what_it_is();
        test_the_window_acts_on_an_outcome();
        test_the_running_status();
        test_the_language_box();
        test_the_command_line();
        test_the_sliders();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "launcher flow: PASS\n";
    return 0;
}
