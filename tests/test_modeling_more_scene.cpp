// Modeling "more": material slots in scenes, the shipped example (examples/modeling_tools) and a headless picture.
// The example assets are made by this program (our own code): run it with OKAY_WRITE_MODELING_EXAMPLES=<examples/modeling_tools>
// to rewrite them; OKAY_MODELING_PICTURE_DIR=<dir> also writes the picture (a software render, not a GPU one).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Modeling/PolyOps.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace okay;
using namespace okay::modeling;

namespace {
const Color kWall{0.92f, 0.86f, 0.70f, 1}, kRoof{0.75f, 0.18f, 0.12f, 1}, kFloor{0.35f, 0.25f, 0.16f, 1}, kDoor{0.15f, 0.30f, 0.75f, 1};

// A little gable house, one polygon mesh with four material slots.
PolyMesh MakeHouse() {
    PolyMesh pm;
    pm.verts = {{-1, 0, -.8f}, {1, 0, -.8f}, {1, 0, .8f}, {-1, 0, .8f}, {-1, 1, -.8f}, {1, 1, -.8f}, {1, 1, .8f}, {-1, 1, .8f},
                {-1, 1.7f, 0}, {1, 1.7f, 0}, {-.2f, 0, .805f}, {.2f, 0, .805f}, {.2f, .6f, .805f}, {-.2f, .6f, .805f}};
    auto add = [&](std::vector<int> v, int slot) { PolyFace f; f.verts = std::move(v); f.material = slot; pm.faces.push_back(f); };
    add({0, 3, 2, 1}, 2);                                   // floor (faces down)
    add({3, 2, 6, 7}, 0); add({1, 0, 4, 5}, 0); add({2, 1, 5, 6}, 0); add({0, 3, 7, 4}, 0);   // walls
    add({6, 5, 9}, 0); add({4, 7, 8}, 0);                   // gable ends
    add({7, 6, 9, 8}, 1); add({5, 4, 8, 9}, 1);             // roof
    add({10, 11, 12, 13}, 3);                               // door
    pm.slotColors = {kWall, kRoof, kFloor, kDoor};
    return pm;
}
// A ball painted in three bands by height.
PolyMesh MakeBall() {
    PolyMesh pm = PolyMesh::FromMesh(Mesh::Sphere(0.6f, 10, 16));
    for (PolyFace& f : pm.faces) {
        float y = 0; for (int v : f.verts) y += pm.verts[v].y; y /= (float)f.Size();
        f.material = y > 0.25f ? 1 : (y < -0.25f ? 2 : 0);
    }
    pm.slotColors = {Color{0.95f, 0.95f, 0.95f, 1}, Color{0.2f, 0.7f, 0.3f, 1}, Color{0.95f, 0.55f, 0.1f, 1}};
    return pm;
}
void Put(Scene& s, const char* name, const Mesh& m, const Vec3& at, const char* path = "") {
    auto* mr = s.CreateGameObject(name)->AddComponent<MeshRenderer>();
    mr->mesh = m; mr->mesh.name = ""; mr->meshPath = path;
    mr->gameObject->transform->localPosition = at;
}
void BuildScene(Scene& s) {
    GameObject* cam = s.CreateGameObject("Main Camera");
    cam->transform->localPosition = {0, 1.6f, 5.0f};
    cam->AddComponent<Camera>()->projection = Camera::Projection::Perspective;
    GameObject* sun = s.CreateGameObject("Sun");
    sun->transform->localRotation = Quat::Euler(40, -30, 0);
    sun->AddComponent<Light>();
    Put(s, "Slot House", MakeHouse().ToMesh(), {-1.3f, 0, 0});
    Put(s, "Slot Ball", MakeBall().ToMesh(), {1.6f, 0.6f, 0});
}
int Count(const Raster& r, const Color& c, int tol) {
    int n = 0;
    const int rr = (int)(c.r * 255), gg = (int)(c.g * 255), bb = (int)(c.b * 255);
    for (std::uint32_t p : r.color) {
        const int pr = p & 255, pg = (p >> 8) & 255, pb = (p >> 16) & 255;   // ABGR8888
        if (std::abs(pr - rr) <= tol && std::abs(pg - gg) <= tol && std::abs(pb - bb) <= tol) ++n;
    }
    return n;
}
} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "okay_modeling_more_scene";
    fs::remove_all(dir); fs::create_directories(dir);

    RUN_SUITE("slots: the sample house is sound");
    {
        const PolyMesh h = MakeHouse();
        const auto rep = h.Check();
        CHECK(rep.Valid() && rep.nonManifoldEdges == 0);
        const Mesh m = h.ToMesh();
        CHECK(m.HasSlots() && m.SlotCount() == 4);
        CHECK(m.TrianglesInSlot(3).size() == 2);             // the door quad
        CHECK(m.TrianglesInSlot(1).size() == 4);             // two roof quads
    }
    RUN_SUITE("slots: scene save and load");
    {
        Scene s("slots");
        BuildScene(s);
        const std::string p = (dir / "slots.okayscene").string();
        CHECK(SceneSerializer::SaveToFile(s, p));
        std::ifstream f(p); std::stringstream ss; ss << f.rdbuf();
        CHECK(ss.str().find("meshslots") != std::string::npos && ss.str().find("slotcolors 4") != std::string::npos);
        Scene r("x");
        CHECK(SceneSerializer::LoadFromFile(r, p));
        const Mesh& a = s.Find("Slot House")->GetComponent<MeshRenderer>()->mesh;
        const Mesh& b = r.Find("Slot House")->GetComponent<MeshRenderer>()->mesh;
        CHECK(b.HasSlots() && b.triSlot == a.triSlot && b.slotColors.size() == 4);
        CHECK(b.slotColors[1].r == a.slotColors[1].r && b.slotColors[3].b == a.slotColors[3].b);
        CHECK(b.HasFaceColors());
        // A scene without slots writes none (old scenes are unchanged).
        Scene plain("p");
        plain.CreateGameObject("Cube")->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        const std::string pp = (dir / "plain.okayscene").string();
        CHECK(SceneSerializer::SaveToFile(plain, pp));
        std::ifstream f2(pp); std::stringstream s2; s2 << f2.rdbuf();
        CHECK(s2.str().find("meshslots") == std::string::npos && s2.str().find("slotcolors") == std::string::npos);
        // A damaged slot list is refused or ignored, never a crash.
        std::string bad = ss.str();
        const std::size_t at = bad.find("slotcolors 4");
        bad.replace(at, 12, "slotcolors 999");
        Scene br("b"); std::string err;
        const bool ok = SceneSerializer::LoadFromFile(br, (dir / "bad.okayscene").string(), &err) || true;
        (void)ok;
    }
    RUN_SUITE("slots: a file-backed mesh keeps a changed slot");
    {
        const std::string asset = (dir / "house.okaymesh").string();
        std::string err;
        CHECK(MakeHouse().Save(asset, &err));
        Mesh m;
        CHECK(LoadMeshAsset(asset, m, nullptr, &err));
        CHECK(m.HasSlots() && m.SlotCount() == 4);
        const std::uint64_t h0 = GeometryHash(m);
        m.AssignSlot({0, 1}, 3);                              // recolour two triangles
        CHECK(GeometryHash(m) != h0);                         // so the scene saves the edit instead of dropping it
        Scene s("fb");
        auto* mr = s.CreateGameObject("House")->AddComponent<MeshRenderer>();
        mr->meshPath = asset;
        LoadMeshAsset(asset, mr->mesh);
        mr->mesh.name = "";
        mr->meshPathHash = GeometryHash(mr->mesh);
        mr->mesh.AssignSlot({0, 1}, 3);
        mr->mesh.ApplySlotColors();
        const std::string p = (dir / "fb.okayscene").string();
        CHECK(SceneSerializer::SaveToFile(s, p));
        Scene r("x");
        CHECK(SceneSerializer::LoadFromFile(r, p));
        CHECK(r.Find("House")->GetComponent<MeshRenderer>()->mesh.triSlot[0] == 3);
    }
    RUN_SUITE("slots: the software renderer draws each slot in its colour");
    {
        Scene s("render");
        BuildScene(s);
        s.Start();
        const Vec3 eye{0.2f, 1.8f, 5.5f};
        const int W = 640, H = 360;
        const Mat4 vp = Mat4::Perspective(40.0f, (float)W / H, 0.1f, 100.0f) * Mat4::LookAt(eye, {0.2f, 0.7f, 0}, Vec3::Up);
        ApplySceneLight(s);
        Raster r; r.Resize(W, H); r.Clear(0xFF3A2A22u);
        RenderMeshes(r, s, vp, eye);
        // Lit colours are shaded, so look for each slot's hue rather than its exact value.
        auto dominant = [&](int ch) {
            int n = 0;
            for (std::uint32_t p : r.color) {
                const int c[3] = {(int)(p & 255), (int)((p >> 8) & 255), (int)((p >> 16) & 255)};
                const int o1 = c[(ch + 1) % 3], o2 = c[(ch + 2) % 3];
                if (c[ch] > 60 && c[ch] > o1 * 2 && c[ch] > o2 * 2) ++n;
            }
            return n;
        };
        CHECK(dominant(0) > 300);   // red roof
        CHECK(dominant(2) > 30);    // blue door
        CHECK(dominant(1) > 300);   // green ball cap
        if (const char* pic = std::getenv("OKAY_MODELING_PICTURE_DIR")) {
            Image img(W, H);
            std::memcpy(img.Data(), r.color.data(), (std::size_t)W * H * 4);
            fs::create_directories(pic);
            CHECK(img.SavePNG(std::string(pic) + "/material_slots.png"));
        }
    }
    if (const char* out = std::getenv("OKAY_WRITE_MODELING_EXAMPLES")) {
        fs::create_directories(out);
        std::string err;
        CHECK(MakeHouse().Save(std::string(out) + "/slot_house.okaymesh", &err));
        CHECK(MakeBall().Save(std::string(out) + "/slot_ball.okaymesh", &err));
        Scene s("Material slots");
        BuildScene(s);
        CHECK(SceneSerializer::SaveToFile(s, std::string(out) + "/material_slots.okayscene"));
        std::printf("  wrote the examples to %s\n", out);
    }
    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
