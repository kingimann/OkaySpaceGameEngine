#pragma once
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace okay {

/// Minimal logging facility, similar in spirit to `Debug.Log`. Thread safe: a
/// line is formatted, then written to the console, file and sink under one lock,
/// so concurrent callers never interleave. Messages below the active level are
/// rejected before anything is formatted.
///
/// Extras (all optional, defaults keep the classic `[okay][INFO ] text` output):
///   Log::Category net("net"); net.Warning("lost ", n);   // `[okay][WARN ][net] ...`
///   Log::SetCategoryLevel("net", Log::Level::Warning);   // per-category override
///   Log::Configure("info,net=trace,physics=error");      // also read from $OKAY_LOG
///   Log::showTimestamps = true;                          // `HH:MM:SS.mmm ` prefix
///   Log::OpenFile("game.log");                           // rotating file sink
class Log {
public:
    enum class Level { Trace, Info, Warning, Error };

    /// Global threshold. Set it at startup (or through Configure); it is a plain
    /// value, so changing it while other threads log is not synchronised.
    static Level minLevel;

    /// Optional extra sink for every emitted message (level + formatted text).
    /// The editor installs one so script print/debug output shows in its Console.
    /// Called under the log lock with the bare message (no tag, category or time).
    using Sink = std::function<void(Level, const std::string&)>;
    static Sink sink;

    static bool showTimestamps;   // prefix console/file lines with HH:MM:SS.mmm
    static bool console;          // write to stdout/stderr (default true)

    template <typename... Args>
    static void Trace(Args&&... args)   { Emit(Level::Trace,   "TRACE", nullptr, std::forward<Args>(args)...); }
    template <typename... Args>
    static void Info(Args&&... args)    { Emit(Level::Info,    "INFO ", nullptr, std::forward<Args>(args)...); }
    template <typename... Args>
    static void Warning(Args&&... args) { Emit(Level::Warning, "WARN ", nullptr, std::forward<Args>(args)...); }
    template <typename... Args>
    static void Error(Args&&... args)   { Emit(Level::Error,   "ERROR", nullptr, std::forward<Args>(args)...); }

    /// A named channel. Its lines show the name and obey SetCategoryLevel.
    class Category {
    public:
        explicit Category(std::string name) : m_name(std::move(name)) {}
        const std::string& Name() const { return m_name; }
        template <typename... Args>
        void Trace(Args&&... args)   const { Log::Emit(Level::Trace,   "TRACE", m_name.c_str(), std::forward<Args>(args)...); }
        template <typename... Args>
        void Info(Args&&... args)    const { Log::Emit(Level::Info,    "INFO ", m_name.c_str(), std::forward<Args>(args)...); }
        template <typename... Args>
        void Warning(Args&&... args) const { Log::Emit(Level::Warning, "WARN ", m_name.c_str(), std::forward<Args>(args)...); }
        template <typename... Args>
        void Error(Args&&... args)   const { Log::Emit(Level::Error,   "ERROR", m_name.c_str(), std::forward<Args>(args)...); }
    private:
        std::string m_name;
    };

    /// Override the threshold for one category (beats minLevel, both ways).
    static void SetCategoryLevel(const std::string& category, Level level) {
        RecGuard lk(St().mu);
        St().cats[category] = level;
        St().anyCats = true;
    }
    static void ClearCategoryLevels() {
        RecGuard lk(St().mu);
        St().cats.clear();
        St().anyCats = false;
    }
    /// The level a category currently logs at ("" or unknown = minLevel).
    static Level EffectiveLevel(const std::string& category) {
        RecGuard lk(St().mu);
        auto it = St().cats.find(category);
        return it != St().cats.end() ? it->second : minLevel;
    }

    /// Apply a spec like "info,net=trace,physics=error": a bare level sets
    /// minLevel, name=level sets a category. Levels: trace, info, warn|warning,
    /// error (any case). Malformed entries are skipped. Returns entries applied.
    static int Configure(const std::string& spec) {
        RecGuard lk(St().mu);
        int applied = 0;
        std::size_t pos = 0;
        while (pos <= spec.size()) {
            std::size_t end = spec.find(',', pos);
            if (end == std::string::npos) end = spec.size();
            std::string tok = Trim(spec.substr(pos, end - pos));
            pos = end + 1;
            if (tok.empty()) continue;
            std::size_t eq = tok.find('=');
            Level lv;
            if (eq == std::string::npos) {
                if (!ParseLevel(tok, lv)) continue;
                minLevel = lv; ++applied;
            } else {
                std::string name = Trim(tok.substr(0, eq));
                if (name.empty() || !ParseLevel(Trim(tok.substr(eq + 1)), lv)) continue;
                St().cats[name] = lv; St().anyCats = true; ++applied;
            }
        }
        return applied;
    }

    /// Also write every line to `path` (appending). The file rotates when a line
    /// would push it past maxBytes: path -> path.1 -> path.2 ... keeping `keep`
    /// old files. Flushed on Warning and above. False if it cannot be opened.
    static bool OpenFile(const std::string& path, std::size_t maxBytes = 5u * 1024u * 1024u, int keep = 3) {
        RecGuard lk(St().mu);
        CloseFileLocked();
        std::error_code ec;
        std::uintmax_t sz = std::filesystem::file_size(path, ec);
        St().file.open(path, std::ios::app | std::ios::binary);
        if (!St().file.is_open()) { St().file.clear(); return false; }
        St().filePath = path;
        St().fileBytes = ec ? 0 : (std::size_t)sz;
        St().maxBytes = maxBytes;
        St().keep = keep < 0 ? 0 : keep;
        return true;
    }
    static void CloseFile() {
        RecGuard lk(St().mu);
        CloseFileLocked();
    }
    static bool FileOpen() {
        RecGuard lk(St().mu);
        return St().file.is_open();
    }

private:
    // A small recursive lock built on std::atomic only. <mutex> and <thread> are not
    // usable with MinGW's "win32" thread model, and this header is included everywhere.
    struct RecLock {
        std::atomic<const void*> owner{nullptr};
        int depth = 0;
        static const void* Me() { static thread_local char tag; return &tag; }
        void lock() {
            const void* me = Me();
            if (owner.load(std::memory_order_relaxed) == me) { ++depth; return; }
            const void* expected = nullptr;
            while (!owner.compare_exchange_weak(expected, me, std::memory_order_acquire)) expected = nullptr;
            depth = 1;
        }
        void unlock() { if (--depth == 0) owner.store(nullptr, std::memory_order_release); }
    };
    struct RecGuard {
        RecLock& l;
        explicit RecGuard(RecLock& lock) : l(lock) { l.lock(); }
        ~RecGuard() { l.unlock(); }
        RecGuard(const RecGuard&) = delete;
        RecGuard& operator=(const RecGuard&) = delete;
    };
    struct State {
        RecLock mu;                  // recursive: a sink may itself log
        std::map<std::string, Level> cats;
        std::atomic<bool> anyCats{false};
        std::ofstream file;
        std::string   filePath;
        std::size_t   fileBytes = 0, maxBytes = 0;
        int           keep = 0;
    };
    static State& St() { static State s; return s; }

    static std::string Trim(const std::string& s) {
        std::size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    }
    static bool ParseLevel(const std::string& s, Level& out) {
        std::string l; for (char c : s) l += (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
        if (l == "trace") out = Level::Trace;
        else if (l == "info") out = Level::Info;
        else if (l == "warn" || l == "warning") out = Level::Warning;
        else if (l == "error") out = Level::Error;
        else return false;
        return true;
    }

    /// Reads $OKAY_LOG exactly once, on the first message or Configure-free use.
    static void EnsureInit() {
        static const bool once = [] {
            if (const char* env = std::getenv("OKAY_LOG")) Configure(env);
            return true;
        }();
        (void)once;
    }

    static bool Enabled(Level level, const char* category) {
        EnsureInit();
        if (category && St().anyCats.load(std::memory_order_relaxed)) {
            RecGuard lk(St().mu);
            auto it = St().cats.find(category);
            if (it != St().cats.end()) return level >= it->second;
        }
        return level >= minLevel;
    }

    static std::string Stamp() {
        using namespace std::chrono;
        auto now = system_clock::now();
        std::time_t t = system_clock::to_time_t(now);
        int ms = (int)(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
        return buf;
    }

    static void CloseFileLocked() {
        if (St().file.is_open()) St().file.close();
        St().file.clear();
        St().filePath.clear();
    }

    static void Rotate() {
        State& s = St();
        s.file.close();
        namespace fs = std::filesystem;
        std::error_code ec;
        if (s.keep > 0) {
            fs::remove(s.filePath + "." + std::to_string(s.keep), ec);
            for (int i = s.keep - 1; i >= 1; --i)
                fs::rename(s.filePath + "." + std::to_string(i), s.filePath + "." + std::to_string(i + 1), ec);
            fs::rename(s.filePath, s.filePath + ".1", ec);
        }
        s.file.clear();
        s.file.open(s.filePath, std::ios::trunc | std::ios::binary);   // fresh file (also if keep == 0)
        s.fileBytes = 0;
    }

    static void WriteFile(Level level, const std::string& line) {
        State& s = St();
        if (!s.file.is_open()) return;
        if (s.fileBytes > 0 && s.fileBytes + line.size() > s.maxBytes) {
            Rotate();
            if (!s.file.is_open()) return;
        }
        s.file.write(line.data(), (std::streamsize)line.size());
        s.fileBytes += line.size();
        if (level >= Level::Warning) s.file.flush();
    }

    template <typename... Args>
    static void Emit(Level level, const char* tag, const char* category, Args&&... args) {
        if (!Enabled(level, category)) return;   // filtered: nothing formatted
        std::ostringstream oss;
        (oss << ... << args);
        const std::string msg = oss.str();

        RecGuard lk(St().mu);
        std::string line;
        if (showTimestamps) line += Stamp();
        line += "[okay]["; line += tag; line += ']';
        if (category) { line += '['; line += category; line += ']'; }
        line += ' '; line += msg; line += '\n';

        if (console) {
            std::ostream& out = (level == Level::Error || level == Level::Warning)
                                    ? std::cerr : std::cout;
            out << line;
        }
        WriteFile(level, line);
        if (sink) sink(level, msg);
    }
};

inline Log::Level Log::minLevel = Log::Level::Trace;
inline Log::Sink  Log::sink = nullptr;
inline bool Log::showTimestamps = false;
inline bool Log::console = true;

} // namespace okay

#define OKAY_INFO(...)  ::okay::Log::Info(__VA_ARGS__)
#define OKAY_WARN(...)  ::okay::Log::Warning(__VA_ARGS__)
#define OKAY_ERROR(...) ::okay::Log::Error(__VA_ARGS__)
#define OKAY_TRACE(...) ::okay::Log::Trace(__VA_ARGS__)
