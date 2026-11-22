// A big model is read without freezing the editor: ModelImportTask reads the file on a JobSystem worker into a
// private scene, and the main thread moves the finished model into the open scene with Scene::AdoptFrom (no copy).
// Before this, every model import ran on the main thread and the editor stopped drawing until it ended.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Core/Jobs.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Render/ModelImportAsync.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace okay;
namespace fs = std::filesystem;

namespace {
struct Dir {
    fs::path p;
    Dir() {
        static int n = 0;
        p = fs::temp_directory_path() / ("okay_importasync_" + std::to_string(++n) + "_" +
            std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(p);
    }
    ~Dir() { std::error_code ec; fs::remove_all(p, ec); }
    std::string at(const std::string& f) const { return (p / f).generic_string(); }
};
// An OBJ grid of n x n quads (2 n^2 triangles).
std::string WriteGridObj(const std::string& path, int n) {
    std::ostringstream o;
    for (int y = 0; y <= n; ++y) for (int x = 0; x <= n; ++x) o << "v " << x * 0.01f << " 0 " << y * 0.01f << "\n";
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
        const int a = y * (n + 1) + x + 1, b = a + 1, c = a + n + 1, d = c + 1;
        o << "f " << a << " " << b << " " << d << " " << c << "\n";
    }
    std::ofstream(path, std::ios::binary | std::ios::trunc) << o.str();
    return path;
}
double Ms(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
// A short wait between "frames" without <thread> (rule 21a: tests may be built for Windows too).
void Nap(double ms) { const auto t0 = std::chrono::steady_clock::now(); while (Ms(t0) < ms) {} }
// Poll like the editor does each frame, up to `seconds`.
GameObject* WaitCollect(ModelImportTask& t, Scene& s, bool* failed, double seconds = 30.0) {
    const auto t0 = std::chrono::steady_clock::now();
    while (Ms(t0) < seconds * 1000.0) {
        if (GameObject* r = t.Collect(s, failed)) return r;
        if (failed && *failed) return nullptr;
        Nap(2.0);
    }
    return nullptr;
}
int Triangles(GameObject* root) {
    int n = 0;
    if (!root) return 0;
    for (MeshRenderer* mr : root->GetComponentsInChildren<MeshRenderer>(true)) n += (int)mr->mesh.triangles.size() / 3;
    return n;
}
struct Waker : Component {
    static int awakes;
    void Awake() override { ++awakes; }
};
int Waker::awakes = 0;
} // namespace

int main() {
    RUN_SUITE("Scene::AdoptFrom moves a subtree without copying it");
    {
        Scene from("worker"), to("open");
        GameObject* r = from.CreateGameObject("Model");
        GameObject* c = from.CreateGameObject("Part");
        c->transform->SetParent(r->transform, false);
        c->transform->localPosition = Vec3(1, 2, 3);
        c->AddComponent<Waker>();
        GameObject* other = from.CreateGameObject("Other");
        const std::uint64_t rid = r->InstanceId();
        const std::string cguid = c->Guid();
        Waker::awakes = 0;
        CHECK(to.AdoptFrom(from, r) == r);
        CHECK(r->scene() == &to && c->scene() == &to);
        CHECK(r->InstanceId() == rid && c->Guid() == cguid);
        CHECK(to.Objects().size() == 2 && from.Objects().size() == 1);
        CHECK(from.Objects().size() == 1 && from.Objects()[0].get() == other);
        CHECK(to.FindByGuid(cguid) == c && from.FindByGuid(cguid) == nullptr);
        CHECK(c->transform->Parent() == r->transform);
        CHECK(Waker::awakes == 0);
        to.Update(0.0f);
        CHECK(Waker::awakes == 1);                           // woke up in the scene it joined
        from.Update(0.0f);
        CHECK(Waker::awakes == 1);                           // and not again in the one it left
        CHECK(to.AdoptFrom(to, r) == nullptr);               // not from itself
        CHECK(to.AdoptFrom(from, r) == nullptr);             // r is no longer in `from`
        CHECK(to.AdoptFrom(from, nullptr) == nullptr);
    }

    RUN_SUITE("a model read on a worker joins the open scene");
    {
        Dir d;
        const std::string m = WriteGridObj(d.at("grid.obj"), 20);
        JobSystem jobs(2);
        ModelImportTask t;
        CHECK(!t.Busy() && t.Progress() == 0.0f && std::string(t.StageText()).empty());
        CHECK(t.Start(m, &jobs));
        CHECK(t.Busy());
        CHECK(!t.Start(m, &jobs));                           // one at a time
        CHECK(t.Path() == m);
        Scene s;
        bool failed = false;
        GameObject* r = WaitCollect(t, s, &failed);
        CHECK(r != nullptr && !failed);
        CHECK(r && r->scene() == &s);
        CHECK(Triangles(r) == 800);
        CHECK(!t.Busy());
        CHECK(t.Progress() == 1.0f);
        CHECK(t.Collect(s, &failed) == nullptr);             // handed out once
        bool inScene = false;
        for (const auto& up : s.Objects()) if (up.get() == r) inScene = true;
        CHECK(inScene);
    }

    RUN_SUITE("a file that does not import says why");
    {
        Dir d;
        JobSystem jobs(1);
        ModelImportTask t;
        CHECK(t.Start(d.at("missing.glb"), &jobs));
        Scene s;
        bool failed = false;
        CHECK(WaitCollect(t, s, &failed) == nullptr);
        CHECK(failed);
        CHECK(!t.Error().empty());
        CHECK(s.Objects().empty());
        CHECK(!t.Busy());
        CHECK(!t.Start("", &jobs));
    }

    RUN_SUITE("cancel: nothing arrives, and a new reading can start at once");
    {
        Dir d;
        const std::string m = WriteGridObj(d.at("grid.obj"), 60);
        JobSystem jobs(2);
        ModelImportTask t;
        CHECK(t.Start(m, &jobs));
        t.Cancel();
        CHECK(!t.Busy());
        Scene s;
        bool failed = false;
        CHECK(t.Collect(s, &failed) == nullptr && !failed);
        CHECK(t.Start(m, &jobs));                            // the old worker may still be reading: it does not matter
        GameObject* r = WaitCollect(t, s, &failed);
        CHECK(r != nullptr);
        jobs.WaitIdle();
        int roots = 0;
        for (const auto& up : s.Objects()) if (!up->transform->Parent()) ++roots;
        CHECK(roots == 1);                                   // the cancelled one never joined
    }

    RUN_SUITE("with no workers it reads before Start returns (WebGL, tests)");
    {
        Dir d;
        const std::string m = WriteGridObj(d.at("grid.obj"), 4);
        JobSystem inlineJobs(0);
        ModelImportTask t;
        CHECK(t.Start(m, &inlineJobs));
        CHECK(t.Finished());
        Scene s;
        bool failed = false;
        GameObject* r = t.Collect(s, &failed);
        CHECK(r != nullptr && Triangles(r) == 32);
    }

    RUN_SUITE("the private scene can be looked at first (the Import Model dialog's scan)");
    {
        Dir d;
        const std::string m = WriteGridObj(d.at("grid.obj"), 3);
        JobSystem jobs(1);
        ModelImportTask t;
        CHECK(t.Start(m, &jobs));
        std::unique_ptr<Scene> scan;
        GameObject* root = nullptr;
        bool failed = false;
        const auto t0 = std::chrono::steady_clock::now();
        while (!t.TakeResult(scan, root, &failed) && Ms(t0) < 30000.0) Nap(2.0);
        CHECK(scan != nullptr && root != nullptr && !failed);
        CHECK(root && root->scene() == scan.get());
        CHECK(Triangles(root) == 18);
        CHECK(!t.Busy());
    }

    RUN_SUITE("the main thread is not held while a big file is read");
    {
        Dir d;
        const std::string m = WriteGridObj(d.at("big.obj"), 400);   // 320,000 triangles
        double syncMs = 0.0;
        {
            Scene s;
            const auto t0 = std::chrono::steady_clock::now();
            bool ok = false;
            ImportModelScene(s, m, &ok);
            syncMs = Ms(t0);
            CHECK(ok);
        }
        JobSystem jobs(2);
        ModelImportTask t;
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(t.Start(m, &jobs));
        const double startMs = Ms(t0);
        int frames = 0;
        Scene s;
        bool failed = false;
        GameObject* r = nullptr;
        double longestFrame = 0.0;
        while (!r && !failed && Ms(t0) < 60000.0) {
            const auto f0 = std::chrono::steady_clock::now();
            r = t.Collect(s, &failed);
            longestFrame = std::max(longestFrame, Ms(f0));
            ++frames;
            Nap(1.0);
        }
        std::cout << "  sync import " << syncMs << " ms; Start " << startMs << " ms; " << frames
                  << " polls; longest poll (with the move into the scene) " << longestFrame << " ms\n";
        CHECK(r != nullptr && Triangles(r) == 320000);
        CHECK(startMs < syncMs * 0.25 + 5.0);
        CHECK(longestFrame < syncMs * 0.25 + 5.0);           // the move is cheap next to the reading
        CHECK(frames > 1);                                   // the "editor" kept polling while it read
    }

    TEST_MAIN_RESULT();
}
