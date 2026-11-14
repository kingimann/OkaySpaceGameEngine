#pragma once
// The host side of input methods (IME): the events an operating system's input method sends (a preedit string that is still being
// composed, a commit, a cancel) and what a program must tell it back (a text field has the keyboard, so start text input; where the
// caret is, so the candidate window can sit under it). Pure C++: no SDL, no window, so it is tested headless with an in-memory sink
// (tests/test_okayui_ime.cpp). shared/OkayUIHost.hpp wires it to SDL (SDL_TEXTEDITING, SDL_StartTextInput, SDL_SetTextInputRect).
//
// The frame, as a host runs it:
//     ime.Preedit(text, caret) / ime.Commit(text) / ime.Cancel()     as the system delivers them (any thread that is the UI thread)
//     OkayUI::Input in = ...;  ime.Apply(in);                         before OkayUI::BeginFrame(in)
//     ...OkayUI widgets run...
//     ime.AfterFrame(sink);                                           after the frame: starts / stops text input, places the candidate window
//
// What this does NOT prove: that a real input method (Windows TSF, macOS NSTextInputClient, X11 XIM / IBus, Wayland text-input) behaves
// like these events on a real window. Everything here is exercised with synthetic events only.
#include "okay/UI/OkayUI.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace okayuihost {

/// Where the program tells the system what it needs (the real one is SDL; the test one records).
class ImeSink {
public:
    virtual ~ImeSink() {}
    virtual void StartTextInput() = 0;
    virtual void StopTextInput() = 0;
    /// The rectangle (pixels, top-left origin, window coordinates) of the text being edited: the candidate window goes under it.
    virtual void SetCandidateRect(float x, float y, float w, float h) = 0;
};

/// An in-memory sink for tests and headless hosts: remembers the last state and counts the calls.
class RecordingImeSink : public ImeSink {
public:
    void StartTextInput() override { active = true; ++starts; }
    void StopTextInput() override { active = false; ++stops; }
    void SetCandidateRect(float x, float y, float w, float h) override { rx = x; ry = y; rw = w; rh = h; ++rectCalls; haveRect = true; }
    bool active = false, haveRect = false;
    int starts = 0, stops = 0, rectCalls = 0;
    float rx = 0, ry = 0, rw = 0, rh = 0;
};

/// The input method's state between frames.
class ImeState {
public:
    static constexpr std::size_t kMaxPreedit = 1024;   // bytes kept; OkayUI shows at most 256 of them after cleaning

    /// The system's current preedit (a STATE: send it again on every change, send "" or Cancel when composing ends). `caretChars` is the
    /// caret position in characters (SDL's `start`), or -1 for "at the end". Bad UTF-8 is not an error here: OkayUI drops what it cannot show.
    void Preedit(const char* utf8, int caretChars = -1) {
        m_pre.clear();
        if (utf8) { for (const char* p = utf8; *p && m_pre.size() < kMaxPreedit; ++p) m_pre += *p; }
        m_caretBytes = CharsToBytes(m_pre, caretChars);
    }
    /// Text the input method finished: delivered once, and composing is over.
    void Commit(const char* utf8) {
        if (utf8 && *utf8) { if (m_commit.size() < kMaxPreedit) m_commit += std::string(utf8).substr(0, kMaxPreedit - m_commit.size()); }
        m_pre.clear(); m_caretBytes = -1;
    }
    /// Composing was cancelled (or the field lost the keyboard): the preedit goes, nothing is inserted.
    void Cancel() { m_pre.clear(); m_caretBytes = -1; }
    bool Composing() const { return !m_pre.empty(); }
    const std::string& PreeditText() const { return m_pre; }
    int PreeditCaretBytes() const { return m_caretBytes; }

    /// Fill `in` for the frame about to start (composition is a state and stays until it ends; a commit is cleared once handed out).
    /// The returned pointers stay valid until the next Apply.
    void Apply(OkayUI::Input& in) {
        m_outPre = m_pre; m_outCommit = m_commit; m_commit.clear();
        in.composition = m_outPre.empty() ? nullptr : m_outPre.c_str();
        in.compositionCaret = m_outPre.empty() ? -1 : m_caretBytes;
        in.commit = m_outCommit.empty() ? nullptr : m_outCommit.c_str();
    }

    /// After the frame: tell the sink whether text input should be on (a text field has the keyboard), and where the candidate window
    /// goes (the preedit span while composing, else the caret). Only changes are sent: no call per frame while nothing moves.
    void AfterFrame(ImeSink& sink) {
        const bool want = OkayUI::WantsTextInput();
        if (want && !m_active) { sink.StartTextInput(); m_active = true; m_haveRect = false; }
        else if (!want && m_active) { sink.StopTextInput(); m_active = false; m_haveRect = false; Cancel(); }   // a field that lost the keyboard drops a half-made composition
        if (!m_active) return;
        const OkayUI::ImeRect r = OkayUI::GetImeRect();
        if (!r.valid) return;
        const OkayUI::Rect& use = (Composing() && r.composition.h > 0.0f) ? r.composition : r.caret;
        if (!m_haveRect || use.x != m_last.x || use.y != m_last.y || use.w != m_last.w || use.h != m_last.h) {
            sink.SetCandidateRect(use.x, use.y, use.w, use.h);
            m_last = use; m_haveRect = true;
        }
    }
    bool TextInputActive() const { return m_active; }

    /// UTF-8 characters (code points) to a byte offset into `s`; -1 stays -1; past the end is the end.
    static int CharsToBytes(const std::string& s, int chars) {
        if (chars < 0) return -1;
        std::size_t i = 0; int n = 0;
        while (i < s.size() && n < chars) {
            const unsigned char c = (unsigned char)s[i];
            std::size_t k = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            i += k; ++n;
        }
        return (int)std::min(i, s.size());
    }

private:
    std::string m_pre, m_commit, m_outPre, m_outCommit;
    int  m_caretBytes = -1;
    bool m_active = false, m_haveRect = false;
    OkayUI::Rect m_last;
};

} // namespace okayuihost
