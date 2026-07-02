#include "okay/Scene/SceneManager.hpp"
#include "okay/Scene/Scene.hpp"

namespace okay {

std::vector<std::string>& SceneManager::list() {
    static std::vector<std::string> s_scenes;
    return s_scenes;
}
int& SceneManager::activeIndex() { static int s_active = -1; return s_active; }

// Strip folders and extension to get a bare scene name ("Assets/Level1.okayscene"
// -> "Level1") for name-based lookups.
static std::string StemOf(const std::string& path) {
    std::size_t slash = path.find_last_of("/\\");
    std::size_t start = slash == std::string::npos ? 0 : slash + 1;
    std::size_t dot = path.find_last_of('.');
    std::size_t end = (dot == std::string::npos || dot < start) ? path.size() : dot;
    return path.substr(start, end - start);
}

void SceneManager::ClearScenes() { list().clear(); activeIndex() = -1; }

int SceneManager::AddScene(const std::string& path) {
    auto& l = list();
    for (std::size_t i = 0; i < l.size(); ++i)
        if (l[i] == path) return (int)i;
    l.push_back(path);
    return (int)l.size() - 1;
}

const std::vector<std::string>& SceneManager::Scenes() { return list(); }
int SceneManager::SceneCount() { return (int)list().size(); }

int SceneManager::IndexOf(const std::string& nameOrPath) {
    auto& l = list();
    for (std::size_t i = 0; i < l.size(); ++i)
        if (l[i] == nameOrPath) return (int)i;
    std::string stem = StemOf(nameOrPath);
    for (std::size_t i = 0; i < l.size(); ++i)
        if (StemOf(l[i]) == stem) return (int)i;
    return -1;
}

std::string SceneManager::SceneName(int index) {
    auto& l = list();
    if (index < 0 || index >= (int)l.size()) return {};
    return StemOf(l[index]);
}

int SceneManager::ActiveIndex() { return activeIndex(); }

bool SceneManager::LoadScene(Scene& scene, int index) {
    auto& l = list();
    if (index < 0 || index >= (int)l.size()) return false;
    activeIndex() = index;
    scene.RequestLoad(l[index]);
    return true;
}

bool SceneManager::LoadSceneByName(Scene& scene, const std::string& nameOrPath) {
    int idx = IndexOf(nameOrPath);
    if (idx >= 0) return LoadScene(scene, idx);
    // Not registered: still allow loading a raw path so scripts aren't blocked.
    if (nameOrPath.find('.') != std::string::npos ||
        nameOrPath.find('/') != std::string::npos) {
        scene.RequestLoad(nameOrPath);
        return true;
    }
    return false;
}

std::string SceneManager::PathForName(const std::string& nameOrPath) {
    int idx = IndexOf(nameOrPath);
    if (idx >= 0 && idx < (int)list().size()) return list()[idx];
    // Not in the build list: accept a raw path so scripts aren't blocked.
    if (nameOrPath.find('.') != std::string::npos || nameOrPath.find('/') != std::string::npos)
        return nameOrPath;
    return "";
}

bool SceneManager::LoadNextScene(Scene& scene) {
    if (list().empty()) return false;
    int next = (activeIndex() + 1) % (int)list().size();
    return LoadScene(scene, next);
}

bool SceneManager::ReloadScene(Scene& scene) {
    if (activeIndex() < 0) return false;
    return LoadScene(scene, activeIndex());
}

} // namespace okay
