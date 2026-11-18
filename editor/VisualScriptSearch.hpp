#pragma once
// Plain-word search for the add-node list: typing "move", "jump" or "score" finds the right blocks by name,
// by the other names people use for them (synonym groups), and by the words in their description. Starter blocks
// rank first. Pure functions, no ImGui, so a test can run them. Written for this project.
#include <cctype>
#include <string>
#include <vector>

namespace okay::editor::vsed {

/// Groups of words that mean nearly the same thing to a beginner. Typing one finds blocks named with any other.
inline const char* const kVsSynonymGroups[] = {
    "move walk go slide travel run follow chase",
    "jump hop leap",
    "score points point coin coins collect pickup",
    "sound audio noise music play beep",
    "say show text message write print speak label",
    "wait pause delay later timer",
    "repeat again forever",
    "destroy delete remove kill hide",
    "spawn create copy clone make duplicate",
    "scene level map room",
    "key press keyboard button",
    "click mouse tap",
    "touch hit bump collide trigger enter",
    "health life lives",
    "turn rotate spin",
    "if check condition question",
    "game over win lose",
    "door open close",
};

inline std::vector<std::string> VsSearchWords(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (std::isalnum((unsigned char)c)) cur += (char)std::tolower((unsigned char)c);
        else if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

inline bool VsHasWord(const std::vector<std::string>& words, const std::string& w) {
    for (const std::string& x : words) if (x == w) return true;
    return false;
}

/// The other words people use for what `text` says (space separated). Empty when there are none.
inline std::string VsSynonymWords(const std::string& text) {
    const std::vector<std::string> have = VsSearchWords(text);
    std::string out;
    for (const char* group : kVsSynonymGroups) {
        const std::vector<std::string> g = VsSearchWords(group);
        bool hit = false;
        for (const std::string& w : g) if (VsHasWord(have, w)) { hit = true; break; }
        if (!hit) continue;
        for (const std::string& w : g) if (!VsHasWord(have, w)) { if (!out.empty()) out += ' '; out += w; }
    }
    return out;
}

/// How well a block matches what was typed: 0 when a typed word is found nowhere. Name words count most,
/// then the other names (keywords and synonyms), then the description.
inline int VsMatchScore(const std::string& typed, const std::string& title, const std::string& keywords,
                        const std::string& description) {
    const std::vector<std::string> q = VsSearchWords(typed);
    if (q.empty()) return 1;
    const std::vector<std::string> t = VsSearchWords(title), k = VsSearchWords(keywords + " " + VsSynonymWords(title)),
                                   d = VsSearchWords(description);
    auto prefix = [](const std::vector<std::string>& words, const std::string& w) {
        for (const std::string& x : words) if (x.compare(0, w.size(), w) == 0) return true;
        return false;
    };
    int total = 0;
    for (const std::string& w : q) {
        int best = 0;
        if (VsHasWord(t, w)) best = 100;
        else if (prefix(t, w)) best = 70;
        else if (VsHasWord(k, w)) best = 40;
        else if (prefix(k, w)) best = 25;
        else if (VsHasWord(d, w)) best = 15;
        if (best == 0) return 0;
        total += best;
    }
    // A title that is exactly what was typed beats one that only contains it.
    if (t.size() == q.size()) total += 30;
    return total;
}

/// Starter blocks come first: the same match is worth more when the block is in the Starter group.
/// Text-script one-to-one blocks (type starts with "Fn.") come last: a beginner wants the ready-made block.
inline int VsStarterBoost(int score, bool starter, bool scriptBuiltin = false) {
    if (score <= 0) return score;
    if (starter) return score + 120;
    return scriptBuiltin && score > 40 ? score - 40 : score;
}

} // namespace okay::editor::vsed
