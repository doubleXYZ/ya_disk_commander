/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)

Minimal HTTP/1.1 server on 127.0.0.1 which is used as the OAuth redirect
target ("Get token in browser" method). It replaces the cpp-httplib based
server of the *nix version of the plugin - plain Winsock is enough for the
few requests which are needed to catch the token.

Copyright (C) 2026 doublexyz, LGPL 2.1 or later (see README.md)
*/

#ifndef YDISK_HTTP_SERVER_H
#define YDISK_HTTP_SERVER_H

#include "common.h"

#include <functional>
#include <stdint.h>
#include <string>

namespace ydisk {

class LocalHttpServer {
public:
    explicit LocalHttpServer(unsigned short port);
    ~LocalHttpServer();

    LocalHttpServer(const LocalHttpServer&) = delete;
    LocalHttpServer& operator=(const LocalHttpServer&) = delete;

    /** Binds to 127.0.0.1:port and starts listening. */
    bool start(std::string& error);
    /** Closes the listening socket (a pending wait_token() call returns). */
    void stop();
    /** Waits (up to timeout_ms) for the browser to deliver the token.
        on_idle is called regularly - it is used to keep the GUI responsive;
        returning false from it stops the wait. */
    std::string wait_token(int timeout_ms, const std::function<bool()>& on_idle = std::function<bool()>());

    unsigned short port() const { return port_; }

    /** HTML page which extracts the token from the url fragment. */
    static std::string callback_page(unsigned short port);

    /** Routes a parsed request line ("GET /path?query HTTP/1.1") and returns
        the HTTP response. token_out receives the token if one was delivered.
        Exposed (static) for unit testing. */
    static std::string handle_request(unsigned short port, const std::string& request_line,
                                      const std::string& query, std::string* token_out);

private:
    static std::string query_value(const std::string& query, const std::string& name);

    unsigned short port_;
    uintptr_t listen_socket_; /* INVALID_SOCKET == (uintptr_t)-1 */
    std::string token_;
};

} /* namespace ydisk */

#endif /* YDISK_HTTP_SERVER_H */
