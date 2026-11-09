#pragma once
// Friendly names for visual-script nodes. The file token stays in the registry;
// this table is the title, the pins, and the old words search still matches.
// Rules: docs/visual_scripting_naming.md.
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace okay::editor::vsed {

enum class VsNameKind { Event, Action, Question, Reading, Calculation };

struct VsName {
    const char* type;
    VsNameKind  kind;
    const char* title;
    const char* oldTitle;   // previous title; also a search keyword
    const char* extra;      // more search keywords (old jargon, the token's words)
    const char* const* inPins;
    int inCount;
    const char* const* outPins;
    int outCount;
};

inline const char* const kPinValuePrevious[] = {"Value", "Previous"};
inline const char* const kPinCondition[] = {"Condition"};
inline const char* const kPinTimes[] = {"Times"};
inline const char* const kExecFirstSecond[] = {"First", "Second"};
inline const char* const kExecCases[] = {"Zero", "One", "Two", "Default"};

// Events. oldTitle is what the palette used to say.
inline const VsName kVsNames[] = {
    {"OnStart",          VsNameKind::Event, "On Start",              "On Start",              "", nullptr, 0, nullptr, 0},
    {"OnUpdate",         VsNameKind::Event, "On Every Frame",        "On Update",             "Update frame", nullptr, 0, nullptr, 0},
    {"OnLateUpdate",     VsNameKind::Event, "On Late Update",        "On Late Update",        "On After Every Frame late", nullptr, 0, nullptr, 0},
    {"OnFixedUpdate",    VsNameKind::Event, "On Physics Update",     "On Fixed Update",       "On Steady Tick fixed timestep physics", nullptr, 0, nullptr, 0},
    {"OnEnable",         VsNameKind::Event, "On Enabled",            "On Enable",             "On Becomes Active enabled active", nullptr, 0, nullptr, 0},
    {"OnDisable",        VsNameKind::Event, "On Disabled",           "On Disable",            "On Becomes Inactive disabled inactive", nullptr, 0, nullptr, 0},
    {"OnDestroy",        VsNameKind::Event, "On Destroyed",          "On Destroy",            "destroy", nullptr, 0, nullptr, 0},
    {"OnTimer",          VsNameKind::Event, "On Every Few Seconds",  "On Timer",              "timer seconds", nullptr, 0, nullptr, 0},
    {"OnKey",            VsNameKind::Event, "On Key Pressed",        "On Key Pressed",        "", nullptr, 0, nullptr, 0},
    {"OnKeyUp",          VsNameKind::Event, "On Key Released",       "On Key Released",       "", nullptr, 0, nullptr, 0},
    {"OnClick",          VsNameKind::Event, "On Click",              "On Click",              "", nullptr, 0, nullptr, 0},
    {"OnMouseDown",      VsNameKind::Event, "On Mouse Pressed",      "On Mouse Down",         "mousedown", nullptr, 0, nullptr, 0},
    {"OnMouseUp",        VsNameKind::Event, "On Mouse Released",     "On Mouse Up",           "mouseup", nullptr, 0, nullptr, 0},
    {"OnMouseEnter",     VsNameKind::Event, "On Mouse Entered",      "On Mouse Enter",        "", nullptr, 0, nullptr, 0},
    {"OnMouseExit",      VsNameKind::Event, "On Mouse Left",         "On Mouse Exit",         "exit", nullptr, 0, nullptr, 0},
    {"OnMouseOver",      VsNameKind::Event, "On Mouse Hover",        "On Mouse Over",         "hover over", nullptr, 0, nullptr, 0},
    {"OnCollision",      VsNameKind::Event, "On Collision Start",    "On Collision",          "On Bump hit start", nullptr, 0, nullptr, 0},
    {"OnCollisionStay",  VsNameKind::Event, "On Collision Stay",     "On Collision Stay",     "On Still Bumping bump stay", nullptr, 0, nullptr, 0},
    {"OnCollisionExit",  VsNameKind::Event, "On Collision End",      "On Collision Exit",     "On Bump Ended end exit", nullptr, 0, nullptr, 0},
    {"OnTrigger",        VsNameKind::Event, "On Trigger Enter",      "On Trigger",            "On Entered Area enter", nullptr, 0, nullptr, 0},
    {"OnTriggerStay",    VsNameKind::Event, "On Trigger Stay",       "On Trigger Stay",       "On Still Inside Area stay", nullptr, 0, nullptr, 0},
    {"OnTriggerExit",    VsNameKind::Event, "On Trigger Exit",       "On Trigger Exit",       "On Left Area exit", nullptr, 0, nullptr, 0},
    {"OnAnimEvent",      VsNameKind::Event, "On Animation Marker",   "On Anim Event",         "anim animation", nullptr, 0, nullptr, 0},
    {"OnEvent",          VsNameKind::Event, "On Message",            "On Custom Event",       "custom event", nullptr, 0, nullptr, 0},
    {"OnObjectPlaced",   VsNameKind::Event, "On Object Placed",      "On Object Placed",      "", nullptr, 0, nullptr, 0},
    {"OnObjectRemoved",  VsNameKind::Event, "On Object Removed",     "On Object Removed",     "", nullptr, 0, nullptr, 0},
    {"OnLogicSignal",    VsNameKind::Event, "On Signal",             "On Logic Signal",       "logic", nullptr, 0, nullptr, 0},
    {"OnItemAdded",      VsNameKind::Event, "On Item Added",         "On Item Added",         "", nullptr, 0, nullptr, 0},
    {"OnItemRemoved",    VsNameKind::Event, "On Item Removed",       "On Item Removed",       "", nullptr, 0, nullptr, 0},
    {"OnInventoryFull",  VsNameKind::Event, "On Inventory Full",     "On Inventory Full",     "", nullptr, 0, nullptr, 0},
    {"OnItemUsed",       VsNameKind::Event, "On Item Used",          "On Item Used",          "", nullptr, 0, nullptr, 0},
    {"OnEquip",          VsNameKind::Event, "On Item Equipped",      "On Equip",              "equip", nullptr, 0, nullptr, 0},
    {"OnCraft",          VsNameKind::Event, "On Item Crafted",       "On Craft",              "craft", nullptr, 0, nullptr, 0},
    {"OnSequenceEvent",  VsNameKind::Event, "On Cutscene Marker",    "On Sequence Event",     "sequence cutscene", nullptr, 0, nullptr, 0},
    {"OnDialogueEvent",  VsNameKind::Event, "On Conversation Line",  "On Dialogue Event",     "dialogue", nullptr, 0, nullptr, 0},
    {"OnDialogueEnd",    VsNameKind::Event, "On Conversation Ended", "On Dialogue End",       "dialogue", nullptr, 0, nullptr, 0},
    {"OnQuestStarted",   VsNameKind::Event, "On Quest Started",      "On Quest Started",      "", nullptr, 0, nullptr, 0},
    {"OnQuestCompleted", VsNameKind::Event, "On Quest Completed",    "On Quest Completed",    "", nullptr, 0, nullptr, 0},
    {"OnQuestFailed",    VsNameKind::Event, "On Quest Failed",       "On Quest Failed",       "", nullptr, 0, nullptr, 0},
    {"OnObjectiveCompleted", VsNameKind::Event, "On Objective Completed", "On Objective Completed", "", nullptr, 0, nullptr, 0},
    {"OnQuestEvent",     VsNameKind::Event, "On Quest Message",      "On Quest Event",        "", nullptr, 0, nullptr, 0},
    {"OnCheatDetected",  VsNameKind::Event, "On Cheat Detected",     "On Cheat Detected",     "", nullptr, 0, nullptr, 0},
    {"OnValidateRpc",    VsNameKind::Event, "On Network Action",     "On Validate Rpc",       "rpc validate", nullptr, 0, nullptr, 0},
    {"OnValidateHit",    VsNameKind::Event, "On Hit Check",          "On Validate Hit",       "validate hit", nullptr, 0, nullptr, 0},
    {"OnLanguageChanged",VsNameKind::Event, "On Language Changed",   "On Language Changed",   "", nullptr, 0, nullptr, 0},
    {"OnVarChanged",     VsNameKind::Event, "On Value Changed",      "On Var Changed",        "var variable", nullptr, 0, kPinValuePrevious, 2},
    {"OnSceneVarChanged",VsNameKind::Event, "On Scene Value Changed","On Scene Var Changed",  "var variable scene", nullptr, 0, kPinValuePrevious, 2},
    {"OnGlobalVarChanged", VsNameKind::Event, "On Game Value Changed", "On Global Var Changed", "var variable global", nullptr, 0, kPinValuePrevious, 2},
    // Flow. oldTitle is the previous palette title, including the unit in brackets.
    {"Branch",        VsNameKind::Action, "Branch",                 "Branch",            "", kPinCondition, 1, nullptr, 0},
    {"Sequence",      VsNameKind::Action, "Sequence",               "Sequence",          "", nullptr, 0, nullptr, 0},
    {"Once",          VsNameKind::Action, "Run Once",               "Once",              "", nullptr, 0, nullptr, 0},
    {"Timer",         VsNameKind::Action, "Every Seconds",          "Every (s)",         "Timer seconds", nullptr, 0, nullptr, 0},
    {"Wait",          VsNameKind::Action, "Wait Once",              "Wait (s)",          "Wait seconds", nullptr, 0, nullptr, 0},
    {"Delay",         VsNameKind::Action, "Wait Seconds",           "Delay (s)",         "Delay pause seconds", nullptr, 0, nullptr, 0},
    {"Cooldown",      VsNameKind::Action, "Cooldown",               "Cooldown (s)",      "seconds", nullptr, 0, nullptr, 0},
    {"ForLoop",       VsNameKind::Action, "For Loop",               "For Loop",          "", nullptr, 0, nullptr, 0},
    {"Repeat",        VsNameKind::Action, "Repeat",                 "Repeat N",          "count", nullptr, 0, nullptr, 0},
    {"FlipFlop",      VsNameKind::Action, "Flip Flop",              "Flip-Flop",         "A B toggle", nullptr, 0, nullptr, 0},
    {"Gate",          VsNameKind::Action, "Gate",                   "Gate",              "", nullptr, 0, nullptr, 0},
    {"DoN",           VsNameKind::Action, "Run a Number of Times", "Do N Times",        "DoN limit", nullptr, 0, nullptr, 0},
    {"SwitchInt",     VsNameKind::Action, "Switch Number",          "Switch (int)",      "switch int case", nullptr, 0, nullptr, 0},
    {"ChanceBranch",  VsNameKind::Action, "Random Branch",          "Random Branch",     "Chance", nullptr, 0, nullptr, 0},
    {"Counter",       VsNameKind::Action, "Counter",                "Counter",           "", nullptr, 0, nullptr, 0},
    {"EveryN",        VsNameKind::Action, "Every Number of Times",  "Every N Times",     "EveryN", kPinTimes, 1, nullptr, 0},
    {"FunctionStart", VsNameKind::Action, "Define Function",        "Function",          "", nullptr, 0, nullptr, 0},
    {"Return",        VsNameKind::Action, "Return",                 "Return",            "", nullptr, 0, nullptr, 0},
    {"CallFunction",  VsNameKind::Action, "Call Function",          "Call Function",     "", nullptr, 0, nullptr, 0},
    {"While",         VsNameKind::Action, "While",                  "While",             "", kPinCondition, 1, nullptr, 0},
    {"ForEach",       VsNameKind::Action, "For Each",               "For Each",          "", nullptr, 0, nullptr, 0},
    {"Break",         VsNameKind::Action, "Break Loop",             "Break",             "", nullptr, 0, nullptr, 0},
    {"Continue",      VsNameKind::Action, "Continue Loop",          "Continue",          "", nullptr, 0, nullptr, 0},
};

struct VsExecLabels { const char* type; const char* const* pins; int count; };
inline const VsExecLabels kVsExecLabels[] = {
    {"Sequence",  kExecFirstSecond, 2},
    {"FlipFlop",  kExecFirstSecond, 2},
    {"SwitchInt", kExecCases, 4},
};

inline const VsName* FindVsName(const char* type) {
    if (!type) return nullptr;
    for (const VsName& n : kVsNames) if (std::strcmp(n.type, type) == 0) return &n;
    return nullptr;
}

inline std::string VsSearchKeywords(const char* type) {
    const VsName* n = FindVsName(type);
    if (!n) return {};
    std::string s = n->oldTitle ? n->oldTitle : "";
    if (n->extra && n->extra[0]) { if (!s.empty()) s += ' '; s += n->extra; }
    return s;
}

// ---- The style check (docs/visual_scripting_naming.md) ----------------------
inline std::string VsLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline bool VsBannedWord(const std::string& lower) {
    static const char* k[] = {
        "anim", "api", "bool", "dt", "euler", "float", "fov", "fps", "gameobject", "id", "idx",
        "ik", "int", "max", "min", "nav", "num", "obj", "pos", "prefab",
        "old", "quat", "rad", "rigidbody", "rot", "rpc", "string", "transform",
        "ui", "var", "vec", "vec3",
    };
    for (const char* w : k) if (lower == w) return true;
    return false;
}

inline bool VsSmallWord(const std::string& lower) {
    static const char* k[] = {"a", "and", "at", "by", "for", "in", "is", "of", "on", "or", "the", "to", "up"};
    for (const char* w : k) if (lower == w) return true;
    return false;
}

inline bool VsMajorWord(const std::string& word) {
    if (word.size() < 3) return false;
    if (!std::isupper((unsigned char)word[0])) return false;
    for (std::size_t i = 1; i < word.size(); ++i)
        if (!std::islower((unsigned char)word[i])) return false;
    return true;
}

// Empty string means the title is fine. Otherwise a reason a person can read.
inline std::string LintVsTitle(const char* titleIn, VsNameKind kind) {
    if (!titleIn || !titleIn[0]) return "the name is empty";
    const std::string title = titleIn;
    if (title.front() == ' ' || title.back() == ' ') return "the name has a stray space";
    std::vector<std::string> words;
    std::string cur;
    for (char c : title) {
        if (c == ' ') {
            if (cur.empty()) return "the name has a double space";
            words.push_back(cur); cur.clear();
        } else cur += c;
    }
    if (cur.empty()) return "the name has a stray space";
    words.push_back(cur);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string& w = words[i];
        const std::string low = VsLower(w);
        if (VsBannedWord(low)) return "\"" + w + "\" is jargon or an abbreviation; say it in ordinary words";
        const bool small = VsSmallWord(low);
        bool capSmall = small && !w.empty() && std::isupper((unsigned char)w[0]);
        for (std::size_t k = 1; capSmall && k < w.size(); ++k)
            if (!std::islower((unsigned char)w[k])) capSmall = false;
        if (i == 0) {
            if (!(VsMajorWord(w) || capSmall))
                return "\"" + w + "\" should be a full word starting with a capital";
        } else if (small) {
            if (w != low) return "small word \"" + w + "\" stays lowercase";
        } else if (!VsMajorWord(w)) {
            return "\"" + w + "\" should be a full word (no symbols, digits, or abbreviations)";
        }
    }
    auto starts = [&](const char* p) { return title.rfind(p, 0) == 0; };
    if (kind == VsNameKind::Event && !starts("On ")) return "an event starts with \"On \"";
    if (kind == VsNameKind::Question && !(starts("Is ") || starts("Has "))) return "a yes-or-no value starts with \"Is \" or \"Has \"";
    if (kind == VsNameKind::Reading && !starts("Get ")) return "a reading starts with \"Get \"";
    if (kind == VsNameKind::Action && (starts("On ") || starts("Get ") || starts("Is ") || starts("Has ")))
        return "an action starts with a verb (Play, Wait, Move, Set), not On, Get, Is, or Has";
    return {};
}

inline std::string LintVsPin(const char* pin) {
    if (!pin || !pin[0]) return {};   // an unlabeled flow pin
    return LintVsTitle(pin, VsNameKind::Calculation);
}

} // namespace okay::editor::vsed
