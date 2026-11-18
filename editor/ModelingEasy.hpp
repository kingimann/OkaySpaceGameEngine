#pragma once
// ModelingEasy: the beginner layer of the Modeling window as plain data and small pure functions (no ImGui, no engine).
// What is here: the tool catalogue (plain names, one-sentence tips, an example, the tab, what must be selected first),
// the "no jargon" word check, the "What do I click next?" hint line, the quick actions shown next to a selection, the
// last-used tools strip and the click-count table for eight beginner jobs. The window code only draws what this says.
// Names are labels only: saved models and scenes never store them. Written from scratch for this engine.
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace oked::easymodel {

// ---- Tabs -------------------------------------------------------------------------------------------------------------
enum class Tab { Shape, Reshape, Details, Colour, Finish, Pick };   // Pick = the buttons of the "Pick parts" row, not a tab
constexpr int kBeginnerTabCount = 5;   // a beginner never sees more than five (every other tool is a page in the list on the left of the window)
inline const char* TabName(Tab t) {
    switch (t) {
        case Tab::Shape: return "Shape";   case Tab::Reshape: return "Reshape"; case Tab::Details: return "Details";
        case Tab::Colour: return "Colour"; case Tab::Finish: return "Finish";   default: return "Pick";
    }
}
inline const char* TabHelp(Tab t) {
    switch (t) {
        case Tab::Shape:   return "Start with a ready-made model, or add a ball, box, tube, cone, plane or ring.";
        case Tab::Reshape: return "Move it, make it bigger, turn it, squash it, smooth it or copy it to the other side.";
        case Tab::Details: return "Cut it in half, add detail, round the edges, make it hollow, push faces out.";
        case Tab::Colour:  return "Paint the whole model or just the faces you picked.";
        case Tab::Finish:  return "Fix problems, put it on the ground, give it a solid body and save it.";
        case Tab::Pick:    return "Pick the whole part, or everything that looks the same.";
        default:           return "";
    }
}

// ---- Tools ------------------------------------------------------------------------------------------------------------
// What has to be picked before a tool can run.
enum class Needs { Nothing, Model, Faces, Corners, Parts };   // Parts = any faces, edges or corners
struct Tool {
    const char* id;        // stable id the window code switches on (never saved)
    const char* name;      // verb first, one to three words
    const char* tip;       // one sentence
    const char* example;   // a tiny example
    Tab tab;
    Needs needs;
    const char* key;       // shortcut shown in the tooltip, or ""
};

inline const std::vector<Tool>& Tools() {
    static const std::vector<Tool> t = {
        // Shape
        {"add.ball",  "Add ball",  "Adds a round ball to the scene.",                    "Good for a head, a moon or a snowman.", Tab::Shape, Needs::Nothing, ""},
        {"add.box",   "Add box",   "Adds a square box to the scene.",                    "Good for a crate, a wall or a house.",  Tab::Shape, Needs::Nothing, ""},
        {"add.tube",  "Add tube",  "Adds a hollow pipe to the scene.",                   "Good for a chimney or a cup.",          Tab::Shape, Needs::Nothing, ""},
        {"add.cone",  "Add cone",  "Adds a pointy cone to the scene.",                   "Good for a party hat or a pine tree.",  Tab::Shape, Needs::Nothing, ""},
        {"add.plane", "Add plane", "Adds a flat sheet to the scene.",                    "Good for a floor or a pond.",           Tab::Shape, Needs::Nothing, ""},
        {"add.ring",  "Add ring",  "Adds a ring like a doughnut to the scene.",          "Good for a wheel or a bracelet.",       Tab::Shape, Needs::Nothing, ""},
        // Reshape
        {"move",      "Move",         "Slide the model or the parts you picked.",         "Lift a roof up.",                      Tab::Reshape, Needs::Model, "G"},
        {"bigger",    "Make bigger",  "Grow the model or the parts you picked.",          "Make a ball into a giant ball.",       Tab::Reshape, Needs::Model, "S"},
        {"turn",      "Turn",         "Spin the model or the parts you picked.",          "Tip a sign to the side.",              Tab::Reshape, Needs::Model, "R"},
        {"squash",    "Squash",       "Make the model flatter, like pressing it down.",   "Turn a ball into a pancake.",          Tab::Reshape, Needs::Model, ""},
        {"smooth",    "Smooth it",    "Evens out bumps without adding detail.",                 "Make a rock look like a pebble.",      Tab::Reshape, Needs::Model, ""},
        {"mirror",    "Copy to other side", "Makes a matching copy on the other side.",   "Draw one wing and get two.",           Tab::Reshape, Needs::Model, ""},
        // Details
        {"cut.half",  "Cut in half",  "Adds a cut across the middle of the model.",       "Add a row of faces for a belt.",       Tab::Details, Needs::Model, "Ctrl+R"},
        {"detail.add","Add detail",   "Splits every face into four small ones.",          "Gives you more faces to shape.",       Tab::Details, Needs::Model, ""},
        {"round.it",  "Round all edges","Softens every sharp edge and corner of the whole model.", "Make a box look like a soft pillow.",  Tab::Details, Needs::Model, ""},
        {"hollow",    "Make hollow",  "Gives the model thick walls with an empty inside.", "Turn a ball into a bowl shell.",       Tab::Details, Needs::Model, ""},
        {"push.out",  "Push out",     "Pulls the faces you picked out of the model.",     "Pull a box out of a wall.",            Tab::Details, Needs::Faces, "E"},
        {"make.hole", "Make hole",    "Cuts a hole through the faces you picked.",        "Cut a window in a wall.",              Tab::Details, Needs::Faces, ""},
        {"delete.faces","Delete faces","Removes the faces you picked.",                   "Take the lid off a box.",              Tab::Details, Needs::Faces, "X"},
        {"round.corners","Round picked corners","Rounds only the corners you picked, nothing else.", "Soften one corner of a table.",        Tab::Details, Needs::Corners, ""},
        // Colour
        {"paint.faces","Paint faces", "Colours only the faces you picked.",               "Paint one wall red.",                  Tab::Colour, Needs::Faces, ""},
        {"paint.all", "Paint all",    "Colours the whole model.",                         "Make the whole car blue.",             Tab::Colour, Needs::Model, ""},
        // Pick parts row
        {"select.part",    "Pick whole part", "Picks everything that is joined to what you picked.", "Click one wheel face, get the whole wheel.", Tab::Pick, Needs::Parts, "Double-click"},
        {"select.similar", "Pick similar",    "Picks every face that looks the same way as the ones you picked.", "Pick every wall that faces the front.", Tab::Pick, Needs::Faces, ""},
        // Finish
        {"fix.problems","Fix problems","Closes holes and joins loose corners, then says what it did.", "Fix a model that shows gaps.", Tab::Finish, Needs::Model, ""},
        {"on.ground", "Put on ground","Moves the model so it sits on the ground.",        "Stop a tree from floating.",           Tab::Finish, Needs::Model, ""},
        {"solid",     "Make solid",   "Gives the model a solid body so players cannot walk through it.", "Stop players walking through a wall.", Tab::Finish, Needs::Model, ""},
        {"save.asset","Save model",   "Saves the model as a file you can use again.",     "Use the same tree in many levels.",    Tab::Finish, Needs::Model, ""},
    };
    return t;
}
inline const Tool* FindTool(const std::string& id) {
    for (const Tool& t : Tools()) if (id == t.id) return &t;
    return nullptr;
}
inline std::vector<const Tool*> ToolsOnTab(Tab tab) {
    std::vector<const Tool*> v;
    for (const Tool& t : Tools()) if (t.tab == tab) v.push_back(&t);
    return v;
}
/// The tooltip text: the tip, the example and the shortcut.
inline std::string TooltipFor(const Tool& t) {
    std::string s = t.tip; s += "\nFor example: "; s += t.example;
    if (t.key[0]) { s += "\nShortcut: "; s += t.key; }
    return s;
}

// ---- Words a beginner should not meet ------------------------------------------------------------------------------------
inline const std::vector<std::string>& BannedWords() {
    static const std::vector<std::string> b = {"vertex", "vertices", "vert", "verts", "edge loop", "bevel", "extrude", "normals", "normal", "uv", "uvs",
        "topology", "subdivision", "subdivide", "manifold", "boolean", "modifier", "modifiers", "triangle", "triangles", "mesh", "polygon", "ngon", "n-gon"};
    return b;
}
/// The first banned word found in `text` (whole words, any case), or "".
inline std::string FindBannedWord(const std::string& text) {
    std::string low = text;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    auto isw = [](char c) { return std::isalnum((unsigned char)c) != 0; };
    for (const std::string& w : BannedWords()) {
        for (std::size_t p = low.find(w); p != std::string::npos; p = low.find(w, p + 1)) {
            const bool l = p == 0 || !isw(low[p - 1]);
            const bool r = p + w.size() >= low.size() || !isw(low[p + w.size()]);
            if (l && r) return w;
        }
    }
    return "";
}

// ---- What is selected, and what can run ---------------------------------------------------------------------------------
struct EditState {
    bool haveObject = false;     // a model is selected in the scene
    bool editMode = false;       // Edit Mesh is on
    int  selMode = 2;            // 0 corners, 1 edges, 2 faces (the order of the 1 / 2 / 3 keys)
    int  selCount = 0;           // how many corners / edges / faces are picked
    bool modalActive = false;    // a G / R / S move is in progress
    bool wholeModelOnly = true;  // nothing is picked: tools work on the whole model
};
inline const char* SelName(int selMode, bool plural) {
    switch (selMode) { case 0: return plural ? "corners" : "corner"; case 1: return plural ? "edges" : "edge"; default: return plural ? "faces" : "face"; }
}
/// Empty when the tool can run now, otherwise the plain-words reason (shown on the greyed-out button).
inline std::string WhyNot(const Tool& t, const EditState& s) {
    const bool add = t.tab == Tab::Shape;
    if (add) return "";
    if (!s.haveObject) return "Pick a shape first: add one on the Shape tab, or click a model in the scene.";
    switch (t.needs) {
        case Needs::Faces:
            if (!s.editMode || s.selMode != 2 || s.selCount == 0) return "Select at least one face first: press 3, then click a face on the model.";
            break;
        case Needs::Corners:
            if (!s.editMode || s.selMode != 0 || s.selCount == 0) return "Select at least one corner first: press 1, then click a corner on the model.";
            break;
        case Needs::Parts:
            if (!s.editMode || s.selCount == 0) return "Select something first: press Tab, then click a part of the model.";
            break;
        default: break;
    }
    return "";
}

// ---- "What do I click next?" --------------------------------------------------------------------------------------------
inline std::string NextHint(const EditState& s) {
    if (!s.haveObject) return "Pick a shape to start";
    if (s.modalActive) return "Move the mouse, then click to finish. Press Esc to cancel";
    if (!s.editMode) return "Click a tool, or press Tab to pick parts of the model";
    if (s.selCount == 0) return std::string("Click a ") + SelName(s.selMode, false) + " to select it. Keys 1, 2, 3 change what you pick";
    if (s.selCount == 1) return std::string("Drag the arrows to move it, or use the quick buttons. Press Tab to leave edit mode");
    return std::string("Drag the arrows to move all ") + SelName(s.selMode, true) + ", or use the quick buttons. Press Tab to leave edit mode";
}

// ---- Quick actions: the six most useful things for what is selected -------------------------------------------------------
// O(1): it reads three numbers, never the mesh. The caller draws at most six buttons.
inline std::vector<std::string> QuickActions(const EditState& s) {
    if (!s.haveObject) return {};
    if (s.editMode && s.selCount > 0) {
        if (s.selMode == 2) return {"push.out", "make.hole", "delete.faces", "paint.faces", "move", "bigger"};
        if (s.selMode == 0) return {"move", "round.corners", "bigger", "turn"};
        return {"move", "bigger", "turn", "round.it"};
    }
    return {"bigger", "smooth", "mirror", "round.it", "hollow", "on.ground"};
}

/// Quick actions without the buttons the open tab already shows (so nothing appears twice on screen).
inline std::vector<std::string> QuickActionsFor(const EditState& s, int openTab) {
    std::vector<std::string> out;
    for (const std::string& id : QuickActions(s)) { const Tool* t = FindTool(id); if (t && (int)t->tab != openTab) out.push_back(id); }
    return out;
}
/// The "used lately" strip without anything the quick row or the open tab already shows.
inline std::vector<std::string> RecentShown(const std::vector<std::string>& recent, const std::vector<std::string>& quick, int openTab) {
    std::vector<std::string> out;
    for (const std::string& id : recent) {
        const Tool* t = FindTool(id);
        if (t && (int)t->tab != openTab && std::find(quick.begin(), quick.end(), id) == quick.end()) out.push_back(id);
    }
    return out;
}

// ---- Last used tools (the strip of four) --------------------------------------------------------------------------------
constexpr std::size_t kRecentMax = 4;
inline void NoteRecent(std::vector<std::string>& r, const std::string& id) {
    if (id.empty()) return;
    r.erase(std::remove(r.begin(), r.end(), id), r.end());
    r.insert(r.begin(), id);
    if (r.size() > kRecentMax) r.resize(kRecentMax);
}
/// The id the "Repeat" button runs: the newest tool that exists, or "".
inline std::string RepeatId(const std::vector<std::string>& r) {
    for (const std::string& id : r) if (FindTool(id)) return id;
    return "";
}
/// A number for a tool id that survives adding tools later (FNV-1a, kept positive), for the settings file. 0 = empty slot.
inline int StableCode(const std::string& id) {
    unsigned h = 2166136261u;
    for (char c : id) { h ^= (unsigned char)c; h *= 16777619u; }
    return (int)((h & 0x3FFFFFFFu) | 1u);
}
inline std::vector<int> RecentToCodes(const std::vector<std::string>& r) {
    std::vector<int> c(kRecentMax, 0);
    for (std::size_t i = 0; i < r.size() && i < kRecentMax; ++i) c[i] = StableCode(r[i]);
    return c;
}
inline std::vector<std::string> RecentFromCodes(const int* codes, std::size_t n) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < n && out.size() < kRecentMax; ++i) {
        if (codes[i] == 0) continue;
        for (const Tool& t : Tools()) if (StableCode(t.id) == codes[i] && std::find(out.begin(), out.end(), t.id) == out.end()) { out.push_back(t.id); break; }
    }
    return out;
}
inline std::string RecentToText(const std::vector<std::string>& r) {
    std::string s; for (const std::string& id : r) { if (!s.empty()) s += ','; s += id; } return s;
}
inline std::vector<std::string> RecentFromText(const std::string& text) {
    std::vector<std::string> out; std::stringstream in(text); std::string id;
    while (std::getline(in, id, ',')) if (FindTool(id) && std::find(out.begin(), out.end(), id) == out.end() && out.size() < kRecentMax) out.push_back(id);
    return out;
}

// ---- Friendly sizes ---------------------------------------------------------------------------------------------------
/// "1.0 wide, 2.0 tall, 1.0 deep" for the size line.
inline std::string SizeLine(float x, float y, float z) {
    char b[96]; std::snprintf(b, sizeof(b), "%.1f wide, %.1f tall, %.1f deep", x, y, z); return b;
}
/// The "Fix problems" report in plain words. Counts come from Mesh::Repair.
inline std::string FixReport(int holesBefore, int holesAfter, int loosePointsJoined) {
    if (holesBefore == 0 && loosePointsJoined == 0) return "No problems found. The model is closed and tidy.";
    std::string s = "Fixed: ";
    if (holesBefore > holesAfter) s += std::to_string(holesBefore - holesAfter) + " gap" + (holesBefore - holesAfter == 1 ? "" : "s") + " closed";
    if (loosePointsJoined > 0) { if (s.back() != ' ') s += ", "; s += std::to_string(loosePointsJoined) + " loose corner" + (loosePointsJoined == 1 ? "" : "s") + " joined"; }
    if (holesAfter > 0) s += ". " + std::to_string(holesAfter) + " gap" + (holesAfter == 1 ? "" : "s") + " left: select the faces around it and use Delete faces";
    return s;
}

// ---- Fewer clicks: eight beginner jobs, counted --------------------------------------------------------------------------
// Each job lists the clicks and key presses with the old Modeling window and with the new one. The lists were written by
// walking the window code step by step (not timed with a person). "After" steps name a tool id or a plain action.
struct Job { const char* name; std::vector<const char*> before; std::vector<const char*> after; };
inline const std::vector<Job>& Jobs() {
    static const std::vector<Job> j = {
        {"Box with a roof",
         {"Add shapes page", "Cube", "Fix the surface page", "Edit button", "Face mode", "Click top face", "Extrude", "Drag amount", "Scale top"},
         {"Shape tab: House starter"}},
        {"Round a corner",
         {"Fix the surface page", "Edit button", "Vertex mode", "Click corner", "Bevel Verts amount", "Bevel Verts button"},
         {"Pick corners", "Click corner", "round.corners"}},
        {"Add a window hole",
         {"Fix the surface page", "Edit button", "Face mode", "Click face", "Inset", "Delete faces", "Leave edit mode"},
         {"Pick faces", "Click face", "make.hole"}},
        {"Make a symmetric face",
         {"Bend and cut page", "Effects page", "Add Mirror", "Pick axis"},
         {"mirror"}},
        {"Stretch a shape",
         {"Bend and cut page", "Stretch axis", "Stretch amount"},
         {"squash"}},
        {"Add legs to a table",
         {"Add shapes page", "Cube", "Fix the surface page", "Edit button", "Face mode", "Click bottom face", "Inset", "Extrude", "Drag amount", "Leave edit mode"},
         {"Shape tab: Table starter"}},
        {"Smooth a rock",
         {"Add shapes page", "Rock", "Bend and cut page", "Subdiv + Smooth", "Smooth"},
         {"add.ball", "smooth"}},
        {"Paint two colours",
         {"Change the shape page", "Open colour picker", "Pick colour", "Fix the surface page", "Edit button", "Face mode", "Click faces", "Material Slots page", "Pick colour"},
         {"Pick faces", "Click faces", "paint.faces", "Pick colour"}},
    };
    return j;
}

// ---- Settings ---------------------------------------------------------------------------------------------------------
} // namespace oked::easymodel
