#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Build a walkable grid from rows of text: '.' walkable, '#' wall.
static std::vector<bool> Grid(const std::vector<const char*>& rows, int& w, int& h) {
    h = (int)rows.size();
    w = h ? (int)std::string(rows[0]).size() : 0;
    std::vector<bool> g(static_cast<std::size_t>(w) * h, false);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            g[static_cast<std::size_t>(y) * w + x] = (rows[y][x] != '#');
    return g;
}

int main() {
    RUN_SUITE("pathfinding");

    // --- Straight path on an open row ---
    {
        int w, h;
        auto g = Grid({"....."}, w, h);
        auto path = Pathfinding::AStar(g, w, h, 0, 0, 4, 0);
        CHECK(path.size() == 5);
        CHECK(path.front().x == 0 && path.front().y == 0);
        CHECK(path.back().x == 4 && path.back().y == 0);
    }

    // --- Around a wall: must detour, so longer than Manhattan distance ---
    {
        int w, h;
        auto g = Grid({
            ".....",
            "###.#",
            ".....",
        }, w, h);
        auto path = Pathfinding::AStar(g, w, h, 0, 0, 0, 2);
        CHECK(!path.empty());
        CHECK(path.front().x == 0 && path.front().y == 0);
        CHECK(path.back().x == 0 && path.back().y == 2);
        // Manhattan distance is 2, but the wall forces a detour through column 3.
        CHECK(path.size() > 3);
        // Every step is adjacent and walkable.
        for (std::size_t i = 1; i < path.size(); ++i) {
            int dx = path[i].x - path[i - 1].x, dy = path[i].y - path[i - 1].y;
            CHECK(std::abs(dx) + std::abs(dy) == 1);
            CHECK(g[static_cast<std::size_t>(path[i].y) * w + path[i].x]);
        }
    }

    // --- No path when the goal is walled off entirely ---
    {
        int w, h;
        auto g = Grid({
            "...#.",
            "...#.",
            "...#.",
        }, w, h);
        auto path = Pathfinding::AStar(g, w, h, 0, 0, 4, 0);
        CHECK(path.empty());
    }

    // --- Diagonal movement yields a shorter step count ---
    {
        int w, h;
        auto g = Grid({
            ".....",
            ".....",
            ".....",
        }, w, h);
        auto ortho = Pathfinding::AStar(g, w, h, 0, 0, 2, 2, false);
        auto diag  = Pathfinding::AStar(g, w, h, 0, 0, 2, 2, true);
        CHECK(ortho.size() == 5);      // 4 ortho steps
        CHECK(diag.size() == 3);       // 2 diagonal steps
    }

    // --- Over a Tilemap: tile id 0 walkable, non-zero is a wall ---
    {
        Scene scene("Nav");
        GameObject* go = scene.CreateGameObject("Map");
        auto* tm = go->AddComponent<Tilemap>();
        tm->Resize(5, 1);
        tm->SetTile(2, 0, 1); // a wall in the middle
        auto blocked = Pathfinding::OnTilemap(*tm, 0, 0, 4, 0);
        CHECK(blocked.empty()); // wall splits the 1-row corridor
        tm->SetTile(2, 0, 0);   // clear it
        auto open = Pathfinding::OnTilemap(*tm, 0, 0, 4, 0);
        CHECK(open.size() == 5);
    }

    TEST_MAIN_RESULT();
}
