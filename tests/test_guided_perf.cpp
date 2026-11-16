#include "test_framework.hpp"
#include "okay/Modeling/Guided.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Modeling/PolyMesh.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::modeling;
namespace gd = okay::modeling::guided;

// Guided modeling: why the panel lagged, and the checks that keep it fixed. Each check below failed before the fix
// (docs/guided_perf.md has the numbers). Set OKAY_GUIDED_PERF=1 to print the timing table for every template at
// Low, High and Ultra: time per build, per commit (what a released slider costs), and per frame.
namespace {
double Ms(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
const PolyMesh::FromMeshOptions kPanel{false, true, 1.0f};   // how the panel reads a model's mesh back

gd::Recipe RecipeAt(const gd::Template& t, float detail) {
    gd::Recipe r = gd::DefaultRecipe(t);
    r.params[gd::kDetailParam] = detail;
    return r;
}
// The mesh the panel holds: built, triangulated, smooth-shaded from High (SetMesh in editor/GuidedPanel.cpp).
Mesh PanelMesh(const PolyMesh& pm, float detail) {
    Mesh m = pm.ToMesh();
    if (detail >= 1.5f) m.ComputeAutoSmoothNormals(40.0f);
    return m;
}
}

// 1. A model nobody touched must read back as untouched at every detail level. Before: at High and Ultra the smooth-shading
//    pass split the corners, the polygons could not be rebuilt, and 27 of the templates' parts showed as "edited by you"
//    (so every setting change asked "Parts you edited by hand").
static void TestUntouchedAfterSmoothing() {
    RUN_SUITE("guided perf: an untouched model stays untouched after smoothing (all templates, Low/Medium/High/Ultra)");
    for (const gd::Template& t : gd::Registry::Instance().All()) {
        for (float lv : {0.0f, 1.0f, 2.0f, 3.0f}) {
            const gd::Recipe r = RecipeAt(t, lv);
            const PolyMesh built = gd::Create(r);
            const PolyMesh cur = PolyMesh::FromMesh(PanelMesh(built, lv), kPanel);
            const gd::Analysis a = gd::Analyze(cur, r);
            const bool untouched = gd::IsUntouched(a);
            if (!untouched) std::fprintf(stderr, "  %s at detail %g: %d edited, %d removed, %d own faces\n", t.id.c_str(), lv, a.Count(gd::PartState::Edited), a.Count(gd::PartState::Deleted), a.customFaces);
            CHECK(untouched);
            gd::Recipe next = r;
            for (const auto& p : t.params) if (p.kind == gd::ParamDef::Kind::Float && p.id != gd::kDetailParam) { next.params[p.id] = p.def + (p.max - p.min) * 0.1; break; }
            const gd::RegenResult rr = gd::Regenerate(cur, r, next);
            CHECK(rr.ok);
            CHECK(!rr.needsDecision);
        }
    }
}

// 2. Smooth shading must not move vertices. Before: corners within 1/4096 of each other were treated as one point and took
//    the first one's position, which squeezed thin parts (the car's 0.0002-wide wiper blades) flat.
static void TestSmoothingKeepsPositions() {
    RUN_SUITE("guided perf: smooth shading keeps every vertex where it was");
    PolyMesh pm;
    pm.verts = {{0, 0, 0}, {0.0001f, 0, 0}, {0.0001f, 1, 0}, {0, 1, 0}};
    PolyFace f; f.verts = {0, 1, 2, 3}; pm.faces.push_back(f);
    Mesh m = pm.ToMesh();
    std::set<std::tuple<float, float, float>> before;
    for (const Vec3& v : m.vertices) before.insert({v.x, v.y, v.z});
    m.ComputeAutoSmoothNormals(40.0f);
    std::set<std::tuple<float, float, float>> after;
    for (const Vec3& v : m.vertices) after.insert({v.x, v.y, v.z});
    CHECK(before == after);
    CHECK(after.size() == 4);
    // and the real model: the car at Ultra keeps all of its wiper blade faces as quads
    const gd::Recipe r = RecipeAt(*gd::Registry::Instance().Find("car"), 3.0f);
    const PolyMesh built = gd::Create(r);
    const PolyMesh cur = PolyMesh::FromMesh(PanelMesh(built, 3.0f), kPanel);
    CHECK(cur.faces.size() == built.faces.size());
}

// 3. A model saved to its .okaymesh file and opened again is still untouched.
static void TestSaveReopen() {
    RUN_SUITE("guided perf: save, reopen, still untouched");
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "okay_guided_perf";
    std::filesystem::create_directories(dir);
    for (const gd::Template& t : gd::Registry::Instance().All()) {
        for (float lv : {1.0f, 3.0f}) {
            const gd::Recipe r = RecipeAt(t, lv);
            const Mesh m = PanelMesh(gd::Create(r), lv);
            const std::string path = (dir / (t.id + ".okaymesh")).string();
            std::string err;
            CHECK(SaveMeshAsset(path, m, &err));
            Mesh back;
            CHECK(LoadMeshAsset(path, back, nullptr, &err));
            const gd::Analysis a = gd::Analyze(PolyMesh::FromMesh(back, kPanel), r);
            if (!gd::IsUntouched(a)) std::fprintf(stderr, "  %s at detail %g reopened: %d edited, %d removed, %d own faces\n", t.id.c_str(), lv, a.Count(gd::PartState::Edited), a.Count(gd::PartState::Deleted), a.customFaces);
            CHECK(gd::IsUntouched(a));
        }
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// 4. The same recipe is built once: Create, the analysis, a regenerate and the validity check share the parts.
//    Before: every one of them built the model again (about five builds per released slider).
static void TestBuildCache() {
    RUN_SUITE("guided perf: parts are built once per recipe");
    const gd::Template& t = *gd::Registry::Instance().Find("car");
    gd::Recipe r = RecipeAt(t, 3.0f);
    r.params["length"] = r.Param("length", 4) + 0.05;   // a recipe nobody built yet
    gd::ClearCache();
    const long m0 = gd::Stats().misses;
    const PolyMesh a = gd::Create(r);
    CHECK(gd::Stats().misses == m0 + 1);
    const auto t0 = std::chrono::steady_clock::now();
    const PolyMesh b = gd::Create(r);
    const std::vector<gd::GeneratedPart> parts = gd::Generate(r);
    gd::Validate(parts, false);
    gd::Analyze(PolyMesh::FromMesh(PanelMesh(a, 3.0f), kPanel), r);
    (void)Ms(t0);
    CHECK(gd::Stats().misses == m0 + 1);   // nothing was built again
    CHECK(a.faces.size() == b.faces.size());
    CHECK(a.verts.size() == b.verts.size());
    // a changed recipe is a different build, and a changed template does not answer from the old one
    gd::Recipe r2 = r; r2.params["length"] = r.Param("length", 4) + 0.1;
    gd::Create(r2);
    CHECK(gd::Stats().misses == m0 + 2);
    gd::Template tt = t;
    gd::Registry::Instance().Register(tt);
    gd::Create(r2);
    CHECK(gd::Stats().misses == m0 + 3);
}

// 5. For an untouched model the editor skips reading the mesh back; the answer must be the same as the full regenerate.
static void TestUntouchedFastPath() {
    RUN_SUITE("guided perf: RegenerateUntouched gives what Regenerate gives");
    for (const gd::Template& t : gd::Registry::Instance().All()) {
        for (float lv : {1.0f, 3.0f}) {
            const gd::Recipe r = RecipeAt(t, lv);
            gd::Recipe next = r;
            for (const auto& p : t.params) if (p.kind == gd::ParamDef::Kind::Float && p.id != gd::kDetailParam) { next.params[p.id] = p.def + (p.max - p.min) * 0.2; break; }
            const PolyMesh cur = PolyMesh::FromMesh(PanelMesh(gd::Create(r), lv), kPanel);
            const gd::RegenResult full = gd::Regenerate(cur, r, next);
            const gd::RegenResult fast = gd::RegenerateUntouched(r, next);
            CHECK(full.ok && fast.ok);
            CHECK(full.mesh.faces.size() == fast.mesh.faces.size());
            CHECK(full.mesh.verts.size() == fast.mesh.verts.size());
            CHECK(full.recipe.Serialize() == fast.recipe.Serialize());
            CHECK(full.regenerated == fast.regenerated);
            // no stray loose vertices after a rebuild (smooth shading leaves unused copies of corner vertices behind)
            std::vector<char> used(full.mesh.verts.size(), 0);
            for (const PolyFace& f : full.mesh.faces) for (int v : f.verts) used[v] = 1;
            int loose = 0;
            for (char u : used) if (!u) ++loose;
            CHECK(loose == 0);
            // the analysis of the new mesh is known without running it
            const gd::Analysis ua = gd::UntouchedAnalysis(full.recipe);
            const gd::Analysis ra = gd::Analyze(full.mesh, full.recipe);
            CHECK(ua.ok && ra.ok);
            CHECK(ua.parts.size() == ra.parts.size());
            CHECK(ua.faceOwner == ra.faceOwner);
            CHECK(ua.customFaces == ra.customFaces);
            if (full.mesh.verts.size() == fast.mesh.verts.size()) {
                bool same = true;
                for (size_t i = 0; i < full.mesh.verts.size(); ++i) { const Vec3 d = full.mesh.verts[i] - fast.mesh.verts[i]; if (d.Magnitude() > 1e-6f) { same = false; break; } }
                CHECK(same);
            }
        }
    }
    // a model with a hand edit is not "untouched": the fast path is never taken for it
    const gd::Recipe r = RecipeAt(*gd::Registry::Instance().Find("fence"), 1.0f);
    PolyMesh cur = gd::Create(r);
    cur.verts[0].x += 0.3f;
    CHECK(!gd::IsUntouched(gd::Analyze(cur, r)));
}

// 5b. Undo states of an untouched model keep only the recipe.
static void TestStateFromRecipe() {
    RUN_SUITE("guided perf: undo states from the recipe");
    const gd::Recipe r = RecipeAt(*gd::Registry::Instance().Find("car"), 2.0f);
    gd::GuidedState st; st.recipe = r; st.fromRecipe = true;
    const PolyMesh m = gd::StateMesh(st);
    const PolyMesh direct = gd::Create(r);
    CHECK(m.faces.size() == direct.faces.size());
    CHECK(st.mesh.faces.empty());
    gd::GuidedHistory hist;
    hist.Push(st);
    gd::GuidedState cur; cur.recipe = RecipeAt(*gd::Registry::Instance().Find("car"), 3.0f); cur.fromRecipe = true;
    CHECK(hist.Undo(cur));
    CHECK(cur.recipe.Param(gd::kDetailParam, 1) == 2.0);
    CHECK(gd::StateMesh(cur).faces.size() == direct.faces.size());
    CHECK(hist.Redo(cur));
    CHECK(cur.recipe.Param(gd::kDetailParam, 1) == 3.0);
}

// 6. The drag preview: cheap detail, and never rebuilt more often than its cost allows.
static void TestPreviewThrottle() {
    RUN_SUITE("guided perf: drag preview detail and throttle");
    CHECK_NEAR(gd::PreviewDetail(3.0f), 1.0f, 1e-6);
    CHECK_NEAR(gd::PreviewDetail(2.0f), 1.0f, 1e-6);
    CHECK_NEAR(gd::PreviewDetail(0.4f), 0.4f, 1e-6);
    gd::PreviewThrottle th(0.08, 0.25);
    CHECK(th.Ready(0.0));
    th.Done(0.0, 1.0);                 // a 1 ms rebuild: the minimum gap applies
    CHECK(!th.Ready(0.05));
    CHECK(th.Ready(0.0801));
    th.Done(1.0, 100.0);               // a 100 ms rebuild may use a quarter of the time: next in 0.4 s
    CHECK(!th.Ready(1.3));
    CHECK(th.Ready(1.41));
    th.Reset();
    CHECK(th.Ready(0.0));
    // over a 2 second drag at the cost of an Ultra rebuild, the time spent rebuilding stays within the share
    gd::PreviewThrottle slow;
    double spent = 0;
    for (double now = 0; now < 2.0; now += 0.001) if (slow.Ready(now)) { slow.Done(now, 60.0); spent += 0.060; }
    CHECK(spent <= 2.0 * 0.3);
}

// 7. The timing table, and budgets for what one released slider costs on an untouched model (generous, for a loaded CI machine).
static void TestTimingsAndBudgets() {
    RUN_SUITE("guided perf: timings (OKAY_GUIDED_PERF=1 prints the table)");
    const bool table = std::getenv("OKAY_GUIDED_PERF") != nullptr;
    if (table) std::printf("%-12s %-6s %9s %9s %9s %9s %9s\n", "template", "detail", "tris", "build", "commit", "frame", "preview");
    double worst[4] = {0, 0, 0, 0};
    for (const gd::Template& t : gd::Registry::Instance().All()) {
        for (int lv : {0, 2, 3}) {
            gd::Recipe r = RecipeAt(t, (float)lv);
            gd::ClearCache();
            auto t0 = std::chrono::steady_clock::now();
            const PolyMesh pm = gd::Create(r);
            const double build = Ms(t0);
            const int tris = gd::TriangleCount(pm);
            Mesh mesh = PanelMesh(pm, (float)lv);
            gd::Recipe next = r;
            for (const auto& p : t.params) if (p.kind == gd::ParamDef::Kind::Float && p.id != gd::kDetailParam) { next.params[p.id] = p.def + (p.max - p.min) * 0.1; break; }
            // one released slider as the panel does it for an untouched model: rebuild, mesh, smooth shading, both files
            t0 = std::chrono::steady_clock::now();
            const gd::RegenResult rr = gd::RegenerateUntouched(r, next);
            Mesh m2 = PanelMesh(rr.mesh, (float)lv);
            std::string err;
            const std::string tmp = (std::filesystem::temp_directory_path() / "okay_guided_perf_bench.okaymesh").string();
            SaveMeshAsset(tmp, m2, &err);
            gd::SaveRecipe(gd::SidecarPath(tmp), rr.recipe);
            const double commit = Ms(t0);
            // work done every frame while the panel is open
            t0 = std::chrono::steady_clock::now();
            volatile std::uint64_t hh = GeometryHash(mesh); (void)hh;
            const double frame = Ms(t0);
            // a drag preview: Medium detail rebuilt and turned into the mesh to draw
            gd::Recipe pv = next; pv.params[gd::kDetailParam] = gd::PreviewDetail((float)lv);
            t0 = std::chrono::steady_clock::now();
            Mesh pmesh = PanelMesh(gd::Create(pv), gd::PreviewDetail((float)lv));
            const double preview = Ms(t0);
            if (table) std::printf("%-12s %-6d %9d %9.1f %9.1f %9.2f %9.1f\n", t.id.c_str(), lv, tris, build, commit, frame, preview);
            worst[lv] = std::max(worst[lv], commit);
            CHECK(rr.ok);
            CHECK(preview < 400.0);   // a drag preview is always cheap
        }
    }
    if (table) std::printf("worst released-slider cost (ms): Low %.0f, High %.0f, Ultra %.0f\n", worst[0], worst[2], worst[3]);
    CHECK(worst[0] < 500.0);
    CHECK(worst[2] < 1000.0);
    CHECK(worst[3] < 3000.0);
    std::error_code ec;
    std::filesystem::remove(std::filesystem::temp_directory_path() / "okay_guided_perf_bench.okaymesh", ec);
    std::filesystem::remove(std::filesystem::temp_directory_path() / "okay_guided_perf_bench.okayguided", ec);
}

int main() {
    gd::RegisterBuiltinTemplates();
    TestUntouchedAfterSmoothing();
    TestSmoothingKeepsPositions();
    TestSaveReopen();
    TestBuildCache();
    TestUntouchedFastPath();
    TestStateFromRecipe();
    TestPreviewThrottle();
    TestTimingsAndBudgets();
    TEST_MAIN_RESULT();
}
