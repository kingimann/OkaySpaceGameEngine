#pragma once
// The words on the Visual Script left panel (Events, saved values, bundles, action inputs), in one place so a test can
// read every one. Plain words, one short sentence each. No ImGui. Written for this project.
namespace okay::editor::vsed {

inline constexpr const char* kPanelEvents      = "When this happens";
inline constexpr const char* kPanelNoEvents    = "Nothing starts the script yet. Add a block that starts with On.";
inline constexpr const char* kPanelEventTip    = "%d blocks come after it. Click to go there.";
inline constexpr const char* kPanelNamedActions = "Named actions";
inline constexpr const char* kPanelStopPoints  = "Stop points";
inline constexpr const char* kPanelStopTip     = "Click to go there. Right-click to remove.";
inline constexpr const char* kPanelStopGone    = "(removed block)";

inline constexpr const char* kPanelSaved       = "Saved values";
inline constexpr const char* kPanelSavedHelp   = "A saved value is something the script remembers, like a score.";
inline constexpr const char* kPanelSavedBad    = "This saved value can't be used: %s";
inline constexpr const char* kPanelNameTip     = "The name. Press Enter to rename. Blocks that use it follow.";
inline constexpr const char* kPanelRemoveTip   = "Remove this saved value. Blocks that use the name still work.";
inline constexpr const char* kPanelScopeTip    = "Who can see it: this object, this scene, or the whole game.";
inline constexpr const char* kPanelTypeTip     = "What kind of thing it holds.";
inline constexpr const char* kPanelStartHint   = "start value as text, like [1, 2, 3]";
inline constexpr const char* kPanelStartTip    = "Type the start value and press Enter.";
inline constexpr const char* kPanelNewSaved    = "New saved value";
inline constexpr const char* kPanelNameHint    = "name, like health";
inline constexpr const char* kPanelAddSaved    = "Add saved value";
inline constexpr const char* kPanelScopes[3]   = {"This object", "This scene", "Whole game"};

inline constexpr const char* kPanelUsed        = "Where each value is used";
inline constexpr const char* kPanelNoneUsed    = "Saved values you use show up here.";
inline constexpr const char* kPanelUsedTip     = "Click to go to its block.";

inline constexpr const char* kPanelBundles     = "Bundles";
inline constexpr const char* kPanelBundlesHelp = "A bundle keeps a few named values together, like an Enemy with health and speed.";
inline constexpr const char* kPanelBundleName  = "new bundle name";
inline constexpr const char* kPanelPartName    = "part name";
inline constexpr const char* kPanelPartStart   = "start value (blank = normal)";
inline constexpr const char* kPanelAddPart     = "+ part";
inline constexpr const char* kPanelAddBundle   = "+ bundle";
inline constexpr const char* kPanelRemoveBundle = "Remove bundle";

inline constexpr const char* kPanelInputs      = "Action inputs";
inline constexpr const char* kPanelNoInputs    = "A named action lists its inputs here.";

/// Every sentence and label above, for the test that reads them all.
inline const char* const kPanelAllWords[] = {
    kPanelEvents, kPanelNoEvents, kPanelEventTip, kPanelNamedActions, kPanelStopPoints, kPanelStopTip, kPanelStopGone,
    kPanelSaved, kPanelSavedHelp, kPanelSavedBad, kPanelNameTip, kPanelRemoveTip, kPanelScopeTip, kPanelTypeTip,
    kPanelStartHint, kPanelStartTip, kPanelNewSaved, kPanelNameHint, kPanelAddSaved,
    kPanelScopes[0], kPanelScopes[1], kPanelScopes[2], kPanelUsed, kPanelNoneUsed, kPanelUsedTip,
    kPanelBundles, kPanelBundlesHelp, kPanelBundleName, kPanelPartName, kPanelPartStart, kPanelAddPart, kPanelAddBundle,
    kPanelRemoveBundle, kPanelInputs, kPanelNoInputs,
};

} // namespace okay::editor::vsed
