/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)

Windows port inspired by https://github.com/ivanenko/ydisk_commander
Copyright (C) 2019 Ivanenko Danil (ivanenko.danil@gmail.com)      - original Linux version
Copyright (C) 2026 doublexyz                                      - Windows port

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.
*/

#ifndef YDISK_COMMON_H
#define YDISK_COMMON_H

/*
 * All types/handles which the plugin interface uses (HANDLE, WCHAR, FILETIME,
 * WIN32_FIND_DATAW, ...) are the real ones from the Windows SDK - the plugin is
 * Windows-only, so there is no need to emulate them (as the *nix version did).
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <stdint.h>

/*
 * Calling convention of all plugin functions / of all callbacks provided by the
 * plugin host.
 *
 * On x86 __stdcall is required (Total Commander expects stdcall functions and
 * cleans the stack itself). On x64 there is a single calling convention, the
 * instruction is meaningless and MinGW warns about it, therefore it is not used.
 */
#if defined(_M_X64) || defined(__x86_64__) || defined(_WIN64)
  #define DCPCALL
#elif defined(_MSC_VER)
  #define DCPCALL __stdcall
#else
  #define DCPCALL __attribute__((stdcall))
#endif

/*
 * Marks a function as a plugin export.
 *
 * For x64 (the recommended target) the plain compiler export is used - there is
 * no name decoration on x64, so Total Commander finds e.g. "FsFindFirstW".
 * For x86 builds the stdcall "@N" suffix would break the lookup, so the x86
 * build exports through src/ydisk_commander.def instead (define
 * YDISK_NO_DLLEXPORT for that build).
 */
#ifndef YDISK_NO_DLLEXPORT
  #define YDISK_EXPORT extern "C" __declspec(dllexport)
#else
  #define YDISK_EXPORT extern "C"
#endif

/* Total Commander uses MAX_PATH for FsDefaultParamStruct.DefaultIniName,
   but passes remote paths up to 1023 characters into the "W" functions. */
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#define YDISK_MAX_REMOTE_PATH 1024

#endif /* YDISK_COMMON_H */
