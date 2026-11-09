#include "okay/Core/LocScan.hpp"
#include "okay/Core/LocCheck.hpp"
#include "okay/Components/DialogueGraph.hpp"
#include "okay/Components/QuestLog.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace okay {

namespace {

namespace fs = std::filesystem;
const std::size_t npos = std::string::npos;

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool IsIdent(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

bool IsKeyLiteral(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!(std::isalnum((unsigned char)c) && (unsigned char)c < 0x80) && c != '.' && c != '_' && c != '-') return false;
    return true;
}

struct Lines {
    std::vector<std::size_t> starts{0};
    explicit Lines(const std::string& text) {
        for (std::size_t i = 0; i < text.size(); ++i) if (text[i] == '\n') starts.push_back(i + 1);
    }
    int Of(std::size_t pos) const {
        return (int)(std::upper_bound(starts.begin(), starts.end(), pos) - starts.begin());
    }
};

bool IsKeyCall(const std::string& callee, const std::vector<std::string>& names) {
    const std::size_t dot = callee.rfind('.');
    const std::string last = dot == npos ? callee : callee.substr(dot + 1);
    for (const std::string& n : names) {
        if (n.find('.') == npos) { if (last == n) return true; continue; }
        if (callee.size() >= n.size() && callee.compare(callee.size() - n.size(), n.size(), n) == 0 &&
            (callee.size() == n.size() || callee[callee.size() - n.size() - 1] == '.'))
            return true;
    }
    return false;
}

// loc("key"), T('key'), Localization.Get("key"), also inside a scene file's escaped script text (\"key\").
void ScanCalls(const std::string& text, const std::string& file, const LocScanOptions& opt, std::vector<LocScanUse>& out) {
    const Lines lines(text);
    for (std::size_t p = 0; p < text.size(); ++p) {
        if (text[p] != '(') continue;
        std::size_t e = p;
        while (e > 0 && (text[e - 1] == ' ' || text[e - 1] == '\t')) --e;
        std::size_t s = e;
        while (s > 0 && (IsIdent(text[s - 1]) || text[s - 1] == '.' || text[s - 1] == ':')) --s;
        if (s == e) continue;
        std::string callee;
        for (std::size_t i = s; i < e; ++i) {
            if (text[i] == ':') { if (i + 1 < e && text[i + 1] == ':') ++i; callee += '.'; }
            else callee += text[i];
        }
        if (!IsKeyCall(callee, opt.callNames)) continue;

        std::size_t q = p + 1;
        while (q < text.size() && std::isspace((unsigned char)text[q])) ++q;
        bool escaped = false;
        if (q + 1 < text.size() && text[q] == '\\' && (text[q + 1] == '"' || text[q + 1] == '\'')) { escaped = true; ++q; }
        if (q >= text.size() || (text[q] != '"' && text[q] != '\'')) continue;
        const char quote = text[q++];
        const std::size_t start = q;
        bool closed = false;
        while (q < text.size()) {
            if (text[q] == '\n') break;
            if (escaped) { if (text[q] == '\\' && q + 1 < text.size() && text[q + 1] == quote) { closed = true; break; } }
            else if (text[q] == quote) { closed = true; break; }
            else if (text[q] == '\\') ++q;
            ++q;
        }
        if (!closed) continue;
        const std::string literal = text.substr(start, q - start);
        if (!IsKeyLiteral(literal)) continue;
        std::size_t r = q + (escaped ? 2 : 1);
        while (r < text.size() && (text[r] == ' ' || text[r] == '\t')) ++r;
        LocScanUse u;
        u.key = literal;
        u.file = file;
        u.line = lines.Of(p);
        u.how = callee + "()";
        u.prefix = r < text.size() && (text[r] == '+' || (text[r] == '.' && r + 1 < text.size() && text[r + 1] == '.'));
        out.push_back(std::move(u));
        p = q;
    }
}

// "@menu.title"
void ScanAtKeys(const std::string& text, const std::string& file, std::vector<LocScanUse>& out) {
    const Lines lines(text);
    for (std::size_t p = 0; p + 2 < text.size(); ++p) {
        if ((text[p] != '"' && text[p] != '\'') || text[p + 1] != '@') continue;
        const std::size_t end = text.find(text[p], p + 2);
        if (end == npos) continue;
        const std::string literal = text.substr(p + 2, end - p - 2);
        if (!IsKeyLiteral(literal)) continue;
        LocScanUse u;
        u.key = literal;
        u.file = file;
        u.line = lines.Of(p);
        u.how = "@key";
        out.push_back(std::move(u));
        p = end;
    }
}

// The `loc` record an object has in a scene or prefab: loc text="menu.title" button="menu.start"
void ScanSceneRecords(const std::string& text, const std::string& file, std::vector<LocScanUse>& out) {
    static const char* const kFields[][2] = {{"text", "Text"}, {"button", "Button"}, {"toggle", "Toggle"},
                                             {"tooltip", "Tooltip"}, {"world", "World UI"}, {"input", "Input Field"}};
    int lineNo = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == npos) nl = text.size();
        ++lineNo;
        std::size_t i = pos;
        while (i < nl && (text[i] == ' ' || text[i] == '\t')) ++i;
        if (nl - i > 4 && text.compare(i, 3, "loc") == 0 && (text[i + 3] == ' ' || text[i + 3] == '\t')) {
            i += 3;
            while (i < nl) {
                while (i < nl && std::isspace((unsigned char)text[i])) ++i;
                std::string name;
                while (i < nl && text[i] != '=' && !std::isspace((unsigned char)text[i])) name += text[i++];
                if (i >= nl || text[i] != '=') continue;
                ++i;
                std::string value;
                if (i < nl && text[i] == '"') {
                    ++i;
                    while (i < nl && text[i] != '"') {
                        if (text[i] == '\\' && i + 1 < nl) ++i;
                        value += text[i++];
                    }
                    if (i < nl) ++i;
                } else {
                    while (i < nl && !std::isspace((unsigned char)text[i])) value += text[i++];
                }
                for (const auto& f : kFields)
                    if (name == f[0] && !value.empty()) {
                        LocScanUse u;
                        u.key = value;
                        u.file = file;
                        u.line = lineNo;
                        u.how = f[1];
                        out.push_back(std::move(u));
                    }
            }
        }
        pos = nl + 1;
    }
}

// node 5 Localize menu.title
void ScanVisualScript(const std::string& text, const std::string& file, std::vector<LocScanUse>& out) {
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream ls(line);
        std::string a, id, type, key;
        if (!(ls >> a >> id >> type >> key) || a != "node" || type != "Localize") continue;
        if (key.size() >= 2 && key.front() == '"' && key.back() == '"') key = key.substr(1, key.size() - 2);
        if (!IsKeyLiteral(key)) continue;
        LocScanUse u;
        u.key = key;
        u.file = file;
        u.line = lineNo;
        u.how = "Localize node";
        out.push_back(std::move(u));
    }
}

const char* const kPluralNames[] = {"zero", "one", "two", "few", "many", "other"};

bool StartsWithAny(const std::string& s, const std::vector<std::string>& prefixes) {
    for (const auto& p : prefixes) if (!p.empty() && s.compare(0, p.size(), p) == 0) return true;
    return false;
}

} // namespace

void LocScanText(const std::string& file, const std::string& text, const LocScanOptions& opt, std::vector<LocScanUse>& out) {
    const fs::path path = fs::u8path(file);
    const std::string ext = Lower(path.extension().u8string());
    if (ext == ".okaydialog") {
        DialogueGraph g;
        if (g.Parse(text)) {
            const std::string name = !g.name.empty() ? g.name : path.stem().u8string();
            for (const auto& kv : DialogueStrings(g, name)) {
                LocScanUse u;
                u.key = kv.first; u.file = file; u.how = "dialogue"; u.optional = true;
                out.push_back(std::move(u));
            }
        }
        return;
    }
    if (ext == ".okayquest") {
        std::vector<QuestDef> quests;
        if (QuestFile::Parse(text, quests))
            for (const QuestDef& q : quests)
                for (const auto& kv : QuestStrings(q)) {
                    LocScanUse u;
                    u.key = kv.first; u.file = file; u.how = "quest"; u.optional = true;
                    out.push_back(std::move(u));
                }
        return;
    }
    if (ext == ".okayvs") { ScanVisualScript(text, file, out); return; }
    if (ext == ".okayscene" || ext == ".okayprefab") ScanSceneRecords(text, file, out);
    ScanCalls(text, file, opt, out);
    if (opt.atKeys) ScanAtKeys(text, file, out);
}

void LocScanResolve(const StringTable& table, const LocScanOptions& opt, LocScanResult& result) {
    std::set<std::string> exact;
    std::vector<std::string> prefixes;
    for (const LocScanUse& u : result.uses) {
        if (u.prefix) prefixes.push_back(u.key);
        else exact.insert(u.key);
    }
    std::set<std::string> pluralBases;
    for (const LocPluralGroup& g : LocPluralGroups(table)) pluralBases.insert(g.base);

    result.usedKeys.clear();
    result.unused.clear();
    result.undefined.clear();
    for (const auto& kv : table.Entries()) {
        const std::string& key = kv.first;
        bool used = exact.count(key) > 0;
        if (!used) for (const auto& p : prefixes) if (key.compare(0, p.size(), p) == 0) { used = true; break; }
        if (!used) {   // a plural form is used when its base name is
            const std::size_t dot = key.rfind('.');
            if (dot != npos && dot > 0) {
                const std::string base = key.substr(0, dot), cat = key.substr(dot + 1);
                for (const char* n : kPluralNames)
                    if (cat == n && pluralBases.count(base) && exact.count(base)) used = true;
            }
        }
        if (used) result.usedKeys.insert(key);
        else if (!StartsWithAny(key, opt.engineKeyPrefixes)) result.unused.push_back(key);
    }
    for (const LocScanUse& u : result.uses) {
        if (u.prefix || u.optional || table.Has(u.key) || pluralBases.count(u.key)) continue;
        if (StartsWithAny(u.key, opt.engineKeyPrefixes)) continue;   // a built-in text with no entry yet
        result.undefined.push_back(u);
    }
}

LocScanResult LocScanFiles(const std::vector<std::pair<std::string, std::string>>& files, const StringTable& table,
                           const LocScanOptions& opt) {
    LocScanResult r;
    for (const auto& f : files) {
        const std::string ext = Lower(fs::u8path(f.first).extension().u8string());
        if (std::find(opt.extensions.begin(), opt.extensions.end(), ext) == opt.extensions.end()) continue;
        LocScanText(f.first, f.second, opt, r.uses);
        ++r.filesScanned;
    }
    LocScanResolve(table, opt, r);
    return r;
}

LocScanResult LocScanProject(const std::string& root, const StringTable& table, const LocScanOptions& opt) {
    LocScanResult r;
    std::error_code ec;
    const fs::path base = fs::u8path(root);
    std::vector<fs::path> found;
    for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        const std::string name = it->path().filename().u8string();
        if (it->is_directory(e2)) {
            if ((!name.empty() && name[0] == '.') || opt.skipDirs.count(name)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(e2)) continue;
        const std::string ext = Lower(it->path().extension().u8string());
        if (std::find(opt.extensions.begin(), opt.extensions.end(), ext) == opt.extensions.end()) continue;
        found.push_back(it->path());
    }
    std::sort(found.begin(), found.end());
    for (const fs::path& p : found) {
        std::error_code e2;
        const auto size = fs::file_size(p, e2);
        if (e2 || size > opt.maxFileBytes) { ++r.filesSkipped; continue; }
        std::ifstream f(p, std::ios::binary);
        if (!f) { ++r.filesSkipped; continue; }
        std::stringstream ss;
        ss << f.rdbuf();
        const fs::path rel = fs::relative(p, base, e2);
        LocScanText(e2 ? p.u8string() : rel.generic_u8string(), ss.str(), opt, r.uses);
        ++r.filesScanned;
    }
    LocScanResolve(table, opt, r);
    return r;
}

std::string LocScanResult::ToText() const {
    std::ostringstream o;
    o << "unused (" << unused.size() << "):";
    for (const auto& k : unused) o << " " << k;
    o << "\n";
    o << "undefined (" << undefined.size() << "):\n";
    for (const auto& u : undefined) o << "  " << u.key << "  " << u.file << ":" << u.line << "  " << u.how << "\n";
    o << filesScanned << " files scanned";
    if (filesSkipped) o << ", " << filesSkipped << " skipped";
    o << "\n";
    return o.str();
}

} // namespace okay
