// The memory report (okay/Core/Memory.hpp): the process's own memory and the named counters of its big owners.
// Written from the Linux proc(5) manual page (the VmRSS, RssAnon and VmHWM lines of /proc/self/status) and the
// Windows documentation of GetProcessMemoryInfo / PROCESS_MEMORY_COUNTERS_EX. No outside code.
#include "okay/Core/Memory.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

std::string MemoryReportText(const std::string& title) {
    const ProcessMemory p = QueryProcessMemory();
    std::string r = title + ": ";
    if (p.known) r += "resident " + Mb(p.residentBytes) + ", private " + Mb(p.privateBytes) + ", peak " + Mb(p.peakResidentBytes) + "\n";
    else r += "process memory unknown on this system\n";
    auto rows = MemoryCounters();
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::size_t sum = 0;
    for (const auto& row : rows) {
        sum += row.second;
        char b[256];
        std::snprintf(b, sizeof(b), "  %-44s %10s\n", row.first.c_str(), Mb(row.second).c_str());
        r += b;
    }
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

} // namespace okay
