#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/ImageWrite.hpp"
#include "okay/Graphics/Font.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

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

    // --- Dimensions whose product overflows int: refused, not an out-of-bounds write ---
    // (65536 * 65537 wraps to 65536, so the work arrays were one row short while the
    //  cell index still reached 65536 and beyond)
    {
        std::vector<bool> some(200000, true);
        auto wrapped = Pathfinding::AStar(some, 65536, 65537, 0, 0, 0, 1);
        CHECK(wrapped.empty());
        bool threw = false;
        std::vector<Pathfinding::Cell> negative;
        try { negative = Pathfinding::AStar(some, 2, 1 << 30, 0, 0, 1, 1); } catch (...) { threw = true; }
        CHECK(!threw);                      // a negative wrapped size used to throw from the vector
        CHECK(negative.empty());
        // Just past the cell limit is refused too; a normal grid is untouched.
        std::vector<bool> tiny(16, true);
        CHECK(Pathfinding::AStar(tiny, 4096, 4097, 0, 0, 1, 0).empty());
        CHECK(Pathfinding::AStar(tiny, 4, 4, 0, 0, 3, 3).size() == 7);
    }

    // --- A world position far outside any grid saturates to a cell instead of being undefined ---
    {
        Scene sc("far");
        auto* tm = sc.CreateGameObject("Map")->AddComponent<Tilemap>();
        tm->Resize(4, 4);
        int cx = 0, cy = 0;
        tm->WorldToCell({1e30f, -1e30f, 0}, cx, cy);
        CHECK(cx == std::numeric_limits<int>::max());
        CHECK(cy == std::numeric_limits<int>::min());
        CHECK(!tm->InBounds(cx, cy));
        tm->WorldToCell({std::numeric_limits<float>::quiet_NaN(), 0, 0}, cx, cy);
        CHECK(cx == 0);
    }

    auto lengthOf = [](const std::vector<Pathfinding::Cell>& path) {
        float n = 0.0f;
        for (std::size_t i = 1; i < path.size(); ++i) {
            float dx = float(path[i].x - path[i - 1].x), dy = float(path[i].y - path[i - 1].y);
            n += std::sqrt(dx * dx + dy * dy);
        }
        return n;
    };

    // --- A diagonal step cannot squeeze between two walls ---
    {
        int w, h;
        auto g = Grid({
            "#.",
            ".#",
        }, w, h);
        auto cut = Pathfinding::AStar(g, w, h, 1, 0, 0, 1, true);
        CHECK(cut.empty());
        CHECK(!Pathfinding::LineClear(g, w, h, Pathfinding::Cell{1, 0}, Pathfinding::Cell{0, 1}));
    }

    // --- A price of 0 blocks a cell the walkable flag still calls open ---
    {
        int w, h;
        auto g = Grid({"..."}, w, h);
        std::vector<float> cost = {1.0f, 0.0f, 1.0f};
        CHECK(Pathfinding::AStar(g, w, h, 0, 0, 2, 0, false, &cost).empty());
    }

    // --- The cheap road is longer in cells, and it is still the one that is used ---
    // Row 0 is mud (price 8) except the ends. Row 2 is a road (price 0.25).
    // With no prices the straight mud line is shorter, so that is what comes back.
    {
        const int w = 6, h = 3;
        std::vector<bool> g((std::size_t)w * h, true);
        std::vector<float> cost((std::size_t)w * h, 1.0f);
        for (int x = 1; x <= 4; ++x) cost[(std::size_t)x] = 8.0f;                 // mud on the short way
        for (int x = 0; x < w; ++x) cost[(std::size_t)2 * w + x] = 0.25f;         // road along the bottom
        auto plain = Pathfinding::AStar(g, w, h, 0, 0, 5, 0, false);
        auto cheap = Pathfinding::AStar(g, w, h, 0, 0, 5, 0, false, &cost);
        CHECK(plain.size() == 6);
        for (const auto& c : plain) CHECK(c.y == 0);
        CHECK(!cheap.empty());
        CHECK(cheap.front().x == 0 && cheap.front().y == 0);
        CHECK(cheap.back().x == 5 && cheap.back().y == 0);
        bool usedRoad = false, usedMud = false;
        for (const auto& c : cheap) {
            if (c.y == 2) usedRoad = true;
            if (c.y == 0 && c.x >= 1 && c.x <= 4) usedMud = true;
        }
        CHECK(usedRoad);
        CHECK(!usedMud);
        CHECK(cheap.size() > plain.size());
    }

    // --- Tile prices: mud is walkable but the path takes the road ---
    {
        Scene scene("Prices");
        auto* tm = scene.CreateGameObject("Map")->AddComponent<Tilemap>();
        tm->Resize(6, 3);
        for (int x = 1; x <= 4; ++x) tm->SetTile(x, 0, 2);
        for (int x = 0; x < 6; ++x) tm->SetTile(x, 2, 1);
        std::vector<float> tiles = {1.0f, 0.25f, 8.0f};
        auto cheap = Pathfinding::OnTilemap(*tm, 0, 0, 5, 0, false, &tiles);
        CHECK(!cheap.empty());
        bool usedRoad = false, usedMud = false;
        for (const auto& c : cheap) {
            if (tm->GetTile(c.x, c.y) == 1) usedRoad = true;
            if (tm->GetTile(c.x, c.y) == 2) usedMud = true;
        }
        CHECK(usedRoad);
        CHECK(!usedMud);
        // No cost table: tile 2 is a wall, and the old search still goes around it.
        auto old = Pathfinding::OnTilemap(*tm, 0, 0, 5, 0);
        CHECK(!old.empty());
        for (const auto& c : old) CHECK(tm->GetTile(c.x, c.y) == 0);
    }

    // --- Smoothing is shorter than the staircase and does not cross the wall ---
    {
        int w, h;
        auto g = Grid({
            ".......",
            ".......",
            "...#...",
            ".......",
            ".......",
        }, w, h);
        auto raw = Pathfinding::AStar(g, w, h, 0, 0, 6, 4, false);
        auto smooth = Pathfinding::AStar(g, w, h, 0, 0, 6, 4, false, nullptr, true);
        CHECK(raw.size() > smooth.size());
        CHECK(lengthOf(smooth) < lengthOf(raw) - 0.05f);
        CHECK(!Pathfinding::LineClear(g, w, h, Pathfinding::Cell{0, 0}, Pathfinding::Cell{6, 4}));
        CHECK(smooth.front().x == 0 && smooth.front().y == 0);
        CHECK(smooth.back().x == 6 && smooth.back().y == 4);
        for (std::size_t i = 1; i < smooth.size(); ++i)
            CHECK(Pathfinding::LineClear(g, w, h, smooth[i - 1], smooth[i]));
        for (const auto& c : smooth) CHECK(g[(std::size_t)c.y * w + c.x]);
    }

    // --- An obstacle on one agent's path replans that agent and leaves the other ---
    {
        Scene scene("Blocks");
        auto* tm = scene.CreateGameObject("Map")->AddComponent<Tilemap>();
        tm->Resize(7, 3);
        auto* a = scene.CreateGameObject("Walker")->AddComponent<GridPathAgent2D>();
        a->gameObject->transform->SetPosition(tm->CellToWorld(0, 1));
        a->SetCell(6, 1);
        CHECK(a->Path().size() == 7);
        auto* b = scene.CreateGameObject("Other")->AddComponent<GridPathAgent2D>();
        b->gameObject->transform->SetPosition(tm->CellToWorld(0, 0));
        b->SetCell(2, 0);
        const auto kept = b->Path();
        CHECK(kept.size() == 3);
        for (const auto& c : kept) CHECK(c.y == 0);

        auto* block = scene.CreateGameObject("Crate")->AddComponent<GridObstacle2D>();
        block->Place(3, 1);
        CHECK(!a->Path().empty());
        CHECK(a->Path().front().x == 0 && a->Path().front().y == 1);
        CHECK(a->Path().back().x == 6 && a->Path().back().y == 1);
        for (const auto& c : a->Path()) CHECK(!(c.x == 3 && c.y == 1));
        for (std::size_t i = 1; i < a->Path().size(); ++i) {
            int dx = std::abs(a->Path()[i].x - a->Path()[i - 1].x);
            int dy = std::abs(a->Path()[i].y - a->Path()[i - 1].y);
            CHECK(dx + dy == 1);
            CHECK(tm->GetTile(a->Path()[i].x, a->Path()[i].y) == 0);
        }
        CHECK(b->Path().size() == kept.size());
        for (std::size_t i = 0; i < kept.size(); ++i)
            CHECK(b->Path()[i].x == kept[i].x && b->Path()[i].y == kept[i].y);
    }

    // --- A script sends the agent, and a weighted smoothed path can be drawn ---
    {
        Scene scene("Shot");
        auto* tm = scene.CreateGameObject("Map")->AddComponent<Tilemap>();
        const char* rows[] = {
            "##########",
            "#S..####.#",
            "#.##MMMM.#",
            "#.##.....#",
            "#.RRRRRR.#",
            "#.##.....#",
            "#.##....G#",
            "##########",
        };
        const int h = 8, w = 10;
        tm->Resize(w, h);
        int sx = 1, sy = 1, gx = 8, gy = 6;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                char ch = rows[y][x];
                int id = 0;
                if (ch == '#') id = 3;
                else if (ch == 'M') id = 2;
                else if (ch == 'R') id = 1;
                tm->SetTile(x, y, id);
                if (ch == 'S') { sx = x; sy = y; }
                if (ch == 'G') { gx = x; gy = y; }
            }
        }
        auto* costs = tm->gameObject->AddComponent<GridCostLayer>();
        costs->roadTile = 1; costs->roadCost = 0.25f;
        costs->mudTile = 2; costs->mudCost = 12.0f;
        auto* walker = scene.CreateGameObject("Walker");
        walker->transform->SetPosition(tm->CellToWorld(sx, sy));
        auto* agent = walker->AddComponent<GridPathAgent2D>();
        agent->useCosts = true;
        agent->smoothPath = true;
        agent->allowDiagonal = false;
        std::vector<float> tiles = {1.0f, 0.25f, 12.0f, 0.0f};
        auto raw = Pathfinding::OnTilemap(*tm, sx, sy, gx, gy, false, &tiles, false);
        agent->SetCell(gx, gy);
        const auto& path = agent->Path();
        CHECK(!path.empty());
        CHECK(path.front().x == sx && path.front().y == sy);
        CHECK(path.back().x == gx && path.back().y == gy);
        CHECK(path.size() < raw.size());
        CHECK(lengthOf(path) < lengthOf(raw) - 0.05f);
        bool rawRoad = false, rawMud = false;
        for (const auto& c : raw) {
            int id = tm->GetTile(c.x, c.y);
            if (id == 1) rawRoad = true;
            if (id == 2) rawMud = true;
        }
        CHECK(rawRoad);
        CHECK(!rawMud);
        for (const auto& c : path) {
            int id = tm->GetTile(c.x, c.y);
            CHECK(id != 2);
            CHECK(id != 3);
        }
        std::vector<bool> open((std::size_t)w * h, false);
        std::vector<bool> noMud((std::size_t)w * h, false);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int id = tm->GetTile(x, y);
                open[(std::size_t)y * w + x] = id != 3;
                noMud[(std::size_t)y * w + x] = id != 3 && id != 2;
            }
        for (std::size_t i = 1; i < path.size(); ++i) {
            CHECK(Pathfinding::LineClear(open, w, h, path[i - 1], path[i]));
            CHECK(Pathfinding::LineClear(noMud, w, h, path[i - 1], path[i]));
        }

        std::printf("  weighted smoothed path:");
        for (const auto& c : path) std::printf(" (%d,%d)", c.x, c.y);
        std::printf("\n");

        auto* sc = walker->AddComponent<ScriptComponent>();
        std::string err;
        CHECK(sc->LoadSource("func start() { grid_move_cell(8, 6)\n }\n", &err));
        scene.Start();
        CHECK(agent->Path().back().x == gx && agent->Path().back().y == gy);

        const int cell = 28, legend = 36;
        const int imgW = w * cell, imgH = h * cell + legend;
        std::vector<unsigned char> px((std::size_t)imgW * imgH * 3, 20);
        auto put = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b) {
            if (x < 0 || y < 0 || x >= imgW || y >= imgH) return;
            unsigned char* p = px.data() + ((std::size_t)y * imgW + x) * 3;
            p[0] = r; p[1] = g; p[2] = b;
        };
        auto fill = [&](int x0, int y0, int x1, int y1, unsigned char r, unsigned char g, unsigned char b) {
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) put(x, y, r, g, b);
        };
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                unsigned char r = 70, g = 120, b = 70;
                int id = tm->GetTile(x, y);
                if (id == 3) { r = 32; g = 34; b = 42; }
                else if (id == 2) { r = 110; g = 78; b = 42; }
                else if (id == 1) { r = 196; g = 168; b = 90; }
                fill(x * cell, y * cell, x * cell + cell - 1, y * cell + cell - 1, r, g, b);
            }
        }
        auto line = [&](int x0, int y0, int x1, int y1) {
            int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
            int sxn = x0 < x1 ? 1 : -1, syn = y0 < y1 ? 1 : -1;
            int err = dx - dy;
            for (;;) {
                fill(x0 - 2, y0 - 2, x0 + 3, y0 + 3, 255, 220, 40);
                if (x0 == x1 && y0 == y1) break;
                int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x0 += sxn; }
                if (e2 < dx) { err += dx; y0 += syn; }
            }
        };
        auto centre = [&](const Pathfinding::Cell& c, int& x, int& y) {
            x = c.x * cell + cell / 2;
            y = c.y * cell + cell / 2;
        };
        for (std::size_t i = 1; i < path.size(); ++i) {
            int x0, y0, x1, y1;
            centre(path[i - 1], x0, y0);
            centre(path[i], x1, y1);
            line(x0, y0, x1, y1);
        }
        int hx, hy;
        centre(path.front(), hx, hy);
        fill(hx - 5, hy - 5, hx + 6, hy + 6, 80, 220, 255);
        centre(path.back(), hx, hy);
        fill(hx - 5, hy - 5, hx + 6, hy + 6, 255, 80, 160);
        auto text = [&](int x, int y, const char* s) {
            for (int i = 0; s[i]; ++i) {
                for (int gy = 0; gy < 8; ++gy)
                    for (int gx = 0; gx < 8; ++gx)
                        if (Font8x8::Pixel(s[i], gx, gy)) put(x + i * 8 + gx, y + gy, 230, 230, 220);
            }
        };
        text(8, h * cell + 8, "Yellow: weighted smoothed path");
        text(8, h * cell + 20, "Gold road, brown mud, grey wall");
        const char* shot = std::getenv("OKAY_PATH_PNG");
        if (shot && shot[0]) CHECK(ImageWrite::WritePNG(shot, imgW, imgH, 3, px.data()));
    }

    TEST_MAIN_RESULT();
}
