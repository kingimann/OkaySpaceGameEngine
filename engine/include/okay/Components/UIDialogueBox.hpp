#pragma once
// A dialogue box for scene UI: a speaker name, body text that types itself out, up to four choice
// buttons and a "next" prompt. It is plain data plus a few calls, so any script, visual script or
// C++ code can drive it (the .okaydialog runner has its own built-in box; this one is for UI you
// lay out yourself).
//
// The box finds its parts by child name: "Speaker" (text), "Body" (text), "Choice1".."Choice4"
// (buttons, each with a text label child or its own label), "Next" (anything shown when a line is
// finished and there is no choice). Parts that are missing are skipped. The "Dialogue" template in
// UI > Prebuilt Screens builds all of them.
//
//   box->Show("Mara", "Where did you find this?", {"On the road", "None of your business"});
//   // ... the player clicks a choice: script on_dialogue_choice() runs, box->LastChoice() is 0 or 1
//
// Events on the object's script: on_dialogue_line() when a line starts, on_dialogue_done() when the
// player moves past a line that has no choices, on_dialogue_choice() when a choice is picked.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include <string>
#include <vector>

namespace okay {

class UIDialogueBox final : public Behaviour {
public:
    float charsPerSecond = 45.0f;      ///< typewriter speed; 0 = the whole line at once
    bool  hideWhenDone = true;         ///< close the box when a line without choices is advanced
    bool  advanceOnClick = true;       ///< a click on the box (or Space / Enter) finishes or advances the line
    bool  startHidden = true;          ///< the box is hidden until Show() is called

    /// Show a line. `choices` (up to 4) turn into buttons once the line has finished typing.
    void Show(const std::string& speaker, const std::string& text,
              const std::vector<std::string>& choices = {});
    /// Remember a line without showing it (a box laid out in the editor with its first words). Restart() then
    /// shows it, from the start, with the typing effect.
    void Prime(const std::string& speaker, const std::string& text, const std::vector<std::string>& choices = {}) {
        m_speaker = speaker; m_text = text; m_choices.assign(choices.begin(), choices.begin() + (std::ptrdiff_t)(choices.size() < 4 ? choices.size() : 4));
        m_primed = true;
    }
    /// Show the last line again from its first letter (a Talk button). False when no line was ever shown or primed.
    bool Restart() {
        if (!m_primed && m_text.empty()) return false;
        const std::string sp = m_speaker, tx = m_text; const std::vector<std::string> ch = m_choices;
        Show(sp, tx, ch);
        return true;
    }
    /// Finish the typing, or (when already finished and there are no choices) move on.
    void Advance();
    /// Pick a choice (0-based). False when there is no such choice or the line is still typing.
    bool Choose(int index);
    void Close();

    bool IsOpen() const { return m_open; }
    bool IsTyping() const { return m_open && !m_complete; }
    bool WaitingForChoice() const { return m_open && m_complete && !m_choices.empty(); }
    int  LastChoice() const { return m_lastChoice; }
    const std::string& Speaker() const { return m_speaker; }
    const std::string& Line() const { return m_text; }
    const std::vector<std::string>& Choices() const { return m_choices; }

    void Start() override;
    void Update(float dt) override;
    void OnDisable() override { m_open = false; }

private:
    GameObject* Part(const char* name) const;
    void Refresh();
    void Fire(const char* fn);
    std::string m_speaker, m_text;
    std::vector<std::string> m_choices;
    bool m_open = false, m_complete = true;
    int  m_lastChoice = -1;
    float m_shown = 0.0f;
    unsigned m_serial = 0;
    bool  m_primed = false;
    bool  m_justShown = false;
};

} // namespace okay
