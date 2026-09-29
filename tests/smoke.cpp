/*
 * Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)
 * Smoke tests - run by scripts\build.ps1 -Test.
 *
 * Copyright (C) 2026 doublexyz, LGPL 2.1 or later (see README.md)
 */

#include "wfxplugin.h"
#include "plugin_utils.h"
#include "http_client.h"
#include "http_server.h"
#include "json11.hpp"

#include <cassert>
#include <cstring>
#include <iostream>

int main()
{
    using namespace ydisk;
    wchar_t temporary_dir[MAX_PATH] = {};
    assert(::GetTempPathW(MAX_PATH, temporary_dir) != 0);
    std::wstring path = std::wstring(temporary_dir) + L"ydisk_smoke_config_tmp.json";
    delete_local_file(path);

    PluginConfig original;
    original.get_token_method = "manual";
    original.save_type = "config";
    original.client_id = "client-id";
    original.oauth_token = "secret-test-token";
    assert(save_config(path, original));

    std::string bytes;
    assert(read_file_content(path, bytes));
    std::string parse_error;
    json11::Json json = json11::Json::parse(bytes, parse_error);
    assert(parse_error.empty());
    assert(json["oauth_token"].string_value().find("dpapi1:") == 0);
    assert(bytes.find("secret-test-token") == std::string::npos);

    PluginConfig loaded;
    assert(read_config(path, loaded));
    assert(loaded.oauth_token == original.oauth_token);
    assert(loaded.client_id == original.client_id);
    assert(loaded.get_token_method == original.get_token_method);

    original.save_type = "dont_save";
    assert(save_config(path, original));
    assert(read_config(path, loaded));
    assert(loaded.oauth_token.empty());
    assert(loaded.client_id == "client-id");

    original.save_type = "password_manager";
    assert(save_config(path, original));
    assert(read_config(path, loaded));
    assert(loaded.oauth_token.empty());

    assert(write_file_content(path, "{\"get_token_method\":\"oauth\",\"save_type\":\"config\","
                                    "\"oauth_token\":\"legacy-plain\",\"client_id\":\"old-id\"}"));
    assert(read_config(path, loaded));
    assert(loaded.get_token_method == "browser");
    assert(loaded.oauth_token == "legacy-plain");
    assert(loaded.client_id == "old-id");

    assert(write_file_content(path, "{\"oauth_token\":\"dpapi1:invalid!\"}"));
    assert(read_config(path, loaded));
    assert(loaded.oauth_token.empty());
    assert(delete_local_file(path));

    RemotePath trash = parse_remote_path(L"\\.Trash\\folder\\file.txt");
    assert(trash.trash && trash.api_path == "trash:/folder/file.txt");
    RemotePath root = parse_remote_path(L"\\");
    assert(!root.trash && root.api_path == "disk:/");

    std::string scheme, host, target;
    unsigned short port = 0;
    assert(HttpClient::parse_url("https://cloud-api.yandex.net/v1/disk/", scheme, host, port, target));
    assert(scheme == "https" && host == "cloud-api.yandex.net" && port == 443);
    assert(target == "/v1/disk/");

    /* LocalHttpServer request routing (no real sockets involved). */
    {
        std::string token;
        std::string response = LocalHttpServer::handle_request(
            12345, "GET /receive_token?access_token=abc123 HTTP/1.1", "access_token=abc123", &token);
        assert(token == "abc123");
        assert(response.find("200 OK") != std::string::npos);

        token.clear();
        response = LocalHttpServer::handle_request(12345, "GET /get_token?code=xyz HTTP/1.1",
                                                   "code=xyz", &token);
        assert(token == "xyz");

        token.clear();
        response = LocalHttpServer::handle_request(12345, "GET /receive_token HTTP/1.1", "", &token);
        assert(token.empty());
        assert(response.find("400 Bad Request") != std::string::npos);

        /* Yandex redirects to /get_token#access_token=... - the fragment never
           reaches the server, so an empty query must return the helper page. */
        token.clear();
        response = LocalHttpServer::handle_request(12345, "GET /get_token HTTP/1.1", "", &token);
        assert(token.empty());
        assert(response.find("200 OK") != std::string::npos);
        assert(response.find("text/html") != std::string::npos);
        assert(response.find("access_token=") != std::string::npos);
        /* The page script must not depend on .status - some browsers throw
           on it once the server has closed the connection. */
        assert(response.find("this.status") == std::string::npos);

        response = LocalHttpServer::handle_request(12345, "GET / HTTP/1.1", "", &token);
        assert(response.find("200 OK") != std::string::npos);
        assert(response.find("text/html") != std::string::npos);

        response = LocalHttpServer::handle_request(12345, "GET /unknown HTTP/1.1", "", &token);
        assert(response.find("404 Not Found") != std::string::npos);
    }

    char name[40] = {};
    FsGetDefRootName(name, sizeof(name));
    assert(std::strcmp(name, "Yandex Disk") == 0);
    assert(FsInitW(1, NULL, NULL, NULL) == 0);
    assert(FsGetBackgroundFlags() == 0);
    assert(FsFindNextW(INVALID_HANDLE_VALUE, NULL) == FALSE);
    assert(FsFindClose(INVALID_HANDLE_VALUE) == 0);
    assert(FsSetAttrW(NULL, 0) == FALSE);
    std::cout << "smoke tests passed\n";
    return 0;
}