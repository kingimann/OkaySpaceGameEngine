// Theme assets: a UI theme saved to its own small text file (.okaytheme) and loaded back, so one look
// can be shared between scenes and projects. The file is `okaytheme 1` and then one `name=value` line
// per property, in the property registry's exact text form, so every field a theme has is kept.
#include "okay/Components/UITheme.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <fstream>
#include <sstream>

namespace okay {

std::string UIThemeToText(const UITheme& theme) {
    RegisterBuiltinProperties();
    std::ostringstream out;
    out << "okaytheme 1\n";
    auto info = PropertyRegistry::FindFor(theme);
    if (!info) return out.str();
    for (const PropertyDesc& d : info->properties) {
        if (d.IsRuntimeOnly() || d.IsDerived()) continue;
        const PropertyValue v = d.Get(theme);
        if (!v.IsValid()) continue;
        std::string t = v.ToString();
        if (t.find('\n') != std::string::npos) continue;
        out << d.name << '=' << t << '\n';
    }
    return out.str();
}

bool UIThemeFromText(UITheme& theme, const std::string& text, std::string* error) {
    RegisterBuiltinProperties();
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("okaytheme", 0) != 0) {
        if (error) *error = "this is not a theme file";
        return false;
    }
    auto info = PropertyRegistry::FindFor(theme);
    if (!info) { if (error) *error = "themes are not available"; return false; }
    int read = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        const PropertyDesc* d = info->Find(line.substr(0, eq));
        if (!d || d->IsReadOnly()) continue;                 // a field from a newer version: skipped
        PropertyValue v;
        if (!d->ParseText(line.substr(eq + 1), v)) continue; // unreadable value: that field keeps its value
        if (d->Set(theme, v)) ++read;
    }
    if (read == 0) { if (error) *error = "the file has no theme settings"; return false; }
    return true;
}

bool SaveUIThemeAsset(const UITheme& theme, const std::string& path, std::string* error) {
    const std::string text = UIThemeToText(theme);
    std::ofstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "could not write " + path; return false; }
    f << text;
    f.flush();
    if (!f) { if (error) *error = "could not write " + path; return false; }
    return true;
}

bool LoadUIThemeAsset(UITheme& theme, const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "could not open " + path; return false; }
    std::ostringstream ss; ss << f.rdbuf();
    return UIThemeFromText(theme, ss.str(), error);
}

} // namespace okay
