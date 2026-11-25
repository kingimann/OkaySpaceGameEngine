// okay-ms own Win32 header, part of <windows.h>: threads and processes (starting, waiting, ending, the current
// thread and process, environment variables). Written in our own words from Microsoft's public Win32 documentation
// of "Processes and Threads" (CreateThread, CreateProcessW, STARTUPINFOW, PROCESS_INFORMATION, GetExitCodeProcess,
// OpenProcess, the process creation flags and access rights). Structure layouts are the documented x64 ones.
#pragma once
#include "base.h"

typedef DWORD (WINAPI* PTHREAD_START_ROUTINE)(LPVOID parameter);
typedef PTHREAD_START_ROUTINE LPTHREAD_START_ROUTINE;

OKAY_WIN32_IMPORT HANDLE CreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stack_size, LPTHREAD_START_ROUTINE start,
                                      LPVOID parameter, DWORD creation_flags, LPDWORD thread_id);
OKAY_WIN32_IMPORT HANDLE GetCurrentThread(void);
OKAY_WIN32_IMPORT DWORD GetCurrentThreadId(void);
OKAY_WIN32_IMPORT HANDLE GetCurrentProcess(void);
OKAY_WIN32_IMPORT DWORD GetCurrentProcessId(void);
OKAY_WIN32_IMPORT void Sleep(DWORD milliseconds);
OKAY_WIN32_IMPORT DWORD SleepEx(DWORD milliseconds, BOOL alertable);
OKAY_WIN32_IMPORT BOOL SwitchToThread(void);
OKAY_WIN32_IMPORT BOOL GetExitCodeThread(HANDLE thread, LPDWORD exit_code);
OKAY_WIN32_IMPORT void ExitProcess(UINT exit_code);
OKAY_WIN32_IMPORT BOOL TerminateProcess(HANDLE process, UINT exit_code);
OKAY_WIN32_IMPORT BOOL GetExitCodeProcess(HANDLE process, LPDWORD exit_code);
OKAY_WIN32_IMPORT HANDLE OpenProcess(DWORD desired_access, BOOL inherit_handle, DWORD process_id);
OKAY_WIN32_IMPORT BOOL SetThreadPriority(HANDLE thread, int priority);

// Thread creation flag and priorities.
#define CREATE_SUSPENDED 0x00000004
#define THREAD_PRIORITY_LOWEST (-2)
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2

// The exit code a process or thread reports while it is still running.
#define STILL_ACTIVE 259L

// Process access rights (OpenProcess) and the general SYNCHRONIZE right (wait on the handle).
#define SYNCHRONIZE 0x00100000L
#define PROCESS_TERMINATE 0x0001
#define PROCESS_VM_READ 0x0010
#define PROCESS_QUERY_INFORMATION 0x0400
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000

// Process creation flags (CreateProcessW).
#define DEBUG_PROCESS 0x00000001
#define DETACHED_PROCESS 0x00000008
#define CREATE_NEW_CONSOLE 0x00000010
#define NORMAL_PRIORITY_CLASS 0x00000020
#define CREATE_NEW_PROCESS_GROUP 0x00000200
#define CREATE_UNICODE_ENVIRONMENT 0x00000400
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000
#define CREATE_BREAKAWAY_FROM_JOB 0x01000000
#define CREATE_NO_WINDOW 0x08000000

// STARTUPINFO flags.
#define STARTF_USESHOWWINDOW 0x00000001
#define STARTF_USESTDHANDLES 0x00000100

// Show commands (ShowWindow, and STARTUPINFO's wShowWindow).
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_SHOWMINNOACTIVE 7
#define SW_SHOWNA 8
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10

typedef struct _STARTUPINFOW {
    DWORD cb;
    LPWSTR lpReserved;
    LPWSTR lpDesktop;
    LPWSTR lpTitle;
    DWORD dwX;
    DWORD dwY;
    DWORD dwXSize;
    DWORD dwYSize;
    DWORD dwXCountChars;
    DWORD dwYCountChars;
    DWORD dwFillAttribute;
    DWORD dwFlags;
    WORD wShowWindow;
    WORD cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
} STARTUPINFOW, *LPSTARTUPINFOW;

// An attribute list (inherited handles, ...) after the STARTUPINFOW, with EXTENDED_STARTUPINFO_PRESENT.
typedef struct okay_win32_proc_thread_attribute_list_* LPPROC_THREAD_ATTRIBUTE_LIST;
typedef struct _STARTUPINFOEXW {
    STARTUPINFOW StartupInfo;
    LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList;
} STARTUPINFOEXW, *LPSTARTUPINFOEXW;
#define PROC_THREAD_ATTRIBUTE_HANDLE_LIST 0x00020002
OKAY_WIN32_IMPORT BOOL InitializeProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD count, DWORD flags,
                                                         PSIZE_T size);
OKAY_WIN32_IMPORT BOOL UpdateProcThreadAttribute(LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD flags, DWORD_PTR attribute,
                                                 PVOID value, SIZE_T size, PVOID previous, PSIZE_T return_size);
OKAY_WIN32_IMPORT void DeleteProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST list);

typedef struct _PROCESS_INFORMATION {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD dwProcessId;
    DWORD dwThreadId;
} PROCESS_INFORMATION, *PPROCESS_INFORMATION, *LPPROCESS_INFORMATION;

OKAY_WIN32_IMPORT BOOL CreateProcessW(LPCWSTR application, LPWSTR command_line, LPSECURITY_ATTRIBUTES process_attributes,
                                      LPSECURITY_ATTRIBUTES thread_attributes, BOOL inherit_handles, DWORD creation_flags,
                                      LPVOID environment, LPCWSTR current_directory, LPSTARTUPINFOW startup_info,
                                      LPPROCESS_INFORMATION process_information);

// Environment variables of the process (W: UTF-16 names and values). Get returns the length written, or the size
// needed (with the terminating zero) when the buffer is too small, or 0 when the variable is not set.
OKAY_WIN32_IMPORT DWORD GetEnvironmentVariableW(LPCWSTR name, LPWSTR buffer, DWORD size);
OKAY_WIN32_IMPORT DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size);
OKAY_WIN32_IMPORT BOOL SetEnvironmentVariableW(LPCWSTR name, LPCWSTR value);
OKAY_WIN32_IMPORT LPWSTR GetCommandLineW(void);
