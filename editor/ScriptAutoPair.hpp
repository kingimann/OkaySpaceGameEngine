#pragma once
// Script Editor bracket and quote pairing, decided once per frame before the code editor takes the frame's typing
// (editor/main.cpp): ( [ { " ' get their closer, and typing a closer that is already right after the caret steps over it
// instead of doubling it.
//
// The decision must look at the SAME characters the code editor is about to type. The ImGui editor (ImGuiColorTextEdit) types
// from Dear ImGui's io.InputQueueCharacters; our own editor (OKAY_CODE_EDITOR=own) types from OkayUI's frame input, which gets
// every character in the frame its event arrived. The two queues can disagree for a frame: Dear ImGui "trickles" a character
// to the next frame when a non-character key changed or the mouse moved in the same frame (Escape closing the autocomplete
// list, then `)`). Deciding from ImGui's queue while ours typed from OkayUI's made `)` before an auto-inserted `)` type a
// second one ("zz(1))"). TypedForPairing takes the characters from the editor that will type them.
#include <cctype>
#include <string>

namespace scriptpair {

struct Decision {
    char open = 0;          // an opener was typed: insert this closer after it (the caret stays between)
    bool stepOver = false;  // the closer typed is already right after the caret: drop the typed one, move the caret past it
};

/// What one typed character does, given the caret's line and column (a byte index into `line`). `typed` is the text the code
/// editor types this frame; pairing only acts on exactly one character, as the ImGui path always has.
inline Decision Decide(const std::string& line, int col, const std::string& typed) {
    Decision d;
    if (typed.size() != 1) return d;
    const char ch = typed[0];
    const char next = col >= 0 && col < (int)line.size() ? line[(size_t)col] : 0;
    const char prev = col > 0 && col - 1 < (int)line.size() ? line[(size_t)col - 1] : 0;
    if ((ch == ')' || ch == ']' || ch == '}' || ch == '"' || ch == '\'') && next == ch) {
        d.stepOver = true;
    } else if (ch == '(' || ch == '[' || ch == '{' || ch == '"' || ch == '\'') {
        bool ok = next == 0 || next == ' ' || next == ')' || next == ']' || next == '}' || next == ';' || next == ',';
        if ((ch == '"' || ch == '\'') && (std::isalnum((unsigned char)prev) || prev == '_')) ok = false;   // it's / x"
        if (ok) d.open = ch == '(' ? ')' : ch == '[' ? ']' : ch == '{' ? '}' : ch;
    }
    return d;
}

/// The characters the code editor types this frame. `okayText` is OkayUI's frame text (OkayUI::GetFrameInput().text, may be
/// null), `imguiQueue`/`imguiCount` Dear ImGui's io.InputQueueCharacters. Our editor types OkayUI's, the ImGui editor ImGui's.
/// Only ASCII matters for pairing; anything else comes back as an empty or multi-character string (no pairing).
template <class Wchar>
inline std::string TypedForPairing(bool ownEditor, const char* okayText, const Wchar* imguiQueue, int imguiCount) {
    std::string out;
    if (ownEditor) {
        if (okayText) out = okayText;
        return out;
    }
    for (int i = 0; i < imguiCount; ++i) {
        const unsigned long c = (unsigned long)imguiQueue[i];
        out.push_back(c < 0x80 ? (char)c : '\x7f');   // a non-ASCII character pairs with nothing
    }
    return out;
}

} // namespace scriptpair
