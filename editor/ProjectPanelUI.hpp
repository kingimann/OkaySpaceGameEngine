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

/// Show what uses an asset (and what it uses). Clicking a line calls `onPick(path)`.
void ShowReferences(okaypb::Browser& browser, const std::string& assetPath, std::function<void(const std::string&)> onPick);

/// Draw all open dialogs. Call every frame, inside the Project window, outside any child window.
void DrawDialogs();

/// The import settings of one asset (texture, model or audio), with Apply and Revert. Draws nothing and
/// returns false for types that have none. Returns true in the frame the settings were changed. The same
/// function is meant for the Inspector: call it with the selected asset's path.
bool DrawImportSettings(okaypb::Browser& browser, const std::string& assetPath);

} // namespace oked::projectpanel
