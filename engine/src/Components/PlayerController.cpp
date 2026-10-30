#include "okay/Components/PlayerController.hpp"

namespace okay {

namespace {
template <class T>
void Keep(GameObject* go, bool want) {
    T* part = go->GetComponent<T>();
    if (want && !part) go->AddComponent<T>();
    else if (!want && part) go->RemoveComponent(part);
}
} // namespace

void PlayerController::EnsureParts() {
    if (!gameObject) return;
    // Going from first person only to Both starts on the eyes; every other way
    // in starts on the orbit.
    const bool startFirst = gameObject->GetComponent<FirstPersonController>() &&
                            !gameObject->GetComponent<ThirdPersonController>();
    Keep<FirstPersonController>(gameObject, m_mode != ViewMode::Third);
    Keep<ThirdPersonController>(gameObject, m_mode != ViewMode::First);
    const bool hadToggle = toggle() != nullptr;
    Keep<PlayerView>(gameObject, m_mode == ViewMode::Both);
    if (!hadToggle)
        if (PlayerView* pv = toggle()) {
            pv->allowToggle = true;
            pv->defaultView = startFirst ? PlayerView::View::First : PlayerView::View::Third;
        }
}

void PlayerController::SetViewMode(ViewMode mode) {
    m_mode = mode;
    EnsureParts();
}

PlayerController* PlayerController::Add(GameObject* go, ViewMode mode, PlayerView::View start) {
    auto* pc = go->AddComponent<PlayerController>();
    pc->m_mode = mode;
    pc->EnsureParts();
    if (PlayerView* pv = pc->toggle()) pv->defaultView = start;
    return pc;
}

PlayerController* PlayerController::Adopt(GameObject* go) {
    if (!go || Of(go)) return Of(go);
    auto* fp = go->GetComponent<FirstPersonController>();
    auto* tp = go->GetComponent<ThirdPersonController>();
    auto* pv = go->GetComponent<PlayerView>();
    if (!fp && !tp) return nullptr;
    // One controller with the toggle on is a switchable player: it gets the other.
    const bool both = (fp && tp) || (pv && pv->allowToggle);
    ViewMode mode = both ? ViewMode::Both : (fp ? ViewMode::First : ViewMode::Third);
    if (mode == ViewMode::Both && !pv) {
        // Both controllers and no view component: first person moved the body.
        pv = go->AddComponent<PlayerView>();
        pv->allowToggle = false;
        pv->defaultView = PlayerView::View::First;
    }
    auto* pc = go->AddComponent<PlayerController>();
    pc->m_mode = mode;
    // Sit above the parts, so the Inspector draws the controller first.
    std::size_t index = 0, firstPart = 0;
    bool found = false;
    for (const auto& c : go->Components()) {
        if (!found && (c.get() == fp || c.get() == tp || c.get() == pv)) { firstPart = index; found = true; }
        ++index;
    }
    if (found) go->MoveComponent(pc, -(int)(index - 1 - firstPart));
    pc->EnsureParts();
    return pc;
}

int PlayerController::CurrentView() const {
    if (m_mode == ViewMode::Both) {
        if (PlayerView* pv = toggle()) return pv->GetView();
        return 0;
    }
    return m_mode == ViewMode::Third ? 1 : 0;
}

void PlayerController::CollectOwnedParts(std::vector<Component*>& out) const {
    if (!gameObject) return;
    if (auto* fp = first())  out.push_back(fp);
    if (auto* tp = third())  out.push_back(tp);
    if (auto* pv = toggle()) out.push_back(pv);
}

void PlayerController::SyncEnabled() {
    std::vector<Component*> parts;
    CollectOwnedParts(parts);
    for (Component* part : parts) part->enabled = enabled;
}

} // namespace okay
