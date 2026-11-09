#pragma once
// Localization tools: checking a string table before it ships (docs/localization_tools.md).
//
//  * Messages: LocParseMessage splits a text into literal runs, {0} / {name}
//    placeholders and {n, plural, ...} blocks, the same grammar Localization::Format reads.
//  * LocCheck walks a StringTable and reports missing texts, placeholder sets that
//    differ between languages, plural forms a language needs, whitespace that
//    differs from the source and text that grows too long for the UI.
//  * LocMissingReport lists the keys each language still has to translate.
//  * LocFontCoverage tells which characters of a language's texts no configured font draws.
//
// Pure functions over data: nothing here changes the table or the Localization service.
#include "okay/Core/Localization.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace okay {

class TtfFont;

// ---- Messages ------------------------------------------------------------------------
/// One piece of a message. Text keeps its characters as written (a doubled brace stays
/// doubled), Arg is a `{0}` / `{name}` placeholder, Plural is `{n, plural, one {...} other {...}}`.
struct LocNode {
    enum class Kind { Text, Arg, Plural };
    Kind kind = Kind::Text;
    std::string text;   ///< Text: the characters; Arg: the placeholder as written ("{name}")
    std::string name;   ///< Arg / Plural: the argument's name ("0", "name", "count")
    /// Plural: each form's selector ("one", "=0", "other") and its text.
    std::vector<std::pair<std::string, std::vector<LocNode>>> forms;
};

/// Split `message` into nodes. False (with `err`) when braces don't match; `out` still holds
/// what could be read, so a checker can go on.
bool LocParseMessage(const std::string& message, std::vector<LocNode>& out, std::string* err = nullptr);
/// The argument names a message uses ("0", "name", "count"), inside plural forms too.
std::set<std::string> LocPlaceholders(const std::string& message);
/// The plural selectors a message's first-level plural blocks give: "one", "other", "=0"...
/// (one entry per block, in order).
std::vector<std::set<std::string>> LocPluralSelectors(const std::string& message);
/// How wide a message draws, in character cells: wide (CJK, full width) characters count 2,
/// combining marks 0, an argument 3, a plural block its longest form, a multi-line text its
/// widest line. For judging whether a translation fits the space made for the source.
int LocMessageWidth(const std::string& message);
/// True when the message has a letter outside its placeholders (any character from U+0080
/// up counts as one): something a translator changes. "123", "-" and "{name}%" have none.
bool LocHasLetters(const std::string& text);

/// The plural forms a language needs, in order ("zero", "one", "two", "few", "many",
/// "other"; "other" always): a built-in table for common languages plus every form the
/// language's installed Localization rule produces.
std::vector<std::string> LocRequiredPlurals(const std::string& lang);

/// Keys that spell a plural out as separate keys: `coins.one`, `coins.other`, `coins.few`.
/// A group is the base name (`coins`) and each form's key; only bases with at least two
/// forms count, so a lone `button.other` isn't taken for one.
struct LocPluralGroup {
    std::string base;
    std::map<std::string, std::string> forms;   ///< "one" -> "coins.one"
};
std::vector<LocPluralGroup> LocPluralGroups(const StringTable& table);

// ---- Checking a table ----------------------------------------------------------------
enum class LocSeverity { Info, Warning, Error };
const char* LocSeverityName(LocSeverity s);

enum class LocIssueKind {
    MissingText,           ///< a key has no text in a language
    PlaceholderMismatch,   ///< the {0} / {name} set differs from the source text's
    UnbalancedBraces,      ///< a { or } that doesn't pair up
    PluralFormMissing,     ///< a {n, plural, ...} block lacks a form the language needs
    PluralKeyMissing,      ///< a key.one / key.few... group lacks a form the language needs
    TrailingWhitespace,    ///< leading or trailing whitespace that differs from the source text
    LengthGrowth,          ///< much longer than the source text: may not fit its UI
    SameAsSource,          ///< identical to the source text: probably untranslated
    ExtraText,             ///< text for a language the table doesn't list, or for a key the source has no text for
};
const char* LocIssueKindName(LocIssueKind k);

struct LocIssue {
    LocIssueKind kind = LocIssueKind::MissingText;
    LocSeverity severity = LocSeverity::Warning;
    std::string key;
    std::string language;
    std::string message;   ///< a sentence for a translator or a log
};

struct LocCheckOptions {
    std::string source;                 ///< the language texts are compared with ("" = the table's default)
    std::vector<std::string> languages; ///< the languages to check (empty = all)
    bool missing = true;                ///< report keys without text
    bool placeholders = true;           ///< report unbalanced braces and placeholder sets that differ
    bool plurals = true;                ///< report plural forms a language lacks
    bool whitespace = true;             ///< report leading / trailing whitespace that differs from the source
    bool growth = true;                 ///< report texts much longer than the source
    bool sameAsSource = false;          ///< report texts identical to the source (untranslated?)
    /// A translation wider than the source's width times this is reported ...
    float growthWarn = 1.5f;
    /// ... unless it is within this many cells of the source's width (short labels grow a lot).
    int growthSlack = 6;
    std::set<std::string> sameOk;       ///< keys that may be identical to the source ("OK", names)
};

struct LocReport {
    std::vector<LocIssue> issues;
    int keys = 0;                       ///< keys in the table
    std::vector<std::string> languages; ///< languages checked

    int Count(LocIssueKind k) const;
    int Count(LocSeverity s) const;
    bool HasErrors() const { return Count(LocSeverity::Error) > 0; }
    std::vector<LocIssue> ForLanguage(const std::string& lang) const;
    std::vector<LocIssue> ForKey(const std::string& key) const;
    /// One line per issue: "error  fr  menu.start  placeholder {0} is missing", then a summary line.
    std::string ToText() const;
    /// "severity,kind,language,key,message" rows (CSV with a byte order mark, CRLF).
    std::string ToCsv() const;
};

LocReport LocCheck(const StringTable& table, const LocCheckOptions& options = {});

// ---- Missing keys --------------------------------------------------------------------
struct LocMissing {
    std::string language;
    std::vector<std::string> keys;   ///< keys with no text in this language, sorted
    int total = 0;                   ///< keys in the table
    float completion = 1.0f;         ///< share of the keys with text
};
/// What each language (empty = all but the source) still lacks.
std::vector<LocMissing> LocMissingReport(const StringTable& table, const std::vector<std::string>& languages = {});
/// "fr: 3 of 120 keys missing (97.5% done)" followed by the keys, one per line.
std::string LocMissingToText(const std::vector<LocMissing>& report, bool listKeys = true);

// ---- Font coverage -------------------------------------------------------------------
/// One character a font chain has trouble with.
struct LocGlyphGap {
    std::uint32_t codePoint = 0;
    std::string utf8;
    int count = 0;           ///< occurrences in the language's texts
    std::string firstKey;    ///< a key that uses it
};

struct LocFontOptions {
    /// Where relative language fonts are looked up when they are not found as written
    /// (the folder the table was loaded from).
    std::string baseDir;
    /// The fonts to test, first choice first. Empty = the language's own font (when the
    /// table names one that loads), the built-in UI font, then the installed fallbacks.
    std::vector<const TtfFont*> fonts;
    bool useSystemFonts = true;         ///< with the default chain: include the installed fallback fonts
};

struct LocFontLanguage {
    std::string language;
    std::string primary;                   ///< the first font tried ("" = none could be loaded)
    int charactersUsed = 0;                ///< distinct visible characters in the language's texts
    std::vector<LocGlyphGap> missing;      ///< drawn by no font in the chain
    std::vector<LocGlyphGap> fallbacks;    ///< not in the first font, but some later font has them
    /// "key" of every text no single font draws completely (the engine draws a text with one font).
    std::vector<std::string> undrawableTexts;
    std::vector<std::string> warnings;     ///< "the font for ja couldn't be loaded: ..."
};

struct LocFontReport {
    std::vector<LocFontLanguage> languages;
    bool Clean() const;                    ///< no missing character and no undrawable text in any language
    std::string ToText() const;
};

/// Which characters of each language's texts the configured fonts can't draw. A character
/// counts when it is visible (not whitespace, a control or a zero-width mark).
LocFontReport LocFontCoverage(const StringTable& table, const LocFontOptions& options = {});

} // namespace okay
