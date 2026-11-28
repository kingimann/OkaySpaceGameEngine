#pragma once
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Input/Input.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

/// Casts a point (canvas pixels) against every screen-space UI widget in the
/// scene and returns the top-most one under it, or nullptr. "Top-most" follows
/// draw order: the topmost matching
/// widget under the point wins — the one a player would actually click. Draw order is the
/// renderer's: Canvas sort order, then the widget's layer or uiDrawOrder, then hierarchy order.
/// A widget that is not drawn (an inactive parent, a hidden Canvas) is not hit.
inline GameObject* UIRaycast(Scene& scene, const Vec2& point,
                             float canvasW = UICanvas::Width(),
                             float canvasH = UICanvas::Height()) {
    GameObject* hit = nullptr;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go->active) continue;
        if (UIScreenContains(go, point, canvasW, canvasH) && (!hit || UIDrawsAbove(go, hit)) && !UIHidden(go)) hit = go;
    }
    return hit;
}

/// True for a widget the player works with the pointer (a button, toggle, slider, text field,
/// dropdown, scroll or list view, tabs, stepper, rating, keybind field, draggable, a panel that
/// blocks the pointer, and the other focusable widgets). Only these cover one another for the
/// Event System: a label or a picture drawn over a button does not take its clicks.
bool UIIsPointerTarget(GameObject* go);

/// The top-most pointer target (UIIsPointerTarget) under a point, or nullptr.
GameObject* UIRaycastTarget(Scene& scene, const Vec2& point,
                            float canvasW = UICanvas::Width(), float canvasH = UICanvas::Height());

class EventSystem; // defined below

/// The EventSystem governing a scene's UI. Returns
/// the first active EventSystem found, or nullptr.
EventSystem* SceneEventSystem(Scene& scene);
inline bool SceneHasEventSystem(Scene& scene) { return SceneEventSystem(scene) != nullptr; }

/// True when the scene has more than one active Event System (only the first one is used).
bool SceneHasSeveralEventSystems(Scene& scene);

/// Scene::DispatchPointer asks this: true when the pointer is over UI and the scene's Event
/// System is set to keep those clicks away from world objects.
bool UIBlocksWorldPointer(Scene& scene);

/// One thing the Event System did this frame, for the Inspector readout, scripts and tests.
struct UIEvent {
    enum class Kind { Enter, Exit, Down, Up, Click, DragBegin, Drag, DragEnd, Select, Submit, Cancel };
    Kind kind = Kind::Enter;
    GameObject* target = nullptr;
    Vec2 position{0.0f, 0.0f};
};
/// "enter", "exit", "down", "up", "click", "drag begin", "drag", "drag end", "select", "submit", "cancel".
const char* UIEventName(UIEvent::Kind kind);

/// The one place game UI input goes through.
///
/// Pointer: each frame it raycasts the cursor against the scene's widgets and tracks what is
/// hovered, pressed and selected. It sends the pointer messages (enter, exit, down, up, click;
/// drag begin, drag and drag end once the pointer moves past `dragThreshold`) to the widget under
/// the pointer and its parents: scripts get on_mouse_enter, on_mouse_exit, on_mouse_down,
/// on_mouse_up, on_click, on_drag_start, on_drag and on_drag_end. Widgets reach the pointer
/// through it too: the hosts' hit test (UIClipHitTest) asks UIEventSystemReaches, so a widget
/// covered by another interactive widget drawn above it gets no hover and no click.
///
/// Keyboard and gamepad: NavigateUI reads `navigation`, the submit and navigation actions and
/// `firstSelected` from it, and tells it which widget is focused (Selected()). The cancel action
/// sends on_cancel to the selected widget (or its nearest parent with a script).
///
/// A scene with UI and no Event System keeps the widgets' own input (as before this system) and
/// logs one warning; nothing is added to the scene behind the author's back.
class EventSystem : public Behaviour {
public:
    // ---- Settings (saved with the scene) ----
    /// Pixels the pointer must move with the button held before a press becomes a drag (and no
    /// click is sent on release).
    float dragThreshold = 10.0f;
    /// Arrow keys, WASD, the D-pad and the navigation action move the focus between widgets.
    bool navigation = true;
    /// While the pointer is over UI, world objects get no mouse messages and gameplay world clicks
    /// are held back (GameInputBlocked's pointer reason).
    bool blockWorldClicks = true;
    /// Input action that presses the focused widget, in addition to Enter, Space and pad A.
    /// An action the project does not define is ignored.
    std::string submitAction = "Submit";
    /// Input action that sends on_cancel to the selected widget, in addition to Escape and pad B.
    std::string cancelAction = "Cancel";
    /// Input action (a direction) that moves the focus, in addition to the arrow keys, WASD and
    /// the D-pad. Empty: only those.
    std::string navigateAction;
    /// Name of the widget that takes the focus first (empty: the first one in reading order).
    std::string firstSelected;

    void Start() override;
    /// Drive the system from the current Input pointer against this scene.
    void Update(float) override;

    /// Update state from an explicit pointer (used by tools/tests). `down` is the
    /// button held this frame; `pressed` is the press edge.
    void Pump(Scene& scene, const Vec2& pointer, bool down, bool pressed);

    GameObject* Hovered()  const { return m_hovered; }
    GameObject* Pressed()  const { return m_pressed; }
    GameObject* Selected() const { return m_selected; }
    const Vec2& Pointer()  const { return m_pointer; }
    /// True while a press has moved past the drag threshold.
    bool Dragging() const { return m_dragging; }
    /// True when the cursor is over any UI widget — games use this to swallow
    /// world clicks that land on the HUD.
    bool IsPointerOverUI() const { return m_hovered != nullptr; }
    /// Whether Update has run since the game started (the Inspector's live readout).
    bool Live() const { return m_live; }

    /// Select a widget: the keyboard and gamepad focus moves to it (with its ring) when it can
    /// take focus. nullptr clears the selection.
    void Select(GameObject* go);
    /// The selected widget's name ("" for none).
    std::string SelectedName() const;

    /// What happened this frame (cleared at the start of each Pump), and the newest event in words
    /// ("click on Play"; "" before the first one).
    const std::vector<UIEvent>& FrameEvents() const { return m_events; }
    std::string LastEventText() const { return m_lastEvent; }

    /// May the pointer at `p` reach `widget`? False when another pointer target drawn above it,
    /// not one of its parents or children, is under the point. Cached for the frame.
    bool Reaches(GameObject* widget, const Vec2& p, float screenW, float screenH);

    // ---- Called by NavigateUI ----
    /// The navigation keys from the project's actions this frame: -1, 0 or +1 on each axis
    /// (y down is +1), edge-triggered so a held stick moves one step. Call once a frame.
    void NavigationActions(int& dx, int& dy, bool& submit);
    /// The focus was put on `go` (SetUIFocus, Select): it is now the selected widget.
    void NoteFocus(GameObject* go);
    /// NavigateUI's focus this frame: when it differs from last frame's, `go` becomes the
    /// selected widget (a pointer press keeps its selection until the keys move the focus).
    void NoteNavigationFocus(GameObject* go);
    /// A submit press NavigateUI handled on `go`.
    void NoteSubmit(GameObject* go);

private:
    void Send(UIEvent::Kind kind, GameObject* target);

    Vec2        m_pointer{0.0f, 0.0f};
    Vec2        m_pressAt{0.0f, 0.0f};
    GameObject* m_hovered  = nullptr;
    GameObject* m_pressed  = nullptr;
    GameObject* m_selected = nullptr;
    std::vector<GameObject*> m_hoverChain;   // the hovered widget and its parents (they got Enter)
    bool m_wasDown = false;
    bool m_dragging = false;
    bool m_live = false;
    int  m_navPrevX = 0, m_navPrevY = 0;
    GameObject* m_navFocus = nullptr;        // NavigateUI's focus last frame
    std::vector<UIEvent> m_events;
    std::string m_lastEvent;
    // Reaches() cache: the top pointer target under one point, for one frame and scene state.
    struct ReachCache {
        bool valid = false;
        std::uint64_t frame = 0, structure = 0, components = 0;
        Vec2 point{0.0f, 0.0f};
        float w = 0.0f, h = 0.0f;
        GameObject* top = nullptr;
    } m_reach;
};

} // namespace okay
