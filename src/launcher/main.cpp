#include "disc_import.h"
#include "launcher_flow.h"
#include "launcher_settings.h"
#include "region_disc.h"
#include "startup_log.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace disruptor::launcher;
namespace {
constexpr UINT progress_message = WM_APP + 1;
constexpr UINT finished_message = WM_APP + 2;
constexpr int browse_id = 100, play_id = 101, cancel_id = 102, help_id = 103;
constexpr int language_id = 104, add_disc_id = 105, remove_disc_id = 106, reset_id = 107, saves_id = 108;
constexpr int preset_base = 200, option_base = 1000;
constexpr int client_width = 680, client_height = 664;
constexpr size_t log_lines = 8;
constexpr LRESULT status_lines = 3;
constexpr const wchar_t* image_filter =
    L"Disc images (*.cue;*.bin;*.iso;*.img)\0*.cue;*.bin;*.iso;*.img\0CUE sheets (*.cue)\0*.cue\0"
    L"Raw images (*.bin;*.iso;*.img)\0*.bin;*.iso;*.img\0\0";
constexpr const wchar_t* page_notes[] = {
    L"HUD size applies in widescreen.",
    L"Perspective textures and in-between frames need exact geometry.",
    L"Key bindings are in keybinds.ini beside the launcher.",
    L"",
};

fs::path executable_directory() {
    std::wstring buffer(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) throw std::runtime_error("Cannot find the launcher's folder.");
    buffer.resize(size);
    return fs::path(buffer).parent_path();
}

void check_package(const fs::path& root, const DiscProfile& profile) {
    for (const auto& name : {fs::path(L"DisruptorRecompiled.exe"), fs::path(profile.game_config),
                             fs::path(L"bios/openbios.bin")}) {
        if (!fs::is_regular_file(root / name))
            throw std::runtime_error("The build is incomplete. Use Extract All on the release ZIP and keep "
                                     "the launcher, game executable, game.toml, and bios folder together.");
    }
    if (fs::file_size(root / "bios/openbios.bin") != 524288)
        throw std::runtime_error("The bundled OpenBIOS is incomplete. Extract the complete release again.");
}

HANDLE start_game(const fs::path& root, const VerifiedDisc& disc) {
    check_package(root, *disc.profile);
    const auto executable = root / "DisruptorRecompiled.exe";
    std::wstring command = game_command(root, disc);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE log = CreateFileW((root / "startup.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        throw std::runtime_error("Cannot create startup.log. Put the extracted build in a writable Games folder.");
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = startup.hStdError = log;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                                        TRUE, CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &process);
    const DWORD failure = GetLastError();
    CloseHandle(log);
    CloseHandle(input);
    if (!started)
        throw std::runtime_error("Windows could not start DisruptorRecompiled.exe (error " +
            std::to_string(failure) + "). Check that the complete build is extracted and the "
            "Microsoft Visual C++ x64 Redistributable is installed. See GETTING_STARTED.md.");
    CloseHandle(process.hThread);
    return process.hProcess;
}

std::optional<fs::path> pick_image(HWND owner, const wchar_t* title) {
    std::wstring selected(32768, L'\0');
    OPENFILENAMEW picker{};
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = owner;
    picker.lpstrTitle = title;
    picker.lpstrFilter = image_filter;
    picker.lpstrFile = selected.data();
    picker.nMaxFile = static_cast<DWORD>(selected.size());
    picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&picker)) return std::nullopt;
    selected.resize(wcslen(selected.c_str()));
    return fs::path(selected);
}

struct Update { std::wstring status; unsigned percent; };
struct Placed { HWND window; int x, y, width, height; bool heading; };
struct Row { const Option* option; HWND label = nullptr, control = nullptr, value = nullptr; };

struct App {
    fs::path root;
    SettingsFile settings;
    HWND window = nullptr, status = nullptr, progress = nullptr, disc_text = nullptr;
    HWND browse = nullptr, play = nullptr, cancel = nullptr, auto_play = nullptr;
    HWND language = nullptr, language_note = nullptr, add_disc = nullptr, remove_disc = nullptr;
    HWND tabs = nullptr, page_note = nullptr, reset = nullptr;
    std::vector<HWND> preset_buttons;
    std::vector<Placed> placed;
    std::vector<Row> rows;
    std::vector<std::string> discs;
    Regions regions; // filled by the worker only: an image on a share that is gone must not stall the window
    HFONT body_font = nullptr, heading_font = nullptr;
    HBRUSH page_brush = nullptr;
    std::thread worker;
    std::atomic_bool cancelled{false};
    bool busy = false, closing = false, unwritten = false;
    HANDLE game = nullptr;
    VerifiedDisc verified;

    explicit App(fs::path folder) : root(std::move(folder)), settings(root / "settings.toml") {}
    ~App() {
        cancelled = true;
        if (worker.joinable()) worker.join();
        if (game) CloseHandle(game);
        if (body_font) DeleteObject(body_font);
        if (heading_font) DeleteObject(heading_font);
        if (page_brush) DeleteObject(page_brush);
    }

    void say(const std::wstring& text) {
        std::wstring lines;
        for (const wchar_t letter : text) {
            if (letter == L'\n' && (lines.empty() || lines.back() != L'\r')) lines += L'\r';
            lines += letter;
        }
        if (!settings.readable() && text != settings_error()) lines += std::wstring(L"\r\n") + settings_error();
        SetWindowTextW(status, lines.c_str());
        ShowScrollBar(status, SB_VERT, SendMessageW(status, EM_GETLINECOUNT, 0, 0) > status_lines);
    }
    void match_page() {
        COLORREF colour = GetSysColor(COLOR_3DFACE);
        if (HTHEME theme = OpenThemeData(tabs, L"TAB")) {
            // The theme's fill colour hint is not the colour it paints, so a pane is drawn and its middle read.
            RECT pane{0, 0, 64, 64};
            HDC screen = GetDC(tabs), drawn = CreateCompatibleDC(screen);
            HBITMAP bitmap = CreateCompatibleBitmap(screen, pane.right, pane.bottom);
            HGDIOBJ before = SelectObject(drawn, bitmap);
            if (SUCCEEDED(DrawThemeBackground(theme, drawn, TABP_PANE, 0, &pane, nullptr)))
                colour = GetPixel(drawn, pane.right / 2, pane.bottom / 2);
            SelectObject(drawn, before);
            DeleteObject(bitmap);
            DeleteDC(drawn);
            ReleaseDC(tabs, screen);
            CloseThemeData(theme);
        }
        if (page_brush) DeleteObject(page_brush);
        page_brush = CreateSolidBrush(colour);
    }
    const wchar_t* settings_error() const {
        return settings.readable()
            ? L"Cannot write settings.toml. Put the extracted build in a writable Games folder."
            : L"settings.toml is not valid TOML, so it is left as it is. Fix or delete it, then reopen the launcher.";
    }
    bool editable() const { return !busy && !game && settings.readable(); }

    HWND add(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style, DWORD extended,
             int x, int y, int width, int height, int id = 0, bool heading = false) {
        HWND handle = CreateWindowExW(extended, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        placed.push_back({handle, x, y, width, height, heading});
        return handle;
    }
    void lay_out() {
        const int dpi = static_cast<int>(GetDpiForWindow(window));
        const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        HFONT body = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HFONT heading = CreateFontW(-MulDiv(21, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        for (const auto& item : placed) {
            SendMessageW(item.window, WM_SETFONT, reinterpret_cast<WPARAM>(item.heading ? heading : body), TRUE);
            SetWindowPos(item.window, nullptr, scale(item.x), scale(item.y), scale(item.width), scale(item.height),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (body_font) DeleteObject(body_font);
        if (heading_font) DeleteObject(heading_font);
        body_font = body;
        heading_font = heading;
    }

    void show_languages() {
        discs = settings.language_discs();
        const LanguageList list = language_list(discs, regions, settings.language_disc());
        SendMessageW(language, CB_RESETCONTENT, 0, 0);
        for (const std::wstring& label : list.labels) SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(language, CB_SETCURSEL, list.selected, 0);
        SetWindowTextW(language_note, list.note.c_str());
        EnableWindow(remove_disc, editable() && list.selected > 0);
    }
    void show_settings() {
        const int page = static_cast<int>(SendMessageW(tabs, TCM_GETCURSEL, 0, 0));
        for (const auto& row : rows) {
            const Option& option = *row.option;
            const int value = settings.shown(option), highest = settings.highest(option);
            if (option.kind == Kind::toggle) {
                SendMessageW(row.control, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
            } else if (option.kind == Kind::choice) {
                SendMessageW(row.control, CB_SETCURSEL, static_cast<WPARAM>(value), 0);
            } else {
                const bool logarithmic = option.unit == Unit::thousandths;
                SendMessageW(row.control, TBM_SETRANGEMIN, FALSE, logarithmic ? 0 : option.lowest);
                SendMessageW(row.control, TBM_SETRANGEMAX, FALSE, logarithmic ? slider_steps : highest);
                SendMessageW(row.control, TBM_SETPOS, TRUE, to_position(option, value, highest));
                SetWindowTextW(row.value, value_text(option, value).c_str());
            }
            const bool on = editable() && settings.applies(option);
            const int shown = static_cast<int>(option.page) == page ? SW_SHOW : SW_HIDE;
            for (HWND part : {row.label, row.control, row.value}) {
                if (!part) continue;
                EnableWindow(part, on);
                ShowWindow(part, shown);
            }
        }
        SetWindowTextW(page_note, page_notes[std::clamp(page, 0, 3)]);
    }
    void controls() {
        EnableWindow(browse, !busy && !game);
        EnableWindow(play, !busy && !game && verified.profile);
        EnableWindow(cancel, busy && !closing);
        EnableWindow(auto_play, !busy && !game);
        for (HWND button : preset_buttons) EnableWindow(button, editable());
        for (HWND part : {language, add_disc, reset}) EnableWindow(part, editable());
        const std::wstring disc = verified.profile
            ? std::wstring(verified.profile->label) + L", verified."
            : L"Not installed. Browse to your USA disc image (SLUS-00224) or drop it on this window.";
        SetWindowTextW(disc_text, disc.c_str());
        show_languages();
        show_settings();
    }
    void saved(bool written, const std::wstring& done = {}) {
        if (!written) say(settings_error());
        else if (!done.empty()) say(done);
        controls();
    }

    void begin(Task task, fs::path source, bool launch) {
        if (worker.joinable()) worker.join();
        // The game reads the file, not what this window last saw or shows of it.
        if (launch) unwritten = !settings.complete() && settings.readable();
        Request request{task, std::move(source), launch, verified.data, settings.language_disc(), settings.language_discs()};
        if (task == Task::import || task == Task::verify) verified = {}; // release previous read locks before replacing data
        cancelled = false;
        busy = true;
        controls();
        SendMessageW(progress, PBM_SETPOS, 0, 0);
        if (task != Task::look)
            say(task == Task::import ? L"Preparing to import your image..."
                : task == Task::verify ? L"Checking installed game data..."
                : task == Task::take ? L"Looking at the image..." : L"Checking the language disc...");
        worker = std::thread([this, request = std::move(request)]() mutable {
            unsigned previous = 101;
            std::wstring previous_status;
            const auto report = [this, &previous, &previous_status](const std::wstring& text, unsigned percent) {
                if (previous == percent && previous_status == text) return;
                previous = percent;
                previous_status = text;
                auto update = std::make_unique<Update>(Update{text, percent});
                if (PostMessageW(window, progress_message, 0, reinterpret_cast<LPARAM>(update.get())))
                    update.release();
            };
            Discs discs;
            discs.import = [&](const fs::path& image) { return import_disc(image, root, cancelled, report); };
            discs.verify = [&](const fs::path& cue) { return verify_disc(cue, cancelled, report); };
            discs.check_package = [&](const VerifiedDisc& disc) { check_package(root, *disc.profile); };
            discs.look = [](const fs::path& image) { return region_disc(image); };
            discs.check = [](const fs::path& home, const fs::path& image) { return check_region_disc(home, image); };
            discs.report = report;
            auto outcome = std::make_unique<Outcome>(run(std::move(request), discs, cancelled));
            if (PostMessageW(window, finished_message, 0, reinterpret_cast<LPARAM>(outcome.get())))
                outcome.release();
        });
    }
    void take_image(const fs::path& image) {
        if (!busy && !game) begin(Task::take, image, SendMessageW(auto_play, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }
    void look() {
        if (!busy && !settings.language_discs().empty()) begin(Task::look, {}, false);
    }
    void launch() {
        try {
            game = start_game(root, verified);
            say(running_text(environment_overrides(), unwritten));
            SetTimer(window, 1, 500, nullptr);
        } catch (const std::exception& error) {
            say(error_text(error));
        }
        controls();
    }
};

// Controls on a tab page are the tab control's children, so their messages are passed on to the window.
LRESULT CALLBACK pass_to_window(HWND tab, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR) {
    if (message == WM_COMMAND || message == WM_HSCROLL || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN)
        return SendMessageW(GetParent(tab), message, wparam, lparam);
    return DefSubclassProc(tab, message, wparam, lparam);
}

void create_controls(App& app) {
    HWND window = app.window;
    app.add(window, L"STATIC", L"Disruptor Recompiled", SS_LEFT, 0, 28, 16, 624, 42, 0, true);

    app.add(window, L"STATIC", L"Disc", SS_LEFT, 0, 30, 72, 86, 20);
    app.disc_text = app.add(window, L"STATIC", L"", SS_LEFT, 0, 124, 72, 388, 36);
    app.browse = app.add(window, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 0, 524, 68, 126, 30, browse_id);
    app.auto_play = app.add(window, L"BUTTON", L"Start the game after importing",
                            BS_AUTOCHECKBOX | WS_TABSTOP, 0, 124, 110, 388, 22);
    SendMessageW(app.auto_play, BM_SETCHECK, BST_CHECKED, 0);

    app.add(window, L"STATIC", L"Language", SS_LEFT, 0, 30, 146, 86, 20);
    app.language = app.add(window, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0,
                           124, 142, 250, 220, language_id);
    app.add_disc = app.add(window, L"BUTTON", L"Add disc...", BS_PUSHBUTTON | WS_TABSTOP, 0,
                           384, 141, 128, 28, add_disc_id);
    app.remove_disc = app.add(window, L"BUTTON", L"Remove", BS_PUSHBUTTON | WS_TABSTOP, 0,
                              524, 141, 126, 28, remove_disc_id);
    app.language_note = app.add(window, L"STATIC", L"", SS_LEFT, 0, 124, 174, 526, 20);

    app.tabs = app.add(window, WC_TABCONTROLW, L"", WS_CLIPCHILDREN | WS_TABSTOP, WS_EX_CONTROLPARENT,
                       30, 202, 620, 268);
    SetWindowSubclass(app.tabs, pass_to_window, 1, 0);
    int index = 0;
    for (const wchar_t* label : page_labels()) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(label);
        SendMessageW(app.tabs, TCM_INSERTITEMW, static_cast<WPARAM>(index++), reinterpret_cast<LPARAM>(&item));
    }
    int rows_on_page[4] = {};
    index = 0;
    for (const auto& option : options()) {
        const int y = 38 + 28 * rows_on_page[static_cast<int>(option.page)]++;
        const int id = option_base + index++;
        Row row{&option};
        if (option.kind == Kind::toggle) {
            row.control = app.add(app.tabs, L"BUTTON", option.label, BS_AUTOCHECKBOX | WS_TABSTOP, 0,
                                  18, y, 560, 24, id);
        } else {
            row.label = app.add(app.tabs, L"STATIC", option.label, SS_LEFT, 0, 18, y + 4, 250, 20);
            if (option.kind == Kind::choice) {
                row.control = app.add(app.tabs, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0,
                                      280, y, 250, 240, id);
                for (const wchar_t* label : option.choices)
                    SendMessageW(row.control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
            } else {
                row.control = app.add(app.tabs, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0,
                                      274, y, 256, 26, id);
                row.value = app.add(app.tabs, L"STATIC", L"", SS_LEFT, 0, 538, y + 4, 70, 20);
            }
        }
        app.rows.push_back(row);
    }
    app.page_note = app.add(app.tabs, L"STATIC", L"", SS_LEFT, 0, 18, 238, 584, 20);

    app.add(window, L"STATIC", L"Presets", SS_LEFT, 0, 30, 486, 86, 20);
    int x = 124;
    index = 0;
    for (const auto& preset : presets()) {
        const int width = static_cast<int>(wcslen(preset.label)) * 7 + 44;
        app.preset_buttons.push_back(app.add(window, L"BUTTON", preset.label, BS_PUSHBUTTON | WS_TABSTOP, 0,
                                             x, 480, width, 30, preset_base + index++));
        x += width + 10;
    }
    app.reset = app.add(window, L"BUTTON", L"Reset settings", BS_PUSHBUTTON | WS_TABSTOP, 0,
                        524, 480, 126, 30, reset_id);

    app.status = app.add(window, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                         WS_EX_CLIENTEDGE, 30, 520, 620, 66);
    app.progress = app.add(window, PROGRESS_CLASSW, L"", 0, 0, 30, 592, 620, 12);
    app.play = app.add(window, L"BUTTON", L"Play game", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 30, 612, 150, 38, play_id);
    app.cancel = app.add(window, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 0, 190, 612, 100, 38, cancel_id);
    app.add(window, L"BUTTON", L"Saves folder", BS_PUSHBUTTON | WS_TABSTOP, 0, 394, 612, 126, 38, saves_id);
    app.add(window, L"BUTTON", L"Help", BS_PUSHBUTTON | WS_TABSTOP, 0, 530, 612, 120, 38, help_id);
}

void command(App& app, int id, int code) {
    HWND window = app.window;
    if ((id == browse_id || id == add_disc_id) && !app.busy && !app.game) {
        const bool adding = id == add_disc_id;
        const auto image = pick_image(window, adding ? L"Select a French, German or Japanese Disruptor disc image"
                                                     : L"Select your Disruptor disc image");
        if (!image) return;
        if (!adding) app.take_image(*image);
        else if (app.verified.profile) app.begin(Task::language, *image, false);
        else app.say(L"Install the USA disc first: it is the game, and the other disc gives it a language.");
    } else if ((id == play_id || id == IDOK) && !app.busy && !app.game && app.verified.profile) {
        const auto cue = app.verified.cue;
        app.begin(Task::verify, cue, true); // always verify again immediately before Play
    } else if ((id == cancel_id || id == IDCANCEL) && app.busy) {
        app.cancelled = true;
        app.say(L"Cancelling...");
    } else if (id == help_id) {
        // Windows players may have no Markdown file association.
        const auto guide = quote((app.root / "GETTING_STARTED.md").wstring());
        const auto opened = ShellExecuteW(window, L"open", L"notepad.exe", guide.c_str(), app.root.c_str(), SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(opened) <= 32)
            app.say(L"Open GETTING_STARTED.md in the build folder for setup and troubleshooting help.");
    } else if (id == saves_id) {
        ShellExecuteW(window, L"open", app.settings.saves_folder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (!app.editable()) {
        return;
    } else if (id == language_id && code == CBN_SELCHANGE) {
        const auto chosen = static_cast<size_t>(SendMessageW(app.language, CB_GETCURSEL, 0, 0));
        if (chosen == 0) {
            app.saved(app.settings.choose_language_disc({}), L"The game is in English.");
        } else if (chosen > app.discs.size()) {
            return;
        } else if (app.verified.profile) {
            app.begin(Task::language, from_utf8(app.discs[chosen - 1]), false);
        } else {
            app.say(L"Install the USA disc first: it is the game, and the other disc gives it a language.");
            app.controls();
        }
    } else if (id == remove_disc_id) {
        const auto chosen = static_cast<size_t>(SendMessageW(app.language, CB_GETCURSEL, 0, 0));
        if (chosen > 0 && chosen <= app.discs.size())
            app.saved(app.settings.forget_language_disc(app.discs[chosen - 1]),
                      L"The disc is off the list. Its file is left as it is.");
    } else if (id == reset_id) {
        const int answer = MessageBoxW(window,
            L"Put every setting on these tabs back to what a new installation has?\n"
            L"The language, the key bindings and your saves stay as they are.",
            L"Disruptor Launcher", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (answer == IDYES) app.saved(app.settings.reset(), L"The settings are those of a new installation again.");
    } else if (id >= preset_base && id < preset_base + static_cast<int>(presets().size())) {
        const Preset& preset = presets()[static_cast<size_t>(id - preset_base)];
        app.saved(app.settings.apply(preset), std::wstring(L"Preset applied: ") + preset.label + L".");
    } else if (id >= option_base && id < option_base + static_cast<int>(app.rows.size())) {
        const Row& row = app.rows[static_cast<size_t>(id - option_base)];
        if (row.option->kind == Kind::toggle && code == BN_CLICKED) {
            const bool checked = SendMessageW(row.control, BM_GETCHECK, 0, 0) == BST_CHECKED;
            app.saved(app.settings.set(*row.option, checked ? 1 : 0));
        } else if (row.option->kind == Kind::choice && code == CBN_SELCHANGE) {
            const int chosen = static_cast<int>(SendMessageW(row.control, CB_GETCURSEL, 0, 0));
            if (chosen >= 0) app.saved(app.settings.set(*row.option, chosen));
        }
    }
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetParent(reinterpret_cast<HWND>(lparam)) == app->tabs && app->page_brush
            ? app->page_brush : GetSysColorBrush(COLOR_WINDOW));
    case WM_THEMECHANGED:
        app->match_page();
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_CREATE: {
        create_controls(*app);
        app->match_page();
        app->lay_out();
        DragAcceptFiles(window, TRUE);
        DEVMODEW display{};
        display.dmSize = sizeof(display);
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &display))
            app->settings.set_display_rate(display.dmDisplayFrequency);
        app->settings.load();
        app->controls();
        app->say(app->settings.readable()
            ? L"No game data installed. Choose your disc image to get started.\n"
              L"It is copied and verified, and your original files are kept."
            : app->settings_error());
        try {
            auto installed = find_installed_disc(app->root);
            if (!installed.empty()) {
                bool legacy = installed != installed_cue(app->root, supported_discs().front());
                app->begin(legacy ? Task::import : Task::verify, installed, false);
            }
        } catch (const std::exception& error) { app->say(error_text(error)); }
        app->look();
        return 0;
    }
    case WM_COMMAND:
        command(*app, LOWORD(wparam), HIWORD(wparam));
        return 0;
    case WM_HSCROLL:
        for (const auto& row : app->rows) {
            if (row.control != reinterpret_cast<HWND>(lparam) || row.option->kind != Kind::slider) continue;
            const int highest = app->settings.highest(*row.option), before = app->settings.shown(*row.option);
            const int position = static_cast<int>(SendMessageW(row.control, TBM_GETPOS, 0, 0));
            const bool moved = position != to_position(*row.option, before, highest);
            const int value = moved ? from_position(*row.option, position, highest) : before;
            SetWindowTextW(row.value, value_text(*row.option, value).c_str());
            // One write when the slider is let go, not one for every step of a drag.
            if (LOWORD(wparam) == TB_ENDTRACK && app->editable() && moved)
                app->saved(app->settings.set(*row.option, value));
        }
        return 0;
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lparam)->hwndFrom == app->tabs &&
            reinterpret_cast<NMHDR*>(lparam)->code == TCN_SELCHANGE)
            app->show_settings();
        return 0;
    case WM_DROPFILES: {
        const auto drop = reinterpret_cast<HDROP>(wparam);
        std::wstring dropped(32768, L'\0');
        const UINT size = DragQueryFileW(drop, 0, dropped.data(), static_cast<UINT>(dropped.size()));
        DragFinish(drop);
        dropped.resize(size);
        if (size) app->take_image(fs::path(dropped));
        return 0;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        app->lay_out();
        return 0;
    }
    case progress_message: {
        std::unique_ptr<Update> update(reinterpret_cast<Update*>(lparam));
        if (!app->cancelled) app->say(update->status);
        SendMessageW(app->progress, PBM_SETPOS, update->percent, 0);
        return 0;
    }
    case finished_message: {
        std::unique_ptr<Outcome> outcome(reinterpret_cast<Outcome*>(lparam));
        if (app->worker.joinable()) app->worker.join();
        app->busy = false;
        if (app->closing) { DestroyWindow(window); return 0; }
        SendMessageW(app->progress, PBM_SETPOS, 0, 0);
        app->regions = std::move(outcome->regions);
        Step step = after(*outcome, app->cancelled);
        if (step.import_next) {
            app->begin(Task::import, outcome->image, outcome->launch);
            return 0;
        }
        if (step.disc == DiscAfter::cleared) app->verified = {};
        else if (step.disc == DiscAfter::taken) app->verified = std::move(outcome->disc);
        if (step.save_language && !app->settings.choose_language_disc(utf8(outcome->image))) step.status = app->settings_error();
        if (!step.status.empty()) app->say(step.status);
        if (step.launch) app->launch();
        app->controls();
        return 0;
    }
    case WM_TIMER:
        if (app->game) {
            // Asked of the handle and not of the code: a game may exit with 259, which reads as STILL_ACTIVE.
            if (WaitForSingleObject(app->game, 0) == WAIT_OBJECT_0) {
                DWORD code = 1;
                GetExitCodeProcess(app->game, &code);
                KillTimer(window, 1);
                CloseHandle(app->game);
                app->game = nullptr;
                app->settings.load(); // the in-game menu writes the same file
                if (code == 0) {
                    app->say(L"Game closed. Select Play game to start again.");
                } else {
                    wchar_t heading[160];
                    swprintf(heading, 160, L"The game exited with an error (code 0x%08lX). If a runtime DLL is missing, "
                             L"install the Visual C++ x64 Redistributable.", code);
                    const std::wstring tail = log_tail(app->root / "startup.log", log_lines);
                    app->say(tail.empty() ? std::wstring(heading) + L"\nSee GETTING_STARTED.md."
                                          : std::wstring(heading) + L"\nThe end of startup.log:\n" + tail);
                }
                app->controls();
                app->look();
            }
        }
        return 0;
    case WM_CLOSE:
        if (app->busy) {
            app->closing = true;
            app->cancelled = true;
            app->say(L"Finishing cancellation...");
            app->controls();
        } else DestroyWindow(window);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// These diagnostic modes use the same production checks as the GUI. They work
// with redirected stdout/stderr without opening a console window.
int diagnostic(int count, wchar_t** arguments) {
    if (count < 2) return -1;
    const std::wstring mode = arguments[1];
    if (mode != L"--verify" && mode != L"--import" && mode != L"--check")
        throw std::runtime_error("Usage: DisruptorLauncher.exe --verify IMAGE | --import IMAGE [--root FOLDER] | --check [--root FOLDER]");
    const bool needs_image = mode != L"--check";
    if (needs_image && count < 3) throw std::runtime_error("A disc image path is required.");
    fs::path root = executable_directory();
    const int option = needs_image ? 3 : 2;
    if (count == option + 2 && std::wstring(arguments[option]) == L"--root") root = fs::absolute(arguments[option + 1]);
    else if (count != option) throw std::runtime_error("Unexpected launcher arguments.");
    std::atomic_bool cancelled{false};
    VerifiedDisc verified;
    if (mode == L"--verify") verified = verify_disc(arguments[2], cancelled);
    else if (mode == L"--import") verified = import_disc(arguments[2], root, cancelled);
    else {
        const auto installed = find_installed_disc(root);
        if (installed.empty()) throw std::runtime_error("No supported disc image is installed.");
        verified = verify_disc(installed, cancelled);
        check_package(root, *verified.profile);
    }
    const std::string message = std::string("Verified ") + verified.profile->id + "\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
    return 0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    try {
        if (!arguments) throw std::runtime_error("Cannot read launcher arguments.");
        int result = diagnostic(count, arguments);
        LocalFree(arguments);
        arguments = nullptr;
        if (result >= 0) return result;
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        // The Language row names the disc and checks it: the game would let this variable replace it unchecked.
        SetEnvironmentVariableW(L"PSX_DISRUPTOR_LANGUAGE_DISC", nullptr);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS | ICC_TAB_CLASSES | ICC_BAR_CLASSES};
        InitCommonControlsEx(&controls);
        App app(executable_directory());
        WNDCLASSW cls{};
        cls.lpfnWndProc = window_proc;
        cls.hInstance = instance;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        cls.lpszClassName = L"DisruptorLauncher";
        if (!RegisterClassW(&cls)) throw std::runtime_error("Cannot create the launcher window.");
        const auto dpi = GetDpiForSystem();
        RECT bounds{0, 0, MulDiv(client_width, dpi, 96), MulDiv(client_height, dpi, 96)};
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
        AdjustWindowRectExForDpi(&bounds, style, FALSE, WS_EX_CONTROLPARENT, dpi);
        HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"Disruptor Launcher", style,
            CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
            nullptr, nullptr, instance, &app);
        if (!window) throw std::runtime_error("Cannot create the launcher window.");
        ShowWindow(window, show);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        if (count > 1) {
            DWORD written = 0;
            const std::string text = std::string(error.what()) + "\n";
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        } else MessageBoxW(nullptr, error_text(error).c_str(), L"Disruptor Launcher", MB_OK | MB_ICONERROR);
        if (arguments) LocalFree(arguments);
        return 1;
    }
}
