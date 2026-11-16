#include "GuidedPanel.hpp"
#include "EditorWidgets.hpp"
#include "imgui.h"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Modeling/Guided.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Modeling/PolyMesh.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace oked::guided_ui {

namespace fs = std::filesystem;
namespace gd = okay::modeling::guided;
using okay::GameObject;
using okay::MeshRenderer;
using okay::modeling::PolyMesh;

namespace {

// ---- State that lives between frames ---------------------------------------------------------------------
struct State {
    int category = 0;
    std::string templateId;             // the template whose designs are shown
    std::string startId;                // the chosen starting design ("" = the first)
    int userStart = -1;                 // index into the user templates when one is chosen
    char objName[64] = "Fence";

    // The model being customised (the selected object, when it is a guided model).
    std::string path;                   // its mesh asset path
    bool loaded = false;
    bool hasRecipe = false;
    std::string loadError;
    gd::Recipe stored;                  // what the sidecar holds
    gd::Recipe pending;                 // what the panel is editing
    std::uint64_t analysisKey = 0;
    gd::Analysis analysis;

    std::map<std::string, gd::GuidedHistory> history;   // per model path

    // A change waiting for the user's decision about hand-edited parts.
    bool conflictOpen = false;
    gd::RegenResult conflict;
    gd::Recipe conflictNext;

    bool confirmConvert = false;

    // User library (templates and parts) read from <Assets>/GuidedTemplates.
    std::vector<gd::UserTemplate> userTemplates;
    std::vector<gd::UserPart> userParts;
    std::string libraryDir;
    bool libraryLoaded = false;

    char saveName[64] = "My fence";
    char partName[64] = "My part";
    int partSlot = 0;
};
State& S() { static State s; return s; }

std::string AssetsDir(Host& h) {
    const auto& ed = *h.ed;
    if (!ed.projectDir().empty()) return (fs::path(ed.projectDir()) / "Assets").string();
    if (!ed.path().empty()) return fs::path(ed.path()).parent_path().string();
    return (fs::current_path() / "Assets").string();
}
std::string LibraryDir(Host& h) { return (fs::path(AssetsDir(h)) / "GuidedTemplates").string(); }

void RefreshLibrary(Host& h) {
    State& s = S();
    s.libraryDir = LibraryDir(h);
    s.userTemplates = gd::LoadUserTemplates(s.libraryDir);
    s.userParts = gd::LoadUserParts(s.libraryDir);
    s.libraryLoaded = true;
}

void Log(Host& h, const std::string& m, int level = 0) { if (h.log) h.log(m, level); }

std::string SafeName(const std::string& n) {
    std::string b;
    for (char c : n) b += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    return b.empty() ? std::string("Model") : b;
}

// Where a model's mesh file really is. A scene stores the path as typed; an example scene refers to its files by name and the
// loader also looks next to the scene, so do the same here.
std::string ResolveMeshPath(Host& h, const std::string& p) {
    std::error_code ec;
    if (p.empty() || fs::exists(p, ec)) return p;
    if (!h.ed->path().empty()) {
        const fs::path beside = fs::path(h.ed->path()).parent_path() / fs::path(p).filename();
        if (fs::exists(beside, ec)) return beside.string();
    }
    return p;
}

// What the panel knows about the selected object: its mesh asset and, if there is one, its recipe.
void LoadSelected(Host& h, MeshRenderer* mr) {
    State& s = S();
    const std::string raw = mr ? mr->meshPath : std::string();
    if (s.loaded && s.path == raw) return;
    s.loaded = true;
    s.hasRecipe = false;
    s.loadError.clear();
    s.analysisKey = 0;
    s.path = raw;
    if (raw.empty()) return;
    const std::string p = ResolveMeshPath(h, raw);
    std::error_code ec;
    if (!fs::exists(gd::SidecarPath(p), ec)) return;
    std::string err;
    gd::Recipe r;
    if (gd::LoadRecipe(gd::SidecarPath(p), r, &err)) {
        s.stored = r; s.pending = r; s.hasRecipe = true;
        if (p != raw) { mr->meshPath = p; s.path = p; }   // from now on the object points at the real file
    } else s.loadError = err;
}

void Invalidate() { S().loaded = false; S().analysisKey = 0; }

bool SaveFiles(Host& h, MeshRenderer* mr, const gd::Recipe& r) {
    std::string err;
    if (!okay::modeling::SaveMeshAsset(mr->meshPath, mr->mesh, &err)) { Log(h, "Couldn't save the model file: " + err, 2); return false; }
    mr->meshPathHash = okay::modeling::GeometryHash(mr->mesh);
    if (!gd::SaveRecipe(gd::SidecarPath(mr->meshPath), r, &err)) { Log(h, "Couldn't save the guided recipe: " + err, 2); return false; }
    return true;
}

void SetMesh(MeshRenderer* mr, const PolyMesh& pm) {
    mr->mesh = pm.ToMesh();
    mr->mesh.name = "";
    mr->color = okay::Color{1, 1, 1, 1};   // faces carry their own colours
}

// Make a new model object from a recipe: a MeshRenderer, a .okaymesh in Assets and the recipe next to it.
bool CreateModel(Host& h, const gd::Recipe& recipe, const std::string& name) {
    std::string err;
    const PolyMesh pm = gd::Create(recipe, &err);
    if (pm.faces.empty()) { Log(h, "Couldn't create the model: " + (err.empty() ? std::string("nothing was generated") : err), 2); return false; }
    auto& ed = *h.ed;
    const fs::path dir = AssetsDir(h);
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string base = SafeName(name);
    fs::path path = dir / (base + ".okaymesh");
    for (int n = 2; fs::exists(path, ec) || fs::exists(gd::SidecarPath(path.string()), ec); ++n) path = dir / (base + "_" + std::to_string(n) + ".okaymesh");
    ed.PushUndo();
    GameObject* go = ed.scene().CreateGameObject(name);
    MeshRenderer* mr = go->AddComponent<MeshRenderer>();
    SetMesh(mr, pm);
    mr->meshPath = path.string();
    if (!SaveFiles(h, mr, recipe)) { ed.scene().DestroyImmediate(go); return false; }
    ed.SettleSpawn(go);
    ed.Select(go);
    ed.view3D = true;
    ed.dirty = true;
    Invalidate();
    S().history[path.string()].Clear();
    Log(h, "Created " + name + " from the guided designs (" + std::to_string(pm.faces.size()) + " faces). It is an ordinary mesh: Edit Mesh works on it.");
    return true;
}

// Rebuild the model from `next`, keeping everything the user did by hand (see Guided.hpp). Returns true when the mesh changed.
bool Apply(Host& h, GameObject* go, MeshRenderer* mr, const gd::Recipe& next, const gd::RegenOptions& opt, const std::string& what) {
    State& s = S();
    if (h.leaveEditMode) h.leaveEditMode(go);
    const PolyMesh cur = PolyMesh::FromMesh(mr->mesh, {false, true, 1.0f});
    gd::RegenResult r = gd::Regenerate(cur, s.stored, next, opt);
    if (r.needsDecision) {
        s.conflictOpen = true;
        s.conflict = r;
        s.conflictNext = next;
        return false;
    }
    if (!r.ok) { Log(h, what + ": " + r.message, 1); s.pending = s.stored; return false; }
    gd::GuidedState before; before.mesh = cur; before.recipe = s.stored;
    s.history[s.path].Push(before);
    h.ed->PushUndo();
    SetMesh(mr, r.mesh);
    if (!SaveFiles(h, mr, r.recipe)) return false;
    s.stored = r.recipe; s.pending = r.recipe; s.analysisKey = 0;
    h.ed->dirty = true;
    Log(h, what + ": " + r.message);
    return true;
}

// ---- Parameter widgets ------------------------------------------------------------------------------------
// Returns true when the user finished a change (released a slider, toggled, chose).
bool ParamWidget(const gd::ParamDef& p, gd::Recipe& r) {
    ImGui::PushID(p.id.c_str());
    bool done = false;
    double& v = r.params[p.id];
    ImGui::SetNextItemWidth(220);
    switch (p.kind) {
        case gd::ParamDef::Kind::Float: {
            float f = (float)v;
            if (ImGui::SliderFloat(p.label.c_str(), &f, (float)p.min, (float)p.max, "%.3f")) v = f;
            done = ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        case gd::ParamDef::Kind::Int: {
            int i = (int)v;
            if (ImGui::SliderInt(p.label.c_str(), &i, (int)p.min, (int)p.max)) v = i;
            done = ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        case gd::ParamDef::Kind::Bool: {
            bool b = v >= 0.5;
            if (ImGui::Checkbox(p.label.c_str(), &b)) { v = b ? 1 : 0; done = true; }
            break;
        }
        case gd::ParamDef::Kind::Choice: {
            int i = (int)v;
            if (ImGui::BeginCombo(p.label.c_str(), i >= 0 && i < (int)p.choices.size() ? p.choices[i].c_str() : "")) {
                for (int k = 0; k < (int)p.choices.size(); ++k)
                    if (ImGui::Selectable(p.choices[k].c_str(), k == i)) { v = k; done = true; }
                ImGui::EndCombo();
            }
            break;
        }
    }
    if (!p.tip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tip.c_str());
    ImGui::PopID();
    return done;
}

// The designs a slot can take: the template's own plus user parts made for that slot.
struct DesignChoice { std::string id, name; };
std::vector<DesignChoice> DesignsFor(const gd::Template& t, const gd::SlotDef& slot, const gd::Recipe& r) {
    State& s = S();
    std::vector<DesignChoice> v;
    for (const auto& d : slot.designs) v.push_back({d.id, d.name});
    std::set<std::string> seen;
    for (const auto& up : s.userParts) if (up.slot == slot.id) { v.push_back({"user:" + up.id, "My part: " + up.name}); seen.insert(up.id); }
    for (const auto& kv : r.userParts) if (kv.second.slot == slot.id && !seen.count(kv.first)) v.push_back({"user:" + kv.first, "My part: " + kv.second.name});
    (void)t;
    return v;
}
std::string DesignName(const std::vector<DesignChoice>& v, const std::string& id) {
    for (const auto& d : v) if (d.id == id) return d.name;
    return id;
}
// Make sure a chosen user part travels with the recipe.
void Embed(gd::Recipe& r, const std::string& design) {
    if (design.rfind("user:", 0) != 0) return;
    const std::string id = design.substr(5);
    if (r.userParts.count(id)) return;
    for (const auto& up : S().userParts) if (up.id == id) { r.userParts[id] = up; return; }
}

// ---- The decision window --------------------------------------------------------------------------------
void DrawConflict(Host& h, GameObject* go, MeshRenderer* mr) {
    State& s = S();
    if (s.conflictOpen) { ImGui::OpenPopup("Parts you edited by hand##guided"); s.conflictOpen = false; }
    if (!ImGui::BeginPopupModal("Parts you edited by hand##guided", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextWrapped("This change would rebuild parts you edited by hand. Nothing has been changed yet. What should happen to them?");
    ImGui::Separator();
    int shown = 0;
    for (const auto& c : s.conflict.conflicts) {
        if (shown++ >= 12) { ImGui::TextDisabled("... and %d more", (int)s.conflict.conflicts.size() - 12); break; }
        ImGui::BulletText("%s: %s", c.id.c_str(), c.why.c_str());
    }
    ImGui::Separator();
    gd::RegenOptions opt;
    bool go2 = false;
    if (ImGui::Button("Keep my edits")) { opt.all = gd::Decision::Keep; go2 = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("These parts stop following the design and stay exactly as you made them. Everything else is rebuilt.");
    ImGui::SameLine();
    if (ImGui::Button("Discard my edits")) { opt.all = gd::Decision::Discard; go2 = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("These parts (and anything you attached to them) are rebuilt from the design. Undo brings your edits back.");
    ImGui::SameLine();
    if (ImGui::Button("Convert to regular mesh")) {
        std::string err;
        if (gd::ConvertToRegular(mr->meshPath, &err)) { Log(h, "Converted to a regular mesh: it no longer follows the guided design."); Invalidate(); }
        else Log(h, err, 1);
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forget the design. The mesh stays exactly as it is and you edit it by hand from now on.");
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) { s.pending = s.stored; ImGui::CloseCurrentPopup(); }
    if (go2) {
        ImGui::CloseCurrentPopup();
        const gd::Recipe next = s.conflictNext;
        Apply(h, go, mr, next, opt, "Rebuilt");
    }
    ImGui::EndPopup();
}

// ---- One part's controls ------------------------------------------------------------------------------------
void DrawPartRow(Host& h, GameObject* go, MeshRenderer* mr, const gd::Template& t, const gd::PartInfo& p, bool compact) {
    State& s = S();
    ImGui::PushID(p.id.c_str());
    const char* tag = p.state == gd::PartState::Edited ? "  [edited by you]" : p.state == gd::PartState::Deleted ? "  [removed by you]" : "";
    if (compact) ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(p.id.c_str());
    if (*tag) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.93f, 0.78f, 0.45f, 1.0f), "%s", tag); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.why.c_str()); }
    ImGui::SameLine();
    if (ImGui::SmallButton("Select") && h.selectFaces) {
        std::vector<int> faces;
        for (int i = 0; i < (int)s.analysis.faceOwner.size(); ++i) if (s.analysis.faceOwner[i] == p.id) faces.push_back(i);
        if (faces.empty()) Log(h, p.id + " has no faces left to select.", 1);
        else h.selectFaces(go, faces);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select this part's faces in Edit Mesh so you can reshape it by hand.");
    const gd::SlotDef* slot = nullptr;
    for (const auto& sd : t.slots) if (sd.id == p.slot) slot = &sd;
    if (slot) {
        const std::vector<DesignChoice> choices = DesignsFor(t, *slot, s.stored);
        const std::string cur = s.stored.DesignFor(slot->id, p.id);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        if (ImGui::BeginCombo("##design", DesignName(choices, cur).c_str())) {
            for (const auto& d : choices)
                if (ImGui::Selectable(d.name.c_str(), d.id == cur)) {
                    gd::Recipe next = s.stored;
                    next.partDesign[p.id] = d.id;
                    Embed(next, d.id);
                    Apply(h, go, mr, next, {}, "Changed " + p.id + " to " + d.name);
                }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose another design for just this part.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Detach")) {
        gd::Recipe next = s.stored;
        if (gd::DetachPart(next, p.id)) {
            gd::GuidedState before; before.mesh = PolyMesh::FromMesh(mr->mesh, {false, true, 1.0f}); before.recipe = s.stored;
            s.history[s.path].Push(before);
            s.stored = next; s.pending = next; s.analysisKey = 0;
            gd::SaveRecipe(gd::SidecarPath(mr->meshPath), next);
            Log(h, p.id + " is now a regular part: changing the settings will not touch it.");
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("This part becomes plain mesh. The design no longer builds or changes it; the geometry stays.");
    ImGui::PopID();
}

// ---- The picker: category, design cards -----------------------------------------------------------------------
void DrawCreate(Host& h) {
    State& s = S();
    gd::RegisterBuiltinTemplates();
    auto& reg = gd::Registry::Instance();
    if (!s.libraryLoaded) RefreshLibrary(h);
    const auto& cats = gd::Registry::Categories();
    ImGui::SeparatorText("1. What do you want to make?");
    for (int i = 0; i < (int)cats.size(); ++i) {
        if (i) ImGui::SameLine();
        const int n = (int)reg.InCategory(cats[i]).size();
        const std::string label = cats[i] + (n ? " (" + std::to_string(n) + ")" : "");
        if (AccentToggleButton((label + "##cat" + std::to_string(i)).c_str(), s.category == i)) { s.category = i; s.templateId.clear(); s.startId.clear(); s.userStart = -1; }
    }
    const auto tpls = reg.InCategory(cats[s.category]);
    if (tpls.empty()) {
        ImGui::TextDisabled("No designs here yet. %s has none built so far; the Architecture category has the fence.", cats[s.category].c_str());
        return;
    }
    if (s.templateId.empty() || !reg.Find(s.templateId) || reg.Find(s.templateId)->category != cats[s.category]) s.templateId = tpls.front()->id;
    ImGui::SeparatorText("2. Pick a thing");
    for (size_t i = 0; i < tpls.size(); ++i) {
        if (i) ImGui::SameLine();
        if (AccentToggleButton((tpls[i]->name + "##tpl" + tpls[i]->id).c_str(), s.templateId == tpls[i]->id)) { s.templateId = tpls[i]->id; s.startId.clear(); s.userStart = -1; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tpls[i]->description.c_str());
    }
    const gd::Template& t = *reg.Find(s.templateId);
    ImGui::TextDisabled("%s", t.description.c_str());
    ImGui::SeparatorText("3. Pick a starting design");
    if (s.startId.empty() && s.userStart < 0 && !t.starts.empty()) s.startId = t.starts.front().id;

    struct Card { std::string key, name, desc; gd::Recipe recipe; bool user = false; int userIndex = -1; };
    std::vector<Card> cards;
    for (const auto& st : t.starts) {
        Card c; c.key = t.id + ":" + st.id; c.name = st.name; c.desc = st.description;
        c.recipe = gd::DefaultRecipe(t, st.id);
        c.recipe.params["length"] = std::min(c.recipe.Param("length", 4), 4.0);
        cards.push_back(c);
    }
    for (int i = 0; i < (int)s.userTemplates.size(); ++i) {
        const auto& ut = s.userTemplates[i];
        if (ut.recipe.templateId != t.id) continue;
        Card c; c.key = t.id + ":user:" + std::to_string(i) + ":" + std::to_string(std::hash<std::string>()(ut.recipe.Serialize()) & 0xffffff);
        c.name = ut.name; c.desc = ut.description.empty() ? "Your own saved template." : ut.description; c.recipe = ut.recipe; c.user = true; c.userIndex = i;
        cards.push_back(c);
    }
    const float cardW = 148;
    const int perRow = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (cardW + 10)));
    int idx = 0;
    for (const Card& c : cards) {
        if (idx++ % perRow != 0) ImGui::SameLine();
        const bool sel = c.user ? s.userStart == c.userIndex : (s.userStart < 0 && s.startId == c.key.substr(t.id.size() + 1));
        ImGui::PushID(c.key.c_str());
        ImGui::BeginGroup();
        if (ImGui::Selectable("##card", sel, 0, ImVec2(cardW, 118))) {
            if (c.user) { s.userStart = c.userIndex; s.startId.clear(); } else { s.startId = c.key.substr(t.id.size() + 1); s.userStart = -1; }
        }
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        void* tex = nullptr;
        if (h.thumb) tex = h.thumb(c.key, [&] { return gd::Create(c.recipe).ToMesh(); });
        if (tex) dl->AddImage((ImTextureID)tex, ImVec2(mn.x + 10, mn.y + 2), ImVec2(mn.x + cardW - 10, mn.y + 2 + (cardW - 20) * 0.62f));
        else dl->AddText(ImVec2(mn.x + 10, mn.y + 30), ImGui::GetColorU32(ImGuiCol_TextDisabled), "drawing...");
        dl->PushClipRect(mn, mx, true);
        dl->AddText(ImVec2(mn.x + 6, mx.y - ImGui::GetTextLineHeight() - 4), ImGui::GetColorU32(ImGuiCol_Text), c.name.c_str());
        dl->PopClipRect();
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.desc.c_str());
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("Name##objname", s.objName, sizeof(s.objName));
    ImGui::SameLine();
    if (ImGui::Button("Create##guided", ImVec2(110, 0))) {
        gd::Recipe r;
        if (s.userStart >= 0 && s.userStart < (int)s.userTemplates.size()) r = s.userTemplates[s.userStart].recipe;
        else r = gd::DefaultRecipe(t, s.startId);
        CreateModel(h, r, s.objName[0] ? s.objName : t.name);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make this in the scene as a normal editable mesh (saved in Assets). You can then change its settings here or edit it with the usual tools.");
    if (ImGui::SmallButton("Reload my templates##guided")) RefreshLibrary(h);
    ImGui::SameLine();
    ImGui::TextDisabled("Your templates and parts live in %s", s.libraryDir.c_str());
}

// ---- The customise panel for a guided model -----------------------------------------------------------------
void DrawCustomise(Host& h, GameObject* go, MeshRenderer* mr) {
    State& s = S();
    gd::RegisterBuiltinTemplates();
    const gd::Template* tp = gd::Registry::Instance().Find(s.stored.templateId);
    if (!tp) { ImGui::TextWrapped("This model was made with the '%s' guided template, which this editor does not have. It is still an ordinary mesh.", s.stored.templateId.c_str()); return; }
    const gd::Template& t = *tp;
    ImGui::SeparatorText(("Customise: " + go->name).c_str());
    if (s.stored.templateVersion != t.version) {
        ImGui::TextColored(ImVec4(0.93f, 0.78f, 0.45f, 1.0f), "Made with version %d of %s; this editor has version %d.", s.stored.templateVersion, t.name.c_str(), t.version);
        ImGui::TextWrapped("Its parts cannot be checked against the new version, so settings are locked. Convert it to a regular mesh to keep working.");
    }
    const bool locked = s.stored.templateVersion != t.version;

    // Undo / redo of guided steps (the scene's Ctrl+Z also restores the mesh).
    {
        gd::GuidedHistory& hist = s.history[s.path];
        ImGui::BeginDisabled(!hist.CanUndo());
        if (ImGui::Button("Undo guided step")) {
            gd::GuidedState cur; cur.mesh = PolyMesh::FromMesh(mr->mesh, {false, true, 1.0f}); cur.recipe = s.stored;
            if (h.leaveEditMode) h.leaveEditMode(go);
            if (hist.Undo(cur)) { h.ed->PushUndo(); SetMesh(mr, cur.mesh); s.stored = cur.recipe; s.pending = cur.recipe; SaveFiles(h, mr, cur.recipe); s.analysisKey = 0; h.ed->dirty = true; }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!hist.CanRedo());
        if (ImGui::Button("Redo guided step")) {
            gd::GuidedState cur; cur.mesh = PolyMesh::FromMesh(mr->mesh, {false, true, 1.0f}); cur.recipe = s.stored;
            if (h.leaveEditMode) h.leaveEditMode(go);
            if (hist.Redo(cur)) { h.ed->PushUndo(); SetMesh(mr, cur.mesh); s.stored = cur.recipe; s.pending = cur.recipe; SaveFiles(h, mr, cur.recipe); s.analysisKey = 0; h.ed->dirty = true; }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Convert to regular mesh")) s.confirmConvert = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forget the design. The mesh stays exactly as it is; you edit it by hand from now on.");
    }
    if (s.confirmConvert) { ImGui::OpenPopup("Convert to regular mesh?##guided"); s.confirmConvert = false; }
    if (ImGui::BeginPopupModal("Convert to regular mesh?##guided", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("The model keeps every face exactly as it is now. It stops following the design: the settings and part designs on this page go away for this model.");
        if (ImGui::Button("Convert")) {
            std::string err;
            if (gd::ConvertToRegular(mr->meshPath, &err)) { Log(h, go->name + " is now a regular mesh."); Invalidate(); }
            else Log(h, err, 1);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!s.hasRecipe) return;

    // What is intact, edited, custom (cached until the mesh or recipe changes).
    const std::uint64_t key = okay::modeling::GeometryHash(mr->mesh) * 31u + std::hash<std::string>()(s.stored.Serialize());
    if (key != s.analysisKey) {
        s.analysisKey = key;
        s.analysis = gd::Analyze(PolyMesh::FromMesh(mr->mesh, {false, true, 1.0f}), s.stored);
    }
    if (s.analysis.ok) {
        const int ed2 = s.analysis.Count(gd::PartState::Edited), del = s.analysis.Count(gd::PartState::Deleted);
        ImGui::TextDisabled("%d parts follow the design, %d edited by you, %d removed by you, %d faces of your own.",
                            s.analysis.Count(gd::PartState::Intact), ed2, del, s.analysis.customFaces);
    } else ImGui::TextWrapped("%s", s.analysis.error.c_str());

    ImGui::BeginDisabled(locked);
    // The part you have selected in Edit Mesh.
    if (h.selectedFaces && s.analysis.ok) {
        const std::vector<int> sel = h.selectedFaces(go);
        std::set<std::string> owners;
        int custom = 0;
        for (int f : sel) if (f >= 0 && f < (int)s.analysis.faceOwner.size()) { if (s.analysis.faceOwner[f].empty()) ++custom; else owners.insert(s.analysis.faceOwner[f]); }
        if (!sel.empty()) {
            ImGui::SeparatorText("Selected in Edit Mesh");
            if (owners.empty()) ImGui::TextDisabled("%d face(s) of your own (not part of the design).", custom);
            for (const std::string& id : owners)
                for (const auto& p : s.analysis.parts) if (p.id == id) DrawPartRow(h, go, mr, t, p, true);
        }
    }

    // Settings by group.
    std::vector<std::string> groups;
    for (const auto& p : t.params) if (std::find(groups.begin(), groups.end(), p.group) == groups.end()) groups.push_back(p.group);
    bool commit = false;
    for (const std::string& g : groups) {
        if (!ImGui::CollapsingHeader((g + "##grp").c_str(), g == "Size" ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
        for (const auto& p : t.params) if (p.group == g && ParamWidget(p, s.pending)) commit = true;
        if (g == "Size") {
            // The drawn path: a list of points the fence follows (height included, so it follows the ground).
            ImGui::TextDisabled("Path");
            if (s.pending.path.size() < 2) {
                if (ImGui::SmallButton("Draw a path instead of a straight run##path")) {
                    const float len = (float)s.pending.Param("length", 6);
                    s.pending.path = {{0, 0, 0}, {len, 0, 0}};
                    commit = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start a list of points the fence follows. Edit the numbers; posts always stand at the corners.");
            } else {
                int remove = -1;
                for (int i = 0; i < (int)s.pending.path.size(); ++i) {
                    ImGui::PushID(i);
                    float xyz[3] = {s.pending.path[i].x, s.pending.path[i].y, s.pending.path[i].z};
                    ImGui::SetNextItemWidth(260);
                    if (ImGui::DragFloat3("##pt", xyz, 0.05f)) s.pending.path[i] = {xyz[0], xyz[1], xyz[2]};
                    if (ImGui::IsItemDeactivatedAfterEdit()) commit = true;
                    ImGui::SameLine();
                    ImGui::BeginDisabled(s.pending.path.size() <= 2);
                    if (ImGui::SmallButton("Remove")) remove = i;
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                if (remove >= 0) { s.pending.path.erase(s.pending.path.begin() + remove); commit = true; }
                if (ImGui::SmallButton("Add a point##path")) {
                    const okay::Vec3 last = s.pending.path.back();
                    s.pending.path.push_back({last.x + 2.0f, last.y, last.z});
                    commit = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Back to a straight run##path")) { s.pending.path.clear(); commit = true; }
            }
        }
    }
    if (ImGui::CollapsingHeader("Parts and materials##grp", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Pick a design for every part of a kind. Colours are drawn now; the material slot number is stored for per-face materials.");
        for (const auto& slot : t.slots) {
            ImGui::PushID(slot.id.c_str());
            const std::vector<DesignChoice> choices = DesignsFor(t, slot, s.pending);
            const std::string cur = s.pending.slotDesign.count(slot.id) ? s.pending.slotDesign.at(slot.id) : slot.designs.front().id;
            ImGui::SetNextItemWidth(180);
            if (ImGui::BeginCombo(slot.name.c_str(), DesignName(choices, cur).c_str())) {
                for (const auto& d : choices)
                    if (ImGui::Selectable(d.name.c_str(), d.id == cur)) {
                        s.pending.slotDesign[slot.id] = d.id;
                        // a new design for the kind replaces the per-part choices for that kind
                        for (auto it = s.pending.partDesign.begin(); it != s.pending.partDesign.end();)
                            it = it->first.compare(0, slot.id.size() + 1, slot.id + "/") == 0 ? s.pending.partDesign.erase(it) : std::next(it);
                        Embed(s.pending, d.id);
                        commit = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            okay::Color& c = s.pending.colors[slot.id];
            float rgb[3] = {c.r, c.g, c.b};
            if (ImGui::ColorEdit3("##col", rgb, ImGuiColorEditFlags_NoInputs)) c = okay::Color{rgb[0], rgb[1], rgb[2], 1};
            if (ImGui::IsItemDeactivatedAfterEdit()) commit = true;
            ImGui::SameLine();
            ImGui::TextDisabled("material slot %d", s.pending.materials.count(slot.id) ? s.pending.materials.at(slot.id) : slot.defaultMaterial);
            ImGui::PopID();
        }
    }
    if (commit) Apply(h, go, mr, s.pending, {}, "Updated " + go->name);

    // Individual parts.
    if (s.analysis.ok && ImGui::CollapsingHeader("Each part##grp")) {
        for (const auto& slot : t.slots) {
            std::vector<const gd::PartInfo*> mine;
            for (const auto& p : s.analysis.parts) if (p.slot == slot.id) mine.push_back(&p);
            if (mine.empty()) continue;
            if (ImGui::TreeNode((slot.name + " (" + std::to_string(mine.size()) + ")##parts" + slot.id).c_str()))
                { for (const auto* p : mine) DrawPartRow(h, go, mr, t, *p, false); ImGui::TreePop(); }
        }
    }

    // Reusable parts and templates.
    if (ImGui::CollapsingHeader("My parts and templates##grp")) {
        ImGui::TextWrapped("Make your own part from faces you selected in Edit Mesh (a post you reshaped, a new picket) and use it as a design for a kind of part. Or save this whole setup as a template.");
        const std::vector<int> sel = h.selectedFaces ? h.selectedFaces(go) : std::vector<int>();
        ImGui::SetNextItemWidth(160);
        ImGui::InputText("Part name##pn", S().partName, sizeof(S().partName));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        const char* slotName = t.slots[std::min<int>(S().partSlot, (int)t.slots.size() - 1)].name.c_str();
        if (ImGui::BeginCombo("Fits##ps", slotName)) {
            for (int i = 0; i < (int)t.slots.size(); ++i) if (ImGui::Selectable(t.slots[i].name.c_str(), i == S().partSlot)) S().partSlot = i;
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which kind of part this can replace. It is stretched to fit that part's box.");
        ImGui::BeginDisabled(sel.empty());
        if (ImGui::Button("Save selection as part")) {
            gd::UserPart up; std::string err;
            if (!gd::MakeUserPart(PolyMesh::FromMesh(mr->mesh, {false, true, 1.0f}), sel, S().partName, t.slots[S().partSlot].id, up, &err)) Log(h, "Couldn't make the part: " + err, 1);
            else {
                std::error_code ec; fs::create_directories(LibraryDir(h), ec);
                const std::string f = (fs::path(LibraryDir(h)) / (up.id + ".okaypart")).string();
                if (gd::SaveUserPart(f, up, &err)) { Log(h, "Saved part '" + up.name + "' to " + f + ". It is now a design for " + t.slots[S().partSlot].name + "."); RefreshLibrary(h); }
                else Log(h, err, 2);
            }
        }
        ImGui::EndDisabled();
        if (sel.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Select faces first: Edit Mesh page, Edit mode (Tab), Face mode, click faces.");
        ImGui::SetNextItemWidth(160);
        ImGui::InputText("Template name##tn", S().saveName, sizeof(S().saveName));
        ImGui::SameLine();
        if (ImGui::Button("Save as template")) {
            gd::UserTemplate ut; ut.name = S().saveName; ut.description = "Saved from " + go->name; ut.recipe = s.stored;
            ut.recipe.detached.clear();   // a template is the design, not this model's hand-made parts
            std::error_code ec; fs::create_directories(LibraryDir(h), ec);
            const std::string f = (fs::path(LibraryDir(h)) / (SafeName(ut.name) + ".okaytemplate")).string();
            std::string err;
            if (gd::SaveUserTemplate(f, ut, &err)) { Log(h, "Saved template '" + ut.name + "' to " + f + ". It shows up with the starting designs."); RefreshLibrary(h); }
            else Log(h, err, 2);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep this model's settings and designs as a starting design you can create again.");
    }
    ImGui::EndDisabled();
    DrawConflict(h, go, mr);
}

} // namespace

void DrawGuidedPage(Host& h) {
    State& s = S();
    GameObject* go = h.ed->selected();
    MeshRenderer* mr = go ? go->GetComponent<MeshRenderer>() : nullptr;
    LoadSelected(h, mr);
    const bool guided = mr && s.hasRecipe;
    if (guided) {
        DrawCustomise(h, go, mr);
        ImGui::Spacing();
        if (!ImGui::CollapsingHeader("Make another one")) return;
    } else if (mr && !s.loadError.empty()) {
        ImGui::TextColored(ImVec4(0.93f, 0.78f, 0.45f, 1.0f), "%s", s.loadError.c_str());
        ImGui::TextWrapped("The model is still an ordinary mesh; it just cannot be customised here.");
    } else {
        ImGui::TextDisabled("Pick something below to get an editable model. Select a model made here to customise it.");
    }
    DrawCreate(h);
}

} // namespace oked::guided_ui
