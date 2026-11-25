// okay-ms own Win32 header, part of <windows.h>: the first user32 pieces the engine needs outside its window code
// (message boxes and the clipboard). Written in our own words from Microsoft's public Win32 documentation of
// MessageBoxW and the clipboard functions (OpenClipboard, SetClipboardData, the standard clipboard formats). The window,
// message and input functions of the platform layer (OwnWin32.cpp) are not declared yet.
#pragma once
#include "base.h"

#define MB_OK 0x00000000L
#define MB_OKCANCEL 0x00000001L
#define MB_YESNO 0x00000004L
#define MB_ICONERROR 0x00000010L
#define MB_ICONHAND 0x00000010L
#define MB_ICONQUESTION 0x00000020L
#define MB_ICONWARNING 0x00000030L
#define MB_ICONINFORMATION 0x00000040L
#define MB_TOPMOST 0x00040000L
#define MB_SETFOREGROUND 0x00010000L
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7
OKAY_WIN32_IMPORT int MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type);
OKAY_WIN32_IMPORT int MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type);

#define CF_TEXT 1
#define CF_UNICODETEXT 13
OKAY_WIN32_IMPORT BOOL OpenClipboard(HWND owner);
OKAY_WIN32_IMPORT BOOL CloseClipboard(void);
OKAY_WIN32_IMPORT BOOL EmptyClipboard(void);
OKAY_WIN32_IMPORT HANDLE SetClipboardData(UINT format, HANDLE memory);
OKAY_WIN32_IMPORT HANDLE GetClipboardData(UINT format);
OKAY_WIN32_IMPORT BOOL IsClipboardFormatAvailable(UINT format);
