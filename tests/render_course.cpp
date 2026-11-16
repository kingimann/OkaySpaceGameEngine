// Headless pictures of the player obstacle course (software rasteriser, no window): render_course <scene> <out-dir>
// Flat-shaded, no text signs, no water shine: a map of where things are, plus the F3 player lines drawn with the 8x8 font.
#include <Okay.hpp>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace okay;

static void Text(Raster& r, const std::string& s, int x, int y, int px, std::uint32_t col) {
    for (char c : s) {
        for (int j = 0; j < 8; ++j) for (int i = 0; i < 8; ++i)
            if (Font8x8::Pixel(c, i, j))
                for (int dy = 0; dy < px; ++dy) for (int dx = 0; dx < px; ++dx) {
                    const int X = x + i * px + dx, Y = y + j * px + dy;
                    if (X >= 0 && Y >= 0 && X < r.width && Y < r.height) r.color[(size_t)Y * r.width + X] = col;
                }
        x += 8 * px;
    }
}
static bool SavePic(const Raster& r, const std::string& path) {
    std::vector<std::uint8_t> rgba(r.color.size() * 4);
    std::memcpy(rgba.data(), r.color.data(), rgba.size());
    Image img; img.Set(r.width, r.height, rgba);
    return img.SavePNG(path);
}

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    Scene sc("c"); sc.physicsEnabled = true; sc.UseFixedStepPhysics(true);
    if (!SceneSerializer::LoadFromFile(sc, argv[1])) { std::puts("load failed"); return 1; }
    sc.Start();
    GameObject* p = sc.Find("Player");
    auto frames = [&](double s, std::vector<char> k) { for (int i = 0, n = (int)(s * 60); i < n; ++i) { Input::FeedKeys(k); sc.Update(1.0f / 60.0f); } Input::FeedKeys({}); };
    struct Shot { const char* name; Vec3 eye, at; float px; bool f3; Vec3 player; float yaw; std::vector<char> keys; double secs; };
    const Shot shots[] = {
        {"01_start_overview", {16, 12, 12}, {0, 0, -30}, 0, false, {0, 0.05f, 6}, 0, {}, 1.0},
        {"02_ramps", {-16, 9, -4}, {-4, 2, -16}, 0, false, {-5, 1.6f, -15.6f}, 0, {}, 2.0},
        {"03_stairs_and_steps", {10, 7, -30}, {0, 1, -40}, 0, false, {0, 0.05f, -29.5f}, 0, {'w'}, 1.2},
        {"04_moving_platform_and_bridge", {-14, 7, -86}, {0, 0, -100}, 0, false, {-8, 0.4f, -96}, 0, {}, 1.5},
        {"05_water_and_voxel_hill", {14, 9, -122}, {0, -1, -146}, 0, false, {0, 0.05f, -128}, 0, {'w'}, 1.0},
        {"06_ladder_tower", {12, 9, -158}, {0, 3, -170}, 0, false, {0, 0.05f, -168}, 0, {'w'}, 2.5},
    };
    for (const Shot& s : shots) {
        p->transform->SetPosition(s.player);
        frames(s.secs, s.keys);
        Raster r; r.Resize(960, 540);
        r.Clear(0xFFE0C8A0u);   // sky (ABGR)
        const Mat4 view = Mat4::LookAt(s.eye, s.at, Vec3::Up);
        const Mat4 proj = Mat4::Perspective(60.0f, 960.0f / 540.0f, 0.1f, 400.0f);
        RenderMeshes(r, sc, proj * view, s.eye);
        Text(r, s.name, 8, 8, 2, 0xFFFFFFFFu);
        if (s.f3) {}
        SavePic(r, std::string(argv[2]) + "/" + s.name + ".png");
        std::printf("wrote %s\n", s.name);
    }
    // F3 panel picture: the player on the 30 degree ramp, with the player lines
    p->transform->SetPosition({-5, 1.6f, -15.6f});
    frames(1.5, {});
    Raster r; r.Resize(960, 540); r.Clear(0xFFE0C8A0u);
    const Mat4 view = Mat4::LookAt({-9, 5, -9}, {-5, 2, -15}, Vec3::Up);
    RenderMeshes(r, sc, Mat4::Perspective(60.0f, 960.0f / 540.0f, 0.1f, 400.0f) * view, {-9, 5, -9});
    int y = 16;
    Text(r, "Performance (F3)", 16, y, 2, 0xFF78FF50u); y += 24;
    Text(r, "60.0 fps  16.67 ms avg", 16, y, 2, 0xFF78FF50u); y += 24;
    for (const std::string& l : PlayerDebug::Lines(sc)) { Text(r, l, 16, y, 2, 0xFF78FF50u); y += 20; }
    SavePic(r, std::string(argv[2]) + "/07_f3_player_lines_on_30_degree_ramp.png");
    return 0;
}
