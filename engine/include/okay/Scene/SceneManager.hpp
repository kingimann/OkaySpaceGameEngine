#pragma once
#include <string>
#include <vector>

namespace okay {

class Scene;

/// A lightweight scene registry and loader — the engine's analogue of Unity's
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
    /// The build index of the scene most recently asked to load (-1 if none).
    static int  ActiveIndex();

    // ---- Loading -------------------------------------------------------
    /// Request loading the scene at the given build index. False if out of range.
    static bool LoadScene(Scene& scene, int index);
    /// Request loading by scene name (file stem) or full path. False if unknown.
    static bool LoadSceneByName(Scene& scene, const std::string& nameOrPath);
    /// Load the next scene in the build list, wrapping to the first.
    static bool LoadNextScene(Scene& scene);
    /// Reload the active scene.
    static bool ReloadScene(Scene& scene);

private:
    static std::vector<std::string>& list();
    static int& activeIndex();
};

} // namespace okay
