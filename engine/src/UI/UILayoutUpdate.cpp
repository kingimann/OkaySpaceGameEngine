#include "okay/UI/UILayoutUpdate.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Scene/Scene.hpp"
#include <vector>

namespace okay {

void UpdateUILayouts(Scene& scene) {
    // List views create their pooled rows (new scene objects), so collect them
    // before laying any out.
    static thread_local std::vector<UIListView*> lists;
    lists.clear();
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (auto* lg = go->GetComponent<UILayoutGroup>())
            if (lg->IsFlex()) lg->Arrange();
        if (auto* sv = go->GetComponent<UIScrollView>())
            if (sv->autoContent) {
                const float ch = ScrollViewContentHeight(go), cw = ScrollViewContentWidth(go);
                sv->contentHeight = ch > sv->size.y ? ch : sv->size.y;
                sv->contentWidth = cw > sv->size.x ? cw : sv->size.x;
            }
        if (auto* lv = go->GetComponent<UIListView>()) lists.push_back(lv);
    }
    for (UIListView* lv : lists) lv->Layout();
}

} // namespace okay
