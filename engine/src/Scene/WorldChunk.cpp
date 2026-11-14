#include "okay/Scene/WorldChunk.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneText.hpp"
#include "okay/Scene/StreamingSource.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace okay::worldpart {

namespace fs = std::filesystem;

std::uint64_t Fnv1a64(const std::string& d) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : d) { h ^= c; h *= 1099511628211ull; }
    return h;
}

static std::string Hex16(std::uint64_t v) {
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

std::string Chunk::Name() const { return persistent ? std::string(kPersistentChunkName) : WorldGrid::CellName(cell); }

std::string EncodeChunk(const Chunk& c) {
    std::string payload;
    for (const ChunkRoot& r : c.roots) {
        payload += "root " + std::to_string(r.guids.size()) + " " + std::to_string(r.text.size()) + "\n";
        for (std::size_t i = 0; i < r.guids.size(); ++i) { if (i) payload += ' '; payload += r.guids[i]; }
        payload += "\n";
        payload += r.text;
        payload += "\n";
    }
    const std::string cellLine = c.persistent ? std::string("cell persistent\n")
        : "cell " + std::to_string(c.cell.x) + " " + std::to_string(c.cell.y) + " " + std::to_string(c.cell.z) + "\n";
    const std::string rootsLine = "roots " + std::to_string(c.roots.size()) + "\n";
    std::string out = "okaychunk " + std::to_string(kChunkFormatVersion) + "\n";
    out += cellLine + rootsLine;
    out += "sum " + Hex16(Fnv1a64(cellLine + rootsLine + payload)) + "\n";
    out += payload;
    return out;
}

namespace {

struct Reader {
    const std::string& s;
    std::size_t pos = 0;
    explicit Reader(const std::string& str) : s(str) {}
    std::size_t Left() const { return s.size() - pos; }
    // One line (without '\n'), at most `maxLen` long. False when there is no '\n' within the limit.
    bool Line(std::string& out, std::size_t maxLen) {
        const std::size_t end = s.find('\n', pos);
        if (end == std::string::npos || end - pos > maxLen) return false;
        out.assign(s, pos, end - pos);
        pos = end + 1;
        return true;
    }
};

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t a = 0;
    for (;;) {
        const std::size_t b = s.find(sep, a);
        if (b == std::string::npos) { out.push_back(s.substr(a)); break; }
        out.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    return out;
}

// Strict decimal: optional '-', 1..10 digits, nothing else.
bool ParseInt(const std::string& s, long long& v) {
    std::size_t i = 0;
    bool neg = false;
    if (i < s.size() && s[i] == '-') { neg = true; ++i; }
    if (i >= s.size() || s.size() - i > 10) return false;
    long long r = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        r = r * 10 + (s[i] - '0');
    }
    v = neg ? -r : r;
    return true;
}

bool Fail(std::string* e, const std::string& m) { if (e) *e = m; return false; }

} // namespace

bool DecodeChunk(const std::string& text, Chunk& out, std::string* error) {
    out = Chunk{};
    if (text.size() > kMaxChunkBytes) return Fail(error, "chunk is larger than the limit");
    Reader rd(text);
    std::string line;
    if (!rd.Line(line, 64) || line != "okaychunk " + std::to_string(kChunkFormatVersion)) {
        if (line.compare(0, 10, "okaychunk ") == 0) return Fail(error, "unsupported chunk version \"" + line.substr(10) + "\"");
        return Fail(error, "not a chunk (no okaychunk header)");
    }
    Chunk c;
    const std::size_t sumFrom = rd.pos;   // the checksum covers the cell and roots lines too
    if (!rd.Line(line, 64) || line.compare(0, 5, "cell ") != 0) return Fail(error, "missing cell line");
    if (line == "cell persistent") c.persistent = true;
    else {
        const auto t = Split(line.substr(5), ' ');
        long long v[3];
        if (t.size() != 3) return Fail(error, "bad cell line");
        for (int i = 0; i < 3; ++i) {
            if (!ParseInt(t[i], v[i]) || v[i] < -kMaxCellIndex || v[i] > kMaxCellIndex) return Fail(error, "cell coordinate out of range");
        }
        c.cell = CellCoord{static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2])};
    }
    long long nroots = 0;
    if (!rd.Line(line, 64) || line.compare(0, 6, "roots ") != 0 || !ParseInt(line.substr(6), nroots) || nroots < 0)
        return Fail(error, "bad roots line");
    const std::size_t sumTo = rd.pos;
    if (!rd.Line(line, 64) || line.compare(0, 4, "sum ") != 0 || line.size() != 20) return Fail(error, "bad sum line");
    const std::string payload = text.substr(rd.pos);
    if (line.substr(4) != Hex16(Fnv1a64(text.substr(sumFrom, sumTo - sumFrom) + payload))) return Fail(error, "checksum does not match (chunk is damaged)");
    // Each root needs at least the 3 bytes "root", so the count cannot exceed what is left.
    if (static_cast<unsigned long long>(nroots) > payload.size() / 8) return Fail(error, "root count larger than the file");

    Reader pr(payload);
    std::set<std::string> seen;
    for (long long i = 0; i < nroots; ++i) {
        if (!pr.Line(line, 64) || line.compare(0, 5, "root ") != 0) return Fail(error, "missing root line " + std::to_string(i));
        const auto t = Split(line.substr(5), ' ');
        long long ng = 0, nb = 0;
        if (t.size() != 2 || !ParseInt(t[0], ng) || !ParseInt(t[1], nb) || ng < 1 || nb < 1) return Fail(error, "bad root line " + std::to_string(i));
        // A guid is at least 1 character plus a separator.
        if (static_cast<unsigned long long>(ng) > pr.Left() / 2 + 1) return Fail(error, "guid count larger than the file");
        std::string glist;
        if (!pr.Line(glist, static_cast<std::size_t>(ng) * 65)) return Fail(error, "missing guid line " + std::to_string(i));
        ChunkRoot r;
        r.guids = Split(glist, ' ');
        if (static_cast<long long>(r.guids.size()) != ng) return Fail(error, "guid count does not match on root " + std::to_string(i));
        for (const std::string& g : r.guids) {
            if (!SceneText::IsValidGuid(g)) return Fail(error, "bad guid on root " + std::to_string(i));
            if (!seen.insert(g).second) return Fail(error, "guid " + g + " appears twice");
        }
        if (static_cast<unsigned long long>(nb) + 1 > pr.Left()) return Fail(error, "root text is longer than the file");
        r.text.assign(payload, pr.pos, static_cast<std::size_t>(nb));
        pr.pos += static_cast<std::size_t>(nb);
        if (payload[pr.pos] != '\n') return Fail(error, "root text is not followed by a line end");
        ++pr.pos;
        c.roots.push_back(std::move(r));
    }
    if (pr.pos != payload.size()) return Fail(error, "unexpected data after the last root");
    out = std::move(c);
    return true;
}

// ---- live trees -------------------------------------------------------------------------------

static void CollectTree(const GameObject* root, std::vector<const GameObject*>& out) {
    // Same order SerializeObject numbers its objects: depth first, children in sibling order.
    std::vector<const GameObject*> stack{root};
    while (!stack.empty()) {
        const GameObject* g = stack.back(); stack.pop_back();
        out.push_back(g);
        const auto& kids = g->transform->Children();
        for (std::size_t i = kids.size(); i-- > 0;) if (kids[i] && kids[i]->gameObject) stack.push_back(kids[i]->gameObject);
    }
}

bool CaptureRoot(const GameObject& root, ChunkRoot& out, std::string* error) {
    out = ChunkRoot{};
    if (root.IsDestroyed() || !root.transform) return Fail(error, "object is destroyed");
    std::vector<const GameObject*> tree;
    CollectTree(&root, tree);
    ChunkRoot r;
    r.text = SceneSerializer::SerializeObject(root);
    // The writer numbers every object of the tree but skips some (generated rigs): read which it wrote.
    long long last = -1;
    std::size_t p = 0;
    while (p < r.text.size()) {
        const std::size_t e = r.text.find('\n', p);
        const std::size_t end = e == std::string::npos ? r.text.size() : e;
        if (r.text.compare(p, 11, "gameobject ") == 0) {
            const std::size_t ns = p + 11;
            std::size_t ne = ns;
            while (ne < end && r.text[ne] >= '0' && r.text[ne] <= '9') ++ne;
            long long idx = 0;
            if (ne == ns || ne - ns > 9) return Fail(error, "cannot read an object number from the object text");
            idx = std::stoll(r.text.substr(ns, ne - ns));
            if (idx <= last || idx >= static_cast<long long>(tree.size())) return Fail(error, "object text does not match the tree");
            last = idx;
            r.guids.push_back(tree[static_cast<std::size_t>(idx)]->Guid());
        }
        p = end + 1;
    }
    if (r.guids.empty()) return Fail(error, "nothing of this tree is saved");
    out = std::move(r);
    return true;
}

ChunkApply InstantiateChunk(Scene& scene, const Chunk& chunk, std::vector<std::string>* rootGuids, std::string* error) {
    RegisterStreamingSerializers();
    for (const ChunkRoot& r : chunk.roots)
        for (const std::string& g : r.guids)
            if (const GameObject* old = scene.FindByGuid(g)) {
                if (old->IsDestroyed() || scene.IsDestroyPending(old)) {
                    if (error) *error = "object " + g + " is still being destroyed";
                    return ChunkApply::Busy;
                }
                if (error) *error = "object " + g + " is already in the scene";
                return ChunkApply::Failed;
            }
    std::vector<GameObject*> created;
    auto rollback = [&] { for (GameObject* g : created) scene.DestroyImmediate(g); };
    std::vector<std::string> roots;
    for (const ChunkRoot& r : chunk.roots) {
        std::string err;
        GameObject* go = SceneSerializer::InstantiateFromText(scene, r.text, &err);
        if (!go) { rollback(); if (error) *error = "cannot read an object tree: " + err; return ChunkApply::Failed; }
        created.push_back(go);
        std::vector<const GameObject*> tree;
        CollectTree(go, tree);
        if (tree.size() != r.guids.size()) {
            rollback();
            if (error) *error = "object tree has " + std::to_string(tree.size()) + " objects, the chunk lists " + std::to_string(r.guids.size());
            return ChunkApply::Failed;
        }
        for (std::size_t i = 0; i < tree.size(); ++i)
            if (!const_cast<GameObject*>(tree[i])->SetGuid(r.guids[i])) { rollback(); if (error) *error = "guid refused"; return ChunkApply::Failed; }
        roots.push_back(r.guids.front());
    }
    if (rootGuids) *rootGuids = std::move(roots);
    return ChunkApply::Ok;
}

// ---- splitter ---------------------------------------------------------------------------------

static bool Marked(const GameObject* root, const std::string& tag) {
    std::vector<const GameObject*> tree;
    CollectTree(root, tree);
    for (const GameObject* g : tree) {
        if (!tag.empty() && g->CompareTag(tag)) return true;
        if (g->GetComponent<StreamingSource>()) return true;
    }
    return false;
}

bool SplitScene(const Scene& scene, const WorldGrid& grid, const SplitOptions& options, SplitResult& out, std::string* error) {
    out = SplitResult{};
    if (!grid.Valid()) return Fail(error, "the grid is invalid (cell size must be a finite number above 0)");
    RegisterStreamingSerializers();
    std::map<CellCoord, Chunk> cells;
    Chunk persistent;
    persistent.persistent = true;
    for (GameObject* root : scene.Roots()) {
        if (!root || root->IsDestroyed()) continue;
        ChunkRoot cr;
        std::string err;
        if (!CaptureRoot(*root, cr, &err)) { ++out.skippedRoots; continue; }
        CellCoord cell;
        const bool inWorld = grid.CellOf(root->transform->Position(), cell);
        if (Marked(root, options.alwaysLoadedTag) || !inWorld) {
            if (!inWorld) ++out.outsideWorld;
            persistent.roots.push_back(std::move(cr));
            ++out.persistentRoots;
        } else {
            Chunk& ch = cells[cell];
            ch.cell = cell;
            ch.roots.push_back(std::move(cr));
            ++out.cellRoots;
        }
    }
    if (!persistent.roots.empty()) out.chunks[persistent.Name()] = EncodeChunk(persistent);
    for (auto& kv : cells) {
        out.cells.push_back(kv.first);
        out.chunks[kv.second.Name()] = EncodeChunk(kv.second);
    }
    return true;
}

// ---- stores -----------------------------------------------------------------------------------

bool MemoryChunkStore::Read(const std::string& name, std::string& out) const {
    auto it = m_chunks.find(name);
    if (it == m_chunks.end()) return false;
    out = it->second;
    return true;
}
bool MemoryChunkStore::Write(const std::string& name, const std::string& text) { m_chunks[name] = text; return true; }
std::vector<std::string> MemoryChunkStore::List() const {
    std::vector<std::string> v;
    for (const auto& kv : m_chunks) v.push_back(kv.first);
    return v;
}
void MemoryChunkStore::Add(const SplitResult& split) { for (const auto& kv : split.chunks) m_chunks[kv.first] = kv.second; }

bool DirectoryChunkStore::ValidName(const std::string& n) {
    if (n.empty() || n.size() > 80) return false;
    for (char c : n) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

bool DirectoryChunkStore::Read(const std::string& name, std::string& out) const {
    if (!ValidName(name)) return false;
    std::ifstream f((fs::path(m_dir) / (name + ".okaychunk")).string(), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    if (f.bad()) return false;
    out = ss.str();
    return true;
}

bool DirectoryChunkStore::Write(const std::string& name, const std::string& text) {
    if (!ValidName(name)) return false;
    std::error_code ec;
    fs::create_directories(m_dir, ec);
    const fs::path final = fs::path(m_dir) / (name + ".okaychunk");
    const fs::path tmp = fs::path(m_dir) / (name + ".okaychunk.tmp");
    {
        std::ofstream f(tmp.string(), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.flush();
        if (!f) { f.close(); fs::remove(tmp, ec); return false; }
    }
    fs::rename(tmp, final, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

std::vector<std::string> DirectoryChunkStore::List() const {
    std::vector<std::string> v;
    std::error_code ec;
    for (fs::directory_iterator it(m_dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        if (p.extension() != ".okaychunk") continue;
        const std::string stem = p.stem().string();
        if (ValidName(stem)) v.push_back(stem);
    }
    std::sort(v.begin(), v.end());
    return v;
}

} // namespace okay::worldpart
