// The engine runs its own work on the job system (EngineJobs): the physics bounding-box
// pass (2D and 3D), CPU skinning across SkinnedMesh instances, AssetManager background
// loads and navmesh bakes. Results must not depend on the worker count: the same scene
// stepped with JobSystem(0) (everything inline, in order) and JobSystem(4) must give
// bit-identical poses and vertices, and the 4-worker run must really have used jobs.
//
// `test_jobs_adoption bench [bodies]` prints step times for a large scene instead
// (serial vs workers); it is not part of ctest.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/AssetManager.hpp"
#include "okay/Core/Jobs.hpp"
#include "okay/Physics/ColliderShapes3D.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

// Raw bytes of every value that describes a pose, so the comparison is bit-for-bit.
template <class T> void Put(std::vector<unsigned char>& out, const T& v) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

// ---- 3D -----------------------------------------------------------------------------

void Build3D(Scene& s, int bodies, bool improved) {
    s.physics3D().quality = improved ? PhysicsQuality::Improved : PhysicsQuality::Classic;
    GameObject* floor = s.CreateGameObject("Floor");
    floor->transform->localPosition = {0, -0.5f, 0};
    floor->AddComponent<BoxCollider3D>()->size = {200, 1, 200};
    floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    const int side = (int)std::ceil(std::sqrt((double)bodies / 4.0));
    for (int i = 0; i < bodies; ++i) {
        GameObject* g = s.CreateGameObject("B" + std::to_string(i));
        const int layer = i / (side * side), cell = i % (side * side);
        const float x = (float)(cell % side) * 1.05f - side * 0.5f, z = (float)(cell / side) * 1.05f - side * 0.5f;
        g->transform->localPosition = {x + 0.07f * (float)(i % 3), 0.6f + (float)layer * 1.1f, z};
        g->transform->localRotation = Quat::Euler(0.0f, (float)(i * 13 % 90), (float)(i * 7 % 20));
        switch (i % 5) {
            case 0: g->AddComponent<BoxCollider3D>()->size = {0.9f, 0.9f, 0.9f}; break;
            case 1: g->AddComponent<SphereCollider3D>()->radius = 0.45f; break;
            case 2: { auto* c = g->AddComponent<CapsuleCollider3D>(); c->radius = 0.3f; c->height = 1.0f; break; }
            case 3: { auto* c = g->AddComponent<CylinderCollider3D>(); c->radius = 0.4f; c->height = 0.8f; break; }
            default: {   // a compound (bounds done on the calling thread: it caches its parsed parts)
                auto* c = g->AddComponent<CompoundCollider3D>();
                c->AddBox({0, 0, 0}, {0.8f, 0.3f, 0.8f});
                c->AddSphere({0, 0.3f, 0}, 0.25f);
                break;
            }
        }
        auto* rb = g->AddComponent<Rigidbody3D>();
        rb->velocity = {0.1f * (float)(i % 7) - 0.3f, 0.0f, 0.05f * (float)(i % 5)};
    }
}

std::vector<unsigned char> Run3D(JobSystem& js, int bodies, bool improved, int frames) {
    SetEngineJobs(&js);
    std::vector<unsigned char> out;
    {
        Scene s("p3");
        Build3D(s, bodies, improved);
        s.Start();
        for (int f = 0; f < frames; ++f) s.Update(1.0f / 60.0f);
        for (const auto& go : s.Objects()) {
            Put(out, go->transform->localPosition);
            Put(out, go->transform->localRotation);
            if (auto* rb = go->GetComponent<Rigidbody3D>()) { Put(out, rb->velocity); Put(out, rb->angularVelocity); }
        }
    }
    SetEngineJobs(nullptr);
    return out;
}

// ---- 2D -----------------------------------------------------------------------------

void Build2D(Scene& s, int bodies, bool improved) {
    s.physics().quality = improved ? PhysicsQuality::Improved : PhysicsQuality::Classic;
    GameObject* floor = s.CreateGameObject("Floor");
    floor->transform->localPosition = {0, -0.5f, 0};
    floor->AddComponent<BoxCollider2D>()->size = {400, 1};
    floor->AddComponent<Rigidbody2D>()->bodyType = Rigidbody2D::BodyType::Static;
    const int row = 40;
    for (int i = 0; i < bodies; ++i) {
        GameObject* g = s.CreateGameObject("B" + std::to_string(i));
        g->transform->localPosition = {(float)(i % row) * 1.02f - row * 0.5f + 0.05f * (float)(i % 3), 0.6f + (float)(i / row) * 1.05f, 0};
        g->transform->localRotation = Quat::Euler(0, 0, (float)(i * 11 % 30));
        switch (i % 4) {
            case 0: g->AddComponent<BoxCollider2D>()->size = {0.9f, 0.9f}; break;
            case 1: g->AddComponent<CircleCollider2D>()->radius = 0.45f; break;
            case 2: { auto* c = g->AddComponent<CapsuleCollider2D>(); c->size = {0.6f, 1.0f}; break; }
            default: { auto* c = g->AddComponent<PolygonCollider2D>(); c->points = {{-0.4f, -0.4f}, {0.4f, -0.4f}, {0.0f, 0.45f}}; break; }
        }
        auto* rb = g->AddComponent<Rigidbody2D>();
        rb->velocity = {0.1f * (float)(i % 7) - 0.3f, 0.0f};
    }
}

std::vector<unsigned char> Run2D(JobSystem& js, int bodies, bool improved, int frames) {
    SetEngineJobs(&js);
    std::vector<unsigned char> out;
    {
        Scene s("p2");
        Build2D(s, bodies, improved);
        s.Start();
        for (int f = 0; f < frames; ++f) s.Update(1.0f / 60.0f);
        for (const auto& go : s.Objects()) {
            Put(out, go->transform->localPosition);
            Put(out, go->transform->localRotation);
            if (auto* rb = go->GetComponent<Rigidbody2D>()) { Put(out, rb->velocity); Put(out, rb->angularVelocity); }
        }
    }
    SetEngineJobs(nullptr);
    return out;
}

// ---- Skinning -----------------------------------------------------------------------

// `count` characters, each a strip of `verts` vertices bound to three joints in a chain.
std::vector<unsigned char> RunSkin(JobSystem& js, int count, int verts, int frames) {
    SetEngineJobs(&js);
    std::vector<unsigned char> out;
    {
        Scene s("skin");
        std::vector<Transform*> bones;
        std::vector<MeshRenderer*> mrs;
        for (int c = 0; c < count; ++c) {
            GameObject* root = s.CreateGameObject("Char" + std::to_string(c));
            root->transform->localPosition = {(float)c * 2.0f, 0, 0};
            GameObject* meshGo = s.CreateGameObject("Mesh");
            meshGo->transform->SetParent(root->transform, false);
            mrs.push_back(meshGo->AddComponent<MeshRenderer>());
            auto* sm = meshGo->AddComponent<SkinnedMesh>();
            sm->method = c % 3 == 2 ? (int)SkinnedMesh::Method::DualQuaternion : (int)SkinnedMesh::Method::Linear;
            Transform* parent = root->transform;
            for (int j = 0; j < 3; ++j) {
                GameObject* b = s.CreateGameObject("Bone" + std::to_string(j));
                b->transform->SetParent(parent, false);
                b->transform->localPosition = {0, j == 0 ? 0.0f : 1.0f, 0};
                parent = b->transform;
                sm->joints.push_back(b->transform);
                sm->inverseBind.push_back(Mat4::Translate(Vec3{0, -(float)j, 0}));
                bones.push_back(b->transform);
            }
            for (int v = 0; v < verts; ++v) {
                const float y = 3.0f * (float)v / (float)verts;
                sm->bind.vertices.push_back({0.3f * std::cos((float)v), y, 0.3f * std::sin((float)v)});
                sm->bind.normals.push_back({std::cos((float)v), 0.0f, std::sin((float)v)});
                const int j0 = std::min(2, (int)y), j1 = std::min(2, j0 + 1);
                const float w1 = y - (float)j0 > 0.5f ? 0.4f : 0.1f;
                sm->jointIdx.push_back({j0, j1, 0, 0});
                sm->jointWt.push_back({1.0f - w1, w1, 0.0f, 0.0f});
            }
            for (int t = 0; t + 2 < verts; ++t) { sm->bind.triangles.push_back(t); sm->bind.triangles.push_back(t + 1); sm->bind.triangles.push_back(t + 2); }
        }
        s.Start();
        for (int f = 0; f < frames; ++f) {
            for (std::size_t b = 0; b < bones.size(); ++b)
                bones[b]->localRotation = Quat::Euler((float)(f * 3 + (int)b % 7), (float)(f * 5), (float)((int)b % 11) * 2.0f);
            s.Update(1.0f / 60.0f);
        }
        for (MeshRenderer* mr : mrs) {
            for (const Vec3& v : mr->mesh.vertices) Put(out, v);
            for (const Vec3& n : mr->mesh.normals) Put(out, n);
        }
    }
    SetEngineJobs(nullptr);
    return out;
}

std::uint64_t JobsRun(const JobSystem& js) {
    const JobStats st = js.Stats();
    return st.ranOnWorkers + st.ranHelping + st.ranInline;
}

// ---- Asset manager and navmesh -------------------------------------------------------

struct Blob { std::string text; };

void RegisterBlob(AssetManager& m, int delayMs) {
    m.RegisterLoader<Blob>("Blob", [delayMs](const std::string& p, std::string& err) -> std::shared_ptr<Blob> {
        if (delayMs) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        std::ifstream f(p, std::ios::binary);
        if (!f) { err = "cannot open " + p; return nullptr; }
        auto b = std::make_shared<Blob>();
        b->text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return b;
    });
}

void TestAssets(int workers, const fs::path& dir) {
    JobSystem js(workers);
    SetEngineJobs(&js);
    {
        AssetManager m;
        RegisterBlob(m, 0);
        bool got = false;
        auto h = m.LoadAsync<Blob>((dir / "a.txt").string(), [&](AssetHandle<Blob> hh, const std::string&) { got = hh.Ready(); });
        for (int i = 0; i < 5000 && !got; ++i) { m.Pump(); if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        CHECK(got);
        CHECK(h.Ready() && h->text == "alpha");
        CHECK(js.Stats().submitted >= 1);   // the decode ran as a job, not on a thread of its own
    }
    {   // destroyed with slow loads queued and one running: nothing may touch the dead manager
        AssetManager m;
        RegisterBlob(m, 20);
        for (int i = 0; i < 4; ++i) m.LoadAsync<Blob>((dir / "a.txt").string() + std::string(i, '/'));
        m.LoadAsync<Blob>((dir / "b.txt").string());
    }
    js.WaitIdle();
    CHECK(js.Stats().failed == 0);
    SetEngineJobs(nullptr);
}

void TestNavBake(int workers) {
    JobSystem js(workers);
    SetEngineJobs(&js);
    {
        Scene s("nav");
        GameObject* g = s.CreateGameObject("Ground");
        g->transform->localPosition = {0, -0.5f, 0}; g->transform->localScale = {20, 1, 20};
        g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        auto* ns = s.CreateGameObject("Nav Mesh")->AddComponent<NavMeshSurface>();
        ns->settings.cellSize = 0.25f;
        CHECK(ns->StartBake(s));
        CHECK(!ns->StartBake(s));   // one at a time, as before
        bool ok = false, done = false;
        for (int i = 0; i < 20000 && !done; ++i) { done = ns->PollBake(&ok); if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        CHECK(done && ok);
        CHECK(ns->HasMesh());
        CHECK(js.Stats().submitted >= 1);
        // A bake still running when the scene goes: the destructor cancels and waits for it.
        CHECK(ns->StartBake(s));
    }
    {   // cancel right away
        Scene s("nav2");
        GameObject* g = s.CreateGameObject("Ground");
        g->transform->localScale = {30, 1, 30};
        g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        auto* ns = s.CreateGameObject("Nav Mesh")->AddComponent<NavMeshSurface>();
        CHECK(ns->StartBake(s));
        ns->CancelBake();
        CHECK(!ns->PollBake());
        CHECK(ns->StartBake(s));   // a new bake can start after a cancel
        ns->CancelBake();
    }
    js.WaitIdle();
    SetEngineJobs(nullptr);
}

double TimeSteps(JobSystem& js, int bodies, bool improved, int frames, bool is3d) {
    SetEngineJobs(&js);
    double ms = 0.0;
    {
        Scene s("bench");
        if (is3d) Build3D(s, bodies, improved); else Build2D(s, bodies, improved);
        s.Start();
        s.Update(1.0f / 60.0f);
        const auto t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < frames; ++f) s.Update(1.0f / 60.0f);
        ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
    }
    SetEngineJobs(nullptr);
    return ms;
}

double TimeSkin(JobSystem& js, int count, int verts, int frames) {
    const auto t0 = std::chrono::steady_clock::now();
    RunSkin(js, count, verts, frames);
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "bench") {
        const int bodies = argc > 2 ? std::atoi(argv[2]) : 2000;
        const int frames = argc > 3 ? std::atoi(argv[3]) : 30;
        JobSystem serial(0), pool(4);
        for (int q = 0; q < 2; ++q) {
            const char* qn = q ? "Improved" : "Classic";
            std::printf("3D %s %d bodies: 0 workers %.3f ms/step, 4 workers %.3f ms/step\n", qn, bodies,
                        TimeSteps(serial, bodies, q != 0, frames, true), TimeSteps(pool, bodies, q != 0, frames, true));
            std::printf("2D %s %d bodies: 0 workers %.3f ms/step, 4 workers %.3f ms/step\n", qn, bodies,
                        TimeSteps(serial, bodies, q != 0, frames, false), TimeSteps(pool, bodies, q != 0, frames, false));
        }
        std::printf("skinning 200 x 2000 verts, 30 frames (build included): 0 workers %.1f ms, 4 workers %.1f ms\n",
                    TimeSkin(serial, 200, 2000, 30), TimeSkin(pool, 200, 2000, 30));
        return 0;
    }

    RUN_SUITE("jobs_adoption");

    // ---- physics: bit-identical with 0 and 4 workers, and jobs really ran ----
    for (int q = 0; q < 2; ++q) {
        const bool improved = q != 0;
        {
            JobSystem js0(0), js4(4);
            const auto a = Run3D(js0, 400, improved, 40);
            const auto b = Run3D(js4, 400, improved, 40);
            CHECK(!a.empty() && a.size() == b.size());
            CHECK(a == b);
            CHECK(JobsRun(js4) > 0);
            CHECK(js4.Stats().failed == 0);
        }
        {
            JobSystem js0(0), js4(4);
            const auto a = Run2D(js0, 400, improved, 40);
            const auto b = Run2D(js4, 400, improved, 40);
            CHECK(!a.empty() && a.size() == b.size());
            CHECK(a == b);
            CHECK(JobsRun(js4) > 0);
            CHECK(js4.Stats().failed == 0);
        }
    }
    // A tiny scene stays on the calling thread (no job overhead).
    {
        JobSystem js4(4);
        Run3D(js4, 8, false, 5);
        Run2D(js4, 8, false, 5);
        CHECK(js4.Stats().submitted == 0);
    }

    // ---- skinning across instances ----
    {
        JobSystem js0(0), js4(4);
        const auto a = RunSkin(js0, 24, 800, 6);
        const auto b = RunSkin(js4, 24, 800, 6);
        CHECK(!a.empty() && a.size() == b.size());
        CHECK(a == b);
        CHECK(JobsRun(js4) > 0);
    }
    {   // a single small character is skinned inline
        JobSystem js4(4);
        RunSkin(js4, 1, 50, 3);
        CHECK(js4.Stats().submitted == 0);
    }

    // ---- asset manager and navmesh bakes run as jobs ----
    const fs::path dir = fs::temp_directory_path() / ("okay_jobs_adoption_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    { std::ofstream(dir / "a.txt", std::ios::binary) << "alpha"; std::ofstream(dir / "b.txt", std::ios::binary) << "beta"; }
    TestAssets(4, dir);
    TestAssets(0, dir);   // inline: the load finishes before LoadAsync returns; Pump still reports it
    std::error_code ec;
    fs::remove_all(dir, ec);
    TestNavBake(2);
    TestNavBake(0);

    TEST_MAIN_RESULT();
}
