// Asset package benchmark (docs/engine/asset_packages.md, "Benchmarks"): packing speed (one thread and the job system),
// the cache on a second export, opening, reading every entry against reading loose files, random reads by id, the
// materialised view (first and second start), Hash64 speed, and the reader's memory. Prints numbers; it checks only
// that every read gave the right bytes, never a timing (timings depend on the machine).
//   test_asset_package_bench [files=2000] [megabytes=64]
#include "test_framework.hpp"
#include "okay/Core/AssetPackage.hpp"
#include "okay/Core/Jobs.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::assetpkg;
namespace fs = std::filesystem;

namespace {
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::uint64_t Rand(std::uint64_t& s) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
long PeakKb() {
#if defined(__linux__)
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) if (line.rfind("VmHWM:", 0) == 0) return std::atol(line.c_str() + 6);
#endif
    return -1;
}
double Mb(std::uint64_t b) { return (double)b / (1024.0 * 1024.0); }
} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("asset_package_bench");
    const int files = argc > 1 ? std::max(1, std::atoi(argv[1])) : 2000;
    const int megabytes = argc > 2 ? std::max(1, std::atoi(argv[2])) : 64;
    const fs::path dir = fs::temp_directory_path() / "okay_assetpkg_bench";
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path src = dir / "Data";
    fs::create_directories(src);

    // A mix like a game's: two thirds text-like and mesh-like data that compresses, one third already-compressed bytes.
    std::uint64_t seed = 12345, total = 0;
    const std::uint64_t per = (std::uint64_t)megabytes * 1024 * 1024 / (std::uint64_t)files;
    std::vector<std::string> names;
    const char* words[] = {"vertex ", "normal ", "0.125 ", "1.5 ", "texture ", "Assets/rock.png ", "transform ", "-3.25 "};
    for (int i = 0; i < files; ++i) {
        const std::string name = "Assets/Folder" + std::to_string(i % 37) + "/file_" + std::to_string(i) + (i % 3 == 2 ? ".bin" : ".txt");
        const std::size_t n = (std::size_t)(per / 2 + Rand(seed) % (per + 1));
        std::string body;
        body.reserve(n);
        if (i % 3 == 2) { while (body.size() < n) body += (char)Rand(seed); }
        else { while (body.size() < n) body += words[Rand(seed) % 8]; body.resize(n); }
        fs::create_directories((src / name).parent_path());
        std::ofstream(src / name, std::ios::binary) << body;
        names.push_back(name);
        total += n;
    }
    std::printf("  input: %d files, %.1f MB\n", files, Mb(total));

    // Hash64 speed.
    {
        std::vector<std::uint8_t> buf(64u << 20, 7);
        const double t = Now();
        volatile std::uint64_t h = Hash64(buf.data(), buf.size());
        (void)h;
        std::printf("  Hash64: %.0f MB/s\n", 64.0 / (Now() - t));
    }

    std::string why;
    auto pack = [&](JobSystem* jobs, const std::string& cache, WriteStats& st, const std::string& out) {
        PackageWriter w;
        for (const std::string& n : names) w.AddFile(n, (src / n).string());
        WriteOptions o; o.jobs = jobs; o.cacheDir = cache;
        const double t = Now();
        const bool ok = w.Write(out, o, &st, &why);
        CHECK(ok);
        return Now() - t;
    };
    const std::string pkgPath = (dir / "game.okaypkg").string();
    WriteStats st1, st2, st3, st4;
    const double t1 = pack(nullptr, "", st1, pkgPath);
    std::printf("  pack, 1 thread:        %.2f s  (%.0f MB/s in), %.1f MB -> %.1f MB (%.0f%%), %zu of %zu compressed\n", t1, Mb(total) / t1,
                Mb(st1.bytesIn), Mb(st1.bytesOut), 100.0 * (double)st1.bytesOut / (double)st1.bytesIn, st1.compressed, st1.entries);
    JobSystem jobs(JobSystem::kAuto);
    const double t2 = pack(&jobs, "", st2, pkgPath);
    std::printf("  pack, %d workers:       %.2f s  (%.0f MB/s in)\n", jobs.WorkerCount(), t2, Mb(total) / t2);
    const std::string cache = (dir / "Library" / "PackageCache").string();
    const double t3 = pack(&jobs, cache, st3, pkgPath);
    const double t4 = pack(&jobs, cache, st4, pkgPath);
    std::printf("  pack with cache:       first %.2f s, again (nothing changed) %.2f s, %zu entries from the cache\n", t3, t4, st4.fromCache);
    // One file changed: only it is compressed again.
    { std::ofstream(src / names[0], std::ios::binary | std::ios::app) << "changed"; }
    WriteStats st5;
    const double t5 = pack(&jobs, cache, st5, pkgPath);
    std::printf("  pack, 1 file changed:  %.2f s (%zu of %zu entries from the cache)\n", t5, st5.fromCache, st5.entries);

    // Open and read everything, against reading the loose files.
    double t = Now();
    auto p = Package::OpenFile(pkgPath, why);
    const double tOpen = Now() - t;
    CHECK(p != nullptr);
    if (!p) return 1;
    std::size_t meta = p->Entries().size() * sizeof(Entry);
    std::printf("  open: %.2f ms, reader memory for the tables: %.1f KB (%zu entries)\n", tOpen * 1000, meta / 1024.0, p->Entries().size());
    std::vector<std::uint8_t> out;
    std::uint64_t readBytes = 0;
    t = Now();
    for (const std::string& n : names) { const Entry* e = p->Find(n); if (e && p->Read(*e, out, why)) readBytes += out.size(); }
    const double tPkg = Now() - t;
    CHECK(readBytes == st5.bytesIn);
    std::uint64_t looseBytes = 0;
    t = Now();
    for (const std::string& n : names) {
        std::ifstream f(src / n, std::ios::binary);
        std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        looseBytes += b.size();
    }
    const double tLoose = Now() - t;
    CHECK(looseBytes == readBytes);
    std::printf("  read all, package:     %.3f s (%.0f MB/s, hash-checked, unpacked)\n", tPkg, Mb(readBytes) / tPkg);
    std::printf("  read all, loose files: %.3f s (%.0f MB/s, no checks; files warm in the OS cache)\n", tLoose, Mb(looseBytes) / tLoose);
    t = Now();
    std::uint64_t s2 = 99;
    for (int i = 0; i < 2000; ++i) { const Entry* e = p->Find(IdOf(names[Rand(s2) % names.size()])); CHECK(e != nullptr); }
    std::printf("  lookup by id: %.0f ns each\n", (Now() - t) / 2000 * 1e9);
    t = Now();
    CHECK(p->VerifyAll(why));
    std::printf("  verify every byte: %.3f s\n", Now() - t);

    // Async reads on the job system.
    PackageStack stack;
    CHECK(stack.Mount(p, why));
    std::atomic<std::uint64_t> asyncBytes{0};
    t = Now();
    for (const std::string& n : names)
        stack.ReadAsync(IdOf(n), [&asyncBytes](std::vector<std::uint8_t>&& b, const std::string&) { asyncBytes += b.size(); }, &jobs);
    jobs.WaitIdle();
    std::printf("  read all, async on %d workers: %.3f s\n", jobs.WorkerCount(), Now() - t);
    CHECK(asyncBytes == readBytes);

    // The materialised view: first start writes everything, a second start writes nothing.
    MaterializeStats m1, m2;
    t = Now();
    CHECK(Materialize(stack, dir.string(), (dir / "view").string(), &m1, why));
    const double tm1 = Now() - t;
    t = Now();
    CHECK(Materialize(stack, dir.string(), (dir / "view").string(), &m2, why));
    std::printf("  view: first start %.2f s (%zu files written), next start %.3f s (%zu kept)\n", tm1, m1.written, Now() - t, m2.kept);
    std::printf("  peak memory of this program: %ld KB\n", PeakKb());
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
