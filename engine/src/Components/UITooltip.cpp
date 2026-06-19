#include "okay/Components/UITooltip.hpp"
#include "okay/Components/UIElement.hpp"   // GetUIRect (collapses any widget)
#include "okay/Scene/GameObject.hpp"
#include "okay/Input/Input.hpp"

namespace okay {

void UITooltip::Update(float dt) {
    if (!gameObject) { m_inside = false; m_hover = 0.0f; return; }
    UIRect r = GetUIRect(gameObject);
    if (!r.valid) { m_inside = false; m_hover = 0.0f; return; }

    // Resolve the widget's screen rect against the global canvas, then test the
    // mouse against it (same convention the widgets' own Contains() use).
    Vec2 o = ResolveAnchor(r.anchor, r.position ? *r.position : Vec2{}, r.size);
    Vec2 m = Input::MousePosition();
    m_inside = m.x >= o.x && m.y >= o.y && m.x <= o.x + r.size.x && m.y <= o.y + r.size.y;

    if (m_inside) m_hover += dt;
    else          m_hover = 0.0f;
}

} // namespace okay
