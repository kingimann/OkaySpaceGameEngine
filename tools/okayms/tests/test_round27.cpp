// Round 27: okay-ms's own <windows.h> (and <winhttp.h>, <tlhelp32.h>, <process.h>), and the compiler and header
// fixes the engine files stopped at after Round 26. Each program returns 0 when all of its checks hold, or the number
// of the first check that failed; it is built by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW g++ builds
// the same text with its own headers (MinGW's <windows.h> on the g++ side) and must agree (program_check.hpp). The
// Win32 programs call kernel32.dll, winhttp.dll and msvcrt.dll for real under Wine, so the structure layouts and
// constants of our header are checked against the system DLLs, not only against MinGW's numbers. Written from
// Microsoft's public Win32 API documentation and ISO/IEC 14882 ([class.union.anon], [dcl.typedef], [lex.ccon],
// [expr.new], [expr.static.cast], [basic.scope.hiding], [conv.func]). No code from other compilers or libraries.

#include "program_check.hpp"

#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r27", {}};
const okayms_test::ProgramRun kRunKernel{
    "r27",
    {"GetSystemInfo", "GetNativeSystemInfo", "QueryPerformanceCounter", "QueryPerformanceFrequency", "GetTickCount",
     "GetTickCount64", "MultiByteToWideChar", "WideCharToMultiByte", "GetModuleHandleW", "LoadLibraryA", "GetProcAddress",
     "FreeLibrary", "CreateFileW", "WriteFile", "ReadFile", "SetFilePointerEx", "GetFileSizeEx", "CloseHandle",
     "GetFileAttributesW", "GetFileAttributesExW", "DeleteFileW", "GetLastError", "SetLastError", "GlobalMemoryStatusEx",
     "GetEnvironmentVariableW", "SetEnvironmentVariableW", "GetCurrentProcessId", "GetModuleFileNameW", "GetLocalTime",
     "GetSystemTimeAsFileTime", "CreateToolhelp32Snapshot", "Module32FirstW", "GlobalAlloc", "GlobalLock",
     "GlobalUnlock", "GlobalFree"}};
const okayms_test::ProgramRun kRunThreads{
    "r27",
    {"CreateThread", "WaitForSingleObject", "WaitForMultipleObjects", "GetExitCodeThread", "CloseHandle", "CreateEventW",
     "SetEvent", "ResetEvent", "InitializeCriticalSection", "EnterCriticalSection", "LeaveCriticalSection",
     "DeleteCriticalSection", "InitializeConditionVariable", "SleepConditionVariableCS", "WakeAllConditionVariable",
     "InitializeSRWLock", "AcquireSRWLockExclusive", "ReleaseSRWLockExclusive", "Sleep", "SwitchToThread",
     "GetCurrentThreadId", "msvcrt.dll:_beginthreadex"}};
const okayms_test::ProgramRun kRunHttp{"r27", {"winhttp.dll:WinHttpCrackUrl"}};
}  // namespace

// The bump allocator of program_check.hpp (no C runtime is linked on the okay-ms side).
static const char* kAlloc = okayms_test::bump_alloc();

// Types, structure sizes and member offsets, and constants of <windows.h>: our header must match the documented x64
// layout (MinGW's header on the g++ side gives the same numbers).
static void r27_win_types(Tally& tally) {
    std::printf("r27_win_types\n");
    okayms_test::run_both(kRun, tally, "win_types", std::string(kAlloc) + R"OKMS(
#include <windows.h>
#include <tlhelp32.h>
#define OFF(T, m) ((unsigned long long)&((T*)0)->m)
int entry(void) {
  CK(1, sizeof(BOOL) == 4 && sizeof(DWORD) == 4 && sizeof(LONG) == 4 && sizeof(WORD) == 2 && sizeof(BYTE) == 1);
  CK(2, sizeof(HANDLE) == 8 && sizeof(SIZE_T) == 8 && sizeof(ULONG_PTR) == 8 && sizeof(LPARAM) == 8 && sizeof(WPARAM) == 8);
  CK(3, sizeof(LARGE_INTEGER) == 8 && sizeof(ULARGE_INTEGER) == 8 && sizeof(FILETIME) == 8 && sizeof(GUID) == 16);
  CK(4, sizeof(CRITICAL_SECTION) == 40 && sizeof(SRWLOCK) == 8 && sizeof(CONDITION_VARIABLE) == 8);
  CK(5, sizeof(SYSTEM_INFO) == 48 && OFF(SYSTEM_INFO, dwPageSize) == 4 && OFF(SYSTEM_INFO, dwNumberOfProcessors) == 32);
  CK(6, sizeof(STARTUPINFOW) == 104 && OFF(STARTUPINFOW, dwFlags) == 60 && OFF(STARTUPINFOW, wShowWindow) == 64 &&
            OFF(STARTUPINFOW, hStdInput) == 80);
  CK(7, sizeof(PROCESS_INFORMATION) == 24 && sizeof(SECURITY_ATTRIBUTES) == 24);
  CK(8, sizeof(MEMORYSTATUSEX) == 64 && sizeof(SYSTEMTIME) == 16 && sizeof(WIN32_FILE_ATTRIBUTE_DATA) == 36);
  CK(9, sizeof(OVERLAPPED) == 32 && OFF(OVERLAPPED, Offset) == 16 && OFF(OVERLAPPED, OffsetHigh) == 20 &&
            OFF(OVERLAPPED, Pointer) == 16 && OFF(OVERLAPPED, hEvent) == 24);
  CK(10, sizeof(MODULEENTRY32W) == 1080 && OFF(MODULEENTRY32W, modBaseAddr) == 24 && OFF(MODULEENTRY32W, szModule) == 48);
  CK(11, sizeof(OSVERSIONINFOW) == 276 && sizeof(FILE_NOTIFY_INFORMATION) == 16);
  CK(12, INFINITE == 0xFFFFFFFF && WAIT_OBJECT_0 == 0 && WAIT_TIMEOUT == 258 && STILL_ACTIVE == 259);
  CK(13, GENERIC_READ == 0x80000000 && GENERIC_WRITE == 0x40000000 && OPEN_EXISTING == 3 && CREATE_ALWAYS == 2);
  CK(14, CP_UTF8 == 65001 && MAX_PATH == 260 && CREATE_NO_WINDOW == 0x08000000 && FILE_FLAG_BACKUP_SEMANTICS == 0x02000000);
  CK(15, INVALID_HANDLE_VALUE == (HANDLE)(LONG_PTR)-1 && INVALID_FILE_ATTRIBUTES == 0xFFFFFFFF);
  CK(16, LOWORD(0x12345678) == 0x5678 && HIWORD(0x12345678) == 0x1234 && MAKEWORD(1, 2) == 0x0201);
  CK(17, SUCCEEDED(S_OK) && FAILED(E_FAIL) && !FAILED(S_FALSE) && E_FAIL == (HRESULT)0x80004005L);
  CK(18, ERROR_FILE_NOT_FOUND == 2 && ERROR_ALREADY_EXISTS == 183 && ERROR_IO_PENDING == 997);
  LARGE_INTEGER li;
  li.QuadPart = 0x123456789LL;
  CK(19, li.LowPart == 0x23456789u && li.HighPart == 1 && li.u.LowPart == 0x23456789u);
  li.HighPart = 2;
  CK(20, li.QuadPart == 0x223456789LL);
  SYSTEM_INFO si;
  ZeroMemory(&si, sizeof(si));
  si.wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
  CK(21, si.dwOemId == 9 && si.dwPageSize == 0);
  GUID a = {1, 2, 3, {4, 5, 6, 7, 8, 9, 10, 11}};
  GUID b = a;
  CK(22, IsEqualGUID(a, b));
  b.Data4[7] = 0;
  CK(23, !IsEqualGUID(a, b));
  char buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  MoveMemory(buf + 1, buf, 4);
  CK(24, buf[0] == 1 && buf[1] == 1 && buf[4] == 4 && buf[5] == 6);
  return 0;
}
)OKMS", 0);
}

// kernel32 calls through our declarations: system facts, the clock, text conversion, modules, files, errors, memory,
// environment, time and the Tool Help module list.
static void r27_win_kernel(Tally& tally) {
    std::printf("r27_win_kernel\n");
    okayms_test::run_both(kRunKernel, tally, "win_kernel", std::string(kAlloc) + R"OKMS(
#include <windows.h>
#include <tlhelp32.h>
typedef DWORD (WINAPI* TickFn)(void);
int entry(void) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  CK(1, si.dwNumberOfProcessors > 0 && si.dwPageSize == 4096);
  SYSTEM_INFO ni;
  GetNativeSystemInfo(&ni);
  CK(2, ni.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64);
  LARGE_INTEGER f, c0, c1;
  CK(3, QueryPerformanceFrequency(&f) && f.QuadPart > 0);
  QueryPerformanceCounter(&c0);
  QueryPerformanceCounter(&c1);
  CK(4, c1.QuadPart >= c0.QuadPart);
  CK(5, GetTickCount64() >= GetTickCount() / 2);
  // UTF-8 "a\xC3\xA9\xE2\x82\xAC" (a, e acute, euro sign) to UTF-16 and back.
  const char* text = "a\xC3\xA9\xE2\x82\xAC";
  int n = MultiByteToWideChar(CP_UTF8, 0, text, 6, nullptr, 0);
  CK(6, n == 3);
  WCHAR w[8];
  MultiByteToWideChar(CP_UTF8, 0, text, 6, w, 8);
  CK(7, w[0] == L'a' && w[1] == L'é' && w[2] == 0x20AC);
  char back[16];
  int k = WideCharToMultiByte(CP_UTF8, 0, w, 3, back, 16, nullptr, nullptr);
  CK(8, k == 6 && back[1] == (char)0xC3 && back[5] == (char)0xAC);
  CK(9, GetModuleHandleW(nullptr) != nullptr);
  HMODULE kern = LoadLibraryA("kernel32.dll");
  CK(10, kern != nullptr);
  TickFn tick = (TickFn)(void*)GetProcAddress(kern, "GetTickCount");
  CK(11, tick != nullptr && tick() > 0);
  CK(12, GetProcAddress(kern, "NoSuchFunctionHere") == nullptr);
  FreeLibrary(kern);
  SetLastError(0);
  HANDLE miss = CreateFileW(L"r27-no-such-file.bin", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  CK(13, miss == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND);
  const WCHAR* name = L"r27-kernel-file.bin";
  HANDLE h = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  CK(14, h != INVALID_HANDLE_VALUE);
  DWORD wrote = 0;
  CK(15, WriteFile(h, "hello world", 11, &wrote, nullptr) && wrote == 11);
  LARGE_INTEGER zero;
  zero.QuadPart = 6;
  LARGE_INTEGER pos;
  CK(16, SetFilePointerEx(h, zero, &pos, FILE_BEGIN) && pos.QuadPart == 6);
  char got[8] = {0};
  DWORD read = 0;
  CK(17, ReadFile(h, got, 5, &read, nullptr) && read == 5 && got[0] == 'w' && got[4] == 'd');
  LARGE_INTEGER size;
  CK(18, GetFileSizeEx(h, &size) && size.QuadPart == 11);
  CloseHandle(h);
  DWORD attrs = GetFileAttributesW(name);
  CK(19, attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0);
  WIN32_FILE_ATTRIBUTE_DATA fad;
  CK(20, GetFileAttributesExW(name, GetFileExInfoStandard, &fad) && fad.nFileSizeLow == 11 && fad.nFileSizeHigh == 0);
  CK(21, DeleteFileW(name) && GetFileAttributesW(name) == INVALID_FILE_ATTRIBUTES);
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof(ms);
  CK(22, GlobalMemoryStatusEx(&ms) && ms.ullTotalPhys > 0 && ms.ullAvailPhys <= ms.ullTotalPhys);
  WCHAR env[16];
  CK(23, GetEnvironmentVariableW(L"R27_NOT_SET_ANYWHERE", env, 16) == 0);
  CK(24, SetEnvironmentVariableW(L"R27_VAR", L"xyz") && GetEnvironmentVariableW(L"R27_VAR", env, 16) == 3 && env[2] == L'z');
  WCHAR path[MAX_PATH];
  DWORD plen = GetModuleFileNameW(nullptr, path, MAX_PATH);
  CK(25, plen > 4 && path[plen - 4] == L'.' && (path[plen - 1] == L'e' || path[plen - 1] == L'E'));
  SYSTEMTIME st;
  GetLocalTime(&st);
  CK(26, st.wYear >= 2024 && st.wMonth >= 1 && st.wMonth <= 12);
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  CK(27, ft.dwHighDateTime > 0);
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
  CK(28, snap != INVALID_HANDLE_VALUE);
  MODULEENTRY32W me;
  me.dwSize = sizeof(me);
  CK(29, Module32FirstW(snap, &me) && me.hModule == GetModuleHandleW(nullptr));
  CloseHandle(snap);
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 16);
  char* gp = (char*)GlobalLock(g);
  CK(30, gp != nullptr);
  gp[0] = 'q';
  GlobalUnlock(g);
  CK(31, GlobalFree(g) == nullptr);
  return 0;
}
)OKMS", 0);
}

// Threads, events, critical sections, condition variables, SRW locks, waiting; a thread started from a static member
// function by its address (&W::Main) and by its bare name inside the class; _beginthreadex from <process.h>.
static void r27_win_threads(Tally& tally) {
    std::printf("r27_win_threads\n");
    okayms_test::run_both(kRunThreads, tally, "win_threads", std::string(kAlloc) + R"OKMS(
#include <windows.h>
#include <process.h>
static CRITICAL_SECTION cs;
static CONDITION_VARIABLE cv;
static SRWLOCK srw;
static int counter = 0;
static int ready = 0;
static DWORD WINAPI Adder(LPVOID p) {
  int n = *(int*)p;
  for (int i = 0; i < n; ++i) {
    EnterCriticalSection(&cs);
    ++counter;
    LeaveCriticalSection(&cs);
    AcquireSRWLockExclusive(&srw);
    ReleaseSRWLockExclusive(&srw);
  }
  return 7;
}
static unsigned __stdcall CrtEntry(void* p) { *(int*)p = 42; return 3; }
struct W {
  HANDLE thread = nullptr;
  HANDLE wake = nullptr;
  int seen = 0;
  static DWORD WINAPI Main(LPVOID p) {
    W* w = static_cast<W*>(p);
    WaitForSingleObject(w->wake, INFINITE);
    w->seen = 1;
    return 11;
  }
  void Start(bool by_name) {
    wake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (by_name) thread = CreateThread(nullptr, 0, Main, this, 0, nullptr);
    else thread = CreateThread(nullptr, 0, &W::Main, this, 0, nullptr);
  }
};
static DWORD WINAPI Waiter(LPVOID p) {
  (void)p;
  EnterCriticalSection(&cs);
  ready = 1;
  WakeAllConditionVariable(&cv);
  LeaveCriticalSection(&cs);
  return 0;
}
int entry(void) {
  InitializeCriticalSection(&cs);
  InitializeConditionVariable(&cv);
  InitializeSRWLock(&srw);
  int per = 500;
  HANDLE hs[4];
  for (int i = 0; i < 4; ++i) hs[i] = CreateThread(nullptr, 0, Adder, &per, 0, nullptr);
  CK(1, hs[0] != nullptr && hs[3] != nullptr);
  CK(2, WaitForMultipleObjects(4, hs, TRUE, INFINITE) < WAIT_OBJECT_0 + 4);
  DWORD code = 0;
  CK(3, GetExitCodeThread(hs[2], &code) && code == 7);
  for (int i = 0; i < 4; ++i) CloseHandle(hs[i]);
  CK(4, counter == 2000);
  for (int pass = 0; pass < 2; ++pass) {
    W w;
    w.Start(pass == 0);
    CK(5, w.thread != nullptr && w.wake != nullptr);
    CK(6, WaitForSingleObject(w.thread, 30) == WAIT_TIMEOUT);
    SetEvent(w.wake);
    CK(7, WaitForSingleObject(w.thread, INFINITE) == WAIT_OBJECT_0 && w.seen == 1);
    CK(8, GetExitCodeThread(w.thread, &code) && code == 11);
    CloseHandle(w.thread);
    CloseHandle(w.wake);
  }
  HANDLE wt = CreateThread(nullptr, 0, Waiter, nullptr, 0, nullptr);
  EnterCriticalSection(&cs);
  while (!ready) SleepConditionVariableCS(&cv, &cs, INFINITE);
  LeaveCriticalSection(&cs);
  WaitForSingleObject(wt, INFINITE);
  CloseHandle(wt);
  CK(9, ready == 1);
  int out = 0;
  HANDLE ct = (HANDLE)_beginthreadex(nullptr, 0, &CrtEntry, &out, 0, nullptr);
  CK(10, ct != nullptr);
  WaitForSingleObject(ct, INFINITE);
  CK(11, GetExitCodeThread(ct, &code) && code == 3 && out == 42);
  CloseHandle(ct);
  HANDLE ev = CreateEventW(nullptr, FALSE, TRUE, nullptr);
  CK(12, WaitForSingleObject(ev, 0) == WAIT_OBJECT_0 && WaitForSingleObject(ev, 0) == WAIT_TIMEOUT);
  SetEvent(ev);
  ResetEvent(ev);
  CK(13, WaitForSingleObject(ev, 0) == WAIT_TIMEOUT);
  CloseHandle(ev);
  CK(14, GetCurrentThreadId() != 0);
  SwitchToThread();
  Sleep(1);
  DeleteCriticalSection(&cs);
  return 0;
}
)OKMS", 0);
}

// <winhttp.h>: WinHttpCrackUrl fills URL_COMPONENTS, so the structure's layout is checked against winhttp.dll.
static void r27_winhttp(Tally& tally) {
    std::printf("r27_winhttp\n");
    okayms_test::run_both(kRunHttp, tally, "winhttp", std::string(kAlloc) + R"OKMS(
#include <windows.h>
#include <winhttp.h>
int entry(void) {
  CK(1, sizeof(URL_COMPONENTS) == 104);
  const WCHAR* url = L"https://example.org:8443/a/b?q=1";
  URL_COMPONENTS uc;
  ZeroMemory(&uc, sizeof(uc));
  uc.dwStructSize = sizeof(uc);
  uc.dwHostNameLength = (DWORD)-1;
  uc.dwUrlPathLength = (DWORD)-1;
  uc.dwExtraInfoLength = (DWORD)-1;
  CK(2, WinHttpCrackUrl(url, 0, 0, &uc) != FALSE);
  CK(3, uc.nScheme == INTERNET_SCHEME_HTTPS && uc.nPort == 8443);
  CK(4, uc.dwHostNameLength == 11 && uc.lpszHostName == url + 8);
  CK(5, uc.dwUrlPathLength == 4 && uc.lpszUrlPath[0] == L'/' && uc.lpszUrlPath[3] == L'b');
  CK(6, uc.dwExtraInfoLength == 4 && uc.lpszExtraInfo[0] == L'?');
  CK(7, WINHTTP_FLAG_SECURE == 0x00800000 && WINHTTP_QUERY_STATUS_CODE == 19 && WINHTTP_QUERY_FLAG_NUMBER == 0x20000000);
  CK(8, ERROR_WINHTTP_TIMEOUT == 12002 && WINHTTP_OPTION_REDIRECT_POLICY == 88 && WINHTTP_QUERY_ETAG == 54);
  return 0;
}
)OKMS", 0);
}

// Language pieces the engine's Windows paths needed: anonymous unions and structs as members, several typedef names in
// one declaration, wide character literals and the full escape set, new T{...} and new (std::nothrow) T, casts to a
// reference type, a data member that hides a type name, and pointers to static member functions.
static void r27_lang(Tally& tally) {
    std::printf("r27_lang\n");
    okayms_test::run_both(kRun, tally, "lang", std::string(kAlloc) + R"OKMS(
#include <new>
#include <string>
struct thread { int id; };  // a type name that a data member below hides (in the engine: std::thread)
typedef union _V { struct { unsigned short lo; unsigned short hi; }; unsigned int all; } V, *PV, **PPV;
struct Outer {
  int tag;
  union {
    struct { int x; int y; };
    long long xy;
  };
  int last;
};
struct Plain { int a; void* p; long long c; };
struct Base { int b; std::string s; };
struct Derived : Base { int d; };
struct Holder {
  int thread = 0;  // the same name as std::thread
  int bump() { thread = thread + 1; thread += 2; return thread; }
};
typedef int (*IntFn)(int);
struct Fns {
  static int Twice(int v) { return v * 2; }
  IntFn pick(bool f) { return f ? &Fns::Twice : Twice; }
};
static int call(IntFn f, int v) { return f(v); }
int entry(void) {
  V v;
  v.all = 0x00020001u;
  PV pv = &v;
  PPV ppv = &pv;
  CK(1, v.lo == 1 && v.hi == 2 && (*ppv)->hi == 2 && sizeof(V) == 4);
  Outer o;
  o.tag = 5;
  o.xy = 0;
  o.x = 7;
  o.y = 9;
  o.last = 3;
  CK(2, sizeof(Outer) == 24 && o.x == 7 && o.y == 9 && o.xy == ((9LL << 32) | 7) && o.tag == 5 && o.last == 3);
  Outer* op = &o;
  op->y = 1;
  CK(3, op->xy == ((1LL << 32) | 7));
  CK(4, L'a' == 97 && L'\0' == 0 && L'é' == 0xE9 && u'\x20AC' == 0x20AC && U'\U0001F600' == 0x1F600);
  CK(5, '\a' == 7 && '\b' == 8 && '\f' == 12 && '\v' == 11 && '\033' == 27 && '\101' == 'A' && '\7' == 7);
  const char* s = "\101\102\x43\a";
  CK(6, s[0] == 'A' && s[1] == 'B' && s[2] == 'C' && s[3] == 7);
  wchar_t wbuf[3] = {L'x', L'\\', 0};
  CK(7, wbuf[1] == 92);
  Plain* p = new Plain{4, nullptr, 9};
  CK(8, p->a == 4 && p->p == nullptr && p->c == 9);
  Plain* q = new Plain{1};
  CK(9, q->a == 1 && q->p == nullptr && q->c == 0);
  int* i = new int{6};
  int* z = new int{};
  CK(10, *i == 6 && *z == 0);
  Plain* r = new (std::nothrow) Plain;
  CK(11, r != nullptr);
  r->a = 2;
  Derived d;
  d.b = 3;
  d.s = "hi";
  d.d = 4;
  Base copy;
  copy = static_cast<const Base&>(d);
  CK(12, copy.b == 3 && copy.s == "hi");
  Base& br = static_cast<Base&>(d);
  br.b = 8;
  CK(13, d.b == 8);
  int n = 5;
  (int&)n = 6;
  CK(14, n == 6);
  Holder h;
  CK(15, h.bump() == 3 && h.bump() == 6);
  Fns fns;
  CK(16, call(&Fns::Twice, 4) == 8 && call(Fns::Twice, 5) == 10);
  CK(17, fns.pick(true)(6) == 12 && fns.pick(false)(7) == 14);
  return 0;
}
)OKMS", 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r27_win_types(tally);
    r27_win_kernel(tally);
    r27_win_threads(tally);
    r27_winhttp(tally);
    r27_lang(tally);
    return tally.finish(5);
}
