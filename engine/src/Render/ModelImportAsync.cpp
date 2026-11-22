// Reading a model on a JobSystem worker (see okay/Render/ModelImportAsync.hpp). Written for this engine on its own
// JobSystem and Scene::AdoptFrom: the worker owns a private Scene until it publishes it (`done`); the main thread takes
// it after that, so the scene is only ever touched by one thread at a time. A Scene is always destroyed on the main
// thread (its destructor tells shared script and pool tables to forget it): a reading nobody wants any more parks its
// scene in a list the main thread empties on its next call. Who disposes of a cancelled result is settled by one
// atomic exchange (`claimed`), so the worker and Cancel never both touch it.
#include "okay/Render/ModelImportAsync.hpp"

#include "okay/Core/Jobs.hpp"
#include "okay/Debug/DebugUtil.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Render/ModelImportProblem.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <vector>

namespace okay {

struct ModelImportTask::Shared {
    std::atomic<int>  stage{static_cast<int>(Stage::Waiting)};
    std::atomic<bool> done{false};        // the worker has published `scene` / `root` / `ok`
    std::atomic<bool> cancelled{false};   // nobody wants the result any more
    std::atomic<bool> claimed{false};     // someone took charge of `scene` (the main thread, or the worker parking it)
    // Written by the worker before `done` is stored; read by the main thread after it is loaded.
    std::unique_ptr<Scene> scene;
    GameObject* root = nullptr;
    bool ok = false;
    std::string error;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};

namespace {
// Scenes of readings that were cancelled while a worker held them, waiting to be destroyed on the main thread.
debugutil::SpinLock& ParkedLock() { static debugutil::SpinLock l; return l; }
std::vector<std::unique_ptr<Scene>>& Parked() { static std::vector<std::unique_ptr<Scene>> v; return v; }
void Park(std::unique_ptr<Scene> s) {
    if (!s) return;
    debugutil::SpinGuard g(ParkedLock());
    Parked().push_back(std::move(s));
}
void FreeParked() {   // main thread
    std::vector<std::unique_ptr<Scene>> gone;
    { debugutil::SpinGuard g(ParkedLock()); gone.swap(Parked()); }
}   // destroyed here, outside the lock
} // namespace

ModelImportTask::ModelImportTask() = default;

ModelImportTask::~ModelImportTask() { Cancel(); }

bool ModelImportTask::Start(const std::string& path, JobSystem* jobs) {
    FreeParked();
    if (path.empty() || Busy()) return false;
    auto st = std::make_shared<Shared>();
    m_state = st;
    m_path = path;
    m_error.clear();
    JobSystem& js = jobs ? *jobs : JobSystem::Global();
    JobOptions opt;
    opt.priority = JobPriority::Low;   // gameplay jobs first; a model can wait a frame
    opt.name = "model import";
    js.Submit([st, path] {
        if (st->cancelled.load()) {   // cancelled before a worker got to it: nothing was made
            st->stage.store(static_cast<int>(Stage::Done));
            st->done.store(true);
            return;
        }
        st->stage.store(static_cast<int>(Stage::Reading), std::memory_order_release);
        auto scene = std::make_unique<Scene>("model import");
        scene->physicsEnabled = false;
        bool ok = false;
        GameObject* root = nullptr;
        std::string error;
        try {
            root = ImportModelScene(*scene, path, &ok);
        } catch (const std::exception& e) {
            ok = false; root = nullptr; error = e.what();
        } catch (...) {
            ok = false; root = nullptr; error = "the reader stopped with an unknown error";
        }
        if (!ok || !root) {
            if (error.empty()) error = DescribeModelImportProblem(path);
            root = nullptr;
            ok = false;
        }
        st->scene = std::move(scene);
        st->root = root;
        st->ok = ok;
        st->error = std::move(error);
        st->stage.store(static_cast<int>(Stage::Done));
        st->done.store(true);
        // Cancelled meanwhile: if Cancel did not see `done`, the scene is ours to park for the main thread.
        if (st->cancelled.load() && !st->claimed.exchange(true)) Park(std::move(st->scene));
    }, opt);
    return true;
}

bool ModelImportTask::Busy() const { return m_state != nullptr; }

bool ModelImportTask::Finished() const { return m_state && m_state->done.load(std::memory_order_acquire); }

ModelImportTask::Stage ModelImportTask::CurrentStage() const {
    if (!m_state) return Stage::Idle;
    return static_cast<Stage>(m_state->stage.load(std::memory_order_acquire));
}

float ModelImportTask::Progress() const {
    switch (CurrentStage()) {
        case Stage::Idle: return m_collectedOnce ? 1.0f : 0.0f;   // 1 after a model was handed out
        case Stage::Waiting: return 0.05f;
        case Stage::Reading: return 0.15f;
        case Stage::Done: return 1.0f;
    }
    return 0.0f;
}

const char* ModelImportTask::StageText() const {
    switch (CurrentStage()) {
        case Stage::Idle: return "";
        case Stage::Waiting: return "Waiting for a worker";
        case Stage::Reading: return "Reading the file";
        case Stage::Done: return "Done";
    }
    return "";
}

double ModelImportTask::ElapsedSeconds() const {
    if (!m_state) return 0.0;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_state->started).count();
}

void ModelImportTask::Cancel() {
    FreeParked();
    if (!m_state) return;
    std::shared_ptr<Shared> st = std::move(m_state);
    m_state.reset();
    st->cancelled.store(true);
    // Already published: the scene is freed here, on the main thread. Otherwise the worker parks it when it ends.
    if (st->done.load() && !st->claimed.exchange(true)) st->scene.reset();
    m_collectedOnce = false;
}

GameObject* ModelImportTask::Collect(Scene& scene, bool* failed) {
    if (failed) *failed = false;
    std::unique_ptr<Scene> from;
    GameObject* root = nullptr;
    bool bad = false;
    if (!TakeResult(from, root, &bad)) return nullptr;
    if (bad || !from || !root) { if (failed) *failed = true; return nullptr; }
    GameObject* moved = scene.AdoptFrom(*from, root);
    if (!moved) {
        m_error = "the model could not be moved into the scene";
        if (failed) *failed = true;
        return nullptr;
    }
    m_collectedOnce = true;
    return moved;   // `from` (now empty of the model) is freed here, on the main thread
}

bool ModelImportTask::TakeResult(std::unique_ptr<Scene>& scene, GameObject*& root, bool* failed) {
    if (failed) *failed = false;
    FreeParked();
    if (!Finished()) return false;
    std::shared_ptr<Shared> st = std::move(m_state);
    m_state.reset();
    st->claimed.store(true);   // not cancelled, so the worker never claims it
    scene = std::move(st->scene);
    root = st->ok ? st->root : nullptr;
    m_error = st->ok ? std::string() : st->error;
    if (!st->ok && failed) *failed = true;
    m_collectedOnce = st->ok;
    return true;
}

} // namespace okay
