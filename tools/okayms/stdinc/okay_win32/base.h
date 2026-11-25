// okay-ms own Win32 header, part 1 of <windows.h>: the basic Windows data types, handle types and small helper
// macros. Written in our own words from Microsoft's public "Windows Data Types" documentation and the x64 data
// model it describes (LLP64): int and long are 32 bits, pointers, LONG_PTR, SIZE_T and HANDLE are 64 bits. No text
// from the Windows SDK, MinGW or Wine headers. Only what the engine uses is here; add to it as more files need it.
#pragma once

// Calling-convention words: x64 has one calling convention, so these are empty (okay-ms also ignores __stdcall).
#define WINAPI
#define APIENTRY
#define CALLBACK
#define WINAPIV
#define STDMETHODCALLTYPE
#define NTAPI
#define WINBASEAPI
#define WINUSERAPI
#define CONST const
#define VOID void

// The okay-ms import marker for a function that lives in a system DLL (kernel32, user32, ...): calls go through
// the import address table.
#define OKAY_WIN32_IMPORT extern "C" __declspec(dllimport)

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned char BOOLEAN;
typedef unsigned char UCHAR;
typedef char CHAR;
typedef wchar_t WCHAR;
typedef short SHORT;
typedef unsigned short WORD;
typedef unsigned short USHORT;
typedef int INT;
typedef unsigned int UINT;
typedef unsigned int UINT32;
typedef int INT32;
typedef long LONG;
typedef unsigned long ULONG;
typedef unsigned long DWORD;
typedef float FLOAT;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef long long INT64;
typedef unsigned long long UINT64;
typedef unsigned long long DWORD64;
typedef unsigned long long DWORDLONG;
typedef long long LONG_PTR;
typedef unsigned long long ULONG_PTR;
typedef long long INT_PTR;
typedef unsigned long long UINT_PTR;
typedef unsigned long long DWORD_PTR;
typedef unsigned long long SIZE_T;
typedef long long SSIZE_T;
typedef long HRESULT;
typedef long NTSTATUS;

typedef BOOL* PBOOL;
typedef BOOL* LPBOOL;
typedef BYTE* PBYTE;
typedef BYTE* LPBYTE;
typedef UCHAR* PUCHAR;
typedef CHAR* PCHAR;
typedef WORD* PWORD;
typedef WORD* LPWORD;
typedef USHORT* PUSHORT;
typedef INT* PINT;
typedef INT* LPINT;
typedef UINT* PUINT;
typedef LONG* PLONG;
typedef LONG* LPLONG;
typedef ULONG* PULONG;
typedef DWORD* PDWORD;
typedef DWORD* LPDWORD;
typedef LONGLONG* PLONGLONG;
typedef ULONGLONG* PULONGLONG;
typedef DWORD64* PDWORD64;
typedef ULONG_PTR* PULONG_PTR;
typedef DWORD_PTR* PDWORD_PTR;
typedef SIZE_T* PSIZE_T;
typedef void* PVOID;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef CHAR* LPSTR;
typedef CHAR* PSTR;
typedef const CHAR* LPCSTR;
typedef const CHAR* PCSTR;
typedef WCHAR* LPWSTR;
typedef WCHAR* PWSTR;
typedef WCHAR* PWCHAR;
typedef const WCHAR* LPCWSTR;
typedef const WCHAR* PCWSTR;

// Window-message parameters and results.
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;
typedef WORD ATOM;

// Handles. HANDLE is a plain pointer; the others are pointers to distinct incomplete structs, so that they do not
// mix by mistake (the "strict" handle types the documentation describes). HMODULE and HINSTANCE are one type.
typedef void* HANDLE;
typedef HANDLE* PHANDLE;
typedef HANDLE* LPHANDLE;
typedef struct okay_win32_hinstance_* HINSTANCE;
typedef HINSTANCE HMODULE;
typedef struct okay_win32_hwnd_* HWND;
typedef struct okay_win32_hdc_* HDC;
typedef struct okay_win32_hkey_* HKEY;
typedef HKEY* PHKEY;
typedef struct okay_win32_hicon_* HICON;
typedef HICON HCURSOR;
typedef struct okay_win32_hbrush_* HBRUSH;
typedef struct okay_win32_hmenu_* HMENU;
typedef struct okay_win32_hmonitor_* HMONITOR;
typedef struct okay_win32_hbitmap_* HBITMAP;
typedef struct okay_win32_hgdiobj_* HGDIOBJ;
typedef struct okay_win32_hglrc_* HGLRC;
typedef HANDLE HGLOBAL;
typedef HANDLE HLOCAL;

// A function found at run time by GetProcAddress: the caller casts it to the real type.
typedef INT_PTR (WINAPI* FARPROC)();
typedef INT_PTR (WINAPI* PROC)();

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif

#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)

// Pieces of a 16-bit or 32-bit value.
#define MAKEWORD(lo, hi) ((WORD)(((BYTE)((DWORD_PTR)(lo) & 0xff)) | ((WORD)((BYTE)((DWORD_PTR)(hi) & 0xff))) << 8))
#define MAKELONG(lo, hi) ((LONG)(((WORD)((DWORD_PTR)(lo) & 0xffff)) | ((DWORD)((WORD)((DWORD_PTR)(hi) & 0xffff))) << 16))
#define LOWORD(v) ((WORD)(((DWORD_PTR)(v)) & 0xffff))
#define HIWORD(v) ((WORD)((((DWORD_PTR)(v)) >> 16) & 0xffff))
#define LOBYTE(v) ((BYTE)(((DWORD_PTR)(v)) & 0xff))
#define HIBYTE(v) ((BYTE)((((DWORD_PTR)(v)) >> 8) & 0xff))

// A 64-bit signed value that can also be read as two 32-bit halves (low half first, as on x64).
typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    struct {
        DWORD LowPart;
        LONG HighPart;
    } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
    struct {
        DWORD LowPart;
        DWORD HighPart;
    };
    struct {
        DWORD LowPart;
        DWORD HighPart;
    } u;
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

// A 128-bit identifier (COM classes and interfaces, device property keys).
typedef struct _GUID {
    unsigned long Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID, IID, CLSID;
typedef const GUID& REFGUID;
typedef const IID& REFIID;
typedef const CLSID& REFCLSID;
inline bool IsEqualGUID(REFGUID a, REFGUID b) {
    if (a.Data1 != b.Data1 || a.Data2 != b.Data2 || a.Data3 != b.Data3) return false;
    for (int i = 0; i < 8; ++i)
        if (a.Data4[i] != b.Data4[i]) return false;
    return true;
}

typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

// Fill or copy memory (the documented macros RtlZeroMemory, RtlCopyMemory, RtlFillMemory; written here as loops so no C
// runtime is needed).
inline void okay_win32_fill(void* dst, unsigned long long n, unsigned char value) {
    unsigned char* d = (unsigned char*)dst;
    for (unsigned long long i = 0; i < n; ++i) d[i] = value;
}
inline void okay_win32_copy(void* dst, const void* src, unsigned long long n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d > s) {
        for (unsigned long long i = n; i > 0; --i) d[i - 1] = s[i - 1];  // overlapping, destination after source
    } else {
        for (unsigned long long i = 0; i < n; ++i) d[i] = s[i];
    }
}
#define ZeroMemory(dst, n) okay_win32_fill((dst), (n), 0)
#define SecureZeroMemory(dst, n) okay_win32_fill((dst), (n), 0)
#define FillMemory(dst, n, v) okay_win32_fill((dst), (n), (unsigned char)(v))
#define CopyMemory(dst, src, n) okay_win32_copy((dst), (src), (n))
#define MoveMemory(dst, src, n) okay_win32_copy((dst), (src), (n))

// Every handle that kernel32 hands out (files, threads, processes, events) is closed by CloseHandle.
OKAY_WIN32_IMPORT BOOL CloseHandle(HANDLE handle);
