#include "FpHandsWindow.hpp"
#include "okay/Components/FpHandsPreview.hpp"
#include "okay/Components/HoldableObject.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "imgui.h"
#include <SDL.h>
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
    if (ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Checkbox("Enabled", &s.enabled)) dirty = true;
        if (Vec("Position", s.position, 0.005f)) dirty = true;
        if (Vec("Rotation", s.rotation, 0.5f)) dirty = true;
        if (ImGui::DragFloat("Uniform scale", &s.uniformScale, 0.01f, 0.2f, 3.0f)) dirty = true;
        if (Vec("Scale", s.scale, 0.01f)) dirty = true;
        if (Vec("Size", s.size, 0.01f)) dirty = true;
        if (ImGui::Checkbox("Custom colour", &s.useColour)) dirty = true;
        if (s.useColour && Col("Colour", s.colour)) dirty = true;
        if (!s.useColour) ImGui::TextDisabled("Colour comes from the character's skin.");
        if (ImGui::Checkbox("Sleeve", &s.sleeve)) dirty = true;
        if (s.sleeve) {
            if (ImGui::DragFloat("Sleeve length", &s.sleeveLength, 0.01f, 0.04f, 0.8f)) dirty = true;
            if (Col("Sleeve colour", s.sleeveColour)) dirty = true;
        }
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
        ImGui::TextDisabled("Open the preview to edit an example item's grip points,\n"
                            "or pick something up to edit that item.");
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

void DrawFpHandsBody(EditorState& ed, FirstPersonHands* h) {
    if (!h) return;
    bool dirty = false;
    ImGui::TextWrapped("Nothing shows in first person until this player is holding an item. "
                       "Other players always see the full character.");
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

    Section("Player View (first person)");
    if (ImGui::Checkbox("Show when empty", &h->showWhenEmpty)) dirty = true;
    if (ImGui::Checkbox("Show while running", &h->showWhileRunning)) dirty = true;
    const char* modes[] = {"One hand (right)", "One hand (left)", "Two hands"};
    int mode = (int)h->holdMode;
    if (ImGui::Combo("Hold mode", &mode, modes, 3)) {
        h->holdMode = (FirstPersonHands::HoldMode)mode;
        dirty = true;
    }
    ImGui::TextDisabled("A two-handed item uses both hands even if the mode is one hand.");
    if (ImGui::DragFloat("Bob amount", &h->bobAmount, 0.01f, 0.0f, 3.0f)) dirty = true;
    if (ImGui::DragFloat("Sway amount", &h->swayAmount, 0.01f, 0.0f, 3.0f)) dirty = true;
    if (ImGui::DragFloat("Punch swing amount", &h->punchAmount, 0.01f, 0.0f, 3.0f)) dirty = true;
    if (ImGui::DragFloat("Hand field of view", &h->handFov, 0.2f, 0.0f, 120.0f)) dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 uses the camera. A smaller value draws the hands larger.");
    if (Vec("Held item offset", h->itemOffset, 0.005f)) dirty = true;
    if (Vec("Held item rotation", h->itemRotation, 0.5f)) dirty = true;
    if (ImGui::DragFloat("Held item scale", &h->itemScale, 0.01f, 0.1f, 3.0f)) dirty = true;
    HandSection("Right hand", "rh", h->right, dirty);
    HandSection("Left hand", "lh", h->left, dirty);
    if (ImGui::SmallButton("Reset##player")) { h->ResetPlayerView(); dirty = true; }
    ImGui::SameLine();
    ImGui::TextDisabled("Player view");

    Section("Others View (third person)");
    ImGui::TextWrapped("Where the held item sits on the full body, and where each hand grips it. "
                       "The arms follow the item.");
    if (Vec("Item offset", h->othersItemOffset, 0.005f)) dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Side, up, forward, in metres, added to the hold.");
    if (Vec("Item rotation", h->othersItemRotation, 0.5f)) dirty = true;
    if (ImGui::DragFloat("Arm lift", &h->othersArmLift, 0.005f, -1.0f, 1.0f)) dirty = true;
    if (ImGui::DragFloat("Arm reach", &h->othersArmForward, 0.005f, -1.0f, 1.0f)) dirty = true;
    ImGui::TextDisabled("Grip points live on the item.");
    HoldableObject* grips = HeldItem(h);
    if (!grips && g_open) grips = g_preview.ExampleItem();
    GripFields(grips, dirty);
    if (ImGui::SmallButton("Reset##others")) { h->ResetOthersView(); dirty = true; }
    ImGui::SameLine();
    ImGui::TextDisabled("Others view");

    if (dirty) ed.dirty = true;
}

void DrawFpHandsPreviewWindow(EditorState& ed) {
    if (!g_open) return;
    ImGui::SetNextWindowSize(ImVec2(920.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("First Person Hands", &g_open)) { ImGui::End(); return; }
    FirstPersonHands* live = FindHands(ed);
    if (!live) {
        ImGui::TextDisabled("The First Person Hands component is not in this scene.");
        ImGui::End();
        return;
    }
    const char* items[] = {"One-handed block", "Two-handed crate", "Empty hands"};
    if (ImGui::Combo("Preview item", &g_example, items, 3)) g_preview.SetExample(g_example);
    ImGui::SameLine();
    ImGui::TextDisabled(ed.isPlaying() ? "Play" : "Edit");

    g_preview.CopyFrom(*live);
    g_preview.SetExample(g_example);
    if (HoldableObject* held = HeldItem(live))
        if (HoldableObject* example = g_preview.ExampleItem())
            if (held != example) {
                example->leftGrip = held->leftGrip;
                example->rightGrip = held->rightGrip;
            }
    g_preview.Step(1);
    // The first frames ease the hand in. Catch up once when the window opens.
    static int warmedFor = -1;
    if (warmedFor != g_example) {
        g_preview.Step(36);
        warmedFor = g_example;
    }

    const int W = 420, H = 280;
    auto player = g_preview.Render(true, W, H);
    auto other = g_preview.Render(false, W, H);
    // Each upload shares g_texW, so size both the same and upload independently.
    SDL_Texture* pt = nullptr;
    SDL_Texture* ot = nullptr;
    if (oked::g_sdlRenderer) {
        auto up = [&](SDL_Texture*& slot, const std::vector<std::uint32_t>& px) {
            if (!slot) {
                slot = SDL_CreateTexture(oked::g_sdlRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, W, H);
                if (slot) SDL_SetTextureBlendMode(slot, SDL_BLENDMODE_NONE);
            }
            if (slot) SDL_UpdateTexture(slot, nullptr, px.data(), W * 4);
            return slot;
        };
        pt = up(g_playerTex, player);
        ot = up(g_otherTex, other);
    }
    ImGui::Columns(2, "fphandsviews", false);
    ImGui::TextUnformatted("Player view (first person)");
    if (pt) ImGui::Image((ImTextureID)pt, ImVec2((float)W, (float)H));
    ImGui::NextColumn();
    ImGui::TextUnformatted("Others view (third person)");
    if (ot) ImGui::Image((ImTextureID)ot, ImVec2((float)W, (float)H));
    ImGui::Columns(1);
    ImGui::TextDisabled("Both views follow the Inspector. Change a value and they update.");
    ImGui::End();
}

} // namespace okay::editor
