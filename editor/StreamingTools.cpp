// World streaming in the editor. See StreamingTools.hpp and docs/engine/world_streaming.md.
#include "StreamingTools.hpp"
#include "ConsolePanel.hpp"     // ConsoleLog
#include "okay/Components/Camera.hpp"
#include "okay/Components/LevelStreaming.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/StreamingSource.hpp"
#include "okay/Components/VoxelWorld.hpp"
#include "okay/Components/WorldStreamer.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/WorldPartition.hpp"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor;

namespace oked {

bool g_showWorldPartition = false;

namespace {

struct View {
    Mat4 vp; ImVec2 center, size; ImDrawList* dl = nullptr;
    bool Project(const Vec3& w, ImVec2& out) const {
        const Vec4 c = vp * Vec4{w, 1.0f};
        if (c.w <= 0.05f) return false;
        out = ImVec2(center.x + (c.x / c.w) * size.x * 0.5f, center.y - (c.y / c.w) * size.y * 0.5f);
        return true;
    }
    void Line(const Vec3& a, const Vec3& b, ImU32 col, float th) const {
        ImVec2 p, q;
        if (Project(a, p) && Project(b, q)) dl->AddLine(p, q, col, th);
    }
    void Quad(float x0, float z0, float x1, float z1, float y, ImU32 fill) const {
        ImVec2 a, b, c, d;
        if (Project({x0, y, z0}, a) && Project({x1, y, z0}, b) && Project({x1, y, z1}, c) && Project({x0, y, z1}, d))
            dl->AddQuadFilled(a, b, c, d, fill);
    }
    void Text(const Vec3& at, ImU32 col, const char* s) const {
        ImVec2 p;
        if (!Project(at, p)) return;
        const ImVec2 sz = ImGui::CalcTextSize(s);
        dl->AddText(ImVec2(p.x - sz.x * 0.5f, p.y - sz.y * 0.5f), col, s);
    }
    void Box(const Vec3& c, const Vec3& h, ImU32 col, float th) const {
        const Vec3 p[8] = {{c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z + h.z},
                           {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z - h.z}, {c.x + h.x, c.y + h.y, c.z - h.z},
                           {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z}};
        static const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& k : e) Line(p[k[0]], p[k[1]], col, th);
    }
};

// ---- World Partition window state ------------------------------------------------------------------------

struct PartitionUi {
    PartitionSettings settings;
    bool showGrid = true;
    // The preview, recomputed when the scene or the settings change (and every half second for moved objects).
    struct Cell { int x, z, count; };
    struct Kept { Vec3 at; std::string name, reason; };
    std::vector<Cell> cells;
    std::vector<Kept> kept;
    int streamed = 0, always = 0;
    float minX = 0, minZ = 0, maxX = 0, maxZ = 0;
    std::uint64_t planStructure = 0;
    double planTime = -1.0;
    float planCell = -1.0f; int planRule = -1; float planSpan = -1.0f;
    std::string lastResult;
    bool lastOk = false;
    std::string lastScene;      // the partitioned scene written last
};

PartitionUi& Ui() { static PartitionUi u; return u; }

std::string OutScenePath(const EditorState& ed) {
    namespace fs = std::filesystem;
    if (ed.path().empty()) return {};
    fs::path p(ed.path());
    return (p.parent_path() / (p.stem().string() + "_partitioned.okayscene")).string();
}

std::string CellFolder(const EditorState& ed) {
    namespace fs = std::filesystem;
    if (ed.path().empty()) return {};
    fs::path p(ed.path());
    return (p.parent_path() / (p.stem().string() + "_cells")).string();
}

void RefreshPlan(EditorState& ed, bool force) {
    PartitionUi& u = Ui();
    Scene& s = ed.scene();
    const double now = ImGui::GetTime();
    const bool settingsChanged = u.planCell != u.settings.cellSize || u.planRule != (int)u.settings.spanRule ||
                                 u.planSpan != u.settings.maxSpanCells;
    if (!force && !settingsChanged && u.planStructure == s.StructureVersion() && now - u.planTime < 0.5) return;
    u.planStructure = s.StructureVersion();
    u.planTime = now;
    u.planCell = u.settings.cellSize; u.planRule = (int)u.settings.spanRule; u.planSpan = u.settings.maxSpanCells;
    const PartitionPlan plan = PlanPartition(s, u.settings);
    u.cells.clear(); u.kept.clear();
    for (const auto& kv : plan.cellCounts) u.cells.push_back({kv.first.first, kv.first.second, kv.second});
    for (const auto& e : plan.entries)
        if (e.alwaysLoaded && e.object) u.kept.push_back({e.centre, e.object->name, e.reason});
    u.streamed = plan.streamed; u.always = plan.alwaysLoaded;
    u.minX = plan.minX; u.minZ = plan.minZ; u.maxX = plan.maxX; u.maxZ = plan.maxZ;
}

// A selected World Streamer in Edit mode shows the cell files it would stream: scan once per selection.
std::uint64_t g_scannedStreamer = 0;

}  // namespace

void DrawWorldPartitionWindow(EditorState& ed) {
    if (!g_showWorldPartition) return;
    ImGui::SetNextWindowSize(ImVec2(420, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("World Partition", &g_showWorldPartition)) { ImGui::End(); return; }
    PartitionUi& u = Ui();
    ImGui::TextWrapped("Splits this scene into a grid of streaming cells. Your scene is not changed: the cells and a new "
                       "scene that streams them in and out around the player are written next to it.");
    ImGui::Separator();
    ImGui::SetNextItemWidth(160);
    ImGui::DragFloat("Cell size", &u.settings.cellSize, 1.0f, 5.0f, 2000.0f, "%.0f m");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Width of one square cell on the ground (X and Z).");
    char pre[64];
    std::snprintf(pre, sizeof(pre), "%s", u.settings.prefix.c_str());
    ImGui::SetNextItemWidth(160);
    if (ImGui::InputText("Cell file name", pre, sizeof(pre))) {
        std::string p = pre;
        p.erase(std::remove_if(p.begin(), p.end(), [](char c) { return !(std::isalnum((unsigned char)c) || c == '-'); }), p.end());
        u.settings.prefix = p.empty() ? "cell" : p;
    }
    int rule = (int)u.settings.spanRule;
    const char* rules[] = {"Stay loaded", "Go to the cell of their centre"};
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Wide objects", &rule, rules, 2)) u.settings.spanRule = (PartitionSettings::SpanRule)rule;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What happens to an object wider than the limit below (a terrain, a long road).");
    ImGui::SetNextItemWidth(160);
    ImGui::DragFloat("Wide = more cells than", &u.settings.maxSpanCells, 0.1f, 0.5f, 100.0f, "%.1f");
    ImGui::SetNextItemWidth(160);
    ImGui::SliderInt("Load radius (cells)", &u.settings.loadRadius, 0, 8);
    ImGui::SetNextItemWidth(160);
    ImGui::SliderInt("Unload radius (cells)", &u.settings.unloadRadius, 0, 10);
    if (u.settings.unloadRadius < u.settings.loadRadius) u.settings.unloadRadius = u.settings.loadRadius;
    ImGui::Checkbox("Show the grid in the Scene view", &u.showGrid);
    ImGui::TextDisabled("Put a Partition Layer on an object to keep it loaded or to stream it.");

    RefreshPlan(ed, false);
    ImGui::Separator();
    ImGui::Text("%d objects go into %d cells, %d stay loaded.", u.streamed, (int)u.cells.size(), u.always);
    if (!u.kept.empty() && ImGui::TreeNode("Stays loaded")) {
        for (const auto& k : u.kept) ImGui::BulletText("%s (%s)", k.name.c_str(), k.reason.c_str());
        ImGui::TreePop();
    }
    ImGui::Separator();
    const std::string outScene = OutScenePath(ed), cellDir = CellFolder(ed);
    if (outScene.empty()) {
        ImGui::TextWrapped("Save the scene first: the cells are written next to the scene file.");
    } else {
        ImGui::TextWrapped("Writes %s and the cells in %s/.", std::filesystem::path(outScene).filename().string().c_str(),
                           std::filesystem::path(cellDir).filename().string().c_str());
        const bool busy = ed.isPlaying();
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button(u.lastScene.empty() ? "Partition into cells" : "Update the cells", ImVec2(-1, 0))) {
            const PartitionResult r = WritePartition(ed.scene(), u.settings, outScene, cellDir);
            u.lastOk = r.ok;
            if (r.ok) {
                char buf[400];
                std::snprintf(buf, sizeof(buf), "Wrote %d cells (%d objects), removed %d old cells; %d objects stay loaded.",
                              r.cellsWritten, r.objectsStreamed, r.cellsRemoved, r.objectsAlwaysLoaded);
                u.lastResult = buf;
                u.lastScene = outScene;
                ConsoleLog("World Partition: " + u.lastResult + " Scene: " + outScene);
            } else {
                u.lastResult = r.error;
                ConsoleLog("World Partition failed: " + r.error, 2);
            }
        }
        if (busy) ImGui::EndDisabled();
        if (busy && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Stop Play first.");
    }
    if (!u.lastResult.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, u.lastOk ? ImVec4(0.55f, 0.85f, 0.55f, 1) : ImVec4(0.95f, 0.5f, 0.45f, 1));
        ImGui::TextWrapped("%s", u.lastResult.c_str());
        ImGui::PopStyleColor();
    }
    if (!u.lastScene.empty() && u.lastOk) {
        const bool blocked = ed.dirty || ed.isPlaying();
        if (blocked) ImGui::BeginDisabled();
        if (ImGui::Button("Open the partitioned scene", ImVec2(-1, 0))) {
            std::string err;
            if (ed.Load(u.lastScene, &err)) ConsoleLog("Opened " + u.lastScene + ": press Play and walk around; the cells stream in.");
            else ConsoleLog("Could not open " + u.lastScene + ": " + err, 2);
        }
        if (blocked) ImGui::EndDisabled();
        if (blocked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(ed.isPlaying() ? "Stop Play first." : "Save this scene first (it has changes).");
    }
    ImGui::End();
}

void DrawStreamingOverlay(EditorState& ed, ImDrawList* dl, const Mat4& vp, ImVec2 center, ImVec2 canvasSize) {
    if (!ed.view3D) return;
    View v{vp, center, canvasSize, dl};
    Scene& s = ed.scene();

    // 1. The partition preview: the grid over the scene, cells with objects tinted and counted.
    PartitionUi& u = Ui();
    if (g_showWorldPartition && u.showGrid && !ed.isPlaying()) {
        RefreshPlan(ed, false);
        const float cs = std::max(0.01f, u.settings.cellSize);
        if (!u.cells.empty()) {
            const float x0 = u.minX - cs, x1 = u.maxX + cs, z0 = u.minZ - cs, z1 = u.maxZ + cs;
            const int lines = (int)((x1 - x0) / cs) + (int)((z1 - z0) / cs);
            if (lines < 400) {
                for (float x = x0; x <= x1 + 0.01f; x += cs) v.Line({x, 0.02f, z0}, {x, 0.02f, z1}, IM_COL32(120, 200, 255, 150), 1.0f);
                for (float z = z0; z <= z1 + 0.01f; z += cs) v.Line({x0, 0.02f, z}, {x1, 0.02f, z}, IM_COL32(120, 200, 255, 150), 1.0f);
            }
            int most = 1;
            for (const auto& c : u.cells) most = std::max(most, c.count);
            for (const auto& c : u.cells) {
                const float t = (float)c.count / (float)most;
                v.Quad(c.x * cs, c.z * cs, (c.x + 1) * cs, (c.z + 1) * cs, 0.01f,
                       IM_COL32((int)(40 + 40 * t), (int)(120 + 80 * t), 255, (int)(40 + 60 * t)));
                char buf[24];
                std::snprintf(buf, sizeof(buf), "%d,%d: %d", c.x, c.z, c.count);
                v.Text({(c.x + 0.5f) * cs, 0.05f, (c.z + 0.5f) * cs}, IM_COL32(230, 245, 255, 255), buf);
            }
        }
        for (const auto& k : u.kept) {
            ImVec2 p;
            if (v.Project(k.at, p)) {
                dl->AddCircleFilled(p, 4.0f, IM_COL32(255, 200, 60, 230));
                dl->AddText(ImVec2(p.x + 6, p.y - 7), IM_COL32(255, 220, 120, 230), k.name.c_str());
            }
        }
    }

    // 2. World Streamers: in Play every streamer's cells by state; in Edit mode the selected one's cell files.
    for (WorldStreamer* ws : s.FindObjectsOfType<WorldStreamer>()) {
        if (!ws || !ws->gameObject) continue;
        const bool selected = ed.selected() == ws->gameObject;
        if (!ed.isPlaying() && !selected) continue;
        if (!ed.isPlaying() && g_scannedStreamer != ws->gameObject->InstanceId()) {
            ws->Rescan();
            g_scannedStreamer = ws->gameObject->InstanceId();
        }
        const float cs = std::max(0.01f, ws->cellSize);
        for (const auto& c : ws->CellFiles()) {
            const bool loaded = ws->IsCellLoaded(c.first, c.second), loading = ws->IsCellLoading(c.first, c.second);
            const ImU32 col = loaded ? IM_COL32(70, 220, 100, 200) : loading ? IM_COL32(250, 200, 40, 220) : IM_COL32(150, 150, 160, 120);
            const float x0 = c.first * cs, z0 = c.second * cs, x1 = x0 + cs, z1 = z0 + cs, y = 0.05f;
            v.Line({x0, y, z0}, {x1, y, z0}, col, loaded ? 2.0f : 1.0f);
            v.Line({x1, y, z0}, {x1, y, z1}, col, loaded ? 2.0f : 1.0f);
            v.Line({x1, y, z1}, {x0, y, z1}, col, loaded ? 2.0f : 1.0f);
            v.Line({x0, y, z1}, {x0, y, z0}, col, loaded ? 2.0f : 1.0f);
            if (loaded || loading) v.Quad(x0, z0, x1, z1, y, loaded ? IM_COL32(70, 220, 100, 40) : IM_COL32(250, 200, 40, 50));
        }
        if (ed.isPlaying())
            for (const auto& src : ws->Sources()) {
                ImVec2 p;
                if (v.Project(src.position, p)) dl->AddCircleFilled(p, 5.0f, IM_COL32(255, 255, 255, 230));
            }
    }

    // 3. Level Streaming Volumes: the box (cyan; green while its level is in) and the preload distance.
    for (LevelStreamingVolume* vol : s.FindObjectsOfType<LevelStreamingVolume>()) {
        if (!vol || !vol->gameObject || !vol->gameObject->transform) continue;
        const Vec3 c = vol->gameObject->transform->Position();
        const Vec3 h{std::fabs(vol->size.x) * 0.5f, std::fabs(vol->size.y) * 0.5f, std::fabs(vol->size.z) * 0.5f};
        const bool in = ed.isPlaying() && vol->IsLoaded();
        v.Box(c, h, in ? IM_COL32(70, 230, 110, 230) : IM_COL32(80, 210, 240, 220), 1.6f);
        if (ed.selected() == vol->gameObject && vol->preloadDistance > 0.0f) {
            const float p = vol->preloadDistance;
            v.Box(c, Vec3{h.x + p, h.y + p, h.z + p}, IM_COL32(80, 210, 240, 90), 1.0f);
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s%s", vol->level.empty() ? "(no level)" : std::filesystem::path(vol->level).stem().string().c_str(),
                      in ? " (loaded)" : "");
        v.Text(Vec3{c.x, c.y + h.y + 0.5f, c.z}, IM_COL32(200, 240, 255, 240), buf);
    }
}

void StreamingAddComponentItems(EditorState& ed, GameObject* go, const std::function<bool(bool, const char*)>& item) {
    if (item(!go->GetComponent<StreamingSource>(), "Streaming Source (keeps the world loaded around it)")) {
        go->AddComponent<StreamingSource>(); ed.dirty = true;
    }
    if (item(!go->GetComponent<LevelStreamingVolume>(), "Level Streaming Volume (load a level on entering)")) {
        go->AddComponent<LevelStreamingVolume>(); ed.view3D = true; ed.dirty = true;
    }
    if (item(!go->GetComponent<VoxelWorld>(), "Voxel World (endless, streamed)")) {
        go->AddComponent<VoxelWorld>(); ed.view3D = true; ed.dirty = true;
    }
    if (item(!go->GetComponent<PartitionLayer>(), "Partition Layer (World Partition setting)")) {
        go->AddComponent<PartitionLayer>(); ed.dirty = true;
    }
}

void StreamingDevHooks(EditorState& ed) {
    static int s_frame = 0;
    if (s_frame < 100000) ++s_frame;
    if (std::getenv("OKAY_UI_PARTITION") && s_frame == 33 && !ed.isPlaying()) {
        // A small town: houses on a grid, a long road (wide: stays loaded), a camera and a sun.
        Scene& sc = ed.scene();
        GameObject* ground = sc.CreateGameObject("Road");
        ground->transform->localPosition = Vec3{60, -0.5f, 25};
        ground->transform->localScale = Vec3{150, 0.2f, 6};
        ground->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 4; ++j) {
                if ((i + j) % 3 == 2) continue;
                GameObject* h = sc.CreateGameObject("House " + std::to_string(i) + "-" + std::to_string(j));
                h->transform->localPosition = Vec3{i * 30.0f + 8.0f + (j % 2) * 6.0f, 2.0f, j * 30.0f + 10.0f};
                h->transform->localScale = Vec3{6, 4, 6};
                h->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
            }
        Ui().settings.cellSize = 30.0f;
        g_showWorldPartition = true;
        ed.view3D = true;
        ed.camTarget = {70, 0, 55}; ed.camYaw = 20.0f; ed.camPitch = 58.0f; ed.camDist = 170.0f;
        ConsoleLog("World Partition demo: a town of houses and a road");
    }
    if (const char* f = std::getenv("OKAY_UI_STREAMSCENE"); f && s_frame == 33 && !ed.isPlaying()) {
        std::string err;
        if (!ed.Load(f, &err)) ConsoleLog(std::string("could not open ") + f + ": " + err, 2);
        for (WorldStreamer* ws : ed.scene().FindObjectsOfType<WorldStreamer>()) ws->debugOverlay = true;
        ed.view3D = true;
        if (GameObject* g = ed.scene().Find("World Streamer")) ed.Select(g);
    }
    if (const char* c = std::getenv("OKAY_UI_STREAMCAM"); c && s_frame >= 34 && s_frame <= 300) {
        float x = 0, y = 0, z = 0, yaw = 0, pitch = 0, dist = 10;
        if (std::sscanf(c, "%f,%f,%f,%f,%f,%f", &x, &y, &z, &yaw, &pitch, &dist) == 6) {
            ed.camTarget = {x, y, z}; ed.camYaw = yaw; ed.camPitch = pitch; ed.camDist = dist;
        }
    }
}

} // namespace oked
