#pragma once
// LocTable: the data model a translation-table window or a command-line tool works with
// (docs/localization_tools.md).
//
// It wraps a StringTable and adds what the runtime doesn't need:
//  * loading every language file of a folder (.okayloc, .csv, .json) into one table,
//  * a grid model: keys as rows, languages as columns, every cell with a state
//    (translated, missing, same as the source, placeholder mismatch, extra),
//  * CSV and JSON import and export (RFC 4180 quoting, UTF-8 with a byte order mark,
//    plural forms kept as the separate keys `coins.one`, `coins.other`), with a diff of
//    what an import changed.
//
// Nothing here touches the Localization service.
#include "okay/Core/LocCheck.hpp"
#include "okay/Core/Localization.hpp"

#include <set>
#include <string>
#include <vector>

namespace okay {

enum class LocCellState {
    Translated,            ///< has text that differs from the source's (or is the source's)
    Missing,               ///< no text
    SameAsSource,          ///< identical to the source text and has letters: probably untranslated
    PlaceholderMismatch,   ///< its {0} / {name} set differs from the source text's
    Extra,                 ///< has text where the source language has none, or for a language the table doesn't list
};
const char* LocCellStateName(LocCellState s);
/// A bit for LocGridOptions::stateMask.
constexpr unsigned LocStateBit(LocCellState s) { return 1u << (unsigned)s; }

struct LocCell {
    std::string text;
    LocCellState state = LocCellState::Missing;
};

struct LocGridRow {
    std::string key;
    std::string note;
    std::vector<LocCell> cells;   ///< one per column of the grid
};

/// A snapshot of the table as rows and columns, for a UI to draw.
struct LocGrid {
    std::vector<std::string> languages;   ///< the columns
    std::vector<LocGridRow> rows;
    int RowCount() const { return (int)rows.size(); }
    int ColCount() const { return (int)languages.size(); }
    /// Column of a language, or -1.
    int ColumnOf(const std::string& lang) const;
    const LocCell& Cell(int row, int col) const { return rows[(std::size_t)row].cells[(std::size_t)col]; }
};

struct LocGridOptions {
    std::vector<std::string> languages;   ///< the columns (empty = every language, in table order)
    std::string search;                   ///< keep rows whose key, note or a shown text has this (case-insensitive)
    std::string keyPrefix;                ///< keep keys starting with this ("menu.")
    unsigned stateMask = 0;               ///< keep rows with a cell in one of these states (LocStateBit); 0 = all rows
    bool sortByIssues = false;            ///< rows with the worst cells first (else by key)
};

struct LocStats {
    std::string language;
    int total = 0;
    int translated = 0, missing = 0, sameAsSource = 0, placeholderMismatch = 0, extra = 0;
    /// Share of the keys with text (translated, same-as-source, mismatched and extra all have text).
    float Completion() const { return total == 0 ? 1.0f : (float)(total - missing) / (float)total; }
};

/// What an import changed (or, for a dry run, would change).
struct LocDiff {
    std::vector<std::string> addedKeys;
    std::vector<std::string> addedLanguages;
    /// (key, language) of texts that were empty and now have one / had one and changed / were cleared.
    std::vector<std::pair<std::string, std::string>> filled, changed, cleared;
    int notesChanged = 0;
    bool Empty() const {
        return addedKeys.empty() && addedLanguages.empty() && filled.empty() && changed.empty() && cleared.empty() && notesChanged == 0;
    }
};

struct LocImportOptions {
    enum class Mode {
        Overwrite,   ///< non-empty cells replace the text the table has; empty cells keep it
        FillEmpty,   ///< only cells the table has no text for are written
        Replace,     ///< every cell is written, an empty one clears the text
    };
    Mode mode = Mode::Overwrite;
    bool addKeys = true;          ///< keys the table doesn't have are added (else skipped)
    bool addLanguages = true;     ///< columns for languages the table doesn't list are added (else skipped)
    bool dryRun = false;          ///< work out the diff but leave the table alone
    /// JSON of one language ({"key": "text"}): which language it is. A file's stem when loading a folder.
    std::string language;
};

struct LocImportResult {
    bool ok = false;
    std::string error;
    int rows = 0;                 ///< rows (CSV) or keys (JSON) read
    LocDiff diff;
    std::vector<std::string> warnings;   ///< duplicate keys, short rows, skipped columns...
};

struct LocCsvOptions {
    std::vector<std::string> languages;   ///< the language columns (empty = all)
    bool note = true;                     ///< a "note" column after "key"
    bool bom = true;                      ///< UTF-8 byte order mark, so spreadsheets read accents
    bool crlf = true;                     ///< CRLF row ends (else LF)
    char delimiter = ',';                 ///< ',' ';' or '\t'
    /// Only the keys with no text in this language ("" = every key): a hand-off sheet for a translator.
    std::string missingIn;
};

/// RFC 4180 rows: quoted fields may hold the delimiter, doubled quotes and line breaks. A
/// byte order mark is skipped, UTF-16 text (spreadsheet "Unicode text") is converted to
/// UTF-8, CR, LF and CRLF all end a row. `delimiter` 0 = detect from the first row.
/// False (with `err`) for a file that is too big or whose quotes never close.
bool LocParseCsv(const std::string& text, std::vector<std::vector<std::string>>& rows, std::string* err = nullptr,
                 char delimiter = 0);
/// One CSV field: quoted when it holds a comma, semicolon, tab, quote or line break, or
/// starts or ends with a space (so it reads back the same with any delimiter).
std::string LocCsvField(const std::string& value);

class LocTable {
public:
    StringTable table;                       ///< the data; edit it directly (Set, Add, Rename...)
    std::string sourceLanguage;              ///< the language others are compared with ("" = the table's default)
    std::set<std::string> sameAsSourceOk;    ///< keys that may match the source ("OK", names, numbers)
    std::vector<std::string> files;          ///< the files LoadFolder / LoadFile read
    std::vector<std::string> warnings;       ///< problems loading, in order

    LocTable() = default;
    explicit LocTable(StringTable t) : table(std::move(t)) {}

    // ---- Loading ----
    /// Read every `.okayloc`, `.csv` and `.json` file in `dir` (not its subfolders), in file
    /// name order, merging them into one table; later files overwrite earlier texts. A JSON
    /// file of one language (`fr.json`: {"key": "text"}) is that language by its file name.
    /// False (with `err`) when the folder is missing or holds none of those.
    bool LoadFolder(const std::string& dir, std::string* err = nullptr);
    /// One file, merged into the table by its extension.
    bool LoadFile(const std::string& path, std::string* err = nullptr);

    // ---- The grid ----
    std::string Source() const;
    LocCellState StateOf(const std::string& key, const std::string& lang) const;
    LocGrid Grid(const LocGridOptions& options = {}) const;
    LocStats Stats(const std::string& lang) const;
    std::vector<LocStats> AllStats() const;
    /// Texts stored for a language code the table doesn't list: (key, language).
    std::vector<std::pair<std::string, std::string>> ExtraTexts() const;

    // ---- CSV ----
    std::string ToCsv(const LocCsvOptions& options = {}) const;
    LocImportResult ImportCsv(const std::string& csv, const LocImportOptions& options = {});

    // ---- JSON ----
    /// The whole table: {"okayloc": 1, "default": "en", "languages": [...], "keys": {"menu.start": {"note": "", "text": {"en": "Start"}}}}.
    std::string ToJson() const;
    /// One language as {"key": "text"}, keys with a text only.
    std::string ToJsonFlat(const std::string& lang) const;
    /// Read either form. A flat object (or nested objects, whose keys join with ".": {"coins": {"one": "..."}}
    /// reads as `coins.one`) needs `options.language`.
    LocImportResult ImportJson(const std::string& json, const LocImportOptions& options = {});

    // ---- Files ----
    bool SaveCsv(const std::string& path, const LocCsvOptions& options = {}) const;
    bool SaveJson(const std::string& path) const;
    bool SaveOkayloc(const std::string& path) const { return table.Save(path); }

    /// Keys that spell a plural out as `base.one`, `base.other`...
    std::vector<LocPluralGroup> PluralGroups() const { return LocPluralGroups(table); }
};

/// Merge `from` into `into`: languages and keys are added, non-empty texts overwrite, a note
/// is taken when the key had none. The default language is kept, or taken from `from` when
/// `into` had none.
void LocMerge(StringTable& into, const StringTable& from);
/// What changes when `after` replaces `before` (texts, keys, languages, notes).
LocDiff LocDiffTables(const StringTable& before, const StringTable& after);

} // namespace okay
