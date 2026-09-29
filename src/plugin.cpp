/*
 * Yandex Disk WFX plugin for Total Commander / Double Commander (Windows).
 * The Unicode WFX entry points are implemented here; ANSI entry points are
 * deliberately kept as small conversion wrappers.
 *
 * Copyright (C) 2019 Ivanenko Danil - original Linux version (library.cpp)
 * Copyright (C) 2026 doublexyz - Windows port, LGPL 2.1 or later (see README.md)
 */

#include "wfxplugin.h"
#include "auth.h"
#include "dialogs.h"
#include "plugin_utils.h"
#include "restclient.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

using namespace ydisk;

namespace {

const char* kPluginName = "Yandex Disk";
const size_t kMaxTokenLength = 4096;

int g_plugin_number = 0;
int g_crypto_number = 0;
std::wstring g_config_path;
std::string g_oauth_token;
thread_local bool g_folder_operation = false;

tProgressProc g_progress = NULL;
tProgressProcW g_progress_w = NULL;
tLogProc g_log = NULL;
tLogProcW g_log_w = NULL;
tRequestProc g_request = NULL;
tRequestProcW g_request_w = NULL;
tCryptProc g_crypt = NULL;
tCryptProcW g_crypt_w = NULL;

struct FindState {
    std::vector<WIN32_FIND_DATAW> items;
    size_t index;
    FindState() : index(0) {}
};

void copy_wide(WCHAR* target, size_t capacity, const std::wstring& value)
{
    if (!target || capacity == 0)
        return;
    size_t count = std::min(capacity - 1, value.size());
    if (count != 0)
        std::memcpy(target, value.data(), count * sizeof(WCHAR));
    target[count] = 0;
}

void copy_ansi(char* target, size_t capacity, const std::string& value)
{
    if (!target || capacity == 0)
        return;
    size_t count = std::min(capacity - 1, value.size());
    if (count != 0)
        std::memcpy(target, value.data(), count);
    target[count] = 0;
}

void report_error(const std::wstring& message)
{
    if (g_request_w) {
        std::wstring title = L"Yandex Disk";
        g_request_w(g_plugin_number, RT_MsgOK, &title[0], const_cast<WCHAR*>(message.c_str()), NULL, 0);
    } else if (g_request) {
        std::string title = kPluginName;
        std::string text = utf8_to_ansi(to_utf8(message));
        g_request(g_plugin_number, RT_MsgOK, &title[0], text.empty() ? NULL : &text[0], NULL, 0);
    }
}

void report_exception(const std::exception& exception)
{
    report_error(from_utf8(exception.what()));
}

bool save_in_password_manager(const std::string& token)
{
    if (token.empty())
        return true;

    if (g_crypt_w) {
        WCHAR connection[] = L"Yandex Disk";
        std::vector<WCHAR> password(token.size() * 2 + 32, 0);
        std::wstring wide = from_utf8(token);
        copy_wide(&password[0], password.size(), wide);
        return g_crypt_w(g_plugin_number, g_crypto_number, FS_CRYPT_SAVE_PASSWORD, connection,
                         &password[0], (int)password.size()) == 0;
    }
    if (g_crypt) {
        char connection[] = "Yandex Disk";
        std::string password = utf8_to_ansi(token);
        password.resize(password.size() + 1);
        return g_crypt(g_plugin_number, g_crypto_number, FS_CRYPT_SAVE_PASSWORD, connection,
                       &password[0], (int)password.size()) == 0;
    }
    return false;
}

std::string load_from_password_manager()
{
    if (g_crypt_w) {
        WCHAR connection[] = L"Yandex Disk";
        std::vector<WCHAR> password(kMaxTokenLength, 0);
        int result = g_crypt_w(g_plugin_number, g_crypto_number, FS_CRYPT_LOAD_PASSWORD_NO_UI,
                               connection, &password[0], (int)password.size());
        if (result == 0)
            return to_utf8(&password[0]);
    } else if (g_crypt) {
        char connection[] = "Yandex Disk";
        std::vector<char> password(kMaxTokenLength, 0);
        int result = g_crypt(g_plugin_number, g_crypto_number, FS_CRYPT_LOAD_PASSWORD_NO_UI,
                             connection, &password[0], (int)password.size());
        if (result == 0)
            return from_ansi(&password[0]).empty() ? std::string() :
                   to_utf8(from_ansi(&password[0]));
    }
    return std::string();
}

bool ensure_token()
{
    if (!g_oauth_token.empty())
        return true;

    PluginConfig config;
    if (!read_config(g_config_path, config))
        return false;

    if (config.save_type == "password_manager")
        g_oauth_token = load_from_password_manager();
    else if (config.save_type == "config")
        g_oauth_token = config.oauth_token;

    if (!g_oauth_token.empty()) {
        g_oauth_token = trim_string(g_oauth_token);
        return !g_oauth_token.empty();
    }
    return false;
}

bool report_missing_token()
{
    report_error(L"No OAuth token is configured. Open the plugin properties and authorize Yandex Disk.");
    return false;
}

WIN32_FIND_DATAW make_find_data(const json11::Json& item)
{
    WIN32_FIND_DATAW data = {};
    std::wstring name = item["name"].is_string() ? from_utf8(item["name"].string_value()) : L"";
    copy_wide(data.cFileName, MAX_PATH, name);

    const bool directory = item["type"].is_string() && item["type"].string_value() == "dir";
    data.dwFileAttributes = directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (item["size"].is_number() && !directory) {
        unsigned long long size = (unsigned long long)item["size"].number_value();
        data.nFileSizeLow = (DWORD)(size & 0xffffffffu);
        data.nFileSizeHigh = (DWORD)(size >> 32);
    }
    if (item["created"].is_string())
        data.ftCreationTime = parse_iso_time(item["created"].string_value());
    if (item["modified"].is_string())
        data.ftLastWriteTime = parse_iso_time(item["modified"].string_value());
    data.ftLastAccessTime = data.ftLastWriteTime;
    return data;
}

bool fill_listing(const RemotePath& path, FindState& state)
{
    YdiskRestClient client;
    client.set_oauth_token(g_oauth_token);
    json11::Json result;
    try {
        result = client.get_resources(path.api_path, path.trash);
    } catch (const rest_client_exception& exception) {
        /* the API answers "404 Resource not found" for trash:/ while the trash is
           EMPTY - present it as an empty folder instead of an error. Without this
           the TC command "count the size of all folders" fails in the disk root,
           because it recurses into the virtual ".Trash" folder too. */
        if (path.trash && exception.get_status() == 404)
            return false;
        throw;
    }
    json11::Json embedded = result["_embedded"];
    if (!embedded.is_object() || !embedded["items"].is_array())
        throw std::runtime_error("The Yandex Disk API returned an invalid folder listing");

    int offset = 0;
    for (;;) {
        const std::vector<json11::Json>& items = embedded["items"].array_items();
        for (size_t i = 0; i < items.size(); ++i)
            state.items.push_back(make_find_data(items[i]));
        offset += (int)items.size();
        if (items.empty() || !embedded["total"].is_number() || offset >= embedded["total"].int_value())
            break;
        result = client.get_resources(path.api_path, path.trash, 1000, offset);
        embedded = result["_embedded"];
        if (!embedded.is_object() || !embedded["items"].is_array())
            throw std::runtime_error("The Yandex Disk API returned an invalid folder listing");
    }

    if (!path.trash && path.local == L"/") {
        WIN32_FIND_DATAW trash = {};
        copy_wide(trash.cFileName, MAX_PATH, L".Trash");
        trash.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        state.items.push_back(trash);
    }
    return !state.items.empty();
}

bool progress_update(unsigned long long done, unsigned long long total,
                     const std::wstring& source, const std::wstring& target)
{
    int percent = total == 0 ? 0 : (int)((done * 100) / total);
    if (percent > 100)
        percent = 100;
    if (g_progress_w) {
        std::vector<WCHAR> source_buffer(source.begin(), source.end());
        std::vector<WCHAR> target_buffer(target.begin(), target.end());
        source_buffer.push_back(0);
        target_buffer.push_back(0);
        return g_progress_w(g_plugin_number, &source_buffer[0], &target_buffer[0], percent) == 0;
    }
    if (g_progress) {
        std::string source_text = utf8_to_ansi(to_utf8(source));
        std::string target_text = utf8_to_ansi(to_utf8(target));
        return g_progress(g_plugin_number, source_text.empty() ? NULL : &source_text[0],
                          target_text.empty() ? NULL : &target_text[0], percent) == 0;
    }
    return true;
}

ydisk::ProgressFn transfer_progress(const std::wstring& source, const std::wstring& target)
{
    return [source, target](unsigned long long done, unsigned long long total) {
        return progress_update(done, total, source, target);
    };
}

bool local_overwrite_allowed(const std::wstring& path, int flags)
{
    return !file_exists(path) || (flags & FS_COPYFLAGS_OVERWRITE) != 0;
}

int operation_error(const std::exception& exception, int code)
{
    report_exception(exception);
    return code;
}

template <typename T>
T* as_find_state(HANDLE handle)
{
    if (handle == INVALID_HANDLE_VALUE || handle == NULL)
        return NULL;
    return reinterpret_cast<T*>(handle);
}

} /* anonymous namespace */

YDISK_EXPORT void DCPCALL FsGetDefRootName(char* name, int maxlen)
{
    copy_ansi(name, maxlen > 0 ? (size_t)maxlen : 0, kPluginName);
}

YDISK_EXPORT void DCPCALL FsSetDefaultParams(FsDefaultParamStruct* params)
{
    if (!params)
        return;
    g_config_path = config_path_from_ini(params->DefaultIniName);
}

YDISK_EXPORT int DCPCALL FsInitW(int plugin_number, tProgressProcW progress, tLogProcW log,
                                 tRequestProcW request)
{
    g_plugin_number = plugin_number;
    g_progress_w = progress;
    g_log_w = log;
    g_request_w = request;
    g_oauth_token.clear();
    return 0;
}

YDISK_EXPORT int DCPCALL FsInit(int plugin_number, tProgressProc progress, tLogProc log,
                                tRequestProc request)
{
    g_plugin_number = plugin_number;
    g_progress = progress;
    g_log = log;
    g_request = request;
    g_oauth_token.clear();
    return 0;
}

YDISK_EXPORT void DCPCALL FsSetCryptCallbackW(tCryptProcW crypt, int crypto_number, int)
{
    g_crypt_w = crypt;
    g_crypto_number = crypto_number;
}

YDISK_EXPORT void DCPCALL FsSetCryptCallback(tCryptProc crypt, int crypto_number, int)
{
    g_crypt = crypt;
    g_crypto_number = crypto_number;
}

YDISK_EXPORT HANDLE DCPCALL FsFindFirstW(WCHAR* path_text, WIN32_FIND_DATAW* find_data)
{
    if (!path_text || !find_data || g_folder_operation)
        return INVALID_HANDLE_VALUE;
    if (!ensure_token()) {
        if (!g_folder_operation)
            report_missing_token();
        return INVALID_HANDLE_VALUE;
    }

    try {
        RemotePath path = parse_remote_path(path_text);
        std::unique_ptr<FindState> state(new FindState());
        if (!fill_listing(path, *state))
            return INVALID_HANDLE_VALUE;
        *find_data = state->items[0];
        state->index = 0;
        return reinterpret_cast<HANDLE>(state.release());
    } catch (const std::exception& exception) {
        report_exception(exception);
        return INVALID_HANDLE_VALUE;
    }
}

YDISK_EXPORT BOOL DCPCALL FsFindNextW(HANDLE handle, WIN32_FIND_DATAW* find_data)
{
    FindState* state = as_find_state<FindState>(handle);
    if (!state || !find_data || state->index + 1 >= state->items.size())
        return FALSE;
    *find_data = state->items[++state->index];
    return TRUE;
}

YDISK_EXPORT int DCPCALL FsFindClose(HANDLE handle)
{
    delete as_find_state<FindState>(handle);
    return 0;
}

YDISK_EXPORT BOOL DCPCALL FsMkDirW(WCHAR* path_text)
{
    if (!path_text)
        return FALSE;
    if (!ensure_token()) {
        report_missing_token();
        return FALSE;
    }
    try {
        RemotePath path = parse_remote_path(path_text);
        if (path.trash || path.local == L"/")
            return FALSE;
        YdiskRestClient client;
        client.set_oauth_token(g_oauth_token);
        client.make_folder(path.api_path);
        return TRUE;
    } catch (const std::exception& exception) {
        report_exception(exception);
        return FALSE;
    }
}

YDISK_EXPORT int DCPCALL FsGetFileW(WCHAR* remote_name, WCHAR* local_name, int copy_flags,
                                    RemoteInfoStruct*)
{
    if (!remote_name || !local_name)
        return FS_FILE_NOTFOUND;
    if (!ensure_token()) {
        report_missing_token();
        return FS_FILE_NOTFOUND;
    }
    if ((copy_flags & FS_COPYFLAGS_RESUME) != 0)
        return FS_FILE_NOTSUPPORTED;
    std::wstring local(local_name);
    if (!local_overwrite_allowed(local, copy_flags))
        return FS_FILE_EXISTS;
    try {
        RemotePath remote = parse_remote_path(remote_name);
        YdiskRestClient client;
        client.set_oauth_token(g_oauth_token);
        client.download_file(remote.api_path, local, transfer_progress(remote_name, local));
        return FS_FILE_OK;
    } catch (const user_abort&) {
        return FS_FILE_USERABORT;
    } catch (const std::exception& exception) {
        return operation_error(exception, FS_FILE_WRITEERROR);
    }
}

YDISK_EXPORT int DCPCALL FsPutFileW(WCHAR* local_name, WCHAR* remote_name, int copy_flags)
{
    if (!local_name || !remote_name)
        return FS_FILE_NOTFOUND;
    if (!ensure_token()) {
        report_missing_token();
        return FS_FILE_WRITEERROR;
    }
    if (!file_exists(local_name))
        return FS_FILE_NOTFOUND;
    try {
        RemotePath remote = parse_remote_path(remote_name);
        YdiskRestClient client;
        client.set_oauth_token(g_oauth_token);
        client.upload_file(remote.api_path, local_name, (copy_flags & FS_COPYFLAGS_OVERWRITE) != 0,
                           transfer_progress(local_name, remote_name));
        return FS_FILE_OK;
    } catch (const user_abort&) {
        return FS_FILE_USERABORT;
    } catch (const std::exception& exception) {
        return operation_error(exception, FS_FILE_READERROR);
    }
}

YDISK_EXPORT int DCPCALL FsRenMovFileW(WCHAR* old_name, WCHAR* new_name, BOOL move,
                                       BOOL overwrite, RemoteInfoStruct*)
{
    if (!old_name || !new_name)
        return FS_FILE_NOTFOUND;
    if (!ensure_token()) {
        report_missing_token();
        return FS_FILE_NOTFOUND;
    }
    try {
        RemotePath old_path = parse_remote_path(old_name);
        RemotePath new_path = parse_remote_path(new_name);
        YdiskRestClient client;
        client.set_oauth_token(g_oauth_token);
        if (move)
            client.move(old_path.api_path, new_path.api_path, overwrite != FALSE);
        else
            client.copy(old_path.api_path, new_path.api_path, overwrite != FALSE);
        return FS_FILE_OK;
    } catch (const std::exception& exception) {
        return operation_error(exception, FS_FILE_WRITEERROR);
    }
}

YDISK_EXPORT BOOL DCPCALL FsDeleteFileW(WCHAR* remote_name)
{
    if (!remote_name)
        return FALSE;
    if (!ensure_token()) {
        report_missing_token();
        return FALSE;
    }
    try {
        RemotePath path = parse_remote_path(remote_name);
        if (path.local == L"/" || path.local == L"/.Trash")
            return FALSE;
        YdiskRestClient client;
        client.set_oauth_token(g_oauth_token);
        if (path.trash)
            client.delete_from_trash(path.api_path);
        else
            client.remove_resource(path.api_path);
        return TRUE;
    } catch (const std::exception& exception) {
        report_exception(exception);
        return FALSE;
    }
}

YDISK_EXPORT BOOL DCPCALL FsRemoveDirW(WCHAR* remote_name)
{
    return FsDeleteFileW(remote_name);
}

YDISK_EXPORT BOOL DCPCALL FsDisconnectW(WCHAR*)
{
    return TRUE;
}

YDISK_EXPORT BOOL DCPCALL FsSetAttrW(WCHAR*, int)
{
    return FALSE;
}

YDISK_EXPORT BOOL DCPCALL FsSetTimeW(WCHAR*, FILETIME*, FILETIME*, FILETIME*)
{
    return FALSE;
}

YDISK_EXPORT int DCPCALL FsExecuteFileW(HWND parent, WCHAR* remote_name, WCHAR* verb)
{
    if (!verb)
        return FS_EXEC_ERROR;
    std::wstring command(verb);
    try {
        if (command == L"properties" && (!remote_name || parse_remote_path(remote_name).local == L"/")) {
            std::string error;
            SaveTokenFn save_token;
            if (g_crypt_w || g_crypt)
                save_token = [](const std::string& token) { return save_in_password_manager(token); };
            show_settings_dialog(parent, g_config_path, save_token, error);
            if (!error.empty())
                report_error(from_utf8(error));
            g_oauth_token.clear();
            return FS_EXEC_OK;
        }

        const std::wstring prefix = L"quote ";
        if (command.compare(0, prefix.size(), prefix) != 0)
            return FS_EXEC_OK;
        std::wstring arguments = command.substr(prefix.size());
        if (arguments.compare(0, 9, L"download ") == 0 && remote_name) {
            std::wstring rest = arguments.substr(9);
            size_t separator = rest.find(L' ');
            std::wstring url = separator == std::wstring::npos ? rest : rest.substr(0, separator);
            std::wstring name = separator == std::wstring::npos ? L"" : rest.substr(separator + 1);
            if (url.empty())
                return FS_EXEC_ERROR;
            RemotePath directory = parse_remote_path(remote_name);
            if (name.empty()) {
                size_t slash = url.find_last_of(L"/\\");
                name = slash == std::wstring::npos ? L"download" : url.substr(slash + 1);
            }
            std::wstring destination = join_plugin_path(directory.local, name);
            YdiskRestClient client;
            if (!ensure_token()) {
                report_missing_token();
                return FS_EXEC_ERROR;
            }
            client.set_oauth_token(g_oauth_token);
            client.save_from_url(to_utf8(url), parse_remote_path(destination).api_path);
            return FS_EXEC_OK;
        }
        if (arguments == L"trash clean") {
            if (g_request_w) {
                WCHAR title[] = L"Yandex Disk";
                WCHAR text[] = L"Remove all files from the trash?";
                if (!g_request_w(g_plugin_number, RT_MsgOKCancel, title, text, NULL, 0))
                    return FS_EXEC_OK;
            }
            if (!ensure_token()) {
                report_missing_token();
                return FS_EXEC_ERROR;
            }
            YdiskRestClient client;
            client.set_oauth_token(g_oauth_token);
            client.clean_trash();
            return FS_EXEC_OK;
        }
        if (arguments == L"trash" && remote_name) {
            std::wstring link = L"/.Trash";
            copy_wide(remote_name, YDISK_MAX_REMOTE_PATH, link);
            return FS_EXEC_SYMLINK;
        }
        return FS_EXEC_ERROR;
    } catch (const std::exception& exception) {
        report_exception(exception);
        return FS_EXEC_ERROR;
    }
}

YDISK_EXPORT void DCPCALL FsStatusInfoW(WCHAR*, int start_end, int operation)
{
    if (operation == FS_STATUS_OP_DELETE || operation == FS_STATUS_OP_RENMOV_MULTI)
        g_folder_operation = (start_end == FS_STATUS_START);
}

YDISK_EXPORT BOOL DCPCALL FsGetBackgroundFlags(void)
{
    return 0; /* global callback state is not safe for concurrent background transfers */
}

YDISK_EXPORT int DCPCALL FsExtractCustomIconW(WCHAR*, int, HICON*)
{
    return FS_ICON_USEDEFAULT;
}

/* ------------------------------- ANSI wrappers ------------------------- */

YDISK_EXPORT HANDLE DCPCALL FsFindFirst(char* path, WIN32_FIND_DATAA* data)
{
    std::wstring wide = from_ansi(path);
    WIN32_FIND_DATAW wide_data = {};
    HANDLE handle = FsFindFirstW(wide.empty() ? NULL : &wide[0], &wide_data);
    if (handle != INVALID_HANDLE_VALUE && data) {
        *data = {};
        copy_ansi(data->cFileName, MAX_PATH, utf8_to_ansi(to_utf8(wide_data.cFileName)));
        data->dwFileAttributes = wide_data.dwFileAttributes;
        data->ftCreationTime = wide_data.ftCreationTime;
        data->ftLastAccessTime = wide_data.ftLastAccessTime;
        data->ftLastWriteTime = wide_data.ftLastWriteTime;
        data->nFileSizeHigh = wide_data.nFileSizeHigh;
        data->nFileSizeLow = wide_data.nFileSizeLow;
    }
    return handle;
}

YDISK_EXPORT BOOL DCPCALL FsFindNext(HANDLE handle, WIN32_FIND_DATAA* data)
{
    WIN32_FIND_DATAW wide_data = {};
    if (!data || !FsFindNextW(handle, &wide_data))
        return FALSE;
    *data = {};
    copy_ansi(data->cFileName, MAX_PATH, utf8_to_ansi(to_utf8(wide_data.cFileName)));
    data->dwFileAttributes = wide_data.dwFileAttributes;
    data->ftCreationTime = wide_data.ftCreationTime;
    data->ftLastAccessTime = wide_data.ftLastAccessTime;
    data->ftLastWriteTime = wide_data.ftLastWriteTime;
    data->nFileSizeHigh = wide_data.nFileSizeHigh;
    data->nFileSizeLow = wide_data.nFileSizeLow;
    return TRUE;
}

YDISK_EXPORT BOOL DCPCALL FsMkDir(char* path)
{
    std::wstring wide = from_ansi(path);
    return FsMkDirW(wide.empty() ? NULL : &wide[0]);
}

YDISK_EXPORT int DCPCALL FsGetFile(char* remote_name, char* local_name, int flags, RemoteInfoStruct* info)
{
    std::wstring remote = from_ansi(remote_name), local = from_ansi(local_name);
    return FsGetFileW(remote.empty() ? NULL : &remote[0], local.empty() ? NULL : &local[0], flags, info);
}

YDISK_EXPORT int DCPCALL FsPutFile(char* local_name, char* remote_name, int flags)
{
    std::wstring local = from_ansi(local_name), remote = from_ansi(remote_name);
    return FsPutFileW(local.empty() ? NULL : &local[0], remote.empty() ? NULL : &remote[0], flags);
}

YDISK_EXPORT int DCPCALL FsRenMovFile(char* old_name, char* new_name, BOOL move, BOOL overwrite,
                                      RemoteInfoStruct* info)
{
    std::wstring old_w = from_ansi(old_name), new_w = from_ansi(new_name);
    return FsRenMovFileW(old_w.empty() ? NULL : &old_w[0], new_w.empty() ? NULL : &new_w[0], move,
                         overwrite, info);
}

YDISK_EXPORT BOOL DCPCALL FsDeleteFile(char* name)
{
    std::wstring wide = from_ansi(name);
    return FsDeleteFileW(wide.empty() ? NULL : &wide[0]);
}

YDISK_EXPORT BOOL DCPCALL FsRemoveDir(char* name)
{
    std::wstring wide = from_ansi(name);
    return FsRemoveDirW(wide.empty() ? NULL : &wide[0]);
}

YDISK_EXPORT BOOL DCPCALL FsDisconnect(char* name)
{
    std::wstring wide = from_ansi(name);
    return FsDisconnectW(wide.empty() ? NULL : &wide[0]);
}

YDISK_EXPORT BOOL DCPCALL FsSetAttr(char* name, int attr)
{
    std::wstring wide = from_ansi(name);
    return FsSetAttrW(wide.empty() ? NULL : &wide[0], attr);
}

YDISK_EXPORT BOOL DCPCALL FsSetTime(char* name, FILETIME* creation, FILETIME* access, FILETIME* write)
{
    std::wstring wide = from_ansi(name);
    return FsSetTimeW(wide.empty() ? NULL : &wide[0], creation, access, write);
}

YDISK_EXPORT int DCPCALL FsExecuteFile(HWND parent, char* remote_name, char* verb)
{
    std::wstring remote = from_ansi(remote_name), wide_verb = from_ansi(verb);
    std::vector<WCHAR> remote_buffer(remote.begin(), remote.end());
    remote_buffer.push_back(0);
    int result = FsExecuteFileW(parent, remote_buffer.empty() ? NULL : &remote_buffer[0],
                                wide_verb.empty() ? NULL : &wide_verb[0]);
    if (result == FS_EXEC_SYMLINK && remote_name && !remote_buffer.empty())
        copy_ansi(remote_name, YDISK_MAX_REMOTE_PATH, utf8_to_ansi(to_utf8(&remote_buffer[0])));
    return result;
}

YDISK_EXPORT void DCPCALL FsStatusInfo(char* dir, int start_end, int operation)
{
    std::wstring wide = from_ansi(dir);
    FsStatusInfoW(wide.empty() ? NULL : &wide[0], start_end, operation);
}

YDISK_EXPORT int DCPCALL FsExtractCustomIcon(char* name, int flags, HICON* icon)
{
    std::wstring wide = from_ansi(name);
    return FsExtractCustomIconW(wide.empty() ? NULL : &wide[0], flags, icon);
}