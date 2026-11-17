#pragma once
// One-click recipes: a small ready-made graph, as plain data (the same text a saved .okayvs file holds), built
// only from nodes that already exist. The Recipes button in the Visual Script toolbar lists them. Each recipe
// carries a budget (most nodes, most wires) that tests/test_vs_recipes.cpp enforces, and each one is run in a
// headless scene by that test. Wording is for a 10 year old: short names, no programming words.
#include "VisualScriptGlossary.hpp"
#include <cstring>
#include <string>
#include <vector>

namespace okay::editor::vsed {

struct VsRecipe {
    const char* name;
    const char* blurb;      // one plain sentence, with a tiny example
    const char* needs;      // what the object needs (empty: nothing)
    const char* add;        // parts to add on the selected object, same words as VsTemplate::add
    int maxNodes;           // budget: a recipe that needs more fails its test
    int maxWires;
    const char* oldTemplate; // the longer template that does the same job (VisualScriptTemplates.hpp), or ""
    std::string graph;
};

namespace recipe {
inline std::string Note(const char* t) { return std::string("comment 16 8 520 90 1 \"") + t + "\"\n"; }
} // namespace recipe

inline const std::vector<VsRecipe>& VsRecipes() {
    using recipe::Note;
    static const std::vector<VsRecipe> all = {
        {"Move with arrow keys", "The object slides when you press the arrow keys or W A S D. Example: press D to go right.",
         "", "Solid", 7, 6, "Move with WASD",
         Note("Press the arrow keys or W A S D to move. Change 5 to go faster.") +
         "node 1 OnUpdate\npos 1 40 40\nentry OnUpdate 1\n"
         "node 2 GlGetHorizontalInput\npos 2 40 160\n"
         "node 3 Mul\npos 3 280 160\ndefault 3 1 5\ndata 3 0 2 0\n"
         "node 4 GlMoveSideways\npos 4 520 40\ndata 4 0 3 0\nexec 1 0 4\n"
         "node 5 GlGetVerticalInput\npos 5 40 300\n"
         "node 6 Mul\npos 6 280 300\ndefault 6 1 5\ndata 6 0 5 0\n"
         "node 7 GlMoveForward\npos 7 760 40\ndata 7 0 6 0\nexec 4 0 7\n"},
        {"Jump with Space", "The object jumps up when you press Space. Example: press Space to hop over a box.",
         "The object needs a physics body (it is added for you).", "Rigidbody", 2, 1, "Jump",
         Note("Press Space to jump. Change 7 to jump higher.") +
         "node 1 OnKey space\npos 1 40 40\nentry OnKey:space 1\n"
         "node 2 GlJump\npos 2 300 40\ndefault 2 0 7\nexec 1 0 2\n"},
        {"Collect coins and score", "Touch this object to get a point, and it disappears. Example: touch a coin to score 1.",
         "The object needs a touch zone (it is added for you).", "Trigger", 3, 2, "Pickup",
         Note("When something touches this, add 1 to the score and hide it.") +
         "node 1 OnTrigger\npos 1 40 40\nentry OnTrigger 1\n"
         "node 2 ScoreAdd\npos 2 300 40\ndefault 2 0 1\nexec 1 0 2\n"
         "node 3 SetActive self\npos 3 560 40\ndefault 3 0 0\nexec 2 0 3\n"},
        {"Open a door when touched", "The door slides up when something touches it. Example: walk into the door to open it.",
         "The object needs a touch zone (it is added for you).", "Trigger", 2, 1, "Door",
         Note("When something touches this, it moves up by 2.") +
         "node 1 OnTrigger\npos 1 40 40\nentry OnTrigger 1\n"
         "node 2 Translate\npos 2 300 40\ndefault 2 0 0\ndefault 2 1 2\ndefault 2 2 0\nexec 1 0 2\n"},
        {"Enemy follows the player", "The object walks toward the object named Player. Example: a ghost that chases you.",
         "Name the thing to follow Player.", "", 2, 1, "Chase",
         Note("This walks toward the object named Player. Change 3 to go faster.") +
         "node 1 OnUpdate\npos 1 40 40\nentry OnUpdate 1\n"
         "node 2 MoveTowards Player\npos 2 300 40\ndefault 2 0 3\nexec 1 0 2\n"},
        {"Say hello", "The object shows a message when the game starts. Example: show Hello! on the screen.",
         "The object needs a text (it is added for you).", "Text", 2, 1, "Message",
         Note("Shows a message when the game starts. Click the message to change it.") +
         "node 1 OnStart\npos 1 40 40\nentry OnStart 1\n"
         "node 2 SetText self\npos 2 300 40\ndefault 2 0 \"Hello!\"\nexec 1 0 2\n"},
        {"Play a sound on click", "A sound plays when you click the object. Example: click a button to hear a beep.",
         "The object needs a sound (it is added for you).", "Audio", 2, 1, "",
         Note("Click this object to play its sound.") +
         "node 1 OnClick\npos 1 40 40\nentry OnClick 1\n"
         "node 2 PlaySound\npos 2 300 40\nexec 1 0 2\n"},
        {"Game over when health is 0", "The game says You lose when the object has no health left. Example: a hero that can die.",
         "The object needs health (it is added for you).", "Health", 2, 1, "",
         Note("When health reaches 0, the game ends and shows You lose.") +
         "node 1 OnDeath\npos 1 40 40\nentry OnDeath 1\n"
         "node 2 Lose\npos 2 300 40\nexec 1 0 2\n"},
    };
    return all;
}

inline int VsRecipeCount() { return (int)VsRecipes().size(); }

/// Counts the "node" lines in a saved graph.
inline int VsGraphNodeCount(const std::string& graph) {
    int n = 0;
    std::size_t pos = 0;
    while (pos < graph.size()) {
        std::size_t e = graph.find('\n', pos);
        if (e == std::string::npos) e = graph.size();
        if (graph.compare(pos, 5, "node ") == 0) ++n;
        pos = e + 1;
    }
    return n;
}

/// Counts the wires ("exec" and "data" lines) in a saved graph.
inline int VsGraphWireCount(const std::string& graph) {
    int n = 0;
    std::size_t pos = 0;
    while (pos < graph.size()) {
        std::size_t e = graph.find('\n', pos);
        if (e == std::string::npos) e = graph.size();
        if (graph.compare(pos, 5, "exec ") == 0 || graph.compare(pos, 5, "data ") == 0) ++n;
        pos = e + 1;
    }
    return n;
}

} // namespace okay::editor::vsed
