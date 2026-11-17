#pragma once
// UI Input Validator: rules for a UI Input Field on the same object (required, length,
// email, whole number, number range, letters only, "must match another field"). It
// shows a red ring and a one-line message under the field, and tells the object's
// script with on_valid() and on_invalid() when the answer changes. Rules written
// from scratch; the email check is deliberately simple (one @, a dot in the domain).
#include "okay/Components/UIWidgetUtil.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Render/Color.hpp"
#include <cstdlib>
#include <string>

namespace okay {

class UIInputValidator final : public Behaviour {
public:
    enum class Kind { Any, Email, Integer, Number, Letters, LettersAndDigits };
    Kind kind = Kind::Any;
    bool required = true;
    int minLength = 0;
    /// 0 = no upper limit.
    int maxLength = 0;
    /// Used by Integer and Number kinds when `useRange` is on.
    bool useRange = false;
    float minNumber = 0.0f;
    float maxNumber = 100.0f;
    /// Name of another object with a UI Input Field whose text this one must equal
    /// (a "repeat the password" box). Empty = no comparison.
    std::string mustMatch;
    /// Shown instead of the built-in sentence when set.
    std::string message;
    /// Show nothing until the player has typed (or left the field once).
    bool quietUntilTouched = true;
    bool showMessage = true;
    Color errorColor = Color::FromBytes(232, 84, 84);
    Color okColor = Color::FromBytes(88, 200, 130);
    /// Show a green ring when the value is fine and has been touched.
    bool showOk = false;

    /// The reason `text` is not acceptable, or "" when it is. `other` is the text of the
    /// `mustMatch` field (ignored when mustMatch is empty).
    std::string Validate(const std::string& text, const std::string& other = std::string()) const {
        auto fail = [&](const char* builtin) { return message.empty() ? std::string(builtin) : message; };
        if (text.empty()) return required ? fail("This is required") : std::string();
        const int len = (int)Length(text);
        if (minLength > 0 && len < minLength) return fail(("At least " + std::to_string(minLength) + " characters").c_str());
        if (maxLength > 0 && len > maxLength) return fail(("At most " + std::to_string(maxLength) + " characters").c_str());
        switch (kind) {
            case Kind::Any: break;
            case Kind::Email: if (!LooksLikeEmail(text)) return fail("Enter an email like name@site.com"); break;
            case Kind::Integer: {
                if (!IsWhole(text)) return fail("Enter a whole number");
                if (useRange) {
                    const double v = std::strtod(text.c_str(), nullptr);
                    if (v < minNumber || v > maxNumber) return fail(RangeSentence().c_str());
                }
                break;
            }
            case Kind::Number: {
                double v = 0.0;
                if (!IsNumber(text, v)) return fail("Enter a number");
                if (useRange && (v < minNumber || v > maxNumber)) return fail(RangeSentence().c_str());
                break;
            }
            case Kind::Letters:
                for (char c : text) if (!IsLetter(c) && c != ' ' && c != '-' && c != '\'') return fail("Letters only");
                break;
            case Kind::LettersAndDigits:
                for (char c : text) if (!IsLetter(c) && !(c >= '0' && c <= '9')) return fail("Letters and digits only");
                break;
        }
        if (!mustMatch.empty() && text != other) return fail("The two entries do not match");
        return std::string();
    }

    /// The last answer from Update (or Check): empty = fine.
    const std::string& Error() const { return m_error; }
    bool IsValid() const { return m_error.empty(); }
    /// True once the player typed in the field (or it lost focus after being used).
    bool Touched() const { return m_touched; }
    void MarkTouched() { m_touched = true; }
    /// Whether the red ring and message should be on screen right now.
    bool ShowsError() const { return !m_error.empty() && (m_touched || !quietUntilTouched); }
    bool ShowsOk() const { return showOk && m_error.empty() && m_touched; }

    /// Re-check the sibling field now. Returns true when it is valid. Fires on_valid /
    /// on_invalid only when the answer changed.
    bool Check() {
        UIInputField* in = gameObject ? gameObject->GetComponent<UIInputField>() : nullptr;
        if (!in) return true;
        std::string other;
        if (!mustMatch.empty() && gameObject->scene())
            if (GameObject* o = gameObject->scene()->Find(mustMatch))
                if (auto* oin = o->GetComponent<UIInputField>()) other = oin->text;
        const std::string next = Validate(in->text, other);
        const bool changed = !m_known || (next.empty() != m_error.empty());
        m_error = next;
        if (changed) {
            m_known = true;
            FireUIEvent(gameObject, m_error.empty() ? "on_valid" : "on_invalid");
        }
        return m_error.empty();
    }

    void Update(float) override {
        if (!enabled) return;
        UIInputField* in = gameObject ? gameObject->GetComponent<UIInputField>() : nullptr;
        if (!in) return;
        if (in->text != m_lastText) { if (m_known) m_touched = true; m_lastText = in->text; }
        if (m_wasFocused && !in->focused && !in->text.empty()) m_touched = true;
        m_wasFocused = in->focused;
        Check();
    }

    static std::size_t Length(const std::string& s) {
        std::size_t n = 0;
        for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;   // count characters, not bytes
        return n;
    }
    static bool LooksLikeEmail(const std::string& s) {
        const std::size_t at = s.find('@');
        if (at == std::string::npos || at == 0 || s.find('@', at + 1) != std::string::npos) return false;
        for (char c : s) if (c == ' ' || c == '\t') return false;
        const std::string domain = s.substr(at + 1);
        const std::size_t dot = domain.rfind('.');
        return dot != std::string::npos && dot > 0 && dot + 1 < domain.size();
    }

private:
    std::string RangeSentence() const {
        char a[32], b[32];
        std::snprintf(a, sizeof(a), "%g", (double)minNumber);
        std::snprintf(b, sizeof(b), "%g", (double)maxNumber);
        return std::string("Between ") + a + " and " + b;
    }
    static bool IsLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (unsigned char)c >= 0x80; }
    static bool IsWhole(const std::string& s) {
        std::size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
        if (i >= s.size()) return false;
        for (; i < s.size(); ++i) if (s[i] < '0' || s[i] > '9') return false;
        return true;
    }
    static bool IsNumber(const std::string& s, double& v) {
        for (char c : s) if (!((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E')) return false;   // no "nan", "inf" or hex
        char* end = nullptr;
        v = std::strtod(s.c_str(), &end);
        return end && *end == '\0' && end != s.c_str();
    }
    std::string m_error;
    std::string m_lastText;
    bool m_known = false;
    bool m_touched = false;
    bool m_wasFocused = false;
};

} // namespace okay
