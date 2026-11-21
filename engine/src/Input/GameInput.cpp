#include "okay/Input/GameInput.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Components/Seat.hpp"
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

// One reading of the Interact action's bindings: 0 held, 1 pressed this frame, 2 released this frame.
bool InteractRead(int what) {
    const InputActions::Action* a = InputActions::Find("Interact");
    if (!a) return false;
    for (const InputBinding& b : a->bindings) {
        switch (b.kind) {
        case InputBinding::Kind::Key: {
            const char k = (char)b.code;
            if (what == 0 ? Input::GetKeyGame(k) : what == 1 ? Input::GetKeyDownGame(k) : Input::GetKeyUp(k)) return true;
            break;
        }
        case InputBinding::Kind::Mouse:
            if (what == 0 ? Input::GetMouseButton(b.code) : what == 1 ? Input::GetMouseButtonDown(b.code) : Input::GetMouseButtonUp(b.code))
                return true;
            break;
        case InputBinding::Kind::PadButton:
            if (what == 0 ? Input::GetGamepadButton(b.code, b.player) : what == 1 ? Input::GetGamepadButtonDown(b.code, b.player)
                                                                       : Input::GetGamepadButtonUp(b.code, b.player))
                return true;
            break;
        default: break;   // directions and chords are not a use key
        }
    }
    return false;
}

} // namespace

bool InteractPressed() { return InteractRead(1); }
bool InteractHeld() { return InteractRead(0); }
bool InteractReleased() { return InteractRead(2); }

bool IsInteractKey(char key) {
    if (!key) return false;
    const InputActions::Action* a = InputActions::Find("Interact");
    if (!a) return false;
    for (const InputBinding& b : a->bindings)
        if (b.kind == InputBinding::Kind::Key && (char)b.code == key) return true;
    return false;
}

bool UseKeyDown(char componentKey) {
    if (!componentKey) return false;
    if (componentKey == kInteractDefaultKey) return InteractPressed();
    return Input::GetKeyDown(componentKey);
}

bool UseKeyIs(char componentKey, char key) {
    if (!componentKey || !key) return false;
    if (componentKey == kInteractDefaultKey) return IsInteractKey(key);
    return componentKey == key;
}

std::string UseKeyName(char componentKey) {
    if (componentKey == kInteractDefaultKey)
        if (const InputActions::Action* a = InputActions::Find("Interact"))
            for (const InputBinding& b : a->bindings)
                if (b.kind == InputBinding::Kind::Key || b.kind == InputBinding::Kind::Mouse) return b.Label();
    return Input::KeyName(componentKey);
}

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
        if (UseKeyIs(h->dropKey, key) || (h->altDropKey && h->altDropKey == key)) return true;
    }
    if (!havePlayer) return false;
    for (const ItemPickup* pk : scene->FindObjectsOfType<ItemPickup>()) {
        if (!pk || !pk->enabled || !UseKeyIs(pk->interactKey, key) || pk->Held()) continue;
        const GameObject* go = pk->gameObject;
        if (!go || !go->active || !go->transform) continue;
        if ((go->transform->Position() - at).Magnitude() <= pk->interactRange) return true;
    }
    return false;
}

bool LeanKeyTaken(const Scene* scene, char key, const GameObject* who) {
    (void)who;
    if (!scene || !key) return false;
    if (InteractClaimsKey(scene, key)) return true;
    if (IsInteractKey(key))   // a seat in reach (or the one the player sits on) takes the Interact key
        for (const Seat* s : scene->FindObjectsOfType<Seat>())
            if (s && s->enabled && (s->playerInRange || s->Occupant())) return true;
    if (!Input::GetKeyDown(key)) return false;
    for (const InventoryUI* ui : scene->FindObjectsOfType<InventoryUI>())
        if (ui && ui->enabled && ui->toggleKey == key) return true;
    for (const GridInventory* g : scene->FindObjectsOfType<GridInventory>())
        if (g && g->enabled && g->toggleKey == key) return true;
    return false;
}

} // namespace okay
