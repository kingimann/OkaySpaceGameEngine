#include "FpHandsWindow.hpp"
#include "NavigationWindow.hpp"   // InspectorRowsOkayUI (stage 8)
#include "../shared/InspectorFieldsModel.hpp"
#include "okay/Components/FpHandsPreview.hpp"
#include "okay/Components/HoldableObject.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "imgui.h"
#include <SDL.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace oked { extern SDL_Renderer* g_sdlRenderer; }

namespace okay::editor {
namespace {

bool g_open = false;
std::string g_object;
int g_example = 0;
FpHandsPreview g_preview;
SDL_Texture* g_playerTex = nullptr;
SDL_Texture* g_otherTex = nullptr;

void Help(const char* text) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

void Section(const char* label) {
    ImGui::Spacing();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 1.0f), ImVec2(p.x + 3.0f, p.y + h),
                                              IM_COL32(120, 170, 230, 255), 1.5f);
    ImGui::Indent(9.0f);
    ImGui::TextUnformatted(label);
    ImGui::Unindent(9.0f);
    ImGui::Separator();
}

bool Vec(const char* label, Vec3& v, float speed) {
    float a[3] = {v.x, v.y, v.z};
    if (!ImGui::DragFloat3(label, a, speed)) return false;
    v = {a[0], a[1], a[2]};
    return true;
}
bool Col(const char* label, Color& c) {
    float a[3] = {c.r, c.g, c.b};
    if (!ImGui::ColorEdit3(label, a)) return false;
    c = {a[0], a[1], a[2], 1.0f};
    return true;
}

void HandSection(const char* title, const char* id, FirstPersonHands::HandSide& s, bool& dirty) {
    ImGui::PushID(id);
    if (ImGui::CollapsingHeader(title)) {
        if (ImGui::Checkbox("Enabled", &s.enabled)) dirty = true;
        if (Vec("Position", s.position, 0.005f)) dirty = true;
        if (Vec("Rotation", s.rotation, 0.5f)) dirty = true;
        if (ImGui::DragFloat("Uniform scale", &s.uniformScale, 0.01f, 0.2f, 3.0f)) dirty = true;
        if (Vec("Scale", s.scale, 0.01f)) dirty = true;
        if (ImGui::Checkbox("Custom colour", &s.useColour)) dirty = true;
        if (s.useColour && Col("Colour", s.colour)) dirty = true;
        if (!s.useColour) ImGui::TextDisabled("Colour comes from the character's skin.");
        if (ImGui::DragFloat("Width", &s.size.x, 0.01f, 0.2f, 3.0f)) dirty = true;
        if (ImGui::DragFloat("Height", &s.size.y, 0.01f, 0.2f, 3.0f)) dirty = true;
        if (ImGui::DragFloat("Depth", &s.size.z, 0.01f, 0.2f, 3.0f)) dirty = true;
        if (Vec("Wrist offset", s.wristOffset, 0.005f)) dirty = true;
        if (Vec("Wrist rotation", s.wristRotation, 0.5f)) dirty = true;
        if (ImGui::DragFloat("Elbow bend", &s.elbowBend, 0.5f, -80.0f, 120.0f)) dirty = true;
        if (ImGui::DragFloat("Reach", &s.reach, 0.005f, -0.4f, 0.6f)) dirty = true;
        if (ImGui::Checkbox("Forearm", &s.forearm)) dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The arm under the hand. The Old look and Minimal presets turn it off.");
        if (ImGui::Checkbox("Sleeve and forearm use the shirt colour", &s.matchShirt)) dirty = true;
        if (s.forearm) {
            if (ImGui::DragFloat("Forearm length", &s.forearmLength, 0.01f, 0.05f, 1.2f)) dirty = true;
            if (ImGui::DragFloat("Forearm thickness", &s.forearmThickness, 0.005f, 0.02f, 0.4f)) dirty = true;
            if (Col("Forearm colour", s.forearmColour)) dirty = true;
        }
        if (ImGui::Checkbox("Sleeve", &s.sleeve)) dirty = true;
        if (s.sleeve) {
            if (ImGui::DragFloat("Sleeve length", &s.sleeveLength, 0.01f, 0.04f, 0.8f)) dirty = true;
            if (Col("Sleeve colour", s.sleeveColour)) dirty = true;
        }
    }
    ImGui::PopID();
}

void OthersArmSection(const char* title, const char* id, FirstPersonHands::OthersArm& a, bool& dirty) {
    ImGui::PushID(id);
    if (ImGui::CollapsingHeader(title)) {
        if (ImGui::Checkbox("Forearm", &a.forearm)) dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The body's forearm. On so the arm still connects to the hand.");
        if (ImGui::DragFloat("Forearm length", &a.forearmLength, 0.01f, 0.05f, 1.2f)) dirty = true;
        if (ImGui::DragFloat("Forearm thickness", &a.forearmThickness, 0.005f, 0.02f, 0.4f)) dirty = true;
        if (Col("Forearm colour", a.forearmColour)) dirty = true;
        if (ImGui::DragFloat("Hand width", &a.handSize.x, 0.01f, 0.2f, 3.0f)) dirty = true;
        if (ImGui::DragFloat("Hand height", &a.handSize.y, 0.01f, 0.2f, 3.0f)) dirty = true;
        if (ImGui::DragFloat("Hand depth", &a.handSize.z, 0.01f, 0.2f, 3.0f)) dirty = true;
        if (Vec("Wrist offset", a.wristOffset, 0.005f)) dirty = true;
        if (Vec("Wrist rotation", a.wristRotation, 0.5f)) dirty = true;
        if (ImGui::DragFloat("Elbow bend", &a.elbowBend, 0.5f, 0.0f, 120.0f)) dirty = true;
        if (ImGui::DragFloat("Reach", &a.reach, 0.005f, -0.4f, 0.6f)) dirty = true;
    }
    ImGui::PopID();
}

HoldableObject* HeldItem(FirstPersonHands* hands) {
    if (!hands || !hands->gameObject) return nullptr;
    auto* hold = hands->gameObject->GetComponent<ProceduralObjectHolding>();
    if (!hold || !hold->IsHolding() || !hold->Held()) return nullptr;
    return hold->Held()->GetComponent<HoldableObject>();
}

void GripFields(HoldableObject* item, bool& dirty) {
        if (!item) {
        Help("Open the preview to edit an example item's grip points, or pick something up to edit that item.");
        return;
    }
    auto grip = [&](const char* name, const char* id, HoldableObject::Grip& g) {
        ImGui::PushID(id);
        if (ImGui::Checkbox(name, &g.enabled)) dirty = true;
        if (g.enabled) {
            if (Vec("Grip position", g.position, 0.01f)) dirty = true;
            if (Vec("Grip rotation", g.rotation, 0.5f)) dirty = true;
        }
        ImGui::PopID();
    };
    int st = (int)item->style;
    const char* styles =
        "Automatic\0Custom\0Pistol\0Rifle\0Box\0Flashlight\0Sword\0Cup\0Heavy\0"
        "One hand right\0One hand left\0Two handed\0Carry in front\0Over the shoulder\0";
    if (ImGui::Combo("Hold style", &st, styles)) {
        item->style = (HoldableObject::Style)st;
        dirty = true;
    }
    if (ImGui::SmallButton("One hand right")) { item->ApplyPreset(HoldableObject::Preset::OneHandRight); dirty = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("One hand left")) { item->ApplyPreset(HoldableObject::Preset::OneHandLeft); dirty = true; }
    if (ImGui::SmallButton("Two handed")) { item->ApplyPreset(HoldableObject::Preset::TwoHanded); dirty = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Carry in front")) { item->ApplyPreset(HoldableObject::Preset::CarryFront); dirty = true; }
    if (ImGui::SmallButton("Over shoulder")) { item->ApplyPreset(HoldableObject::Preset::OverShoulder); dirty = true; }
    if (Vec("In-hand offset", item->holdOffset, 0.01f)) dirty = true;
    if (Vec("In-hand rotation", item->holdRotation, 0.5f)) dirty = true;
    if (ImGui::DragFloat("In-hand scale", &item->inHandScale, 0.01f, 0.1f, 3.0f)) dirty = true;
    if (Vec("FP offset", item->fpOffset, 0.005f)) dirty = true;
    if (Vec("FP rotation", item->fpRotation, 0.5f)) dirty = true;
    if (ImGui::DragFloat("FP scale", &item->fpScale, 0.01f, 0.1f, 3.0f)) dirty = true;
    if (Vec("TP offset", item->tpOffset, 0.005f)) dirty = true;
    if (Vec("TP rotation", item->tpRotation, 0.5f)) dirty = true;
    grip("Left grip", "lg", item->leftGrip);
    grip("Right grip", "rg", item->rightGrip);
    if (ImGui::SmallButton("Reset grips")) {
        item->leftGrip = {};
        item->rightGrip = {};
        item->leftGrip.enabled = item->WantsTwoHands();
        item->rightGrip.enabled = item->WantsTwoHands();
        item->leftGrip.position = {-0.2f, 0.0f, 0.0f};
        item->rightGrip.position = {0.2f, 0.0f, 0.0f};
        dirty = true;
    }
}

FirstPersonHands* FindHands(EditorState& ed) {
    if (g_object.empty()) return nullptr;
    GameObject* g = ed.scene().Find(g_object);
    return g ? g->GetComponent<FirstPersonHands>() : nullptr;
}

} // namespace

// ---- the body as OkayUI field rows (stage 8) ------------------------------------------------------------------------------------------
// Written from DrawFpHandsBody and its helpers above, row for row: Help is a wrapping note, Section the small section heading, a
// CollapsingHeader a collapsing header row, Vec a three-cell drag, Col a three-channel colour. The preset list has no per-preset tooltip
// in OkayUI (its help line under the list says the same).
namespace {
struct FpRows {
    okaypanels::InspFieldList L;
    std::vector<Color*> cols;     // the colour rows' values: their alpha is set to 1 on a change, as Col does
    std::vector<int> colRows;
    void Vec(const char* label, Vec3& v, float speed) { L.Drag(label, &v.x, 3, speed); }
    void Col(const char* label, Color& c) { colRows.push_back(L.Color(label, &c.r, 3)); cols.push_back(&c); }
};
void HandRows(FpRows& f, const char* title, FirstPersonHands::HandSide& s) {
    auto& L = f.L;
    L.Header(title);
    L.Check("Enabled", &s.enabled);
    f.Vec("Position", s.position, 0.005f); f.Vec("Rotation", s.rotation, 0.5f);
    L.Drag("Uniform scale", &s.uniformScale, 1, 0.01f, 0.2f, 3.0f); f.Vec("Scale", s.scale, 0.01f);
    L.Check("Custom colour", &s.useColour);
    if (s.useColour) f.Col("Colour", s.colour); else L.Note("Colour comes from the character's skin.");
    L.Drag("Width", &s.size.x, 1, 0.01f, 0.2f, 3.0f); L.Drag("Height", &s.size.y, 1, 0.01f, 0.2f, 3.0f); L.Drag("Depth", &s.size.z, 1, 0.01f, 0.2f, 3.0f);
    f.Vec("Wrist offset", s.wristOffset, 0.005f); f.Vec("Wrist rotation", s.wristRotation, 0.5f);
    L.Drag("Elbow bend", &s.elbowBend, 1, 0.5f, -80.0f, 120.0f); L.Drag("Reach", &s.reach, 1, 0.005f, -0.4f, 0.6f);
    L.Check("Forearm", &s.forearm); L.Tip("The arm under the hand. The Old look and Minimal presets turn it off.");
    L.Check("Sleeve and forearm use the shirt colour", &s.matchShirt);
    if (s.forearm) {
        L.Drag("Forearm length", &s.forearmLength, 1, 0.01f, 0.05f, 1.2f); L.Drag("Forearm thickness", &s.forearmThickness, 1, 0.005f, 0.02f, 0.4f);
        f.Col("Forearm colour", s.forearmColour);
    }
    L.Check("Sleeve", &s.sleeve);
    if (s.sleeve) { L.Drag("Sleeve length", &s.sleeveLength, 1, 0.01f, 0.04f, 0.8f); f.Col("Sleeve colour", s.sleeveColour); }
    L.HeaderEnd();
}
void OthersArmRows(FpRows& f, const char* title, FirstPersonHands::OthersArm& a) {
    auto& L = f.L;
    L.Header(title);
    L.Check("Forearm", &a.forearm); L.Tip("The body's forearm. On so the arm still connects to the hand.");
    L.Drag("Forearm length", &a.forearmLength, 1, 0.01f, 0.05f, 1.2f); L.Drag("Forearm thickness", &a.forearmThickness, 1, 0.005f, 0.02f, 0.4f);
    f.Col("Forearm colour", a.forearmColour);
    L.Drag("Hand width", &a.handSize.x, 1, 0.01f, 0.2f, 3.0f); L.Drag("Hand height", &a.handSize.y, 1, 0.01f, 0.2f, 3.0f);
    L.Drag("Hand depth", &a.handSize.z, 1, 0.01f, 0.2f, 3.0f);
    f.Vec("Wrist offset", a.wristOffset, 0.005f); f.Vec("Wrist rotation", a.wristRotation, 0.5f);
    L.Drag("Elbow bend", &a.elbowBend, 1, 0.5f, 0.0f, 120.0f); L.Drag("Reach", &a.reach, 1, 0.005f, -0.4f, 0.6f);
    L.HeaderEnd();
}
} // namespace

bool DrawFpHandsBodyOkayUI(EditorState& ed, FirstPersonHands* h, bool& remove) {
    using namespace okaypanels;
    remove = false;
    if (!h) return false;
    static const char* const kStyles[] = {"Automatic", "Custom", "Pistol", "Rifle", "Box", "Flashlight", "Sword", "Cup", "Heavy",
                                          "One hand right", "One hand left", "Two handed", "Carry in front", "Over the shoulder"};
    static const char* const kModes[] = {"One hand (right)", "One hand (left)", "Two hands"};
    FpRows f;
    InspFieldList& L = f.L;
    L.rows.reserve(160);
    L.Note("Your own arm in first person. Other players always see the full character. "
           "Pick a Preset to compare looks; the Preview window updates as you change it.", false, true);
    static std::vector<const char*> presets, presetTips;
    if (presets.empty())
        for (int i = 0; i < FirstPersonHands::kPresetCount; ++i) {
            presets.push_back(FirstPersonHands::PresetName((FirstPersonHands::Preset)i));
            presetTips.push_back(FirstPersonHands::PresetHelp((FirstPersonHands::Preset)i));
        }
    int preset = (int)h->preset;
    const int kPreset = L.Combo("Preset", &preset, presets.data(), (int)presets.size());
    L.Last().preview = FirstPersonHands::PresetName(h->preset);
    L.Last().itemTips = presetTips.data();   // each preset's help on hover, as Dear ImGui's list (stage 9)
    L.Note(FirstPersonHands::PresetHelp(h->preset), false, true);
    const int kNormal = L.Button("Use normal first-person arms"); L.Tip("Sets every player-view number to the Normal preset. Ctrl+Z puts it back.");
    L.SameLine(); const int kOld = L.Button("Old look"); L.Tip("The look before presets, for comparing.");
    L.Check("Left click swings the arm", &h->swingOnClick); L.Tip("Others see the same swing on the real arm.");
    L.Separator();
    const int kPreview = L.Button("Open Preview");
    L.SameLine(); const int kApply = L.Button("Apply"); L.Tip("Play mode restores the scene when you press Stop. Apply copies these values back.");
    L.SmallSection("Player View");
    L.Check("Show when empty", &h->showWhenEmpty); L.Check("Show while running", &h->showWhileRunning);
    int mode = (int)h->holdMode;
    const int kMode = L.Combo("Hold mode", &mode, kModes, 3);
    L.Note("A two-handed item uses both hands even if the mode is one hand.", false, true);
    L.Drag("Bob amount", &h->bobAmount, 1, 0.01f, 0.0f, 3.0f); L.Drag("Sway amount", &h->swayAmount, 1, 0.01f, 0.0f, 3.0f);
    L.Drag("Punch", &h->punchAmount, 1, 0.01f, 0.0f, 3.0f); L.Tip("Punch swing amount.");
    L.Drag("Hand FOV", &h->handFov, 1, 0.2f, 0.0f, 120.0f); L.Tip("Hand field of view. 0 uses the camera. A smaller value draws the hands larger.");
    f.Vec("Item offset", h->itemOffset, 0.005f); L.Tip("Held item offset.");
    f.Vec("Item rotation", h->itemRotation, 0.5f); L.Tip("Held item rotation.");
    L.Drag("Item scale", &h->itemScale, 1, 0.01f, 0.1f, 3.0f); L.Tip("Held item scale.");
    HandRows(f, "Left hand", h->left);
    HandRows(f, "Right hand", h->right);
    const int kResetPlayer = L.SmallButton("Reset##player");
    L.SameLine(); L.Note("Player view");
    L.SmallSection("Others View");
    L.Note("Where the held item sits on the full body, and where each hand grips it. The arms follow the item.", false, true);
    f.Vec("Item offset", h->othersItemOffset, 0.005f); L.Tip("Side, up, forward, in metres, added to the hold.");
    f.Vec("Item rotation", h->othersItemRotation, 0.5f);
    L.Drag("Arm lift", &h->othersArmLift, 1, 0.005f, -1.0f, 1.0f); L.Drag("Arm reach", &h->othersArmForward, 1, 0.005f, -1.0f, 1.0f);
    OthersArmRows(f, "Left arm", h->othersLeft);
    OthersArmRows(f, "Right arm", h->othersRight);
    L.Note("Grip points and the hold style live on the item.");
    HoldableObject* item = HeldItem(h);
    if (!item && g_open) item = g_preview.ExampleItem();
    int style = item ? (int)item->style : 0, kStyle = -1, kP[5] = {-1, -1, -1, -1, -1}, kResetGrips = -1;
    if (!item) L.Note("Open the preview to edit an example item's grip points, or pick something up to edit that item.", false, true);
    else {
        kStyle = L.Combo("Hold style", &style, kStyles, 14);
        kP[0] = L.SmallButton("One hand right"); L.SameLine(); kP[1] = L.SmallButton("One hand left");
        kP[2] = L.SmallButton("Two handed"); L.SameLine(); kP[3] = L.SmallButton("Carry in front");
        kP[4] = L.SmallButton("Over shoulder");
        f.Vec("In-hand offset", item->holdOffset, 0.01f); f.Vec("In-hand rotation", item->holdRotation, 0.5f);
        L.Drag("In-hand scale", &item->inHandScale, 1, 0.01f, 0.1f, 3.0f);
        f.Vec("FP offset", item->fpOffset, 0.005f); f.Vec("FP rotation", item->fpRotation, 0.5f);
        L.Drag("FP scale", &item->fpScale, 1, 0.01f, 0.1f, 3.0f);
        f.Vec("TP offset", item->tpOffset, 0.005f); f.Vec("TP rotation", item->tpRotation, 0.5f);
        for (HoldableObject::Grip* g : {&item->leftGrip, &item->rightGrip}) {
            L.Check(g == &item->leftGrip ? "Left grip" : "Right grip", &g->enabled);
            if (g->enabled) { f.Vec("Grip position", g->position, 0.01f); f.Vec("Grip rotation", g->rotation, 0.5f); }
        }
        kResetGrips = L.SmallButton("Reset grips");
    }
    const int kResetOthers = L.SmallButton("Reset##others");
    L.SameLine(); L.Note("Others view");
    const int kRemove = L.SmallButton("Remove##fphs");
    if (!oked::InspectorRowsOkayUI(h, "##okayui_fphands", L)) return false;
    bool dirty = L.AnyChanged();
    for (std::size_t i = 0; i < f.colRows.size(); ++i) if (L[f.colRows[i]].changed) f.cols[i]->a = 1.0f;
    if (L[kPreset].changed && preset >= 0) { ed.PushUndo("First person preset"); h->ApplyPreset((FirstPersonHands::Preset)preset); dirty = true; }
    if (L[kNormal].clicked) { ed.PushUndo("Use normal first-person arms"); h->UseNormalArms(); dirty = true; }
    if (L[kOld].clicked) { ed.PushUndo("First person old look"); h->UseOldLook(); dirty = true; }
    if (L[kPreview].clicked) { g_open = true; g_object = h->gameObject ? h->gameObject->name : ""; }
    if (L[kApply].clicked) { if (ed.isPlaying()) FirstPersonHands::CaptureApplied(ed.scene()); dirty = true; }
    if (L[kMode].changed) h->holdMode = (FirstPersonHands::HoldMode)mode;
    if (L[kResetPlayer].clicked) { h->ResetPlayerView(); dirty = true; }
    if (item) {
        if (L[kStyle].changed) item->style = (HoldableObject::Style)style;
        static const HoldableObject::Preset kPresetOf[5] = {HoldableObject::Preset::OneHandRight, HoldableObject::Preset::OneHandLeft,
                                                            HoldableObject::Preset::TwoHanded, HoldableObject::Preset::CarryFront,
                                                            HoldableObject::Preset::OverShoulder};
        for (int i = 0; i < 5; ++i) if (L[kP[i]].clicked) { item->ApplyPreset(kPresetOf[i]); dirty = true; }
        if (L[kResetGrips].clicked) {
            item->leftGrip = {}; item->rightGrip = {};
            item->leftGrip.enabled = item->WantsTwoHands(); item->rightGrip.enabled = item->WantsTwoHands();
            item->leftGrip.position = {-0.2f, 0.0f, 0.0f}; item->rightGrip.position = {0.2f, 0.0f, 0.0f};
            dirty = true;
        }
    }
    if (L[kResetOthers].clicked) { h->ResetOthersView(); dirty = true; }
    if (L[kRemove].clicked) remove = true;
    if (dirty) ed.dirty = true;
    return true;
}

void DrawFpHandsBody(EditorState& ed, FirstPersonHands* h) {
    if (!h) return;
    bool dirty = false;
    Help("Your own arm in first person. Other players always see the full character. "
         "Pick a Preset to compare looks; the Preview window updates as you change it.");
    {
        // Compare tool: each preset sets every player-view number at once. Ctrl+Z undoes it.
        const auto cur = h->preset;
        if (ImGui::BeginCombo("Preset", FirstPersonHands::PresetName(cur))) {
            for (int i = 0; i < FirstPersonHands::kPresetCount; ++i) {
                const auto pr = (FirstPersonHands::Preset)i;
                if (ImGui::Selectable(FirstPersonHands::PresetName(pr), cur == pr)) {
                    ed.PushUndo("First person preset");
                    h->ApplyPreset(pr);
                    dirty = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", FirstPersonHands::PresetHelp(pr));
            }
            ImGui::EndCombo();
        }
        Help(FirstPersonHands::PresetHelp(cur));
        if (ImGui::Button("Use normal first-person arms")) {
            ed.PushUndo("Use normal first-person arms");
            h->UseNormalArms();
            dirty = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sets every player-view number to the Normal preset. Ctrl+Z puts it back.");
        ImGui::SameLine();
        if (ImGui::Button("Old look")) {
            ed.PushUndo("First person old look");
            h->UseOldLook();
            dirty = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The look before presets, for comparing.");
        if (ImGui::Checkbox("Left click swings the arm", &h->swingOnClick)) dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Others see the same swing on the real arm.");
        ImGui::Separator();
    }
    if (ImGui::Button("Open Preview")) {
        g_open = true;
        g_object = h->gameObject ? h->gameObject->name : "";
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        if (ed.isPlaying()) FirstPersonHands::CaptureApplied(ed.scene());
        dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Play mode restores the scene when you press Stop. Apply copies these values back.");

    Section("Player View");
    if (ImGui::Checkbox("Show when empty", &h->showWhenEmpty)) dirty = true;
    if (ImGui::Checkbox("Show while running", &h->showWhileRunning)) dirty = true;
    const char* modes[] = {"One hand (right)", "One hand (left)", "Two hands"};
    int mode = (int)h->holdMode;
    if (ImGui::Combo("Hold mode", &mode, modes, 3)) {
        h->holdMode = (FirstPersonHands::HoldMode)mode;
        dirty = true;
    }
    Help("A two-handed item uses both hands even if the mode is one hand.");
    if (ImGui::DragFloat("Bob amount", &h->bobAmount, 0.01f, 0.0f, 3.0f)) dirty = true;
    if (ImGui::DragFloat("Sway amount", &h->swayAmount, 0.01f, 0.0f, 3.0f)) dirty = true;
    if (ImGui::DragFloat("Punch", &h->punchAmount, 0.01f, 0.0f, 3.0f)) dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Punch swing amount.");
    if (ImGui::DragFloat("Hand FOV", &h->handFov, 0.2f, 0.0f, 120.0f)) dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hand field of view. 0 uses the camera. A smaller value draws the hands larger.");
    if (Vec("Item offset", h->itemOffset, 0.005f)) dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Held item offset.");
    if (Vec("Item rotation", h->itemRotation, 0.5f)) dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Held item rotation.");
    if (ImGui::DragFloat("Item scale", &h->itemScale, 0.01f, 0.1f, 3.0f)) dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Held item scale.");
    HandSection("Left hand", "lh", h->left, dirty);
    HandSection("Right hand", "rh", h->right, dirty);
    if (ImGui::SmallButton("Reset##player")) { h->ResetPlayerView(); dirty = true; }
    ImGui::SameLine();
    ImGui::TextDisabled("Player view");

    Section("Others View");
    Help("Where the held item sits on the full body, and where each hand grips it. "
         "The arms follow the item.");
    if (Vec("Item offset", h->othersItemOffset, 0.005f)) dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Side, up, forward, in metres, added to the hold.");
    if (Vec("Item rotation", h->othersItemRotation, 0.5f)) dirty = true;
    if (ImGui::DragFloat("Arm lift", &h->othersArmLift, 0.005f, -1.0f, 1.0f)) dirty = true;
    if (ImGui::DragFloat("Arm reach", &h->othersArmForward, 0.005f, -1.0f, 1.0f)) dirty = true;
    OthersArmSection("Left arm", "ola", h->othersLeft, dirty);
    OthersArmSection("Right arm", "ora", h->othersRight, dirty);
    ImGui::TextDisabled("Grip points and the hold style live on the item.");
    HoldableObject* grips = HeldItem(h);
    if (!grips && g_open) grips = g_preview.ExampleItem();
    GripFields(grips, dirty);
    if (ImGui::SmallButton("Reset##others")) { h->ResetOthersView(); dirty = true; }
    ImGui::SameLine();
    ImGui::TextDisabled("Others view");

    if (dirty) ed.dirty = true;
}

int MixFloat(int s, float f) {
    std::uint32_t b = 0;
    std::memcpy(&b, &f, sizeof(b));
    return (int)((std::uint32_t)s * 16777619u ^ b);
}
int MixVec(int s, const Vec3& v) { s = MixFloat(s, v.x); s = MixFloat(s, v.y); return MixFloat(s, v.z); }
int MixGrip(int s, const HoldableObject::Grip& g) {
    s = s * 33 + (g.enabled ? 1 : 0);
    s = MixVec(s, g.position);
    return MixVec(s, g.rotation);
}
int MixHand(int s, const FirstPersonHands::HandSide& h) {
    s = s * 33 + (h.enabled ? 1 : 0);
    s = s * 33 + (h.useColour ? 1 : 0);
    s = s * 33 + (h.sleeve ? 1 : 0);
    s = MixVec(s, h.position);
    s = MixVec(s, h.rotation);
    s = MixFloat(s, h.uniformScale);
    s = MixVec(s, h.scale);
    s = MixVec(s, h.size);
    s = MixFloat(s, h.colour.r); s = MixFloat(s, h.colour.g); s = MixFloat(s, h.colour.b);
    s = MixFloat(s, h.sleeveLength);
    s = MixFloat(s, h.sleeveColour.r); s = MixFloat(s, h.sleeveColour.g); s = MixFloat(s, h.sleeveColour.b);
    s = s * 33 + (h.forearm ? 1 : 0);
    s = s * 33 + (h.matchShirt ? 1 : 0);
    s = MixFloat(s, h.forearmLength);
    s = MixFloat(s, h.forearmThickness);
    s = MixVec(s, h.wristOffset);
    s = MixVec(s, h.wristRotation);
    s = MixFloat(s, h.elbowBend);
    return MixFloat(s, h.reach);
}
int MixOthers(int s, const FirstPersonHands::OthersArm& a) {
    s = s * 33 + (a.forearm ? 1 : 0);
    s = MixFloat(s, a.forearmLength);
    s = MixFloat(s, a.elbowBend);
    s = MixFloat(s, a.reach);
    s = MixVec(s, a.handSize);
    s = MixVec(s, a.wristOffset);
    return MixVec(s, a.wristRotation);
}
int PreviewSig(const FirstPersonHands& h, int example, const HoldableObject* item) {
    int s = example * 17 + (int)h.holdMode;
    s = s * 33 + (h.showWhenEmpty ? 1 : 0);
    s = s * 33 + (h.showWhileRunning ? 1 : 0);
    s = MixFloat(s, h.bobAmount);
    s = MixFloat(s, h.swayAmount);
    s = MixFloat(s, h.punchAmount);
    s = MixFloat(s, h.handFov);
    s = MixFloat(s, h.itemScale);
    s = MixVec(s, h.itemOffset);
    s = MixVec(s, h.itemRotation);
    s = MixVec(s, h.othersItemOffset);
    s = MixVec(s, h.othersItemRotation);
    s = MixFloat(s, h.othersArmLift);
    s = MixFloat(s, h.othersArmForward);
    s = MixHand(s, h.right);
    s = MixHand(s, h.left);
    s = MixOthers(s, h.othersRight);
    s = MixOthers(s, h.othersLeft);
    if (item) {
        s = s * 17 + (int)item->style;
        s = MixFloat(s, item->inHandScale);
        s = MixFloat(s, item->fpScale);
        s = MixVec(s, item->fpOffset);
        s = MixVec(s, item->fpRotation);
        s = MixVec(s, item->tpOffset);
        s = MixVec(s, item->tpRotation);
        s = MixGrip(s, item->leftGrip);
        s = MixGrip(s, item->rightGrip);
    }
    return s;
}

void DrawFpHandsPreviewWindow(EditorState& ed) {
    if (!g_open) return;
    // Sit in the scene area: right of the Hierarchy, left of the Inspector.
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = 680.0f;
    float x = display.x - width - 300.0f;
    if (x < 250.0f) x = 250.0f;
    ImGui::SetNextWindowPos(ImVec2(x, 64.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(width, 520.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin("First Person Hands", &g_open)) { ImGui::End(); return; }
    FirstPersonHands* live = FindHands(ed);
    if (!live) {
        ImGui::TextDisabled("The First Person Hands component is not in this scene.");
        ImGui::End();
        return;
    }
    const char* items[] = {"One-handed block", "Two-handed crate", "Empty hands"};
    ImGui::SetNextItemWidth(180.0f);
    const bool picked = ImGui::Combo("Preview item", &g_example, items, 3);
    auto preset = [&](const char* label, HoldableObject::Preset p) {
        ImGui::SameLine();
        if (ImGui::SmallButton(label)) {
            g_preview.RefitExample(p);
            g_example = (p == HoldableObject::Preset::OneHandRight || p == HoldableObject::Preset::OneHandLeft) ? 0 : 1;
            if (HoldableObject* liveItem = HeldItem(live)) liveItem->ApplyPreset(p);
            else if (HoldableObject* ex = g_preview.ExampleItem()) {
                // The preview item is the one on screen. Keep the live component in step
                // when a held prop is not selected.
                (void)ex;
            }
            ed.dirty = true;
            return true;
        }
        return false;
    };
    bool presetChanged = false;
    presetChanged = preset("One hand R", HoldableObject::Preset::OneHandRight) || presetChanged;
    presetChanged = preset("One hand L", HoldableObject::Preset::OneHandLeft) || presetChanged;
    presetChanged = preset("Two hands", HoldableObject::Preset::TwoHanded) || presetChanged;
    presetChanged = preset("Carry front", HoldableObject::Preset::CarryFront) || presetChanged;
    presetChanged = preset("Shoulder", HoldableObject::Preset::OverShoulder) || presetChanged;
    ImGui::TextDisabled("Arm look");
    for (int i = 0; i < FirstPersonHands::kPresetCount; ++i) {
        const auto pr = (FirstPersonHands::Preset)i;
        if (i) ImGui::SameLine();
        const bool on = live->preset == pr;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(FirstPersonHands::PresetName(pr))) {
            ed.PushUndo("First person preset");
            live->ApplyPreset(pr);
            presetChanged = true;
            ed.dirty = true;
        }
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", FirstPersonHands::PresetHelp(pr));
    }
    if (ImGui::Button("Use normal first-person arms")) {
        ed.PushUndo("Use normal first-person arms");
        live->UseNormalArms();
        presetChanged = true;
        ed.dirty = true;
    }
    bool foreYou = live->right.forearm || live->left.forearm;
    bool foreThem = live->othersRight.forearm && live->othersLeft.forearm;
    if (ImGui::Checkbox("Your forearm", &foreYou)) {
        live->right.forearm = live->left.forearm = foreYou;
        ed.dirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The bar under the hand in the view you see. Off shows the hand only.");
    ImGui::SameLine();
    if (ImGui::Checkbox("Their forearm", &foreThem)) {
        live->othersRight.forearm = live->othersLeft.forearm = foreThem;
        ed.dirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The body's forearm in the view everyone else sees.");

    HoldableObject* held = HeldItem(live);
    const int sig = PreviewSig(*live, g_example, held);
    static int cachedSig = 0x7fffffff;
    static int texW = 0, texH = 0;
    // Small software frames. Rebuilt only when the example or a setting changes,
    // so an open preview does not re-render the character every editor frame.
    const int W = 200, H = 132;
    const bool dirty = picked || presetChanged || sig != cachedSig || !g_playerTex || texW != W || texH != H;
    if (dirty) {
        g_preview.CopyFrom(*live);
        g_preview.SetExample(g_example);
        if (held)
            if (HoldableObject* example = g_preview.ExampleItem())
                if (held != example) {
                    example->style = held->style;
                    example->twoHanded = held->twoHanded;
                    example->leftGrip = held->leftGrip;
                    example->rightGrip = held->rightGrip;
                    example->holdOffset = held->holdOffset;
                    example->holdRotation = held->holdRotation;
                    example->inHandScale = held->inHandScale;
                    example->fpOffset = held->fpOffset;
                    example->fpRotation = held->fpRotation;
                    example->fpScale = held->fpScale;
                    example->tpOffset = held->tpOffset;
                    example->tpRotation = held->tpRotation;
                }
        g_preview.Step(48);
        auto player = g_preview.Render(true, W, H);
        auto other = g_preview.Render(false, W, H);
        if (oked::g_sdlRenderer) {
            auto up = [&](SDL_Texture*& slot, const std::vector<std::uint32_t>& px) {
                if (slot && (texW != W || texH != H)) { SDL_DestroyTexture(slot); slot = nullptr; }
                if (!slot) {
                    slot = SDL_CreateTexture(oked::g_sdlRenderer, SDL_PIXELFORMAT_ABGR8888,
                                             SDL_TEXTUREACCESS_STATIC, W, H);
                    if (slot) SDL_SetTextureBlendMode(slot, SDL_BLENDMODE_NONE);
                }
                if (slot) SDL_UpdateTexture(slot, nullptr, px.data(), W * 4);
                return slot;
            };
            up(g_playerTex, player);
            up(g_otherTex, other);
            texW = W; texH = H;
        }
        cachedSig = sig;
    }
    const ImVec2 shown(400.0f, 264.0f);
    ImGui::Columns(2, "fphandsviews", false);
    ImGui::TextUnformatted("Player view (first person)");
    if (g_example == 2) Help("No hands shown when empty.");
    if (g_playerTex) ImGui::Image((ImTextureID)g_playerTex, shown);
    ImGui::NextColumn();
    ImGui::TextUnformatted("Others view (third person)");
    if (g_otherTex) ImGui::Image((ImTextureID)g_otherTex, shown);
    ImGui::Columns(1);
    ImGui::TextDisabled("Both views follow the Inspector. Change a value and they update.");
    ImGui::End();
}

} // namespace okay::editor
