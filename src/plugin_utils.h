/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)

Common helpers: UTF-8/UTF-16 conversions, remote path mapping, ISO time parsing,
configuration file handling and DPAPI protection of the stored OAuth token.

Copyright (C) 2026 doublexyz, LGPL 2.1 or later (see README.md)
*/

#ifndef YDISK_PLUGIN_UTILS_H
#define YDISK_PLUGIN_UTILS_H

#include "common.h"

#include <string>
#include <vector>

namespace ydisk {

typedef std::wstring wcharstring;

/* ------------------------------------------------------------ conversions */

std::string to_utf8(const std::wstring& text);
std::string to_utf8(const WCHAR* text);
std::wstring from_utf8(const std::string& utf8);
std::wstring from_ansi(const char* ansi);          /* CP_ACP -> UTF-16 */
std::string utf8_to_ansi(const std::string& utf8); /* for the ANSI plugin functions */

/** RFC 3986 percent encoding - used for every value put into a query string. */
std::string url_encode(const std::string& text);

std::string trim_string(const std::string& value);
std::string format_win_error(DWORD error);

/* --------------------------------------------------------- remote paths */

/*
 * Yandex Disk paths are built from the path given by Total Commander:
 *
 *   "\"                 -> disk:/            (root of the disk)
 *   "\docs\file.txt"    -> disk:/docs/file.txt
 *   "\.Trash"           -> trash:/           (virtual folder)
 *   "\.Trash\file.txt"  -> trash:/file.txt
 *
 * Yandex Disk expects the documented "scheme:/path" form, therefore the
 * "disk:" / "trash:" prefix is added here.
 */
struct RemotePath {
    bool trash;             /* the resource is located in the trash */
    std::string api_path;   /* "disk:/dir/file" or "trash:/file" */
    std::wstring local;     /* normalized plugin path "/dir/file" */
};

RemotePath parse_remote_path(const std::wstring& plugin_path);
std::wstring join_plugin_path(const std::wstring& directory, const std::wstring& name);
std::wstring file_name(const std::wstring& path);
std::wstring directory_name(const std::wstring& path);
std::vector<std::wstring> split(const std::wstring& text, wchar_t separator);

/* --------------------------------------------------------------- file io */

FILETIME parse_iso_time(const std::string& iso);
std::wstring extended_path(const std::wstring& path); /* adds \\?\ for long paths */
BOOL file_exists(const std::wstring& path);
unsigned long long file_size_of(const std::wstring& path);
bool read_file_content(const std::wstring& path, std::string& content);
bool write_file_content(const std::wstring& path, const std::string& content);
bool delete_local_file(const std::wstring& path);

/* ------------------------------------------------------------ DPAPI token */

/** Encrypts the token for the current Windows user -> "dpapi1:<base64>". */
std::string dpapi_protect(const std::string& plain);
/** Reverses dpapi_protect(). Plain (not encrypted) values are passed through,
    so a configuration file of the Linux plugin keeps working. */
bool dpapi_unprotect(const std::string& stored, std::string& plain);
std::string base64_encode(const std::string& data);
bool base64_decode(const std::string& text, std::string& data);

/* ------------------------------------------------------------ configuration */

struct PluginConfig {
    std::string get_token_method;   /* "device" | "browser" | "manual" */
    std::string save_type;          /* "dont_save" | "config" | "password_manager" */
    std::string oauth_token;        /* in memory always plain (!) */
    std::string client_id;          /* OAuth application id (empty = built in) */

    PluginConfig() : get_token_method("device"), save_type("dont_save") {}
};

/** "C:\...\wfx_YandexDisk.ini" -> "C:\...\yandex_disk.ini" */
std::wstring config_path_from_ini(const std::string& default_ini_name);
bool read_config(const std::wstring& path, PluginConfig& config);
bool save_config(const std::wstring& path, const PluginConfig& config);

} /* namespace ydisk */

#endif /* YDISK_PLUGIN_UTILS_H */
