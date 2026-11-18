#pragma once
// Where a node sits in the Add Node menu. Every node has one topic from kPaletteCategories. A blank
// subcategory means it sits directly in that topic. docs/visual_scripting_naming.md has the rules for new nodes.
//
//   VisualScriptCategories.hpp     this file: the topics, the subcategories, the public lookups
//   VisualScriptPlaceRules.hpp     the rules: which node goes in which topic (data, one row per node or family)
//   VisualScriptLegacyPlaces.hpp   where nodes sat BEFORE the topics were split (search words only)
//   VisualScriptPlace.hpp          VsPlace and two small "same job" tests
#include "VisualScriptLegacyPlaces.hpp"
#include "VisualScriptPlace.hpp"
#include "VisualScriptPlaceRules.hpp"

#include <cctype>
#include <cstring>
#include <string>

#include "okay/VisualScript/QuickBlocks.hpp"

namespace okay::editor::vsed {

struct VsPaletteCategory {
    const char* name;
    const char* blurb;
    const char* section;   // the muted heading above this topic (and its neighbours) in the sidebar
};

// Common topics first. The order is the sidebar order, and topics of one section sit together.
// Rules: at most kMaxDirectPerTopic nodes sit directly in a topic (the rest go into subcategories), and a subcategory
// holds at most kMaxPerSubcategory. tests/test_vs_categories.cpp enforces both on every node the menu lists.
inline constexpr int kMaxDirectPerTopic = 30;
inline constexpr int kMaxPerSubcategory = 45;
inline const VsPaletteCategory kPaletteCategories[] = {
    // ---- Events: where a script starts. Every "On ..." node is in one of these five.
    {"Game Events",     "Where a script starts: the frame, being switched on or off, a message, a changed value, a death.", "Events"},
    {"Input Events",    "When a key, a mouse button, a touch or a controller action happens.", "Events"},
    {"Collision & Trigger Events", "When things touch, enter an area or leave it, and what the touch was like.", "Events"},
    {"Inventory & Quest Events",   "When an item is added or used, a quest moves on, or a conversation ends.", "Events"},
    {"Network Events",  "When another player's message arrives, or the server catches a cheat.", "Events"},
    // ---- Flow: the order things happen in.
    {"Branches & Gates", "Choose a path, run things in order, let the flow through only some of the time, and keep state machines.", "Flow"},
    {"Loops",           "Do something again and again, for each item, or until something changes.", "Flow"},
    {"Waiting & Timers", "Wait, repeat on a timer, cooldowns, stopwatches, and delayed calls from a script.", "Flow"},
    {"Functions & Calls", "Define your own function, call another graph or script, or run a list of steps.", "Flow"},
    {"Time & Game Speed", "The clock, the frame time, slow motion and pausing the game.", "Flow"},
    // ---- Quick blocks: one-block ready-made actions for beginners.
    {"Quick blocks",    "One-block actions: move around, game rules, objects in the game, show and sound, do in order.", "Quick blocks"},
    // ---- Variables, lists and saving: remember things.
    {"Variables",       "Remember a number, a word or a yes/no on this object, the scene or the whole game.", "Variables, Lists & Saving"},
    {"Saving & Loading", "Keep things between runs: save slots, saved values and checkpoints.", "Variables, Lists & Saving"},
    {"Lists",           "Several values in order under one name: add, remove, sort, search, and array functions.", "Variables, Lists & Saving"},
    {"Maps & Structs",  "Values you look up by a name, records with fields, and JSON text.", "Variables, Lists & Saving"},
    // ---- Math
    {"Numbers",         "Everyday math: arithmetic, rounding, limits, averages, percentages, bits, type checks and script math functions.", "Math"},
    {"Blending & Easing", "Mix two values, remap a range, and smooth steps with curves, waves and springs.", "Math"},
    {"Angles & Trigonometry", "Degrees and radians, sine and cosine, and the angle between two things.", "Math"},
    {"Vectors",         "Points and directions: build, measure, add, scale, and the flat (ground) versions.", "Math"},
    {"Comparisons & Logic", "Is it bigger, equal or between? And, or, not, picking a value, and comparing scores or number sets.", "Math"},
    {"Random",          "Dice rolls, random picks, chances and noise.", "Math"},
    // ---- Text
    {"Text",            "Join, cut, search, clean up, format and compare words and sentences, and use the clipboard.", "Text"},
    {"Language & Translation", "Show the game in the player's language and react when it changes.", "Text"},
    // ---- Objects
    {"Create & Destroy", "Make new objects, remove them, and keep spawners and pools of them.", "Objects"},
    {"Find Objects",    "Find objects by name, tag or distance, and read their names and tags.", "Objects"},
    {"Show & Hide",     "Switch objects on and off, and show or hide what they draw.", "Objects"},
    {"Parents & Components", "Parents and children, and the parts (components) an object has.", "Objects"},
    {"Messages & Signals", "Tell other objects that something happened.", "Objects"},
    {"Color & Appearance", "Color, sprites, textures, materials and fading.", "Objects"},
    {"Particles & Trails", "Particles, lines and trails.", "Objects"},
    // ---- Movement & physics
    {"Position",        "Where an object is, putting it somewhere else, and keeping it in bounds.", "Movement & Physics"},
    {"Rotation & Scale", "Turn an object, face or look at something, and change its size.", "Movement & Physics"},
    {"Move & Follow",   "Walk toward something, follow, chase, patrol, orbit, bob, spin, motion patterns and script functions.", "Movement & Physics"},
    {"Tweens",          "Smoothly change a position, size, color or number over time.", "Movement & Physics"},
    {"Paths & Splines", "Curves and paths an object can follow.", "Movement & Physics"},
    {"Forces & Velocity", "Push things, set their speed, jump, and set up their physics body.", "Movement & Physics"},
    {"Raycasts & Overlaps", "Shoot an invisible line or shape and see what it hits: ray results, overlaps and shape casts.", "Movement & Physics"},
    {"Sight & Distance Checks", "Is the target close, in front, behind, to the left or in view?", "Movement & Physics"},
    // ---- Gameplay
    {"Health & Damage", "Health, damage, healing, armor, lives, respawning, regeneration and resources.", "Gameplay"},
    {"Weapons & Shooting", "Projectiles, shooting, swinging, reloading and ammo.", "Gameplay"},
    {"Inventory & Items", "Pick up, carry, equip, craft and use items: equipment, slots, loot and limits.", "Gameplay"},
    {"Quests & Dialogue", "Quests and their objectives, conversations, and story flags.", "Gameplay"},
    {"Score & Winning", "Score, experience and levels, winning, losing and restarting.", "Gameplay"},
    {"Doors & Switches", "Doors, levers and wired logic signals.", "Gameplay"},
    {"Build Mode",      "Let players place and remove objects, and save what they built.", "Gameplay"},
    // ---- Characters and AI
    {"Animation",       "Play and stop animation clips, and set animator parameters and blend shapes.", "Characters & AI"},
    {"Character Actions", "Roll, vault, climb, sit, die, stand and crouch, and ragdoll.", "Characters & AI"},
    {"Inverse Kinematics", "Make hands reach, feet plant and heads look at things (IK).", "Characters & AI"},
    {"Navigation",      "Walk to a point around obstacles, on a mesh or on a grid.", "Characters & AI"},
    {"Behavior Trees",  "Run a behavior tree, read and set its blackboard, and build outcomes.", "Characters & AI"},
    // ---- Sound, camera and screen
    {"Sound Effects",   "Play, stop and shape sounds on an object.", "Sound, Camera & Screen"},
    {"Music & Volume",  "Music tracks, the volume of each group, muting and ducking.", "Sound, Camera & Screen"},
    {"Camera",          "Where the player is looking: follow, switch, zoom, tilt and shake.", "Sound, Camera & Screen"},
    {"Screen Text",     "Words and numbers drawn on the screen.", "Sound, Camera & Screen"},
    {"Buttons & Inputs", "Buttons, sliders, toggles, dropdowns, text fields and pickers.", "Sound, Camera & Screen"},
    {"Bars & Layout",   "Progress bars, tabs and pages, scroll areas and grids, layout helpers and themes.", "Sound, Camera & Screen"},
    {"Popups & Dialogs", "Toasts, tooltips, modals and bottom sheets.", "Sound, Camera & Screen"},
    // ---- Scenes and the environment
    {"Scenes & Levels", "Load, restart and quit scenes, and read which scene is open.", "Scenes & Environment"},
    {"Lighting & Sky",  "Ambient light, background color and the lights in a scene.", "Scenes & Environment"},
    {"Weather & Fog",   "Weather, wind and fog.", "Scenes & Environment"},
    {"Post Effects",    "Full-screen looks: bloom, exposure, color grade and the like.", "Scenes & Environment"},
    {"Window & Display", "Screen size, display modes, refresh rate and the game window.", "Scenes & Environment"},
    {"Cutscenes",       "Play a cutscene (a sequence) and build one from tweens.", "Scenes & Environment"},
    // ---- Input
    {"Keyboard",        "Is a key held, pressed or released.", "Input"},
    {"Mouse & Touch",   "The mouse position and buttons, the cursor, taps and swipes.", "Input"},
    {"Gamepad & Controls", "Movement axes, the gamepad, named actions and rebinding keys.", "Input"},
    // ---- Online
    {"Multiplayer",     "Host or join, browse servers, matchmaking, messages, synced objects, players and matches.", "Online"},
    {"Steam",           "Steam achievements, stats, lobbies, friends, cloud files and the workshop.", "Online"},
    {"Server Security", "Anti-cheat: validate what clients send, rate limits, kicks and budgets.", "Online"},
    {"Accounts & Online Saves", "Log in, cloud saves, leaderboards and PlayFab.", "Online"},
    // ---- Debugging
    {"Debug",           "Messages for you while building, not for the player.", "Debugging"},
};

inline int VsPaletteCategoryCount() { return (int)(sizeof kPaletteCategories / sizeof kPaletteCategories[0]); }

inline int VsPaletteCategoryIndex(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < VsPaletteCategoryCount(); ++i)
        if (std::strcmp(kPaletteCategories[i].name, name) == 0) return i;
    return -1;
}

// The sidebar groups topics under a muted heading. Returns the section index (0 for the first heading), or -1.
inline int VsPaletteSectionIndex(int categoryIndex) {
    if (categoryIndex < 0 || categoryIndex >= VsPaletteCategoryCount()) return -1;
    int section = -1;
    const char* last = nullptr;
    for (int i = 0; i <= categoryIndex; ++i) {
        if (!last || std::strcmp(last, kPaletteCategories[i].section) != 0) { ++section; last = kPaletteCategories[i].section; }
    }
    return section;
}

// A subcategory: a smaller heading inside one topic. Order here is the order in the sidebar and in the list.
// Names are unique across all topics, and none is also a topic name.
struct VsPaletteSub {
    const char* category;
    const char* name;
    const char* blurb;
};
inline const VsPaletteSub kPaletteSubs[] = {
    // Quick blocks
    {"Quick blocks", "Move Around", "Move with keys, jump, follow, walk back and forth."},
    {"Quick blocks", "Game Rules", "Score, health, winning, losing and checkpoints."},
    {"Quick blocks", "Objects In The Game", "Coins, doors, platforms, bullets and things that spin or appear."},
    {"Quick blocks", "Show And Sound", "Messages, sounds, colours and the camera."},
    {"Quick blocks", "Do In Order", "Wait, repeat, do forever, and do something only when a key is pressed or touched."},
    // Flow
    {"Branches & Gates", "State Machines", "Keep one state at a time and react when it changes."},
    {"Waiting & Timers", "Delayed Calls", "Call something later, or every few seconds, from a script."},
    {"Time & Game Speed", "Clock", "What time it is, and how long the last frame took."},
    {"Time & Game Speed", "Game Speed", "Slow motion, fast forward and hit stops."},
    {"Time & Game Speed", "Pausing", "Pause the game and ask who paused it."},
    {"Time & Game Speed", "Time Math", "Turn time into portions, beats and blinks."},
    // Data
    {"Variables", "Change a Variable", "Add to, multiply, clamp, round or randomize a variable in place."},
    {"Variables", "Check a Variable", "Is a variable equal, above, below, even or near something?"},
    {"Variables", "Shared Variables", "Variables the whole scene or the whole game can read."},
    {"Saving & Loading", "Saved Values", "Named values kept between runs."},
    {"Saving & Loading", "Save Slots", "Whole-game saves: save, load, continue and delete a slot."},
    {"Saving & Loading", "Save Files", "Read, write and check save files and their dates."},
    {"Lists", "Sorting & Searching", "Sort, reverse, shuffle, find, filter and join lists."},
    {"Lists", "Array Functions", "The same list jobs written as text-script function calls."},
    // Math
    {"Numbers", "Rounding & Snapping", "Round down, round up, snap to a step, keep the fraction."},
    {"Numbers", "Limits & Wrapping", "Clamp, wrap, ping-pong and keep a number inside a range."},
    {"Numbers", "More Arithmetic", "Squares, cubes, logs, differences, reciprocals and picking the larger."},
    {"Numbers", "Averages & Percentages", "Averages, running totals, peaks, percentages and spreads."},
    {"Numbers", "Bits", "Bit operations on whole numbers."},
    {"Numbers", "Types & Checks", "Is it a number, a word, a list? Is it finite?"},
    {"Numbers", "Script Math Functions", "The same jobs written as text-script function calls, like Mathf.Abs, min or pow."},
    {"Blending & Easing", "Easing & Curves", "Ease in and out, curves, waves and noise."},
    {"Blending & Easing", "Smoothing & Springs", "Move a value toward a target smoothly, with damping and springs."},
    {"Angles & Trigonometry", "Angle Helpers", "Shortest angle, wrap, snap, blend and direction from an angle."},
    {"Vectors", "Directions & Axes", "Forward, right, up, zero and one."},
    {"Vectors", "Flat Vectors", "Vector math that ignores height: ground distance, sideways offsets, reflections."},
    {"Vectors", "Script Vector Functions", "The same jobs written as text-script function calls, like Vector3.Dot or dist."},
    {"Comparisons & Logic", "True & False Logic", "And, or, not, and picking one of several values."},
    {"Comparisons & Logic", "Scores & Number Sets", "Compare scores, and ask about a whole set of numbers."},
    // Text
    {"Text", "Search & Replace", "Does it contain, start or end with something? Find and replace."},
    {"Text", "Pieces & Words", "Cut a text into pieces, words and letters, and measure it."},
    {"Text", "Clean Up & Padding", "Trim, pad, change the case, reverse and repeat."},
    {"Text", "Compare & Check", "Is it blank, the same, or longer?"},
    {"Text", "Numbers & Text", "Turn numbers into text and text into numbers."},
    {"Text", "Clipboard", "Copy text to the clipboard and paste it back."},
    // Objects
    {"Create & Destroy", "Spawners & Pools", "Spawn on a timer, keep a pool of objects to reuse, and spawn waves."},
    {"Find Objects", "Names & Tags", "Read and change the name and the tag of an object."},
    {"Parents & Components", "Components & Properties", "Add or get a component, and read or set one of its properties by name."},
    {"Color & Appearance", "Sprites & Textures", "Sprite images, textures, flipping and tiles."},
    {"Color & Appearance", "Materials & Meshes", "Meshes, emissive glow, shading and texture tiling."},
    // Movement & physics
    {"Position", "Read a Position", "Where is it? The x, y and z of this object or another one."},
    {"Position", "Stay in Bounds", "Keep an object inside a box, wrap it around, snap it to a grid or limit its height."},
    {"Rotation & Scale", "Facing & Looking", "Face a direction or an object, and look at a point."},
    {"Rotation & Scale", "Scale", "Read and change the size of an object."},
    {"Move & Follow", "Motion Patterns", "Back and forth, in a circle, a hop, a shake, bob and spin."},
    {"Move & Follow", "Script Movement Functions", "The same jobs written as text-script function calls, like move to, chase or wasd move."},
    {"Tweens", "Start a Tween", "Begin moving, turning, scaling or fading something over time."},
    {"Tweens", "Control Tweens", "Pause, play, restart, rewind or kill tweens."},
    {"Tweens", "Tween Options", "Easing, loops, relative values, speed and other settings."},
    {"Tweens", "Tween Events", "Run something when a tween starts, updates or ends."},
    {"Tweens", "Tween Sequences", "Chain tweens one after another, or run them together."},
    {"Forces & Velocity", "Speed & Jumping", "Set or read the speed, jump, fall and stop."},
    {"Forces & Velocity", "Body Settings", "Mass, gravity, drag, bounce, friction and what the body may do."},
    {"Raycasts & Overlaps", "Ray Results", "What the last ray hit: the point, the distance, the surface direction."},
    {"Raycasts & Overlaps", "Overlaps & Shape Casts", "Is anything inside a box, circle or capsule? Sweep a shape along a line."},
    // Gameplay
    {"Health & Damage", "Health Values", "Read and set health, ask if it is low or full, heal to full, kill."},
    {"Health & Damage", "Armor & Lives", "Armor, damage resistance, lives, respawning and reviving."},
    {"Health & Damage", "Resources & Buffs", "Damage and healing over time, regeneration, spending a resource, timed buffs."},
    {"Inventory & Items", "Inventory Slots", "Slots: add, remove, move, count, select and name items."},
    {"Inventory & Items", "Equipment", "Put items on and take them off."},
    {"Inventory & Items", "Crafting & Loot", "Craft recipes, roll loot and drop pickups."},
    {"Inventory & Items", "Carry Limits", "How much can be carried, stacked and how it slows you."},
    {"Inventory & Items", "Pick Up & Drop", "Pick up, hold, throw and drop things."},
    {"Quests & Dialogue", "Quests", "Start, advance, complete and fail quests and their objectives."},
    {"Quests & Dialogue", "Dialogue", "Start a conversation, read the line, offer choices and end it."},
    {"Quests & Dialogue", "Story Flags", "Yes/no flags and values that remember the story so far."},
    // Characters & AI
    {"Animation", "Animator Parameters", "Set and read the numbers, switches and triggers of an animator."},
    {"Animation", "Blend Shapes & Trees", "Blend shapes (face morphs) and blend trees."},
    {"Character Actions", "Stances & Ragdoll", "Stand, crouch, go prone, and let the body go limp."},
    {"Navigation", "Grid Paths", "Walk across a tile grid with costs and obstacles."},
    // Sound, camera and screen
    {"Camera", "Follow & Switch", "Pick the camera, make it follow something and move it."},
    {"Camera", "Zoom & Shake", "Zoom, field of view, tilt and shake."},
    {"Screen Text", "Text Style & Format", "Color, size, opacity, number formats and typing out."},
    {"Screen Text", "Numbers in Text", "Show a number, the health or the session time in a text."},
    {"Screen Text", "Script Text Widgets", "Text drawn from a script each frame: text, heading, caption, badge."},
    {"Buttons & Inputs", "Sliders & Toggles", "Sliders, checkboxes, switches and radio buttons."},
    {"Buttons & Inputs", "Dropdowns & Text Fields", "Dropdown lists and text boxes."},
    {"Buttons & Inputs", "Pickers & Search", "Color, date, time and file pickers, and search bars."},
    {"Bars & Layout", "Progress Bars", "Progress bars, radial bars, spinners and loading skeletons."},
    {"Bars & Layout", "Tabs & Pages", "Tabs, pages, accordions and breadcrumbs."},
    {"Bars & Layout", "Scroll Areas & Grids", "Scroll views and slot grids."},
    {"Bars & Layout", "Layout Helpers", "Begin and end a panel or card, put things on one line, separators and list rows."},
    {"Bars & Layout", "Themes", "Apply a theme or a style to the screen."},
    {"Popups & Dialogs", "Toasts & Tooltips", "Small messages and hover tips."},
    {"Popups & Dialogs", "Modals & Sheets", "Modal windows with a title and buttons, and bottom sheets."},
    // Input
    {"Mouse & Touch", "Touch & Gestures", "Touch screens, swipes, pinches and twists."},
    {"Gamepad & Controls", "Named Actions", "Define an action, read its value, rebind its key and rumble the pad."},
    // Online
    {"Multiplayer", "Hosting & Joining", "Host, join, leave and ask who is the server."},
    {"Multiplayer", "Server Browser", "List servers and join one."},
    {"Multiplayer", "Matchmaking & Relay", "Find a match, queue, and play through a relay."},
    {"Multiplayer", "Messages & RPC", "Send messages and call functions on other players."},
    {"Multiplayer", "Synced Objects & Variables", "Objects and values that every player sees."},
    {"Multiplayer", "Players & Matches", "Who is connected, who is ready, start and end a match, scores."},
    {"Steam", "Achievements & Stats", "Unlock achievements and read or change stats."},
    {"Steam", "Lobbies & Friends", "Create or join a lobby, invite friends, show presence."},
    {"Steam", "Cloud & Workshop", "Steam cloud files and Workshop items."},
    {"Accounts & Online Saves", "Accounts", "Register, log in and read the signed-in user."},
    {"Accounts & Online Saves", "Cloud Saves", "Save and load data on the server."},
    {"Accounts & Online Saves", "Leaderboards & PlayFab", "Submit scores, read the top list, and PlayFab stats."},
};
inline int VsPaletteSubCount() { return (int)(sizeof kPaletteSubs / sizeof kPaletteSubs[0]); }

inline int VsPaletteSubIndex(const char* category, const char* sub) {
    if (!category || !sub || !sub[0]) return -1;
    for (int i = 0; i < VsPaletteSubCount(); ++i)
        if (std::strcmp(kPaletteSubs[i].category, category) == 0 && std::strcmp(kPaletteSubs[i].name, sub) == 0) return i;
    return -1;
}

// The hover text of a subcategory, by its (unique) name. Null when there is none.
inline const char* VsPaletteSubBlurb(const char* sub) {
    if (!sub || !sub[0]) return nullptr;
    for (int i = 0; i < VsPaletteSubCount(); ++i)
        if (std::strcmp(kPaletteSubs[i].name, sub) == 0) return kPaletteSubs[i].blurb;
    return nullptr;
}

// Vague buckets that must not appear as a topic, a subcategory or a section.
inline bool VsCatchAllCategory(const char* name) {
    if (!name || !name[0]) return false;
    static const char* k[] = {
        "Do something", "Check something", "Do", "Check", "Actions", "Data", "Engine API",
        "Misc", "Other", "General", "Basic", "Look", "World", "Stuff", "Various", "Uncategorized",
        "Advanced", "Extra", "Extras", "More", "Utilities", "Utility", "Helpers", "Tools and more",
        "Miscellaneous", "Everything else", "Rest", "Remaining",
    };
    for (const char* s : k) if (std::strcmp(name, s) == 0) return true;
    return false;
}

// Lower ranks sort first. An empty subcategory is the common part of a topic and always comes first.
// A subcategory name is unique across topics, so the name alone gives its place in kPaletteSubs.
inline int VsSubOrder(const char* sub) {
    if (!sub || !sub[0]) return -1;
    for (int i = 0; i < VsPaletteSubCount(); ++i)
        if (std::strcmp(kPaletteSubs[i].name, sub) == 0) return i;
    return 1000;
}

// ------------------------------------------------------------------------------------------------------------
// Where a node goes. The three kinds of palette row each have a lookup; all of them are a table of rules first
// (VisualScriptPlaceRules.hpp) and, for a name the rules do not mention, the OLD place translated to a new one.

inline VsPlace VsPlaceForBuiltin(const char* name);   // a text-script function (Fn.print), without the "Fn."

// Hand-written nodes, library nodes (Lb...), gameplay-library nodes (Gl...) and text-script builtins (Fn....).
// A type no rule covers comes back with a null category.
inline VsPlace VsPlaceForNode(const char* type) {
    if (!type || !type[0]) return {};
    if (const okay::vs::QuickBlock* qb = okay::vs::FindQuickBlock(type)) {
        const std::string g = qb->group;
        return VsAt("Quick blocks", g == "Movement" ? "Move Around" : g == "Game rules" ? "Game Rules"
                                    : g == "World" ? "Objects In The Game" : g == "Feedback" ? "Show And Sound" : "Do In Order");
    }
    if (VsPre(type, "Fn.")) return VsPlaceForBuiltin(type + 3);
    if (const VsRule* r = VsFindRule(kNodeRules, VsRuleCount(kNodeRules), type)) return VsAt(r->category, r->sub);
    return VsTranslateLegacy(VsLegacyPlaceForNode(type));
}

inline VsPlace VsPlaceForBuiltin(const char* name) {
    if (!name || !name[0]) return {};
    if (const VsRule* r = VsFindRule(kBuiltinRules, VsRuleCount(kBuiltinRules), name)) return VsAt(r->category, r->sub);
    return VsTranslateLegacy(VsLegacyPlaceForBuiltin(name));
}

// An engine-API node: `apiType` is the script type (Transform), `member` the property or method (Translate).
inline VsPlace VsPlaceForApi(const char* apiType, const char* member) {
    if (!apiType) return {};
    if (!member) member = "";
    const std::string key = std::string(apiType) + "." + member;
    if (const VsRule* r = VsFindRule(kApiRules, VsRuleCount(kApiRules), key.c_str())) return VsAt(r->category, r->sub);
    return VsTranslateLegacy(VsLegacyPlaceForApi(apiType, member));
}

// One Actions step (the old Do / Check lists). `group` is the old bucket in main.cpp.
inline VsPlace VsPlaceForStep(const char* op, const char* group) {
    if (!op) op = "";
    if (!group) group = "";
    VsPlace p;
    if (const VsRule* r = VsFindRule(kStepRules, VsRuleCount(kStepRules), op)) p = VsAt(r->category, r->sub);
    else p = VsTranslateLegacy(VsLegacyPlaceForStep(op, group));
    if (p.category && VsStepSameAsNode(op)) p.browse = false;
    return p;
}

// ------------------------------------------------------------------------------------------------------------
// People who remember where a node used to be can still search for the old topic: the old place becomes search words.
// Empty when the node did not move (the topic words are searched anyway).
inline std::string VsMovedFromWords(const VsPlace& was, const VsPlace& now) {
    if (!was.category || !now.category) return "";
    const bool sameTopic = std::strcmp(was.category, now.category) == 0;
    const bool sameSub = std::strcmp(was.subcategory, now.subcategory) == 0;
    if (sameTopic && sameSub) return "";
    std::string s = was.category;
    if (was.subcategory[0]) { s += ' '; s += was.subcategory; }
    return s;
}

// The name a favourite or a recently used node is remembered under in okay_vsnodes.txt ("fav n:OnKey").
// It is built from the node's type (or an Actions step's short name) and never from its topic, so moving a node to
// another topic cannot lose anyone's favourites. tests/test_vs_categories.cpp checks this.
inline std::string VsPaletteNodeKey(const char* type) { return std::string("n:") + (type ? type : ""); }
inline std::string VsPaletteStepKey(bool condition, const char* op) {
    return std::string(condition ? "c:" : "a:") + (op ? op : "");
}

// The old place of a palette row, for VsMovedFromWords.
inline VsPlace VsOldPlaceForNode(const char* type) { return VsLegacyPlaceForNode(type); }
inline VsPlace VsOldPlaceForApi(const char* apiType, const char* member) { return VsLegacyPlaceForApi(apiType, member); }
inline VsPlace VsOldPlaceForStep(const char* op, const char* group) { return VsLegacyPlaceForStep(op, group); }

} // namespace okay::editor::vsed
