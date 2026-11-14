#pragma once
// The Localization window's view of a string table (ImGui-free).
//
// DrawLocalizationWindow calls these functions. They are the editor path onto
// LocTable (the grid and its cell states), LocCheck and LocMissingReport (missing
// translations and placeholder mismatches), LocScanProject (unused and undefined
// keys) and LocPreview (pseudo-locale and mirrored right-to-left text). Nothing
// here changes the Localization service or the table it is given.
#include "okay/Core/LocCheck.hpp"
#include "okay/Core/LocPreview.hpp"
#include "okay/Core/LocScan.hpp"
#include "okay/Core/LocTable.hpp"

#include <string>
#include <utility>
#include <vector>

namespace oked {

/// Which rows the Localization window keeps. Search still applies on top.
enum class LocWindowFilter {
    All,                    ///< every key
    MissingIn,              ///< no text in filterLanguage
    PlaceholderMismatch,    ///< a {0} / {name} set differs from the source text
    SameAsSource,           ///< copied from the source language
    Unused,                 ///< nothing in the last scan mentions the key
    Issues,                 ///< LocCheck reported the key, or the scan says it is undefined
};

/// What the window is asking to see. A scan reads the project only when `scan` is set;
/// otherwise pass the previous LocScanResult so unused and undefined keys stay up to date
/// with the table without reading the disk again.
struct LocWindowQuery {
    std::string search;
    LocWindowFilter filter = LocWindowFilter::All;
    std::string filterLanguage;
    std::string projectDir;     ///< LocScanProject root; empty skips the directory walk
    bool scan = false;
    /// Keys the open scene uses. Counted when `scan` is set, so an unsaved scene is not
    /// reported unused.
    std::vector<std::string> sceneKeys;
    /// Extra sources (inline scripts, unsaved graphs). The path's extension picks the reader
    /// (".okay", ".okayvs").
    std::vector<std::pair<std::string, std::string>> extraFiles;
};

/// One row of the report the window lists. Clicking it selects `key`.
struct LocWindowIssue {
    okay::LocSeverity severity = okay::LocSeverity::Warning;
    okay::LocIssueKind kind = okay::LocIssueKind::MissingText;
    std::string key;
    std::string language;    ///< empty for an undefined key (no language owns it)
    std::string message;
    bool undefined = false;  ///< used by code or a scene, and absent from the table
};

/// The grid, the check and the scan the window draws for one table.
struct LocWindowModel {
    okay::LocGrid grid;
    okay::LocReport report;
    std::vector<okay::LocMissing> missing;   ///< LocMissingReport: non-source languages
    std::vector<okay::LocStats> stats;       ///< one per language, table order
    okay::LocScanResult scan;
    bool scanned = false;
};

/// Build the model. `previousScan` is the last scan (its mentions are compared with the
/// table again). Ignored when `query.scan` is set: that reads `projectDir` and the extra
/// files and scene keys.
okay::LocScanResult ScanLocWindow(const okay::StringTable& table, const LocWindowQuery& query);
LocWindowModel BuildLocWindowModel(const okay::StringTable& table, const LocWindowQuery& query,
                                   const okay::LocScanResult* previousScan = nullptr);

/// The issues the window lists: LocCheck warnings and errors (missing text, placeholder
/// mismatches, plurals, braces, length, whitespace), then undefined keys from a scan.
/// Info rows (same as the source, extra text) stay out of the list; the grid still colours them.
std::vector<LocWindowIssue> LocWindowIssues(const LocWindowModel& model);

/// One line for the window: "2 missing translations, 1 placeholder mismatch", or
/// "No missing keys or placeholder problems."
std::string LocWindowIssueSummary(const LocWindowModel& model);

/// The preview line for one key. `language` empty uses the table's default; "qps-ploc"
/// or `pseudo` turns on pseudo-localization. A right-to-left language is mirrored.
/// Does not switch the running language, log a missing key, or write the table.
struct LocWindowPreview {
    std::string language;
    std::string text;     ///< as the window shows it (pseudo-localized and/or mirrored)
    std::string plain;    ///< the stored text, before pseudo-localization and mirroring
    bool pseudo = false;
    bool mirrored = false;
    bool missing = false; ///< neither the shown language nor the default has text
};
LocWindowPreview PreviewLocKey(const okay::StringTable& table, const std::string& key,
                               const std::string& language, bool pseudo);

/// True when the window draws a preview line for the language it is showing
/// (the pseudo language, or a right-to-left one).
bool LocWindowShowsPreview(const std::string& language);

} // namespace oked
