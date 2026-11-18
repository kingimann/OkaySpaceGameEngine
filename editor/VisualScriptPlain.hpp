#pragma once
// Plain words for node names and one-line descriptions ("imagine a 10 year old was using it").
// VsPlainText swaps programming words for everyday ones (see VisualScriptGlossary.hpp for the banned list);
// VsPlainTitleFor holds hand-picked names for the nodes whose swapped name would still read badly.
// Written for this project from its own glossary (docs/visual_script_category_audit.md, "Glossary").
#include "VisualScriptGlossary.hpp"

#include <cstring>
#include <string>
#include <unordered_map>

namespace okay::editor::vsed {

struct VsPlainWord { const char* word; const char* plain; };
// One row per word (singular and plural are separate rows). The right side must not be a banned word.
inline const VsPlainWord kVsPlainWords[] = {
    {"variable", "saved value"}, {"variables", "saved values"},
    {"boolean", "true-or-false value"}, {"bool", "true-or-false value"},
    {"float", "number"}, {"floats", "numbers"}, {"integer", "whole number"}, {"int", "whole number"},
    {"vector", "direction"}, {"vectors", "directions"}, {"vec", "direction"}, {"vec2", "flat direction"},
    {"vec3", "position"}, {"instance", "copy"}, {"callback", "reply"}, {"callbacks", "replies"},
    {"handler", "helper"}, {"parameter", "setting"}, {"parameters", "settings"},
    {"argument", "input"}, {"arguments", "inputs"}, {"iterate", "go through"}, {"iteration", "round"},
    {"index", "place number"}, {"indexes", "place numbers"}, {"string", "text"}, {"strings", "texts"},
    {"array", "list"}, {"arrays", "lists"}, {"function", "named action"}, {"functions", "named actions"},
    {"node", "block"}, {"nodes", "blocks"}, {"euler", "angle"}, {"quaternion", "turn"}, {"quat", "turn"},
    {"gameobject", "object"}, {"prefab", "saved object"}, {"prefabs", "saved objects"},
    {"transform", "position and size"}, {"transforms", "positions and sizes"},
    {"rigidbody", "physics body"}, {"rigidbodys", "physics bodies"}, {"api", "engine"},
    {"enum", "choice list"}, {"struct", "bundle"}, {"structs", "bundles"}, {"null", "nothing"},
    {"void", "empty"}, {"const", "fixed value"}, {"constant", "fixed value"}, {"casting", "sweeping"},
    {"cast", "sweep"}, {"casts", "sweeps"}, {"serialize", "save as text"}, {"json", "data text"},
    {"dt", "frame time"}, {"obj", "object"}, {"pos", "position"}, {"rot", "turn"}, {"idx", "place number"},
    {"fps", "frames per second"}, {"fov", "zoom"}, {"rpc", "remote call"}, {"lerp", "blend"},
    {"raycast", "line check"}, {"raycasts", "line checks"}, {"scalar", "number"}, {"tuple", "pair"},
    {"dictionary", "word list"}, {"hash", "code"}, {"mutex", "lock"}, {"thread", "worker"},
    {"loop", "repeat"}, {"loops", "repeats"},
    {"var", "saved value"}, {"vars", "saved values"}, {"anim", "animation"}, {"num", "number"}, {"id", "name"},
    {"ids", "names"}, {"nav", "path"}, {"ik", "limb aim"}, {"min", "smallest"}, {"max", "largest"}, {"deg", "degrees"},
    {"dist", "distance"},
};

inline std::string VsPlainText(const std::string& in, bool titleCase = false) {
    std::string out;
    std::size_t i = 0;
    bool sentenceStart = true;
    while (i < in.size()) {
        if (!std::isalnum((unsigned char)in[i])) {
            if (in[i] == '.' || in[i] == '!' || in[i] == '?') sentenceStart = true;
            out += in[i++];
            continue;
        }
        std::size_t j = i;
        while (j < in.size() && std::isalnum((unsigned char)in[j])) ++j;
        const std::string word = in.substr(i, j - i);
        std::string low;
        for (char c : word) low += (char)std::tolower((unsigned char)c);
        const char* plain = nullptr;
        for (const VsPlainWord& w : kVsPlainWords) if (low == w.word) { plain = w.plain; break; }
        // A mixed-case token such as "OnVarChanged" is not a word; leave it. Plain words only.
        if (plain) {
            std::string r = plain;
            const bool upper = std::isupper((unsigned char)word[0]) != 0;
            if (titleCase) {
                bool start = true;
                for (char& c : r) { if (start && std::isalpha((unsigned char)c)) c = (char)std::toupper((unsigned char)c); start = (c == ' ' || c == '-'); }
            } else if (upper) r[0] = (char)std::toupper((unsigned char)r[0]);
            out += r;
        } else out += word;
        sentenceStart = false;
        i = j;
    }
    (void)sentenceStart;
    return out;
}

/// A plain title for a node whose title still made a beginner stop. `oldTitle` stays a search word and is shown in
/// the help card ("also called ..."); the type (the name written into a saved graph) is not changed, so every
/// saved graph loads exactly as before.
struct VsPlainRename { const char* type; const char* title; const char* oldTitle; };
inline const VsPlainRename kVsPlainRenames[] = {
    {"Timer",    "Repeat Every Few Seconds", "Every Seconds"},
    {"Wait",     "Wait The First Time Only", "Wait Once"},
    {"Delay",    "Wait, Then Go On",         "Wait Seconds"},
    {"Cooldown", "Limit How Often",         "Cooldown"},
    {"FlipFlop", "Switch Between Two Paths", "Flip Flop"},
    {"Gate",     "Open Or Close The Way",    "Gate"},
    {"Sequence", "Do These In Order",        "Do in Order"},
    {"Const",    "Type In A Value",          "Fixed Value"},
};

inline const VsPlainRename* VsPlainRenameFor(const char* type) {
    if (!type) return nullptr;
    for (const VsPlainRename& r : kVsPlainRenames) if (std::strcmp(r.type, type) == 0) return &r;
    return nullptr;
}

// Hand-picked display names. Anything not here that still has a banned word goes through VsPlainText.
inline const char* VsPlainTitleFor(const char* type) {
    if (const VsPlainRename* r = VsPlainRenameFor(type)) return r->title;
    static const std::unordered_map<std::string, const char*> t = {
        // The Starter blocks: the names a beginner searches for.
        {"Translate", "Move Object"}, {"Rotate", "Turn Object"}, {"Print", "Say Text"}, {"SetText", "Show Text"},
        {"Spawn", "Spawn Copy Of"}, {"Destroy", "Remove Object"}, {"SetActive", "Show Or Hide Object"},
        {"Key", "Is Key Held Down"}, {"ShowUI", "Show Screen Item"}, {"DeltaTime", "Get Frame Time"},
        {"ScoreAdd", "Change Score by"}, {"Compare", "Compare Numbers"}, {"RandomRange", "Random Number Between"},
        {"Add", "Add Numbers"}, {"Sub", "Subtract Numbers"}, {"Mul", "Multiply Numbers"}, {"Div", "Divide Numbers"},
        {"Const", "Fixed Value"}, {"Lerp", "Blend Between"}, {"InverseLerp", "How Far Between"},
        {"MakeVec3", "Make Position"}, {"VecX", "Get Position Left-Right"}, {"VecY", "Get Position Up-Down"},
        {"VecZ", "Get Position Forward-Back"}, {"Vec3Add", "Add Positions"}, {"Vec3Sub", "Subtract Positions"},
        {"Vec3Scale", "Stretch Position"}, {"Vec3Length", "Get Distance From Zero"},
        {"Vec3Normalize", "Make Length One"}, {"Vec3Dot", "Compare Directions"}, {"Vec3Distance", "Get Distance Between"},
        {"Vec3Cross", "Get Sideways Direction"}, {"Vec3Lerp", "Blend Positions"}, {"Vec3Negate", "Flip Direction"},
        {"AnimSetFloat", "Set Animation Number"}, {"AnimSetBool", "Set Animation Switch"},
        {"SetFov", "Set Camera View Angle"}, {"PickByIndex", "Pick From List by Place"}, {"IndexOf", "Find Place In List"},
        {"LbLoopTime", "Repeat Over Time"}, {"ForLoop", "Repeat Counting"}, {"RandomInt", "Random Whole Number"},
        {"ForwardVector", "Get Forward Direction"}, {"RightVector", "Get Right Direction"}, {"UpVector", "Get Up Direction"},
        {"SetPositionVec", "Set Position From Direction"}, {"MoveBy", "Move By Position"},
        {"Raycast", "Look In A Line 2D"}, {"Raycast3D", "Look In A Line 3D"}, {"RaycastAll3D", "Look Through Everything In A Line"},
        {"SphereCast3D", "Sweep A Ball"}, {"BoxCast2D", "Sweep A Box"}, {"CapsuleCast2D", "Sweep A Capsule"},
        {"NetRpc", "Call Players"}, {"NetObjRpc", "Call This Object On Players"}, {"OnNetRpc", "When Player Calls This Object"},
        {"RejectRpc", "Refuse Call"}, {"FunctionStart", "Define Named Action"}, {"CallFunction", "Run Named Action"},
        {"Break", "Stop Repeating"}, {"Continue", "Next Repeat"}, {"StructNew", "Make New Bundle"},
        {"ToFixed", "Round To Decimals"}, {"GlFormatNumber", "Show Number Nicely"}, {"NetSend", "Send To Players"},
        {"GlSetVelocity", "Set Exact Speed"},
        {"Fn.Quaternion.Euler", "Turn From Angles"}, {"Fn.String.IsNullOrEmpty", "Is Text Empty"},
        {"Fn.string.IsNullOrWhiteSpace", "Is Text Blank"},
    };
    auto it = t.find(type ? type : "");
    return it == t.end() ? nullptr : it->second;
}

} // namespace okay::editor::vsed
