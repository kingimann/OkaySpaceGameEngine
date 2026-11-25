#pragma once
// The ported Inspector parts stacked the way the editor's host stacks them inside its Dear ImGui window (editor/OkayPanels.cpp: one
// placeholder per part, Dear ImGui's item spacing after each): the header card, the Transform, each component's header bar (the Mesh
// Renderer's shading fields under its bar when it is open), the divider and Add Component. Used by the OkayUI test and the comparison
// with Dear ImGui.
#include "InspectorFixtures.hpp"
#include "OkayUIEditorPanels.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace inspfix {

namespace S = okayuiscreens;
namespace UIs = OkayUI;

struct StackResult {
    S::InspectorHeaderResult header;
    S::TransformResult transform;
    std::vector<S::CompHeaderResult> comps;
    S::MeshRendererResult mesh;
    bool meshDrawn = false;
    S::AddComponentResult add;
    P::MeshRendererValues meshValues;   // the values the mesh part edited this frame
    P::TransformModel transformModel;
    float bottom = 0.0f;
};

/// One frame of the stack at (x, y), `w` wide, with the Inspector's spacing; intents are carried out on `s` afterwards.
inline StackResult DrawStack(Scene& s, float x, float y, float w, float h, const InspectorMetrics& im = InspectorMetrics{}) {
    StackResult r;
    S::EditorMetrics em;
    em.itemSpacing = UIs::Vec2{im.itemX, im.itemY};
    em.framePadding = UIs::Vec2{im.frameX, im.frameY};
    em.innerSpacing = UIs::Vec2{im.innerX, im.innerY};
    S::SetEditorMetrics(em);
    float cy = y;
    P::InspectorHeaderModel hm = s.Header();
    r.header = S::DrawInspectorHeader(hm, s.header, x, cy, w, h - (cy - y));
    cy += r.header.contentHeight + im.itemY;   // the card, then Dear ImGui's spacing after it
    cy += im.itemY;                            // ImGui::Spacing()
    em.itemWidth = InspectorItemWidth(w);
    S::SetEditorMetrics(em);
    const float bleed = 8.0f;                  // half the window's 16-pixel padding
    r.transformModel = s.Transform();
    r.transform = S::DrawTransform(r.transformModel, bleed, x, cy, w, h - (cy - y));
    cy += r.transform.contentHeight + im.itemY;
    Obj& o = s.Shown();
    for (std::size_t i = 0; i < o.comps.size(); ++i) {
        char id[48];
        std::snprintf(id, sizeof id, "CompHeader%zu##okayui", i);
        const P::CompHeaderModel cm = s.CompHeader(i);
        r.comps.push_back(S::DrawComponentHeader(cm, id, bleed, x, cy, w, h - (cy - y)));
        cy += r.comps.back().contentHeight + im.itemY;
        if (i == 0 && o.hasMesh && o.comps[0].open) {
            P::MeshRendererModel mm = s.Mesh(r.meshValues);
            r.mesh = S::DrawMeshRenderer(mm, x, cy, w, h - (cy - y));
            r.meshDrawn = true;
            cy += r.mesh.contentHeight + im.itemY;
        }
    }
    // ImGui::Spacing(), Separator(), Spacing() before the button. The divider itself stays Dear ImGui's in the editor (it is not part of a
    // ported section), so this OkayUI picture leaves it out: only its room is kept.
    cy += im.itemY + 1.0f + im.itemY + im.itemY;
    r.add = S::DrawAddComponentButton(P::HierCol(60, 121, 204), x, cy, w, h - (cy - y));
    cy += r.add.contentHeight;
    r.bottom = cy;
    S::SetEditorMetrics(S::EditorMetrics{});
    return r;
}

/// Carries out what the stack's parts asked for, in the editor's order.
inline void ApplyStack(Scene& s, const StackResult& r) {
    s.Apply(r.header.intent);
    s.Apply(r.transform.intent, r.transformModel);
    if (r.meshDrawn) s.Apply(r.mesh.intent, r.meshValues);
    for (std::size_t i = r.comps.size(); i-- > 0;) s.Apply(i, r.comps[i].intent);   // from the end: a removal does not move the others
    if (r.add.open) ++s.addComponentOpened;
}

} // namespace inspfix
