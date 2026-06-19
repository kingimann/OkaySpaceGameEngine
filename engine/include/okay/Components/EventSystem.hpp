#pragma once
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Input/Input.hpp"
#include <vector>

namespace okay {

/// Casts a point (canvas pixels) against every screen-space UI widget in the
/// scene and returns the top-most one under it, or nullptr. "Top-most" follows
/// draw order: later objects render over earlier ones, so the last matching
/// widget in scene order wins — the one a player would actually click.
inline GameObject* UIRaycast(Scene& scene, const Vec2& point,
                             float canvasW = UICanvas::Width(),
                             float canvasH = UICanvas::Height()) {
    GameObject* hit = nullptr;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go->active) continue;
        UIRect r = GetUIRect(go);
        if (r.valid && r.Contains(point, canvasW, canvasH)) hit = go;
    }
    return hit;
}

/// The single place pointer input meets the UI, mirroring Unity's EventSystem.
/// Each frame it raycasts the cursor against the scene's widgets and tracks what
/// is hovered, what is pressed, and what is currently selected (the widget the
/// pointer last pressed). Individual widgets still run their own behaviour; this
/// centralizes the *state* games and tools read ("is anything hovered?", "what
/// did the player just click?") and keeps a stable notion of UI focus.
class EventSystem : public Behaviour {
public:
    /// Drive the system from the current Input pointer against this scene.
    void Update(float) override {
        if (!gameObject || !gameObject->scene()) return;
        Pump(*gameObject->scene(), Input::MousePosition(),
             Input::GetMouseButton(0), Input::GetMouseButtonDown(0));
    }

    /// Update state from an explicit pointer (used by tools/tests). `down` is the
    /// button held this frame; `pressed` is the press edge.
    void Pump(Scene& scene, const Vec2& pointer, bool down, bool pressed) {
        m_pointer  = pointer;
        m_hovered  = UIRaycast(scene, pointer);
        if (pressed) {
            m_pressed  = m_hovered;
            m_selected = m_hovered;     // clicking empty space clears selection
        }
        if (!down) m_pressed = nullptr;
    }

    GameObject* Hovered()  const { return m_hovered; }
    GameObject* Pressed()  const { return m_pressed; }
    GameObject* Selected() const { return m_selected; }
    const Vec2& Pointer()  const { return m_pointer; }
    /// True when the cursor is over any UI widget — games use this to swallow
    /// world clicks that land on the HUD.
    bool IsPointerOverUI() const { return m_hovered != nullptr; }

    void Select(GameObject* go) { m_selected = go; }

private:
    Vec2        m_pointer{0.0f, 0.0f};
    GameObject* m_hovered  = nullptr;
    GameObject* m_pressed  = nullptr;
    GameObject* m_selected = nullptr;
};

} // namespace okay
