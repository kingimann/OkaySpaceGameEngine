#pragma once
// The Input Actions window (View > Editors & Tools > Input Actions): author an input asset (.okayinput) - control schemes, action maps,
// actions with their value type, bindings (single, 2D vector, 3D vector, 1D axis, chord) with scheme group and processors, and
// interactions - with a validation list, plus a live view of the player's actions (value, phase, which binding drives it).
// All logic is okay::ActionEditor (engine); this file only draws it with ImGui. Written for this project.
#include "okay/Input/ActionEditor.hpp"
#include "okay/Input/InputRuntime.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <string>

namespace okay::editor {

namespace detail {   // state the window keeps; a screenshot tool can select an action or a tab through these
inline std::string& IaSelected() { static std::string s; return s; }
inline int& IaForceTab() { static int t = -1; return t; }   // 0 Action, 1 Check, 2 Live; -1 = leave alone
inline ImGuiTabItemFlags IaTabFlag(int tab) { return IaForceTab() == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; }
}

inline void DrawInputActionsWindow(bool* open, const std::string& projectDir) {
    if (!open || !*open) return;
    static ActionEditor ed;
    static bool started = false;
    std::string& sel = detail::IaSelected();
    static std::string status, filePath;
    static char nameBuf[64] = "";
    static char partBuf[160] = "", procBuf[160] = "", interBuf[96] = "";
    if (!started) { started = true; ed.NewFromDefault(); ed.dirty = false; filePath = projectDir.empty() ? "default.okayinput" : projectDir + "/Assets/input.okayinput"; }
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Input Actions", open)) { ImGui::End(); return; }

    // File row.
    char fp[512]; std::snprintf(fp, sizeof fp, "%s", filePath.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##inputfile", fp, sizeof fp)) filePath = fp;
    if (ImGui::Button("Load")) { std::string err; status = ed.Load(filePath, &err) ? "Loaded." : err; sel.clear(); }
    ImGui::SameLine();
    if (ImGui::Button("Save")) status = ed.SaveAs(filePath) ? "Saved." : "Could not write the file.";
    ImGui::SameLine();
    if (ImGui::Button("New from default")) { ed.NewFromDefault(); sel.clear(); status = "Started from the default asset."; }
    ImGui::SameLine();
    if (ImGui::Button("Migrate Input Actions")) { ed.NewFromMigration(); sel.clear(); status = "Built from the actions the game defines now."; }
    ImGui::SameLine();
    if (ImGui::Button("Use in game")) {   // the running game's players start from this asset
        if (!ed.path.empty() || ed.SaveAs(filePath)) status = InputRuntime::LoadAsset(ed.path) ? "The game now uses this asset." : "Could not load it into the game.";
    }
    if (ed.dirty) { ImGui::SameLine(); ImGui::TextDisabled("(unsaved changes)"); }
    if (!status.empty()) ImGui::TextDisabled("%s", status.c_str());

    ImGui::Separator();
    ImGui::BeginChild("##ia_left", ImVec2(ImGui::GetFontSize() * 21.0f, 0), true);
    ImGui::TextDisabled("Schemes");
    for (const ControlScheme& c : ed.set.Schemes()) ImGui::BulletText("%s%s%s%s%s", c.name.c_str(), c.keyboard ? " keyboard" : "", c.mouse ? " mouse" : "", c.gamepad ? " gamepad" : "", c.touch ? " touch" : "");
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##newname", "name (no spaces)", nameBuf, sizeof nameBuf);
    if (ImGui::SmallButton("+ Map")) status = ed.AddMap(nameBuf) ? "Map added." : "Map name taken, empty or has a space.";
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Scheme")) status = ed.AddScheme(nameBuf, true, true, false, false) ? "Scheme added (keyboard and mouse; edit the file for others)." : "Scheme name taken, empty or has a space.";
    ImGui::Separator();
    for (const ActionMap& m : ed.set.Maps()) {
        ImGui::Text("%s  (priority %d%s%s)", m.name.c_str(), m.priority, m.consume ? ", consumes" : "", m.enabled ? "" : ", off");
        for (const ActionDef& a : ed.set.Actions()) {
            if (a.map != m.name) continue;
            const std::string label = "    " + a.name + "  [" + ValueTypeName(a.type) + "]##" + m.name + a.name;
            if (ImGui::Selectable(label.c_str(), sel == a.name)) { sel = a.name; procBuf[0] = 0; }
        }
    }
    ImGui::Separator();
    static int newType = 0;
    ImGui::SetNextItemWidth(110); ImGui::Combo("##newtype", &newType, "button\0axis\0vector2\0vector3\0");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Action")) {
        const ValueType vt = newType == 0 ? ValueType::Button : newType == 1 ? ValueType::Axis1D : newType == 2 ? ValueType::Vector2 : ValueType::Vector3;
        const std::string map = ed.set.Maps().empty() ? std::string() : ed.set.Maps().front().name;
        if (ed.AddAction(nameBuf, vt, map)) { sel = nameBuf; status = "Action added to " + (map.empty() ? std::string("the default map") : map) + "."; }
        else status = "Action name taken, empty or has a space.";
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##ia_right", ImVec2(0, 0), true);
    if (ImGui::BeginTabBar("##iatabs")) {
        if (ImGui::BeginTabItem("Action", nullptr, detail::IaTabFlag(0))) {
            ActionDef* a = ed.set.Find(sel);
            if (!a) ImGui::TextDisabled("Pick an action on the left.");
            else {
                for (const std::string& l : ed.Describe(sel)) ImGui::TextWrapped("%s", l.c_str());
                ImGui::Separator();
                for (int b = 0; b < (int)a->bindings.size(); ++b) {
                    ImGui::PushID(b);
                    BindingSpec& bs = a->bindings[(std::size_t)b];
                    ImGui::Text("Binding %d", b + 1);
                    for (int p = 0; p < (int)bs.parts.size(); ++p) {
                        ImGui::PushID(p);
                        std::snprintf(partBuf, sizeof partBuf, "%s", bs.parts[(std::size_t)p].c_str());
                        ImGui::SetNextItemWidth(std::max(70.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * (float)bs.parts.size()) / (float)bs.parts.size() - 4.0f));
                        if (ImGui::InputText("##part", partBuf, sizeof partBuf, ImGuiInputTextFlags_EnterReturnsTrue))
                            status = ed.SetPart(sel, b, p, partBuf) ? "Input changed." : "That is not an input (try key:w, pad:a, stick:left, mouse:left).";
                        ImGui::PopID();
                        if (p + 1 < (int)bs.parts.size()) ImGui::SameLine();
                    }
                    std::snprintf(procBuf, sizeof procBuf, "%s", ProcessorsToString(bs.processors).c_str());
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                    if (ImGui::InputText("processors", procBuf, sizeof procBuf, ImGuiInputTextFlags_EnterReturnsTrue))
                        status = ed.SetProcessors(sel, b, procBuf) ? "Processors changed." : "Cannot read the processors (deadzone(0.2) invert(y) scale(2) ...).";
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove binding")) { ed.RemoveBinding(sel, b); ImGui::PopID(); break; }
                    ImGui::PopID();
                }
                const char* kinds[] = {"+ single", "+ 2D vector", "+ 3D vector", "+ 1D axis", "+ chord"};
                const BindingSpec::Kind ks[] = {BindingSpec::Kind::Single, BindingSpec::Kind::Vector2, BindingSpec::Kind::Vector3, BindingSpec::Kind::Axis1D, BindingSpec::Kind::Chord};
                for (int i = 0; i < 5; ++i) { if (i) ImGui::SameLine(); if (ImGui::SmallButton(kinds[i])) ed.AddBinding(sel, ks[i]); }
                ImGui::Separator();
                std::snprintf(interBuf, sizeof interBuf, "%s", "");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputTextWithHint("##inter", "add interaction (Enter): hold(0.5)  tap(0.2)  multitap(2)  repeat(0.1)  press  release", interBuf, sizeof interBuf, ImGuiInputTextFlags_EnterReturnsTrue))
                    status = ed.AddInteraction(sel, interBuf) ? "Interaction added." : "Cannot read the interaction.";
                for (int i = 0; i < (int)a->interactions.size(); ++i) {
                    ImGui::PushID(1000 + i);
                    ImGui::BulletText("%s", a->interactions[(std::size_t)i].ToString().c_str()); ImGui::SameLine();
                    if (ImGui::SmallButton("x")) { ed.RemoveInteraction(sel, i); ImGui::PopID(); break; }
                    ImGui::PopID();
                }
                if (ImGui::SmallButton("Delete action")) { ed.Remove(sel); sel.clear(); }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Check", nullptr, detail::IaTabFlag(1))) {
            const auto problems = ed.Validate();
            if (problems.empty()) ImGui::TextDisabled("No problems found.");
            for (const std::string& p : problems) ImGui::BulletText("%s", p.c_str());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Live", nullptr, detail::IaTabFlag(2))) {
            for (const std::string& l : ControllerDebugLines()) ImGui::TextWrapped("%s", l.c_str());
            ImGui::Separator();
            PlayerInputManager& pm = InputRuntime::Players();
            if (pm.Count() == 0) ImGui::TextDisabled("No local player yet: a script that reads an action (ia_x, ia_down ...) joins one, or call ia_join().");
            for (int p = 0; p < pm.Count(); ++p) {
                const ActionSet* s = pm.Player(p);
                if (!s) continue;
                ImGui::Text("Player %d", p + 1);
                for (const std::string& l : ActionSetDebugLines(*s)) ImGui::TextWrapped("%s", l.c_str());
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace okay::editor
