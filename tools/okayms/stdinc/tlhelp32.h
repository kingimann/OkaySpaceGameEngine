// okay-ms own <tlhelp32.h>: listing the modules of a process with the Tool Help functions, declared in our own words
// from Microsoft's public Tool Help documentation (CreateToolhelp32Snapshot, Module32FirstW, Module32NextW,
// MODULEENTRY32W and the snapshot flags). The functions live in kernel32.dll. The x64 layout of MODULEENTRY32W: five
// DWORDs, the base address, the size, the module handle, then the module name (256 UTF-16 units) and path (MAX_PATH).
#pragma once
#include "windows.h"

#define TH32CS_SNAPHEAPLIST 0x00000001
#define TH32CS_SNAPPROCESS 0x00000002
#define TH32CS_SNAPTHREAD 0x00000004
#define TH32CS_SNAPMODULE 0x00000008
#define TH32CS_SNAPMODULE32 0x00000010
#define MAX_MODULE_NAME32 255

typedef struct tagMODULEENTRY32W {
    DWORD dwSize;  // set to sizeof(MODULEENTRY32W) before the first call
    DWORD th32ModuleID;
    DWORD th32ProcessID;
    DWORD GlblcntUsage;
    DWORD ProccntUsage;
    BYTE* modBaseAddr;
    DWORD modBaseSize;
    HMODULE hModule;
    WCHAR szModule[MAX_MODULE_NAME32 + 1];
    WCHAR szExePath[MAX_PATH];
} MODULEENTRY32W, *PMODULEENTRY32W, *LPMODULEENTRY32W;

OKAY_WIN32_IMPORT HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD process_id);
OKAY_WIN32_IMPORT BOOL Module32FirstW(HANDLE snapshot, LPMODULEENTRY32W entry);
OKAY_WIN32_IMPORT BOOL Module32NextW(HANDLE snapshot, LPMODULEENTRY32W entry);
