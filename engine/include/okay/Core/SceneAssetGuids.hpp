#pragma once
// GUID references inside scene files (feature map 1.12). A scene keeps writing asset paths exactly as before; on save a
// small table is added after the header line, one record per asset the scene names:
//
//     assetguid <32 hex digits> "<path as written in the scene>" "<project path when it was saved>"
//
// On load, a path that no longer resolves (the asset was renamed or moved outside the editor, with no move history) is looked
// up again by its GUID and rewritten to the asset's current path. Old files have no table and load exactly as before; builds
// that do not know the record keep it and warn about an unknown record (the loader of this build skips it silently).
// Written from our own design (docs/core_assets_upgrade.md); standard library only.
#include "okay/Core/AssetDatabase.hpp"
#include <string>
#include <vector>

namespace okay {
namespace sceneguids {

struct TableEntry { Guid guid; std::string written, projectPath; };

struct Report {
    int stamped = 0;                        ///< records written by Stamp
    int repaired = 0;                       ///< paths rewritten by Resolve (an asset moved or renamed)
    std::vector<std::string> repairs;       ///< "old -> new" for each repaired path
    std::vector<std::string> broken;        ///< paths that resolve to nothing, with no usable GUID either
    std::string ToString() const;
};

/// The records of `text`, in order.
std::vector<TableEntry> ReadTable(const std::string& text);
/// `text` with every `assetguid` line removed.
std::string StripTable(const std::string& text);

/// Returns `text` with a fresh table (an old one is replaced). Only assets that resolve in `db` get a record; each one gets a
/// `.okaymeta` (EnsureMeta) so its GUID lasts. `db` must have been scanned.
std::string Stamp(const std::string& text, AssetDatabase& db, Report* report = nullptr);
/// Returns `text` with the paths of moved or renamed assets rewritten through the table. The table itself is kept. Paths
/// without a record, or whose record has no live asset, are left alone and (if the file is also missing) listed as broken.
std::string Resolve(const std::string& text, const AssetDatabase& db, Report* report = nullptr);
/// Paths in `text` that resolve to nothing in `db` (a read-only report; the same list Resolve fills in `broken`).
std::vector<std::string> FindBroken(const std::string& text, const AssetDatabase& db);

/// File helpers (written aside and swapped in). The old file is left as `<path>.bak` when it was changed by Resolve.
bool StampFile(const std::string& scenePath, AssetDatabase& db, Report* report = nullptr, std::string* error = nullptr);
bool RepairFile(const std::string& scenePath, const AssetDatabase& db, Report* report = nullptr, std::string* error = nullptr);

} // namespace sceneguids
} // namespace okay
