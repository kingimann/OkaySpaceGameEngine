#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UIWorldContext.hpp"   // UIToLocal
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Localization.hpp"
#include "okay/Core/Utf8.hpp"
#include <string>

namespace okay {

/// A screen-space text input box. Click it to focus, then
/// type — characters come from the OS text-input event so uppercase, symbols
/// and punctuation all work (Shift/Caps respected); Backspace deletes, Enter
/// submits (calls the sibling ScriptComponent's on_submit()) and Esc cancels.
/// Shows `placeholder` when empty, blinks a caret, and scrolls horizontally so
/// the caret stays visible in long text. An optional numeric/password mode and
/// a character whitelist constrain what can be entered.
class UIInputField final : public Behaviour {
public:
    enum class ContentType { Standard, Integer, Decimal, Password };

    Vec2 position{20.0f, 20.0f};
    Vec2 size{220.0f, 40.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    Color color            = Color::FromBytes(41, 46, 59);
    Color focusColor       = Color::FromBytes(48, 54, 69);
    Color textColor        = Color::White;
    Color placeholderColor = Color::FromBytes(140, 150, 171);
    std::string text;
    std::string placeholder = "Type...";
    /// Localized: a string-table key whose text replaces `placeholder` in the current language.
    LocalizedText loc;
    int  maxLength = 64;
    ContentType contentType = ContentType::Standard;
    char passwordChar = '*';
    bool focused = false;
    // Silhouette + corners and an optional focus ring (clean form-field styling).
    UIShape shape = UIShape::Rounded;
    float cornerRadius = 6.0f;
    float borderWidth = 0.0f;                         // focus ring thickness (0 = none)
    Color borderColor = Color::FromBytes(66, 150, 250);  // shown while focused
    // grok/ui-controls — off by default, so Enter still submits and blurs.
    /// Enter inserts a newline instead of submitting. The box stacks the lines.
    bool multiline = false;
    void SetMultiline(bool on) { multiline = on; }
    bool Multiline() const { return multiline; }
    void SetMaxLength(int n) { maxLength = n < 0 ? 0 : n; }
    void SetContentType(int t) {
        if (t < 0) t = 0;
        if (t > (int)ContentType::Password) t = (int)ContentType::Password;
        contentType = (ContentType)t;
    }
    int ContentTypeIndex() const { return (int)contentType; }

    /// A screen point mapped into this widget's own frame (where it is really drawn:
    /// inside its UI parent, at the Canvas scale). See UIToLocal.
    Vec2 ToLocal(const Vec2& p) const {
        Vec2 o, sz;
        return UIControlPoint(gameObject, anchor, position, size, p, o, sz);
    }
    bool Contains(const Vec2& sp) const {
        Vec2 o, sz, p = UIControlPoint(gameObject, anchor, position, size, sp, o, sz);
        return p.x >= o.x && p.y >= o.y && p.x <= o.x + sz.x && p.y <= o.y + sz.y;
    }

    Color CurrentColor() const { return focused ? focusColor : color; }

    /// What the renderer should draw for the value (masked for Password).
    std::string DisplayText() const {
        if (contentType != ContentType::Password) return text;
        return std::string(utf8::Length(text), passwordChar);   // one mask per character, not per byte
    }

    /// Caret on/off for a ~1s blink (renderers check this while focused).
    bool CaretVisible() const { return m_blink < 0.55f; }

    /// Whether `c` is allowed by the current content type.
    bool Accepts(char c) const {
        if ((unsigned char)c < 32) return false;            // control chars
        switch (contentType) {
            case ContentType::Integer: return (c >= '0' && c <= '9') || (c == '-' && text.empty());
            case ContentType::Decimal: return (c >= '0' && c <= '9') || (c == '-' && text.empty())
                                            || (c == '.' && text.find('.') == std::string::npos);
            default: return true;
        }
    }

    /// Add typed text (UTF-8). Control characters and characters the content type refuses are dropped;
    /// `maxLength` counts characters, so a limit never cuts a character in half. True when text was added.
    bool TypeText(const std::string& typed) {
        bool changed = false;
        const char* p = typed.data();
        const char* end = p + typed.size();
        std::size_t length = utf8::Length(text);
        while (p < end) {
            const char* start = p;
            const std::uint32_t cp = utf8::Next(p, end);
            if ((int)length >= maxLength) break;
            if (cp == utf8::kReplacement) continue;                       // a broken byte: skip it
            if (cp < 128 ? !Accepts((char)cp) : contentType != ContentType::Standard && contentType != ContentType::Password) continue;
            if (cp == 127) continue;
            text.append(start, (std::size_t)(p - start));
            ++length;
            changed = true;
        }
        return changed;
    }
    /// Delete the last character (all of its bytes). True when something was removed.
    bool Backspace() {
        if (text.empty()) return false;
        std::size_t i = text.size() - 1;
        int back = 0;
        while (i > 0 && ((unsigned char)text[i] & 0xC0) == 0x80 && back < 3) { --i; ++back; }
        if (((unsigned char)text[i] & 0xC0) == 0x80 || (unsigned char)text[i] < 0x80) i = text.size() - 1;   // not a lead byte: drop the last byte only
        text.erase(i);
        return true;
    }

    void Submit() {
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_submit");
    }

    void Update(float dt) override {
        if (Input::GetMouseButtonDown(0)) {
            bool was = focused;
            focused = Contains(Input::MousePosition());
            if (focused && !was) m_blink = 0.0f;            // solid caret on focus
        }
        if (!focused) return;

        // Real typed characters (Shift/Caps/symbols handled by the OS).
        bool changed = TypeText(Input::TypedText());
        // Backspace / Delete.
        if (Input::GetKeyDown((char)8) || Input::GetKeyDown((char)127)) {
            if (Backspace()) changed = true;
        }
        if (changed) m_blink = 0.0f;                        // keep caret solid while typing

        // Enter submits (and blurs); Esc cancels focus without submitting.
        // A multiline field inserts a newline instead and stays focused.
        if (Input::GetKeyDown('\r') || Input::GetKeyDown('\n')) {
            if (!multiline) { focused = false; Submit(); }
            else if ((int)utf8::Length(text) < maxLength) { text.push_back('\n'); m_blink = 0.0f; }
        } else if (Input::GetKeyDown((char)27)) focused = false;

        m_blink += dt;
        if (m_blink >= 1.1f) m_blink -= 1.1f;
    }

private:
    float m_blink = 0.0f;
};

} // namespace okay
