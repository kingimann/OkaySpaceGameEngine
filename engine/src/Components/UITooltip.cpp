#include "okay/Components/UITooltip.hpp"
#include "okay/Components/UIElement.hpp"   // GetUIRect (collapses any widget)
#include "okay/Scene/GameObject.hpp"
#include "okay/Input/Input.hpp"

namespace okay {

void UITooltip::Update(float dt) {
    if (!gameObject) { m_inside = false; m_hover = 0.0f; return; }
    UIRect r = GetUIRect(gameObject);
    if (!r.valid) { m_inside = false; m_hover = 0.0f; return; }

    // Where the widget is really drawn (the host's rectOf: inside its UI parent, at
    // the Canvas scale), else its anchor against the whole canvas.
    Vec2 o, sz;
    if (!(UIWorld().rectOf && UIWorld().rectOf(gameObject, o, sz))) {
        o = ResolveAnchor(r.anchor, r.position ? *r.position : Vec2{}, r.size);
        sz = r.size;
    }
    Vec2 m = Input::MousePosition();
    m_inside = m.x >= o.x && m.y >= o.y && m.x <= o.x + sz.x && m.y <= o.y + sz.y;

    if (m_inside) m_hover += dt;
    else          m_hover = 0.0f;
}

} // namespace okay
