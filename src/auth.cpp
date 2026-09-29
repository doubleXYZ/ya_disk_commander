/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)
OAuth authorization - see auth.h.

Copyright (C) 2026 doublexyz, LGPL 2.1 or later (see README.md)
*/

#include "auth.h"
#include "http_client.h"
#include "http_server.h"
#include "json11.hpp"
#include "plugin_utils.h"

#include <shellapi.h>

using json11::Json;

namespace ydisk {

const char* kDefaultClientId = "bc2f272cc37349b7a1320b9ac7826ebf";

namespace {

const char* kOauthHost = "https://oauth.yandex.ru";

std::string form_value(const Json& json, const char* key)
{
    if (json[key].is_string())
        return json[key].string_value();
    return std::string();
}

std::string json_error_description(const Json& json, const std::string& fallback)
{
    std::string description = form_value(json, "error_description");
    if (!description.empty())
        return description;

    std::string code = form_value(json, "error");
    if (!code.empty())
        return code;

    return fallback;
}

} /* anonymous namespace */

bool open_in_browser(const std::wstring& url)
{
    HINSTANCE result = ::ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    return ((INT_PTR)result > 32);
}

std::string acquire_token_browser(const std::string& client_id, const StatusFn& status,
                                  const WaitFn& wait, const CancelFn& cancel, std::string& error)
{
    error.clear();

    LocalHttpServer server(kOauthCallbackPort);
    if (!server.start(error)) {
        error += "\nThe 'Enter the token manually' method can be used instead.";
        return std::string();
    }

    std::string url = std::string(kOauthHost) + "/authorize?response_type=token&client_id=" +
                      url_encode(client_id);

    if (status)
        status(L"Waiting for the confirmation in the browser window...\n\n"
               L"If the browser did not open, use this link:\n" + from_utf8(url));

    open_in_browser(from_utf8(url));

    std::function<bool()> on_idle;
    if (wait || cancel) {
        on_idle = [&wait, &cancel]() -> bool {
            if (wait)
                wait(0);
            if (cancel && cancel())
                return false;
            return true;
        };
    }

    std::string token = server.wait_token(180 * 1000, on_idle); /* 3 minutes */
    if (token.empty() && !(cancel && cancel()))
        error = "The browser did not deliver an OAuth token within 3 minutes.";

    return token;
}

std::string acquire_token_device(const std::string& client_id, const StatusFn& status,
                                 const WaitFn& wait, const CancelFn& cancel, std::string& error)
{
    error.clear();

    HttpClient client;

    std::string request_body = "client_id=" + url_encode(client_id) +
                               "&device_name=" + url_encode("Total Commander (Windows)");

    HttpResponse response;
    try {
        response = client.request("POST", std::string(kOauthHost) + "/device/code", HttpHeaders(),
                                  request_body, "application/x-www-form-urlencoded");
    } catch (const std::exception& exception) {
        error = std::string("Cannot reach oauth.yandex.ru: ") + exception.what();
        return std::string();
    }

    std::string parse_error;
    Json json = Json::parse(response.body, parse_error);
    if (response.status != 200 || json.is_null() || !parse_error.empty()) {
        error = "Cannot request the confirmation code: HTTP " + std::to_string(response.status);
        if (!json.is_null())
            error += "\n" + json_error_description(json, response.body);
        return std::string();
    }

    const std::string device_code = form_value(json, "device_code");
    const std::string user_code = form_value(json, "user_code");
    std::string verification_url = form_value(json, "verification_url");
    int expires_in = json["expires_in"].is_number() ? json["expires_in"].int_value() : 600;
    int interval = json["interval"].is_number() ? json["interval"].int_value() : 5;

    if (device_code.empty() || user_code.empty()) {
        error = "oauth.yandex.ru answered without a device code";
        return std::string();
    }
    if (verification_url.empty())
        verification_url = "https://ya.ru/device";
    if (interval < 1)
        interval = 5;
    if (expires_in < interval)
        expires_in = interval * 6;

    if (status) {
        status(L"Open the page " + from_utf8(verification_url) + L"\n\nand enter this code:\n\n" +
               from_utf8(user_code));
    }
    open_in_browser(from_utf8(verification_url));

    const int attempts = expires_in / interval;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (cancel && cancel()) {
            error.clear();
            return std::string();
        }

        if (wait)
            wait(interval * 1000);
        else
            ::Sleep((DWORD)interval * 1000);

        std::string poll_body = "grant_type=device_code&code=" + url_encode(device_code) +
                                "&client_id=" + url_encode(client_id);

        HttpResponse poll;
        try {
            poll = client.request("POST", std::string(kOauthHost) + "/token", HttpHeaders(),
                                  poll_body, "application/x-www-form-urlencoded");
        } catch (const std::exception& exception) {
            error = std::string("Cannot reach oauth.yandex.ru: ") + exception.what();
            return std::string();
        }

        Json poll_json = Json::parse(poll.body, parse_error);
        if (poll_json.is_null())
            continue;

        if (poll.status == 200) {
            std::string token = form_value(poll_json, "access_token");
            if (!token.empty())
                return token;

            error = "oauth.yandex.ru answered without an access token";
            return std::string();
        }

        const std::string code = form_value(poll_json, "error");
        if (code == "authorization_pending")
            continue; /* the user has not entered the code yet */

        error = json_error_description(poll_json, "Authorization failed");
        return std::string();
    }

    error = "The confirmation code expired before it was entered.";
    return std::string();
}

} /* namespace ydisk */
