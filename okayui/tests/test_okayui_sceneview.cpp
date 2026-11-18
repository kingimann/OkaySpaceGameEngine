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
    TEST_MAIN_RESULT();
}
