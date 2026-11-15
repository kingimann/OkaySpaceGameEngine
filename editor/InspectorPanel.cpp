#include "InspectorPanel.hpp"
#include "EditUndo.hpp"
#include "EditorWidgets.hpp"
#include "ConsolePanel.hpp"
#include "InspectorClickRules.hpp"
#include "InspectorModel.hpp"
#include "PrefabEdit.hpp"
#include "imgui_internal.h"
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unordered_set>

using namespace okay;

namespace oked {

namespace {

okay::editor::EditorState* s_ed = nullptr;
const okay::Component* s_markComp = nullptr;   // the component whose fields are being drawn (SetTipComponent)
bool s_nameFieldHovered = false;               // the object name box (FieldTip("##name")) is under the mouse: it has its own right-click menu

std::string LowerS(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool StartsWith(const char* s, const char* prefix) { return std::strncmp(s, prefix, std::strlen(prefix)) == 0; }

// Is this window one whose widgets edit the scene (or a popup / child opened from one)? The Inspector and
// Hierarchy, and the panels that write into the scene too: Lighting (scene settings), Material, the Animation
// tab and Custom Actions. Panels with an undo of their own (Modeling, Timeline, the script editors, the Project
// panel ...) stay out, so one edit never lands on two stacks.
bool InEditorWindow(const ImGuiWindow* w) {
    for (; w; w = w->ParentWindow)
        if (StartsWith(w->Name, "Inspector") || StartsWith(w->Name, "Hierarchy") || StartsWith(w->Name, "Lighting") ||
            StartsWith(w->Name, "Material") || StartsWith(w->Name, "Animation") || StartsWith(w->Name, "Custom Actions"))
            return true;
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
    const bool hit = ImGui::Button(id, ImVec2(fh, fh));
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
    static ImGuiID lastActive = 0;   // the widget being edited: the same widget edited again joins the step
    s_ed = &ed;
    s_markComp = nullptr;            // a component freed last frame must not be looked at by a field of this one
    s_nameFieldHovered = false;
    if (ed.isPlaying()) { if (u.IsOpen()) u.Cancel(); return; }   // Play changes are thrown away anyway
    ImGuiContext& g = *GImGui;
    const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    const bool activeMine = g.ActiveId != 0 && InEditorWindow(g.ActiveIdWindow);
    if (!u.IsOpen()) {
        // The picture is taken on the press, before the widget changes anything.
        // (In a huge scene the extra picture on every click would be felt: then only a pressed widget starts one.)
        const bool bigScene = ed.scene().Objects().size() > 8000;
        if ((clicked && !bigScene && InEditorWindow(g.HoveredWindow)) || activeMine) { u.Begin(ed); lastActive = 0; }
    } else if (!down && !activeMine) {
        u.End(ed, lastActive);
    }
    if (u.IsOpen() && activeMine) lastActive = g.ActiveId;
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

// A drop-down arrow on the right of the last item (a text box holding a path): click it, or right-click the box, for the file picker.
bool AssetPathPicker(std::string& value, const char* kind) {
    bool changed = false;
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const bool boxHot = ImGui::IsItemHovered();
    const bool over = boxHot && ImGui::IsMouseHoveringRect(ImVec2(mx.x - 22.0f, mn.y), mx, false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = ImGui::GetColorU32(over ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    {   // a small drop-down arrow: "choose from a list"
        const float cx = mx.x - 12.0f, cy = (mn.y + mx.y) * 0.5f;
        dl->AddTriangleFilled(ImVec2(cx - 4.0f, cy - 2.0f), ImVec2(cx + 4.0f, cy - 2.0f), ImVec2(cx, cy + 3.0f), col);
    }
    if (over) ImGui::SetTooltip("Choose a file from the project (or right-click)");
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

// The marker and right-click menu of a field (defined with the prefab code below). `desc` is the property the field shows when the
// caller knows it (the generic card), else it is found from the label when the menu opens. `wholeRowTopY`: when set, a right click
// anywhere on the row counts (the row is the full width of the window, from that screen y down to the last item), not only on the last item.
static void FieldMark(const char* label, const PropertyDesc* desc, const float* wholeRowTopY);

// Draws one property of the component `typeName` (index `idx`) on all `targets`. True when something was written.
static bool PropertyField(okay::editor::EditorState& ed, const std::vector<GameObject*>& targets,
                          const std::string& typeName, int idx, const PropertyDesc& d) {
    const FieldState st = ReadField(targets, typeName, idx, d);
    if (st.have == 0) return false;
    const std::string label = PrettyName(d.name);
    const std::string tipText = TooltipFor(typeName, d);
    const char* tip = tipText.c_str();
    const bool mixed = st.mixed;
    bool wrote = false;
    ImGui::PushID(d.name.c_str());
    const bool ro = d.IsReadOnly();
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
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
            // NoOptions: the colour box's own right-click popup (display format) would be replaced by the field menu anyway.
            if (ImGui::ColorEdit4("##v", f, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoOptions)) {
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
    // The blue bar of a prefab instance's overridden property, and the right-click menu (Reset to Default, Copy / Paste Value, Revert ...).
    // The whole row answers, every kind of value: the label, the gap and the box (Dear ImGui's text boxes have no right-click menu of their own).
    FieldMark(label.c_str(), &d, &rowStart.y);
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
        if (d.name == "enabled") continue;                    // the header's check box is this one
        if (d.IsRuntimeOnly() && !playing) continue;
        shown.push_back(&d);
    }
    if (shown.empty()) return;
    if (std::getenv("OKAY_DBG_FALLBACK")) { static std::unordered_set<std::string> seen; if (seen.insert(info->name).second) std::fprintf(stderr, "FALLBACK %s\n", info->name.c_str()); }
    if (!header(PrettyName(info->name).c_str(), comp, &toRemove, true)) return;
    std::vector<GameObject*> targets;
    const int idx = ComponentIndexOf(*go, *comp);
    for (GameObject* g : ed.MultiSelection()) if (g) targets.push_back(g);
    if (std::find(targets.begin(), targets.end(), go) == targets.end()) targets = {go};
    int have = 0;
    for (GameObject* g : targets) if (PropertyRegistry::GetProperty(*g, ComponentPath(info->name, idx) + "." + shown[0]->name).IsValid()) ++have;
    if (have > 1) ImGui::TextDisabled("Editing %d selected objects", have);
    SetTipComponent(comp);   // which component the per-property prefab marks belong to
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

// ---- Tooltips and mixed values for hand-written Inspectors ------------------------------------------------------------------

namespace { std::string s_tipType; }

void SetTipComponent(const okay::Component* comp) {
    s_tipType.clear();
    s_markComp = comp;
    if (!comp) return;
    if (auto info = PropertyRegistry::FindFor(*comp)) s_tipType = info->name;
}

void FieldTip(const char* label, bool wholeRow) {
    if (std::strcmp(label, "##name") == 0) s_nameFieldHovered = ImGui::IsItemHovered();   // the object name box: the header menu stays off it
    const float rowTop = ImGui::GetItemRectMin().y;
    FieldMark(label, nullptr, wholeRow ? &rowTop : nullptr);   // a prefab instance's overridden field: blue bar; right-click: Reset, Copy / Paste Value, Revert ...
    if (s_tipType.empty() || !ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) return;
    const std::string tip = FieldTooltip(s_tipType, label);
    if (!tip.empty()) ImGui::SetTooltip("%s", tip.c_str());
}

void DrawMixedNote(const okay::Component* comp) {
    if (!comp || !s_ed || !comp->gameObject) return;
    const std::vector<GameObject*>& sel = s_ed->MultiSelection();
    if (sel.size() < 2 || std::find(sel.begin(), sel.end(), comp->gameObject) == sel.end()) return;
    auto info = PropertyRegistry::FindFor(*comp);
    if (!info) return;
    const std::vector<std::string> names = MixedPropertyNames(sel, info->name, ComponentIndexOf(*comp->gameObject, *comp));
    if (names.empty()) return;
    std::string text = "Different on the selected objects: ";
    for (std::size_t i = 0; i < names.size(); ++i) text += (i ? ", " : "") + names[i];
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.78f, 0.40f, 1.0f));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What you change here is set on every selected object.");
}

// ---- Prefab instances: header, per-property marks, changes list --------------------------------------------------------------
//
// The logic (reading differences, Revert / Apply, undo, updating the other instances) is oked::PrefabEdit / PrefabTools; this is
// only the drawing. Buttons never rebuild the instance in the middle of a frame (the object the Inspector draws would vanish
// under it): they queue a request that RunPrefabRequests carries out once the drawing is done.

namespace {
std::vector<PrefabChange> s_changes;
std::vector<PrefabObjectChange> s_objChanges;
GameObject* s_changesRoot = nullptr;
std::unordered_set<const Component*> s_changedComps;
std::unordered_map<const Component*, std::size_t> s_changeOf;   // component -> index in s_changes
int s_changesFrame = -1000;
PrefabUiHooks s_hooks;

struct PrefabReq {
    enum Kind { None, Revert, Apply, RevertAll, ApplyAll, RevertObject, ApplyObject, Variant, Unpack, UnpackAll, UpdateAll } kind = None;
    GameObject* root = nullptr;
    std::vector<PrefabTarget> targets;
    PrefabObjectChange object;
    std::string path;   // Variant: where to write
} s_req;

bool s_openVariant = false;
bool s_variantSeed = false;   // fill the name box with the default on the next Create Variant button / dialog open
char s_variantName[160] = "";

ImVec4 PrefabBlue() { return ImVec4(0.45f, 0.70f, 1.0f, 1.0f); }

void Queue(PrefabReq::Kind k, GameObject* root, std::vector<PrefabTarget> t = {}) {
    s_req = PrefabReq(); s_req.kind = k; s_req.root = root; s_req.targets = std::move(t);
}
}   // namespace

void SetPrefabUiHooks(PrefabUiHooks hooks) { s_hooks = std::move(hooks); }

void InvalidatePrefabChanges() { s_changesFrame = -1000; ClearPrefabInfoCache(); }

void RefreshPrefabChanges(GameObject* go) {
    GameObject* root = go ? PrefabTools::NearestPrefabRoot(*go) : nullptr;
    if (root && root->prefabSource.empty()) root = nullptr;
    const int frame = ImGui::GetFrameCount();
    const bool stale = root != s_changesRoot || frame - s_changesFrame > 30 || ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    if (!stale) return;
    s_changesRoot = root;
    s_changesFrame = frame;
    s_changes.clear();
    s_objChanges.clear();
    s_changedComps.clear();
    s_changeOf.clear();
    if (!root) return;
    s_changes = PrefabChangesOf(*root);
    s_objChanges = PrefabObjectChangesOf(*root);
    for (std::size_t i = 0; i < s_changes.size(); ++i) {
        const PrefabChange& ch = s_changes[i];
        if (!ch.object || ch.removed) continue;
        int n = 0;
        for (const auto& up : ch.object->Components())
            if (up && RecordNameOf(*up) == ch.record) {
                if (n++ == ch.occurrence) { s_changedComps.insert(up.get()); s_changeOf[up.get()] = i; break; }
            }
    }
}

bool PrefabChangedFor(const okay::Component* comp) { return comp && s_changedComps.count(comp) != 0; }

namespace {
// The diff fields of the labelled property of the current component that differ from the prefab. Empty when none.
std::vector<std::string> OverriddenFieldsFor(const char* label, const PrefabChange** change) {
    if (!s_markComp) return {};
    auto it = s_changeOf.find(s_markComp);
    if (it == s_changeOf.end() || it->second >= s_changes.size()) return {};
    const PrefabChange& ch = s_changes[it->second];
    if (change) *change = &ch;
    return ChangedFieldsForLabel(ch, *s_markComp, label);
}
}   // namespace

void PrefabFieldMark(const char* label) { FieldMark(label, nullptr, nullptr); }

void PrefabComponentMenu(okay::Component* comp) {
    auto it = comp ? s_changeOf.find(comp) : s_changeOf.end();
    if (it == s_changeOf.end() || it->second >= s_changes.size() || !s_changesRoot) return;
    const PrefabChange& ch = s_changes[it->second];
    ImGui::Separator();
    if (ImGui::MenuItem(ch.added ? "Revert Added Component" : "Revert Component to Prefab")) Queue(PrefabReq::Revert, s_changesRoot, {TargetOf(ch)});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(ch.added ? "Remove this component: the prefab does not have it." : "Put every value of this component back to the prefab's.");
    if (ImGui::MenuItem(ch.added ? "Apply Added Component to Prefab" : "Apply Component to Prefab")) Queue(PrefabReq::Apply, s_changesRoot, {TargetOf(ch)});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Write this component into the prefab. Other instances that did not change it take it too.");
}

// Next button of the header row: on the same line when it fits, else on a new one (a narrow Inspector must not clip the buttons).
static void HdrSameLine(const char* label) {
    const float need = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + need < right) ImGui::SameLine();
}

// Header of the Inspector for an object that is (or is part of) a prefab instance.
void DrawPrefabHeader(okay::editor::EditorState& ed, okay::GameObject* go) {
    PrefabInstanceInfo info;
    if (!go || !DescribePrefabInstance(*go, info)) return;
    GameObject* root = info.root;
    const int nChanges = s_changesRoot == root ? (int)(s_changes.size() + s_objChanges.size()) : 0;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.17f, 0.27f, 1.0f));
    ImGui::BeginChild("##prefabbanner", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    if (info.exists) ImGui::TextColored(PrefabBlue(), "Prefab: %s", info.name.c_str());
    else ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f), "Prefab: %s (missing)", info.name.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.source.c_str());
    if (info.kind != PrefabKind::Plain || info.isNestedInstance) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", PrefabKindLabel(info).c_str());
        ImGui::PopTextWrapPos();
    }
    if (!info.isRoot) {
        ImGui::TextDisabled("This object is part of the prefab instance %s.", root->name.c_str());
        if (ImGui::SmallButton("Select Instance Root")) ed.Select(root);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Prefab Revert All / Apply All / Create Variant work on the instance root.");
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("Open")) { if (s_hooks.open) s_hooks.open(root); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Edit this prefab: shows the instance on its own in the Scene view (Esc leaves).\nApply All writes your changes into the prefab file.");
    HdrSameLine("Select Asset");
    if (ImGui::SmallButton("Select Asset")) { if (s_hooks.selectAsset) s_hooks.selectAsset(info.source); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Find %s in the Project panel.", info.name.c_str());
    if (info.kind == PrefabKind::Variant) {
        const std::string base = ResolveVariantBase(info);
        HdrSameLine("Select Base");
        ImGui::BeginDisabled(base.empty());
        if (ImGui::SmallButton("Select Base")) { if (s_hooks.selectAsset) s_hooks.selectAsset(base); }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(base.empty() ? "The base prefab %s was not found." : "Find the base prefab %s in the Project panel.", info.baseName.c_str());
    }
    if (info.isRoot) {
        HdrSameLine("Revert All");
        ImGui::BeginDisabled(nChanges == 0);
        if (ImGui::SmallButton("Revert All")) Queue(PrefabReq::RevertAll, root);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(nChanges ? "Put everything back to what the prefab says." : "Nothing differs from the prefab.");
        HdrSameLine("Apply All");
        ImGui::BeginDisabled(nChanges == 0 || !info.exists);
        if (ImGui::SmallButton("Apply All")) Queue(PrefabReq::ApplyAll, root);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(nChanges ? "Write every change into the prefab file. Other instances that did not change the same thing take it too." : "Nothing differs from the prefab.");
        HdrSameLine("Create Variant");
        ImGui::BeginDisabled(!info.exists);
        if (ImGui::SmallButton("Create Variant")) {
            s_openVariant = true; s_variantSeed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Save what this instance changed as a new prefab that is based on %s, and make this object an instance of it.", info.name.c_str());
        HdrSameLine("Update All");
        if (ImGui::SmallButton("Update All")) Queue(PrefabReq::UpdateAll, root);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rebuild every instance of this prefab in the scene from the file (each keeps what it changed).\nUse it after the file was edited outside the editor.");
        HdrSameLine("Unpack");
        if (ImGui::SmallButton("Unpack")) ImGui::OpenPopup("##unpackmenu");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cut the link to the prefab; this becomes plain objects.");
        if (ImGui::BeginPopup("##unpackmenu")) {
            if (ImGui::MenuItem("Unpack")) Queue(PrefabReq::Unpack, root);
            if (ImGui::MenuItem("Unpack Completely", nullptr, false, info.nestedCount > 0)) Queue(PrefabReq::UnpackAll, root);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Also cut the links of the prefabs inside this one.");
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (s_openVariant) {
        if (s_variantSeed || !s_variantName[0]) {
            const std::string d = std::filesystem::path(DefaultVariantPath(info.source)).stem().string();
            std::snprintf(s_variantName, sizeof(s_variantName), "%s", d.c_str());
            s_variantSeed = false;
        }
        ImGui::OpenPopup("Create Variant");
        s_openVariant = false;
    }
    if (ImGui::BeginPopupModal("Create Variant", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("A variant is a prefab based on %s. It stores only what is different from it, so changes to the base reach it.", info.name.c_str());
        ImGui::SetNextItemWidth(260.0f);
        const bool enter = ImGui::InputText("Name", s_variantName, sizeof(s_variantName), ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string file = std::string(s_variantName) + ".okayprefab";
        const std::string full = (std::filesystem::path(info.source).parent_path() / file).string();
        std::error_code ec;
        const bool taken = std::filesystem::exists(full, ec);
        const bool ok = s_variantName[0] && !taken;
        if (taken) ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f), "%s already exists.", file.c_str());
        else ImGui::TextDisabled("Saved next to the base: %s", file.c_str());
        ImGui::BeginDisabled(!ok);
        if (ImGui::Button("Create", ImVec2(110, 0)) || (enter && ok)) {
            Queue(PrefabReq::Variant, root);
            s_req.path = full;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void QueuePrefabAction(PrefabAction a, GameObject* root) {
    switch (a) {
        case PrefabAction::RevertAll:        Queue(PrefabReq::RevertAll, root); break;
        case PrefabAction::ApplyAll:         Queue(PrefabReq::ApplyAll, root); break;
        case PrefabAction::Unpack:           Queue(PrefabReq::Unpack, root); break;
        case PrefabAction::UnpackCompletely: Queue(PrefabReq::UnpackAll, root); break;
        case PrefabAction::UpdateAll:        Queue(PrefabReq::UpdateAll, root); break;
    }
}

void OpenCreateVariantDialog() { s_openVariant = true; s_variantSeed = true; }

bool RunPrefabRequests(okay::editor::EditorState& ed) {
    PrefabReq r = std::move(s_req);
    s_req = PrefabReq();
    if (r.kind == PrefabReq::None || !r.root) return false;
    // The object may be gone (the scene changed since the click).
    bool alive = false;
    for (const auto& up : ed.scene().Objects()) if (up.get() == r.root && !up->IsDestroyed()) { alive = true; break; }
    if (!alive) return false;
    PrefabOpResult res;
    switch (r.kind) {
        case PrefabReq::Revert:       res = PrefabRevert(ed, *r.root, r.targets); break;
        case PrefabReq::Apply:        res = PrefabApply(ed, *r.root, r.targets); break;
        case PrefabReq::RevertAll:    res = PrefabRevertAll(ed, *r.root); break;
        case PrefabReq::ApplyAll:     res = PrefabApplyAll(ed, *r.root); break;
        case PrefabReq::RevertObject: res = PrefabRevertObject(ed, *r.root, r.object); break;
        case PrefabReq::ApplyObject:  res = PrefabApplyObject(ed, *r.root, r.object); break;
        case PrefabReq::Variant:      res = PrefabCreateVariant(ed, *r.root, r.path); break;
        case PrefabReq::Unpack:       res = PrefabUnpack(ed, *r.root, false); break;
        case PrefabReq::UnpackAll:    res = PrefabUnpack(ed, *r.root, true); break;
        case PrefabReq::UpdateAll:    res = PrefabUpdateInstances(ed, r.root->prefabSource); break;
        default: break;
    }
    ConsoleLog(res.message, res.ok ? 0 : 2);
    InvalidatePrefabChanges();
    s_changesRoot = nullptr; s_changes.clear(); s_objChanges.clear(); s_changedComps.clear(); s_changeOf.clear();
    // Reverts and Update All rebuild the instance: the object the Inspector is showing is gone, so the caller stops drawing this frame.
    return res.ok && (r.kind == PrefabReq::Revert || r.kind == PrefabReq::RevertAll || r.kind == PrefabReq::RevertObject || r.kind == PrefabReq::UpdateAll);
}

// The "Changed from the prefab" box for the selected object, and (on the instance root) the objects it gained or lost.
void DrawPrefabChanges(okay::editor::EditorState& ed, okay::GameObject* go) {
    GameObject* root = s_changesRoot;
    if (!root || !go) return;
    const bool isRoot = (go == root);
    std::vector<const PrefabChange*> mine;
    for (const PrefabChange& c : s_changes) if (c.object == go) mine.push_back(&c);
    const std::size_t objCount = isRoot ? s_objChanges.size() : 0;
    if (mine.empty() && objCount == 0) {
        if (isRoot && !s_changes.empty()) ImGui::TextDisabled("%d change%s in objects below. Select them to see them.", (int)s_changes.size(), s_changes.size() == 1 ? "" : "s");
        return;
    }
    const std::string pname = std::filesystem::path(root->prefabSource).filename().string();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.17f, 0.25f, 1.0f));
    ImGui::BeginChild("##prefabchanges", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    ImGui::TextColored(PrefabBlue(), "Changed from the prefab");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", pname.c_str());
    int id = 0;
    for (const PrefabChange* c : mine) {
        ImGui::PushID(id++);
        const char* what = c->added ? "Added component" : c->removed ? "Removed component" : "Component";
        ImGui::TextColored(ImVec4(0.82f, 0.88f, 1.0f, 1.0f), "%s: %s", what, PrettyName(c->record).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Revert")) Queue(PrefabReq::Revert, root, {TargetOf(*c)});
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(c->added ? "Remove the component: the prefab does not have it." : c->removed ? "Bring the component back from the prefab." : "Put every value of this component back to the prefab's.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Apply")) Queue(PrefabReq::Apply, root, {TargetOf(*c)});
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Write this component into the prefab. Other instances that did not change it take it too.");
        if (!c->wholeRecordOnly && !c->added && !c->removed) {
            int fi = 0;
            for (const PrefabFieldDiff& f : c->fields) {
                ImGui::PushID(fi++);
                ImGui::Bullet();
                ImGui::TextWrapped("%s: %s -> %s", f.name.c_str(), f.inPrefab ? f.prefabValue.c_str() : "(none)", f.inInstance ? f.instanceValue.c_str() : "(none)");
                ImGui::SameLine();
                if (ImGui::SmallButton("Revert")) Queue(PrefabReq::Revert, root, {TargetOf(*c, f.name)});
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put just this value back to the prefab's.");
                ImGui::SameLine();
                if (ImGui::SmallButton("Apply")) Queue(PrefabReq::Apply, root, {TargetOf(*c, f.name)});
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Write just this value into the prefab.");
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    if (objCount) {
        PrefabInstanceInfo info;
        DescribePrefabInstance(*root, info);
        const std::string blocked = ApplyObjectBlockedReason(info);
        for (const PrefabObjectChange& o : s_objChanges) {
            ImGui::PushID(id++);
            ImGui::TextColored(ImVec4(0.82f, 0.88f, 1.0f, 1.0f), "%s", o.description.c_str());
            if (!o.path.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", o.path.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Revert")) { Queue(PrefabReq::RevertObject, root); s_req.object = o; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(o.added ? "Delete this object: the prefab does not have it." : "Bring this object back from the prefab.");
            ImGui::SameLine();
            ImGui::BeginDisabled(!blocked.empty());
            if (ImGui::SmallButton("Apply")) { Queue(PrefabReq::ApplyObject, root); s_req.object = o; }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", blocked.empty() ? (o.added ? "Add this object to the prefab." : "Remove this object from the prefab.") : blocked.c_str());
            ImGui::PopID();
        }
    }
    const int others = (int)s_changes.size() - (int)mine.size();
    if (isRoot && others > 0) ImGui::TextDisabled("%d more change%s in objects below", others, others == 1 ? "" : "s");
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
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

// ---- Right-click menus -------------------------------------------------------------------------------------------------------------------
//
// The rules (which rows, in what order, when one is disabled and why) are in InspectorContextModel.hpp and are tested there; the
// operations behind the rows are in InspectorModel.cpp. This part draws a list of rows and queues or runs what a row asks for:
//   component header   "..." button and right-click   DrawComponentMenu (+ ApplyComponentValueRequests after the component loop)
//   Transform header   right-click                    DrawTransformMenu
//   a field            right-click                    FieldMark (the generic card and every hand-drawn field that matches a property)
//   object header bar  right-click                    DrawObjectHeaderMenu
//   empty space        right-click                    DrawInspectorBackgroundMenu
// Changes to the list of components (move, duplicate, add, remove) are queued and made once the component loop is done, because
// the loop is walking that list; value changes and selection of the same object are made at once.

namespace {

namespace ctx = okay::inspectorctx;

InspectorMenuHooks s_menuHooks;
ComponentClip s_clip;              // Copy Values: Paste Values and Paste As New Component use it
ObjectClip s_objClip;              // Copy All Component Values
TransformClip s_tclip;             // Copy Transform Values
bool s_addComponentRequest = false;
ctx::ClickRouter s_router;         // hands each right click to one menu: a field's, or the empty space's (editor/InspectorClickRules.hpp)
ctx::ClipboardText s_fieldClip;    // the system clipboard as it was when the open field menu was opened: read once, not every frame

enum class Act {
    None, CopyValues, PasteValues, Reset, Duplicate, PasteNew, Toggle, Move, Remove,
    CopyAllValues, PasteAllValues, ResetTransform, SelectParent, SelectPrefabRoot, SelectPrefabAsset, SelectGuid
};
struct Req {
    Act act = Act::None;
    Component* comp = nullptr;      // the component the menu belongs to (null for the object-level rows)
    ctx::MoveKind move = ctx::MoveKind::Up;
    std::string text;               // SelectGuid: the object to select
} s_menuReq;

void Ask(Act a, Component* c = nullptr, ctx::MoveKind m = ctx::MoveKind::Up, std::string text = {}) {
    s_menuReq = Req(); s_menuReq.act = a; s_menuReq.comp = c; s_menuReq.move = m; s_menuReq.text = std::move(text);
}

// The objects an action reaches: the whole selection when `go` is in it, else just `go`.
std::vector<GameObject*> SelectedObjects(okay::editor::EditorState& ed, GameObject* go) {
    std::vector<GameObject*> objs;
    for (GameObject* g : ed.MultiSelection()) if (g && !g->IsDestroyed()) objs.push_back(g);
    if (std::find(objs.begin(), objs.end(), go) == objs.end()) objs = {go};
    return objs;
}

// The same kind of component, in the same position among components of that kind, on each selected object that has one.
struct SameKindSet { std::vector<GameObject*> objs; std::vector<Component*> comps; };
SameKindSet SameKindOnSelection(okay::editor::EditorState& ed, GameObject* go, const Component* c) {
    SameKindSet out;
    int idx = 0;
    for (const auto& up : go->Components()) { if (up.get() == c) break; if (up && typeid(*up) == typeid(*c)) ++idx; }
    for (GameObject* g : SelectedObjects(ed, go)) {
        int n = 0;
        for (const auto& up : g->Components())
            if (up && typeid(*up) == typeid(*c)) { if (n++ == idx) { out.objs.push_back(g); out.comps.push_back(up.get()); break; } }
    }
    return out;
}

// Only compares addresses: safe for a component that was freed since the menu queued the request.
bool OnObject(const GameObject& go, const Component* c) {
    for (const auto& up : go.Components()) if (up.get() == c) return true;
    return false;
}

// A path as the Project panel can find it: as stored, else relative to the project folder.
std::string ResolveProjectPath(const std::string& p) {
    std::error_code ec;
    if (p.empty() || std::filesystem::exists(p, ec)) return p;
    if (s_ed && !s_ed->projectDir().empty()) {
        const std::filesystem::path q = std::filesystem::path(s_ed->projectDir()) / p;
        if (std::filesystem::exists(q, ec)) return q.string();
    }
    return p;
}

using SubFn = std::function<std::vector<ctx::Item>(const std::string&)>;
using ActFn = std::function<void(const std::string&)>;

// Draw a model menu: separators, rows (greyed with the reason as a tooltip when disabled), submenus. `slot` draws the prefab rows.
void DrawItems(const std::vector<ctx::Item>& items, const SubFn& sub, const ActFn& act, const std::function<void()>& slot) {
    for (const ctx::Item& it : items) {
        if (it.separator) { ImGui::Separator(); continue; }
        if (it.id == ctx::kPrefabSlot) { if (slot) slot(); continue; }
        const std::string& tip = it.enabled ? it.tip : it.reason;
        if (!it.submenu.empty()) {
            const bool open = ImGui::BeginMenu(it.label.c_str(), it.enabled);
            if (!it.enabled && !tip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tip.c_str());
            if (open) { if (sub) DrawItems(sub(it.submenu), sub, act, slot); ImGui::EndMenu(); }
            continue;
        }
        const bool hit = ImGui::MenuItem(it.label.c_str(), it.shortcut.empty() ? nullptr : it.shortcut.c_str(), it.checked, it.enabled);
        if (!tip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tip.c_str());
        if (hit && it.enabled) act(it.id);
    }
}

void SetCardsOpen(bool open) { if (s_menuHooks.setCardsOpen) s_menuHooks.setCardsOpen(open); }

ctx::ValueKind KindOf(PropertyType t) {
    switch (t) {
        case PropertyType::Bool: return ctx::ValueKind::Bool;
        case PropertyType::Int: return ctx::ValueKind::Int;
        case PropertyType::Float: return ctx::ValueKind::Float;
        case PropertyType::String: return ctx::ValueKind::String;
        case PropertyType::Vec2: return ctx::ValueKind::Vec2;
        case PropertyType::Vec3: return ctx::ValueKind::Vec3;
        case PropertyType::Vec4: return ctx::ValueKind::Vec4;
        case PropertyType::Quat: return ctx::ValueKind::Quat;
        case PropertyType::Color: return ctx::ValueKind::Color;
        case PropertyType::Enum: return ctx::ValueKind::Enum;
        case PropertyType::AssetPath: return ctx::ValueKind::Asset;
        case PropertyType::ObjectRef: return ctx::ValueKind::Object;
        default: return ctx::ValueKind::None;
    }
}

}   // namespace

void SetInspectorMenuHooks(InspectorMenuHooks hooks) { s_menuHooks = std::move(hooks); }
bool TakeAddComponentRequest() { const bool r = s_addComponentRequest; s_addComponentRequest = false; return r; }

// ---- Component header ----

void DrawComponentMenu(okay::Component* comp, bool removable, okay::Component** toRemove) {
    if (!comp || !s_ed || !comp->gameObject) return;
    okay::editor::EditorState& ed = *s_ed;
    GameObject* go = comp->gameObject;
    const SameKindSet same = SameKindOnSelection(ed, go, comp);
    const ComponentSlot slot = SlotOf(*go, *comp);
    const auto* script = dynamic_cast<const ScriptComponent*>(comp);
    const auto* vscript = dynamic_cast<const VisualScriptComponent*>(comp);
    const std::string scriptPath = script ? script->Path() : vscript ? vscript->Path() : std::string();

    ctx::ComponentMenuState st;
    st.typeName = ComponentDisplayName(*comp);
    st.typeKey = ComponentSerializerRegistry::TypeKey(*comp);
    st.kind = script ? ctx::ComponentKind::Script : vscript ? ctx::ComponentKind::VisualScript : ctx::ComponentKind::Plain;
    st.hasScriptFile = !scriptPath.empty();
    st.selectionCount = std::max(1, (int)same.comps.size());
    st.index = slot.index; st.count = slot.count;
    st.enabled = comp->enabled;
    st.removable = removable;
    st.neededBy = NeededByName(*go, *comp);
    st.hasValues = HasRegisteredProperties(*comp);
    {
        std::string why;
        st.canAddAnother = CanAddAnother(*comp, &why);   // "only one per object" is the Add Component list's own table (editor/AddComponentRules.hpp)
        st.noAnotherReason = why;
    }
    // Would a copy be the same? Tried once when the menu opens (it loads the object's saved text a few times), only for a kind that
    // could be duplicated at all, and not at all for one that is only allowed once.
    if (st.kind == ctx::ComponentKind::Plain && st.hasValues && st.canAddAnother) {
        static const Component* s_checkedComp = nullptr;
        static CopyCheck s_check;
        if (ImGui::IsWindowAppearing() || s_checkedComp != comp) { s_check = CheckCopyIsExact(*go, *comp); s_checkedComp = comp; }
        st.copyExact = s_check.exact;
        st.copyNote = s_check.note;
    }
    st.prefabChanged = PrefabChangedFor(comp);
    if (s_clip.Valid()) {
        st.clipType = TypeDisplayName(s_clip.type);
        std::string why;
        st.pasteNewObjects = PasteAsNewTargets(SelectedObjects(ed, go), s_clip, &why);   // every selected object that can take it, not only those with this kind
        st.clipCanAdd = st.pasteNewObjects > 0;
        st.clipBlockedReason = why;
    }

    const ActFn act = [&](const std::string& id) {
        if (id == "comp.toggleEnabled") Ask(Act::Toggle, comp);
        else if (id == "comp.moveUp") Ask(Act::Move, comp, ctx::MoveKind::Up);
        else if (id == "comp.moveDown") Ask(Act::Move, comp, ctx::MoveKind::Down);
        else if (id == "comp.moveTop") Ask(Act::Move, comp, ctx::MoveKind::Top);
        else if (id == "comp.moveBottom") Ask(Act::Move, comp, ctx::MoveKind::Bottom);
        else if (id == "comp.duplicate") Ask(Act::Duplicate, comp);
        else if (id == "comp.copy") Ask(Act::CopyValues, comp);
        else if (id == "comp.paste") Ask(Act::PasteValues, comp);
        else if (id == "comp.pasteNew") Ask(Act::PasteNew, comp);
        else if (id == "comp.reset") Ask(Act::Reset, comp);
        else if (id == "comp.expandAll") SetCardsOpen(true);
        else if (id == "comp.collapseAll") SetCardsOpen(false);
        else if (id == "comp.editScript") { if (s_menuHooks.editScript) s_menuHooks.editScript(comp); }
        else if (id == "comp.revealScript") { if (s_hooks.selectAsset) s_hooks.selectAsset(ResolveProjectPath(scriptPath)); }
        else if (id == "comp.scriptDocs") { if (s_menuHooks.showScriptReference) s_menuHooks.showScriptReference(); }
        else if (id == "comp.copyTypeName") ImGui::SetClipboardText(st.typeKey.c_str());
        else if (id == "comp.remove") {
            if (same.comps.size() > 1) Ask(Act::Remove, comp);   // several objects: after the loop
            else if (toRemove) *toRemove = comp;                 // one: the Inspector's own removal path
        }
    };
    DrawItems(ctx::ComponentMenu(st), nullptr, act, [&] { PrefabComponentMenu(comp); });
}

// ---- Transform header ----

void DrawTransformMenu(okay::editor::EditorState& ed, okay::GameObject* go) {
    if (!go || !go->transform) return;
    const std::vector<GameObject*> objs = SelectedObjects(ed, go);
    std::vector<ctx::TransformObjectState> states;
    for (GameObject* g : objs) {
        const unsigned at = TransformAtDefault(*g->transform);
        ctx::TransformObjectState o;
        o.camera = ScaleIsLocked(*g);   // its scale cannot change: the scale rows look at the other selected objects only
        o.posAtDefault = (at & kTransformPosition) != 0;
        o.rotAtDefault = (at & kTransformRotation) != 0;
        o.scaleAtDefault = (at & kTransformScale) != 0;
        states.push_back(o);
    }
    const ctx::TransformMenuState st = ctx::TransformStateFor(states, s_tclip.valid);

    const auto pasteParts = [&](unsigned parts) {
        ed.PushUndo("Paste Transform");
        for (GameObject* g : objs) PasteTransform(s_tclip, *g->transform, ScaleIsLocked(*g) ? (parts & ~kTransformScale) : parts);
        ed.dirty = true;
    };
    const auto resetParts = [&](unsigned parts) {
        ed.PushUndo("Reset Transform");
        for (GameObject* g : objs) ResetTransform(*g, parts);   // a camera keeps its scale
        ed.dirty = true;
    };
    const ActFn act = [&](const std::string& id) {
        if (id == "xform.copy") { s_tclip = CopyTransform(*go->transform); ConsoleLog("Copied the Transform of " + go->name); }
        else if (id == "xform.paste") pasteParts(kTransformAll);
        else if (id == "xform.pastePos") pasteParts(kTransformPosition);
        else if (id == "xform.pasteRot") pasteParts(kTransformRotation);
        else if (id == "xform.pasteScale") pasteParts(kTransformScale);
        else if (id == "xform.resetAll") resetParts(kTransformAll);
        else if (id == "xform.resetPos") resetParts(kTransformPosition);
        else if (id == "xform.resetRot") resetParts(kTransformRotation);
        else if (id == "xform.resetScale") resetParts(kTransformScale);
        else if (id == "comp.expandAll") SetCardsOpen(true);
        else if (id == "comp.collapseAll") SetCardsOpen(false);
    };
    DrawItems(ctx::TransformMenu(st), [&](const std::string& sub) { return ctx::SubmenuItems(sub, st); }, act, nullptr);
}

// ---- Object header bar and empty space ----

namespace {

ctx::ObjectMenuState ObjectStateOf(okay::editor::EditorState& ed, GameObject* go) {
    ctx::ObjectMenuState st;
    st.selectionCount = (int)SelectedObjects(ed, go).size();
    if (go->transform && go->transform->Parent() && go->transform->Parent()->gameObject) {
        st.hasParent = true;
        st.parentName = go->transform->Parent()->gameObject->name;
    }
    PrefabInstanceInfo info;
    if (DescribePrefabInstance(*go, info)) {
        st.inPrefab = true;
        st.isPrefabRoot = info.root == go;
        st.prefabMissing = !info.exists;
        st.prefabName = info.name;
    }
    st.hasObjectClip = s_objClip.Valid();
    st.clipObjectName = s_objClip.from;
    st.componentCount = SlotOf(*go, *go->transform).count;
    if (s_clip.Valid()) {
        st.clipType = TypeDisplayName(s_clip.type);
        std::string why;
        st.pasteNewObjects = PasteAsNewTargets(SelectedObjects(ed, go), s_clip, &why);   // the objects it will really be added to
        st.clipCanAdd = st.pasteNewObjects > 0;
        st.clipBlockedReason = why;
    }
    return st;
}

void ObjectAction(const std::string& id, GameObject* go) {
    if (id == "obj.copyAll") Ask(Act::CopyAllValues);
    else if (id == "obj.pasteAll") Ask(Act::PasteAllValues);
    else if (id == "obj.resetTransform") Ask(Act::ResetTransform);
    else if (id == "obj.selectParent") Ask(Act::SelectParent);
    else if (id == "obj.selectPrefabRoot") Ask(Act::SelectPrefabRoot);
    else if (id == "obj.selectPrefabAsset") Ask(Act::SelectPrefabAsset);
    else if (id == "obj.copyName") ImGui::SetClipboardText(go->name.c_str());
    else if (id == "obj.copyPath") ImGui::SetClipboardText(go->Path().c_str());
    else if (id == "obj.addComponent") s_addComponentRequest = true;
    else if (id == "comp.pasteNew") Ask(Act::PasteNew);
    else if (id == "comp.expandAll") SetCardsOpen(true);
    else if (id == "comp.collapseAll") SetCardsOpen(false);
}

}   // namespace

void DrawObjectHeaderMenu(okay::editor::EditorState& ed, okay::GameObject* go) {
    if (!go) return;
    // The last item is the header bar. A right click on it opens the menu, except on the name box (it has the text menu of its own).
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !s_nameFieldHovered && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax())) {
        ImGui::OpenPopup("##objectctx");
        s_router.Claim(ImGui::GetFrameCount());   // this click is the header's: the empty-space menu must not replace it
    }
    if (ImGui::BeginPopup("##objectctx")) {
        DrawItems(ctx::ObjectMenu(ObjectStateOf(ed, go)), nullptr, [&](const std::string& id) { ObjectAction(id, go); }, nullptr);
        ImGui::EndPopup();
    }
}

void DrawInspectorBackgroundMenu(okay::editor::EditorState& ed, okay::GameObject* go) {
    if (!go) return;
    // Opened by hand (not BeginPopupContextWindow): that one opens whenever no widget is under the mouse, which is also true over a field's
    // label and the gaps between rows, and its popup would replace the field's own menu opened earlier in the same frame.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ctx::RightClick click{ImGui::IsMouseReleased(ImGuiMouseButton_Right), mouse.x, mouse.y};
    if (s_router.BackgroundOpens(ImGui::GetFrameCount(), click, ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup), ImGui::IsAnyItemHovered()))
        ImGui::OpenPopup("##inspectorbg");
    if (ImGui::BeginPopup("##inspectorbg")) {
        DrawItems(ctx::BackgroundMenu(ObjectStateOf(ed, go)), nullptr, [&](const std::string& id) { ObjectAction(id, go); }, nullptr);
        ImGui::EndPopup();
    }
}

// ---- Running what the menus queued ----

void ApplyComponentValueRequests(okay::editor::EditorState& ed, okay::GameObject* go) {
    const Req r = s_menuReq;
    s_menuReq = Req();
    if (r.act == Act::None || !go) return;
    Component* c = r.comp;
    if (c && !OnObject(*go, c)) return;   // gone since the click
    const std::vector<GameObject*> objs = SelectedObjects(ed, go);
    const std::string name = c ? ComponentDisplayName(*c) : std::string();
    const auto plural = [](int n, const char* one) { return std::to_string(n) + " " + one + (n == 1 ? "" : "s"); };

    switch (r.act) {
        case Act::CopyValues: {
            if (!c) return;
            s_clip = CopyComponentValues(*c);
            // Would a new component made from these values be the same? (Paste As New is only offered when it would.)
            const CopyCheck check = CheckCopyIsExact(*go, *c);
            s_clip.exact = check.exact;
            s_clip.inexactNote = check.note;
            ConsoleLog("Copied the values of " + name + (check.exact ? std::string() : " (not copied: " + check.note + "; Paste As New is not offered for it)"));
            return;
        }
        case Act::PasteValues:
        case Act::Reset: {
            if (!c) return;
            const SameKindSet same = SameKindOnSelection(ed, go, c);   // the same component on every selected object that has one
            ed.PushUndo(r.act == Act::Reset ? "Reset " + name : "Paste Values onto " + name);
            int written = 0;
            for (Component* t : same.comps) written += r.act == Act::PasteValues ? PasteComponentValues(s_clip, *t) : ResetComponentValues(*t);
            if (written > 0) ed.dirty = true;
            ConsoleLog(std::string(r.act == Act::PasteValues ? "Pasted values onto " : "Reset ") + name + (written == 0 ? " (nothing changed)" : ""));
            return;
        }
        case Act::Toggle: {
            if (!c) return;
            const SameKindSet same = SameKindOnSelection(ed, go, c);
            const bool on = !c->enabled;
            ed.PushUndo(std::string(on ? "Enable " : "Disable ") + name);
            for (Component* t : same.comps) t->enabled = on;
            ed.dirty = true;
            ConsoleLog(std::string(on ? "Enabled " : "Disabled ") + name);
            return;
        }
        case Act::Move: {
            if (!c) return;
            const ComponentSlot slot = SlotOf(*go, *c);
            if (ctx::MoveDelta(r.move, slot.index, slot.count) == 0) return;
            ed.PushUndo("Move " + name);
            if (MoveComponentTo(*go, *c, r.move)) ed.dirty = true;
            return;
        }
        case Act::Duplicate: {
            if (!c) return;
            const SameKindSet same = SameKindOnSelection(ed, go, c);
            ed.PushUndo("Duplicate " + name);
            int made = 0;
            for (std::size_t i = 0; i < same.comps.size(); ++i) if (DuplicateComponent(*same.objs[i], *same.comps[i])) ++made;
            if (made > 0) ed.dirty = true;
            ConsoleLog(made == 0 ? "Could not duplicate " + name : "Duplicated " + name + (same.comps.size() > 1 ? " on " + plural(made, "object") : std::string()), made == 0 ? 1 : 0);
            return;
        }
        case Act::PasteNew: {
            if (!s_clip.Valid()) return;
            const std::string kind = TypeDisplayName(s_clip.type);
            ed.PushUndo("Paste " + kind + " As New Component");
            int made = 0, skipped = 0;
            for (GameObject* g : objs) { if (PasteComponentAsNew(*g, s_clip)) ++made; else ++skipped; }
            if (made > 0) ed.dirty = true;
            ConsoleLog(made == 0 ? "Could not add a " + kind + " here" : "Added a new " + kind + " with the copied values" + (objs.size() > 1 ? " to " + plural(made, "object") : std::string()) +
                                       (skipped > 0 ? " (" + plural(skipped, "object") + " skipped: it cannot be added there)" : std::string()), made == 0 ? 1 : 0);
            return;
        }
        case Act::Remove: {   // several objects selected: the same component on each (the one object case goes through the Inspector's own removal)
            if (!c) return;
            const SameKindSet same = SameKindOnSelection(ed, go, c);
            ed.PushUndo("Remove " + name);
            int removed = 0, blocked = 0;
            for (std::size_t i = 0; i < same.comps.size(); ++i) {
                if (!NeededByName(*same.objs[i], *same.comps[i]).empty()) { ++blocked; continue; }
                if (same.objs[i]->RemoveComponent(same.comps[i])) ++removed;
            }
            if (removed > 0) ed.dirty = true;
            ConsoleLog("Removed " + name + " from " + plural(removed, "object") + (blocked > 0 ? " (" + plural(blocked, "object") + " kept it: another component needs it)" : std::string()), removed == 0 ? 1 : 0);
            return;
        }
        case Act::CopyAllValues:
            s_objClip = CopyObjectValues(*go);
            ConsoleLog("Copied the component values of " + go->name);
            return;
        case Act::PasteAllValues: {
            if (!s_objClip.Valid()) return;
            ed.PushUndo("Paste All Component Values");
            int written = 0;
            for (GameObject* g : objs) written += PasteObjectValues(s_objClip, *g);
            if (written > 0) ed.dirty = true;
            ConsoleLog(written == 0 ? "Pasted component values (nothing changed)" : "Pasted component values from " + s_objClip.from + " onto " + plural((int)objs.size(), "object"));
            return;
        }
        case Act::ResetTransform:
            ed.PushUndo("Reset Transform");
            for (GameObject* g : objs) ResetTransform(*g, kTransformAll);   // a camera keeps its scale
            ed.dirty = true;
            return;
        case Act::SelectParent:
            if (go->transform && go->transform->Parent() && go->transform->Parent()->gameObject) ed.Select(go->transform->Parent()->gameObject);
            return;
        case Act::SelectPrefabRoot:
        case Act::SelectPrefabAsset: {
            PrefabInstanceInfo info;
            if (!DescribePrefabInstance(*go, info) || !info.root) return;
            if (r.act == Act::SelectPrefabRoot) ed.Select(info.root);
            else if (s_hooks.selectAsset) s_hooks.selectAsset(ResolveProjectPath(info.source));
            return;
        }
        case Act::SelectGuid:
            if (GameObject* o = ed.scene().FindByGuid(r.text)) ed.Select(o);
            return;
        case Act::None: return;
    }
}

// ---- A field: the prefab marker and the right-click menu ----

static void FieldMark(const char* label, const PropertyDesc* knownDesc, const float* wholeRowTopY) {
    if (!s_markComp) return;
    const PrefabChange* ch = nullptr;
    std::vector<std::string> fields;
    if (!s_changeOf.empty() && s_changesRoot) fields = OverriddenFieldsFor(label, &ch);
    const bool overridden = !fields.empty() && ch;
    if (overridden) {   // a blue bar at the left edge of the row, so overridden fields stand out in a long card
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        const float x = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x - 5.0f;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, mn.y), ImVec2(x + 3.0f, mx.y), IM_COL32(90, 165, 255, 255), 1.5f);
    }
    // The popup's name must be this field's own. The last item of a labelled field is often its label text, which has no id (0), so
    // the label and the component are mixed in: two fields (two rows of one card, or the same row of two Lights) never share a menu.
    char pid[48];
    {
        const ImGuiID item = ImGui::GetItemID() ? ImGui::GetItemID() : ImGui::GetID(label);
        std::snprintf(pid, sizeof(pid), "##fieldctx%08x%08x", (unsigned)item, (unsigned)((std::uintptr_t)s_markComp >> 4));
    }

    // The property this field shows. A box that was greyed out on purpose (a camera's scale) is not ours to change: no value rows.
    const ImGuiItemFlags itemFlags = GImGui->LastItemData.ItemFlags;
    std::shared_ptr<const ComponentTypeInfo> info;   // looked up only when a menu is opened or open: this runs for every field every frame
    const auto findDesc = [&]() -> const PropertyDesc* {
        if (knownDesc) return knownDesc;
        if (!info) info = PropertyRegistry::FindFor(*s_markComp);
        if (!info) return nullptr;
        const PropertyDesc* d = FindPropertyForLabel(*info, label);
        if (d && (itemFlags & ImGuiItemFlags_Disabled)) return nullptr;
        return d;
    };

    // Who gets this right click: this field, or the empty-space menu (editor/InspectorClickRules.hpp). A field with a menu takes the
    // click when the mouse is on its row (label, gap and box) and tells the empty-space menu, so exactly one menu opens.
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const ctx::RightClick click{ImGui::IsMouseReleased(ImGuiMouseButton_Right), mouse.x, mouse.y};
        const int frame = ImGui::GetFrameCount();
        const auto hasMenu = [&] { return overridden || findDesc() != nullptr; };
        bool opens;
        if (wholeRowTopY) {   // the full width of the row: from where it started down to the bottom of its last item
            const ctx::Rect row = ctx::FieldRowRect(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x,
                                                    ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x, *wholeRowTopY,
                                                    ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().y, ImGui::GetStyle().FramePadding.y);
            opens = s_router.FieldOpensInRow(frame, click, ImGui::IsWindowHovered(), row, hasMenu);
        } else {
            opens = s_router.FieldOpens(frame, click, ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled), hasMenu);
        }
        if (opens) {
            ImGui::OpenPopup(pid);
            s_fieldClip = ctx::ReadClipboard(ImGui::GetClipboardText());   // once, now: not every frame the menu stays open
        }
    }
    if (!ImGui::BeginPopup(pid)) return;

    // The prefab rows: what differs, and Revert / Apply.
    const auto prefabRows = [&] {
        if (!overridden) return;
        const std::string pname = std::filesystem::path(s_changesRoot->prefabSource).filename().string();
        ImGui::TextColored(PrefabBlue(), "Overridden from %s", pname.c_str());
        for (const std::string& f : fields)
            for (const PrefabFieldDiff& d : ch->fields)
                if (d.name == f) ImGui::TextDisabled("%s: %s -> %s", f.c_str(), d.prefabValue.c_str(), d.instanceValue.c_str());
        ImGui::Separator();
        std::vector<PrefabTarget> targets;
        for (const std::string& f : fields) targets.push_back(TargetOf(*ch, f));
        if (ch->wholeRecordOnly || ch->added || ch->removed) targets = {TargetOf(*ch)};
        if (ImGui::MenuItem("Revert Property")) Queue(PrefabReq::Revert, s_changesRoot, targets);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put this property back to the prefab's value.");
        if (ImGui::MenuItem("Apply Property to Prefab")) Queue(PrefabReq::Apply, s_changesRoot, targets);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Write this value into %s. Other instances that did not change it take it too.", pname.c_str());
    };

    const PropertyDesc* d = findDesc();
    if (!info) info = PropertyRegistry::FindFor(*s_markComp);
    GameObject* go = s_markComp->gameObject;
    if (!d || !info || !go || !s_ed) { prefabRows(); ImGui::EndPopup(); return; }

    okay::editor::EditorState& ed = *s_ed;
    const int idx = ComponentIndexOf(*go, *s_markComp);
    // The objects a write reaches: the same component on every selected object that has one, except cameras for a scale (a camera
    // cannot be scaled, as the Scale row of the Transform and Reset Transform already say).
    int skipped = 0;
    const std::vector<GameObject*> targets = WriteTargets(SameKindOnSelection(ed, go, s_markComp).objs, info->name, *d, &skipped);
    if (targets.empty()) { prefabRows(); ImGui::EndPopup(); return; }
    const FieldState fs = ReadField(targets, info->name, idx, *d);

    ctx::PropertyMenuState st;
    st.path = ComponentPath(info->name, idx) + "." + d->name;
    st.name = PrettyName(d->name);
    st.kind = KindOf(d->type);
    st.selectionCount = std::max(1, (int)targets.size());
    st.skippedCount = skipped;
    st.skippedWhy = skipped > 0 ? "a camera cannot be scaled" : "";
    st.readOnly = d->IsReadOnly();
    const PropertyValue def = DefaultValueOf(*info, *d);
    st.hasDefault = def.IsValid();
    st.atDefault = def.IsValid() && fs.have > 0 && !fs.mixed && fs.value == def;
    const PasteCheck paste = CheckPaste(*d, s_fieldClip, fs, ed.projectDir());   // from the text read when the menu opened
    st.clipboardFits = paste.ok;
    st.clipRefusal = paste.ok ? std::string() : paste.reason;
    st.clipPreview = paste.ok ? paste.preview : std::string();
    st.clipIsSame = paste.ok && paste.same;
    st.hasRange = d->hasRange; st.rangeMin = d->rangeMin; st.rangeMax = d->rangeMax;
    const bool isText = d->type == PropertyType::String || d->type == PropertyType::AssetPath || d->type == PropertyType::ObjectRef;
    st.valueEmpty = isText && !fs.mixed && fs.value.AsString().empty();
    st.overridden = overridden;
    st.objectResolves = d->type == PropertyType::ObjectRef && !fs.mixed && ed.scene().FindByGuid(fs.value.AsString()) != nullptr;

    const auto write = [&](const PropertyValue& v, unsigned axisMask = 0) {
        if (axisMask == 0) {   // every selected object already holds it: no undo step for a change that is not one
            PropertyValue norm;
            if (d->Normalize(v, norm) && fs.have > 0 && !fs.mixed && fs.value == norm) { ConsoleLog(st.name + " already has that value"); return; }
        }
        ed.PushUndo("Set " + st.name);
        const int n = axisMask ? WriteFieldAxes(targets, info->name, idx, *d, v, axisMask) : WriteField(targets, info->name, idx, *d, v);
        if (n > 0) ed.dirty = true;
    };
    const auto allParts = [&](float v) -> PropertyValue {   // every part of a number or vector set to v
        switch (d->type) {
            case PropertyType::Int: return PropertyValue::MakeInt((long long)v);
            case PropertyType::Vec2: return PropertyValue::MakeVec2({v, v});
            case PropertyType::Vec3: return PropertyValue::MakeVec3({v, v, v});
            case PropertyType::Vec4: return PropertyValue::MakeVec4({v, v, v, v});
            default: return PropertyValue::MakeFloat(v);
        }
    };
    const ActFn act = [&](const std::string& id) {
        if (id == "prop.reset") {
            ed.PushUndo("Reset " + st.name);
            if (ResetField(targets, info->name, idx, *d) > 0) ed.dirty = true;
        }
        else if (id == "prop.copy") { ImGui::SetClipboardText(fs.value.ToString().c_str()); ConsoleLog("Copied " + st.name); }
        else if (id == "prop.paste") { if (paste.ok) write(paste.value); }   // the value the tooltip showed, from the text read when the menu opened
        else if (id == "prop.copyPath") { ImGui::SetClipboardText(st.path.c_str()); ConsoleLog("Copied " + st.path); }
        else if (id == "prop.zero") write(allParts(0.0f));
        else if (id == "prop.one") write(allParts(1.0f));
        else if (id == "prop.identity") write(PropertyValue::MakeQuat(Quat::Identity));
        else if (id == "prop.white") write(PropertyValue::MakeColor(Color{1, 1, 1, 1}), 0x7u);   // red, green, blue: each object keeps its own alpha
        else if (id == "prop.clear")
            write(d->type == PropertyType::AssetPath ? PropertyValue::MakeAssetPath("") : d->type == PropertyType::ObjectRef ? PropertyValue::MakeObjectRef("") : PropertyValue::MakeString(""));
        else if (id == "prop.showAsset") { if (s_hooks.selectAsset) s_hooks.selectAsset(ResolveProjectPath(fs.value.AsString())); }
        else if (id == "prop.selectObject") Ask(Act::SelectGuid, nullptr, ctx::MoveKind::Up, fs.value.AsString());
    };
    DrawItems(ctx::PropertyMenu(st), nullptr, act, prefabRows);
    ImGui::EndPopup();
}


} // namespace oked
