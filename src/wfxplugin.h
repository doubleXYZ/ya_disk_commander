/*
Yandex Disk WFX plugin for Total Commander / Double Commander (Windows)

Declaration of the Total Commander file system plugin interface (WFX).
Based on fsplugin.h version 2.1 (27.April.2010) from the official WFX plugin
SDK (https://github.com/ghisler/WFX-SDK) - the constants and the structures are
kept identical so that Total Commander can talk to the plugin.
*/

#ifndef YDISK_WFXPLUGIN_H
#define YDISK_WFXPLUGIN_H

#include "common.h"

/* ids for FsGetFile */
#define FS_FILE_OK 0
#define FS_FILE_EXISTS 1
#define FS_FILE_NOTFOUND 2
#define FS_FILE_READERROR 3
#define FS_FILE_WRITEERROR 4
#define FS_FILE_USERABORT 5
#define FS_FILE_NOTSUPPORTED 6
#define FS_FILE_EXISTSRESUMEALLOWED 7

#define FS_EXEC_OK 0
#define FS_EXEC_ERROR 1
#define FS_EXEC_YOURSELF -1
#define FS_EXEC_SYMLINK -2

#define FS_COPYFLAGS_OVERWRITE 1
#define FS_COPYFLAGS_RESUME 2
#define FS_COPYFLAGS_MOVE 4
#define FS_COPYFLAGS_EXISTS_SAMECASE 8
#define FS_COPYFLAGS_EXISTS_DIFFERENTCASE 16

/* flags for tRequestProc */
#define RT_Other 0
#define RT_UserName 1
#define RT_Password 2
#define RT_Account 3
#define RT_UserNameFirewall 4
#define RT_PasswordFirewall 5
#define RT_TargetDir 6
#define RT_URL 7
#define RT_MsgOK 8
#define RT_MsgYesNo 9
#define RT_MsgOKCancel 10

/* flags for tLogProc */
#define MSGTYPE_CONNECT 1
#define MSGTYPE_DISCONNECT 2
#define MSGTYPE_DETAILS 3
#define MSGTYPE_TRANSFERCOMPLETE 4
#define MSGTYPE_CONNECTCOMPLETE 5
#define MSGTYPE_IMPORTANTERROR 6
#define MSGTYPE_OPERATIONCOMPLETE 7

/* flags for FsStatusInfo */
#define FS_STATUS_START 0
#define FS_STATUS_END 1

#define FS_STATUS_OP_LIST 1
#define FS_STATUS_OP_GET_SINGLE 2
#define FS_STATUS_OP_GET_MULTI 3
#define FS_STATUS_OP_PUT_SINGLE 4
#define FS_STATUS_OP_PUT_MULTI 5
#define FS_STATUS_OP_RENMOV_SINGLE 6
#define FS_STATUS_OP_RENMOV_MULTI 7
#define FS_STATUS_OP_DELETE 8
#define FS_STATUS_OP_ATTRIB 9
#define FS_STATUS_OP_MKDIR 10
#define FS_STATUS_OP_EXEC 11
#define FS_STATUS_OP_CALCSIZE 12
#define FS_STATUS_OP_SEARCH 13
#define FS_STATUS_OP_SEARCH_TEXT 14
#define FS_STATUS_OP_SYNC_SEARCH 15
#define FS_STATUS_OP_SYNC_GET 16
#define FS_STATUS_OP_SYNC_PUT 17
#define FS_STATUS_OP_SYNC_DELETE 18
#define FS_STATUS_OP_GET_MULTI_THREAD 19
#define FS_STATUS_OP_PUT_MULTI_THREAD 20

#define FS_ICONFLAG_SMALL 1
#define FS_ICONFLAG_BACKGROUND 2

#define FS_ICON_USEDEFAULT 0
#define FS_ICON_EXTRACTED 1
#define FS_ICON_EXTRACTED_DESTROY 2
#define FS_ICON_DELAYED 3

#define FS_BITMAP_NONE 0
#define FS_BITMAP_EXTRACTED 1
#define FS_BITMAP_EXTRACT_YOURSELF 2
#define FS_BITMAP_EXTRACT_YOURSELF_ANDDELETE 3
#define FS_BITMAP_CACHE 256

#define FS_CRYPT_SAVE_PASSWORD 1
#define FS_CRYPT_LOAD_PASSWORD 2
#define FS_CRYPT_LOAD_PASSWORD_NO_UI 3 /* Load password only if master password has already been entered! */
#define FS_CRYPT_COPY_PASSWORD 4       /* Copy encrypted password to new connection name */
#define FS_CRYPT_MOVE_PASSWORD 5       /* Move password when renaming a connection */
#define FS_CRYPT_DELETE_PASSWORD 6     /* Delete password */

#define FS_CRYPTOPT_MASTERPASS_SET 1   /* The user already has a master password defined */

#define BG_DOWNLOAD 1                  /* Plugin supports downloads in background */
#define BG_UPLOAD 2                    /* Plugin supports uploads in background */
#define BG_ASK_USER 4                  /* Plugin requires separate connection for background transfers */

typedef struct {
    DWORD SizeLow, SizeHigh;
    FILETIME LastWriteTime;
    int Attr;
} RemoteInfoStruct;

typedef struct {
    int size;
    DWORD PluginInterfaceVersionLow;
    DWORD PluginInterfaceVersionHi;
    char DefaultIniName[MAX_PATH];
} FsDefaultParamStruct;

extern "C" {

/* callback functions provided by the plugin host */
typedef int (DCPCALL *tProgressProc)(int PluginNr, char* SourceName,
             char* TargetName, int PercentDone);
typedef int (DCPCALL *tProgressProcW)(int PluginNr, WCHAR* SourceName,
             WCHAR* TargetName, int PercentDone);
typedef void (DCPCALL *tLogProc)(int PluginNr, int MsgType, char* LogString);
typedef void (DCPCALL *tLogProcW)(int PluginNr, int MsgType, WCHAR* LogString);

typedef BOOL (DCPCALL *tRequestProc)(int PluginNr, int RequestType, char* CustomTitle,
              char* CustomText, char* ReturnedText, int maxlen);
typedef BOOL (DCPCALL *tRequestProcW)(int PluginNr, int RequestType, WCHAR* CustomTitle,
              WCHAR* CustomText, WCHAR* ReturnedText, int maxlen);
typedef int (DCPCALL *tCryptProc)(int PluginNr, int CryptoNr, int Mode,
              char* ConnectionName, char* Password, int maxlen);
typedef int (DCPCALL *tCryptProcW)(int PluginNr, int CryptoNr, int Mode,
              WCHAR* ConnectionName, WCHAR* Password, int maxlen);

} /* extern "C" */

/*
 * Function prototypes (see the WFX plugin SDK for the full description of every
 * function). The plugin implements the Unicode ("...W") variants and provides
 * thin ANSI wrappers for them, therefore both sets are exported.
 */
extern "C" {
int DCPCALL FsInit(int PluginNr, tProgressProc pProgressProc,
                   tLogProc pLogProc, tRequestProc pRequestProc);
int DCPCALL FsInitW(int PluginNr, tProgressProcW pProgressProcW,
                    tLogProcW pLogProcW, tRequestProcW pRequestProcW);
void DCPCALL FsSetCryptCallback(tCryptProc pCryptProc, int CryptoNr, int Flags);
void DCPCALL FsSetCryptCallbackW(tCryptProcW pCryptProcW, int CryptoNr, int Flags);
HANDLE DCPCALL FsFindFirst(char* Path, WIN32_FIND_DATAA* FindData);
HANDLE DCPCALL FsFindFirstW(WCHAR* Path, WIN32_FIND_DATAW* FindData);
BOOL DCPCALL FsFindNext(HANDLE Hdl, WIN32_FIND_DATAA* FindData);
BOOL DCPCALL FsFindNextW(HANDLE Hdl, WIN32_FIND_DATAW* FindData);
int DCPCALL FsFindClose(HANDLE Hdl);
BOOL DCPCALL FsMkDir(char* Path);
BOOL DCPCALL FsMkDirW(WCHAR* Path);
int DCPCALL FsExecuteFile(HWND MainWin, char* RemoteName, char* Verb);
int DCPCALL FsExecuteFileW(HWND MainWin, WCHAR* RemoteName, WCHAR* Verb);
int DCPCALL FsRenMovFile(char* OldName, char* NewName, BOOL Move,
                         BOOL OverWrite, RemoteInfoStruct* ri);
int DCPCALL FsRenMovFileW(WCHAR* OldName, WCHAR* NewName, BOOL Move,
                          BOOL OverWrite, RemoteInfoStruct* ri);
int DCPCALL FsGetFile(char* RemoteName, char* LocalName, int CopyFlags, RemoteInfoStruct* ri);
int DCPCALL FsGetFileW(WCHAR* RemoteName, WCHAR* LocalName, int CopyFlags, RemoteInfoStruct* ri);
int DCPCALL FsPutFile(char* LocalName, char* RemoteName, int CopyFlags);
int DCPCALL FsPutFileW(WCHAR* LocalName, WCHAR* RemoteName, int CopyFlags);
BOOL DCPCALL FsDeleteFile(char* RemoteName);
BOOL DCPCALL FsDeleteFileW(WCHAR* RemoteName);
BOOL DCPCALL FsRemoveDir(char* RemoteName);
BOOL DCPCALL FsRemoveDirW(WCHAR* RemoteName);
BOOL DCPCALL FsDisconnect(char* DisconnectRoot);
BOOL DCPCALL FsDisconnectW(WCHAR* DisconnectRoot);
BOOL DCPCALL FsSetAttr(char* RemoteName, int NewAttr);
BOOL DCPCALL FsSetAttrW(WCHAR* RemoteName, int NewAttr);
BOOL DCPCALL FsSetTime(char* RemoteName, FILETIME* CreationTime,
                       FILETIME* LastAccessTime, FILETIME* LastWriteTime);
BOOL DCPCALL FsSetTimeW(WCHAR* RemoteName, FILETIME* CreationTime,
                        FILETIME* LastAccessTime, FILETIME* LastWriteTime);
void DCPCALL FsStatusInfo(char* RemoteDir, int InfoStartEnd, int InfoOperation);
void DCPCALL FsStatusInfoW(WCHAR* RemoteDir, int InfoStartEnd, int InfoOperation);
void DCPCALL FsGetDefRootName(char* DefRootName, int maxlen);
int DCPCALL FsExtractCustomIcon(char* RemoteName, int ExtractFlags, HICON* TheIcon);
int DCPCALL FsExtractCustomIconW(WCHAR* RemoteName, int ExtractFlags, HICON* TheIcon);
void DCPCALL FsSetDefaultParams(FsDefaultParamStruct* dps);
BOOL DCPCALL FsGetBackgroundFlags(void);
}

#endif /* YDISK_WFXPLUGIN_H */
