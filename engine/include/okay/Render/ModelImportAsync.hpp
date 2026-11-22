#pragma once
// Reading a model file without freezing the editor. ModelImportTask runs ImportModelScene on a worker of the engine's
// JobSystem into a private Scene; the main thread polls it each frame and, once it is done, moves the finished model
// into the open scene with Scene::AdoptFrom (no copy). Nothing is drawn or uploaded on the worker: the renderers
// upload meshes and pictures when they first draw them, on the main thread, as before.
// Progress is by stage (waiting for a worker, reading the file, done); the file readers do not report finer steps.
// Cancel stops waiting at once and throws the result away; the worker still finishes reading the file in the
// background, because the readers cannot be stopped half way.
// Header note: keep this header free of <mutex>, <thread> and <condition_variable> (rule 21a); the state shared with
// the worker is atomics in a heap block kept alive by both sides.
#include <memory>
#include <string>

namespace okay {

class GameObject;
class JobSystem;
class Scene;

class ModelImportTask {
public:
    enum class Stage { Idle = 0, Waiting = 1, Reading = 2, Done = 3 };

    ModelImportTask();
    /// A task still running is cancelled (its result is thrown away when the worker ends).
    ~ModelImportTask();
    ModelImportTask(const ModelImportTask&) = delete;
    ModelImportTask& operator=(const ModelImportTask&) = delete;

    /// Start reading `path` on `jobs` (JobSystem::Global() when null; with 0 workers it runs before Start returns).
    /// False, starting nothing, when the path is empty or a reading is already running (Busy).
    bool Start(const std::string& path, JobSystem* jobs = nullptr);
    /// Started and not yet collected or cancelled.
    bool Busy() const;
    /// The reading has ended (imported or failed) and waits for Collect / TakeResult.
    bool Finished() const;
    Stage CurrentStage() const;
    /// 0 before a start; 0.05 waiting for a worker; 0.15 reading; 1 done.
    float Progress() const;
    /// "Waiting for a worker", "Reading the file", "Done", or "" when idle.
    const char* StageText() const;
    /// Seconds since Start (0 when idle).
    double ElapsedSeconds() const;
    /// The file being read (or last read).
    const std::string& Path() const { return m_path; }

    /// Stop waiting: Busy turns false now, and the model is thrown away when the worker ends. A new Start may follow
    /// at once.
    void Cancel();

    /// Main thread. Once Finished: move the model into `scene` and return its top object (once; later calls return
    /// nullptr). nullptr while still reading, or when the file did not import (`failed` then set, and Error() says
    /// why in plain words).
    GameObject* Collect(Scene& scene, bool* failed = nullptr);
    /// Main thread. Once Finished: hand over the private scene the model was read into and its top object (null when
    /// it did not import), for a look before deciding (the Import Model dialog's scan). False while still reading.
    bool TakeResult(std::unique_ptr<Scene>& scene, GameObject*& root, bool* failed = nullptr);
    /// Why the last reading failed ("" when it did not).
    const std::string& Error() const { return m_error; }

    struct Shared;   // the state the worker writes (defined in ModelImportAsync.cpp)
private:
    std::shared_ptr<Shared> m_state;
    std::string m_path;
    std::string m_error;
    bool m_collectedOnce = false;   // the last reading was handed out (Progress stays 1)
};

} // namespace okay
