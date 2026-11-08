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
#include <limits>
#include <locale>

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
/// system locale can't change it).
inline std::string FormatFloat(float v) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o.precision(std::numeric_limits<float>::max_digits10);
    o << v;
    return o.str();
}
/// Read a float written by FormatFloat (or by older versions); `def` if it isn't one.
inline float ParseFloat(const std::string& s, float def = 0.0f) {
    std::istringstream is(s);
    is.imbue(std::locale::classic());
    float v = 0.0f;
    if (is >> v) return v;
    return def;
}
/// FNV-1a 64-bit.
inline std::uint64_t Fnv1a(const char* data, std::size_t n, std::uint64_t h = 1469598103934665603ull) {
    for (std::size_t i = 0; i < n; ++i) { h ^= (unsigned char)data[i]; h *= 1099511628211ull; }
    return h;
}
inline std::string Hex16(std::uint64_t v) {
    char b[17]; std::snprintf(b, sizeof(b), "%016llx", (unsigned long long)v); return b;
}

/// Write `content` to `path` by writing `path.tmp` and renaming it over `path`, so a
/// crash or a full disk never leaves a half-written file where a good one was.
/// False (and the old file untouched) if anything fails.
inline bool AtomicWrite(const std::string& path, const std::string& content) {
    namespace fs = std::filesystem;
    const std::string tmp = path + ".tmp";
    std::error_code ec;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(content.data(), (std::streamsize)content.size());
        f.flush();
        if (!f) { f.close(); fs::remove(tmp, ec); return false; }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        // Some platforms won't rename over an existing file: only a plain file is replaced.
        if (fs::is_regular_file(path, ec)) { fs::remove(path, ec); fs::rename(tmp, path, ec); }
        if (ec || !fs::is_regular_file(path, ec)) { fs::remove(tmp, ec); return false; }
    }
    return true;
}

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
class SaveFile {
public:
    void SetFloat (const std::string& k, float v)              { m_data[k] = "f:" + Num(v); }
    void SetInt   (const std::string& k, int v)                { m_data[k] = "i:" + std::to_string(v); }
    void SetBool  (const std::string& k, bool v)               { m_data[k] = std::string("b:") + (v ? "1" : "0"); }
    void SetString(const std::string& k, const std::string& v) { m_data[k] = "s:" + v; }
    void SetVec3  (const std::string& k, const Vec3& v)        { m_data[k] = "v:" + Num(v.x) + "," + Num(v.y) + "," + Num(v.z); }

    float GetFloat(const std::string& k, float def = 0.0f) const {
        std::string p; if (!Payload(k, p)) return def; return savefmt::ParseFloat(p, 0.0f);
    }
    int GetInt(const std::string& k, int def = 0) const {
        std::string p; if (!Payload(k, p)) return def; return std::atoi(p.c_str());
    }
    bool GetBool(const std::string& k, bool def = false) const {
        std::string p; if (!Payload(k, p)) return def; return p == "1" || p == "true";
    }
    std::string GetString(const std::string& k, const std::string& def = "") const {
        std::string p; if (!Payload(k, p)) return def; return p;
    }
    Vec3 GetVec3(const std::string& k, const Vec3& def = Vec3::Zero) const {
        std::string p; if (!Payload(k, p)) return def;
        Vec3 r = def; std::replace(p.begin(), p.end(), ',', ' ');
        std::istringstream is(p); is.imbue(std::locale::classic()); is >> r.x >> r.y >> r.z; return r;
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
    /// escaped onto a single line). The file is written aside and swapped in, so a
    /// crash or a full disk leaves the old file alone; false if anything failed.
    bool Save(const std::string& path) const {
        std::string out = "okaysave 2\n";
        for (auto& kv : m_data) out += savefmt::Escape(kv.first, true) + '=' + savefmt::Escape(kv.second) + '\n';
        return savefmt::AtomicWrite(path, out);
    }
    /// Replace contents with `path`. A missing file leaves the store empty and
    /// returns false (so callers can tell "no save yet" from "loaded"). Files
    /// from before escaping (`okaysave 1`, or no header) load as plain text.
    bool Load(const std::string& path) {
        m_data.clear();
        std::ifstream f(path); if (!f) return false;
        std::string line;
        std::getline(f, line);
        bool escaped = false;
        if (line.rfind("okaysave", 0) == 0) escaped = std::atoi(line.c_str() + 8) >= 2;
        else if (!line.empty()) AddLine(line, false);
        while (std::getline(f, line)) AddLine(line, escaped);
        return true;
    }

private:
    std::unordered_map<std::string, std::string> m_data;

    static std::string Num(float v) { return savefmt::FormatFloat(v); }
    bool Payload(const std::string& k, std::string& out) const {
        auto it = m_data.find(k);
        if (it == m_data.end()) return false;
        const std::string& s = it->second;
        std::size_t c = s.find(':');
        out = (c == std::string::npos) ? s : s.substr(c + 1);
        return true;
    }
    void AddLine(const std::string& line, bool escaped) {
        if (line.empty()) return;
        std::size_t eq = escaped ? savefmt::FindSeparator(line) : line.find('=');
        if (eq == std::string::npos) return;
        if (escaped) m_data[savefmt::Unescape(line.substr(0, eq))] = savefmt::Unescape(line.substr(eq + 1));
        else m_data[line.substr(0, eq)] = line.substr(eq + 1);
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

    static SaveFile& File(const std::string& path) {
        std::string key = Resolve(path);
        auto& m = Files();
        auto it = m.find(key);
        if (it == m.end()) { SaveFile sf; sf.Load(key); it = m.emplace(key, std::move(sf)).first; }
        return it->second;
    }
    static bool Flush(const std::string& path) { return File(path).Save(Resolve(path)); }

    // Write-through helpers (set + persist immediately), Easy-Save-3 style.
    static void SetFloat (const std::string& k, float v, const std::string& file = DefaultFile())              { File(file).SetFloat(k, v);  Flush(file); }
    static void SetInt   (const std::string& k, int v, const std::string& file = DefaultFile())                { File(file).SetInt(k, v);    Flush(file); }
    static void SetBool  (const std::string& k, bool v, const std::string& file = DefaultFile())               { File(file).SetBool(k, v);   Flush(file); }
    static void SetString(const std::string& k, const std::string& v, const std::string& file = DefaultFile()) { File(file).SetString(k, v); Flush(file); }
    static void SetVec3  (const std::string& k, const Vec3& v, const std::string& file = DefaultFile())         { File(file).SetVec3(k, v);   Flush(file); }

    static float       GetFloat (const std::string& k, float def = 0.0f, const std::string& file = DefaultFile())              { return File(file).GetFloat(k, def); }
    static int         GetInt   (const std::string& k, int def = 0, const std::string& file = DefaultFile())                   { return File(file).GetInt(k, def); }
    static bool        GetBool  (const std::string& k, bool def = false, const std::string& file = DefaultFile())              { return File(file).GetBool(k, def); }
    static std::string GetString(const std::string& k, const std::string& def = "", const std::string& file = DefaultFile())  { return File(file).GetString(k, def); }
    static Vec3        GetVec3  (const std::string& k, const Vec3& def = Vec3::Zero, const std::string& file = DefaultFile())   { return File(file).GetVec3(k, def); }

    static bool Has(const std::string& k, const std::string& file = DefaultFile())    { return File(file).Has(k); }
    static void Delete(const std::string& k, const std::string& file = DefaultFile()) { File(file).Delete(k); Flush(file); }
    static void Clear(const std::string& file = DefaultFile())                        { File(file).Clear();   Flush(file); }
    static bool FileExists(const std::string& file = DefaultFile()) {
        std::ifstream f(Resolve(file)); return (bool)f;
    }
    static bool DeleteFile(const std::string& file = DefaultFile()) {
        std::string key = Resolve(file);
        Files().erase(key);
        return std::remove(key.c_str()) == 0;
    }

private:
    static std::unordered_map<std::string, SaveFile>& Files() {
        static std::unordered_map<std::string, SaveFile> m; return m;
    }
};

} // namespace okay
