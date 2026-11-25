// okay-ms own Win32 header, part of <windows.h>: facts about the machine and the clock (processor count and
// architecture, memory, tick counts, the performance counter, system and local time, the user's locale name, debugger
// checks). Written in our own words from Microsoft's public Win32 documentation of GetSystemInfo (SYSTEM_INFO),
// GlobalMemoryStatusEx (MEMORYSTATUSEX), GetTickCount64, QueryPerformanceCounter, GetSystemTimeAsFileTime,
// GetLocalTime (SYSTEMTIME), GetUserDefaultLocaleName, IsDebuggerPresent and RtlGetVersion (OSVERSIONINFOW).
#pragma once
#include "base.h"
#include "files.h"

typedef struct _SYSTEM_INFO {
    union {
        DWORD dwOemId;
        struct {
            WORD wProcessorArchitecture;
            WORD wReserved;
        };
    };
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
    DWORD_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;
#define PROCESSOR_ARCHITECTURE_INTEL 0
#define PROCESSOR_ARCHITECTURE_ARM 5
#define PROCESSOR_ARCHITECTURE_AMD64 9
#define PROCESSOR_ARCHITECTURE_ARM64 12
#define PROCESSOR_ARCHITECTURE_UNKNOWN 0xffff
OKAY_WIN32_IMPORT void GetSystemInfo(LPSYSTEM_INFO info);
OKAY_WIN32_IMPORT void GetNativeSystemInfo(LPSYSTEM_INFO info);

typedef struct _MEMORYSTATUSEX {
    DWORD dwLength;  // set to sizeof(MEMORYSTATUSEX) before the call
    DWORD dwMemoryLoad;
    DWORDLONG ullTotalPhys;
    DWORDLONG ullAvailPhys;
    DWORDLONG ullTotalPageFile;
    DWORDLONG ullAvailPageFile;
    DWORDLONG ullTotalVirtual;
    DWORDLONG ullAvailVirtual;
    DWORDLONG ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;
OKAY_WIN32_IMPORT BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX status);

// Milliseconds since the system started (GetTickCount wraps after about 49.7 days).
OKAY_WIN32_IMPORT DWORD GetTickCount(void);
OKAY_WIN32_IMPORT ULONGLONG GetTickCount64(void);
OKAY_WIN32_IMPORT BOOL QueryPerformanceCounter(LARGE_INTEGER* count);
OKAY_WIN32_IMPORT BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency);
OKAY_WIN32_IMPORT void GetSystemTimeAsFileTime(LPFILETIME time);

typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;
OKAY_WIN32_IMPORT void GetLocalTime(LPSYSTEMTIME time);
OKAY_WIN32_IMPORT void GetSystemTime(LPSYSTEMTIME time);
OKAY_WIN32_IMPORT BOOL FileTimeToSystemTime(const FILETIME* file_time, LPSYSTEMTIME system_time);

// The user's locale as a name such as "en-US" (at most LOCALE_NAME_MAX_LENGTH characters with the zero).
#define LOCALE_NAME_MAX_LENGTH 85
OKAY_WIN32_IMPORT int GetUserDefaultLocaleName(LPWSTR name, int size);

OKAY_WIN32_IMPORT BOOL IsDebuggerPresent(void);
OKAY_WIN32_IMPORT BOOL CheckRemoteDebuggerPresent(HANDLE process, PBOOL present);
OKAY_WIN32_IMPORT void OutputDebugStringA(LPCSTR text);
OKAY_WIN32_IMPORT void OutputDebugStringW(LPCWSTR text);
OKAY_WIN32_IMPORT void DebugBreak(void);

// The version of Windows (RtlGetVersion in ntdll fills one of these; dwOSVersionInfoSize is set by the caller).
typedef struct _OSVERSIONINFOW {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    WCHAR szCSDVersion[128];
} OSVERSIONINFOW, *POSVERSIONINFOW, *LPOSVERSIONINFOW, RTL_OSVERSIONINFOW, *PRTL_OSVERSIONINFOW;

OKAY_WIN32_IMPORT BOOL GetComputerNameW(LPWSTR buffer, LPDWORD size);
