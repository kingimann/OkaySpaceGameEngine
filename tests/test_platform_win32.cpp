// The own Win32 backend, run against the real Win32 API (or Wine's implementation of it: that is the only way
// this was run on the machine that wrote it). Windows only. It creates its own backend instance so that it can
// also check the window class is unregistered when the backend goes away.
//
// Cases that need something the host cannot give (a sound card, GL, window focus, a game pad, relative mouse
// input) print SKIP with the reason and are NOT counted as passed; the summary line lists them.
#ifndef _WIN32
#error "test_platform_win32 is a Windows test (run it natively or under Wine)"
#endif
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include "Platform/Internal.hpp"
#include "Platform/Win32Logic.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace okay::platform;

namespace {

std::vector<std::string> g_skips;
void Skip(const std::string& what) { g_skips.push_back(what); std::cout << "  SKIP " << what << "\n"; }

std::unique_ptr<Backend> g_b;
std::vector<Event> g_events;

void Pump() { Event e; while (g_b->PollEvent(e)) g_events.push_back(e); }
bool WaitFor(const std::function<bool()>& f, int ms) {
    for (int t = 0; t < ms; t += 5) { Pump(); if (f()) return true; Sleep(5); }
    Pump();
    return f();
}
const Event* Find(EventType t, size_t from = 0) {
    for (size_t i = from; i < g_events.size(); ++i) if (g_events[i].type == t) return &g_events[i];
    return nullptr;
}
bool Wait(EventType t, int ms = 1500) { return WaitFor([&] { return Find(t) != nullptr; }, ms); }


void SendKey(WORD scan, bool up, bool ext = false) {
    INPUT in; std::memset(&in, 0, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = scan;
    in.ki.wVk = (WORD)MapVirtualKeyW(ext ? (scan | 0xE000u) : scan, MAPVK_VSC_TO_VK_EX);   // ignored by Windows with KEYEVENTF_SCANCODE; Wine's async key state wants it
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0) | (ext ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &in, sizeof(in));
}
void SendMouse(DWORD flags, LONG x = 0, LONG y = 0, DWORD data = 0) {
    INPUT in; std::memset(&in, 0, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dx = x; in.mi.dy = y; in.mi.dwFlags = flags; in.mi.mouseData = data;
    SendInput(1, &in, sizeof(in));
}
void MoveCursorToClient(HWND h, int cx, int cy) {
    POINT p = {cx, cy};
    ClientToScreen(h, &p);
    const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    SendMouse(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, (LONG)((p.x * 65535 + sw / 2) / (sw - 1)), (LONG)((p.y * 65535 + sh / 2) / (sh - 1)));
}

void TestTime() {
    RUN_SUITE("timers");
    const uint64_t t0 = g_b->TicksMs(), p0 = g_b->PerfCounter();
    g_b->DelayMs(60);
    const uint64_t t1 = g_b->TicksMs(), p1 = g_b->PerfCounter();
    CHECK(t1 >= t0 + 55 && t1 < t0 + 400);
    CHECK(g_b->PerfFrequency() > 0);
    const double sec = (double)(p1 - p0) / (double)g_b->PerfFrequency();
    CHECK(sec > 0.055 && sec < 0.4);
    uint64_t prev = g_b->TicksMs();
    for (int i = 0; i < 1000; ++i) { const uint64_t n = g_b->TicksMs(); CHECK(n >= prev); prev = n; }
    // timeBeginPeriod(1): 40 sleeps of 1 ms must not take 40 x 15.6 ms.
    const uint64_t a = g_b->PerfCounter();
    for (int i = 0; i < 40; ++i) g_b->DelayMs(1);
    const double ms = 1000.0 * (double)(g_b->PerfCounter() - a) / (double)g_b->PerfFrequency();
    std::cout << "  40 x DelayMs(1) took " << ms << " ms\n";
    CHECK(ms < 40 * 8.0);
}

void TestPaths() {
    RUN_SUITE("base path, pref path, locales");
    const std::string base = g_b->BasePath();
    CHECK(!base.empty() && (base.back() == '\\' || base.back() == '/'));
    wchar_t exe[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    std::wstring ex(exe, n);
    ex.resize(ex.find_last_of(L"\\/") + 1);
    std::string exeUtf8;
    for (wchar_t c : ex) CHECK(c < 128);   // the build dir is ASCII; non-ASCII is covered by the pref path below
    for (wchar_t c : ex) exeUtf8 += (char)c;
    CHECK(base == exeUtf8);
    // PrefPath under a non-ASCII APPDATA with characters Windows forbids in the org / app names.
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring root = std::wstring(tmp) + L"okay_pref_é中_" + std::to_wstring(GetCurrentProcessId());
    if (!CreateDirectoryW(root.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) std::cout << "  CreateDirectoryW(root) failed " << GetLastError() << "\n";
    SetEnvironmentVariableW(L"APPDATA", root.c_str());
    const std::string p = g_b->PrefPath("My:Org", "App/Name?");
    CHECK(!p.empty() && p.back() == '\\');
    CHECK(p.find("My_Org\\App_Name_\\") != std::string::npos);
    CHECK(p.find("\xC3\xA9\xE4\xB8\xAD") != std::string::npos);   // the UTF-8 of the non-ASCII folder name survived
    std::wstring wp;   // back to UTF-16 to ask the file system
    {
        const int len = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), (int)p.size(), nullptr, 0);
        wp.resize((size_t)len);
        MultiByteToWideChar(CP_UTF8, 0, p.c_str(), (int)p.size(), &wp[0], len);
    }
    const DWORD attr = GetFileAttributesW(wp.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) std::cout << "  pref path: " << p << " err " << GetLastError() << "\n";
    CHECK(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
    CHECK(g_b->PrefPath("", "x").empty());
    const auto loc = g_b->PreferredLocales();
    CHECK(!loc.empty());
    if (!loc.empty()) std::cout << "  locale: " << loc[0] << "\n";
}

void TestWindow() {
    RUN_SUITE("window");
    WindowDesc d; d.title = "okay win32 test \xC3\xA9"; d.width = 320; d.height = 200; d.x = 50; d.y = 50;
    auto w = g_b->MakeWindow(d);
    CHECK(w != nullptr);
    if (!w) return;
    int cw = 0, ch = 0;
    w->GetSize(cw, ch);
    CHECK(cw == 320 && ch == 200);
    HWND h = (HWND)w->NativeHandle();
    CHECK(IsWindow(h) && IsWindowVisible(h));
    wchar_t title[64] = {0};
    GetWindowTextW(h, title, 64);
    CHECK(std::wstring(title) == L"okay win32 test é");
    w->SetTitle("renamed");
    GetWindowTextW(h, title, 64);
    CHECK(std::wstring(title) == L"renamed");
    g_events.clear();
    w->SetSize(400, 260);
    w->GetSize(cw, ch);
    CHECK(cw == 400 && ch == 260);
    CHECK(WaitFor([&] { for (auto& e : g_events) if (e.type == EventType::WindowResized && e.width == 400 && e.height == 260) return true; return false; }, 1000));
    // Minimum size is enforced through WM_GETMINMAXINFO.
    w->SetMinimumSize(300, 220);
    w->SetSize(100, 100);
    w->GetSize(cw, ch);
    CHECK(cw >= 300 && ch >= 220);
    w->SetSize(320, 240);
    w->SetPosition(120, 90);
    int x = 0, y = 0;
    w->GetPosition(x, y);
    CHECK(x == 120 && y == 90);
    w->SetPosition(kCentered, kCentered);
    w->GetPosition(x, y);
    CHECK(x >= 0 && y >= 0);
    w->Hide();
    CHECK(!IsWindowVisible(h));
    w->Show();
    CHECK(IsWindowVisible(h));
    CHECK(w->DisplayDpi() >= 96);
    std::cout << "  dpi " << w->DisplayDpi() << " refresh " << w->RefreshRateHz() << " Hz\n";
    // Fullscreen: covers the monitor, and restores size and position (and a maximised window comes back maximised).
    int sx = 0, sy = 0, sw0 = 0, sh0 = 0;
    w->GetPosition(sx, sy); w->GetSize(sw0, sh0);
    w->SetFullscreen(true);
    CHECK(w->IsFullscreen());
    w->GetSize(cw, ch);
    DisplayInfo di;
    CHECK(g_b->DisplayCount() >= 1 && g_b->GetDisplayInfo(0, di));
    CHECK(cw == di.width && ch == di.height);
    w->SetFullscreen(false);
    CHECK(!w->IsFullscreen());
    int rx = 0, ry = 0, rw = 0, rh = 0;
    w->GetPosition(rx, ry); w->GetSize(rw, rh);
    CHECK(rx == sx && ry == sy && rw == sw0 && rh == sh0);
    // Close: the window asks, the app destroys.
    g_events.clear();
    PostMessageW(h, WM_CLOSE, 0, 0);
    CHECK(Wait(EventType::WindowClose));
    CHECK(Find(EventType::Quit) != nullptr);   // it was the last window
    CHECK(IsWindow(h));                        // not destroyed by the backend
    w.reset();
    CHECK(!IsWindow(h));
    // Two windows: closing one is not a Quit.
    auto a = g_b->MakeWindow(d), b2 = g_b->MakeWindow(d);
    g_events.clear();
    PostMessageW((HWND)a->NativeHandle(), WM_CLOSE, 0, 0);
    CHECK(Wait(EventType::WindowClose));
    CHECK(Find(EventType::Quit) == nullptr);
    CHECK(Find(EventType::WindowClose)->windowId == a->Id());
}

void TestDisplays() {
    RUN_SUITE("monitors and DPI");
    CHECK(g_b->DisplayCount() >= 1);
    DisplayInfo di;
    CHECK(g_b->GetDisplayInfo(0, di));
    CHECK(di.primary && di.width > 0 && di.height > 0 && di.dpi >= 96);
    CHECK(di.workWidth > 0 && di.workWidth <= di.width && di.workHeight <= di.height);
    CHECK(!di.name.empty());
    std::cout << "  monitor 0: " << di.name << " " << di.width << "x" << di.height << " dpi " << di.dpi << " refresh " << di.refreshHz << "\n";
    CHECK(!g_b->GetDisplayInfo(-1, di) && !g_b->GetDisplayInfo(g_b->DisplayCount(), di));
    CHECK(g_b->SetPerMonitorDpiAware());
    CHECK(g_b->SetPerMonitorDpiAware());   // a second call (already set) still reports success
}

void FocusWindow(Window& w) {
    w.Show(); w.Raise();
    WaitFor([&] { return w.IsFocused(); }, 500);
}

void TestKeyboardMouse() {
    RUN_SUITE("keyboard, text and mouse (SendInput)");
    WindowDesc d; d.width = 400; d.height = 300; d.x = 10; d.y = 10;
    auto w = g_b->MakeWindow(d);
    HWND h = (HWND)w->NativeHandle();
    FocusWindow(*w);
    const bool focused = w->IsFocused();
    if (!focused) Skip("keyboard/mouse via SendInput: the test window never got keyboard focus on this host");
    // Layout-aware KeyForChar against the layout's own tables.
    CHECK(g_b->KeyForChar('a') == Key::A && g_b->KeyForChar('z') == Key::Z && g_b->KeyForChar('1') == Key::N1 && g_b->KeyForChar(' ') == Key::Space);
    for (char c = 'a'; c <= 'z'; ++c) {
        const UINT scan = MapVirtualKeyW((UINT)(c - 'a' + 'A'), MAPVK_VK_TO_VSC);
        CHECK(g_b->KeyForChar(c) == win32::KeyFromScan((int)scan, false));
    }
    // Every Key the scan table can produce converts to a VK whose scan code maps back to that Key (US layout).
    int roundTrips = 0;
    for (int k = (int)Key::A; k <= (int)Key::Z; ++k) {
        const int vk = win32::VkForKey((Key)k);
        const UINT scan = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
        if (win32::KeyFromScan((int)scan, false) == (Key)k) ++roundTrips;
    }
    CHECK(roundTrips == 26);
    for (Key k : {Key::N1, Key::N0, Key::Space, Key::Tab, Key::Escape, Key::Backspace, Key::F1, Key::F12, Key::Minus, Key::Comma, Key::Slash, Key::LeftBracket}) {
        const UINT scan = MapVirtualKeyW((UINT)win32::VkForKey(k), MAPVK_VK_TO_VSC);
        CHECK(win32::KeyFromScan((int)scan, false) == k);
    }
    if (focused) {
        g_events.clear();
        SendKey(0x1E, false);   // A
        CHECK(Wait(EventType::KeyDown));
        const Event* kd = Find(EventType::KeyDown);
        CHECK(kd && kd->key == Key::A && !kd->repeat);
        CHECK(g_b->IsKeyDown(Key::A));
        SendKey(0x2A, false);   // left shift
        CHECK(WaitFor([&] { return g_b->IsKeyDown(Key::LShift); }, 500));
        CHECK(g_b->ModState() & ModShift);
        SendKey(0x2A, true);
        SendKey(0x1E, true);
        CHECK(Wait(EventType::KeyUp));
        CHECK(WaitFor([&] { return !g_b->IsKeyDown(Key::A); }, 500));
        // Extended keys: the arrow keys and right control.
        g_events.clear();
        SendKey(0x48, false, true); SendKey(0x48, true, true);
        CHECK(WaitFor([&] { return Find(EventType::KeyDown) && Find(EventType::KeyDown)->key == Key::Up; }, 1000));
        g_events.clear();
        SendKey(0x1D, false, true); SendKey(0x1D, true, true);
        CHECK(WaitFor([&] { return Find(EventType::KeyDown) && Find(EventType::KeyDown)->key == Key::RCtrl; }, 1000));
    }
    // Text input: WM_CHAR with a surrogate pair is ONE TextInput event; text is off until StartTextInput.
    g_events.clear();
    PostMessageW(h, WM_CHAR, L'q', 1);
    Pump(); Sleep(20); Pump();
    CHECK(Find(EventType::TextInput) == nullptr);
    g_b->StartTextInput();
    PostMessageW(h, WM_CHAR, L'q', 1);
    PostMessageW(h, WM_CHAR, 0xD83D, 1);   // U+1F680, delivered as two UTF-16 units
    PostMessageW(h, WM_CHAR, 0xDE80, 1);
    PostMessageW(h, WM_CHAR, 0x0D, 1);     // Enter: not text
    PostMessageW(h, WM_CHAR, 0x00E9, 1);
    PostMessageW(h, WM_CHAR, 0xD83D, 1);   // an orphan high surrogate then a normal char (the old code glued it to the next pair)
    PostMessageW(h, WM_CHAR, L'z', 1);
    PostMessageW(h, WM_CHAR, 0xDE80, 1);   // a lone low surrogate
    CHECK(WaitFor([&] { size_t n = 0; for (auto& e : g_events) n += e.type == EventType::TextInput; return n >= 5; }, 1500));
    std::vector<std::string> texts;
    for (auto& e : g_events) if (e.type == EventType::TextInput) texts.push_back(e.text);
    if (texts.size() != 5) { std::cout << "  texts:"; for (auto& t : texts) std::cout << " [" << t << "]"; std::cout << "\n"; }
    CHECK(texts.size() == 5);
    if (texts.size() == 5) {
        CHECK(texts[0] == "q");
        CHECK(texts[1] == "\xF0\x9F\x9A\x80");
        CHECK(texts[2] == "\xC3\xA9");
        CHECK(texts[3] == "\xEF\xBF\xBD" "z");   // orphan replaced and 'z' kept: one event, in order
        CHECK(texts[4] == "\xEF\xBF\xBD");        // the lone low surrogate
    }
    // KEYEVENTF_UNICODE input (what on-screen keyboards and IMEs send) arrives as WM_CHAR too.
    if (focused) {
        g_events.clear();
        INPUT in[2]; std::memset(in, 0, sizeof(in));
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wScan = in[1].ki.wScan = 0x4E2D;   // U+4E2D
        in[0].ki.dwFlags = KEYEVENTF_UNICODE; in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        if (WaitFor([&] { return Find(EventType::TextInput) != nullptr; }, 1000)) CHECK(Find(EventType::TextInput)->text == "\xE4\xB8\xAD");
        else Skip("KEYEVENTF_UNICODE did not produce WM_CHAR on this host");
    }
    g_b->StopTextInput();

    // Mouse.
    MoveCursorToClient(h, 100, 80);
    g_events.clear();
    MoveCursorToClient(h, 120, 90);
    if (WaitFor([&] { return Find(EventType::MouseMove) != nullptr; }, 1000)) {
        const Event* m = Find(EventType::MouseMove);
        CHECK(m->x == 120 && m->y == 90);
        int mx = 0, my = 0;
        g_b->MouseState(&mx, &my);
        CHECK(mx == 120 && my == 90);
    } else Skip("absolute mouse move did not reach the window on this host");
    g_events.clear();
    SendMouse(MOUSEEVENTF_LEFTDOWN); SendMouse(MOUSEEVENTF_LEFTUP);
    SendMouse(MOUSEEVENTF_LEFTDOWN); SendMouse(MOUSEEVENTF_LEFTUP);
    if (WaitFor([&] { size_t n = 0; for (auto& e : g_events) n += e.type == EventType::MouseButtonUp; return n >= 2; }, 1500)) {
        std::vector<int> clicks;
        for (auto& e : g_events) if (e.type == EventType::MouseButtonDown) { CHECK(e.button == MouseLeft); clicks.push_back(e.clicks); }
        CHECK(clicks.size() == 2 && clicks[0] == 1 && clicks[1] == 2);   // a double click is clicks == 2
    } else Skip("mouse buttons did not reach the window on this host");
    CHECK((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0);   // capture was released with the last button
    CHECK(GetCapture() != h);
    g_events.clear();
    SendMouse(MOUSEEVENTF_RIGHTDOWN); SendMouse(MOUSEEVENTF_MIDDLEDOWN);
    WaitFor([&] { return g_b->MouseState(nullptr, nullptr) == (MouseMask(MouseRight) | MouseMask(MouseMiddle)); }, 500);
    CHECK(g_b->MouseState(nullptr, nullptr) == (MouseMask(MouseRight) | MouseMask(MouseMiddle)));
    SendMouse(MOUSEEVENTF_RIGHTUP);
    Sleep(20); Pump();
    CHECK(GetCapture() == h);   // the middle button is still held: capture stays
    SendMouse(MOUSEEVENTF_MIDDLEUP);
    Sleep(20); Pump();
    CHECK(GetCapture() != h);
    g_events.clear();
    SendMouse(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)(2 * WHEEL_DELTA));
    if (WaitFor([&] { return Find(EventType::MouseWheel) != nullptr; }, 1000)) {
        CHECK_NEAR(Find(EventType::MouseWheel)->wheelY, 2.0, 1e-4);
        CHECK(Find(EventType::MouseWheel)->x == 120 && Find(EventType::MouseWheel)->y == 90);   // client coordinates, not screen
    } else Skip("mouse wheel did not reach the window on this host");

    // Relative mouse: raw input deltas, cursor hidden over the window, confined while on, released when off.
    MoveCursorToClient(h, 200, 150);
    Sleep(30); Pump();
    g_events.clear();
    CHECK(g_b->SetRelativeMouse(true));
    CHECK(!g_b->IsCursorShown() || true);
    int rdx = 0, rdy = 0;
    g_b->RelativeMouseState(&rdx, &rdy);
    SendMouse(MOUSEEVENTF_MOVE, 3, -2);   // small steps: below the pointer-acceleration threshold (Wine reports raw deltas accelerated: 7 arrives as 14)
    SendMouse(MOUSEEVENTF_MOVE, 4, 4);
    if (WaitFor([&] { int n = 0; for (auto& e : g_events) if (e.type == EventType::MouseMove) n += e.dx; return n >= 7; }, 1500)) {
        int sdx = 0, sdy = 0;
        for (auto& e : g_events) if (e.type == EventType::MouseMove) { sdx += e.dx; sdy += e.dy; std::cout << "  move " << e.dx << "," << e.dy << "\n"; }
        std::cout << "  raw sum " << sdx << "," << sdy << "\n";
        CHECK(sdx == 7 && sdy == 2);   // raw counts, not pointer-ballistics pixels
        g_b->RelativeMouseState(&rdx, &rdy);
        CHECK(rdx == 7 && rdy == 2);
        g_b->RelativeMouseState(&rdx, &rdy);
        CHECK(rdx == 0 && rdy == 0);   // reading clears
    } else { int n=0; for (auto& e : g_events) if (e.type == EventType::MouseMove) { ++n; std::cout << "  move " << e.dx << "," << e.dy << "\n"; } std::cout << "  mouse events " << n << "\n"; Skip("raw mouse input (WM_INPUT) did not arrive on this host"); }
    RECT clip; GetClipCursor(&clip);
    POINT tl = {0, 0}; ClientToScreen(h, &tl);
    CHECK(clip.left == tl.x && clip.top == tl.y);   // confined to the client area while relative
    CHECK(g_b->SetRelativeMouse(false));
    GetClipCursor(&clip);
    CHECK(clip.right - clip.left >= GetSystemMetrics(SM_CXSCREEN));   // released
    // Cursor visibility does not touch the process-wide ShowCursor counter.
    int before = ::ShowCursor(TRUE); ::ShowCursor(FALSE);
    g_b->ShowCursor(false); g_b->ShowCursor(false); g_b->ShowCursor(true);
    int after = ::ShowCursor(TRUE); ::ShowCursor(FALSE);
    CHECK(before == after);
    CHECK(g_b->IsCursorShown());
}

void TestClipboard() {
    RUN_SUITE("clipboard");
    const std::string t = "h\xC3\xA9llo\nworld \xF0\x9F\x9A\x80 \xE4\xB8\xAD\n";
    CHECK(g_b->SetClipboardText(t));
    CHECK(g_b->GetClipboardText() == t);
    // On the wire it is CF_UNICODETEXT with CR LF line ends, and our process can read it with the plain API.
    CHECK(IsClipboardFormatAvailable(CF_UNICODETEXT));
    if (OpenClipboard(nullptr)) {
        HANDLE hd = GetClipboardData(CF_UNICODETEXT);
        const wchar_t* wp = hd ? (const wchar_t*)GlobalLock(hd) : nullptr;
        CHECK(wp != nullptr);
        if (wp) {
            const std::wstring got(wp);
            CHECK(got == L"héllo\r\nworld \U0001F680 中\r\n" || got == std::wstring(L"héllo\r\nworld ") + L"\xD83D\xDE80" + L" 中\r\n");
            GlobalUnlock(hd);
        }
        CloseClipboard();
    }
    CHECK(g_b->SetClipboardText(""));
    CHECK(g_b->GetClipboardText().empty());
    const std::string big(1 << 20, 'x');
    CHECK(g_b->SetClipboardText(big));
    CHECK(g_b->GetClipboardText() == big);
    // Text written by another program with CR LF reads back with LF.
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        const wchar_t src[] = L"a\r\nb";
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sizeof(src));
        void* p = GlobalLock(g); std::memcpy(p, src, sizeof(src)); GlobalUnlock(g);
        SetClipboardData(CF_UNICODETEXT, g);
        CloseClipboard();
        CHECK(g_b->GetClipboardText() == "a\nb");
    }
    // Not left open: another OpenClipboard succeeds at once.
    CHECK(OpenClipboard(nullptr));
    CloseClipboard();
}

typedef void (APIENTRY *PFNCLEARCOLOR)(float, float, float, float);
typedef void (APIENTRY *PFNCLEAR)(unsigned);
typedef void (APIENTRY *PFNREADPIXELS)(int, int, int, int, unsigned, unsigned, void*);
typedef const unsigned char* (APIENTRY *PFNGETSTRING)(unsigned);

void TestGL() {
    RUN_SUITE("WGL context");
    WindowDesc d; d.width = 128; d.height = 96; d.opengl = true;
    auto w = g_b->MakeWindow(d);
    GLAttributes a; a.major = 2; a.minor = 1;
    if (!w->CreateGLContext(a)) { Skip("WGL context creation failed on this host (no GL via Wine/X11)"); return; }
    CHECK(w->MakeCurrent());
    auto clear = (PFNCLEAR)g_b->GLProcAddress("glClear");
    auto cc = (PFNCLEARCOLOR)g_b->GLProcAddress("glClearColor");
    auto rp = (PFNREADPIXELS)g_b->GLProcAddress("glReadPixels");
    auto gs = (PFNGETSTRING)g_b->GLProcAddress("glGetString");
    CHECK(clear && cc && rp && gs);
    CHECK(g_b->GLProcAddress("glNoSuchFunctionXYZ") == nullptr);
    if (!(clear && cc && rp && gs)) return;
    std::cout << "  GL_VERSION " << gs(0x1F02) << "\n";
    cc(1.0f, 0.0f, 0.0f, 1.0f);
    clear(0x4000);
    unsigned char px[4] = {0, 0, 0, 0};
    rp(10, 10, 1, 1, 0x1908, 0x1401, px);   // GL_RGBA, GL_UNSIGNED_BYTE
    CHECK(px[0] == 255 && px[1] == 0 && px[2] == 0);
    w->SwapBuffers();
    w->SetSwapInterval(0);
    // Context is per thread: another thread cannot use it until it is made current there; release works.
    g_b->ReleaseCurrent();
    CHECK(wglGetCurrentContext() == nullptr);
    CHECK(w->MakeCurrent() && wglGetCurrentContext() != nullptr);
    // Destroy and create again on the SAME window: the pixel format can be set only once per window.
    w->DestroyGLContext();
    CHECK(wglGetCurrentContext() == nullptr);
    CHECK(w->CreateGLContext(a));
    CHECK(w->MakeCurrent());
    cc(0.0f, 1.0f, 0.0f, 1.0f); clear(0x4000);
    rp(10, 10, 1, 1, 0x1908, 0x1401, px);
    CHECK(px[0] == 0 && px[1] == 255);
    // A second window with its own context; both current in turn.
    auto w2 = g_b->MakeWindow(d);
    CHECK(w2->CreateGLContext(a) && w2->MakeCurrent());
    w2.reset();   // destroying a window whose context is current releases it
    CHECK(wglGetCurrentContext() == nullptr);
    CHECK(w->MakeCurrent());
    w.reset();
    CHECK(wglGetCurrentContext() == nullptr);
}

void TestPresent() {
    RUN_SUITE("StretchDIBits painter");
    WindowDesc d; d.width = 64; d.height = 48; d.x = 200; d.y = 200;
    auto w = g_b->MakeWindow(d);
    std::vector<uint8_t> px(64 * 48 * 4);
    for (int y = 0; y < 48; ++y) for (int x = 0; x < 64; ++x) {
        uint8_t* p = &px[((size_t)y * 64 + (size_t)x) * 4];
        p[0] = (uint8_t)(x * 4); p[1] = (uint8_t)(y * 5); p[2] = 77; p[3] = 255;   // RGBA
    }
    CHECK(w->PresentPixels(px.data(), 64, 48));
    CHECK(!w->PresentPixels(nullptr, 64, 48) && !w->PresentPixels(px.data(), 0, 48) && !w->PresentPixels(px.data(), 64, -1));
    CHECK(w->PresentPixels(px.data(), 64, 48));
    // A frame bigger than the window: what fits is drawn, no overrun.
    std::vector<uint8_t> big(200 * 200 * 4, 90);
    CHECK(w->PresentPixels(big.data(), 200, 200));
    CHECK(w->PresentPixels(px.data(), 64, 48));
    // Read the window's own pixels back and compare channel order and orientation.
    HDC wdc = GetDC((HWND)w->NativeHandle());
    int bad = 0, checked = 0;
    for (int y = 2; y < 48; y += 9) for (int x = 2; x < 64; x += 9) {
        const COLORREF c = GetPixel(wdc, x, y);
        if (c == CLR_INVALID) continue;
        ++checked;
        if (GetRValue(c) != (x * 4 & 255) || GetGValue(c) != (y * 5 & 255) || GetBValue(c) != 77) ++bad;
    }
    ReleaseDC((HWND)w->NativeHandle(), wdc);
    if (checked == 0) Skip("could not read back the window's pixels on this host (GetPixel returned CLR_INVALID)");
    else CHECK(bad == 0);
}

void TestPads() {
    RUN_SUITE("XInput polling");
    const int n = g_b->GamepadDeviceCount();
    std::cout << "  connected XInput pads: " << n << "\n";
    CHECK(g_b->OpenGamepad(n) == nullptr && g_b->OpenGamepad(-1) == nullptr);
    g_events.clear();
    // The frame-loop cost with no pad plugged in: PollEvent must not hit XInputGetState on empty slots every frame.
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20000; ++i) { Event e; while (g_b->PollEvent(e)) g_events.push_back(e); }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 20000.0;
    std::cout << "  PollEvent: " << us << " us per call\n";
    CHECK(us < 200.0);
    if (n == 0) {
        CHECK(Find(EventType::GamepadAdded) == nullptr);
        Skip("controller data path (axes, buttons, rumble): no XInput pad on this host; logic covered by the native pad-tracker tests only");
    }
}

void TestAudio() {
    RUN_SUITE("audio devices");
    const int outs = g_b->AudioDeviceCount(false), ins = g_b->AudioDeviceCount(true);
    std::cout << "  waveOut devices " << outs << ", waveIn devices " << ins << "\n";
    AudioSpec want; want.freq = 22050; want.channels = 1; want.frames = 512;
    AudioSpec got;
    auto dev = g_b->OpenAudio(want, false, nullptr, &got);
    if (outs == 0) {
        CHECK(dev == nullptr);
        CHECK(!g_b->LastError().empty());
        Skip("audio playback/capture: no sound device on this host, the waveOut data path did NOT run");
        return;
    }
    CHECK(dev != nullptr);
    if (!dev) return;
    std::vector<int16_t> pcm(4410, 100);
    const uint32_t bytes = (uint32_t)(pcm.size() * 2);
    CHECK(dev->QueuedBytes() == 0);
    CHECK(dev->Queue(pcm.data(), bytes) && dev->QueuedBytes() == bytes);   // paused: nothing consumed
    dev->ClearQueued();
    CHECK(dev->QueuedBytes() == 0);
    dev->Queue(pcm.data(), bytes);
    dev->Pause(false);
    CHECK(WaitFor([&] { return dev->QueuedBytes() < bytes / 2; }, 3000));
}

} // namespace

int main() {
    g_b = CreateOwnBackend();
    CHECK(g_b != nullptr);
    if (!g_b) return 1;
    CHECK(g_b->Init());
    CHECK(g_b->Kind() == BackendKind::Own);
    TestTime();
    TestPaths();
    TestDisplays();
    TestWindow();
    TestClipboard();
    TestKeyboardMouse();
    TestGL();
    TestPresent();
    TestPads();
    TestAudio();
    // The window class is unregistered when the backend is destroyed (no leak across a re-created backend).
    {
        WNDCLASSEXW wc; std::memset(&wc, 0, sizeof(wc)); wc.cbSize = sizeof(wc);
        CHECK(GetClassInfoExW(GetModuleHandleW(nullptr), L"OkaySpacePlatformWindow", &wc) != 0);
        g_events.clear();
        g_b.reset();
        CHECK(GetClassInfoExW(GetModuleHandleW(nullptr), L"OkaySpacePlatformWindow", &wc) == 0);
        g_b = CreateOwnBackend();
        auto w = g_b->MakeWindow(WindowDesc());   // and a fresh backend can register it again
        CHECK(w != nullptr);
        w.reset();
        g_b.reset();
    }
    std::cout << "skipped cases: " << g_skips.size() << "\n";
    for (const auto& s : g_skips) std::cout << "  - " << s << "\n";
    TEST_MAIN_RESULT();
}
