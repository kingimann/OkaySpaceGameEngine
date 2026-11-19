#pragma once
// The start screen of the Visual Script window: when a graph is new or empty it shows big recipe cards instead of
// a blank canvas. This header holds the cards (the existing recipes, VisualScriptRecipes.hpp, one plain sentence
// each), when the screen shows, and how the cards are laid out. No ImGui: the canvas draws, tests check the rules.
#include "VisualScriptRecipes.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace okay::editor::vsed {

struct VsStartCard {
    int recipe = 0;          ///< index into VsRecipes()
    std::string title;
    std::string line;        ///< one plain sentence
};

/// One sentence of a recipe blurb: the part before " Example:".
inline std::string VsStartSentence(const char* blurb) {
    std::string s = blurb ? blurb : "";
    const auto e = s.find(" Example:");
    if (e != std::string::npos) s.erase(e);
    return s;
}

/// The goals a beginner has in mind, in the order the start screen lists them: recipe name -> the goal in plain words.
struct VsStartGoal { const char* recipe; const char* goal; };
inline const VsStartGoal kVsStartGoals[] = {
    {"Move with arrow keys", "Make it move when I press a key"},
    {"Open a door when touched", "Do something when two things touch"},
    {"Show the score", "Show a score"},
    {"Play a sound on click", "Play a sound"},
    {"Spawn a copy when I click", "Spawn something"},
    {"Wait, then do something", "Wait then do something"},
};

/// The cards: the goals above first (titled with the goal), then the other recipes in listed order. At most `maxCards` (8).
inline std::vector<VsStartCard> VsStartCards(int maxCards = 8) {
    std::vector<VsStartCard> out;
    const auto& r = VsRecipes();
    std::vector<bool> used(r.size(), false);
    for (const VsStartGoal& g : kVsStartGoals)
        for (int i = 0; i < (int)r.size() && (int)out.size() < maxCards; ++i)
            if (!used[(std::size_t)i] && std::string(r[(std::size_t)i].name) == g.recipe) {
                used[(std::size_t)i] = true;
                out.push_back({i, g.goal, VsStartSentence(r[(std::size_t)i].blurb)});
            }
    for (int i = 0; i < (int)r.size() && (int)out.size() < maxCards; ++i)
        if (!used[(std::size_t)i]) out.push_back({i, r[(std::size_t)i].name, VsStartSentence(r[(std::size_t)i].blurb)});
    return out;
}

/// Show the start screen? Only for a graph with no blocks, and not when the person chose "Start empty" for this
/// graph, hid it for good, or the file could not be opened.
inline bool VsStartVisible(int nodeCount, bool hiddenForGood, bool startedEmpty, bool fileMissing) {
    return nodeCount == 0 && !hiddenForGood && !startedEmpty && !fileMissing;
}

struct VsStartRect { float x = 0, y = 0, w = 0, h = 0; };

/// Positions for `count` cards in a canvas `w` x `h` wide (with a header of `headerH` above and `footerH` below
/// for the title and the buttons). Cards in rows of up to `perRow`, each with the same size, centred.
inline std::vector<VsStartRect> VsStartLayout(int count, float w, float h, float headerH = 110.0f, float footerH = 70.0f,
                                              int perRow = 4, float gap = 14.0f) {
    std::vector<VsStartRect> r;
    if (count <= 0) return r;
    const int cols = std::max(1, std::min(perRow, count));
    const int rows = (count + cols - 1) / cols;
    float cw = std::min(250.0f, (w - 48.0f - gap * (cols - 1)) / (float)cols);
    float ch = std::min(150.0f, (h - headerH - footerH - gap * (rows - 1)) / (float)rows);
    cw = std::max(cw, 60.0f); ch = std::max(ch, 50.0f);
    const float totalW = cw * cols + gap * (cols - 1), totalH = ch * rows + gap * (rows - 1);
    const float x0 = (w - totalW) * 0.5f;
    const float y0 = std::max(headerH, (h - totalH) * 0.5f - 10.0f);
    for (int i = 0; i < count; ++i) {
        const int c = i % cols, row = i / cols;
        r.push_back({x0 + c * (cw + gap), y0 + row * (ch + gap), cw, ch});
    }
    return r;
}

} // namespace okay::editor::vsed
