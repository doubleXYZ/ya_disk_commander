/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)
Local OAuth callback server - see http_server.h.

Copyright (C) 2026 doublexyz, LGPL 2.1 or later (see README.md)
*/

#include "http_server.h"
#include "plugin_utils.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <vector>

namespace ydisk {

namespace {

const uintptr_t kInvalidSocket = (uintptr_t)(SOCKET)INVALID_SOCKET;

void close_socket(uintptr_t& socket_handle)
{
    if (socket_handle != kInvalidSocket) {
        ::closesocket((SOCKET)socket_handle);
        socket_handle = kInvalidSocket;
    }
}

std::string url_decode(const std::string& text)
{
    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') {
            result += ' ';
        } else if (text[i] == '%' && i + 2 < text.size()) {
            char hex[3] = {text[i + 1], text[i + 2], 0};
            result += (char)::strtol(hex, NULL, 16);
            i += 2;
        } else {
            result += text[i];
        }
    }

    return result;
}

std::vector<std::string> split_ascii(const std::string& text, char separator)
{
    std::vector<std::string> result;
    size_t previous = 0;

    while (true) {
        size_t position = text.find(separator, previous);
        if (position == std::string::npos) {
            result.push_back(text.substr(previous));
            break;
        }
        result.push_back(text.substr(previous, position - previous));
        previous = position + 1;
    }

    return result;
}

std::string http_response(int status, const char* status_text, const std::string& content_type,
                          const std::string& body)
{
    (void)status;
    std::string header;
    header += "HTTP/1.1 " + std::string(status_text) + "\r\n";
    header += "Content-Type: " + content_type + "\r\n";
    header += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    header += "Connection: close\r\n";
    header += "Cache-Control: no-store\r\n\r\n";
    return header + body;
}

bool send_all(SOCKET socket_handle, const std::string& data)
{
    size_t sent = 0;
    while (sent < data.size()) {
        int chunk = (int)((data.size() - sent > 16384) ? 16384 : data.size() - sent);
        int result = ::send(socket_handle, data.data() + sent, chunk, 0);
        if (result <= 0)
            return false;
        sent += (size_t)result;
    }
    return true;
}

} /* anonymous namespace */

LocalHttpServer::LocalHttpServer(unsigned short port)
    : port_(port), listen_socket_(kInvalidSocket)
{
}

LocalHttpServer::~LocalHttpServer()
{
    stop();
}

bool LocalHttpServer::start(std::string& error)
{
    error.clear();

    WSADATA wsa_data;
    if (::WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        error = "WSAStartup failed";
        return false;
    }

    SOCKET server = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        error = "Cannot create the listening socket: " + format_win_error(WSAGetLastError());
        ::WSACleanup();
        return false;
    }

    BOOL exclusive = TRUE;
    ::setsockopt(server, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof(exclusive));

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = ::htons(port_);
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK); /* 127.0.0.1 - local only! */

    if (::bind(server, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        error = "Cannot bind to 127.0.0.1:" + std::to_string(port_) + " - " +
                format_win_error(WSAGetLastError());
        ::closesocket(server);
        ::WSACleanup();
        return false;
    }

    if (::listen(server, 5) == SOCKET_ERROR) {
        error = "Cannot listen on 127.0.0.1:" + std::to_string(port_) + " - " +
                format_win_error(WSAGetLastError());
        ::closesocket(server);
        ::WSACleanup();
        return false;
    }

    listen_socket_ = (uintptr_t)server;
    return true;
}

void LocalHttpServer::stop()
{
    if (listen_socket_ != kInvalidSocket) {
        close_socket(listen_socket_);
        ::WSACleanup();
    }
}

std::string LocalHttpServer::callback_page(unsigned short port)
{
    std::string port_text = std::to_string(port);

    std::string page =
        "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">\n"
        "<title>Yandex Disk Commander</title>\n"
        "<style>body{font-family:Segoe UI,Arial,sans-serif;margin:40px;}"
        "h2{color:#c00}</style>\n</head><body>\n"
        "<h2 id=\"message\">Checking the authorization result...</h2>\n"
        "<script type=\"text/javascript\">\n"
        "var hash = window.location.hash.substr(1);\n"
        "var i = hash.search(\"access_token=\");\n"
        "if (i < 0) {\n"
        "  document.getElementById(\"message\").textContent = \"Error: the access token was not found in the URL\";\n"
        "} else {\n"
        "  var token = hash.substr(i).split(\"&\")[0].split(\"=\")[1];\n"
        "  if (token) {\n"
        "    var xhttp = new XMLHttpRequest();\n"
        "    var shown = false;\n"
        "    xhttp.onreadystatechange = function() {\n"
        "      /* readyState 4 alone already means the answer arrived. */\n"
        "      if (!shown && this.readyState == 4) {\n"
        "        shown = true;\n"
        "        document.getElementById(\"message\").style.color = \"#080\";\n"
        "        document.getElementById(\"message\").textContent =\n"
        "            \"Done! The token was passed to Total Commander. You can close this tab.\";\n"
        "      }\n"
        "    };\n"
        "    xhttp.open(\"GET\", \"http://127.0.0.1:" + port_text + "/receive_token?access_token=\" + token, true);\n"
        "    xhttp.send();\n"
        "  }\n"
        "}\n"
        "</script>\n</body></html>\n";

    return page;
}

std::string LocalHttpServer::query_value(const std::string& query, const std::string& name)
{
    std::vector<std::string> pairs = split_ascii(query, '&');
    for (size_t i = 0; i < pairs.size(); ++i) {
        size_t equals = pairs[i].find('=');
        std::string key = (equals == std::string::npos) ? pairs[i] : pairs[i].substr(0, equals);
        if (key != name)
            continue;
        std::string value = (equals == std::string::npos) ? std::string() : pairs[i].substr(equals + 1);
        return url_decode(value);
    }
    return std::string();
}

std::string LocalHttpServer::handle_request(unsigned short port, const std::string& request_line,
                                            const std::string& query, std::string* token_out)
{
    /* request_line has the form "GET /receive_token?... HTTP/1.1". */
    size_t first_space = request_line.find(' ');
    size_t second_space = first_space == std::string::npos
                              ? std::string::npos
                              : request_line.find(' ', first_space + 1);
    std::string path;
    if (first_space != std::string::npos) {
        size_t begin = first_space + 1;
        size_t length = second_space == std::string::npos ? std::string::npos : second_space - begin;
        path = request_line.substr(begin, length);
    }
    size_t query_position = path.find('?');
    if (query_position != std::string::npos)
        path = path.substr(0, query_position);

    if (path == "/receive_token" || path == "/get_token") {
        std::string token = query_value(query, "access_token");
        if (token.empty())
            token = query_value(query, "code");

        if (!token.empty()) {
            if (token_out)
                *token_out = token;
            return http_response(200, "200 OK", "text/plain; charset=utf-8", "OAuth token received");
        }

        /* The registered redirect uri of the application is "/get_token" and
           Yandex appends the token as the url *fragment* ("#access_token=...")
           which never reaches the server. Answer with the helper page whose
           script extracts the fragment and posts it to /receive_token. */
        if (path == "/get_token")
            return http_response(200, "200 OK", "text/html; charset=utf-8", callback_page(port));

        return http_response(400, "400 Bad Request", "text/plain; charset=utf-8",
                             "access_token is missing");
    }

    if (path == "/" || path == "/index.html")
        return http_response(200, "200 OK", "text/html; charset=utf-8", callback_page(port));

    return http_response(404, "404 Not Found", "text/plain; charset=utf-8", "Not found");
}

std::string LocalHttpServer::wait_token(int timeout_ms, const std::function<bool()>& on_idle)
{
    if (listen_socket_ == kInvalidSocket)
        return std::string();

    DWORD start = ::GetTickCount();
    while (token_.empty()) {
        if (on_idle && !on_idle())
            break;

        if (::GetTickCount() - start > (DWORD)timeout_ms)
            break;

        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET((SOCKET)listen_socket_, &read_set);

        timeval timeout = {0, 200000}; /* 200 ms */
        int ready = ::select(0, &read_set, NULL, NULL, &timeout);
        if (ready == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEINTR)
                continue;
            break;
        }
        if (ready == 0)
            continue;

        SOCKET client = ::accept((SOCKET)listen_socket_, NULL, NULL);
        if (client == INVALID_SOCKET)
            continue;

        /* read the request (headers are enough for our tiny API) */
        DWORD receive_timeout = 2000;
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&receive_timeout, sizeof(receive_timeout));
        std::string request;
        char buffer[2048];
        while (request.find("\r\n\r\n") == std::string::npos) {
            int received = ::recv(client, buffer, (int)sizeof(buffer), 0);
            if (received <= 0)
                break;
            request.append(buffer, (size_t)received);
            if (request.size() > 16384)
                break;
        }

        std::string request_line = request.substr(0, request.find("\r\n"));
        if (request_line.compare(0, 4, "GET ") != 0) {
            ::closesocket(client);
            continue;
        }
        /* request target sits between the first and the second space of
           "GET /path?query HTTP/1.1"; without the trailing cut the last query
           value would keep the " HTTP/1.1" suffix. */
        size_t first_space = request_line.find(' ');
        size_t second_space = request_line.find(' ', first_space + 1);
        std::string path_and_query = request_line.substr(
            first_space + 1,
            second_space == std::string::npos ? std::string::npos : second_space - first_space - 1);
        size_t question = path_and_query.find('?');
        std::string query = (question == std::string::npos) ? std::string()
                                                           : path_and_query.substr(question + 1);

        std::string response = handle_request(port_, request_line, query, &token_);
        send_all(client, response);
        ::shutdown(client, SD_BOTH);
        ::closesocket(client);

        /* The token arrived via the XHR of the callback page. Give the
           browser a moment to process the answer and paint the "Done!"
           message before the listening socket disappears - otherwise the
           tab stays on "Checking the authorization result...". */
        if (!token_.empty())
            ::Sleep(1500);
    }

    stop();
    return token_;
}

} /* namespace ydisk */
