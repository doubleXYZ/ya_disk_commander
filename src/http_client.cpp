/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)
HTTP transport based on WinHTTP - see http_client.h.

Copyright (C) 2026 doublexyz, LGPL 2.1 or later (see README.md)
*/

#include "http_client.h"
#include "plugin_utils.h" /* extended_path() */

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include <winhttp.h>

namespace ydisk {

namespace {

/* ------------------------------------------------------------------ helpers */

std::string lower_ascii(const std::string& value)
{
    std::string result(value);
    for (size_t i = 0; i < result.size(); ++i) {
        char c = result[i];
        if (c >= 'A' && c <= 'Z')
            result[i] = (char)(c - 'A' + 'a');
    }
    return result;
}

bool equals_ci(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return false;
    }
    return true;
}

std::string trim(const std::string& value)
{
    size_t begin = 0, end = value.size();
    while (begin < end && (unsigned char)value[begin] <= ' ')
        ++begin;
    while (end > begin && (unsigned char)value[end - 1] <= ' ')
        --end;
    return value.substr(begin, end - begin);
}

/** RAII wrapper for WinHTTP handles. */
class WinHttpHandle {
public:
    WinHttpHandle() : handle_(NULL) {}
    explicit WinHttpHandle(HINTERNET handle) : handle_(handle) {}
    ~WinHttpHandle() { reset(NULL); }

    void reset(HINTERNET handle)
    {
        if (handle_)
            ::WinHttpCloseHandle(handle_);
        handle_ = handle;
    }
    HINTERNET get() const { return handle_; }
    operator HINTERNET() const { return handle_; }
    bool valid() const { return handle_ != NULL; }

private:
    WinHttpHandle(const WinHttpHandle&);
    WinHttpHandle& operator=(const WinHttpHandle&);
    HINTERNET handle_;
};

/** RAII wrapper for a Win32 file handle. */
class FileHandle {
public:
    FileHandle() : handle_(INVALID_HANDLE_VALUE) {}
    explicit FileHandle(HANDLE handle) : handle_(handle) {}
    ~FileHandle() { reset(); }

    void reset(HANDLE handle = INVALID_HANDLE_VALUE)
    {
        if (handle_ != INVALID_HANDLE_VALUE && handle_ != NULL)
            ::CloseHandle(handle_);
        handle_ = handle;
    }
    HANDLE get() const { return handle_; }
    operator HANDLE() const { return handle_; }
    bool valid() const { return handle_ != INVALID_HANDLE_VALUE && handle_ != NULL; }

private:
    FileHandle(const FileHandle&);
    FileHandle& operator=(const FileHandle&);
    HANDLE handle_;
};

bool write_all(HANDLE file, const char* data, size_t size)
{
    size_t done = 0;
    while (done < size) {
        DWORD chunk = (DWORD)std::min<size_t>(size - done, 1u << 20);
        DWORD written = 0;
        if (!::WriteFile(file, data + done, chunk, &written, NULL))
            return false;
        if (written == 0)
            return false;
        done += written;
    }
    return true;
}

std::wstring query_wide_header(HINTERNET request, DWORD info)
{
    DWORD length = 0;
    ::SetLastError(ERROR_SUCCESS);
    ::WinHttpQueryHeaders(request, info, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &length, WINHTTP_NO_HEADER_INDEX);
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || length == 0)
        return std::wstring();

    std::vector<wchar_t> buffer(length / sizeof(wchar_t) + 1, L'\0');
    if (!::WinHttpQueryHeaders(request, info, WINHTTP_HEADER_NAME_BY_INDEX, &buffer[0], &length,
                               WINHTTP_NO_HEADER_INDEX))
        return std::wstring();

    return std::wstring(&buffer[0]);
}

/** Parses the "CRLF separated" header block returned by WinHTTP. */
HttpHeaders parse_raw_headers(const std::string& raw)
{
    HttpHeaders result;
    size_t pos = 0;
    bool first_line = true;

    while (pos < raw.size()) {
        size_t end = raw.find("\r\n", pos);
        std::string line = raw.substr(pos, (end == std::string::npos) ? std::string::npos : end - pos);
        pos = (end == std::string::npos) ? raw.size() : end + 2;

        if (first_line) { /* "HTTP/1.1 200 OK" */
            first_line = false;
            continue;
        }

        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;

        HttpHeader header;
        header.name = trim(line.substr(0, colon));
        header.value = trim(line.substr(colon + 1));
        if (!header.name.empty())
            result.push_back(header);
    }

    return result;
}

} /* anonymous namespace */

/* --------------------------------------------------------------- public api */

std::string HttpResponse::header(const std::string& name) const
{
    for (size_t i = 0; i < headers.size(); ++i) {
        if (equals_ci(headers[i].name, name))
            return headers[i].value;
    }
    return std::string();
}

std::string HttpClient::last_error_message(const char* what)
{
    DWORD error = ::GetLastError();
    std::string message(what ? what : "WinHTTP call");
    message += " failed: ";

    LPSTR buffer = NULL;
    DWORD length = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                        FORMAT_MESSAGE_IGNORE_INSERTS,
                                    NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                    (LPSTR)&buffer, 0, NULL);
    if (length && buffer) {
        message += trim(std::string(buffer, length));
        ::LocalFree(buffer);
    } else {
        message += "unknown error";
    }

    std::ostringstream code;
    code << " (Win32 error " << (unsigned long)error << ")";
    message += code.str();
    return message;
}

std::wstring HttpClient::widen(const std::string& utf8)
{
    if (utf8.empty())
        return std::wstring();

    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int length = ::MultiByteToWideChar(code_page, flags, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (length <= 0) {
        /* not valid UTF-8 - fall back to the ANSI code page */
        code_page = CP_ACP;
        flags = 0;
        length = ::MultiByteToWideChar(code_page, flags, utf8.c_str(), (int)utf8.size(), NULL, 0);
        if (length <= 0)
            return std::wstring();
    }

    std::vector<wchar_t> buffer((size_t)length);
    ::MultiByteToWideChar(code_page, flags, utf8.c_str(), (int)utf8.size(), &buffer[0], length);
    return std::wstring(&buffer[0], (size_t)length);
}

std::string HttpClient::narrow(const std::wstring& wide)
{
    if (wide.empty())
        return std::string();

    int length = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), NULL, 0, NULL, NULL);
    if (length <= 0)
        return std::string();

    std::vector<char> buffer((size_t)length);
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &buffer[0], length, NULL, NULL);
    return std::string(&buffer[0], (size_t)length);
}

bool HttpClient::parse_url(const std::string& url, std::string& scheme, std::string& host,
                           unsigned short& port, std::string& target)
{
    scheme.clear();
    host.clear();
    target.clear();
    port = 0;

    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos)
        return false;

    scheme = lower_ascii(url.substr(0, scheme_end));
    std::string rest = url.substr(scheme_end + 3);

    size_t slash = rest.find('/');
    std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    target = (slash == std::string::npos) ? std::string("/") : rest.substr(slash);

    size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        std::string port_string = authority.substr(colon + 1);
        long value = std::strtol(port_string.c_str(), NULL, 10);
        if (value <= 0 || value > 65535)
            return false;
        port = (unsigned short)value;
        host = authority.substr(0, colon);
    } else {
        host = authority;
        port = (scheme == "https") ? 443 : 80;
    }

    return !host.empty();
}

std::string HttpClient::resolve_location(const std::string& base, const std::string& location)
{
    if (location.empty())
        return location;

    std::string lower = lower_ascii(location);
    if (lower.compare(0, 7, "http://") == 0 || lower.compare(0, 8, "https://") == 0)
        return location;

    std::string scheme, host, target;
    unsigned short port = 0;
    if (!parse_url(base, scheme, host, port, target))
        return location;

    bool default_port = (scheme == "https" && port == 443) || (scheme == "http" && port == 80);
    std::ostringstream prefix;
    prefix << scheme << "://" << host;
    if (!default_port)
        prefix << ":" << port;

    if (location[0] == '/')
        return prefix.str() + location;

    /* relative to the directory of the base url */
    size_t slash = target.find_last_of('/');
    std::string directory = (slash == std::string::npos) ? std::string("/") : target.substr(0, slash + 1);
    return prefix.str() + directory + location;
}

HttpClient::HttpClient() : session_(NULL)
{
}

HttpClient::~HttpClient()
{
    if (session_)
        ::WinHttpCloseHandle((HINTERNET)session_);
    session_ = NULL;
}

HINTERNET HttpClient::session()
{
    if (session_)
        return (HINTERNET)session_;

    HINTERNET handle = ::WinHttpOpen(L"YandexDiskCommander/1.0 (WFX plugin for Total Commander)",
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!handle) {
        /* Windows < 8.1 does not know WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY */
        handle = ::WinHttpOpen(L"YandexDiskCommander/1.0 (WFX plugin for Total Commander)",
                               WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!handle)
        throw http_error(last_error_message("WinHttpOpen"));

    /* Redirects are followed manually: the OAuth header must not be forwarded
       to the storage servers, which the download links of the API point to. */
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    ::WinHttpSetOption(handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));

    ::WinHttpSetTimeouts(handle, 30000, 30000, 60000, 60000);
    session_ = handle;
    return handle;
}

HttpResponse HttpClient::perform(const std::string& method, const std::string& url,
                                 const HttpHeaders& headers, const Body& body,
                                 const std::string& content_type, Sink& sink,
                                 const ProgressFn& progress)
{
    std::string scheme, host, target;
    unsigned short port = 0;
    if (!parse_url(url, scheme, host, port, target))
        throw http_error("Invalid URL: " + url);

    std::string scheme_lower = lower_ascii(scheme);
    if (scheme_lower != "http" && scheme_lower != "https")
        throw http_error("Unsupported URL scheme: " + scheme);

    HttpResponse result;

    WinHttpHandle connection(::WinHttpConnect((HINTERNET)session(), widen(host).c_str(), port, 0));
    if (!connection.valid())
        throw http_error(last_error_message("WinHttpConnect"));

    DWORD flags = (scheme_lower == "https") ? WINHTTP_FLAG_SECURE : 0;
    std::wstring wmethod = widen(method);
    std::wstring wtarget = widen(target);

    WinHttpHandle request(::WinHttpOpenRequest(connection.get(), wmethod.c_str(), wtarget.c_str(),
                                               NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request.valid())
        throw http_error(last_error_message("WinHttpOpenRequest"));

    /* request headers */
    std::string raw_headers;
    for (size_t i = 0; i < headers.size(); ++i) {
        if (headers[i].name.empty())
            continue;
        raw_headers += headers[i].name;
        raw_headers += ": ";
        raw_headers += headers[i].value;
        raw_headers += "\r\n";
    }
    if (!content_type.empty())
        raw_headers += "Content-Type: " + content_type + "\r\n";

    if (!raw_headers.empty()) {
        std::wstring whdrs = widen(raw_headers);
        if (!::WinHttpAddRequestHeaders(request.get(), whdrs.c_str(), (DWORD)-1L,
                                        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
            throw http_error(last_error_message("WinHttpAddRequestHeaders"));
    }

    /* body */
    bool body_from_file = (body.file != INVALID_HANDLE_VALUE && body.file != NULL);
    if (!body_from_file) {
        size_t size = body.memory ? body.memory->size() : 0;
        void* data = (size > 0) ? (void*)body.memory->data() : NULL;
        if (!::WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, data, (DWORD)size,
                                  (DWORD)size, 0))
            throw http_error(last_error_message("WinHttpSendRequest"));
    } else {
        if (!::WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0,
                                  (DWORD)body.size, 0))
            throw http_error(last_error_message("WinHttpSendRequest"));

        std::vector<char> buffer(64 * 1024);
        unsigned long long sent = 0;
        for (;;) {
            DWORD read = 0;
            if (!::ReadFile(body.file, &buffer[0], (DWORD)buffer.size(), &read, NULL))
                throw http_error(last_error_message("ReadFile (upload source)"));
            if (read == 0)
                break;

            DWORD written = 0;
            if (!::WinHttpWriteData(request.get(), &buffer[0], read, &written))
                throw http_error(last_error_message("WinHttpWriteData"));
            sent += written;

            if (progress && !progress(sent, body.size)) {
                result.aborted = true;
                return result;
            }
        }
    }

    if (!::WinHttpReceiveResponse(request.get(), NULL))
        throw http_error(last_error_message("WinHttpReceiveResponse"));

    /* status code */
    DWORD status = 0;
    DWORD status_length = sizeof(status);
    if (!::WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_length, WINHTTP_NO_HEADER_INDEX))
        throw http_error(last_error_message("WinHttpQueryHeaders (status code)"));
    result.status = (int)status;

    /* response headers */
    result.headers = parse_raw_headers(narrow(query_wide_header(request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF)));

    /* The body is only written into the local file for successful transfers,
       otherwise the file would contain the error page of the server. */
    bool write_to_file = (sink.file != INVALID_HANDLE_VALUE && sink.file != NULL) &&
                         (result.status == 200 || result.status == 206);

    unsigned long long total = 0;
    {
        std::string content_length = result.header("Content-Length");
        if (!content_length.empty())
            total = _strtoui64(content_length.c_str(), NULL, 10);
    }

    std::vector<char> buffer(64 * 1024);
    unsigned long long done = 0;
    for (;;) {
        DWORD available = 0;
        if (!::WinHttpQueryDataAvailable(request.get(), &available))
            throw http_error(last_error_message("WinHttpQueryDataAvailable"));
        if (available == 0)
            break;

        DWORD to_read = (DWORD)std::min<size_t>(available, buffer.size());
        DWORD read = 0;
        if (!::WinHttpReadData(request.get(), &buffer[0], to_read, &read))
            throw http_error(last_error_message("WinHttpReadData"));
        if (read == 0)
            break;

        if (write_to_file) {
            if (!write_all(sink.file, &buffer[0], read))
                throw http_error(last_error_message("WriteFile (download target)"));
        } else {
            result.body.append(&buffer[0], read);
        }

        done += read;
        if (progress && !progress(done, total)) {
            result.aborted = true;
            break;
        }
    }

    return result;
}

HttpResponse HttpClient::with_redirects(const std::string& method, const std::string& url,
                                       HttpHeaders headers, const Body& body,
                                       const std::string& content_type, Sink& sink,
                                       const ProgressFn& progress, bool follow_redirects)
{
    const int max_redirects = 5;

    std::string current_method = method;
    std::string current_url = url;
    bool send_body = true;

    for (int hop = 0; ; ++hop) {
        Body request_body;
        if (send_body)
            request_body = body;

        HttpResponse response = perform(current_method, current_url, headers, request_body, content_type,
                                        sink, progress);
        if (response.aborted)
            return response;

        bool is_redirect = (response.status >= 300 && response.status < 400 && response.status != 304);
        if (!follow_redirects || !is_redirect)
            return response;

        std::string location = response.header("Location");
        if (location.empty() || hop >= max_redirects)
            return response;

        std::string next_url = resolve_location(current_url, location);

        /* The Authorization header must not be forwarded to a different host -
           Yandex Disk answers with links to the storage servers. */
        std::string scheme, old_host, new_host, target_part;
        unsigned short old_port = 0, new_port = 0;
        parse_url(current_url, scheme, old_host, old_port, target_part);
        parse_url(next_url, scheme, new_host, new_port, target_part);

        if (!equals_ci(old_host, new_host) || old_port != new_port ||
            lower_ascii(current_url.substr(0, current_url.find(':'))) !=
            lower_ascii(next_url.substr(0, next_url.find(':')))) {
            HttpHeaders filtered;
            for (size_t i = 0; i < headers.size(); ++i) {
                if (!equals_ci(headers[i].name, "Authorization"))
                    filtered.push_back(headers[i]);
            }
            headers = filtered;
        }

        if (response.status == 303 || ((response.status == 301 || response.status == 302) &&
                                       !equals_ci(current_method, "GET") && !equals_ci(current_method, "HEAD"))) {
            /* the classical behaviour: switch to GET and drop the body */
            current_method = "GET";
            send_body = false;
        } else if (body.file != INVALID_HANDLE_VALUE && body.file != NULL) {
            /* 307/308 - the body has to be sent again */
            ::SetFilePointer(body.file, 0, NULL, FILE_BEGIN);
        }

        current_url = next_url;
    }
}

HttpResponse HttpClient::request(const std::string& method, const std::string& url,
                                 const HttpHeaders& headers, const std::string& body,
                                 const std::string& content_type, bool follow_redirects,
                                 const ProgressFn& progress)
{
    Body request_body;
    request_body.memory = &body;
    request_body.size = body.size();

    Sink sink; /* memory sink: the body is collected by perform() itself */
    return with_redirects(method, url, headers, request_body, content_type, sink, progress, follow_redirects);
}

HttpResponse HttpClient::download_to_file(const std::string& url, const HttpHeaders& headers,
                                          const std::wstring& local_path, const ProgressFn& progress)
{
    std::wstring full_path = extended_path(local_path);
    FileHandle file(::CreateFileW(full_path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
    if (!file.valid())
        throw http_error("Cannot create the local file: " + narrow(local_path) + " - " +
                         last_error_message("CreateFileW"));

    Sink sink;
    sink.file = file.get();

    Body no_body;
    HttpResponse response = with_redirects("GET", url, headers, no_body, std::string(), sink, progress, true);

    if (response.aborted || response.status < 200 || response.status >= 300) {
        /* do not leave an empty or a half downloaded file behind */
        file.reset();
        ::DeleteFileW(full_path.c_str());
    }

    return response;
}

HttpResponse HttpClient::upload_file(const std::string& method, const std::string& url,
                                     const HttpHeaders& headers, const std::wstring& local_path,
                                     const std::string& content_type, const ProgressFn& progress)
{
    std::wstring full_path = extended_path(local_path);
    FileHandle file(::CreateFileW(full_path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
    if (!file.valid())
        throw http_error("Cannot open the local file: " + narrow(local_path) + " - " +
                         last_error_message("CreateFileW"));

    LARGE_INTEGER file_size;
    file_size.QuadPart = 0;
    if (!::GetFileSizeEx(file.get(), &file_size))
        throw http_error(last_error_message("GetFileSizeEx"));

    Body body;
    body.file = file.get();
    body.size = (unsigned long long)file_size.QuadPart;

    Sink sink;
    return with_redirects(method, url, headers, body, content_type, sink, progress, true);
}

} /* namespace ydisk */
