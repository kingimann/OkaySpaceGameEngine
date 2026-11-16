#pragma once
// The node text fields a node can't run without. One list, read by the Visual
// Script editor (the red "Missing" badge and its "Required: ..." line) and by
// Check Graph (ValidateGraphText), so the two can't disagree about a node.
// A node whose empty field has a meaning ("empty = this object") is not listed.
#include <string>

namespace okay::vs {

struct VsRequiredArg {
    const char* type;   ///< node type in .okayvs files
    const char* what;   ///< what is missing, after "Required: "
    const char* how;    ///< one short sentence on how to fill it in
};

/// The required text field of node `type`, or nullptr when it may stay empty.
inline const VsRequiredArg* VsRequiredArgFor(const std::string& type) {
    static const VsRequiredArg kRows[] = {
        {"PlayClip",        "clip name",     "Pick one from the list, or drop a clip file."},
        {"CrossfadeClip",   "clip name",     "Pick one from the list, or drop a clip file."},
        {"Spawn",           "prefab",        "Drop a prefab file on the slot, or pick one."},
        {"NetSpawnOwned",   "prefab",        "Drop a prefab file on the slot, or pick one."},
        {"SpawnProjectile", "prefab",        "Drop a prefab file on the slot, or pick one."},
        {"SpawnEvery",      "prefab",        "Drop a prefab file on the slot, or pick one."},
        {"PoolGet",         "prefab",        "Drop a prefab file on the slot, or pick one."},
        {"LoadScene",       "scene",         "Pick a scene from the list."},
        {"GetVar",          "variable name", "Pick one from the list, or type a name."},
        {"SetVar",          "variable name", "Pick one from the list, or type a name."},
        {"AddVar",          "variable name", "Pick one from the list, or type a name."},
        {"HasVar",          "variable name", "Pick one from the list, or type a name."},
        {"SendEvent",       "event name",    "Pick one from the list, or type a name."},
        {"BroadcastEvent",  "event name",    "Pick one from the list, or type a name."},
        {"OnEvent",         "event name",    "Pick one from the list, or type a name."},
        {"RunGraph",        "graph file",    "Drop a .okayvs file on the slot, or type file.okayvs|Event."},
        {"SwitchCamera",    "camera",        "Drop the camera's object on the slot, or pick it."},
        {"PlaySequence",    "sequence name", "Type the Sequence Player's file name (Assets/<name>.okayseq)."},
    };
    for (const VsRequiredArg& r : kRows)
        if (type == r.type) return &r;
    return nullptr;
}

} // namespace okay::vs
