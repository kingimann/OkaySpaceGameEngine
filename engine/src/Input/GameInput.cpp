#include "okay/Input/GameInput.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/ChestInventory.hpp"
#include "okay/Components/CraftingStation.hpp"
#include "okay/Components/InventoryPlayer.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/HoldPrompt.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIModal.hpp"
#include "okay/Components/DialogueRunner.hpp"
#include "okay/Components/SettingsScreen.hpp"
#include "okay/Components/EventSystem.hpp"
#include "okay/Components/SequencePlayer.hpp"
#include "okay/Components/Minimap.hpp"

namespace okay {
namespace {

template <class T>
bool Any(const Scene* scene, bool (*pred)(const T*)) {
    if (!scene) return false;
    for (const T* c : scene->FindObjectsOfType<T>())
        if (c && pred(c)) return true;
    return false;
}

bool BagOpen(const InventoryUI* ui) { return ui && ui->enabled && ui->open; }
bool GridOpen(const GridInventory* g) { return g && g->enabled && g->open; }
bool ChestOpen(const ChestInventory* c) { return c && c->enabled && c->open; }
bool CraftOpen(const CraftingStation* c) { return c && c->enabled && c->open; }
bool FieldOn(const UIInputField* f) { return f && f->enabled && f->focused && f->gameObject && f->gameObject->active; }
bool MapOpen(const Minimap* m) { return m && m->enabled && m->fullscreen; }

bool PointerOnUI(const Scene* scene) {
    if (!scene) return false;
    const bool click = Input::GetMouseButton(0) || Input::GetMouseButton(1) || Input::GetMouseButton(2);
    if (!click) return false;
    return UIRaycast(const_cast<Scene&>(*scene), Input::MousePosition()) != nullptr;
}

} // namespace

const char* GameInputBlockReason(const Scene* scene, unsigned mask) {
    if ((mask & (unsigned)GameInputMask::Pause) && Game::Paused()) return "pause";
    if ((mask & (unsigned)GameInputMask::Bag) && (Any<InventoryUI>(scene, BagOpen) || Any<GridInventory>(scene, GridOpen)))
        return "bag";
    if ((mask & (unsigned)GameInputMask::Chest) && Any<ChestInventory>(scene, ChestOpen)) return "chest";
    if ((mask & (unsigned)GameInputMask::Craft) && Any<CraftingStation>(scene, CraftOpen)) return "craft";
    if ((mask & (unsigned)GameInputMask::Text) && Any<UIInputField>(scene, FieldOn)) return "text";
    if ((mask & (unsigned)GameInputMask::Modal) && scene &&
        (UIModal::Top(scene) != nullptr || SettingsScreen::AnyOpen(scene))) return "modal";
    if ((mask & (unsigned)GameInputMask::Dialogue) && scene && DialogueRunner::ActiveIn(const_cast<Scene*>(scene)))
        return "dialogue";
    if ((mask & (unsigned)GameInputMask::Sequence) && scene && SequencePlayer::AnyLocksInput(*scene)) return "sequence";
    if ((mask & (unsigned)GameInputMask::Bag) && Any<Minimap>(scene, MapOpen)) return "map";
    if ((mask & (unsigned)GameInputMask::Pointer) && PointerOnUI(scene)) return "pointer";
    return "";
}

bool GameInputBlocked(const Scene* scene, unsigned mask) {
    return GameInputBlockReason(scene, mask)[0] != '\0';
}

bool InteractClaimsKey(const Scene* scene, char key) {
    if (!scene || !key) return false;
    if (InventoryInteractable::Nearest(scene, key)) return true;
    GameObject* player = FindInventoryPlayer(scene);
    const Vec3 at = (player && player->transform) ? player->transform->Position() : Vec3{};
    const bool havePlayer = player && player->transform;
    for (const HoldPrompt* h : scene->FindObjectsOfType<HoldPrompt>()) {
        if (!h || !h->enabled || !h->gameObject) continue;
        const auto* hold = h->gameObject->GetComponent<ProceduralObjectHolding>();
        if (!hold || !hold->IsHolding()) continue;
        if (h->dropKey == key || (h->altDropKey && h->altDropKey == key)) return true;
    }
    if (!havePlayer) return false;
    for (const ItemPickup* pk : scene->FindObjectsOfType<ItemPickup>()) {
        if (!pk || !pk->enabled || pk->interactKey != key || pk->Held()) continue;
        const GameObject* go = pk->gameObject;
        if (!go || !go->active || !go->transform) continue;
        if ((go->transform->Position() - at).Magnitude() <= pk->interactRange) return true;
    }
    return false;
}

bool LeanKeyTaken(const Scene* scene, char key) {
    if (!scene || !key) return false;
    if (InteractClaimsKey(scene, key)) return true;
    if (!Input::GetKeyDown(key)) return false;
    for (const InventoryUI* ui : scene->FindObjectsOfType<InventoryUI>())
        if (ui && ui->enabled && ui->toggleKey == key) return true;
    for (const GridInventory* g : scene->FindObjectsOfType<GridInventory>())
        if (g && g->enabled && g->toggleKey == key) return true;
    return false;
}

} // namespace okay
