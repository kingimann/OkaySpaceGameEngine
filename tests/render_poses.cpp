// Headless pictures of the player body poses (software rasteriser, no window), as one contact sheet:
//   render_poses <out.png>     (not in CMake; build by hand like bench_busy)
// Stand, crouch, crouch-walk, prone, run, three frames of a stop from a run, and a hard landing at its deepest point,
// each seen from the side, with the collider height drawn as a bar so the visible body can be compared with it.
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

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    const int TW = 300, TH = 340, N = 9;
    Raster sheet; sheet.Resize(TW * N, TH); sheet.Clear(0xFF303030u);
    auto shot = [&](int slot, const char* label, auto&& setup, float startY = 0.02f) {
        Scene sc("p"); sc.physicsEnabled = true; sc.UseFixedStepPhysics(true);
        GameObject* f = sc.CreateGameObject("Floor"); f->transform->localPosition = {0, -0.5f, 0};
        auto* fm = f->AddComponent<MeshRenderer>(); fm->mesh = Mesh::Cube(1.0f); fm->color = Color::FromBytes(120, 124, 132);
        f->transform->localScale = {40, 1, 40}; f->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        GameObject* p = Templates::BuildPlayerBody(sc, {0, startY, 0}, "Player");
        PlayerController::Add(p, PlayerController::ViewMode::Third, PlayerView::View::Third);
        Templates::MountEyeCamera(sc, p, "Main Camera");
        sc.Start();
        const float dt = 1.0f / 60.0f;
        auto frames = [&](double s, std::vector<char> k) { for (int i = 0, n = (int)(s / dt + 0.5); i < n; ++i) { Input::FeedKeys(k); sc.Update(dt); } };
        setup(sc, p, frames);
        Input::FeedKeys({});
        const Vec3 at = p->transform->Position() + Vec3{0, 0.9f, 0};
        const Vec3 eye = at + Vec3{4.0f, 0.3f, 0.0f};   // from the player's right side
        Raster r; r.Resize(TW, TH); r.Clear(0xFFE0C8A0u);
        RenderMeshes(r, sc, Mat4::Perspective(35.0f, (float)TW / TH, 0.1f, 100.0f) * Mat4::LookAt(eye, at, Vec3::Up), eye);
        auto* box = p->GetComponent<BoxCollider3D>();
        // collider top: a thin bar at the screen height of the collider top (projected)
        const Mat4 vp = Mat4::Perspective(35.0f, (float)TW / TH, 0.1f, 100.0f) * Mat4::LookAt(eye, at, Vec3::Up);
        Vec3 top{0, p->transform->Position().y + box->offset.y + box->size.y * 0.5f, 0.6f};
        Vec2 sp; float d = 0;
        (void)sp; (void)d; (void)vp; (void)top;
        Text(r, label, 6, 6, 2, 0xFFFFFFFFu);
        char b[48]; std::snprintf(b, sizeof b, "collider %.2f m", box->size.y); Text(r, b, 6, TH - 18, 1, 0xFFFFFFFFu);
        for (int y = 0; y < TH; ++y) std::memcpy(&sheet.color[(size_t)y * sheet.width + slot * TW], &r.color[(size_t)y * TW], TW * sizeof(std::uint32_t));
    };
    shot(0, "stand", [](Scene&, GameObject*, auto& fr) { fr(1.0, {}); });
    shot(1, "crouch", [](Scene&, GameObject*, auto& fr) { fr(0.5, {}); fr(1.0 / 60, {Input::KeyCtrl}); fr(1.0, {}); });
    shot(2, "crouch-walk", [](Scene&, GameObject*, auto& fr) { fr(0.5, {}); fr(1.0 / 60, {Input::KeyCtrl}); fr(1.0, {'w'}); });
    shot(3, "prone", [](Scene&, GameObject*, auto& fr) { fr(0.5, {}); fr(1.0 / 60, {'z'}); fr(1.5, {}); });
    shot(4, "run", [](Scene&, GameObject*, auto& fr) { fr(0.5, {}); fr(1.2, {'w', Input::KeyShift}); });
    for (int k = 0; k < 3; ++k)
        shot(5 + k, k == 0 ? "stop +0.03s" : k == 1 ? "stop +0.10s" : "stop +0.30s", [k](Scene&, GameObject*, auto& fr) {
            fr(0.5, {}); fr(1.5, {'w', Input::KeyShift}); fr(k == 0 ? 0.03 : k == 1 ? 0.10 : 0.30, {}); });
    shot(8, "hard landing", [](Scene&, GameObject* p, auto& fr) {
        auto* rb = p->GetComponent<Rigidbody3D>();
        for (int i = 0; i < 400 && rb->velocity.y > -3.0f; ++i) fr(1.0 / 60, {});   // wait until it is falling
        for (int i = 0; i < 400 && !p->GetComponent<ThirdPersonController>()->IsGrounded(); ++i) fr(1.0 / 60, {});
        fr(0.12, {});
    }, 9.0f);
    Image img; std::vector<std::uint8_t> rgba(sheet.color.size() * 4);
    std::memcpy(rgba.data(), sheet.color.data(), rgba.size());
    img.Set(sheet.width, sheet.height, rgba);
    return img.SavePNG(argv[1]) ? 0 : 2;
}
