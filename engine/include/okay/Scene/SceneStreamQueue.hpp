#pragma once
// ---------------------------------------------------------------------------
// Streaming scene documents into a running scene without a frame hitch
// (feature map 27.4). A document is a cell prefab of a World Streamer, a level
// section of a Level Streaming Volume, or a remembered snapshot of a cell.
//
//   1. Request: the file is read and unpacked on the engine job system
//      (EngineJobs()), off the main thread. With 0 workers (a WebGL build, a
//      deterministic test) the job runs inline and the rest is unchanged.
//   2. Pump (main thread, once a frame): a finished read is handed to
//      Scene::BeginAsyncLoad(job), which makes the objects a few at a time in a
//      per-frame time budget (`attachBudgetMs`). At most `maxAttaching`
//      documents are being made at once, so the budget is the frame's total.
//   3. Release: stops a load (what it made is removed) or removes what a
//      finished load brought in.
//
// Flush() finishes every read and every document not yet being made right now,
// on the calling thread: for the first frame of a level and for teleports.
//
// This header has no locks or threads (rule 21a): the worker writes into a slot
// that only the main thread reads, after the job has finished.
// ---------------------------------------------------------------------------
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okay {

class Scene;

/// The folder of the scene file being loaded right now, or else of the one loaded last ("" when none):
/// streaming components remember it when they are made, so their relative paths are also looked for
/// next to the scene file. Defined in SceneSerializer.cpp.
std::string CurrentSceneLoadDirectory();

enum class StreamLoadState {
    Reading,     ///< the file is being read on a worker
    Waiting,     ///< read; waiting for its turn to be made
    Attaching,   ///< objects are being made, a few per frame
    Loaded,      ///< every object is in the scene and started
    Failed,      ///< unreadable file or bad document
    Unknown,     ///< no such ticket (released, or never issued)
};

struct SceneStreamStats {
    int reading = 0, waiting = 0, attaching = 0, loaded = 0;
    std::uint64_t requested = 0, finished = 0, failed = 0, released = 0;
    double readMs = 0.0;        ///< time spent reading + unpacking, summed over all reads (worker time)
};

class SceneStreamQueue {
public:
    SceneStreamQueue();
    ~SceneStreamQueue();
    SceneStreamQueue(const SceneStreamQueue&) = delete;
    SceneStreamQueue& operator=(const SceneStreamQueue&) = delete;

    float attachBudgetMs = 2.0f;   ///< main-thread milliseconds per frame for making streamed objects
    int   objectsPerStep = 8;      ///< objects made per batch inside that budget
    int   maxAttaching = 1;        ///< documents being made at once
    bool  background = true;       ///< false: Request reads on the calling thread (the made-a-few-per-frame part stays)

    /// Called once per ticket when it is Loaded (true) or Failed (false). Not called for a released ticket.
    using Done = std::function<void(int ticket, bool ok)>;

    /// Start streaming a scene or prefab file in, moved by `offset`. Returns a ticket (> 0).
    int  RequestFile(const std::string& path, const Vec3& offset = Vec3{0, 0, 0}, Done onDone = {});
    /// The same with document text (a remembered snapshot); the text is unpacked on the worker too.
    int  RequestText(std::string text, const Vec3& offset = Vec3{0, 0, 0}, Done onDone = {});

    /// Once a frame, on the main thread, with the scene the documents go into.
    void Pump(Scene& scene);
    /// Finish every read and make every waiting document now (blocking). Documents already being made
    /// keep going in the frame budget.
    void Flush(Scene& scene);
    /// Stop a load or remove what it brought in. False for an unknown ticket.
    bool Release(Scene& scene, int ticket);
    /// Release everything.
    void Clear(Scene& scene);

    StreamLoadState State(int ticket) const;
    /// Instance ids of the top-level objects a Loaded ticket brought in.
    std::vector<std::uint64_t> Roots(int ticket) const;
    /// Something is still reading, waiting or being made.
    bool Busy() const;
    SceneStreamStats Stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace okay
