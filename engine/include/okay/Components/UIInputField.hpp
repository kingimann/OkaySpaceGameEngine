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
#include "okay/Core/Clipboard.hpp"
#include "okay/Core/Localization.hpp"
#include "okay/Core/Utf8.hpp"
#include <string>

namespace okay {

/// A screen-space text input box. Click it to focus, then
/// type — characters come from the OS text-input event so uppercase, symbols
/// and punctuation all work (Shift/Caps respected). Typing goes in at the caret;
/// Left / Right move it one character, Home / End to the start / end (of the line,
/// in a multiline field). Backspace deletes the character before the caret and
/// Delete the one after it; Backspace, Delete, Left and Right repeat while held
/// (after `repeatDelay`, every `repeatInterval` seconds). Enter submits (calls the
/// sibling ScriptComponent's on_submit()) and Esc cancels.
/// Ctrl or Command with C copies the whole value, X cuts it, and V pastes at the caret.
/// There is no selection, so there is no select-all shortcut.
/// A password field does not copy or cut. Pasted text is capped at maxLength
/// and control characters are dropped (a newline stays only while multiline).
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
    /// Keyboard / gamepad menus (NavigateUI) may rest their focus here: the d-pad highlights the field and A / Enter starts
    /// editing (on an Xbox that brings up the on-screen keyboard). Not saved in scenes yet (always on when loaded).
    bool focusable = true;
    ContentType contentType = ContentType::Standard;
    char passwordChar = '*';
    bool focused = false;
    /// The caret, as a byte offset into `text` (-1 = after the last character, the default). It is not
    /// saved. Setting `text` from code keeps a caret at the end there; any other value is clamped.
    int caret = -1;
    /// Held-key repeat for Backspace, Delete, Left and Right: the first repeat after `repeatDelay`
    /// seconds, then one every `repeatInterval` seconds (about the usual OS keyboard repeat).
    float repeatDelay = 0.45f;
    float repeatInterval = 0.035f;
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

    Color CurrentColor() const { return (focused || m_navFocused) ? focusColor : color; }

    /// Menu focus (NavigateUI): highlighted, not editing. `focused` is the editing state.
    bool IsNavFocused() const { return m_navFocused; }
    void SetNavFocused(bool f) { m_navFocused = f; }
    /// Start editing (A / Enter on the highlighted field): caret at the end, solid.
    void BeginEdit() { focused = true; caret = -1; m_blink = 0.0f; }

    /// What the renderer should draw for the value (masked for Password).
    std::string DisplayText() const {
        if (contentType != ContentType::Password) return text;
        return std::string(utf8::Length(text), passwordChar);   // one mask per character, not per byte
    }

    /// Caret on/off for a ~1s blink (renderers check this while focused).
    bool CaretVisible() const { return m_blink < 0.55f; }

    /// The caret as a byte offset into `text`, clamped and moved back to the start of a character.
    std::size_t CaretIndex() const {
        const std::size_t n = text.size();
        if (caret < 0 || (std::size_t)caret >= n) return n;
        std::size_t i = (std::size_t)caret;
        while (i > 0 && IsContinuation(text[i])) --i;
        return i;
    }
    /// Put the caret at byte offset `i` (the end of the text or past it = -1, "stay at the end").
    void SetCaretIndex(std::size_t i) { caret = i >= text.size() ? -1 : (int)i; }
    /// Move the caret one character left / right. True when it moved.
    bool MoveCaretLeft() {
        std::size_t i = CaretIndex();
        if (i == 0) return false;
        --i;
        while (i > 0 && IsContinuation(text[i])) --i;
        SetCaretIndex(i);
        return true;
    }
    bool MoveCaretRight() {
        std::size_t i = CaretIndex();
        if (i >= text.size()) return false;
        ++i;
        while (i < text.size() && IsContinuation(text[i])) ++i;
        SetCaretIndex(i);
        return true;
    }
    /// Home / End: the start / end of the text, or of the caret's line in a multiline field.
    void MoveCaretHome() {
        std::size_t i = CaretIndex();
        if (!multiline) i = 0;
        else while (i > 0 && text[i - 1] != '\n') --i;
        SetCaretIndex(i);
    }
    void MoveCaretEnd() {
        std::size_t i = CaretIndex();
        if (!multiline) i = text.size();
        else while (i < text.size() && text[i] != '\n') ++i;
        SetCaretIndex(i);
    }
    /// The shown text before the caret (masked for Password): renderers measure it to place the caret.
    std::string DisplayTextBeforeCaret() const {
        const std::string head = text.substr(0, CaretIndex());
        if (contentType != ContentType::Password) return head;
        return std::string(utf8::Length(head), passwordChar);
    }

    /// Whether `c` is allowed by the current content type, typed at the end.
    bool Accepts(char c) const { return AcceptsAt(c, text.size()); }
    /// Whether `c` is allowed by the current content type, typed at byte offset `at`
    /// (a minus sign only goes first, and only once).
    bool AcceptsAt(char c, std::size_t at) const {
        if ((unsigned char)c < 32) return false;            // control chars
        const bool minusOk = c == '-' && at == 0 && (text.empty() || text[0] != '-');
        switch (contentType) {
            case ContentType::Integer: return (c >= '0' && c <= '9' && !(at == 0 && !text.empty() && text[0] == '-')) || minusOk;
            case ContentType::Decimal: return (c >= '0' && c <= '9' && !(at == 0 && !text.empty() && text[0] == '-')) || minusOk
                                            || (c == '.' && text.find('.') == std::string::npos);
            default: return true;
        }
    }

    /// Add typed text (UTF-8) at the caret. Control characters and characters the content type refuses are
    /// dropped; `maxLength` counts characters, so a limit never cuts a character in half. True when text was added.
    bool TypeText(const std::string& typed) {
        bool changed = false;
        const char* p = typed.data();
        const char* end = p + typed.size();
        std::size_t length = utf8::Length(text);
        const bool atEnd = caret < 0 || (std::size_t)caret >= text.size();
        std::size_t at = CaretIndex();
        while (p < end) {
            const char* start = p;
            const std::uint32_t cp = utf8::Next(p, end);
            if ((int)length >= maxLength) break;
            if (cp == utf8::kReplacement) continue;                       // a broken byte: skip it
            if (cp == '\n') {                                             // a pasted line break, only in a multiline field
                if (!multiline || (contentType != ContentType::Standard && contentType != ContentType::Password)) continue;
                text.insert(at, 1, '\n');
                ++at;
                ++length;
                changed = true;
                continue;
            }
            if (cp < 128 ? !AcceptsAt((char)cp, at) : contentType != ContentType::Standard && contentType != ContentType::Password) continue;
            if (cp == 127) continue;
            const std::size_t bytes = (std::size_t)(p - start);
            text.insert(at, start, bytes);
            at += bytes;
            ++length;
            changed = true;
        }
        if (atEnd) caret = -1; else SetCaretIndex(at);
        return changed;
    }
    /// Delete the character before the caret (all of its bytes). True when something was removed.
    bool Backspace() {
        const bool atEnd = caret < 0 || (std::size_t)caret >= text.size();
        const std::size_t i = CaretIndex();
        if (i == 0) return false;
        std::size_t j = i - 1;
        while (j > 0 && IsContinuation(text[j]) && i - j < 4) --j;
        if (IsContinuation(text[j]) || (unsigned char)text[j] < 0x80) j = i - 1;   // not a lead byte: drop one byte only
        text.erase(j, i - j);
        if (atEnd) caret = -1; else SetCaretIndex(j);
        return true;
    }
    /// Delete the character after the caret (all of its bytes). True when something was removed.
    bool DeleteForward() {
        const std::size_t i = CaretIndex();
        if (i >= text.size()) return false;
        std::size_t j = i + 1;
        while (j < text.size() && IsContinuation(text[j]) && j - i < 4) ++j;
        text.erase(i, j - i);
        SetCaretIndex(i);
        return true;
    }

    /// Copy the whole value. A password field and a failed write leave the clipboard alone.
    bool CopyToClipboard() const {
        if (contentType == ContentType::Password) return false;
        return SetClipboardText(text);
    }
    /// Copy the whole value, then clear the field. A password field, or an empty one, is left as it is.
    bool CutToClipboard() {
        if (contentType == ContentType::Password || text.empty()) return false;
        if (!SetClipboardText(text)) return false;
        text.clear();
        caret = -1;
        return true;
    }
    /// Insert the clipboard at the caret. Length, content type and control characters are applied by TypeText.
    bool PasteFromClipboard() { return TypeText(GetClipboardText()); }

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

        // Ctrl/Cmd+C, X and V. There is no selection, so copy and cut use the whole value.
        bool changed = false;
        bool shortcut = false;
        const bool command = Input::GetKey(Input::KeyCtrl) || Input::GetKey(Input::KeyCmd);
        if (command) {
            if (Input::GetKeyDown('c')) { CopyToClipboard(); shortcut = true; }
            else if (Input::GetKeyDown('x')) { if (CutToClipboard()) changed = true; shortcut = true; }
            else if (Input::GetKeyDown('v')) { if (PasteFromClipboard()) changed = true; shortcut = true; }
        }
        // Real typed characters (Shift/Caps/symbols handled by the OS). A shortcut
        // frame is not also typed, so Ctrl+V does not insert a "v".
        if (!shortcut && TypeText(Input::TypedText())) changed = true;
        // Backspace, Delete and the caret keys; each repeats while held (Input reports held keys,
        // so the repeat is timed here, the same at any frame rate).
        for (int n = KeyPresses(0, (char)8, dt) + KeyPresses(1, (char)127, dt); n > 0; --n)
            if (Backspace()) changed = true;
        for (int n = KeyPresses(2, Input::KeyDelete, dt); n > 0; --n)
            if (DeleteForward()) changed = true;
        bool moved = false;
        for (int n = KeyPresses(3, Input::KeyLeft, dt); n > 0; --n) moved |= MoveCaretLeft();
        for (int n = KeyPresses(4, Input::KeyRight, dt); n > 0; --n) moved |= MoveCaretRight();
        if (Input::GetKeyDown(Input::KeyHome)) { MoveCaretHome(); moved = true; }
        if (Input::GetKeyDown(Input::KeyEnd))  { MoveCaretEnd(); moved = true; }
        if (changed || moved) m_blink = 0.0f;               // keep caret solid while typing or moving

        // Enter submits (and blurs); Esc cancels focus without submitting.
        // A multiline field inserts a newline instead and stays focused.
        if (Input::GetKeyDown('\r') || Input::GetKeyDown('\n')) {
            if (!multiline) { focused = false; Submit(); }
            else if ((int)utf8::Length(text) < maxLength) {
                const bool atEnd = caret < 0 || (std::size_t)caret >= text.size();
                const std::size_t at = CaretIndex();
                text.insert(at, 1, '\n');
                if (atEnd) caret = -1; else SetCaretIndex(at + 1);
                m_blink = 0.0f;
            }
        } else if (Input::GetKeyDown((char)27)) focused = false;

        m_blink += dt;
        if (m_blink >= 1.1f) m_blink -= 1.1f;
    }

private:
    bool m_navFocused = false;
    static bool IsContinuation(char c) { return ((unsigned char)c & 0xC0) == 0x80; }
    /// How many times a held key acts this frame: once when it goes down, then after `repeatDelay`
    /// once per `repeatInterval` while it stays down (several in one long frame, capped).
    int KeyPresses(int slot, char key, float dt) {
        float& t = m_repeat[slot];
        if (Input::GetKeyDown(key)) { t = repeatDelay; return 1; }
        if (!Input::GetKey(key)) { t = 0.0f; return 0; }
        t -= dt;
        const float step = repeatInterval > 0.005f ? repeatInterval : 0.005f;
        int n = 0;
        while (t <= 0.0f && n < 64) { ++n; t += step; }
        if (t <= 0.0f) t = step;
        return n;
    }
    float m_blink = 0.0f;
    float m_repeat[5] = {0, 0, 0, 0, 0};
};

} // namespace okay
