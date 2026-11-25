// okay-ms own Win32 header, part of <windows.h>: the last-error value of a thread, the system error codes the engine
// tests for, and HRESULT values with their test macros. Written in our own words from Microsoft's public "System
// Error Codes", GetLastError and "Error Handling in COM" documentation. Only what the engine uses.
#pragma once
#include "base.h"

OKAY_WIN32_IMPORT DWORD GetLastError(void);
OKAY_WIN32_IMPORT void SetLastError(DWORD code);

#define ERROR_SUCCESS 0L
#define NO_ERROR 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_SHARING_VIOLATION 32L
#define ERROR_FILE_EXISTS 80L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_ENVVAR_NOT_FOUND 203L
#define ERROR_MORE_DATA 234L
#define ERROR_NO_MORE_ITEMS 259L
#define ERROR_OPERATION_ABORTED 995L
#define ERROR_IO_INCOMPLETE 996L
#define ERROR_IO_PENDING 997L
#define ERROR_NOTIFY_ENUM_DIR 1022L
#define ERROR_CLASS_ALREADY_EXISTS 1410L
#define ERROR_TIMEOUT 1460L

// HRESULT: negative means failure.
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_POINTER ((HRESULT)0x80004003L)
#define E_ABORT ((HRESULT)0x80004004L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#define E_ACCESSDENIED ((HRESULT)0x80070005L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? ((HRESULT)(x)) : ((HRESULT)(((x) & 0x0000FFFF) | (7 << 16) | 0x80000000)))
