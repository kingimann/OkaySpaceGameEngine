#pragma once
// The Visual Script editor's variable declarations: add a typed variable, pick its
// scope, edit its default (numbers, vectors, colours, objects, lists, maps, structs)
// and define struct types. The model lives in VsEditGraph (declare, setVarType...);
// this is the ImGui for it. Header-only, included by VisualScriptEditor.hpp.
#include "imgui.h"
#include "okay/VisualScript/VsDecl.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace okay::editor::vsed {

/// An old `var` row (or a typed one): its default as a value of its type.
inline okay::vs::VsValue VsExposedValue(const std::string& type, const std::string& value, bool typed,
                                        const std::vector<okay::vs::StructDef>& structs, okay::vs::VarType& vt) {
    using namespace okay::vs;
    if (!ParseVarType(type, structs, vt)) vt = VarType{};
    VsValue v;
    if (typed) {
        if (VsParseTyped(vt, value, structs, v)) return v;
        return VsDefaultFor(vt, structs);
    }
    if (vt.base == PinType::Number) { float f = 0; VsParseFinite(value, f); return VsValue(f); }
    if (vt.base == PinType::Bool) return VsValue(value == "true");
    return VsValue(value);
}

inline const std::vector<std::string>& VsVarTypeChoices() {
    static const std::vector<std::string> c = {
        "Number", "Bool", "Text", "Vector3", "Color", "Object", "List", "Map",
        "List<Number>", "List<Text>", "List<Bool>", "List<Vector3>", "List<Color>", "List<Object>",
        "Map<Number>", "Map<Text>", "Map<Bool>", "Map<Vector3>", "Map<Color>", "Map<Object>"};
    return c;
}

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

/// A graph's text with variable `name` (in `scope`) given the starting value `literal`. An old
/// `var` line becomes a typed `decl` line with the type its value had. Everything else in the
/// text is kept byte for byte. False when no such declaration is in `text`.
inline bool VsSetDeclDefaultInText(const std::string& text, const std::string& name, okay::vs::VarScope scope,
                                   const std::string& literal, const std::string& typeOfLegacy, std::string& out) {
    std::string result;
    bool done = false;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        const bool last = nl == std::string::npos;
        const std::string line = text.substr(pos, last ? std::string::npos : nl - pos);
        std::string repl = line;
        if (!done) {
            const std::vector<std::string> t = okay::vs::TokenizeLine(line);
            auto unq = [](std::string x) { if (x.size() >= 2 && x.front() == '"' && x.back() == '"') x = x.substr(1, x.size() - 2); return x; };
            if (t.size() >= 4 && t[0] == "decl" && unq(t[1]) == name) {
                okay::vs::VarScope sc;
                if (okay::vs::VarScopeFromName(unq(t[3]), sc) && sc == scope) {
                    repl = "decl " + name + " " + okay::vs::QuoteIfNeeded(unq(t[2])) + " " + okay::vs::VarScopeName(sc) + " " + okay::vs::QuoteText(literal);
                    done = true;
                }
            } else if (t.size() >= 2 && t[0] == "var" && unq(t[1]) == name && scope == okay::vs::VarScope::Object) {
                repl = "decl " + name + " " + typeOfLegacy + " object " + okay::vs::QuoteText(literal);
                done = true;
            }
        }
        result += repl;
        if (last) break;
        result += '\n';
        pos = nl + 1;
    }
    if (done) out = result;
    return done;
}

/// The Variables section of the sidebar: declarations, defaults and struct types.
template <class Graph>
inline void VsDrawVariableDecls(Graph& g) {
    using namespace okay::vs;
    static std::string msg;      // the last refusal ("there is already a ... variable")
    static char newName[64] = "";
    static std::string newType = "Number";
    static int newScope = 0;
    static char newStruct[64] = "";
    static char newField[64] = "";
    static std::string newFieldType = "Number";
    static int fieldStruct = -1;
    static const char* kScopes[] = {"Object (this graph)", "Scene", "Global"};
    static const char* kScopeNames[] = {"object", "scene", "global"};

    ImGui::Spacing();
    ImGui::TextUnformatted("Declared variables");
    ImGui::Separator();
    ImGui::TextWrapped("Give a variable a type and a starting value. Object variables belong to this graph, scene ones are shared by every graph in the scene, global ones last for the whole run.");
    if (!g.declError().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        ImGui::TextWrapped("A declaration doesn't load: %s", g.declError().c_str());
        ImGui::PopStyleColor();
    }
    std::vector<std::string> structNames;
    for (const auto& s : g.structs) structNames.push_back(s.name);

    int removeAt = -1;
    for (std::size_t i = 0; i < g.vars.size(); ++i) {
        auto& e = g.vars[i];
        ImGui::PushID((int)i + 910000);
        ImGui::Separator();
        // Name (a rename also renames the nodes that use it), scope, remove.
        char nb[64]; std::snprintf(nb, sizeof(nb), "%s", e.name.c_str());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
        if (ImGui::InputText("##vname", nb, sizeof(nb), ImGuiInputTextFlags_EnterReturnsTrue) || (ImGui::IsItemDeactivatedAfterEdit() && std::string(nb) != e.name)) {
            std::string err; msg = g.renameVar(i, nb, &err) ? "" : err;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The variable's name. Press Enter to rename; nodes that use it follow.");
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeAt = (int)i;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this declaration (nodes that use the name keep working, untyped)");
        int sc = e.scope == "scene" ? 1 : e.scope == "global" ? 2 : 0;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##vscope", &sc, kScopes, 3)) { std::string err; msg = g.setVarScope(i, kScopeNames[sc], &err) ? "" : err; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Who sees it: this graph's object, every graph in the scene, or the whole game run.");
        std::string ty = e.type;
        if (VsTypeCombo("##vtype", ty, structNames, true)) { std::string err; msg = g.setVarType(i, ty, &err) ? "" : err; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The variable's type. A Set node that sends a value that doesn't fit is reported and the variable keeps its value.");
        // The default, edited with the widget for its type.
        VarType vt;
        VsValue val = VsExposedValue(e.type, e.value, e.typed, g.structDefs(), vt);
        if (VsEditValue(e.name.c_str(), vt, val, g.structDefs())) {
            e.value = e.typed ? VsToLiteral(val) : (val.IsString() ? val.AsString() : VsToLiteral(val));
            if (!e.typed) { e.typed = true; e.value = VsToLiteral(val); }
            g.declsChanged();
        }
        // The same default as text, for pasting or typing a whole list at once.
        if (vt.base == PinType::List || vt.base == PinType::Map || vt.IsStruct() || vt.base == PinType::Object) {
            char lb[1024]; std::snprintf(lb, sizeof(lb), "%s", e.typed ? e.value.c_str() : VsToLiteral(val).c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##vlit", "default as text, e.g. [1, 2, 3]", lb, sizeof(lb), ImGuiInputTextFlags_EnterReturnsTrue)) {
                std::string err; msg = g.setVarDefault(i, lb, &err) ? "" : "default: " + err;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Edit the default as text and press Enter. Lists [a, b], maps {key: v}, vectors (x, y, z), colours #rrggbb.");
        }
        ImGui::PopID();
    }
    if (removeAt >= 0) g.removeVar((std::size_t)removeAt);

    ImGui::Separator();
    ImGui::TextUnformatted("New variable");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##newvar", "name, e.g. health", newName, sizeof(newName));
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##newscope", &newScope, kScopes, 3);
    VsTypeCombo("##newtype", newType, structNames, true);
    if (ImGui::Button("Add variable##decl", ImVec2(-1, 0))) {
        std::string err;
        if (g.declare(newName, newType, kScopeNames[newScope], &err)) { newName[0] = '\0'; msg.clear(); }
        else msg = err;
    }
    if (!msg.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.45f, 1.0f));
        ImGui::TextWrapped("%s", msg.c_str());
        ImGui::PopStyleColor();
    }

    // ---- Struct types ----
    ImGui::Spacing();
    ImGui::TextUnformatted("Structs");
    ImGui::Separator();
    ImGui::TextWrapped("A struct is a record with named, typed fields (Enemy: health, position, tint). Declare one, then pick its name as a variable's type.");
    int removeStruct = -1;
    for (std::size_t si = 0; si < g.structs.size(); ++si) {
        auto& st = g.structs[si];
        ImGui::PushID((int)si + 950000);
        const std::string head = st.name + "  (" + std::to_string(st.fields.size()) + " field" + (st.fields.size() == 1 ? "" : "s") + ")";
        if (ImGui::TreeNodeEx(head.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            int removeField = -1;
            for (std::size_t fi = 0; fi < st.fields.size(); ++fi) {
                auto& f = st.fields[fi];
                ImGui::PushID((int)fi);
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(f.name.c_str());
                ImGui::SameLine(); if (ImGui::SmallButton("x")) removeField = (int)fi;
                std::string fty = f.type;
                std::vector<std::string> before;
                for (std::size_t k = 0; k < si; ++k) before.push_back(g.structs[k].name);
                if (VsTypeCombo("##ftype", fty, before, true)) {
                    // A struct field can only use structs declared above it.
                    std::vector<StructDef> names; for (const auto& b : before) names.push_back({b, {}});
                    VarType probe; std::string err;
                    if (ParseVarType(fty, names, probe, &err)) { f.type = VarTypeText(probe); f.value.clear(); g.declsChanged(); msg.clear(); }
                    else msg = err;
                }
                char fb[256]; std::snprintf(fb, sizeof(fb), "%s", f.value.c_str());
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputTextWithHint("##fdef", "default (blank = type's default)", fb, sizeof(fb))) { f.value = fb; g.declsChanged(); }
                ImGui::PopID();
            }
            if (removeField >= 0) g.removeStructField(si, (std::size_t)removeField);
            if (fieldStruct != (int)si) { /* add-field row is per struct */ }
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
            ImGui::InputTextWithHint("##nfield", "field name", newField, sizeof(newField));
            ImGui::SameLine();
            if (ImGui::SmallButton("+ field")) {
                std::string err;
                if (g.addStructField(si, newField, "Number", &err)) { newField[0] = '\0'; msg.clear(); } else msg = err;
            }
            if (ImGui::SmallButton("Remove struct")) removeStruct = (int)si;
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeStruct >= 0) g.removeStruct((std::size_t)removeStruct);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    ImGui::InputTextWithHint("##nstruct", "new struct name", newStruct, sizeof(newStruct));
    ImGui::SameLine();
    if (ImGui::SmallButton("+ struct")) {
        std::string err;
        if (g.declareStruct(newStruct, &err)) { newStruct[0] = '\0'; msg.clear(); } else msg = err;
    }
    (void)newFieldType;
}

} // namespace okay::editor::vsed
