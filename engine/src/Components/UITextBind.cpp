#include "okay/Components/UITextBind.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Scene/GameObject.hpp"

#include <cmath>
#include <cstdio>

namespace okay {

// Format a number for display: drop the decimal point for whole numbers, otherwise
// show up to 3 decimals with trailing zeros trimmed (so 5.0 -> "5", 2.5 -> "2.5").
static std::string FormatBindNum(float f) {
    if (std::fabs(f - (float)(long long)f) < 1e-4f && std::fabs(f) < 1e15f)
        return std::to_string((long long)f);
    char buf[32]; std::snprintf(buf, sizeof(buf), "%.3f", f);
    std::string s = buf;
    s.erase(s.find_last_not_of('0') + 1);
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

// Resolve a single {key}: a visual-scripting variable first (the new "grab a script
// var" path), then a Prefs value. Numeric values are prettified either way.
std::string UITextBind::ResolveKey(const std::string& key) {
    auto& vars = ActionList::Vars();
    auto it = vars.find(key);
    if (it != vars.end()) return FormatBindNum(it->second);
    if (Prefs::Has(key)) {
        std::string s = Prefs::GetString(key, "");
        try { std::size_t pos = 0; float f = std::stof(s, &pos); if (pos == s.size()) return FormatBindNum(f); }
        catch (...) {}
        return s;
    }
    return "";
}

std::string UITextBind::Resolve(const std::string& format) {
    std::string out;
    out.reserve(format.size());
    for (std::size_t i = 0; i < format.size(); ++i) {
        char c = format[i];
        if (c == '{') {
            if (i + 1 < format.size() && format[i + 1] == '{') { out += '{'; ++i; continue; }
            std::size_t end = format.find('}', i + 1);
            if (end == std::string::npos) { out += c; continue; }   // unterminated
            std::string key = format.substr(i + 1, end - i - 1);
            out += ResolveKey(key);
            i = end;
        } else if (c == '}') {
            if (i + 1 < format.size() && format[i + 1] == '}') { out += '}'; ++i; continue; }
            out += c;
        } else {
            out += c;
        }
    }
    return out;
}

void UITextBind::Apply() {
    if (!gameObject) return;
    std::string v = Resolve(format);
    // Bind whichever text-bearing widget is on this object.
    if (auto* tr = gameObject->GetComponent<TextRenderer>())      tr->text = v;
    else if (auto* bt = gameObject->GetComponent<UIButton>())     bt->label = v;
    else if (auto* in = gameObject->GetComponent<UIInputField>()) in->text = v;
}

} // namespace okay
