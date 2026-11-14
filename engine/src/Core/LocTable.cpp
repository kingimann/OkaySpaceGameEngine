#include "okay/Core/LocTable.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/Utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace okay {

namespace {

namespace fs = std::filesystem;
const std::size_t npos = std::string::npos;

constexpr std::size_t kMaxFileBytes = 64u * 1024u * 1024u;   // as for the .okayloc file
constexpr std::size_t kMaxColumns = 4096;
constexpr int kMaxJsonDepth = 64;
constexpr std::size_t kMaxWarnings = 30;

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string LowerAscii(std::string s) {
    for (char& c : s) if ((unsigned char)c < 0x80) c = (char)std::tolower((unsigned char)c);
    return s;
}

void Warn(std::vector<std::string>& w, const std::string& msg) {
    if (w.size() < kMaxWarnings) w.push_back(msg);
    else if (w.size() == kMaxWarnings) w.push_back("... and more");
}

bool ReadFile(const std::string& path, std::string& out, std::string* err) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) { if (err) *err = "can't open " + path; return false; }
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size > (std::streamoff)kMaxFileBytes) { if (err) *err = path + " is too large"; return false; }
    f.seekg(0);
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Write beside the target and swap it in, so a crash leaves the old file (the shared
// savefmt::AtomicWriteChecked, with a UTF-8 path).
bool WriteFile(const std::string& path, const std::string& data) {
    savefmt::WriteOptions opt;
    opt.sync = true;
    opt.utf8Path = true;
    return savefmt::AtomicWriteChecked(path, data, nullptr, opt);
}

// ---- Text encodings ---------------------------------------------------------------------
// UTF-8 without a byte order mark: UTF-16 (either order) converted, a UTF-8 mark dropped.
std::string ToUtf8(const std::string& in) {
    const auto b = [&](std::size_t i) { return (unsigned char)in[i]; };
    if (in.size() >= 2 && ((b(0) == 0xFF && b(1) == 0xFE) || (b(0) == 0xFE && b(1) == 0xFF))) {
        const bool little = b(0) == 0xFF;
        std::string out;
        out.reserve(in.size() / 2);
        for (std::size_t i = 2; i + 1 < in.size(); i += 2) {
            std::uint32_t u = little ? (b(i) | (b(i + 1) << 8)) : ((b(i) << 8) | b(i + 1));
            if (u >= 0xD800 && u <= 0xDBFF && i + 3 < in.size()) {
                const std::uint32_t lo = little ? (b(i + 2) | (b(i + 3) << 8)) : ((b(i + 2) << 8) | b(i + 3));
                if (lo >= 0xDC00 && lo <= 0xDFFF) { u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
            }
            utf8::Append(out, u);
        }
        return out;
    }
    if (in.compare(0, 3, "\xEF\xBB\xBF") == 0) return in.substr(3);
    return in;
}

// ---- A small JSON reader and writer --------------------------------------------------------
struct JVal {
    enum class T { Null, Bool, Num, Str, Arr, Obj } t = T::Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;
    const JVal* Get(const std::string& k) const {
        for (const auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

struct JsonReader {
    const std::string& s;
    std::size_t i = 0;
    std::string err;
    explicit JsonReader(const std::string& text) : s(text) {}

    bool Fail(const std::string& why) {
        if (err.empty()) err = why + " at byte " + std::to_string(i);
        return false;
    }
    void Ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool Hex4(std::uint32_t& v) {
        if (i + 4 > s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (std::uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (std::uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (std::uint32_t)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool String(std::string& out) {
        ++i;   // the opening quote
        out.clear();
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            const char e = s[i++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    std::uint32_t u = 0;
                    if (!Hex4(u)) return Fail("bad \\u escape");
                    if (u >= 0xD800 && u <= 0xDBFF && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        const std::size_t save = i;
                        i += 2;
                        std::uint32_t lo = 0;
                        if (Hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                        else i = save;
                    }
                    utf8::Append(out, u);
                    break;
                }
                default: out += e;   // \" \\ \/
            }
        }
        return Fail("a string never closes");
    }
    bool Value(JVal& v, int depth) {
        if (depth > kMaxJsonDepth) return Fail("nested too deeply");
        Ws();
        if (i >= s.size()) return Fail("the text ends early");
        const char c = s[i];
        if (c == '"') { v.t = JVal::T::Str; return String(v.s); }
        if (c == '{') {
            v.t = JVal::T::Obj;
            ++i; Ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) {
                Ws();
                if (i >= s.size() || s[i] != '"') return Fail("expected a key");
                std::string k;
                if (!String(k)) return false;
                Ws();
                if (i >= s.size() || s[i] != ':') return Fail("expected ':'");
                ++i;
                JVal child;
                if (!Value(child, depth + 1)) return false;
                v.obj.emplace_back(std::move(k), std::move(child));
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return Fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.t = JVal::T::Arr;
            ++i; Ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) {
                JVal child;
                if (!Value(child, depth + 1)) return false;
                v.arr.push_back(std::move(child));
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return Fail("expected ',' or ']'");
            }
        }
        if (s.compare(i, 4, "true") == 0) { v.t = JVal::T::Bool; v.b = true; i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { v.t = JVal::T::Bool; v.b = false; i += 5; return true; }
        if (s.compare(i, 4, "null") == 0) { v.t = JVal::T::Null; i += 4; return true; }
        const std::size_t start = i;
        while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) ++i;
        if (i == start) return Fail("unexpected character");
        v.t = JVal::T::Num;
        v.n = std::strtod(s.substr(start, i - start).c_str(), nullptr);
        v.s = s.substr(start, i - start);
        return true;
    }
};

std::string JsonQuote(const std::string& v) {
    std::string out = "\"";
    for (char ch : v) {
        const unsigned char c = (unsigned char)ch;
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
                else out += ch;
        }
    }
    return out + "\"";
}

// The text a JSON scalar stands for in a table.
bool ScalarText(const JVal& v, std::string& out) {
    switch (v.t) {
        case JVal::T::Str: out = v.s; return true;
        case JVal::T::Num: out = v.s; return true;
        case JVal::T::Bool: out = v.b ? "true" : "false"; return true;
        default: return false;
    }
}

// ---- Writing a cell ------------------------------------------------------------------------
struct Importer {
    StringTable& work;
    const LocImportOptions& opt;
    std::vector<std::string>& warnings;
    std::set<std::string> skippedLanguages;

    bool LanguageOk(const std::string& lang) {
        if (work.FindLanguage(lang)) return true;
        if (opt.addLanguages) { work.AddLanguage(lang); return true; }
        if (skippedLanguages.insert(lang).second) Warn(warnings, "skipped the language \"" + lang + "\": the table doesn't have it");
        return false;
    }
    void Cell(const std::string& key, const std::string& lang, const std::string& text) {
        const bool has = work.Text(key, lang) != nullptr;
        switch (opt.mode) {
            case LocImportOptions::Mode::Overwrite: if (!text.empty()) work.Set(key, lang, text); break;
            case LocImportOptions::Mode::FillEmpty: if (!text.empty() && !has) work.Set(key, lang, text); break;
            case LocImportOptions::Mode::Replace: if (!text.empty() || has) work.Set(key, lang, text); break;
        }
    }
    void Note(const std::string& key, const std::string& note) {
        LocEntry* e = work.Find(key);
        if (!e) return;
        switch (opt.mode) {
            case LocImportOptions::Mode::Overwrite: if (!note.empty()) e->note = note; break;
            case LocImportOptions::Mode::FillEmpty: if (!note.empty() && e->note.empty()) e->note = note; break;
            case LocImportOptions::Mode::Replace: e->note = note; break;
        }
    }
    // The key's entry exists afterwards (true) unless keys may not be added.
    bool Key(const std::string& key) {
        if (work.Has(key)) return true;
        if (!opt.addKeys) return false;
        work.Add(key);
        return true;
    }
};

// Flat or nested objects: {"menu": {"start": "Start"}} reads as menu.start.
void FlattenJson(const JVal& v, const std::string& prefix, std::vector<std::pair<std::string, std::string>>& out,
                 std::vector<std::string>& warnings, int depth) {
    for (const auto& kv : v.obj) {
        const std::string key = prefix.empty() ? kv.first : prefix + "." + kv.first;
        std::string text;
        if (kv.second.t == JVal::T::Obj && depth < kMaxJsonDepth) FlattenJson(kv.second, key, out, warnings, depth + 1);
        else if (ScalarText(kv.second, text)) out.emplace_back(key, std::move(text));
        else Warn(warnings, "skipped \"" + key + "\": not text");
    }
}

LocImportResult Finish(LocTable& lt, StringTable&& work, LocImportResult&& r, bool dryRun) {
    r.diff = LocDiffTables(lt.table, work);
    if (!dryRun) lt.table = std::move(work);
    r.ok = true;
    return std::move(r);
}

int Rank(LocCellState s) {
    switch (s) {
        case LocCellState::PlaceholderMismatch: return 4;
        case LocCellState::Missing: return 3;
        case LocCellState::Extra: return 2;
        case LocCellState::SameAsSource: return 1;
        case LocCellState::Translated: break;
    }
    return 0;
}

} // namespace

// =====================================================================================
// CSV
// =====================================================================================
std::string LocCsvField(const std::string& v) {
    const bool quote = v.find_first_of(",;\t\"\r\n") != npos || (!v.empty() && (v.front() == ' ' || v.back() == ' '));
    if (!quote) return v;
    std::string out = "\"";
    for (char c : v) { if (c == '"') out += '"'; out += c; }
    return out + "\"";
}

bool LocParseCsv(const std::string& textIn, std::vector<std::vector<std::string>>& rows, std::string* err, char delimiter) {
    rows.clear();
    if (textIn.size() > kMaxFileBytes) { if (err) *err = "the file is too large"; return false; }
    const std::string s = ToUtf8(textIn);

    if (delimiter == 0) {   // the delimiter the first row has most of, outside quotes
        int comma = 0, semi = 0, tab = 0;
        bool quoted = false;
        for (char c : s) {
            if (c == '"') quoted = !quoted;
            else if (!quoted && (c == '\n' || c == '\r')) break;
            else if (!quoted && c == ',') ++comma;
            else if (!quoted && c == ';') ++semi;
            else if (!quoted && c == '\t') ++tab;
        }
        delimiter = (semi > comma && semi >= tab) ? ';' : (tab > comma && tab > semi) ? '\t' : ',';
    }

    std::vector<std::string> row;
    std::string field;
    bool quoted = false, started = false;
    auto endField = [&] { row.push_back(std::move(field)); field.clear(); started = false; };
    auto endRow = [&] {
        endField();
        if (!(row.size() == 1 && row[0].empty())) rows.push_back(std::move(row));   // a blank line is no row
        row.clear();
    };
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (row.size() > kMaxColumns) { if (err) *err = "too many columns (at most " + std::to_string(kMaxColumns) + ")"; return false; }
        if (quoted) {
            if (c == '"') {
                if (i + 1 < s.size() && s[i + 1] == '"') { field += '"'; ++i; }
                else quoted = false;
            } else if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') {
                // a CRLF inside a cell reads as a plain line break
            } else {
                field += c;
            }
            continue;
        }
        if (c == '"' && !started) { quoted = true; started = true; }
        else if (c == delimiter) endField();
        else if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ++i;
            endRow();
        } else { field += c; started = true; }
    }
    if (quoted) { if (err) *err = "a quoted field never closes"; return false; }
    if (started || !field.empty() || !row.empty()) endRow();
    return true;
}

// =====================================================================================
// Merging and diffing
// =====================================================================================
void LocMerge(StringTable& into, const StringTable& from) {
    for (const LocLanguage& l : from.languages) {
        const bool had = into.FindLanguage(l.code) != nullptr;
        LocLanguage& mine = into.AddLanguage(l.code, l.name);
        if (had && (mine.name.empty() || mine.name == mine.code) && !l.name.empty()) mine.name = l.name;
        if (mine.font.empty()) mine.font = l.font;
    }
    if (into.defaultLanguage.empty()) into.defaultLanguage = from.defaultLanguage;
    for (const auto& kv : from.Entries()) {
        LocEntry& e = into.Add(kv.first);
        if (e.note.empty()) e.note = kv.second.note;
        for (const auto& t : kv.second.text) if (!t.second.empty()) into.Set(kv.first, t.first, t.second);
    }
}

LocDiff LocDiffTables(const StringTable& before, const StringTable& after) {
    LocDiff d;
    for (const LocLanguage& l : after.languages) if (!before.FindLanguage(l.code)) d.addedLanguages.push_back(l.code);
    for (const auto& kv : after.Entries()) {
        const LocEntry* old = before.Find(kv.first);
        if (!old) d.addedKeys.push_back(kv.first);
        else if (old->note != kv.second.note) ++d.notesChanged;
        std::set<std::string> langs;
        for (const auto& t : kv.second.text) langs.insert(t.first);
        if (old) for (const auto& t : old->text) langs.insert(t.first);
        for (const std::string& lang : langs) {
            const std::string* was = before.Text(kv.first, lang);
            const std::string* now = after.Text(kv.first, lang);
            if (!was && now) d.filled.emplace_back(kv.first, lang);
            else if (was && now && *was != *now) d.changed.emplace_back(kv.first, lang);
            else if (was && !now) d.cleared.emplace_back(kv.first, lang);
        }
    }
    return d;
}

// =====================================================================================
// LocGrid, states
// =====================================================================================
const char* LocCellStateName(LocCellState s) {
    switch (s) {
        case LocCellState::Translated: return "translated";
        case LocCellState::Missing: return "missing";
        case LocCellState::SameAsSource: return "same as source";
        case LocCellState::PlaceholderMismatch: return "placeholder mismatch";
        case LocCellState::Extra: break;
    }
    return "extra";
}

int LocGrid::ColumnOf(const std::string& lang) const {
    for (std::size_t i = 0; i < languages.size(); ++i) if (languages[i] == lang) return (int)i;
    return -1;
}

std::string LocTable::Source() const {
    if (!sourceLanguage.empty() && table.FindLanguage(sourceLanguage)) return sourceLanguage;
    return table.DefaultLanguage();
}

LocCellState LocTable::StateOf(const std::string& key, const std::string& lang) const {
    const LocEntry* e = table.Find(key);
    if (!e) return LocCellState::Missing;
    auto it = e->text.find(lang);
    const bool has = it != e->text.end() && !it->second.empty();
    if (!has) return LocCellState::Missing;
    if (!table.FindLanguage(lang)) return LocCellState::Extra;
    const std::string src = Source();
    if (lang == src) return LocCellState::Translated;
    const std::string* source = table.Text(key, src);
    if (!source) return LocCellState::Extra;
    if (LocPlaceholders(*source) != LocPlaceholders(it->second)) return LocCellState::PlaceholderMismatch;
    if (it->second == *source && !sameAsSourceOk.count(key) && LocHasLetters(*source)) return LocCellState::SameAsSource;
    return LocCellState::Translated;
}

LocGrid LocTable::Grid(const LocGridOptions& o) const {
    LocGrid g;
    if (o.languages.empty()) for (const auto& l : table.languages) g.languages.push_back(l.code);
    else g.languages = o.languages;
    const std::string needle = LowerAscii(o.search);

    for (const auto& kv : table.Entries()) {
        const std::string& key = kv.first;
        if (!o.keyPrefix.empty() && key.compare(0, o.keyPrefix.size(), o.keyPrefix) != 0) continue;
        LocGridRow row;
        row.key = key;
        row.note = kv.second.note;
        bool hit = needle.empty() || LowerAscii(key).find(needle) != npos || LowerAscii(row.note).find(needle) != npos;
        bool stateHit = o.stateMask == 0;
        for (const std::string& lang : g.languages) {
            LocCell c;
            auto it = kv.second.text.find(lang);
            if (it != kv.second.text.end()) c.text = it->second;
            c.state = StateOf(key, lang);
            if (!hit && LowerAscii(c.text).find(needle) != npos) hit = true;
            if (o.stateMask & LocStateBit(c.state)) stateHit = true;
            row.cells.push_back(std::move(c));
        }
        if (hit && stateHit) g.rows.push_back(std::move(row));
    }
    if (o.sortByIssues) {
        auto score = [](const LocGridRow& r) {
            int worst = 0, bad = 0;
            for (const auto& c : r.cells) { worst = std::max(worst, Rank(c.state)); if (c.state != LocCellState::Translated) ++bad; }
            return worst * 1000 + bad;
        };
        std::stable_sort(g.rows.begin(), g.rows.end(), [&](const LocGridRow& a, const LocGridRow& b) { return score(a) > score(b); });
    }
    return g;
}

LocStats LocTable::Stats(const std::string& lang) const {
    LocStats s;
    s.language = lang;
    for (const auto& kv : table.Entries()) {
        ++s.total;
        switch (StateOf(kv.first, lang)) {
            case LocCellState::Translated: ++s.translated; break;
            case LocCellState::Missing: ++s.missing; break;
            case LocCellState::SameAsSource: ++s.sameAsSource; break;
            case LocCellState::PlaceholderMismatch: ++s.placeholderMismatch; break;
            case LocCellState::Extra: ++s.extra; break;
        }
    }
    return s;
}

std::vector<LocStats> LocTable::AllStats() const {
    std::vector<LocStats> out;
    for (const auto& l : table.languages) out.push_back(Stats(l.code));
    return out;
}

std::vector<std::pair<std::string, std::string>> LocTable::ExtraTexts() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& kv : table.Entries())
        for (const auto& t : kv.second.text)
            if (!t.second.empty() && !table.FindLanguage(t.first)) out.emplace_back(kv.first, t.first);
    return out;
}

// =====================================================================================
// CSV import / export
// =====================================================================================
std::string LocTable::ToCsv(const LocCsvOptions& o) const {
    std::vector<std::string> cols = o.languages;
    if (cols.empty()) for (const auto& l : table.languages) cols.push_back(l.code);
    const char delim = o.delimiter == ';' || o.delimiter == '\t' ? o.delimiter : ',';
    const std::string eol = o.crlf ? "\r\n" : "\n";
    auto field = [](const std::string& v) { return LocCsvField(v); };
    std::string out = o.bom ? "\xEF\xBB\xBF" : "";
    out += "key";
    if (o.note) out += std::string(1, delim) + "note";
    for (const auto& c : cols) out += std::string(1, delim) + field(c);
    out += eol;
    for (const auto& kv : table.Entries()) {
        if (!o.missingIn.empty() && table.Text(kv.first, o.missingIn)) continue;
        out += field(kv.first);
        if (o.note) out += std::string(1, delim) + field(kv.second.note);
        for (const auto& c : cols) {
            auto it = kv.second.text.find(c);
            out += std::string(1, delim) + (it == kv.second.text.end() ? std::string() : field(it->second));
        }
        out += eol;
    }
    return out;
}

LocImportResult LocTable::ImportCsv(const std::string& csv, const LocImportOptions& options) {
    LocImportResult r;
    std::vector<std::vector<std::string>> rows;
    if (!LocParseCsv(csv, rows, &r.error)) return r;
    if (rows.empty()) { r.error = "the file is empty"; return r; }

    int keyCol = -1, noteCol = -1;
    std::vector<std::pair<int, std::string>> langCols;
    std::set<std::string> seenCols;
    for (int i = 0; i < (int)rows[0].size(); ++i) {
        const std::string cell = Trim(rows[0][(std::size_t)i]);
        const std::string low = LowerAscii(cell);
        if ((low == "key" || low == "keys" || low == "id") && keyCol < 0) keyCol = i;
        else if (low == "note" || low == "notes" || low == "comment" || low == "comments" || low == "context") noteCol = i;
        else if (!cell.empty()) {
            if (!seenCols.insert(cell).second) Warn(r.warnings, "the column \"" + cell + "\" appears twice; the later one wins");
            langCols.emplace_back(i, cell);
        }
    }
    if (keyCol < 0) { r.error = "no \"key\" column in the header row"; return r; }

    StringTable work = table;
    Importer in{work, options, r.warnings, {}};
    std::vector<std::pair<int, std::string>> usable;
    for (const auto& lc : langCols) if (in.LanguageOk(lc.second)) usable.push_back(lc);

    std::set<std::string> seen;
    int shortRows = 0;
    for (std::size_t ri = 1; ri < rows.size(); ++ri) {
        const auto& row = rows[ri];
        const std::string key = keyCol < (int)row.size() ? Trim(row[(std::size_t)keyCol]) : std::string();
        if (key.empty()) { Warn(r.warnings, "row " + std::to_string(ri + 1) + " has no key; skipped"); continue; }
        if (!seen.insert(key).second) Warn(r.warnings, "the key \"" + key + "\" appears twice; the later row wins");
        if (!in.Key(key)) continue;
        if ((int)row.size() < (int)rows[0].size()) ++shortRows;
        if (noteCol >= 0 && noteCol < (int)row.size()) in.Note(key, row[(std::size_t)noteCol]);
        for (const auto& lc : usable) {
            const std::string cell = lc.first < (int)row.size() ? row[(std::size_t)lc.first] : std::string();
            in.Cell(key, lc.second, cell);
        }
        ++r.rows;
    }
    if (shortRows) Warn(r.warnings, std::to_string(shortRows) + " rows have fewer cells than the header; the rest read as empty");
    return Finish(*this, std::move(work), std::move(r), options.dryRun);
}

// =====================================================================================
// JSON import / export
// =====================================================================================
std::string LocTable::ToJson() const {
    std::ostringstream o;
    o << "{\n  \"okayloc\": 1,\n";
    const std::string def = table.DefaultLanguage();
    if (!def.empty()) o << "  \"default\": " << JsonQuote(def) << ",\n";
    o << "  \"languages\": [";
    for (std::size_t i = 0; i < table.languages.size(); ++i) {
        const LocLanguage& l = table.languages[i];
        o << (i ? ",\n    " : "\n    ") << "{ \"code\": " << JsonQuote(l.code) << ", \"name\": " << JsonQuote(l.name);
        if (!l.font.empty()) o << ", \"font\": " << JsonQuote(l.font);
        o << " }";
    }
    o << (table.languages.empty() ? "" : "\n  ") << "],\n  \"keys\": {";
    bool firstKey = true;
    for (const auto& kv : table.Entries()) {
        o << (firstKey ? "\n    " : ",\n    ") << JsonQuote(kv.first) << ": { ";
        firstKey = false;
        bool any = false;
        if (!kv.second.note.empty()) { o << "\"note\": " << JsonQuote(kv.second.note); any = true; }
        o << (any ? ", " : "") << "\"text\": {";
        bool firstText = true;
        for (const auto& l : table.languages) {
            auto it = kv.second.text.find(l.code);
            if (it == kv.second.text.end() || it->second.empty()) continue;
            o << (firstText ? " " : ", ") << JsonQuote(l.code) << ": " << JsonQuote(it->second);
            firstText = false;
        }
        o << (firstText ? "" : " ") << "} }";
    }
    o << (firstKey ? "" : "\n  ") << "}\n}\n";
    return o.str();
}

std::string LocTable::ToJsonFlat(const std::string& lang) const {
    std::ostringstream o;
    o << "{";
    bool first = true;
    for (const auto& kv : table.Entries()) {
        auto it = kv.second.text.find(lang);
        if (it == kv.second.text.end() || it->second.empty()) continue;
        o << (first ? "\n  " : ",\n  ") << JsonQuote(kv.first) << ": " << JsonQuote(it->second);
        first = false;
    }
    o << (first ? "" : "\n") << "}\n";
    return o.str();
}

LocImportResult LocTable::ImportJson(const std::string& jsonIn, const LocImportOptions& options) {
    LocImportResult r;
    if (jsonIn.size() > kMaxFileBytes) { r.error = "the file is too large"; return r; }
    const std::string text = ToUtf8(jsonIn);
    JsonReader jr(text);
    JVal root;
    if (!jr.Value(root, 0)) { r.error = jr.err; return r; }
    if (root.t != JVal::T::Obj) { r.error = "expected a JSON object"; return r; }

    StringTable work = table;
    Importer in{work, options, r.warnings, {}};
    const JVal* keys = root.Get("keys");
    if (keys && keys->t == JVal::T::Obj) {
        // The whole table.
        std::string fallbackLang = options.language;
        if (const JVal* langs = root.Get("languages"); langs && langs->t == JVal::T::Arr) {
            for (const JVal& l : langs->arr) {
                std::string code, name, font;
                if (l.t == JVal::T::Str) code = l.s;
                else if (l.t == JVal::T::Obj) {
                    if (const JVal* c = l.Get("code")) code = c->s;
                    if (const JVal* n = l.Get("name")) name = n->s;
                    if (const JVal* f = l.Get("font")) font = f->s;
                }
                if (code.empty()) { Warn(r.warnings, "a language without a code was skipped"); continue; }
                if (!in.LanguageOk(code)) continue;
                LocLanguage* mine = work.FindLanguage(code);
                if (!name.empty() && (mine->name.empty() || mine->name == mine->code)) mine->name = name;
                if (!font.empty() && mine->font.empty()) mine->font = font;
            }
        }
        if (const JVal* def = root.Get("default"); def && def->t == JVal::T::Str && work.defaultLanguage.empty() && work.FindLanguage(def->s))
            work.defaultLanguage = def->s;
        if (fallbackLang.empty()) fallbackLang = work.DefaultLanguage();
        for (const auto& kv : keys->obj) {
            if (kv.first.empty()) { Warn(r.warnings, "a key with no name was skipped"); continue; }
            if (!in.Key(kv.first)) continue;
            const JVal& v = kv.second;
            if (v.t == JVal::T::Str) {
                if (!fallbackLang.empty() && in.LanguageOk(fallbackLang)) in.Cell(kv.first, fallbackLang, v.s);
            } else if (v.t == JVal::T::Obj) {
                if (const JVal* n = v.Get("note"); n && n->t == JVal::T::Str) in.Note(kv.first, n->s);
                if (const JVal* t = v.Get("text"); t && t->t == JVal::T::Obj) {
                    for (const auto& lt : t->obj) {
                        std::string cell;
                        if (!ScalarText(lt.second, cell)) { Warn(r.warnings, "skipped " + kv.first + " / " + lt.first + ": not text"); continue; }
                        if (in.LanguageOk(lt.first)) in.Cell(kv.first, lt.first, cell);
                    }
                }
            } else {
                Warn(r.warnings, "skipped \"" + kv.first + "\": expected text or an object");
                continue;
            }
            ++r.rows;
        }
    } else {
        // One language: {"key": "text"} or nested.
        if (options.language.empty()) {
            r.error = "this JSON holds one language's texts: say which language it is";
            return r;
        }
        if (!in.LanguageOk(options.language)) { r.error = "the table doesn't have the language \"" + options.language + "\""; return r; }
        std::vector<std::pair<std::string, std::string>> flat;
        FlattenJson(root, "", flat, r.warnings, 0);
        for (const auto& kv : flat) {
            if (kv.first.empty() || !in.Key(kv.first)) continue;
            in.Cell(kv.first, options.language, kv.second);
            ++r.rows;
        }
    }
    return Finish(*this, std::move(work), std::move(r), options.dryRun);
}

bool LocTable::SaveCsv(const std::string& path, const LocCsvOptions& options) const { return WriteFile(path, ToCsv(options)); }
bool LocTable::SaveJson(const std::string& path) const { return WriteFile(path, ToJson()); }

// =====================================================================================
// Loading
// =====================================================================================
bool LocTable::LoadFile(const std::string& path, std::string* err) {
    std::string data, e;
    if (!ReadFile(path, data, &e)) { if (err) *err = e; return false; }
    const fs::path p = fs::u8path(path);
    const std::string ext = LowerAscii(p.extension().u8string());
    const std::string name = p.filename().u8string();
    if (ext == ".okayloc") {
        StringTable t;
        if (!t.Parse(data, &e)) { if (err) *err = name + ": " + e; return false; }
        LocMerge(table, t);
    } else if (ext == ".csv") {
        LocImportResult r = ImportCsv(data);
        if (!r.ok) { if (err) *err = name + ": " + r.error; return false; }
        for (const auto& w : r.warnings) warnings.push_back(name + ": " + w);
    } else if (ext == ".json") {
        LocImportOptions o;
        o.language = p.stem().u8string();
        LocImportResult r = ImportJson(data, o);
        if (!r.ok) { if (err) *err = name + ": " + r.error; return false; }
        for (const auto& w : r.warnings) warnings.push_back(name + ": " + w);
    } else {
        if (err) *err = name + ": not a .okayloc, .csv or .json file";
        return false;
    }
    files.push_back(path);
    return true;
}

bool LocTable::LoadFolder(const std::string& dir, std::string* err) {
    std::error_code ec;
    const fs::path root = fs::u8path(dir);
    if (!fs::is_directory(root, ec)) { if (err) *err = dir + " is not a folder"; return false; }
    std::vector<std::pair<std::string, std::string>> found;   // lowercase name, path
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const std::string ext = LowerAscii(it->path().extension().u8string());
        if (ext != ".okayloc" && ext != ".csv" && ext != ".json") continue;
        found.emplace_back(LowerAscii(it->path().filename().u8string()), it->path().u8string());
    }
    if (found.empty()) { if (err) *err = "no .okayloc, .csv or .json files in " + dir; return false; }
    std::sort(found.begin(), found.end());
    int loaded = 0;
    std::string lastErr;
    for (const auto& f : found) {
        std::string e;
        if (LoadFile(f.second, &e)) ++loaded;
        else { warnings.push_back(e); lastErr = e; }
    }
    if (loaded == 0) { if (err) *err = lastErr; return false; }
    return true;
}

} // namespace okay
