#include "okay/Debug/ConsoleModel.hpp"
#include "okay/Debug/DebugUtil.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace okay {

using namespace debugutil;

// ---------------------------------------------------------------------------
// Source references
// ---------------------------------------------------------------------------

static std::string TrimWs(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static bool AllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}

// `path:12` or `path:12:5` where path ends in an extension (at least one letter).
static bool SplitPathLine(const std::string& token, std::string& path, int& line, int& column) {
    std::string t = token;
    while (!t.empty() && std::string(".,;:)]}").find(t.back()) != std::string::npos) t.pop_back();
    std::size_t end = t.size();
    int nums[2] = {0, 0};
    int n = 0;
    while (n < 2 && end > 0) {
        std::size_t c = t.rfind(':', end - 1);
        if (c == std::string::npos || c == 0) break;
        std::string digits = t.substr(c + 1, end - c - 1);
        if (!AllDigits(digits) || digits.size() > 9) break;
        nums[n++] = std::atoi(digits.c_str());
        end = c;
    }
    if (n == 0) return false;
    std::string p = t.substr(0, end);
    std::size_t dot = p.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= p.size()) return false;
    std::string ext = p.substr(dot + 1);
    if (ext.size() > 10) return false;
    bool letter = false;
    for (char c : ext) {
        bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        bool num = c >= '0' && c <= '9';
        if (!alpha && !num) return false;
        if (alpha) letter = true;
    }
    if (!letter) return false;
    path = p;
    if (n == 1) { line = nums[0]; column = 0; }
    else { line = nums[1]; column = nums[0]; }
    return true;
}

static bool IsTokenDelim(char c) {
    switch (c) {
        case ' ': case '\t': case '\r': case '\n': case '"': case '\'': case '(': case ')': case '[': case ']':
        case '<': case '>': case ',': case ';': case '`': case '=':
            return true;
        default: return false;
    }
}

ConsoleSource ParseConsoleSource(const std::string& m) {
    ConsoleSource src;
    // 1. [Object: A/B] and [Asset: path[:line]] tags (earliest tag wins)
    struct Tag { const char* name; ConsoleSource::Kind kind; };
    static const Tag kTags[] = {
        {"object", ConsoleSource::Kind::Object}, {"gameobject", ConsoleSource::Kind::Object},
        {"asset", ConsoleSource::Kind::Asset},   {"file", ConsoleSource::Kind::Asset}};
    const std::string lower = ToLowerAscii(m);
    std::size_t bestPos = std::string::npos;
    const Tag* best = nullptr;
    for (const Tag& t : kTags) {
        std::string key = std::string("[") + t.name + ":";
        std::size_t p = lower.find(key);
        while (p != std::string::npos) {
            std::size_t close = m.find(']', p);
            if (close != std::string::npos && !TrimWs(m.substr(p + key.size(), close - p - key.size())).empty()) break;
            p = lower.find(key, p + 1);
        }
        if (p != std::string::npos && p < bestPos) { bestPos = p; best = &t; }
    }
    if (best) {
        std::size_t start = bestPos + std::strlen(best->name) + 2;
        std::size_t close = m.find(']', start);
        std::string body = TrimWs(m.substr(start, close - start));
        src.kind = best->kind;
        src.path = body;
        if (best->kind == ConsoleSource::Kind::Asset) {
            std::string p; int line = 0, col = 0;
            if (SplitPathLine(body, p, line, col)) { src.path = p; src.line = line; src.column = col; }
        }
        return src;
    }
    // 2. file.ext:line[:col] anywhere in the text
    std::size_t i = 0;
    while (i < m.size()) {
        while (i < m.size() && IsTokenDelim(m[i])) ++i;
        std::size_t s = i;
        while (i < m.size() && !IsTokenDelim(m[i])) ++i;
        if (i > s) {
            std::string p; int line = 0, col = 0;
            if (SplitPathLine(m.substr(s, i - s), p, line, col)) {
                src.kind = ConsoleSource::Kind::Asset;
                src.path = p; src.line = line; src.column = col;
                return src;
            }
        }
    }
    return src;
}

// A leading `[tag] ` that is a category, not a source reference.
static bool SplitCategoryPrefix(const std::string& msg, std::string& category, std::string& rest) {
    if (msg.size() < 3 || msg[0] != '[') return false;
    std::size_t close = msg.find(']');
    if (close == std::string::npos || close < 2 || close > 33) return false;
    for (std::size_t i = 1; i < close; ++i) {
        char c = msg[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    category = msg.substr(1, close - 1);
    std::size_t r = close + 1;
    if (r < msg.size() && msg[r] == ' ') ++r;
    rest = msg.substr(r);
    return true;
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct ConsoleModel::Impl {
    SpinLock lock;
    std::vector<ConsoleEntry> ring;
    std::size_t head = 0;
    std::size_t cap = 2000;
    std::uint64_t nextSeq = 0;
    std::uint64_t frame = 0;
    bool paused = false;
    std::uint64_t dropped = 0;
    bool parsePrefix = true;
    ConsoleFilter filter;
    bool collapse = true;
    std::function<double()> clock;                      // called under `lock`: keep it quick, never log from it
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    std::atomic<bool> pauseOnError{false}, pauseRequested{false};
    std::atomic<std::uint64_t> hook{0};                 // id of the Log::sink hook that currently feeds this model; 0 = detached

    double NowLocked() const {
        if (clock) return clock();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    std::size_t SizeLocked() const { return ring.size(); }
    const ConsoleEntry& At(std::size_t i) const { return ring[(head + i) % ring.size()]; }

    static void MergeSource(ConsoleSource& src, const ConsoleSource& extra) {
        if (extra.kind != ConsoleSource::Kind::None && src.kind == ConsoleSource::Kind::None)
            src.kind = extra.kind;
        else if (extra.kind == ConsoleSource::Kind::Script || extra.kind == ConsoleSource::Kind::Graph ||
                 extra.kind == ConsoleSource::Kind::Object)
            src.kind = extra.kind;
        if (!extra.path.empty()) src.path = extra.path;
        if (!extra.guid.empty()) src.guid = extra.guid;
        if (!extra.component.empty()) src.component = extra.component;
        if (!extra.nodeId.empty()) src.nodeId = extra.nodeId;
        if (extra.line) src.line = extra.line;
        if (extra.column) src.column = extra.column;
    }
    void Push(Log::Level level, std::string message, std::string category, const ConsoleSource* explicitSrc) {
        ConsoleSource src = ParseConsoleSource(message);
        if (explicitSrc) MergeSource(src, *explicitSrc);
        const ConsoleSource ambient = ActiveConsoleSource();
        if (ambient.kind != ConsoleSource::Kind::None || !ambient.guid.empty() || !ambient.nodeId.empty())
            MergeSource(src, ambient);
        SpinGuard g(lock);
        if (paused) { ++dropped; return; }
        ConsoleEntry e;
        e.seq = nextSeq++;
        e.level = level;
        e.category = std::move(category);
        e.message = std::move(message);
        e.timeSec = NowLocked();
        e.frame = frame;
        e.source = std::move(src);
        if (ring.size() < cap) ring.push_back(std::move(e));
        else if (cap > 0) { ring[head] = std::move(e); head = (head + 1) % ring.size(); }
        if (level == Log::Level::Error && pauseOnError.load(std::memory_order_relaxed))
            pauseRequested.store(true, std::memory_order_release);
    }

    void PushFromLog(Log::Level level, const std::string& msg) {
        std::string category, rest;
        bool parse;
        { SpinGuard g(lock); parse = parsePrefix; }
        if (parse && SplitCategoryPrefix(msg, category, rest)) Push(level, std::move(rest), std::move(category), nullptr);
        else Push(level, msg, std::string(), nullptr);
    }

    static bool Passes(const ConsoleEntry& e, const ConsoleFilter& f) {
        if (!(f.levelMask & ConsoleModel::LevelBit(e.level))) return false;
        if (!f.category.empty() && e.category != f.category) return false;
        return TextMatches(e, f.text, f.caseSensitive);
    }
    static bool TextMatches(const ConsoleEntry& e, const std::string& text, bool cs) {
        if (text.empty()) return true;
        return Contains(e.message, text, cs) || Contains(e.category, text, cs) || Contains(e.source.path, text, cs);
    }
    static bool SameRun(const ConsoleEntry& a, const ConsoleEntry& b) {
        return a.level == b.level && a.category == b.category && a.message == b.message;
    }
    std::vector<ConsoleRow> BuildRows(const ConsoleFilter& f, bool collapseRuns) const {   // caller holds the lock
        std::vector<ConsoleRow> rows;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const ConsoleEntry& e = At(i);
            if (!Passes(e, f)) continue;
            if (collapseRuns && !rows.empty() && SameRun(rows.back().entry, e)) {
                ConsoleRow& r = rows.back();
                ++r.repeat;
                r.lastSeq = e.seq; r.lastFrame = e.frame; r.lastTimeSec = e.timeSec;
                continue;
            }
            ConsoleRow r;
            r.entry = e;
            r.lastSeq = e.seq; r.lastFrame = e.frame; r.lastTimeSec = e.timeSec;
            rows.push_back(std::move(r));
        }
        return rows;
    }
};

namespace {
// The hook installed into Log::sink: the previous sink first, then this model. Only the
// newest hook of a model feeds it, so stacking after a Detach never records a line twice.
struct ChainSink {
    std::shared_ptr<ConsoleModel::Impl> impl;
    Log::Sink prev;
    std::uint64_t id = 0;
    void operator()(Log::Level level, const std::string& msg) const {
        if (prev) prev(level, msg);
        if (impl->hook.load(std::memory_order_acquire) == id) impl->PushFromLog(level, msg);
    }
};
std::atomic<std::uint64_t> g_nextHookId{1};
}

// ---------------------------------------------------------------------------
// ConsoleModel
// ---------------------------------------------------------------------------

ConsoleModel::ConsoleModel(std::size_t capacity) : m_impl(std::make_shared<Impl>()) {
    m_impl->cap = capacity;
}

ConsoleModel::~ConsoleModel() { Detach(); }

namespace {
std::vector<ConsoleSource>& SourceStack() {
    thread_local std::vector<ConsoleSource> stack;
    return stack;
}
}

ConsoleSourceScope::ConsoleSourceScope(ConsoleSource source) {
    SourceStack().push_back(std::move(source));
}
ConsoleSourceScope::~ConsoleSourceScope() {
    if (!SourceStack().empty()) SourceStack().pop_back();
}
ConsoleSource ActiveConsoleSource() {
    return SourceStack().empty() ? ConsoleSource{} : SourceStack().back();
}

void ConsoleModel::Add(Log::Level level, const std::string& message, const std::string& category) {
    m_impl->Push(level, message, category, nullptr);
}

void ConsoleModel::Add(Log::Level level, const std::string& message, const std::string& category, const ConsoleSource& source) {
    m_impl->Push(level, message, category, &source);
}

bool ConsoleModel::Attach() {
    if (m_impl->hook.load(std::memory_order_acquire) != 0) return false;
    const std::uint64_t id = g_nextHookId.fetch_add(1, std::memory_order_relaxed);
    ChainSink cs;
    cs.impl = m_impl;
    cs.prev = Log::sink;
    cs.id = id;
    Log::sink = std::move(cs);
    m_impl->hook.store(id, std::memory_order_release);
    return true;
}

void ConsoleModel::Detach() {
    const std::uint64_t id = m_impl->hook.exchange(0, std::memory_order_acq_rel);
    if (id == 0) return;
    if (const ChainSink* cur = Log::sink.target<ChainSink>()) {
        if (cur->impl == m_impl && cur->id == id) {
            Log::Sink restore = cur->prev;       // copy first: `cur` points into Log::sink
            Log::sink = std::move(restore);
        }
    }
}

bool ConsoleModel::Attached() const { return m_impl->hook.load(std::memory_order_acquire) != 0; }

void ConsoleModel::SetParseCategoryPrefix(bool on) { SpinGuard g(m_impl->lock); m_impl->parsePrefix = on; }

void ConsoleModel::SetFrame(std::uint64_t frame) { SpinGuard g(m_impl->lock); m_impl->frame = frame; }
void ConsoleModel::NextFrame() { SpinGuard g(m_impl->lock); ++m_impl->frame; }
std::uint64_t ConsoleModel::Frame() const { SpinGuard g(m_impl->lock); return m_impl->frame; }
void ConsoleModel::SetClock(std::function<double()> clock) { SpinGuard g(m_impl->lock); m_impl->clock = std::move(clock); }

void ConsoleModel::SetPaused(bool paused) { SpinGuard g(m_impl->lock); m_impl->paused = paused; }
bool ConsoleModel::Paused() const { SpinGuard g(m_impl->lock); return m_impl->paused; }
std::uint64_t ConsoleModel::DroppedWhilePaused() const { SpinGuard g(m_impl->lock); return m_impl->dropped; }
void ConsoleModel::SetPauseOnError(bool on) { m_impl->pauseOnError.store(on, std::memory_order_relaxed); }
bool ConsoleModel::PauseOnError() const { return m_impl->pauseOnError.load(std::memory_order_relaxed); }
bool ConsoleModel::PauseRequested() const { return m_impl->pauseRequested.load(std::memory_order_acquire); }
bool ConsoleModel::ConsumePauseRequest() { return m_impl->pauseRequested.exchange(false, std::memory_order_acq_rel); }

void ConsoleModel::Clear() {
    SpinGuard g(m_impl->lock);
    m_impl->ring.clear();
    m_impl->head = 0;
    m_impl->pauseRequested.store(false, std::memory_order_release);
}

void ConsoleModel::SetCapacity(std::size_t capacity) {
    SpinGuard g(m_impl->lock);
    Impl& d = *m_impl;
    std::vector<ConsoleEntry> fresh;
    std::size_t keep = d.ring.size() < capacity ? d.ring.size() : capacity;
    fresh.reserve(keep);
    for (std::size_t i = d.ring.size() - keep; i < d.ring.size(); ++i) fresh.push_back(std::move(d.ring[(d.head + i) % d.ring.size()]));
    d.ring = std::move(fresh);
    d.head = 0;
    d.cap = capacity;
}

std::size_t ConsoleModel::Size() const { SpinGuard g(m_impl->lock); return m_impl->ring.size(); }
std::uint64_t ConsoleModel::TotalReceived() const { SpinGuard g(m_impl->lock); return m_impl->nextSeq; }

ConsoleCounts ConsoleModel::Counts() const {
    SpinGuard g(m_impl->lock);
    ConsoleCounts c;
    for (const ConsoleEntry& e : m_impl->ring) {
        switch (e.level) {
            case Log::Level::Trace:   ++c.trace; break;
            case Log::Level::Info:    ++c.info; break;
            case Log::Level::Warning: ++c.warning; break;
            case Log::Level::Error:   ++c.error; break;
        }
    }
    return c;
}

void ConsoleModel::SetFilter(const ConsoleFilter& f) { SpinGuard g(m_impl->lock); m_impl->filter = f; }
ConsoleFilter ConsoleModel::GetFilter() const { SpinGuard g(m_impl->lock); return m_impl->filter; }
void ConsoleModel::SetCollapse(bool on) { SpinGuard g(m_impl->lock); m_impl->collapse = on; }
bool ConsoleModel::Collapse() const { SpinGuard g(m_impl->lock); return m_impl->collapse; }

std::vector<ConsoleRow> ConsoleModel::Rows() const {
    SpinGuard g(m_impl->lock);
    return m_impl->BuildRows(m_impl->filter, m_impl->collapse);
}

std::vector<ConsoleRow> ConsoleModel::Rows(const ConsoleFilter& f, bool collapse) const {
    SpinGuard g(m_impl->lock);
    return m_impl->BuildRows(f, collapse);
}

std::vector<ConsoleEntry> ConsoleModel::Entries() const {
    SpinGuard g(m_impl->lock);
    std::vector<ConsoleEntry> out;
    out.reserve(m_impl->ring.size());
    for (std::size_t i = 0; i < m_impl->ring.size(); ++i) out.push_back(m_impl->At(i));
    return out;
}

long long ConsoleModel::Find(const std::string& query, std::size_t startRow, bool forward, bool caseSensitive) const {
    if (query.empty()) return -1;
    std::vector<ConsoleRow> rows = Rows();
    const std::size_t n = rows.size();
    if (n == 0) return -1;
    if (startRow >= n) startRow = forward ? 0 : n - 1;
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t i = forward ? (startRow + k) % n : (startRow + n - (k % n)) % n;
        if (Impl::TextMatches(rows[i].entry, query, caseSensitive)) return (long long)i;
    }
    return -1;
}

std::vector<std::size_t> ConsoleModel::FindAll(const std::string& query, bool caseSensitive) const {
    std::vector<std::size_t> out;
    if (query.empty()) return out;
    std::vector<ConsoleRow> rows = Rows();
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (Impl::TextMatches(rows[i].entry, query, caseSensitive)) out.push_back(i);
    return out;
}

const char* ConsoleModel::LevelName(Log::Level l) {
    switch (l) {
        case Log::Level::Trace:   return "TRACE";
        case Log::Level::Info:    return "INFO";
        case Log::Level::Warning: return "WARN";
        case Log::Level::Error:   return "ERROR";
    }
    return "INFO";
}

std::string ConsoleModel::FormatRow(const ConsoleRow& row) {
    const ConsoleEntry& e = row.entry;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "[%.3fs f%llu] %s ", e.timeSec, (unsigned long long)e.frame, LevelName(e.level));
    std::string out = buf;
    if (!e.category.empty()) { out += '['; out += e.category; out += "] "; }
    out += e.message;
    if (row.repeat > 1) out += " (x" + std::to_string(row.repeat) + ")";
    return out;
}

std::string ConsoleModel::CopyAll(bool visibleOnly) const {
    std::string out;
    if (visibleOnly) {
        for (const ConsoleRow& r : Rows()) { out += FormatRow(r); out += '\n'; }
    } else {
        for (const ConsoleEntry& e : Entries()) {
            ConsoleRow r;
            r.entry = e;
            out += FormatRow(r); out += '\n';
        }
    }
    return out;
}

} // namespace okay
