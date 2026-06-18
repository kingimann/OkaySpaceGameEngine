#include "okay/Core/Prefs.hpp"

#include <fstream>
#include <sstream>

namespace okay {

std::unordered_map<std::string, std::string>& Prefs::Store() {
    static std::unordered_map<std::string, std::string> s;
    return s;
}

void Prefs::SetInt(const std::string& key, int value) { Store()[key] = std::to_string(value); }
void Prefs::SetFloat(const std::string& key, float value) { Store()[key] = std::to_string(value); }
void Prefs::SetString(const std::string& key, const std::string& value) { Store()[key] = value; }

int Prefs::GetInt(const std::string& key, int def) {
    auto it = Store().find(key);
    if (it == Store().end()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}
float Prefs::GetFloat(const std::string& key, float def) {
    auto it = Store().find(key);
    if (it == Store().end()) return def;
    try { return std::stof(it->second); } catch (...) { return def; }
}
std::string Prefs::GetString(const std::string& key, const std::string& def) {
    auto it = Store().find(key);
    return it != Store().end() ? it->second : def;
}

bool Prefs::Has(const std::string& key) { return Store().count(key) != 0; }
void Prefs::Delete(const std::string& key) { Store().erase(key); }
void Prefs::Clear() { Store().clear(); }

namespace {
// Encode a string onto a single line: backslash-escape newlines and backslashes.
std::string Encode(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}
std::string Decode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            out += (n == 'n') ? '\n' : n;
        } else {
            out += s[i];
        }
    }
    return out;
}
} // namespace

bool Prefs::Save(const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << "okayprefs 1\n";
    for (const auto& [k, v] : Store())
        f << Encode(k) << '\t' << Encode(v) << '\n';
    return static_cast<bool>(f);
}

bool Prefs::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    Store().clear();
    std::string line;
    std::getline(f, line); // header (ignored if absent)
    if (line.rfind("okayprefs", 0) != 0) {
        // No header: treat this first line as data too.
        if (!line.empty()) {
            auto tab = line.find('\t');
            if (tab != std::string::npos)
                Store()[Decode(line.substr(0, tab))] = Decode(line.substr(tab + 1));
        }
    }
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        Store()[Decode(line.substr(0, tab))] = Decode(line.substr(tab + 1));
    }
    return true;
}

} // namespace okay
