// okay-ms own <winhttp.h>: the WinHTTP client functions the engine's HTTP seam uses (okay/Platform/NativeHttp.hpp),
// declared in our own words from Microsoft's public WinHTTP documentation (WinHttpOpen, WinHttpConnect,
// WinHttpOpenRequest, WinHttpSendRequest, WinHttpReceiveResponse, WinHttpQueryHeaders, WinHttpReadData,
// WinHttpCrackUrl, URL_COMPONENTS, the option, query and error numbers). The functions live in winhttp.dll.
#pragma once
#include "windows.h"

typedef LPVOID HINTERNET;
typedef HINTERNET* LPHINTERNET;
typedef WORD INTERNET_PORT;
typedef int INTERNET_SCHEME;

#define INTERNET_DEFAULT_PORT 0
#define INTERNET_DEFAULT_HTTP_PORT 80
#define INTERNET_DEFAULT_HTTPS_PORT 443
#define INTERNET_SCHEME_HTTP 1
#define INTERNET_SCHEME_HTTPS 2

// Access types (WinHttpOpen).
#define WINHTTP_ACCESS_TYPE_DEFAULT_PROXY 0
#define WINHTTP_ACCESS_TYPE_NO_PROXY 1
#define WINHTTP_ACCESS_TYPE_NAMED_PROXY 3
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4

// The "nothing here" values of optional arguments.
#define WINHTTP_NO_PROXY_NAME 0
#define WINHTTP_NO_PROXY_BYPASS 0
#define WINHTTP_NO_REFERER 0
#define WINHTTP_DEFAULT_ACCEPT_TYPES 0
#define WINHTTP_NO_ADDITIONAL_HEADERS 0
#define WINHTTP_NO_REQUEST_DATA 0
#define WINHTTP_HEADER_NAME_BY_INDEX 0
#define WINHTTP_NO_OUTPUT_BUFFER 0
#define WINHTTP_NO_HEADER_INDEX 0

// WinHttpOpen and WinHttpOpenRequest flags.
#define WINHTTP_FLAG_ASYNC 0x10000000
#define WINHTTP_FLAG_SECURE 0x00800000
#define WINHTTP_FLAG_BYPASS_PROXY_CACHE 0x00000100
#define WINHTTP_FLAG_REFRESH WINHTTP_FLAG_BYPASS_PROXY_CACHE

// Options (WinHttpSetOption) and their values.
#define WINHTTP_OPTION_CONNECT_TIMEOUT 3
#define WINHTTP_OPTION_SECURITY_FLAGS 31
#define WINHTTP_OPTION_URL 34
#define WINHTTP_OPTION_DISABLE_FEATURE 63
#define WINHTTP_OPTION_REDIRECT_POLICY 88
#define WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS 89
#define WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL 133
#define WINHTTP_DISABLE_COOKIES 0x00000001
#define WINHTTP_DISABLE_REDIRECTS 0x00000002
#define WINHTTP_DISABLE_AUTHENTICATION 0x00000004
#define WINHTTP_DISABLE_KEEP_ALIVE 0x00000008
#define WINHTTP_OPTION_REDIRECT_POLICY_NEVER 0
#define WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP 1
#define WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS 2
#define WINHTTP_PROTOCOL_FLAG_HTTP2 0x1

// Header queries (WinHttpQueryHeaders): the header number, optionally with the "as a number" flag.
#define WINHTTP_QUERY_CONTENT_TYPE 1
#define WINHTTP_QUERY_CONTENT_LENGTH 5
#define WINHTTP_QUERY_DATE 9
#define WINHTTP_QUERY_LAST_MODIFIED 11
#define WINHTTP_QUERY_STATUS_CODE 19
#define WINHTTP_QUERY_STATUS_TEXT 20
#define WINHTTP_QUERY_RAW_HEADERS 21
#define WINHTTP_QUERY_RAW_HEADERS_CRLF 22
#define WINHTTP_QUERY_CONTENT_ENCODING 29
#define WINHTTP_QUERY_LOCATION 33
#define WINHTTP_QUERY_RETRY_AFTER 36
#define WINHTTP_QUERY_CONTENT_RANGE 53
#define WINHTTP_QUERY_ETAG 54
#define WINHTTP_QUERY_CUSTOM 65535
#define WINHTTP_QUERY_FLAG_NUMBER 0x20000000

// Errors (GetLastError after a failed call): 12000 plus a number.
#define WINHTTP_ERROR_BASE 12000
#define ERROR_WINHTTP_TIMEOUT 12002
#define ERROR_WINHTTP_INTERNAL_ERROR 12004
#define ERROR_WINHTTP_INVALID_URL 12005
#define ERROR_WINHTTP_UNRECOGNIZED_SCHEME 12006
#define ERROR_WINHTTP_NAME_NOT_RESOLVED 12007
#define ERROR_WINHTTP_OPERATION_CANCELLED 12017
#define ERROR_WINHTTP_INCORRECT_HANDLE_TYPE 12018
#define ERROR_WINHTTP_CANNOT_CONNECT 12029
#define ERROR_WINHTTP_CONNECTION_ERROR 12030
#define ERROR_WINHTTP_HEADER_NOT_FOUND 12150
#define ERROR_WINHTTP_REDIRECT_FAILED 12156
#define ERROR_WINHTTP_SECURE_FAILURE 12175

// The parts of a URL (WinHttpCrackUrl). A length of -1 (or a nonzero length with a null pointer) asks for that part;
// the pointers then point into the URL text.
typedef struct _URL_COMPONENTS {
    DWORD dwStructSize;
    LPWSTR lpszScheme;
    DWORD dwSchemeLength;
    INTERNET_SCHEME nScheme;
    LPWSTR lpszHostName;
    DWORD dwHostNameLength;
    INTERNET_PORT nPort;
    LPWSTR lpszUserName;
    DWORD dwUserNameLength;
    LPWSTR lpszPassword;
    DWORD dwPasswordLength;
    LPWSTR lpszUrlPath;
    DWORD dwUrlPathLength;
    LPWSTR lpszExtraInfo;
    DWORD dwExtraInfoLength;
} URL_COMPONENTS, *LPURL_COMPONENTS, URL_COMPONENTSW, *LPURL_COMPONENTSW;

OKAY_WIN32_IMPORT HINTERNET WinHttpOpen(LPCWSTR agent, DWORD access_type, LPCWSTR proxy, LPCWSTR proxy_bypass, DWORD flags);
OKAY_WIN32_IMPORT HINTERNET WinHttpConnect(HINTERNET session, LPCWSTR server, INTERNET_PORT port, DWORD reserved);
OKAY_WIN32_IMPORT HINTERNET WinHttpOpenRequest(HINTERNET connection, LPCWSTR verb, LPCWSTR object, LPCWSTR version,
                                               LPCWSTR referrer, LPCWSTR* accept_types, DWORD flags);
OKAY_WIN32_IMPORT BOOL WinHttpAddRequestHeaders(HINTERNET request, LPCWSTR headers, DWORD length, DWORD modifiers);
OKAY_WIN32_IMPORT BOOL WinHttpSendRequest(HINTERNET request, LPCWSTR headers, DWORD headers_length, LPVOID optional,
                                          DWORD optional_length, DWORD total_length, DWORD_PTR context);
OKAY_WIN32_IMPORT BOOL WinHttpWriteData(HINTERNET request, LPCVOID buffer, DWORD to_write, LPDWORD written);
OKAY_WIN32_IMPORT BOOL WinHttpReceiveResponse(HINTERNET request, LPVOID reserved);
OKAY_WIN32_IMPORT BOOL WinHttpQueryHeaders(HINTERNET request, DWORD info_level, LPCWSTR name, LPVOID buffer,
                                           LPDWORD buffer_length, LPDWORD index);
OKAY_WIN32_IMPORT BOOL WinHttpQueryDataAvailable(HINTERNET request, LPDWORD available);
OKAY_WIN32_IMPORT BOOL WinHttpReadData(HINTERNET request, LPVOID buffer, DWORD to_read, LPDWORD read);
OKAY_WIN32_IMPORT BOOL WinHttpCloseHandle(HINTERNET handle);
OKAY_WIN32_IMPORT BOOL WinHttpSetOption(HINTERNET handle, DWORD option, LPVOID buffer, DWORD length);
// WinHttpQueryOption: the option's value into the buffer; with a null buffer (or one too small) it fails with
// ERROR_INSUFFICIENT_BUFFER and stores the size needed, in bytes (Round 32, NativeHttp.hpp's WINHTTP_OPTION_URL).
OKAY_WIN32_IMPORT BOOL WinHttpQueryOption(HINTERNET handle, DWORD option, LPVOID buffer, LPDWORD length);
OKAY_WIN32_IMPORT BOOL WinHttpSetTimeouts(HINTERNET handle, int resolve_ms, int connect_ms, int send_ms, int receive_ms);
OKAY_WIN32_IMPORT BOOL WinHttpCrackUrl(LPCWSTR url, DWORD length, DWORD flags, LPURL_COMPONENTS components);
