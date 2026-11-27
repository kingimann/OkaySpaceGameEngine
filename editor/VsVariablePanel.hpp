#pragma once
#include "VisualScriptPanelWords.hpp"
// A Visual Script value edited with Dear ImGui (the Inspector's exposed graph variables, editor/main.cpp): numbers, vectors, colours,
// objects, lists, maps and structs, and a type picker. The Visual Script app edits the same values with OkayUI
// (shared/VisualScriptAppFields.cpp); the rules (VsExposedValue, VsVarTypeChoices, VsSetDeclDefaultInText) are in VsEditorActions.hpp.
#include "imgui.h"
#include "okay/VisualScript/VsDecl.hpp"
#include "VisualScriptModel.hpp"   // VsVarTypeChoices
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace okay::editor::vsed {

/// A type picker: the fixed types, then the graph's structs. True when the choice changed.
inline bool VsTypeCombo(const char* id, std::string& type, const std::vector<std::string>& structNames, bool structsToo) {
    bool changed = false;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo(id, type.c_str())) {
        for (const auto& t : VsVarTypeChoices())
            if (ImGui::Selectable(t.c_str(), type == t)) { type = t; changed = true; }
        if (structsToo && !structNames.empty()) {
            ImGui::Separator();
            for (const auto& s : structNames)
                if (ImGui::Selectable(s.c_str(), type == s)) { type = s; changed = true; }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/// Edit any value in place with the widget its type calls for. Lists and maps show
/// their items with a remove button each and an add button; structs show their fields.
/// True when something changed.
inline bool VsEditValue(const char* label, const okay::vs::VarType& t, okay::vs::VsValue& v,
                        const std::vector<okay::vs::StructDef>& structs, int depth = 0, float reserve = 1.0f) {
    using namespace okay::vs;
    bool changed = false;
    if (depth > 6) { ImGui::TextDisabled("(nested too deep to edit here)"); return false; }
    ImGui::PushID(label);
    if (t.IsStruct()) {
        const StructDef* sd = FindStruct(structs, t.structName);
        if (!sd) ImGui::TextDisabled("unknown struct %s", t.structName.c_str());
        else if (!v.IsMap()) { v = VsDefaultFor(t, structs); changed = true; }
        else if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen, "%s  (%s)", label, t.structName.c_str())) {
            auto m = v.AsMap();
            for (const auto& f : sd->fields) {
                auto it = m->find(f.name);
                if (it == m->end()) it = m->emplace(f.name, VsDeepCopy(f.def)).first;
                changed |= VsEditValue(f.name.c_str(), f.type, it->second, structs, depth + 1);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        return changed;
    }
    switch (t.base) {
        case PinType::Number: {
            float f = v.AsFloat();
            ImGui::SetNextItemWidth(-reserve);
            if (ImGui::DragFloat(("##" + std::string(label)).c_str(), &f, 0.05f)) { v = VsValue(f); changed = true; }
            break;
        }
        case PinType::Bool: {
            bool b = v.AsBool();
            if (ImGui::Checkbox(("##" + std::string(label)).c_str(), &b)) { v = VsValue(b); changed = true; }
            break;
        }
        case PinType::Vec3: {
            Vec3 p = v.AsVec3(); float a[3] = {p.x, p.y, p.z};
            ImGui::SetNextItemWidth(-reserve);
            if (ImGui::DragFloat3(("##" + std::string(label)).c_str(), a, 0.05f)) { v = VsValue(Vec3{a[0], a[1], a[2]}); changed = true; }
            break;
        }
        case PinType::Color: {
            const VsColor c = v.AsColor(); float a[4] = {c.r, c.g, c.b, c.a};
            ImGui::SetNextItemWidth(-reserve);
            if (ImGui::ColorEdit4(("##" + std::string(label)).c_str(), a)) { v = VsValue::Color(a[0], a[1], a[2], a[3]); changed = true; }
            break;
        }
        case PinType::Text:
        case PinType::Object: {
            char buf[512];
            std::snprintf(buf, sizeof(buf), "%s", v.IsObject() ? "" : v.AsString().c_str());
            ImGui::SetNextItemWidth(-reserve);
            const bool obj = t.base == PinType::Object;
            if (ImGui::InputTextWithHint(("##" + std::string(label)).c_str(), obj ? "object name or @guid:..." : "", buf, sizeof(buf))) {
                v = (obj && !buf[0]) ? VsValue::Object(0) : VsValue(std::string(buf)); changed = true;
            }
            break;
        }
        case PinType::List: {
            if (!v.IsArray()) { v = VsValue::MakeArray(); changed = true; }
            auto a = v.AsArray();
            if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen, "%s  (%d item%s)", label, (int)a->size(), a->size() == 1 ? "" : "s")) {
                int removeAt = -1;
                for (std::size_t i = 0; i < a->size(); ++i) {
                    ImGui::PushID((int)i);
                    VarType et; et.base = t.elem;
                    if (et.base == PinType::Any) {   // untyped list: edit each item as what it is
                        const VsValue& e = (*a)[i];
                        et.base = e.IsNumber() ? PinType::Number : e.IsBool() ? PinType::Bool : e.IsVec3() ? PinType::Vec3
                                : e.IsColor() ? PinType::Color : e.IsArray() ? PinType::List : e.IsMap() ? PinType::Map : PinType::Text;
                    }
                    const std::string row = "[" + std::to_string(i) + "]";
                    ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%s", row.c_str()); ImGui::SameLine();
                    ImGui::BeginGroup();
                    changed |= VsEditValue(row.c_str(), et, (*a)[i], structs, depth + 1, 34.0f);
                    ImGui::EndGroup();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) removeAt = (int)i;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this item");
                    ImGui::PopID();
                }
                if (removeAt >= 0) { a->erase(a->begin() + removeAt); changed = true; }
                if (t.elem == PinType::Any) {
                    static const struct { const char* n; PinType p; } kinds[] = {{"Number", PinType::Number}, {"Bool", PinType::Bool},
                        {"Text", PinType::Text}, {"Vector3", PinType::Vec3}, {"Color", PinType::Color}};
                    if (ImGui::SmallButton("+ Add item")) ImGui::OpenPopup("addkind");
                    if (ImGui::BeginPopup("addkind")) {
                        for (const auto& k : kinds) if (ImGui::MenuItem(k.n)) {
                            VarType kt; kt.base = k.p; a->push_back(VsDefaultFor(kt, structs)); changed = true;
                        }
                        ImGui::EndPopup();
                    }
                } else if (ImGui::SmallButton("+ Add item")) {
                    VarType et; et.base = t.elem; a->push_back(VsDefaultFor(et, structs)); changed = true;
                }
                ImGui::TreePop();
            }
            break;
        }
        case PinType::Map: {
            if (!v.IsMap()) { v = VsValue::MakeMap(); changed = true; }
            auto m = v.AsMap();
            if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen, "%s  (%d entr%s)", label, (int)m->size(), m->size() == 1 ? "y" : "ies")) {
                std::string removeKey; bool doRemove = false;
                for (auto& kv : *m) {
                    ImGui::PushID(kv.first.c_str());
                    VarType et; et.base = t.elem;
                    if (et.base == PinType::Any) {
                        const VsValue& e = kv.second;
                        et.base = e.IsNumber() ? PinType::Number : e.IsBool() ? PinType::Bool : e.IsVec3() ? PinType::Vec3
                                : e.IsColor() ? PinType::Color : e.IsArray() ? PinType::List : e.IsMap() ? PinType::Map : PinType::Text;
                    }
                    ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%s", kv.first.c_str()); ImGui::SameLine();
                    ImGui::BeginGroup();
                    changed |= VsEditValue(kv.first.c_str(), et, kv.second, structs, depth + 1, 34.0f);
                    ImGui::EndGroup();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) { removeKey = kv.first; doRemove = true; }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this entry");
                    ImGui::PopID();
                }
                if (doRemove) { m->erase(removeKey); changed = true; }
                static char newKey[64];
                ImGui::SetNextItemWidth(90);
                ImGui::InputTextWithHint("##newkey", "new key", newKey, sizeof(newKey));
                ImGui::SameLine();
                if (ImGui::SmallButton("+ Add entry") && newKey[0] && !m->count(newKey)) {
                    VarType et; et.base = t.elem == PinType::Any ? PinType::Number : t.elem;
                    (*m)[newKey] = VsDefaultFor(et, structs); newKey[0] = '\0'; changed = true;
                }
                ImGui::TreePop();
            }
            break;
        }
        default: {
            ImGui::TextDisabled("%s", VsDescribe(v).c_str());
            break;
        }
    }
    ImGui::PopID();
    return changed;
}

} // namespace okay::editor::vsed
