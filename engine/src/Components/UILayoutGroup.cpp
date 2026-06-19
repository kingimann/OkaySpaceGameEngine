#include "okay/Components/UILayoutGroup.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"

namespace okay {

void UILayoutGroup::Arrange() {
    if (!gameObject || !gameObject->transform) return;
    float cursor = padding;
    for (Transform* child : gameObject->transform->Children()) {
        if (!child->gameObject || !child->gameObject->active) continue;
        UIRect r = GetUIRect(child->gameObject);
        if (!r.valid || !r.position) continue;
        if (direction == Direction::Vertical) {
            *r.position = {origin.x, origin.y + cursor};
            cursor += r.size.y + spacing;
        } else {
            *r.position = {origin.x + cursor, origin.y};
            cursor += r.size.x + spacing;
        }
        // GetUIRect returns a copy's anchor; write the real one too.
        if (auto* b = child->gameObject->GetComponent<UIButton>())   b->anchor = anchor;
        if (auto* p = child->gameObject->GetComponent<UIPanel>())    p->anchor = anchor;
        if (auto* im = child->gameObject->GetComponent<UIImage>())   im->anchor = anchor;
        if (auto* sl = child->gameObject->GetComponent<UISlider>())  sl->anchor = anchor;
        if (auto* tg = child->gameObject->GetComponent<UIToggle>())  tg->anchor = anchor;
        if (auto* pb = child->gameObject->GetComponent<UIProgressBar>()) pb->anchor = anchor;
        if (auto* tr = child->gameObject->GetComponent<TextRenderer>())  tr->anchor = anchor;
    }
    m_contentSize = cursor > 0.0f ? cursor - spacing : 0.0f;
}

} // namespace okay
