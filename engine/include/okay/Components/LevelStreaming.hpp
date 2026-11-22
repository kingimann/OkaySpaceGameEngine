#pragma once
// Level streaming (feature map 27.4): whole scenes or sections loaded next to the running scene and
// removed again, with no loading screen and no frame hitch. See docs/engine/world_streaming.md.
//
//   LevelStreaming::LoadLevelAsync(scene, "Corridor_B")   - stream a level in (build-list name or path)
//   LevelStreaming::UnloadLevel(scene, "Corridor_B")      - take it out again
//   LevelStreamingVolume                                  - a box that loads its level when a source comes
//                                                           near and unloads it when every source has left
//
// The file is read on the job system and its objects are made a few per frame in a time budget
// (SceneStreamQueue). When a level is in, the object that asked gets the custom event "LevelLoaded"
// (the level name as its value): a visual script's On Custom Event node or a script's OnEvent_LevelLoaded.
// "LevelUnloaded" follows an unload and "LevelFailed" a load that could not be read.
// Scripts: load_level_async(name [, x, y, z]), unload_level(name), is_level_loaded(name), level_progress(name).
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okay {

class GameObject;
class Scene;

class LevelStreaming {
public:
    /// Start streaming `nameOrPath` into `scene` (additive), moved by `offset`. A level already loaded or
    /// loading is not loaded twice (its handle comes back and `onDone` runs when it is in). `notify`, when
    /// given, gets the LevelLoaded / LevelFailed event. A relative path is also looked for in `sceneDir`.
    /// Returns a handle > 0, or 0 when the level is not known.
    static int  LoadLevelAsync(Scene& scene, const std::string& nameOrPath, const Vec3& offset = Vec3{0, 0, 0},
                               std::function<void(bool)> onDone = {}, GameObject* notify = nullptr,
                               const std::string& sceneDir = std::string());
    /// Remove a streamed level (or stop one that is loading). False when it is not streamed in.
    static bool UnloadLevel(Scene& scene, const std::string& nameOrPath, GameObject* notify = nullptr);
    static bool IsLevelLoaded(const Scene& scene, const std::string& nameOrPath);
    static bool IsLevelLoading(const Scene& scene, const std::string& nameOrPath);
    /// 0 unknown, 0.5 reading or being made, 1 loaded.
    static float LevelProgress(const Scene& scene, const std::string& nameOrPath);
    /// The levels streamed in (loaded or loading), by the name they were asked for.
    static std::vector<std::string> Levels(const Scene& scene);
    /// Top-level objects a loaded level brought in.
    static std::vector<std::uint64_t> LevelRoots(const Scene& scene, const std::string& nameOrPath);
    /// The file a name resolves to ("" when unknown): a build-list name, a path, a path with ".okayscene"
    /// added, or any of those under `sceneDir`.
    static std::string Resolve(const std::string& nameOrPath, const std::string& sceneDir = std::string());
    /// Main-thread milliseconds per frame for making streamed level objects (all levels together).
    static void  SetAttachBudgetMs(Scene& scene, float ms);
};

/// A box in the world that streams a level in when a source comes near and out when every source has left.
/// Sources: `target` (a named object, or the main camera when empty) and, with `followSources`, every enabled
/// StreamingSource. The box is centred on this object, axis-aligned, `size` across (scaled by nothing).
class LevelStreamingVolume : public Behaviour {
public:
    std::string level;                 ///< scene to stream (build-list name or path, relative to this scene's folder)
    Vec3  size{20.0f, 10.0f, 20.0f};   ///< the box, centred on this object
    float preloadDistance = 15.0f;     ///< start loading when a source is this close to the box (0 = on entering)
    float unloadMargin = 10.0f;        ///< unload only once every source is this much farther than the preload distance
    bool  keepLoaded = false;          ///< once loaded, never unload
    Vec3  offset{0.0f, 0.0f, 0.0f};    ///< added to the level's object positions
    std::string target;                ///< object to watch; empty = the main camera
    bool  followSources = true;        ///< StreamingSource components count too

    /// Folder of the scene this volume was loaded from (not saved; taken when the volume is made).
    std::string sceneDir;

    LevelStreamingVolume();

    void Update(float dt) override;
    void OnDestroy() override;

    bool IsLoaded() const;
    bool IsLoading() const;
    /// Distance from the nearest source to the box (0 inside); a large number when there is no source.
    float NearestSourceDistance() const;
    /// Called when the level is in (true) or failed (false), and on unload.
    std::function<void(bool)> onLoaded;
    std::function<void()> onUnloaded;

    int loadCount = 0, unloadCount = 0;   ///< how often it streamed in / out (runtime)

private:
    bool m_wanted = false;
};

} // namespace okay
