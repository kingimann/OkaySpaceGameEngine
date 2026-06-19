#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <vector>

namespace okay {

/// A grid of integer tile ids (0 = empty), positioned by the GameObject's
/// Transform. Provides cell<->world conversion and simple queries — the data
/// backbone of a 2D tile-based game.
class Tilemap : public Behaviour {
public:
    float tileSize = 1.0f;

    void Resize(int width, int height) {
        m_w = width < 0 ? 0 : width;
        m_h = height < 0 ? 0 : height;
        m_tiles.assign(static_cast<std::size_t>(m_w) * m_h, 0);
    }
    int Width() const { return m_w; }
    int Height() const { return m_h; }

    bool InBounds(int x, int y) const { return x >= 0 && y >= 0 && x < m_w && y < m_h; }

    int GetTile(int x, int y) const {
        return InBounds(x, y) ? m_tiles[static_cast<std::size_t>(y) * m_w + x] : 0;
    }
    void SetTile(int x, int y, int id) {
        if (InBounds(x, y)) m_tiles[static_cast<std::size_t>(y) * m_w + x] = id;
    }
    void Fill(int id) { std::fill(m_tiles.begin(), m_tiles.end(), id); }

    /// World-space center of cell (x, y).
    Vec3 CellToWorld(int x, int y) const {
        Vec3 o = transform ? transform->Position() : Vec3::Zero;
        return {o.x + (x + 0.5f) * tileSize, o.y + (y + 0.5f) * tileSize, o.z};
    }
    /// Cell containing the given world position.
    void WorldToCell(const Vec3& world, int& x, int& y) const {
        if (tileSize == 0.0f) { x = y = 0; return; }   // avoid divide-by-zero
        Vec3 o = transform ? transform->Position() : Vec3::Zero;
        x = static_cast<int>(Mathf::Floor((world.x - o.x) / tileSize));
        y = static_cast<int>(Mathf::Floor((world.y - o.y) / tileSize));
    }

    /// Number of cells holding the given id.
    int Count(int id) const {
        int n = 0;
        for (int t : m_tiles) if (t == id) ++n;
        return n;
    }

    const std::vector<int>& Tiles() const { return m_tiles; }

private:
    int m_w = 0, m_h = 0;
    std::vector<int> m_tiles;
};

} // namespace okay
