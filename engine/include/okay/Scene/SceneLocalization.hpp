#pragma once
// Keeps a scene's localized text in the current language (see Localization.hpp
// and docs/localization.md).
#include <string>
#include <vector>

namespace okay {

class Scene;
class GameObject;

/// Rewrite the localized widgets of `scene` (Text, Button, Toggle, Tooltip, World
/// UI label, Input Field placeholder) whose key, language or table changed, and
/// tell running scripts once per change of language: on_language_changed(code)
/// (OnLanguageChanged in typed scripts) and On Language Changed in visual scripts.
/// Scene::Update calls it every frame and it only walks the scene when the
/// language, the table or the scene's objects changed; `force` walks anyway (the
/// editor, where keys are edited outside Play).
void LocalizeScene(Scene& scene, bool force = false);
/// The same for one object's widgets (after its key was edited).
void LocalizeObject(GameObject& go);

/// One use of a string-table key in a scene.
struct LocKeyUse {
    std::string key;
    GameObject* object = nullptr;
    std::string where;   ///< "Text", "Button", "Hold Prompt"...
};
/// Every string-table key the scene's components use (Find in scene, unused keys).
std::vector<LocKeyUse> CollectLocKeys(const Scene& scene);

} // namespace okay
