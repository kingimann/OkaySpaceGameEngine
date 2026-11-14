#include "LocalizationIO.hpp"

#include "okay/Core/Utf8.hpp"
#include "okay/Graphics/TtfFont.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>

namespace oked {

namespace {

std::string Clip(const std::string& s, std::size_t maxChars) {
    std::string t;
    for (char c : s) {
        if (c == '\n') t += "\\n";
        else if (c == '\r') t += "\\r";
        else if (c == '\t') t += " ";
        else t += c;
    }
    if (okay::utf8::Length(t) <= maxChars) return t;
    return okay::utf8::Truncate(t, maxChars) + "...";
}

std::string Quote(const std::string& s) { return "\"" + Clip(s, 48) + "\""; }

std::string Plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

std::string CodePointLabel(std::uint32_t cp) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "U+%04X", (unsigned)cp);
    return buf;
}

// A spreadsheet's header may spell a language differently (FR, pt_br): name the table's own code
// instead, so the column fills it and no second language is added. Other bytes pass through.
std::string MatchCsvHeader(const okay::StringTable& live, const std::string& csv) {
    std::vector<std::vector<std::string>> rows;
    if (!okay::LocParseCsv(csv, rows, nullptr, 0) || rows.empty()) return csv;
    auto fold = [](std::string v) {
        for (char& c : v) c = c == '_' ? '-' : (char)std::tolower((unsigned char)c);
        return v;
    };
    bool changed = false;
    for (std::string& cell : rows[0]) {
        const std::string f = fold(cell);
        if (f == "key" || f == "note") continue;
        for (const okay::LocLanguage& l : live.languages)
            if (l.code != cell && fold(l.code) == f && !live.FindLanguage(cell)) { cell = l.code; changed = true; break; }
    }
    if (!changed) return csv;
    std::string out;
    for (const auto& row : rows) {
        for (std::size_t i = 0; i < row.size(); ++i) out += (i ? "," : "") + okay::LocCsvField(row[i]);
        out += "\r\n";
    }
    return out;
}

} // namespace

// ---- Import preview ---------------------------------------------------------------------------

int LocImportPreview::ChangeCount() const {
    return (int)(diff.addedKeys.size() + diff.addedLanguages.size() + diff.filled.size() + diff.changed.size() +
                 diff.cleared.size()) + diff.notesChanged;
}

std::string LocImportPreview::Summary() const {
    if (!ok) return "Cannot import: " + error;
    std::string note = "Imported " + Plural(rows, "row", "rows");
    auto add = [&](int n, const char* one, const char* many) { if (n > 0) note += ", " + Plural(n, one, many); };
    add((int)diff.filled.size(), "text filled", "texts filled");
    add((int)diff.changed.size(), "text changed", "texts changed");
    add((int)diff.addedKeys.size(), "new key", "new keys");
    add((int)diff.addedLanguages.size(), "new language", "new languages");
    add((int)diff.cleared.size(), "text cleared", "texts cleared");
    add(diff.notesChanged, "note changed", "notes changed");
    if (diff.Empty()) note += ", nothing would change";
    return note;
}


LocImportFormat DetectLocFormat(const std::string& bytes) {
    std::size_t i = 0;
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) i = 3;
    while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t' || bytes[i] == '\r' || bytes[i] == '\n')) ++i;
    return i < bytes.size() && bytes[i] == '{' ? LocImportFormat::Json : LocImportFormat::Csv;
}

LocImportPreview PreviewLocImport(const okay::StringTable& live, const std::string& bytes, LocImportFormat format,
                                  const std::string& jsonLanguage, okay::LocImportOptions::Mode mode, std::size_t maxLines) {
    LocImportPreview p;
    p.format = format == LocImportFormat::Auto ? DetectLocFormat(bytes) : format;
    if (bytes.empty()) { p.error = "the file is empty"; return p; }
    if (bytes.size() > kLocImportMaxBytes) { p.error = "the file is bigger than 32 MB"; return p; }
    okay::LocTable work(live);
    okay::LocImportOptions options;
    options.mode = mode;
    options.language = jsonLanguage;
    const okay::LocImportResult r = p.format == LocImportFormat::Json ? work.ImportJson(bytes, options) : work.ImportCsv(MatchCsvHeader(live, bytes), options);
    if (!r.ok) { p.error = r.error.empty() ? "the file could not be read" : r.error; return p; }
    p.ok = true;
    p.rows = r.rows;
    p.diff = r.diff;
    p.warnings = r.warnings;
    p.after = work.table;

    auto push = [&](const std::string& line) {
        if (p.lines.size() < maxLines) p.lines.push_back(line);
        else ++p.hiddenLines;
    };
    for (const std::string& code : p.diff.addedLanguages) push("+ language " + code);
    for (const std::string& key : p.diff.addedKeys) push("+ key " + key);
    for (const auto& kl : p.diff.filled) {
        const std::string* now = p.after.Text(kl.first, kl.second);
        push("+ " + kl.second + " " + kl.first + ": " + Quote(now ? *now : ""));
    }
    for (const auto& kl : p.diff.changed) {
        const std::string* was = live.Text(kl.first, kl.second);
        const std::string* now = p.after.Text(kl.first, kl.second);
        push("~ " + kl.second + " " + kl.first + ": " + Quote(was ? *was : "") + " -> " + Quote(now ? *now : ""));
    }
    for (const auto& kl : p.diff.cleared) {
        const std::string* was = live.Text(kl.first, kl.second);
        push("- " + kl.second + " " + kl.first + ": " + Quote(was ? *was : ""));
    }
    if (p.diff.notesChanged > 0) push("~ " + Plural(p.diff.notesChanged, "note", "notes") + " changed");
    return p;
}

bool ApplyLocImport(okay::StringTable& live, const LocImportPreview& preview) {
    if (!preview.ok || preview.diff.Empty()) return false;
    live = preview.after;
    return true;
}

std::string ExportLocJson(const okay::StringTable& table, const std::string& language) {
    const okay::LocTable lt(table);
    return language.empty() ? lt.ToJson() : lt.ToJsonFlat(language);
}

// ---- Font coverage ----------------------------------------------------------------------------

std::string LocFontView::Summary() const {
    if (!ok) return "Font check failed: " + error;
    if (clean) return "Every character can be drawn.";
    std::string s;
    if (missingCharacters > 0) s = Plural(missingCharacters, "character", "characters") + " no font can draw";
    if (undrawableTexts > 0) s += (s.empty() ? "" : ", ") + Plural(undrawableTexts, "text", "texts") + " no single font draws completely";
    return s;
}

LocFontView LocWindowFontCoverage(const okay::StringTable& table, const std::string& language, const std::string& fontPath,
                                  const std::string& baseDir, bool useSystemFonts, std::size_t maxLines) {
    LocFontView view;
    okay::LocFontOptions options;
    options.baseDir = baseDir;
    options.useSystemFonts = useSystemFonts;
    std::unique_ptr<okay::TtfFont> chosen;
    if (!fontPath.empty()) {
        chosen = std::make_unique<okay::TtfFont>();
        if (!chosen->LoadFromFile(fontPath)) { view.error = "cannot load the font " + fontPath; return view; }
        options.fonts.push_back(chosen.get());
    }
    if (!language.empty() && !table.FindLanguage(language)) { view.error = "the table has no language " + language; return view; }
    okay::LocFontReport all = okay::LocFontCoverage(table, options);
    view.ok = true;
    for (okay::LocFontLanguage& l : all.languages)
        if (language.empty() || l.language == language) view.report.languages.push_back(std::move(l));
    auto push = [&](const std::string& line) { if (view.lines.size() < maxLines) view.lines.push_back(line); };
    for (const okay::LocFontLanguage& l : view.report.languages) {
        view.missingCharacters += (int)l.missing.size();
        view.undrawableTexts += (int)l.undrawableTexts.size();
        for (const std::string& w : l.warnings) push(l.language + ": warning: " + w);
        for (const okay::LocGlyphGap& g : l.missing)
            push(l.language + ": no font draws " + CodePointLabel(g.codePoint) + " " + g.utf8 + " (" + std::to_string(g.count) +
                 "x, first in " + g.firstKey + ")");
        for (const okay::LocGlyphGap& g : l.fallbacks)
            push(l.language + ": first font lacks " + CodePointLabel(g.codePoint) + " " + g.utf8 + ", a fallback font draws it");
        for (const std::string& key : l.undrawableTexts) push(l.language + ": no single font draws " + key + " completely");
    }
    view.clean = view.missingCharacters == 0 && view.undrawableTexts == 0;
    return view;
}

// ---- Right to left ----------------------------------------------------------------------------

namespace {

enum class Cls { L, R, EN, AN, Neutral };

bool IsMark(std::uint32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0591 && cp <= 0x05BD) || cp == 0x05BF || (cp >= 0x05C1 && cp <= 0x05C2) ||
           (cp >= 0x05C4 && cp <= 0x05C5) || cp == 0x05C7 || (cp >= 0x0610 && cp <= 0x061A) || (cp >= 0x064B && cp <= 0x065F) ||
           cp == 0x0670 || (cp >= 0x06D6 && cp <= 0x06DC) || (cp >= 0x06DF && cp <= 0x06E4) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
           cp == 0x200D;
}

Cls Classify(std::uint32_t cp) {
    if (cp >= '0' && cp <= '9') return Cls::EN;
    if (cp >= 0x06F0 && cp <= 0x06F9) return Cls::EN;
    if (cp >= 0x0660 && cp <= 0x0669) return Cls::AN;
    if (cp == 0x200E) return Cls::L;
    if (cp == 0x200F) return Cls::R;
    if (cp == 0x060C || cp == 0x061B) return Cls::Neutral;   // Arabic comma and semicolon
    if ((cp >= 0x0590 && cp <= 0x05FF) || (cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x07C0 && cp <= 0x085F) ||
        (cp >= 0x0750 && cp <= 0x077F) || (cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFB4F) ||
        (cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF))
        return Cls::R;
    if (cp < 0x80) {
        const bool alnum = (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
        return alnum ? Cls::L : Cls::Neutral;
    }
    if (cp >= 0x80 && cp <= 0xBF) return (cp == 0xAA || cp == 0xB5 || cp == 0xBA) ? Cls::L : Cls::Neutral;
    if (cp == 0xD7 || cp == 0xF7) return Cls::Neutral;
    if (cp >= 0x2000 && cp <= 0x206F) return Cls::Neutral;   // general punctuation (the marks above are handled first)
    if (cp >= 0x3000 && cp <= 0x303F) return Cls::Neutral;   // CJK punctuation
    if (cp >= 0x2190 && cp <= 0x2BFF) return Cls::Neutral;   // arrows, math, boxes, symbols
    return Cls::L;
}

struct Unit {
    std::string text;
    Cls cls = Cls::Neutral;
    int level = 0;
    bool bracket = false;   // a one-character bracket that mirrors in a right-to-left run
};

std::string MirroredBracket(const std::string& s) {
    if (s.size() != 1) return s;
    switch (s[0]) {
        case '(': return ")";
        case ')': return "(";
        case '[': return "]";
        case ']': return "[";
        case '<': return ">";
        case '>': return "<";
        case '{': return "}";
        case '}': return "{";
        default: return s;
    }
}

std::vector<Unit> SplitUnits(const std::string& line) {
    std::vector<Unit> units;
    for (std::size_t i = 0; i < line.size();) {
        if (line[i] == '{' && !(i + 1 < line.size() && line[i + 1] == '{')) {
            int depth = 0;
            std::size_t j = i;
            for (; j < line.size(); ++j) {
                if (line[j] == '{') ++depth;
                else if (line[j] == '}' && --depth == 0) break;
            }
            if (j < line.size()) {
                Unit u;
                u.text = line.substr(i, j - i + 1);
                u.cls = Cls::L;   // a placeholder is a left-to-right island
                units.push_back(std::move(u));
                i = j + 1;
                continue;
            }
        }
        std::size_t next = i;
        const std::uint32_t cp = okay::utf8::Next(line, next);
        const std::string piece = line.substr(i, next - i);
        i = next;
        if (IsMark(cp) && !units.empty()) { units.back().text += piece; continue; }
        Unit u;
        u.text = piece;
        u.cls = Classify(cp);
        u.bracket = piece.size() == 1 && std::string("()[]<>{}").find(piece[0]) != std::string::npos;
        units.push_back(std::move(u));
    }
    return units;
}

std::string ReorderLine(const std::string& line, bool rtl) {
    std::vector<Unit> u = SplitUnits(line);
    if (u.empty()) return line;
    const Cls para = rtl ? Cls::R : Cls::L;
    // Weak rule: a European digit after a strong left-to-right letter reads as part of it.
    Cls lastStrong = para;
    for (Unit& x : u) {
        if (x.cls == Cls::L || x.cls == Cls::R) lastStrong = x.cls;
        else if (x.cls == Cls::EN && lastStrong == Cls::L) x.cls = Cls::L;
    }
    // Neutrals: between two runs of one direction they join it (digits count as right to left),
    // otherwise they take the paragraph direction.
    auto dirOf = [](Cls c) { return c == Cls::L ? Cls::L : Cls::R; };
    for (std::size_t i = 0; i < u.size(); ++i) {
        if (u[i].cls != Cls::Neutral) continue;
        std::size_t j = i;
        while (j < u.size() && u[j].cls == Cls::Neutral) ++j;
        const Cls before = i == 0 ? para : dirOf(u[i - 1].cls);
        const Cls after = j == u.size() ? para : dirOf(u[j].cls);
        const Cls resolved = before == after ? before : para;
        for (std::size_t k = i; k < j; ++k) u[k].level = resolved == Cls::L ? (rtl ? 2 : 0) : 1;
        i = j - 1;
    }
    for (Unit& x : u) {
        switch (x.cls) {
            case Cls::L: x.level = rtl ? 2 : 0; break;
            case Cls::R: x.level = 1; break;
            case Cls::EN: case Cls::AN: x.level = 2; break;
            case Cls::Neutral: break;   // set above
        }
        if (x.bracket && (x.level & 1)) x.text = MirroredBracket(x.text);
    }
    int highest = 0, lowestOdd = 1000;
    for (const Unit& x : u) {
        highest = std::max(highest, x.level);
        if (x.level & 1) lowestOdd = std::min(lowestOdd, x.level);
    }
    for (int level = highest; level >= lowestOdd && level >= 1; --level) {
        for (std::size_t i = 0; i < u.size();) {
            if (u[i].level < level) { ++i; continue; }
            std::size_t j = i;
            while (j < u.size() && u[j].level >= level) ++j;
            std::reverse(u.begin() + (std::ptrdiff_t)i, u.begin() + (std::ptrdiff_t)j);
            i = j;
        }
    }
    std::string out;
    for (const Unit& x : u) out += x.text;
    return out;
}

} // namespace

std::string LocVisualOrder(const std::string& text, bool rtlParagraph) {
    std::string out;
    std::size_t from = 0;
    for (;;) {
        const std::size_t nl = text.find('\n', from);
        out += ReorderLine(text.substr(from, nl == std::string::npos ? std::string::npos : nl - from), rtlParagraph);
        if (nl == std::string::npos) break;
        out += '\n';
        from = nl + 1;
    }
    return out;
}

bool LocHasRtlText(const std::string& text) {
    for (std::size_t i = 0; i < text.size();)
        if (Classify(okay::utf8::Next(text, i)) == Cls::R) return true;
    return false;
}

} // namespace oked
