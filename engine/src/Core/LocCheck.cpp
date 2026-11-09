#include "okay/Core/LocCheck.hpp"
#include "okay/Core/Utf8.hpp"
#include "okay/Graphics/TtfFont.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace okay {

namespace {

constexpr int kMaxDepth = 16;   // nested plural blocks, as Localization::Format allows
const std::size_t npos = std::string::npos;

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string BaseLanguage(const std::string& code) {
    const std::size_t dash = code.find_first_of("-_");
    return Lower(dash == npos ? code : code.substr(0, dash));
}

// The index of the } closing the { at `open`, or npos.
std::size_t MatchBrace(const std::string& s, std::size_t open) {
    int depth = 0;
    for (std::size_t i = open; i < s.size(); ++i) {
        if (s[i] == '{') ++depth;
        else if (s[i] == '}' && --depth == 0) return i;
    }
    return npos;
}

// The first comma outside braces at or after `from`.
std::size_t TopLevelComma(const std::string& s, std::size_t from = 0) {
    int depth = 0;
    for (std::size_t i = from; i < s.size(); ++i) {
        if (s[i] == '{') ++depth;
        else if (s[i] == '}') --depth;
        else if (s[i] == ',' && depth == 0) return i;
    }
    return npos;
}

bool ParseText(const std::string& s, int depth, std::vector<LocNode>& out, std::string& err) {
    bool ok = true;
    std::string text;
    auto flush = [&] {
        if (text.empty()) return;
        LocNode n;
        n.text = std::move(text);
        out.push_back(std::move(n));
        text.clear();
    };
    auto fail = [&](const char* why) { ok = false; if (err.empty()) err = why; };
    for (std::size_t i = 0; i < s.size();) {
        const char c = s[i];
        if (c == '{' && i + 1 < s.size() && s[i + 1] == '{') { text += "{{"; i += 2; continue; }
        if (c == '}' && i + 1 < s.size() && s[i + 1] == '}') { text += "}}"; i += 2; continue; }
        if (c == '}') { text += c; ++i; fail("a } without a {"); continue; }
        if (c != '{') { text += c; ++i; continue; }
        const std::size_t close = MatchBrace(s, i);
        if (close == npos) {
            fail("a { without a }");
            text.append(s, i, npos);
            break;
        }
        flush();
        const std::string inner = s.substr(i + 1, close - i - 1);
        i = close + 1;
        const std::size_t c1 = TopLevelComma(inner);
        LocNode n;
        n.name = Trim(inner.substr(0, c1));
        const std::size_t c2 = c1 == npos ? npos : TopLevelComma(inner, c1 + 1);
        if (c2 != npos && Trim(inner.substr(c1 + 1, c2 - c1 - 1)) == "plural" && depth < kMaxDepth) {
            n.kind = LocNode::Kind::Plural;
            const std::string body = inner.substr(c2 + 1);
            for (std::size_t p = 0; p < body.size();) {
                while (p < body.size() && std::isspace((unsigned char)body[p])) ++p;
                if (p >= body.size()) break;
                const std::size_t open = body.find('{', p);
                if (open == npos) { fail("a plural form without {text}"); break; }
                const std::size_t end = MatchBrace(body, open);
                if (end == npos) { fail("a plural form without its }"); break; }
                std::vector<LocNode> sub;
                if (!ParseText(body.substr(open + 1, end - open - 1), depth + 1, sub, err)) ok = false;
                n.forms.emplace_back(Trim(body.substr(p, open - p)), std::move(sub));
                p = end + 1;
            }
        } else {
            n.kind = LocNode::Kind::Arg;
            n.text = "{" + inner + "}";
        }
        out.push_back(std::move(n));
    }
    flush();
    return ok;
}

void CollectNames(const std::vector<LocNode>& nodes, std::set<std::string>& out) {
    for (const LocNode& n : nodes) {
        if (n.kind == LocNode::Kind::Text) continue;
        if (!n.name.empty()) out.insert(n.name);
        for (const auto& f : n.forms) CollectNames(f.second, out);
    }
}

struct PluralUse { std::string name; std::set<std::string> selectors; };
void CollectPlurals(const std::vector<LocNode>& nodes, std::vector<PluralUse>& out) {
    for (const LocNode& n : nodes) {
        if (n.kind != LocNode::Kind::Plural) continue;
        PluralUse u;
        u.name = n.name;
        for (const auto& f : n.forms) u.selectors.insert(f.first);
        out.push_back(std::move(u));
        for (const auto& f : n.forms) CollectPlurals(f.second, out);
    }
}

// ---- Widths ---------------------------------------------------------------------------
int CellWidth(std::uint32_t cp) {
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0591 && cp <= 0x05C7) || (cp >= 0x064B && cp <= 0x065F) ||
        (cp >= 0x200B && cp <= 0x200F) || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF)
        return 0;
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x20000 && cp <= 0x3FFFD))
        return 2;
    return 1;
}

int LineWidth(const std::string& line) {
    int w = 0;
    for (std::size_t i = 0; i < line.size();) w += CellWidth(utf8::Next(line, i));
    return w;
}

int WidestLine(const std::string& text) {
    int best = 0;
    std::size_t from = 0;
    for (;;) {
        const std::size_t nl = text.find('\n', from);
        const std::string line = text.substr(from, nl == npos ? npos : nl - from);
        best = std::max(best, LineWidth(line));
        if (nl == npos) break;
        from = nl + 1;
    }
    return best;
}

// A message as it would roughly read: arguments as three characters, a plural block as its
// longest form, doubled braces as one.
std::string Flatten(const std::vector<LocNode>& nodes) {
    std::string out;
    for (const LocNode& n : nodes) {
        switch (n.kind) {
            case LocNode::Kind::Text:
                for (std::size_t i = 0; i < n.text.size(); ++i) {
                    out += n.text[i];
                    if ((n.text[i] == '{' || n.text[i] == '}') && i + 1 < n.text.size() && n.text[i + 1] == n.text[i]) ++i;
                }
                break;
            case LocNode::Kind::Arg:
                out += "###";
                break;
            case LocNode::Kind::Plural: {
                std::string best;
                int bestWidth = -1;
                for (const auto& f : n.forms) {
                    std::string flat = Flatten(f.second);
                    const int w = WidestLine(flat);
                    if (w > bestWidth) { bestWidth = w; best = std::move(flat); }
                }
                out += best;
                break;
            }
        }
    }
    return out;
}

bool NodesHaveLetters(const std::vector<LocNode>& nodes) {
    for (const LocNode& n : nodes) {
        if (n.kind == LocNode::Kind::Text) {
            for (std::size_t i = 0; i < n.text.size();) {
                const std::uint32_t cp = utf8::Next(n.text, i);
                if (cp >= 0x80 || std::isalpha((int)cp)) return true;
            }
        } else {
            for (const auto& f : n.forms) if (NodesHaveLetters(f.second)) return true;
        }
    }
    return false;
}

// ---- Plural categories ------------------------------------------------------------------
const char* const kPluralOrder[] = {"zero", "one", "two", "few", "many", "other"};

const std::map<std::string, std::vector<std::string>>& PluralTable() {
    static const std::vector<std::string> other = {"other"};
    static const std::vector<std::string> oneFewManyOther = {"one", "few", "many", "other"};
    static const std::vector<std::string> oneFewOther = {"one", "few", "other"};
    static const std::vector<std::string> arabic = {"zero", "one", "two", "few", "many", "other"};
    static const std::map<std::string, std::vector<std::string>> t = {
        {"ja", other}, {"zh", other}, {"ko", other}, {"vi", other}, {"th", other}, {"id", other},
        {"ms", other}, {"km", other}, {"lo", other}, {"my", other},
        {"ru", oneFewManyOther}, {"uk", oneFewManyOther}, {"be", oneFewManyOther}, {"pl", oneFewManyOther},
        {"cs", oneFewManyOther}, {"sk", oneFewManyOther}, {"lt", oneFewManyOther},
        {"sl", {"one", "two", "few", "other"}},
        {"hr", oneFewOther}, {"sr", oneFewOther}, {"bs", oneFewOther}, {"ro", oneFewOther},
        {"lv", {"zero", "one", "other"}},
        {"ar", arabic}, {"cy", arabic},
        {"he", {"one", "two", "other"}},
        {"ga", {"one", "two", "few", "many", "other"}},
    };
    return t;
}

std::string CsvCell(const std::string& v) {
    if (v.find_first_of(",\"\r\n") == npos) return v;
    std::string out = "\"";
    for (char c : v) { if (c == '"') out += '"'; out += c; }
    return out + "\"";
}

std::string Names(const std::set<std::string>& s) {
    std::string out;
    for (const auto& n : s) { if (!out.empty()) out += ", "; out += "{" + n + "}"; }
    return out;
}

std::string Edge(const std::string& s, bool leading) {
    std::size_t n = 0;
    if (leading) { while (n < s.size() && std::isspace((unsigned char)s[n])) ++n; return s.substr(0, n); }
    while (n < s.size() && std::isspace((unsigned char)s[s.size() - 1 - n])) ++n;
    return s.substr(s.size() - n);
}

std::string DescribeSpace(const std::string& e) {
    if (e.empty()) return "none";
    std::string out;
    for (char c : e) out += c == ' ' ? "space," : c == '\t' ? "tab," : c == '\n' ? "newline," : c == '\r' ? "return," : "blank,";
    out.pop_back();
    return out;
}

} // namespace

// =====================================================================================
// Messages
// =====================================================================================
bool LocParseMessage(const std::string& message, std::vector<LocNode>& out, std::string* err) {
    out.clear();
    std::string e;
    const bool ok = ParseText(message, 0, out, e);
    if (err) *err = e;
    return ok;
}

std::set<std::string> LocPlaceholders(const std::string& message) {
    std::vector<LocNode> nodes;
    std::string err;
    ParseText(message, 0, nodes, err);
    std::set<std::string> names;
    CollectNames(nodes, names);
    return names;
}

std::vector<std::set<std::string>> LocPluralSelectors(const std::string& message) {
    std::vector<LocNode> nodes;
    std::string err;
    ParseText(message, 0, nodes, err);
    std::vector<PluralUse> uses;
    CollectPlurals(nodes, uses);
    std::vector<std::set<std::string>> out;
    for (auto& u : uses) out.push_back(std::move(u.selectors));
    return out;
}

int LocMessageWidth(const std::string& message) {
    std::vector<LocNode> nodes;
    std::string err;
    ParseText(message, 0, nodes, err);
    return WidestLine(Flatten(nodes));
}

bool LocHasLetters(const std::string& text) {
    std::vector<LocNode> nodes;
    std::string err;
    ParseText(text, 0, nodes, err);
    return NodesHaveLetters(nodes);
}

std::vector<std::string> LocRequiredPlurals(const std::string& lang) {
    std::set<std::string> need = {"other"};
    const auto& table = PluralTable();
    auto it = table.find(BaseLanguage(lang));
    if (it != table.end()) need.insert(it->second.begin(), it->second.end());
    else { need.insert("one"); }
    // What the installed rule for the language produces.
    for (int n = 0; n <= 120; ++n) need.insert(Localization::PluralName(Localization::Plural(lang, (double)n)));
    need.insert(Localization::PluralName(Localization::Plural(lang, 0.5)));
    need.insert(Localization::PluralName(Localization::Plural(lang, 1.5)));
    std::vector<std::string> out;
    for (const char* name : kPluralOrder) if (need.count(name)) out.push_back(name);
    return out;
}

std::vector<LocPluralGroup> LocPluralGroups(const StringTable& table) {
    std::map<std::string, LocPluralGroup> by;
    for (const auto& kv : table.Entries()) {
        const std::string& key = kv.first;
        const std::size_t dot = key.rfind('.');
        if (dot == npos || dot == 0) continue;
        const std::string cat = key.substr(dot + 1);
        bool known = false;
        for (const char* name : kPluralOrder) if (cat == name) known = true;
        if (!known) continue;
        LocPluralGroup& g = by[key.substr(0, dot)];
        g.base = key.substr(0, dot);
        g.forms[cat] = key;
    }
    std::vector<LocPluralGroup> out;
    for (auto& kv : by) if (kv.second.forms.size() >= 2) out.push_back(std::move(kv.second));
    return out;
}

// =====================================================================================
// LocReport
// =====================================================================================
const char* LocSeverityName(LocSeverity s) {
    switch (s) {
        case LocSeverity::Info: return "info";
        case LocSeverity::Warning: return "warning";
        case LocSeverity::Error: break;
    }
    return "error";
}

const char* LocIssueKindName(LocIssueKind k) {
    switch (k) {
        case LocIssueKind::MissingText: return "missing";
        case LocIssueKind::PlaceholderMismatch: return "placeholder";
        case LocIssueKind::UnbalancedBraces: return "braces";
        case LocIssueKind::PluralFormMissing: return "plural";
        case LocIssueKind::PluralKeyMissing: return "plural-key";
        case LocIssueKind::TrailingWhitespace: return "whitespace";
        case LocIssueKind::LengthGrowth: return "length";
        case LocIssueKind::SameAsSource: return "same-as-source";
        case LocIssueKind::ExtraText: break;
    }
    return "extra";
}

int LocReport::Count(LocIssueKind k) const {
    int n = 0;
    for (const auto& i : issues) if (i.kind == k) ++n;
    return n;
}
int LocReport::Count(LocSeverity s) const {
    int n = 0;
    for (const auto& i : issues) if (i.severity == s) ++n;
    return n;
}
std::vector<LocIssue> LocReport::ForLanguage(const std::string& lang) const {
    std::vector<LocIssue> out;
    for (const auto& i : issues) if (i.language == lang) out.push_back(i);
    return out;
}
std::vector<LocIssue> LocReport::ForKey(const std::string& key) const {
    std::vector<LocIssue> out;
    for (const auto& i : issues) if (i.key == key) out.push_back(i);
    return out;
}

std::string LocReport::ToText() const {
    std::ostringstream o;
    for (const auto& i : issues)
        o << LocSeverityName(i.severity) << "  " << i.language << "  " << i.key << "  " << i.message << "\n";
    o << issues.size() << (issues.size() == 1 ? " issue" : " issues") << " (" << Count(LocSeverity::Error)
      << " errors, " << Count(LocSeverity::Warning) << " warnings, " << Count(LocSeverity::Info) << " notes) in "
      << keys << " keys, " << languages.size() << " languages\n";
    return o.str();
}

std::string LocReport::ToCsv() const {
    std::string out = "\xEF\xBB\xBF" "severity,kind,language,key,message\r\n";
    for (const auto& i : issues)
        out += std::string(LocSeverityName(i.severity)) + "," + LocIssueKindName(i.kind) + "," + CsvCell(i.language) + "," +
               CsvCell(i.key) + "," + CsvCell(i.message) + "\r\n";
    return out;
}

// =====================================================================================
// LocCheck
// =====================================================================================
LocReport LocCheck(const StringTable& t, const LocCheckOptions& o) {
    LocReport r;
    r.keys = (int)t.Size();
    const std::string src = o.source.empty() ? t.DefaultLanguage() : o.source;
    if (o.languages.empty()) for (const auto& l : t.languages) r.languages.push_back(l.code);
    else for (const auto& c : o.languages) if (t.FindLanguage(c)) r.languages.push_back(c);

    auto add = [&](LocIssueKind kind, LocSeverity sev, const std::string& key, const std::string& lang, std::string msg) {
        r.issues.push_back(LocIssue{kind, sev, key, lang, std::move(msg)});
    };
    const std::vector<LocPluralGroup> groups = o.plurals ? LocPluralGroups(t) : std::vector<LocPluralGroup>{};

    for (const std::string& lang : r.languages) {
        const std::vector<std::string> required = LocRequiredPlurals(lang);
        std::string requiredList;
        for (const auto& n : required) { if (!requiredList.empty()) requiredList += ", "; requiredList += n; }
        for (const auto& kv : t.Entries()) {
            const std::string& key = kv.first;
            const std::string* text = t.Text(key, lang);
            if (!text) {
                if (o.missing) add(LocIssueKind::MissingText, LocSeverity::Warning, key, lang,
                                   lang == src ? "has no " + lang + " text (the source language)" : "has no " + lang + " text");
                continue;
            }
            const std::string* source = lang == src ? nullptr : t.Text(key, src);

            std::vector<LocNode> nodes;
            std::string perr;
            const bool balanced = LocParseMessage(*text, nodes, &perr);
            if (!balanced && o.placeholders)
                add(LocIssueKind::UnbalancedBraces, LocSeverity::Error, key, lang, perr + "; write {{ or }} for a literal brace");

            if (o.plurals) {
                std::vector<PluralUse> uses;
                CollectPlurals(nodes, uses);
                for (const PluralUse& u : uses) {
                    std::string lacks;
                    for (const std::string& need : required)
                        if (!u.selectors.count(need)) { if (!lacks.empty()) lacks += ", "; lacks += need; }
                    if (!lacks.empty())
                        add(LocIssueKind::PluralFormMissing, LocSeverity::Error, key, lang,
                            "the plural of {" + u.name + "} lacks " + lacks + " (" + lang + " needs " + requiredList + ")");
                }
            }

            if (lang == src) continue;
            if (!source) {
                add(LocIssueKind::ExtraText, LocSeverity::Info, key, lang, "has text, but the source language (" + src + ") has none");
                continue;
            }
            if (o.placeholders && balanced) {   // unbalanced braces are reported above, not as lost placeholders
                const std::set<std::string> a = LocPlaceholders(*source), b = LocPlaceholders(*text);
                if (a != b) {
                    std::set<std::string> lost, added;
                    for (const auto& n : a) if (!b.count(n)) lost.insert(n);
                    for (const auto& n : b) if (!a.count(n)) added.insert(n);
                    std::string msg;
                    if (!lost.empty()) msg += Names(lost) + (lost.size() == 1 ? " is" : " are") + " missing";
                    if (!added.empty()) msg += std::string(msg.empty() ? "" : "; ") + Names(added) + (added.size() == 1 ? " isn't" : " aren't") + " in the " + src + " text";
                    add(LocIssueKind::PlaceholderMismatch, LocSeverity::Error, key, lang, msg);
                }
            }
            if (o.whitespace) {
                const std::string ls = Edge(*source, true), lt = Edge(*text, true), ts = Edge(*source, false), tt = Edge(*text, false);
                if (tt != ts)
                    add(LocIssueKind::TrailingWhitespace, LocSeverity::Warning, key, lang,
                        "trailing whitespace differs from the " + src + " text (" + DescribeSpace(ts) + " there, " + DescribeSpace(tt) + " here)");
                if (lt != ls)
                    add(LocIssueKind::TrailingWhitespace, LocSeverity::Warning, key, lang,
                        "leading whitespace differs from the " + src + " text (" + DescribeSpace(ls) + " there, " + DescribeSpace(lt) + " here)");
            }
            if (o.growth) {
                const int ws = LocMessageWidth(*source), wt = LocMessageWidth(*text);
                if (ws > 0 && (float)wt > (float)ws * o.growthWarn && wt > ws + o.growthSlack)
                    add(LocIssueKind::LengthGrowth, LocSeverity::Warning, key, lang,
                        "is " + std::to_string(wt) + " wide against " + std::to_string(ws) + " in " + src + ": may not fit");
            }
            if (o.sameAsSource && *text == *source && !o.sameOk.count(key) && LocHasLetters(*source))
                add(LocIssueKind::SameAsSource, LocSeverity::Info, key, lang, "is the same as the " + src + " text: untranslated?");
        }

        // key.one / key.few... groups: each form the language needs must exist.
        for (const LocPluralGroup& g : groups) {
            bool started = false;
            for (const auto& f : g.forms) if (t.Text(f.second, lang)) started = true;
            if (!started) continue;   // nothing translated yet: the missing texts say so
            for (const std::string& need : required) {
                if (g.forms.count(need)) continue;   // the key is there: a missing text is reported above
                add(LocIssueKind::PluralKeyMissing, LocSeverity::Warning, g.base + "." + need, lang,
                    "add the key " + g.base + "." + need + ": " + lang + " needs the plural form \"" + need + "\"");
            }
        }
    }

    // Stable order: language (table order), then key.
    std::map<std::string, int> order;
    for (std::size_t i = 0; i < r.languages.size(); ++i) order[r.languages[i]] = (int)i;
    std::stable_sort(r.issues.begin(), r.issues.end(), [&](const LocIssue& a, const LocIssue& b) {
        const int la = order[a.language], lb = order[b.language];
        if (la != lb) return la < lb;
        return a.key < b.key;
    });
    return r;
}

// =====================================================================================
// Missing keys
// =====================================================================================
std::vector<LocMissing> LocMissingReport(const StringTable& t, const std::vector<std::string>& languages) {
    std::vector<std::string> langs = languages;
    if (langs.empty()) {
        const std::string def = t.DefaultLanguage();
        for (const auto& l : t.languages) if (l.code != def) langs.push_back(l.code);
    }
    std::vector<LocMissing> out;
    for (const std::string& lang : langs) {
        if (!t.FindLanguage(lang)) continue;
        LocMissing m;
        m.language = lang;
        m.total = (int)t.Size();
        for (const auto& kv : t.Entries()) if (!t.Text(kv.first, lang)) m.keys.push_back(kv.first);
        m.completion = t.Completion(lang);
        out.push_back(std::move(m));
    }
    return out;
}

std::string LocMissingToText(const std::vector<LocMissing>& report, bool listKeys) {
    std::ostringstream o;
    for (const LocMissing& m : report) {
        char pct[32];
        std::snprintf(pct, sizeof pct, "%.1f", m.completion * 100.0f);
        o << m.language << ": " << m.keys.size() << " of " << m.total << " keys missing (" << pct << "% done)\n";
        if (listKeys) for (const auto& k : m.keys) o << "  " << k << "\n";
    }
    return o.str();
}

// =====================================================================================
// Font coverage
// =====================================================================================
namespace {

bool Visible(std::uint32_t cp) {
    if (cp <= 0x20 || (cp >= 0x7F && cp <= 0xA0)) return false;
    if ((cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202F) || (cp >= 0x2060 && cp <= 0x206F)) return false;
    if ((cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF) return false;
    return true;
}

struct Seen { int count = 0; std::string firstKey; std::uint64_t mask = 0; };

} // namespace

LocFontReport LocFontCoverage(const StringTable& t, const LocFontOptions& o) {
    namespace fs = std::filesystem;
    LocFontReport report;
    for (const LocLanguage& lang : t.languages) {
        LocFontLanguage rep;
        rep.language = lang.code;

        std::vector<const TtfFont*> chain;
        if (!o.fonts.empty()) {
            for (const TtfFont* f : o.fonts) if (f && f->Valid()) chain.push_back(f);
        } else {
            if (!lang.font.empty()) {
                std::error_code ec;
                std::string path = lang.font;
                const fs::path p = fs::u8path(path);
                if (p.is_relative() && !fs::exists(p, ec) && !o.baseDir.empty()) {
                    const fs::path beside = fs::u8path(o.baseDir) / p;
                    if (fs::exists(beside, ec)) path = beside.u8string();
                }
                if (const TtfFont* f = GetFont(path)) chain.push_back(f);
                else rep.warnings.push_back("the font for " + lang.code + " (" + lang.font + ") couldn't be loaded");
            }
            if (const TtfFont* f = BuiltinUIFont()) chain.push_back(f);
            if (o.useSystemFonts)
                for (const std::string& path : SystemFallbackFonts())
                    if (const TtfFont* f = GetFont(path)) chain.push_back(f);
        }
        if (chain.size() > 64) chain.resize(64);
        if (!chain.empty()) rep.primary = chain.front()->Name();
        if (chain.empty()) rep.warnings.push_back("no font could be loaded to test " + lang.code + " against");

        auto maskOf = [&](std::uint32_t cp) {
            std::uint64_t m = 0;
            for (std::size_t i = 0; i < chain.size(); ++i) if (chain[i]->Has(cp)) m |= (std::uint64_t)1 << i;
            return m;
        };

        std::map<std::uint32_t, Seen> seen;
        for (const auto& kv : t.Entries()) {
            const std::string* text = t.Text(kv.first, lang.code);
            if (!text) continue;
            std::uint64_t all = ~(std::uint64_t)0;
            bool any = false;
            for (std::size_t i = 0; i < text->size();) {
                const std::uint32_t cp = utf8::Next(*text, i);
                if (!Visible(cp)) continue;
                any = true;
                auto it = seen.find(cp);
                if (it == seen.end()) {
                    it = seen.emplace(cp, Seen{}).first;
                    it->second.firstKey = kv.first;
                    it->second.mask = maskOf(cp);
                }
                ++it->second.count;
                all &= it->second.mask;
            }
            if (any && all == 0) rep.undrawableTexts.push_back(kv.first);
        }

        rep.charactersUsed = (int)seen.size();
        for (const auto& s : seen) {
            LocGlyphGap g;
            g.codePoint = s.first;
            utf8::Append(g.utf8, s.first);
            g.count = s.second.count;
            g.firstKey = s.second.firstKey;
            if (s.second.mask == 0) rep.missing.push_back(g);
            else if (!(s.second.mask & 1) && !chain.empty()) rep.fallbacks.push_back(g);
        }
        auto byCount = [](const LocGlyphGap& a, const LocGlyphGap& b) {
            return a.count != b.count ? a.count > b.count : a.codePoint < b.codePoint;
        };
        std::sort(rep.missing.begin(), rep.missing.end(), byCount);
        std::sort(rep.fallbacks.begin(), rep.fallbacks.end(), byCount);
        report.languages.push_back(std::move(rep));
    }
    return report;
}

bool LocFontReport::Clean() const {
    for (const auto& l : languages) if (!l.missing.empty() || !l.undrawableTexts.empty()) return false;
    return true;
}

std::string LocFontReport::ToText() const {
    std::ostringstream o;
    for (const auto& l : languages) {
        o << l.language << ": " << l.charactersUsed << " characters";
        if (!l.primary.empty()) o << ", first font " << l.primary;
        o << "\n";
        for (const auto& w : l.warnings) o << "  warning: " << w << "\n";
        if (!l.missing.empty()) {
            o << "  no font draws " << l.missing.size() << " characters:";
            for (const auto& g : l.missing) o << " " << g.utf8 << " (U+" << std::hex << std::uppercase << g.codePoint << std::dec << ")";
            o << "\n";
        }
        if (!l.fallbacks.empty()) {
            o << "  the first font lacks " << l.fallbacks.size() << " characters that a fallback font draws:";
            for (const auto& g : l.fallbacks) o << " " << g.utf8;
            o << "\n";
        }
        if (!l.undrawableTexts.empty()) {
            o << "  no single font draws these texts completely:";
            for (const auto& k : l.undrawableTexts) o << " " << k;
            o << "\n";
        }
    }
    return o.str();
}

} // namespace okay
