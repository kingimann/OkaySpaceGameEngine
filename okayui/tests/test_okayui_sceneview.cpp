// The picture of a scene without UI (editor/SceneViewCore.hpp, window-mode plan 6.24 step 6): a real okay::Scene with a cube and a camera is rendered through
// the orbit camera and the game camera. Checks size fitting, that something is drawn where it should be, that orbiting changes the picture, that an empty
// scene is transparent and that the same input gives the same pixels.
#include "test_framework.hpp"
#include "SceneViewCore.hpp"

using namespace okay;
namespace sv = oked::sceneview;

static std::uint64_t Hash(const std::uint32_t* px, int w, int h) {
    std::uint64_t a = 1469598103934665603ull;
    for (long i = 0, n = (long)w * h; i < n; ++i) { a ^= px[i]; a *= 1099511628211ull; }
    return a;
}
static int Lit(const std::uint32_t* px, int w, int h) { int n = 0; for (long i = 0, m = (long)w * h; i < m; ++i) if (px[i] >> 24) ++n; return n; }

int main() {
    RUN_SUITE("okayui_sceneview");
    int w = 0, h = 0;
    sv::FitSize(800, 600, 1.0f, 1920, w, h);  CHECK(w == 800 && h == 600);
    sv::FitSize(4000, 2000, 1.0f, 1920, w, h); CHECK(w == 1920 && h == 960);
    sv::FitSize(800, 600, 0.5f, 1920, w, h);  CHECK(w == 400 && h == 300);
    sv::FitSize(800, 600, 0.0f, 1920, w, h);  CHECK(w == 200 && h == 150);    // the scale is held at 0.25
    sv::FitSize(0, 0, 1.0f, 1920, w, h);      CHECK(w == 1 && h == 1);

    Scene empty("Empty");
    sv::Renderer r;
    sv::OrbitCamera cam; cam.dist = 6.0f; cam.yawDeg = 30; cam.pitchDeg = 20;
    const int W = 160, H = 120;
    {
        const std::uint32_t* px = r.Render(empty, sv::EditorFrame(cam, W, H));
        CHECK(px != nullptr);
        CHECK(!sv::AnyPixel(px, W, H));   // nothing to draw: fully transparent
    }

    Scene s("Cube");
    GameObject* cube = s.CreateGameObject("Cube");
    cube->AddComponent<MeshRenderer>()->mesh = Mesh::Cube(2.0f);
    GameObject* camGo = s.CreateGameObject("Camera");
    camGo->transform->SetPosition({0, 0, 6});
    Camera* c = camGo->AddComponent<Camera>();
    s.mainCamera = c;
    s.Start(); s.Update(0.0f);

    const std::uint32_t* a = r.Render(s, sv::EditorFrame(cam, W, H));
    CHECK(a != nullptr);
    const int litA = Lit(a, W, H);
    CHECK(litA > 500 && litA < W * H);
    CHECK((a[(H / 2) * W + W / 2] >> 24) != 0);       // the cube is in the middle
    CHECK((a[2 * W + 2] >> 24) == 0);                  // the corner is empty
    const std::uint64_t ha = Hash(a, W, H);
    CHECK(Hash(r.Render(s, sv::EditorFrame(cam, W, H)), W, H) == ha);   // deterministic

    cam.yawDeg += 90;
    const std::uint32_t* b = r.Render(s, sv::EditorFrame(cam, W, H));
    CHECK(Hash(b, W, H) != ha);                        // orbiting changes the picture
    CHECK(Lit(b, W, H) > 500);
    cam.dist = 60.0f;                                  // far away: smaller
    const int litFar = Lit(r.Render(s, sv::EditorFrame(cam, W, H)), W, H);
    CHECK(litFar < litA && litFar > 0);

    // the game view: the main camera looks down -Z at the cube from z = 6
    Camera* gc = sv::GameCamera(s);
    CHECK(gc == c);
    sv::Frame gf;
    CHECK(sv::GameFrame(gc, W, H, gf));
    CHECK(gf.eye.z > 5.9f && gf.eye.z < 6.1f);
    const std::uint32_t* g = r.Render(s, gf);
    CHECK((g[(H / 2) * W + W / 2] >> 24) != 0);
    CHECK(Lit(g, W, H) > 200);
    CHECK(!sv::GameFrame(nullptr, W, H, gf));

    // ---- round 11: grid, picking, outline, gizmo maths ----
    {
        sv::OrbitCamera oc; oc.dist = 10; oc.yawDeg = 30; oc.pitchDeg = 30;
        const sv::Frame f = sv::EditorFrame(oc, 320, 240);
        float x = 0, y = 0;
        CHECK(sv::ToScreen(f, {0, 0, 0}, x, y) && std::fabs(x - 160) < 2 && std::fabs(y - 120) < 2);   // the target is the middle of the picture
        const auto grid = sv::GroundGrid(f, {0, 0, 0}, 1.0f, 10);
        int major = 0, xa = 0, za = 0; for (const auto& l : grid) { major += l.major; xa += l.xAxis; za += l.zAxis; }
        CHECK(grid.size() >= 30 && major >= 4 && xa == 1 && za == 1);
        CHECK(sv::GroundGrid(f, {0, 0, 0}, 0.0f, 10).empty());
        // pick: the cube (2 wide at the origin) is hit in the middle, nothing in the corner; the nearer of two cubes wins
        float dist = 0;
        CHECK(sv::Pick(s, f, 160, 120, &dist) == cube && dist > 5 && dist < 15);
        CHECK(sv::Pick(s, f, 3, 3) == nullptr);
        GameObject* near = s.CreateGameObject("Near");
        near->AddComponent<MeshRenderer>()->mesh = Mesh::Cube(1.0f);
        sv::OrbitCamera oc2; oc2.dist = 10; oc2.yawDeg = 0; oc2.pitchDeg = 0;   // looking down -Z from +Z
        const sv::Frame f2 = sv::EditorFrame(oc2, 320, 240);
        near->transform->SetPosition({0, 0, 3});
        CHECK(sv::Pick(s, f2, 160, 120) == near);
        near->active = false;
        CHECK(sv::Pick(s, f2, 160, 120) == cube);
        near->active = true; near->transform->SetPosition({0, 0, -3});
        CHECK(sv::Pick(s, f2, 160, 120) == cube);   // behind the cube
        CHECK(sv::BoundsOutline(*cube, f).size() == 12 && sv::BoundsOutline(*camGo, f).empty());
        // gizmo maths
        sv::Ray3 r; CHECK(sv::PixelRay(f2, 160, 120, r) && std::fabs(r.d.z + 1.0f) < 1e-3f);
        float t = 0; CHECK(sv::ClosestOnAxis({0, 0, 0}, {1, 0, 0}, sv::Ray3{{2, 5, 0}, {0, -1, 0}}, t) && std::fabs(t - 2.0f) < 1e-4f);
        CHECK(!sv::ClosestOnAxis({0, 0, 0}, {1, 0, 0}, sv::Ray3{{0, 5, 0}, {1, 0, 0}}, t));   // parallel
        const Vec3 moved = sv::MoveAlong({1, 2, 3}, sv::Axis::X, 1.0f, 4.0f);
        CHECK(moved.x == 4.0f && moved.y == 2.0f && moved.z == 3.0f);
        CHECK(sv::ScaleFactor(2.0f, 4.0f) == 2.0f && sv::ScaleFactor(0.0f, 4.0f) == 1.0f && sv::ScaleFactor(1.0f, -5.0f) == 0.01f);
        float deg = 0;   // a quarter turn about Y seen from above: grab at +X, now at -Z
        CHECK(sv::RotateAngle({0, 0, 0}, sv::Axis::Y, sv::Ray3{{1, 5, 0}, {0, -1, 0}}, sv::Ray3{{0, 5, -1}, {0, -1, 0}}, deg) && std::fabs(deg - 90.0f) < 0.1f);
    }
    TEST_MAIN_RESULT();
}
