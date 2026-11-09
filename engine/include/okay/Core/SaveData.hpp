#pragma once
#include "okay/Math/Vec3.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <functional>
#include <limits>
#include <locale>
#include <mutex>

namespace okay {

/// Small helpers the save writers share (SaveFile, SaveGame, DataAsset):
/// line escaping, locale-proof float text, a checksum and crash-safe file writes.
namespace savefmt {

/// One value on one line: backslash, newline, carriage return and tab are
/// escaped; with `key` set, '=' is too, so it can't be taken for the separator.
inline std::string Escape(const std::string& s, bool key = false) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c == '=' && key) o += "\\=";
        else o += c;
    }
    return o;
}
/// Undo Escape. `\s` is a space (used where leading/trailing blanks must survive).
inline std::string Unescape(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            o += n == 'n' ? '\n' : n == 't' ? '\t' : n == 'r' ? '\r' : n == 's' ? ' ' : n;
        } else o += s[i];
    }
    return o;
}
/// Position of the first '=' that isn't escaped, or npos.
inline std::size_t FindSeparator(const std::string& s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\') ++i;
        else if (s[i] == '=') return i;
    }
    return std::string::npos;
}
/// A float as text that reads back bit-exact, in the classic locale (a comma-decimal
/// system locale can't change it). Infinities and NaN are "inf", "-inf" and "nan", so
/// they read back too (a stream writes them differently on each platform).
inline std::string FormatFloat(float v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o.precision(std::numeric_limits<float>::max_digits10);
    o << v;
    return o.str();
}
/// Read a float written by FormatFloat (or by older versions, which wrote "inf" and "nan"
/// the same way on most systems). False (and `out` untouched) if `s` isn't a number.
inline bool TryParseFloat(const std::string& s, float& out) {
    std::istringstream is(s);
    is.imbue(std::locale::classic());
    float v = 0.0f;
    if (is >> v) { out = v; return true; }
    // The stream reader refuses the words for infinity and NaN.
    std::string w;
    for (char c : s) if (c != ' ' && c != '\t' && c != '\r' && c != '\n') w += (char)std::tolower((unsigned char)c);
    if (w == "inf" || w == "+inf" || w == "infinity" || w == "+infinity") { out = std::numeric_limits<float>::infinity(); return true; }
    if (w == "-inf" || w == "-infinity") { out = -std::numeric_limits<float>::infinity(); return true; }
    if (w == "nan" || w == "+nan" || w == "-nan") { out = std::numeric_limits<float>::quiet_NaN(); return true; }
    return false;
}
/// TryParseFloat, with `def` when `s` isn't a number.
inline float ParseFloat(const std::string& s, float def = 0.0f) {
    float v = 0.0f;
    return TryParseFloat(s, v) ? v : def;
}
/// An int out of text: digits, or a float's text ("1e+09", "3.7": cut toward zero). Values
/// beyond the int range are clamped (atoi is undefined there), NaN is 0. `ok` (when given)
/// says whether the text held a number at all; a text without one is 0.
inline int ParseInt(const std::string& s, bool* ok = nullptr) {
    const char* b = s.c_str();
    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(b, &end, 10);
    if (ok) *ok = end != b;
    if (end == b) return 0;
    if (*end == '.' || *end == 'e' || *end == 'E') {   // a float's text: read it whole
        std::istringstream is(s);
        is.imbue(std::locale::classic());
        double d = 0.0;
        if (is >> d) {
            if (std::isnan(d)) return 0;
            if (d >= 2147483647.0) return std::numeric_limits<int>::max();
            if (d <= -2147483648.0) return std::numeric_limits<int>::min();
            return (int)d;
        }
    }
    if (errno == ERANGE || v > std::numeric_limits<int>::max()) return v < 0 ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
    if (v < std::numeric_limits<int>::min()) return std::numeric_limits<int>::min();
    return (int)v;
}
/// FNV-1a 64-bit.
inline std::uint64_t Fnv1a(const char* data, std::size_t n, std::uint64_t h = 1469598103934665603ull) {
    for (std::size_t i = 0; i < n; ++i) { h ^= (unsigned char)data[i]; h *= 1099511628211ull; }
    return h;
}
inline std::string Hex16(std::uint64_t v) {
    char b[17]; std::snprintf(b, sizeof(b), "%016llx", (unsigned long long)v); return b;
}

/// Options for AtomicWriteChecked.
struct WriteOptions {
    bool sync = false;        ///< flush the data to the disk before the swap (slower; survives a power cut)
    bool createDirs = false;  ///< create the missing folders above `path`
    bool utf8Path = false;    ///< `path` is UTF-8 text (std::filesystem::u8path): non-ASCII names work on Windows
};

/// Write `content` to `path` by writing `path.tmp` and renaming it over `path`, so a
/// crash or a full disk never leaves a half-written file where a good one was. On failure
/// the old file is untouched, no temp file is left, and `error` (when given) says why
/// ("the disk is full", "permission denied", "no such folder"...). Writers to the same
/// path in one process take turns (they share the temp name). Defined in
/// engine/src/Core/SaveFile.cpp.
bool AtomicWriteChecked(const std::string& path, const std::string& content, std::string* error = nullptr,
                        const WriteOptions& options = {});

/// AtomicWriteChecked without the reason or the options.
inline bool AtomicWrite(const std::string& path, const std::string& content) {
    return AtomicWriteChecked(path, content, nullptr);
}

/// Call before replacing `path` with a new write: when the file there is good (`isGood` says
/// so for its bytes) it is copied to `path.bak`, the one-step-back copy a reader falls back to.
/// When it is damaged and `path.bak` is not a good file either, it is moved to `path.corrupt`
/// so its bytes can still be looked at. A good `.bak` is never replaced by a damaged file.
/// Nothing happens when there is no file. Defined in engine/src/Core/SaveFile.cpp.
void KeepBackup(const std::string& path, const std::function<bool(const std::string& bytes)>& isGood,
                std::size_t maxBytes = 256u * 1024u * 1024u);
/// KeepBackup for a file this process wrote itself a moment ago (the write-through `Save`
/// facade): the file is trusted, so it is copied to `path.bak` without being read and checked.
void KeepBackupTrusted(const std::string& path);

/// True for names a Windows file system treats as devices (CON, PRN, AUX, NUL, COM1-9,
/// LPT1-9, any case): a save named like that would not be a file there.
bool IsReservedDeviceName(const std::string& name);

/// Read a whole file, refusing one bigger than `maxBytes` (error says why).
inline bool ReadFileCapped(const std::string& path, std::size_t maxBytes, std::string& out, std::string* error = nullptr) {
    out.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "can't open " + path; return false; }
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0) { if (error) *error = "can't read " + path; return false; }
    if ((unsigned long long)size > maxBytes) { if (error) *error = path + " is too large (" + std::to_string((long long)size) + " bytes)"; return false; }
    f.seekg(0);
    out.resize((std::size_t)size);
    if (size > 0) f.read(&out[0], size);
    if (!f && size > 0) { out.clear(); if (error) *error = "can't read " + path; return false; }
    return true;
}

} // namespace savefmt

/// A single save file — a typed key/value store you can write to disk and read
/// back, modeled on "Easy Save 3". Unlike Prefs (one global bag of
/// strings), a SaveFile holds typed values (float / int / bool / string / Vec3),
/// you can have as many files as you like (slots, profiles, per-level data), and
/// each key remembers its type. Values are stored as `type:payload` lines:
///   coins=i:120
///   name=s:Hero
///   spawn=v:1.5,0,3
/// A file ends with `#end` and a `#sum` checksum line (format `okaysave 3`), so a
/// file that was cut off or damaged is noticed; the previous good file is kept as
/// `<file>.bak` and read instead when the main one fails. Not thread safe on its
/// own (the `Save` facade below is).
class SaveFile {
public:
    /// The newest format this build writes. Files from before the end marker
    /// (`okaysave 2`, `okaysave 1`, no header) still load.
    static constexpr int kFormatVersion = 3;
    /// A file bigger than this is refused when loading.
    static constexpr std::size_t kMaxFileBytes = 64u * 1024u * 1024u;

    void SetFloat (const std::string& k, float v)              { m_data[k] = "f:" + Num(v); }
    void SetInt   (const std::string& k, int v)                { m_data[k] = "i:" + std::to_string(v); }
    void SetBool  (const std::string& k, bool v)               { m_data[k] = std::string("b:") + (v ? "1" : "0"); }
    void SetString(const std::string& k, const std::string& v) { m_data[k] = "s:" + v; }
    void SetVec3  (const std::string& k, const Vec3& v)        { m_data[k] = "v:" + Num(v.x) + "," + Num(v.y) + "," + Num(v.z); }

    float GetFloat(const std::string& k, float def = 0.0f) const {
        std::string p; if (!Payload(k, p)) return def; return savefmt::ParseFloat(p, 0.0f);
    }
    int GetInt(const std::string& k, int def = 0) const {
        std::string p; if (!Payload(k, p)) return def; return savefmt::ParseInt(p);
    }
    bool GetBool(const std::string& k, bool def = false) const {
        std::string p; if (!Payload(k, p)) return def; return p == "1" || p == "true";
    }
    std::string GetString(const std::string& k, const std::string& def = "") const {
        std::string p; if (!Payload(k, p)) return def; return p;
    }
    /// A Vec3 key; `def` when the text isn't three numbers.
    Vec3 GetVec3(const std::string& k, const Vec3& def = Vec3::Zero) const {
        std::string p; if (!Payload(k, p)) return def;
        std::replace(p.begin(), p.end(), ',', ' ');
        std::istringstream is(p); is.imbue(std::locale::classic());
        std::string a, b, c;
        if (!(is >> a >> b >> c)) return def;
        return Vec3(savefmt::ParseFloat(a, def.x), savefmt::ParseFloat(b, def.y), savefmt::ParseFloat(c, def.z));
    }

    bool Has(const std::string& k) const { return m_data.find(k) != m_data.end(); }
    void Delete(const std::string& k)    { m_data.erase(k); }
    void Clear()                         { m_data.clear(); }
    std::size_t Count() const            { return m_data.size(); }
    std::vector<std::string> Keys() const {
        std::vector<std::string> ks; ks.reserve(m_data.size());
        for (auto& kv : m_data) ks.push_back(kv.first);
        return ks;
    }
    /// Raw `type:payload` store, for tools that want to inspect/edit entries.
    const std::unordered_map<std::string, std::string>& Raw() const { return m_data; }
    void SetRaw(const std::string& k, const std::string& typedValue) { m_data[k] = typedValue; }

    /// Write every key to `path` (one `key=type:payload` line each, keys and values
    /// escaped onto a single line, keys in order so the same keys give the same bytes).
    /// The file is written aside and swapped in, so a crash or a full disk leaves the old
    /// file alone, and the old file (when it was a good one) becomes `<path>.bak`; a damaged
    /// one with no good backup is kept as `<path>.corrupt`. `durable` also flushes the data
    /// to the disk before the swap. `checkOld` false copies the old file to `.bak` without reading
    /// it first (for a file this program wrote a moment ago). False if anything failed (`error` says why).
    bool Save(const std::string& path, std::string* error = nullptr, bool durable = true, bool checkOld = true) const;
    /// Replace contents with `path`. A missing file leaves the store empty and
    /// returns false (so callers can tell "no save yet" from "loaded"). A file that is cut
    /// off, edited so it fails its checksum, or not a save at all is damaged: its `.bak` is
    /// read instead (true, with `LoadedFromBackup()` set and `LastError()` saying what was
    /// wrong), and with no good backup the store is empty and the result false. Files from
    /// before escaping (`okaysave 1`, or no header) load as plain text.
    bool Load(const std::string& path);
    /// Why the last Load or Save failed or fell back ("" when it went fine).
    const std::string& LastError() const { return m_error; }
    /// True when the last Load read `<path>.bak` because the main file was damaged.
    bool LoadedFromBackup() const { return m_fromBackup; }

private:
    std::unordered_map<std::string, std::string> m_data;
    mutable std::string m_error;
    bool m_fromBackup = false;
    mutable bool m_recheckOld = false;   // the last Load met a damaged file: the next Save checks what it replaces

    static std::string Num(float v) { return savefmt::FormatFloat(v); }
    bool Payload(const std::string& k, std::string& out) const {
        auto it = m_data.find(k);
        if (it == m_data.end()) return false;
        const std::string& s = it->second;
        std::size_t c = s.find(':');
        out = (c == std::string::npos) ? s : s.substr(c + 1);
        return true;
    }
};

/// Easy-Save-style global facade: name a file once and Save/Load keys into it.
/// Files are cached in memory, so repeated saves to the same file are cheap and
/// a single Flush() (or the write-through Save* helpers) persists them to disk.
/// The default file is "save.okaysave" beside the game.
class Save {
public:
    static const std::string& DefaultFile() { static std::string f = "save.okaysave"; return f; }

    /// Folder that relative save files resolve into.
    /// The player points this at a per-user, writable app folder so saves work in
    /// read-only install locations and don't clash between games. Empty = current
    /// dir (the legacy behaviour). Absolute file paths are never relocated.
    static std::string& BaseDir() { static std::string d; return d; }
    static void SetBaseDir(const std::string& dir) {
        std::string d = dir;
        if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
        BaseDir() = d;
    }
    /// True if `name` is a safe relative file name for a game script to use:
    /// not empty, not absolute ("/x", "\\x", "C:x", "\\\\server\\x"), no ".."
    /// part and no NUL byte. Scripts come from game content, so they must not
    /// reach files outside the game's own folders.
    static bool IsSafeRelative(const std::string& name) {
        if (name.empty() || name.find('\0') != std::string::npos) return false;
        if (name[0] == '/' || name[0] == '\\' || (name.size() > 1 && name[1] == ':')) return false;
        std::size_t start = 0;
        while (start <= name.size()) {
            std::size_t end = name.find_first_of("/\\", start);
            if (end == std::string::npos) end = name.size();
            if (name.compare(start, end - start, "..") == 0 && end - start == 2) return false;
            start = end + 1;
        }
        return true;
    }
    /// A script-supplied file name resolved into the save folder; false (and
    /// `out` cleared) if the name could escape it.
    static bool SafeResolve(const std::string& name, std::string& out) {
        out.clear();
        if (!IsSafeRelative(name)) return false;
        out = Resolve(name);
        return true;
    }
    static std::string Resolve(const std::string& path) {
        const std::string& b = BaseDir();
        if (b.empty() || path.empty()) return path;
        bool abs = path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':');
        return abs ? path : b + path;
    }

    /// The cached SaveFile for `path`, loaded from disk on first use. The calls below
    /// (Set*, Get*, Has, Delete, Clear, Flush, FileExists, DeleteFile) take a lock, so
    /// they may be used from several threads; a `SaveFile&` kept from here is not guarded.
    static SaveFile& File(const std::string& path) {
        std::string key = Resolve(path);
        std::lock_guard<std::recursive_mutex> lock(Mutex());
        auto& m = Files();
        auto it = m.find(key);
        if (it == m.end()) {
            SaveFile sf;
            sf.Load(key);
            if (!sf.LastError().empty()) LastErrorRef() = sf.LastError();   // damaged: says so (and whether a backup was read)
            it = m.emplace(key, std::move(sf)).first;
        }
        return it->second;
    }
    /// Write the file to disk, creating its folder. False when that failed; `LastError()`
    /// says why (full disk, no permission...). The Set* helpers below flush too but have
    /// no result: read `LastError()` after them.
    static bool Flush(const std::string& path) {
        const std::string resolved = Resolve(path);
        std::lock_guard<std::recursive_mutex> lock(Mutex());
        SaveFile& sf = File(path);
        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(resolved).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
        std::string err;
        // Write-through is frequent, so it is not flushed to the disk each time (durable = false).
        const bool ok = sf.Save(resolved, &err, false, false);
        LastErrorRef() = ok ? std::string() : err;
        return ok;
    }
    /// Why the last Flush (or the load of a file's first use) failed or fell back to a
    /// backup; "" when it went fine.
    static std::string LastError() {
        std::lock_guard<std::recursive_mutex> lock(Mutex());
        return LastErrorRef();
    }

    // Write-through helpers (set + persist immediately), Easy-Save-3 style.
    static void SetFloat (const std::string& k, float v, const std::string& file = DefaultFile())              { Locked([&] { File(file).SetFloat(k, v);  Flush(file); }); }
    static void SetInt   (const std::string& k, int v, const std::string& file = DefaultFile())                { Locked([&] { File(file).SetInt(k, v);    Flush(file); }); }
    static void SetBool  (const std::string& k, bool v, const std::string& file = DefaultFile())               { Locked([&] { File(file).SetBool(k, v);   Flush(file); }); }
    static void SetString(const std::string& k, const std::string& v, const std::string& file = DefaultFile()) { Locked([&] { File(file).SetString(k, v); Flush(file); }); }
    static void SetVec3  (const std::string& k, const Vec3& v, const std::string& file = DefaultFile())         { Locked([&] { File(file).SetVec3(k, v);   Flush(file); }); }

    static float       GetFloat (const std::string& k, float def = 0.0f, const std::string& file = DefaultFile())              { return Locked([&] { return File(file).GetFloat(k, def); }); }
    static int         GetInt   (const std::string& k, int def = 0, const std::string& file = DefaultFile())                   { return Locked([&] { return File(file).GetInt(k, def); }); }
    static bool        GetBool  (const std::string& k, bool def = false, const std::string& file = DefaultFile())              { return Locked([&] { return File(file).GetBool(k, def); }); }
    static std::string GetString(const std::string& k, const std::string& def = "", const std::string& file = DefaultFile())  { return Locked([&] { return File(file).GetString(k, def); }); }
    static Vec3        GetVec3  (const std::string& k, const Vec3& def = Vec3::Zero, const std::string& file = DefaultFile())   { return Locked([&] { return File(file).GetVec3(k, def); }); }

    static bool Has(const std::string& k, const std::string& file = DefaultFile())    { return Locked([&] { return File(file).Has(k); }); }
    static void Delete(const std::string& k, const std::string& file = DefaultFile()) { Locked([&] { File(file).Delete(k); Flush(file); }); }
    static void Clear(const std::string& file = DefaultFile())                        { Locked([&] { File(file).Clear();   Flush(file); }); }
    static bool FileExists(const std::string& file = DefaultFile()) {
        std::ifstream f(Resolve(file)); return (bool)f;
    }
    /// Read the file again from the disk, dropping the cached keys (a file changed by another
    /// program, or a profile switch). Like SaveFile::Load: false for a missing or unreadable
    /// file; `LastError()` says when a backup was used.
    static bool Reload(const std::string& file = DefaultFile()) {
        const std::string key = Resolve(file);
        std::lock_guard<std::recursive_mutex> lock(Mutex());
        SaveFile& sf = File(file);
        const bool ok = sf.Load(key);
        LastErrorRef() = sf.LastError();
        return ok;
    }
    /// Forget the file's keys and remove it from the disk with its backup and leftovers.
    static bool DeleteFile(const std::string& file = DefaultFile()) {
        const std::string key = Resolve(file);
        std::lock_guard<std::recursive_mutex> lock(Mutex());
        Files().erase(key);
        for (const char* suffix : {".bak", ".tmp", ".corrupt"}) std::remove((key + suffix).c_str());
        return std::remove(key.c_str()) == 0;
    }

private:
    static std::recursive_mutex& Mutex() { static std::recursive_mutex m; return m; }
    template <class F> static auto Locked(F&& f) -> decltype(f()) {
        std::lock_guard<std::recursive_mutex> lock(Mutex());
        return f();
    }
    static std::string& LastErrorRef() { static std::string e; return e; }
    static std::unordered_map<std::string, SaveFile>& Files() {
        static std::unordered_map<std::string, SaveFile> m; return m;
    }
};

} // namespace okay
