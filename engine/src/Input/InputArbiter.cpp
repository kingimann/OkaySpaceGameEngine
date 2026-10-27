#include "okay/Input/InputArbiter.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Components/Interactor.hpp"
#include "okay/Components/InventoryPlayer.hpp"
#include "okay/Components/ChestInventory.hpp"
#include "okay/Components/CraftingStation.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/LogicLinks.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/GridInventoryUI.hpp"
#include "okay/Components/MeleeAttacker.hpp"
#include "okay/Components/Flashlight.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Components/StructureBuilder.hpp"
#include "okay/Components/CraftingMenu.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <map>
#include <vector>
#include <cmath>

namespace okay {
namespace {

const Scene* g_frame = nullptr;
bool g_valid = false;
InteractChoice g_choice;

const Interactor* FindInteractor(const Scene* s) {
    if (!s) return nullptr;
    if (GameObject* p = FindInventoryPlayer(s))
        if (auto* i = p->GetComponent<Interactor>()) return i;
    for (const auto& up : s->Objects())
        if (up) if (auto* i = up->GetComponent<Interactor>()) return i;
    return nullptr;
}

std::string ContainerName(InventoryInteractable* c) {
    if (!c) return "Use";
    if (auto* ch = dynamic_cast<ChestInventory*>(c))
        return ch->title.empty() ? "Chest" : ch->title;
    if (auto* cs = dynamic_cast<CraftingStation*>(c))
        return cs->title.empty() ? "Crafting" : cs->title;
    return (c->gameObject && !c->gameObject->name.empty()) ? c->gameObject->name : "Use";
}

std::string MakePrompt(char key, const std::string& verb, const std::string& name) {
    return Input::KeyName(key) + " \xE2\x80\x94 " + verb + " " + name;
}

float DistSq(const Scene* s, Transform* t) {
    if (!t) return 0.0f;
    Vec3 p;
    if (!InventoryPlayerPosition(s, "", p)) return 0.0f;
    const Vec3 a = t->Position();
    const float dx = a.x - p.x, dy = a.y - p.y, dz = a.z - p.z;
    return dx * dx + dy * dy + dz * dz;
}

InteractChoice Compute(const Scene* s) {
    InteractChoice best;
    if (!s) return best;
    const char key = InputArbiter::SharedInteractKey(s);
    float bestD = 0.0f;
    bool any = false;
    auto consider = [&](float d, InteractChoice::Kind kind, Behaviour* target, const std::string& verb, const std::string& name) {
        if (d < 0.0f) d = 0.0f;
        if (any && d >= bestD) return;
        any = true; bestD = d;
        best.kind = kind; best.target = target;
        best.prompt = MakePrompt(key, verb, name);
    };
    for (const auto& up : s->Objects()) {
        if (!up || !up->active) continue;
        for (InventoryInteractable* c : up->GetComponents<InventoryInteractable>()) {
            if (!c->enabled || !c->PlayerInRange()) continue;
            consider(c->PlayerDistanceSq(), InteractChoice::Kind::Container, c, "Open", ContainerName(c));
        }
        if (auto* pk = up->GetComponent<ItemPickup>()) {
            if (!pk->enabled || pk->Held() || pk->interactRange <= 0.0f || !up->transform) continue;
            const float d = DistSq(s, up->transform);
            if (d <= pk->interactRange * pk->interactRange)
                consider(d, InteractChoice::Kind::Pickup, pk, "Take", pk->item.empty() ? up->name : pk->item);
        }
        if (auto* lg = up->GetComponent<LogicSource>()) {
            if (!lg->enabled || !lg->CanPress()) continue;
            consider(DistSq(s, up->transform), InteractChoice::Kind::Logic, lg, "Use", up->name);
        }
    }
    return best;
}

void AddKey(std::map<char, std::vector<std::string>>& used, char key, const char* what) {
    if (!key || !what) return;
    auto& list = used[key];
    for (const std::string& s : list) if (s == what) return;
    list.emplace_back(what);
}

} // namespace

void InputArbiter::BeginFrame(const Scene* s) {
    g_frame = s;
    g_valid = false;
}

InteractChoice InputArbiter::BestInteract(const Scene* s) {
    if (g_valid && g_frame == s) return g_choice;
    g_frame = s;
    g_choice = Compute(s);
    g_valid = true;
    return g_choice;
}

char InputArbiter::SharedInteractKey(const Scene* s) {
    if (const Interactor* i = FindInteractor(s)) return i->Key();
    return InputActions::Default(GameAction::Interact).key;
}

int InputArbiter::SharedInteractPad(const Scene* s) {
    if (const Interactor* i = FindInteractor(s)) return i->Pad();
    return InputActions::Default(GameAction::Interact).pad;
}

bool InputArbiter::SharedInteractDown(const Scene* s) {
    if (!InputActions::Allows(GameAction::Interact)) return false;
    const char key = SharedInteractKey(s);
    if (key && Input::GetKeyDown(key)) return true;
    const int pad = SharedInteractPad(s);
    return pad >= 0 && Input::GetGamepadButtonDown(pad);
}

bool InputArbiter::KeyOwnedByWorld(const Scene* s, char key) {
    return InventoryInteractable::Nearest(s, key) != nullptr;
}

bool InputArbiter::SuppressLean(const Scene* s, char key) {
    if (!key || !s) return false;
    if (key == SharedInteractKey(s) && BestInteract(s).kind != InteractChoice::Kind::None) return true;
    if (KeyOwnedByWorld(s, key)) return true;
    if (!Input::GetKeyDown(key)) return false;
    for (const auto& up : s->Objects()) {
        if (!up || !up->active) continue;
        if (auto* ui = up->GetComponent<InventoryUI>())
            if (ui->enabled && ui->toggleKey == key) return true;
        if (auto* g = up->GetComponent<GridInventory>())
            if (g->enabled && g->toggleKey == key) return true;
    }
    return false;
}

bool InputArbiter::BagOpen(const Scene* s) {
    if (!s) return false;
    for (const auto& up : s->Objects()) {
        if (!up || !up->active) continue;
        if (auto* ui = up->GetComponent<InventoryUI>()) if (ui->enabled && ui->open) return true;
        if (auto* g = up->GetComponent<GridInventory>()) if (g->enabled && g->open) return true;
        if (auto* cs = up->GetComponent<CraftingStation>()) if (cs->enabled && cs->open) return true;
        if (auto* ch = up->GetComponent<ChestInventory>()) if (ch->enabled && ch->open) return true;
    }
    return false;
}

bool InputArbiter::HotbarOwnsNumberKeys(const Scene* s) {
    if (!s) return false;
    for (const auto& up : s->Objects()) {
        if (!up || !up->active) continue;
        if (auto* ui = up->GetComponent<InventoryUI>())
            if (ui->enabled && ui->selectHotkeys) return true;
    }
    return false;
}

bool InputArbiter::GridDragOwnsRotate(const Scene* s, char key) {
    if (!s || !key) return false;
    for (const auto& up : s->Objects()) {
        if (!up || !up->active) continue;
        if (auto* ui = up->GetComponent<GridInventoryUI>())
            if (ui->enabled && ui->dragIndex >= 0 && ui->rotateKey == key) return true;
    }
    return false;
}

WheelOwner InputArbiter::WheelFor(const Scene* s) {
    if (InputActions::BlocksAll()) return WheelOwner::None;
    if (BagOpen(s) || Input::UICaptured() || InputActions::Context() == InputContext::InventoryOpen)
        return WheelOwner::Bag;
    if (InputActions::HotbarWheelEnabled() && s) {
        for (const auto& up : s->Objects()) {
            if (!up || !up->active) continue;
            if (auto* ui = up->GetComponent<InventoryUI>())
                if (ui->enabled && ui->scrollSelect && !ui->open) return WheelOwner::Hotbar;
        }
    }
    return WheelOwner::Gameplay;
}

std::string InputArbiter::KeyConflictSummary(const GameObject* go) {
    if (!go) return {};
    std::map<char, std::vector<std::string>> used;
    if (auto* ui = go->GetComponent<InventoryUI>()) {
        AddKey(used, ui->toggleKey, "Backpack");
        AddKey(used, ui->sortKey, "Sort");
        AddKey(used, ui->dropKey, "Drop");
        AddKey(used, ui->useKey, "Use");
        if (ui->selectHotkeys)
            for (int i = 0; i < ui->hotbarSlots && i < 9; ++i)
                AddKey(used, (char)('1' + i), "Hotbar");
    }
    if (auto* g = go->GetComponent<GridInventory>()) AddKey(used, g->toggleKey, "Grid bag");
    if (auto* m = go->GetComponent<MeleeAttacker>()) AddKey(used, m->attackKey, "Melee");
    if (auto* f = go->GetComponent<Flashlight>()) AddKey(used, f->toggleKey, "Flashlight");
    if (auto* c = go->GetComponent<CraftingMenu>()) AddKey(used, c->toggleKey, "Crafting menu");
    if (auto* ia = go->GetComponent<Interactor>()) AddKey(used, ia->Key(), "Interact");
    auto leans = [&](char left, char right) {
        AddKey(used, left, "Lean left");
        AddKey(used, right, "Lean right");
    };
    if (auto* fp = go->GetComponent<FirstPersonController>()) leans(fp->leanLeftKey, fp->leanRightKey);
    if (auto* tp = go->GetComponent<ThirdPersonController>()) leans(tp->leanLeftKey, tp->leanRightKey);
    if (auto* ts = go->GetComponent<ThirdPersonShooterController>()) leans(ts->leanLeftKey, ts->leanRightKey);
    if (auto* b = go->GetComponent<StructureBuilder>()) {
        AddKey(used, b->repairKey, "Repair");
        AddKey(used, b->rotateKey, "Build rotate");
        AddKey(used, b->upgradeKey, "Upgrade");
        if (b->pieceHotkeys)
            for (int i = 0; i < 8; ++i) AddKey(used, (char)('1' + i), "Build piece");
    }
    std::string out;
    for (const auto& kv : used) {
        if (kv.second.size() < 2) continue;
        if (!out.empty()) out += " ";
        out += Input::KeyName(kv.first);
        out += ": ";
        for (std::size_t i = 0; i < kv.second.size(); ++i) {
            if (i) out += (i + 1 == kv.second.size()) ? " and " : ", ";
            out += kv.second[i];
        }
        out += ".";
    }
    return out;
}

} // namespace okay
