/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)

OAuth 2.0 authorization against Yandex (https://yandex.ru/dev/id/doc/).

Two interactive methods are implemented:
  * "device"  - the confirmation code flow: the plugin shows a short code, the
                user enters it on the Yandex page, the plugin polls for the token.
                No local listener and no browser automation is needed (default).
  * "browser" - the implicit flow used by the *nix version of the plugin: a
                temporary local web server (127.0.0.1:3359) catches the token
                from the redirect of the browser.

Copyright (C) 2026 doublexyz, LGPL 2.1 or later (see README.md)
*/

#ifndef YDISK_AUTH_H
#define YDISK_AUTH_H

#include "common.h"

#include <functional>
#include <string>

namespace ydisk {

/* OAuth application id of the plugin, the same one which is used by the
   original *nix plugin. It can be changed by the user in the configuration
   file ("client_id"). */
extern const char* kDefaultClientId;

/* Port of the local callback server of the implicit flow - it must match the
   redirect uri which is registered for the application at Yandex. */
const unsigned short kOauthCallbackPort = 3359;

/** Reports progress/instructions to the user interface. */
typedef std::function<void(const std::wstring&)> StatusFn;

/* Waits the given number of milliseconds; the settings dialog passes a
   function which keeps the window responsive (message pump) instead. */
typedef std::function<void(int)> WaitFn;

/** Returns true when the operation should be cancelled (dialog closed). */
typedef std::function<bool()> CancelFn;

/* Returns the token or an empty string; `error` is filled in that case. */
std::string acquire_token_device(const std::string& client_id, const StatusFn& status,
                                 const WaitFn& wait, const CancelFn& cancel, std::string& error);

std::string acquire_token_browser(const std::string& client_id, const StatusFn& status,
                                  const WaitFn& wait, const CancelFn& cancel, std::string& error);

/** Opens an url in the default browser. */
bool open_in_browser(const std::wstring& url);

} /* namespace ydisk */

#endif /* YDISK_AUTH_H */
