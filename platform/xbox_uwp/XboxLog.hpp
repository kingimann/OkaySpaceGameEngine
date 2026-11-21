#pragma once
// A plain text log file for the Xbox UWP app, written into the app's LocalFolder\logs so the owner can read it from the Windows Device Portal
// (Apps > the app's "File explorer" / "Browse" page, LocalAppData > <package> > LocalState > logs) or, when the app runs on a PC, from
// %LOCALAPPDATA%\Packages\<package family name>\LocalState\logs. Pure C++17: no Windows headers, no <mutex> / <thread> (rule 21a: an atomic flag guards
// the one writer). Every line is flushed at once, so the file is complete even when the system ends the app. When the file passes `maxBytes` it
// is renamed to <name>.1.log (the older .1.log is dropped), so the folder never holds more than about twice `maxBytes`.
// Written from our own design (docs/platform/xbox_uwp.md, "Logs"); nothing copied. UNVERIFIED on Windows or an Xbox; tested on Linux.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace okay::xbox {

class FileLog {
public:
    FileLog() : t0_(std::chrono::steady_clock::now()) {}
    FileLog(const FileLog&) = delete;
    FileLog& operator=(const FileLog&) = delete;
    ~FileLog() { Close(); }

    /// Opens <dir>/<name>.log for appending (creating <dir>). False (and Error() says why) when the folder cannot be written; the log then drops lines.
    bool Open(const std::string& dir, const std::string& name = "okayspace", std::uint64_t maxBytes = 512 * 1024) {
        Lock l(flag_);
        CloseLocked();
        dir_ = dir; name_ = name; max_ = maxBytes < 1024 ? 1024 : maxBytes; error_.clear();
        std::error_code ec; std::filesystem::create_directories(std::filesystem::u8path(dir_), ec);
        if (ec) { error_ = "cannot create the log folder " + dir_ + ": " + ec.message(); return false; }
        return OpenLocked();
    }
    bool IsOpen() const { return f_ != nullptr; }
    const std::string& Error() const { return error_; }
    std::string Path() const { return dir_.empty() ? std::string() : dir_ + "/" + name_ + ".log"; }

    /// One line, with the seconds since the log object was made in front. A newline inside `text` becomes a new indented line.
    void Line(const std::string& text) {
        Lock l(flag_);
        if (!f_) return;
        std::string out; out.reserve(text.size() + 16);
        char stamp[32]; std::snprintf(stamp, sizeof stamp, "[%9.3f] ", Seconds()); out += stamp;
        for (char c : text) { if (c == '\r') continue; out += c; if (c == '\n') out += "            "; }
        out += '\n';
        if (std::fwrite(out.data(), 1, out.size(), f_) != out.size()) { error_ = "the log file could not be written (disk full?)"; CloseLocked(); return; }
        std::fflush(f_);
        written_ += out.size();
        if (written_ >= max_) RotateLocked();
    }
    void Close() { Lock l(flag_); CloseLocked(); }

private:
    struct Lock { std::atomic_flag& f; explicit Lock(std::atomic_flag& x) : f(x) { while (f.test_and_set(std::memory_order_acquire)) {} } ~Lock() { f.clear(std::memory_order_release); } };
    double Seconds() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count(); }
    bool OpenLocked() {
        const std::filesystem::path p = std::filesystem::u8path(Path());
        std::error_code ec; const std::uintmax_t size = std::filesystem::file_size(p, ec);
        written_ = ec ? 0 : static_cast<std::uint64_t>(size);
#if defined(_WIN32)
        f_ = _wfopen(p.c_str(), L"ab");
#else
        f_ = std::fopen(p.string().c_str(), "ab");
#endif
        if (!f_) { error_ = "cannot open the log file " + Path(); return false; }
        return true;
    }
    void CloseLocked() { if (f_) { std::fclose(f_); f_ = nullptr; } }
    void RotateLocked() {
        CloseLocked();
        const std::filesystem::path cur = std::filesystem::u8path(Path()), old = std::filesystem::u8path(dir_ + "/" + name_ + ".1.log");
        std::error_code ec; std::filesystem::remove(old, ec); std::filesystem::rename(cur, old, ec);
        if (ec) std::filesystem::remove(cur, ec);   // cannot keep the old one: start fresh so the file stays small
        OpenLocked();
    }
    std::chrono::steady_clock::time_point t0_;
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    std::FILE* f_ = nullptr;
    std::string dir_, name_, error_;
    std::uint64_t max_ = 512 * 1024, written_ = 0;
};

}
