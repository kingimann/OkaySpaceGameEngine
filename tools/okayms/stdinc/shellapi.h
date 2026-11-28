// okay-ms own header <shellapi.h>: the shell functions the engine uses (dropped files, opening a document, the command
// line split into words), declarations only, written in our own words from Microsoft's public Win32 API documentation
// (the pages of DragAcceptFiles, DragQueryFileW, DragQueryPoint, DragFinish, ShellExecuteW and CommandLineToArgvW:
// their documented prototypes, "Header shellapi.h", "DLL Shell32.dll"). No text from the Windows SDK, MinGW or Wine
// headers. Round 36 (OwnWin32.cpp's WM_DROPFILES handling).
#pragma once
#include <windows.h>
// The drop handle the WM_DROPFILES message carries in its wParam (a distinct handle type, as the other strict handles).
typedef struct okay_win32_hdrop_* HDROP;
#ifndef WM_DROPFILES
#define WM_DROPFILES 0x0233
#endif
extern "C" {
void WINAPI DragAcceptFiles(HWND hwnd, BOOL accept);
UINT WINAPI DragQueryFileW(HDROP drop, UINT index, WCHAR* file, UINT count);
BOOL WINAPI DragQueryPoint(HDROP drop, POINT* point);
void WINAPI DragFinish(HDROP drop);
HINSTANCE WINAPI ShellExecuteW(HWND hwnd, const WCHAR* operation, const WCHAR* file, const WCHAR* parameters,
                               const WCHAR* directory, INT show);
WCHAR** WINAPI CommandLineToArgvW(const WCHAR* command_line, int* argc);
}
