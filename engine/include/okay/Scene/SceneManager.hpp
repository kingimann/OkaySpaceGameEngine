#pragma once
#include "okay/Scene/Scene.hpp"
#include <string>
#include <vector>

namespace okay {

class Scene;

/// A lightweight scene registry and loader — the engine's
/// SceneManager plus the Build Settings scene list. Register scene file paths
/// (the "build list") once, then load them by index or by name at runtime.
/// Every load defers through Scene::RequestLoad, so it is safe to call from
/// gameplay code / scripts mid-frame (the swap happens at the end of the frame).
///
/// The list is a process-wide singleton: the editor fills it from the open
/// project, a built game fills it from the scenes shipped beside the executable.
class SceneManager {
public:
    // ---- Build list ----------------------------------------------------
    static void ClearScenes();
    /// Append a scene path to the build list; returns its build index. Ignores
    /// exact-duplicate paths (returns the existing index).
    static int  AddScene(const std::string& path);
    static const std::vector<std::string>& Scenes();
    static int  SceneCount();
    /// The build index of a scene by full path or by name (file stem), or -1.
    static int  IndexOf(const std::string& nameOrPath);
    /// Bare scene name (file stem, no folder or extension) at a build index.
    static std::string SceneName(int index);
    /// The build index of the scene that last LOADED (-1 if none). It moves when a load
    /// succeeds, not when one is requested, so a missing or corrupt scene file leaves it alone.
    static int  ActiveIndex();
    /// Resolve a build-list name (file stem) OR a raw path to a scene file path,
    /// or "" if unknown. Used by additive scene merges.
    static std::string PathForName(const std::string& nameOrPath);

    // ---- Loading -------------------------------------------------------
    /// Request loading the scene at the given build index. False if out of range.
    static bool LoadScene(Scene& scene, int index);
    /// Request loading by scene name (file stem) or full path. False if unknown.
    static bool LoadSceneByName(Scene& scene, const std::string& nameOrPath);
    /// Load the next scene in the build list, wrapping to the first.
    static bool LoadNextScene(Scene& scene);
    /// Reload the active scene.
    static bool ReloadScene(Scene& scene);

    // ---- Loading over several frames -------------------------------------
    // For a loading screen: the scene comes in a few objects per Update (see
    // Scene::BeginAsyncLoad) and `opts.onProgress` / LoadProgress report how far along it
    // is. `opts.additive` brings it in next to what is there; the returned handle
    // unloads exactly that content again. Replacing loads (additive = false) also move
    // ActiveIndex once they finish. Returns a handle > 0, or 0 if the scene is unknown or
    // its file cannot be read. The synchronous / deferred calls above are unchanged.
    static int   LoadSceneAsync(Scene& scene, int index, Scene::AsyncLoadOptions opts);
    static int   LoadSceneAsync(Scene& scene, int index) { return LoadSceneAsync(scene, index, Scene::AsyncLoadOptions()); }
    static int   LoadSceneAsyncByName(Scene& scene, const std::string& nameOrPath, Scene::AsyncLoadOptions opts);
    static int   LoadSceneAsyncByName(Scene& scene, const std::string& nameOrPath) { return LoadSceneAsyncByName(scene, nameOrPath, Scene::AsyncLoadOptions()); }
    static bool  IsLoading(const Scene& scene, int handle);
    static bool  IsLoaded(const Scene& scene, int handle);
    static float LoadProgress(const Scene& scene, int handle);
    /// Remove the content of an additive load (or cancel one in flight).
    static bool  UnloadScene(Scene& scene, int handle);

private:
    static std::vector<std::string>& list();
    static int& activeIndex();
};

} // namespace okay
