#pragma once
// Upgrading one `.okaymeta` file (an asset's GUID, labels and import settings) to the layout this build writes,
// without an asset database and without writing anything: the project upgrade (editor/ProjectUpgrade.cpp) uses it so
// metas are versioned, backed up and reported like scenes, instead of being rewritten quietly by the next scan.
// Header note: keep this header free of <mutex>, <thread> and <condition_variable>.
#include <string>
#include <vector>

namespace okay {

struct MetaUpgradeResult {
    bool ok = false;                  ///< the text is a meta file this build can read (`error` says why not)
    bool changed = false;             ///< it has to be rewritten (an older meta or settings layout)
    bool newer = false;               ///< written by a newer layout: left exactly as it is
    std::string text;                 ///< what to write (the input when nothing changes)
    std::string kind;                 ///< the asset kind the settings are for ("texture", "model", ...; "" for none)
    int metaFrom = 0, metaTo = 0;     ///< the `okaymeta <n>` line before and after
    int importerFrom = 0, importerTo = 0;   ///< the settings layout before and after (0 = the file has none)
    std::vector<std::string> notes;   ///< what the steps did, one line each
    std::vector<std::string> losses;  ///< values that could not be carried over (a bad value, a type that no longer fits)
    std::string error;
};

/// Upgrade the text of `<assetPath>.okaymeta`. `assetPath` decides the asset kind (by its extension), as the asset
/// database does. The GUID, labels, the stored hash and size, and every line this build does not know are kept.
MetaUpgradeResult UpgradeMetaText(const std::string& assetPath, const std::string& text);

} // namespace okay
