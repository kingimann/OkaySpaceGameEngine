#pragma once
// Anti-cheat: client hardening signals (opt-in; docs/anticheat.md, "Hardening: code and import integrity").
//
// Everything here produces WEIGHTED FINDINGS (okayac::Finding::weight, 1..6), never a
// verdict: a client signal alone is never grounds for a ban, and none of these ever
// ends a game by themselves, even in enforce mode. They ride the existing path:
// RuntimeGuard logs them, the player files them as one `client_check` report
// (AntiCheatBackend.hpp), and the server side decides what a pile of weights means.
//
// What is in here:
//   * inline-hook detection on a short list of critical APIs (decoding the first
//     bytes of the function and asking where a jump lands);
//   * IAT checking of the game's own import table (an import that resolves outside
//     system and game modules);
//   * a signed module blocklist / allowlist data file, updatable without a rebuild;
//   * debugger and hypervisor hints beyond IsDebuggerPresent, including a check
//     that the debugger API and the raw process block agree;
//   * a speed-hack cross-check between QueryPerformanceCounter, GetTickCount64 and
//     (when the network layer feeds it) server time;
//   * the scheduler (`Hardening`) that runs these, with the code-section hash scan of
//     AntiCheatPe.hpp, a little each frame.
//
// The decision logic is plain code, built and unit tested on every platform
// (tests/test_anticheat_hardening.cpp); the probes that read a Windows process are
// compiled out elsewhere. All of it is client side: a determined cheater patches
// the checks, or hooks the functions they call, or hides from a user-mode scan
// altogether. These raise the cost of off-the-shelf tools; they do not prove anyone
// innocent, and the server stays authoritative.
#include <cstring>
#include <memory>

#include "AntiCheatPe.hpp"
#include "AntiCheatProtected.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#endif
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#define OKAY_AC_HAVE_CPUID_H 1
#endif

namespace okayac {

constexpr int kMaxHardeningWeight = 6;   // nothing here is ever worth more: the server decides

inline std::string FormatFinding(const Finding& f) {
    return f.check + (f.weight > 0 ? "(w" + std::to_string(f.weight) + ")" : std::string()) + ": " + f.detail;
}

// ---- Where an address lands -------------------------------------------------------
struct ModuleRange {
    std::string path;                       // as the system reports it
    std::uint64_t base = 0, size = 0;
    bool trusted = false;                   // Windows' own, the game's own, or allowlisted
};
struct TargetClass {
    enum Kind { Trusted, Untrusted, Private } kind = Private;   // Private: in no module at all (a trampoline, shellcode)
    std::string module;
};
inline TargetClass ClassifyTarget(std::uint64_t addr, const std::vector<ModuleRange>& mods) {
    for (const ModuleRange& m : mods)
        if (addr >= m.base && addr - m.base < m.size) {
            std::string n = NormalizeModulePath(m.path);
            const std::size_t s = n.find_last_of('/');
            return {m.trusted ? TargetClass::Trusted : TargetClass::Untrusted, s == std::string::npos ? n : n.substr(s + 1)};
        }
    return {TargetClass::Private, ""};
}

// ---- Inline hooks ------------------------------------------------------------------
enum class BranchKind { None, JmpRel, JmpIndirect, MovJmp, PushRet, Int3 };
struct EntryBranch {
    BranchKind kind = BranchKind::None;
    bool hasTarget = false;
    std::uint64_t target = 0;
};
using ReadMem = std::function<bool(std::uint64_t addr, void* out, std::size_t n)>;

// What does the first instruction of a function do? Only the shapes a hook library writes
// over a function entry are recognised (a relative or indirect jump, a mov/jmp or push/ret
// pair, a breakpoint); anything else is "not a hook". Genuinely forwarding stubs (a system
// DLL that jumps into another system DLL) decode as jumps too, which is why the CALLER
// judges where the jump lands rather than that it jumps.
inline EntryBranch DecodeEntry(const std::uint8_t* b, std::size_t n, std::uint64_t addr, bool x64, const ReadMem& rd) {
    EntryBranch r;
    if (n < 1) return r;
    auto i32 = [&](std::size_t o) { std::int32_t v; std::memcpy(&v, b + o, 4); return v; };
    auto u32 = [&](std::size_t o) { std::uint32_t v; std::memcpy(&v, b + o, 4); return v; };
    auto u64 = [&](std::size_t o) { std::uint64_t v; std::memcpy(&v, b + o, 8); return v; };
    if (b[0] == 0xCC) { r.kind = BranchKind::Int3; return r; }
    if (b[0] == 0xE9 && n >= 5) { r.kind = BranchKind::JmpRel; r.hasTarget = true; r.target = addr + 5 + (std::int64_t)i32(1); return r; }
    if (b[0] == 0xEB && n >= 2) { r.kind = BranchKind::JmpRel; r.hasTarget = true; r.target = addr + 2 + (std::int64_t)(std::int8_t)b[1]; return r; }
    if (b[0] == 0xFF && n >= 6 && b[1] == 0x25) {   // jmp [rip+disp32] / jmp [abs32]
        r.kind = BranchKind::JmpIndirect;
        const std::uint64_t slot = x64 ? addr + 6 + (std::int64_t)i32(2) : (std::uint64_t)u32(2);
        std::uint64_t t = 0;
        if (rd && rd(slot, &t, x64 ? 8 : 4)) {
            if (!x64) t &= 0xFFFFFFFFull;
            r.hasTarget = true;
            r.target = t;
        }
        return r;
    }
    if (x64 && n >= 12 && b[0] == 0x48 && b[1] == 0xB8) {   // mov rax, imm64 ; jmp rax | push rax; ret
        if ((n >= 14 && b[10] == 0xFF && b[11] == 0xE0) || (n >= 12 && b[10] == 0x50 && b[11] == 0xC3)) {
            r.kind = BranchKind::MovJmp; r.hasTarget = true; r.target = u64(2); return r;
        }
    }
    if (x64 && n >= 13 && b[0] == 0x49 && b[1] == 0xBB && b[10] == 0x41 && b[11] == 0xFF && b[12] == 0xE3) {   // mov r11, imm64 ; jmp r11
        r.kind = BranchKind::MovJmp; r.hasTarget = true; r.target = u64(2); return r;
    }
    if (!x64 && n >= 7 && b[0] == 0xB8 && b[5] == 0xFF && b[6] == 0xE0) {   // mov eax, imm32 ; jmp eax
        r.kind = BranchKind::MovJmp; r.hasTarget = true; r.target = u32(1); return r;
    }
    if (n >= 6 && b[0] == 0x68 && b[5] == 0xC3) {   // push imm32 ; ret
        r.kind = BranchKind::PushRet; r.hasTarget = true;
        r.target = x64 ? (std::uint64_t)(std::int64_t)i32(1) : (std::uint64_t)u32(1);
        return r;
    }
    return r;
}

struct ApiEntry {
    std::string name;                   // "kernel32!QueryPerformanceCounter"
    std::uint64_t addr = 0;
    std::vector<std::uint8_t> bytes;    // the first bytes of the function (up to 16)
};

// One function entry: nothing, or a finding. A jump that lands in a trusted module (Windows'
// own forwarding stubs do that all the time) is fine; one that lands in an untrusted module or
// in memory that belongs to no module is the shape of a hook. A hot-patch hop (a short jump
// back into the padding before the function) is followed once through `rd`.
inline bool CheckApiEntry(const ApiEntry& e, bool x64, const std::vector<ModuleRange>& mods, const ReadMem& rd, Finding& out) {
    EntryBranch br = DecodeEntry(e.bytes.data(), e.bytes.size(), e.addr, x64, rd);
    if (br.kind == BranchKind::None) return false;
    if (br.kind == BranchKind::Int3) {
        out = {"hook", e.name + " starts with a breakpoint (int3)", 3};
        return true;
    }
    if (br.kind == BranchKind::JmpRel && br.hasTarget && br.target < e.addr && e.addr - br.target <= 16 && rd) {   // hot-patch landing pad
        std::uint8_t pad[16] = {};
        if (rd(br.target, pad, sizeof(pad))) {
            const EntryBranch second = DecodeEntry(pad, sizeof(pad), br.target, x64, rd);
            if (second.hasTarget) br = second;
        }
    }
    if (!br.hasTarget) {   // an indirect jump we couldn't read through: say nothing rather than guess
        return false;
    }
    const TargetClass t = ClassifyTarget(br.target, mods);
    if (t.kind == TargetClass::Trusted) return false;
    char hex[32];
    std::snprintf(hex, sizeof(hex), "0x%llx", (unsigned long long)br.target);
    out = {"hook", e.name + " jumps to " + (t.kind == TargetClass::Untrusted ? "untrusted module " + t.module : std::string("memory outside any module (") + hex + ")"),
           5};
    return true;
}
inline std::vector<Finding> CheckApiEntries(const std::vector<ApiEntry>& apis, bool x64, const std::vector<ModuleRange>& mods, const ReadMem& rd) {
    std::vector<Finding> out;
    for (const ApiEntry& a : apis) {
        Finding f;
        if (CheckApiEntry(a, x64, mods, rd, f)) out.push_back(f);
    }
    return out;
}

// The APIs worth watching: the clocks a speed-hack bends, the debugger probes a hider bends,
// and the loader / protection calls an injector uses.
struct CriticalApi { const char* module; const char* name; };
inline const std::vector<CriticalApi>& CriticalApis() {
    static const std::vector<CriticalApi> v = {
        {"kernel32.dll", "QueryPerformanceCounter"}, {"kernel32.dll", "QueryPerformanceFrequency"},
        {"kernel32.dll", "GetTickCount"},            {"kernel32.dll", "GetTickCount64"},
        {"kernel32.dll", "IsDebuggerPresent"},       {"kernel32.dll", "CheckRemoteDebuggerPresent"},
        {"kernel32.dll", "LoadLibraryW"},            {"kernel32.dll", "LoadLibraryExW"},
        {"kernel32.dll", "VirtualProtect"},          {"ntdll.dll", "NtQueryInformationProcess"},
        {"ntdll.dll", "NtProtectVirtualMemory"},     {"ntdll.dll", "LdrLoadDll"},
        {"winmm.dll", "timeGetTime"},                {"user32.dll", "GetAsyncKeyState"},
    };
    return v;
}

// ---- Imports -------------------------------------------------------------------------
// The game's own import address table, as the loader filled it in: every entry should point
// into a system module, one of the game's own, or an allowlisted one. An entry that points
// anywhere else was redirected after the load (an IAT hook).
inline std::vector<Finding> CheckImports(const std::uint8_t* mapped, std::size_t size, const std::vector<ModuleRange>& mods,
                                         std::size_t maxEntries = 20000) {
    std::vector<Finding> out;
    pe::Image im;
    if (!pe::Parse(mapped, size, im) || im.importRva == 0) return out;
    const std::size_t ptr = im.is64 ? 8 : 4;
    std::size_t checked = 0;
    for (std::uint32_t d = im.importRva; checked < maxEntries; d += 20) {
        std::uint64_t oft = 0, name = 0, ft = 0;
        if (!pe::detail::Rd(mapped, size, d, 4, oft) || !pe::detail::Rd(mapped, size, d + 12, 4, name) ||
            !pe::detail::Rd(mapped, size, d + 16, 4, ft))
            break;
        if (name == 0 && ft == 0) break;
        std::string dll;
        for (std::size_t i = 0; i < 128 && name + i < size && mapped[name + i]; ++i) dll += (char)mapped[name + i];
        const std::uint64_t lookup = oft ? oft : ft;
        for (std::uint32_t i = 0; checked < maxEntries; ++i, ++checked) {
            std::uint64_t addr = 0, orig = 0;
            if (!pe::detail::Rd(mapped, size, ft + (std::size_t)i * ptr, ptr, addr) || addr == 0) break;
            std::string fn = "?";
            if (pe::detail::Rd(mapped, size, lookup + (std::size_t)i * ptr, ptr, orig) && orig != 0 &&
                !(orig >> (ptr * 8 - 1)) && orig + 2 < size) {   // by name (not by ordinal)
                fn.clear();
                for (std::size_t k = 0; k < 128 && orig + 2 + k < size && mapped[orig + 2 + k]; ++k) fn += (char)mapped[orig + 2 + k];
            }
            const TargetClass t = ClassifyTarget(addr, mods);
            if (t.kind == TargetClass::Trusted) continue;
            char hex[32];
            std::snprintf(hex, sizeof(hex), "0x%llx", (unsigned long long)addr);
            out.push_back({"iat", "import " + dll + "!" + fn + " resolves to " +
                                      (t.kind == TargetClass::Untrusted ? "untrusted module " + t.module : std::string("memory outside any module (") + hex + ")"),
                           4});
        }
    }
    return out;
}

// ---- Module rules (a signed data file) -------------------------------------------------
// {"format": "okayspace-modules/1", "version": 3,
//  "block": [{"name": "*cheat*.dll", "reason": "memory tool", "weight": 4}, {"signer": "Some Cheat Vendor"}],
//  "allow": [{"name": "gameoverlayrenderer64.dll"}, {"signer": "NVIDIA Corporation"}],
//  "signature": {"alg": "ed25519", "key": "<public key hex>", "sig": "<hex>"}}
// A rule has a file-name pattern (* and ? wildcards, case-insensitive), a substring of the
// code-signing subject, or both (both must match). The block list wins over the allow list.
// The owner edits this file and signs it with the same key as the manifest
// (tools: `okay_ac_tool sign-rules`); it lives outside the manifest's files
// (.anticheat/modules.json) so it can change without a rebuild. A higher "version" replaces a lower.
constexpr const char* kModuleRulesFormat = "okayspace-modules/1";

struct ModuleRule {
    std::string name, signer, reason;
    int weight = 4;
};
struct ModuleRules {
    long version = 0;
    std::vector<ModuleRule> block, allow;
    bool hasSignature = false;
    ed25519::PublicKey key{};
    ed25519::Signature sig{};
};

inline bool GlobMatch(const std::string& pattern, const std::string& text) {   // case-insensitive, * and ?
    const std::string p = og::Lower(pattern), t = og::Lower(text);
    std::size_t pi = 0, ti = 0, star = std::string::npos, mark = 0;
    while (ti < t.size()) {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti])) { ++pi; ++ti; }
        else if (pi < p.size() && p[pi] == '*') { star = pi++; mark = ti; }
        else if (star != std::string::npos) { pi = star + 1; ti = ++mark; }
        else return false;
    }
    while (pi < p.size() && p[pi] == '*') ++pi;
    return pi == p.size();
}

// The signer a module is known to have (Windows fills it in; a test passes it directly).
struct ModuleSigner {
    bool checked = false;    // a signature check happened
    bool valid = false;      // Windows says the signature chains to a trusted root
    std::string subject;     // the signing certificate's name ("" if unsigned / unknown)
};

inline bool RuleMatches(const ModuleRule& r, const std::string& fileName, const ModuleSigner& s) {
    if (r.name.empty() && r.signer.empty()) return false;
    if (!r.name.empty() && !GlobMatch(r.name, fileName)) return false;
    if (!r.signer.empty() && (s.subject.empty() || og::Lower(s.subject).find(og::Lower(r.signer)) == std::string::npos)) return false;
    return true;
}
// A signer rule only counts when the signature is valid, so a self-made certificate carrying a
// trusted company's name doesn't allow-list anything.
inline bool RuleMatchesTrusted(const ModuleRule& r, const std::string& fileName, const ModuleSigner& s) {
    if (!r.signer.empty() && !s.valid) return false;
    return RuleMatches(r, fileName, s);
}

inline std::string FileNameOf(const std::string& path) {
    const std::string n = NormalizeModulePath(path);
    const std::size_t s = n.find_last_of('/');
    return s == std::string::npos ? n : n.substr(s + 1);
}
inline const ModuleRule* BlockedBy(const ModuleRules& r, const std::string& path, const ModuleSigner& s) {
    const std::string n = FileNameOf(path);
    for (const ModuleRule& b : r.block)
        if (RuleMatches(b, n, s)) return &b;   // a block by signer doesn't need validity: a revoked-but-signed cheat is still that cheat
    return nullptr;
}
inline bool AllowedBy(const ModuleRules& r, const std::string& path, const ModuleSigner& s) {
    const std::string n = FileNameOf(path);
    for (const ModuleRule& a : r.allow)
        if (RuleMatchesTrusted(a, n, s)) return true;
    return false;
}

inline std::string SignedPayload(const ModuleRules& m) {
    using og::json::Escape;
    std::ostringstream o;
    auto list = [&](const char* key, const std::vector<ModuleRule>& v, bool weights) {
        o << "  \"" << key << "\": [";
        for (std::size_t i = 0; i < v.size(); ++i) {
            o << (i ? ",\n    " : "\n    ") << "{";
            bool first = true;
            auto field = [&](const char* k, const std::string& val) {
                if (val.empty()) return;
                o << (first ? "" : ", ") << "\"" << k << "\": \"" << Escape(val) << "\"";
                first = false;
            };
            field("name", v[i].name);
            field("signer", v[i].signer);
            field("reason", v[i].reason);
            if (weights) { o << (first ? "" : ", ") << "\"weight\": " << v[i].weight; }
            o << "}";
        }
        o << (v.empty() ? "]" : "\n  ]");
    };
    o << "{\n  \"format\": \"" << kModuleRulesFormat << "\",\n  \"version\": " << m.version << ",\n";
    list("block", m.block, true);
    o << ",\n";
    list("allow", m.allow, false);
    return o.str();
}
inline std::string ToJson(const ModuleRules& m) {
    std::string s = SignedPayload(m);
    if (m.hasSignature)
        s += ",\n  \"signature\": {\"alg\": \"ed25519\", \"key\": \"" + ToHex(m.key.data(), m.key.size()) + "\", \"sig\": \"" +
             ToHex(m.sig.data(), m.sig.size()) + "\"}";
    return s + "\n}\n";
}
inline bool CheckSignature(const ModuleRules& m) { return m.hasSignature && ed25519::Verify(SignedPayload(m), m.sig, m.key); }
inline void Sign(ModuleRules& m, const ed25519::Seed& seed) {
    m.key = ed25519::PublicKeyFromSeed(seed);
    m.sig = ed25519::Sign(SignedPayload(m), seed);
    m.hasSignature = true;
}

inline bool ParseModuleRules(const std::string& text, ModuleRules& out, std::string* err = nullptr) {
    auto fail = [&](const std::string& w) { if (err) *err = w; return false; };
    if (text.size() > 1u << 20) return fail("rules file too large");
    og::json::Value root;
    if (!og::json::Reader(text).Parse(root) || root.kind != og::json::Value::Object) return fail("rules aren't valid JSON");
    const og::json::Value* f = root.get("format");
    if (!f || f->kind != og::json::Value::String || f->str != kModuleRulesFormat) return fail(std::string("not a module rules file (") + kModuleRulesFormat + ")");
    ModuleRules m;
    std::uint64_t ver = 0;
    if (!og::ParseSize(root.get("version"), ver) || ver > 2000000000ull) return fail("rules have no version");
    m.version = (long)ver;
    auto rules = [&](const char* key, std::vector<ModuleRule>& dst, bool weights) {
        const og::json::Value* a = root.get(key);
        if (!a) return true;
        if (a->kind != og::json::Value::Array || a->arr.size() > 2000) return false;
        for (const og::json::Value& v : a->arr) {
            if (v.kind != og::json::Value::Object) return false;
            ModuleRule r;
            auto s = [&](const char* k, std::string& d, std::size_t cap) {
                const og::json::Value* x = v.get(k);
                if (!x) return true;
                if (x->kind != og::json::Value::String || x->str.size() > cap) return false;
                d = x->str;
                return true;
            };
            if (!s("name", r.name, 200) || !s("signer", r.signer, 200) || !s("reason", r.reason, 300)) return false;
            if (r.name.find_first_of("/\\") != std::string::npos) return false;   // file names, not paths
            if (r.name.empty() && r.signer.empty()) return false;                 // a rule that matches nothing is a mistake
            if (const og::json::Value* w = v.get("weight")) {
                if (!weights) return false;   // weights belong to block rules (an allow rule has none, and the signature covers what's written)
                std::uint64_t wv = 0;
                if (!og::ParseSize(w, wv) || wv < 1 || wv > (std::uint64_t)kMaxHardeningWeight) return false;
                r.weight = (int)wv;
            }
            dst.push_back(std::move(r));
        }
        return true;
    };
    if (!rules("block", m.block, true) || !rules("allow", m.allow, false)) return fail("bad rule entry");
    if (const og::json::Value* sig = root.get("signature")) {
        if (sig->kind != og::json::Value::Object) return fail("bad signature");
        const og::json::Value* alg = sig->get("alg");
        if (!alg || alg->kind != og::json::Value::String || alg->str != "ed25519" || !detail::HexArray(sig->get("key"), m.key) ||
            !detail::HexArray(sig->get("sig"), m.sig))
            return fail("bad signature");
        m.hasSignature = true;
    }
    out = std::move(m);
    return true;
}

// Which of several candidate rule texts to use: the valid ones (parse; signature trusted where
// required), the highest version wins. `minVersion` refuses anything older than a version
// already seen. Reasons for each rejected candidate go in `notes`.
inline bool PickModuleRules(const std::vector<std::string>& texts, const TrustedKeySet& trusted, long minVersion, ModuleRules& out,
                            std::vector<std::string>* notes = nullptr) {
    bool have = false;
    for (const std::string& t : texts) {
        ModuleRules m;
        std::string why;
        if (!ParseModuleRules(t, m, &why)) { if (notes) notes->push_back("module rules ignored: " + why); continue; }
        if (trusted.required) {
            bool ok = m.hasSignature && CheckSignature(m);
            if (ok) { ok = false; for (const auto& k : trusted.keys) if (k == m.key) ok = true; }
            if (!ok) { if (notes) notes->push_back("module rules ignored: not signed by a trusted key"); continue; }
        } else if (notes) {
            notes->push_back(m.hasSignature && CheckSignature(m) ? "module rules are signed, but this build has no trusted keys: telemetry, not proof"
                                                                 : "module rules are unsigned: telemetry, not proof");
        }
        if (m.version < minVersion) { if (notes) notes->push_back("module rules ignored: older than one already seen"); continue; }
        if (!have || m.version > out.version) { out = m; have = true; }
    }
    return have;
}

// ---- Debugger / hypervisor hints ----------------------------------------------------------
// Each probe is empty when it can't be asked on this platform. They are hints: a
// debugger's presence is also a developer's normal day, and a hypervisor is a normal
// desktop (Windows itself runs virtualised when VBS or WSL2 is on).
struct HintProbes {
    std::function<bool()> debuggerApi;        // IsDebuggerPresent
    std::function<bool()> pebBeingDebugged;   // the same flag read straight from the process block
    std::function<bool()> ntGlobalFlag;       // heap-debug flags a debugger-started process gets (0x70)
    std::function<bool()> debugPort;          // NtQueryInformationProcess: a debug port is attached
    std::function<bool()> debugObject;        // ...a debug object exists
    std::function<bool()> debugFlagsOff;      // ...ProcessDebugFlags says "being debugged"
    std::function<bool()> hwBreakpoints;      // a debug register is armed on this thread
    std::function<bool()> hypervisor;         // CPUID says a hypervisor is present
    std::function<std::string()> hypervisorVendor;
};

inline std::vector<Finding> CheckHints(const HintProbes& p) {
    std::vector<Finding> out;
    const bool api = p.debuggerApi && p.debuggerApi();
    const bool peb = p.pebBeingDebugged && p.pebBeingDebugged();
    if (p.debuggerApi && p.pebBeingDebugged && api != peb)
        out.push_back({"debugger-hint", std::string("IsDebuggerPresent says ") + (api ? "yes" : "no") + " but the process block says " +
                                            (peb ? "yes" : "no") + ": the API may be hooked to hide a debugger", 5});
    else if (peb && !api && !p.debuggerApi)
        out.push_back({"debugger-hint", "the process block says a debugger is attached", 4});
    if (p.ntGlobalFlag && p.ntGlobalFlag()) out.push_back({"debugger-hint", "the process was started under a debugger (heap-debug flags set)", 3});
    if (p.debugPort && p.debugPort()) out.push_back({"debugger-hint", "the kernel reports a debug port on this process", 4});
    if (p.debugObject && p.debugObject()) out.push_back({"debugger-hint", "the kernel reports a debug object on this process", 4});
    if (p.debugFlagsOff && p.debugFlagsOff()) out.push_back({"debugger-hint", "the kernel reports this process is being debugged", 4});
    if (p.hwBreakpoints && p.hwBreakpoints()) out.push_back({"debugger-hint", "a hardware breakpoint is armed on the game's thread", 3});
    if (p.hypervisor && p.hypervisor()) {
        const std::string v = p.hypervisorVendor ? p.hypervisorVendor() : std::string();
        out.push_back({"hypervisor", "running under a hypervisor" + (v.empty() ? std::string() : " (" + v + ")") +
                                         ": common and harmless on its own (context for a reviewer)", 0});
    }
    return out;
}

// ---- Clocks ------------------------------------------------------------------------------
// Windowed rate of one clock against another. A window is off when the rate differs by more
// than the tolerance plus what the sampling slack explains; `sustain` windows in a row, all
// off the same way, are drift (a single step in either clock is never flagged).
class RateChannel {
public:
    RateChannel(double tolerance = 0.05, double windowMs = 4000, int sustain = 3, double slackMs = 150)
        : tol_(tolerance), win_(windowMs), sustain_(sustain < 1 ? 1 : sustain), slack_(slackMs) {}
    // `a` is the clock under test, `b` the reference, both in ms. Returns true while drift is sustained.
    bool Feed(double a, double b) {
        if (!set_) { a0_ = a; b0_ = b; set_ = true; return false; }
        const double db = b - b0_;
        if (db < win_) return drift_;
        rate_ = (a - a0_) / db - 1.0;
        a0_ = a; b0_ = b;
        const int sign = std::fabs(rate_) <= tol_ + slack_ / db ? 0 : (rate_ > 0 ? 1 : -1);
        if (sign != 0 && sign == sign_) ++run_;
        else { run_ = sign != 0 ? 1 : 0; sign_ = sign; }
        drift_ = run_ >= sustain_;
        return drift_;
    }
    double Rate() const { return rate_; }   // the last window: a / b - 1
    bool Drifting() const { return drift_; }
    void Reset() { set_ = false; run_ = 0; sign_ = 0; drift_ = false; }
private:
    double tol_, win_;
    int sustain_;
    double slack_;
    bool set_ = false, drift_ = false;
    double a0_ = 0, b0_ = 0, rate_ = 0;
    int run_ = 0, sign_ = 0;
};

// QueryPerformanceCounter against GetTickCount64 against server time. Server time (fed when
// the network layer has an authenticated reading) breaks the tie that two local clocks can't:
// when only one of them runs off the server, that one is the bent clock.
class ClockCrossCheck {
public:
    ClockCrossCheck() : qt_(), qs_(0.05, 6000, 3, 400), ts_(0.05, 6000, 3, 400) {}

    // A local sample of both clocks (ms). One finding per episode, with its explanation.
    std::vector<Finding> AddLocal(double qpcMs, double tickMs) {
        std::vector<Finding> out;
        const bool d = qt_.Feed(qpcMs, tickMs);
        if (d && !localFlag_ && !serverSeen_) {
            localFlag_ = true;
            out.push_back({"clock-cross", Percent(qt_.Rate()) + " difference between QueryPerformanceCounter and GetTickCount64 (no server time to say which)", 3});
        } else if (!d) {
            localFlag_ = false;
        }
        return out;
    }
    // The same two clocks read at the moment a server time reading arrived.
    std::vector<Finding> AddServer(double qpcMs, double tickMs, double serverMs) {
        std::vector<Finding> out;
        serverSeen_ = true;
        const bool q = qs_.Feed(qpcMs, serverMs), t = ts_.Feed(tickMs, serverMs);
        if (q && !t && !qFlag_) {
            qFlag_ = true;
            out.push_back({"clock-cross", "QueryPerformanceCounter runs " + Percent(qs_.Rate()) + " off server time while GetTickCount64 keeps pace: the performance counter is being bent", 5});
        } else if (t && !q && !tFlag_) {
            tFlag_ = true;
            out.push_back({"clock-cross", "GetTickCount64 runs " + Percent(ts_.Rate()) + " off server time while QueryPerformanceCounter keeps pace: the tick clock is being bent", 5});
        } else if (q && t && !bothFlag_) {
            bothFlag_ = true;
            out.push_back({"clock-cross", "both local clocks run " + Percent(qs_.Rate()) + " off server time (or the server's time is unreliable): the whole machine clock may be scaled", 4});
        }
        if (!q) qFlag_ = false;
        if (!t) tFlag_ = false;
        if (!q || !t) bothFlag_ = false;
        return out;
    }
    const RateChannel& QpcVsTick() const { return qt_; }
    const RateChannel& QpcVsServer() const { return qs_; }
    const RateChannel& TickVsServer() const { return ts_; }

private:
    static std::string Percent(double r) {
        char b[48];
        std::snprintf(b, sizeof(b), "%+.1f%%", r * 100.0);
        return b;
    }
    RateChannel qt_, qs_, ts_;
    bool serverSeen_ = false, localFlag_ = false, qFlag_ = false, tFlag_ = false, bothFlag_ = false;
};

// ---- The scheduler -----------------------------------------------------------------------
// What the hardening run found, boiled down: the facts a heartbeat attestation hashes (see
// AntiCheatAttest.hpp). Each is "has this ever been seen this run".
struct IntegrityFacts {
    std::string buildId;
    bool codeScanned = false;        // at least one full pass of the code hash ran
    bool codeModified = false;
    bool importsHooked = false;
    bool apiHooked = false;
    bool moduleBlocked = false;
    bool debuggerHint = false;
    bool clockSuspect = false;
    bool protectedTampered = false;
    int weight = 0;                  // sum of the weights of the distinct findings
};

struct HardeningProbes {
    // Each empty probe is a check that isn't run.
    std::function<std::vector<Finding>(std::size_t budgetBytes)> codeStep;   // the .text hash scan
    std::function<int()> codePasses;                                         // completed passes of it
    std::function<std::vector<ApiEntry>()> apis;
    std::function<std::vector<ModuleRange>()> modules;
    std::function<std::vector<Finding>(const std::vector<ModuleRange>&)> imports;
    std::function<std::vector<std::string>()> loadedModules;
    std::function<ModuleSigner(const std::string& path)> signer;
    ReadMem readMem;
    bool x64 = true;
    HintProbes hints;
    std::function<double()> qpcMs, tickMs;
    ModuleRules rules;
    bool haveRules = false;
    std::string buildId;
};

class Hardening {
public:
    explicit Hardening(HardeningProbes p) : p_(std::move(p)) { facts_.buildId = p_.buildId; }

    // Call often (the guard calls it every ~100 ms of its own clock); each part keeps its own schedule.
    std::vector<Finding> Tick(std::uint64_t nowMs) {
        std::vector<Finding> out;
        if (!started_) { started_ = true; nextCode_ = nextApi_ = nextImp_ = nextHint_ = nextClock_ = nextMod_ = nowMs; }
        if (p_.codeStep && nowMs >= nextCode_) {
            nextCode_ = nowMs + 100;
            Take(out, p_.codeStep(kCodeBudget));
            if (p_.codePasses) {
                const int passes = p_.codePasses();
                if (passes > lastPasses_) { lastPasses_ = passes; facts_.codeScanned = true; nextCode_ = nowMs + kCodeRestMs; }
            }
        }
        if (p_.apis && p_.modules && nowMs >= nextApi_) {
            nextApi_ = nowMs + 10000;
            Take(out, CheckApiEntries(p_.apis(), p_.x64, p_.modules(), p_.readMem));
        }
        if (p_.imports && p_.modules && nowMs >= nextImp_) {
            nextImp_ = nowMs + 15000;
            Take(out, p_.imports(p_.modules()));
        }
        if (p_.loadedModules && p_.haveRules && nowMs >= nextMod_) {
            nextMod_ = nowMs + 10000;
            Take(out, CheckBlocklist(p_.loadedModules()));
        }
        if (nowMs >= nextHint_) {
            nextHint_ = nowMs + 7000;
            Take(out, CheckHints(p_.hints));
        }
        if (p_.qpcMs && p_.tickMs && nowMs >= nextClock_) {
            nextClock_ = nowMs + 1000;
            Take(out, clocks_.AddLocal(p_.qpcMs(), p_.tickMs()));
        }
        return out;
    }

    // The network layer calls this when it has a server time it trusts (ms); the clocks are read now.
    std::vector<Finding> NoteServerTime(double serverMs) {
        std::vector<Finding> out;
        if (!p_.qpcMs || !p_.tickMs) return out;
        Take(out, clocks_.AddServer(p_.qpcMs(), p_.tickMs(), serverMs));
        return out;
    }

    // Are the module rules in force (and which one blocked what)? Public so a guard can ask.
    bool ModuleAllowedByRules(const std::string& path) {
        if (!p_.haveRules || p_.rules.allow.empty()) return false;
        return AllowedBy(p_.rules, path, SignerOf(path));
    }

    const IntegrityFacts& Facts() {
        facts_.protectedTampered = okay::ProtectedTamperTotal() > 0;
        return facts_;
    }
    const std::vector<Finding>& Findings() const { return all_; }

private:
    static constexpr std::size_t kCodeBudget = 64 * 1024;       // hashed per ~100 ms
    static constexpr std::uint64_t kCodeRestMs = 30000;         // between full passes

    ModuleSigner SignerOf(const std::string& path) {
        const std::string k = NormalizeModulePath(path);
        auto it = signers_.find(k);
        if (it != signers_.end()) return it->second;
        ModuleSigner s = p_.signer ? p_.signer(path) : ModuleSigner{};
        if (signers_.size() < 512) signers_[k] = s;
        return s;
    }

    std::vector<Finding> CheckBlocklist(const std::vector<std::string>& loaded) {
        std::vector<Finding> out;
        int budget = 3;   // new signature checks per run of this (they cost milliseconds each)
        for (const std::string& m : loaded) {
            const bool known = signers_.count(NormalizeModulePath(m)) != 0;
            ModuleSigner s;
            const bool needsSigner = [&] { for (const ModuleRule& r : p_.rules.block) if (!r.signer.empty()) return true; return false; }();
            if (needsSigner) {
                if (!known && budget-- <= 0) continue;
                s = SignerOf(m);
            }
            if (const ModuleRule* r = BlockedBy(p_.rules, m, s))
                out.push_back({"module-blocked", "blocklisted module " + FileNameOf(m) + (r->reason.empty() ? std::string() : " (" + r->reason + ")"), r->weight});
        }
        return out;
    }

    void Take(std::vector<Finding>& out, const std::vector<Finding>& in) {
        for (Finding f : in) {
            if (f.weight > kMaxHardeningWeight) f.weight = kMaxHardeningWeight;
            const std::string id = f.check + "|" + f.detail;
            if (!seen_.insert(id).second) continue;
            if (f.check == "code") facts_.codeModified = true;
            else if (f.check == "iat") facts_.importsHooked = true;
            else if (f.check == "hook") facts_.apiHooked = true;
            else if (f.check == "module-blocked") facts_.moduleBlocked = true;
            else if (f.check == "debugger-hint") facts_.debuggerHint = true;
            else if (f.check == "clock-cross") facts_.clockSuspect = true;
            facts_.weight += f.weight;
            all_.push_back(f);
            out.push_back(f);
        }
    }

    HardeningProbes p_;
    ClockCrossCheck clocks_;
    IntegrityFacts facts_;
    bool started_ = false;
    int lastPasses_ = 0;
    std::uint64_t nextCode_ = 0, nextApi_ = 0, nextImp_ = 0, nextHint_ = 0, nextClock_ = 0, nextMod_ = 0;
    std::set<std::string> seen_;
    std::vector<Finding> all_;
    std::map<std::string, ModuleSigner> signers_;
};

// ---- Windows probes -------------------------------------------------------------------------
#if defined(_WIN32)
namespace winprobe {

inline std::string Utf8(const wchar_t* w) {
    char buf[MAX_PATH * 4];
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, (int)sizeof(buf), nullptr, nullptr);
    return n > 1 ? std::string(buf, (std::size_t)(n - 1)) : std::string();
}

inline bool ReadSelf(std::uint64_t addr, void* out, std::size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>((std::uintptr_t)addr), out, n, &got) && got == n;
}

// Every loaded module with its address range; trusted = under the Windows folder, or what the
// guard's allowlist accepts (the game's own manifest-listed code, known overlays).
inline std::vector<ModuleRange> Modules(const ModuleAllowlist& allow) {
    std::vector<ModuleRange> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return out;
    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            ModuleRange r;
            r.path = Utf8(me.szExePath);
            r.base = (std::uint64_t)(std::uintptr_t)me.modBaseAddr;
            r.size = me.modBaseSize;
            r.trusted = ModuleAllowed(allow, r.path);
            out.push_back(std::move(r));
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return out;
}

inline std::vector<ApiEntry> Apis() {
    std::vector<ApiEntry> out;
    for (const CriticalApi& c : CriticalApis()) {
        wchar_t wm[64];
        MultiByteToWideChar(CP_UTF8, 0, c.module, -1, wm, 64);
        HMODULE h = GetModuleHandleW(wm);   // never load a module just to inspect it
        if (!h) continue;
        FARPROC f = GetProcAddress(h, c.name);
        if (!f) continue;
        ApiEntry e;
        e.name = std::string(c.module) + "!" + c.name;
        e.addr = (std::uint64_t)(std::uintptr_t)reinterpret_cast<void*>(f);
        e.bytes.resize(16);
        if (!ReadSelf(e.addr, e.bytes.data(), 16)) continue;
        out.push_back(std::move(e));
    }
    return out;
}

// The signer of a file on disk. WinVerifyTrust and the certificate calls are looked up at run
// time so linking the player needs no extra libraries.
inline ModuleSigner Signer(const std::string& pathUtf8) {
    ModuleSigner s;
    wchar_t wp[MAX_PATH * 2];
    if (MultiByteToWideChar(CP_UTF8, 0, pathUtf8.c_str(), -1, wp, MAX_PATH * 2) <= 0) return s;
    HMODULE wt = LoadLibraryW(L"wintrust.dll");
    HMODULE cr = LoadLibraryW(L"crypt32.dll");
    if (!wt || !cr) return s;
    typedef LONG(WINAPI * WvtFn)(HWND, GUID*, LPVOID);
    WvtFn wvt = reinterpret_cast<WvtFn>(reinterpret_cast<void*>(GetProcAddress(wt, "WinVerifyTrust")));
    if (!wvt) return s;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO fi;
    std::memset(&fi, 0, sizeof(fi));
    fi.cbStruct = sizeof(fi);
    fi.pcwszFilePath = wp;
    WINTRUST_DATA wd;
    std::memset(&wd, 0, sizeof(wd));
    wd.cbStruct = sizeof(wd);
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE;   // never go to the network from a game's frame loop
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    wd.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    const LONG rc = wvt(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    wvt(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
    s.checked = true;
    s.valid = rc == 0;

    // The signing certificate's display name.
    typedef BOOL(WINAPI * QoFn)(DWORD, const void*, DWORD, DWORD, DWORD, DWORD*, DWORD*, DWORD*, HCERTSTORE*, HCRYPTMSG*, const void**);
    typedef BOOL(WINAPI * MgFn)(HCRYPTMSG, DWORD, DWORD, void*, DWORD*);
    typedef PCCERT_CONTEXT(WINAPI * FindFn)(HCERTSTORE, DWORD, DWORD, DWORD, const void*, PCCERT_CONTEXT);
    typedef DWORD(WINAPI * NameFn)(PCCERT_CONTEXT, DWORD, DWORD, void*, LPSTR, DWORD);
    typedef BOOL(WINAPI * FreeCtxFn)(PCCERT_CONTEXT);
    typedef BOOL(WINAPI * CloseStoreFn)(HCERTSTORE, DWORD);
    typedef BOOL(WINAPI * CloseMsgFn)(HCRYPTMSG);
    auto fn = [&](const char* n) { return reinterpret_cast<void*>(GetProcAddress(cr, n)); };
    QoFn qo = reinterpret_cast<QoFn>(fn("CryptQueryObject"));
    MgFn mg = reinterpret_cast<MgFn>(fn("CryptMsgGetParam"));
    FindFn find = reinterpret_cast<FindFn>(fn("CertFindCertificateInStore"));
    NameFn name = reinterpret_cast<NameFn>(fn("CertGetNameStringA"));
    FreeCtxFn freeCtx = reinterpret_cast<FreeCtxFn>(fn("CertFreeCertificateContext"));
    CloseStoreFn closeStore = reinterpret_cast<CloseStoreFn>(fn("CertCloseStore"));
    CloseMsgFn closeMsg = reinterpret_cast<CloseMsgFn>(fn("CryptMsgClose"));
    if (!qo || !mg || !find || !name || !freeCtx || !closeStore || !closeMsg) return s;
    HCERTSTORE store = nullptr;
    HCRYPTMSG msg = nullptr;
    DWORD enc = 0, ct = 0, ft = 0;
    if (qo(CERT_QUERY_OBJECT_FILE, wp, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY, 0, &enc, &ct, &ft, &store, &msg, nullptr)) {
        DWORD sz = 0;
        if (mg(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &sz) && sz > 0 && sz < (1u << 20)) {
            std::vector<std::uint8_t> buf(sz);
            if (mg(msg, CMSG_SIGNER_INFO_PARAM, 0, buf.data(), &sz)) {
                const CMSG_SIGNER_INFO* si = reinterpret_cast<const CMSG_SIGNER_INFO*>(buf.data());
                CERT_INFO ci;
                std::memset(&ci, 0, sizeof(ci));
                ci.Issuer = si->Issuer;
                ci.SerialNumber = si->SerialNumber;
                PCCERT_CONTEXT cc = find(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_CERT, &ci, nullptr);
                if (cc) {
                    char nm[256] = {};
                    if (name(cc, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nm, sizeof(nm)) > 1) s.subject = nm;
                    freeCtx(cc);
                }
            }
        }
        if (store) closeStore(store, 0);
        if (msg) closeMsg(msg);
    }
    return s;
}

// ---- debugger / hypervisor probes
inline std::uint8_t* Peb() {
#if defined(__x86_64__)
    std::uint8_t* p;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(p));
    return p;
#elif defined(__i386__)
    std::uint8_t* p;
    __asm__ volatile("movl %%fs:0x30, %0" : "=r"(p));
    return p;
#else
    return nullptr;
#endif
}

typedef LONG(NTAPI* NtQipFn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
inline NtQipFn NtQip() {
    static NtQipFn f = reinterpret_cast<NtQipFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    return f;
}

inline HintProbes Hints() {
    HintProbes h;
    h.debuggerApi = [] { return IsDebuggerPresent() != FALSE; };
    if (Peb()) {
        h.pebBeingDebugged = [] { return Peb()[2] != 0; };
#if defined(__x86_64__)
        h.ntGlobalFlag = [] { std::uint32_t v; std::memcpy(&v, Peb() + 0xBC, 4); return (v & 0x70) != 0; };
#else
        h.ntGlobalFlag = [] { std::uint32_t v; std::memcpy(&v, Peb() + 0x68, 4); return (v & 0x70) != 0; };
#endif
    }
    if (NtQip()) {
        h.debugPort = [] {
            ULONG_PTR port = 0;
            return NtQip()(GetCurrentProcess(), 7, &port, sizeof(port), nullptr) == 0 && port != 0;
        };
        h.debugObject = [] {
            HANDLE obj = nullptr;
            const LONG st = NtQip()(GetCurrentProcess(), 0x1E, &obj, sizeof(obj), nullptr);
            if (st == 0 && obj) { CloseHandle(obj); return true; }
            return false;   // STATUS_PORT_NOT_SET (no debugger) is the normal answer
        };
        h.debugFlagsOff = [] {
            ULONG flags = 1;
            return NtQip()(GetCurrentProcess(), 0x1F, &flags, sizeof(flags), nullptr) == 0 && flags == 0;   // 0 = being debugged
        };
    }
    h.hwBreakpoints = [] {
        CONTEXT c;
        std::memset(&c, 0, sizeof(c));
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        return GetThreadContext(GetCurrentThread(), &c) && (c.Dr0 || c.Dr1 || c.Dr2 || c.Dr3);
    };
#if defined(OKAY_AC_HAVE_CPUID_H)
    h.hypervisor = [] {
        unsigned a = 0, b = 0, c = 0, d = 0;
        return __get_cpuid(1, &a, &b, &c, &d) && (c & (1u << 31)) != 0;
    };
    h.hypervisorVendor = [] {
        unsigned a = 0, b = 0, c = 0, d = 0;
        __cpuid(0x40000000, a, b, c, d);
        char v[13];
        std::memcpy(v, &b, 4); std::memcpy(v + 4, &c, 4); std::memcpy(v + 8, &d, 4);
        v[12] = 0;
        for (int i = 0; i < 12; ++i) if (v[i] < 32 || v[i] > 126) return std::string();
        return std::string(v);
    };
#endif
    return h;
}

inline double QpcMs() {
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return f.QuadPart > 0 ? (double)c.QuadPart * 1000.0 / (double)f.QuadPart : 0.0;
}
inline double TickMs() {
    typedef ULONGLONG(WINAPI * Tick64Fn)();
    static const Tick64Fn t64 = reinterpret_cast<Tick64Fn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount64")));
    return t64 ? (double)t64() : (double)GetTickCount();
}

inline const std::uint8_t* SelfImage(std::size_t& size) {
    const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    pe::Image im;
    // The header is mapped, so it can be parsed in place; SizeOfImage tells how far the image goes.
    if (!base || !pe::Parse(base, 4096, im)) { size = 0; return nullptr; }
    size = im.sizeOfImage;
    return base;
}

} // namespace winprobe
#endif

// ---- Opting in, and the player's one call -----------------------------------------------------
// Hardening runs only when the game's anti-cheat mode is report or enforce AND the game opts in:
// it ships Data/codebaseline.json or .anticheat/modules.json, or the environment has
// OKAY_AC_HARDENING=1 (for trying it). A game with neither behaves exactly as before.
constexpr const char* kModuleRulesFile = "modules.json";

inline std::string ReadTextFile(const fs::path& p, std::size_t cap = 1u << 21) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return s.size() > cap ? std::string() : s;
}

struct HardeningSetup {
    bool enabled = false;
    std::shared_ptr<Hardening> hardening;
    std::vector<std::string> notes;   // what was loaded / ignored and why (for the log)
};

// Builds the probes from the game's files. `gameRoot` holds Data/ and .anticheat/; `saveDir` is
// where downloaded rules may be dropped (saveDir/anticheat/modules.json). Elsewhere than Windows
// the system probes are empty, so only the data-file logic loads (and nothing is scanned).
inline HardeningSetup SetupHardening(const fs::path& gameRoot, const fs::path& saveDir, Mode mode, const TrustedKeySet& trusted,
                                     const ModuleAllowlist& allow, const std::string& optIn) {
    HardeningSetup out;
    if (mode == Mode::Off) return out;
    std::error_code ec;
    const fs::path baselinePath = gameRoot / pe::kCodeBaselineFile;
    const fs::path rulesA = gameRoot / ".anticheat" / kModuleRulesFile, rulesB = saveDir / "anticheat" / kModuleRulesFile;
    const bool haveBaseline = fs::is_regular_file(baselinePath, ec);
    const bool haveRules = fs::is_regular_file(rulesA, ec) || fs::is_regular_file(rulesB, ec);
    if (!(haveBaseline || haveRules || optIn == "1")) return out;
    out.enabled = true;

    HardeningProbes p;
    pe::CodeBaseline baseline;
    bool baselineOk = false;
    if (haveBaseline) {
        std::string err;
        if (!pe::ParseCodeBaseline(ReadTextFile(baselinePath), baseline, &err)) {
            out.notes.push_back("code baseline ignored: " + err);
        } else {
            const pe::BaselineTrust t = pe::CheckBaselineTrust(baseline, trusted);
            out.notes.push_back(t.note.empty() ? "code baseline loaded" : t.note);
            baselineOk = t.usable;
        }
    }
    if (haveRules) {
        std::vector<std::string> texts;
        for (const fs::path& f : {rulesA, rulesB}) { const std::string t = ReadTextFile(f); if (!t.empty()) texts.push_back(t); }
        out.notes.push_back("module rules: " + std::to_string(texts.size()) + " file(s) found");
        if (PickModuleRules(texts, trusted, 0, p.rules, &out.notes)) {
            p.haveRules = true;
            out.notes.push_back("module rules v" + std::to_string(p.rules.version) + ": " + std::to_string(p.rules.block.size()) + " block, " +
                                std::to_string(p.rules.allow.size()) + " allow");
        }
    }
    if (baselineOk) p.buildId = baseline.build;
#if defined(_WIN32)
    p.x64 = sizeof(void*) == 8;
    p.readMem = winprobe::ReadSelf;
    p.hints = winprobe::Hints();
    p.qpcMs = [] { return winprobe::QpcMs(); };
    p.tickMs = [] { return winprobe::TickMs(); };
    p.signer = [](const std::string& path) { return winprobe::Signer(path); };
    p.loadedModules = [] { return LoadedModules(); };
    p.apis = [] { return winprobe::Apis(); };
    p.modules = [allow] { return winprobe::Modules(allow); };
    p.imports = [](const std::vector<ModuleRange>& mods) {
        std::size_t sz = 0;
        const std::uint8_t* img = winprobe::SelfImage(sz);
        return img ? CheckImports(img, sz, mods) : std::vector<Finding>();
    };
    if (baselineOk) {
        // One scanner per baseline module that is actually loaded (the exe, and game DLLs).
        auto scanners = std::make_shared<std::vector<pe::CodeScanner>>();
        for (const pe::ModuleBaseline& m : baseline.modules) {
            wchar_t wn[MAX_PATH];
            if (MultiByteToWideChar(CP_UTF8, 0, m.name.c_str(), -1, wn, MAX_PATH) <= 0) continue;
            HMODULE h = GetModuleHandleW(wn);
            if (!h) continue;
            const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(h);
            pe::Image im;
            if (!pe::Parse(base, 4096, im)) continue;
            scanners->emplace_back(base, im.sizeOfImage, m);
        }
        if (!scanners->empty()) {
            auto cur = std::make_shared<std::size_t>(0);
            p.codeStep = [scanners, cur](std::size_t budget) {
                return (*scanners)[(*cur)++ % scanners->size()].Step(budget);
            };
            p.codePasses = [scanners] { int m = 1 << 30; for (auto& s : *scanners) m = std::min(m, s.Passes()); return m == (1 << 30) ? 0 : m; };
        } else {
            out.notes.push_back("code baseline: none of its modules is loaded in this process");
        }
    }
#else
    (void)allow;
#endif
    out.hardening = std::make_shared<Hardening>(std::move(p));
    return out;
}

} // namespace okayac
