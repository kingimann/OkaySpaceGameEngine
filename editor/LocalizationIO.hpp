#pragma once
// What the Localization window does around files and fonts (ImGui-free, so it is tested without a window):
//  * JSON import and export of the string table,
//  * a preview of an import (CSV or JSON): what would change, shown before anything is applied,
//  * a font coverage report for one language and one chosen font,
//  * right-to-left text put in the order a reader sees it (a small, honest subset of the Unicode
//    bidirectional algorithm: see LocVisualOrder).
// Nothing here touches the live Localization service; the caller applies a preview's `after` table.
#include "okay/Core/LocCheck.hpp"
#include "okay/Core/LocTable.hpp"

#include <string>
#include <vector>

namespace oked {

// ---- Import preview and JSON ------------------------------------------------------------------

enum class LocImportFormat { Auto, Csv, Json };

/// Files bigger than this are refused before they are parsed (a translator's sheet is far smaller).
constexpr std::size_t kLocImportMaxBytes = 32u << 20;

/// What an import would do. `ok` false means nothing can be applied (`error` says why). `after` is
/// the table the import makes from the live one, so applying the preview gives exactly what was shown.
struct LocImportPreview {
    bool ok = false;
    std::string error;
    LocImportFormat format = LocImportFormat::Csv;   ///< what the input was read as
    int rows = 0;                                    ///< CSV rows or JSON keys read
    okay::LocDiff diff;
    std::vector<std::string> warnings;
    okay::StringTable after;
    /// Readable lines, one per change ("+ key menu.quit", "~ fr menu.start: "Start" -> "Commencer"").
    /// At most `maxLines`; `hiddenLines` counts the ones left out.
    std::vector<std::string> lines;
    int hiddenLines = 0;
    int ChangeCount() const;                         ///< new keys + new languages + filled + changed + cleared + notes
    bool NothingToDo() const { return ok && diff.Empty(); }
    std::string Summary() const;                     ///< "Imported 3 rows: 2 filled, 1 changed, 1 new key"
};

/// Guess CSV or JSON from the bytes: a first character `{` (after a byte order mark and white space) is
/// JSON, anything else is CSV (which includes the spreadsheet's UTF-16 "Unicode text").
LocImportFormat DetectLocFormat(const std::string& bytes);

/// Work out what importing `bytes` into `live` would change, without changing `live`. `jsonLanguage`
/// is the language of a one-language JSON file ({"key": "text"}); the whole-table form names its own.
/// `mode` is the import mode (the window uses Overwrite: non-empty cells replace, empty cells keep).
LocImportPreview PreviewLocImport(const okay::StringTable& live, const std::string& bytes,
                                  LocImportFormat format = LocImportFormat::Auto, const std::string& jsonLanguage = "",
                                  okay::LocImportOptions::Mode mode = okay::LocImportOptions::Mode::Overwrite,
                                  std::size_t maxLines = 200);

/// Make `live` the previewed table. False (and `live` untouched) when the preview is not ok or has nothing to do.
bool ApplyLocImport(okay::StringTable& live, const LocImportPreview& preview);

/// The table as JSON: every language and key (the form ImportJson reads back), or, with `language`, one
/// language as {"key": "text"}.
std::string ExportLocJson(const okay::StringTable& table, const std::string& language = "");

// ---- Font coverage ---------------------------------------------------------------------------------

/// The report the window lists for one language (empty = every language). `fontPath` is the font the
/// translator chose; empty uses each language's own font from the table, then the built-in UI font.
struct LocFontView {
    bool ok = false;               ///< the report ran (false: the chosen font could not be loaded)
    std::string error;
    okay::LocFontReport report;
    bool clean = true;             ///< nothing missing and no text that one font cannot draw
    int missingCharacters = 0;     ///< distinct characters no font in the chain draws
    int undrawableTexts = 0;
    std::vector<std::string> lines;
    std::string Summary() const;   ///< "ja: 3 characters the font cannot draw" or "Every character can be drawn."
};
LocFontView LocWindowFontCoverage(const okay::StringTable& table, const std::string& language = "",
                                  const std::string& fontPath = "", const std::string& baseDir = "",
                                  bool useSystemFonts = false, std::size_t maxLines = 100);

// ---- Right to left ---------------------------------------------------------------------------------

/// `text` in the order a reader sees it left to right on a screen (visual order), for a paragraph that
/// runs right to left (`rtlParagraph`) or left to right. Each line is reordered on its own.
///
/// A subset of Unicode UAX #9, written from the public description: strong right-to-left letters
/// (Hebrew, Arabic and related blocks) and strong left-to-right letters get embedding levels, digits
/// stay in reading order, neutral characters between two runs of the same direction join them and
/// otherwise take the paragraph direction, runs are reversed from the highest level down, and
/// brackets in a right-to-left run are mirrored. A `{placeholder}` is one left-to-right unit.
/// Not done: explicit embeddings and isolates (LRE, RLI...), the weak-type rules for separators
/// (a comma inside a number), and Arabic shaping (joining forms), so connected letters stay in
/// their isolated forms.
std::string LocVisualOrder(const std::string& text, bool rtlParagraph);

/// True when the text has a strong right-to-left character.
bool LocHasRtlText(const std::string& text);

} // namespace oked
