#pragma once
// Localization: a project's player-facing strings in several languages.
//
// A StringTable holds keys x languages (with a note per key for translators)
// and reads / writes the `.okayloc` text file and CSV for translators. The
// Localization service holds the table a game uses and the current language:
// Get(key) falls back to the default language, then to the key itself (each
// miss reported once), Localize(key, args...) fills `{0}` / `{name}` and plural
// forms, and every language or table change bumps Version() so text that shows
// a key refreshes. See docs/localization.md.
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// One language a table carries: its code ("en", "fr", "pt-BR"), the name a
/// language menu shows ("Français"), and optionally a font that has its glyphs.
struct LocLanguage {
    std::string code;
    std::string name;
    std::string font;   ///< .ttf / .otf for this language's text ("" = the font each text uses)
};

/// One key's note for translators and its text per language code.
struct LocEntry {
    std::string note;
    std::map<std::string, std::string> text;   ///< language code -> text; empty or absent = untranslated
};

/// A project's strings: keys x languages, with a default language that fills in
/// for missing translations. Pure data plus its two text formats.
class StringTable {
public:
    std::vector<LocLanguage> languages;
    std::string defaultLanguage;   ///< the fallback ("" = the first language)

    // ---- Languages ----
    const LocLanguage* FindLanguage(const std::string& code) const;
    LocLanguage* FindLanguage(const std::string& code);
    /// The language with this code, added (named `name`, else the code) when missing.
    LocLanguage& AddLanguage(const std::string& code, const std::string& name = "");
    /// Remove a language and its texts. False when there is none.
    bool RemoveLanguage(const std::string& code);
    /// The fallback language: `defaultLanguage` when the table has it, else the first one.
    std::string DefaultLanguage() const;

    // ---- Keys ----
    const std::map<std::string, LocEntry>& Entries() const { return m_entries; }
    const LocEntry* Find(const std::string& key) const;
    LocEntry* Find(const std::string& key);
    bool Has(const std::string& key) const { return Find(key) != nullptr; }
    /// The entry for `key`, added when missing.
    LocEntry& Add(const std::string& key);
    bool Remove(const std::string& key);
    /// Give a key a new name, keeping its texts. False when `to` exists or `from` doesn't.
    bool Rename(const std::string& from, const std::string& to);
    /// Set one text (adds the key and the language when missing; "" clears it).
    void Set(const std::string& key, const std::string& lang, const std::string& text);
    /// The text of `key` in `lang`, or null when it has none.
    const std::string* Text(const std::string& key, const std::string& lang) const;
    /// Share of the keys that have text in `lang` (1 for a table without keys).
    float Completion(const std::string& lang) const;
    std::size_t Size() const { return m_entries.size(); }
    bool Empty() const { return m_entries.empty() && languages.empty(); }
    void Clear() { m_entries.clear(); languages.clear(); defaultLanguage.clear(); }

    // ---- The .okayloc text format (one record per line, see Write) ----
    bool Parse(const std::string& text, std::string* err = nullptr);
    std::string Write() const;
    bool Load(const std::string& path, std::string* err = nullptr);
    bool Save(const std::string& path) const;

    // ---- CSV for translators: "key,note,<lang>,<lang>..." then one row per key ----
    /// UTF-8 with a BOM, CRLF rows, fields quoted when needed. `langs` empty = every language.
    std::string ExportCsv(const std::vector<std::string>& langs = {}) const;
    /// Merge a CSV in (comma, semicolon or tab separated; with or without a BOM): new
    /// keys and languages are added, non-empty cells overwrite. `rows` receives the
    /// rows read. False (with `err`) when there is no key column.
    bool ImportCsv(const std::string& csv, std::string* err = nullptr, int* rows = nullptr);

private:
    std::map<std::string, LocEntry> m_entries;
};

/// An argument for Localize: positional ({0}, {1}...) and, when named, also {name}.
struct LocArg {
    std::string name;
    std::string text;
    double number = 0.0;
    bool isNumber = false;

    LocArg(const std::string& v) : text(v) {}
    LocArg(const char* v) : text(v ? v : "") {}
    // Integers keep their exact digits (a double would round a 64-bit id).
    LocArg(int v) : text(std::to_string(v)), number((double)v), isNumber(true) {}
    LocArg(long v) : text(std::to_string(v)), number((double)v), isNumber(true) {}
    LocArg(long long v) : text(std::to_string(v)), number((double)v), isNumber(true) {}
    LocArg(unsigned v) : text(std::to_string(v)), number((double)v), isNumber(true) {}
    LocArg(unsigned long v) : text(std::to_string(v)), number((double)v), isNumber(true) {}
    LocArg(unsigned long long v) : text(std::to_string(v)), number((double)v), isNumber(true) {}
    LocArg(float v) : LocArg((double)v) {}
    LocArg(double v);
    /// A named argument: Localize("greet", LocArg::Named("player", name)).
    template <class T> static LocArg Named(std::string n, T&& v) {
        LocArg a(std::forward<T>(v));
        a.name = std::move(n);
        return a;
    }
};

class Localization {
public:
    /// The pseudo-localized preview language: the default language's text with
    /// accented letters and about 30% more length, to spot truncation and strings
    /// that bypass the tables. Always available; never listed in Languages().
    static const char* const kPseudo;   // "qps-ploc"

    // ---- The table ----
    /// The strings in use. After editing it, call TableChanged().
    static StringTable& Table();
    /// Replace the table with a file's. Keeps the current language when the new
    /// table has it, else switches to its default.
    static bool LoadTable(const std::string& path, std::string* err = nullptr);
    static void SetTable(const StringTable& table, const std::string& path = "");
    /// The file the table came from ("" when built in code).
    static const std::string& TablePath();
    /// Tell texts that the table changed (refreshes every localized text).
    static void TableChanged();
    /// Back to an empty table in no language, no listeners, nothing reported.
    static void Reset();

    // ---- The language ----
    /// The current language's code (the table's default until one is set).
    static std::string Language();
    /// Switch language. False (nothing changes) when the table has no such
    /// language; kPseudo is always accepted. Listeners hear about a change.
    static bool SetLanguage(const std::string& code);
    /// The table's language codes, in table order.
    static std::vector<std::string> Languages();
    /// A language's display name (the code when it has none).
    static std::string LanguageName(const std::string& code);
    /// The font the current language asks for, resolved next to the table when
    /// the path is relative ("" = none).
    static std::string LanguageFont();
    /// The table language that best matches `preferred` (the OS locales, best
    /// first: "fr-CA", "fr_FR.UTF-8", "en"): an exact match, else the same base
    /// language (the one without a region first). Chinese keeps its script: zh-TW,
    /// zh-HK and zh-Hant-TW pick a Traditional table language (zh-Hant), zh-CN and zh-SG
    /// a Simplified one (zh-Hans). The default language when nothing matches.
    static std::string PickLanguage(const std::vector<std::string>& preferred);

    // ---- Player settings ----
    /// The language a player chose before (saved in Prefs), else the best match
    /// for the OS locales, else the default. Call after Prefs::Load and LoadTable.
    static void LoadSettings(const std::vector<std::string>& osLocales = {});
    /// Remember the current language in Prefs (Prefs::Save writes it to disk).
    static void SaveSettings();
    static const char* const kPrefsKey;   // "loc.language"

    // ---- Lookups ----
    /// The key's text: the current language, else the default language, else the
    /// key itself. Each miss is reported once (a warning in the log).
    static std::string Get(const std::string& key);
    /// The key's text in the current or the default language; false (nothing
    /// reported) when the table has no text for it.
    static bool TryGet(const std::string& key, std::string& out);
    /// The key's text, or `fallback` when the table has none (nothing reported):
    /// for text that is localized once a project adds its key (dialogue, quests).
    static std::string GetOr(const std::string& key, const std::string& fallback);
    /// A name as part of a key: lower case, letters, digits, '_' and '-' kept,
    /// anything else '_' ("Old Smith" -> "old_smith"). Letters of other scripts stay as
    /// they are (UTF-8), so names in Cyrillic or Chinese give different keys.
    static std::string KeyPart(const std::string& name);
    /// Get(key) with its {0} / {name} / plural placeholders filled in.
    static std::string LocalizeArgs(const std::string& key, const std::vector<LocArg>& args);
    template <class... A> static std::string Localize(const std::string& key, A&&... args) {
        return LocalizeArgs(key, std::vector<LocArg>{LocArg(std::forward<A>(args))...});
    }
    /// Fill a pattern's placeholders: {0} or {name} is an argument's text,
    /// {0, plural, =0 {none} one {# coin} other {# coins}} picks a form by the
    /// argument's number (# is the number), {who, select, male {...} other {...}} picks a
    /// form by the argument's text, {0, number} writes the number the language's way
    /// ("1,234,567.5"; styles `integer`, `percent` and a pattern like `#,##0.00`),
    /// {0, currency, USD} writes money, {{ and }} are literal braces. A
    /// placeholder without an argument stays as written. `lang` picks the plural
    /// rule and the number format ("" = the current language).
    static std::string Format(const std::string& pattern, const std::vector<LocArg>& args,
                              const std::string& lang = "");

    // ---- Plurals ----
    enum class PluralForm { Zero, One, Two, Few, Many, Other };
    static const char* PluralName(PluralForm f);
    /// The form a number takes in a language (CLDR categories, on the absolute
    /// value). Built in: en, de, nl, sv, it... (one / other), fr and pt (0 and 1
    /// are "one"; millions "many"), es (millions "many"), ru and uk (one / few /
    /// many), pl (one / few / many), cs and sk (one / few / many for fractions),
    /// ar (zero / one / two / few / many / other), hr / sr / bs (one / few / other by the
    /// last digits), sl, lt, lv, ro, he, cy, ga, mt, is, mk, da, hi / bn / fa / gu / zu
    /// (0 is "one"), and no plural (always other) for ja, zh, ko, vi, th, id, ms, lo, my,
    /// km, jv. Other languages use one / other. SetPluralRule overrides any of them.
    static PluralForm Plural(const std::string& lang, double n);
    /// The plural form of a string-table key: looks up `key.one`, `key.few`...
    /// (the form `count` takes in the current language) in the current language,
    /// then `key.other`, then the same two in the default language, then `key` itself
    /// as a {0, plural, ...} pattern. {count} and {0} are the number; `args` follow
    /// as {1}, {2} and by name. A key with none of these is reported and shown as is.
    static std::string PluralText(const std::string& key, double count, const std::vector<LocArg>& args = {});
    using PluralRule = std::function<PluralForm(double n)>;
    /// Install the rule for a language with more forms (Russian, Polish, Arabic...).
    /// A rule for "pt" also covers "pt-BR". Null removes it.
    static void SetPluralRule(const std::string& lang, PluralRule rule);

    // ---- Locale-aware formatting ----
    // `lang` is a language code ("fr", "pt-BR", "en_GB.UTF-8"; "" = the current
    // language). Unknown languages format like English.
    /// 1234567.891 with `decimals` places: "1,234,567.89" (en), "1.234.567,89" (de),
    /// "1 234 567,89" (fr, narrow no-break space group). Negative numbers get "-".
    static std::string FormatNumber(double value, int decimals = 0, const std::string& lang = "");
    /// Money with its symbol placed as the language does it: "$1,234.50" (en),
    /// "1.234,50 €" (de), "R$ 1.234,50" (pt-BR). `currency` is an
    /// ISO code (USD, EUR, GBP, JPY, CNY, KRW, INR, RUB, PLN, BRL...); an unknown code is
    /// shown as the code. Decimals follow the currency (JPY and KRW none).
    static std::string FormatCurrency(double amount, const std::string& currency, const std::string& lang = "");
    /// A fraction as a percentage: FormatPercent(0.256, 1) = "25.6%" ("25,6 %" in fr, de).
    static std::string FormatPercent(double fraction, int decimals = 0, const std::string& lang = "");
    enum class DateStyle { Short, Long };
    /// A calendar date: Short "3/5/2025" (en), "05.03.2025" (de), "2025/3/5" (ja);
    /// Long "March 5, 2025", "5 mars 2025", "2025年3月5日". An invalid date gives ISO text.
    static std::string FormatDate(int year, int month, int day, DateStyle style = DateStyle::Short, const std::string& lang = "");
    /// A time of day: "3:05 PM" (en-US), "15:05" elsewhere; 12 hour clock follows the language.
    static std::string FormatTime(int hour, int minute, const std::string& lang = "");

    // ---- Changes ----
    /// Bumps on every language and table change: a text compares it with the
    /// value it last applied.
    static std::uint32_t Version();
    using Listener = std::function<void(const std::string& language)>;
    /// Hear about language changes (called inside SetLanguage). Returns an id.
    static int AddListener(Listener fn);
    static void RemoveListener(int id);

    // ---- Missing keys ----
    /// The misses reported so far ("fr: menu.title" for a missing translation,
    /// "?: menu.title" for an unknown key), in order. Each is reported once.
    static const std::vector<std::string>& Reported();

    /// The pseudo-localized form of `text` (placeholders kept as they are).
    static std::string Pseudo(const std::string& text);
};

/// Localization::Localize, for short call sites.
template <class... A> std::string Localize(const std::string& key, A&&... args) {
    return Localization::Localize(key, std::forward<A>(args)...);
}

/// The "Localized" option of a text field: a string-table key whose text, in the
/// current language, replaces the field's literal. An empty key leaves the field
/// as typed.
struct LocalizedText {
    std::string key;
    /// Write the key's text into `field` when the key, the language or the table
    /// changed since the last call. True when it wrote.
    bool Apply(std::string& field) {
        if (key.empty()) { m_version = 0; return false; }
        const std::uint32_t v = Localization::Version();
        if (v == m_version && key == m_applied) return false;
        field = Localization::Get(key);
        m_version = v;
        m_applied = key;
        return true;
    }
private:
    std::uint32_t m_version = 0;   // Localization::Version() last applied (0 = never)
    std::string m_applied;         // the key last applied
};

} // namespace okay
