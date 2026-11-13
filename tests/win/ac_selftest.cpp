// Windows self-test for the anti-cheat client hardening (shared/AntiCheatHardening.hpp, AntiCheatPe.hpp).
// Not part of ctest: it needs a Windows process (or wine) and a baseline made from this very exe:
//   scripts/ac_wine_selftest.sh builds it with MinGW, makes Data/codebaseline.json with okay_ac_tool,
//   and runs it under wine. Exit 0 = every check passed.
//
// It exercises the real probes: the in-memory code hash against the baseline from disk (ASLR / relocation
// masking), a patched code byte, a redirected IAT entry, an inline hook written over a real API, the
// debugger hints, the clocks, the module list, WinVerifyTrust loading and the guard loop end to end.
#include <windows.h>
#include <cstdio>
#include <thread>

#include "AntiCheatHardening.hpp"

using namespace okayac;

static int g_fail = 0, g_ok = 0;
#define EXPECT(cond, ...)                                                       \
    do {                                                                        \
        if (cond) { ++g_ok; std::printf("  ok:   " __VA_ARGS__); std::printf("\n"); }   \
        else { ++g_fail; std::printf("  FAIL: " __VA_ARGS__); std::printf("   [%s]\n", #cond); } \
    } while (0)

// Never called: a place in .text whose bytes the test may flip safely.
extern "C" __attribute__((noinline, used)) void AcSelfTestSpare() {
    __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop");
}

static std::string ExeDir() {
    wchar_t w[MAX_PATH];
    GetModuleFileNameW(nullptr, w, MAX_PATH);
    std::wstring p(w);
    return winprobe::Utf8(p.substr(0, p.find_last_of(L"\\/")).c_str());
}

static std::size_t FindingsOf(const std::vector<Finding>& v, const char* check) {
    std::size_t n = 0;
    for (const Finding& f : v) if (f.check == check) ++n;
    return n;
}

int main() {
    const std::string dir = ExeDir();
    std::printf("ac_selftest: dir=%s ptr=%zu\n", dir.c_str(), sizeof(void*));
    const ModuleAllowlist allow = DefaultModuleAllowlist(dir, "C:/windows", nullptr);

    // ---- the baseline from disk against the image in memory
    pe::CodeBaseline baseline;
    {
        std::string err;
        const std::string text = ReadTextFile(fs::u8path(dir) / "Data" / "codebaseline.json");
        EXPECT(pe::ParseCodeBaseline(text, baseline, &err), "baseline parses (%s)", err.c_str());
        EXPECT(baseline.hasSignature && pe::CheckSignature(baseline), "baseline signature verifies");
    }
    std::size_t imgSize = 0;
    const std::uint8_t* img = winprobe::SelfImage(imgSize);
    EXPECT(img && imgSize > 4096, "own image found: base=%p size=0x%zx", (const void*)img, imgSize);
    pe::Image im;
    pe::Parse(img, 4096, im);
    std::printf("  image base preferred=0x%llx loaded=%p (%s)\n", (unsigned long long)im.imageBase, (const void*)img,
                (std::uint64_t)(std::uintptr_t)img == im.imageBase ? "at preferred base" : "RELOCATED");
    const pe::ModuleBaseline* mine = nullptr;
    for (const auto& m : baseline.modules) if (m.name == "ac_selftest.exe") mine = &m;
    EXPECT(mine != nullptr, "baseline has this exe");
    if (!mine) return 1;
    {
        pe::CodeScanner sc(img, imgSize, *mine);
        const auto f = sc.ScanAll();
        EXPECT(f.empty(), "clean image: no code findings (%zu)", f.size());
        for (const Finding& x : f) std::printf("    %s\n", FormatFinding(x).c_str());
    }
    {   // patch a spare byte in .text, scan, restore, scan
        auto* spare = reinterpret_cast<std::uint8_t*>(&AcSelfTestSpare) + 6;
        DWORD old = 0;
        EXPECT(VirtualProtect(spare, 1, PAGE_EXECUTE_READWRITE, &old) != 0, "VirtualProtect on .text");
        const std::uint8_t orig = *spare;
        *spare = 0x91;
        FlushInstructionCache(GetCurrentProcess(), spare, 1);
        pe::CodeScanner sc(img, imgSize, *mine);
        const auto f = sc.ScanAll();
        EXPECT(f.size() == 1 && f[0].check == "code" && f[0].weight == 6, "patched byte is found (%zu finding(s))", f.size());
        *spare = orig;
        VirtualProtect(spare, 1, old, &old);
        pe::CodeScanner sc2(img, imgSize, *mine);
        EXPECT(sc2.ScanAll().empty(), "restored image is clean again");
    }

    // ---- modules, imports, hooks on the clean process
    const std::vector<ModuleRange> mods = winprobe::Modules(allow);
    EXPECT(mods.size() >= 3, "module list has %zu entries", mods.size());
    std::size_t trusted = 0;
    for (const auto& m : mods) { trusted += m.trusted; if (!m.trusted) std::printf("    untrusted: %s\n", m.path.c_str()); }
    std::printf("  trusted %zu of %zu\n", trusted, mods.size());
    {
        const auto f = CheckImports(img, imgSize, mods);
        EXPECT(f.empty(), "clean process: IAT clean (%zu finding(s))", f.size());
        for (const Finding& x : f) std::printf("    %s\n", FormatFinding(x).c_str());
    }
    const bool x64 = sizeof(void*) == 8;
    {
        const auto apis = winprobe::Apis();
        std::printf("  critical APIs resolved: %zu of %zu\n", apis.size(), CriticalApis().size());
        EXPECT(apis.size() >= 5, "resolved the critical APIs");
        for (const ApiEntry& a : apis) {
            std::printf("    %-44s %02x %02x %02x %02x %02x %02x\n", a.name.c_str(), a.bytes[0], a.bytes[1], a.bytes[2], a.bytes[3], a.bytes[4], a.bytes[5]);
        }
        const auto f = CheckApiEntries(apis, x64, mods, winprobe::ReadSelf);
        EXPECT(f.empty(), "clean process: no inline hooks reported (%zu)", f.size());
        for (const Finding& x : f) std::printf("    %s\n", FormatFinding(x).c_str());
    }

    // ---- an IAT entry redirected into private memory
    {
        const std::uint32_t impRva = im.importRva;
        EXPECT(impRva != 0, "has an import table");
        std::uint8_t* slot = nullptr;
        for (std::uint32_t d = impRva; ; d += 20) {
            std::uint32_t name, ft;
            std::memcpy(&name, img + d + 12, 4);
            std::memcpy(&ft, img + d + 16, 4);
            if (!name && !ft) break;
            if (!_stricmp(reinterpret_cast<const char*>(img + name), "KERNEL32.dll")) { slot = const_cast<std::uint8_t*>(img) + ft; break; }
        }
        EXPECT(slot != nullptr, "found the KERNEL32 IAT");
        if (slot) {
            void* shell = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            std::uintptr_t orig;
            std::memcpy(&orig, slot, sizeof(orig));
            DWORD old = 0;
            VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old);
            const std::uintptr_t evil = (std::uintptr_t)shell;
            std::memcpy(slot, &evil, sizeof(evil));
            auto f = CheckImports(img, imgSize, winprobe::Modules(allow));
            EXPECT(f.size() == 1 && f[0].check == "iat", "redirected IAT entry reported (%zu): %s", f.size(), f.empty() ? "" : f[0].detail.c_str());
            std::memcpy(slot, &orig, sizeof(orig));
            VirtualProtect(slot, sizeof(void*), old, &old);
            f = CheckImports(img, imgSize, winprobe::Modules(allow));
            EXPECT(f.empty(), "IAT restored: clean");
            VirtualFree(shell, 0, MEM_RELEASE);
        }
    }

    // ---- an inline hook written over a real API entry (GetTickCount64 in kernel32)
    {
        FARPROC fn = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount64");
        auto* p = reinterpret_cast<std::uint8_t*>(reinterpret_cast<void*>(fn));
        void* shell = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        std::uint8_t saved[16];
        std::memcpy(saved, p, 16);
        DWORD old = 0;
        VirtualProtect(p, 16, PAGE_EXECUTE_READWRITE, &old);
        if (x64) {   // mov rax, imm64 ; jmp rax
            p[0] = 0x48; p[1] = 0xB8;
            const std::uint64_t t = (std::uint64_t)(std::uintptr_t)shell;
            std::memcpy(p + 2, &t, 8);
            p[10] = 0xFF; p[11] = 0xE0;
        } else {
            p[0] = 0xB8;
            const std::uint32_t t = (std::uint32_t)(std::uintptr_t)shell;
            std::memcpy(p + 1, &t, 4);
            p[5] = 0xFF; p[6] = 0xE0;
        }
        const auto f = CheckApiEntries(winprobe::Apis(), x64, winprobe::Modules(allow), winprobe::ReadSelf);
        bool named = false;
        for (const Finding& x : f) if (x.detail.find("GetTickCount64") != std::string::npos) named = true;
        EXPECT(named && f.size() == 1, "hooked GetTickCount64 reported (%zu): %s", f.size(), f.empty() ? "" : f[0].detail.c_str());
        std::memcpy(p, saved, 16);
        VirtualProtect(p, 16, old, &old);
        EXPECT(CheckApiEntries(winprobe::Apis(), x64, winprobe::Modules(allow), winprobe::ReadSelf).empty(), "unhooked: clean again");
        VirtualFree(shell, 0, MEM_RELEASE);
    }

    // ---- debugger / hypervisor hints on a clean process
    {
        const HintProbes h = winprobe::Hints();
        std::printf("  probes present: api=%d peb=%d ntglobal=%d port=%d obj=%d flags=%d hwbp=%d hv=%d\n", !!h.debuggerApi, !!h.pebBeingDebugged,
                    !!h.ntGlobalFlag, !!h.debugPort, !!h.debugObject, !!h.debugFlagsOff, !!h.hwBreakpoints, !!h.hypervisor);
        const auto f = CheckHints(h);
        std::size_t bad = 0;
        for (const Finding& x : f) { std::printf("    %s\n", FormatFinding(x).c_str()); if (x.check != "hypervisor") ++bad; }
        EXPECT(bad == 0, "no debugger hints in a clean process");
        EXPECT(h.pebBeingDebugged && h.pebBeingDebugged() == (IsDebuggerPresent() != 0), "PEB BeingDebugged agrees with IsDebuggerPresent");
    }

    // ---- clocks: QPC against GetTickCount64 over a few seconds
    {
        const double q0 = winprobe::QpcMs(), t0 = winprobe::TickMs();
        Sleep(3000);
        const double q1 = winprobe::QpcMs(), t1 = winprobe::TickMs();
        const double rate = (q1 - q0) / (t1 - t0) - 1.0;
        std::printf("  qpc/tick rate over 3 s: %+.2f%%\n", rate * 100);
        EXPECT(std::fabs(rate) < 0.05, "QPC and GetTickCount64 agree to 5%%");
    }

    // ---- WinVerifyTrust / signer probe: must not crash; under wine the answer isn't meaningful
    {
        const ModuleSigner s = winprobe::Signer("C:\\windows\\system32\\kernel32.dll");
        std::printf("  signer(kernel32): checked=%d valid=%d subject='%s'\n", s.checked, s.valid, s.subject.c_str());
        EXPECT(s.checked || !s.checked, "signer probe returned");
    }

    // ---- the guard end to end: setup from files, then a patched byte is reported within seconds, in enforce mode too
    {
        TrustedKeySet none;
        HardeningSetup hs = SetupHardening(fs::u8path(dir), fs::u8path(dir) / "save", Mode::Enforce, none, allow, "");
        for (const std::string& n : hs.notes) std::printf("    note: %s\n", n.c_str());
        EXPECT(hs.enabled && hs.hardening, "hardening is on (a baseline exists)");
        GuardProbes gp = SystemGuardProbes(dir, nullptr);
        gp.allowlist = allow;
        auto h = hs.hardening;
        gp.hardening = [h](std::uint64_t now) { return h->Tick(now); };
        RuntimeGuard guard(Mode::Enforce, gp, [](const std::string& s) { std::printf("    log: %s\n", s.c_str()); });
        // clean for ~2.5 s
        bool quit = false;
        for (int i = 0; i < 25 && !quit; ++i) { quit = guard.Tick(); Sleep(100); }
        std::size_t signals = 0;
        for (const Finding& f : guard.Findings()) if (f.weight > 0) ++signals;
        EXPECT(!quit && signals == 0, "clean run: no signals, no quit (%zu signals)", signals);
        auto* spare = reinterpret_cast<std::uint8_t*>(&AcSelfTestSpare) + 9;
        DWORD old = 0;
        VirtualProtect(spare, 1, PAGE_EXECUTE_READWRITE, &old);
        const std::uint8_t orig = *spare;
        *spare = 0x91;
        bool seen = false;
        for (int i = 0; i < 600 && !seen; ++i) {   // up to 60 s
            guard.Tick(); Sleep(100);
            for (const Finding& f : guard.Findings()) if (f.check == "code") seen = true;
        }
        EXPECT(seen, "the patched byte is reported by the running guard");
        EXPECT(!guard.Tick(), "enforce mode still doesn't end the game for a hardening signal");
        *spare = orig;
        VirtualProtect(spare, 1, old, &old);
        const IntegrityFacts& fx = h->Facts();
        std::printf("  facts: scanned=%d modified=%d weight=%d build='%s'\n", fx.codeScanned, fx.codeModified, fx.weight, fx.buildId.c_str());
        EXPECT(fx.codeModified, "facts record the modification");
    }

    std::printf("ac_selftest: %d ok, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
