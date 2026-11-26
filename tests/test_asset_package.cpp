// Asset packages (okay/Core/AssetPackage.hpp, docs/engine/asset_packages.md): the hash, ids and collisions, round trips,
// byte-identical output, every kind of damage to a package (truncation, flipped bytes, crafted fields with a resealed
// trailer, random mutation), patches over a base, the manifest, mounting a data folder, the materialised view, packing
// an exported data folder with its dependencies, async reads, and the AssetManager mount.
//
// The fuzz part (FuzzSweep) needs only AssetPackage.cpp, ImageWrite.cpp and Jobs.cpp, so it can also be built on its
// own with -fsanitize=address,undefined (OKAY_ASSETPKG_STANDALONE: the AssetManager and PackFolder parts are left out).
#include "test_framework.hpp"
#include "okay/Core/AssetPackage.hpp"
#include "okay/Core/Jobs.hpp"
#ifndef OKAY_ASSETPKG_STANDALONE
#include "okay/Core/AssetManager.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::assetpkg;
namespace fs = std::filesystem;

namespace {

std::vector<std::uint8_t> Bytes(const std::string& s) { return std::vector<std::uint8_t>(s.begin(), s.end()); }
std::string Text(const std::vector<std::uint8_t>& b) { return std::string(b.begin(), b.end()); }

struct Rng {   // a fixed generator so every run fuzzes the same inputs
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 2 + 1) {}
    std::uint64_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    std::size_t Below(std::size_t n) { return n ? (std::size_t)(Next() % n) : 0; }
};

std::vector<std::uint8_t> Compressible(std::size_t n) {
    std::vector<std::uint8_t> v(n);
    const char* words = "okayspace asset package round trip ";
    for (std::size_t i = 0; i < n; ++i) v[i] = (std::uint8_t)words[(i * 7 / 5) % 35];
    return v;
}
std::vector<std::uint8_t> Random(std::size_t n, std::uint64_t seed) {
    Rng r(seed);
    std::vector<std::uint8_t> v(n);
    for (auto& b : v) b = (std::uint8_t)r.Next();
    return v;
}

std::uint64_t Get64(const std::vector<std::uint8_t>& b, std::size_t at) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | b[at + (std::size_t)i];
    return v;
}
std::uint32_t Get32(const std::vector<std::uint8_t>& b, std::size_t at) { return (std::uint32_t)Get64(b, at) ; }
void Put64(std::vector<std::uint8_t>& b, std::size_t at, std::uint64_t v) { for (int i = 0; i < 8; ++i) b[at + (std::size_t)i] = (std::uint8_t)(v >> (8 * i)); }
void Put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) b[at + (std::size_t)i] = (std::uint8_t)(v >> (8 * i)); }
void Put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t v) { b[at] = (std::uint8_t)v; b[at + 1] = (std::uint8_t)(v >> 8); }

// Recompute the trailer's metadata hash after a test edited the header or the table, so the reader's own field checks
// (not just the hash) are what refuses the package. `tocOffset` is the good package's.
void Reseal(std::vector<std::uint8_t>& b, std::uint64_t tocOffset) {
    Hasher h;
    h.Update(b.data(), kHeaderBytes);
    h.Update(b.data() + tocOffset, b.size() - kTrailerBytes - (std::size_t)tocOffset);
    Put64(b, b.size() - kTrailerBytes + 16, h.Final());
}

// A small package used by the damage tests: a stored entry, a compressed one, an empty one, dependencies.
std::vector<std::uint8_t> SamplePackage() {
    PackageWriter w;
    w.Add("game.okayscene", Bytes("okayscene 1\nobject Cube\n  mesh \"Assets/cube.obj\"\nend\n"), {"Assets/cube.obj", "Assets/wood.okaymat"});
    w.Add("Assets/cube.obj", Compressible(3000), {"Assets/wood.okaymat"});
    w.Add("Assets/wood.okaymat", Bytes("texture Assets/wood.png\n"), {"Assets/wood.png"});
    w.Add("Assets/wood.png", Random(200, 3));
    w.Add("Assets/empty.txt", {});
    std::vector<std::uint8_t> out;
    std::string why;
    w.WriteToMemory(out, {}, nullptr, &why);
    return out;
}

// Open, then read everything and verify: must never crash whatever the bytes are. True when it all worked.
bool OpenAndReadAll(const std::vector<std::uint8_t>& bytes, std::string& why) {
    auto p = Package::OpenMemory(bytes, why);
    if (!p) return false;
    bool all = true;
    for (const Entry& e : p->Entries()) {
        std::vector<std::uint8_t> out;
        std::string w;
        if (e.method != Method::Removed && !p->Read(e, out, w)) { all = false; why = w; }
        (void)p->NameOf(e);
        (void)p->DependenciesOf(e);
    }
    std::string v;
    if (!p->VerifyAll(v)) { all = false; why = v; }
    return all;
}

fs::path TempDir(const std::string& name) {
    fs::path d = fs::temp_directory_path() / ("okay_assetpkg_test_" + name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}
void WriteFile(const fs::path& p, const std::vector<std::uint8_t>& b) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write((const char*)b.data(), (std::streamsize)b.size());
}
std::vector<std::uint8_t> ReadFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void TestHash() {
    RUN_SUITE("asset_package: Hash64");
    // Pinned values: the format depends on them, so a change here is a format change (bump kVersion).
    CHECK(Hash64("", 0) == 0xebfac749bfc4be10ull);
    CHECK(Hash64("abc", 0) == 0xf53f739602111699ull);
    CHECK(Hash64(std::string(100, 'x'), 0) == 0x9c983b8a10dee49dull);
    CHECK(IdOf("Assets/a.png") == 0x2c9009a23905e134ull);

    // Streaming in any split gives the one-shot value, for every length around the 8- and 32-byte steps.
    const std::vector<std::uint8_t> data = Random(300, 11);
    bool same = true;
    for (std::size_t n = 0; n <= data.size(); ++n) {
        const std::uint64_t one = Hash64(data.data(), n, 5);
        for (std::size_t cut : {std::size_t(0), std::size_t(1), n / 3, n / 2, n > 7 ? n - 7 : 0, n}) {
            if (cut > n) continue;
            Hasher h(5);
            h.Update(data.data(), cut);
            h.Update(data.data() + cut, n - cut);
            if (h.Final() != one) same = false;
        }
        Hasher bytewise(5);
        for (std::size_t i = 0; i < n; ++i) bytewise.Update(data.data() + i, 1);
        if (bytewise.Final() != one) same = false;
    }
    CHECK(same);
    CHECK(Hash64("abc") != Hash64(""));   // a string literal is hashed as text, never as (pointer, size)
    CHECK(Hash64("abc", 1) != Hash64("abc", 0));
    CHECK(Hash64("abc", 3) != Hash64("abd", 3));
    CHECK(Hash64(std::string(32, '\0')) != Hash64(std::string(33, '\0')));   // the length counts

    // Avalanche: flipping one input bit flips about half of the 64 output bits, on short and long inputs.
    for (std::size_t len : {std::size_t(8), std::size_t(20), std::size_t(64), std::size_t(200)}) {
        double total = 0; int flips = 0, worst = 64;
        for (int sample = 0; sample < 20; ++sample) {
            std::vector<std::uint8_t> in = Random(len, 100 + (std::uint64_t)sample);
            const std::uint64_t base = Hash64(in.data(), in.size());
            for (std::size_t bit = 0; bit < len * 8; ++bit) {
                in[bit / 8] ^= (std::uint8_t)(1u << (bit % 8));
                const std::uint64_t x = Hash64(in.data(), in.size()) ^ base;
                in[bit / 8] ^= (std::uint8_t)(1u << (bit % 8));
                int c = 0;
                for (std::uint64_t v = x; v; v &= v - 1) ++c;
                total += c; ++flips; worst = std::min(worst, c);
            }
        }
        const double mean = total / flips;
        CHECK(mean > 31.0 && mean < 33.0);
        CHECK(worst >= 12);
    }

    // Ids of many similar names: no collision, and the low bits spread evenly (each of 256 buckets near 256).
    std::set<AssetId> ids;
    int buckets[256] = {};
    for (int i = 0; i < 65536; ++i) {
        const AssetId id = IdOf("Assets/Textures/tile_" + std::to_string(i) + ".png");
        ids.insert(id);
        ++buckets[id & 255];
    }
    CHECK(ids.size() == 65536);
    int lo = 1 << 30, hi = 0;
    for (int b : buckets) { lo = std::min(lo, b); hi = std::max(hi, b); }
    CHECK(lo > 180 && hi < 340);
}

void TestNames() {
    RUN_SUITE("asset_package: names and ids");
    CHECK(NormalizeName("Assets\\Foo//bar.png") == "Assets/Foo/bar.png");
    CHECK(NormalizeName("./Assets/./a.png") == "Assets/a.png");
    CHECK(NormalizeName("/Assets/a.png") == "Assets/a.png");
    CHECK(IdOf("Assets/A.PNG") == IdOf("assets/a.png"));
    CHECK(IdOf("Assets\\a.png") == IdOf("Assets/a.png"));
    CHECK(IdOf("Assets/a.png") != IdOf("Assets/b.png"));
    CHECK(IdOf("x") != 0);
    CHECK(Hex64(0x0123456789abcdefull) == "0123456789abcdef");
    std::string why;
    CHECK(IsValidName("Assets/a.png"));
    CHECK(!IsValidName("", &why));
    CHECK(!IsValidName("../a.png", &why));
    CHECK(!IsValidName("Assets/../../a.png", &why));
    CHECK(!IsValidName("C:/a.png", &why));
    CHECK(!IsValidName("Assets\\a.png", &why));     // not normalised
    CHECK(!IsValidName("Assets/con.txt", &why));    // a Windows device name
    CHECK(!IsValidName("Assets/a.", &why));
    CHECK(!IsValidName(std::string(1100, 'a'), &why));
    CHECK(!why.empty());
}

void TestRoundTrip() {
    RUN_SUITE("asset_package: round trip");
    const std::vector<std::uint8_t> big = Compressible(1 << 20), noise = Random(256 << 10, 7);
    PackageWriter w;
    std::string why;
    CHECK(w.Add("game.okayscene", Bytes("scene text"), {"Assets/big.bin", "Assets/noise.bin"}, 3, &why));
    CHECK(w.Add("Assets/big.bin", big, {}, 0, &why));
    CHECK(w.Add("Assets/noise.bin", noise, {}, 0, &why));
    CHECK(w.Add("Assets/empty.txt", {}, {}, 0, &why));
    CHECK(w.Count() == 4);
    CHECK(w.Has("assets/BIG.bin"));
    CHECK(!w.AddRemoval("Assets/x.png", &why) || true);   // a removal is allowed to be collected; a base package refuses it at Write
    PackageWriter base = w;
    std::vector<std::uint8_t> pkg;
    WriteStats st;
    WriteOptions wo;
    CHECK(!w.WriteToMemory(pkg, wo, &st, &why));          // a base package with a removal is refused
    CHECK(why.find("removal") != std::string::npos);

    PackageWriter w2;
    w2.Add("game.okayscene", Bytes("scene text"), {"Assets/big.bin", "Assets/noise.bin", "Assets/missing.png"}, 3);
    w2.Add("Assets/big.bin", big);
    w2.Add("Assets/noise.bin", noise);
    w2.Add("Assets/empty.txt", {});
    CHECK(w2.WriteToMemory(pkg, wo, &st, &why));
    CHECK(st.entries == 4);
    CHECK(st.compressed >= 1);
    CHECK(st.bytesOut == pkg.size());
    CHECK(pkg.size() < big.size() / 4 + noise.size() + 4096);   // the big repetitive file shrank, the noise did not grow much
    auto p = Package::OpenMemory(pkg, why);
    CHECK(p != nullptr);
    if (!p) { std::cerr << "  open: " << why << "\n"; return; }
    CHECK(p->HasNames());
    CHECK(!p->IsPatch());
    CHECK(p->Entries().size() == 4);
    for (std::size_t i = 1; i < p->Entries().size(); ++i) CHECK(p->Entries()[i - 1].id < p->Entries()[i].id);
    std::vector<std::uint8_t> out;
    const Entry* e = p->Find("Assets/big.bin");
    CHECK(e && e->method == Method::Deflate && e->size == big.size());
    CHECK(e && p->Read(*e, out, why) && out == big);
    CHECK(e && e->offset % 16 == 0);
    e = p->Find("ASSETS/NOISE.BIN");   // ids ignore letter case
    CHECK(e && e->method == Method::Stored);
    CHECK(e && p->Read(*e, out, why) && out == noise);
    e = p->Find("Assets/empty.txt");
    CHECK(e && p->Read(*e, out, why) && out.empty());
    e = p->Find("game.okayscene");
    CHECK(e && e->kind == 3);
    CHECK(e && p->NameOf(*e) == "game.okayscene");
    CHECK(e && p->DependenciesOf(*e).size() == 3);   // a dependency on a file this package lacks is kept (a patch may add it)
    CHECK(p->Find("Assets/none.png") == nullptr);
    CHECK(p->VerifyAll(why));

    // The same through a file.
    const fs::path dir = TempDir("roundtrip");
    const std::string path = (dir / "game.okaypkg").string();
    CHECK(w2.Write(path, wo, &st, &why));
    CHECK(!fs::exists(path + ".part"));
    auto pf = Package::OpenFile(path, why);
    CHECK(pf != nullptr);
    if (pf) {
        e = pf->Find("Assets/big.bin");
        CHECK(e && pf->Read(*e, out, why) && out == big);
        CHECK(pf->VerifyAll(why));
    }
    CHECK(ReadFile(path) == pkg);
    CHECK(Package::OpenFile((dir / "nothing.okaypkg").string(), why) == nullptr);
    CHECK(!why.empty());

    // Entries read from files when the package is written.
    WriteFile(dir / "src" / "a.bin", big);
    PackageWriter wf;
    CHECK(wf.AddFile("Assets/a.bin", (dir / "src" / "a.bin").string(), {}, 0, &why));
    CHECK(wf.AddFile("Assets/gone.bin", (dir / "src" / "gone.bin").string(), {}, 0, &why));
    std::vector<std::uint8_t> pf2;
    CHECK(!wf.WriteToMemory(pf2, wo, nullptr, &why));   // a missing source file fails the write, with its name
    CHECK(why.find("gone.bin") != std::string::npos);

    // Names stripped: lookups by id still work, names are gone.
    WriteOptions strip; strip.keepNames = false;
    std::vector<std::uint8_t> sp;
    CHECK(w2.WriteToMemory(sp, strip, nullptr, &why));
    auto ps = Package::OpenMemory(sp, why);
    CHECK(ps && !ps->HasNames());
    if (ps) {
        e = ps->Find("Assets/big.bin");
        CHECK(e && ps->NameOf(*e).empty());
        CHECK(e && ps->Read(*e, out, why) && out == big);
    }
    // No compression and a 4096 alignment.
    WriteOptions raw; raw.compress = false; raw.alignment = 4096;
    std::vector<std::uint8_t> rp;
    CHECK(w2.WriteToMemory(rp, raw, &st, &why));
    CHECK(st.compressed == 0);
    auto pr = Package::OpenMemory(rp, why);
    CHECK(pr != nullptr);
    if (pr) for (const Entry& x : pr->Entries()) CHECK(x.method == Method::Stored && x.offset % 4096 == 0);
    WriteOptions bad; bad.alignment = 3;
    CHECK(!w2.WriteToMemory(rp, bad, nullptr, &why));
    fs::remove_all(dir);
}

void TestDeterminism() {
    RUN_SUITE("asset_package: same input, same bytes");
    auto fill = [](PackageWriter& w, bool reverse) {
        std::vector<int> order;
        for (int i = 0; i < 40; ++i) order.push_back(i);
        if (reverse) std::reverse(order.begin(), order.end());
        for (int i : order)
            w.Add("Assets/f" + std::to_string(i) + ".dat", i % 2 ? Compressible(1000 + (std::size_t)i * 300) : Random(500 + (std::size_t)i, (std::uint64_t)i),
                  {"Assets/f" + std::to_string((i + 1) % 40) + ".dat"});
    };
    PackageWriter a, b;
    fill(a, false);
    fill(b, true);
    std::vector<std::uint8_t> pa, pb, pc;
    std::string why;
    CHECK(a.WriteToMemory(pa, {}, nullptr, &why));
    CHECK(b.WriteToMemory(pb, {}, nullptr, &why));
    CHECK(pa == pb);
    JobSystem jobs(2);
    WriteOptions par; par.jobs = &jobs;
    CHECK(a.WriteToMemory(pc, par, nullptr, &why));
    CHECK(pa == pc);   // compressing on workers changes nothing
}

void TestCollisions() {
    RUN_SUITE("asset_package: id collisions are refused with both names");
    PackageWriter w;
    std::string why;
    CHECK(w.Add("Assets/Rock.png", Bytes("1"), {}, 0, &why));
    CHECK(!w.Add("assets/rock.PNG", Bytes("2"), {}, 0, &why));   // same id: a different name for the same file on Windows
    CHECK(why.find("Assets/Rock.png") != std::string::npos && why.find("assets/rock.PNG") != std::string::npos);
    CHECK(!w.Add("Assets/Rock.png", Bytes("3"), {}, 0, &why));   // the same name twice
    CHECK(!w.Add("../evil.png", Bytes("x"), {}, 0, &why));
    CHECK(!w.Add("Assets\\a.png", Bytes("x"), {}, 0, &why));      // names are stored normalised
    CHECK(w.Count() == 1);

    // A package whose name and id disagree (a collision or damage) is refused by the reader.
    std::vector<std::uint8_t> pkg = SamplePackage();
    const std::uint64_t toc = Get64(pkg, 48);
    std::vector<std::uint8_t> b = pkg;
    const std::size_t namesAt = (std::size_t)Get64(b, 56);
    b[namesAt] = (std::uint8_t)(b[namesAt] == 'z' ? 'y' : 'z');   // another letter: the name no longer gives the id
    Reseal(b, toc);
    CHECK(Package::OpenMemory(b, why) == nullptr);
    CHECK(why.find("id") != std::string::npos);
}

void TestCorruption() {
    RUN_SUITE("asset_package: damaged packages are refused, never a crash");
    const std::vector<std::uint8_t> good = SamplePackage();
    std::string why;
    CHECK(OpenAndReadAll(good, why));
    const std::uint64_t toc = Get64(good, 48);
    const std::uint64_t dataOff = Get64(good, 32), dataBytes = Get64(good, 40);

    // Every truncation.
    int refused = 0;
    for (std::size_t n = 0; n < good.size(); ++n) {
        std::vector<std::uint8_t> b(good.begin(), good.begin() + (std::ptrdiff_t)n);
        if (!Package::OpenMemory(b, why)) ++refused;
    }
    CHECK(refused == (int)good.size());
    // Bytes added at the end.
    { std::vector<std::uint8_t> b = good; b.push_back(0); CHECK(Package::OpenMemory(b, why) == nullptr); }

    // Every single byte flipped: outside the data the package does not open; inside it, the damaged entry does not read.
    int metaRefused = 0, metaBytes = 0, dataCaught = 0, dataBytesN = 0;
    for (std::size_t i = 0; i < good.size(); ++i) {
        std::vector<std::uint8_t> b = good;
        b[i] ^= 0xA5;
        // The data region and the trailer's data hash are checked when entries are read (and by VerifyAll), not at open.
        const bool inData = (i >= dataOff && i < dataOff + dataBytes) || i >= good.size() - 8;
        if (inData) { ++dataBytesN; if (!OpenAndReadAll(b, why)) ++dataCaught; }
        else { ++metaBytes; if (!Package::OpenMemory(b, why)) ++metaRefused; }
    }
    CHECK(metaRefused == metaBytes);
    CHECK(dataCaught == dataBytesN);   // padding bytes too: the data region hash covers them

    // Crafted fields with a valid trailer hash: the field checks themselves must refuse them.
    struct Case { const char* what; std::function<void(std::vector<std::uint8_t>&)> edit; };
    const std::size_t e0 = (std::size_t)toc;   // first table record
    std::vector<Case> cases = {
        {"magic", [](std::vector<std::uint8_t>& b) { b[0] = 'X'; }},
        {"newer version", [](std::vector<std::uint8_t>& b) { Put16(b, 8, 2); }},
        {"header size", [](std::vector<std::uint8_t>& b) { Put16(b, 10, 128); }},
        {"unknown flag", [](std::vector<std::uint8_t>& b) { Put32(b, 12, Get32(b, 12) | 0x100); }},
        {"entry count huge", [](std::vector<std::uint8_t>& b) { Put32(b, 16, 0xFFFFFFFFu); }},
        {"entry count +1", [](std::vector<std::uint8_t>& b) { Put32(b, 16, Get32(b, 16) + 1); }},
        {"record size", [](std::vector<std::uint8_t>& b) { Put32(b, 20, 32); }},
        {"alignment 3", [](std::vector<std::uint8_t>& b) { Put32(b, 24, 3); }},
        {"alignment 0", [](std::vector<std::uint8_t>& b) { Put32(b, 24, 0); }},
        {"reserved", [](std::vector<std::uint8_t>& b) { Put32(b, 28, 1); }},
        {"data offset overflow", [](std::vector<std::uint8_t>& b) { Put64(b, 32, ~0ull - 8); }},
        {"data bytes overflow", [](std::vector<std::uint8_t>& b) { Put64(b, 40, ~0ull); }},
        {"toc offset", [](std::vector<std::uint8_t>& b) { Put64(b, 48, Get64(b, 48) + 8); }},
        {"names bytes huge", [](std::vector<std::uint8_t>& b) { Put64(b, 64, 1ull << 62); }},
        {"deps bytes odd", [](std::vector<std::uint8_t>& b) { Put64(b, 80, Get64(b, 80) - 1); }},
        {"base hash on a base", [](std::vector<std::uint8_t>& b) { Put64(b, 88, 1); }},
        {"trailer size", [](std::vector<std::uint8_t>& b) { Put64(b, b.size() - 24, b.size() + 1); }},
        {"trailer magic", [](std::vector<std::uint8_t>& b) { b[b.size() - 32] = 'X'; }},
        {"entry offset past data", [e0](std::vector<std::uint8_t>& b) { Put64(b, e0 + 8, Get64(b, 32) + Get64(b, 40)); }},
        {"entry offset unaligned", [e0](std::vector<std::uint8_t>& b) { Put64(b, e0 + 8, Get64(b, e0 + 8) + 1); }},
        {"entry stored bytes huge", [e0](std::vector<std::uint8_t>& b) { Put64(b, e0 + 16, ~0ull); }},
        {"entry size huge", [e0](std::vector<std::uint8_t>& b) { Put64(b, e0 + 24, 1ull << 40); }},
        {"name past table", [e0](std::vector<std::uint8_t>& b) { Put32(b, e0 + 40, 0xFFFFFFF0u); }},
        {"name length 0", [e0](std::vector<std::uint8_t>& b) { Put32(b, e0 + 44, 0); }},
        {"deps past table", [e0](std::vector<std::uint8_t>& b) { Put32(b, e0 + 48, 0xFFFFFFF0u); Put32(b, e0 + 52, 0x20); }},
        {"deps count overflow", [e0](std::vector<std::uint8_t>& b) { Put32(b, e0 + 48, 1); Put32(b, e0 + 52, 0xFFFFFFFFu); }},
        {"unknown method", [e0](std::vector<std::uint8_t>& b) { b[e0 + 56] = 9; }},
        {"removal in a base", [e0](std::vector<std::uint8_t>& b) { b[e0 + 56] = 2; }},
        {"entry flags", [e0](std::vector<std::uint8_t>& b) { b[e0 + 58] = 1; }},
        {"entry reserved", [e0](std::vector<std::uint8_t>& b) { b[e0 + 60] = 1; }},
        {"ids out of order", [e0](std::vector<std::uint8_t>& b) {
            std::vector<std::uint8_t> r(b.begin() + (std::ptrdiff_t)e0, b.begin() + (std::ptrdiff_t)e0 + 64);
            std::copy(b.begin() + (std::ptrdiff_t)e0 + 64, b.begin() + (std::ptrdiff_t)e0 + 128, b.begin() + (std::ptrdiff_t)e0);
            std::copy(r.begin(), r.end(), b.begin() + (std::ptrdiff_t)e0 + 64); }},
        {"same id twice", [e0](std::vector<std::uint8_t>& b) { std::copy(b.begin() + (std::ptrdiff_t)e0, b.begin() + (std::ptrdiff_t)e0 + 64, b.begin() + (std::ptrdiff_t)e0 + 64); }},
    };
    for (const Case& c : cases) {
        std::vector<std::uint8_t> b = good;
        c.edit(b);
        Reseal(b, toc);
        const bool opened = Package::OpenMemory(b, why) != nullptr;
        if (opened) std::cerr << "  opened a package with a bad " << c.what << "\n";
        CHECK(!opened);
        CHECK(!why.empty());
    }
    // A stored entry whose size field disagrees with its stored bytes, and a compressed entry that unpacks to more
    // than it claims (a "bomb"): refused at open or at read, with no allocation beyond the claimed size.
    {
        auto p = Package::OpenMemory(good, why);
        const Entry* obj = p ? p->Find("Assets/cube.obj") : nullptr;
        CHECK(obj && obj->method == Method::Deflate);
        std::size_t rec = 0;
        for (std::size_t i = 0; p && i < p->Entries().size(); ++i) if (&p->Entries()[i] == obj) rec = (std::size_t)toc + i * 64;
        std::vector<std::uint8_t> b = good;
        Put64(b, rec + 24, 100);   // claims 100 bytes; unpacks to 3000
        Reseal(b, toc);
        std::string w;
        CHECK(!OpenAndReadAll(b, w));
        std::vector<std::uint8_t> c = good;
        const Entry* png = p ? p->Find("Assets/wood.png") : nullptr;
        for (std::size_t i = 0; p && i < p->Entries().size(); ++i) if (&p->Entries()[i] == png) rec = (std::size_t)toc + i * 64;
        Put64(c, rec + 24, Get64(c, rec + 24) + 1);   // stored, size != stored bytes
        Reseal(c, toc);
        CHECK(Package::OpenMemory(c, w) == nullptr);
    }
}

void FuzzSweep() {
    RUN_SUITE("asset_package: random mutation fuzz");
    // Three seeds: the sample base package, a patch with a removal, and a package without names at a 4096 alignment.
    std::vector<std::vector<std::uint8_t>> seeds = {SamplePackage()};
    {
        PackageWriter w;
        w.Add("Assets/a.txt", Compressible(500), {"Assets/b.txt"});
        w.AddRemoval("Assets/old.txt");
        std::vector<std::uint8_t> b;
        WriteOptions o; o.baseStackHash = 0x1234;
        w.WriteToMemory(b, o);
        seeds.push_back(b);
    }
    {
        PackageWriter w;
        w.Add("Assets/x.bin", Random(300, 5), {"Assets/y.bin"});
        w.Add("Assets/y.bin", Compressible(900));
        std::vector<std::uint8_t> b;
        WriteOptions o; o.keepNames = false; o.alignment = 4096;
        w.WriteToMemory(b, o);
        seeds.push_back(b);
    }
    for (const auto& sd : seeds) { std::string w; CHECK(OpenAndReadAll(sd, w)); }
    Rng r(42);
    int opened = 0, readOk = 0;
    const char* env = std::getenv("OKAY_FUZZ_ROUNDS");   // the ASan/UBSan run uses more
    const int rounds = env ? std::max(1, std::atoi(env)) : 30000;
    for (int i = 0; i < rounds; ++i) {
        const std::vector<std::uint8_t>& good = seeds[(std::size_t)i % seeds.size()];
        const std::uint64_t toc = Get64(good, 48);
        std::vector<std::uint8_t> b = good;
        const int kind = (int)r.Below(7);
        const int n = 1 + (int)r.Below(8);
        for (int k = 0; k < n; ++k) {
            std::size_t at = r.Below(b.size());
            if (kind == 6) at = (std::size_t)toc + r.Below(b.size() - (std::size_t)toc);   // aim at the tables
            if (kind == 0 || kind == 6) b[at] ^= (std::uint8_t)(1u << r.Below(8));
            else if (kind == 1) b[at] = (std::uint8_t)r.Next();
            else if (kind == 2 && at + 8 <= b.size()) Put64(b, at, r.Next() >> r.Below(64));
            else if (kind == 3 && at + 4 <= b.size()) Put32(b, at, (std::uint32_t)r.Next());
        }
        if (kind == 4) b.resize(r.Below(b.size()));
        if (kind == 5 && b.size() > 4) b.insert(b.begin() + (std::ptrdiff_t)r.Below(b.size()), (std::uint8_t)r.Next());
        if (r.Below(2) && b.size() == good.size()) Reseal(b, toc);   // half the time the trailer hash is fixed up, so the field checks run
        std::string why;
        auto p = Package::OpenMemory(b, why);
        if (!p) { if (why.empty()) { CHECK(!"a refusal must say why"); } continue; }
        ++opened;
        if (OpenAndReadAll(b, why)) ++readOk;
    }
    std::cout << "  fuzz: " << rounds << " mutated packages, " << opened << " opened, " << readOk << " fully read\n";
    CHECK(opened < rounds);
}

void TestPatches() {
    RUN_SUITE("asset_package: patches over a base");
    PackageWriter bw;
    bw.Add("game.okayscene", Bytes("scene v1"), {"Assets/a.txt"});
    bw.Add("Assets/a.txt", Bytes("a v1"));
    bw.Add("Assets/b.txt", Bytes("b v1"));
    bw.Add("Assets/c.txt", Bytes("c v1"));
    std::vector<std::uint8_t> basePkg;
    std::string why;
    CHECK(bw.WriteToMemory(basePkg, {}, nullptr, &why));
    auto base = Package::OpenMemory(basePkg, why);
    CHECK(base != nullptr);
    if (!base) return;

    PackageStack stack;
    CHECK(stack.Mount(base, why));
    CHECK(stack.StackHash() == base->Info().metaHash);
    const std::uint64_t h0 = stack.StackHash();

    const fs::path dir = TempDir("patch");
    WriteFile(dir / "a.txt", Bytes("a v1"));
    WriteFile(dir / "b.txt", Bytes("b v2"));
    WriteFile(dir / "d.txt", Bytes("d new"));
    WriteFile(dir / "scene", Bytes("scene v1"));
    std::vector<PatchSource> wanted = {
        {"game.okayscene", (dir / "scene").string(), {"Assets/a.txt", "Assets/d.txt"}, 0},   // same bytes, new dependency: changed
        {"Assets/a.txt", (dir / "a.txt").string(), {}, 0},
        {"Assets/b.txt", (dir / "b.txt").string(), {}, 0},
        {"Assets/d.txt", (dir / "d.txt").string(), {}, 0},
    };
    PackageWriter pw;
    std::size_t changed = 0;
    CHECK(MakePatch(stack, wanted, pw, &changed, why));
    CHECK(changed == 3);            // scene (dependencies), b (bytes), d (new); a is unchanged
    CHECK(pw.Count() == 4);         // + the removal of c
    WriteOptions po; po.baseStackHash = h0;
    std::vector<std::uint8_t> patchPkg;
    CHECK(pw.WriteToMemory(patchPkg, po, nullptr, &why));
    auto patch = Package::OpenMemory(patchPkg, why);
    CHECK(patch && patch->IsPatch());
    if (!patch) return;

    // A patch cannot be a base; a base cannot go on top; a patch for other packages is refused.
    PackageStack wrong;
    CHECK(!wrong.Mount(patch, why));
    CHECK(why.find("patch") != std::string::npos);
    CHECK(stack.Mount(patch, why));
    CHECK(stack.Layers() == 2);
    CHECK(stack.StackHash() != h0);
    CHECK(!stack.Mount(patch, why));   // made for the base alone, not for base + itself
    CHECK(!stack.Mount(base, why));

    std::vector<std::uint8_t> out;
    CHECK(stack.Read("Assets/a.txt", out, why) && Text(out) == "a v1");
    CHECK(stack.Read("Assets/b.txt", out, why) && Text(out) == "b v2");
    CHECK(!stack.Read("Assets/c.txt", out, why));
    CHECK(!stack.Contains("Assets/c.txt"));
    CHECK(stack.Read("Assets/d.txt", out, why) && Text(out) == "d new");
    PackageStack::Hit hit;
    CHECK(stack.Find(IdOf("Assets/a.txt"), hit) && hit.layer == 0);
    CHECK(stack.Find(IdOf("Assets/b.txt"), hit) && hit.layer == 1);
    CHECK(stack.Visible().size() == 4);
    CHECK(stack.NameOf(IdOf("Assets/d.txt")) == "Assets/d.txt");

    // Dependencies come first in the closure, each once.
    std::vector<AssetId> cl = stack.Closure(IdOf("game.okayscene"));
    CHECK(cl.size() == 3);
    CHECK(!cl.empty() && cl.back() == IdOf("game.okayscene"));

    // A second patch chains on the first.
    PackageWriter p2;
    p2.Add("Assets/a.txt", Bytes("a v3"));
    std::vector<std::uint8_t> p2b;
    WriteOptions o2; o2.baseStackHash = stack.StackHash();
    CHECK(p2.WriteToMemory(p2b, o2, nullptr, &why));
    auto patch2 = Package::OpenMemory(p2b, why);
    CHECK(patch2 && stack.Mount(patch2, why));
    CHECK(stack.Read("Assets/a.txt", out, why) && Text(out) == "a v3");
    fs::remove_all(dir);
}

void TestClosureCycles() {
    RUN_SUITE("asset_package: dependency closure with a cycle");
    PackageWriter w;
    w.Add("a", Bytes("a"), {"b"});
    w.Add("b", Bytes("b"), {"a", "c"});
    w.Add("c", Bytes("c"), {"c", "missing"});
    std::vector<std::uint8_t> pkg;
    std::string why;
    CHECK(w.WriteToMemory(pkg, {}, nullptr, &why));
    PackageStack s;
    auto p = Package::OpenMemory(pkg, why);
    CHECK(p && s.Mount(p, why));
    std::vector<AssetId> cl = s.Closure(IdOf("a"));
    CHECK(cl.size() == 3);
    CHECK(cl.size() == 3 && cl[0] == IdOf("c") && cl[2] == IdOf("a"));
    CHECK(s.Closure(IdOf("nothing")).empty());
}

void TestManifest() {
    RUN_SUITE("asset_package: packages.manifest");
    std::vector<ManifestItem> items = {{"game.okaypkg", 1234, 0xabcdefull}, {"patch-0001.okaypkg", 99, 0x1ull}};
    const std::string text = WriteManifest(items);
    std::vector<ManifestItem> back;
    std::string why;
    CHECK(ParseManifest(text, back, why));
    CHECK(back.size() == 2 && back[1].file == "patch-0001.okaypkg" && back[0].size == 1234 && back[0].hash == 0xabcdefull);
    for (const char* bad : {"", "okaypkg-manifest 2\n", "okaypkg-manifest 1\ngame.okaypkg 12\n", "okaypkg-manifest 1\n../x.okaypkg 1 0000000000000001\n",
                            "okaypkg-manifest 1\ngame.zip 1 0000000000000001\n", "okaypkg-manifest 1\ngame.okaypkg -1 0000000000000001\n",
                            "okaypkg-manifest 1\ngame.okaypkg 1 xyz\n", "okaypkg-manifest 1\ngame.okaypkg 1 0000000000000001\ngame.okaypkg 1 0000000000000001\n",
                            "okaypkg-manifest 1\ngame.okaypkg 99999999999999999999999 0000000000000001\n"}) {
        CHECK(!ParseManifest(bad, back, why));
    }
    std::vector<ManifestItem> wanted = items;
    wanted[1].hash = 2;
    wanted.push_back({"patch-0002.okaypkg", 5, 5});
    std::vector<ManifestItem> ch = ChangedPackages(items, wanted);
    CHECK(ch.size() == 2 && ch[0].file == "patch-0001.okaypkg" && ch[1].file == "patch-0002.okaypkg");
    CHECK(ChangedPackages(items, items).empty());
}

#ifndef OKAY_ASSETPKG_STANDALONE
void TestFolderAndView() {
    RUN_SUITE("asset_package: a data folder, mounted and materialised");
    const fs::path dir = TempDir("folder");
    const fs::path data = dir / "Data", view = dir / "view";
    PackageWriter w;
    w.Add("game.okayscene", Bytes("scene"));
    w.Add("Assets/a.txt", Bytes("a v1"));
    w.Add("Assets/sub/c.txt", Bytes("c v1"));
    std::string why;
    WriteStats st;
    CHECK(w.Write((data / "game.okaypkg").string(), {}, &st, &why));
    std::uint64_t h = 0, sz = 0;
    CHECK(HashFile((data / "game.okaypkg").string(), h, sz, why));
    CHECK(sz == st.bytesOut);
    {
        std::ofstream m(data / "packages.manifest", std::ios::binary);
        m << WriteManifest({{"game.okaypkg", sz, h}});
    }
    WriteFile(data / "content.okayconfig", Bytes("url="));   // a loose file next to the package

    PackageStack s;
    CHECK(MountFolder(data.string(), s, why));
    CHECK(s.Layers() == 1);
    MaterializeStats ms;
    CHECK(Materialize(s, data.string(), view.string(), &ms, why));
    CHECK(ms.written == 3 && ms.loose == 1);
    CHECK(Text(ReadFile(view / "Assets" / "sub" / "c.txt")) == "c v1");
    CHECK(Text(ReadFile(view / "content.okayconfig")) == "url=");
    CHECK(Materialize(s, data.string(), view.string(), &ms, why));
    CHECK(ms.written == 0 && ms.kept == 3);   // a second start writes nothing
    WriteFile(view / "Assets" / "a.txt", Bytes("edited!"));   // the view was changed by hand: put back
    CHECK(Materialize(s, data.string(), view.string(), &ms, why));
    CHECK(ms.written == 1 && Text(ReadFile(view / "Assets" / "a.txt")) == "a v1");

    // A patch: a changed, c removed, d added; the manifest lists both packages.
    PackageWriter pw;
    pw.Add("Assets/a.txt", Bytes("a v2"));
    pw.Add("Assets/d.txt", Bytes("d"));
    pw.AddRemoval("Assets/sub/c.txt");
    WriteOptions po; po.baseStackHash = s.StackHash();
    CHECK(pw.Write((data / "patch-0001.okaypkg").string(), po, nullptr, &why));
    std::uint64_t h2 = 0, s2 = 0;
    CHECK(HashFile((data / "patch-0001.okaypkg").string(), h2, s2, why));
    { std::ofstream m(data / "packages.manifest", std::ios::binary); m << WriteManifest({{"game.okaypkg", sz, h}, {"patch-0001.okaypkg", s2, h2}}); }
    PackageStack s3;
    CHECK(MountFolder(data.string(), s3, why));
    CHECK(s3.Layers() == 2);
    CHECK(Materialize(s3, data.string(), view.string(), &ms, why));
    CHECK(ms.written == 2 && ms.removed == 1 && ms.kept == 1);
    CHECK(Text(ReadFile(view / "Assets" / "a.txt")) == "a v2");
    CHECK(!fs::exists(view / "Assets" / "sub" / "c.txt"));

    // A manifest that disagrees with the file on disk, or names a missing package, is refused.
    { std::ofstream m(data / "packages.manifest", std::ios::binary); m << WriteManifest({{"game.okaypkg", sz + 1, h}}); }
    PackageStack s4;
    CHECK(!MountFolder(data.string(), s4, why));
    { std::ofstream m(data / "packages.manifest", std::ios::binary); m << WriteManifest({{"game.okaypkg", sz, h}, {"patch-0009.okaypkg", 1, 1}}); }
    CHECK(!MountFolder(data.string(), s4, why));
    CHECK(why.find("patch-0009") != std::string::npos);
    // No manifest: game.okaypkg alone. No package at all: an empty stack, and that is fine.
    fs::remove(data / "packages.manifest");
    PackageStack s5;
    CHECK(MountFolder(data.string(), s5, why) && s5.Layers() == 1);
    PackageStack s6;
    CHECK(MountFolder((dir / "nothing").string(), s6, why) && s6.Layers() == 0);

    // A package without names cannot be materialised (there is nothing to call the files).
    PackageWriter nw; nw.Add("Assets/a.txt", Bytes("x"));
    std::vector<std::uint8_t> nb;
    WriteOptions strip; strip.keepNames = false;
    CHECK(nw.WriteToMemory(nb, strip, nullptr, &why));
    PackageStack s7;
    auto np = Package::OpenMemory(nb, why);
    CHECK(np && s7.Mount(np, why));
    CHECK(!Materialize(s7, data.string(), (dir / "view2").string(), nullptr, why));
    fs::remove_all(dir);
}
#endif

void TestAsync() {
    RUN_SUITE("asset_package: reads on the job system");
    PackageWriter w;
    for (int i = 0; i < 32; ++i) w.Add("Assets/f" + std::to_string(i), Compressible(10000 + (std::size_t)i));
    std::vector<std::uint8_t> pkg;
    std::string why;
    CHECK(w.WriteToMemory(pkg, {}, nullptr, &why));
    PackageStack s;
    auto p = Package::OpenMemory(pkg, why);
    CHECK(p && s.Mount(p, why));
    for (int workers : {0, 3}) {
        JobSystem jobs(workers);
        std::atomic<int> ok{0}, bad{0};
        for (int i = 0; i < 32; ++i)
            s.ReadAsync(IdOf("Assets/f" + std::to_string(i)), [&ok, &bad, i](std::vector<std::uint8_t>&& b, const std::string& e) {
                if (e.empty() && b == Compressible(10000 + (std::size_t)i)) ++ok; else ++bad;
            }, &jobs);
        s.ReadAsync(IdOf("Assets/none"), [&bad](std::vector<std::uint8_t>&&, const std::string& e) { if (!e.empty()) ++bad; }, &jobs);
        jobs.WaitIdle();
        CHECK(ok == 32);
        CHECK(bad == 1);
    }
}

#ifndef OKAY_ASSETPKG_STANDALONE
void TestPackFolder() {
    RUN_SUITE("asset_package: packing an exported data folder");
    const fs::path dir = TempDir("packfolder");
    const fs::path data = dir / "Data", cache = dir / "Library" / "PackageCache";
    auto make = [&]() {
        std::error_code ec; fs::remove_all(data, ec);
        WriteFile(data / "game.okayscene", Bytes("okayscene 1\ngameobject Box\n  meshrenderer \"Assets/box.obj\" \"Assets/wood.okaymat\"\nend\n"));
        WriteFile(data / "game.okayconfig", Bytes("title=Test\nstartup=game.okayscene\n"));
        WriteFile(data / "Assets" / "box.obj", Compressible(20000));
        WriteFile(data / "Assets" / "wood.okaymat", Bytes("texture Assets/wood.png\n"));
        WriteFile(data / "Assets" / "wood.png", Random(5000, 9));
        WriteFile(data / "Assets" / "wood.png.okaymeta", Bytes("okaymeta 1\n"));
        WriteFile(data / "StreamingAssets" / "level.txt", Bytes("loose on purpose"));
        WriteFile(data / "build.json", Bytes("{}"));
    };
    make();
    // Keep a copy of every file to compare the materialised view with.
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> originals;
    for (auto& e : fs::recursive_directory_iterator(data))
        if (e.is_regular_file()) originals.push_back({fs::relative(e.path(), data).generic_string(), ReadFile(e.path())});

    PackFolderOptions po;
    po.write.cacheDir = cache.string();
    PackFolderResult res;
    std::string why;
    CHECK(PackFolder(data.string(), "game.okaypkg", po, res, why));
    if (!why.empty()) std::cerr << "  " << why << "\n";
    CHECK(res.packed.size() == 6);
    CHECK(res.stats.fromCache == 0);
    CHECK(res.stats.compressed >= 1);
    CHECK(res.dependencyLinks >= 2);
    CHECK(fs::exists(data / "game.okaypkg") && fs::exists(data / "packages.manifest"));
    CHECK(!fs::exists(data / "Assets") && !fs::exists(data / "game.okayscene"));   // packed files are gone from the folder
    CHECK(fs::exists(data / "StreamingAssets" / "level.txt") && fs::exists(data / "build.json"));
    const std::vector<std::uint8_t> first = ReadFile(data / "game.okaypkg");

    PackageStack s;
    CHECK(MountFolder(data.string(), s, why));
    auto deps = [&](const std::string& n) {
        PackageStack::Hit h; std::set<std::string> out;
        if (s.Find(IdOf(n), h)) for (AssetId d : h.package->DependenciesOf(*h.entry)) out.insert(s.NameOf(d));
        return out;
    };
    CHECK(deps("game.okayscene").count("Assets/box.obj") == 1);
    CHECK(deps("game.okayscene").count("Assets/wood.okaymat") == 1);
    CHECK(deps("Assets/wood.okaymat").count("Assets/wood.png") == 1);
    const fs::path view = dir / "view";
    CHECK(Materialize(s, data.string(), view.string(), nullptr, why));
    bool same = true;
    for (const auto& o : originals) if (ReadFile(view / o.first) != o.second && o.first.rfind("StreamingAssets", 0) != 0) { same = false; std::cerr << "  differs: " << o.first << "\n"; }
    CHECK(same);

    // The same folder again: every compressed copy comes from the cache, and the package is byte-identical.
    make();
    PackFolderResult res2;
    CHECK(PackFolder(data.string(), "game.okaypkg", po, res2, why));
    CHECK(res2.stats.fromCache == res.stats.compressed);
    CHECK(ReadFile(data / "game.okaypkg") == first);

    // A damaged cache file is caught by the verify step: the package is written again without the cache.
    for (auto& e : fs::directory_iterator(cache)) {
        std::vector<std::uint8_t> b = ReadFile(e.path());
        if (b.size() > 40) { b[b.size() - 3] ^= 0x5A; WriteFile(e.path(), b); }
    }
    make();
    PackFolderResult res3;
    CHECK(PackFolder(data.string(), "game.okaypkg", po, res3, why));
    CHECK(ReadFile(data / "game.okaypkg") == first);
    fs::remove_all(dir);
}

void TestAssetManagerMount() {
    RUN_SUITE("asset_package: AssetManager reads from mounted packages");
    PackageWriter w;
    w.Add("game.okayscene", Bytes("scene"), {"Assets/mat.okaymat"});
    w.Add("Assets/mat.okaymat", Bytes("texture Assets/t.png\n"), {"Assets/t.png"});
    w.Add("Assets/t.png", Bytes("PNGDATA"));
    std::vector<std::uint8_t> pkg;
    std::string why;
    CHECK(w.WriteToMemory(pkg, {}, nullptr, &why));
    auto stack = std::make_shared<PackageStack>();
    auto p = Package::OpenMemory(pkg, why);
    CHECK(p && stack->Mount(p, why));

    JobSystem jobs(0);
    SetEngineJobs(&jobs);
    {
        AssetManager m;
        m.RegisterDefaultLoaders();
        const std::string root = (fs::temp_directory_path() / "okay_assetpkg_no_such_folder").generic_string();
        std::vector<std::uint8_t> out;
        CHECK(!m.ReadBytes(root + "/Assets/t.png", out));   // nothing mounted, nothing on disk
        m.MountPackages(stack, root);
        CHECK(m.ReadBytes(root + "/Assets/t.png", out) && Text(out) == "PNGDATA");
        CHECK(m.ReadBytes("Assets/T.png", out) && Text(out) == "PNGDATA");   // relative to the mount, any letter case
        AssetHandle<AssetBytes> h = m.Get<AssetBytes>(root + "/Assets/mat.okaymat");
        CHECK(h && Text(h->data) == "texture Assets/t.png\n");
        int done = 0;
        AssetHandle<AssetBytes> a = m.LoadAsync<AssetBytes>(root + "/game.okayscene", [&done](AssetHandle<AssetBytes> r, const std::string& e) { if (r && e.empty()) ++done; });
        m.Pump();
        CHECK(done == 1 && a && Text(a->data) == "scene");
        std::vector<AssetHandle<AssetBytes>> all = m.StreamWithDependencies(root + "/game.okayscene", 1.0f);
        m.Pump();
        CHECK(all.size() == 3);
        bool ready = true;
        for (auto& x : all) if (!x) ready = false;
        CHECK(ready);
        CHECK(all.size() == 3 && Text(all[0]->data) == "PNGDATA");   // dependencies first
        m.UnmountPackages();
        CHECK(!m.ReadBytes(root + "/Assets/t.png", out));
    }
    SetEngineJobs(nullptr);
}
#endif

} // namespace

int main() {
    TestHash();
    TestNames();
    TestRoundTrip();
    TestDeterminism();
    TestCollisions();
    TestCorruption();
    FuzzSweep();
    TestPatches();
    TestClosureCycles();
    TestManifest();
    TestAsync();
#ifndef OKAY_ASSETPKG_STANDALONE
    TestFolderAndView();
    TestPackFolder();
    TestAssetManagerMount();
#endif
    TEST_MAIN_RESULT();
}
