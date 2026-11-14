#include "okay/Scene/PartitionStreamer.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace okay::worldpart {

// ---- the decision -------------------------------------------------------------------------------

static bool UsableSource(const SourceSample& s) {
    return std::isfinite(s.position.x) && std::isfinite(s.position.y) && std::isfinite(s.position.z) &&
           std::isfinite(s.loadRadius) && s.loadRadius >= 0.0f && std::isfinite(s.unloadRadius) &&
           s.unloadRadius >= s.loadRadius && std::isfinite(s.priority) && s.priority > 0.0f;
}

CellPlan PlanCells(const WorldGrid& grid, const std::vector<SourceSample>& sourcesIn, const std::vector<CellCoord>& known,
                   const std::vector<CellCoord>& loaded, const std::vector<CellCoord>& queued) {
    CellPlan plan;
    if (!grid.Valid()) return plan;
    std::vector<SourceSample> sources;
    for (const SourceSample& s : sourcesIn) if (UsableSource(s)) sources.push_back(s);
    if (sources.empty()) return plan;

    // Wanted: a known cell within some source's load radius, scored by the best (distance / priority).
    std::map<CellCoord, float> best;
    auto consider = [&](const CellCoord& c, const SourceSample& s) {
        const float d = grid.DistanceToCell(s.position, c);
        if (!(d <= s.loadRadius)) return;
        const float score = d / s.priority;
        auto it = best.find(c);
        if (it == best.end() || score < it->second) best[c] = score;
    };
    for (const SourceSample& s : sources) {
        std::vector<CellCoord> around;
        // A small catalogue is scanned; a big one is looked up cell by cell inside the radius.
        if (known.size() > 2048 && grid.CellsInRadius(s.position, s.loadRadius, around)) {
            for (const CellCoord& c : around)
                if (std::binary_search(known.begin(), known.end(), c)) consider(c, s);
        } else {
            for (const CellCoord& c : known) consider(c, s);
        }
    }
    plan.wanted.assign(best.begin(), best.end());
    std::sort(plan.wanted.begin(), plan.wanted.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second < b.second;
        return a.first < b.first;
    });

    auto kept = [&](const CellCoord& c) {
        for (const SourceSample& s : sources)
            if (grid.DistanceToCell(s.position, c) <= s.unloadRadius) return true;
        return false;
    };
    for (const CellCoord& c : loaded) if (!kept(c)) plan.unload.push_back(c);
    for (const CellCoord& c : queued) if (!kept(c)) plan.cancel.push_back(c);
    return plan;
}

// ---- streamer ---------------------------------------------------------------------------------

PartitionStreamer::PartitionStreamer(Scene& scene, const WorldGrid& grid, IChunkStore& store, ICellLoader& loader)
    : m_scene(scene), m_grid(grid), m_store(store), m_loader(loader) {
    RegisterStreamingSerializers();
    RefreshCatalog();
}

void PartitionStreamer::RefreshCatalog() {
    std::set<CellCoord> cells;
    if (!m_grid.Valid()) { m_known.clear(); return; }
    auto add = [&](const std::string& name) {
        CellCoord c;
        if (WorldGrid::ParseCellName(name, c) && m_grid.InRange(c)) cells.insert(c);
    };
    for (const std::string& n : m_store.List()) add(n);
    for (const auto& kv : m_overlay) add(kv.first);
    m_known.assign(cells.begin(), cells.end());
}

bool PartitionStreamer::ReadChunkText(const std::string& name, std::string& out) const {
    auto it = m_overlay.find(name);
    if (it != m_overlay.end()) { out = it->second; return true; }
    return m_store.Read(name, out);
}

std::vector<SourceSample> PartitionStreamer::GatherSources() const {
    std::vector<SourceSample> out = m_extra;
    for (StreamingSource* s : m_scene.FindObjectsOfType<StreamingSource>()) {
        if (!s || !s->enabled || !s->gameObject || s->gameObject->IsDestroyed() || !s->gameObject->ActiveInHierarchy()) continue;
        SourceSample v;
        v.position = s->gameObject->transform->Position();
        v.loadRadius = s->EffectiveLoadRadius();
        v.unloadRadius = s->EffectiveUnloadRadius();
        v.priority = s->EffectivePriority();
        out.push_back(v);
    }
    return out;
}

LoadStatus PartitionStreamer::LoadCell(const CellCoord& cell, std::string* error) {
    const std::string name = WorldGrid::CellName(cell);
    std::string text, err;
    if (!ReadChunkText(name, text)) { if (error) *error = name + ": chunk cannot be read"; return LoadStatus::Failed; }
    Chunk chunk;
    if (!DecodeChunk(text, chunk, &err)) { if (error) *error = name + ": " + err; return LoadStatus::Failed; }
    if (chunk.persistent || chunk.cell != cell) { if (error) *error = name + ": the chunk is for another cell"; return LoadStatus::Failed; }
    Loaded rec;
    const ChunkApply a = InstantiateChunk(m_scene, chunk, &rec.roots, &err);
    if (a == ChunkApply::Busy) return LoadStatus::Retry;
    if (a != ChunkApply::Ok) { if (error) *error = name + ": " + err; return LoadStatus::Failed; }
    rec.hash = Fnv1a64(EncodeChunk(chunk));
    m_loaded[cell] = std::move(rec);
    return LoadStatus::Done;
}

bool PartitionStreamer::CaptureCell(const CellCoord& cell, const Loaded& rec, std::string& text,
                                    std::vector<GameObject*>& live, std::string* error) const {
    Chunk c;
    c.cell = cell;
    live.clear();
    for (const std::string& guid : rec.roots) {
        GameObject* go = m_scene.FindByGuid(guid);
        if (!go || go->IsDestroyed() || m_scene.IsDestroyPending(go)) continue;   // the game destroyed it: it stays gone
        ChunkRoot r;
        std::string err;
        if (!CaptureRoot(*go, r, &err)) { if (error) *error = WorldGrid::CellName(cell) + ": " + err; return false; }
        c.roots.push_back(std::move(r));
        live.push_back(go);
    }
    text = EncodeChunk(c);
    return true;
}

bool PartitionStreamer::LoadNow(const CellCoord& cellIn, std::string* error) {
    const CellCoord cell = m_grid.Normalize(cellIn);
    if (m_loaded.count(cell)) return true;
    if (!m_grid.Valid() || !m_grid.InRange(cell)) { if (error) *error = "cell is outside the world"; return false; }
    m_loader.Cancel(cell);
    m_queued.erase(cell);
    std::string err;
    const LoadStatus st = LoadCell(cell, &err);
    if (st == LoadStatus::Done) { m_retryAt.erase(cell); return true; }
    if (error) *error = st == LoadStatus::Retry ? "objects of this cell are still being destroyed" : err;
    return false;
}

bool PartitionStreamer::UnloadNow(const CellCoord& cellIn, std::string* error) {
    const CellCoord cell = m_grid.Normalize(cellIn);
    auto it = m_loaded.find(cell);
    if (it == m_loaded.end()) { m_loader.Cancel(cell); m_queued.erase(cell); return true; }
    std::string text;
    std::vector<GameObject*> live;
    if (!CaptureCell(cell, it->second, text, live, error)) return false;
    const std::string name = WorldGrid::CellName(cell);
    if (m_overlay.count(name) || Fnv1a64(text) != it->second.hash) m_overlay[name] = text;
    for (GameObject* g : live) m_scene.DestroyImmediate(g);
    m_loaded.erase(it);
    return true;
}

int PartitionStreamer::UnloadAll(bool keepState) {
    int n = 0;
    std::vector<CellCoord> cells = LoadedCells();
    for (const CellCoord& c : cells) {
        if (keepState) {
            if (UnloadNow(c)) ++n;
        } else {
            auto it = m_loaded.find(c);
            for (const std::string& guid : it->second.roots)
                if (GameObject* go = m_scene.FindByGuid(guid)) if (!go->IsDestroyed()) m_scene.DestroyImmediate(go);
            m_loaded.erase(it);
            ++n;
        }
    }
    for (const CellCoord& c : std::vector<CellCoord>(m_queued.begin(), m_queued.end())) m_loader.Cancel(c);
    m_queued.clear();
    return n;
}

TickReport PartitionStreamer::Tick() {
    ++m_tick;
    TickReport rep;
    const std::vector<SourceSample> sources = GatherSources();
    for (const SourceSample& s : sources) if (UsableSource(s)) ++rep.sources;

    const std::vector<CellCoord> loaded = LoadedCells();
    const std::vector<CellCoord> queued(m_queued.begin(), m_queued.end());
    const CellPlan plan = PlanCells(m_grid, sources, m_known, loaded, queued);
    rep.wanted = static_cast<int>(plan.wanted.size());

    for (const CellCoord& c : plan.unload) {
        std::string err;
        if (UnloadNow(c, &err)) ++rep.unloaded;
        else rep.errors.push_back(err);
    }
    for (const CellCoord& c : plan.cancel) {
        m_loader.Cancel(c);
        m_queued.erase(c);
        ++rep.cancelled;
    }
    for (const auto& w : plan.wanted) {
        const CellCoord& c = w.first;
        if (m_loaded.count(c)) continue;
        auto r = m_retryAt.find(c);
        if (r != m_retryAt.end()) {
            if (r->second > m_tick) continue;
            m_retryAt.erase(r);
        }
        if (!m_queued.count(c)) ++rep.queued;
        m_queued.insert(c);
        m_loader.Enqueue(c, w.second);
    }

    rep.loaded = 0;
    m_loader.Pump([&](const CellCoord& c) {
        std::string err;
        const LoadStatus st = LoadCell(c, &err);
        if (st == LoadStatus::Done) { m_queued.erase(c); ++rep.loaded; }
        else if (st == LoadStatus::Failed) {
            m_queued.erase(c);
            m_retryAt[c] = m_tick + static_cast<std::uint64_t>(m_retryTicks);
            ++rep.failures;
            rep.errors.push_back(err);
        }
        return st;
    });
    rep.pending = static_cast<int>(m_loader.Pending());
    return rep;
}

CellState PartitionStreamer::State(const CellCoord& cellIn) const {
    const CellCoord c = m_grid.Normalize(cellIn);
    if (m_loaded.count(c)) return CellState::Loaded;
    if (m_queued.count(c)) return CellState::Queued;
    if (m_retryAt.count(c)) return CellState::Failed;
    return CellState::Unloaded;
}

std::vector<CellCoord> PartitionStreamer::LoadedCells() const {
    std::vector<CellCoord> v;
    for (const auto& kv : m_loaded) v.push_back(kv.first);
    return v;
}
std::size_t PartitionStreamer::LoadedCount() const { return m_loaded.size(); }
std::vector<std::string> PartitionStreamer::RootsOf(const CellCoord& cell) const {
    auto it = m_loaded.find(m_grid.Normalize(cell));
    return it == m_loaded.end() ? std::vector<std::string>{} : it->second.roots;
}

// ---- saves --------------------------------------------------------------------------------------

std::string PartitionStreamer::EscapeLine(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '\0': o += "\\0"; break;
            default: o += c;
        }
    }
    return o;
}

bool PartitionStreamer::UnescapeLine(const std::string& s, std::string& out) {
    std::string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') { o += s[i]; continue; }
        if (++i >= s.size()) return false;
        switch (s[i]) {
            case '\\': o += '\\'; break;
            case 'n': o += '\n'; break;
            case 'r': o += '\r'; break;
            case 't': o += '\t'; break;
            case '0': o += '\0'; break;
            default: return false;
        }
    }
    out = std::move(o);
    return true;
}

std::vector<std::string> PartitionStreamer::CaptureState() {
    for (const auto& kv : m_loaded) {
        std::string text;
        std::vector<GameObject*> live;
        const std::string name = WorldGrid::CellName(kv.first);
        if (!CaptureCell(kv.first, kv.second, text, live, nullptr)) continue;   // keep what the overlay had
        if (m_overlay.count(name) || Fnv1a64(text) != kv.second.hash) m_overlay[name] = text;
    }
    std::vector<std::string> lines;
    for (const auto& kv : m_overlay) lines.push_back(kv.first + "\t" + EscapeLine(kv.second));
    return lines;
}

bool PartitionStreamer::RestoreState(const std::vector<std::string>& lines, std::string* error) {
    std::map<std::string, std::string> next;
    for (const std::string& line : lines) {
        const std::size_t tab = line.find('\t');
        auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
        if (tab == std::string::npos) return fail("state line without a name");
        const std::string name = line.substr(0, tab);
        CellCoord cell;
        if (!WorldGrid::ParseCellName(name, cell) || !m_grid.InRange(cell)) return fail("state line for a cell this grid cannot hold: " + name.substr(0, 40));
        std::string text, err;
        if (!UnescapeLine(line.substr(tab + 1), text)) return fail(name + ": bad escape in state line");
        Chunk c;
        if (!DecodeChunk(text, c, &err)) return fail(name + ": " + err);
        if (c.persistent || c.cell != cell) return fail(name + ": the chunk is for another cell");
        if (!next.emplace(name, std::move(text)).second) return fail(name + " appears twice");
    }
    UnloadAll(/*keepState=*/false);
    m_retryAt.clear();
    m_overlay = std::move(next);
    RefreshCatalog();
    return true;
}

void PartitionStreamer::WriteState(SaveWriter& w) {
    w.Int("version", 1);
    w.Strings("chunks", CaptureState());
}

bool PartitionStreamer::ReadState(const SaveReader& r, std::string* error) {
    if (!r.Has("version")) { if (error) *error = "this save holds no streaming state"; return false; }
    if (r.Int("version", 0) != 1) { if (error) *error = "streaming state is from a newer build"; return false; }
    return RestoreState(r.Strings("chunks"), error);
}

} // namespace okay::worldpart
