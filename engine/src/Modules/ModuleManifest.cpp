#include "okay/Modules/ModuleManifest.hpp"
#include <cstdlib>
#include <sstream>

namespace okay {

namespace {
std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}
bool IsOn(const std::string& v) { return v == "1" || v == "on" || v == "true" || v == "yes"; }
} // namespace

bool ModuleManifest::ValidName(const std::string& name) {
    if (name.empty() || name.size() > 48) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

std::vector<std::string> ModuleManifest::SplitList(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        std::string t = Trim(cur);
        if (!t.empty()) out.push_back(t);
        cur.clear();
    };
    for (char c : s) {
        if (c == sep || (sep == ' ' && c == '\t')) flush();
        else cur += c;
    }
    flush();
    return out;
}

bool ModuleManifest::Parse(const std::string& text, ModuleManifest& out, std::string* err) {
    ModuleManifest m;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
        if (k == "name") m.name = v;
        else if (k == "title") m.title = v;
        else if (k == "area") m.area = v;
        else if (k == "description") m.description = v;
        else if (k == "version") m.version = std::atoi(v.c_str());
        else if (k == "default") m.defaultOn = IsOn(v);
        else if (k == "core") m.core = IsOn(v);
        else if (k == "requires") m.requires_ = SplitList(v);
        else if (k == "provides") m.provides = SplitList(v);
        else if (k == "components") m.components = SplitList(v);
        else if (k == "serializers") m.serializers = SplitList(v);
        else if (k == "vs_nodes") m.vsNodes = SplitList(v);
        else if (k == "vs_group") m.vsGroup = v;
        else if (k == "editor") m.editor = SplitList(v);
        else if (k == "sources") m.sources = SplitList(v, ' ');
    }
    if (!ValidName(m.name)) {
        if (err) *err = "module manifest has a missing or invalid name ('" + m.name + "')";
        return false;
    }
    if (m.title.empty()) m.title = m.name;
    if (m.core) m.defaultOn = true;
    out = m;
    return true;
}

} // namespace okay
