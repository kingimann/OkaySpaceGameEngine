#pragma once
#include <string>

namespace okay {

class Scene;
class GameObject;

/// Keeps inventories across scenes and saves them on their own.
///  - An Inventory / GridInventory with keepOnSceneLoad carries its contents into the
///    next scene loaded with RequestLoad / load_scene: the object with the same name
///    there (the player, usually) gets them. Scene calls Before/AfterSceneLoad.
///  - SaveObject / LoadObject write and read every inventory on one object (its
///    Inventory and grid bag) to a small text file, so a game can save the player's bag
///    without a whole-scene snapshot.
namespace InventoryPersistence {

void BeforeSceneLoad(Scene& oldScene);
void AfterSceneLoad(Scene& newScene);

bool SaveObject(const GameObject& go, const std::string& path);
bool LoadObject(GameObject& go, const std::string& path);
std::string SerializeObject(const GameObject& go);
bool LoadObjectText(GameObject& go, const std::string& text);

} // namespace InventoryPersistence
} // namespace okay
