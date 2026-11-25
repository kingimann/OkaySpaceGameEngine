// okay-ms own Win32 header, part of <windows.h>: files, directories and asynchronous file input/output. Written in
// our own words from Microsoft's public Win32 documentation of "File Management" (CreateFileW, ReadFile, WriteFile,
// SetFilePointerEx, GetFileAttributesExW, ReadDirectoryChangesW, OVERLAPPED, the access rights, share modes,
// creation dispositions and attribute flags). Structure layouts are the documented x64 ones.
#pragma once
#include "base.h"

// Generic and file-specific access rights.
#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define GENERIC_EXECUTE 0x20000000L
#define GENERIC_ALL 0x10000000L
#define DELETE 0x00010000L
#define FILE_READ_DATA 0x0001
#define FILE_LIST_DIRECTORY 0x0001
#define FILE_WRITE_DATA 0x0002
#define FILE_APPEND_DATA 0x0004
#define FILE_READ_ATTRIBUTES 0x0080
#define FILE_WRITE_ATTRIBUTES 0x0100

// Share modes.
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004

// Creation dispositions.
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5

// Attributes and flags.
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_DEVICE 0x00000040
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_FLAG_WRITE_THROUGH 0x80000000
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FILE_FLAG_NO_BUFFERING 0x20000000
#define FILE_FLAG_RANDOM_ACCESS 0x10000000
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000

// SetFilePointerEx origins.
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2

// MoveFileExW flags.
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED 0x00000002
#define MOVEFILE_WRITE_THROUGH 0x00000008

// A 64-bit count of 100-nanosecond intervals since 1601-01-01 (UTC), as two 32-bit halves.
typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

// State of an asynchronous read or write: the result fields, then the file offset (or a pointer), then an event.
typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct {
            DWORD Offset;
            DWORD OffsetHigh;
        };
        PVOID Pointer;
    };
    HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

typedef void (WINAPI* LPOVERLAPPED_COMPLETION_ROUTINE)(DWORD error_code, DWORD bytes, LPOVERLAPPED overlapped);

OKAY_WIN32_IMPORT HANDLE CreateFileW(LPCWSTR name, DWORD access, DWORD share_mode, LPSECURITY_ATTRIBUTES attributes,
                                     DWORD creation, DWORD flags_and_attributes, HANDLE template_file);
OKAY_WIN32_IMPORT HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share_mode, LPSECURITY_ATTRIBUTES attributes,
                                     DWORD creation, DWORD flags_and_attributes, HANDLE template_file);
OKAY_WIN32_IMPORT BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD to_read, LPDWORD read, LPOVERLAPPED overlapped);
OKAY_WIN32_IMPORT BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD to_write, LPDWORD written, LPOVERLAPPED overlapped);
OKAY_WIN32_IMPORT BOOL FlushFileBuffers(HANDLE file);
OKAY_WIN32_IMPORT BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, PLARGE_INTEGER new_position, DWORD origin);
OKAY_WIN32_IMPORT BOOL SetEndOfFile(HANDLE file);
OKAY_WIN32_IMPORT BOOL GetFileSizeEx(HANDLE file, PLARGE_INTEGER size);
OKAY_WIN32_IMPORT BOOL DeleteFileW(LPCWSTR name);
OKAY_WIN32_IMPORT BOOL MoveFileExW(LPCWSTR existing, LPCWSTR replacement, DWORD flags);
OKAY_WIN32_IMPORT BOOL CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES attributes);
OKAY_WIN32_IMPORT BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES attributes);
OKAY_WIN32_IMPORT BOOL RemoveDirectoryW(LPCWSTR path);
OKAY_WIN32_IMPORT DWORD GetFileAttributesW(LPCWSTR name);
OKAY_WIN32_IMPORT DWORD GetFileAttributesA(LPCSTR name);
OKAY_WIN32_IMPORT DWORD GetTempPathW(DWORD size, LPWSTR buffer);
OKAY_WIN32_IMPORT DWORD GetCurrentDirectoryW(DWORD size, LPWSTR buffer);
OKAY_WIN32_IMPORT UINT GetWindowsDirectoryW(LPWSTR buffer, UINT size);
OKAY_WIN32_IMPORT UINT GetSystemWindowsDirectoryW(LPWSTR buffer, UINT size);
OKAY_WIN32_IMPORT BOOL GetVolumeInformationW(LPCWSTR root, LPWSTR volume_name, DWORD volume_name_size, LPDWORD serial,
                                             LPDWORD max_component_length, LPDWORD file_system_flags,
                                             LPWSTR file_system_name, DWORD file_system_name_size);

// GetFileAttributesExW with GetFileExInfoStandard fills a WIN32_FILE_ATTRIBUTE_DATA.
typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard = 0, GetFileExMaxInfoLevel = 1 } GET_FILEEX_INFO_LEVELS;
typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;
OKAY_WIN32_IMPORT BOOL GetFileAttributesExW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID information);

// Asynchronous results and cancelling.
OKAY_WIN32_IMPORT BOOL GetOverlappedResult(HANDLE file, LPOVERLAPPED overlapped, LPDWORD transferred, BOOL wait);
OKAY_WIN32_IMPORT BOOL CancelIo(HANDLE file);
OKAY_WIN32_IMPORT BOOL CancelIoEx(HANDLE file, LPOVERLAPPED overlapped);

// Watching a directory: ReadDirectoryChangesW fills a buffer with FILE_NOTIFY_INFORMATION records, each NextEntryOffset
// bytes after the one before (0 for the last); FileName is FileNameLength bytes of UTF-16, not zero-terminated.
#define FILE_NOTIFY_CHANGE_FILE_NAME 0x00000001
#define FILE_NOTIFY_CHANGE_DIR_NAME 0x00000002
#define FILE_NOTIFY_CHANGE_ATTRIBUTES 0x00000004
#define FILE_NOTIFY_CHANGE_SIZE 0x00000008
#define FILE_NOTIFY_CHANGE_LAST_WRITE 0x00000010
#define FILE_NOTIFY_CHANGE_CREATION 0x00000040
#define FILE_ACTION_ADDED 0x00000001
#define FILE_ACTION_REMOVED 0x00000002
#define FILE_ACTION_MODIFIED 0x00000003
#define FILE_ACTION_RENAMED_OLD_NAME 0x00000004
#define FILE_ACTION_RENAMED_NEW_NAME 0x00000005
typedef struct _FILE_NOTIFY_INFORMATION {
    DWORD NextEntryOffset;
    DWORD Action;
    DWORD FileNameLength;
    WCHAR FileName[1];
} FILE_NOTIFY_INFORMATION, *PFILE_NOTIFY_INFORMATION;
OKAY_WIN32_IMPORT BOOL ReadDirectoryChangesW(HANDLE directory, LPVOID buffer, DWORD buffer_size, BOOL watch_subtree,
                                             DWORD notify_filter, LPDWORD returned, LPOVERLAPPED overlapped,
                                             LPOVERLAPPED_COMPLETION_ROUTINE completion);

// The standard input, output and error handles of the process.
#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)
OKAY_WIN32_IMPORT HANDLE GetStdHandle(DWORD which);
