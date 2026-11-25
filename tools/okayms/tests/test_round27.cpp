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
const okayms_test::ProgramRun kRunAtomic{"r27", {"CreateThread", "WaitForMultipleObjects", "CloseHandle", "SwitchToThread"}};
const okayms_test::ProgramRun kRunShared{"r27", {"CreateThread", "WaitForMultipleObjects", "CloseHandle", "GetExitCodeThread"}};
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

// Hash containers with a hasher given (keys that have == and no <), and the ordered form kept for the default hasher.
static void r27_unordered_hash(Tally& tally) {
    std::printf("r27_unordered_hash\n");
    okayms_test::run_both(kRun, tally, "unordered_hash", std::string(kAlloc) + R"OKMS(
#include <unordered_map>
#include <unordered_set>
#include <string>
struct Ep { unsigned a; unsigned short port; bool operator==(const Ep& o) const { return a == o.a && port == o.port; } };
struct EpHash { unsigned long long operator()(const Ep& e) const { return ((unsigned long long)e.a << 16) ^ e.port; } };
static Ep ep(unsigned a, unsigned short p) { Ep e; e.a = a; e.port = p; return e; }
int entry(void) {
  std::unordered_map<Ep, int, EpHash> m;
  for (int i = 0; i < 100; ++i) m[ep((unsigned)i, (unsigned short)(i * 3))] = i * 2;
  CK(1, m.size() == 100);
  Ep k = ep(42, 126);
  CK(2, m.count(k) == 1 && m[k] == 84 && m.find(k)->second == 84 && m.find(ep(42, 1)) == m.end());
  int erased = 0;
  for (auto it = m.begin(); it != m.end();) {
    if (it->second % 4 == 0) {
      it = m.erase(it);
      ++erased;
    } else {
      ++it;
    }
  }
  CK(3, erased == 50 && m.size() == 50 && m.count(k) == 0);
  CK(4, m.at(ep(43, 129)) == 86);
  int sum = 0;
  for (auto& kv : m) sum += kv.second;
  CK(5, sum == 5000);
  CK(6, m.erase(ep(43, 129)) == 1 && m.erase(ep(43, 129)) == 0 && m.size() == 49);
  auto ins = m.insert(std::pair<Ep, int>(ep(7, 7), 70));
  CK(7, ins.second && ins.first->second == 70 && !m.insert(std::pair<Ep, int>(ep(7, 7), 1)).second);
  std::unordered_set<Ep, EpHash> s;
  s.insert(k);
  s.insert(ep(1, 2));
  s.insert(k);
  CK(8, s.size() == 2 && s.count(ep(1, 2)) == 1);
  s.erase(k);
  CK(9, s.size() == 1 && s.count(k) == 0);
  std::unordered_map<std::string, int> plain;
  plain["b"] = 2;
  plain["a"] = 1;
  CK(10, plain.size() == 2 && plain["a"] == 1 && plain.count("c") == 0);
  std::unordered_map<Ep, std::unordered_map<unsigned, int>, EpHash> nested;
  nested[k][5] = 9;
  CK(11, nested[k][5] == 9 && nested.size() == 1);
  return 0;
}
)OKMS", 0);
}

// More language pieces: c ? a : b with temporaries of a class with a destructor, if with an init statement, adjacent
// string literals, extern variables defined later in the file, const locals as array lengths, member functions and
// data members that hide a type or template name, a plain struct as a default template argument with a partial
// specialization chosen through it, and a macro call over several lines.
static void r27_lang2(Tally& tally) {
    std::printf("r27_lang2\n");
    okayms_test::run_both(kRun, tally, "lang2", std::string(kAlloc) + R"OKMS(
#include <string>
static int dtors = 0;
struct Noisy { int v; Noisy(int x) : v(x) {} Noisy(const Noisy& o) : v(o.v) {} ~Noisy() { ++dtors; } };
static Noisy make(int x) { return Noisy(x); }
static std::string cut_spaces(const std::string& s) {
  std::size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
static int check(int n) { return n * 2; }
extern int later_value;
extern const char* const kLaterName;
static int read_later() { return later_value; }
int later_value = 41;
extern const char* const kLaterName = "na" "me";
struct Bounds { int unused; };
struct duration { int unused; };
template <typename T> struct Box { T v; };
struct Shape {
  int w = 3;
  float duration = 2.0f;
  void Bounds(int& lo, int& hi) const { lo = 1; hi = w; }
  int Span() const { int lo, hi; Bounds(lo, hi); return hi - lo; }
  int Box = 4;
  bool Short() const { return duration < 3.0f && Box < 5; }
};
struct Tag {};
template <typename K, typename H = Tag> struct Pick { int v() const { return 1; } };
template <typename K> struct Pick<K, Tag> { int v() const { return 2; } };
#define SUM3(a, b, c) ((a) + (b) + (c))
int entry(void) {
  {
    bool c = true;
    Noisy n = c ? make(1) : Noisy(2);
    CK(1, n.v == 1);
    c = false;
    Noisy m = c ? make(3) : make(4);
    CK(2, m.v == 4);
  }
  CK(3, dtors >= 2);
  CK(4, cut_spaces("  ab ") == "ab" && cut_spaces("   ").empty() && cut_spaces("x") == "x");
  if (const int w = check(3); w > 5) {
    CK(5, w == 6);
  } else {
    return 6;
  }
  int hits = 0;
  if (int q = 0; q == 0) hits = 1; else hits = 2;
  CK(7, hits == 1);
  const char* joined = "ab" "cd"
                       "ef";
  CK(8, joined[0] == 'a' && joined[4] == 'e' && joined[5] == 'f' && joined[6] == 0);
  CK(9, read_later() == 41 && kLaterName[0] == 'n' && kLaterName[3] == 'e');
  constexpr int kLen = 4;
  const int kTwice = kLen * 2;
  int arr[kLen];
  int big[kTwice];
  CK(10, sizeof(arr) == 16 && sizeof(big) == 32);
  static const int kStatic = 3;
  char small[kStatic];
  CK(11, sizeof(small) == 3);
  Shape s;
  CK(12, s.Span() == 2 && s.Short());
  Pick<int> p1;
  Pick<int, int> p2;
  CK(13, p1.v() == 2 && p2.v() == 1);
  int total = SUM3(1,
                   2,
                   3);
  CK(14, total == 6);
  std::string path = "a/b/c.txt";
  CK(15, path.find_last_of('/') == 3 && path.find_first_of('/') == 1 && path.find_last_of("/.") == 5 &&
             path.find_first_not_of('a') == 1 && path.find_last_not_of('t') == 7 && path.find_last_not_of("tx") == 5);
  return 0;
}
)OKMS", 0);
}

// Lambdas in member functions: [this], [&] and [=] reaching the members of the class through the captured pointer,
// a lambda in a lambda, and a captureless lambda calling a static member function of the class around it.
static void r27_lambda_this(Tally& tally) {
    std::printf("r27_lambda_this\n");
    okayms_test::run_both(kRun, tally, "lambda_this", std::string(kAlloc) + R"OKMS(
struct Font { int w = 8; int Advance() const { return w + 1; } static int Base() { return 2; }
  int Line(int glyphs) const { auto lineW = [this](int g) { return g > 0 ? (g - 1) * Advance() + Base() : 0; }; return lineW(glyphs); }
  int Sum(int n) { int total = 0; auto add = [&](int k) { total += k * w; this->w += 0; }; for (int i = 0; i < n; ++i) add(i); return total; }
  int Nested() { auto outer = [this]() { auto inner = [this]() { return w * 2; }; return inner() + w; }; return outer(); }
  int ByValue(int k) { auto f = [=]() { return k + w; }; w = 100; return f(); }
};
int entry(void) {
  Font f;
  if (f.Line(3) != 20) return 1;
  if (f.Sum(4) != 48) return 2;
  if (f.Nested() != 24) return 3;
  if (f.ByValue(1) != 101) return 4;
  return 0;
}
)OKMS", 0);
}

// What okay/Render/Mesh.hpp (included by 157 engine files) stopped at: brace elision for std::array elements, a
// reference member made from a braced list, x = y = v with objects, a local class whose name a member function also has,
// a range for over a braced list of pointers, a class template derived from a class template, a fold over operator<<, and
// the algorithms unique, replace, rotate, the heap operations, transform, equal and nth_element.
static void r27_mesh_pieces(Tally& tally) {
    std::printf("r27_mesh_pieces\n");
    okayms_test::run_both(kRun, tally, "mesh_pieces", std::string(kAlloc) + R"OKMS(
#include <array>
#include <vector>
#include <map>
#include <tuple>
#include <sstream>
#include <string>
#include <algorithm>
#include <numeric>
struct V3 { float x, y, z; };
struct Mesh {
  std::vector<V3> vertices;
  static Mesh Grid(int n) { Mesh m; m.vertices.resize((std::size_t)n); return m; }
  int LocalGrid() const {
    struct Grid { int cells = 3; int Count() const { return cells; } };
    Grid g;
    return g.Count();
  }
  void Bounds(V3& lo, V3& hi) const {
    if (vertices.empty()) { lo = hi = V3{0, 0, 0}; return; }
    lo = hi = vertices[0];
  }
  static int Twice(int v) { return v * 2; }
  int Apply(int v) const { auto f = [](int x) { return Twice(x) + 1; }; return f(v); }
};
template <typename T> struct Base { T v_; Base() : v_() {} Base(T d) : v_(d) {} T get() const { return v_; } };
template <typename T> struct Derived : Base<T> { Derived() : Base<T>() {} Derived(T d) : Base<T>(d) {} T twice() const { return this->v_ + this->v_; } };
template <typename... A> std::string join(const A&... args) { std::ostringstream os; (os << ... << args); return os.str(); }
int entry(void) {
  std::vector<std::array<int, 3>> groups;
  int slot = 4, i = 5;
  groups.push_back({slot, i, 0});
  ++groups.back()[2];
  CK(1, groups.size() == 1 && groups[0][0] == 4 && groups[0][1] == 5 && groups[0][2] == 1);
  std::vector<std::pair<int, int>> cvv;
  cvv.push_back({1, 2});
  struct { std::vector<std::pair<int, int>>& second; } cv{cvv};
  cv.second.push_back({3, 4});
  CK(2, cvv.size() == 2 && cvv[1].first == 3);
  Mesh m;
  V3 lo, hi;
  m.Bounds(lo, hi);
  CK(3, lo.x == 0 && hi.z == 0);
  m.vertices.push_back(V3{1, 2, 3});
  m.Bounds(lo, hi);
  CK(4, lo.y == 2 && hi.z == 3);
  CK(5, m.LocalGrid() == 3 && Mesh::Grid(2).vertices.size() == 2 && m.Apply(5) == 11);
  std::vector<int> a = {1, 2}, b = {3};
  int total = 0;
  for (std::vector<int>* l : {&a, &b}) total += (int)l->size();
  CK(6, total == 3);
  Derived<int> d(21);
  Derived<bool> db(true);
  CK(7, d.twice() == 42 && d.get() == 21 && db.get());
  CK(8, join(1, "-", 2.5, '!') == "1-2.5!");
  std::vector<int> u = {1, 1, 2, 2, 2, 3, 1};
  u.erase(std::unique(u.begin(), u.end()), u.end());
  CK(9, u.size() == 4 && u[2] == 3 && u[3] == 1);
  std::replace(u.begin(), u.end(), 1, 9);
  CK(10, u[0] == 9 && u[3] == 9);
  std::vector<int> r = {1, 2, 3, 4, 5};
  std::rotate(r.begin(), r.begin() + 2, r.end());
  CK(11, r[0] == 3 && r[4] == 2);
  std::vector<int> h = {5, 1, 4, 2};
  std::make_heap(h.begin(), h.end());
  CK(12, h[0] == 5);
  std::pop_heap(h.begin(), h.end());
  CK(13, h.back() == 5 && h[0] == 4);
  h.back() = 7;
  std::push_heap(h.begin(), h.end());
  CK(14, h[0] == 7);
  std::vector<int> t(3);
  std::transform(r.begin(), r.begin() + 3, t.begin(), [](int x) { return x * 10; });
  CK(15, t[0] == 30 && t[2] == 50 && std::equal(t.begin(), t.end(), t.begin()));
  std::vector<int> nth = {9, 3, 7, 1};
  std::nth_element(nth.begin(), nth.begin() + 1, nth.end());
  CK(16, nth[1] == 3);
  return 0;
}
)OKMS", 0);
}

// The interlocked intrinsics of <intrin.h>, compiled inline as LOCK-prefixed instructions (on stack and global data).
static void r27_intrinsics(Tally& tally) {
    std::printf("r27_intrinsics\n");
    okayms_test::run_both(kRun, tally, "intrinsics", std::string(kAlloc) + R"OKMS(
#include <intrin.h>
int entry(void) {
  long v = 5;
  if (_InterlockedExchange(&v, 9) != 5 || v != 9) return 1;
  if (_InterlockedExchangeAdd(&v, 3) != 9 || v != 12) return 2;
  if (_InterlockedCompareExchange(&v, 20, 11) != 12 || v != 12) return 3;
  if (_InterlockedCompareExchange(&v, 20, 12) != 12 || v != 20) return 4;
  if (_InterlockedIncrement(&v) != 21 || _InterlockedDecrement(&v) != 20) return 5;
  __int64 w = 0x100000000LL;
  if (_InterlockedExchangeAdd64(&w, -1) != 0x100000000LL || w != 0xFFFFFFFFLL) return 6;
  if (_InterlockedCompareExchange64(&w, 7, 0xFFFFFFFFLL) != 0xFFFFFFFFLL || w != 7) return 7;
  if (_InterlockedExchange64(&w, -2) != 7 || w != -2 || _InterlockedIncrement64(&w) != -1) return 8;
#ifndef __GNUC__  // MinGW's <intrin.h> has no 8-bit and only some 16-bit forms
  char c = 'a';
  if (_InterlockedExchange8(&c, 'b') != 'a' || c != 'b') return 9;
  if (_InterlockedCompareExchange8(&c, 'z', 'b') != 'b' || c != 'z' || _InterlockedExchangeAdd8(&c, 1) != 'z' || c != '{') return 10;
  short s = -3;
  if (_InterlockedExchangeAdd16(&s, 1) != -3 || s != -2 || _InterlockedCompareExchange16(&s, 100, -2) != -2 || s != 100) return 11;
  if (_InterlockedDecrement16(&s) != 99 || _InterlockedExchange16(&s, -7) != 99 || s != -7) return 12;
#endif
  long arr[3] = {1, 2, 3};
  int i = 1;
  if (_InterlockedExchangeAdd(&arr[i + 1], arr[0] + 10) != 3 || arr[2] != 14) return 13;
  return 0;
}
)OKMS", 0);
}

// std::atomic on the interlocked instructions: every member on int, bool, an enumeration, pointers, float, 1, 2 and 8
// byte integers, atomic_flag, and four threads that add 20000 each to shared counters (80000 only if the adds are atomic).
static void r27_atomic_threads(Tally& tally) {
    std::printf("r27_atomic_threads\n");
    okayms_test::run_both(kRunAtomic, tally, "atomic_threads", std::string(kAlloc) + R"OKMS(
#include <atomic>
#include <windows.h>
enum class Mode : unsigned char { A = 1, B = 7 };
struct Pair { short x; short y; };
static std::atomic<int> counter{0};
static std::atomic<long long> wide{0};
static std::atomic<bool> go{false};
static DWORD WINAPI Worker(LPVOID p) {
  (void)p;
  while (!go.load()) SwitchToThread();
  for (int i = 0; i < 20000; ++i) {
    counter.fetch_add(1);
    ++wide;
    counter -= 0;
  }
  return 0;
}
int entry(void) {
  std::atomic<int> a(5);
  CK(1, a.load() == 5 && a.exchange(7) == 5 && a == 7);
  int e = 6;
  CK(2, !a.compare_exchange_strong(e, 9) && e == 7 && a.compare_exchange_strong(e, 9) && a.load() == 9);
  CK(3, a.fetch_add(3) == 9 && a.fetch_sub(2) == 12 && a.load() == 10);
  CK(4, a.fetch_or(5) == 10 && a.fetch_and(6) == 15 && a.fetch_xor(1) == 6 && a.load() == 7);
  CK(5, ++a == 8 && a++ == 8 && a-- == 9 && --a == 7 && (a += 3) == 10 && (a -= 4) == 6);
  std::atomic<bool> f(false);
  CK(6, !f.exchange(true) && f.load() && f);
  std::atomic<Mode> m(Mode::A);
  Mode want = Mode::A;
  CK(7, m.compare_exchange_strong(want, Mode::B) && m.load() == Mode::B);
  int arr[4] = {0, 1, 2, 3};
  std::atomic<int*> p(arr);
  CK(8, p.fetch_add(2) == arr && p.load() == arr + 2 && *p.load() == 2 && (p -= 1) == arr + 1);
  std::atomic<float> fl(1.5f);
  CK(9, fl.exchange(2.0f) == 1.5f && fl.load() == 2.0f);
  std::atomic<unsigned char> uc(250);
  CK(10, uc.fetch_add(10) == 250 && uc.load() == 4);
  std::atomic<short> sh(-5);
  CK(11, sh.fetch_add(2) == -5 && sh.load() == -3);
  std::atomic<unsigned long long> big(0xFFFFFFFFull);
  CK(12, big.fetch_add(1) == 0xFFFFFFFFull && big.load() == 0x100000000ull);
  std::atomic_flag flag = ATOMIC_FLAG_INIT;
  CK(13, !flag.test_and_set() && flag.test_and_set());
  flag.clear();
  CK(14, !flag.test_and_set());
  HANDLE hs[4];
  for (int i = 0; i < 4; ++i) hs[i] = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
  go.store(true);
  WaitForMultipleObjects(4, hs, TRUE, INFINITE);
  for (int i = 0; i < 4; ++i) CloseHandle(hs[i]);
  CK(15, counter.load() == 80000 && wide.load() == 80000);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  return 0;
}
)OKMS", 0);
}

// shared_ptr and weak_ptr copied and locked by four threads at once: the counts stay right (atomic counts).
static void r27_shared_ptr_threads(Tally& tally) {
    std::printf("r27_shared_ptr_threads\n");
    okayms_test::run_both(kRunShared, tally, "shared_ptr_threads", std::string(kAlloc) + R"OKMS(
#include <memory>
#include <windows.h>
struct Obj { int v; };
static std::shared_ptr<Obj> shared;
static long volatile_go = 0;
static DWORD WINAPI Copier(LPVOID p) {
  (void)p;
  for (int i = 0; i < 20000; ++i) {
    std::shared_ptr<Obj> local = shared;
    std::weak_ptr<Obj> w = local;
    if (!w.lock()) return 1;
  }
  return 0;
}
int entry(void) {
  shared = std::make_shared<Obj>();
  shared->v = 3;
  HANDLE hs[4];
  for (int i = 0; i < 4; ++i) hs[i] = CreateThread(nullptr, 0, Copier, nullptr, 0, nullptr);
  WaitForMultipleObjects(4, hs, TRUE, INFINITE);
  DWORD code = 0;
  for (int i = 0; i < 4; ++i) { GetExitCodeThread(hs[i], &code); CloseHandle(hs[i]); if (code != 0) return 1; }
  CK(2, shared.use_count() == 1 && shared->v == 3);
  std::weak_ptr<Obj> w = shared;
  shared.reset();
  CK(3, w.expired() && !w.lock());
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
    r27_unordered_hash(tally);
    r27_lang2(tally);
    r27_lambda_this(tally);
    r27_mesh_pieces(tally);
    r27_intrinsics(tally);
    r27_atomic_threads(tally);
    r27_shared_ptr_threads(tally);
    return tally.finish(12);
}
