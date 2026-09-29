/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)
Common helpers - see plugin_utils.h.

Copyright (C) 2026 doublexyz, LGPL 2.1 or later (see README.md)
*/

#include "plugin_utils.h"
#include "json11.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>

#include <wincrypt.h>

namespace ydisk {

/* ------------------------------------------------------------ conversions */

std::string to_utf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();

    int length = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    if (length <= 0)
        return std::string();

    std::vector<char> buffer((size_t)length);
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &buffer[0], length, NULL, NULL);
    return std::string(&buffer[0], (size_t)length);
}

std::string to_utf8(const WCHAR* text)
{
    if (!text)
        return std::string();
    return to_utf8(std::wstring(text));
}

std::wstring from_utf8(const std::string& utf8)
{
    if (utf8.empty())
        return std::wstring();

    int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (length <= 0)
        return from_ansi(utf8.c_str()); /* not UTF-8 - assume the ANSI code page */

    std::vector<wchar_t> buffer((size_t)length);
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), (int)utf8.size(), &buffer[0], length);
    return std::wstring(&buffer[0], (size_t)length);
}

std::wstring from_ansi(const char* ansi)
{
    if (!ansi || !*ansi)
        return std::wstring();

    int length = (int)::strlen(ansi);
    int wide_length = ::MultiByteToWideChar(CP_ACP, 0, ansi, length, NULL, 0);
    if (wide_length <= 0)
        return std::wstring();

    std::vector<wchar_t> buffer((size_t)wide_length);
    ::MultiByteToWideChar(CP_ACP, 0, ansi, length, &buffer[0], wide_length);
    return std::wstring(&buffer[0], (size_t)wide_length);
}

std::string utf8_to_ansi(const std::string& utf8)
{
    std::wstring wide = from_utf8(utf8);
    if (wide.empty())
        return std::string();

    BOOL used_default = FALSE;
    int length = ::WideCharToMultiByte(CP_ACP, 0, wide.c_str(), (int)wide.size(), NULL, 0, NULL, &used_default);
    if (length <= 0)
        return std::string();

    std::vector<char> buffer((size_t)length);
    ::WideCharToMultiByte(CP_ACP, 0, wide.c_str(), (int)wide.size(), &buffer[0], length, NULL, NULL);
    return std::string(&buffer[0], (size_t)length);
}

std::string url_encode(const std::string& text)
{
    static const char* hex = "0123456789ABCDEF";

    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = (unsigned char)text[i];
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                          c == '-' || c == '.' || c == '_' || c == '~';
        if (unreserved) {
            result += (char)c;
        } else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 0x0f];
        }
    }

    return result;
}

std::string trim_string(const std::string& value)
{
    size_t begin = 0, end = value.size();
    while (begin < end && (unsigned char)value[begin] <= ' ')
        ++begin;
    while (end > begin && (unsigned char)value[end - 1] <= ' ')
        --end;
    return value.substr(begin, end - begin);
}

std::string format_win_error(DWORD error)
{
    LPSTR buffer = NULL;
    DWORD length = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                        FORMAT_MESSAGE_IGNORE_INSERTS,
                                    NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                    (LPSTR)&buffer, 0, NULL);
    std::string message;
    if (length && buffer) {
        message = trim_string(std::string(buffer, length));
        ::LocalFree(buffer);
    }
    if (message.empty())
        message = "unknown error";

    char code[32] = {0};
    ::wsprintfA(code, " (error %lu)", (unsigned long)error);
    return message + code;
}

/* --------------------------------------------------------- remote paths */

namespace {

/* Splits "/a/b//c/./d/../e" into clean components. */
std::vector<std::wstring> clean_components(const std::wstring& path)
{
    std::vector<std::wstring> components;
    std::wstring current;

    for (size_t i = 0; i <= path.size(); ++i) {
        wchar_t c = (i < path.size()) ? path[i] : L'/';
        if (c == L'\\')
            c = L'/';

        if (c == L'/') {
            if (current.empty() || current == L".") {
                /* ignore */
            } else if (current == L"..") {
                if (!components.empty())
                    components.pop_back();
            } else {
                components.push_back(current);
            }
            current.clear();
        } else {
            current += c;
        }
    }

    return components;
}

std::wstring join_components(const std::vector<std::wstring>& components)
{
    std::wstring result;
    for (size_t i = 0; i < components.size(); ++i) {
        result += L"/";
        result += components[i];
    }
    if (result.empty())
        result = L"/";
    return result;
}

} /* anonymous namespace */

RemotePath parse_remote_path(const std::wstring& plugin_path)
{
    std::vector<std::wstring> components = clean_components(plugin_path);

    RemotePath result;
    result.trash = false;

    /* the virtual folder which gives access to the recycle bin: "\.Trash" */
    if (!components.empty() && _wcsicmp(components[0].c_str(), L".Trash") == 0) {
        result.trash = true;
        components.erase(components.begin());
    }

    if (result.trash)
        result.local = components.empty() ? L"/.Trash" : (L"/.Trash" + join_components(components));
    else
        result.local = join_components(components);

    std::string api_path = result.trash ? "trash:/" : "disk:/";
    for (size_t i = 0; i < components.size(); ++i) {
        if (i > 0)
            api_path += "/";
        api_path += to_utf8(components[i]);
    }

    result.api_path = api_path;
    return result;
}

std::wstring join_plugin_path(const std::wstring& directory, const std::wstring& name)
{
    if (directory.empty() || directory == L"/")
        return L"\\" + name;
    if (name.empty())
        return directory;

    std::wstring result = directory;
    while (result.size() > 1 && (result[result.size() - 1] == L'\\' || result[result.size() - 1] == L'/'))
        result.resize(result.size() - 1);

    return result + L"\\" + name;
}

std::wstring file_name(const std::wstring& path)
{
    size_t position = path.find_last_of(L"\\/");
    if (position == std::wstring::npos)
        return path;
    return path.substr(position + 1);
}

std::wstring directory_name(const std::wstring& path)
{
    size_t position = path.find_last_of(L"\\/");
    if (position == std::wstring::npos)
        return L"";
    return path.substr(0, position);
}

std::vector<std::wstring> split(const std::wstring& text, wchar_t separator)
{
    std::vector<std::wstring> result;
    size_t previous = 0;

    while (true) {
        size_t position = text.find(separator, previous);
        if (position == std::wstring::npos) {
            result.push_back(text.substr(previous));
            break;
        }
        result.push_back(text.substr(previous, position - previous));
        previous = position + 1;
    }

    return result;
}

/* --------------------------------------------------------------- file io */

FILETIME parse_iso_time(const std::string& iso)
{
    /* Yandex Disk returns dates like "2019-01-31T12:34:56+03:00" */
    FILETIME empty = {0, 0};
    if (iso.size() < 19)
        return empty;

    SYSTEMTIME utc = {};
    utc.wYear = (WORD)::atoi(iso.substr(0, 4).c_str());
    utc.wMonth = (WORD)::atoi(iso.substr(5, 2).c_str());
    utc.wDay = (WORD)::atoi(iso.substr(8, 2).c_str());
    utc.wHour = (WORD)::atoi(iso.substr(11, 2).c_str());
    utc.wMinute = (WORD)::atoi(iso.substr(14, 2).c_str());
    utc.wSecond = (WORD)::atoi(iso.substr(17, 2).c_str());
    utc.wMilliseconds = 0;

    if (utc.wYear < 1601 || utc.wMonth < 1 || utc.wMonth > 12 || utc.wDay < 1 || utc.wDay > 31)
        return empty;

    /* time zone part: "Z" or "+03:00" / "-04:00" (defaults to UTC) */
    long long offset_minutes = 0;
    if (iso.size() > 19) {
        char sign = iso[19];
        if ((sign == '+' || sign == '-') && iso.size() >= 25) {
            offset_minutes = ::atoi(iso.substr(20, 2).c_str()) * 60 + ::atoi(iso.substr(23, 2).c_str());
            if (sign == '-')
                offset_minutes = -offset_minutes;
        }
    }

    FILETIME file_time;
    if (!::SystemTimeToFileTime(&utc, &file_time))
        return empty;

    ULARGE_INTEGER ticks;
    ticks.LowPart = file_time.dwLowDateTime;
    ticks.HighPart = file_time.dwHighDateTime;

    /* the parsed values are UTC values - correct them by the given offset */
    long long corrected = (long long)ticks.QuadPart - offset_minutes * 60LL * 10000000LL;
    if (corrected < 0)
        corrected = 0;

    ticks.QuadPart = (unsigned long long)corrected;
    file_time.dwLowDateTime = ticks.LowPart;
    file_time.dwHighDateTime = ticks.HighPart;
    return file_time;
}

std::wstring extended_path(const std::wstring& path)
{
    if (path.size() < 248)
        return path;
    if (path.compare(0, 4, L"\\\\?\\") == 0)
        return path;
    if (path.size() >= 2 && path[1] == L':')
        return L"\\\\?\\" + path;
    if (path.compare(0, 2, L"\\\\") == 0)
        return L"\\\\?\\UNC\\" + path.substr(2);
    return path;
}

BOOL file_exists(const std::wstring& path)
{
    DWORD attributes = ::GetFileAttributesW(extended_path(path).c_str());
    return (attributes != INVALID_FILE_ATTRIBUTES) ? TRUE : FALSE;
}

unsigned long long file_size_of(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!::GetFileAttributesExW(extended_path(path).c_str(), GetFileExInfoStandard, &data))
        return 0;
    return ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
}

bool read_file_content(const std::wstring& path, std::string& content)
{
    content.clear();

    HANDLE file = ::CreateFileW(extended_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    std::vector<char> buffer(64 * 1024);
    for (;;) {
        DWORD read = 0;
        if (!::ReadFile(file, &buffer[0], (DWORD)buffer.size(), &read, NULL)) {
            ::CloseHandle(file);
            return false;
        }
        if (read == 0)
            break;
        content.append(&buffer[0], read);
    }

    ::CloseHandle(file);
    return true;
}

bool write_file_content(const std::wstring& path, const std::string& content)
{
    HANDLE file = ::CreateFileW(extended_path(path).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    bool ok = true;
    size_t written_total = 0;
    while (written_total < content.size()) {
        DWORD written = 0;
        DWORD chunk = (DWORD)((content.size() - written_total > 65536) ? 65536 : content.size() - written_total);
        if (!::WriteFile(file, content.data() + written_total, chunk, &written, NULL)) {
            ok = false;
            break;
        }
        written_total += written;
    }

    ::CloseHandle(file);
    return ok;
}

bool delete_local_file(const std::wstring& path)
{
    return ::DeleteFileW(extended_path(path).c_str()) ? true : false;
}

/* ------------------------------------------------------------ DPAPI token */

namespace {

const char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

const char* kDpapiPrefix = "dpapi1:";

} /* anonymous namespace */

std::string base64_encode(const std::string& data)
{
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        unsigned value = ((unsigned char)data[i] << 16) | ((unsigned char)data[i + 1] << 8) |
                         (unsigned char)data[i + 2];
        result += kBase64Alphabet[(value >> 18) & 0x3f];
        result += kBase64Alphabet[(value >> 12) & 0x3f];
        result += kBase64Alphabet[(value >> 6) & 0x3f];
        result += kBase64Alphabet[value & 0x3f];
    }

    size_t remaining = data.size() - i;
    if (remaining == 1) {
        unsigned value = (unsigned char)data[i] << 16;
        result += kBase64Alphabet[(value >> 18) & 0x3f];
        result += kBase64Alphabet[(value >> 12) & 0x3f];
        result += "==";
    } else if (remaining == 2) {
        unsigned value = ((unsigned char)data[i] << 16) | ((unsigned char)data[i + 1] << 8);
        result += kBase64Alphabet[(value >> 18) & 0x3f];
        result += kBase64Alphabet[(value >> 12) & 0x3f];
        result += kBase64Alphabet[(value >> 6) & 0x3f];
        result += '=';
    }

    return result;
}

bool base64_decode(const std::string& text, std::string& data)
{
    data.clear();

    static const std::string alphabet(kBase64Alphabet);
    unsigned buffer = 0;
    int bits = 0;

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\r' || c == '\n' || c == ' ')
            continue;
        if (c == '=')
            break;

        size_t index = alphabet.find(c);
        if (index == std::string::npos)
            return false;

        buffer = (buffer << 6) | (unsigned)index;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            data += (char)((buffer >> bits) & 0xff);
        }
    }

    return true;
}

std::string dpapi_protect(const std::string& plain)
{
    if (plain.empty())
        return std::string();

    DATA_BLOB input;
    input.pbData = (BYTE*)plain.data();
    input.cbData = (DWORD)plain.size();

    DATA_BLOB output;
    output.pbData = NULL;
    output.cbData = 0;

    if (!::CryptProtectData(&input, L"Yandex Disk Commander OAuth token", NULL, NULL, NULL,
                            CRYPTPROTECT_UI_FORBIDDEN, &output))
        return std::string();

    std::string encrypted((const char*)output.pbData, output.cbData);
    ::LocalFree(output.pbData);

    return std::string(kDpapiPrefix) + base64_encode(encrypted);
}

bool dpapi_unprotect(const std::string& stored, std::string& plain)
{
    plain.clear();
    if (stored.empty())
        return false;

    /* plain value - e.g. a configuration file of the Linux version of the plugin */
    if (stored.compare(0, ::strlen(kDpapiPrefix), kDpapiPrefix) != 0) {
        plain = stored;
        return true;
    }

    std::string encrypted;
    if (!base64_decode(stored.substr(::strlen(kDpapiPrefix)), encrypted))
        return false;

    DATA_BLOB input;
    input.pbData = (BYTE*)encrypted.data();
    input.cbData = (DWORD)encrypted.size();

    DATA_BLOB output;
    output.pbData = NULL;
    output.cbData = 0;

    if (!::CryptUnprotectData(&input, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return false;

    plain.assign((const char*)output.pbData, output.cbData);
    ::LocalFree(output.pbData);
    return true;
}

/* ----------------------------------------------------------- configuration */

namespace {

std::string lower_string(const std::string& value)
{
    std::string result(value);
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i] >= 'A' && result[i] <= 'Z')
            result[i] = (char)(result[i] - 'A' + 'a');
    }
    return result;
}

} /* anonymous namespace */

std::wstring config_path_from_ini(const std::string& default_ini_name)
{
    /* Total Commander passes e.g.
       "C:\Users\user\AppData\Roaming\GHISLER\wfx_YandexDisk.ini" */
    std::wstring ini_name = from_ansi(default_ini_name.c_str());

    size_t position = ini_name.find_last_of(L"\\/");
    if (position != std::wstring::npos)
        return ini_name.substr(0, position + 1) + L"yandex_disk.ini";

    /* no directory in the name - store next to the plugin dll */
    wchar_t module[MAX_PATH] = {0};
    DWORD length = ::GetModuleFileNameW((HMODULE)NULL, module, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        std::wstring module_path(module, length);
        size_t separator = module_path.find_last_of(L"\\/");
        if (separator != std::wstring::npos)
            return module_path.substr(0, separator + 1) + L"yandex_disk.ini";
    }

    return L"yandex_disk.ini";
}

bool read_config(const std::wstring& path, PluginConfig& config)
{
    config = PluginConfig();

    std::string content;
    if (!read_file_content(path, content))
        return false;

    std::string error;
    json11::Json json = json11::Json::parse(content, error);
    if (json.is_null() || !error.empty())
        return false;

    if (json["get_token_method"].is_string()) {
        std::string method = lower_string(json["get_token_method"].string_value());
        /* "oauth" is the value written by the *nix version of the plugin,
           there it means the interactive browser flow */
        if (method == "oauth")
            method = "browser";
        if (method == "device" || method == "browser" || method == "manual")
            config.get_token_method = method;
    }

    if (json["save_type"].is_string()) {
        std::string save_type = lower_string(json["save_type"].string_value());
        if (save_type == "dont_save" || save_type == "config" || save_type == "password_manager")
            config.save_type = save_type;
    }

    if (json["oauth_token"].is_string()) {
        std::string plain;
        if (dpapi_unprotect(json["oauth_token"].string_value(), plain))
            config.oauth_token = plain;
    }

    if (json["client_id"].is_string())
        config.client_id = trim_string(json["client_id"].string_value());

    return true;
}

bool save_config(const std::wstring& path, const PluginConfig& config)
{
    json11::Json::object object;
    object["get_token_method"] = config.get_token_method.empty() ? std::string("device")
                                                                 : config.get_token_method;
    object["save_type"] = config.save_type.empty() ? std::string("dont_save") : config.save_type;

    /* The token is only written into the file when the user asked for it; it is
       encrypted with DPAPI (bound to the current Windows account). */
    if (config.save_type == "config" && !config.oauth_token.empty()) {
        std::string protected_token = dpapi_protect(config.oauth_token);
        if (protected_token.empty())
            return false; /* never silently downgrade to a plaintext token */
        object["oauth_token"] = protected_token;
    } else {
        object["oauth_token"] = std::string();
    }

    if (!config.client_id.empty())
        object["client_id"] = config.client_id;

    return write_file_content(path, json11::Json(object).dump());
}

} /* namespace ydisk */
