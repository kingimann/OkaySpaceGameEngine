#pragma once
// The own-stack dogfood switch and the startup line (docs/own_stack_rollout.md).
//
// Two jobs, both header-only so the player, launcher, editor and the tests all use the same text:
//
//  1. Startup(app): called once at the top of main(). With the environment variable OKAY_OWN_STACK=1 it first sets the
//     run-time switches of every own backend that can be chosen without rebuilding (kRuntimeSettings below), each only
//     when that variable is not already set, so a developer's own OKAY_OWN_FBX=0 still wins. Then it writes ONE line
//     to stderr saying which parts are ours and which are third-party:
//         [okay] stack: platform=sdl2 http=curl readers=ufbx+assimp crypto=libsodium ui=imgui
//  2. Describe(): the same line as a string (the editor's About popup shows it; the tests check its text).
//
// Cryptography is NEVER switched on here (owner rule: it needs an outside review first). It is chosen at build time only
// (-DOKAY_OWN_CRYPTO=ON, or the test-only -DOKAY_OWN_STACK_CRYPTO_TEST=ON), and the line says which one this binary has.
//
// What the line reports is what the code would do now: it asks the real selectors (okay::platform::DefaultKind, the model
// import table, NativeAvailable), it does not repeat the settings. One thing it cannot know at the top of main(): a UI
// backend saved in the settings file of a developer build (OKAY_DEV); the About popup passes the resolved one.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "AntiCheatCrypto.hpp"   // okayac::ed25519::UseSodium(): what Ed25519 really uses at run time
#include "OkayUIBackend.hpp"
#include "okay/Core/ThirdPartySwitch.hpp"
#include "okay/Platform/Platform.hpp"
#include "okay/Render/FbxReader.hpp"
#include "okay/Render/ModelData.hpp"
#if !defined(_WIN32)
#include "okay/Platform/NativeHttp.hpp"
#endif

namespace okay::ownstack {

/// One own reader that can be chosen at run time: the environment variable, the file extension the import table knows it
/// by ("" for FBX, which has its own switch), and the short name used in the startup line.
struct ReaderSwitch { const char* var; const char* ext; const char* label; };

inline const std::vector<ReaderSwitch>& Readers() {
    static const std::vector<ReaderSwitch> r = {
        {"OKAY_OWN_FBX", "", "fbx"},          {"OKAY_OWN_COLLADA", ".dae", "dae"},   {"OKAY_OWN_3DS", ".3ds", "3ds"},
        {"OKAY_OWN_BVH", ".bvh", "bvh"},      {"OKAY_OWN_OFF", ".off", "off"},       {"OKAY_OWN_SMD", ".smd", "smd"},
        {"OKAY_OWN_MD5", ".md5mesh", "md5"},  {"OKAY_OWN_MS3D", ".ms3d", "ms3d"},    {"OKAY_OWN_ASE", ".ase", "ase"},
        {"OKAY_OWN_3MF", ".3mf", "3mf"},      {"OKAY_OWN_AMF", ".amf", "amf"},       {"OKAY_OWN_X3D", ".x3d", "x3d"},
        {"OKAY_OWN_DXF", ".dxf", "dxf"},      {"OKAY_OWN_LWO", ".lwo", "lwo"},       {"OKAY_OWN_BLEND", ".blend", "blend"},
        {"OKAY_OWN_MD2", ".md2", "md2"},      {"OKAY_OWN_MD3", ".md3", "md3"},       {"OKAY_OWN_IRRMESH", ".irrmesh", "irrmesh"},
        {"OKAY_OWN_B3D", ".b3d", "b3d"},      {"OKAY_OWN_X", ".x", "x"},
    };
    return r;
}

struct Setting { const char* var; const char* value; };

/// Everything OKAY_OWN_STACK=1 sets (when not already set): the readers, our HTTP client, the okay::platform backend and OkayUI.
/// HTTP only does anything in a build with -DOKAY_OWN_HTTP=ON on Linux; the platform backend is set on Linux only (the Win32 backend
/// has never run on real Windows); OkayUI only where it is compiled in. Crypto is not here and never will be.
inline std::vector<Setting> RuntimeSettings() {
    std::vector<Setting> s;
    for (const ReaderSwitch& r : Readers()) s.push_back({r.var, "1"});
    s.push_back({"OKAY_OWN_HTTP", "1"});
#if defined(__linux__)
    s.push_back({"OKAY_PLATFORM_BACKEND", "own"});
#endif
    s.push_back({"OKAY_UI_BACKEND", "okayui"});
    return s;
}

/// True when OKAY_OWN_STACK is "1" in the environment.
inline bool Requested() {
    const char* e = std::getenv("OKAY_OWN_STACK");
    return e && e[0] == '1' && e[1] == '\0';
}

inline bool SetIfUnset(const char* name, const char* value) {
    if (std::getenv(name)) return false;
#if defined(_WIN32)
    return _putenv_s(name, value) == 0;
#else
    return setenv(name, value, 0) == 0;
#endif
}

/// Set (or replace) an environment variable for this process and the programs it starts.
inline bool SetEnv(const char* name, const char* value) {
#if defined(_WIN32)
    return _putenv_s(name, value) == 0;
#else
    return setenv(name, value, 1) == 0;
#endif
}

/// With OKAY_OWN_STACK=1, set every run-time switch that is not set yet. Returns how many it set (0 when not requested).
/// Call before anything reads them: the okay::platform default and the FBX choice are decided on first use and then kept.
inline int ApplyEnvironment() {
    if (!Requested()) return 0;
    int n = 0;
    for (const Setting& s : RuntimeSettings()) if (SetIfUnset(s.var, s.value)) ++n;
    return n;
}

// ---- The line -------------------------------------------------------------------------------------------------------

struct Info {
    std::string platform;               // "sdl2" / "own"
    std::string http;                   // "curl" / "own" / "winhttp"
    std::vector<std::string> ownReaders;   // short names of the opt-in formats read by our own reader
    bool ufbx = false;                  // FBX still goes through ufbx
    bool assimp = false;                // Assimp is compiled in (the fallback, and the reader of the formats not listed)
    std::string crypto;                 // "libsodium" / "own(no libsodium)" / "own" / "own(test)"
    std::string ui;                     // "imgui" / "okayui" / "none"
};

/// Pure text: `[okay] stack: platform=own http=curl readers=own(fbx,dae,3ds,bvh)+assimp crypto=libsodium ui=imgui`.
inline std::string FormatLine(const Info& i) {
    std::string readers;
    auto add = [&](const std::string& part) { if (!readers.empty()) readers += '+'; readers += part; };
    if (!i.ownReaders.empty()) {
        std::string l = "own(";
        for (std::size_t k = 0; k < i.ownReaders.size(); ++k) { if (k) l += ','; l += i.ownReaders[k]; }
        add(l + ")");
    }
    if (i.ufbx) add("ufbx");
    if (i.assimp) add("assimp");
    if (readers.empty()) readers = "none";
    return "[okay] stack: platform=" + i.platform + " http=" + i.http + " readers=" + readers + " crypto=" + i.crypto + " ui=" + i.ui;
}

/// The `crypto=` word of the startup line, honest about what Ed25519 (updates, anti-cheat manifest, host identity) really uses:
///   libsodium            libsodium is built in and initialised (the only value a release may show),
///   own(no libsodium)    libsodium is NOT in this build (or failed to start): okay::crypto, our own UNREVIEWED code, is doing it,
///   own(test) / own      -DOKAY_OWN_CRYPTO=ON (with / without -DOKAY_OWN_STACK_CRYPTO_TEST=ON).
/// scripts/check_release_own_stack.sh --binary refuses a release program unless this says exactly `libsodium`.
inline std::string CryptoLabel(bool ownCryptoBuild, bool ownCryptoTest, bool sodiumInUse) {
    if (ownCryptoBuild) return ownCryptoTest ? "own(test)" : "own";
    return sodiumInUse ? "libsodium" : "own(no libsodium)";
}

/// OKAY_PRINT_STACK_AND_EXIT=1 (exactly "1"): Startup() prints the line to STDOUT and exits 0 before the program does anything
/// else. Used by the release guard to ask a built program what it will really do.
inline bool PrintAndExitRequested() {
    const char* v = std::getenv("OKAY_PRINT_STACK_AND_EXIT");
    return v && v[0] == '1' && v[1] == '\0';
}

/// What this process would use right now. `ui` is the UI toolkit the caller resolved ("imgui" / "okayui" / "none");
/// null works it out from OKAY_UI_BACKEND, OKAY_DEV and the build default (not the saved setting).
inline Info Gather(const char* ui = nullptr) {
    Info i;
    namespace pl = okay::platform;
    pl::BackendKind k = pl::DefaultKind();
    if (!pl::IsCompiledIn(k)) k = (k == pl::BackendKind::Own) ? pl::BackendKind::Sdl2 : pl::BackendKind::Own;
    i.platform = pl::BackendName(k);

#if defined(_WIN32)
    i.http = std::getenv("OKAY_HTTP_CURL") ? "curl" : "winhttp";   // not asking NativeAvailable(): that opens the WinHTTP session
#else
    i.http = okay::http::NativeAvailable() ? "own" : "curl";
#endif

    for (const ReaderSwitch& r : Readers()) {
        const bool own = r.ext[0] == '\0' ? okay::fbx::OwnReaderEnabled()
                                          : okay::EffectiveImportBackend(r.ext) == okay::ImportBackend::Own;
        if (own) i.ownReaders.push_back(r.label);
    }
    i.ufbx = !okay::fbx::OwnReaderEnabled();   // ufbx is always compiled in
#ifdef OKAY_HAVE_ASSIMP
    i.assimp = true;
#endif

#if defined(OKAY_OWN_CRYPTO)
    i.crypto = CryptoLabel(true,
#  if defined(OKAY_OWN_STACK_CRYPTO_TEST)
                           true,
#  else
                           false,
#  endif
                           false);
#else
    i.crypto = CryptoLabel(false, false, okayac::ed25519::UseSodium());   // asks the real backend, not just the build's names
#endif

    if (ui) i.ui = ui;
    else i.ui = okayuibackend::Name(okayuibackend::Resolve(std::getenv("OKAY_UI_BACKEND"), std::string(), okayuibackend::DevMode(std::getenv("OKAY_DEV"))));
    return i;
}

inline std::string Describe(const char* ui = nullptr) { return FormatLine(Gather(ui)); }

/// Our own replacements are the default (owner decision, 2026-11-17, "start switching them so i can test them out"). The switches
/// that live in the engine library and are read from the environment get their "own" value here, at the top of the launcher's,
/// the editor's and the player's main(), only when they are not set already and OKAY_THIRD_PARTY=1 is not set: today that is
/// OKAY_PLATFORM_BACKEND (okay::platform's own backend, when it is compiled in). The other switches (renderers, the editors'
/// platform side, WAV, fonts, the code editor, the OkayUI screens) default to our own code where they are read. Tests and tools
/// that do not call this keep the library's build default.
inline int ApplyOwnDefaults() {
    if (okay::ThirdPartyForced()) return 0;
    int n = 0;
    if (okay::platform::IsCompiledIn(okay::platform::BackendKind::Own) && SetIfUnset("OKAY_PLATFORM_BACKEND", "own")) ++n;
    okay::platform::ReconsiderDefault();   // a static initializer may have read the clock (and so the default) before main()
    return n;
}

/// One part of a program and whether our own code or the third-party code runs it now.
struct Part { const char* name; bool own; };

/// The plain start-up line each program writes once its parts are chosen (the launcher to okay_launcher.log, the editor to its
/// Console, all three to stderr), for example:
///   own: platform, renderer, wav, fonts, code editor, okayui screens; third party: libsodium (set OKAY_THIRD_PARTY=1 for all old paths)
/// Cryptography is always third party (libsodium; CLAUDE.md rule 18), so it is always listed there.
inline std::string SummaryLine(const std::vector<Part>& parts) {
    std::string own, third;
    for (const Part& p : parts) {
        std::string& to = p.own ? own : third;
        if (!to.empty()) to += ", ";
        to += p.name;
    }
    if (!third.empty()) third += ", ";
    third += "libsodium";
    std::string line = "own: " + (own.empty() ? std::string("nothing") : own) + "; third party: " + third;
    line += okay::ThirdPartyForced() ? " (OKAY_THIRD_PARTY=1 is set: old paths)" : " (set OKAY_THIRD_PARTY=1 for all old paths)";
    return line;
}

/// The one call each program makes at the top of main(): apply OKAY_OWN_STACK=1 and the own defaults, then log the line to stderr.
/// `ui` as for Gather(); the player has no UI toolkit and passes "none".
inline void Startup(const char* ui = nullptr) {
    ApplyEnvironment();
    ApplyOwnDefaults();
    if (PrintAndExitRequested()) {   // the release guard's question: print and stop, touching nothing else
        std::printf("%s\n", Describe(ui).c_str());
        std::fflush(stdout);
        std::exit(0);
    }
    std::fprintf(stderr, "%s\n", Describe(ui).c_str());
}

} // namespace okay::ownstack
