#include "okay/Components/UIListView.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <cmath>

namespace okay {

namespace {
void CallScript(GameObject* go, const char* fn) {
    if (!go) return;
    if (auto* sc = go->GetComponent<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEvent(fn);
}

// The text a row shows: its first screen-space Text in the subtree, else a button label.
std::string* RowText(GameObject* row) {
    if (auto* tr = row->GetComponent<TextRenderer>())
        if (tr->screenSpace) return &tr->text;
    if (auto* b = row->GetComponent<UIButton>()) {
        if (GameObject* t = UIButtonTextChild(row)) return &t->GetComponent<TextRenderer>()->text;
        return &b->label;
    }
    for (Transform* c : row->transform->Children())
        if (c->gameObject)
            if (std::string* s = RowText(c->gameObject)) return s;
    return nullptr;
}
} // namespace

float UIListView::ContentHeight() const {
    const int n = Count();
    return n > 0 ? n * Stride() - spacing : 0.0f;
}
float UIListView::ScrollMax() const { return Mathf::Max(0.0f, ContentHeight() - size.y); }
void UIListView::SetScroll(float v) { scroll = Mathf::Clamp(v, 0.0f, ScrollMax()); }
void UIListView::ScrollTo(int index) {
    if (index < 0 || index >= Count()) return;
    const float top = index * Stride(), bottom = top + itemHeight;
    if (top < scroll) SetScroll(top);
    else if (bottom > scroll + size.y) SetScroll(bottom - size.y);
}

GameObject* UIListView::Template() const {
    if (!gameObject || !gameObject->transform) return nullptr;
    for (Transform* c : gameObject->transform->Children())
        if (c->gameObject && !c->gameObject->transient) return c->gameObject;
    return nullptr;
}

void UIListView::Bind(GameObject* row, int index) {
    if (!items.empty() && index < (int)items.size())
        if (std::string* s = RowText(row)) *s = items[index];
    if (onBind) onBind(row, index);
    if (gameObject && gameObject->GetComponent<ScriptComponent>()) {
        Prefs::SetInt("ui_list_index", index);
        Prefs::SetString("ui_list_row", row->name);
        CallScript(gameObject, "on_list_bind");
    }
}

void UIListView::Layout() {
    if (!gameObject || !gameObject->transform || !gameObject->scene()) return;
    Scene& scene = *gameObject->scene();
    GameObject* tpl = Template();
    // A new or removed template (or a rebuild request): start the pool over.
    if (tpl != m_template || m_rebuild) {
        m_rebuild = false;
        for (GameObject* r : m_rows) if (r && !r->IsDestroyed()) scene.Destroy(r);
        m_rows.clear();
        m_rowIndex.clear();
        m_template = tpl;
        m_dirty = true;
    }
    if (!tpl) return;
    tpl->active = false;   // the template itself never shows
    SetScroll(scroll);
    // Pool: enough rows to cover the viewport while a row straddles each edge.
    const int n = Count();
    const float stride = Stride() > 1.0f ? Stride() : 1.0f;
    const int want = std::min(n, (int)std::ceil(size.y / stride) + 1);
    while ((int)m_rows.size() < want) {
        GameObject* row = scene.Instantiate(*tpl);
        if (!row) break;
        row->transform->SetParent(gameObject->transform, false);
        row->transient = true;
        row->name = tpl->name + " row " + std::to_string(m_rows.size());
        m_rows.push_back(row);
        m_rowIndex.push_back(-1);
    }
    // Place: pooled row k shows entry first + k, so each entry keeps one row
    // while it stays in view and only rows scrolled past get rebound.
    const int firstEntry = (int)(scroll / stride);
    for (std::size_t k = 0; k < m_rows.size(); ++k) {
        GameObject* row = m_rows[k];
        if (!row || row->IsDestroyed()) continue;
        const int pool = (int)m_rows.size();
        const int entry = firstEntry + (((int)k - firstEntry % pool) + pool) % pool;
        const bool show = entry < n && (int)k < want;
        row->active = show;
        if (!show) { m_rowIndex[k] = -1; continue; }
        UIRect r = GetUIRect(row);
        if (r.valid && r.position) {
            *r.position = Vec2{0.0f, entry * stride - scroll};
            if (r.anchorPtr) *r.anchorPtr = UIAnchor::TopLeft;
            if (r.sizePtr) r.sizePtr->x = size.x;   // rows span the list's width
        }
        if (m_dirty || m_rowIndex[k] != entry) {
            m_rowIndex[k] = entry;
            Bind(row, entry);
        }
    }
    m_dirty = false;
}

void UIListView::Update(float) {
    // Wheel scroll over the list; a click on a row's button selects that entry.
    Vec2 o = ResolveAnchor(anchor, position, size);
    Vec2 m = UIToLocal(gameObject, o, size, Input::MousePosition());
    const float wheel = Input::MouseWheel();
    if (wheel != 0.0f && m.x >= o.x && m.y >= o.y && m.x <= o.x + size.x && m.y <= o.y + size.y)
        SetScroll(scroll - wheel * wheelSpeed);
    for (std::size_t k = 0; k < m_rows.size(); ++k) {
        GameObject* row = m_rows[k];
        if (!row || row->IsDestroyed() || !row->active || m_rowIndex[k] < 0) continue;
        if (auto* b = row->GetComponent<UIButton>())
            if (b->WasClicked() && selected != m_rowIndex[k]) {
                selected = m_rowIndex[k];
                Prefs::SetInt("ui_list_selected", selected);
                CallScript(gameObject, "on_list_select");
            }
    }
}

} // namespace okay
