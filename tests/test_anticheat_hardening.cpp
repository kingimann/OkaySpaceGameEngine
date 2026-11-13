#include "test_framework.hpp"
#include "AntiCheatHardening.hpp"
#include "ac_pe_testkit.hpp"
#include <random>

// Anti-cheat client hardening: code-section hashing against a baseline (relocation
// aware), inline-hook and IAT detection, the signed module rules, debugger / hypervisor
// hints, the clock cross-check and the scheduler. All logic; the Windows probes that
// feed it are compiled out here (they are run under wine, see docs/anticheat.md).
namespace {
using namespace okayac;

ed25519::Seed TestSeed(std::uint8_t b = 7) { ed25519::Seed s{}; for (std::size_t i = 0; i < s.size(); ++i) s[i] = (std::uint8_t)(b + i); return s; }

std::vector<std::uint8_t> MakeText(std::size_t n, unsigned seed = 1) {
    std::mt19937 r(seed);
    std::vector<std::uint8_t> v(n);
    for (auto& b : v) b = (std::uint8_t)r();
    return v;
}
const std::vector<std::uint32_t> kRelocs = {0x10, 0x48, 0x1008, 0x1ff8};   // the last straddles nothing; 0x1008 is on page 2

actest::TestPe Build() {
    return actest::MakePe(MakeText(0x2400), kRelocs, {{"KERNEL32.dll", {"QueryPerformanceCounter", "GetTickCount64"}}, {"USER32.dll", {"MessageBoxW"}}});
}

void TestPeParse() {
    RUN_SUITE("pe: parse");
    actest::TestPe t = Build();
    pe::Image im;
    std::string err;
    CHECK(pe::Parse(t.file.data(), t.file.size(), im, &err));
    CHECK(im.is64 && im.imageBase == 0x140000000ull);
    CHECK(im.sections.size() == 3 && im.sections[0].name == ".text" && im.sections[0].IsCode());
    CHECK(!im.sections[1].IsCode());
    CHECK(im.importRva != 0 && im.relocRva != 0);
    pe::Image im2;
    CHECK(pe::Parse(t.mapped.data(), t.mapped.size(), im2));
    CHECK(im2.sections.size() == 3);
    CHECK(!pe::Parse(nullptr, 0, im));
    // Garbage never crashes: every truncation and many random corruptions are either parsed or refused.
    std::mt19937 rng(5);
    for (std::size_t n = 0; n < t.file.size(); n += 37) { pe::Image x; pe::Parse(t.file.data(), n, x); }
    for (int i = 0; i < 3000; ++i) {
        auto c = t.file;
        for (int k = 0; k < 4; ++k) c[rng() % 0x400] = (std::uint8_t)rng();
        pe::Image x;
        if (pe::Parse(c.data(), c.size(), x)) {
            std::vector<pe::SectionDigest> d;
            pe::HashCodeSections(c.data(), c.size(), pe::Layout::File, d);
            CheckImports(c.data(), c.size(), {});
        }
    }
    CHECK(true);
}

void TestHashRelocAware() {
    RUN_SUITE("pe: the code hash is the same on disk and in memory at any base; a patch changes it");
    actest::TestPe t = Build();
    std::vector<pe::SectionDigest> disk, mem;
    std::string err;
    CHECK(pe::HashCodeSections(t.file.data(), t.file.size(), pe::Layout::File, disk, &err));
    CHECK(disk.size() == 1 && disk[0].name == ".text" && disk[0].sha256.size() == 64 && disk[0].size == 0x2400);
    CHECK(pe::HashCodeSections(t.mapped.data(), t.mapped.size(), pe::Layout::Mapped, mem, &err));
    CHECK(mem[0].sha256 == disk[0].sha256);
    actest::Rebase(t, kRelocs, 0x7ff000000000ll - 0x140000000ll);   // ASLR moved it
    CHECK(pe::HashCodeSections(t.mapped.data(), t.mapped.size(), pe::Layout::Mapped, mem));
    CHECK(mem[0].sha256 == disk[0].sha256);
    // A patched instruction (a NOPed check, a detour) is caught.
    for (std::uint32_t off : {0u, 0x11u, 0x100u, 0x23ffu}) {
        actest::TestPe p = Build();
        p.mapped[0x1000 + off] ^= 0x90;
        pe::HashCodeSections(p.mapped.data(), p.mapped.size(), pe::Layout::Mapped, mem);
        // 0x11 sits inside the relocated field at 0x10..0x17: masked on purpose, so a change there isn't seen (documented limit)
        CHECK((mem[0].sha256 != disk[0].sha256) == (off != 0x11u));
    }
    // A data section being written isn't code and is ignored.
    actest::TestPe q = Build();
    q.mapped[q.iatRva] ^= 0xFF;
    pe::HashCodeSections(q.mapped.data(), q.mapped.size(), pe::Layout::Mapped, mem);
    CHECK(mem[0].sha256 == disk[0].sha256);
}

void TestHasherSteps() {
    RUN_SUITE("pe: stepped hashing equals the one-shot hash for any budget");
    actest::TestPe t = Build();
    std::vector<pe::SectionDigest> ref;
    pe::HashCodeSections(t.mapped.data(), t.mapped.size(), pe::Layout::Mapped, ref);
    pe::Image im;
    pe::Parse(t.mapped.data(), t.mapped.size(), im);
    const pe::Section& s = im.sections[0];
    for (std::size_t budget : {1u, 3u, 7u, 100u, 4096u, 1000000u}) {
        pe::SectionHasher h(pe::SectionData(t.mapped.data(), t.mapped.size(), s, pe::Layout::Mapped), s.Len(),
                            pe::RelocMasks(t.mapped.data(), t.mapped.size(), im, pe::Layout::Mapped, s));
        int steps = 0;
        while (!h.Step(budget) && steps < 5000000) ++steps;
        CHECK(h.Hex() == ref[0].sha256);
        if (budget == 1) CHECK(steps > 1000);
    }
}


void TestBaselineFile() {
    RUN_SUITE("baseline file: format, signing, trust");
    actest::TestPe t = Build();
    pe::CodeBaseline b;
    b.build = "1.2.3+abc";
    pe::ModuleBaseline m;
    m.name = "game.exe";
    std::string err;
    CHECK(pe::HashCodeSections(t.file.data(), t.file.size(), pe::Layout::File, m.sections, &err));
    b.modules.push_back(m);
    const ed25519::Seed seed = TestSeed();
    pe::Sign(b, seed);
    CHECK(pe::CheckSignature(b));
    const std::string json = pe::ToJson(b);
    pe::CodeBaseline r;
    CHECK(pe::ParseCodeBaseline(json, r, &err));
    CHECK(r.build == b.build && r.modules.size() == 1 && r.modules[0].sections[0].sha256 == m.sections[0].sha256);
    CHECK(pe::CheckSignature(r));
    {   // any edit to the signed text breaks the signature
        pe::CodeBaseline e = r;
        e.modules[0].sections[0].sha256[3] = e.modules[0].sections[0].sha256[3] == 'a' ? 'b' : 'a';
        CHECK(!pe::CheckSignature(e));
        e = r; e.build = "1.2.4"; CHECK(!pe::CheckSignature(e));
    }
    // Bad input is refused.
    pe::CodeBaseline x;
    CHECK(!pe::ParseCodeBaseline("", x));
    CHECK(!pe::ParseCodeBaseline("{\"format\":\"other\"}", x));
    CHECK(!pe::ParseCodeBaseline(std::string(json).replace(json.find("sha256\": \"") + 10, 4, "ZZZZ"), x));
    CHECK(!pe::ParseCodeBaseline(std::string(json).replace(json.find("game.exe"), 8, "a/b.exe"), x));   // names, not paths
    // Trust: with trusted keys required, only that key's signature is enough.
    TrustedKeySet ts;
    ts.required = true;
    ts.keys.push_back(ed25519::PublicKeyFromSeed(seed));
    CHECK(pe::CheckBaselineTrust(r, ts).usable);
    TrustedKeySet other;
    other.required = true;
    other.keys.push_back(ed25519::PublicKeyFromSeed(TestSeed(99)));
    CHECK(!pe::CheckBaselineTrust(r, other).usable);
    pe::CodeBaseline unsignedB = r; unsignedB.hasSignature = false;
    CHECK(!pe::CheckBaselineTrust(unsignedB, ts).usable);
    TrustedKeySet none;
    CHECK(pe::CheckBaselineTrust(unsignedB, none).usable && !pe::CheckBaselineTrust(unsignedB, none).note.empty());
    TrustedKeySet malformed;   // a malformed built-in key list: required, no keys: fails closed
    malformed.required = true;
    CHECK(!pe::CheckBaselineTrust(r, malformed).usable);
}

void TestScanner() {
    RUN_SUITE("code scanner: clean, patched, slices");
    actest::TestPe t = Build();
    pe::ModuleBaseline m;
    m.name = "game.exe";
    pe::HashCodeSections(t.file.data(), t.file.size(), pe::Layout::File, m.sections);
    {
        actest::TestPe loaded = Build();
        actest::Rebase(loaded, kRelocs, 0x1000000);
        pe::CodeScanner sc(loaded.mapped.data(), loaded.mapped.size(), m);
        CHECK(sc.ScanAll().empty());
        CHECK(sc.Passes() == 1);
    }
    {
        actest::TestPe loaded = Build();
        loaded.mapped[0x1000 + 0x200] ^= 0xCC;
        pe::CodeScanner sc(loaded.mapped.data(), loaded.mapped.size(), m);
        const auto f = sc.ScanAll();
        CHECK(f.size() == 1 && f[0].check == "code" && f[0].weight == 6 && f[0].detail.find(".text") != std::string::npos);
    }
    {   // a patch that lands while slices are being hashed is caught on the next pass
        actest::TestPe loaded = Build();
        pe::CodeScanner sc(loaded.mapped.data(), loaded.mapped.size(), m);
        std::vector<Finding> all;
        int steps = 0;
        while (sc.Passes() < 1 && steps++ < 100000) { auto f = sc.Step(512); all.insert(all.end(), f.begin(), f.end()); }
        CHECK(all.empty() && steps > 10);
        loaded.mapped[0x1000 + 0x300] ^= 1;
        while (sc.Passes() < 2 && steps++ < 200000) { auto f = sc.Step(512); all.insert(all.end(), f.begin(), f.end()); }
        CHECK(all.size() == 1);
    }
    {   // a module the baseline doesn't know a section of
        pe::ModuleBaseline wrong = m;
        wrong.sections[0].name = ".code";
        actest::TestPe loaded = Build();
        pe::CodeScanner sc(loaded.mapped.data(), loaded.mapped.size(), wrong);
        const auto f = sc.ScanAll();
        CHECK(f.size() == 1 && f[0].weight == 3);
    }
    {   // garbage in memory: reported once, no crash
        std::vector<std::uint8_t> junk(100, 0);
        pe::CodeScanner sc(junk.data(), junk.size(), m);
        CHECK(sc.Step(100).size() == 1);
        CHECK(sc.Step(100).empty());
    }
}

std::vector<ModuleRange> Mods() {
    return {{"C:\\Windows\\System32\\ntdll.dll", 0x7ff800000000ull, 0x200000, true},
            {"C:\\Windows\\System32\\kernelbase.dll", 0x7ff810000000ull, 0x300000, true},
            {"C:\\Games\\Mine\\game.exe", 0x140000000ull, 0x100000, true},
            {"C:\\Temp\\cheat.dll", 0x7ff820000000ull, 0x10000, false}};
}

void TestHooks() {
    RUN_SUITE("inline hooks: decode and classify");
    const std::uint64_t fn = 0x7ff810001000ull;   // inside kernelbase
    auto mem = [](std::uint64_t slot, std::uint64_t value) {
        return ReadMem([slot, value](std::uint64_t a, void* out, std::size_t n) {
            if (a != slot) return false;
            std::memcpy(out, &value, n);
            return true;
        });
    };
    auto entry = [&](std::vector<std::uint8_t> b) { b.resize(16, 0x90); ApiEntry e; e.name = "k32!Fn"; e.addr = fn; e.bytes = b; return e; };
    auto rel32 = [&](std::uint64_t target) {
        std::int32_t d = (std::int32_t)(target - (fn + 5));
        std::vector<std::uint8_t> b = {0xE9, 0, 0, 0, 0};
        std::memcpy(&b[1], &d, 4);
        return b;
    };
    Finding f;
    // An ordinary prologue: not a hook.
    CHECK(!CheckApiEntry(entry({0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20}), true, Mods(), nullptr, f));
    CHECK(!CheckApiEntry(entry({0x4C, 0x8B, 0xD1, 0xB8, 0x18, 0x00, 0x00, 0x00, 0x0F, 0x05}), true, Mods(), nullptr, f));   // a syscall stub
    // A forwarding jump into another system module: fine.
    CHECK(!CheckApiEntry(entry(rel32(0x7ff800000500ull)), true, Mods(), nullptr, f));
    {   // jmp [rip+0x20] whose slot holds an address in ntdll: fine; in the cheat DLL: a hook
        std::vector<std::uint8_t> b = {0xFF, 0x25, 0x20, 0, 0, 0};
        CHECK(!CheckApiEntry(entry(b), true, Mods(), mem(fn + 6 + 0x20, 0x7ff800000900ull), f));
        CHECK(CheckApiEntry(entry(b), true, Mods(), mem(fn + 6 + 0x20, 0x7ff820000100ull), f));
        CHECK(f.check == "hook" && f.weight == 5 && f.detail.find("cheat.dll") != std::string::npos);
        CHECK(CheckApiEntry(entry(b), true, Mods(), mem(fn + 6 + 0x20, 0x1234560000ull), f));
        CHECK(f.detail.find("outside any module") != std::string::npos);
        CHECK(!CheckApiEntry(entry(b), true, Mods(), nullptr, f));   // slot unreadable: say nothing
    }
    // A detour into the cheat DLL, three ways, and into shellcode.
    CHECK(CheckApiEntry(entry(rel32(0x7ff820000200ull)), true, Mods(), nullptr, f));
    {
        std::vector<std::uint8_t> b = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
        const std::uint64_t t = 0x7ff820000300ull;
        std::memcpy(&b[2], &t, 8);
        CHECK(CheckApiEntry(entry(b), true, Mods(), nullptr, f) && f.detail.find("cheat.dll") != std::string::npos);
        b[10] = 0x50; b[11] = 0xC3;   // push rax; ret
        CHECK(CheckApiEntry(entry(b), true, Mods(), nullptr, f));
        std::vector<std::uint8_t> c = {0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0, 0x41, 0xFF, 0xE3};
        std::memcpy(&c[2], &t, 8);
        CHECK(CheckApiEntry(entry(c), true, Mods(), nullptr, f));
    }
    {   // 32-bit shapes
        std::vector<std::uint8_t> b = {0xB8, 0x00, 0x00, 0x90, 0x70, 0xFF, 0xE0};
        const std::vector<ModuleRange> m32 = {{"c:/windows/syswow64/ntdll.dll", 0x77000000, 0x100000, true}};
        CHECK(CheckApiEntry(entry(b), false, m32, nullptr, f));
        CHECK(f.detail.find("outside any module") != std::string::npos);
        std::vector<std::uint8_t> p = {0x68, 0x00, 0x00, 0x77, 0x77, 0xC3};
        CHECK(CheckApiEntry(entry(p), false, m32, nullptr, f));   // push imm32; ret into memory no module owns
        std::vector<std::uint8_t> q = {0x68, 0x10, 0x00, 0x00, 0x20, 0xC3};
        CHECK(CheckApiEntry(entry(q), false, m32, nullptr, f));
    }
    // A breakpoint at the entry (a debugger's, or a lazy hook).
    CHECK(CheckApiEntry(entry({0xCC, 0x48, 0x89}), true, Mods(), nullptr, f) && f.weight == 3 && f.detail.find("int3") != std::string::npos);
    {   // hot-patch style: short jump back into the padding, which holds a long jump into the cheat DLL
        const std::uint64_t pad = fn - 5;
        std::vector<std::uint8_t> padBytes = {0xE9, 0, 0, 0, 0};
        std::int32_t d = (std::int32_t)(0x7ff820000400ull - (pad + 5));
        std::memcpy(&padBytes[1], &d, 4);
        padBytes.resize(16, 0x90);
        ReadMem rd = [&](std::uint64_t a, void* out, std::size_t n) {
            if (a != pad) return false;
            std::memcpy(out, padBytes.data(), n);
            return true;
        };
        ApiEntry e = entry({0xEB, 0xF9});   // jmp -5 (to the padding)
        CHECK(CheckApiEntry(e, true, Mods(), rd, f) && f.detail.find("cheat.dll") != std::string::npos);
        CHECK(!CheckApiEntry(e, true, Mods(), nullptr, f));   // can't read the padding: the short jump stays inside kernelbase, so nothing is said
    }
    CHECK(CheckApiEntries({entry(rel32(0x7ff820000200ull)), entry({0x90})}, true, Mods(), nullptr).size() == 1);
    CHECK(!CriticalApis().empty());
}

void TestImports() {
    RUN_SUITE("IAT: every import resolves into a trusted module");
    actest::TestPe t = Build();
    auto setIat = [&](std::uint32_t idx, std::uint64_t v) { std::memcpy(&t.mapped[t.iatRva + 8 * idx], &v, 8); };
    // Resolve the three imports: QPC and GetTickCount64 into kernelbase, MessageBoxW into ntdll (a forward).
    setIat(0, 0x7ff810000100ull); setIat(1, 0x7ff810000200ull);
    auto setUser = [&](std::uint64_t v) { std::memcpy(&t.mapped[t.iatRvas[1]], &v, 8); };
    setUser(0x7ff800000300ull);
    CHECK(CheckImports(t.mapped.data(), t.mapped.size(), Mods()).empty());
    setIat(1, 0x7ff820000050ull);   // hooked: into the cheat DLL
    auto f = CheckImports(t.mapped.data(), t.mapped.size(), Mods());
    CHECK(f.size() == 1 && f[0].check == "iat" && f[0].detail.find("KERNEL32.dll!GetTickCount64") != std::string::npos &&
          f[0].detail.find("cheat.dll") != std::string::npos);
    setIat(1, 0x7ff810000200ull);
    setUser(0x5555000000ull);   // into shellcode
    f = CheckImports(t.mapped.data(), t.mapped.size(), Mods());
    CHECK(f.size() == 1 && f[0].detail.find("USER32.dll!MessageBoxW") != std::string::npos && f[0].detail.find("outside any module") != std::string::npos);
    CHECK(CheckImports(t.file.data(), 10, Mods()).empty());
    auto none = actest::MakePe(MakeText(64), {}, {});
    CHECK(CheckImports(none.mapped.data(), none.mapped.size(), Mods()).empty());
}

void TestModuleRules() {
    RUN_SUITE("module rules: glob, parse, sign, choose");
    CHECK(GlobMatch("*cheat*.dll", "SuperCheatEngine64.DLL"));
    CHECK(GlobMatch("a?c.dll", "abc.dll") && !GlobMatch("a?c.dll", "abbc.dll"));
    CHECK(GlobMatch("*", "x") && GlobMatch("x*", "x") && !GlobMatch("x*y", "x"));
    CHECK(!GlobMatch("", "x") && GlobMatch("", ""));

    ModuleRules r;
    r.version = 4;
    r.block.push_back({"*cheat*.dll", "", "memory tool", 4});
    r.block.push_back({"", "Evil Vendor", "", 5});
    r.allow.push_back({"gameoverlayrenderer64.dll", "", "", 0});
    r.allow.push_back({"", "NVIDIA", "", 0});
    const ed25519::Seed seed = TestSeed();
    Sign(r, seed);
    CHECK(CheckSignature(r));
    const std::string json = ToJson(r);
    ModuleRules p;
    std::string err;
    CHECK(ParseModuleRules(json, p, &err));
    CHECK(p.version == 4 && p.block.size() == 2 && p.allow.size() == 2 && p.block[0].weight == 4 && p.block[1].weight == 5);
    CHECK(CheckSignature(p));
    { ModuleRules e = p; e.block[0].name = "*.dll"; CHECK(!CheckSignature(e)); }
    { ModuleRules e = p; e.version = 3; CHECK(!CheckSignature(e)); }   // no version rollback by editing the number
    CHECK(!ParseModuleRules("{\"format\":\"okayspace-modules/1\",\"version\":1,\"block\":[{}]}", p));          // matches nothing
    CHECK(!ParseModuleRules("{\"format\":\"okayspace-modules/1\",\"version\":1,\"block\":[{\"name\":\"a/b\"}]}", p));
    CHECK(!ParseModuleRules("{\"format\":\"okayspace-modules/1\",\"version\":1,\"block\":[{\"name\":\"a\",\"weight\":99}]}", p));
    CHECK(!ParseModuleRules("{\"format\":\"okayspace-modules/1\",\"version\":1,\"allow\":[{\"name\":\"a\",\"weight\":3}]}", p));
    CHECK(!ParseModuleRules("{\"format\":\"okayspace-modules/1\"}", p));
    CHECK(!ParseModuleRules("[", p));

    // Matching: block by name, by signer; allow only counts a VALID signature for signer rules.
    ModuleSigner none, evil{true, true, "Evil Vendor Ltd"}, nvidiaOk{true, true, "NVIDIA Corporation"}, nvidiaFake{true, false, "NVIDIA Corporation"};
    CHECK(BlockedBy(p, "C:\\x\\MyCheat.dll", none) != nullptr);
    CHECK(BlockedBy(p, "C:\\x\\fine.dll", none) == nullptr);
    CHECK(BlockedBy(p, "C:\\x\\fine.dll", evil) != nullptr);
    CHECK(AllowedBy(p, "C:/a/GameOverlayRenderer64.dll", none));
    CHECK(AllowedBy(p, "x.dll", nvidiaOk));
    CHECK(!AllowedBy(p, "x.dll", nvidiaFake));      // a self-signed cert with the right name allows nothing
    CHECK(!AllowedBy(p, "x.dll", none));

    // Choosing among files.
    ModuleRules v5 = p; v5.version = 5; Sign(v5, seed);
    ModuleRules v2 = p; v2.version = 2; Sign(v2, seed);
    ModuleRules otherKey = p; otherKey.version = 9; Sign(otherKey, TestSeed(50));
    TrustedKeySet ts; ts.required = true; ts.keys.push_back(ed25519::PublicKeyFromSeed(seed));
    ModuleRules got;
    std::vector<std::string> notes;
    CHECK(PickModuleRules({ToJson(v2), ToJson(v5), ToJson(otherKey), "garbage"}, ts, 0, got, &notes));
    CHECK(got.version == 5 && notes.size() >= 2);
    CHECK(!PickModuleRules({ToJson(v2)}, ts, 3, got));   // older than one already seen
    CHECK(!PickModuleRules({ToJson(otherKey)}, ts, 0, got));
    TrustedKeySet dev;
    CHECK(PickModuleRules({ToJson(otherKey), ToJson(v2)}, dev, 0, got) && got.version == 9);   // no keys required: telemetry
    ModuleRules unsigned_ = p; unsigned_.hasSignature = false;
    CHECK(!PickModuleRules({ToJson(unsigned_)}, ts, 0, got));
}

void TestHints() {
    RUN_SUITE("debugger / hypervisor hints");
    HintProbes h;
    CHECK(CheckHints(h).empty());
    h.debuggerApi = [] { return false; };
    h.pebBeingDebugged = [] { return false; };
    h.ntGlobalFlag = [] { return false; };
    h.debugPort = [] { return false; };
    CHECK(CheckHints(h).empty());
    h.pebBeingDebugged = [] { return true; };   // the PEB says yes, the (hooked?) API says no
    auto f = CheckHints(h);
    CHECK(f.size() == 1 && f[0].weight == 5 && f[0].detail.find("hooked") != std::string::npos);
    h.debuggerApi = [] { return true; };       // both say yes: the guard's own check reports it, no mismatch
    CHECK(CheckHints(h).empty());
    h.ntGlobalFlag = [] { return true; };
    h.debugPort = [] { return true; };
    h.hwBreakpoints = [] { return true; };
    f = CheckHints(h);
    CHECK(f.size() == 3);
    for (const Finding& x : f) CHECK(x.weight >= 3 && x.weight <= kMaxHardeningWeight);
    HintProbes v;
    v.hypervisor = [] { return true; };
    v.hypervisorVendor = [] { return std::string("KVMKVMKVM"); };
    f = CheckHints(v);
    CHECK(f.size() == 1 && f[0].weight == 0 && f[0].detail.find("KVMKVMKVM") != std::string::npos);
}

// Three clocks that tick together; `qr`, `tr`, `sr` are each clock's rate against real time.
struct ClockSim {
    ClockCrossCheck cc;
    double t = 0, qr, tr, sr;
    ClockSim(double q, double tk, double s) : qr(q), tr(tk), sr(s) {}
    std::vector<Finding> Run(int seconds, bool server) {
        std::vector<Finding> all;
        for (int i = 0; i < seconds; ++i) {
            t += 1000;
            auto f = cc.AddLocal(5000 + t * qr, 9000 + t * tr);
            all.insert(all.end(), f.begin(), f.end());
            if (server && i % 2 == 0) {
                f = cc.AddServer(5000 + t * qr, 9000 + t * tr, 777000 + t * sr);
                all.insert(all.end(), f.begin(), f.end());
            }
        }
        return all;
    }
};

void TestClocks() {
    RUN_SUITE("clock cross-check: QPC vs GetTickCount64 vs server");
    CHECK(ClockSim(1, 1, 1).Run(120, true).empty());
    CHECK(ClockSim(1.02, 1, 1).Run(120, true).empty());      // inside 5%
    {   // QPC bent, tick and server agree: named
        auto f = ClockSim(2.0, 1, 1).Run(120, true);
        CHECK(f.size() == 1 && f[0].weight == 5 && f[0].detail.find("QueryPerformanceCounter") != std::string::npos);
    }
    {   // tick bent
        auto f = ClockSim(1, 0.5, 1).Run(120, true);
        CHECK(f.size() == 1 && f[0].detail.find("GetTickCount64") != std::string::npos && f[0].detail.find("tick clock") != std::string::npos);
    }
    {   // both local clocks scaled together, server is real
        auto f = ClockSim(3, 3, 1).Run(120, true);
        CHECK(f.size() == 1 && f[0].detail.find("both local clocks") != std::string::npos);
    }
    {   // no server: only "they disagree"
        auto f = ClockSim(2, 1, 1).Run(120, false);
        CHECK(f.size() == 1 && f[0].weight == 3 && f[0].detail.find("no server time") != std::string::npos);
    }
    {   // a single step of the QPC (sleep/resume) is not flagged
        ClockCrossCheck cc;
        std::vector<Finding> all;
        double t = 0;
        for (int i = 0; i < 120; ++i) {
            t += 1000;
            const double q = 5000 + t + (i >= 40 ? 30000 : 0);   // +30 s jump once
            auto f = cc.AddLocal(q, 9000 + t); all.insert(all.end(), f.begin(), f.end());
            f = cc.AddServer(q, 9000 + t, 777000 + t); all.insert(all.end(), f.begin(), f.end());
        }
        CHECK(all.empty());
    }
    {   // network jitter on the server samples doesn't matter inside the slack
        ClockCrossCheck cc;
        std::mt19937 r(3);
        std::vector<Finding> all;
        double t = 0;
        for (int i = 0; i < 300; ++i) {
            t += 1000;
            const double jitter = (double)(r() % 80) - 40;
            auto f = cc.AddServer(5000 + t, 9000 + t, 777000 + t + jitter);
            all.insert(all.end(), f.begin(), f.end());
        }
        CHECK(all.empty());
    }
    RateChannel ch;
    CHECK(!ch.Feed(0, 0) && !ch.Feed(1000, 1000));   // below one window: nothing yet
}

void TestScheduler() {
    RUN_SUITE("Hardening: the scheduler, weights, dedupe, facts");
    int codeCalls = 0, passes = 0;
    HardeningProbes p;
    p.buildId = "b1";
    p.codeStep = [&](std::size_t) {
        ++codeCalls;
        if (codeCalls == 3) ++passes;
        std::vector<Finding> f;
        if (codeCalls == 2) f.push_back({"code", "section .text of game.exe was modified in memory", 6});
        if (codeCalls == 2 || codeCalls == 4) f.push_back({"code", "section .text of game.exe was modified in memory", 6});   // dup
        return f;
    };
    p.codePasses = [&] { return passes; };
    p.modules = [] { return Mods(); };
    p.apis = [] {
        ApiEntry e; e.name = "k32!QueryPerformanceCounter"; e.addr = 0x7ff810001000ull;
        std::int32_t d = (std::int32_t)(0x7ff820000200ull - (e.addr + 5));
        e.bytes = {0xE9, 0, 0, 0, 0}; std::memcpy(&e.bytes[1], &d, 4); e.bytes.resize(16, 0x90);
        return std::vector<ApiEntry>{e};
    };
    p.hints.debugPort = [] { return true; };
    p.loadedModules = [] { return std::vector<std::string>{"C:\\Windows\\System32\\ntdll.dll", "C:\\Temp\\SuperCheat.dll"}; };
    p.haveRules = true;
    p.rules.block.push_back({"*cheat*", "", "tool", 99});   // the weight is clamped
    p.rules.allow.push_back({"overlay.dll", "", "", 0});
    Hardening h(std::move(p));
    std::vector<Finding> got;
    for (std::uint64_t now = 0; now < 2000; now += 100) { auto f = h.Tick(now); got.insert(got.end(), f.begin(), f.end()); }
    bool code = false, hook = false, dbg = false, blocked = false;
    int codeFindings = 0;
    for (const Finding& f : got) {
        CHECK(f.weight <= kMaxHardeningWeight);
        if (f.check == "code") { code = true; ++codeFindings; }
        if (f.check == "hook") hook = true;
        if (f.check == "debugger-hint") dbg = true;
        if (f.check == "module-blocked") { blocked = true; CHECK(f.weight == kMaxHardeningWeight && f.detail.find("supercheat") != std::string::npos); }
    }
    CHECK(code && hook && dbg && blocked);
    CHECK(codeFindings == 1);   // duplicates are one finding
    const IntegrityFacts& fx = h.Facts();
    CHECK(fx.buildId == "b1" && fx.codeModified && fx.apiHooked && fx.debuggerHint && fx.moduleBlocked && !fx.importsHooked && fx.codeScanned);
    CHECK(fx.weight == (6 + 5 + 4 + kMaxHardeningWeight));
    const int callsAfterPass = codeCalls;
    for (std::uint64_t now = 2000; now < 20000; now += 100) h.Tick(now);
    CHECK(codeCalls == callsAfterPass);   // resting between passes (30 s)
    for (std::uint64_t now = 20000; now < 40000; now += 100) h.Tick(now);
    CHECK(codeCalls > callsAfterPass);
}

void TestGuardIntegration() {
    RUN_SUITE("RuntimeGuard: hardening signals never end the game; moduleOk; score");
    GuardProbes gp;
    std::uint64_t now = 1000;
    gp.trustedMs = [&] { return now; };
    gp.modules = [] { return std::vector<std::string>{"C:\\Temp\\overlay.dll", "C:\\Temp\\other.dll"}; };
    gp.moduleOk = [](const std::string& p) { return p.find("overlay.dll") != std::string::npos; };
    gp.hardening = [](std::uint64_t) { return std::vector<Finding>{{"code", "patched", 6}, {"hook", "k32!X jumps to cheat.dll", 5}}; };
    std::vector<std::string> log;
    RuntimeGuard g(Mode::Report, gp, [&](const std::string& s) { log.push_back(s); });
    g.Tick();
    CHECK(g.Findings().size() == 3);   // other.dll + the two signals; overlay.dll was vouched for by the rules
    CHECK(g.Score() == 11);
    CHECK(!log.empty());
    CHECK(FormatFinding({"code", "patched", 6}) == "code(w6): patched");
    CHECK(FormatFinding({"module", "x"}) == "module: x");

    // Enforce: the old checks end the game, the weighted signals alone never do.
    GuardProbes only;
    only.trustedMs = [&] { return now; };
    only.hardening = [](std::uint64_t) { return std::vector<Finding>{{"code", "patched", 6}}; };
    RuntimeGuard e(Mode::Enforce, only);
    CHECK(!e.Tick());
    CHECK(e.Findings().size() == 1);
    GuardProbes old;
    old.trustedMs = [&] { return now; };
    old.modules = [] { return std::vector<std::string>{"C:\\Temp\\x.dll"}; };
    RuntimeGuard e2(Mode::Enforce, old);
    CHECK(e2.Tick());
    // Off: nothing at all runs.
    int ran = 0;
    GuardProbes off;
    off.trustedMs = [&] { return now; };
    off.hardening = [&](std::uint64_t) { ++ran; return std::vector<Finding>{}; };
    RuntimeGuard o(Mode::Off, off);
    o.Tick();
    CHECK(ran == 0);
}

void TestOptIn() {
    RUN_SUITE("opt-in: a game with no baseline, rules or env var is unchanged");
    namespace fs2 = std::filesystem;
    const fs2::path root = fs2::temp_directory_path() / "okayac_harden_test";
    fs2::remove_all(root);
    fs2::create_directories(root / "Data");
    TrustedKeySet none;
    ModuleAllowlist allow;
    CHECK(!SetupHardening(root, root / "save", Mode::Report, none, allow, "").enabled);      // not opted in
    CHECK(!SetupHardening(root, root / "save", Mode::Off, none, allow, "1").enabled);        // anti-cheat off wins
    CHECK(SetupHardening(root, root / "save", Mode::Report, none, allow, "1").enabled);      // env var
    {   // a rules file turns it on, and an unsigned one is only usable where no keys are required
        ModuleRules r; r.version = 1; r.block.push_back({"*bad*", "", "", 4});
        fs2::create_directories(root / ".anticheat");
        std::ofstream(root / ".anticheat" / "modules.json") << ToJson(r);
        HardeningSetup s = SetupHardening(root, root / "save", Mode::Report, none, allow, "");
        CHECK(s.enabled && s.hardening);
        TrustedKeySet req; req.required = true; req.keys.push_back(ed25519::PublicKeyFromSeed(TestSeed()));
        bool logged = false;
        s = SetupHardening(root, root / "save", Mode::Report, req, allow, "");
        for (const std::string& n : s.notes) if (n.find("not signed by a trusted key") != std::string::npos) logged = true;
        CHECK(s.enabled && logged);
    }
    {   // a baseline file likewise; a corrupt one is ignored with a note
        std::ofstream(root / "Data" / "codebaseline.json") << "{not json";
        HardeningSetup s = SetupHardening(root, root / "save", Mode::Report, none, allow, "");
        bool noted = false;
        for (const std::string& n : s.notes) if (n.find("code baseline ignored") != std::string::npos) noted = true;
        CHECK(s.enabled && noted);
    }
    fs2::remove_all(root);
}
} // namespace

int main() {
    TestPeParse();
    TestHashRelocAware();
    TestHasherSteps();
    TestBaselineFile();
    TestScanner();
    TestHooks();
    TestImports();
    TestModuleRules();
    TestHints();
    TestClocks();
    TestScheduler();
    TestGuardIntegration();
    TestOptIn();
    TEST_MAIN_RESULT();
}
