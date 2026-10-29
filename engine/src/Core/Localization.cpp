#include "okay/Core/Localization.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/Utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace okay {

const char* const Localization::kPseudo = "qps-ploc";
const char* const Localization::kPrefsKey = "loc.language";

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// ---- .okayloc tokens -----------------------------------------------------------
// A record is one line of tokens: bare words, or quoted strings with \" \\ \n \t
// \r escapes. A quoted string may run over several lines (hand-edited files).
struct Lexer {
    const std::string& s;
    std::size_t i = 0;

    void SkipBlanks() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) ++i; }
    bool AtLineEnd() { SkipBlanks(); return i >= s.size() || s[i] == '\n'; }
    void SkipLine() {
        while (i < s.size() && s[i] != '\n') ++i;
        if (i < s.size()) ++i;
    }
    // The next token on this record's line; false at the end of the line.
    bool Token(std::string& out, bool* quoted = nullptr) {
        if (AtLineEnd()) return false;
        out.clear();
        if (s[i] == '"') {
            ++i;
            while (i < s.size() && s[i] != '"') {
                const char c = s[i++];
                if (c == '\\' && i < s.size()) {
                    const char e = s[i++];
                    out += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e;
                } else if (c == '\r' && i < s.size() && s[i] == '\n') {
                    continue;   // a CRLF file: the value keeps a plain newline
                } else {
                    out += c;
                }
            }
            if (i < s.size()) ++i;   // the closing quote
            if (quoted) *quoted = true;
            return true;
        }
        while (i < s.size() && !std::isspace((unsigned char)s[i])) out += s[i++];
        if (quoted) *quoted = false;
        return true;
    }
};

std::string Quote(const std::string& v) {
    std::string out = "\"";
    for (char c : v) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:   out += c;
        }
    }
    return out + "\"";
}

// A key or code as a bare word when it reads back as one, else quoted.
std::string Word(const std::string& v) {
    bool bare = !v.empty() && v[0] != '#' && v[0] != '"';
    for (char c : v) if (std::isspace((unsigned char)c) || c == '"' || c == '\\') bare = false;
    return bare ? v : Quote(v);
}

// ---- CSV -------------------------------------------------------------------------
std::string CsvField(const std::string& v) {
    const bool quote = v.find_first_of(",;\t\"\r\n") != std::string::npos ||
                       (!v.empty() && (v.front() == ' ' || v.back() == ' '));
    if (!quote) return v;
    std::string out = "\"";
    for (char c : v) { if (c == '"') out += '"'; out += c; }
    return out + "\"";
}

// The delimiter the header row uses most (outside quotes): comma, semicolon or tab.
char DetectDelimiter(const std::string& s, std::size_t start) {
    int comma = 0, semi = 0, tab = 0;
    bool quoted = false;
    for (std::size_t i = start; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '"') quoted = !quoted;
        else if (!quoted && (c == '\n' || c == '\r')) break;
        else if (!quoted && c == ',') ++comma;
        else if (!quoted && c == ';') ++semi;
        else if (!quoted && c == '\t') ++tab;
    }
    if (semi > comma && semi >= tab) return ';';
    if (tab > comma && tab > semi) return '\t';
    return ',';
}

// RFC 4180 rows: quoted fields may hold the delimiter, doubled quotes and newlines.
std::vector<std::vector<std::string>> CsvRows(const std::string& s, std::size_t start, char delim) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false, any = false;
    auto endField = [&] { row.push_back(field); field.clear(); };
    auto endRow = [&] {
        endField();
        rows.push_back(std::move(row));
        row.clear();
        any = false;
    };
    for (std::size_t i = start; i < s.size(); ++i) {
        const char c = s[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < s.size() && s[i + 1] == '"') { field += '"'; ++i; }
                else quoted = false;
            } else if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') {
                continue;   // CRLF inside a cell: keep a plain newline
            } else {
                field += c;
            }
            continue;
        }
        if (c == '"' && field.empty()) { quoted = true; any = true; }
        else if (c == delim) { endField(); any = true; }
        else if (c == '\n') endRow();
        else if (c == '\r') { if (i + 1 >= s.size() || s[i + 1] != '\n') endRow(); }
        else { field += c; any = true; }
    }
    if (any || !field.empty() || !row.empty()) endRow();
    return rows;
}

// ---- Numbers and placeholders ----------------------------------------------------
std::string NumberText(double v) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    char buf[64];
    if (v == 0.0) return "0";
    if (std::fabs(v) < 9.0e15 && v == std::floor(v)) {
        std::snprintf(buf, sizeof(buf), "%.0f", v);
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::size_t MatchBrace(const std::string& s, std::size_t open) {
    int depth = 0;
    for (std::size_t i = open; i < s.size(); ++i) {
        if (s[i] == '{') ++depth;
        else if (s[i] == '}' && --depth == 0) return i;
    }
    return std::string::npos;
}

std::size_t TopLevelComma(const std::string& s, std::size_t from = 0) {
    int depth = 0;
    for (std::size_t i = from; i < s.size(); ++i) {
        if (s[i] == '{') ++depth;
        else if (s[i] == '}') --depth;
        else if (s[i] == ',' && depth == 0) return i;
    }
    return std::string::npos;
}

const LocArg* FindArg(const std::vector<LocArg>& args, const std::string& name) {
    if (name.empty()) return nullptr;
    if (std::all_of(name.begin(), name.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
        const std::size_t idx = (std::size_t)std::strtoul(name.c_str(), nullptr, 10);
        return idx < args.size() ? &args[idx] : nullptr;
    }
    for (const LocArg& a : args) if (a.name == name) return &a;
    return nullptr;
}

// ---- Pseudo-localization: accented look-alikes of the ASCII letters ----------------
std::uint32_t Accented(std::uint32_t c) {
    switch (c) {
        case 'a': return 0xE5;  case 'c': return 0xE7;  case 'd': return 0x10F; case 'e': return 0xE9;
        case 'g': return 0x11F; case 'h': return 0x125; case 'i': return 0xED;  case 'j': return 0x135;
        case 'k': return 0x137; case 'l': return 0x13E; case 'n': return 0xF1;  case 'o': return 0xF6;
        case 'r': return 0x155; case 's': return 0x161; case 't': return 0x163; case 'u': return 0xFC;
        case 'w': return 0x175; case 'y': return 0xFD;  case 'z': return 0x17E;
        case 'A': return 0xC5;  case 'C': return 0xC7;  case 'D': return 0x10E; case 'E': return 0xC9;
        case 'G': return 0x11E; case 'H': return 0x124; case 'I': return 0xCD;  case 'J': return 0x134;
        case 'K': return 0x136; case 'L': return 0x13D; case 'N': return 0xD1;  case 'O': return 0xD6;
        case 'R': return 0x154; case 'S': return 0x160; case 'T': return 0x162; case 'U': return 0xDC;
        case 'W': return 0x174; case 'Y': return 0xDD;  case 'Z': return 0x17D;
        default:  return c;
    }
}

// ---- The service's state -----------------------------------------------------------
struct State {
    StringTable table;
    std::string path;
    std::string language;   // the chosen language ("" = the table's default)
    std::uint32_t version = 1;
    std::vector<std::pair<int, Localization::Listener>> listeners;
    int nextListener = 1;
    std::set<std::string> reportedSet;
    std::vector<std::string> reported;
    std::map<std::string, Localization::PluralRule> plural;
};
State& S() {
    static State s;
    return s;
}

std::string BaseOf(const std::string& code) {
    const std::size_t dash = code.find_first_of("-_");
    return Lower(dash == std::string::npos ? code : code.substr(0, dash));
}

// "fr_FR.UTF-8@euro" -> "fr-fr"
std::string NormalizeLocale(std::string s) {
    const std::size_t cut = s.find_first_of(".@");
    if (cut != std::string::npos) s.erase(cut);
    for (char& c : s) c = c == '_' ? '-' : (char)std::tolower((unsigned char)c);
    return Trim(s);
}

void Report(const std::string& lang, const std::string& key, const std::string& shown) {
    const std::string id = (lang.empty() ? std::string("?") : lang) + ": " + key;
    if (!S().reportedSet.insert(id).second) return;
    S().reported.push_back(id);
    if (lang.empty()) Log::Warning("Localization: no text for \"", key, "\"; showing the key");
    else Log::Warning("Localization: \"", key, "\" has no ", lang, " text; showing the ", shown, " one");
}

// The text of `key` without pseudo-localization: the current language, the
// default, or the key. Misses are reported once when `report` is set.
std::string Lookup(const std::string& key, bool report, bool* found = nullptr) {
    const StringTable& t = S().table;
    std::string lang = Localization::Language();
    const std::string def = t.DefaultLanguage();
    if (lang == Localization::kPseudo) lang = def;
    if (found) *found = true;
    if (const std::string* v = t.Text(key, lang)) return *v;
    if (def != lang)
        if (const std::string* v = t.Text(key, def)) {
            if (report) Report(lang, key, def);
            return *v;
        }
    if (found) *found = false;
    if (report) Report("", key, "");
    return key;
}

void Notify(const std::string& before) {
    const std::string now = Localization::Language();
    if (now == before) return;
    const auto listeners = S().listeners;   // a listener may remove itself
    for (const auto& l : listeners) if (l.second) l.second(now);
}

} // namespace

// =====================================================================================
// StringTable
// =====================================================================================
const LocLanguage* StringTable::FindLanguage(const std::string& code) const {
    for (const auto& l : languages) if (l.code == code) return &l;
    return nullptr;
}
LocLanguage* StringTable::FindLanguage(const std::string& code) {
    for (auto& l : languages) if (l.code == code) return &l;
    return nullptr;
}

LocLanguage& StringTable::AddLanguage(const std::string& code, const std::string& name) {
    if (LocLanguage* l = FindLanguage(code)) return *l;
    languages.push_back(LocLanguage{code, name.empty() ? code : name, ""});
    return languages.back();
}

bool StringTable::RemoveLanguage(const std::string& code) {
    auto it = std::find_if(languages.begin(), languages.end(), [&](const LocLanguage& l) { return l.code == code; });
    if (it == languages.end()) return false;
    languages.erase(it);
    for (auto& kv : m_entries) kv.second.text.erase(code);
    if (defaultLanguage == code) defaultLanguage.clear();
    return true;
}

std::string StringTable::DefaultLanguage() const {
    if (!defaultLanguage.empty() && FindLanguage(defaultLanguage)) return defaultLanguage;
    return languages.empty() ? std::string() : languages.front().code;
}

const LocEntry* StringTable::Find(const std::string& key) const {
    auto it = m_entries.find(key);
    return it == m_entries.end() ? nullptr : &it->second;
}
LocEntry* StringTable::Find(const std::string& key) {
    auto it = m_entries.find(key);
    return it == m_entries.end() ? nullptr : &it->second;
}

LocEntry& StringTable::Add(const std::string& key) { return m_entries[key]; }

bool StringTable::Remove(const std::string& key) { return m_entries.erase(key) > 0; }

bool StringTable::Rename(const std::string& from, const std::string& to) {
    if (to.empty() || from == to || m_entries.count(to)) return false;
    auto node = m_entries.extract(from);
    if (node.empty()) return false;
    node.key() = to;
    m_entries.insert(std::move(node));
    return true;
}

void StringTable::Set(const std::string& key, const std::string& lang, const std::string& text) {
    if (!lang.empty()) AddLanguage(lang);
    LocEntry& e = Add(key);
    if (text.empty()) e.text.erase(lang);
    else e.text[lang] = text;
}

const std::string* StringTable::Text(const std::string& key, const std::string& lang) const {
    const LocEntry* e = Find(key);
    if (!e) return nullptr;
    auto it = e->text.find(lang);
    return it == e->text.end() || it->second.empty() ? nullptr : &it->second;
}

float StringTable::Completion(const std::string& lang) const {
    if (m_entries.empty()) return 1.0f;
    std::size_t done = 0;
    for (const auto& kv : m_entries) {
        auto it = kv.second.text.find(lang);
        if (it != kv.second.text.end() && !it->second.empty()) ++done;
    }
    return (float)done / (float)m_entries.size();
}

// Format (UTF-8):
//   okayloc 1
//   language en "English" default
//   language ja "日本語" font "Assets/Fonts/NotoSansJP.ttf"
//   key menu.start "A note for translators"
//   text en menu.start "Start"
// Lines starting with # are comments; records this build doesn't know are skipped.
bool StringTable::Parse(const std::string& text, std::string* err) {
    Lexer lx{text};
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) lx.i = 3;
    StringTable t;
    bool header = false;
    std::string word, a, b, c;
    while (lx.i < text.size()) {
        lx.SkipBlanks();
        if (lx.i >= text.size()) break;
        if (text[lx.i] == '\n') { ++lx.i; continue; }
        if (text[lx.i] == '#') { lx.SkipLine(); continue; }
        lx.Token(word);
        if (!header) {
            if (word != "okayloc") {
                if (err) *err = "not a string table (expected 'okayloc 1' on the first line)";
                return false;
            }
            header = true;
        } else if (word == "language") {
            if (lx.Token(a) && !a.empty()) {
                LocLanguage& l = t.AddLanguage(a);
                bool quoted = false, named = false;
                while (lx.Token(b, &quoted)) {
                    if (quoted && !named) { l.name = b; named = true; }
                    else if (b == "default") t.defaultLanguage = l.code;
                    else if (b == "font" && lx.Token(c)) l.font = c;
                }
            }
        } else if (word == "key") {
            if (lx.Token(a) && !a.empty()) {
                LocEntry& e = t.Add(a);
                if (lx.Token(b)) e.note = b;
            }
        } else if (word == "text") {
            if (lx.Token(a) && lx.Token(b) && lx.Token(c) && !a.empty() && !b.empty()) t.Set(b, a, c);
        }
        lx.SkipLine();   // the rest of the record (and unknown records)
    }
    if (!header) {
        if (err) *err = "empty file";
        return false;
    }
    *this = std::move(t);
    return true;
}

std::string StringTable::Write() const {
    std::ostringstream o;
    o << "okayloc 1\n"
      << "# String table: the languages, every key (with a note for translators), then each\n"
      << "# language's texts. One record per line; lines starting with # are comments.\n";
    const std::string def = DefaultLanguage();
    for (const auto& l : languages) {
        o << "language " << Word(l.code) << " " << Quote(l.name.empty() ? l.code : l.name);
        if (l.code == def) o << " default";
        if (!l.font.empty()) o << " font " << Quote(l.font);
        o << "\n";
    }
    if (!m_entries.empty()) o << "\n";
    for (const auto& kv : m_entries) {
        o << "key " << Word(kv.first);
        if (!kv.second.note.empty()) o << " " << Quote(kv.second.note);
        o << "\n";
    }
    for (const auto& l : languages) {
        bool any = false;
        for (const auto& kv : m_entries) {
            auto it = kv.second.text.find(l.code);
            if (it == kv.second.text.end() || it->second.empty()) continue;
            if (!any) { o << "\n"; any = true; }
            o << "text " << Word(l.code) << " " << Word(kv.first) << " " << Quote(it->second) << "\n";
        }
    }
    return o.str();
}

bool StringTable::Load(const std::string& path, std::string* err) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) { if (err) *err = "can't open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    return Parse(ss.str(), err);
}

bool StringTable::Save(const std::string& path) const {
    std::ofstream f(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << Write();
    return (bool)f;
}

std::string StringTable::ExportCsv(const std::vector<std::string>& langs) const {
    std::vector<std::string> cols = langs;
    if (cols.empty()) for (const auto& l : languages) cols.push_back(l.code);
    std::string out = "\xEF\xBB\xBF";   // a BOM, so spreadsheets open it as UTF-8
    out += "key,note";
    for (const auto& c : cols) out += "," + CsvField(c);
    out += "\r\n";
    for (const auto& kv : m_entries) {
        out += CsvField(kv.first) + "," + CsvField(kv.second.note);
        for (const auto& c : cols) {
            auto it = kv.second.text.find(c);
            out += "," + (it == kv.second.text.end() ? std::string() : CsvField(it->second));
        }
        out += "\r\n";
    }
    return out;
}

bool StringTable::ImportCsv(const std::string& csv, std::string* err, int* rowsRead) {
    if (rowsRead) *rowsRead = 0;
    const std::size_t start = csv.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    const auto rows = CsvRows(csv, start, DetectDelimiter(csv, start));
    if (rows.empty()) { if (err) *err = "the file is empty"; return false; }
    int keyCol = -1, noteCol = -1;
    std::vector<std::pair<int, std::string>> langCols;
    for (int i = 0; i < (int)rows[0].size(); ++i) {
        const std::string cell = Trim(rows[0][(std::size_t)i]), low = Lower(cell);
        if (low == "key" && keyCol < 0) keyCol = i;
        else if (low == "note" || low == "notes" || low == "comment" || low == "comments" || low == "context") noteCol = i;
        else if (!cell.empty()) langCols.push_back({i, cell});
    }
    if (keyCol < 0) { if (err) *err = "no \"key\" column in the header row"; return false; }
    for (const auto& lc : langCols) AddLanguage(lc.second);
    int n = 0;
    for (std::size_t r = 1; r < rows.size(); ++r) {
        const auto& row = rows[r];
        if (keyCol >= (int)row.size()) continue;
        const std::string key = Trim(row[(std::size_t)keyCol]);
        if (key.empty()) continue;
        LocEntry& e = Add(key);
        if (noteCol >= 0 && noteCol < (int)row.size() && !row[(std::size_t)noteCol].empty()) e.note = row[(std::size_t)noteCol];
        for (const auto& lc : langCols)
            if (lc.first < (int)row.size() && !row[(std::size_t)lc.first].empty()) e.text[lc.second] = row[(std::size_t)lc.first];
        ++n;
    }
    if (rowsRead) *rowsRead = n;
    return true;
}

// =====================================================================================
// LocArg
// =====================================================================================
LocArg::LocArg(double v) : text(NumberText(v)), number(v), isNumber(true) {}

// =====================================================================================
// Localization
// =====================================================================================
StringTable& Localization::Table() { return S().table; }
const std::string& Localization::TablePath() { return S().path; }

bool Localization::LoadTable(const std::string& path, std::string* err) {
    StringTable t;
    if (!t.Load(path, err)) return false;
    SetTable(t, path);
    return true;
}

void Localization::SetTable(const StringTable& table, const std::string& path) {
    const std::string before = Language();
    S().table = table;
    S().path = path;
    if (S().language != kPseudo && !S().table.FindLanguage(S().language)) S().language.clear();
    ++S().version;
    Notify(before);
}

void Localization::TableChanged() {
    const std::string before = Language();
    if (S().language != kPseudo && !S().table.FindLanguage(S().language)) S().language.clear();
    ++S().version;
    Notify(before);
}

void Localization::Reset() {
    State& s = S();
    s.table.Clear();
    s.path.clear();
    s.language.clear();
    s.listeners.clear();
    s.reportedSet.clear();
    s.reported.clear();
    s.plural.clear();
    ++s.version;
}

std::string Localization::Language() {
    return S().language.empty() ? S().table.DefaultLanguage() : S().language;
}

bool Localization::SetLanguage(const std::string& code) {
    if (code != kPseudo && !S().table.FindLanguage(code)) return false;
    const std::string before = Language();
    S().language = code;
    if (code == before) return true;
    ++S().version;
    Notify(before);
    return true;
}

std::vector<std::string> Localization::Languages() {
    std::vector<std::string> out;
    for (const auto& l : S().table.languages) out.push_back(l.code);
    return out;
}

std::string Localization::LanguageName(const std::string& code) {
    if (code == kPseudo) return "Pseudo";
    const LocLanguage* l = S().table.FindLanguage(code);
    return l && !l->name.empty() ? l->name : code;
}

std::string Localization::LanguageFont() {
    std::string lang = Language();
    if (lang == kPseudo) lang = S().table.DefaultLanguage();
    const LocLanguage* l = S().table.FindLanguage(lang);
    if (!l || l->font.empty()) return {};
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p = fs::u8path(l->font);
    if (p.is_relative() && !fs::exists(p, ec) && !S().path.empty()) {
        const fs::path beside = fs::u8path(S().path).parent_path() / p;
        if (fs::exists(beside, ec)) return beside.u8string();
    }
    return l->font;
}

std::string Localization::PickLanguage(const std::vector<std::string>& preferred) {
    const auto& langs = S().table.languages;
    for (const std::string& raw : preferred) {
        const std::string want = NormalizeLocale(raw);
        if (want.empty() || want == "c" || want == "posix") continue;
        for (const auto& l : langs) if (NormalizeLocale(l.code) == want) return l.code;
        const std::string base = BaseOf(want);
        for (const auto& l : langs) if (NormalizeLocale(l.code) == base) return l.code;
        for (const auto& l : langs) if (BaseOf(l.code) == base) return l.code;
    }
    return S().table.DefaultLanguage();
}

void Localization::LoadSettings(const std::vector<std::string>& osLocales) {
    const std::string saved = Prefs::GetString(kPrefsKey, "");
    if (!saved.empty() && (saved == kPseudo || S().table.FindLanguage(saved))) { SetLanguage(saved); return; }
    const std::string pick = PickLanguage(osLocales);
    if (!pick.empty()) SetLanguage(pick);
}

void Localization::SaveSettings() {
    const std::string lang = Language();
    if (!lang.empty()) Prefs::SetString(kPrefsKey, lang);
}

std::string Localization::Get(const std::string& key) {
    const std::string text = Lookup(key, true);
    return S().language == kPseudo ? Pseudo(text) : text;
}

bool Localization::TryGet(const std::string& key, std::string& out) {
    bool found = false;
    std::string text = Lookup(key, false, &found);
    if (!found) return false;
    out = S().language == kPseudo ? Pseudo(text) : std::move(text);
    return true;
}

std::string Localization::LocalizeArgs(const std::string& key, const std::vector<LocArg>& args) {
    const std::string text = Lookup(key, true);
    if (S().language == kPseudo) return Pseudo(Format(text, args, S().table.DefaultLanguage()));
    return Format(text, args);
}

std::string Localization::Format(const std::string& pattern, const std::vector<LocArg>& args, const std::string& langIn) {
    const std::string lang = langIn.empty() ? Language() : langIn;
    std::string out;
    out.reserve(pattern.size());
    for (std::size_t i = 0; i < pattern.size();) {
        const char c = pattern[i];
        if (c == '{' && i + 1 < pattern.size() && pattern[i + 1] == '{') { out += '{'; i += 2; continue; }
        if (c == '}' && i + 1 < pattern.size() && pattern[i + 1] == '}') { out += '}'; i += 2; continue; }
        if (c != '{') { out += c; ++i; continue; }
        const std::size_t close = MatchBrace(pattern, i);
        if (close == std::string::npos) { out.append(pattern, i, std::string::npos); break; }
        const std::string inner = pattern.substr(i + 1, close - i - 1);
        i = close + 1;
        const std::size_t c1 = TopLevelComma(inner);
        const LocArg* a = FindArg(args, Trim(inner.substr(0, c1)));
        if (!a) { out += "{" + inner + "}"; continue; }
        const std::size_t c2 = c1 == std::string::npos ? std::string::npos : TopLevelComma(inner, c1 + 1);
        if (c2 == std::string::npos || Trim(inner.substr(c1 + 1, c2 - c1 - 1)) != "plural") { out += a->text; continue; }
        // {n, plural, =0 {...} one {...} other {...}}
        const double n = a->isNumber ? a->number : std::strtod(a->text.c_str(), nullptr);
        const std::string body = inner.substr(c2 + 1);
        std::map<std::string, std::string> forms;
        for (std::size_t p = 0; p < body.size();) {
            while (p < body.size() && std::isspace((unsigned char)body[p])) ++p;
            const std::size_t open = body.find('{', p);
            if (open == std::string::npos) break;
            const std::string sel = Trim(body.substr(p, open - p));
            const std::size_t end = MatchBrace(body, open);
            if (end == std::string::npos) break;
            forms[sel] = body.substr(open + 1, end - open - 1);
            p = end + 1;
        }
        auto pick = forms.find("=" + NumberText(n));
        if (pick == forms.end()) pick = forms.find(PluralName(Plural(lang, n)));
        if (pick == forms.end()) pick = forms.find("other");
        if (pick == forms.end()) { out += a->text; continue; }
        std::string msg;   // # (outside nested placeholders) is the number
        int depth = 0;
        for (char m : pick->second) {
            if (m == '{') ++depth;
            else if (m == '}') --depth;
            if (m == '#' && depth == 0) msg += a->isNumber ? a->text : NumberText(n);
            else msg += m;
        }
        out += Format(msg, args, lang);
    }
    return out;
}

const char* Localization::PluralName(PluralForm f) {
    switch (f) {
        case PluralForm::Zero: return "zero";
        case PluralForm::One:  return "one";
        case PluralForm::Two:  return "two";
        case PluralForm::Few:  return "few";
        case PluralForm::Many: return "many";
        case PluralForm::Other: break;
    }
    return "other";
}

Localization::PluralForm Localization::Plural(const std::string& lang, double n) {
    auto& rules = S().plural;
    auto it = rules.find(lang);
    if (it == rules.end()) it = rules.find(BaseOf(lang));
    if (it != rules.end() && it->second) return it->second(n);
    const std::string base = BaseOf(lang);
    if (base == "ja" || base == "zh" || base == "ko") return PluralForm::Other;
    if (base == "fr") return (n >= 0.0 && n < 2.0) ? PluralForm::One : PluralForm::Other;
    return n == 1.0 ? PluralForm::One : PluralForm::Other;
}

void Localization::SetPluralRule(const std::string& lang, PluralRule rule) {
    if (rule) S().plural[lang] = std::move(rule);
    else S().plural.erase(lang);
}

std::uint32_t Localization::Version() { return S().version; }

int Localization::AddListener(Listener fn) {
    const int id = S().nextListener++;
    S().listeners.push_back({id, std::move(fn)});
    return id;
}

void Localization::RemoveListener(int id) {
    auto& ls = S().listeners;
    ls.erase(std::remove_if(ls.begin(), ls.end(), [id](const auto& l) { return l.first == id; }), ls.end());
}

const std::vector<std::string>& Localization::Reported() { return S().reported; }

std::string Localization::Pseudo(const std::string& text) {
    if (text.empty()) return text;
    std::string out = "[";
    int depth = 0;
    std::size_t len = 0;
    for (std::size_t i = 0; i < text.size(); ++len) {
        const std::uint32_t cp = utf8::Next(text, i);
        if (cp == '{') ++depth;
        else if (cp == '}' && depth > 0) --depth;
        utf8::Append(out, depth == 0 ? Accented(cp) : cp);
    }
    out += std::string((len * 3 + 9) / 10, '~');   // about 30% longer
    out += "]";
    return out;
}

} // namespace okay
