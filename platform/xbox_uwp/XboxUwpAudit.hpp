#pragma once
// A source scanner that finds API uses a UWP app may not make, with a small three-valued preprocessor so code fenced out of the UWP
// build (`#if !defined(OKAY_XBOX_UWP)`, `#if defined(__linux__)`, `#ifndef _WIN32` ...) is not reported. Pure C++17, no file access
// (the tool tools/okay_uwp_audit.cpp reads the files). The forbidden list is written from Microsoft's pages "APIs not supported in
// UWP apps" and "Alternatives to Windows APIs in UWP apps" AS REMEMBERED: UNVERIFIED, each line has the reason and the alternative,
// check it before relying on it. This is a text scan, not a compiler: it proves a named API does not appear in code the UWP build
// compiles, not that the code compiles or runs.
#include <cctype>
#include <string>
#include <vector>

namespace okay::xbox {

struct ForbiddenApi { const char* token; const char* category; const char* alternative; };
inline const std::vector<ForbiddenApi>& ForbiddenApis() {
    static const std::vector<ForbiddenApi> k = {
        {"D3DCompile", "d3dcompiler", "compile shaders at build time (.cso, ShaderBundle.hpp)"},
        {"d3dcompiler.h", "d3dcompiler", "compile shaders at build time"},
        {"d3d12.h", "d3d12", "UWP on Xbox One uses Direct3D 11; leave the D3D12 renderer out"},
        {"ID3D12", "d3d12", "leave the D3D12 renderer out"},
        {"WinHttpOpen", "winhttp", "Windows.Web.Http, or leave HTTP out"},
        {"winhttp.h", "winhttp", "Windows.Web.Http, or leave HTTP out"},
        {"MiniDumpWriteDump", "dbghelp", "no crash dumps from inside a UWP app"},
        {"dbghelp.h", "dbghelp", "no crash dumps from inside a UWP app"},
        {"LoadLibraryA", "LoadLibrary", "LoadPackagedLibrary (only DLLs inside the package)"},
        {"LoadLibraryW", "LoadLibrary", "LoadPackagedLibrary"},
        {"LoadLibraryExA", "LoadLibrary", "LoadPackagedLibrary"},
        {"LoadLibraryExW", "LoadLibrary", "LoadPackagedLibrary"},
        {"SDL.h", "SDL2", "the UWP platform backend (platform/xbox_uwp)"},
        {"SDL_", "SDL2", "the UWP platform backend (platform/xbox_uwp)"},
        {"CreateWindowExW", "win32 window", "CoreWindow"},
        {"CreateWindowExA", "win32 window", "CoreWindow"},
        {"RegisterClassExW", "win32 window", "CoreWindow"},
        {"PeekMessageW", "win32 message loop", "CoreDispatcher.ProcessEvents"},
        {"opengl32", "OpenGL", "Direct3D 11"},
        {"wglCreateContext", "OpenGL", "Direct3D 11"},
        {"GL/gl.h", "OpenGL", "Direct3D 11"},
        {"CreateProcessA", "process", "UWP apps cannot start other programs"},
        {"CreateProcessW", "process", "UWP apps cannot start other programs"},
        {"ShellExecute", "process", "Windows.System.Launcher"},
        {"getenv(\"APPDATA\")", "environment folders", "ApplicationData (XboxPaths.hpp, Paths::SetEnvOverride)"},
        {"getenv(\"LOCALAPPDATA\")", "environment folders", "ApplicationData (XboxPaths.hpp, Paths::SetEnvOverride)"},
        {"getenv(\"USERPROFILE\")", "environment folders", "ApplicationData"},
        {"xinput1_4.dll", "XInput by LoadLibrary", "Windows.Gaming.Input (XboxInput.hpp)"},
        {"X11/Xlib.h", "X11", "not Windows code"},
        // Third-party libraries the owner wants replaced by our own code (docs/platform/xbox_uwp.md): none may be reachable in the UWP build.
        {"sodium.h", "libsodium", "okay::crypto (own, unreviewed: docs/platform/xbox_uwp.md)"},
        {"imgui.h", "Dear ImGui", "the software painter (XboxSoftPainter)"},
        {"assimp/", "Assimp", "our own model readers (OKAY_OWN_*)"},
        {"ufbx.h", "ufbx", "our own FBX reader (okay::fbx)"},
        {"curl/curl.h", "libcurl", "our own HTTP client or Windows.Web.Http"},
        {"openssl/", "OpenSSL", "our own TLS (okay::http) or Windows.Web.Http"},
        {"stb_image.h", "stb", "our own image decoders"},
        {"stb_truetype.h", "stb", "our own TTF rasteriser"},
        {"stb_vorbis.c", "stb", "our own OGG decoder"},
        {"dr_wav.h", "dr_libs", "our own WAV decoder"},
        {"dr_mp3.h", "dr_libs", "our own MP3 decoder"},
        // Found by compiling the engine sources with MinGW and WINAPI_FAMILY=WINAPI_FAMILY_APP (its headers hide desktop-only APIs): the names
        // below were reported "not declared". As remembered from Microsoft's pages; UNVERIFIED.
        {"CheckRemoteDebuggerPresent", "debugger probe", "no remote-debugger check in UWP"},
        {"GetModuleHandleW", "module handles", "GetModuleHandleExW / LoadPackagedLibrary"},
        {"GetModuleHandleA", "module handles", "GetModuleHandleExW"},
        {"GetWindowsDirectoryW", "system folders", "no Windows folder in a UWP app"},
        {"CreateToolhelp32Snapshot", "process snapshots", "not available to UWP"},
        {"EnumProcessModules", "process modules", "not available to UWP"},
        {"_getpid", "process id", "GetCurrentProcessId"},
    };
    return k;
}

struct AuditFinding { int line = 0; std::string token, category, alternative; };

namespace audit_detail {
enum class Tri { F, T, U };
inline Tri Not(Tri a) { return a == Tri::T ? Tri::F : a == Tri::F ? Tri::T : Tri::U; }
inline Tri And(Tri a, Tri b) { if (a == Tri::F || b == Tri::F) return Tri::F; if (a == Tri::T && b == Tri::T) return Tri::T; return Tri::U; }
inline Tri Or(Tri a, Tri b) { if (a == Tri::T || b == Tri::T) return Tri::T; if (a == Tri::F && b == Tri::F) return Tri::F; return Tri::U; }
/// What the UWP build (MSVC, x64) defines. Unknown macros are U (the block is scanned, the safe direction).
inline Tri MacroDefined(const std::string& m) {
    static const char* yes[] = {"OKAY_XBOX_UWP", "_WIN32", "_WIN64", "WIN32", "_MSC_VER", "_M_X64", "__cplusplus"};
    // OKAY_HTTP_WINHTTP is defined by NativeHttp.hpp itself only when OKAY_XBOX_UWP is not (so it is off here). OKAY_NET_CRYPTO and OKAY_OWN_CRYPTO are off in the DEFAULT UWP configuration (NetCrypto.hpp then never includes sodium.h; the own-crypto experiment includes okay/Crypto/Crypto.hpp, never sodium.h).
    static const char* no[] = {"__linux__", "__APPLE__", "__EMSCRIPTEN__", "__ANDROID__", "__MINGW32__", "__MINGW64__", "__GNUC__", "__clang__", "__unix__", "_POSIX_VERSION", "OKAY_HTTP_WINHTTP", "OKAY_HAVE_SODIUM", "OKAY_HAVE_ASSIMP", "OKAY_NET_CRYPTO_LIBSODIUM", "OKAY_NET_CRYPTO", "OKAY_OWN_CRYPTO"};
    for (const char* y : yes) if (m == y) return Tri::T;
    for (const char* n : no) if (m == n) return Tri::F;
    return Tri::U;
}
struct CondParser {
    const std::string& s; std::size_t i = 0;
    explicit CondParser(const std::string& str) : s(str) {}
    void Ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i; }
    std::string Ident() { Ws(); std::size_t b = i; while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) ++i; return s.substr(b, i - b); }
    Tri Primary() {
        Ws();
        if (i < s.size() && s[i] == '!') { ++i; return Not(Primary()); }
        if (i < s.size() && s[i] == '(') { ++i; Tri v = Or_(); Ws(); if (i < s.size() && s[i] == ')') ++i; return v; }
        std::string id = Ident();
        if (id == "defined") { Ws(); bool paren = i < s.size() && s[i] == '('; if (paren) ++i; std::string m = Ident(); Ws(); if (paren && i < s.size() && s[i] == ')') ++i; return MacroDefined(m); }
        if (id.empty()) { ++i; return Tri::U; }                          // a number or an operator we do not model
        if (id == "0") return Tri::F;
        if (id == "1") return Tri::T;
        const Tri d = MacroDefined(id);                                    // `#if _WIN32` style: defined-ness stands in for the value
        return d == Tri::F ? Tri::F : Tri::U;
    }
    Tri And_() { Tri v = Primary(); for (;;) { Ws(); if (s.compare(i, 2, "&&") == 0) { i += 2; v = And(v, Primary()); } else return v; } }
    Tri Or_() { Tri v = And_(); for (;;) { Ws(); if (s.compare(i, 2, "||") == 0) { i += 2; v = Or(v, And_()); } else return v; } }
};
inline Tri EvalCondition(const std::string& c) {
    // consumes the `#if` operand; anything left over we do not model (==, <, + ...) makes it unknown
    CondParser p(c); Tri v = p.Or_(); p.Ws();
    if (p.i < c.size() && c.compare(p.i, 2, "//") != 0 && c.compare(p.i, 2, "/*") != 0) return Tri::U;
    return v;
}
inline std::string StripComments(const std::string& src) {   // keeps newlines so line numbers survive; strings are kept (tokens like getenv("APPDATA") live in them)
    std::string o; o.reserve(src.size());
    bool lineC = false, blockC = false, str = false; char q = 0;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const char c = src[i], n = i + 1 < src.size() ? src[i + 1] : 0;
        if (lineC) { if (c == '\n') { lineC = false; o += c; } continue; }
        if (blockC) { if (c == '*' && n == '/') { blockC = false; ++i; } else if (c == '\n') o += c; continue; }
        if (str) { o += c; if (c == '\\' && n) { o += n; ++i; } else if (c == q) str = false; continue; }
        if (c == '/' && n == '/') { lineC = true; ++i; continue; }
        if (c == '/' && n == '*') { blockC = true; ++i; continue; }
        if (c == '"' || c == '\'') { str = true; q = c; }
        o += c;
    }
    return o;
}
}

/// Scan one file's text. `#include` lines are checked as written. Returns every forbidden token in code the UWP build would compile.
inline std::vector<AuditFinding> ScanSource(const std::string& source, std::vector<std::string>* includes = nullptr) {
    using namespace audit_detail;
    const std::string src = StripComments(source);
    std::vector<AuditFinding> out;
    struct Frame { Tri parent; Tri cond; bool taken; };   // `taken`: an earlier branch was certainly true
    std::vector<Frame> st;
    auto active = [&]() { return st.empty() || (st.back().parent != Tri::F && st.back().cond != Tri::F); };
    int line = 0; std::size_t pos = 0;
    while (pos <= src.size()) {
        std::size_t e = src.find('\n', pos); if (e == std::string::npos) e = src.size();
        std::string l = src.substr(pos, e - pos); pos = e + 1; ++line;
        std::size_t b = 0; while (b < l.size() && (l[b] == ' ' || l[b] == '\t')) ++b;
        if (b < l.size() && l[b] == '#') {
            std::size_t k = b + 1; while (k < l.size() && (l[k] == ' ' || l[k] == '\t')) ++k;
            std::size_t d = k; while (d < l.size() && std::isalpha((unsigned char)l[d])) ++d;
            const std::string dir = l.substr(k, d - k), rest = l.substr(d);
            auto effParent = [&]() { return st.empty() ? Tri::T : And(st.back().parent, st.back().cond); };
            if (dir == "if") st.push_back({effParent(), EvalCondition(rest), false});
            else if (dir == "ifdef") { CondParser p(rest); st.push_back({effParent(), MacroDefined(p.Ident()), false}); }
            else if (dir == "ifndef") { CondParser p(rest); st.push_back({effParent(), Not(MacroDefined(p.Ident())), false}); }
            else if (dir == "elif" && !st.empty()) { Frame& f = st.back(); f.taken = f.taken || f.cond == Tri::T; f.cond = f.taken ? Tri::F : EvalCondition(rest); }
            else if (dir == "else" && !st.empty()) { Frame& f = st.back(); f.taken = f.taken || f.cond == Tri::T; f.cond = f.taken ? Tri::F : (f.cond == Tri::F ? Tri::T : Tri::U); }
            else if (dir == "endif" && !st.empty()) st.pop_back();
            if (dir != "include") continue;   // other directives (define, pragma ...) are not scanned
            if (includes && active()) {       // the include closure the UWP build reads: "x.hpp" and <x.hpp> as written
                const std::size_t a = rest.find_first_of("\"<"), z = a == std::string::npos ? a : rest.find_first_of("\">", a + 1);
                if (a != std::string::npos && z != std::string::npos) includes->push_back(rest.substr(a + 1, z - a - 1));
            }
        }
        if (!active()) continue;
        for (const ForbiddenApi& f : ForbiddenApis()) {
            const std::string tok = f.token; std::size_t at = l.find(tok);
            while (at != std::string::npos) {
                const bool wordStart = at == 0 || !(std::isalnum((unsigned char)l[at - 1]) || l[at - 1] == '_');
                const bool endsInUnderscore = tok.back() == '_' || tok.back() == '.' || tok.back() == ')' || tok.back() == '/';
                const std::size_t after = at + tok.size();
                const bool wordEnd = endsInUnderscore || after >= l.size() || !(std::isalnum((unsigned char)l[after]) || l[after] == '_');
                if (wordStart && wordEnd) { out.push_back({line, f.token, f.category, f.alternative}); break; }
                at = l.find(tok, at + 1);
            }
        }
    }
    return out;
}

/// The .cpp / .c files the engine's add_library(okay_engine ...) lists (paths as written, e.g. "src/Core/Paths.cpp"), or another
/// library's in the same file (`library`: "okay_anticheat", which okay_engine links). Pure text parse.
inline std::vector<std::string> ParseEngineSourceList(const std::string& cmake, const std::string& library = "okay_engine") {
    std::vector<std::string> out;
    const std::size_t at = cmake.find("add_library(" + library);
    if (at == std::string::npos) return out;
    std::size_t pos = cmake.find('\n', at); if (pos == std::string::npos) return out;
    ++pos;
    while (pos < cmake.size()) {
        std::size_t e = cmake.find('\n', pos); if (e == std::string::npos) e = cmake.size();
        std::string l = cmake.substr(pos, e - pos); pos = e + 1;
        const std::size_t hash = l.find('#'); if (hash != std::string::npos) l.resize(hash);
        bool close = false;
        std::size_t p = 0; while (p < l.size()) {
            while (p < l.size() && (l[p] == ' ' || l[p] == '\t' || l[p] == '\r')) ++p;
            if (p >= l.size()) break;
            if (l[p] == ')') { close = true; break; }
            std::size_t q = p; while (q < l.size() && l[q] != ' ' && l[q] != '\t' && l[q] != '\r' && l[q] != ')') ++q;
            const std::string w = l.substr(p, q - p);
            if ((w.size() > 4 && w.compare(w.size() - 4, 4, ".cpp") == 0) || (w.size() > 2 && w.compare(w.size() - 2, 2, ".c") == 0)) out.push_back(w);
            p = q;
        }
        if (close) break;
    }
    return out;
}

} // namespace okay::xbox
