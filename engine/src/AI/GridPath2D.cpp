#include "okay/Components/GridPathAgent2D.hpp"
#include "okay/Components/GridObstacle2D.hpp"
#include "okay/Components/GridCostLayer.hpp"
#include "okay/Components/Tilemap.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace okay {
namespace {

bool SameCell(const Pathfinding::Cell& c, int x, int y) { return c.x == x && c.y == y; }

bool AgentUsesMap(const GridPathAgent2D& agent, const Tilemap& map) {
    if (!agent.mapName.empty())
        return map.gameObject && map.gameObject->name == agent.mapName;
    Scene* scene = agent.GetScene();
    return scene && scene->FindObjectOfType<Tilemap>() == &map;
}

void RepathUsersOf(Scene& scene, const Tilemap& map, const std::vector<Pathfinding::Cell>& cells) {
    if (cells.empty()) return;
    for (GridPathAgent2D* agent : scene.FindObjectsOfType<GridPathAgent2D>()) {
        if (!agent || !agent->enabled || !AgentUsesMap(*agent, map)) continue;
        bool hit = false;
        for (const Pathfinding::Cell& c : cells)
            if (agent->PathUses(c.x, c.y)) { hit = true; break; }
        if (hit) agent->Repath();
    }
}

float Dist(float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

// ---- Costs -----------------------------------------------------------------

void GridCostLayer::RebuildExtraText() {
    std::ostringstream o;
    for (std::size_t i = 0; i < m_extra.size(); ++i) {
        if (i) o << ',';
        o << m_extra[i].id << ':' << m_extra[i].cost;
    }
    m_extraTiles = o.str();
}

void GridCostLayer::RebuildCellText() {
    std::ostringstream o;
    for (std::size_t i = 0; i < m_cells.size(); ++i) {
        if (i) o << ';';
        o << m_cells[i].x << ',' << m_cells[i].y << ',' << m_cells[i].cost;
    }
    m_cellText = o.str();
}

void GridCostLayer::ParseExtra() {
    m_extra.clear();
    std::stringstream in(m_extraTiles);
    std::string part;
    while (std::getline(in, part, ',')) {
        const auto colon = part.find(':');
        if (colon == std::string::npos) continue;
        Extra e;
        e.id = std::atoi(part.c_str());
        e.cost = std::strtof(part.c_str() + colon + 1, nullptr);
        if (e.id < 0) continue;
        m_extra.push_back(e);
    }
}

void GridCostLayer::ParseCells() {
    m_cells.clear();
    std::stringstream in(m_cellText);
    std::string part;
    while (std::getline(in, part, ';')) {
        int x = 0, y = 0;
        float c = 0.0f;
        if (std::sscanf(part.c_str(), "%d,%d,%f", &x, &y, &c) == 3)
            m_cells.push_back(Cell{x, y, c});
    }
}

void GridCostLayer::SetExtraTiles(const std::string& text) {
    m_extraTiles = text;
    ParseExtra();
}

void GridCostLayer::SetCellText(const std::string& text) {
    m_cellText = text;
    ParseCells();
}

void GridCostLayer::SetTileCost(int tileId, float cost) {
    if (tileId < 0) return;
    if (tileId == roadTile) roadCost = cost;
    else if (tileId == mudTile) mudCost = cost;
    else {
        bool found = false;
        for (Extra& e : m_extra) if (e.id == tileId) { e.cost = cost; found = true; break; }
        if (!found) m_extra.push_back(Extra{tileId, cost});
        RebuildExtraText();
    }
    Scene* s = GetScene();
    Tilemap* map = gameObject ? gameObject->GetComponent<Tilemap>() : nullptr;
    if (!map && s) map = s->FindObjectOfType<Tilemap>();
    if (!s || !map) return;
    std::vector<Pathfinding::Cell> hit;
    for (int y = 0; y < map->Height(); ++y)
        for (int x = 0; x < map->Width(); ++x)
            if (map->GetTile(x, y) == tileId) hit.push_back(Pathfinding::Cell{x, y});
    for (GridPathAgent2D* agent : s->FindObjectsOfType<GridPathAgent2D>()) {
        if (!agent || !agent->useCosts) continue;
        bool uses = false;
        for (const Pathfinding::Cell& c : hit) if (agent->PathUses(c.x, c.y)) { uses = true; break; }
        if (uses) agent->Repath();
    }
}

void GridCostLayer::SetCellCost(int x, int y, float cost) {
    bool found = false;
    for (Cell& c : m_cells) if (c.x == x && c.y == y) { c.cost = cost; found = true; break; }
    if (!found) m_cells.push_back(Cell{x, y, cost});
    RebuildCellText();
    if (Scene* s = GetScene()) {
        for (GridPathAgent2D* agent : s->FindObjectsOfType<GridPathAgent2D>()) {
            if (!agent || !agent->useCosts) continue;
            if (agent->PathUses(x, y)) agent->Repath();
        }
    }
}

void GridCostLayer::ClearCellCost(int x, int y) {
    m_cells.erase(std::remove_if(m_cells.begin(), m_cells.end(),
                                 [&](const Cell& c) { return c.x == x && c.y == y; }),
                  m_cells.end());
    RebuildCellText();
}

void GridCostLayer::FillTileCosts(std::vector<float>& out) const {
    int n = 1;
    n = std::max(n, roadTile + 1);
    n = std::max(n, mudTile + 1);
    for (const Extra& e : m_extra) n = std::max(n, e.id + 1);
    if (n < 1) n = 1;
    if (n > 4096) n = 4096;
    out.assign((std::size_t)n, 0.0f);
    out[0] = 1.0f;
    if (roadTile >= 0 && roadTile < n) out[(std::size_t)roadTile] = roadCost;
    if (mudTile >= 0 && mudTile < n && mudTile != roadTile) out[(std::size_t)mudTile] = mudCost;
    for (const Extra& e : m_extra)
        if (e.id >= 0 && e.id < n) out[(std::size_t)e.id] = e.cost;
}

bool GridCostLayer::CellOverride(int x, int y, float& cost) const {
    for (const Cell& c : m_cells)
        if (c.x == x && c.y == y) { cost = c.cost; return true; }
    return false;
}

// ---- Obstacle --------------------------------------------------------------

Tilemap* GridObstacle2D::Map() const {
    Scene* s = GetScene();
    if (!s) return nullptr;
    if (!mapName.empty()) {
        GameObject* g = s->Find(mapName);
        return g ? g->GetComponent<Tilemap>() : nullptr;
    }
    return s->FindObjectOfType<Tilemap>();
}

bool GridObstacle2D::UsesMap(const Tilemap& map) const {
    if (mapName.empty()) return true;
    return map.gameObject && map.gameObject->name == mapName;
}

void GridObstacle2D::CoveredCells(const Tilemap& map, std::vector<Pathfinding::Cell>& out) const {
    if (!blocks || !enabled || !UsesMap(map)) return;
    int x = 0, y = 0;
    if (m_hasAnchor) { x = m_anchorX; y = m_anchorY; }
    else if (transform) map.WorldToCell(transform->Position(), x, y);
    else return;
    const int w = std::max(1, cellsWide);
    const int h = std::max(1, cellsTall);
    for (int dy = 0; dy < h; ++dy)
        for (int dx = 0; dx < w; ++dx)
            out.push_back(Pathfinding::Cell{x + dx, y + dy});
}

void GridObstacle2D::Place(int cellX, int cellY) {
    m_hasAnchor = true;
    m_anchorX = cellX;
    m_anchorY = cellY;
    blocks = true;
    if (Tilemap* map = Map()) {
        if (transform) transform->SetPosition(map->CellToWorld(cellX, cellY));
    }
    m_lastX = cellX;
    m_lastY = cellY;
    Commit();
}

void GridObstacle2D::Clear() {
    blocks = false;
    Commit();
}

void GridObstacle2D::Commit() {
    Scene* s = GetScene();
    Tilemap* map = Map();
    if (!s || !map) return;
    std::vector<Pathfinding::Cell> cells;
    CoveredCells(*map, cells);
    RepathUsersOf(*s, *map, cells);
}

void GridObstacle2D::OnEnable() { Commit(); }

void GridObstacle2D::OnDisable() {
    // Covered cells are empty while this is off, so nobody is forced off a
    // route that is still open. Agents plan around it again on their own timer.
}

void GridObstacle2D::Update(float) {
    if (m_hasAnchor || !blocks) return;
    Tilemap* map = Map();
    if (!map || !transform) return;
    int x = 0, y = 0;
    map->WorldToCell(transform->Position(), x, y);
    if (x == m_lastX && y == m_lastY) return;
    m_lastX = x;
    m_lastY = y;
    Commit();
}

// ---- Agent -----------------------------------------------------------------

Tilemap* GridPathAgent2D::Map() const {
    Scene* s = GetScene();
    if (!s) return nullptr;
    if (!mapName.empty()) {
        GameObject* g = s->Find(mapName);
        return g ? g->GetComponent<Tilemap>() : nullptr;
    }
    return s->FindObjectOfType<Tilemap>();
}

bool GridPathAgent2D::PathUses(int x, int y) const {
    for (const Pathfinding::Cell& c : m_path) if (SameCell(c, x, y)) return true;
    return false;
}

float GridPathAgent2D::PathLength() const {
    Tilemap* map = Map();
    if (!map || m_path.size() < 2) return 0.0f;
    float n = 0.0f;
    for (std::size_t i = 1; i < m_path.size(); ++i) {
        Vec3 a = map->CellToWorld(m_path[i - 1].x, m_path[i - 1].y);
        Vec3 b = map->CellToWorld(m_path[i].x, m_path[i].y);
        n += Dist(a.x, a.y, b.x, b.y);
    }
    return n;
}

void GridPathAgent2D::Stop() {
    m_hasGoal = false;
    m_arrived = false;
    m_path.clear();
    m_index = 0;
    if (auto* rb = gameObject ? gameObject->GetComponent<Rigidbody2D>() : nullptr)
        rb->velocity = {0, 0};
}

void GridPathAgent2D::SetDestination(float x, float y) {
    m_goalWorldX = x;
    m_goalWorldY = y;
    m_goalIsCell = false;
    m_hasGoal = true;
    m_arrived = false;
    Repath();
}

void GridPathAgent2D::SetCell(int x, int y) {
    m_goalX = x;
    m_goalY = y;
    m_goalIsCell = true;
    m_hasGoal = true;
    m_arrived = false;
    Repath();
}

void GridPathAgent2D::Repath() {
    m_path.clear();
    m_index = 0;
    m_repathLeft = repathInterval;
    Tilemap* map = Map();
    Scene* scene = GetScene();
    if (!map || !scene || !m_hasGoal || !transform) return;
    const int w = map->Width(), h = map->Height();
    if (w <= 0 || h <= 0) return;
    if (static_cast<long long>(w) * h > Pathfinding::kMaxCells) return;

    int sx = 0, sy = 0;
    map->WorldToCell(transform->Position(), sx, sy);
    int gx = m_goalX, gy = m_goalY;
    if (!m_goalIsCell) map->WorldToCell({m_goalWorldX, m_goalWorldY, 0}, gx, gy);

    std::vector<bool> walk((std::size_t)w * h, false);
    std::vector<float> cost;
    const std::vector<float>* costPtr = nullptr;
    GridCostLayer* layer = nullptr;
    if (useCosts) {
        layer = map->gameObject ? map->gameObject->GetComponent<GridCostLayer>() : nullptr;
        if (!layer) layer = scene->FindObjectOfType<GridCostLayer>();
    }
    if (layer) {
        std::vector<float> tiles;
        layer->FillTileCosts(tiles);
        cost.assign(walk.size(), 1.0f);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t i = (std::size_t)y * w + x;
                const int id = map->GetTile(x, y);
                float c = 0.0f;
                if (id >= 0 && (std::size_t)id < tiles.size()) c = tiles[(std::size_t)id];
                else if (id == 0) c = 1.0f;
                float over = 0.0f;
                if (layer->CellOverride(x, y, over)) c = over;
                if (c > 0.0f && std::isfinite(c)) {
                    walk[i] = true;
                    cost[i] = c;
                }
            }
        }
        costPtr = &cost;
    } else {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                walk[(std::size_t)y * w + x] = (map->GetTile(x, y) == 0);
    }

    for (GridObstacle2D* block : scene->FindObjectsOfType<GridObstacle2D>()) {
        if (!block) continue;
        std::vector<Pathfinding::Cell> cells;
        block->CoveredCells(*map, cells);
        for (const Pathfinding::Cell& c : cells) {
            if (c.x < 0 || c.y < 0 || c.x >= w || c.y >= h) continue;
            walk[(std::size_t)c.y * w + c.x] = false;
        }
    }

    m_path = Pathfinding::AStar(walk, w, h, sx, sy, gx, gy, allowDiagonal, costPtr, smoothPath);
}

void GridPathAgent2D::Update(float dt) {
    if (!m_hasGoal || m_arrived || !transform) return;
    if (!(dt > 0.0f) || !std::isfinite(dt)) dt = 0.0f;
    m_repathLeft -= dt;
    if (m_repathLeft <= 0.0f || m_path.empty()) Repath();
    Tilemap* map = Map();
    if (!map || m_path.empty()) return;

    Vec3 pos = transform->Position();
    const float stepReach = std::max(map->tileSize * 0.2f, 0.02f);
    while (m_index < (int)m_path.size()) {
        Vec3 c = map->CellToWorld(m_path[(std::size_t)m_index].x, m_path[(std::size_t)m_index].y);
        const bool last = m_index + 1 >= (int)m_path.size();
        const float arrive = last ? std::max(stopDistance, 0.02f) : stepReach;
        const float dist = Dist(pos.x, pos.y, c.x, c.y);
        if (dist <= arrive) { ++m_index; continue; }
        const float nx = (c.x - pos.x) / dist, ny = (c.y - pos.y) / dist;
        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody2D>() : nullptr;
        if (rb) rb->velocity = {nx * speed, ny * speed};
        else transform->Translate({nx * speed * dt, ny * speed * dt, 0.0f});
        return;
    }
    m_arrived = true;
    m_path.clear();
    if (auto* rb = gameObject ? gameObject->GetComponent<Rigidbody2D>() : nullptr)
        rb->velocity = {0, 0};
}

} // namespace okay
