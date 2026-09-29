/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)

Yandex Disk Web API (https://yandex.ru/dev/disk/api/) client.
A Windows rewrite of the corresponding class from the *nix plugin; the main
difference is the transport layer (WinHTTP instead of cpp-httplib/OpenSSL).

Copyright (C) 2026 doublexyz, LGPL 2.1 or later (see README.md)
*/

#ifndef YDISK_RESTCLIENT_H
#define YDISK_RESTCLIENT_H

#include "common.h"
#include "http_client.h"
#include "json11.hpp"

#include <stdexcept>
#include <string>

namespace ydisk {

/** An error answered by the Yandex Disk API (HTTP status + description). */
class rest_client_exception : public std::runtime_error {
public:
    rest_client_exception(int status_code, const std::string& message);

    int get_status() const { return status_code_; }
    const std::string& get_message() const { return message_; }

private:
    int status_code_;
    std::string message_;
    std::string text_;
};

class YdiskRestClient {
public:
    static const char* kDefaultBaseUrl; /* https://cloud-api.yandex.net */

    YdiskRestClient();

    void set_oauth_token(const std::string& token);
    bool has_token() const { return !token_.empty(); }

    /** Only needed for the tests (a mock server is used there). */
    void set_base_url(const std::string& base_url) { base_url_ = base_url; }
    const std::string& base_url() const { return base_url_; }

    json11::Json get_disk_info();

    /*
     * api_path is the "scheme:/path" form, e.g. "disk:/photos" or "trash:/file.txt".
     * limit/offset are used by the plugin to read big folders page by page.
     */
    json11::Json get_resources(const std::string& api_path, bool trash, int limit = 1000, int offset = 0);

    void make_folder(const std::string& api_path);
    void remove_resource(const std::string& api_path);
    void download_file(const std::string& api_path, const std::wstring& local_path,
                       const ProgressFn& progress = ProgressFn());
    void upload_file(const std::string& api_path, const std::wstring& local_path, bool overwrite,
                     const ProgressFn& progress = ProgressFn());
    void move(const std::string& from, const std::string& to, bool overwrite);
    void copy(const std::string& from, const std::string& to, bool overwrite);
    void save_from_url(const std::string& url, const std::string& api_path);

    void clean_trash();
    void delete_from_trash(const std::string& api_path);

    /** Replaces the Authorization header, used by the settings dialog. */
    void clear_token();

private:
    HttpClient client_;
    HttpHeaders headers_;
    std::string token_;
    std::string base_url_;

    HttpResponse send(const std::string& method, const std::string& target,
                      const std::string& body = std::string(),
                      const std::string& content_type = std::string(),
                      const ProgressFn& progress = ProgressFn());

    json11::Json send_json(const std::string& method, const std::string& target,
                           const std::string& body = std::string());

    static void require_status(const HttpResponse& response, int expected,
                              const char* operation);
    static void require_status(const HttpResponse& response, const int* expected, size_t count,
                              const char* operation);
    static void throw_response_error(const HttpResponse& response);
    static std::string json_string(const json11::Json& json, const char* key, const char* context);

    /** 202 answers contain the url of an asynchronous operation which is polled here. */
    void wait_success_operation(const std::string& body);
};

} /* namespace ydisk */

#endif /* YDISK_RESTCLIENT_H */
