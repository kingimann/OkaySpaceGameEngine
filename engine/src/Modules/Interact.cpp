#include "okay/Modules/Interact.hpp"
#include "okay/Modules/ISeat.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>

namespace okay {

namespace {
struct InteractFinder { std::string name; interact::Finder fn; };
std::vector<InteractFinder>& InteractFinders() { static std::vector<InteractFinder> v; return v; }
struct SeatFinder { std::string name; seat::Finder fn; };
std::vector<SeatFinder>& SeatFinders() { static std::vector<SeatFinder> v; return v; }
bool& BlockFlag() { static bool b = false; return b; }

// True while the keyboard binding of the Interact action is still the old default (E). Then the raw key
// counts too, so a program that has not run InputActions::Tick (a headless test, a tool) still sees E.
bool KeyboardIsDefaultE() {
    const InputBinding* b = InputActions::Current("Interact", false);
    return !b || (b->kind == InputBinding::Kind::Key && (b->code == 'e' || b->code == 'E'));
}
} // namespace

namespace interact {

bool Pressed(char legacyKey) {
    if (!legacyKey) return false;
    if (legacyKey != 'e') return Input::GetKeyDown(legacyKey);   // chosen on purpose in the scene
    if (InputActions::Down("Interact")) return true;
    return KeyboardIsDefaultE() && Input::GetKeyDown('e');
}

std::string KeyLabel(char legacyKey) {
    if (legacyKey && legacyKey != 'e') return Input::KeyName(legacyKey);
    if (const InputBinding* b = InputActions::Current("Interact", false)) return b->Label();
    return Input::KeyName('e');
}

void Block() { BlockFlag() = true; }
bool Blocked() { return BlockFlag(); }
void ClearBlock() { BlockFlag() = false; }
bool Take(char legacyKey) {
    if (BlockFlag() || !Pressed(legacyKey)) return false;
    BlockFlag() = true;
    return true;
}

void AddFinder(const std::string& name, Finder f) {
    for (auto& e : InteractFinders()) if (e.name == name) { e.fn = std::move(f); return; }
    InteractFinders().push_back({name, std::move(f)});
}
void ClearFinders() { InteractFinders().clear(); }

std::vector<std::unique_ptr<IInteractable>> Find(GameObject* go) {
    (void)ModuleRegistry::Get();   // makes the modules add their adapters
    std::vector<std::unique_ptr<IInteractable>> out;
    if (!go) return out;
    for (auto& f : InteractFinders()) f.fn(go, out);
    return out;
}

std::unique_ptr<IInteractable> Nearest(Scene& scene, GameObject* actor, float* distance) {
    if (!actor || !actor->transform) return nullptr;
    InteractContext ctx;
    ctx.actor = actor;
    const Vec3 at = actor->transform->Position();
    std::unique_ptr<IInteractable> best;
    float bestD = 0.0f;
    for (const auto& up : scene.Objects()) {
        GameObject* g = up.get();
        if (!g || g == actor || !g->active || !g->transform) continue;
        const float d = (g->transform->Position() - at).Magnitude();
        for (auto& it : Find(g)) {
            if (it->Range() > 0.0f && d > it->Range()) continue;
            if (!it->CanInteract(ctx)) continue;
            if (!best || d < bestD) { bestD = d; best = std::move(it); }
        }
    }
    if (best && distance) *distance = bestD;
    return best;
}

bool Use(Scene& scene, GameObject* actor) {
    auto target = Nearest(scene, actor);
    if (!target) return false;
    InteractContext ctx;
    ctx.actor = actor;
    if (!target->Interact(ctx)) return false;
    Block();
    return true;
}

} // namespace interact

namespace seat {

void AddFinder(const std::string& name, Finder f) {
    for (auto& e : SeatFinders()) if (e.name == name) { e.fn = std::move(f); return; }
    SeatFinders().push_back({name, std::move(f)});
}
void ClearFinders() { SeatFinders().clear(); }

std::unique_ptr<ISeat> Find(GameObject* go) {
    (void)ModuleRegistry::Get();
    if (!go) return nullptr;
    for (auto& f : SeatFinders())
        if (auto s = f.fn(go)) return s;
    return nullptr;
}

std::unique_ptr<ISeat> SeatOf(GameObject* who) {
    if (!who || !who->scene()) return nullptr;
    for (const auto& up : who->scene()->Objects()) {
        if (!up) continue;
        if (auto s = Find(up.get()))
            if (s->Occupant() == who) return s;
    }
    return nullptr;
}

} // namespace seat
} // namespace okay
