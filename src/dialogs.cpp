/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)
Native settings dialog - see dialogs.h.

Copyright (C) 2026 doublexyz, LGPL 2.1 or later (see README.md)
*/

#include "dialogs.h"
#include "auth.h"
#include "plugin_utils.h"
#include "restclient.h"

#include <sstream>

namespace ydisk {

const wchar_t* kSettingsWindowClass = L"YDiskCommanderSettings";

namespace {

/* Desired CLIENT area; the window size is derived from it with
   AdjustWindowRectEx() so the layout fits regardless of frame/caption metrics. */
const int kClientWidth = 570;
const int kClientHeight = 490;
const DWORD kWindowStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
const DWORD kWindowExStyle = WS_EX_DLGMODALFRAME;

struct SettingsState {
    HWND window;
    HWND parent;
    PluginConfig config;
    std::wstring config_path;
    SaveTokenFn save_token;
    HFONT font;
    bool closed;   /* the user closed the window while an operation was running */
    bool finished; /* the modal loop may end */
    bool saved;    /* the user pressed "Save" */
    std::string error;
};

/* only one modal settings window at a time */
SettingsState* g_state = NULL;

/* --------------------------------------------------------------- helpers */

std::wstring human_size(unsigned long long bytes)
{
    static const wchar_t* units[] = {L"bytes", L"KB", L"MB", L"GB", L"TB"};
    int unit = 0;
    double value = (double)bytes;

    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }

    wchar_t buffer[64] = {0};
    if (unit == 0)
        ::wsprintfW(buffer, L"%I64u %s", bytes, units[unit]);
    else
        ::wsprintfW(buffer, L"%.2f %s", value, units[unit]);

    return std::wstring(buffer);
}

void set_font(HWND control, HFONT font)
{
    ::SendMessageW(control, WM_SETFONT, (WPARAM)font, (LPARAM)TRUE);
}

HWND create_control(SettingsState* state, const wchar_t* class_name, const wchar_t* text,
                    DWORD style, int x, int y, int width, int height, int id)
{
    HWND control = ::CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style, x, y, width,
                                     height, state->window, (HMENU)(INT_PTR)id,
                                     ::GetModuleHandleW(NULL), NULL);
    if (control && state->font)
        set_font(control, state->font);
    return control;
}

void set_window_text(HWND window, int id, const std::wstring& text)
{
    HWND control = ::GetDlgItem(window, id);
    if (control)
        ::SetWindowTextW(control, text.c_str());
}

std::wstring get_window_text(HWND window, int id)
{
    HWND control = ::GetDlgItem(window, id);
    if (!control)
        return std::wstring();

    int length = ::GetWindowTextLengthW(control);
    if (length <= 0)
        return std::wstring();

    std::vector<wchar_t> buffer((size_t)length + 1, L'\0');
    ::GetWindowTextW(control, &buffer[0], length + 1);
    return std::wstring(&buffer[0], (size_t)length);
}

void set_status(SettingsState* state, const std::wstring& text)
{
    /* multiline EDIT controls only break lines on CRLF */
    std::wstring normalized;
    normalized.reserve(text.size() + 8);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r'))
            normalized += L'\r';
        normalized += text[i];
    }
    set_window_text(state->window, kIdStatus, normalized);
}

bool radio_checked(HWND window, int id)
{
    return ::SendDlgItemMessageW(window, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void check_radio(HWND window, int id, bool checked)
{
    ::SendDlgItemMessageW(window, id, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

std::string selected_token_method(HWND window)
{
    if (radio_checked(window, kIdRadioManual))
        return "manual";
    if (radio_checked(window, kIdRadioBrowser))
        return "browser";
    return "device";
}

std::string selected_save_type(HWND window)
{
    if (radio_checked(window, kIdRadioSaveConfig))
        return "config";
    if (radio_checked(window, kIdRadioSavePasswordManager))
        return "password_manager";
    return "dont_save";
}

/** Keeps the window alive while a long running operation is in progress. */
void pump_messages(HWND window, const bool* stop, int milliseconds)
{
    DWORD start = ::GetTickCount();

    for (;;) {
        MSG msg;
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                ::PostQuitMessage((int)msg.wParam);
                return;
            }
            if (!::IsDialogMessageW(window, &msg)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }

        if (stop && *stop)
            return;
        if ((int)(::GetTickCount() - start) >= milliseconds)
            return;
        ::Sleep(15);
    }
}

void set_edit_text(HWND window, int id, const std::wstring& text)
{
    set_window_text(window, id, text);
}

/* ----------------------------------------------------------- commands */

void on_get_token(SettingsState* state)
{
    std::string error;
    std::string token;

    const std::string client_id = state->config.client_id.empty() ? std::string(kDefaultClientId)
                                                                  : state->config.client_id;

    StatusFn status = [state](const std::wstring& text) {
        if (!state->closed)
            set_status(state, text);
    };
    WaitFn wait = [state](int milliseconds) {
        pump_messages(state->window, &state->closed, milliseconds);
    };
    CancelFn cancel = [state]() { return state->closed; };

    const std::string method = selected_token_method(state->window);
    if (method == "manual") {
        set_status(state, L"Enter the token into the \"OAuth token\" field, then press \"Test connection\".");
        ::SetFocus(::GetDlgItem(state->window, kIdEditToken));
        return;
    }

    set_status(state, L"Starting the authorization...");
    ::EnableWindow(::GetDlgItem(state->window, kIdButtonGetToken), FALSE);
    ::EnableWindow(::GetDlgItem(state->window, kIdButtonTest), FALSE);

    if (method == "browser")
        token = acquire_token_browser(client_id, status, wait, cancel, error);
    else
        token = acquire_token_device(client_id, status, wait, cancel, error);

    if (state->closed)
        return;

    ::EnableWindow(::GetDlgItem(state->window, kIdButtonGetToken), TRUE);
    ::EnableWindow(::GetDlgItem(state->window, kIdButtonTest), TRUE);

    if (token.empty()) {
        std::wstring message = L"The token could not be obtained.";
        if (!error.empty())
            message += L"\n" + from_utf8(error);
        if (method == "browser")
            message += L"\n\nYou can also use the \"Enter the token manually\" method.";
        set_status(state, message);
        return;
    }

    set_edit_text(state->window, kIdEditToken, from_utf8(token));
    set_status(state, L"The token was received.\nPress \"Test connection\" to verify it or \"Save\" to store the settings.");
}

void on_test_connection(SettingsState* state)
{
    std::string token = trim_string(to_utf8(get_window_text(state->window, kIdEditToken)));
    if (token.empty()) {
        set_status(state, L"Please enter an OAuth token first (or use \"Get token\").");
        return;
    }

    set_window_text(state->window, kIdStatus, L"Connecting to cloud-api.yandex.net ...");
    ::EnableWindow(::GetDlgItem(state->window, kIdButtonTest), FALSE);
    pump_messages(state->window, NULL, 50);

    std::wstring message;
    try {
        YdiskRestClient client;
        client.set_oauth_token(token);
        json11::Json info = client.get_disk_info();

        unsigned long long total = (unsigned long long)info["total_space"].number_value();
        unsigned long long used = (unsigned long long)info["used_space"].number_value();
        unsigned long long trash = (unsigned long long)info["trash_size"].number_value();

        std::wstring user;
        if (info["user"]["display_name"].is_string())
            user = from_utf8(info["user"]["display_name"].string_value());
        else if (info["user"]["login"].is_string())
            user = from_utf8(info["user"]["login"].string_value());

        message = L"Connection is OK.";
        if (!user.empty())
            message += L"\nAccount: " + user;
        message += L"\nTotal space: " + human_size(total);
        message += L"\nUsed space:  " + human_size(used);
        message += L"\nFree space:  " + human_size(total > used ? total - used : 0);
        message += L"\nTrash size:  " + human_size(trash);
    } catch (const rest_client_exception& exception) {
        message = L"Yandex Disk API error " + std::to_wstring(exception.get_status()) + L":\n" +
                  from_utf8(exception.get_message());
    } catch (const std::exception& exception) {
        message = L"Connection failed:\n" + from_utf8(exception.what());
    }

    if (!state->closed) {
        set_status(state, message);
        ::EnableWindow(::GetDlgItem(state->window, kIdButtonTest), TRUE);
    }
}

/** Writes the configuration; returns false when the file cannot be written. */
bool save_from_controls(SettingsState* state)
{
    state->config.get_token_method = selected_token_method(state->window);
    state->config.save_type = selected_save_type(state->window);
    state->config.oauth_token = trim_string(to_utf8(get_window_text(state->window, kIdEditToken)));

    if (state->config.save_type == "password_manager") {
        if (!state->save_token) {
            set_status(state, L"The host password manager is not available. Select another save method.");
            return false;
        }
        if (!state->config.oauth_token.empty()) {
            if (!state->save_token(state->config.oauth_token)) {
                set_status(state, L"Total Commander refused to store the token in its password manager.\n"
                                  L"Select \"Save into yandex_disk.ini\" or check the master password settings.");
                return false;
            }
        }
    }

    if (!save_config(state->config_path, state->config)) {
        set_status(state, L"Cannot write the configuration file:\n" + state->config_path + L"\n" +
                             from_utf8(format_win_error(::GetLastError())));
        return false;
    }

    return true;
}

/* ------------------------------------------------------- window creation */

void layout_controls(SettingsState* state)
{
    /* Group: OAuth token. WS_GROUP on the first radio starts a new auto-radio
       group, otherwise the radios of both sections would be linked together. */
    create_control(state, L"BUTTON", L"OAuth token", BS_GROUPBOX, 12, 8, 546, 152, kIdGroupToken);

    create_control(state, L"BUTTON", L"Browser + confirmation code (recommended)",
                   BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 24, 28, 522, 20, kIdRadioDevice);
    create_control(state, L"BUTTON", L"Browser + local callback (port 3359)",
                   BS_AUTORADIOBUTTON, 24, 50, 522, 20, kIdRadioBrowser);
    create_control(state, L"BUTTON", L"Enter the token manually", BS_AUTORADIOBUTTON, 24, 72, 522,
                   20, kIdRadioManual);

    create_control(state, L"STATIC", L"OAuth token:", SS_LEFT, 24, 100, 84, 20, kIdLabelToken);
    create_control(state, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER, 110, 98,
                   308, 24, kIdEditToken);
    create_control(state, L"BUTTON", L"Get token", BS_PUSHBUTTON | WS_TABSTOP, 426, 97, 112, 26,
                   kIdButtonGetToken);

    create_control(state, L"STATIC",
                   L"The token is required for accessing the disk - press \"Get token\" when it is empty.",
                   SS_LEFT, 24, 128, 524, 22, kIdLabelHint);

    /* Group: save method */
    create_control(state, L"BUTTON", L"Save method", BS_GROUPBOX, 12, 168, 546, 96, kIdGroupSave);
    create_control(state, L"BUTTON", L"Do not save (authorize on every start)",
                   BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 24, 188, 522, 20, kIdRadioSaveNone);
    create_control(state, L"BUTTON", L"Save into yandex_disk.ini (encrypted with DPAPI)",
                   BS_AUTORADIOBUTTON, 24, 210, 522, 20, kIdRadioSaveConfig);
    create_control(state, L"BUTTON", L"Save in the Total Commander password manager",
                   BS_AUTORADIOBUTTON, 24, 232, 522, 20, kIdRadioSavePasswordManager);

    /* status / log */
    create_control(state, L"EDIT", L"", ES_LEFT | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                                             WS_VSCROLL | WS_BORDER, 12, 272, 546, 168, kIdStatus);

    /* buttons - keep the caption short: a long one is clipped from both sides */
    create_control(state, L"BUTTON", L"Test connection", BS_PUSHBUTTON | WS_TABSTOP, 12, 450, 168,
                   28, kIdButtonTest);
    create_control(state, L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, 368, 450, 92, 28, IDOK);
    create_control(state, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 466, 450, 92, 28,
                   IDCANCEL);
}

void select_initial_values(SettingsState* state)
{
    HWND window = state->window;

    const std::string method = state->config.get_token_method;
    check_radio(window, kIdRadioDevice, method == "device");
    check_radio(window, kIdRadioBrowser, method == "browser");
    check_radio(window, kIdRadioManual, method == "manual");
    if (!radio_checked(window, kIdRadioDevice) && !radio_checked(window, kIdRadioBrowser) &&
        !radio_checked(window, kIdRadioManual))
        check_radio(window, kIdRadioDevice, true);

    const std::string save_type = state->config.save_type;
    check_radio(window, kIdRadioSaveNone, save_type == "dont_save");
    check_radio(window, kIdRadioSaveConfig, save_type == "config");
    check_radio(window, kIdRadioSavePasswordManager, save_type == "password_manager");
    if (!radio_checked(window, kIdRadioSaveNone) && !radio_checked(window, kIdRadioSaveConfig) &&
        !radio_checked(window, kIdRadioSavePasswordManager))
        check_radio(window, kIdRadioSaveNone, true);

    if (!state->config.oauth_token.empty())
        set_edit_text(window, kIdEditToken, from_utf8(state->config.oauth_token));

    std::wstring status = L"Configure the access to Yandex Disk.";
    if (state->config.oauth_token.empty())
        status += L"\nNo token is stored yet - press \"Get token\" to authorize the plugin.";
    set_status(state, status);
}

void center_window(HWND window, HWND parent)
{
    RECT window_rect;
    if (!::GetWindowRect(window, &window_rect))
        return;

    int x = 0, y = 0;
    RECT parent_rect;
    if (parent && ::GetWindowRect(parent, &parent_rect)) {
        x = parent_rect.left +
            ((parent_rect.right - parent_rect.left) - (window_rect.right - window_rect.left)) / 2;
        y = parent_rect.top +
            ((parent_rect.bottom - parent_rect.top) - (window_rect.bottom - window_rect.top)) / 2;
    } else {
        RECT work_area;
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
        x = work_area.left +
            ((work_area.right - work_area.left) - (window_rect.right - window_rect.left)) / 2;
        y = work_area.top +
            ((work_area.bottom - work_area.top) - (window_rect.bottom - window_rect.top)) / 2;
    }

    ::SetWindowPos(window, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    SettingsState* state = g_state;
    if (!state)
        return ::DefWindowProcW(window, message, wparam, lparam);

    switch (message) {
    case WM_CREATE:
        state->window = window;
        layout_controls(state);
        select_initial_values(state);
        return 0;

    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        if (id == kIdButtonGetToken && HIWORD(wparam) == BN_CLICKED) {
            on_get_token(state);
            return 0;
        }
        if (id == kIdButtonTest && HIWORD(wparam) == BN_CLICKED) {
            on_test_connection(state);
            return 0;
        }
        if (id == IDOK) {
            if (save_from_controls(state)) {
                state->saved = true;
                state->finished = true;
                ::PostMessageW(window, WM_NULL, 0, 0);
            }
            return 0;
        }
        if (id == IDCANCEL) {
            state->saved = false;
            state->finished = true;
            ::PostMessageW(window, WM_NULL, 0, 0);
            return 0;
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        /* keep the sizing grip from hiding the bottom row of buttons */
        MINMAXINFO* info = (MINMAXINFO*)lparam;
        RECT rect = {0, 0, kClientWidth, kClientHeight};
        ::AdjustWindowRectEx(&rect, (DWORD)::GetWindowLongPtrW(window, GWL_STYLE), FALSE,
                             (DWORD)::GetWindowLongPtrW(window, GWL_EXSTYLE));
        info->ptMinTrackSize.x = rect.right - rect.left;
        info->ptMinTrackSize.y = rect.bottom - rect.top;
        return 0;
    }

    case WM_CLOSE:
        /* the window itself is destroyed by show_settings_dialog() */
        state->closed = true;
        state->finished = true;
        ::PostMessageW(window, WM_NULL, 0, 0);
        return 0;

    default:
        break;
    }

    return ::DefWindowProcW(window, message, wparam, lparam);
}

bool register_window_class()
{
    static bool registered = false;
    if (registered)
        return true;

    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_DBLCLKS;
    window_class.lpfnWndProc = settings_proc;
    window_class.hInstance = ::GetModuleHandleW(NULL);
    window_class.hCursor = ::LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    window_class.lpszClassName = kSettingsWindowClass;
    window_class.hIcon = (HICON)::LoadImageW(NULL, IDI_APPLICATION, IMAGE_ICON, 0, 0, LR_SHARED);

    registered = (::RegisterClassExW(&window_class) != 0);
    return registered;
}

HFONT create_ui_font()
{
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);

    if (::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        return ::CreateFontIndirectW(&metrics.lfMessageFont);

    return (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
}

} /* anonymous namespace */

/* --------------------------------------------------------------- public */

bool show_settings_dialog(HWND parent, const std::wstring& config_path, const SaveTokenFn& save_token,
                          std::string& error)
{
    error.clear();

    if (g_state)
        return false; /* already open */

    if (!register_window_class()) {
        error = "Cannot register the settings window class: " + format_win_error(::GetLastError());
        return false;
    }

    SettingsState state = {};
    state.parent = parent;
    state.config_path = config_path;
    state.save_token = save_token;
    state.font = create_ui_font();
    read_config(config_path, state.config); /* unreadable/invalid -> built-in defaults */

    g_state = &state;

    /* size the window so that the CLIENT area is exactly kClientWidth x kClientHeight -
       with a plain pixel height the caption/frame ate into the layout and the
       bottom buttons ended up clipped */
    RECT rect = {0, 0, kClientWidth, kClientHeight};
    ::AdjustWindowRectEx(&rect, kWindowStyle, FALSE, kWindowExStyle);

    HWND window = ::CreateWindowExW(kWindowExStyle, kSettingsWindowClass,
                                    L"Yandex Disk - account settings", kWindowStyle,
                                    CW_USEDEFAULT, CW_USEDEFAULT,
                                    rect.right - rect.left, rect.bottom - rect.top, parent, NULL,
                                    ::GetModuleHandleW(NULL), NULL);
    if (!window) {
        error = "Cannot create the settings window: " + format_win_error(::GetLastError());
        g_state = NULL;
        if (state.font)
            ::DeleteObject(state.font);
        return false;
    }

    center_window(window, parent);
    if (parent)
        ::EnableWindow(parent, FALSE);

    ::ShowWindow(window, SW_SHOW);
    ::SetForegroundWindow(window);

    /* modal loop */
    MSG message;
    while (!state.finished) {
        BOOL result = ::GetMessageW(&message, NULL, 0, 0);
        if (result <= 0)
            break;
        if (!::IsDialogMessageW(window, &message)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }

    ::DestroyWindow(window);
    if (parent)
        ::EnableWindow(parent, TRUE);

    if (state.font)
        ::DeleteObject(state.font);

    g_state = NULL;
    return state.saved;
}

} /* namespace ydisk */

