#include "InspectorPanel.hpp"
#include "EditUndo.hpp"
#include "EditorWidgets.hpp"
#include "ConsolePanel.hpp"
#include "InspectorModel.hpp"
#include "imgui_internal.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <unordered_set>

using namespace okay;

namespace oked {

namespace {

okay::editor::EditorState* s_ed = nullptr;

std::string LowerS(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool StartsWith(const char* s, const char* prefix) { return std::strncmp(s, prefix, std::strlen(prefix)) == 0; }

// Is this window the Inspector or the Hierarchy, or a popup / child opened from one?
bool InEditorWindow(const ImGuiWindow* w) {
    for (; w; w = w->ParentWindow)
        if (StartsWith(w->Name, "Inspector") || StartsWith(w->Name, "Hierarchy")) return true;
    return false;
}

const char* LabelEnd(const char* label) { return ImGui::FindRenderedTextEnd(label); }

// The label to the right of a widget, in the Inspector's usual style.
void FieldLabel(const char* label, const char* tooltip) {
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(label, LabelEnd(label));
    if (tooltip && *tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
}

void DropOutline(bool compatible, bool over) {
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const ImU32 col = compatible ? ImGui::GetColorU32(AccentCol(over ? 1.0f : 0.7f)) : IM_COL32(220, 90, 80, over ? 255 : 150);
    ImGui::GetWindowDrawList()->AddRect(mn, mx, col, 3.0f, 0, over ? 2.5f : 1.5f);
}

// Draws a small "x" inside the right end of a field frame; true when clicked.
bool ClearButton(const char* id) {
    const float fh = ImGui::GetFrameHeight();
    ImGui::SameLine(0, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    const bool hit = ImGui::Button(id, ImVec2(fh, fh));
    ImGui::PopStyleVar();
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    const float r = fh * 0.17f;
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 1.5f);
    dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, 1.5f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear");
    return hit;
}

} // namespace

// ---- Undo for every field ---------------------------------------------------------------------------------

void WatchEditUndo(okay::editor::EditorState& ed) {
    static EditUndo u;
    s_ed = &ed;
    if (ed.isPlaying()) { if (u.IsOpen()) u.Cancel(); return; }   // Play changes are thrown away anyway
    ImGuiContext& g = *GImGui;
    const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    const bool activeMine = g.ActiveId != 0 && InEditorWindow(g.ActiveIdWindow);
    if (!u.IsOpen()) {
        // The picture is taken on the press, before the widget changes anything.
        if ((clicked && InEditorWindow(g.HoveredWindow)) || activeMine) u.Begin(ed);
    } else if (!down && !activeMine) {
        u.End(ed);
    }
}

// ---- Reference fields -----------------------------------------------------------------------------------

void HighlightAssetDrop(const char* kind) {
    const ImGuiPayload* pl = ImGui::GetDragDropPayload();
    if (!pl || !pl->IsDataType("ASSET_PATH") || !pl->Data) return;
    const bool ok = AssetKindAccepts(kind ? kind : "", (const char*)pl->Data);
    DropOutline(ok, ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
}

namespace {

// The picker popup shared by file fields. `value` is changed on a pick. True when it was.
bool AssetPickerPopup(const char* popupId, std::string& value, const char* kind, const std::string& projectDir) {
    bool changed = false;
    static std::vector<std::string> files;
    static std::string root;
    static char filter[96] = "";
    ImGui::SetNextWindowSizeConstraints(ImVec2(320, 120), ImVec2(520, 420));
    if (ImGui::BeginPopup(popupId)) {
        if (ImGui::IsWindowAppearing()) {
            filter[0] = '\0';
            std::error_code ec;
            root = projectDir;
            const std::filesystem::path assets = std::filesystem::path(projectDir) / "Assets";
            files = ListAssetsOfKind(std::filesystem::is_directory(assets, ec) ? assets.string() : projectDir, kind);
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##find", "Search files...", filter, sizeof(filter));
        const std::string needle = LowerS(filter);
        if (ImGui::Selectable("None", value.empty())) { value.clear(); changed = true; ImGui::CloseCurrentPopup(); }
        ImGui::Separator();
        ImGui::BeginChild("##files", ImVec2(0, 260));
        int shown = 0;
        for (const std::string& f : files) {
            std::string rel = f;
            if (!root.empty() && f.rfind(root, 0) == 0) rel = f.substr(root.size() + (f.size() > root.size() && (f[root.size()] == '/' || f[root.size()] == '\\') ? 1 : 0));
            if (!needle.empty() && LowerS(rel).find(needle) == std::string::npos) continue;
            if (++shown > 400) { ImGui::TextDisabled("...type to narrow the list"); break; }
            if (ImGui::Selectable(rel.c_str(), f == value)) { value = f; changed = true; ImGui::CloseCurrentPopup(); }
        }
        if (shown == 0) ImGui::TextDisabled(projectDir.empty() ? "Open a project to pick files." : "No matching files in this project.");
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    return changed;
}

} // namespace

bool AssetField(const char* label, std::string& value, const char* kind, const std::string& projectDir,
                bool mixed, const char* tooltip) {
    bool changed = false;
    ImGui::PushID(label);
    const float fh = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    float w = ImGui::CalcItemWidth();
    const bool hasValue = !mixed && !value.empty();
    if (hasValue) w -= fh + 2.0f;
    std::string shown = mixed ? "Mixed" : value.empty() ? std::string("None") : std::filesystem::path(value).filename().string();
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    if (value.empty() || mixed) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::Button((shown + "###asset").c_str(), ImVec2(std::max(40.0f, w), fh))) ImGui::OpenPopup("##pick");
    if (value.empty() || mixed) ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nDrop a file from the Project panel here, or click to pick one.",
                                                  hasValue ? value.c_str() : (tooltip && *tooltip ? tooltip : "No file"));
    const ImGuiPayload* pl = ImGui::GetDragDropPayload();
    if (pl && pl->IsDataType("ASSET_PATH") && pl->Data)
        DropOutline(AssetKindAccepts(kind, (const char*)pl->Data), ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            const std::string path((const char*)p->Data);
            if (AssetKindAccepts(kind, path) && p->IsDelivery()) { value = path; changed = true; }
        }
        ImGui::EndDragDropTarget();
    }
    if (hasValue && ClearButton("##clear")) { value.clear(); changed = true; }
    if (AssetPickerPopup("##pick", value, kind, projectDir)) changed = true;
    (void)spacing;
    FieldLabel(label, tooltip);
    ImGui::PopID();
    return changed;
}

bool ObjectField(const char* label, std::string& guid, okay::Scene& scene, bool mixed, const char* tooltip) {
    bool changed = false;
    ImGui::PushID(label);
    const float fh = ImGui::GetFrameHeight();
    GameObject* cur = (mixed || guid.empty()) ? nullptr : scene.FindByGuid(guid);
    const bool hasValue = !mixed && !guid.empty();
    float w = ImGui::CalcItemWidth();
    if (hasValue) w -= fh + 2.0f;
    std::string shown = mixed ? "Mixed" : guid.empty() ? std::string("None") : cur ? cur->name : std::string("(missing object)");
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    const bool dim = guid.empty() || mixed || !cur;
    if (dim) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::Button((shown + "###obj").c_str(), ImVec2(std::max(40.0f, w), fh))) ImGui::OpenPopup("##pickobj");
    if (dim) ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\nDrag an object from the Hierarchy here, or click to pick one.", cur ? cur->Path().c_str() : "No object");
    const ImGuiPayload* pl = ImGui::GetDragDropPayload();
    if (pl && pl->IsDataType("GO_PTR")) DropOutline(true, ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GO_PTR")) {
            GameObject* g = *(GameObject**)p->Data;
            if (g && g->Guid() != guid) { guid = g->Guid(); changed = true; }
        }
        ImGui::EndDragDropTarget();
    }
    if (hasValue && ClearButton("##clear")) { guid.clear(); changed = true; }
    ImGui::SetNextWindowSizeConstraints(ImVec2(300, 120), ImVec2(480, 420));
    if (ImGui::BeginPopup("##pickobj")) {
        static char filter[96] = "";
        if (ImGui::IsWindowAppearing()) { filter[0] = '\0'; ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##find", "Search objects...", filter, sizeof(filter));
        const std::string needle = LowerS(filter);
        if (ImGui::Selectable("None", guid.empty())) { guid.clear(); changed = true; ImGui::CloseCurrentPopup(); }
        ImGui::Separator();
        ImGui::BeginChild("##objs", ImVec2(0, 260));
        int shown2 = 0;
        for (const auto& up : scene.Objects()) {
            GameObject* g = up.get();
            if (!g || g->transient) continue;
            if (!needle.empty() && LowerS(g->name).find(needle) == std::string::npos) continue;
            if (++shown2 > 400) { ImGui::TextDisabled("...type to narrow the list"); break; }
            ImGui::PushID(g);
            if (ImGui::Selectable(g->name.c_str(), g->Guid() == guid)) { guid = g->Guid(); changed = true; ImGui::CloseCurrentPopup(); }
            if (g->transform && g->transform->Parent()) {
                ImGui::SameLine();
                ImGui::TextDisabled("in %s", g->transform->Parent()->gameObject->name.c_str());
            }
            ImGui::PopID();
        }
        if (shown2 == 0) ImGui::TextDisabled("No matching objects.");
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    FieldLabel(label, tooltip);
    ImGui::PopID();
    return changed;
}

// A "..." handle on the right of the last item (a text box holding a path): click it, or right-click the box, for the file picker.
bool AssetPathPicker(std::string& value, const char* kind) {
    bool changed = false;
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const bool boxHot = ImGui::IsItemHovered();
    const bool over = boxHot && ImGui::IsMouseHoveringRect(ImVec2(mx.x - 22.0f, mn.y), mx, false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = ImGui::GetColorU32(over ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(ImVec2(mx.x - 11.0f + i * 4.0f, (mn.y + mx.y) * 0.5f), 1.4f, col, 8);
    static bool wantPicker = false;
    ImGui::PushID("##pathpick");
    if (boxHot && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))) {
        ImGui::ClearActiveID();
        ImGui::OpenPopup("##menu");
    }
    if (ImGui::BeginPopup("##menu")) {
        if (ImGui::MenuItem("Choose from project...")) wantPicker = true;
        if (ImGui::MenuItem("Clear", nullptr, false, !value.empty())) { value.clear(); changed = true; }
        ImGui::EndPopup();
    }
    if (wantPicker) { wantPicker = false; ImGui::OpenPopup("##pickfile"); }
    if (AssetPickerPopup("##pickfile", value, kind ? kind : "", s_ed ? s_ed->projectDir() : std::string())) changed = true;
    ImGui::PopID();
    return changed;
}

// ---- One property, any number of objects -----------------------------------------------------------------------

namespace {

float DragSpeed(const PropertyDesc& d, float cur) {
    if (d.step > 0.0) return (float)d.step;
    if (d.hasRange && d.rangeMax > d.rangeMin) return (float)((d.rangeMax - d.rangeMin) / 300.0);
    return std::max(0.01f, std::fabs(cur) * 0.01f);
}

// n floats in one row, each scrubbable and each able to say "mixed" on its own. Returns the mask of the parts edited.
unsigned FloatRow(const char* id, float* v, int n, const std::vector<bool>& mixedAxis, const PropertyDesc& d, const char* const* tags) {
    unsigned edited = 0;
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float total = ImGui::CalcItemWidth();
    const float each = std::max(24.0f, (total - spacing * (n - 1)) / (float)n);
    ImGui::PushID(id);
    for (int i = 0; i < n; ++i) {
        if (i) ImGui::SameLine(0, spacing);
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(each);
        const bool mx = i < (int)mixedAxis.size() && mixedAxis[(std::size_t)i];
        char fmt[24];
        std::snprintf(fmt, sizeof(fmt), mx ? "%s -" : "%s %%.3f", tags[i]);
        const float mn = d.hasRange ? (float)d.rangeMin : 0.0f, mxv = d.hasRange ? (float)d.rangeMax : 0.0f;
        if (ImGui::DragFloat("##f", &v[i], DragSpeed(d, v[i]), mn, mxv, fmt, d.hasRange ? ImGuiSliderFlags_AlwaysClamp : 0)) edited |= 1u << i;
        ImGui::PopID();
    }
    ImGui::PopID();
    return edited;
}

} // namespace

// Draws one property of the component `typeName` (index `idx`) on all `targets`. True when something was written.
static bool PropertyField(okay::editor::EditorState& ed, const std::vector<GameObject*>& targets,
                          const std::string& typeName, int idx, const PropertyDesc& d) {
    const FieldState st = ReadField(targets, typeName, idx, d);
    if (st.have == 0) return false;
    const std::string label = PrettyName(d.name);
    const char* tip = d.tooltip.c_str();
    const bool mixed = st.mixed;
    bool wrote = false;
    ImGui::PushID(d.name.c_str());
    const bool ro = d.IsReadOnly();
    if (ro) ImGui::BeginDisabled();
    switch (d.type) {
        case PropertyType::Bool: {
            bool b = st.value.AsBool();
            if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            if (ImGui::Checkbox(label.c_str(), &b)) wrote = WriteField(targets, typeName, idx, d, PropertyValue::MakeBool(b)) > 0;
            if (mixed) ImGui::PopItemFlag();
            if (*tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            break;
        }
        case PropertyType::Int: {
            int v = (int)st.value.AsInt();
            const int mn = d.hasRange ? (int)d.rangeMin : 0, mxv = d.hasRange ? (int)d.rangeMax : 0;
            if (ImGui::DragInt("##v", &v, d.step > 0.0 ? (float)d.step : 0.2f, mn, mxv, mixed ? "-" : "%d", d.hasRange ? ImGuiSliderFlags_AlwaysClamp : 0))
                wrote = WriteField(targets, typeName, idx, d, PropertyValue::MakeInt(v)) > 0;
            FieldLabel(label.c_str(), tip);
            break;
        }
        case PropertyType::Float: {
            float v = st.value.AsFloat();
            const float mn = d.hasRange ? (float)d.rangeMin : 0.0f, mxv = d.hasRange ? (float)d.rangeMax : 0.0f;
            if (ImGui::DragFloat("##v", &v, DragSpeed(d, v), mn, mxv, mixed ? "-" : "%.3f", d.hasRange ? ImGuiSliderFlags_AlwaysClamp : 0))
                wrote = WriteField(targets, typeName, idx, d, PropertyValue::MakeFloat(v)) > 0;
            FieldLabel(label.c_str(), tip);
            break;
        }
        case PropertyType::String: {
            char buf[512];
            std::snprintf(buf, sizeof(buf), "%s", mixed ? "" : st.value.AsString().c_str());
            bool edited;
            if (d.multiline) edited = ImGui::InputTextMultiline("##v", buf, sizeof(buf), ImVec2(ImGui::CalcItemWidth(), ImGui::GetTextLineHeight() * 4.0f));
            else { ImGui::SetNextItemWidth(ImGui::CalcItemWidth()); edited = ImGui::InputTextWithHint("##v", mixed ? "Mixed" : "", buf, sizeof(buf)); }
            if (edited) wrote = WriteField(targets, typeName, idx, d, PropertyValue::MakeString(buf)) > 0;
            FieldLabel(label.c_str(), tip);
            break;
        }
        case PropertyType::Vec2:
        case PropertyType::Vec3:
        case PropertyType::Vec4: {
            static const char* tags[4] = {"X", "Y", "Z", "W"};
            float v[4] = {0, 0, 0, 0};
            const int n = PropertyTypeComponentCount(d.type);
            if (d.type == PropertyType::Vec2) { Vec2 a = st.value.AsVec2(); v[0] = a.x; v[1] = a.y; }
            else if (d.type == PropertyType::Vec3) { Vec3 a = st.value.AsVec3(); v[0] = a.x; v[1] = a.y; v[2] = a.z; }
            else { Vec4 a = st.value.AsVec4(); v[0] = a.x; v[1] = a.y; v[2] = a.z; v[3] = a.w; }
            const unsigned m = FloatRow("v", v, n, st.axisMixed, d, tags);
            if (m) {
                PropertyValue nv = d.type == PropertyType::Vec2 ? PropertyValue::MakeVec2({v[0], v[1]})
                                 : d.type == PropertyType::Vec3 ? PropertyValue::MakeVec3({v[0], v[1], v[2]})
                                 : PropertyValue::MakeVec4({v[0], v[1], v[2], v[3]});
                wrote = WriteFieldAxes(targets, typeName, idx, d, nv, m) > 0;
            }
            FieldLabel(label.c_str(), tip);
            break;
        }
        case PropertyType::Quat: {   // shown as angles in degrees, like the Transform's Rotation
            static const char* tags[3] = {"X", "Y", "Z"};
            const Vec3 e0 = st.value.AsQuat().ToEuler();
            float v[3] = {e0.x, e0.y, e0.z};
            PropertyDesc dd = d; dd.hasRange = false; dd.step = 1.0;
            std::vector<bool> none(3, mixed);
            const unsigned m = FloatRow("q", v, 3, none, dd, tags);
            if (m) wrote = WriteField(targets, typeName, idx, d, PropertyValue::MakeQuat(Quat::Euler(Vec3{v[0], v[1], v[2]}))) > 0;
            FieldLabel(label.c_str(), tip);
            break;
        }
        case PropertyType::Color: {
            Color c = st.value.AsColor();
            float f[4] = {c.r, c.g, c.b, c.a};
            ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
            if (ImGui::ColorEdit4("##v", f, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
                unsigned m = 0;
                const float old[4] = {c.r, c.g, c.b, c.a};
                for (int i = 0; i < 4; ++i) if (f[i] != old[i]) m |= 1u << i;
                wrote = WriteFieldAxes(targets, typeName, idx, d, PropertyValue::MakeColor(Color{f[0], f[1], f[2], f[3]}), m) > 0;
            }
            if (mixed && ImGui::IsItemHovered()) ImGui::SetTooltip("The selected objects differ. Changing one part sets it on all of them.");
            FieldLabel(label.c_str(), tip);
            break;
        }
        case PropertyType::Enum: {
            const int cur = st.value.AsEnum();
            const char* preview = mixed ? "Mixed" : (cur >= 0 && cur < (int)d.enumOptions.size()) ? d.enumOptions[(std::size_t)cur].c_str() : "?";
            ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
            if (ImGui::BeginCombo("##v", preview)) {
                for (int i = 0; i < (int)d.enumOptions.size(); ++i)
                    if (ImGui::Selectable(d.enumOptions[(std::size_t)i].c_str(), !mixed && i == cur))
                        wrote = WriteField(targets, typeName, idx, d, PropertyValue::MakeEnum(i, d.enumOptions[(std::size_t)i])) > 0;
                ImGui::EndCombo();
            }
            FieldLabel(label.c_str(), tip);
            break;
        }
        case PropertyType::AssetPath: {
            std::string p = st.value.AsString();
            if (AssetField(label.c_str(), p, d.assetKind.c_str(), ed.projectDir(), mixed, tip))
                wrote = WriteField(targets, typeName, idx, d, PropertyValue::MakeAssetPath(p)) > 0;
            break;
        }
        case PropertyType::ObjectRef: {
            std::string g = st.value.AsString();
            if (ObjectField(label.c_str(), g, ed.scene(), mixed, tip))
                wrote = WriteField(targets, typeName, idx, d, PropertyValue::MakeObjectRef(g)) > 0;
            break;
        }
        default: break;
    }
    if (ro) ImGui::EndDisabled();
    ImGui::PopID();
    if (wrote) ed.dirty = true;
    return wrote;
}

// ---- The card for components without their own Inspector ------------------------------------------------------------

static bool s_headerDrawn = false;
void BeginInspectorFrame() { s_headerDrawn = false; }

void DrawFallbackInspector(okay::editor::EditorState& ed, okay::GameObject* go, okay::Component* comp,
                           okay::Component*& toRemove, float cursorYBefore, CompHeaderFn header) {
    if (!go || !comp || comp == go->transform) return;
    if (ImGui::GetCursorPosY() != cursorYBefore) return;           // a hand-written Inspector drew it
    auto info = PropertyRegistry::FindFor(*comp);
    if (!info || info->properties.empty()) return;
    const bool playing = ed.isPlaying();
    std::vector<const PropertyDesc*> shown;
    for (const PropertyDesc& d : info->properties) {
        if (d.IsHidden()) continue;
        if (d.IsRuntimeOnly() && !playing) continue;
        shown.push_back(&d);
    }
    if (shown.empty()) return;
    if (!header(PrettyName(info->name).c_str(), comp, &toRemove, true)) return;
    std::vector<GameObject*> targets;
    const int idx = ComponentIndexOf(*go, *comp);
    for (GameObject* g : ed.MultiSelection()) if (g) targets.push_back(g);
    if (std::find(targets.begin(), targets.end(), go) == targets.end()) targets = {go};
    int have = 0;
    for (GameObject* g : targets) if (PropertyRegistry::GetProperty(*g, ComponentPath(info->name, idx) + "." + shown[0]->name).IsValid()) ++have;
    if (have > 1) ImGui::TextDisabled("Editing %d selected objects", have);
    std::string group;
    for (const PropertyDesc* d : shown) {
        if (d->group != group) {
            group = d->group;
            if (!group.empty()) ImGui::SeparatorText(group.c_str());
        }
        PropertyField(ed, targets, info->name, idx, *d);
    }
    ImGui::Spacing();
}

// ---- Component menu: copy / paste / reset values --------------------------------------------------------------------------

namespace {
ComponentClip s_clip;
Component* s_reqComp = nullptr;
int s_reqKind = 0;   // 1 copy, 2 paste, 3 reset
}

void ComponentValueMenu(okay::Component* comp) {
    if (!comp || !HasRegisteredProperties(*comp)) return;
    auto info = PropertyRegistry::FindFor(*comp);
    ImGui::Separator();
    if (ImGui::MenuItem("Copy Values")) { s_reqComp = comp; s_reqKind = 1; }
    const bool canPaste = s_clip.Valid() && info && info->name == s_clip.type;
    if (ImGui::MenuItem("Paste Values", nullptr, false, canPaste)) { s_reqComp = comp; s_reqKind = 2; }
    if (ImGui::IsItemHovered() && !canPaste) ImGui::SetTooltip(s_clip.Valid() ? "The copied values belong to a different component." : "Copy the values of another component of this kind first.");
    if (ImGui::MenuItem("Reset")) { s_reqComp = comp; s_reqKind = 3; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put every value back to what a new component has.");
}

void ApplyComponentValueRequests(okay::editor::EditorState& ed, okay::GameObject* go) {
    Component* c = s_reqComp;
    const int kind = s_reqKind;
    s_reqComp = nullptr; s_reqKind = 0;
    if (!c || !go || c->gameObject != go) return;
    auto info = PropertyRegistry::FindFor(*c);
    if (!info) return;
    if (kind == 1) { s_clip = CopyComponentValues(*c); ConsoleLog("Copied the values of " + PrettyName(info->name)); return; }
    // Paste and Reset go to the same component on every selected object that has one.
    const int idx = ComponentIndexOf(*go, *c);
    std::vector<Component*> targets;
    std::vector<GameObject*> objs;
    for (GameObject* g : ed.MultiSelection()) if (g) objs.push_back(g);
    if (std::find(objs.begin(), objs.end(), go) == objs.end()) objs = {go};
    for (GameObject* g : objs) {
        int n = 0;
        for (const auto& up : g->Components())
            if (up && PropertyRegistry::FindFor(*up) == info) { if (n++ == idx) { targets.push_back(up.get()); break; } }
    }
    ed.PushUndo();
    int written = 0;
    for (Component* t : targets) written += kind == 2 ? PasteComponentValues(s_clip, *t) : ResetComponentValues(*t);
    if (written > 0) ed.dirty = true;
    ConsoleLog(std::string(kind == 2 ? "Pasted values onto " : "Reset ") + PrettyName(info->name) +
               (written == 0 ? " (nothing changed)" : ""));
}

// ---- Prefab changes ----------------------------------------------------------------------------------------------------------

namespace {
std::vector<PrefabChange> s_changes;
GameObject* s_changesRoot = nullptr;
std::unordered_set<const Component*> s_changedComps;
int s_changesFrame = -1000;
}

void RefreshPrefabChanges(GameObject* go) {
    GameObject* root = go ? PrefabTools::NearestPrefabRoot(*go) : nullptr;
    if (root && root->prefabSource.empty()) root = nullptr;
    const int frame = ImGui::GetFrameCount();
    const bool stale = root != s_changesRoot || frame - s_changesFrame > 30 || ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    if (!stale) return;
    s_changesRoot = root;
    s_changesFrame = frame;
    s_changes.clear();
    s_changedComps.clear();
    if (!root) return;
    s_changes = PrefabChangesOf(*root);
    for (const PrefabChange& ch : s_changes) {
        if (!ch.object || ch.removed) continue;
        int n = 0;
        for (const auto& up : ch.object->Components())
            if (up && RecordNameOf(*up) == ch.record) { if (n++ == ch.occurrence) { s_changedComps.insert(up.get()); break; } }
    }
}

bool PrefabChangedFor(const okay::Component* comp) { return comp && s_changedComps.count(comp) != 0; }

bool DrawPrefabChanges(okay::editor::EditorState& ed, okay::GameObject* go) {
    GameObject* root = s_changesRoot;
    if (!root || !go) return false;
    const bool isRoot = (go == root);
    std::vector<const PrefabChange*> mine;
    for (const PrefabChange& c : s_changes) if (c.object == go) mine.push_back(&c);
    const int others = (int)s_changes.size() - (int)mine.size();
    if (mine.empty() && (!isRoot || others == 0)) {
        if (!isRoot) { ImGui::TextDisabled("Part of prefab %s", std::filesystem::path(root->prefabSource).filename().string().c_str()); ImGui::Spacing(); }
        return false;
    }
    bool rebuilt = false;
    const std::string pname = std::filesystem::path(root->prefabSource).filename().string();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.17f, 0.25f, 1.0f));
    ImGui::BeginChild("##prefabchanges", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    ImGui::TextColored(ImVec4(0.45f, 0.70f, 1.0f, 1.0f), "Changed from the prefab");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", pname.c_str());
    int i = 0;
    for (const PrefabChange* c : mine) {
        ImGui::PushID(i++);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s", c->description.c_str());
        ImGui::PopTextWrapPos();
        std::string err;
        if (ImGui::SmallButton("Revert")) {
            ed.PushUndo();
            if (GameObject* fresh = PrefabTools::RevertOverride(ed.scene(), *root, c->prefabObject, c->record, "", c->occurrence, &err)) {
                ed.Select(fresh); ed.dirty = true; rebuilt = true;
            } else ConsoleLog("Revert failed: " + err, 2);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put this back to what the prefab says.");
        ImGui::SameLine();
        if (!rebuilt && ImGui::SmallButton("Apply")) {
            if (PrefabTools::ApplyToSource(*root, c->prefabObject, c->record, "", c->occurrence, &err)) {
                ConsoleLog("Applied to " + pname + ": " + c->record);
                s_changesFrame = -1000;
            } else ConsoleLog("Apply failed: " + err, 2);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Write this change into the prefab. Every instance gets it.");
        ImGui::PopID();
        if (rebuilt) break;
    }
    if (!rebuilt && isRoot) {
        if (others > 0) ImGui::TextDisabled("%d more change%s in objects below", others, others == 1 ? "" : "s");
        if (ImGui::SmallButton("Revert All")) {
            std::string err;
            ed.PushUndo();
            if (GameObject* fresh = PrefabTools::RevertAll(ed.scene(), *root, &err)) { ed.Select(fresh); ed.dirty = true; rebuilt = true; }
            else ConsoleLog("Revert failed: " + err, 2);
        }
        ImGui::SameLine();
        if (!rebuilt && ImGui::SmallButton("Apply All")) {
            std::string err;
            if (PrefabTools::ApplyAllToSource(*root, &err)) { ConsoleLog("Applied all changes to " + pname); s_changesFrame = -1000; }
            else ConsoleLog("Apply failed: " + err, 2);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
    if (rebuilt) { s_changesRoot = nullptr; s_changes.clear(); s_changedComps.clear(); }
    return rebuilt;
}

// ---- Play mode ---------------------------------------------------------------------------------------------------------------------

void DrawPlayModeNote(okay::editor::EditorState& ed) {
    if (!ed.isPlaying()) return;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.32f, 0.22f, 0.06f, 1.0f));
    ImGui::BeginChild("##playnote", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1.0f), "Playing");
    ImGui::SameLine();
    ImGui::TextWrapped("Changes made here are not saved. They go away when you press Stop.");
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

} // namespace oked
