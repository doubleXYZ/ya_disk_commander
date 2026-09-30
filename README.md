# Yandex Disk Commander — Windows port

[![License: LGPL v2.1+](https://img.shields.io/badge/License-LGPL%20v2.1%2B-blue.svg)](LICENSE)

> **English** | [Русский](README.ru.md)

File system plugin (WFX) for **Total Commander** and **Double Commander** which lets
you work with **Yandex Disk** directly from the file manager on Windows 10/11 x64.

The project was inspired by [ivanenko/ydisk_commander](https://github.com/ivanenko/ydisk_commander)
(the original Linux/*nix plugin by Danil Ivanenko), but the code was **rewritten almost
from scratch for Windows 10 x64**: WinHTTP instead of cpp-httplib + OpenSSL, DPAPI for
token protection, native Win32 dialogs created at runtime instead of GTK/LCL and a plain
Winsock server for the OAuth callback. Only the vendored [json11](https://github.com/dropbox/json11)
library, the WFX SDK header and the general feature set are shared with the original project.

## Features

- Browse, download, upload, create folders (F7), rename/move (F6), copy (F5) and
  delete (F8) files and folders on Yandex Disk
- Recycle bin support: the `/.Trash` folder, per-file deletion, empty-trash command
- Server-side download of files from the Internet directly to the disk
  (`quote download <url>`) — the traffic does not pass through your PC
- Folder size calculation (Alt+Shift+Enter)
- OAuth 2.0 authorization via Yandex ID — three methods (see below)
- Flexible token storage: don't store / DPAPI-encrypted ini file / Total Commander
  password manager
- Unicode file names, files larger than 4 GB, transfer progress with cancel
- Zero external dependencies — only system DLLs (WinHTTP, DPAPI, Winsock)

## Installation

1. Download `ydisk_commander.wfx64` from the
   [Releases](../../releases) page (or build it yourself — see below).
2. In Total Commander: **Configuration → Options → Plugins → File system plugins (.WFX)
   → Configure → Add** and select the downloaded file.
3. Open **Network Neighborhood** in Total Commander — a **Yandex Disk** entry appears.

> **Note:** Total Commander locks the plugin file while it is running. Close Total
> Commander before replacing the plugin with a newer build.

## Configuration

In Network Neighborhood right-click **Yandex Disk → Properties** (or press
Alt+Enter on it) to open the settings dialog.

### Getting an OAuth token

Three methods are available:

- **Confirmation code (device flow, default)** — the plugin shows a short code and a
  link. Open the link in any browser, sign in to Yandex and enter the code. No local
  listener is required, works behind strict firewalls.
- **Browser + local callback** — the plugin starts a temporary web server on
  `127.0.0.1:3359`, opens the Yandex authorization page and catches the token from
  the redirect automatically.
- **Enter the token manually** — paste a token you already have.

The **Test connection** button verifies the token and shows your disk quota
(total / used / free space).

### Token storage

- **Don't save** — the token lives only in memory and is asked again on the next
  Total Commander start.
- **Configuration file** — the token is stored in the plugin ini file, encrypted
  with Windows DPAPI (only your Windows account can decrypt it).
- **Total Commander password manager** — the token is stored by Total Commander
  itself (master password protected, if enabled in TC).

To revoke a token later: **Yandex ID → Доступы к данным** (or «Выйти везде»).

## Custom commands

Type these into the Total Commander command line while a Yandex Disk panel is active:

- `quote trash` — jump to the trash folder (`/.Trash`)
- `quote trash clean` — permanently delete everything in the trash (asks for
  confirmation)
- `quote download <URL> [new_file_name]` — tell Yandex Disk to download a file from
  the Internet **server-side** into the current folder; `new_file_name` is optional

Files in the trash can also be deleted individually with F8 from the `/.Trash` folder.

## Building from source

Requirements:

- **MinGW-w64 g++** (C++11 is enough), e.g. from [winlibs.com](https://winlibs.com)
  or MSYS2, with `g++.exe` on the `PATH`
- **PowerShell** 5+

Build:

```powershell
scripts\build.ps1          # build\ydisk_commander.wfx64 (64-bit, for 64-bit TC)
scripts\build.ps1 -Test    # the same + builds and runs build\smoke.exe tests
scripts\build.ps1 -X86     # build\ydisk_commander.wfx (32-bit, for 32-bit TC)
```

The `-X86` mode requires a 32-bit MinGW toolchain; without one it still compiles the
sources and verifies that the export definitions match the emitted symbols.

The plugin links only against system libraries (`winhttp`, `crypt32`, `ws2_32`,
`shell32`, `user32`, `gdi32`) — no third-party dependencies to install.

## Project layout

| Path | Description |
| --- | --- |
| `src/plugin.cpp` | WFX entry points (Unicode API + thin ANSI wrappers) |
| `src/restclient.*` | Yandex Disk Web API client |
| `src/http_client.*` | WinHTTP transport with progress reporting and cancel |
| `src/http_server.*` | tiny local HTTP server for the OAuth browser callback |
| `src/auth.*` | OAuth 2.0 flows (device code, browser callback, manual) |
| `src/dialogs.*` | native Win32 settings dialog, created fully at runtime |
| `src/plugin_utils.*` | UTF-8/UTF-16 conversion, paths, ini file, DPAPI |
| `src/json11.*` | vendored JSON library |
| `src/wfxplugin.h` | Total Commander WFX interface declaration |
| `tests/smoke.cpp` | smoke tests (config, DPAPI, HTTP, JSON) |
| `scripts/build.ps1` | build script |

## Credits

- [ivanenko/ydisk_commander](https://github.com/ivanenko/ydisk_commander) —
  the original Linux/*nix plugin by **Danil Ivanenko** this Windows port was inspired by.
- [json11](https://github.com/dropbox/json11) — tiny JSON library by Dropbox, Inc. (MIT license).
- [WFX-SDK](https://github.com/ghisler/WFX-SDK) — Total Commander file system plugin
  interface by Christian Ghisler.

## License

This project is free software, distributed under the terms of the
**GNU Lesser General Public License, version 2.1 or (at your option) any later
version** — the same license as the original project. See [LICENSE](LICENSE).

```
Copyright (C) 2019 Ivanenko Danil (ivanenko.danil@gmail.com)  — original Linux version
Copyright (C) 2026 doublexyz                                  — Windows port
```
