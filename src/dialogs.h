/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)

Native Windows settings dialog (account / OAuth configuration).
It replaces the GTK/LCL dialog of the *nix version and is created fully at
runtime - the plugin needs no .rc resources and no GUI toolkit.

Copyright (C) 2026 doublexyz, LGPL 2.1 or later (see README.md)
*/

#ifndef YDISK_DIALOGS_H
#define YDISK_DIALOGS_H

#include "common.h"

#include <functional>
#include <string>

namespace ydisk {

/** Window class of the settings window (also used by the unit tests). */
extern const wchar_t* kSettingsWindowClass;

/** Control ids (used by the tests to drive the dialog). */
enum SettingsControlId {
    kIdRadioDevice = 1001,   /* token: browser + confirmation code */
    kIdRadioBrowser = 1002,  /* token: browser + local callback server */
    kIdRadioManual = 1003,   /* token: manual input */
    kIdEditToken = 1010,
    kIdButtonGetToken = 1011,
    kIdRadioSaveNone = 1020,
    kIdRadioSaveConfig = 1021,
    kIdRadioSavePasswordManager = 1022,
    kIdStatus = 1030,
    kIdButtonTest = 1031,
    kIdLabelToken = 1040,
    kIdLabelHint = 1041,
    kIdGroupToken = 1042,
    kIdGroupSave = 1043
};

/** Stores the token in the Total Commander password manager. */
typedef std::function<bool(const std::string& token)> SaveTokenFn;

/**
 * Shows the modal settings dialog.
 *
 * Returns true when the user saved the settings (yandex_disk.ini was written),
 * false when the dialog was cancelled. `error` is filled when saving failed.
 * If save_token is set and the password manager was selected, it is used to
 * store the token (Total Commander's crypto callback).
 */
bool show_settings_dialog(HWND parent, const std::wstring& config_path,
                          const SaveTokenFn& save_token, std::string& error);

} /* namespace ydisk */

#endif /* YDISK_DIALOGS_H */
