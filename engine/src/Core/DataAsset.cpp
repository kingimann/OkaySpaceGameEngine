#include "okay/Core/DataAsset.hpp"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <cstdlib>

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
    for (const auto& f : m_fields) if (f.first == key) return std::atof(f.second.c_str());
    return def;
}

void DataAsset::Set(const std::string& key, const std::string& value) {
    for (auto& f : m_fields) if (f.first == key) { f.second = value; return; }
    m_fields.emplace_back(key, value);
}

void DataAsset::Remove(const std::string& key) {
    for (std::size_t i = 0; i < m_fields.size(); ++i)
        if (m_fields[i].first == key) { m_fields.erase(m_fields.begin() + i); return; }
}

bool DataAsset::Save(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    for (const auto& f : m_fields) out << f.first << " = " << f.second << "\n";
    return true;
}

bool DataAsset::Load(const std::string& path) {
    m_fields.clear();
    std::ifstream in(path);
    if (!in) {
        if (g_baseDir.empty()) return false;
        in.open(g_baseDir + path);          // retry relative to the game's base dir
        if (!in) return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        std::size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        m_fields.emplace_back(Trim(t.substr(0, eq)), Trim(t.substr(eq + 1)));
    }
    return true;
}

void DataAsset::SetBaseDir(const std::string& dir) { g_baseDir = dir; }

DataAsset& DataAsset::Cached(const std::string& path) {
    auto it = g_cache.find(path);
    if (it != g_cache.end()) return it->second;
    DataAsset& a = g_cache[path];
    a.Load(path);
    return a;
}

} // namespace okay
