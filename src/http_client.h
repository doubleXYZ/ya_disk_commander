/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)

HTTP transport.

The original (*nix) plugin used cpp-httplib together with OpenSSL. On Windows
the same job is done by WinHTTP which is part of the operating system:
- https/TLS without external libraries,
- system proxy settings and proxy authentication,
- WinHTTP streaming API with progress reporting and transfer abort.

Copyright (C) 2026 doublexyz, LGPL 2.1 or later (see README.md)
*/

#ifndef YDISK_HTTP_CLIENT_H
#define YDISK_HTTP_CLIENT_H

#include "common.h"

#include <winhttp.h>

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ydisk {

struct HttpHeader {
    std::string name;
    std::string value;
};

typedef std::vector<HttpHeader> HttpHeaders;

struct HttpResponse {
    int status = 0;
    std::string body;
    HttpHeaders headers;
    bool aborted = false;

    /** Case-insensitive lookup of a response header (empty string if absent). */
    std::string header(const std::string& name) const;
    std::string header_ci(const std::string& name) const { return header(name); }
};

/** Any transport level failure (DNS, socket, TLS, WinHTTP API error). */
class http_error : public std::runtime_error {
public:
    explicit http_error(const std::string& message) : std::runtime_error(message) {}
};

/** Thrown when the user aborted a transfer from the progress callback. */
class user_abort : public std::runtime_error {
public:
    user_abort() : std::runtime_error("Operation aborted by the user") {}
};

/** progress(done_bytes, total_bytes) - return false to abort the transfer.
    total_bytes is 0 when the server did not report a content length. */
typedef std::function<bool(unsigned long long, unsigned long long)> ProgressFn;

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    /** Simple request with an optional in-memory body. */
    HttpResponse request(const std::string& method,
                         const std::string& url,
                         const HttpHeaders& headers,
                         const std::string& body = std::string(),
                         const std::string& content_type = std::string(),
                         bool follow_redirects = true,
                         const ProgressFn& progress = ProgressFn());

    /** GET the url and write the response into local_path (streamed). */
    HttpResponse download_to_file(const std::string& url,
                                  const HttpHeaders& headers,
                                  const std::wstring& local_path,
                                  const ProgressFn& progress = ProgressFn());

    /** PUT/POST the content of local_path to url (streamed). */
    HttpResponse upload_file(const std::string& method,
                             const std::string& url,
                             const HttpHeaders& headers,
                             const std::wstring& local_path,
                             const std::string& content_type = "application/octet-stream",
                             const ProgressFn& progress = ProgressFn());

    /** Splits an url into scheme / host / port / path+query. */
    static bool parse_url(const std::string& url, std::string& scheme, std::string& host,
                          unsigned short& port, std::string& target);

private:
    struct Body {
        const std::string* memory = nullptr;       /* in-memory body (may be null) */
        HANDLE file = INVALID_HANDLE_VALUE;        /* file body (INVALID_HANDLE_VALUE if unused) */
        unsigned long long size = 0;               /* declared body size */
    };

    struct Sink {
        std::string* memory = nullptr;             /* collect into a string */
        HANDLE file = INVALID_HANDLE_VALUE;        /* or write into a file */
    };

    void* session_;                                /* HINTERNET, kept for reuse */

    HttpResponse perform(const std::string& method, const std::string& url,
                         const HttpHeaders& headers, const Body& body,
                         const std::string& content_type, Sink& sink,
                         const ProgressFn& progress);

    HttpResponse with_redirects(const std::string& method, const std::string& url,
                                HttpHeaders headers, const Body& body,
                                const std::string& content_type, Sink& sink,
                                const ProgressFn& progress, bool follow_redirects);

    HINTERNET session();
    static std::wstring widen(const std::string& utf8);
    static std::string narrow(const std::wstring& wide);
    static std::string last_error_message(const char* what);
    static std::string resolve_location(const std::string& base, const std::string& location);
};

} /* namespace ydisk */

#endif /* YDISK_HTTP_CLIENT_H */
