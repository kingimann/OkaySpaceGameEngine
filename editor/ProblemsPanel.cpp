#include "EditorCommon.hpp"
#include "ProblemsPanel.hpp"
#include "EditorWidgets.hpp"
#include "ConsolePanel.hpp"
#include "MainShared.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/Testing/ObjectPath.hpp"

#include <chrono>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace oked {

extern bool g_showProblems;
extern bool g_showVisualScript;
extern bool g_showScriptEditor;
extern bool g_quitRequested;

namespace {

okay::editor::EditorState* g_bound = nullptr;
std::string g_shotPath;
int g_shotArm = 0;

std::uint64_t Mix(std::uint64_t h, const std::string& s) {
    for (unsigned char c : s) h = h * 131u + c;
    return h;
}

std::uint64_t SceneFingerprint(const Scene& scene) {
    std::uint64_t h = scene.StructureVersion() * 0x9E3779B185EBCA87ull ^ (scene.ComponentVersion() << 1);
    for (const auto& up : scene.Objects()) {
        if (!up) continue;
        h = Mix(h, up->Guid());
        h = Mix(h, up->name);
        h = h * 131u + (up->active ? 1u : 0u);
        for (const auto& c : up->Components()) {
            if (!c) continue;
            h = Mix(h, testing::ComponentTypeName(*c));
            if (auto* sr = dynamic_cast<SpriteRenderer*>(c.get())) h = Mix(h, sr->texture);
            if (auto* mr = dynamic_cast<MeshRenderer*>(c.get())) h = Mix(h, mr->meshPath);
            if (auto* sc = dynamic_cast<ScriptComponent*>(c.get())) {
                h = Mix(h, sc->Path());
                h = h * 131u + (std::uint64_t)sc->Source().size();
                if (!sc->Source().empty()) h = Mix(h, sc->Source().substr(0, 48));
            }
            if (auto* vs = dynamic_cast<VisualScriptComponent*>(c.get())) {
                h = Mix(h, vs->Path());
                h = h * 131u + (std::uint64_t)vs->Source().size();
                if (!vs->Source().empty()) h = Mix(h, vs->Source().substr(0, 48));
            }
            if (auto* ft = dynamic_cast<FollowTarget2D*>(c.get())) h = Mix(h, ft->target);
            if (auto* cam = dynamic_cast<Camera*>(c.get())) h = h * 131u + (cam->main ? 3u : 1u);
            if (auto* sm = dynamic_cast<AnimStateMachine*>(c.get())) h = h * 131u + (std::uint64_t)sm->states.size();
            if (auto* nav = dynamic_cast<NavMeshSurface*>(c.get())) h = Mix(h, nav->navPath);
        }
    }
    return h;
}

void RequestShot(const std::string& path, int arm) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    g_shotPath = path;
    g_shotArm = arm;
}

void QueueClick(ImVec2 p) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(p.x, p.y);
    io.AddMouseButtonEvent(0, true);
    io.AddMouseButtonEvent(0, false);
}

std::string GroupOf(const Diagnostic& d) {
    if (!d.source.objectPath.empty()) return d.source.objectPath;
    if (!d.source.scriptPath.empty()) return d.source.scriptPath;
    if (!d.source.assetPath.empty()) return d.source.assetPath;
    if (!d.source.graphPath.empty()) return d.source.graphPath;
    return "Scene";
}

const char* SevName(DiagSeverity s) {
    if (s == DiagSeverity::Error) return "Error";
    if (s == DiagSeverity::Warning) return "Warning";
    return "Info";
}

ImVec4 SevColor(DiagSeverity s) {
    if (s == DiagSeverity::Error) return ImVec4(0.96f, 0.45f, 0.42f, 1.0f);
    if (s == DiagSeverity::Warning) return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
    return ImVec4(0.70f, 0.78f, 0.88f, 1.0f);
}

void OpenDiagnostic(okay::editor::EditorState& ed, const Diagnostic& d) {
    const bool graph = !d.source.nodeId.empty() || d.code.rfind("graph.", 0) == 0;
    GameObject* go = nullptr;
    if (!d.source.objectGuid.empty()) go = ed.scene().FindByGuid(d.source.objectGuid);
    if (!go && !d.source.objectPath.empty()) go = ed.scene().FindByPath(d.source.objectPath);
    if (go) {
        ed.Select(go);
        if (go->transform) {
            Vec3 p = go->transform->Position();
            ed.camTarget = p;
            ed.cameraPos = {p.x, p.y};
        }
    }
    if (!graph && !d.source.scriptPath.empty()) {
        OpenScriptFileInEditorAt(d.source.scriptPath, d.source.scriptLine > 0 ? d.source.scriptLine : 1);
        g_showScriptEditor = true;
    }
    if (graph) {
        const int node = d.source.nodeId.empty() ? -1 : std::atoi(d.source.nodeId.c_str());
        bool opened = false;
        if (go) {
            if (VisualScriptComponent* vs = go->GetComponent<VisualScriptComponent>()) {
                if (!vs->Source().empty()) {
                    okay::editor::vsed::ShowGraphText(vs->Source(), node);
                    if (!vs->Path().empty()) okay::editor::vsed::State().g.path = vs->Path();
                    opened = true;
                }
            }
        }
        if (!opened && !d.source.graphPath.empty())
            okay::editor::vsed::ShowGraphFile(d.source.graphPath, node);
        g_showVisualScript = true;
    }
}

std::string TipFor(const std::vector<Diagnostic>& rows) {
    std::string tip;
    for (const Diagnostic& d : rows) {
        if (!tip.empty()) tip += "\n";
        tip += std::string(SevName(d.severity)) + ": " + d.message;
        if (!d.hint.empty()) tip += "\n" + d.hint;
    }
    return tip;
}

void DrawMarker(ImVec2 c, bool error, const std::string& tip) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = error ? IM_COL32(230, 78, 70, 255) : IM_COL32(230, 176, 64, 255);
    dl->AddCircleFilled(c, 6.5f, col, 14);
    dl->AddCircle(c, 6.5f, IM_COL32(28, 22, 18, 230), 14, 1.2f);
    const ImVec2 ts = ImGui::CalcTextSize("!");
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(32, 22, 16, 255), "!");
    const ImVec2 m = ImGui::GetIO().MousePos;
    if (tip.empty()) return;
    if (std::fabs(m.x - c.x) <= 9.0f && std::fabs(m.y - c.y) <= 9.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        ImGui::SetTooltip("%s", tip.c_str());
}

std::string NormLabel(std::string s) {
    std::string o;
    for (char c : s) {
        if (c == ' ' || c == '_' || c == '-') continue;
        o.push_back((char)std::tolower((unsigned char)c));
    }
    const std::string tail = "component";
    if (o.size() > tail.size() && o.compare(o.size() - tail.size(), tail.size(), tail) == 0)
        o.erase(o.size() - tail.size());
    return o;
}

bool ComponentMatches(const Diagnostic& d, const char* label) {
    if (!label || !label[0] || d.source.component.empty()) return false;
    const std::string a = NormLabel(d.source.component);
    const std::string b = NormLabel(label);
    if (a.empty() || b.empty()) return false;
    return a == b || a.find(b) != std::string::npos || b.find(a) != std::string::npos;
}

// Demo bookkeeping. The dev hook builds the scene; drawing records the row to click.
bool g_demo = false;
int g_demoFrame = 0;
ImVec2 g_rowClick{}, g_fixClick{};
bool g_haveRow = false, g_haveFix = false;

} // namespace

void BindProblemsEditor(okay::editor::EditorState* ed) { g_bound = ed; }

void ActivateBoundConsoleSource(const ConsoleSource& source) {
    if (!g_bound) return;
    if (!source.Valid() && source.guid.empty()) return;
    Diagnostic d;
    d.source.objectGuid = source.guid;
    d.source.component = source.component;
    d.source.nodeId = source.nodeId;
    d.source.scriptLine = source.line;
    if (source.kind == ConsoleSource::Kind::Script) d.source.scriptPath = source.path;
    else if (source.kind == ConsoleSource::Kind::Graph) {
        d.source.graphPath = source.path;
        d.code = "graph.error";
    } else if (source.kind == ConsoleSource::Kind::Asset) d.source.assetPath = source.path;
    else d.source.objectPath = source.path;
    if (source.kind == ConsoleSource::Kind::Object || !source.guid.empty())
        d.source.objectPath = source.kind == ConsoleSource::Kind::Object ? source.path : d.source.objectPath;
    OpenDiagnostic(*g_bound, d);
}

std::string TakePendingEditorShot() {
    if (g_shotArm <= 0 || g_shotPath.empty()) return {};
    if (--g_shotArm > 0) return {};
    std::string path = std::move(g_shotPath);
    g_shotPath.clear();
    return path;
}

DiagTotals ProjectDiagTotals() {
    DiagTotals t;
    t.info = EditorDiagnostics().Count(DiagSeverity::Info);
    t.warning = EditorDiagnostics().Count(DiagSeverity::Warning);
    t.error = EditorDiagnostics().Count(DiagSeverity::Error);
    return t;
}

void TickProjectDiagnostics(okay::editor::EditorState& ed) {
    BindProblemsEditor(&ed);
    if (ed.isPlaying()) return;
    static std::uint64_t lastHash = 0;
    static double lastRun = -1000.0;
    static bool have = false;
    const std::uint64_t hash = SceneFingerprint(ed.scene());
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (have && hash == lastHash) return;
    if (have && now - lastRun < 0.25) return;
    ProjectDiagOptions opt;
    opt.assetRoot = ed.projectDir();
    ProjectDiagResult result = CollectProjectDiagnostics(ed.scene(), opt);
    EditorDiagnostics().ReplaceOwner("project", std::move(result.items));
    lastHash = hash;
    lastRun = now;
    have = true;
}

BuildDiagGate DiagnoseSceneForBuild(const Scene& scene, const std::string& assetRoot, const std::string& label) {
    ProjectDiagOptions opt;
    opt.assetRoot = assetRoot;
    ProjectDiagResult result = CollectProjectDiagnostics(scene, opt);
    BuildDiagGate gate;
    for (const Diagnostic& d : result.items) {
        if (d.severity == DiagSeverity::Info) continue;
        std::string line = label + d.message;
        if (!d.source.objectPath.empty()) line = label + d.source.objectPath + ": " + d.message;
        if (d.severity == DiagSeverity::Error) { ++gate.errors; gate.errorLines.push_back(std::move(line)); }
        else { ++gate.warnings; gate.warningLines.push_back(std::move(line)); }
    }
    return gate;
}

void DrawObjectDiagMarker(const GameObject* go, ImVec2 rowMin, ImVec2 rowMax) {
    if (!go) return;
    const std::vector<Diagnostic> rows = EditorDiagnostics().ForObject(go->Guid());
    if (rows.empty()) return;
    bool error = false;
    for (const Diagnostic& d : rows) if (d.severity == DiagSeverity::Error) error = true;
    const float mid = (rowMin.y + rowMax.y) * 0.5f;
    DrawMarker(ImVec2(rowMax.x - 48.0f, mid), error, TipFor(rows));
}

void DrawComponentDiagMarker(const GameObject* go, const char* label, ImVec2 headerMin, ImVec2 headerMax) {
    if (!go || !label) return;
    std::vector<Diagnostic> rows;
    for (const Diagnostic& d : EditorDiagnostics().ForObject(go->Guid()))
        if (ComponentMatches(d, label)) rows.push_back(d);
    if (rows.empty()) return;
    bool error = false;
    for (const Diagnostic& d : rows) if (d.severity == DiagSeverity::Error) error = true;
    const float mid = (headerMin.y + headerMax.y) * 0.5f;
    const float fh = ImGui::GetFrameHeight();
    DrawMarker(ImVec2(headerMax.x - fh - 18.0f, mid), error, TipFor(rows));
}

void DrawSceneDiagMarker(ImVec2 rowMin, ImVec2 rowMax) {
    std::vector<Diagnostic> rows;
    for (const Diagnostic& d : EditorDiagnostics().Items())
        if (d.source.objectGuid.empty() && d.source.objectPath.empty()) rows.push_back(d);
    if (rows.empty()) return;
    bool error = false;
    for (const Diagnostic& d : rows) if (d.severity == DiagSeverity::Error) error = true;
    DrawMarker(ImVec2(rowMax.x, (rowMin.y + rowMax.y) * 0.5f), error, TipFor(rows));
}

void DrawProblems(okay::editor::EditorState& ed) {
    BindProblemsEditor(&ed);
    const DiagTotals totals = ProjectDiagTotals();
    char title[64];
    if (totals.error > 0)
        std::snprintf(title, sizeof(title), "Problems  [x %d]###Problems", totals.error);
    else if (totals.warning > 0)
        std::snprintf(title, sizeof(title), "Problems  [! %d]###Problems", totals.warning);
    else
        std::snprintf(title, sizeof(title), "Problems###Problems");

    if (g_demo) ++g_demoFrame;
    if (g_demo && g_demoFrame < 34)
        ImGui::SetNextWindowFocus();
    const bool open = ImGui::Begin(title, &g_showProblems);
    if (open) {

    static char filter[96] = "";
    static bool showErr = true, showWarn = true, showInfo = false;
    const ImGuiStyle& st = ImGui::GetStyle();
    auto pillW = [&](int count) {
        char b[16]; std::snprintf(b, sizeof(b), "%d", count);
        return ImGui::CalcTextSize(b).x + ImGui::GetFontSize() * 0.9f + st.FramePadding.x * 2.0f;
    };
    const float pills = pillW(totals.error) + pillW(totals.warning) + pillW(totals.info) + st.ItemSpacing.x;
    SearchField("##pfilter", filter, sizeof(filter), "Search problems", -(pills + st.ItemSpacing.x));
    auto pill = [&](const char* id, bool& on, const ImVec4& col, int count, const char* tip) {
        char buf[32]; std::snprintf(buf, sizeof(buf), "%d%s", count, id);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(1.0f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(on ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? col : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Button(buf, ImVec2(pillW(count), 0))) on = !on;
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        const ImVec2 mn = ImGui::GetItemRectMin();
        const float dotR = ImGui::GetFontSize() * 0.2f;
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(mn.x + st.FramePadding.x + dotR, (mn.y + ImGui::GetItemRectMax().y) * 0.5f), dotR,
            ImGui::GetColorU32(on ? col : ImVec4(col.x, col.y, col.z, 0.35f)));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };
    ImGui::SameLine();
    pill("##pe", showErr, SevColor(DiagSeverity::Error), totals.error, "Show errors");
    ImGui::SameLine(0, st.ItemSpacing.x * 0.5f);
    pill("##pw", showWarn, SevColor(DiagSeverity::Warning), totals.warning, "Show warnings");
    ImGui::SameLine(0, st.ItemSpacing.x * 0.5f);
    pill("##pi", showInfo, SevColor(DiagSeverity::Info), totals.info, "Show notes");
    ImGui::Separator();

    std::string needle = filter;
    for (char& c : needle) c = (char)std::tolower((unsigned char)c);
    std::vector<Diagnostic> rows = EditorDiagnostics().Items();
    std::stable_sort(rows.begin(), rows.end(), [](const Diagnostic& a, const Diagnostic& b) {
        if ((int)a.severity != (int)b.severity) return (int)a.severity > (int)b.severity;
        return GroupOf(a) < GroupOf(b);
    });

    std::unordered_map<std::string, std::vector<int>> groups;
    std::vector<std::string> order;
    std::unordered_set<std::string> seenRow;
    for (int i = 0; i < (int)rows.size(); ++i) {
        const Diagnostic& d = rows[i];
        if (d.severity == DiagSeverity::Error && !showErr) continue;
        if (d.severity == DiagSeverity::Warning && !showWarn) continue;
        if (d.severity == DiagSeverity::Info && !showInfo) continue;
        if (!needle.empty()) {
            std::string hay = d.message + " " + d.code + " " + d.source.objectPath + " " + d.hint;
            for (char& c : hay) c = (char)std::tolower((unsigned char)c);
            if (hay.find(needle) == std::string::npos) continue;
        }
        const std::string ident = d.code + "\n" + d.message + "\n" + d.source.objectGuid + "\n" + d.source.nodeId;
        if (!seenRow.insert(ident).second) continue;
        const std::string g = GroupOf(d);
        if (!groups.count(g)) order.push_back(g);
        groups[g].push_back(i);
    }

    if (order.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled(totals.error + totals.warning + totals.info == 0
                                ? "Nothing is wrong in this scene."
                                : "Nothing matches this filter.");
    }
    for (const std::string& name : order) {
        const std::vector<int>& idx = groups[name];
        int errs = 0, warns = 0;
        for (int i : idx) {
            if (rows[i].severity == DiagSeverity::Error) ++errs;
            else if (rows[i].severity == DiagSeverity::Warning) ++warns;
        }
        ImGui::Spacing();
        ImGui::TextDisabled("%s    %d%s", name.c_str(), (int)idx.size(),
                            errs ? "  error" : warns ? "  warning" : "");
        ImGui::Indent(8.0f);
        for (int i : idx) {
            const Diagnostic& d = rows[i];
            ImGui::PushID((int)d.id);
            const bool canFix = d.fix.kind != DiagFixKind::None;
            if (g_demo && g_demoFrame >= 14 && d.code == "player.missing_collider")
                ImGui::SetScrollHereY(0.45f);
            if (canFix) {
                if (ImGui::SmallButton("Fix")) {
                    ed.PushUndo();
                    std::string err;
                    if (ApplyDiagnosticFix(ed.scene(), d, &err)) {
                        ed.dirty = true;
                        ConsoleLog("Fixed: " + d.message);
                    } else {
                        ConsoleLog(err.empty() ? "Could not fix that." : err, 1);
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.hint.c_str());
                if (g_demo && d.code == "player.missing_collider") {
                    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                    g_fixClick = ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
                    g_haveFix = true;
                }
                ImGui::SameLine();
            }
            ImGui::PushStyleColor(ImGuiCol_Text, SevColor(d.severity));
            ImGui::TextUnformatted((std::string(SevName(d.severity)) + "   " + d.message).c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemClicked()) OpenDiagnostic(ed, d);
            if (g_demo && d.code == "player.missing_collider") {
                const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                g_rowClick = ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
                g_haveRow = true;
            }
            if (ImGui::IsItemHovered()) {
                std::string tip = d.message;
                if (!d.hint.empty()) tip += "\n\n" + d.hint;
                if (!d.code.empty()) tip += "\n\n" + d.code;
                ImGui::SetTooltip("%s", tip.c_str());
            }
            ImGui::PopID();
        }
        ImGui::Unindent(8.0f);
    }

    }
    ImGui::End();

    if (g_demo) {
        const std::string dir = "/opt/cursor/artifacts";
        if (g_demoFrame == 8) RequestShot(dir + "/diag-problems.png", 1);
        if (g_demoFrame == 18 && g_haveRow && g_rowClick.y > 40.0f && g_rowClick.y < 760.0f)
            QueueClick(g_rowClick);
        if (g_demoFrame == 20) RequestShot(dir + "/diag-select.png", 1);
        if (g_demoFrame == 26 && g_haveFix && g_fixClick.y > 40.0f && g_fixClick.y < 760.0f)
            QueueClick(g_fixClick);
        if (g_demoFrame == 30) RequestShot(dir + "/diag-fix.png", 1);
        if (g_demoFrame == 34) {
            for (const auto& up : ed.scene().Objects()) {
                if (!up || up->name != "BadGraph") continue;
                if (VisualScriptComponent* vs = up->GetComponent<VisualScriptComponent>()) {
                    okay::editor::vsed::ShowGraphText(vs->Source(), 1);
                    g_showVisualScript = true;
                    ed.Select(up.get());
                }
            }
        }
        if (g_demoFrame == 48) RequestShot(dir + "/diag-graph.png", 1);
        if (g_demoFrame == 56) { ed.dirty = false; g_quitRequested = true; }
    }
}

void DiagnosticsDemoPrepare(okay::editor::EditorState& ed) {
    g_demo = true;
    g_showProblems = true;
    g_showScriptEditor = false;
    std::vector<GameObject*> cameras;
    for (const auto& up : ed.scene().Objects())
        if (up && up->GetComponent<Camera>()) cameras.push_back(up.get());
    for (GameObject* c : cameras) ed.scene().Destroy(c);
    ed.scene().FlushDestroyed();

    GameObject* crate = ed.CreateEmpty("Crate");
    crate->AddComponent<SpriteRenderer>()->texture = "Assets/missing_crate.png";

    GameObject* chaser = ed.CreateEmpty("Chaser");
    chaser->AddComponent<FollowTarget2D>()->target = "Nope";

    GameObject* player = ed.CreateEmpty("Player");
    player->AddComponent<CharacterController3D>();
    if (player->transform) player->transform->SetPosition({1.5f, 0, 0});

    GameObject* puppet = ed.CreateEmpty("Puppet");
    puppet->AddComponent<AnimStateMachine>();

    GameObject* walker = ed.CreateEmpty("Walker");
    walker->AddComponent<NavMeshAgent>();

    GameObject* missing = ed.CreateEmpty("MissingScript");
    missing->AddComponent<ScriptComponent>()->SetPath("Assets/MissingScript.okay");

    GameObject* graph = ed.CreateEmpty("BadGraph");
    graph->AddComponent<VisualScriptComponent>()->LoadFromText(
        "node 0 OnStart\nnode 1 Transform.Translate/1\nnode 2 Const \"hello\"\n"
        "pos 0 40 60\npos 1 300 40\npos 2 300 200\n"
        "data 1 0 2 0\nexec 0 0 1\nentry OnStart 0\n");
    ed.Select(nullptr);
    ed.dirty = true;
}

} // namespace oked
