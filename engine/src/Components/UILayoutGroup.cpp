#include "okay/Components/UILayoutGroup.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIFlexItem.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <vector>

namespace okay {

namespace {
struct FlexChild {
    UIRect rect;
    UIFlexItem* item = nullptr;
    float main = 0.0f, cross = 0.0f;   // laid-out size along / across the line
};
// Flex layout in the box's own frame (children of a layout group resolve inside its
// rect): main axis = x for a row, y for a column.
void ArrangeFlex(UILayoutGroup& lg, float& contentSize) {
    const bool row = lg.direction == UILayoutGroup::Direction::FlexRow;
    const float boxMain = (row ? lg.boxSize.x : lg.boxSize.y) - lg.padding * 2.0f;
    const float boxCross = (row ? lg.boxSize.y : lg.boxSize.x) - lg.padding * 2.0f;
    static thread_local std::vector<FlexChild> kids;
    kids.clear();
    for (Transform* child : lg.gameObject->transform->Children()) {
        if (!child->gameObject || !child->gameObject->active) continue;
        FlexChild k;
        k.rect = GetUIRect(child->gameObject);
        if (!k.rect.valid || !k.rect.position || !k.rect.anchorPtr) continue;
        k.item = child->gameObject->GetComponent<UIFlexItem>();
        const float ownMain = row ? k.rect.size.x : k.rect.size.y, ownCross = row ? k.rect.size.y : k.rect.size.x;
        if (k.item && k.rect.sizePtr) {   // resizable: grow/shrink start from the base size, taken once
            if (k.item->baseSize.x <= 0.0f) k.item->baseSize.x = k.rect.size.x;
            if (k.item->baseSize.y <= 0.0f) k.item->baseSize.y = k.rect.size.y;
            k.main = row ? k.item->baseSize.x : k.item->baseSize.y;
            k.cross = row ? k.item->baseSize.y : k.item->baseSize.x;
        } else {
            k.item = nullptr;             // no flex item (or a fixed-size widget): positioned only
            k.main = ownMain;
            k.cross = ownCross;
        }
        kids.push_back(k);
    }
    float crossCursor = 0.0f;
    std::size_t first = 0;
    while (first < kids.size()) {
        // One line: as many children as fit (all of them without wrap).
        std::size_t last = first;
        float used = kids[first].main;
        while (last + 1 < kids.size() && (!lg.wrap || used + lg.spacing + kids[last + 1].main <= boxMain)) {
            ++last;
            used += lg.spacing + kids[last].main;
        }
        // Hand out the spare room (grow) or take back the overflow (shrink).
        float free = boxMain - used, growSum = 0.0f, shrinkSum = 0.0f;
        for (std::size_t i = first; i <= last; ++i)
            if (kids[i].item) { growSum += kids[i].item->grow; shrinkSum += kids[i].item->shrink * kids[i].main; }
        if (free > 0.0f && growSum > 0.0f) {
            for (std::size_t i = first; i <= last; ++i)
                if (kids[i].item) kids[i].main += free * kids[i].item->grow / growSum;
            free = 0.0f;
        } else if (free < 0.0f && shrinkSum > 0.0f) {
            for (std::size_t i = first; i <= last; ++i)
                if (kids[i].item)
                    kids[i].main = std::max(0.0f, kids[i].main + free * kids[i].item->shrink * kids[i].main / shrinkSum);
            free = 0.0f;
        }
        const std::size_t n = last - first + 1;
        float lead = 0.0f, gap = lg.spacing;
        if (free > 0.0f) {
            switch (lg.justify) {
                case UILayoutGroup::Justify::Start: break;
                case UILayoutGroup::Justify::Center: lead = free * 0.5f; break;
                case UILayoutGroup::Justify::End: lead = free; break;
                case UILayoutGroup::Justify::SpaceBetween: if (n > 1) gap += free / (float)(n - 1); break;
                case UILayoutGroup::Justify::SpaceAround: gap += free / (float)n; lead = free / (float)n * 0.5f; break;
                case UILayoutGroup::Justify::SpaceEvenly: gap += free / (float)(n + 1); lead = free / (float)(n + 1); break;
            }
        }
        // The line is as thick as its thickest child; a single unwrapped line fills the box.
        float lineCross = 0.0f;
        for (std::size_t i = first; i <= last; ++i) lineCross = std::max(lineCross, kids[i].cross);
        if (!lg.wrap) lineCross = std::max(lineCross, boxCross);
        float mainCursor = lead;
        for (std::size_t i = first; i <= last; ++i) {
            FlexChild& k = kids[i];
            UILayoutGroup::Align a = lg.align;
            if (k.item && k.item->alignSelf != UIFlexItem::Align::Auto)
                a = (UILayoutGroup::Align)((int)k.item->alignSelf - 1);
            if (a == UILayoutGroup::Align::Stretch && k.item) k.cross = lineCross;
            float off = 0.0f;
            if (a == UILayoutGroup::Align::Center) off = (lineCross - k.cross) * 0.5f;
            else if (a == UILayoutGroup::Align::End) off = lineCross - k.cross;
            const float mx = lg.padding + mainCursor, cx = lg.padding + crossCursor + off;
            *k.rect.position = row ? Vec2{mx, cx} : Vec2{cx, mx};
            *k.rect.anchorPtr = UIAnchor::TopLeft;
            if (k.item) *k.rect.sizePtr = row ? Vec2{k.main, k.cross} : Vec2{k.cross, k.main};
            mainCursor += k.main + gap;
        }
        crossCursor += lineCross + lg.spacingY;
        first = last + 1;
    }
    contentSize = crossCursor > 0.0f ? crossCursor - lg.spacingY + lg.padding * 2.0f : 0.0f;
}
} // namespace

void UILayoutGroup::Arrange() {
    if (!gameObject || !gameObject->transform) return;
    if (IsFlex()) { ArrangeFlex(*this, m_contentSize); return; }
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
