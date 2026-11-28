// The memory report (okay/Core/Memory.hpp): the process's own memory and the named counters of its big owners.
// Written from the Linux proc(5) manual page (the VmRSS, RssAnon and VmHWM lines of /proc/self/status) and the
// Windows documentation of GetProcessMemoryInfo / PROCESS_MEMORY_COUNTERS_EX; the start-up clock from proc(5) (/proc/self/stat
// starttime, /proc/uptime) and the GetProcessTimes documentation. No outside code.
#include "okay/Core/Memory.hpp"
#include "okay/Core/Paths.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <malloc.h>   // mallinfo2: the C library's heap in use
#include <unistd.h>   // sysconf(_SC_CLK_TCK)
#endif

namespace okay {

namespace {

// "VmRSS:     12345 kB" -> 12345 * 1024. Returns false when the line is not that key.
bool ReadKb(std::string_view line, std::string_view key, std::size_t& out) {
    if (line.size() <= key.size() || line.compare(0, key.size(), key) != 0 || line[key.size()] != ':') return false;
    std::size_t i = key.size() + 1, v = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    bool digit = false;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') { v = v * 10 + (std::size_t)(line[i] - '0'); ++i; digit = true; }
    if (!digit) return false;
    out = v * 1024;
    return true;
}

struct Counter { std::string name; std::function<std::size_t()> bytes; };
std::vector<Counter>& Counters() { static std::vector<Counter> c; return c; }

std::string Mb(std::size_t bytes) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    return b;
}

} // namespace

ProcessMemory ParseProcStatus(std::string_view text) {
    ProcessMemory m;
    bool rss = false;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(start, end - start);
        if (ReadKb(line, "VmRSS", m.residentBytes)) rss = true;
        else ReadKb(line, "RssAnon", m.privateBytes) || ReadKb(line, "VmHWM", m.peakResidentBytes);
        start = end + 1;
    }
    m.known = rss;
    return m;
}

ProcessMemory QueryProcessMemory() {
#if defined(_WIN32)
    // PROCESS_MEMORY_COUNTERS_EX as documented; K32GetProcessMemoryInfo lives in kernel32 since Windows 7, so no psapi link.
    struct Counters_ { DWORD cb; DWORD PageFaultCount; SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage,
                       QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage, PrivateUsage; };
    using Fn = BOOL(WINAPI*)(HANDLE, void*, DWORD);
    static Fn fn = [] {
        HMODULE k = GetModuleHandleA("kernel32.dll");
        return k ? (Fn)(void*)GetProcAddress(k, "K32GetProcessMemoryInfo") : (Fn) nullptr;
    }();
    ProcessMemory m;
    Counters_ c{};
    c.cb = sizeof(c);
    if (fn && fn(GetCurrentProcess(), &c, sizeof(c))) {
        m.known = true;
        m.residentBytes = c.WorkingSetSize;
        m.privateBytes = c.PrivateUsage;
        m.peakResidentBytes = c.PeakWorkingSetSize;
    }
    return m;
#elif defined(__linux__)
    std::ifstream f("/proc/self/status", std::ios::binary);
    if (!f) return {};
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return ParseProcStatus(text);
#else
    return {};
#endif
}

void AddMemoryCounter(const std::string& name, std::function<std::size_t()> bytes) {
    for (Counter& c : Counters())
        if (c.name == name) { c.bytes = std::move(bytes); return; }
    Counters().push_back({name, std::move(bytes)});
}

void RemoveMemoryCounter(const std::string& name) {
    auto& v = Counters();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Counter& c) { return c.name == name; }), v.end());
}

std::vector<std::pair<std::string, std::size_t>> MemoryCounters() {
    std::vector<std::pair<std::string, std::size_t>> out;
    for (const Counter& c : Counters()) out.push_back({c.name, c.bytes ? c.bytes() : 0});
    return out;
}

std::size_t HeapInUseBytes() {
#if defined(_WIN32)
    // Every heap of the process (the C runtime's malloc and new use the process heap), walked block by block (HeapWalk,
    // PROCESS_HEAP_ENTRY_BUSY). Slow for a big heap (milliseconds), so only for the report.
    HANDLE heaps[64];
    const DWORD n = GetProcessHeaps(64, heaps);
    std::size_t used = 0;
    for (DWORD i = 0; i < n && i < 64; ++i) {
        if (!HeapLock(heaps[i])) continue;
        PROCESS_HEAP_ENTRY e{};
        while (HeapWalk(heaps[i], &e))
            if (e.wFlags & PROCESS_HEAP_ENTRY_BUSY) used += e.cbData;
        HeapUnlock(heaps[i]);
    }
    return used;
#elif defined(__linux__) && defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    const struct mallinfo2 mi = mallinfo2();
    return mi.uordblks + mi.hblkhd;   // in-use chunks of the arenas + the big blocks given their own mapping
#else
    return 0;
#endif
}

std::string MemoryReportText(const std::string& title) {
    const ProcessMemory p = QueryProcessMemory();
    std::string r = title + ": ";
    if (p.known) r += "resident " + Mb(p.residentBytes) + ", private " + Mb(p.privateBytes) + ", peak " + Mb(p.peakResidentBytes) + "\n";
    else r += "process memory unknown on this system\n";
    if (const std::size_t heap = HeapInUseBytes()) {
        // What is not on the heap is the program's own data, thread stacks and what the graphics driver and other system
        // libraries map for themselves: on Windows that last part is often the biggest.
        r += "  heap in use (malloc / new) " + Mb(heap);
        if (p.known && p.privateBytes > heap) r += "; outside the heap (driver, system libraries, stacks, program data) " + Mb(p.privateBytes - heap);
        r += "\n";
    }
    // A counter whose name starts with "of which" is a part of another counter (the cover textures are painter textures):
    // listed after the owners and left out of the sum.
    std::vector<std::pair<std::string, std::size_t>> rows, parts;
    for (auto& row : MemoryCounters()) (row.first.rfind("of which", 0) == 0 ? parts : rows).push_back(std::move(row));
    auto bigFirst = [](const auto& a, const auto& b) { return a.second > b.second; };
    std::stable_sort(rows.begin(), rows.end(), bigFirst);
    std::stable_sort(parts.begin(), parts.end(), bigFirst);
    std::size_t sum = 0;
    auto line = [&r](const std::string& name, std::size_t bytes) {
        char b[256];
        std::snprintf(b, sizeof(b), "  %-52s %10s\n", name.c_str(), Mb(bytes).c_str());
        r += b;
    };
    for (const auto& row : rows) { sum += row.second; line(row.first, row.second); }
    for (const auto& row : parts) line(row.first, row.second);
    if (!rows.empty()) {
        char b[160];
        if (p.known && p.privateBytes > 0)
            std::snprintf(b, sizeof(b), "  counted owners: %s of %s private (%.0f%%)\n", Mb(sum).c_str(), Mb(p.privateBytes).c_str(),
                          100.0 * (double)sum / (double)p.privateBytes);
        else
            std::snprintf(b, sizeof(b), "  counted owners: %s\n", Mb(sum).c_str());
        r += b;
    }
    return r;
}

bool MemoryReportWanted() {
    const char* v = std::getenv("OKAY_MEMORY_REPORT");
    return v && v[0] && !(v[0] == '0' && v[1] == '\0');
}

namespace {
// "60" -> 60; anything that is not a number of at least 5 -> 0 (one report only).
double RepeatSeconds() {
    const char* v = std::getenv("OKAY_MEMORY_REPORT");
    if (!v || !*v) return 0.0;
    char* end = nullptr;
    const double n = std::strtod(v, &end);
    return (end && end != v && *end == '\0' && n >= 5.0) ? n : 0.0;
}
} // namespace

double MemoryReportEverySeconds() {
    if (!MemoryReportWanted()) return 0.0;
    const double n = RepeatSeconds();
    return n > 0.0 ? n : 10.0;
}

void EmitMemoryReport(const std::string& title) {
    const std::string text = MemoryReportText(title);
    std::fputs(text.c_str(), stdout);
    std::fflush(stdout);
    const std::string dir = Paths::LogDir();
    if (!Paths::EnsureDir(dir)) return;
    std::ofstream f(std::filesystem::u8path(Paths::Join(dir, "memory_report.txt")), std::ios::app | std::ios::binary);
    if (!f) return;
    char when[32] = "";
    const std::time_t now = std::time(nullptr);
    if (const std::tm* tm = std::localtime(&now)) std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", tm);
    f << "---- " << when << "\n" << text;
}

void MemoryReportTick(const std::string& title, double seconds) {
    static double s_next = -1.0;
    static bool s_done = false;   // the one report of a plain OKAY_MEMORY_REPORT=1 was made
    const double every = MemoryReportEverySeconds();
    if (every <= 0.0 || s_done) return;
    if (s_next < 0.0) s_next = every;
    if (seconds < s_next) return;
    s_next = seconds + every;
    s_done = RepeatSeconds() <= 0.0;
    EmitMemoryReport(title);
}

double ProcessAgeMs() {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{}, now{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1.0;
    GetSystemTimeAsFileTime(&now);
    const auto ticks = [](const FILETIME& f) { return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime; };   // 100 ns units
    return (double)(ticks(now) - ticks(created)) / 10000.0;
#elif defined(__linux__)
    std::ifstream st("/proc/self/stat", std::ios::binary), up("/proc/uptime", std::ios::binary);
    std::string stat((std::istreambuf_iterator<char>(st)), std::istreambuf_iterator<char>());
    double uptime = 0.0;
    if (!(up >> uptime)) return -1.0;
    // Field 2 (the program name) is in parentheses and may hold spaces: count the fields after its closing ')'.
    const std::size_t close = stat.rfind(')');
    if (close == std::string::npos) return -1.0;
    std::istringstream rest(stat.substr(close + 1));
    std::string field;
    for (int i = 3; i <= 22 && rest >> field; ++i)
        if (i == 22) {   // starttime: clock ticks after boot
            const long hz = sysconf(_SC_CLK_TCK);
            if (hz <= 0) return -1.0;
            return (uptime - std::strtod(field.c_str(), nullptr) / (double)hz) * 1000.0;
        }
    return -1.0;
#else
    return -1.0;
#endif
}

void StartupMark(const std::string& title, const std::string& what) {
    static const bool wanted = [] { const char* v = std::getenv("OKAY_STARTUP_REPORT"); return v && v[0] && !(v[0] == '0' && v[1] == '\0'); }();
    if (!wanted) return;
    static std::vector<std::string> done;
    if (std::find(done.begin(), done.end(), what) != done.end()) return;
    done.push_back(what);
    char line[256];
    std::snprintf(line, sizeof line, "%s: %s at %.0f ms (heap %.1f MB)\n", title.c_str(), what.c_str(), ProcessAgeMs(),
                  (double)HeapInUseBytes() / (1024.0 * 1024.0));
    std::fputs(line, stdout);
    std::fflush(stdout);
    const std::string dir = Paths::LogDir();
    if (!Paths::EnsureDir(dir)) return;
    std::ofstream f(std::filesystem::u8path(Paths::Join(dir, "startup_report.txt")), std::ios::app | std::ios::binary);
    if (f) f << line;
}

} // namespace okay
