#pragma once
// Named choices, required inputs, and which conversion is safe to insert.
// No ImGui: the editor draws these, and tests check the rules directly.
#include <string>
#include <utility>
#include <vector>

namespace okay::editor::vsed {

enum class VsChoiceKind {
    None, Clip, AnimState, AnimParam, Tag, Layer, Scene, Variable, Event, Sound
};

enum class VsReady { Ready, Missing, Error };

/// What a node's text field is a name of, or None when it is free text.
inline VsChoiceKind VsArgChoice(const std::string& type) {
    if (type == "PlayClip") return VsChoiceKind::Clip;
    if (type == "AnimTrigger" || type == "AnimSetFloat" || type == "AnimSetBool") return VsChoiceKind::AnimParam;
    if (type == "FindWithTag" || type == "CountWithTag" || type == "NearestWithTag" || type == "AllWithTag")
        return VsChoiceKind::Tag;
    if (type == "LoadScene") return VsChoiceKind::Scene;
    if (type == "GetVar" || type == "SetVar" || type == "AddVar" || type == "Toggle" || type == "HasVar" ||
        type == "OnVarChanged" || type == "GetSceneVar" || type == "SetSceneVar" || type == "AddSceneVar" ||
        type == "OnSceneVarChanged" || type == "GetGlobalVar" || type == "SetGlobalVar" || type == "AddGlobalVar" ||
        type == "OnGlobalVarChanged")
        return VsChoiceKind::Variable;
    if (type == "OnEvent" || type == "SendEvent" || type == "BroadcastEvent") return VsChoiceKind::Event;
    if (type == "MusicPlay") return VsChoiceKind::Sound;
    if (type == "Raycast" || type == "Raycast3D" || type == "OverlapBox" || type == "OverlapSphere" ||
        type == "OverlapBox3D" || type == "OverlapSphere3D" || type == "BoxCast2D" ||
        type == "CapsuleCast2D" || type == "OverlapBox2D" || type == "OverlapCapsule2D")
        return VsChoiceKind::Layer;
    return VsChoiceKind::None;
}

inline const char* VsChoiceKindName(VsChoiceKind k) {
    switch (k) {
        case VsChoiceKind::Clip: return "clip";
        case VsChoiceKind::AnimState: return "animator state";
        case VsChoiceKind::AnimParam: return "animator parameter";
        case VsChoiceKind::Tag: return "tag";
        case VsChoiceKind::Layer: return "layer";
        case VsChoiceKind::Scene: return "scene";
        case VsChoiceKind::Variable: return "variable";
        case VsChoiceKind::Event: return "event";
        case VsChoiceKind::Sound: return "sound";
        default: return "value";
    }
}

/// Lists the editor gathered. `onTarget` is what this object's animator (or the
/// equivalent) actually has. `fallback` is the rest of the project. Play Clip
/// shows the object's clips first and the project list only when the object
/// has none.
struct VsChoiceLists {
    std::vector<std::string> onTarget;
    std::vector<std::string> fallback;
};

inline void VsPushUnique(std::vector<std::string>& v, const std::string& s) {
    if (s.empty()) return;
    for (const auto& e : v) if (e == s) return;
    v.push_back(s);
}

/// The rows of the dropdown, object's own choices first.
inline std::vector<std::string> VsChoiceRows(VsChoiceKind kind, const VsChoiceLists& lists) {
    std::vector<std::string> out;
    if (kind == VsChoiceKind::Clip && !lists.onTarget.empty()) {
        for (const auto& s : lists.onTarget) VsPushUnique(out, s);
        return out;
    }
    for (const auto& s : lists.onTarget) VsPushUnique(out, s);
    for (const auto& s : lists.fallback) VsPushUnique(out, s);
    return out;
}

/// True when `value` is set and is not one of the known choices. An empty value
/// is "not chosen", not "missing from the list". A `@anim:` file reference is
/// known if its file name (without the extension) is in the list, or always
/// kept so a dragged clip is not flagged before the project scan sees it.
inline bool VsChoiceMissing(const std::string& value, const std::vector<std::string>& rows) {
    if (value.empty()) return false;
    std::string bare = value;
    const std::string p = "@anim:";
    if (bare.rfind(p, 0) == 0) bare = bare.substr(p.size());
    auto slash = bare.find_last_of("/\\");
    if (slash != std::string::npos) bare = bare.substr(slash + 1);
    auto dot = bare.rfind('.');
    if (dot != std::string::npos && dot > 0) {
        std::string ext = bare.substr(dot);
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".okayanim" || ext == ".anim" || ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".okayscene")
            bare = bare.substr(0, dot);
    }
    for (const auto& r : rows) if (r == value || r == bare) return false;
    return true;
}

/// A pin (or the node's text field, pin -1) the node cannot run without.
/// Empty when that input is optional.
inline const char* VsRequiredWhat(const std::string& type, int pin) {
    if (pin < 0) {
        if (type == "PlayClip") return "clip name";
        if (type == "Spawn" || type == "NetSpawnOwned") return "prefab";
        if (type == "LoadScene") return "scene";
        if (type == "GetVar" || type == "SetVar" || type == "AddVar" || type == "HasVar") return "variable name";
        if (type == "SendEvent" || type == "BroadcastEvent" || type == "OnEvent") return "event name";
        return nullptr;
    }
    if (type == "Branch" && pin == 0) return "condition";
    if (type == "MoveTowards" && pin == 1) return "object to move toward";
    if (type == "LookAt" && pin == 0) return "object to face";
    if (type == "Teleport" && pin == 3) return nullptr;   // empty means this object
    return nullptr;
}

inline bool VsArgFilled(const std::string& arg) { return !arg.empty(); }

/// Ready, missing a required input, or an error the checker already found.
/// `filledPin(pin)` is true when that input is wired or has a typed value.
/// `argFilled` is the node's text field. Move Towards is filled by its object
/// pin OR its object name.
template <typename Filled>
inline VsReady VsNodeReadiness(const std::string& type, bool argFilled, Filled filledPin, bool hasError, std::string* missing) {
    if (hasError) return VsReady::Error;
    auto need = [&](int pin, const char* what) {
        if (!what) return false;
        const bool ok = pin < 0 ? argFilled : filledPin(pin);
        if (ok) return false;
        if (missing && missing->empty()) *missing = std::string("Required: ") + what;
        return true;
    };
    bool miss = false;
    if (const char* w = VsRequiredWhat(type, -1)) miss = need(-1, w) || miss;
    if (type == "Branch") miss = need(0, "condition") || miss;
    if (type == "MoveTowards") {
        // The object pin or the name on the node. Either one is enough.
        if (!argFilled && !filledPin(1)) { miss = true; if (missing && missing->empty()) *missing = "Required: object to move toward"; }
    }
    if (type == "LookAt" && !argFilled) miss = need(0, "object to face") || miss;
    return miss ? VsReady::Missing : VsReady::Ready;
}

inline const char* VsReadyName(VsReady s) {
    switch (s) {
        case VsReady::Missing: return "Missing";
        case VsReady::Error: return "Error";
        default: return "Ready";
    }
}

/// Plain words for a pin tooltip.
inline std::string VsPinExplain(const std::string& /*type*/, const char* label, const char* typeName, bool input, bool exec) {
    const std::string name = (label && label[0]) ? label : (exec ? "flow" : "value");
    if (exec && input) return name + " — the step before this one. Drag the white triangle from an event or another step.";
    if (exec && !input) return name + " — what runs next. Drag this triangle onto the next step.";
    if (input) return name + " (" + typeName + ") — wire a value in, drop an object or file, or type one. Leave it empty only when this node has a default.";
    return name + " (" + typeName + ") — the value this node produces. Drag it into an input that wants a " + std::string(typeName) + ".";
}

/// A node to insert so `from` can feed `to`. Empty type when there is no single safe node.
struct VsConvertOffer {
    std::string type;   // node type to insert, "" = don't auto-insert
    std::string label;  // button text
    std::string why;
};

inline VsConvertOffer VsOfferConvert(char from, char to) {
    VsConvertOffer o;
    if (from == to || from == 'A' || to == 'A') return o;
    if (from == 'T' && to == 'N') { o.type = "ToNumber"; o.label = "Insert To Number"; o.why = "Text can't go straight into a number. To Number reads it as a number."; return o; }
    if ((from == 'N' || from == 'B') && to == 'T') { o.type = "ToString"; o.label = "Insert To Text"; o.why = "That input wants text. To Text writes the number out as text."; return o; }
    if (from == 'V' && to == 'N') { o.why = "A vector isn't one number. Use Get X, Get Y or Get Z."; return o; }
    if (from == 'N' && to == 'V') { o.why = "A number isn't a vector. Use Make Vec3."; return o; }
    if (from == 'O' && to == 'V') { o.why = "An object isn't a position. Use Object Position, or Get Position with this object as Target."; return o; }
    if (from == 'O' && to == 'N') { o.why = "An object isn't a number. Use Distance To, or Get X / Get Y / Get Z."; return o; }
    if (to == 'O') { o.why = "This input wants an object. Use Self, Find Object, or drop one from the Hierarchy."; return o; }
    return o;
}

/// True if adding a data wire producer -> consumer would loop (consumer already
/// reaches producer through existing producer->consumer edges).
inline bool VsDataWouldCycle(const std::vector<std::pair<int, int>>& producerToConsumer, int producer, int consumer) {
    if (producer == consumer) return true;
    std::vector<int> stack{consumer};
    std::vector<int> seen;
    auto mark = [&](int id) {
        for (int s : seen) if (s == id) return false;
        seen.push_back(id);
        return true;
    };
    int guard = 0;
    while (!stack.empty() && guard++ < 10000) {
        int id = stack.back(); stack.pop_back();
        if (!mark(id)) continue;
        if (id == producer) return true;
        for (const auto& e : producerToConsumer)
            if (e.first == id) stack.push_back(e.second);
    }
    return false;
}

} // namespace okay::editor::vsed
