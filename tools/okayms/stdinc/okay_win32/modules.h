// okay-ms own Win32 header, part of <windows.h>: loading DLLs and finding functions in them, and the file name of a
// module. Written in our own words from Microsoft's public Win32 documentation of "Dynamic-Link Library Functions"
// (LoadLibraryW, GetProcAddress, FreeLibrary, GetModuleHandleW, GetModuleHandleExW, GetModuleFileNameW).
#pragma once
#include "base.h"

OKAY_WIN32_IMPORT HMODULE LoadLibraryA(LPCSTR name);
OKAY_WIN32_IMPORT HMODULE LoadLibraryW(LPCWSTR name);
OKAY_WIN32_IMPORT HMODULE LoadLibraryExW(LPCWSTR name, HANDLE reserved, DWORD flags);
OKAY_WIN32_IMPORT BOOL FreeLibrary(HMODULE module);
OKAY_WIN32_IMPORT FARPROC GetProcAddress(HMODULE module, LPCSTR name);
OKAY_WIN32_IMPORT HMODULE GetModuleHandleA(LPCSTR name);
OKAY_WIN32_IMPORT HMODULE GetModuleHandleW(LPCWSTR name);
OKAY_WIN32_IMPORT BOOL GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE* module);
OKAY_WIN32_IMPORT BOOL GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE* module);
OKAY_WIN32_IMPORT DWORD GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size);
OKAY_WIN32_IMPORT DWORD GetModuleFileNameW(HMODULE module, LPWSTR buffer, DWORD size);

// LoadLibraryExW flags.
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
// GetModuleHandleEx flags: the name is an address inside the module; the module's reference count is not raised.
#define GET_MODULE_HANDLE_EX_FLAG_PIN 0x00000001
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004

// A resource given by number instead of by name (the number in the low 16 bits of the pointer).
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#define IS_INTRESOURCE(p) ((((ULONG_PTR)(p)) >> 16) == 0)
