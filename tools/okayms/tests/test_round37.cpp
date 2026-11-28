// Round 37: a wrong-code issue Round 36 found, temporaries and inline variables, our own Win32 input headers, and the
// next blockers of the Windows engine survey. Each program returns 0 when all of its checks hold, or the number of the
// first check that failed; it is built by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW g++ builds the
// same text with its own headers and must agree (program_check.hpp). Written from ISO/IEC 14882 (the sections are
// named at each test). No code from other compilers or libraries.

#include "program_check.hpp"

#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r37", {}};
const okayms_test::ProgramRun kRunEh{"r37", {}, false, true};  // own heap and exception runtime

// Compiles `source` (with the test's own headers) and expects an error that contains `want`.
void expect_error(Tally& tally, const char* what, const std::string& source, const std::string& want) {
    okayms::CompileInput in;
    in.source = source;
    in.include_paths.push_back("tools/okayms/stdinc");
    in.include_paths.push_back("stdinc");
    const auto compiled = okayms::compile_c(in);
    const bool good = !compiled.ok() && compiled.error.find(want) != std::string::npos;
    CHECK(tally, good);
    if (!good)
        std::fprintf(stderr, "  %s: wanted an error with \"%s\", got %s\n", what, want.c_str(),
                     compiled.ok() ? "no error" : compiled.error.c_str());
}
}  // namespace

static const char* kAlloc = okayms_test::bump_alloc();

// A pointer to one arithmetic type is not converted to a pointer to another ([conv.ptr] 1-3 name the only pointer
// conversions: a null pointer constant, to `cv void*`, and to a base class; [conv.qual] adds cv-qualifiers). Before Round
// 37 okay-cl took `const char*` for a `const wchar_t*` (here `const unsigned short*`) parameter, initializer,
// assignment or return value, and passed the narrow text's address: SaveFile.cpp's `_wfopen(p.c_str(), L"wb")` with a
// narrow path compiled to a call that gives Windows the wrong kind of text.
static void r37_pointer_mismatch(Tally& tally) {
    std::printf("r37_pointer_mismatch\n");
    const std::string want = "no implicit conversion";
    expect_error(tally, "argument", "void take(const wchar_t* p);\nvoid f() { take(\"abc\"); }\n", want);
    expect_error(tally, "argument from a variable",
                 "void take(const unsigned short* p);\nvoid f() { const char* s = \"x\"; take(s); }\n", want);
    expect_error(tally, "initializer", "void f() { const char* s = \"x\"; const wchar_t* w = s; (void)w; }\n", want);
    expect_error(tally, "assignment", "void f() { const char* s = \"x\"; const wchar_t* w = 0; w = s; (void)w; }\n", want);
    expect_error(tally, "return", "const wchar_t* r() { const char* s = \"x\"; return s; }\n", want);
    expect_error(tally, "int and float", "void f() { int a = 1; float* p = &a; (void)p; }\n", want);
    expect_error(tally, "char and unsigned char",
                 "void take(unsigned char* p);\nvoid f() { char b[4]; take(b); }\n", want);
    expect_error(tally, "int and long long", "void f() { long long v = 0; int* p = &v; (void)p; }\n", want);
    expect_error(tally, "member function argument",
                 "struct W { void put(const wchar_t* s) { (void)s; } };\nvoid f() { W w; w.put(\"narrow\"); }\n", want);
}

// The conversions that do exist still compile and give the right values: a null pointer constant, `T*` to `const T*`
// and to `const void*`, a string literal to `const char*`, L"..." to `const wchar_t*`, an explicit cast between pointer
// types ([expr.reinterpret.cast] 7), and the narrow and wide overloads of one name chosen by the argument ([over.match]).
static void r37_pointer_allowed(Tally& tally) {
    std::printf("r37_pointer_allowed\n");
    okayms_test::run_both(kRun, tally, "pointer_allowed", std::string(kAlloc) + R"OKMS(
static int Len(const char* s) { int n = 0; while (s[n]) ++n; return n; }
static int Len(const wchar_t* s) { int n = 0; while (s[n]) ++n; return 100 + n; }
static int Bytes(const void* p, int n) { const unsigned char* b = (const unsigned char*)p; int t = 0; for (int i = 0; i < n; ++i) t += b[i]; return t; }
static const wchar_t* Wide() { return L"wide"; }
int entry(void) {
  char buf[4] = {'a', 'b', 0, 0};
  char* p = buf;
  const char* cp = p;
  const wchar_t* w = L"xyz";
  const wchar_t* none = 0;
  const char* nul = nullptr;
  CK(1, Len(cp) == 2 && Len(w) == 103 && Len("four") == 4 && Len(L"ab") == 102);
  CK(2, Bytes(p, 2) == 'a' + 'b' && Bytes(w, 2) == 'x');
  CK(3, none == 0 && nul == nullptr && Len(Wide()) == 104);
  const unsigned char* u = reinterpret_cast<const unsigned char*>(cp);
  const unsigned short* s = (const unsigned short*)w;
  CK(4, u[1] == 'b' && s[2] == 'z');
  const void* v = u;
  CK(5, v == cp);
  return 0;
}
)OKMS", 0);
}

// std::filesystem::path on Windows: its value_type is wchar_t and native(), c_str() and wstring() give the UTF-16 text
// ([fs.class.path] 6, [fs.path.native.obs]); a path is made from UTF-8 (u8path) or wide text, and u8string() gives UTF-8
// ([fs.path.type.cvt]). Before Round 37 okay-ms's path was narrow on Windows too, so `_wfopen(p.c_str(), ...)` did not
// see wide text. MinGW's path is wide the same way.
static void r37_wide_path(Tally& tally) {
    std::printf("r37_wide_path\n");
    okayms_test::run_both(kRun, tally, "wide_path", std::string(kAlloc) + R"OKMS(
#include <filesystem>
#include <string>
#include <type_traits>
int entry(void) {
  namespace fs = std::filesystem;
  CK(1, (std::is_same<fs::path::value_type, wchar_t>::value));
  fs::path p = fs::u8path("dir/na\xC3\xAFve.txt");
  const wchar_t* w = p.c_str();
  CK(2, w[0] == L'd' && w[3] == L'/' && w[6] == 0xEF && w[7] == L'v' && w[13] == 0);
  CK(3, p.native().size() == 13 && p.wstring().size() == 13 && p.u8string().size() == 14);
  const wchar_t pair[] = {L'a', 0xD83D, 0xDE00, L'b', 0};
  fs::path q(pair);
  const std::string u = q.u8string();
  CK(4, u.size() == 6 && (unsigned char)u[1] == 0xF0 && (unsigned char)u[2] == 0x9F && (unsigned char)u[3] == 0x98 &&
            (unsigned char)u[4] == 0x80 && u[5] == 'b');
  CK(5, q.c_str()[1] == 0xD83D && q.c_str()[2] == 0xDE00 && q.native().size() == 4);
  fs::path r = fs::u8path("base");
  r /= fs::path(L"file.bin");
  CK(6, r.extension().wstring() == L".bin" && r.filename().native().size() == 8);
  const std::wstring ws(L"C:\\x\\y.txt");
  fs::path s(ws);
  CK(7, s.parent_path().native() == L"C:\\x" && s.stem().u8string() == "y");
  return 0;
}
)OKMS", 0);
}

// A by-value argument the caller made is destroyed when an exception leaves the statement before the call takes it
// ([except.ctor] 1-2: every object of automatic storage duration constructed since the try block was entered is
// destroyed; [expr.call] 7: the callee destroys its parameter, here by the Microsoft x64 convention). f(C(4)) passes the
// temporary itself ([dcl.init] 17.6.1, no copy). Before Round 37, `two(C(4), boom(3))` left C(4) alive (g_live 1) and
// `two(a, boom(3))` left the copy of a alive.
static void r37_by_value_unwind(Tally& tally) {
    std::printf("r37_by_value_unwind\n");
    okayms_test::run_both(kRunEh, tally, "by_value_unwind", R"OKMS(
static int g_live = 0, g_copies = 0;
struct C { int v; C(int x) : v(x) { ++g_live; } C(const C& o) : v(o.v) { ++g_live; ++g_copies; } ~C() { --g_live; } };
struct Big { C c; long long pad[4]; Big(int x) : c(x) {} };
static int boom(int k) { if (k > 0) throw k; return k; }
static int two(C c, int k) { return c.v + k; }
static int two2(int k, C c) { return c.v + k; }
static int big2(Big b, int k) { return b.c.v + k; }
static int thr(C c) { if (c.v > 0) throw c.v; return 0; }
int entry(void) {
  int caught = 0;
  try { two(C(4), boom(3)); } catch (int e) { caught = e; }
  CK(1, caught == 3 && g_live == 0);
  try { thr(C(5)); } catch (int e) { caught = e; }
  CK(2, caught == 5 && g_live == 0);
  try { big2(Big(6), boom(6)); } catch (int e) { caught = e; }
  CK(3, caught == 6 && g_live == 0);
  {
    C a(4);
    try { two(a, boom(7)); } catch (int e) { caught = e; }
    CK(4, caught == 7 && g_live == 1);
    try { two2(boom(8), a); } catch (int e) { caught = e; }
    CK(5, caught == 8 && g_live == 1);
    try { two(7, boom(9)); } catch (int e) { caught = e; }
    CK(6, caught == 9 && g_live == 1);
    const int copies = g_copies;
    int r = two(C(2), 1);
    CK(7, r == 3 && g_live == 1 && g_copies == copies);
    bool no = false;
    r = no && two(a, 2);
    CK(8, r == 0 && g_live == 1);
    for (int i = 0; i < 3; ++i) {
      try { two(C(i), boom(i)); } catch (int e) { caught = e; }
    }
    CK(9, caught == 2 && g_live == 1);
  }
  CK(10, g_live == 0);
  return 0;
}
)OKMS", 0);
}

// Our own <windows.h> window, raw-input and GDI parts, <imm.h>, <xinput.h>, <hidpi.h> and <hidsdi.h> (Round 37, from
// Microsoft's public reference pages): the sizes and member offsets of their structures are the documented ones, the
// same in okay-ms's headers and in MinGW's (the g++ half of the test); and a few user32 functions run under Wine.
static void r37_win32_layouts(Tally& tally) {
    std::printf("r37_win32_layouts\n");
    const okayms_test::ProgramRun run{"r37", {"user32.dll:GetDoubleClickTime", "user32.dll:MapVirtualKeyW"}};
    okayms_test::run_both(run, tally, "win32_layouts", std::string(kAlloc) + R"OKMS(
#include <windows.h>
#include <imm.h>
#include <xinput.h>
#include <hidusage.h>
#include <hidpi.h>
#include <hidsdi.h>
#define OFF(T, m) ((int)((char*)&((T*)0x1000)->m - (char*)0x1000))
int entry(void) {
  CK(1, sizeof(POINT) == 8 && sizeof(RECT) == 16 && sizeof(MSG) == 48 && OFF(MSG, pt) == 36);
  CK(2, sizeof(WNDCLASSEXW) == 80 && OFF(WNDCLASSEXW, lpfnWndProc) == 8 && OFF(WNDCLASSEXW, hIconSm) == 72);
  CK(3, sizeof(CREATESTRUCTW) == 80 && OFF(CREATESTRUCTW, cy) == 32 && OFF(CREATESTRUCTW, dwExStyle) == 72);
  CK(4, sizeof(MINMAXINFO) == 40 && OFF(MINMAXINFO, ptMinTrackSize) == 24 && sizeof(PAINTSTRUCT) == 72);
  CK(5, sizeof(MONITORINFO) == 40 && sizeof(MONITORINFOEXW) == 104 && OFF(MONITORINFOEXW, szDevice) == 40);
  CK(6, sizeof(DEVMODEW) == 220 && OFF(DEVMODEW, dmSize) == 68 && OFF(DEVMODEW, dmFields) == 72 &&
            OFF(DEVMODEW, dmBitsPerPel) == 168 && OFF(DEVMODEW, dmPelsWidth) == 172 && OFF(DEVMODEW, dmDisplayFrequency) == 184);
  CK(7, sizeof(FLASHWINFO) == 32 && sizeof(ICONINFO) == 32 && OFF(ICONINFO, hbmMask) == 16);
  CK(8, sizeof(BITMAPINFOHEADER) == 40 && sizeof(BITMAPINFO) == 44 && sizeof(BITMAPV5HEADER) == 124 &&
            OFF(BITMAPV5HEADER, bV5AlphaMask) == 52 && OFF(BITMAPV5HEADER, bV5Reserved) == 120);
  CK(9, sizeof(PIXELFORMATDESCRIPTOR) == 40 && OFF(PIXELFORMATDESCRIPTOR, cDepthBits) == 23 &&
            OFF(PIXELFORMATDESCRIPTOR, dwLayerMask) == 28);
  CK(10, sizeof(RAWINPUTDEVICE) == 16 && sizeof(RAWINPUTHEADER) == 24 && sizeof(RAWMOUSE) == 24 &&
             sizeof(RAWKEYBOARD) == 16 && OFF(RAWINPUT, data) == 24 && OFF(RAWMOUSE, lLastX) == 12);
  CK(11, sizeof(RID_DEVICE_INFO) == 32 && OFF(RID_DEVICE_INFO, hid.usUsagePage) == 20 && sizeof(RAWINPUTDEVICELIST) == 16);
  CK(12, sizeof(COMPOSITIONFORM) == 28 && sizeof(CANDIDATEFORM) == 32 && OFF(CANDIDATEFORM, rcArea) == 16);
  CK(13, sizeof(XINPUT_GAMEPAD) == 12 && sizeof(XINPUT_STATE) == 16 && OFF(XINPUT_STATE, Gamepad.sThumbRY) == 14);
  CK(14, sizeof(HIDP_CAPS) == 64 && OFF(HIDP_CAPS, NumberInputButtonCaps) == 46 && OFF(HIDP_CAPS, NumberFeatureDataIndices) == 62);
  CK(15, sizeof(HIDP_BUTTON_CAPS) == 72 && OFF(HIDP_BUTTON_CAPS, IsRange) == 12 && OFF(HIDP_BUTTON_CAPS, Range.UsageMin) == 56);
  CK(16, sizeof(HIDP_VALUE_CAPS) == 72 && OFF(HIDP_VALUE_CAPS, BitSize) == 18 && OFF(HIDP_VALUE_CAPS, LogicalMin) == 40 &&
             OFF(HIDP_VALUE_CAPS, NotRange.Usage) == 56);
  CK(17, HIDP_STATUS_SUCCESS == 0x00110000 && HidP_Input == 0 && sizeof(HIDD_ATTRIBUTES) == 12);
  CK(18, WM_INPUT == 0xFF && WM_DPICHANGED == 0x2E0 && WS_OVERLAPPEDWINDOW == 0x00CF0000L && VK_F12 == 0x7B &&
             RIDI_DEVICEINFO == 0x2000000b && GCS_COMPSTR == 8 && XINPUT_GAMEPAD_Y == 0x8000);
  CK(19, GetDoubleClickTime() > 0);
  CK(20, MapVirtualKeyW(VK_ESCAPE, 0) == 1);  // MAPVK_VK_TO_VSC: Escape is scan code 1
  return 0;
}
)OKMS", 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r37_pointer_mismatch(tally);
    r37_pointer_allowed(tally);
    r37_wide_path(tally);
    r37_by_value_unwind(tally);
    r37_win32_layouts(tally);
    return tally.finish(5);
}
