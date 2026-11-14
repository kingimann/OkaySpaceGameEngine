// AssetDatabase: reference extractors and the dependency graph.
#include "okay/Core/AssetDatabase.hpp"
#include "AssetDatabaseInternal.hpp"
#include "okay/Core/AssetManager.hpp"
#include "okay/Core/DataPack.hpp"
#include "okay/Core/SaveData.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_set>

namespace okay {

namespace fs = std::filesystem;
using adb::Lower;
using adb::PathFromU8;
using adb::StartsWith;
using adb::U8;

namespace {

constexpr std::size_t kMaxTextBytes = 64u << 20;

struct Tok {
    std::string value;
    std::size_t begin = 0, end = 0;
    bool quoted = false;
};

// Splits [lb, le) of `text` into tokens: "quoted strings" (escapes removed, span = inside the
// quotes) and bare words separated by blanks, '=' and ','.
void TokenizeLine(const std::string& text, std::size_t lb, std::size_t le, std::vector<Tok>& out) {
    std::size_t i = lb;
    while (i < le) {
        const char c = text[i];
        if (c == '"') {
            Tok t;
            t.quoted = true;
            std::size_t j = i + 1;
            t.begin = j;
            while (j < le && text[j] != '"') {
                if (text[j] == '\\' && j + 1 < le) { ++j; t.value += text[j] == 'n' ? '\n' : text[j]; }
                else t.value += text[j];
                ++j;
            }
            t.end = j;
            out.push_back(std::move(t));
            i = j < le ? j + 1 : j;
        } else if (std::isspace(static_cast<unsigned char>(c)) || c == '=' || c == ',') {
            ++i;
        } else {
            std::size_t j = i;
            while (j < le && !std::isspace(static_cast<unsigned char>(text[j])) && text[j] != '=' && text[j] != ',' && text[j] != '"') ++j;
            Tok t;
            t.value = text.substr(i, j - i);
            t.begin = i;
            t.end = j;
            out.push_back(std::move(t));
            i = j;
        }
    }
}

// A value that is plausibly a path to an asset file of a type the engine knows.
bool LooksLikeAssetPath(const std::string& v) {
    if (v.empty() || v.size() > 260) return false;
    for (unsigned char c : v) if (c < 32 || c == '<' || c == '>' || c == '|' || c == '*' || c == '?') return false;
    if (v.find("://") != std::string::npos || StartsWith(v, "data:")) return false;
    const std::string ext = AssetExtension(v);
    if (ext.empty() || ext == "okaymeta" || ext == "txt" || ext == "json") return false;
    if (AssetTypeFromExtension(ext) == AssetType::Other) return false;
    const std::string name = AssetFileName(v);
    if (name.size() <= ext.size() + 1) return false;   // no file name before the extension
    if (v.find(' ') != std::string::npos && v.find('/') == std::string::npos && v.find('\\') == std::string::npos)
        return false;                                  // a sentence, not a path
    return true;
}

// Looser: the value of a field that is known to hold a file path (prefab, srcscene, ...).
bool LooksLikeFilePath(const std::string& v) {
    if (v.empty() || v.size() > 260) return false;
    for (unsigned char c : v) if (c < 32 || c == '<' || c == '>' || c == '|' || c == '*' || c == '?') return false;
    return v.find("://") == std::string::npos;
}

void Add(std::vector<FoundRef>& out, const Tok& t) {
    FoundRef r;
    r.path = t.value;
    r.begin = t.begin;
    r.end = t.end;
    r.quoted = t.quoted;
    out.push_back(std::move(r));
}

template <class F>
void ForEachLine(const std::string& text, F f) {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::size_t le = nl;
        if (le > pos && text[le - 1] == '\r') --le;
        f(pos, le);
        if (nl >= text.size()) break;
        pos = nl + 1;
    }
}

} // namespace

std::vector<FoundRef> ExtractSceneRefs(const std::string& text) {
    std::vector<FoundRef> out;
    std::vector<Tok> toks;
    ForEachLine(text, [&](std::size_t lb, std::size_t le) {
        toks.clear();
        TokenizeLine(text, lb, le, toks);
        if (toks.empty()) return;
        const std::string head = toks[0].quoted ? std::string() : toks[0].value;
        if (head == "gameobject" || head == "name" || head == "tag") return;   // a name is not a path
        const bool pathLine = head == "prefab" || head == "srcscene" || head == "netprefab" || head == "visualscriptpath";
        for (std::size_t i = 1; i < toks.size(); ++i) {
            const Tok& t = toks[i];
            if (pathLine && i == 1 && t.quoted) { if (LooksLikeFilePath(t.value)) Add(out, t); continue; }
            if (LooksLikeAssetPath(t.value)) Add(out, t);
        }
    });
    return out;
}

std::vector<FoundRef> ExtractMaterialRefs(const std::string& text) {
    std::vector<FoundRef> out;
    std::vector<Tok> toks;
    ForEachLine(text, [&](std::size_t lb, std::size_t le) {
        toks.clear();
        TokenizeLine(text, lb, le, toks);
        if (toks.size() >= 2 && !toks[0].quoted && toks[0].value == "texture" && toks[1].value != "-" && LooksLikeFilePath(toks[1].value))
            Add(out, toks[1]);
    });
    return out;
}

std::vector<FoundRef> ExtractTextRefs(const std::string& text) {
    std::vector<FoundRef> out;
    std::vector<Tok> toks;
    ForEachLine(text, [&](std::size_t lb, std::size_t le) {
        toks.clear();
        TokenizeLine(text, lb, le, toks);
        if (toks.empty() || (!toks[0].quoted && !toks[0].value.empty() && toks[0].value[0] == '#')) return;
        for (const Tok& t : toks) if (LooksLikeAssetPath(t.value)) Add(out, t);
    });
    return out;
}

// --- Extractor registry -------------------------------------------------------------------------

void AssetDatabase::RegisterDefaultExtractors() {
    auto wrap = [](std::vector<FoundRef> (*fn)(const std::string&)) -> RefExtractor {
        return [fn](const std::string&, const std::string& text, std::vector<FoundRef>& out) {
            std::vector<FoundRef> r = fn(text);
            out.insert(out.end(), r.begin(), r.end());
        };
    };
    RegisterExtractor("scene", {"okayscene", "okayprefab"}, wrap(&ExtractSceneRefs));
    RegisterExtractor("material", {"okaymat"}, wrap(&ExtractMaterialRefs));
    RegisterExtractor("text", {"okayanim", "okayclip", "okaydialog", "okayquest", "okaybt", "okayvs", "okayseq",
                               "okayloot", "okayitems", "okayweapon", "okayrecipes", "okaydata", "okaynav", "gltf"},
                      wrap(&ExtractTextRefs));
}

void AssetDatabase::RegisterExtractor(const std::string& name, const std::vector<std::string>& extensions, RefExtractor extractor) {
    ExtractorRec rec;
    rec.name = name;
    for (const auto& e : extensions) {
        std::string x = Lower(e);
        if (!x.empty() && x[0] == '.') x.erase(0, 1);
        if (!x.empty()) rec.extensions.push_back(x);
    }
    rec.fn = std::move(extractor);
    for (auto& e : m_extractors) if (e.name == name) { e = std::move(rec); ++m_depsGeneration; return; }
    m_extractors.push_back(std::move(rec));
    ++m_depsGeneration;
}

bool AssetDatabase::RemoveExtractor(const std::string& name) {
    for (auto it = m_extractors.begin(); it != m_extractors.end(); ++it)
        if (it->name == name) { m_extractors.erase(it); ++m_depsGeneration; return true; }
    return false;
}

std::vector<std::string> AssetDatabase::ExtractorNames() const {
    std::vector<std::string> out;
    for (const auto& e : m_extractors) out.push_back(e.name);
    return out;
}

// --- Extraction and resolution --------------------------------------------------------------------

void AssetDatabase::ExtractDependencies(Slot& slot, bool force) {
    if (!force && slot.rec.hash != 0 && slot.depsHash == slot.rec.hash && slot.depsGeneration == m_depsGeneration) return;
    m_reverseValid = false;
    slot.edges.clear();
    slot.depsHash = slot.rec.hash;
    slot.depsGeneration = m_depsGeneration;
    slot.depsPacked = false;
    const std::string ext = slot.rec.Extension();
    std::vector<const ExtractorRec*> list;
    for (const auto& e : m_extractors)
        for (const auto& x : e.extensions) if (x == ext) { list.push_back(&e); break; }
    if (list.empty()) return;
    std::string raw;
    if (!adb::ReadAll(PathFromU8(m_root + "/" + slot.rec.path), kMaxTextBytes, raw)) return;
    std::string text;
    if (DataPack::IsPacked(raw)) { text = DataPack::Unpack(raw); slot.depsPacked = true; }
    else text = std::move(raw);
    std::vector<FoundRef> refs;
    for (const ExtractorRec* e : list) {
        try { e->fn(slot.rec.path, text, refs); } catch (...) {}
    }
    std::set<std::pair<std::size_t, std::size_t>> seen;
    for (FoundRef& r : refs) {
        if (r.path.empty()) continue;
        if (r.begin != std::string::npos && !seen.insert({r.begin, r.end}).second) continue;
        Edge e;
        e.ref = std::move(r);
        slot.edges.push_back(std::move(e));
    }
}

AssetDatabase::RefMatch AssetDatabase::ResolveRaw(const std::string& raw, const std::string& fromPath) const {
    RefMatch m;
    if (raw.empty()) return m;
    const std::string a = ToProjectPath(raw);
    std::string b;
    {
        const std::string n = AssetManager::Normalize(raw, false);
        const bool absolute = !n.empty() && (n[0] == '/' || (n.size() >= 2 && n[1] == ':'));
        if (!absolute) {
            const std::size_t slash = fromPath.find_last_of('/');
            if (slash != std::string::npos) {
                b = AssetManager::Normalize(fromPath.substr(0, slash + 1) + n, false);
                if (b == ".." || StartsWith(b, "../")) b.clear();
            }
        }
    }
    // The project folder's own name in front ("Assets/x.png" when the root is .../Assets).
    std::string stripped, prefix;
    if (!a.empty()) {
        const std::size_t slash = a.find('/');
        if (slash != std::string::npos) {
            const std::string first = Lower(a.substr(0, slash));
            const std::size_t rs = m_root.find_last_of('/');
            const std::string rootName = Lower(rs == std::string::npos ? m_root : m_root.substr(rs + 1));
            if (first == rootName || first == "assets") { prefix = a.substr(0, slash + 1); stripped = a.substr(slash + 1); }
        }
    }
    struct Cand { const std::string* path; int style; const std::string* prefix; };
    const std::string none;
    const Cand cands[] = {{&a, adb::kStyleRoot, &none}, {&b, 1, &none}, {&stripped, adb::kStyleRoot, &prefix}};
    for (const Cand& c : cands) {
        if (c.path->empty()) continue;
        auto it = m_pathIndex.find(KeyOf(*c.path));
        if (it != m_pathIndex.end()) { m.guid = it->second; m.style = c.style; m.prefix = *c.prefix; return m; }
    }
    for (const Cand& c : cands) {
        if (c.path->empty()) continue;
        auto it = m_redirects.find(KeyOf(*c.path));
        if (it != m_redirects.end() && m_slots.count(it->second)) {
            m.guid = it->second; m.style = c.style; m.prefix = *c.prefix; m.redirect = true;
            return m;
        }
    }
    return m;
}

bool AssetDatabase::ExistsOnDisk(const std::string& raw, const std::string& fromPath) const {
    std::error_code ec;
    const std::string n = AssetManager::Normalize(raw, false);
    const bool absolute = !n.empty() && (n[0] == '/' || (n.size() >= 2 && n[1] == ':'));
    if (absolute) return fs::exists(PathFromU8(n), ec);
    std::vector<std::string> tries;
    if (!n.empty() && n != ".." && !StartsWith(n, "../")) tries.push_back(n);
    const std::size_t slash = fromPath.find_last_of('/');
    if (slash != std::string::npos) {
        const std::string b = AssetManager::Normalize(fromPath.substr(0, slash + 1) + n, false);
        if (b != ".." && !StartsWith(b, "../")) tries.push_back(b);
    }
    const std::size_t s2 = n.find('/');
    if (s2 != std::string::npos) {
        const std::string first = Lower(n.substr(0, s2));
        const std::size_t rs = m_root.find_last_of('/');
        const std::string rootName = Lower(rs == std::string::npos ? m_root : m_root.substr(rs + 1));
        if (first == rootName || first == "assets") tries.push_back(n.substr(s2 + 1));
    }
    for (const auto& t : tries) if (fs::exists(PathFromU8(m_root + "/" + t), ec)) return true;
    return false;
}

void AssetDatabase::ResolveEdgesOf(Slot& slot) {
    m_reverseValid = false;
    for (Edge& e : slot.edges) {
        const RefMatch m = ResolveRaw(e.ref.path, slot.rec.path);
        e.target = m.guid;
        e.viaRedirect = m.redirect;
        e.style = m.style;
        e.prefix = m.prefix;
        e.missing = m.guid.IsNull() && !ExistsOnDisk(e.ref.path, slot.rec.path);
    }
}

void AssetDatabase::ResolveAllEdges() {
    for (auto& kv : m_slots) ResolveEdgesOf(kv.second);
    m_reverseValid = false;
}

void AssetDatabase::RebuildDependencies() {
    for (auto& kv : m_slots) ExtractDependencies(kv.second, true);
    ResolveAllEdges();
}

// --- Graph queries ------------------------------------------------------------------------------------

void AssetDatabase::BuildReverse() const {
    if (m_reverseValid) return;
    m_reverse.clear();
    for (const auto& kv : m_slots) {
        std::unordered_set<Guid, GuidHash> seen;
        for (const Edge& e : kv.second.edges) {
            if (e.target.IsNull() || e.target == kv.first || !seen.insert(e.target).second) continue;
            m_reverse[e.target].push_back(kv.first);
        }
    }
    for (auto& kv : m_reverse)
        std::sort(kv.second.begin(), kv.second.end(), [&](const Guid& a, const Guid& b) {
            return m_slots.at(a).rec.path < m_slots.at(b).rec.path;
        });
    m_reverseValid = true;
}

std::vector<Guid> AssetDatabase::DependenciesOf(const Guid& guid, bool recursive) const {
    std::vector<Guid> out;
    const Slot* start = SlotOf(guid);
    if (!start) return out;
    std::unordered_set<Guid, GuidHash> seen;
    seen.insert(guid);
    std::vector<Guid> queue{guid};
    for (std::size_t qi = 0; qi < queue.size(); ++qi) {
        const Slot* s = SlotOf(queue[qi]);
        if (!s) continue;
        for (const Edge& e : s->edges) {
            if (e.target.IsNull() || !seen.insert(e.target).second) continue;
            out.push_back(e.target);
            if (recursive) queue.push_back(e.target);
        }
        if (!recursive) break;
    }
    return out;
}

std::vector<Guid> AssetDatabase::UsedBy(const Guid& guid, bool recursive) const {
    std::vector<Guid> out;
    if (!SlotOf(guid)) return out;
    BuildReverse();
    std::unordered_set<Guid, GuidHash> seen;
    seen.insert(guid);
    std::vector<Guid> queue{guid};
    for (std::size_t qi = 0; qi < queue.size(); ++qi) {
        auto it = m_reverse.find(queue[qi]);
        if (it != m_reverse.end())
            for (const Guid& g : it->second) {
                if (!seen.insert(g).second) continue;
                out.push_back(g);
                if (recursive) queue.push_back(g);
            }
        if (!recursive) break;
    }
    if (recursive)
        std::sort(out.begin(), out.end(), [&](const Guid& a, const Guid& b) { return m_slots.at(a).rec.path < m_slots.at(b).rec.path; });
    return out;
}

std::vector<Guid> AssetDatabase::BuildClosure(const std::vector<Guid>& roots) const {
    std::vector<Guid> out;
    std::unordered_set<Guid, GuidHash> seen;
    for (const Guid& r : roots)
        if (SlotOf(r) && seen.insert(r).second) out.push_back(r);
    for (std::size_t qi = 0; qi < out.size(); ++qi) {
        const Slot* s = SlotOf(out[qi]);
        if (!s) continue;
        for (const Edge& e : s->edges)
            if (!e.target.IsNull() && seen.insert(e.target).second) out.push_back(e.target);
    }
    return out;
}

std::vector<std::string> AssetDatabase::BuildClosurePaths(const std::vector<std::string>& rootsGuidOrPath) const {
    std::vector<Guid> roots;
    for (const auto& s : rootsGuidOrPath) {
        const AssetResolution r = Resolve(s);
        if (r) roots.push_back(r.guid);
    }
    std::vector<std::string> out;
    for (const Guid& g : BuildClosure(roots)) out.push_back(m_slots.at(g).rec.path);
    return out;
}

std::vector<Guid> AssetDatabase::FindUnused(const std::vector<Guid>& rootsIn) const {
    std::vector<Guid> roots = rootsIn;
    if (roots.empty())
        for (const auto& kv : m_slots) if (kv.second.rec.kind == AssetType::Scene) roots.push_back(kv.first);
    std::unordered_set<Guid, GuidHash> reach;
    for (const Guid& g : BuildClosure(roots)) reach.insert(g);
    std::vector<Guid> out;
    for (const auto& kv : m_slots) if (!reach.count(kv.first)) out.push_back(kv.first);
    std::sort(out.begin(), out.end(), [&](const Guid& a, const Guid& b) { return m_slots.at(a).rec.path < m_slots.at(b).rec.path; });
    return out;
}

std::vector<Guid> AssetDatabase::FindUnreferenced() const {
    BuildReverse();
    std::vector<Guid> out;
    for (const auto& kv : m_slots) if (!m_reverse.count(kv.first)) out.push_back(kv.first);
    std::sort(out.begin(), out.end(), [&](const Guid& a, const Guid& b) { return m_slots.at(a).rec.path < m_slots.at(b).rec.path; });
    return out;
}

std::vector<MissingRef> AssetDatabase::FindMissing() const {
    std::vector<const Slot*> order;
    for (const auto& kv : m_slots) order.push_back(&kv.second);
    std::sort(order.begin(), order.end(), [](const Slot* a, const Slot* b) { return a->rec.path < b->rec.path; });
    std::vector<MissingRef> out;
    for (const Slot* s : order) {
        std::set<std::string> seen;
        for (const Edge& e : s->edges) {
            if (!e.missing || !seen.insert(e.ref.path).second) continue;
            out.push_back({s->rec.guid, s->rec.path, e.ref.path});
        }
    }
    return out;
}

std::vector<StaleRef> AssetDatabase::FindStaleReferences() const {
    std::vector<const Slot*> order;
    for (const auto& kv : m_slots) order.push_back(&kv.second);
    std::sort(order.begin(), order.end(), [](const Slot* a, const Slot* b) { return a->rec.path < b->rec.path; });
    std::vector<StaleRef> out;
    for (const Slot* s : order) {
        std::set<std::string> seen;
        for (const Edge& e : s->edges) {
            if (!e.viaRedirect || !seen.insert(e.ref.path).second) continue;
            StaleRef r;
            r.referrer = s->rec.guid; r.referrerPath = s->rec.path; r.reference = e.ref.path;
            r.target = e.target; r.targetPath = GuidToPath(e.target);
            out.push_back(std::move(r));
        }
    }
    return out;
}

// --- Rewriting references ---------------------------------------------------------------------------------

int AssetDatabase::FixStaleReferences(std::vector<std::string>* changedFiles) {
    std::vector<Guid> referrers;
    for (const auto& kv : m_slots) {
        if (kv.second.depsPacked) continue;
        for (const Edge& e : kv.second.edges) if (e.viaRedirect) { referrers.push_back(kv.first); break; }
    }
    std::sort(referrers.begin(), referrers.end(), [&](const Guid& a, const Guid& b) { return m_slots.at(a).rec.path < m_slots.at(b).rec.path; });
    int total = 0;
    for (const Guid& g : referrers) {
        Slot& s = m_slots.at(g);
        const std::string abs = m_root + "/" + s.rec.path;
        std::string text;
        if (!adb::ReadAll(PathFromU8(abs), kMaxTextBytes, text) || DataPack::IsPacked(text)) continue;
        // Fresh spans from the current text.
        const std::string ext = s.rec.Extension();
        std::vector<FoundRef> refs;
        for (const auto& e : m_extractors) {
            bool applies = false;
            for (const auto& x : e.extensions) if (x == ext) { applies = true; break; }
            if (!applies) continue;
            try { e.fn(s.rec.path, text, refs); } catch (...) {}
        }
        struct Rep { std::size_t begin, end; std::string text; };
        std::vector<Rep> reps;
        std::set<std::pair<std::size_t, std::size_t>> seen;
        const std::size_t dirEnd = s.rec.path.find_last_of('/');
        const std::string dir = dirEnd == std::string::npos ? std::string() : s.rec.path.substr(0, dirEnd);
        for (const FoundRef& r : refs) {
            if (r.begin == std::string::npos || r.end > text.size() || r.begin > r.end || !seen.insert({r.begin, r.end}).second) continue;
            const RefMatch m = ResolveRaw(r.path, s.rec.path);
            if (!m.redirect) continue;
            const Slot* target = SlotOf(m.guid);
            if (!target) continue;
            std::string nt;
            if (m.style == 1) nt = U8(fs::path(target->rec.path).lexically_relative(fs::path(dir.empty() ? "." : dir)));
            else nt = m.prefix + target->rec.path;
            if (r.path.find('\\') != std::string::npos && r.path.find('/') == std::string::npos)
                for (char& c : nt) if (c == '/') c = '\\';
            if (r.quoted) {
                std::string esc;
                for (char c : nt) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                nt = esc;
            }
            reps.push_back({r.begin, r.end, nt});
        }
        if (reps.empty()) continue;
        std::sort(reps.begin(), reps.end(), [](const Rep& a, const Rep& b) { return a.begin > b.begin; });
        for (const Rep& r : reps) text.replace(r.begin, r.end - r.begin, r.text);
        if (!savefmt::AtomicWrite(abs, text)) continue;
        total += static_cast<int>(reps.size());
        if (changedFiles) changedFiles->push_back(s.rec.path);
        RefreshFile(s.rec.path);
    }
    return total;
}

} // namespace okay
