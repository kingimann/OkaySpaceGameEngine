// okay-ms own Win32 header, part of <windows.h>: global memory blocks (used by the clipboard), virtual memory and the
// process heap. Written in our own words from Microsoft's public Win32 documentation of GlobalAlloc, GlobalLock,
// VirtualAlloc, VirtualProtect, GetProcessHeap and HeapAlloc (flags and protection constants).
#pragma once
#include "base.h"

#define GMEM_FIXED 0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GHND (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define GPTR (GMEM_FIXED | GMEM_ZEROINIT)
OKAY_WIN32_IMPORT HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes);
OKAY_WIN32_IMPORT LPVOID GlobalLock(HGLOBAL memory);
OKAY_WIN32_IMPORT BOOL GlobalUnlock(HGLOBAL memory);
OKAY_WIN32_IMPORT HGLOBAL GlobalFree(HGLOBAL memory);
OKAY_WIN32_IMPORT SIZE_T GlobalSize(HGLOBAL memory);
OKAY_WIN32_IMPORT HLOCAL LocalFree(HLOCAL memory);

#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_DECOMMIT 0x00004000
#define MEM_RELEASE 0x00008000
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_GUARD 0x100
OKAY_WIN32_IMPORT LPVOID VirtualAlloc(LPVOID address, SIZE_T size, DWORD allocation_type, DWORD protect);
OKAY_WIN32_IMPORT BOOL VirtualFree(LPVOID address, SIZE_T size, DWORD free_type);
OKAY_WIN32_IMPORT BOOL VirtualProtect(LPVOID address, SIZE_T size, DWORD new_protect, PDWORD old_protect);

#define HEAP_ZERO_MEMORY 0x00000008
OKAY_WIN32_IMPORT HANDLE GetProcessHeap(void);
OKAY_WIN32_IMPORT LPVOID HeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes);
OKAY_WIN32_IMPORT LPVOID HeapReAlloc(HANDLE heap, DWORD flags, LPVOID memory, SIZE_T bytes);
OKAY_WIN32_IMPORT BOOL HeapFree(HANDLE heap, DWORD flags, LPVOID memory);

OKAY_WIN32_IMPORT BOOL ReadProcessMemory(HANDLE process, LPCVOID address, LPVOID buffer, SIZE_T size, SIZE_T* read);
