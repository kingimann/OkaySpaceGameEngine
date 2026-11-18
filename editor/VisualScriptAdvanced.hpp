#pragma once
// What the add-node list shows by default. Beginner mode shows about 55 blocks in six big groups (the Quick
// blocks plus a small set of basics) and nothing else. "Show all nodes" (off by default, remembered) reveals the
// whole library. Search can still find a hidden node: it is marked "Advanced" and needs the switch or a click on
// "Search all nodes". Pure data and functions, no ImGui. Written for this project.
#include "okay/VisualScript/QuickBlocks.hpp"

#include <cstring>

namespace okay::editor::vsed {

enum VsBeginnerGroupId { kGroupEvents = 0, kGroupMove, kGroupLook, kGroupRules, kGroupSound, kGroupFlow, kGroupCount };
inline const char* const kVsBeginnerGroupNames[kGroupCount] = {
    "Events", "Move", "Look", "Game rules", "Sound and screen", "Flow"};
inline const char* const kVsBeginnerGroupBlurbs[kGroupCount] = {
    "When something happens: the game starts, a key, a click, a touch.",
    "Move, turn, jump, follow and walk back and forth.",
    "Spin, flash a colour, show or hide, make copies, and the camera.",
    "Score, health, winning, losing, coins and doors.",
    "Show text, the score and play sounds.",
    "Wait, repeat, choose, and simple maths.",
};

struct VsBeginnerEntry { const char* type; int group; };
/// The default palette: every row is one visible block. A test fails when this grows past the budget.
inline constexpr int kVsDefaultPaletteBudget = 60;
inline constexpr int kVsGroupBudget = 12;
inline const VsBeginnerEntry kVsBeginner[] = {
    // Events
    {"QbRule", kGroupEvents}, {"OnStart", kGroupEvents}, {"OnUpdate", kGroupEvents}, {"OnKey", kGroupEvents}, {"OnClick", kGroupEvents},
    {"OnTrigger", kGroupEvents}, {"OnCollision", kGroupEvents}, {"QbWhenTouchedDo", kGroupEvents},
    {"QbEverySeconds", kGroupEvents}, {"QbDoForever", kGroupEvents},
    // Move
    {"QbMoveWithKeys", kGroupMove}, {"QbMoveToward", kGroupMove}, {"QbFollowObject", kGroupMove}, {"QbPatrol", kGroupMove},
    {"QbJumpOnKey", kGroupMove}, {"QbTurnToFace", kGroupMove}, {"Translate", kGroupMove}, {"Rotate", kGroupMove},
    {"SetPosition", kGroupMove},
    // Look
    {"QbSpinningObject", kGroupLook}, {"QbFlashColour", kGroupLook}, {"QbFollowWithCamera", kGroupLook},
    {"SetActive", kGroupLook}, {"Spawn", kGroupLook}, {"Destroy", kGroupLook}, {"QbRemoveAfter", kGroupLook},
    {"QbSpawnCopyAt", kGroupLook}, {"QbSpawnEvery", kGroupLook}, {"QbShootOnKey", kGroupLook},
    // Game rules
    {"QbAddToScore", kGroupRules}, {"QbLoseHealth", kGroupRules}, {"QbGameOverAtZeroHealth", kGroupRules},
    {"QbGameOverOnTouch", kGroupRules}, {"QbWinAtScore", kGroupRules}, {"QbCollectOnTouch", kGroupRules},
    {"QbOpenDoorWhenTouched", kGroupRules}, {"QbChangeNumber", kGroupRules}, {"QbRespawnAtCheckpoint", kGroupRules}, {"LoadScene", kGroupRules},
    // Sound and screen
    {"QbShowText", kGroupSound}, {"QbShowScore", kGroupSound}, {"QbPlaySoundOnKey", kGroupSound}, {"Print", kGroupSound},
    {"SetText", kGroupSound}, {"PlaySound", kGroupSound}, {"QbPlayOrStopSound", kGroupSound},
    // Flow
    {"QbWaitThenDo", kGroupFlow}, {"QbRepeat", kGroupFlow}, {"QbIfKeyThenDo", kGroupFlow}, {"Branch", kGroupFlow},
    {"Sequence", kGroupFlow}, {"GetVar", kGroupFlow}, {"SetVar", kGroupFlow}, {"AddVar", kGroupFlow},
    {"Add", kGroupFlow}, {"Compare", kGroupFlow}, {"QbIfNumberIs", kGroupFlow},
};
inline constexpr int kVsBeginnerCount = (int)(sizeof kVsBeginner / sizeof kVsBeginner[0]);

/// The beginner group (0..5) a block belongs to, or -1 when it is only in the full library.
inline int VsBeginnerGroup(const char* type) {
    if (!type) return -1;
    for (const VsBeginnerEntry& e : kVsBeginner) if (std::strcmp(e.type, type) == 0) return e.group;
    return -1;
}
inline bool VsVisibleByDefault(const char* type) { return VsBeginnerGroup(type) >= 0; }

/// True when a node is left out of the browse list. `levelFilter` is the existing level buttons (0 = All): a person
/// who picked a level button wants that level, so the rule only applies to "All".
inline bool VsHiddenWhileBrowsing(const char* type, int levelFilter, bool showAll) {
    return !showAll && levelFilter == 0 && !VsVisibleByDefault(type);
}

/// The mark on a search result that browsing would hide.
inline const char* VsAdvancedMark(const char* type, bool showAll) {
    return !showAll && !VsVisibleByDefault(type) ? "Advanced" : "";
}

} // namespace okay::editor::vsed
