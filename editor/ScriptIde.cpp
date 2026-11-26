// ScriptIde (ScriptIde.hpp): the Script Editor's workspace model. No GUI; tests/test_script_ide.cpp runs it headless.
// Written from general ideas (subsequence matching, identifier indexes, an LCS line diff), no code from other editors.
#include "ScriptIde.hpp"
#include "ScriptText.hpp"                        // scripttext::Outline, CodeMask, IsValidNewName (OkayScript's view of a script)
#include "VisualScriptSearchNav.hpp"             // okay::editor::vsed::searchnav::EditDistance
#include "okay/Scripting/StarterScripts.hpp"     // okay::StarterScriptSource: the first template of each language
#include "okay/Lua/Lua.hpp"                      // okay::lua::kKeywords: Lua's reserved words
#include "okay/Core/StringUtil.hpp"               // okay::str::LowerAscii
#include "../shared/OkayThreads.hpp"             // okay::thr: the symbol index builds on a worker (rule 21a: no <thread>)

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace scriptide {

namespace fs = std::filesystem;

static bool IsIdent(char c) { return std::isalnum((unsigned char)c) || c == '_'; }
static bool IsIdentStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }

// ---- fuzzy ----------------------------------------------------------------------------------------------------------------
int FuzzyScore(const std::string& pat, const std::string& cand) {
    if (pat.empty()) return 0;
    int score = 0, run = 0, firstAt = -1;
    std::size_t ci = 0;
    for (std::size_t pi = 0; pi < pat.size(); ++pi) {
        const char pc = (char)std::tolower((unsigned char)pat[pi]);
        if (pc == ' ') continue;                                   // spaces in a query only separate words
        bool found = false;
        for (; ci < cand.size(); ++ci) {
            if ((char)std::tolower((unsigned char)cand[ci]) != pc) { run = 0; continue; }
            if (firstAt < 0) firstAt = (int)ci;
            const char prev = ci ? cand[ci - 1] : ' ';
            const bool wordStart = ci == 0 || prev == '_' || prev == '.' || prev == '/' || prev == '\\' || prev == ' ' || prev == '-' ||
                                   (std::islower((unsigned char)prev) && std::isupper((unsigned char)cand[ci]));
            score += 1 + run * 2 + (wordStart ? 4 : 0);
            ++run; ++ci; found = true;
            break;
        }
        if (!found) return -1;
    }
    if (firstAt == 0) score += 6;
    score -= firstAt > 0 ? firstAt : 0;   // a match that starts late and scatters can fall below 0: not a match
    return score;
}

// ---- languages ------------------------------------------------------------------------------------------------------------
std::string LanguageForPath(const std::string& path) {
    const std::string ext = okay::str::LowerAscii(fs::path(path).extension().string());
    if (ext == ".okay") return "okayscript";
    if (ext == ".lua") return "lua";
    if (ext == ".cs") return "csharp";
    return "";
}
std::string LineCommentFor(const std::string& language) { return language == "lua" ? "--" : "//"; }

// Lua (the Lua 5.4 manual, section 3.1): "--" line comments, "--[[ ]]" / "--[==[ ]==]" block comments, '...' and "..."
// strings with escapes, [[ ]] / [==[ ]==] long strings.
static std::vector<char> LuaCodeMask(const std::string& s) {
    const std::size_t n = s.size();
    std::vector<char> m(n, 1);
    auto longBracket = [&](std::size_t i, int& level) {   // at '[': "[[" or "[==[" ? -> its level
        if (i >= n || s[i] != '[') return false;
        std::size_t j = i + 1; level = 0;
        while (j < n && s[j] == '=') { ++level; ++j; }
        return j < n && s[j] == '[';
    };
    auto closeLong = [&](std::size_t i, int level) {     // the index after "]==]" from i, or n
        for (std::size_t k = i; k < n; ++k) {
            if (s[k] != ']') continue;
            std::size_t j = k + 1; int l = 0;
            while (j < n && s[j] == '=') { ++l; ++j; }
            if (l == level && j < n && s[j] == ']') return j + 1;
        }
        return n;
    };
    for (std::size_t i = 0; i < n;) {
        int level = 0;
        if (s[i] == '-' && i + 1 < n && s[i + 1] == '-') {
            std::size_t e;
            if (longBracket(i + 2, level)) e = closeLong(i + 2, level);
            else { e = i; while (e < n && s[e] != '\n') ++e; }
            for (std::size_t k = i; k < e; ++k) if (s[k] != '\n') m[k] = 0;
            i = e; continue;
        }
        if (longBracket(i, level)) {
            const std::size_t e = closeLong(i, level);
            for (std::size_t k = i; k < e; ++k) if (s[k] != '\n') m[k] = 0;
            i = e; continue;
        }
        if (s[i] == '"' || s[i] == '\'') {
            const char q = s[i];
            std::size_t k = i + 1;
            while (k < n && s[k] != q && s[k] != '\n') { if (s[k] == '\\') ++k; ++k; }
            const std::size_t e = k < n && s[k] == q ? k + 1 : k;
            for (std::size_t j = i; j < e && j < n; ++j) m[j] = 0;
            i = e; continue;
        }
        ++i;
    }
    return m;
}

std::vector<char> CodeMaskFor(const std::string& language, const std::string& text) {
    if (language == "lua") return LuaCodeMask(text);
    return scripttext::CodeMask(text);
}

bool IsKeyword(const std::string& language, const std::string& w) {
    if (language != "lua") return scripttext::IsKeywordWord(w);
    if (w == "self") return true;                               // the method receiver: never a symbol of its own
    for (const char* k : okay::lua::kKeywords) if (w == k) return true;
    return false;
}

// ---- recent files ---------------------------------------------------------------------------------------------------------
void RecentFiles::Push(const std::string& path) {
    if (path.empty()) return;
    Remove(path);
    items_.insert(items_.begin(), path);
    if (items_.size() > cap_) items_.resize(cap_);
}
void RecentFiles::Remove(const std::string& path) { items_.erase(std::remove(items_.begin(), items_.end(), path), items_.end()); }
std::string RecentFiles::Serialize() const {
    std::string out;
    for (const auto& p : items_) out += p + "\n";
    return out;
}
void RecentFiles::Parse(const std::string& text) {
    items_.clear();
    std::istringstream ss(text);
    std::string l;
    while (std::getline(ss, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (!l.empty() && std::find(items_.begin(), items_.end(), l) == items_.end() && items_.size() < cap_) items_.push_back(l);
    }
}

// ---- Quick Open -----------------------------------------------------------------------------------------------------------
QuickOpenQuery ParseQuickOpenQuery(const std::string& q) {
    QuickOpenQuery r{q, 0};
    const std::size_t c = q.rfind(':');
    if (c == std::string::npos || c + 1 >= q.size()) return r;
    for (std::size_t i = c + 1; i < q.size(); ++i) if (!std::isdigit((unsigned char)q[i])) return r;
    r.text = q.substr(0, c);
    r.line = std::atoi(q.c_str() + c + 1);
    return r;
}

static std::string Relative(const std::string& path, const std::string& root) {
    if (root.empty()) return path;
    std::error_code ec;
    const fs::path rel = fs::path(path).lexically_relative(fs::path(root));
    const std::string s = rel.generic_string();
    return s.empty() || s.rfind("..", 0) == 0 ? fs::path(path).generic_string() : s;
}

std::vector<QuickOpenItem> RankQuickOpen(const std::string& query, const std::vector<std::string>& files,
                                         const std::vector<std::string>& recent, const std::string& root, std::size_t maxItems) {
    std::vector<QuickOpenItem> out;
    std::unordered_map<std::string, int> recentRank;
    for (std::size_t i = 0; i < recent.size(); ++i) recentRank.emplace(recent[i], (int)i);
    const std::string q = ParseQuickOpenQuery(query).text;
    std::set<std::string> seen;
    auto consider = [&](const std::string& f) {
        if (!seen.insert(f).second) return;
        QuickOpenItem it;
        it.path = f;
        it.label = Relative(f, root);
        auto rr = recentRank.find(f);
        it.recent = rr != recentRank.end();
        if (q.empty()) {
            it.score = it.recent ? 100000 - rr->second : 0;
        } else {
            const int byName = FuzzyScore(q, fs::path(f).filename().string());
            const int byPath = FuzzyScore(q, it.label);
            if (byName < 0 && byPath < 0) return;
            it.score = byName >= 0 ? byName * 3 + 10 : byPath;
            if (it.recent) it.score += std::max(1, 8 - rr->second);
        }
        out.push_back(std::move(it));
    };
    for (const auto& f : files) consider(f);
    for (const auto& r : recent) {   // a recent file that is not in the list (outside the project folder) still shows
        std::error_code ec;
        if (!seen.count(r) && fs::exists(r, ec)) consider(r);
    }
    std::stable_sort(out.begin(), out.end(), [](const QuickOpenItem& a, const QuickOpenItem& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.label < b.label;
    });
    if (out.size() > maxItems) out.resize(maxItems);
    return out;
}

std::vector<std::string> ListProjectScripts(const std::string& root, std::size_t limit) {
    std::vector<std::string> out;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return out;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end && out.size() < limit; it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (it->is_directory(ec)) {
            if (!name.empty() && name[0] == '.') it.disable_recursion_pending();
            continue;
        }
        if (!LanguageForPath(name).empty() && it->is_regular_file(ec)) out.push_back(it->path().generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<TreeRow> BuildFileTree(const std::vector<std::string>& files, const std::string& root, const std::string& filter) {
    struct Node { std::map<std::string, Node> folders; std::vector<std::pair<std::string, std::string>> files; };
    Node top;
    const std::string f = okay::str::LowerAscii(filter);
    for (const auto& path : files) {
        const std::string rel = Relative(path, root);
        if (!f.empty() && okay::str::LowerAscii(rel).find(f) == std::string::npos) continue;
        Node* n = &top;
        std::size_t start = 0;
        for (std::size_t slash; (slash = rel.find('/', start)) != std::string::npos; start = slash + 1)
            n = &n->folders[rel.substr(start, slash - start)];
        n->files.push_back({rel.substr(start), path});
    }
    std::vector<TreeRow> rows;
    std::function<void(const Node&, int, const std::string&)> walk = [&](const Node& n, int depth, const std::string& prefix) {
        for (const auto& kv : n.folders) {
            const std::string p = prefix.empty() ? kv.first : prefix + "/" + kv.first;
            rows.push_back({kv.first, root.empty() ? p : (fs::path(root) / p).generic_string(), depth, true});
            walk(kv.second, depth + 1, p);
        }
        std::vector<std::pair<std::string, std::string>> fl = n.files;
        std::sort(fl.begin(), fl.end());
        for (const auto& kv : fl) rows.push_back({kv.first, kv.second, depth, false});
    };
    walk(top, 0, "");
    return rows;
}

// ---- symbols --------------------------------------------------------------------------------------------------------------
namespace {
struct Tk { std::string text; int line = 1, col = 0; std::size_t off = 0; bool ident = false; };
// Code tokens of a script: identifiers (split at dots) and single punctuation characters, comments and strings left out.
std::vector<Tk> CodeTokens(const std::string& language, const std::string& s) {
    const std::vector<char> code = CodeMaskFor(language, s);
    std::vector<Tk> out;
    int line = 1; std::size_t lineStart = 0;
    for (std::size_t i = 0; i < s.size();) {
        const char c = s[i];
        if (c == '\n') { ++line; lineStart = ++i; continue; }
        if (!code[i] || std::isspace((unsigned char)c)) { ++i; continue; }
        if (std::isdigit((unsigned char)c)) { while (i < s.size() && (IsIdent(s[i]) || s[i] == '.') && code[i]) ++i; continue; }
        if (IsIdentStart(c)) {
            std::size_t j = i;
            while (j < s.size() && IsIdent(s[j])) ++j;
            out.push_back({s.substr(i, j - i), line, (int)(i - lineStart), i, true});
            i = j; continue;
        }
        out.push_back({std::string(1, c), line, (int)(i - lineStart), i, false});
        ++i;
    }
    return out;
}
std::vector<Symbol> LuaOutline(const std::string& text, const std::string& file) {
    std::vector<Symbol> out;
    const std::vector<Tk> t = CodeTokens("lua", text);
    std::vector<std::size_t> lineStart{0};
    for (std::size_t i = 0; i < text.size(); ++i) if (text[i] == '\n') lineStart.push_back(i + 1);
    auto firstOnTopLine = [&](const Tk& k) {   // the token starts its line, at column 0
        return k.col == 0 && (std::size_t)k.line - 1 < lineStart.size();
    };
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i].text == "function" && t[i].ident && i + 1 < t.size() && t[i + 1].ident) {
            std::size_t k = i + 1;
            std::string display = t[k].text, name = t[k].text;
            while (k + 2 < t.size() && (t[k + 1].text == "." || t[k + 1].text == ":") && t[k + 2].ident) {
                display += t[k + 1].text + t[k + 2].text; name = t[k + 2].text; k += 2;
            }
            if (k + 1 < t.size() && t[k + 1].text == "(") out.push_back({name, display, 'f', file, t[i + 1].line});
            continue;
        }
        if (t[i].text == "local" && firstOnTopLine(t[i]) && i + 1 < t.size() && t[i + 1].ident && t[i + 1].text != "function") {
            const bool table = i + 3 < t.size() && t[i + 2].text == "=" && t[i + 3].text == "{";
            out.push_back({t[i + 1].text, t[i + 1].text, table ? 'C' : 'v', file, t[i + 1].line});
            continue;
        }
        if (t[i].ident && firstOnTopLine(t[i]) && !IsKeyword("lua", t[i].text) && i + 2 < t.size() && t[i + 1].text == "=" &&
            t[i + 2].text == "{" && (i == 0 || t[i - 1].line != t[i].line))
            out.push_back({t[i].text, t[i].text, 'C', file, t[i].line});
    }
    return out;
}
} // namespace

std::vector<Symbol> OutlineOf(const std::string& language, const std::string& text, const std::string& file) {
    if (language == "lua") return LuaOutline(text, file);
    std::vector<Symbol> out;
    for (const auto& e : scripttext::Outline(text)) {
        Symbol s;
        s.kind = e.second.empty() ? 'f' : e.second[0];
        s.display = e.second.size() > 3 ? e.second.substr(3) : e.second;
        const std::size_t dot = s.display.rfind('.');
        s.name = dot == std::string::npos ? s.display : s.display.substr(dot + 1);
        s.file = file; s.line = e.first;
        out.push_back(std::move(s));
    }
    return out;
}

void SymbolIndex::SetFile(const std::string& path, const std::string& text, const std::string& language) {
    FileData d;
    d.language = language.empty() ? LanguageForPath(path) : language;
    d.text = text;
    d.symbols = OutlineOf(d.language, text, path);
    for (const Tk& k : CodeTokens(d.language, text))
        if (k.ident) d.uses[k.text].push_back({path, k.line, k.col, k.off});
    files_[path] = std::move(d);
}
void SymbolIndex::RemoveFile(const std::string& path) { files_.erase(path); }
const std::string* SymbolIndex::Text(const std::string& path) const {
    auto it = files_.find(path);
    return it == files_.end() ? nullptr : &it->second.text;
}
std::vector<std::string> SymbolIndex::Files() const {
    std::vector<std::string> out;
    for (const auto& kv : files_) out.push_back(kv.first);
    return out;
}
std::vector<Symbol> SymbolIndex::Definitions(const std::string& name) const {
    std::vector<Symbol> out;
    for (const auto& kv : files_) for (const auto& s : kv.second.symbols) if (s.name == name || s.display == name) out.push_back(s);
    return out;
}
std::vector<Symbol> SymbolIndex::FileSymbols(const std::string& path) const {
    auto it = files_.find(path);
    return it == files_.end() ? std::vector<Symbol>{} : it->second.symbols;
}
std::vector<Symbol> SymbolIndex::AllSymbols() const {
    std::vector<Symbol> out;
    for (const auto& kv : files_) out.insert(out.end(), kv.second.symbols.begin(), kv.second.symbols.end());
    return out;
}
std::vector<Occurrence> SymbolIndex::References(const std::string& name) const {
    std::vector<Occurrence> out;
    for (const auto& kv : files_) {
        auto u = kv.second.uses.find(name);
        if (u != kv.second.uses.end()) out.insert(out.end(), u->second.begin(), u->second.end());
    }
    return out;
}
std::string SymbolIndex::WordAt(const std::string& path, int line, int col) const {
    const std::string* t = Text(path);
    if (!t) return "";
    std::size_t o = OffsetOf(*t, line, col);
    std::size_t a = o, b = o;
    while (a > 0 && IsIdent((*t)[a - 1])) --a;
    while (b < t->size() && IsIdent((*t)[b])) ++b;
    if (a == b || std::isdigit((unsigned char)(*t)[a])) return "";
    return t->substr(a, b - a);
}

static std::string LineText(const std::string& text, int line) {
    const std::size_t a = OffsetOf(text, line, 0);
    std::size_t b = text.find('\n', a);
    if (b == std::string::npos) b = text.size();
    return text.substr(a, b - a);
}

RenamePlan PlanRename(const SymbolIndex& index, const std::string& from, const std::string& to) {
    RenamePlan p;
    p.from = from; p.to = to;
    if (from == to) { p.error = "The new name is the same as the old one."; return p; }
    if (!scripttext::IsValidNewName(to) || IsKeyword("lua", to) || IsKeyword("okayscript", to)) {
        p.error = "'" + to + "' is not a valid name (letters, digits and _, not a keyword, not starting with a digit).";
        return p;
    }
    const std::vector<Occurrence> uses = index.References(from);
    if (uses.empty()) { p.error = "'" + from + "' is not used in any project script."; return p; }
    const std::vector<Occurrence> clash = index.References(to);
    if (!clash.empty()) {
        p.error = "'" + to + "' is already used in " + fs::path(clash[0].file).filename().string() + " line " +
                  std::to_string(clash[0].line) + ".";
        return p;
    }
    std::map<std::string, std::vector<TextEdit>> perFile;
    for (const auto& o : uses) perFile[o.file].push_back({o.line, o.col, (int)from.size(), to});
    for (auto& kv : perFile) {
        const std::string* text = index.Text(kv.first);
        if (!text) continue;
        const std::string after = ApplyEdits(*text, kv.second);
        p.newTexts[kv.first] = after;
        std::set<int> lines;
        for (const auto& e : kv.second) lines.insert(e.line);
        for (int ln : lines) p.changes.push_back({kv.first, ln, LineText(*text, ln), LineText(after, ln)});
        p.count += (int)kv.second.size();
    }
    return p;
}

// ---- the background index builder -----------------------------------------------------------------------------------------
struct SymbolIndexBuilder::Job {
    std::vector<std::string> files;
    SymbolIndex result;
    okay::thr::AtomicInt done;
    okay::thr::Thread* thread = nullptr;
    bool taken = false;
};
static int BuildIndexThread(void* p) {
    auto* j = static_cast<SymbolIndexBuilder::Job*>(p);
    for (const auto& f : j->files) {
        std::ifstream in(f, std::ios::binary);
        if (!in) continue;
        std::stringstream ss; ss << in.rdbuf();
        j->result.SetFile(f, ss.str());
    }
    okay::thr::AtomicSet(&j->done, 1);
    return 0;
}
SymbolIndexBuilder::SymbolIndexBuilder() = default;
SymbolIndexBuilder::~SymbolIndexBuilder() { Wait(); delete job_; }
bool SymbolIndexBuilder::Start(const std::vector<std::string>& files) {
    if (Busy()) return false;
    Wait();
    delete job_;
    job_ = new Job();
    job_->files = files;
    job_->thread = okay::thr::CreateThread(&BuildIndexThread, "symbol-index", job_);
    if (!job_->thread) BuildIndexThread(job_);   // no thread: build here (still correct, just on this thread)
    return true;
}
bool SymbolIndexBuilder::Busy() const { return job_ && okay::thr::AtomicGet(&job_->done) == 0; }
void SymbolIndexBuilder::Wait() {
    if (job_ && job_->thread) { okay::thr::WaitThread(job_->thread, nullptr); job_->thread = nullptr; }
}
bool SymbolIndexBuilder::Poll(SymbolIndex& out) {
    if (!job_ || job_->taken || okay::thr::AtomicGet(&job_->done) == 0) return false;
    Wait();
    out = std::move(job_->result);
    job_->taken = true;
    return true;
}

// ---- find and replace -----------------------------------------------------------------------------------------------------
namespace {
struct LineMap {
    std::vector<std::size_t> starts{0};
    explicit LineMap(const std::string& t) { for (std::size_t i = 0; i < t.size(); ++i) if (t[i] == '\n') starts.push_back(i + 1); }
    void Place(TextMatch& m) const {
        const auto it = std::upper_bound(starts.begin(), starts.end(), m.offset);
        const std::size_t li = (std::size_t)(it - starts.begin()) - 1;
        m.line = (int)li + 1; m.col = (int)(m.offset - starts[li]);
    }
};
bool WholeWordAt(const std::string& t, std::size_t a, std::size_t len) {
    return (a == 0 || !IsIdent(t[a - 1])) && (a + len >= t.size() || !IsIdent(t[a + len]));
}
// Every match as (offset, length, the regex match for $1 replacement).
bool Matches(const std::string& text, const std::string& needle, const FindOptions& opt, std::vector<TextMatch>& out,
             std::vector<std::smatch>* groups, std::string* error) {
    if (needle.empty()) return true;
    if (!opt.regex) {
        const std::string hay = opt.matchCase ? text : okay::str::LowerAscii(text), nd = opt.matchCase ? needle : okay::str::LowerAscii(needle);
        for (std::size_t p = hay.find(nd); p != std::string::npos; p = hay.find(nd, p + 1)) {
            if (opt.wholeWord && !WholeWordAt(text, p, nd.size())) continue;
            out.push_back({p, nd.size(), 0, 0});
            p += nd.size() - 1;
        }
        return true;
    }
    try {
        auto flags = std::regex::ECMAScript;
        if (!opt.matchCase) flags |= std::regex::icase;
        const std::regex re(opt.wholeWord ? "\\b(?:" + needle + ")\\b" : needle, flags);
        for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) {
            if (it->length(0) == 0) continue;   // an empty match finds nothing to show or replace
            out.push_back({(std::size_t)it->position(0), (std::size_t)it->length(0), 0, 0});
            if (groups) groups->push_back(*it);
        }
    } catch (const std::regex_error& e) {
        if (error) *error = std::string("Bad regular expression: ") + e.what();
        out.clear();
        return false;
    }
    return true;
}
} // namespace

std::vector<TextMatch> FindAll(const std::string& text, const std::string& needle, const FindOptions& opt, std::string* error) {
    std::vector<TextMatch> out;
    if (error) error->clear();
    if (!Matches(text, needle, opt, out, nullptr, error)) return {};
    const LineMap lm(text);
    for (auto& m : out) lm.Place(m);
    return out;
}

std::string ReplaceAll(const std::string& text, const std::string& needle, const std::string& replacement,
                       const FindOptions& opt, int* count, std::string* error) {
    if (count) *count = 0;
    if (error) error->clear();
    std::vector<TextMatch> ms;
    std::vector<std::smatch> groups;
    if (!Matches(text, needle, opt, ms, opt.regex ? &groups : nullptr, error) || ms.empty()) return text;
    std::string out;
    std::size_t at = 0;
    for (std::size_t i = 0; i < ms.size(); ++i) {
        out.append(text, at, ms[i].offset - at);
        out += opt.regex ? groups[i].format(replacement) : replacement;
        at = ms[i].offset + ms[i].length;
    }
    out.append(text, at, std::string::npos);
    if (count) *count = (int)ms.size();
    return out;
}

std::vector<FileMatch> FindInTexts(const std::map<std::string, std::string>& texts, const std::string& needle,
                                   const FindOptions& opt, std::size_t maxResults, std::string* error) {
    std::vector<FileMatch> out;
    for (const auto& kv : texts) {
        std::string err;
        for (const auto& m : FindAll(kv.second, needle, opt, &err)) {
            if (out.size() >= maxResults) return out;
            out.push_back({kv.first, m, LineText(kv.second, m.line)});
        }
        if (!err.empty()) { if (error) *error = err; return {}; }
    }
    return out;
}

// ---- edits and fixes ------------------------------------------------------------------------------------------------------
std::size_t OffsetOf(const std::string& text, int line, int col) {
    if (line < 1) return text.size();
    std::size_t o = 0;
    for (int l = 1; l < line; ++l) {
        const std::size_t nl = text.find('\n', o);
        if (nl == std::string::npos) return text.size();
        o = nl + 1;
    }
    std::size_t end = text.find('\n', o);
    if (end == std::string::npos) end = text.size();
    return std::min(o + (std::size_t)std::max(col, 0), end);
}

std::string ApplyEdits(const std::string& text, std::vector<TextEdit> edits) {
    struct E { std::size_t a, len; std::string t; };
    std::vector<E> es;
    for (const auto& e : edits) {
        const std::size_t a = OffsetOf(text, e.line, e.col);
        es.push_back({a, std::min((std::size_t)std::max(e.length, 0), text.size() - a), e.text});
    }
    std::stable_sort(es.begin(), es.end(), [](const E& x, const E& y) { return x.a > y.a; });
    std::string out = text;
    for (const auto& e : es) out.replace(e.a, e.len, e.t);
    return out;
}

std::string NearestName(const std::string& word, const std::vector<std::string>& known) {
    std::string best;
    int bestD = std::max(1, (int)word.size() / 3) + 1;
    const std::string lw = okay::str::LowerAscii(word);
    for (const auto& k : known) {
        if (k == word) return "";                                   // it is known: nothing to suggest
        const int d = okay::editor::vsed::searchnav::EditDistance(lw, okay::str::LowerAscii(k));   // a case slip counts as 0
        if (d < bestD) { bestD = d; best = k; }
    }
    return best;
}

QuickFix FixFromSuggestion(const Diagnostic& d, const std::string& text) {
    QuickFix f;
    const std::string key = "Did you mean ";
    const std::size_t at = d.message.find(key);
    if (at == std::string::npos || d.line < 1) return f;
    std::size_t e = at + key.size();
    std::string want;
    while (e < d.message.size() && (IsIdent(d.message[e]) || d.message[e] == '.' || d.message[e] == ':')) want += d.message[e++];
    if (want.empty()) return f;
    const std::string line = LineText(text, d.line);
    std::size_t c = d.col > 0 ? (std::size_t)d.col - 1 : line.find_first_not_of(" \t");
    if (c == std::string::npos || c >= line.size()) return f;
    while (c > 0 && IsIdent(line[c - 1])) --c;               // the whole word, wherever in it the column points
    std::size_t b = c;
    while (b < line.size() && IsIdent(line[b])) ++b;
    if (b == c) return f;
    // A suggestion with a dot replaces as much of the dotted name as it covers ("Input.GetKey" over "Input.GetKeyy").
    std::size_t a = c;
    if (want.find('.') != std::string::npos || want.find(':') != std::string::npos)
        while (a > 0 && (IsIdent(line[a - 1]) || line[a - 1] == '.' || line[a - 1] == ':')) --a;
    f.title = "Change to '" + want + "'";
    f.edits.push_back({d.line, (int)a, (int)(b - a), want});
    return f;
}

std::vector<Diagnostic> BasicLint(const std::string& language, const std::string& text, const std::vector<std::string>& known) {
    std::vector<Diagnostic> out;
    const bool lua = language == "lua";
    const std::vector<Tk> t = CodeTokens(language, text);
    std::unordered_set<std::string> knownSet(known.begin(), known.end());
    std::unordered_set<std::string> declared;
    std::unordered_map<std::string, int> useCount;
    for (const auto& s : OutlineOf(language, text)) declared.insert(s.name);
    auto isPunct = [&](std::size_t i, const char* p) { return i < t.size() && !t[i].ident && t[i].text == p; };
    auto member = [&](std::size_t i) { return i > 0 && (isPunct(i - 1, ".") || isPunct(i - 1, ":")); };
    auto assigned = [&](std::size_t i) { return isPunct(i + 1, "=") && !isPunct(i + 2, "=") && !isPunct(i + 2, ">"); };
    // A word that can stand before a declared name: a type (any non-keyword name, or a built-in type keyword) or a modifier.
    static const std::unordered_set<std::string> typeKw = {"var", "int", "float", "double", "bool", "string", "char", "long",
        "object", "const", "static", "public", "private", "protected", "readonly", "let", "out", "ref", "params", "void"};
    auto typeWord = [&](const std::string& w) { return !IsKeyword(language, w) || typeKw.count(w) != 0; };
    // Declarations: assignments, locals, parameters, loop variables, typed declarations.
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (!t[i].ident) continue;
        ++useCount[t[i].text];
        if (member(i)) continue;
        if (assigned(i)) declared.insert(t[i].text);
        if (lua && (t[i].text == "local" || t[i].text == "for")) {
            for (std::size_t k = i + 1; k < t.size() && (t[k].ident || isPunct(k, ",")); ++k)
                if (t[k].ident && t[k].text != "function" && t[k].text != "in") declared.insert(t[k].text); else if (!isPunct(k, ",")) break;
        }
        if (lua && t[i].text == "function") {   // parameters
            std::size_t k = i + 1;
            while (k < t.size() && !isPunct(k, "(")) ++k;
            for (++k; k < t.size() && !isPunct(k, ")"); ++k) if (t[k].ident) declared.insert(t[k].text);
        }
        if (!lua) {
            // `Type name` (a declaration, a parameter, foreach (var x in), catch (E e)), and a lambda's `x =>`.
            if (i + 1 < t.size() && t[i + 1].ident && t[i + 1].line == t[i].line && !IsKeyword(language, t[i + 1].text) &&
                typeWord(t[i].text))
                declared.insert(t[i + 1].text);
            if (isPunct(i + 1, "=") && isPunct(i + 2, ">")) declared.insert(t[i].text);
            // after a generic or array type: `List<Enemy> enemies;`, `int[] xs = ...`
            if (i > 0 && (isPunct(i - 1, ">") || isPunct(i - 1, "]")) && t[i - 1].line == t[i].line &&
                (isPunct(i + 1, ";") || isPunct(i + 1, "=") || isPunct(i + 1, ",") || isPunct(i + 1, ")")))
                declared.insert(t[i].text);
            // a bare function's parameters: name(a, b) {
            if (isPunct(i + 1, "(")) {
                std::size_t k = i + 2; int depth = 1;
                std::vector<std::string> params;
                for (; k < t.size() && depth > 0; ++k) {
                    if (isPunct(k, "(")) ++depth; else if (isPunct(k, ")")) --depth;
                    else if (t[k].ident && depth == 1) params.push_back(t[k].text);
                }
                if (isPunct(k, "{")) declared.insert(params.begin(), params.end());
            }
        }
    }
    std::set<std::string> reported;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const Tk& k = t[i];
        if (!k.ident || member(i) || IsKeyword(language, k.text) || knownSet.count(k.text)) continue;
        if (isPunct(i + 1, ".") || isPunct(i + 1, ":")) continue;                // a receiver: a module, a type or a table
        if (!lua && std::isupper((unsigned char)k.text[0])) continue;            // C#-style types, enums and engine classes
        if (!lua && i > 0 && t[i - 1].ident && t[i - 1].line == k.line && typeWord(t[i - 1].text)) continue;   // a declaration
        if (!lua && (isPunct(i - 1, "[") || isPunct(i - 1, "<") || isPunct(i + 1, ">"))) continue;   // attributes, generics
        const bool call = isPunct(i + 1, "(");
        if (call) {
            if (!lua || declared.count(k.text) || (i > 0 && t[i - 1].text == "function")) continue;
            // Lua: a call to a name that is defined nowhere and that the engine does not have.
            const std::string near = NearestName(k.text, known);
            if (!reported.insert("c:" + k.text).second) continue;
            Diagnostic d;
            d.line = k.line; d.col = k.col + 1; d.severity = 1; d.source = "lint";
            d.message = "'" + k.text + "' is not defined" + (near.empty() ? std::string(".") : ". Did you mean " + near + "?");
            if (!near.empty()) d.fixes.push_back({"Change to '" + near + "'", {{k.line, k.col, (int)k.text.size(), near}}});
            out.push_back(std::move(d));
            continue;
        }
        if (declared.count(k.text) || assigned(i)) continue;
        if (!reported.insert("u:" + k.text).second) continue;
        Diagnostic d;
        d.line = k.line; d.col = k.col + 1; d.severity = 2; d.source = "lint";
        d.message = "'" + k.text + "' is never declared.";
        const std::string lineText = LineText(text, k.line);
        const std::string indent = lineText.substr(0, lineText.find_first_not_of(" \t") == std::string::npos ? 0 : lineText.find_first_not_of(" \t"));
        d.fixes.push_back({"Declare '" + k.text + "'",
                           {{k.line, 0, 0, indent + (lua ? "local " + k.text + " = nil\n" : "var " + k.text + " = 0;\n")}}});
        const std::string near = NearestName(k.text, std::vector<std::string>(declared.begin(), declared.end()));
        if (!near.empty()) d.fixes.push_back({"Change to '" + near + "'", {{k.line, k.col, (int)k.text.size(), near}}});
        out.push_back(std::move(d));
    }
    // Unused locals: `local x = ...` (Lua) or `var x = ...` inside a function (OkayScript), the name used nowhere else.
    for (std::size_t i = 0; i + 2 < t.size(); ++i) {
        const bool decl = lua ? t[i].text == "local" : (t[i].text == "var" && t[i].col > 0);
        if (!decl || !t[i + 1].ident || t[i + 1].text == "function" || !isPunct(i + 2, "=")) continue;
        if (useCount[t[i + 1].text] != 1) continue;
        const std::string lineText = LineText(text, t[i].line);
        if (lineText.find_first_not_of(" \t") != (std::size_t)t[i].col) continue;   // only a line that is just this statement
        Diagnostic d;
        d.line = t[i + 1].line; d.col = t[i + 1].col + 1; d.severity = 2; d.source = "lint";
        d.message = "'" + t[i + 1].text + "' is never used.";
        const bool lastLine = text.find('\n', OffsetOf(text, d.line, 0)) == std::string::npos;
        d.fixes.push_back({"Remove '" + t[i + 1].text + "'", {{d.line, 0, (int)lineText.size() + (lastLine ? 0 : 1), ""}}});
        out.push_back(std::move(d));
    }
    std::stable_sort(out.begin(), out.end(), [](const Diagnostic& a, const Diagnostic& b) { return a.line < b.line; });
    return out;
}

// ---- small editing helpers ------------------------------------------------------------------------------------------------
std::string ToggleComment(const std::string& text, int first, int last, const std::string& prefix) {
    std::vector<std::string> lines;
    { std::size_t a = 0; for (;;) { std::size_t b = text.find('\n', a); if (b == std::string::npos) { lines.push_back(text.substr(a)); break; } lines.push_back(text.substr(a, b - a)); a = b + 1; } }
    first = std::max(first, 1); last = std::min(last, (int)lines.size());
    bool all = true, any = false;
    for (int l = first; l <= last; ++l) {
        const std::string& s = lines[(std::size_t)l - 1];
        const std::size_t nb = s.find_first_not_of(" \t");
        if (nb == std::string::npos) continue;
        any = true;
        if (s.compare(nb, prefix.size(), prefix) != 0) all = false;
    }
    if (!any) return text;
    for (int l = first; l <= last; ++l) {
        std::string& s = lines[(std::size_t)l - 1];
        const std::size_t nb = s.find_first_not_of(" \t");
        if (nb == std::string::npos) continue;
        if (all) {
            std::size_t len = prefix.size();
            if (nb + len < s.size() && s[nb + len] == ' ') ++len;
            s.erase(nb, len);
        } else s.insert(nb, prefix + " ");
    }
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) { out += lines[i]; if (i + 1 < lines.size()) out += '\n'; }
    return out;
}

CaretCall CallAtCaret(const std::string& text, std::size_t caret) {
    CaretCall ci;
    caret = std::min(caret, text.size());
    int depth = 0, commas = 0;
    for (std::size_t i = caret; i > 0; --i) {
        const char c = text[i - 1];
        if (c == '\n' && depth == 0 && caret - i > 400) break;
        if (c == ')' || c == ']' || c == '}') { ++depth; continue; }
        if (c == '[' || c == '{') { if (depth == 0) return ci; --depth; continue; }
        if (c == '(') {
            if (depth > 0) { --depth; continue; }
            std::size_t e = i - 1;
            while (e > 0 && (text[e - 1] == ' ' || text[e - 1] == '\t')) --e;
            std::size_t a = e;
            while (a > 0 && (IsIdent(text[a - 1]) || text[a - 1] == '.' || text[a - 1] == ':')) --a;
            if (a == e) return ci;
            ci.name = text.substr(a, e - a);
            ci.argIndex = commas;
            return ci;
        }
        if (c == ',' && depth == 0) ++commas;
        if (c == ';' && depth == 0) return ci;
    }
    return ci;
}

// ---- templates ------------------------------------------------------------------------------------------------------------
std::vector<ScriptTemplate> TemplatesFor(const std::string& language) {
    std::vector<ScriptTemplate> out;
    out.push_back({"behaviour", "Behaviour (Start and Update)", okay::StarterScriptSource(language)});
    if (language == "lua") {
        out.push_back({"empty", "Empty", "local Script = {}\n\nreturn Script\n"});
        out.push_back({"trigger", "Trigger zone",
                       "local Script = {}\n\n-- Runs when another object enters this object's trigger.\nfunction Script:OnTriggerEnter(other)\n"
                       "    print(\"{Name}: entered\")\nend\n\n-- Runs when it leaves.\nfunction Script:OnTriggerExit(other)\nend\n\nreturn Script\n"});
        out.push_back({"mover", "Mover (moves every frame)",
                       "local Script = {\n    speed = 2,   -- settings show in the Inspector\n}\n\nfunction Script:Update(dt)\n"
                       "    self.transform.position = self.transform.position + Vector3.new(self.speed * dt, 0, 0)\nend\n\nreturn Script\n"});
        return out;
    }
    if (language == "csharp") {
        out.push_back({"empty", "Empty class", "public class {Name}\n{\n}\n"});
        return out;
    }
    out.push_back({"empty", "Empty", "// {Name}\n"});
    out.push_back({"trigger", "Trigger zone",
                   "// Runs when another object enters this object's trigger.\nvoid OnTriggerEnter(Collider other)\n{\n"
                   "    Debug.Log(\"{Name}: entered\");\n}\n\n// Runs when it leaves.\nvoid OnTriggerExit(Collider other)\n{\n}\n"});
    out.push_back({"mover", "Mover (moves every frame)",
                   "public float speed = 2;\n\nvoid Update()\n{\n    transform.position += Vector3.right * speed * Time.deltaTime;\n}\n"});
    return out;
}
std::string FillTemplate(const std::string& body, const std::string& name) {
    std::string out = body;
    for (std::size_t p = out.find("{Name}"); p != std::string::npos; p = out.find("{Name}", p + name.size())) out.replace(p, 6, name);
    return out;
}

// ---- local history --------------------------------------------------------------------------------------------------------
LocalHistory::LocalHistory(std::string projectDir, std::size_t maxPerFile, std::size_t maxBytes)
    : dir_(std::move(projectDir)), maxPerFile_(maxPerFile), maxBytes_(maxBytes) {}

std::string LocalHistory::FolderFor(const std::string& scriptPath) const {
    if (dir_.empty()) return "";
    std::string key = Relative(scriptPath, dir_);
    for (char& c : key) if (!IsIdent(c) && c != '.' && c != '-') c = '_';
    return (fs::path(dir_) / ".okay" / "history" / key).generic_string();
}

static std::string ReadAllText(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

std::vector<HistoryVersion> LocalHistory::Versions(const std::string& scriptPath) const {
    std::vector<HistoryVersion> out;
    const std::string folder = FolderFor(scriptPath);
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(folder, ec)) return out;
    for (const auto& de : fs::directory_iterator(folder, ec)) {
        const std::string name = de.path().filename().string();   // <time>_<label>.txt
        const std::size_t us = name.find('_');
        if (us == std::string::npos || de.path().extension() != ".txt") continue;
        HistoryVersion e;
        e.file = de.path().generic_string();
        e.time = std::atoll(name.substr(0, us).c_str());
        e.label = de.path().stem().string().substr(us + 1);
        e.bytes = (std::size_t)de.file_size(ec);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const HistoryVersion& a, const HistoryVersion& b) { return a.time > b.time; });
    return out;
}
std::string LocalHistory::Load(const HistoryVersion& e) const { return ReadAllText(e.file); }

bool LocalHistory::Record(const std::string& scriptPath, const std::string& text, std::int64_t timeMs, const std::string& label) {
    const std::string folder = FolderFor(scriptPath);
    if (folder.empty()) return false;
    const auto vs = Versions(scriptPath);
    if (!vs.empty() && Load(vs.front()) == text) { last_[scriptPath] = timeMs; return false; }
    std::error_code ec;
    fs::create_directories(folder, ec);
    std::string lab;
    for (char c : label) lab += IsIdent(c) ? c : '-';
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%013lld", (long long)timeMs);
    std::int64_t t = timeMs;
    std::string path = (fs::path(folder) / (std::string(stamp) + "_" + lab + ".txt")).generic_string();
    while (fs::exists(path, ec)) {   // two copies in the same millisecond: the next free time
        std::snprintf(stamp, sizeof(stamp), "%013lld", (long long)++t);
        path = (fs::path(folder) / (std::string(stamp) + "_" + lab + ".txt")).generic_string();
    }
    {
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;
        f << text;
        if (!f) return false;
    }
    last_[scriptPath] = timeMs;
    Prune(folder);
    return true;
}

bool LocalHistory::MaybeRecord(const std::string& scriptPath, const std::string& text, std::int64_t timeMs, std::int64_t intervalMs) {
    auto it = last_.find(scriptPath);
    if (it == last_.end()) {   // first sight this session: from the newest copy on disk
        const auto vs = Versions(scriptPath);
        last_[scriptPath] = vs.empty() ? 0 : vs.front().time;
        it = last_.find(scriptPath);
    }
    if (timeMs - it->second < intervalMs) return false;
    return Record(scriptPath, text, timeMs, "Auto");
}

void LocalHistory::Prune(const std::string& folder) {
    std::error_code ec;
    // Per script: the newest maxPerFile_.
    std::vector<fs::path> mine;
    for (const auto& de : fs::directory_iterator(folder, ec)) if (de.path().extension() == ".txt") mine.push_back(de.path());
    std::sort(mine.begin(), mine.end());
    while (mine.size() > maxPerFile_) { fs::remove(mine.front(), ec); mine.erase(mine.begin()); }
    // In all: the oldest copies of any script go until the folder fits in maxBytes_.
    const fs::path root = fs::path(dir_) / ".okay" / "history";
    std::vector<std::pair<std::string, fs::path>> all;   // (file name = time first, path)
    std::size_t total = 0;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        total += (std::size_t)it->file_size(ec);
        all.push_back({it->path().filename().string(), it->path()});
    }
    std::sort(all.begin(), all.end());
    for (const auto& a : all) {
        if (total <= maxBytes_) break;
        const std::size_t sz = (std::size_t)fs::file_size(a.second, ec);
        if (fs::remove(a.second, ec)) total -= std::min(total, sz);
    }
}

std::vector<DiffLine> LineDiff(const std::string& a, const std::string& b) {
    auto linesOf = [](const std::string& s) {
        std::vector<std::string> v;
        std::size_t p = 0;
        while (p <= s.size()) {
            std::size_t e = s.find('\n', p);
            if (e == std::string::npos) { if (p < s.size()) v.push_back(s.substr(p)); break; }
            v.push_back(s.substr(p, e - p)); p = e + 1;
        }
        return v;
    };
    const std::vector<std::string> x = linesOf(a), y = linesOf(b);
    // Same lines at both ends are kept out of the table, so a small change in a big file stays cheap.
    std::size_t pre = 0;
    while (pre < x.size() && pre < y.size() && x[pre] == y[pre]) ++pre;
    std::size_t suf = 0;
    while (suf < x.size() - pre && suf < y.size() - pre && x[x.size() - 1 - suf] == y[y.size() - 1 - suf]) ++suf;
    const std::size_t n = x.size() - pre - suf, m = y.size() - pre - suf;
    std::vector<DiffLine> out;
    for (std::size_t i = 0; i < pre; ++i) out.push_back({' ', x[i]});
    if (n * m > 4000000) {   // too big for the table: everything in the middle as removed then added
        for (std::size_t i = 0; i < n; ++i) out.push_back({'-', x[pre + i]});
        for (std::size_t j = 0; j < m; ++j) out.push_back({'+', y[pre + j]});
    } else {
        std::vector<std::vector<int>> L(n + 1, std::vector<int>(m + 1, 0));
        for (std::size_t i = n; i-- > 0;)
            for (std::size_t j = m; j-- > 0;)
                L[i][j] = x[pre + i] == y[pre + j] ? L[i + 1][j + 1] + 1 : std::max(L[i + 1][j], L[i][j + 1]);
        std::size_t i = 0, j = 0;
        while (i < n || j < m) {
            if (i < n && j < m && x[pre + i] == y[pre + j]) { out.push_back({' ', x[pre + i]}); ++i; ++j; }
            else if (j < m && (i == n || L[i][j + 1] >= L[i + 1][j])) { out.push_back({'+', y[pre + j]}); ++j; }
            else { out.push_back({'-', x[pre + i]}); ++i; }
        }
    }
    for (std::size_t k = x.size() - suf; k < x.size(); ++k) out.push_back({' ', x[k]});
    return out;
}

} // namespace scriptide
