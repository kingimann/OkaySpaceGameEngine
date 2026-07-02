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

namespace okay {

/// A single save file — a typed key/value store you can write to disk and read
/// back, modeled on Unity's "Easy Save 3". Unlike Prefs (one global bag of
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
        std::string p; if (!Payload(k, p)) return def; return (float)std::atof(p.c_str());
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
        std::istringstream is(p); is >> r.x >> r.y >> r.z; return r;
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

    /// Write every key to `path` (one `key=type:payload` line each).
    bool Save(const std::string& path) const {
        std::ofstream f(path); if (!f) return false;
        f << "okaysave 1\n";
        for (auto& kv : m_data) f << kv.first << '=' << kv.second << '\n';
        return true;
    }
    /// Replace contents with `path`. A missing file leaves the store empty and
    /// returns false (so callers can tell "no save yet" from "loaded").
    bool Load(const std::string& path) {
        m_data.clear();
        std::ifstream f(path); if (!f) return false;
        std::string line;
        std::getline(f, line);                      // header (ignored if present)
        if (line.rfind("okaysave", 0) != 0 && !line.empty()) AddLine(line);
        while (std::getline(f, line)) AddLine(line);
        return true;
    }

private:
    std::unordered_map<std::string, std::string> m_data;

    static std::string Num(float v) {
        std::ostringstream o; o << v; return o.str();
    }
    bool Payload(const std::string& k, std::string& out) const {
        auto it = m_data.find(k);
        if (it == m_data.end()) return false;
        const std::string& s = it->second;
        std::size_t c = s.find(':');
        out = (c == std::string::npos) ? s : s.substr(c + 1);
        return true;
    }
    void AddLine(const std::string& line) {
        if (line.empty()) return;
        std::size_t eq = line.find('=');
        if (eq == std::string::npos) return;
        m_data[line.substr(0, eq)] = line.substr(eq + 1);
    }
};

/// Easy-Save-style global facade: name a file once and Save/Load keys into it.
/// Files are cached in memory, so repeated saves to the same file are cheap and
/// a single Flush() (or the write-through Save* helpers) persists them to disk.
/// The default file is "save.okaysave" beside the game.
class Save {
public:
    static const std::string& DefaultFile() { static std::string f = "save.okaysave"; return f; }

    /// Folder that relative save files resolve into (Unity's persistentDataPath).
    /// The player points this at a per-user, writable app folder so saves work in
    /// read-only install locations and don't clash between games. Empty = current
    /// dir (the legacy behaviour). Absolute file paths are never relocated.
    static std::string& BaseDir() { static std::string d; return d; }
    static void SetBaseDir(const std::string& dir) {
        std::string d = dir;
        if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
        BaseDir() = d;
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
