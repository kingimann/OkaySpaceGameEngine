#include "okay/Core/SceneAssetGuids.hpp"
#include "okay/Core/SaveData.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace okay {
namespace sceneguids {
namespace {

bool StartsWithRecord(const std::string& line) {
    return line.compare(0, 10, "assetguid ") == 0;
}

std::string Esc(const std::string& s) {
    std::string r;
    for (char c : s) { if (c == '"' || c == '\\') r += '\\'; r += c; }
    return r;
}

// Reads `"..."` (with \" and \\ escapes) at s[i]; moves i past it.
bool ReadQuoted(const std::string& s, std::size_t& i, std::string& out) {
    while (i < s.size() && s[i] == ' ') ++i;
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) ++i;
        out += s[i++];
    }
    if (i >= s.size()) return false;
    ++i;
    return true;
}

bool ParseLine(const std::string& line, TableEntry& e) {
    if (!StartsWithRecord(line)) return false;
    std::size_t i = 10;
    const std::size_t sp = line.find(' ', i);
    if (sp == std::string::npos || !Guid::Parse(line.substr(i, sp - i), e.guid)) return false;
    i = sp;
    if (!ReadQuoted(line, i, e.written)) return false;
    if (!ReadQuoted(line, i, e.projectPath)) e.projectPath.clear();
    return true;
}

// Splits into lines keeping the line ends, so Strip / insert keep the file byte for byte otherwise.
std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size() - 1;
        out.push_back(text.substr(pos, nl - pos + 1));
        pos = nl + 1;
    }
    return out;
}

std::string TrimEol(const std::string& l) {
    std::string r = l;
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    return r;
}

bool IsRemote(const std::string& p) {
    return p.find("://") != std::string::npos || p.empty() || p[0] == '@' || p.rfind("proc:", 0) == 0;
}

std::string Quote(const std::string& s) { return "\"" + Esc(s) + "\""; }

} // namespace

std::string Report::ToString() const {
    std::ostringstream o;
    o << stamped << " GUID records written, " << repaired << " paths repaired, " << broken.size() << " broken";
    for (const auto& r : repairs) o << "\n  repaired " << r;
    for (const auto& b : broken) o << "\n  broken " << b;
    return o.str();
}

std::vector<TableEntry> ReadTable(const std::string& text) {
    std::vector<TableEntry> out;
    for (const auto& l : Lines(text)) { TableEntry e; if (ParseLine(TrimEol(l), e)) out.push_back(std::move(e)); }
    return out;
}

std::string StripTable(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const auto& l : Lines(text)) if (!StartsWithRecord(l)) out += l;
    return out;
}

std::string Stamp(const std::string& text, AssetDatabase& db, Report* report) {
    std::string body = StripTable(text);
    const std::vector<FoundRef> refs = ExtractSceneRefs(body);
    std::vector<TableEntry> table;
    std::set<std::string> seen;
    for (const FoundRef& r : refs) {
        if (IsRemote(r.path) || !seen.insert(r.path).second) continue;
        const AssetResolution res = db.Resolve(r.path);
        if (!res || res.viaRedirect) continue;   // missing, or only an old path: nothing reliable to record
        db.EnsureMeta(res.guid);
        table.push_back({res.guid, r.path, res.path});
    }
    if (report) report->stamped = static_cast<int>(table.size());
    if (table.empty()) return body;
    std::string block;
    for (const auto& e : table)
        block += "assetguid " + e.guid.ToString() + " " + Quote(e.written) + " " + Quote(e.projectPath) + "\n";
    // After the header line ("okayscene N").
    const std::size_t nl = body.find('\n');
    if (nl == std::string::npos) return body;
    return body.substr(0, nl + 1) + block + body.substr(nl + 1);
}

std::vector<std::string> FindBroken(const std::string& text, const AssetDatabase& db) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const FoundRef& r : ExtractSceneRefs(StripTable(text))) {
        if (IsRemote(r.path) || !seen.insert(r.path).second) continue;
        if (!db.Resolve(r.path)) out.push_back(r.path);
    }
    return out;
}

std::string Resolve(const std::string& text, const AssetDatabase& db, Report* report) {
    const std::vector<TableEntry> table = ReadTable(text);
    const std::vector<FoundRef> refs = ExtractSceneRefs(StripTable(text));
    // Spans refer to the text without the table; work on that and put the table back afterwards.
    std::string body = StripTable(text);
    std::map<std::string, std::string> newPath;   // written -> replacement
    for (const TableEntry& e : table) {
        if (newPath.count(e.written)) continue;
        const AssetResolution now = db.Resolve(e.written);
        if (now && now.guid == e.guid && !now.viaRedirect) continue;       // still right
        const AssetRecord* target = db.Find(e.guid);
        if (!target) continue;                                              // the asset is gone: reported as broken below
        if (now && !now.viaRedirect && now.guid != e.guid) continue;        // the path now names a different asset: do not hijack it
        std::string np;
        const std::string& pp = e.projectPath;
        if (!pp.empty() && e.written.size() >= pp.size() &&
            e.written.compare(e.written.size() - pp.size(), pp.size(), pp) == 0)
            np = e.written.substr(0, e.written.size() - pp.size()) + target->path;   // keep the spelling's prefix
        else
            np = target->path;
        if (np != e.written) newPath[e.written] = np;
    }
    struct Rep { std::size_t b, e; std::string t; };
    std::vector<Rep> reps;
    std::set<std::string> counted;
    for (const FoundRef& r : refs) {
        auto it = newPath.find(r.path);
        if (it == newPath.end() || r.begin == std::string::npos || r.end > body.size()) continue;
        reps.push_back({r.begin, r.end, r.quoted ? Esc(it->second) : it->second});
        if (counted.insert(r.path).second && report) {
            ++report->repaired;
            report->repairs.push_back(r.path + " -> " + it->second);
        }
    }
    std::sort(reps.begin(), reps.end(), [](const Rep& a, const Rep& b) { return a.b > b.b; });
    for (const Rep& r : reps) body.replace(r.b, r.e - r.b, r.t);
    // Table: keep it, with the new written paths so a second load finds nothing to do.
    std::string result = body;
    if (!table.empty()) {
        std::string block;
        for (TableEntry e : table) {
            auto it = newPath.find(e.written);
            if (it != newPath.end()) { e.written = it->second; if (const AssetRecord* t = db.Find(e.guid)) e.projectPath = t->path; }
            block += "assetguid " + e.guid.ToString() + " " + Quote(e.written) + " " + Quote(e.projectPath) + "\n";
        }
        const std::size_t nl = result.find('\n');
        if (nl != std::string::npos) result = result.substr(0, nl + 1) + block + result.substr(nl + 1);
    }
    if (report) for (const std::string& b : FindBroken(result, db)) report->broken.push_back(b);
    return result;
}

namespace {
bool ReadWhole(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
}

bool StampFile(const std::string& scenePath, AssetDatabase& db, Report* report, std::string* error) {
    std::string text;
    if (!ReadWhole(scenePath, text)) { if (error) *error = "cannot read " + scenePath; return false; }
    const std::string out = Stamp(text, db, report);
    if (out == text) return true;
    if (!savefmt::AtomicWrite(scenePath, out)) { if (error) *error = "cannot write " + scenePath; return false; }
    return true;
}

bool RepairFile(const std::string& scenePath, const AssetDatabase& db, Report* report, std::string* error) {
    std::string text;
    if (!ReadWhole(scenePath, text)) { if (error) *error = "cannot read " + scenePath; return false; }
    Report local;
    const std::string out = Resolve(text, db, &local);
    if (report) *report = local;
    if (out == text) return true;
    { std::ofstream bak(scenePath + ".bak", std::ios::binary | std::ios::trunc); bak << text; }
    if (!savefmt::AtomicWrite(scenePath, out)) { if (error) *error = "cannot write " + scenePath; return false; }
    return true;
}

} // namespace sceneguids
} // namespace okay
