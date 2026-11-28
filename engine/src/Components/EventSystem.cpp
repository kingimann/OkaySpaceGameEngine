// The Event System: the one place game UI input goes through (engine/include/okay/Components/EventSystem.hpp).
// Written from general UI design (a pointer raycast in draw order, enter/exit along the parent chain,
// press-release-click and a drag threshold, focus moved by keys); no other engine's code was used.
#include "okay/Components/EventSystem.hpp"
#include "okay/Components/UINavigation.hpp"
#include "okay/Components/UIColorPicker.hpp"
#include "okay/Components/UIRangeSlider.hpp"
#include "okay/Components/UIDraggable.hpp"
#include "okay/Components/UIWidgetUtil.hpp"   // FireUIEvent (and ScriptComponent)
#include "okay/Input/InputActions.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Log.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace okay {

namespace {

template <class T>
bool On(GameObject* go) {
    const T* c = go->GetComponent<T>();
    return c && c->enabled;
}

// Every enabled component on `go` gets one of the Component pointer messages (ScriptComponent turns them
// into on_mouse_enter ... on_click, a visual script into its On Mouse events, an Action List into its triggers).
void Message(GameObject* go, void (Component::*fn)()) {
    if (!go) return;
    for (Component* c : go->GetComponents<Component>())
        if (c && c->enabled) (c->*fn)();
}

// A script event for `go`, or for its nearest parent with a script when it has none.
void FireUp(GameObject* go, const char* name) {
    for (Transform* t = go ? go->transform : nullptr; t; t = t->Parent()) {
        GameObject* g = t->gameObject;
        if (g && g->GetComponent<ScriptComponent>()) { FireUIEvent(g, name); return; }
    }
}

bool ActionDefined(const std::string& name) { return !name.empty() && InputActions::Has(name); }

// One warning per scene, the first time a widget of the running game asks for an Event System the scene does not have.
void WarnNoEventSystem(Scene& scene) {
    static std::uint64_t s_warned = 0;
    if (s_warned == scene.Uid()) return;
    s_warned = scene.Uid();
    Log::Warning("Scene \"", scene.Name(), "\" has UI but no Event System: its widgets use their own input, so overlapping "
                 "widgets can both react and the Event System settings (drag threshold, navigation, blocking world clicks) "
                 "do not apply. Add one with GameObject > UI > Event System.");
}

} // namespace

bool UIIsPointerTarget(GameObject* go) {
    if (!go) return false;
    if (On<UIButton>(go) || On<UIToggle>(go) || On<UISlider>(go) || On<UIInputField>(go) || On<UIDropdown>(go) ||
        On<UIScrollView>(go) || On<UIListView>(go) || On<UIKeybindField>(go) || On<UITabs>(go) || On<UIStepper>(go) ||
        On<UIRating>(go) || On<UIRangeSlider>(go) || On<UIColorPicker>(go) || On<UIDraggable>(go))
        return true;
    if (const UIPanel* pn = go->GetComponent<UIPanel>(); pn && pn->enabled && pn->blocksPointer) return true;
    return MoreNav::Focusable(go);
}

GameObject* UIRaycastTarget(Scene& scene, const Vec2& point, float canvasW, float canvasH) {
    GameObject* hit = nullptr;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go->active || !UIIsPointerTarget(go)) continue;
        if (UIScreenContains(go, point, canvasW, canvasH) && (!hit || UIDrawsAbove(go, hit)) && !UIHidden(go)) hit = go;
    }
    return hit;
}

EventSystem* SceneEventSystem(Scene& scene) {
    for (EventSystem* es : scene.FindObjectsOfType<EventSystem>())
        if (es && es->gameObject && es->gameObject->active) return es;
    return nullptr;
}

bool SceneHasSeveralEventSystems(Scene& scene) {
    int n = 0;
    for (EventSystem* es : scene.FindObjectsOfType<EventSystem>())
        if (es && es->gameObject && es->gameObject->active && ++n > 1) return true;
    return false;
}

bool UIBlocksWorldPointer(Scene& scene) {
    EventSystem* es = SceneEventSystem(scene);
    if (!es || !es->enabled || !es->blockWorldClicks) return false;
    return UIRaycast(scene, Input::MousePosition()) != nullptr;
}

bool UIEventSystemReaches(GameObject* go, const Vec2& p, float screenW, float screenH) {
    Scene* sc = go ? go->scene() : nullptr;
    if (!sc) return true;
    EventSystem* es = SceneEventSystem(*sc);
    if (!es) {
        if (sc->runScripts) WarnNoEventSystem(*sc);   // the game is running (not the editor's edit mode)
        return true;
    }
    return es->Reaches(go, p, screenW, screenH);
}

const char* UIEventName(UIEvent::Kind kind) {
    switch (kind) {
        case UIEvent::Kind::Enter:     return "enter";
        case UIEvent::Kind::Exit:      return "exit";
        case UIEvent::Kind::Down:      return "down";
        case UIEvent::Kind::Up:        return "up";
        case UIEvent::Kind::Click:     return "click";
        case UIEvent::Kind::DragBegin: return "drag begin";
        case UIEvent::Kind::Drag:      return "drag";
        case UIEvent::Kind::DragEnd:   return "drag end";
        case UIEvent::Kind::Select:    return "select";
        case UIEvent::Kind::Submit:    return "submit";
        case UIEvent::Kind::Cancel:    return "cancel";
    }
    return "";
}

void EventSystem::Start() {
    Scene::worldPointerFilter = &UIBlocksWorldPointer;
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc || firstSelected.empty() || SceneEventSystem(*sc) != this) return;
    GameObject* go = sc->Find(firstSelected);
    if (!go || !go->active || UIHidden(go) || !IsNavFocusable(go)) return;
    for (const auto& o : sc->Objects())
        if (o.get() != go) SetWidgetFocused(o.get(), false);
    SetWidgetFocused(go, true);   // highlighted; the ring itself waits for a key or the pad (UIFocus)
    m_selected = go;
    Send(UIEvent::Kind::Select, go);
}

void EventSystem::Update(float) {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc) return;
    Scene::worldPointerFilter = &UIBlocksWorldPointer;
    if (SceneEventSystem(*sc) != this) return;   // a second Event System does nothing (the Inspector warns)
    Pump(*sc, Input::MousePosition(), Input::GetMouseButton(0), Input::GetMouseButtonDown(0));
    const bool cancel = Input::GetKeyDown(static_cast<char>(27)) || Input::GetGamepadButtonDown(1) ||
                        (ActionDefined(cancelAction) && InputActions::Down(cancelAction));
    if (cancel && navigation && !UITextFieldHasKeyboard(*sc)) {
        Send(UIEvent::Kind::Cancel, m_selected);
        if (m_selected) FireUp(m_selected, "on_cancel");
    }
}

void EventSystem::Send(UIEvent::Kind kind, GameObject* target) {
    UIEvent e;
    e.kind = kind;
    e.target = target;
    e.position = m_pointer;
    m_events.push_back(e);
    m_lastEvent = std::string(UIEventName(kind)) + (target ? " on " + target->name : std::string());
}

void EventSystem::Pump(Scene& scene, const Vec2& pointer, bool down, bool pressed) {
    m_events.clear();
    m_live = true;
    m_reach.valid = false;
    {   // objects destroyed since the last frame are forgotten, never messaged
        std::unordered_set<GameObject*> alive;
        alive.reserve(scene.Objects().size());
        for (const auto& up : scene.Objects()) alive.insert(up.get());
        auto keep = [&](GameObject*& g) { if (g && !alive.count(g)) g = nullptr; };
        keep(m_hovered); keep(m_pressed); keep(m_selected); keep(m_navFocus);
        m_hoverChain.erase(std::remove_if(m_hoverChain.begin(), m_hoverChain.end(),
                                          [&](GameObject* g) { return !alive.count(g); }), m_hoverChain.end());
    }
    const Vec2 before = m_pointer;
    m_pointer = pointer;
    m_hovered = UIRaycast(scene, pointer);

    // Enter / exit along the parent chain: moving from a button onto its own label is not an exit.
    std::vector<GameObject*> chain;
    for (Transform* t = m_hovered ? m_hovered->transform : nullptr; t; t = t->Parent())
        if (t->gameObject) chain.push_back(t->gameObject);
    auto inChain = [](const std::vector<GameObject*>& c, GameObject* g) { return std::find(c.begin(), c.end(), g) != c.end(); };
    const std::vector<GameObject*> old = m_hoverChain;
    for (GameObject* g : old)
        if (!inChain(chain, g)) { Send(UIEvent::Kind::Exit, g); Message(g, &Component::OnMouseExit); }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        if (!inChain(old, *it)) { Send(UIEvent::Kind::Enter, *it); Message(*it, &Component::OnMouseEnter); }
    m_hoverChain = chain;

    // Presses go to the nearest interactive widget in the chain (else the widget itself).
    GameObject* target = m_hovered;
    for (GameObject* g : chain)
        if (UIIsPointerTarget(g)) { target = g; break; }

    if (pressed) {
        m_pressed = target;
        m_pressAt = pointer;
        m_dragging = false;
        if (target) { Send(UIEvent::Kind::Down, target); Message(target, &Component::OnMouseDown); }
        if (target && IsNavFocusable(target) && !UIHidden(target)) {   // the keys carry on from the widget clicked
            for (const auto& o : scene.Objects())
                if (o.get() != target) SetWidgetFocused(o.get(), false);
            SetWidgetFocused(target, true);
            m_navFocus = target;
        }
        if (m_selected != target) {   // clicking empty space clears the selection
            m_selected = target;
            Send(UIEvent::Kind::Select, target);
        }
    } else if (down && m_pressed) {
        const float dx = pointer.x - m_pressAt.x, dy = pointer.y - m_pressAt.y;
        const bool ownDrag = m_pressed->GetComponent<UIDraggable>() != nullptr;   // it sends its own drag events
        if (!m_dragging && std::sqrt(dx * dx + dy * dy) >= std::max(0.0f, dragThreshold)) {
            m_dragging = true;
            Send(UIEvent::Kind::DragBegin, m_pressed);
            if (!ownDrag) FireUp(m_pressed, "on_drag_start");
        } else if (m_dragging && (pointer.x != before.x || pointer.y != before.y)) {
            Send(UIEvent::Kind::Drag, m_pressed);
            if (!ownDrag) FireUp(m_pressed, "on_drag");
        }
    }

    const bool released = !down && (m_wasDown || pressed);
    if (released && m_pressed) {
        GameObject* p = m_pressed;
        Send(UIEvent::Kind::Up, p);
        Message(p, &Component::OnMouseUp);
        if (m_dragging) {
            Send(UIEvent::Kind::DragEnd, p);
            if (!p->GetComponent<UIDraggable>()) FireUp(p, "on_drag_end");
        } else if (inChain(chain, p)) {
            Send(UIEvent::Kind::Click, p);
            if (!p->GetComponent<UIButton>()) Message(p, &Component::OnMouseClick);   // a button fires its own on_click
        }
    }
    if (!down) { m_pressed = nullptr; m_dragging = false; }
    m_wasDown = down;
}

bool EventSystem::Reaches(GameObject* widget, const Vec2& p, float screenW, float screenH) {
    if (!enabled || !widget) return true;
    Scene* sc = widget->scene();
    if (!sc) return true;
    const std::uint64_t frame = Time::FrameCount();
    ReachCache& c = m_reach;
    if (!c.valid || c.frame != frame || c.structure != sc->StructureVersion() || c.components != sc->ComponentVersion() ||
        c.point.x != p.x || c.point.y != p.y || c.w != screenW || c.h != screenH) {
        c.top = UIRaycastTarget(*sc, p, screenW, screenH);
        c.valid = true;
        c.frame = frame;
        c.structure = sc->StructureVersion();
        c.components = sc->ComponentVersion();
        c.point = p;
        c.w = screenW;
        c.h = screenH;
    }
    if (!c.top || c.top == widget) return true;
    if (UIIsSelfOrAncestor(widget, c.top) || UIIsSelfOrAncestor(c.top, widget)) return true;
    // Only the widget's own rectangle is covered. A part it draws outside it (an open dropdown list, a
    // picker's popup) is on top of everything and handles its own clicks.
    return !UIScreenContains(widget, p, screenW, screenH);
}

void EventSystem::Select(GameObject* go) {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (go && go->scene() && IsNavFocusable(go) && SetUIFocus(go)) return;   // SetUIFocus tells NoteFocus
    if (!go && sc)
        for (const auto& o : sc->Objects()) SetWidgetFocused(o.get(), false);
    if (m_selected != go) { m_selected = go; Send(UIEvent::Kind::Select, go); }
}

std::string EventSystem::SelectedName() const { return m_selected ? m_selected->name : std::string(); }

void EventSystem::NavigationActions(int& dx, int& dy, bool& submit) {
    // NavigateUI asks once a frame; a direction counts on the frame it is pushed, so a held stick moves one step.
    int x = 0, y = 0;
    if (ActionDefined(navigateAction)) {
        const Vec2 v = InputActions::Vector(navigateAction);
        x = v.x > 0.5f ? 1 : (v.x < -0.5f ? -1 : 0);
        y = v.y > 0.5f ? -1 : (v.y < -0.5f ? 1 : 0);   // an action's +Y is up; the screen's is down
    }
    dx = (x != 0 && x != m_navPrevX) ? x : 0;
    dy = (y != 0 && y != m_navPrevY) ? y : 0;
    m_navPrevX = x;
    m_navPrevY = y;
    submit = ActionDefined(submitAction) && InputActions::Down(submitAction);
}

void EventSystem::NoteFocus(GameObject* go) {
    m_navFocus = go;
    if (go == m_selected) return;
    m_selected = go;
    Send(UIEvent::Kind::Select, go);
}

void EventSystem::NoteNavigationFocus(GameObject* go) {
    if (go == m_navFocus) return;
    NoteFocus(go);
}

void EventSystem::NoteSubmit(GameObject* go) { Send(UIEvent::Kind::Submit, go); }

} // namespace okay
