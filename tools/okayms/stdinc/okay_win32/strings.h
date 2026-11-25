// okay-ms own Win32 header, part of <windows.h>: converting text between code pages and UTF-16. Written in our own
// words from Microsoft's public Win32 documentation of MultiByteToWideChar and WideCharToMultiByte (code page
// identifiers and flags). Both return the number of units written, or the number needed when the output size is 0.
#pragma once
#include "base.h"

#define CP_ACP 0
#define CP_OEMCP 1
#define CP_UTF7 65000
#define CP_UTF8 65001
#define MB_PRECOMPOSED 0x00000001
#define MB_ERR_INVALID_CHARS 0x00000008
#define WC_ERR_INVALID_CHARS 0x00000080
#define WC_NO_BEST_FIT_CHARS 0x00000400

OKAY_WIN32_IMPORT int MultiByteToWideChar(UINT code_page, DWORD flags, LPCSTR text, int length, LPWSTR wide, int wide_size);
OKAY_WIN32_IMPORT int WideCharToMultiByte(UINT code_page, DWORD flags, LPCWSTR wide, int length, LPSTR text, int text_size,
                                          LPCSTR default_char, LPBOOL used_default_char);
OKAY_WIN32_IMPORT int lstrlenW(LPCWSTR text);
OKAY_WIN32_IMPORT int lstrlenA(LPCSTR text);
