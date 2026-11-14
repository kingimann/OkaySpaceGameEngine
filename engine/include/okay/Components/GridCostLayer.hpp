#pragma once
#include "okay/Scene/Component.hpp"
#include <vector>

namespace okay {

/// Movement prices for a 2D tilemap. A Grid Path Agent pays them only when
/// "Pay for mud and roads" is on. Empty tiles (id 0) cost 1. A price of 0 or
/// less is a wall. Road and mud are the two tiles the Inspector sets; scripts
/// can price any other tile or a single cell.
class GridCostLayer : public Behaviour {
public:
    int roadTile = 1;
    float roadCost = 0.5f;
    int mudTile = 2;
    float mudCost = 4.0f;

    /// Price of one tile id. 0 or less makes that tile a wall.
    void SetTileCost(int tileId, float cost);
    /// Price of one cell, on top of its tile. 0 or less blocks that cell.
    void SetCellCost(int x, int y, float cost);
    void ClearCellCost(int x, int y);

    /// Tile-id prices, index = id. Id 0 is 1 unless a call replaced it.
    void FillTileCosts(std::vector<float>& out) const;
    /// True when this cell has its own price (written to `cost`).
    bool CellOverride(int x, int y, float& cost) const;

    // Saved when a script priced a tile or a cell beyond the road and mud fields.
    const std::string& ExtraTiles() const { return m_extraTiles; }
    const std::string& CellText() const { return m_cellText; }
    void SetExtraTiles(const std::string& text);
    void SetCellText(const std::string& text);

private:
    struct Extra { int id = 0; float cost = 1.0f; };
    struct Cell { int x = 0; int y = 0; float cost = 1.0f; };
    std::vector<Extra> m_extra;
    std::vector<Cell> m_cells;
    std::string m_extraTiles;
    std::string m_cellText;

    void RebuildExtraText();
    void RebuildCellText();
    void ParseExtra();
    void ParseCells();
};
} // namespace okay
