#pragma once
// ImGui pieces of the Project panel that do not need the rest of the editor: the name prompt, the delete
// confirmation (with what uses the asset), the "used by" list and the import-settings editor.
// The logic they sit on is in ProjectBrowser.hpp. Call DrawDialogs() once per frame inside the Project window.
#include "ProjectBrowser.hpp"

#include <functional>
#include <string>
#include <vector>

namespace oked::projectpanel {

/// Ask for a name before creating or renaming something. `accept` gets the typed name with `suffix` added and
/// returns "" when it worked (the prompt closes) or a message (it stays open and shows it).
struct NameRequest {
    std::string title;       ///< "New material"
    std::string hint;        ///< one short line under the field (may be empty)
    std::string initial;     ///< the name without the suffix
    std::string suffix;      ///< ".okaymat": shown after the field, always added; empty for folders
    std::string okLabel = "Create";
    std::function<std::string(const std::string& fullName)> accept;
};
void AskName(NameRequest request);
bool NamePromptOpen();

/// Ask before deleting. Lists what uses the assets. `onConfirm` runs when the user agrees.
void AskDelete(okaypb::Browser& browser, std::vector<std::string> paths, std::function<void()> onConfirm);

/// What the references window lists: both lists, only what uses the asset ("Find References") or only what it uses
/// ("Show Dependencies").
enum class RefMode { Both, UsedBy, Uses };
/// Show what uses an asset (and what it uses), read from the asset database's dependency graph. A checkbox switches
/// between the direct links and the whole graph. Clicking a line calls `onPick(path)`.
void ShowReferences(okaypb::Browser& browser, const std::string& assetPath, std::function<void(const std::string&)> onPick,
                    RefMode mode = RefMode::Both);

/// The Broken References list: references to files that are not there (click a line to show the asset that holds
/// it) and references that still use the old path of an asset moved outside the editor, with a Fix button that
/// rewrites the stale ones (undo-able with Ctrl+Z in the Project panel). `onFixed` runs after a fix.
void ShowReferenceProblems(okaypb::Browser& browser, std::function<void(const std::string&)> onPick, std::function<void(const okaypb::Result&)> onFixed);

/// Draw all open dialogs. Call every frame, inside the Project window, outside any child window.
void DrawDialogs();

/// True when the asset has import settings to show (a texture, model or audio file that is indexed).
bool HasImportSettings(okaypb::Browser& browser, const std::string& assetPath);
/// The import settings of one asset (texture, model or audio), with Apply and Revert. Draws nothing for types
/// that have none (check HasImportSettings first). Returns true in the frame the settings were applied. The same
/// function is meant for the Inspector: call it with the selected asset's path.
bool DrawImportSettings(okaypb::Browser& browser, const std::string& assetPath);

} // namespace oked::projectpanel
