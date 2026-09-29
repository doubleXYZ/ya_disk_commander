/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)
Yandex Disk Web API client - see restclient.h.

Copyright (C) 2026 doublexyz, LGPL 2.1 or later (see README.md)
*/

#include "restclient.h"
#include "plugin_utils.h"

#include <sstream>

using json11::Json;

namespace ydisk {

const char* YdiskRestClient::kDefaultBaseUrl = "https://cloud-api.yandex.net";

/* ----------------------------------------------------------- exceptions */

namespace {

std::string build_error_text(int status, const std::string& message)
{
    std::ostringstream text;
    text << "Yandex Disk API error " << status;
    if (!message.empty())
        text << ": " << message;
    return text.str();
}

} /* anonymous namespace */

rest_client_exception::rest_client_exception(int status_code, const std::string& message)
    : std::runtime_error(build_error_text(status_code, message)),
      status_code_(status_code),
      message_(message),
      text_(build_error_text(status_code, message))
{
}

/* ------------------------------------------------------------ constructor */

YdiskRestClient::YdiskRestClient() : base_url_(kDefaultBaseUrl)
{
}

void YdiskRestClient::set_oauth_token(const std::string& token)
{
    token_ = token;
    headers_.clear();
    if (!token_.empty()) {
        HttpHeader header;
        header.name = "Authorization";
        header.value = "OAuth " + token_;
        headers_.push_back(header);
    }
}

void YdiskRestClient::clear_token()
{
    token_.clear();
    headers_.clear();
}

/* ---------------------------------------------------------------- helpers */

HttpResponse YdiskRestClient::send(const std::string& method, const std::string& target,
                                   const std::string& body, const std::string& content_type,
                                   const ProgressFn& progress)
{
    std::string url = base_url_ + target;
    return client_.request(method, url, headers_, body, content_type, true, progress);
}

Json YdiskRestClient::send_json(const std::string& method, const std::string& target,
                                const std::string& body)
{
    HttpResponse response = send(method, target, body);
    require_status(response, 200, target.c_str());

    std::string error;
    Json json = Json::parse(response.body, error);
    if (json.is_null() || !error.empty())
        throw std::runtime_error("Cannot parse the JSON answer of the Yandex Disk API (" + target +
                                 "): " + (error.empty() ? std::string("empty answer") : error));

    return json;
}

void YdiskRestClient::require_status(const HttpResponse& response, int expected, const char* operation)
{
    (void)operation;
    if (response.status == expected)
        return;

    /* A missing/invalid token is a very common case - give a clear hint. */
    if (response.status == 401)
        throw rest_client_exception(401, "The OAuth token is missing, expired or invalid. "
                                         "Open the plugin settings (Alt+Enter on the plugin root) "
                                         "to enter a new token.");

    throw_response_error(response);
}

void YdiskRestClient::require_status(const HttpResponse& response, const int* expected, size_t count,
                                     const char* operation)
{
    (void)operation;
    for (size_t i = 0; i < count; ++i) {
        if (response.status == expected[i])
            return;
    }

    if (response.status == 401)
        throw rest_client_exception(401, "The OAuth token is missing, expired or invalid.");

    throw_response_error(response);
}

void YdiskRestClient::throw_response_error(const HttpResponse& response)
{
    std::string message = response.body;

    if (!response.body.empty()) {
        std::string error;
        Json json = Json::parse(response.body, error);
        if (json.is_null() == false) {
            if (json["description"].is_string() && !json["description"].string_value().empty())
                message = json["description"].string_value();
            else if (json["message"].is_string())
                message = json["message"].string_value();
        }
    }

    throw rest_client_exception(response.status, trim_string(message));
}

std::string YdiskRestClient::json_string(const Json& json, const char* key, const char* context)
{
    if (!json[key].is_string() || json[key].string_value().empty())
        throw std::runtime_error(std::string("Unexpected answer of the Yandex Disk API (") + context +
                                 "): the field \"" + key + "\" is missing");
    return json[key].string_value();
}

/* --------------------------------------------------------------- API calls */

Json YdiskRestClient::get_disk_info()
{
    return send_json("GET", "/v1/disk/");
}

Json YdiskRestClient::get_resources(const std::string& api_path, bool trash, int limit, int offset)
{
    std::ostringstream target;
    target << "/v1/disk/";
    if (trash)
        target << "trash/";
    target << "resources?limit=" << limit << "&offset=" << offset << "&path=" << url_encode(api_path);

    return send_json("GET", target.str());
}

void YdiskRestClient::make_folder(const std::string& api_path)
{
    HttpResponse response = send("PUT", "/v1/disk/resources?path=" + url_encode(api_path));
    require_status(response, 201, "create a folder");
}

void YdiskRestClient::remove_resource(const std::string& api_path)
{
    HttpResponse response = send("DELETE", "/v1/disk/resources?path=" + url_encode(api_path));

    if (response.status == 204)
        return; /* the resource was removed immediately */

    if (response.status == 202) { /* a not empty folder is removed in the background */
        wait_success_operation(response.body);
        return;
    }

    if (response.status == 404)
        throw rest_client_exception(404, "The resource does not exist any more");

    throw_response_error(response);
}

void YdiskRestClient::download_file(const std::string& api_path, const std::wstring& local_path,
                                    const ProgressFn& progress)
{
    Json json = send_json("GET", "/v1/disk/resources/download?path=" + url_encode(api_path));
    std::string href = json_string(json, "href", "download");

    /* the download link points to a storage server which redirects once more -
       the OAuth header is not sent there */
    HttpResponse response = client_.download_to_file(href, HttpHeaders(), local_path, progress);

    if (response.aborted)
        throw user_abort();

    if (response.status < 200 || response.status >= 300)
        throw_response_error(response);
}

void YdiskRestClient::upload_file(const std::string& api_path, const std::wstring& local_path,
                                  bool overwrite, const ProgressFn& progress)
{
    std::ostringstream target;
    target << "/v1/disk/resources/upload?path=" << url_encode(api_path) << "&overwrite="
           << (overwrite ? "true" : "false");

    Json json = send_json("GET", target.str());
    std::string href = json_string(json, "href", "upload");

    HttpResponse response = client_.upload_file("PUT", href, HttpHeaders(), local_path,
                                                "application/octet-stream", progress);

    if (response.aborted)
        throw user_abort();

    /* 201 = created, 202 = accepted (the file is stored in the background) */
    if (response.status != 201 && response.status != 202)
        throw_response_error(response);
}

void YdiskRestClient::move(const std::string& from, const std::string& to, bool overwrite)
{
    std::ostringstream target;
    target << "/v1/disk/resources/move?from=" << url_encode(from) << "&path=" << url_encode(to)
           << "&overwrite=" << (overwrite ? "true" : "false");

    HttpResponse response = send("POST", target.str());

    /* documented: 201 (done) / 202 (async); some API servers also answer 200 */
    if (response.status == 200 || response.status == 201)
        return;
    if (response.status == 202) {
        wait_success_operation(response.body);
        return;
    }

    throw_response_error(response);
}

void YdiskRestClient::copy(const std::string& from, const std::string& to, bool overwrite)
{
    std::ostringstream target;
    target << "/v1/disk/resources/copy?from=" << url_encode(from) << "&path=" << url_encode(to)
           << "&overwrite=" << (overwrite ? "true" : "false");

    HttpResponse response = send("POST", target.str());

    /* documented: 201 (done) / 202 (async); some API servers also answer 200 */
    if (response.status == 200 || response.status == 201)
        return;
    if (response.status == 202) {
        wait_success_operation(response.body);
        return;
    }

    throw_response_error(response);
}

void YdiskRestClient::save_from_url(const std::string& url, const std::string& api_path)
{
    std::ostringstream target;
    target << "/v1/disk/resources/upload?url=" << url_encode(url) << "&path=" << url_encode(api_path);

    HttpResponse response = send("POST", target.str());

    if (response.status == 201)
        return;
    if (response.status == 202) {
        wait_success_operation(response.body);
        return;
    }

    throw_response_error(response);
}

void YdiskRestClient::clean_trash()
{
    HttpResponse response = send("DELETE", "/v1/disk/trash/resources");

    if (response.status == 204)
        return;
    if (response.status == 202) {
        wait_success_operation(response.body);
        return;
    }

    throw_response_error(response);
}

void YdiskRestClient::delete_from_trash(const std::string& api_path)
{
    HttpResponse response = send("DELETE", "/v1/disk/trash/resources?path=" + url_encode(api_path));

    if (response.status == 204)
        return;
    if (response.status == 202) {
        wait_success_operation(response.body);
        return;
    }

    throw_response_error(response);
}

void YdiskRestClient::wait_success_operation(const std::string& body)
{
    std::string error;
    Json json = Json::parse(body, error);
    if (json.is_null() || !error.empty())
        throw std::runtime_error("Cannot parse the asynchronous operation of the Yandex Disk API");

    std::string href = json_string(json, "href", "operation status");

    /* the href of an operation points to the API server - keep only the path */
    size_t position = href.find("/v1/disk");
    if (position == std::string::npos)
        throw std::runtime_error("Unexpected operation url: " + href);

    const std::string target = href.substr(position);
    const int max_attempts = 30; /* 30 * 2 seconds = 1 minute */

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        ::Sleep(2000);

        HttpResponse response = send("GET", target);
        if (response.status != 200)
            throw_response_error(response);

        Json status_json = Json::parse(response.body, error);
        if (status_json.is_null() || !error.empty() || !status_json["status"].is_string())
            throw std::runtime_error("Unexpected answer while waiting for the operation result");

        const std::string status = status_json["status"].string_value();
        if (status == "success")
            return;
        if (status == "failure")
            throw rest_client_exception(500, "The asynchronous operation failed");
        /* "in-progress" - keep waiting */
    }

    throw std::runtime_error("The asynchronous operation of the Yandex Disk API took too long");
}

} /* namespace ydisk */
