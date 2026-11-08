#include "okay/Core/Prefs.hpp"
#include "okay/Core/SaveData.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>
#include <limits>
#include <locale>
#include <sstream>

namespace okay {

std::unordered_map<std::string, std::string>& Prefs::Store() {
    static std::unordered_map<std::string, std::string> s;
    return s;
}

namespace {
std::vector<std::pair<int, Prefs::Listener>>& Listeners() {
    static std::vector<std::pair<int, Prefs::Listener>> l;
    return l;
}
int g_nextListener = 1;
void Notify(const std::string& key) {
    const auto copy = Listeners();   // a listener may remove itself
    for (const auto& l : copy) if (l.second) l.second(key);
}
// Store `value` under `key`, telling listeners when that changed something.
void Put(std::unordered_map<std::string, std::string>& store, const std::string& key, std::string value) {
    auto it = store.find(key);
    if (it != store.end() && it->second == value) return;
    store[key] = std::move(value);
    Notify(key);
}
std::string FloatText(float value) {
    // max_digits10 so the value reads back bit-exact (std::to_string kept only
    // six decimals: 1e-7 saved as "0.000000"). Old "0.750000"-style values still parse.
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os.precision(std::numeric_limits<float>::max_digits10);
    os << value;
    return os.str();
}
// Read a float in the classic ("C") locale, so a program that changed the global
// locale can't misparse "0.75". False when the text is not a number.
bool ParseFloat(const std::string& text, float& out) {
    std::istringstream is(text);
    is.imbue(std::locale::classic());
    float v = 0.0f;
    if (is >> v) { out = v; return true; }
    try { out = std::stof(text); return true; } catch (...) { return false; }   // "inf" / "nan"
}
std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::vector<std::string> SplitCommas(const std::string& s) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (true) {
        const std::size_t c = s.find(',', from);
        out.push_back(s.substr(from, c == std::string::npos ? std::string::npos : c - from));
        if (c == std::string::npos) break;
        from = c + 1;
    }
    return out;
}
} // namespace

int Prefs::AddListener(Listener fn) {
    const int id = g_nextListener++;
    Listeners().push_back({id, std::move(fn)});
    return id;
}
void Prefs::RemoveListener(int id) {
    auto& ls = Listeners();
    ls.erase(std::remove_if(ls.begin(), ls.end(), [id](const auto& l) { return l.first == id; }), ls.end());
}

void Prefs::SetInt(const std::string& key, int value) { Put(Store(), key, std::to_string(value)); }
void Prefs::SetFloat(const std::string& key, float value) { Put(Store(), key, FloatText(value)); }
void Prefs::SetString(const std::string& key, const std::string& value) { Put(Store(), key, value); }
void Prefs::SetBool(const std::string& key, bool value) { Put(Store(), key, value ? "1" : "0"); }
void Prefs::SetVec3(const std::string& key, const Vec3& v) {
    Put(Store(), key, FloatText(v.x) + "," + FloatText(v.y) + "," + FloatText(v.z));
}
void Prefs::SetColor(const std::string& key, const Color& c) {
    Put(Store(), key, FloatText(c.r) + "," + FloatText(c.g) + "," + FloatText(c.b) + "," + FloatText(c.a));
}

int Prefs::GetInt(const std::string& key, int def) {
    auto it = Store().find(key);
    if (it == Store().end()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}
float Prefs::GetFloat(const std::string& key, float def) {
    auto it = Store().find(key);
    if (it == Store().end()) return def;
    float v = 0.0f;
    return ParseFloat(it->second, v) ? v : def;
}
std::string Prefs::GetString(const std::string& key, const std::string& def) {
    auto it = Store().find(key);
    return it != Store().end() ? it->second : def;
}
bool Prefs::GetBool(const std::string& key, bool def) {
    auto it = Store().find(key);
    if (it == Store().end()) return def;
    const std::string v = Lower(it->second);
    if (v == "true" || v == "yes" || v == "on") return true;
    if (v == "false" || v == "no" || v == "off") return false;
    float f = 0.0f;
    return ParseFloat(v, f) ? f != 0.0f : def;
}
Vec3 Prefs::GetVec3(const std::string& key, const Vec3& def) {
    auto it = Store().find(key);
    if (it == Store().end()) return def;
    const auto parts = SplitCommas(it->second);
    float c[3];
    if (parts.size() != 3) return def;
    for (int i = 0; i < 3; ++i) if (!ParseFloat(parts[i], c[i])) return def;
    return Vec3(c[0], c[1], c[2]);
}
Color Prefs::GetColor(const std::string& key, const Color& def) {
    auto it = Store().find(key);
    if (it == Store().end()) return def;
    const auto parts = SplitCommas(it->second);
    float c[4] = {0, 0, 0, 1.0f};
    if (parts.size() != 3 && parts.size() != 4) return def;
    for (std::size_t i = 0; i < parts.size(); ++i) if (!ParseFloat(parts[i], c[i])) return def;
    return Color(c[0], c[1], c[2], c[3]);
}

bool Prefs::Has(const std::string& key) { return Store().count(key) != 0; }
void Prefs::Delete(const std::string& key) {
    if (Store().erase(key)) Notify(key);
}
void Prefs::Clear() {
    std::vector<std::string> keys;
    keys.reserve(Store().size());
    for (const auto& kv : Store()) keys.push_back(kv.first);
    Store().clear();
    for (const auto& k : keys) Notify(k);
}

namespace {
const char* const kEndLine = "#end";
// Encode a string onto a single line: backslash-escape backslashes, newlines,
// carriage returns and tabs (a raw tab in a key would split it at the wrong
// place). Files written before tab/CR escaping still decode the same way.
std::string Encode(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}
std::string Decode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            out += (n == 'n') ? '\n' : (n == 't') ? '\t' : (n == 'r') ? '\r' : n;
        } else {
            out += s[i];
        }
    }
    return out;
}
} // namespace

bool Prefs::Save(const std::string& path) {
    // Written aside and swapped in (a crash never leaves a half file). The end line
    // lets Load tell a cut-off file from a complete one; older readers skip it (no tab).
    std::string out = "okayprefs 2\n";
    for (const auto& [k, v] : Store())
        out += Encode(k) + '\t' + Encode(v) + '\n';
    out += kEndLine;
    out += '\n';
    return savefmt::AtomicWrite(path, out);
}

bool Prefs::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) { const bool had = !Store().empty(); Store().clear(); if (had) Notify(""); return false; }   // no file = an empty store
    // Build the new store aside; the live one is replaced only by a file that read whole.
    std::unordered_map<std::string, std::string> fresh;
    std::string line;
    std::getline(f, line); // header (ignored if absent)
    int version = 1;
    if (line.rfind("okayprefs", 0) == 0) {
        version = std::atoi(line.c_str() + 9);
        if (version < 1) version = 1;
    } else if (!line.empty()) {
        // No header: treat this first line as data too.
        auto tab = line.find('\t');
        if (tab != std::string::npos)
            fresh[Decode(line.substr(0, tab))] = Decode(line.substr(tab + 1));
    }
    bool sawEnd = false;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line == kEndLine) { sawEnd = true; continue; }
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        fresh[Decode(line.substr(0, tab))] = Decode(line.substr(tab + 1));
    }
    if (f.bad()) return false;
    if (version >= 2 && !sawEnd) return false;   // cut off: keep what we have
    Store() = std::move(fresh);
    Notify("");
    return true;
}

} // namespace okay
