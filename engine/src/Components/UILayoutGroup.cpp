#include "okay/Components/UILayoutGroup.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"

namespace okay {

void UILayoutGroup::Arrange() {
    if (!gameObject || !gameObject->transform) return;
    float cursor = padding;              // Vertical/Horizontal running offset
    int   cols = columns < 1 ? 1 : columns;
    int   gi = 0;                        // Grid child index
    float gridX = padding, gridY = padding, rowH = 0.0f, gridW = padding; // Grid cursors
    for (Transform* child : gameObject->transform->Children()) {
        if (!child->gameObject || !child->gameObject->active) continue;
        UIRect r = GetUIRect(child->gameObject);
        if (!r.valid || !r.position || !r.anchorPtr) continue;
        if (direction == Direction::Vertical) {
            *r.position = {origin.x, origin.y + cursor};
            cursor += r.size.y + spacing;
        } else if (direction == Direction::Horizontal) {
            *r.position = {origin.x + cursor, origin.y};
            cursor += r.size.x + spacing;
        } else {                          // Grid: row-major, `columns` per row
            int col = gi % cols;
            if (col == 0 && gi > 0) { gridY += rowH + spacingY; gridX = padding; rowH = 0.0f; }
            *r.position = {origin.x + gridX, origin.y + gridY};
            gridX += r.size.x + spacing;
            if (gridX - spacing > gridW) gridW = gridX - spacing;   // widest row
            if (r.size.y > rowH) rowH = r.size.y;
            ++gi;
        }
        *r.anchorPtr = anchor;            // GetUIRect exposes the real anchor pointer
    }
    if (direction == Direction::Grid) m_contentSize = gridY + rowH;
    else                              m_contentSize = cursor > 0.0f ? cursor - spacing : 0.0f;
}

} // namespace okay
