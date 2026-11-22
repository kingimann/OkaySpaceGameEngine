#pragma once
// One-click recipes: a small ready-made graph, as plain data (the same text a saved .okayvs file holds), built
// only from nodes that already exist. The Recipes button in the Visual Script toolbar lists them. Each recipe
// carries a budget (most nodes, most wires) that tests/test_vs_recipes.cpp enforces, and each one is run in a
// headless scene by that test. Wording is for a 10 year old: short names, no programming words.
#include "VisualScriptGlossary.hpp"
#include <cstring>
#include <initializer_list>
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
/// A Rule block (WHEN / ONLY IF / DO, up to four actions) with every field written out, so no default sneaks in.
inline std::string Rule(const char* when, const char* wv, const char* cond, const char* cv,
                        std::initializer_list<const char*> actions, int id = 1) {
    std::string v[12] = {when, wv, cond, cv, "none", "", "none", "", "none", "", "none", ""};
    int i = 4;
    for (const char* a : actions) v[i++] = a;
    std::string g = "node " + std::to_string(id) + " QbRule\npos " + std::to_string(id) + " 40 40\n";
    for (int k = 0; k < 12; ++k) if (!v[k].empty()) g += "default " + std::to_string(id) + " " + std::to_string(k) + " " + v[k] + "\n";
    return g;
}
inline std::string Note(const char* t) { return std::string("comment 16 8 520 90 1 \"") + t + "\"\n"; }
} // namespace recipe

// Recipes are built from Quick blocks (okay/VisualScript/QuickBlocks.hpp): one block does a whole job, with the
// values typed on the block. Budget for every recipe: at most 3 blocks and 2 wires.
inline const std::vector<VsRecipe>& VsRecipes() {
    using recipe::Note;
    static const std::vector<VsRecipe> all = {
        {"Move with arrow keys", "The object slides when you press the arrow keys. Example: press the right arrow to go right.",
         "", "Solid", 1, 0, "Move with WASD",
         Note("Hold the arrow keys to move. Change the keys or the speed on the block.") +
         "node 1 QbMoveWithKeys\npos 1 40 40\ndefault 1 0 arrows\ndefault 1 1 5\n"},
        {"Jump with Space", "The object jumps up when you press Space. Example: press Space to hop over a box.",
         "The object needs a physics body (it is added for you).", "Rigidbody", 1, 0, "Jump",
         Note("Press Space to jump. Change the key or the strength on the block.") +
         "node 1 QbJumpOnKey\npos 1 40 40\ndefault 1 0 space\ndefault 1 1 7\n"},
        {"Collect coins and score", "Touch this object to get a point, and it disappears. Example: touch a coin to score 1.",
         "The object needs a touch zone (it is added for you).", "Trigger,Audio", 1, 0, "Pickup",
         Note("When something touches this, add points, play its sound and hide it.") +
         "node 1 QbCollectOnTouch\npos 1 40 40\ndefault 1 0 1\n"},
        {"Open a door when touched", "The door slides up when something touches it. Example: walk into the door to open it.",
         "The object needs a touch zone (it is added for you).", "Trigger", 1, 0, "Door",
         Note("When something touches this, it slides up out of the way.") +
         "node 1 QbOpenDoorWhenTouched\npos 1 40 40\ndefault 1 0 2\n"},
        {"Enemy follows the player", "The object walks toward the object named Player. Example: a ghost that chases you.",
         "Name the thing to follow Player.", "", 1, 0, "Chase",
         Note("This walks toward the object named Player. Change the name or the speed on the block.") +
         "node 1 QbFollowObject\npos 1 40 40\ndefault 1 0 Player\ndefault 1 1 3\n"},
        {"Say hello", "The object shows a message when the game starts. Example: show Hello! on the screen.",
         "The object needs a text (it is added for you).", "Text", 1, 0, "Message",
         Note("Shows a message when the game starts. Change the words in the Rule.") +
         recipe::Rule("start", "", "none", "", {"text", "\"Hello!\""})},
        {"Play a sound on click", "A sound plays when you click the object. Example: click a button to hear a beep.",
         "The object needs a sound (it is added for you).", "Audio", 1, 0, "",
         Note("Click this object to play its sound.") +
         recipe::Rule("click", "", "none", "", {"sound"})},
        {"Game over when health is 0", "The game says You lose when the object has no health left. Example: a hero that can die.",
         "The object needs health (it is added for you).", "Health", 1, 0, "",
         Note("When health reaches 0, the game ends and shows You lose.") +
         "node 1 QbGameOverAtZeroHealth\npos 1 40 40\n"},
        {"Spawn a copy when I click", "A new copy of a saved object appears when you click. Example: click to drop a coin.",
         "Type the file of a saved object in the Rule (a .okayprefab file).", "", 1, 0, "",
         Note("Click this object to make a copy of the saved object. Change the file in the Rule.") +
         recipe::Rule("click", "", "none", "", {"spawn", "examples/node_playground/marker.okayprefab"})},
        {"Change scene when touched", "The game moves to another scene when something touches this. Example: walk into a door to reach Level2.",
         "The object needs a touch zone (it is added for you). Type the scene name in the Rule.", "Trigger", 1, 0, "",
         Note("When something touches this, go to the scene named in the Rule.") +
         recipe::Rule("touched", "", "none", "", {"scene", "Level2"})},
        {"Lava that hurts", "Anything that touches this loses health. Example: lava that hurts the player.",
         "The object needs a touch zone (it is added for you).", "Trigger", 1, 0, "",
         Note("When something touches this, it loses 10 health. Change the amount on the block.") +
         "node 1 QbHurtOnTouch\npos 1 40 40\ndefault 1 0 10\n"},
        {"Heart that heals", "Anything that touches this gets health back. Example: a heart that heals the player.",
         "The object needs a touch zone (it is added for you).", "Trigger", 1, 0, "",
         Note("When something touches this, it gets 25 health back.") +
         "node 1 QbHealOnTouch\npos 1 40 40\ndefault 1 0 25\n"},
        {"Guard walks back and forth", "The object walks to one side and back, over and over. Example: a guard on patrol.",
         "", "", 1, 0, "Patrol",
         Note("Walks 3 steps each way. Change the distance or the speed on the block.") +
         "node 1 QbPatrol\npos 1 40 40\ndefault 1 0 3\ndefault 1 1 1.5\n"},
        {"Enemy runs away", "The object runs away from the object named Player. Example: a rabbit that is hard to catch.",
         "Name the thing to run from Player.", "", 1, 0, "",
         Note("Runs away from the object named Player.") +
         "node 1 QbRunAwayFrom\npos 1 40 40\ndefault 1 0 Player\ndefault 1 1 3\n"},
        {"Bobbing coin", "The object bobs up and down. Example: a coin that bobs above the ground.",
         "", "", 1, 0, "",
         Note("Bobs up and down. Change the height or the speed on the block.") +
         "node 1 QbFloatUpDown\npos 1 40 40\ndefault 1 0 0.5\ndefault 1 1 2\n"},
    };
    return all;
}

/// A small complete game as scripts on four objects, for the block and wire counts in the notes:
/// collect 5 coins (the score shows on screen), avoid a patrolling enemy, game over on touch, win at 5.
struct VsSampleScript { const char* object; const char* what; std::string graph; };
inline const std::vector<VsSampleScript>& VsSampleGame() {
    static const std::vector<VsSampleScript> all = {
        {"Player", "moves with the arrow keys",
         "node 1 QbMoveWithKeys\ndefault 1 0 arrows\ndefault 1 1 5\n"},
        {"Coin (five copies)", "gives a point when touched",
         "node 1 QbCollectOnTouch\ndefault 1 0 1\n"},
        {"Enemy", "walks back and forth; touching the player ends the game",
         "node 1 QbPatrol\ndefault 1 0 3\ndefault 1 1 1.5\nnode 2 QbGameOverOnTouch\n"},
        {"Scoreboard", "shows the score and says You win at 5",
         "node 1 QbShowScore\ndefault 1 0 \"Score: \"\nnode 2 QbWinAtScore\ndefault 2 0 5\n"},
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
