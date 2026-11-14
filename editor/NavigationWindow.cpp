// The Navigation window: bake the scene's static colliders into a navigation mesh
// (settings, Bake / Clear, progress, stats), toggle the Scene-view overlays and
// send agents around for a test. The overlay draws the polygons above the meshes
// like the onion skin does, plus every agent's path and corner and the obstacles.
#include "NavigationWindow.hpp"
#include "ConsolePanel.hpp"     // ConsoleLog
#include "EditorWidgets.hpp"    // AccentCol
#include "okay/AI/NavMesh.hpp"
#include "okay/AI/Navigation.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshLink.hpp"
#include "okay/Components/NavMeshModifier.hpp"
#include "okay/Components/NavMeshObstacle.hpp"
#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor;

namespace oked {

extern bool g_uiHandled;    // a widget consumed this frame's click (main.cpp)
extern bool g_showGizmos;   // Scene-view gizmos on (main.cpp)

bool g_showNavigation = false;

namespace {

bool s_showMesh = true, s_showPortals = false, s_showPaths = true, s_showObstacles = true, s_showLinks = true;
bool s_sendMode = false;    // Scene-view clicks send the selected agent

std::string SafeName(const std::string& n) {
    std::string out;
    for (char c : n) out += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    return out.empty() ? std::string("Scene") : out;
}

// Where a bake is written: next to the scene file, else the project's Assets folder.
std::string DefaultNavPath(EditorState& ed) {
    namespace fs = std::filesystem;
    if (!ed.path().empty()) return fs::path(ed.path()).replace_extension(".okaynav").string();
    fs::path dir = !ed.projectDir().empty() ? fs::path(ed.projectDir()) / "Assets" : fs::current_path();
    return (dir / (SafeName(ed.scene().Name()) + ".okaynav")).string();
}

// The surface the window works on: the selected object's, else the scene's first.
NavMeshSurface* SurfaceFor(EditorState& ed) {
    if (GameObject* sel = ed.selected())
        if (auto* ns = sel->GetComponent<NavMeshSurface>()) return ns;
    return ed.scene().FindObjectOfType<NavMeshSurface>();
}

NavMeshSurface* AddSurface(EditorState& ed) {
    ed.PushUndo();
    GameObject* g = ed.scene().CreateGameObject("Nav Mesh");
    NavMeshSurface* ns = g->AddComponent<NavMeshSurface>();
    ed.Select(g);
    ed.dirty = true;
    ConsoleLog("Navigation: added a Nav Mesh Surface (" + g->name + ")");
    return ns;
}

// A finished bake: keep it in the scene's .okaynav and say what it made.
void StoreBake(EditorState& ed, NavMeshSurface* ns) {
    if (ns->navPath.empty()) { ns->navPath = DefaultNavPath(ed); ed.dirty = true; }
    const NavBakeStats& st = ns->LastStats();
    char buf[256];
    if (st.seconds < 0.1)
        std::snprintf(buf, sizeof(buf), "Navigation: baked %d polygons, %d links from %d cells in %.0f ms",
                      st.polys, st.links, st.walkable, st.seconds * 1000.0);
    else
        std::snprintf(buf, sizeof(buf), "Navigation: baked %d polygons, %d links from %d cells in %.2f s",
                      st.polys, st.links, st.walkable, st.seconds);
    if (ns->Save()) ConsoleLog(std::string(buf) + " -> " + ns->navPath);
    else ConsoleLog(std::string(buf) + "; could not write " + ns->navPath, 1);
}

void BakeNow(EditorState& ed, NavMeshSurface* ns) {
    std::string err;
    if (!ns->Bake(ed.scene(), nullptr, &err)) { ConsoleLog("Navigation: " + err, 2); return; }
    StoreBake(ed, ns);
}

NavMeshAgent* AgentToSend(EditorState& ed) {
    if (GameObject* sel = ed.selected())
        if (auto* a = sel->GetComponent<NavMeshAgent>()) return a;
    const auto& all = Navigation::Agents(ed.scene());
    return all.empty() ? nullptr : all.front();
}

void SendAgent(EditorState& ed, NavMeshAgent* a, Vec3 at) {
    if (!a || !a->gameObject) return;
    Vec3 snapped;
    if (Navigation::SamplePosition(ed.scene(), at, 3.0f, snapped)) at = snapped;
    a->SetDestination(at);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Navigation: %s -> (%.1f, %.1f, %.1f)%s", a->gameObject->name.c_str(), at.x, at.y, at.z,
                  ed.isPlaying() ? "" : " (it walks in Play)");
    ConsoleLog(buf);
}

float FileKB(const std::string& p) {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(std::filesystem::u8path(p), ec);
    return ec ? 0.0f : (float)sz / 1024.0f;
}

void Tip(const char* text) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text); }

// The colour an area is drawn in (Scene view overlay, area table): blue ground, red blocked,
// teal water, tan road, and a stable hue per custom area.
ImU32 AreaColor(int area, int alpha) {
    switch (area) {
        case NavArea::Walkable:    return IM_COL32(70, 150, 255, alpha);
        case NavArea::NotWalkable: return IM_COL32(230, 60, 60, alpha);
        case NavArea::Water:       return IM_COL32(40, 200, 190, alpha);
        case NavArea::Road:        return IM_COL32(225, 190, 90, alpha);
        default: {
            float r, g, b;
            ImGui::ColorConvertHSVtoRGB(std::fmod((float)(area - 4) * 0.137f + 0.08f, 1.0f), 0.65f, 0.95f, r, g, b);
            return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), alpha);
        }
    }
}

// The scene's surface for area names (any surface will do: there is one per scene in practice).
const NavMeshSurface* NamesSurface(Scene& sc) { return sc.FindObjectOfType<NavMeshSurface>(); }
std::string AreaLabel(Scene& sc, int area) {
    const NavMeshSurface* ns = NamesSurface(sc);
    return std::to_string(area) + "  " + (ns ? ns->AreaName(area) : NavAreaDefaultName(area));
}

// A combo of the 32 areas; true when the choice changed.
bool AreaCombo(Scene& sc, const char* label, int* area) {
    bool changed = false;
    *area = std::clamp(*area, 0, kNavAreaCount - 1);
    if (ImGui::BeginCombo(label, AreaLabel(sc, *area).c_str())) {
        for (int i = 0; i < kNavAreaCount; ++i) {
            ImGui::PushID(i);
            ImGui::ColorButton("##c", ImGui::ColorConvertU32ToFloat4(AreaColor(i, 255)), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(10, 10));
            ImGui::SameLine();
            if (ImGui::Selectable(AreaLabel(sc, i).c_str(), i == *area)) { *area = i; changed = true; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// An area mask as a combo of checkboxes.
bool AreaMaskCombo(Scene& sc, const char* label, std::uint32_t* mask) {
    bool changed = false;
    int on = 0; for (int i = 0; i < kNavAreaCount; ++i) if ((*mask >> i) & 1u) ++on;
    char summary[48];
    if (on == kNavAreaCount) std::snprintf(summary, sizeof(summary), "Everything");
    else if (*mask == kNavDefaultAreaMask) std::snprintf(summary, sizeof(summary), "All but Not Walkable");
    else if (on == 0) std::snprintf(summary, sizeof(summary), "Nothing");
    else std::snprintf(summary, sizeof(summary), "%d of %d areas", on, kNavAreaCount);
    if (ImGui::BeginCombo(label, summary)) {
        if (ImGui::SmallButton("All but Not Walkable")) { *mask = kNavDefaultAreaMask; changed = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Everything")) { *mask = kNavAllAreas; changed = true; }
        for (int i = 0; i < kNavAreaCount; ++i) {
            bool b = ((*mask >> i) & 1u) != 0;
            ImGui::PushID(i);
            if (ImGui::Checkbox(AreaLabel(sc, i).c_str(), &b)) { if (b) *mask |= (1u << i); else *mask &= ~(1u << i); changed = true; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// The area table on the surface: name and traversal cost per area. Costs apply at once
// (no rebake); an area's number is what the baked polygons carry.
bool AreasUI(EditorState& ed, NavMeshSurface* ns, const char* id) {
    bool changed = false;
    ImGui::PushID(id);
    if (ImGui::TreeNode("Areas")) {
        ImGui::TextDisabled("Cost multiplies distance in the path search: below 1 is preferred, above 1 avoided.");
        const bool open = ImGui::BeginChild("##areas", ImVec2(0, 170), ImGuiChildFlags_Borders);
        if (open) {
            for (int i = 0; i < kNavAreaCount; ++i) {
                ImGui::PushID(i);
                ImGui::ColorButton("##c", ImGui::ColorConvertU32ToFloat4(AreaColor(i, 255)), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(12, 12));
                ImGui::SameLine();
                ImGui::Text("%2d", i);
                ImGui::SameLine();
                char nm[48]; std::snprintf(nm, sizeof(nm), "%s", ns->areaName[i].c_str());
                ImGui::SetNextItemWidth(110);
                if (ImGui::InputTextWithHint("##n", NavAreaDefaultName(i), nm, sizeof(nm))) { ns->areaName[i] = nm; changed = true; }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                if (i == NavArea::NotWalkable) ImGui::TextDisabled("not baked");
                else if (ImGui::DragFloat("##cost", &ns->areaCost[i], 0.05f, 0.0f, 100.0f, "x%.2f")) { ns->areaCost[i] = std::max(ns->areaCost[i], 0.0f); changed = true; }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        if (ImGui::SmallButton("Reset Costs")) {
            for (int i = 0; i < kNavAreaCount; ++i) ns->areaCost[i] = NavAreaDefaultCost(i);
            changed = true;
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
    if (changed) ed.dirty = true;
    return changed;
}

bool BakeSettingsUI(EditorState& ed, NavMeshSurface* ns, const char* id) {
    bool changed = false;
    ImGui::PushID(id);
    NavMeshBuildSettings& s = ns->settings;
    if (ImGui::DragFloat("Agent Radius", &s.agentRadius, 0.01f, 0.0f, 5.0f, "%.2f")) changed = true;
    Tip("How far the walkable area stays from walls and ledges. Bake with your widest agent.");
    if (ImGui::DragFloat("Agent Height", &s.agentHeight, 0.05f, 0.1f, 10.0f, "%.2f")) changed = true;
    Tip("Head room a floor needs: a low bridge or doorway below this is not walkable.");
    if (ImGui::DragFloat("Max Slope", &s.maxSlope, 0.5f, 0.0f, 89.0f, "%.0f deg")) changed = true;
    Tip("Steepest surface agents walk on, in degrees.");
    if (ImGui::DragFloat("Max Step", &s.maxStep, 0.01f, 0.0f, 3.0f, "%.2f")) changed = true;
    Tip("Tallest step or ledge an agent walks over. Keep Cell Size below Max Step / tan(Max Slope) so ramps stay connected.");
    if (ImGui::DragFloat("Cell Size", &s.cellSize, 0.01f, 0.05f, 5.0f, "%.2f")) changed = true;
    Tip("Bake resolution. Smaller follows walls and doorways more closely, takes longer and makes more polygons.");
    if (ImGui::DragFloat("Cell Height", &s.cellHeight, 0.01f, 0.01f, 2.0f, "%.2f")) changed = true;
    Tip("Height tolerance: how far the ground may bend before a polygon is split (terrain detail).");
    if (ImGui::Checkbox("Auto Bounds", &ns->autoBounds)) changed = true;
    Tip("Bake everything the static colliders cover. Off: bake only the box below.");
    if (!ns->autoBounds) {
        if (ImGui::DragFloat3("Bounds Center", &ns->boundsCenter.x, 0.1f)) changed = true;
        if (ImGui::DragFloat3("Bounds Size", &ns->boundsSize.x, 0.1f, 0.1f, 10000.0f)) changed = true;
    }
    if (ImGui::Checkbox("Include Dynamic Bodies", &ns->includeDynamic)) changed = true;
    Tip("Also bake colliders on dynamic Rigidbodies (crates that can be pushed). Off: static and kinematic only.");
    if (ImGui::Checkbox("Bake Before Play", &ns->bakeBeforePlay)) changed = true;
    Tip("Rebake every time Play starts, so the mesh always matches the scene.");
    ImGui::PopID();
    if (changed) ed.dirty = true;
    return changed;
}

void StatsUI(NavMeshSurface* ns) {
    const NavBakeStats& st = ns->LastStats();
    const NavMesh* mesh = ns->HasMesh() ? ns->Mesh() : nullptr;
    if (!mesh) { ImGui::TextDisabled("No baked mesh. Bake, or set a .okaynav file."); return; }
    ImGui::Text("%d polygons, %d links, %d vertices", (int)mesh->polys.size(), (int)mesh->links.size(), (int)mesh->vertices.size());
    if (st.seconds > 0.0) {
        if (st.seconds < 0.1) ImGui::Text("Baked in %.0f ms from %d columns (%d walkable cells)", st.seconds * 1000.0, st.columns, st.walkable);
        else ImGui::Text("Baked in %.2f s from %d columns (%d walkable cells)", st.seconds, st.columns, st.walkable);
    }
    ImGui::Text("Walkable area: %.1f m2", mesh->Area());
    const std::string file = ns->ResolvedPath();
    if (!file.empty()) {
        const float kb = FileKB(file);
        if (kb > 0.0f) ImGui::TextWrapped("File: %s (%.1f KB)", file.c_str(), kb);
        else ImGui::TextWrapped("File: %s (not written yet)", file.c_str());
    }
}

// ---- Scene-view projection helpers --------------------------------------------
struct View {
    const Mat4* vp; ImVec2 center, size;
    ImVec2 Project(const Vec4& c) const {
        return ImVec2(center.x + (c.x / c.w) * size.x * 0.5f, center.y - (c.y / c.w) * size.y * 0.5f);
    }
    Vec4 Clip(const Vec3& p) const { return (*vp) * Vec4{p, 1}; }
    // A world segment clipped to the near plane (like the gizmo lines in main.cpp).
    void Line(ImDrawList* dl, const Vec3& wa, const Vec3& wb, ImU32 col, float th) const {
        Vec4 a = Clip(wa), b = Clip(wb);
        const float da = a.z + a.w, db = b.z + b.w;
        if (da <= 0.0f && db <= 0.0f) return;
        auto lerp = [](const Vec4& p, const Vec4& q, float t) {
            return Vec4{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t, p.w + (q.w - p.w) * t};
        };
        if (da <= 0.0f) a = lerp(a, b, (0.0f - da) / (db - da) + 1e-4f);
        else if (db <= 0.0f) b = lerp(b, a, (0.0f - db) / (da - db) + 1e-4f);
        if (a.w <= 1e-5f || b.w <= 1e-5f) return;
        dl->AddLine(Project(a), Project(b), col, th);
    }
    // A convex world polygon (up to 4 corners), near-clipped, as fill + outline.
    void Poly(ImDrawList* dl, const Vec3* w, int n, ImU32 fill, ImU32 edge, float th) const {
        Vec4 in[4]; for (int i = 0; i < n; ++i) in[i] = Clip(w[i]);
        Vec4 out[8]; int m = 0;
        for (int i = 0; i < n; ++i) {
            const Vec4& a = in[i]; const Vec4& b = in[(i + 1) % n];
            const float da = a.z + a.w, db = b.z + b.w;
            if (da >= 0.0f) out[m++] = a;
            if ((da >= 0.0f) != (db >= 0.0f)) {
                const float t = da / (da - db);
                out[m++] = Vec4{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
            }
        }
        if (m < 3) return;
        ImVec2 pts[8];
        for (int i = 0; i < m; ++i) { if (out[i].w <= 1e-5f) return; pts[i] = Project(out[i]); }
        // Skip what is far outside the view.
        float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
        for (int i = 0; i < m; ++i) { minx = std::min(minx, pts[i].x); maxx = std::max(maxx, pts[i].x); miny = std::min(miny, pts[i].y); maxy = std::max(maxy, pts[i].y); }
        if (maxx < center.x - size.x || minx > center.x + size.x || maxy < center.y - size.y || miny > center.y + size.y) return;
        dl->AddConvexPolyFilled(pts, m, fill);
        dl->AddPolyline(pts, m, edge, ImDrawFlags_Closed, th);
    }
    void Ring(ImDrawList* dl, const Vec3& c, float r, ImU32 col, float th = 1.5f) const {
        Vec3 prev = c + Vec3{r, 0, 0};
        for (int k = 1; k <= 20; ++k) {
            const float a = (float)k / 20.0f * 6.2831853f;
            const Vec3 cur = c + Vec3{std::cos(a) * r, 0.0f, std::sin(a) * r};
            Line(dl, prev, cur, col, th);
            prev = cur;
        }
    }
};

} // namespace

// ---- Window --------------------------------------------------------------------

void DrawNavigationWindow(EditorState& ed) {
    if (!g_showNavigation) return;
    NavMeshSurface::SetAssetBaseDir(ed.projectDir());
    ImGui::SetNextWindowSize(ImVec2(400, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Navigation", &g_showNavigation)) { ImGui::End(); return; }
    Scene& sc = ed.scene();
    NavMeshSurface* ns = SurfaceFor(ed);
    if (!ns) {
        ImGui::TextWrapped("This scene has no Nav Mesh Surface. One per scene bakes the static colliders "
                           "(floors, walls, crates, terrain) into walkable polygons that NPCs, click-to-move "
                           "and Nav Mesh Agents path over. Until it is baked they path over the probe grid.");
        ImGui::Spacing();
        if (ImGui::Button("Add Nav Mesh Surface", ImVec2(-1, 30))) ns = AddSurface(ed);
        if (!ns) { ImGui::End(); return; }
    }
    // A background bake that just finished: keep it.
    bool ok = false;
    if (ns->PollBake(&ok)) {
        if (ok) { StoreBake(ed, ns); ed.dirty = true; }
        else ConsoleLog("Navigation: bake cancelled", 1);
    }
    if (ns->gameObject) ImGui::TextDisabled("Surface on \"%s\"", ns->gameObject->name.c_str());
    ImGui::SeparatorText("Bake Settings");
    BakeSettingsUI(ed, ns, "navwin");
    AreasUI(ed, ns, "navwinareas");
    ImGui::Spacing();
    if (ns->IsBaking()) {
        char pb[32]; std::snprintf(pb, sizeof(pb), "Baking %.0f%%", ns->BakeProgress() * 100.0f);
        ImGui::ProgressBar(ns->BakeProgress(), ImVec2(-70, 0), pb);
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { ns->CancelBake(); ConsoleLog("Navigation: bake cancelled", 1); }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, AccentCol(0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentCol(1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentCol(0.75f));
        if (ImGui::Button("Bake", ImVec2(120, 28))) {
            if (!ns->StartBake(sc)) ConsoleLog("Navigation: a bake is already running", 1);
        }
        ImGui::PopStyleColor(3);
        Tip("Bake on a background thread; the editor stays responsive. The result is written to the .okaynav file.");
        ImGui::SameLine();
        if (ImGui::Button("Clear", ImVec2(90, 28))) {
            ed.PushUndo();
            ns->Clear();
            std::error_code ec;
            const std::string file = ns->ResolvedPath();
            if (!file.empty() && std::filesystem::remove(std::filesystem::u8path(file), ec)) ConsoleLog("Navigation: removed " + file);
            ns->navPath.clear();
            ed.dirty = true;
        }
        Tip("Drop the baked mesh and its file; the scene paths over the grid again.");
    }
    ImGui::SeparatorText("Baked Mesh");
    StatsUI(ns);
    {
        char fp[512]; std::strncpy(fp, ns->navPath.c_str(), sizeof(fp) - 1); fp[sizeof(fp) - 1] = '\0';
        if (ImGui::InputText("File", fp, sizeof(fp))) { ns->navPath = fp; ed.dirty = true; }
        Tip("The .okaynav the scene loads (next to the scene file by default). Edit to share a bake between scenes.");
        if (!ns->navPath.empty() && !ns->HasMesh() && !ns->IsBaking()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Load")) { std::string err; if (!ns->Load(std::string(), &err)) ConsoleLog("Navigation: " + err, 2); }
        }
    }
    ImGui::SeparatorText("Scene View");
    ImGui::Checkbox("Show Nav Mesh", &s_showMesh); ImGui::SameLine();
    ImGui::Checkbox("Portals", &s_showPortals);
    ImGui::Checkbox("Show Agent Paths", &s_showPaths); ImGui::SameLine();
    ImGui::Checkbox("Show Obstacles", &s_showObstacles);
    ImGui::Checkbox("Show Links", &s_showLinks);
    Tip("Draw every Nav Mesh Link: start and end rings, the crossing arc, an arrow when it is one way. The mesh is coloured by area.");
    if (!g_showGizmos) ImGui::TextDisabled("(Gizmos are off in the Scene view, so the overlays are hidden.)");
    ImGui::SeparatorText("Agents");
    const auto& agents = Navigation::Agents(sc);
    const auto& obstacles = Navigation::Obstacles(sc);
    ImGui::TextDisabled("%d agent%s, %d obstacle%s, %d link%s", (int)agents.size(), agents.size() == 1 ? "" : "s",
                        (int)obstacles.size(), obstacles.size() == 1 ? "" : "s",
                        (int)Navigation::Links(sc).size(), Navigation::Links(sc).size() == 1 ? "" : "s");
    if (ImGui::Checkbox("Send Selected Agent To Click", &s_sendMode)) {}
    Tip("While on, a left click on the ground in the Scene view sends the selected object's Nav Mesh Agent there "
        "(the scene's first agent when none is selected). Agents walk in Play. Esc turns it off.");
    for (NavMeshAgent* a : agents) {
        if (!a || !a->gameObject) continue;
        ImGui::PushID(a);
        const bool sel = ed.selected() == a->gameObject;
        if (ImGui::Selectable(a->gameObject->name.c_str(), sel, 0, ImVec2(150, 0))) ed.Select(a->gameObject);
        ImGui::SameLine();
        if (a->IsTraversingLink()) ImGui::Text("crossing a link, %.0f%%", a->LinkProgress() * 100.0f);
        else if (a->HasPath()) ImGui::Text("%s %.1f m left", a->IsStopped() ? "stopped," : "walking,", a->RemainingDistance());
        else ImGui::TextDisabled(a->Arrived() ? "arrived" : "idle");
        ImGui::PopID();
    }
    ImGui::End();
}

// ---- Scene-view overlay --------------------------------------------------------

void DrawNavigationOverlay(EditorState& ed, ImDrawList* dl, const Mat4& vp, ImVec2 center, ImVec2 canvasSize,
                           ImVec2 canvasPos, bool clickable, ImGuiIO& io) {
    Scene& sc = ed.scene();
    if (s_sendMode && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) s_sendMode = false;
    const View v{&vp, center, canvasSize};
    const NavMesh* mesh = Navigation::Mesh(sc);
    if (s_showMesh && mesh) {
        Vec3 w[4];
        for (const NavMesh::Poly& p : mesh->polys) {
            for (int k = 0; k < 4; ++k) { w[k] = mesh->vertices[(std::size_t)p.v[k]]; w[k].y += 0.03f; }
            v.Poly(dl, w, 4, AreaColor(p.area, p.area == NavArea::Walkable ? 60 : 90), AreaColor(p.area, 180), 1.0f);
        }
        if (s_showPortals)
            for (const NavMesh::Link& l : mesh->links)
                v.Line(dl, l.a + Vec3{0, 0.05f, 0}, l.b + Vec3{0, 0.05f, 0}, IM_COL32(255, 255, 255, 70), 1.0f);
    }
    if (s_showLinks) {
        for (NavMeshLink* l : Navigation::Links(sc)) {
            if (!l || !l->transform || !l->gameObject || !l->gameObject->active) continue;
            const bool sel = ed.selected() == l->gameObject;
            const bool usable = l->Usable();
            const ImU32 col = !usable ? IM_COL32(150, 150, 150, 150)
                            : sel ? IM_COL32(255, 235, 120, 255) : AreaColor(l->area == NavArea::Walkable ? 3 : l->area, 230);
            const Vec3 a = l->WorldStart(), b = l->WorldEnd();
            v.Ring(dl, a + Vec3{0, 0.05f, 0}, 0.3f, col, 2.0f);
            v.Ring(dl, b + Vec3{0, 0.05f, 0}, 0.3f, col, 2.0f);
            Vec3 prev = a;
            for (int k = 1; k <= 16; ++k) { const Vec3 q = l->PointAt(a, b, (float)k / 16.0f); v.Line(dl, prev, q, col, sel ? 3.0f : 2.0f); prev = q; }
            // An arrow head at the end (and at the start when the link works both ways).
            auto head = [&](const Vec3& tip, const Vec3& from) {
                Vec3 d = from - tip; d.y = 0.0f;
                const float m = std::sqrt(d.x * d.x + d.z * d.z);
                if (m < 1e-3f) return;
                d = d * (0.35f / m);
                const Vec3 perp{-d.z * 0.5f, 0.0f, d.x * 0.5f};
                v.Line(dl, tip, tip + d + perp, col, 2.0f);
                v.Line(dl, tip, tip + d - perp, col, 2.0f);
            };
            head(b, l->PointAt(a, b, 0.9f));
            if (l->bidirectional) head(a, l->PointAt(a, b, 0.1f));
        }
    }
    if (s_showObstacles) {
        for (NavMeshObstacle* o : Navigation::Obstacles(sc)) {
            if (!o || !o->transform || !o->gameObject || !o->gameObject->active) continue;
            const ImU32 col = ed.selected() == o->gameObject ? IM_COL32(255, 200, 90, 255) : IM_COL32(255, 160, 60, 200);
            const NavCarveVolume live = o->LiveVolume();
            const Vec3 c = o->WorldCenter();
            if (o->shape == NavMeshObstacle::Shape::Capsule) {
                const float r = o->Radius();
                v.Ring(dl, Vec3{c.x, live.minY + r, c.z}, r, col, 1.5f);
                v.Ring(dl, Vec3{c.x, live.maxY - r, c.z}, r, col, 1.5f);
                for (int k = 0; k < 4; ++k) {
                    const float a = 1.5707963f * (float)k;
                    const Vec3 d{std::cos(a) * r, 0.0f, std::sin(a) * r};
                    v.Line(dl, Vec3{c.x + d.x, live.minY + r, c.z + d.z}, Vec3{c.x + d.x, live.maxY - r, c.z + d.z}, col, 1.5f);
                }
            } else {
                const Vec3 s = o->size * o->transform->LossyScale() * 0.5f;
                const Quat q = o->transform->Rotation();
                Vec3 cr[8];
                for (int i = 0; i < 8; ++i)
                    cr[i] = c + q * Vec3{(i & 1) ? s.x : -s.x, (i & 2) ? s.y : -s.y, (i & 4) ? s.z : -s.z};
                static const int e[12][2] = {{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7}};
                for (auto& ed2 : e) v.Line(dl, cr[ed2[0]], cr[ed2[1]], col, 1.5f);
                v.Ring(dl, Vec3{c.x, live.minY + 0.03f, c.z}, o->Radius(), IM_COL32(255, 160, 60, 120), 1.0f);
            }
            // The hole it cuts in the navigation mesh: solid red while it is carved (Play), a faint outline
            // where it would be cut (Carve on, but not stationary yet or not playing).
            if (o->carve) {
                const bool carved = o->Carving();
                const NavCarveVolume& vol = carved ? o->CarvedVolume() : live;
                const NavMesh* base = Navigation::BakedMesh(sc);
                const float inflate = base ? base->settings.agentRadius : 0.4f, cell = base ? base->settings.cellSize : 0.25f;
                std::vector<NavCarveRect> rects;
                NavCarveRects(vol, inflate, cell, rects);
                for (const NavCarveRect& r : rects) {
                    const float y = vol.minY + 0.06f;
                    const Vec3 w[4] = {{r.minX, y, r.minZ}, {r.maxX, y, r.minZ}, {r.maxX, y, r.maxZ}, {r.minX, y, r.maxZ}};
                    v.Poly(dl, w, 4, carved ? IM_COL32(235, 60, 60, 110) : IM_COL32(255, 200, 60, 35),
                           carved ? IM_COL32(255, 90, 90, 230) : IM_COL32(255, 200, 60, 140), 1.0f);
                }
            }
        }
    }
    if (s_showPaths) {
        for (NavMeshAgent* a : Navigation::Agents(sc)) {
            if (!a || !a->transform || !a->gameObject || !a->gameObject->active) continue;
            const bool sel = ed.selected() == a->gameObject;
            const ImU32 todo = sel ? IM_COL32(90, 240, 255, 255) : IM_COL32(70, 220, 235, 200);
            const ImU32 done = IM_COL32(70, 140, 150, 110);
            const ImU32 corner = IM_COL32(255, 110, 230, 255);
            const Vec3 pos = a->transform->Position();
            v.Ring(dl, Vec3{pos.x, pos.y - a->baseOffset + 0.03f, pos.z}, a->radius, sel ? todo : IM_COL32(70, 220, 235, 120), 1.0f);
            if (!a->HasPath()) continue;
            const std::vector<Vec3>& path = a->Path();
            const int cur = a->CurrentCorner();
            const float lift = 0.1f;
            for (std::size_t i = 0; i + 1 < path.size(); ++i) {
                Vec3 p0 = path[i], p1 = path[i + 1]; p0.y += lift; p1.y += lift;
                v.Line(dl, p0, p1, (int)i + 1 < cur ? done : todo, sel ? 2.5f : 2.0f);
            }
            for (std::size_t i = 0; i < path.size(); ++i) {
                Vec3 p = path[i]; p.y += lift;
                v.Line(dl, p, Vec3{p.x, p.y + 0.3f, p.z}, (int)i < cur ? done : todo, 1.5f);
            }
            Vec3 cp;
            if (a->CurrentCornerPoint(cp)) {
                v.Line(dl, Vec3{pos.x, pos.y - a->baseOffset + lift, pos.z}, Vec3{cp.x, cp.y + lift, cp.z}, IM_COL32(255, 110, 230, 150), 1.0f);
                v.Ring(dl, Vec3{cp.x, cp.y + lift, cp.z}, 0.18f, corner, 2.0f);
            }
            const Vec3 d = a->Destination();
            v.Ring(dl, Vec3{d.x, d.y + 0.05f, d.z}, std::max(a->stoppingDistance, 0.15f), todo, 1.5f);
            v.Line(dl, Vec3{d.x, d.y + 0.05f, d.z}, Vec3{d.x, d.y + 0.8f, d.z}, todo, 2.0f);
        }
    }
    // Send-to-click: a left click on the ground orders the agent there.
    if (s_sendMode) {
        NavMeshAgent* a = AgentToSend(ed);
        char hint[200];
        if (a && a->gameObject) std::snprintf(hint, sizeof(hint), "Navigation: click the ground to send \"%s\"  (Esc to stop)", a->gameObject->name.c_str());
        else std::snprintf(hint, sizeof(hint), "Navigation: no Nav Mesh Agent in the scene to send  (Esc to stop)");
        const ImVec2 ts = ImGui::CalcTextSize(hint);
        const ImVec2 hp(canvasPos.x + 12.0f, canvasPos.y + canvasSize.y - ts.y - 14.0f);
        dl->AddRectFilled(ImVec2(hp.x - 8, hp.y - 4), ImVec2(hp.x + ts.x + 8, hp.y + ts.y + 4), IM_COL32(0, 0, 0, 160), 5.0f);
        dl->AddText(hp, IM_COL32(120, 230, 255, 255), hint);
        if (a && clickable && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const float nx = (io.MousePos.x - canvasPos.x) / canvasSize.x * 2.0f - 1.0f;
            const float ny = 1.0f - (io.MousePos.y - canvasPos.y) / canvasSize.y * 2.0f;
            const Mat4 inv = vp.Inverse();
            const Vec3 p0 = inv.MultiplyPoint({nx, ny, -1.0f}), p1 = inv.MultiplyPoint({nx, ny, 1.0f});
            const Vec3 rd = (p1 - p0).Normalized();
            // Solid geometry only: a pickup's trigger volume must not catch the click.
            RaycastHit3D rh = sc.physics3D().Raycast(sc, p0, rd, 800.0f, a->gameObject, Physics3D::kAllLayers, false);
            Vec3 at;
            if (rh.hit) at = rh.point;
            else if (std::fabs(rd.y) > 1e-4f) at = p0 + rd * (-p0.y / rd.y);
            else at = p0 + rd * 10.0f;
            SendAgent(ed, a, at);
            g_uiHandled = true;   // not a selection click
        }
    }
}

// ---- Inspectors / Add Component -------------------------------------------------

void DrawNavigationInspector(EditorState& ed, GameObject* go, Component* comp, Component*& toRemove, CompHeaderFn header) {
    if (!go || !comp) return;
    if (auto* ns = dynamic_cast<NavMeshSurface*>(comp)) {
        if (header("Nav Mesh Surface", ns, &toRemove, true)) {
            bool ok = false;
            if (ns->PollBake(&ok)) { if (ok) StoreBake(ed, ns); else ConsoleLog("Navigation: bake cancelled", 1); }
            BakeSettingsUI(ed, ns, "navinsp");
            AreasUI(ed, ns, "navinspareas");
            ImGui::Spacing();
            if (ns->IsBaking()) {
                ImGui::ProgressBar(ns->BakeProgress(), ImVec2(-1, 0));
            } else {
                if (ImGui::Button("Bake##navsurf")) { if (!ns->StartBake(ed.scene())) ConsoleLog("Navigation: a bake is already running", 1); }
                ImGui::SameLine();
                if (ImGui::Button("Clear##navsurf")) { ed.PushUndo(); ns->Clear(); ed.dirty = true; }
                ImGui::SameLine();
                if (ImGui::Button("Navigation Window##navsurf")) g_showNavigation = true;
            }
            StatsUI(ns);
            if (ImGui::SmallButton("Remove##navsurf")) toRemove = ns;
        }
    }
    if (auto* na = dynamic_cast<NavMeshAgent*>(comp)) {
        if (header("Nav Mesh Agent", na, &toRemove, true)) {
            if (ImGui::DragFloat("Radius##nava", &na->radius, 0.01f, 0.05f, 5.0f, "%.2f")) ed.dirty = true;
            Tip("Body radius for steering around other agents and obstacles (the mesh clearance comes from the bake).");
            if (ImGui::DragFloat("Height##nava", &na->height, 0.05f, 0.1f, 10.0f, "%.2f")) ed.dirty = true;
            if (ImGui::DragFloat("Speed##nava", &na->speed, 0.1f, 0.0f, 50.0f, "%.2f")) ed.dirty = true;
            if (ImGui::DragFloat("Acceleration##nava", &na->acceleration, 0.1f, 0.0f, 100.0f, "%.1f")) ed.dirty = true;
            Tip("How quickly it reaches its speed (0 = instantly).");
            if (ImGui::DragFloat("Angular Speed##nava", &na->angularSpeed, 1.0f, 0.0f, 3600.0f, "%.0f deg/s")) ed.dirty = true;
            if (ImGui::DragFloat("Stopping Distance##nava", &na->stoppingDistance, 0.01f, 0.0f, 10.0f, "%.2f")) ed.dirty = true;
            if (ImGui::Checkbox("Auto Repath##nava", &na->autoRepath)) ed.dirty = true;
            Tip("Plan again every Repath Interval while walking (moving goals, opened doors).");
            if (na->autoRepath) { if (ImGui::DragFloat("Repath Interval##nava", &na->repathInterval, 0.05f, 0.05f, 10.0f, "%.2f s")) ed.dirty = true; }
            if (ImGui::SliderInt("Avoidance Priority##nava", &na->avoidancePriority, 0, 99)) ed.dirty = true;
            Tip("0: others get out of its way. 99: it yields to everyone. Equal priorities share the dodge.");
            if (ImGui::DragFloat("Avoidance Range##nava", &na->avoidanceRange, 0.1f, 0.0f, 20.0f, "%.1f")) ed.dirty = true;
            Tip("How far it looks for agents and obstacles to steer around (0 = none).");
            if (ImGui::Checkbox("Auto Braking##nava", &na->autoBraking)) ed.dirty = true;
            if (ImGui::Checkbox("Face Movement##nava", &na->faceMovement)) ed.dirty = true;
            if (ImGui::Checkbox("Drive Animation##nava", &na->driveAnimation)) ed.dirty = true;
            Tip("Set a sibling Character's idle / walk / run from the movement.");
            if (AreaMaskCombo(ed.scene(), "Area Mask##nava", &na->areaMask)) ed.dirty = true;
            Tip("Areas this agent may walk on and use links of. A forbidden area is never entered; a goal inside one has no path.");
            if (ImGui::DragFloat("Base Offset##nava", &na->baseOffset, 0.01f, -5.0f, 5.0f, "%.2f")) ed.dirty = true;
            Tip("The object's origin above its feet: 0 for a character pivoted at the feet, half the height for a cube.");
            if (go->GetComponent<NPCController>() || go->GetComponent<ClickToMoveController>())
                ImGui::TextDisabled("Walks through this object's NPC / Click To Move controller.");
            if (ed.isPlaying()) {
                if (na->IsTraversingLink()) ImGui::Text("Crossing a link: %.0f%%", na->LinkProgress() * 100.0f);
                else if (na->HasPath()) ImGui::Text("%s: %.1f m left", na->IsStopped() ? "Stopped" : "Walking", na->RemainingDistance());
                else ImGui::TextDisabled(na->Arrived() ? "Arrived" : "Idle");
                if (na->HasPath() && !na->IsStopped()) { if (ImGui::SmallButton("Stop##navagent")) na->Stop(); }
                else if (na->IsStopped()) { if (ImGui::SmallButton("Resume##navagent")) na->Resume(); }
            }
            if (ImGui::SmallButton(s_sendMode ? "Stop Sending##nava" : "Send To Click (Scene view)##nava")) { s_sendMode = !s_sendMode; if (s_sendMode) ed.Select(go); }
            Tip("Click the ground in the Scene view to order this agent there (it walks in Play).");
            if (ImGui::SmallButton("Remove##navagent")) toRemove = na;
        }
    }
    if (auto* nm = dynamic_cast<NavMeshModifier*>(comp)) {
        if (header("Nav Mesh Modifier", nm, &toRemove, true)) {
            if (AreaCombo(ed.scene(), "Area##navm", &nm->area)) ed.dirty = true;
            Tip("The area this object's colliders bake as: Water and custom areas cost what the surface says, Road is cheap, Not Walkable cuts a hole in the mesh. Bake again after changing it.");
            if (ImGui::Checkbox("Apply To Children##navm", &nm->applyToChildren)) ed.dirty = true;
            Tip("Also the colliders of child objects that have no modifier of their own.");
            if (!go->GetComponent<Collider3D>() && !nm->applyToChildren) ImGui::TextDisabled("No 3D collider on this object: nothing to mark.");
            if (ImGui::SmallButton("Remove##navmod")) toRemove = nm;
        }
    }
    if (auto* nl = dynamic_cast<NavMeshLink*>(comp)) {
        if (header("Nav Mesh Link", nl, &toRemove, true)) {
            int ty = (int)nl->type;
            if (ImGui::Combo("Type##navl", &ty, "Jump\0Drop\0Ladder\0")) { nl->type = (NavMeshLink::Type)ty; ed.dirty = true; }
            Tip("How an agent crosses: Jump follows an arc, Drop falls (slow, then fast), Ladder climbs in a straight line.");
            if (ImGui::DragFloat3("Start##navl", &nl->startPoint.x, 0.05f)) ed.dirty = true;
            Tip("Where the crossing begins, in this object's local space. Keep it on the baked mesh (within 1.5 m).");
            if (ImGui::DragFloat3("End##navl", &nl->endPoint.x, 0.05f)) ed.dirty = true;
            if (ImGui::Checkbox("Bidirectional##navl", &nl->bidirectional)) ed.dirty = true;
            Tip("Off: agents cross from Start to End only (a drop off a ledge).");
            if (ImGui::DragFloat("Cost##navl", &nl->cost, 0.05f, 0.0f, 100.0f, "x%.2f")) { nl->cost = std::max(nl->cost, 0.0f); ed.dirty = true; }
            Tip("Multiplies the crossing distance in the path search, on top of the area cost. Raise it so agents prefer walking when it is nearly as short.");
            if (AreaCombo(ed.scene(), "Area##navl", &nl->area)) ed.dirty = true;
            Tip("Agents whose Area Mask lacks this area ignore the link.");
            if (ImGui::DragFloat("Speed##navl", &nl->speed, 0.05f, 0.0f, 100.0f, "%.2f")) { nl->speed = std::max(nl->speed, 0.0f); ed.dirty = true; }
            Tip("Crossing speed in units per second; 0 uses the agent's own speed.");
            if (nl->type == NavMeshLink::Type::Jump) {
                if (ImGui::DragFloat("Arc Height##navl", &nl->arcHeight, 0.05f, 0.0f, 50.0f, "%.2f")) { nl->arcHeight = std::max(nl->arcHeight, 0.0f); ed.dirty = true; }
            }
            if (ImGui::SmallButton("Swap Start / End##navl")) { ed.PushUndo(); std::swap(nl->startPoint, nl->endPoint); ed.dirty = true; }
            // Does each end sit on the baked mesh? A link whose end is off it is silently unusable.
            if (const NavMesh* mesh = Navigation::Mesh(ed.scene())) {
                const Vec3 ext{1.5f, 3.0f, 1.5f};
                const bool a = mesh->FindNearestPoly(nl->WorldStart(), ext) >= 0, b = mesh->FindNearestPoly(nl->WorldEnd(), ext) >= 0;
                if (!a || !b) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s is not on the baked mesh: the link is not used.", !a && !b ? "Neither end" : (!a ? "The start" : "The end"));
                else ImGui::TextDisabled("Both ends are on the mesh.");
            } else ImGui::TextDisabled("Bake the surface to check the ends are on the mesh.");
            if (ImGui::SmallButton("Remove##navlink")) toRemove = nl;
        }
    }
    if (auto* no = dynamic_cast<NavMeshObstacle*>(comp)) {
        if (header("Nav Mesh Obstacle", no, &toRemove, true)) {
            int sh = (int)no->shape;
            if (ImGui::Combo("Shape##navo", &sh, "Box\0Capsule\0")) { ed.PushUndo(); no->shape = (NavMeshObstacle::Shape)sh; ed.dirty = true; }
            Tip("The solid agents go around: a box, or an upright capsule (a pillar, a barrel).");
            if (no->shape == NavMeshObstacle::Shape::Box) {
                if (ImGui::DragFloat3("Size##navo", &no->size.x, 0.05f, 0.01f, 100.0f)) ed.dirty = true;
            } else {
                if (ImGui::DragFloat("Radius##navo", &no->capsuleRadius, 0.02f, 0.01f, 50.0f, "%.2f")) { no->capsuleRadius = std::max(no->capsuleRadius, 0.01f); ed.dirty = true; }
                if (ImGui::DragFloat("Height##navo", &no->capsuleHeight, 0.05f, 0.02f, 100.0f, "%.2f")) { no->capsuleHeight = std::max(no->capsuleHeight, 0.02f); ed.dirty = true; }
            }
            if (ImGui::DragFloat3("Center##navo", &no->center.x, 0.05f)) ed.dirty = true;
            if (no->shape == NavMeshObstacle::Shape::Box)
                if (auto* mr = go->GetComponent<MeshRenderer>()) {
                    if (ImGui::SmallButton("Fit To Mesh##navo")) {
                        Vec3 lo, hi; mr->DisplayMesh().Bounds(lo, hi);
                        ed.PushUndo();
                        no->size = hi - lo; no->center = (lo + hi) * 0.5f;
                        ed.dirty = true;
                    }
                }
            if (ImGui::Checkbox("Carve##navo", &no->carve)) { ed.PushUndo(); ed.dirty = true; }
            Tip("Cut a hole in the navigation mesh while it stands still, so agents plan their route around it instead of only dodging it up close. Needs a baked Nav Mesh Surface. Works in Play.");
            if (no->carve) {
                if (ImGui::Checkbox("Carve Only When Stationary##navo", &no->carveOnlyWhenStationary)) ed.dirty = true;
                Tip("On: the hole appears once it has stopped moving and closes when it moves, so a moving obstacle does not rebuild the mesh every frame. Off: the hole follows it.");
                if (no->carveOnlyWhenStationary) {
                    if (ImGui::DragFloat("Time To Stationary##navo", &no->timeToStationary, 0.05f, 0.0f, 60.0f, "%.2f s")) { no->timeToStationary = std::max(no->timeToStationary, 0.0f); ed.dirty = true; }
                    Tip("Seconds without moving before it counts as stationary and carves.");
                } else {
                    if (ImGui::DragFloat("Move Threshold##navo", &no->moveThreshold, 0.01f, 0.0f, 50.0f, "%.2f")) { no->moveThreshold = std::max(no->moveThreshold, 0.0f); ed.dirty = true; }
                    Tip("The hole is rebuilt after the obstacle has moved this far.");
                }
                if (!Navigation::BakedMesh(ed.scene())) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "No baked mesh: nothing to carve. Bake the surface first.");
                else if (no->Carving()) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "Carving now: agents route around it.");
                else if (no->carveOnlyWhenStationary && no->StationaryTime() > 0.0f) ImGui::TextDisabled("Still for %.1f s of %.1f s.", no->StationaryTime(), no->timeToStationary);
                else ImGui::TextDisabled("Not carving (carves in Play%s).", no->carveOnlyWhenStationary ? ", once still" : "");
            }
            ImGui::TextDisabled("Agents always steer around it locally; it is never baked.");
            if (ImGui::SmallButton("Remove##navobst")) toRemove = no;
        }
    }
}

// Add a Nav Mesh Obstacle sized to the object's mesh. From the editor it is meant to block routes, so it carves; the
// component's own default is off so older scenes keep their behaviour.
static NavMeshObstacle* AddObstacleTo(GameObject* go) {
    NavMeshObstacle* o = go->AddComponent<NavMeshObstacle>();
    o->carve = true;
    if (auto* mr = go->GetComponent<MeshRenderer>()) {
        Vec3 lo, hi; mr->DisplayMesh().Bounds(lo, hi);
        o->size = hi - lo; o->center = (lo + hi) * 0.5f;
    }
    return o;
}

void NavigationAddComponentItems(EditorState& ed, GameObject* go, const std::function<bool(bool, const char*)>& item) {
    if (!go) return;
    if (item(!go->GetComponent<NavMeshSurface>(), "Nav Mesh Surface (bake walkable polygons)")) {
        ed.PushUndo(); go->AddComponent<NavMeshSurface>(); ed.dirty = true; g_showNavigation = true;
    }
    if (item(!go->GetComponent<NavMeshAgent>(), "Nav Mesh Agent (walks the navmesh)")) {
        ed.PushUndo();
        NavMeshAgent* a = go->AddComponent<NavMeshAgent>();
        if (auto* mr = go->GetComponent<MeshRenderer>()) {   // a prop pivoted at its centre: feet are below the origin
            Vec3 lo, hi; mr->DisplayMesh().Bounds(lo, hi);
            const Vec3 s = go->transform ? go->transform->LossyScale() : Vec3::One;
            if (lo.y < -0.01f && !go->GetComponent<NPCController>() && !go->GetComponent<ClickToMoveController>())
                a->baseOffset = -lo.y * s.y;
        }
        ed.dirty = true;
    }
    if (item(!go->GetComponent<NavMeshModifier>(), "Nav Mesh Modifier (mark water, road or blocked ground)")) {
        ed.PushUndo(); go->AddComponent<NavMeshModifier>(); ed.dirty = true;
    }
    if (item(!go->GetComponent<NavMeshLink>(), "Nav Mesh Link (jump, drop or ladder between two points)")) {
        ed.PushUndo(); go->AddComponent<NavMeshLink>(); ed.dirty = true;
    }
    if (item(!go->GetComponent<NavMeshObstacle>(), "Nav Mesh Obstacle (agents go around it; carves the mesh)")) {
        ed.PushUndo();
        AddObstacleTo(go);
        ed.dirty = true;
    }
}

// ---- Play / dev hooks -----------------------------------------------------------

void NavigationBeforePlay(EditorState& ed) {
    NavMeshSurface::SetAssetBaseDir(ed.projectDir());
    for (NavMeshSurface* ns : ed.scene().FindObjectsOfType<NavMeshSurface>()) {
        if (!ns || !ns->bakeBeforePlay || !ns->enabled) continue;
        ns->CancelBake();
        BakeNow(ed, ns);
    }
}

void NavigationDevHooks(EditorState& ed) {
    static int s_frame = 0;
    if (s_frame < 100000) ++s_frame;
    if (std::getenv("OKAY_UI_NAV")) g_showNavigation = true;
    if (std::getenv("OKAY_UI_NAVSEND")) s_sendMode = true;
    if (std::getenv("OKAY_UI_NAVBAKE") && s_frame == 35) {
        NavMeshSurface* ns = SurfaceFor(ed);
        if (!ns) ns = AddSurface(ed);
        BakeNow(ed, ns);
    }
    if (const char* an = std::getenv("OKAY_UI_NAVAGENT"); an && s_frame == 34) {   // before a NAVBAKE: the agent is not baked
        if (GameObject* g = ed.scene().Find(an)) {
            if (!g->GetComponent<NavMeshAgent>()) {
                NavMeshAgent* a = g->AddComponent<NavMeshAgent>();
                if (auto* mr = g->GetComponent<MeshRenderer>()) {
                    Vec3 lo, hi; mr->DisplayMesh().Bounds(lo, hi);
                    if (lo.y < -0.01f) a->baseOffset = -lo.y * g->transform->LossyScale().y;
                }
                a->speed = 2.5f;
                if (const char* sp = std::getenv("OKAY_UI_NAVSPEED")) a->speed = std::max(0.1f, (float)std::atof(sp));   // slow it down for screenshots
            }
            ed.Select(g);
        }
    }
    // OKAY_UI_NAVOBSTACLE=<object name>[,nocarve]: the "Nav Mesh Obstacle" Add Component item on that object (dev screenshots).
    if (const char* on = std::getenv("OKAY_UI_NAVOBSTACLE"); on && s_frame == 34) {
        std::string name = on; bool carve = true;
        const std::size_t comma = name.find(',');
        if (comma != std::string::npos) { carve = name.substr(comma + 1) != "nocarve"; name = name.substr(0, comma); }
        if (GameObject* g = ed.scene().Find(name)) {
            if (!g->GetComponent<NavMeshObstacle>()) AddObstacleTo(g)->carve = carve;
            ed.Select(g);
        }
    }
    if (const char* d = std::getenv("OKAY_UI_NAVDEST"); d && s_frame == 150 && ed.isPlaying()) {
        float x = 0, y = 0, z = 0;
        if (std::sscanf(d, "%f,%f,%f", &x, &y, &z) == 3)
            if (NavMeshAgent* a = AgentToSend(ed)) SendAgent(ed, a, Vec3{x, y, z});
    }
}

} // namespace oked
