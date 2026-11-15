#pragma once
// While one of these lives, every Dear ImGui widget id is mixed with the address of one component, so the cards of two components of the
// same kind (a duplicate, a second collider, two Volumes) never share an id.
//
// The hand-drawn cards of the Inspector name their widgets by label ("Size##bc"). Two cards of one kind drawn in the same id scope give
// the same id to the same label, and ImGui treats widgets with one id as one widget: hover and drag state are shared, so dragging the
// copy's box also moved the original's. editor/main.cpp puts one of these around the whole card of every component in the Inspector's
// component loop; tests/test_inspector_imgui.cpp runs it inside a window-less ImGui context.
#include "imgui.h"

namespace oked {

class ComponentIdScope {
public:
    explicit ComponentIdScope(const void* component) { ImGui::PushID(component); }
    ~ComponentIdScope() { ImGui::PopID(); }
    ComponentIdScope(const ComponentIdScope&) = delete;
    ComponentIdScope& operator=(const ComponentIdScope&) = delete;
};

}   // namespace oked
