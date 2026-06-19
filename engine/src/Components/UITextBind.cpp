#include "okay/Components/UITextBind.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Scene/GameObject.hpp"

namespace okay {

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
            out += Prefs::GetString(key, "");
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
    if (auto* tr = gameObject->GetComponent<TextRenderer>())
        tr->text = Resolve(format);
}

} // namespace okay
