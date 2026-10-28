#include "FpHandsWindow.hpp"
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
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off by default. The hand is only the brick until this is on.");
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

    Section("Player View");
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
    ImGui::TextWrapped("Where the held item sits on the full body, and where each hand grips it. "
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
    ImGui::SetNextWindowSize(ImVec2(860.0f, 460.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("First Person Hands", &g_open)) { ImGui::End(); return; }
    FirstPersonHands* live = FindHands(ed);
    if (!live) {
        ImGui::TextDisabled("The First Person Hands component is not in this scene.");
        ImGui::End();
        return;
    }
    const char* items[] = {"One-handed block", "Two-handed crate", "Empty hands"};
    ImGui::SetNextItemWidth(220.0f);
    const bool picked = ImGui::Combo("Preview item", &g_example, items, 3);

    HoldableObject* held = HeldItem(live);
    const int sig = PreviewSig(*live, g_example, held);
    static int cachedSig = 0x7fffffff;
    static int texW = 0, texH = 0;
    // Small software frames. Rebuilt only when the example or a setting changes,
    // so an open preview does not re-render the character every editor frame.
    const int W = 200, H = 132;
    const bool dirty = picked || sig != cachedSig || !g_playerTex || texW != W || texH != H;
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
    if (g_playerTex) ImGui::Image((ImTextureID)g_playerTex, shown);
    ImGui::NextColumn();
    ImGui::TextUnformatted("Others view (third person)");
    if (g_otherTex) ImGui::Image((ImTextureID)g_otherTex, shown);
    ImGui::Columns(1);
    ImGui::TextDisabled("Both views follow the Inspector. Change a value and they update.");
    ImGui::End();
}

} // namespace okay::editor
