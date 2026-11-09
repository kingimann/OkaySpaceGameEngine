#pragma once
// Which string-table keys a project actually uses (docs/localization_tools.md).
//
// LocScanProject reads a project's text assets and script sources and collects the keys
// they mention:
//  * scenes and prefabs: the `loc` record of an object (Text, Button, Toggle, Tooltip,
//    World UI, Input Field keys),
//  * visual scripts: Localize nodes,
//  * scripts (OkayScript, C#-style, Lua, C++) and UI documents: calls such as
//    loc("key"), Loc("key"), T("key"), Localize("key"), Localization.Get("key"),
//  * dialogue graphs and quest files: the keys the engine derives from them,
//  * optionally strings written "@key".
// A key built from a literal and more ("shop." + name) marks every key with that prefix as used.
// Against a table that gives the keys nobody uses and the keys used but not defined.
#include "okay/Core/Localization.hpp"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// One mention of a key.
struct LocScanUse {
    std::string key;
    std::string file;
    int line = 0;
    std::string how;           ///< "loc()", "Text", "Localize node", "dialogue"...
    bool prefix = false;       ///< `key` is the literal start of a key built at run time
    bool optional = false;     ///< a key the engine derives (dialogue, quests): the text in the asset shows without it
};

struct LocScanOptions {
    /// Files with these extensions (lower case, with the dot) are read.
    std::vector<std::string> extensions = {".okayscene", ".okayprefab", ".okayui", ".okay", ".cs", ".lua",
                                           ".okayvs", ".okaydialog", ".okayquest", ".cpp", ".hpp", ".h"};
    /// Functions whose first argument is a key. A name with a dot ("Localization.Get") must be
    /// written with that qualifier at the call (`::` reads as a dot); a bare name matches
    /// whatever object it is called on.
    std::vector<std::string> callNames = {"loc", "Loc", "T", "tr", "Tr", "Localize", "LocalizeArgs",
                                          "Localization.Get", "Localization.GetOr", "Localization.TryGet",
                                          "Localization.Localize", "Localization.LocalizeArgs"};
    /// Also read "@some.key" strings as keys (the engine doesn't resolve these itself).
    bool atKeys = false;
    /// Keys starting with these are read by built-in components (pause menu, hold prompt,
    /// loading screen, dialogue box, quest tracker): never reported unused.
    std::vector<std::string> engineKeyPrefixes = {"pause.", "hold.", "loading.", "dialogue.", "quest.", "dlg.", "speaker."};
    /// Folders with these names are not entered ("build", ".git"...); folders starting with a dot never are.
    std::set<std::string> skipDirs = {"build", "bin", "obj", "Library", "Temp", "node_modules", "dist"};
    std::size_t maxFileBytes = 8u * 1024u * 1024u;   ///< bigger files are skipped
};

struct LocScanResult {
    std::vector<LocScanUse> uses;             ///< every mention, in file order
    std::set<std::string> usedKeys;           ///< the table keys that are used
    std::vector<std::string> unused;          ///< table keys nothing uses, sorted
    std::vector<LocScanUse> undefined;        ///< mentions of keys the table lacks (not run-time-built, not engine-derived)
    int filesScanned = 0;
    int filesSkipped = 0;                     ///< too big or unreadable

    /// "unused (3): a, b, c" and "undefined (2): key  file:line", one per line.
    std::string ToText() const;
};

/// Mentions in one file's text. `file` picks the reader by its extension and is
/// recorded on each mention.
void LocScanText(const std::string& file, const std::string& text, const LocScanOptions& options,
                 std::vector<LocScanUse>& out);

/// Compare mentions with a table: fills usedKeys, unused and undefined of `result`
/// (which already holds `uses`).
void LocScanResolve(const StringTable& table, const LocScanOptions& options, LocScanResult& result);

/// Scan (path, text) pairs, then compare with the table.
LocScanResult LocScanFiles(const std::vector<std::pair<std::string, std::string>>& files, const StringTable& table,
                           const LocScanOptions& options = {});

/// Scan every file under `root`, then compare with the table.
LocScanResult LocScanProject(const std::string& root, const StringTable& table, const LocScanOptions& options = {});

} // namespace okay
