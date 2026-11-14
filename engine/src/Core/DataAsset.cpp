#include "okay/Core/DataAsset.hpp"
#include "okay/Core/SaveData.hpp"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace okay {
namespace {
std::string g_baseDir;
std::unordered_map<std::string, DataAsset> g_cache;

std::string Trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
} // namespace

bool DataAsset::Has(const std::string& key) const {
    for (const auto& f : m_fields) if (f.first == key) return true;
    return false;
}

std::string DataAsset::GetString(const std::string& key, const std::string& def) const {
    for (const auto& f : m_fields) if (f.first == key) return f.second;
    return def;
}

double DataAsset::GetNumber(const std::string& key, double def) const {
    for (const auto& f : m_fields) if (f.first == key) {
        const double v = std::strtod(f.second.c_str(), nullptr);   // what atof reads
        return std::isfinite(v) ? v : def;                         // "nan", "inf" and "1e999" are not numbers a game can use
    }
    return def;
}

int DataAsset::GetInt(const std::string& key, int def) const {
    const double v = GetNumber(key, std::numeric_limits<double>::quiet_NaN());
    if (v != v) return def;   // missing, or not a usable number
    if (v >= 2147483647.0) return std::numeric_limits<int>::max();
    if (v <= -2147483648.0) return std::numeric_limits<int>::min();
    return (int)v;
}

bool DataAsset::GetBool(const std::string& key, bool def) const {
    for (const auto& f : m_fields) if (f.first == key) {
        std::string v = Trim(f.second);
        for (char& c : v) c = (char)std::tolower((unsigned char)c);
        if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
        if (v == "false" || v == "no" || v == "off" || v == "0") return false;
        return def;
    }
    return def;
}

bool DataAsset::TryGetNumber(const std::string& key, double& out) const {
    for (const auto& f : m_fields) if (f.first == key) {
        const std::string t = Trim(f.second);
        if (t.empty()) return false;
        char* end = nullptr;
        const double v = std::strtod(t.c_str(), &end);
        if (!end || *end != '\0' || !std::isfinite(v)) return false;
        out = v;
        return true;
    }
    return false;
}

void DataAsset::Set(const std::string& key, const std::string& value) {
    for (auto& f : m_fields) if (f.first == key) { f.second = value; return; }
    m_fields.emplace_back(key, value);
}

void DataAsset::Remove(const std::string& key) {
    for (std::size_t i = 0; i < m_fields.size(); ++i)
        if (m_fields[i].first == key) { m_fields.erase(m_fields.begin() + i); return; }
}

namespace {
const char* const kHeader = "# okaydata 2";   // marks escaped keys/values
// Edge blanks are written as \s so the loader's trimming can't eat them; '#' at the
// start of a key as \#.
std::string EncodeField(const std::string& v, bool key) {
    std::string e = savefmt::Escape(v, key);
    if (!e.empty() && e.front() == ' ') e.replace(0, 1, "\\s");
    if (!e.empty() && e.back() == ' ') e.replace(e.size() - 1, 1, "\\s");
    if (key && !e.empty() && e.front() == '#') e.insert(0, "\\");
    return e;
}
} // namespace

bool DataAsset::Save(const std::string& path) const {
    std::string out = std::string(kHeader) + "\n";
    for (const auto& f : m_fields) out += EncodeField(f.first, true) + " = " + EncodeField(f.second, false) + "\n";
    return savefmt::AtomicWrite(path, out);
}

bool DataAsset::Load(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        if (g_baseDir.empty()) return false;
        std::string full = g_baseDir;       // retry relative to the game's base dir
        if (full.back() != '/' && full.back() != '\\') full += '/';
        in.open(full + path);
        if (!in) return false;
    }
    // Only a file that opened replaces the fields.
    std::vector<Field> fields;
    bool escaped = false, first = true;
    std::string line;
    while (std::getline(in, line)) {
        if (first && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);   // a byte order mark from a text editor
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = Trim(line);
        if (first) { first = false; escaped = (t == kHeader); }
        if (t.empty() || t[0] == '#') continue;
        // A '=' that is escaped (\=) belongs to the key.
        std::size_t eq = escaped ? savefmt::FindSeparator(t) : t.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Trim(t.substr(0, eq)), v = Trim(t.substr(eq + 1));
        if (escaped) { k = savefmt::Unescape(k); v = savefmt::Unescape(v); }
        fields.emplace_back(std::move(k), std::move(v));
    }
    if (in.bad()) return false;
    m_fields = std::move(fields);
    return true;
}

void DataAsset::SetBaseDir(const std::string& dir) { g_baseDir = dir; }
const std::string& DataAsset::BaseDir() { return g_baseDir; }

DataAsset& DataAsset::Cached(const std::string& path) {
    DataAsset& a = g_cache[path];
    // An entry whose file wasn't there is only a place for scripts to Set() into: while
    // it is still untouched, every call tries the file again, so creating it later works.
    if (!a.m_loaded && a.m_fields.empty() && a.Load(path)) a.m_loaded = true;
    return a;
}

bool DataAsset::Reload(const std::string& path) {
    DataAsset a;
    if (!a.Load(path)) return false;     // keep what was cached
    a.m_loaded = true;
    g_cache[path] = std::move(a);        // assigned in place: earlier references stay valid
    return true;
}

// Emptied in place rather than erased: a DataAsset& the caller still holds (a script's cached lookup, a
// panel) must not dangle. An emptied, unloaded entry is read from disk again by the next Cached().
void DataAsset::Invalidate(const std::string& path) {
    auto it = g_cache.find(path);
    if (it == g_cache.end()) return;
    it->second.m_fields.clear();
    it->second.m_loaded = false;
}
void DataAsset::InvalidateAll() {
    for (auto& kv : g_cache) { kv.second.m_fields.clear(); kv.second.m_loaded = false; }
}

} // namespace okay
