#pragma once
// Controller-only text entry on the Xbox: when a UI Input Field is being edited, the system's on-screen keyboard comes up; when the
// editing ends it goes away; when the player closes the keyboard (B), the field stops editing so the pad navigates the menu again.
// Pure logic, no Windows headers; tested on Linux by tests/test_xbox_uwp_runtime.cpp (TextEntryTests).
//
// How the parts fit (the Windows side is in app/engine_shell.hpp and is UNCOMPILED):
//   * XboxGameRuntime finds the field being edited each frame and calls TextEntry::Frame; a Show / Hide command goes to the shell
//     through ShellCallbacks::textEntry.
//   * The shell owns a Windows.UI.Text.Core.CoreTextEditContext (InputPaneDisplayPolicy Manual, InputScope from TextScope): Show =
//     NotifyFocusEnter + InputPane.TryShow, Hide = InputPane.TryHide + NotifyFocusLeave. InputPane Showing / Hiding come back as
//     KeyboardShown / KeyboardHidden.
//   * The edit context asks for the text and sends edits as UTF-16 ranges (TextUpdating: Range, Text). TextMirror keeps that text in
//     UTF-16, applies each edit and hands the whole value back as UTF-8; the runtime puts it into the field (which applies its own
//     length and content-type rules) and the shell re-syncs the edit context when the field refused something.
// Written from Microsoft's documentation of Windows.UI.Text.Core (CoreTextServicesManager.CreateEditContext, CoreTextEditContext
// TextRequested / SelectionRequested / TextUpdating / FocusRemoved, NotifyFocusEnter / NotifyFocusLeave / NotifyTextChanged,
// InputPaneDisplayPolicy, InputScope) and Windows.UI.ViewManagement.InputPane (TryShow, TryHide, Showing, Hiding), and of UTF-8 /
// UTF-16 (Unicode chapter 3). Our own code. UNVERIFIED on Windows and on a console.
#include <cstdint>
#include <string>

namespace okay::xbox {

/// What the keyboard should offer: the field's content type (Integer / Decimal -> Number, Password -> Password).
enum class TextScope { Default, Number, Password };

struct TextEntryCommand {
    enum Kind { None, Show, Hide } kind = None;
    TextScope scope = TextScope::Default;
    bool releaseField = false;   // the player closed the keyboard: the field should stop editing
};

class TextEntry {
public:
    /// On only where a pad is the keyboard (a console). Off: nothing is ever shown; turning it off while shown hides it.
    void SetEnabled(bool on) { enabled_ = on; }
    bool Enabled() const { return enabled_; }
    /// Once a frame. `field` = the field being edited now (any stable pointer), null when none.
    TextEntryCommand Frame(const void* field, TextScope scope) {
        TextEntryCommand c;
        if (!enabled_) {
            if (for_) { for_ = nullptr; shown_ = false; closedByPlayer_ = false; c.kind = TextEntryCommand::Hide; }
            return c;
        }
        if (closedByPlayer_) {   // the player closed the keyboard while this field edited: it lets go (once)
            closedByPlayer_ = false;
            if (field && field == for_) { c.releaseField = true; for_ = nullptr; return c; }
        }
        if (!field) {
            if (for_) { for_ = nullptr; shown_ = false; c.kind = TextEntryCommand::Hide; }
            return c;
        }
        if (field != for_) {   // a new field (the keyboard may still be up from the last one: it stays "shown")
            shown_ = shown_ && for_ != nullptr; for_ = field; c.kind = TextEntryCommand::Show; c.scope = scope;
        }
        return c;
    }
    /// InputPane.Showing / Hiding (the shell forwards them on the UI thread).
    void KeyboardShown() { if (for_) shown_ = true; }
    void KeyboardHidden() { if (for_ && shown_) closedByPlayer_ = true; shown_ = false; }
    bool Showing() const { return shown_; }

private:
    bool enabled_ = false, shown_ = false, closedByPlayer_ = false;
    const void* for_ = nullptr;
};

// ---- UTF-8 <-> UTF-16 (a bad sequence becomes U+FFFD) ----------------------------------------------------------------------------
inline std::u16string Utf8ToUtf16(const std::string& s) {
    std::u16string out;
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = (unsigned char)s[i];
        std::uint32_t cp = 0xFFFD; int len = 1, need = 0;
        if (c < 0x80) { cp = c; }
        else if (c >= 0xC2 && c <= 0xDF) { cp = c & 0x1F; need = 1; }
        else if (c >= 0xE0 && c <= 0xEF) { cp = c & 0x0F; need = 2; }
        else if (c >= 0xF0 && c <= 0xF4) { cp = c & 0x07; need = 3; }
        if (need) {
            bool ok = true;
            for (int k = 1; ok && k <= need; ++k) {
                if (i + (std::size_t)k >= s.size()) { ok = false; break; }
                const unsigned char d = (unsigned char)s[i + (std::size_t)k];
                if ((d & 0xC0) != 0x80) { ok = false; break; }
                cp = (cp << 6) | (d & 0x3F);
            }
            // overlong, surrogate and out-of-range forms are refused
            if (ok && ((need == 2 && cp < 0x800) || (need == 3 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF))) ok = false;
            if (ok) len = need + 1; else cp = 0xFFFD;
        }
        if (cp >= 0x10000) { cp -= 0x10000; out += (char16_t)(0xD800 + (cp >> 10)); out += (char16_t)(0xDC00 + (cp & 0x3FF)); }
        else out += (char16_t)cp;
        i += (std::size_t)len;
    }
    return out;
}

inline std::string Utf16ToUtf8(const std::u16string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        std::uint32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00); ++i;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;   // a lone surrogate
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    return out;
}

/// The text the edit context sees, in UTF-16 (its ranges count UTF-16 code units).
class TextMirror {
public:
    void Reset(const std::string& utf8) { t_ = Utf8ToUtf16(utf8); caret_ = (std::int32_t)t_.size(); }
    /// TextUpdating: replace [start, end) with `text`. A reversed range is put in order; a range past the end is refused.
    bool Replace(std::int32_t start, std::int32_t end, const std::u16string& text) {
        if (start > end) { const std::int32_t x = start; start = end; end = x; }
        if (start < 0 || (std::size_t)end > t_.size()) return false;
        t_.replace((std::size_t)start, (std::size_t)(end - start), text);
        caret_ = start + (std::int32_t)text.size();
        return true;
    }
    std::string Utf8() const { return Utf16ToUtf8(t_); }
    const std::u16string& Utf16() const { return t_; }
    std::int32_t Utf16Size() const { return (std::int32_t)t_.size(); }
    std::int32_t Caret16() const { return caret_; }

private:
    std::u16string t_;
    std::int32_t caret_ = 0;
};

} // namespace okay::xbox
