#pragma once
// Guided first steps: a thin one-line "next step" bar at the bottom of the Visual Script window. This is the
// state machine behind it, as plain functions: given what is on the canvas and what the person did, which hint
// to show now, and which steps count as done (so they are remembered and never nag again).
// The four steps, in order: pick a recipe or add a block, connect two blocks, press Play, change a number.
#include <string>

namespace okay::editor::vsed {

enum class VsHint { None = 0, PickRecipe, AddNext, Connect, Play, ChangeNumber };

/// Bits of VsUsagePrefs::hintsDone.
inline constexpr unsigned kHintDoneAdded   = 1u << 0;   // a block was on the canvas
inline constexpr unsigned kHintDoneWired   = 1u << 1;   // the person drew a wire themselves
inline constexpr unsigned kHintDonePlayed  = 1u << 2;   // they pressed Play with a graph on the canvas
inline constexpr unsigned kHintDoneChanged = 1u << 3;   // they changed a number on a block
inline constexpr unsigned kHintDoneAll = kHintDoneAdded | kHintDoneWired | kHintDonePlayed | kHintDoneChanged;

struct VsHintFacts {
    int  nodeCount = 0;
    int  wireCount = 0;
    bool playing = false;        ///< the scene is running now
    bool userWired = false;      ///< this frame the person finished drawing a wire
    bool editedNumber = false;   ///< this frame the person changed a value typed on a block
};

struct VsHintResult {
    VsHint hint = VsHint::None;
    unsigned done = 0;           ///< the new done bits; save them when they differ from the old ones
};

/// One step of the machine. `enabled` false (the person turned hints off) shows nothing but still records.
inline VsHintResult VsHintStep(unsigned done, const VsHintFacts& f, bool enabled = true) {
    if (f.nodeCount > 0) done |= kHintDoneAdded;
    if (f.userWired && f.wireCount > 0) done |= kHintDoneWired;
    if (f.playing && f.nodeCount > 0) done |= kHintDonePlayed;
    if (f.editedNumber && f.nodeCount > 0) done |= kHintDoneChanged;
    VsHintResult r;
    r.done = done;
    if (!enabled || (done & kHintDoneAll) == kHintDoneAll) return r;
    if (f.nodeCount == 0) r.hint = VsHint::PickRecipe;
    else if (f.wireCount == 0 && f.nodeCount == 1) r.hint = VsHint::AddNext;
    else if (f.wireCount == 0) r.hint = VsHint::Connect;
    else if (!(done & kHintDonePlayed)) r.hint = VsHint::Play;
    else if (!(done & kHintDoneChanged)) r.hint = VsHint::ChangeNumber;
    return r;
}

inline const char* VsHintText(VsHint h) {
    switch (h) {
        case VsHint::PickRecipe:   return "Pick a recipe, or press Space to add a block.";
        case VsHint::AddNext:      return "Press Space to add the next block.";
        case VsHint::Connect:      return "Drag from the dot on the right of a block to the dot on the left of the next one.";
        case VsHint::Play:         return "Press Play to try it.";
        case VsHint::ChangeNumber: return "Change a number on a block to change what it does.";
        default:                   return "";
    }
}

} // namespace okay::editor::vsed
