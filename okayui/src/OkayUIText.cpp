// OkayUI::InputTextMultiline: a multi-line text box over the caller's char buffer, built on OkayUI::CodeEdit with the gutter, the
// highlighting and the auto-indent turned off. The editor object (text, caret, undo, scroll) is kept per label in a small table; the
// caller's buffer is the truth: if it changes behind the box's back the editor takes the new text. See docs/own_ui_toolkit.md section 4.34.
#include "okay/UI/OkayUICodeEdit.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

namespace OkayUI {

namespace {

struct Box {
    unsigned id = 0;
    std::unique_ptr<CodeEdit> ed;
    std::string last;                // the buffer text as the box last saw it
    long long frame = -1;
};
constexpr int kBoxes = 16;
Box g_boxes[kBoxes];

Box& BoxFor(unsigned id, long long now) {
    Box* free = nullptr; Box* oldest = &g_boxes[0];
    for (Box& b : g_boxes) {
        if (b.ed && b.id == id) return b;
        if (!b.ed && !free) free = &b;
        if (b.frame < oldest->frame) oldest = &b;
    }
    Box& b = free ? *free : *oldest;
    b = Box();
    b.id = id; b.frame = now;
    b.ed.reset(new CodeEdit());
    b.ed->SetShowLineNumbers(false);
    b.ed->SetShowBreakpointGutter(false);
    b.ed->SetAutoIndent(false);
    b.ed->SetInsertSpaces(false);
    b.ed->SetHighlighter(CodeHighlighter());
    return b;
}

Color32 Style(Col c) { return ColorConvertFloat4ToU32(GetStyleColorVec4(c)); }

// A CodeEdit palette from the theme: the field colour behind the text colour, no gutter, no current-line wash.
CodePalette ThemePalette() {
    CodePalette p = DefaultCodePalette();
    const Color32 txt = Style(Col_Text), bg = Style(Col_FrameBg), clear = RGBA(0, 0, 0, 0);
    for (int i = 0; i < CodeColor_COUNT; ++i) p.text[i] = txt;
    p.background = bg; p.gutter = bg;
    p.lineNumber = p.lineNumberCurrent = txt;
    p.caret = txt;
    p.selection = Style(Col_TextSelectedBg);
    p.selectionInactive = ColorMulAlpha(p.selection, 0.5f);
    p.currentLine = clear; p.currentLineEdge = clear;
    p.scrollTrack = Style(Col_ScrollbarBg);
    p.scrollThumb = Style(Col_ScrollbarGrab);
    p.scrollThumbHot = Style(Col_ScrollbarGrabHovered);
    return p;
}

} // namespace

bool InputTextMultiline(const char* label, char* buf, int cap, Vec2 size, unsigned flags) {
    if (!buf || cap < 1) return false;
    if (buf[cap - 1] != '\0') {   // never trust the caller's buffer to be terminated; cut at a whole character
        buf[cap - 1] = '\0';
        int n = (int)std::strlen(buf);
        while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80) --n;   // inside a character: back to its start
        if (n < (int)std::strlen(buf)) buf[n] = '\0';
    }
    const char* name = label ? label : "";
    const long long now = GetFrameCount();
    PushID(name);
    const unsigned id = GetID("##multiline");
    PopID();
    Box& box = BoxFor(id, now);
    box.frame = now;
    CodeEdit& ed = *box.ed;
    ed.SetReadOnly((flags & InputText_ReadOnly) != 0);
    ed.SetPalette(ThemePalette());
    if (box.last != buf) {   // changed behind our back (or the first sight of it)
        ed.SetText(buf);
        box.last = buf;
    }

    const float lw = CalcTextSize(name).x > 0.0f && !(name[0] == '#' && name[1] == '#') ? CalcTextSize(name).x + 8.0f : 0.0f;
    float w = size.x > 0.0f && std::isfinite(size.x) ? size.x : GetContentRegionAvail().x - lw;
    const float h = size.y > 0.0f && std::isfinite(size.y) ? size.y : GetFrameHeight() * 5.0f;
    w = std::max(32.0f, w);
    PushID(name);
    const bool edited = ed.Draw("##multiline", w, std::max(24.0f, h));
    PopID();
    if (lw > 0.0f) { const Rect r = GetItemRect(); GetWindowDrawList().AddText(Vec2{r.x + r.w + 8.0f, r.y + (std::min(r.h, GetFrameHeight()) - CalcTextSize(name).y) * 0.5f}, GetColorU32(Col_Text), name); }   // the label, to the right

    bool changed = false;
    if (edited && !(flags & InputText_ReadOnly)) {
        std::string text = ed.GetText();
        if ((int)text.size() >= cap) {                       // does not fit: cut at a whole character, keep the caret where it was
            int n = cap - 1;
            while (n > 0 && ((unsigned char)text[(size_t)n] & 0xC0) == 0x80) --n;
            text.resize((size_t)n);
            const CodePos c = ed.Caret(), a = ed.Anchor();
            ed.SetText(text);
            ed.SetSelection(a, c);
        }
        if (text != box.last) {
            std::memcpy(buf, text.data(), text.size());
            buf[text.size()] = '\0';
            box.last = text;
            changed = true;
        }
    }
    return changed;
}

} // namespace OkayUI
