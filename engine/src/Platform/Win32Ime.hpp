#pragma once
// The input-method composition state machine of the own Windows backend, free of Windows calls (tested on
// Linux by tests/test_platform_win32_pure.cpp). Written from the public Microsoft documentation of
// WM_IME_STARTCOMPOSITION / WM_IME_COMPOSITION / WM_IME_ENDCOMPOSITION and the GCS_* flags.
//
// What it decides: when to tell the application "the text being composed is now X, caret at N" (a
// TextEditing event) and when the composition is over (an empty TextEditing). The finished text itself
// arrives as ordinary WM_CHAR / TextInput, so this machine never produces TextInput.
#include <cstdint>
#include <string>
#include <vector>

namespace okay::platform::win32ime {

constexpr uint32_t kGcsCompStr = 0x0008, kGcsCursorPos = 0x0080, kGcsResultStr = 0x0800;

struct Editing { std::string text; int cursor = 0; };   // UTF-8 text; cursor in code points

/// UTF-16 -> UTF-8 (a lone surrogate becomes U+FFFD); `cursorUnits` (UTF-16 units) -> code points.
inline Editing FromUtf16(const std::u16string& s, int cursorUnits) {
    Editing e;
    int cps = 0;
    if (cursorUnits < 0) cursorUnits = 0;
    if ((size_t)cursorUnits > s.size()) cursorUnits = (int)s.size();
    auto put = [&](uint32_t c) {
        if (c < 0x80) e.text += (char)c;
        else if (c < 0x800) { e.text += (char)(0xC0 | (c >> 6)); e.text += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { e.text += (char)(0xE0 | (c >> 12)); e.text += (char)(0x80 | ((c >> 6) & 0x3F)); e.text += (char)(0x80 | (c & 0x3F)); }
        else { e.text += (char)(0xF0 | (c >> 18)); e.text += (char)(0x80 | ((c >> 12) & 0x3F)); e.text += (char)(0x80 | ((c >> 6) & 0x3F)); e.text += (char)(0x80 | (c & 0x3F)); }
    };
    for (size_t i = 0; i < s.size();) {
        uint32_t c = s[i];
        size_t step = 1;
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00); step = 2; }
        else if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;
        if (i < (size_t)cursorUnits) ++cps;
        put(c);
        i += step;
    }
    e.cursor = cps;
    return e;
}

class Machine {
public:
    bool Active() const { return m_active; }
    /// WM_IME_STARTCOMPOSITION. No event: nothing is composed yet.
    void Start() { m_active = true; m_last.clear(); m_has = false; }
    /// WM_IME_COMPOSITION with the GCS_* `flags`, the current composition string and cursor (valid only when the flag is set).
    /// Returns the events to give the application, in order.
    std::vector<Editing> Composition(uint32_t flags, const std::u16string& comp, int cursorUnits) {
        std::vector<Editing> out;
        if (!m_active) m_active = true;                 // some IMEs skip STARTCOMPOSITION
        if (flags & kGcsCompStr) Emit(out, FromUtf16(comp, (flags & kGcsCursorPos) ? cursorUnits : (int)comp.size()));
        else if (flags & kGcsResultStr) Emit(out, Editing{});   // the text was committed: the composing text is gone
        return out;
    }
    /// WM_IME_ENDCOMPOSITION. An empty editing event when something was showing, so the application clears it.
    std::vector<Editing> End() {
        std::vector<Editing> out;
        if (m_active) Emit(out, Editing{});
        m_active = false; m_last.clear(); m_cursor = 0; m_has = false;
        return out;
    }
private:
    void Emit(std::vector<Editing>& out, const Editing& e) {
        if (m_has ? (e.text == m_last && e.cursor == m_cursor) : e.text.empty()) return;   // nothing new
        m_last = e.text; m_cursor = e.cursor; m_has = !e.text.empty();
        out.push_back(e);
    }
    bool m_active = false, m_has = false;
    std::string m_last;
    int m_cursor = 0;
};

} // namespace okay::platform::win32ime
