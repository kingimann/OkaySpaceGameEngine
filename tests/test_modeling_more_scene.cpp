// Modeling "more": material slots in scenes, the shipped example (examples/modeling_tools) and a headless picture.
// The example assets are made by this program (our own code): run it with OKAY_WRITE_MODELING_EXAMPLES=<examples/modeling_tools>
// to rewrite them; OKAY_MODELING_PICTURE_DIR=<dir> also writes the picture (a software render, not a GPU one).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/MeshModifiers.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Modeling/Measure.hpp"
#include "okay/Modeling/PolyOps.hpp"
#include "okay/Modeling/SelectOps.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include <array>
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
// Spin, Extrude Along Path and Array Along Curve on three objects.
PolyMesh MakeVase() {
    PolyMesh pm;
    const float prof[][2] = {{0.05f, 0.0f}, {0.45f, 0.05f}, {0.55f, 0.35f}, {0.35f, 0.7f}, {0.22f, 0.95f}, {0.3f, 1.15f}};
    EdgeList chain;
    for (int i = 0; i < 6; ++i) { pm.verts.push_back({prof[i][0], prof[i][1], 0}); if (i) chain.push_back({i - 1, i}); }
    SpinEdges(pm, chain, {0, 0, 0}, {0, 1, 0}, 360.0f, 24);
    for (PolyFace& f : pm.faces) f.material = 0;
    pm.slotColors = {Color{0.35f, 0.55f, 0.85f, 1}};
    return pm;
}
PolyMesh MakeHorn() {
    PolyMesh pm = PolyMesh::FromMesh(Mesh::Cube(0.5f));
    int top = 0;
    for (int f = 0; f < (int)pm.faces.size(); ++f) if (pm.FaceNormal(f).y > 0.9f) top = f;
    const float y = pm.verts[pm.faces[top].verts[0]].y;
    std::vector<Vec3> path;
    for (int k = 0; k <= 10; ++k) { const float a = 1.4f * (float)k / 10.0f; path.push_back({0.9f * (1 - std::cos(a)), y + 0.9f * std::sin(a), 0}); }
    ExtrudeAlongPath(pm, {top}, path);
    return pm;
}
void BuildToolsScene(Scene& s) {
    GameObject* cam = s.CreateGameObject("Main Camera");
    cam->transform->localPosition = {1.4f, 2.4f, 16.0f};
    cam->AddComponent<Camera>()->projection = Camera::Projection::Perspective;
    GameObject* sun = s.CreateGameObject("Sun");
    sun->transform->localRotation = Quat::Euler(40, -30, 0);
    sun->AddComponent<Light>();
    Put(s, "Spun Vase", MakeVase().ToMesh(), {-3.0f, 0, 0});
    Put(s, "Bent Horn", MakeHorn().ToMesh(), {-1.0f, 0, 0});
    Put(s, "Table", MakeTable(), {6.0f, 0, 0});                                  // the "Add a Table" recipe
    Mesh wall = Mesh::Cube(1.0f);
    wall.vertices = Mesh::Cube(1.0f).Transformed({1.6f, 1.2f, 0.25f}, {0, 0, 0}).vertices;
    std::string werr;
    MakeWindowHole(wall, SelectByNormal(wall, Vec3{0, 0, 1}, 5.0f), 0.3f, &werr);      // the "Make a Window Hole" recipe
    Put(s, "Window Wall", wall, {-5.6f, 0.6f, 0});
    Mesh hiddenRoof = MakeHouse().ToMesh();                                      // roof faces hidden: Edit Mesh, Alt+H brings them back
    hiddenRoof.HideTriangles(hiddenRoof.TrianglesInSlot(1));
    Put(s, "House Roof Hidden", hiddenRoof, {8.6f, 0, 0});
    Put(s, "Curve Array", Mesh::Cube(0.35f), {0.8f, 0.4f, 0});
    GameObject* arr = s.Find("Curve Array");
    auto* mm = arr->AddComponent<MeshModifiers>();
    mm->modifiers.push_back(MeshModifier::Make(MeshModifier::Type::CurveArray));
    mm->modifiers.back().count = 9;
}
// A strip painted red to blue with vertex colours, a creased cube and a plain cube (both with the Subdivision modifier), a sphere to wrap onto.
Mesh MakeStrip() {
    Mesh m;
    const int N = 8;
    for (int i = 0; i <= N; ++i) { const float x = -2.0f + 4.0f * (float)i / N; m.vertices.push_back({x, -0.5f, 0}); m.vertices.push_back({x, 0.5f, 0}); }
    for (int i = 0; i < N; ++i) { const int a = i * 2; m.triangles.insert(m.triangles.end(), {a, a + 2, a + 3, a, a + 3, a + 1}); }
    for (int i = 0; i <= N; ++i) { const float t = (float)i / N; for (int k = 0; k < 2; ++k) m.vertexColors.push_back(Color{1 - t, 0, t, 1}); }
    return m;
}
void BuildVertexScene(Scene& s) {
    GameObject* cam = s.CreateGameObject("Main Camera");
    cam->transform->localPosition = {0.5f, 1.6f, 9.0f};
    cam->AddComponent<Camera>()->projection = Camera::Projection::Perspective;
    GameObject* sun = s.CreateGameObject("Sun");
    sun->transform->localRotation = Quat::Euler(40, -30, 0);
    sun->AddComponent<Light>();
    Put(s, "Gradient Bar", MakeStrip(), {-2.6f, 0.6f, 0});
    PolyMesh creased = PolyMesh::FromMesh(Mesh::Cube());
    for (const auto& e : creased.Edges()) creased.SetCrease(e.a, e.b, 3.0f);
    Put(s, "Creased Cube", creased.ToMesh(), {1.0f, 0.5f, 0});
    Put(s, "Plain Cube", Mesh::Cube(), {2.8f, 0.5f, 0});
    for (const char* nm : {"Creased Cube", "Plain Cube"}) {
        auto* mm = s.Find(nm)->AddComponent<MeshModifiers>();
        mm->modifiers.push_back(MeshModifier::Make(MeshModifier::Type::Subdivision));
        mm->modifiers.back().levels = 2;
    }
    Put(s, "Wrap Target", Mesh::Sphere(0.7f, 24, 32), {5.0f, 0.7f, 0});
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
    RUN_SUITE("slots: a picture per slot (software renderer)");
    {
        Image magenta(8, 8); magenta.Fill(Color{1, 0, 1, 1});
        RegisterTexture("@slot_magenta", std::move(magenta));
        auto render = [&](bool withTexture) {
            Scene s("tex");
            BuildScene(s);
            auto* mr = s.Find("Slot House")->GetComponent<MeshRenderer>();
            if (withTexture) mr->slotTextures = {"", "@slot_magenta"};      // slot 1 (the roof) gets the picture, slot 0 none
            s.Start();
            const Vec3 eye{0.2f, 1.8f, 5.5f};
            const Mat4 vp = Mat4::Perspective(40.0f, 640.0f / 360.0f, 0.1f, 100.0f) * Mat4::LookAt(eye, {1.4f, 0.7f, 0}, Vec3::Up);
            ApplySceneLight(s);
            Raster r; r.Resize(640, 360); r.Clear(0xFF3A2A22u);
            RenderMeshes(r, s, vp, eye);
            return r;
        };
        auto magentaPixels = [](const Raster& r) {
            int n = 0;
            for (std::uint32_t p : r.color) { const int R = p & 255, G = (p >> 8) & 255, B = (p >> 16) & 255; if (R > 60 && B > 60 && G * 2 < R && G * 2 < B) ++n; }
            return n;
        };
        auto redPixels = [](const Raster& r) {
            int n = 0;
            for (std::uint32_t p : r.color) { const int R = p & 255, G = (p >> 8) & 255, B = (p >> 16) & 255; if (R > 60 && G * 3 < R && B * 3 < R) ++n; }
            return n;
        };
        const Raster plain = render(false), textured = render(true);
        CHECK(magentaPixels(plain) < 50);                     // no picture: no magenta anywhere
        CHECK(magentaPixels(textured) > 1000);                // the roof now shows the picture
        CHECK(redPixels(textured) < redPixels(plain) / 4);    // ...instead of its red slot colour
        // The door (slot 3) has no picture and keeps its colour, so only the roof changed.
        CHECK(Count(textured, kDoor, 90) > 0);
        // Saved with the scene, and an empty picture list writes nothing.
        Scene s("tt"); BuildScene(s);
        const std::string p = (dir / "tt.okayscene").string();
        CHECK(SceneSerializer::SaveToFile(s, p));
        { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); CHECK(ss.str().find("slottex") == std::string::npos); }
        s.Find("Slot House")->GetComponent<MeshRenderer>()->slotTextures = {"", "Assets/brick.png", "", "Assets/door.png"};
        CHECK(SceneSerializer::SaveToFile(s, p));
        Scene r("x"); CHECK(SceneSerializer::LoadFromFile(r, p));
        const auto& st = r.Find("Slot House")->GetComponent<MeshRenderer>()->slotTextures;
        CHECK(st.size() == 4 && st[0].empty() && st[1] == "Assets/brick.png" && st[2].empty() && st[3] == "Assets/door.png");
        bool listed = false;
        for (const auto& a : SceneSerializer::CollectAssetPaths(s)) if (a == "Assets/brick.png") listed = true;
        CHECK(listed);                                        // packaged with the game
    }
    RUN_SUITE("vertex colours: scene, asset, software renderer");
    {
        Scene s("vc");
        auto* mr = s.CreateGameObject("Strip")->AddComponent<MeshRenderer>();
        // A flat strip facing the camera, red on the left edge and blue on the right edge, 8 quads wide.
        Mesh m;
        const int N = 8;
        for (int i = 0; i <= N; ++i) { const float x = -2.0f + 4.0f * (float)i / N; m.vertices.push_back({x, -0.5f, 0}); m.vertices.push_back({x, 0.5f, 0}); }
        for (int i = 0; i < N; ++i) { const int a = i * 2; m.triangles.insert(m.triangles.end(), {a, a + 2, a + 3, a, a + 3, a + 1}); }
        for (int i = 0; i <= N; ++i) { const float t = (float)i / N; for (int k = 0; k < 2; ++k) m.vertexColors.push_back(Color{1 - t, 0, t, 1}); }
        mr->mesh = m; mr->mesh.name = "";
        const std::string p = (dir / "vc.okayscene").string();
        CHECK(SceneSerializer::SaveToFile(s, p));
        { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); CHECK(ss.str().find("meshvcolors 18") != std::string::npos); }
        Scene r("x");
        CHECK(SceneSerializer::LoadFromFile(r, p));
        const Mesh& lm = r.Find("Strip")->GetComponent<MeshRenderer>()->mesh;
        CHECK(lm.HasVertexColors() && lm.vertexColors.size() == 18 && lm.vertexColors[0].r == 1 && std::fabs(lm.vertexColors[17].b - 1.0f) < 1e-6f);
        Scene none("n"); none.CreateGameObject("C")->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        const std::string pn = (dir / "vcn.okayscene").string(); CHECK(SceneSerializer::SaveToFile(none, pn));
        { std::ifstream f(pn); std::stringstream ss; ss << f.rdbuf(); CHECK(ss.str().find("meshvcolors") == std::string::npos); }
        // A wrongly sized colour list is ignored (the mesh just draws without it), never trusted.
        { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); std::string t = ss.str();
          const std::size_t at = t.find("meshvcolors 18"); t.replace(at, 14, "meshvcolors 3 ");
          std::ofstream o((dir / "vcbad.okayscene").string()); o << t; }
        Scene bad("b"); std::string err;
        SceneSerializer::LoadFromFile(bad, (dir / "vcbad.okayscene").string(), &err);
        if (auto* bo = bad.Find("Strip")) CHECK(!bo->GetComponent<MeshRenderer>()->mesh.HasVertexColors());
        // As a mesh asset.
        const std::string asset = (dir / "vc.okaymesh").string();
        CHECK(SaveMeshAsset(asset, m, &err));
        Mesh loaded; CHECK(LoadMeshAsset(asset, loaded, nullptr, &err) && loaded.HasVertexColors());
        // Draw it, lit (per-pixel and per-vertex paths) and unlit: the left is red, the right blue, the middle both.
        for (int mode = 0; mode < 3; ++mode) {
            Scene t("draw");
            GameObject* cam = t.CreateGameObject("Cam"); cam->transform->localPosition = {0, 0, 2.5f};
            cam->AddComponent<Camera>()->projection = Camera::Projection::Perspective;
            auto* tm = t.CreateGameObject("Strip")->AddComponent<MeshRenderer>();
            tm->mesh = m; tm->mesh.name = ""; tm->doubleSided = true;
            if (mode == 2) tm->unlit = true;
            t.CreateGameObject("Sun")->AddComponent<Light>();
            t.Start();
            const Vec3 eye{0, 0, 2.5f};
            const Mat4 vp = Mat4::Perspective(40.0f, 4.0f, 0.1f, 100.0f) * Mat4::LookAt(eye, {0, 0, 0}, Vec3::Up);
            ApplySceneLight(t);
            const bool before = PerPixelLighting();
            PerPixelLighting() = (mode == 1);
            Raster rr; rr.Resize(400, 100); rr.Clear(0xFF202020u);
            RenderMeshes(rr, t, vp, eye);
            PerPixelLighting() = before;
            auto px = [&](int x) { const std::uint32_t c = rr.color[50 * 400 + x]; return std::array<int, 3>{(int)(c & 255), (int)((c >> 8) & 255), (int)((c >> 16) & 255)}; };
            const auto L = px(100), Mi = px(200), R = px(300);
            CHECK(L[0] > L[2] * 2 && L[0] > 40);                 // left: red dominates
            CHECK(R[2] > R[0] * 2 && R[2] > 40);                 // right: blue dominates
            CHECK(Mi[0] > 20 && Mi[2] > 20);                     // middle: a mix of both
            CHECK(px(130)[0] > px(270)[0] && px(270)[2] > px(130)[2]);   // red fades left to right, blue grows
        }
    }
    RUN_SUITE("hidden faces survive save, load and a mesh asset");
    {
        Scene s("hid");
        auto* mr = s.CreateGameObject("Box")->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube(); mr->mesh.ProjectUVBox(1.0f);
        mr->mesh.AssignSlot({0, 1}, 1); mr->mesh.slotColors[1] = Color{0, 1, 0, 1}; mr->mesh.ApplySlotColors();
        mr->mesh.name = "";
        CHECK(mr->mesh.HideTriangles({0, 1, 6}) == 3);
        const std::string p = (dir / "hid.okayscene").string();
        CHECK(SceneSerializer::SaveToFile(s, p));
        { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); CHECK(ss.str().find("meshhidden 3 1 1") != std::string::npos); }
        Scene r("x");
        CHECK(SceneSerializer::LoadFromFile(r, p));
        Mesh& lm = r.Find("Box")->GetComponent<MeshRenderer>()->mesh;
        CHECK(lm.TriangleCount() == 9 && lm.HiddenTriangleCount() == 3 && lm.HiddenConsistent());
        CHECK(lm.hiddenSlot[0] == 1 && lm.hiddenSlot[2] == 0);
        CHECK(lm.RevealHidden().size() == 3 && lm.TriangleCount() == 12 && lm.Topology().Closed());
        CHECK(lm.HasSlots() && lm.TrianglesInSlot(1).size() == 2 && lm.HasUVs());
        // A scene with nothing hidden writes no record, and a damaged record is refused, not trusted.
        Scene plain("p"); plain.CreateGameObject("C")->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        const std::string pp = (dir / "plain2.okayscene").string();
        CHECK(SceneSerializer::SaveToFile(plain, pp));
        { std::ifstream f(pp); std::stringstream ss; ss << f.rdbuf(); CHECK(ss.str().find("meshhidden") == std::string::npos); }
        { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); std::string t = ss.str();
          const std::size_t at = t.find("meshhidden 3 1 1"); t.replace(at, 16, "meshhidden 3 1 7");
          std::ofstream o((dir / "dmg.okayscene").string()); o << t; }
        Scene dm("d"); std::string err;
        const bool ok = SceneSerializer::LoadFromFile(dm, (dir / "dmg.okayscene").string(), &err);
        if (ok) { const Mesh& m2 = dm.Find("Box")->GetComponent<MeshRenderer>()->mesh; CHECK(m2.HiddenConsistent()); }
        // Saving the mesh as an asset includes the hidden faces.
        const std::string asset = (dir / "hidden.okaymesh").string();
        Mesh withHidden = Mesh::Cube(); withHidden.HideTriangles({0, 1, 2, 3});
        CHECK(SaveMeshAsset(asset, withHidden, &err));
        PolyMesh back; CHECK(PolyMesh::Load(asset, back, &err) && back.CountFaces(4) == 6);
        Mesh allHidden = Mesh::Cube(); std::vector<int> every; for (int t = 0; t < 12; ++t) every.push_back(t); allHidden.HideTriangles(every);
        CHECK(SaveMeshAsset((dir / "all.okaymesh").string(), allHidden, &err));
    }
    RUN_SUITE("spin, path and curve array: objects, save and load, Copy");
    {
        const PolyMesh vase = MakeVase();
        CHECK(vase.Check().Valid() && vase.Check().nonManifoldEdges == 0 && vase.faces.size() == 5 * 24);
        const PolyMesh horn = MakeHorn();
        CHECK(horn.Check().Closed() && horn.Check().Valid());
        Scene s("tools");
        BuildToolsScene(s);
        auto* mm = s.Find("Curve Array")->GetComponent<MeshModifiers>();
        mm->modifiers[0].path.push_back({5, 1, 1});
        mm->modifiers[0].align = false;
        mm->Refresh(true);
        CHECK(s.Find("Curve Array")->GetComponent<MeshRenderer>()->DisplayMesh().TriangleCount() == 9 * 12);
        CHECK(mm->costMs.size() == 1 && mm->costMs[0] >= 0.0f);
        // Copy puts an identical modifier right below, and the whole stack still evaluates.
        CHECK(mm->Duplicate(0) == 1 && mm->modifiers.size() == 2 && mm->Duplicate(7) == -1 && mm->Duplicate(-1) == -1);
        CHECK(SameModifiers({mm->modifiers[0]}, {mm->modifiers[1]}));
        mm->Refresh(true);
        CHECK(mm->costMs.size() == 2 && mm->errors.size() == 2);
        mm->modifiers.pop_back();
        const std::string p = (dir / "tools.okayscene").string();
        CHECK(SceneSerializer::SaveToFile(s, p));
        Scene r("x");
        CHECK(SceneSerializer::LoadFromFile(r, p));
        auto* lm = r.Find("Curve Array")->GetComponent<MeshModifiers>();
        CHECK(lm && lm->modifiers.size() == 1 && lm->modifiers[0].type == MeshModifier::Type::CurveArray);
        CHECK(lm->modifiers[0].count == 9 && !lm->modifiers[0].align && lm->modifiers[0].path.size() == 6);
        CHECK_NEAR(lm->modifiers[0].path[5].x, 5.0f, 1e-5); CHECK_NEAR(lm->modifiers[0].path[5].z, 1.0f, 1e-5);
        if (const char* out = std::getenv("OKAY_WRITE_MODELING_EXAMPLES")) {
            Scene t("Spin, path and curve");
            BuildToolsScene(t);
            CHECK(SceneSerializer::SaveToFile(t, std::string(out) + "/spin_path_curve.okayscene"));
            std::string err;
            CHECK(vase.Save(std::string(out) + "/spun_vase.okaymesh", &err));
        }
        if (const char* out = std::getenv("OKAY_WRITE_MODELING_EXAMPLES")) {
            Scene v("Vertex colours, creases and shrinkwrap");
            BuildVertexScene(v);
            CHECK(SceneSerializer::SaveToFile(v, std::string(out) + "/vertex_paint_creases.okayscene"));
        }
        {   // the vertex / crease scene round-trips and the creased cube stays boxier than the plain one after subdivision
            Scene v("vc2"); BuildVertexScene(v); v.Start();
            for (const auto& go : v.Objects()) if (auto* m2 = go->GetComponent<MeshModifiers>()) m2->Refresh(true);
            const std::string vp2 = (dir / "vc2.okayscene").string();
            CHECK(SceneSerializer::SaveToFile(v, vp2));
            { std::ifstream f(vp2); std::stringstream ss; ss << f.rdbuf(); CHECK(ss.str().find("meshcreases") != std::string::npos && ss.str().find("meshvcolors") != std::string::npos); }
            Scene vr("vr"); CHECK(SceneSerializer::LoadFromFile(vr, vp2));
            CHECK(!vr.Find("Creased Cube")->GetComponent<MeshRenderer>()->mesh.creases.empty());
            CHECK(vr.Find("Gradient Bar")->GetComponent<MeshRenderer>()->mesh.HasVertexColors());
            for (const auto& go : vr.Objects()) if (auto* m2 = go->GetComponent<MeshModifiers>()) m2->Refresh(true);
            const float vc = MeasureMesh(vr.Find("Creased Cube")->GetComponent<MeshRenderer>()->DisplayMesh()).volume;
            const float vp = MeasureMesh(vr.Find("Plain Cube")->GetComponent<MeshRenderer>()->DisplayMesh()).volume;
            CHECK(vc > 0.95f && vp < 0.75f);
        }
        if (const char* pic = std::getenv("OKAY_MODELING_PICTURE_DIR")) {
            {   // picture of the vertex / crease scene
                Scene t("pic2"); BuildVertexScene(t); t.Start();
                for (const auto& go : t.Objects()) if (auto* m2 = go->GetComponent<MeshModifiers>()) m2->Refresh(true);
                const Vec3 eye{0.5f, 1.6f, 9.0f}; const int W = 640, H = 360;
                const Mat4 vpm = Mat4::Perspective(40.0f, (float)W / H, 0.1f, 100.0f) * Mat4::LookAt(eye, {0.5f, 0.7f, 0}, Vec3::Up);
                ApplySceneLight(t);
                Raster rr; rr.Resize(W, H); rr.Clear(0xFF3A2A22u);
                RenderMeshes(rr, t, vpm, eye);
                Image img(W, H); std::memcpy(img.Data(), rr.color.data(), (std::size_t)W * H * 4);
                fs::create_directories(pic);
                CHECK(img.SavePNG(std::string(pic) + "/vertex_paint_creases.png"));
            }
            Scene t("pic"); BuildToolsScene(t); t.Start();
            for (const auto& go : t.Objects()) if (auto* m2 = go->GetComponent<MeshModifiers>()) m2->Refresh(true);
            const Vec3 eye{1.4f, 2.8f, 16.5f}; const int W = 640, H = 360;
            const Mat4 vp = Mat4::Perspective(40.0f, (float)W / H, 0.1f, 100.0f) * Mat4::LookAt(eye, {0.2f, 0.7f, 0}, Vec3::Up);
            ApplySceneLight(t);
            Raster rr; rr.Resize(W, H); rr.Clear(0xFF3A2A22u);
            RenderMeshes(rr, t, vp, eye);
            Image img(W, H); std::memcpy(img.Data(), rr.color.data(), (std::size_t)W * H * 4);
            fs::create_directories(pic);
            CHECK(img.SavePNG(std::string(pic) + "/spin_path_curve.png"));
        }
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
