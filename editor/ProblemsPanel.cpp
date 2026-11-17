#include "EditorCommon.hpp"
#include "ProblemsPanel.hpp"
#include "EditorWidgets.hpp"
#include "ConsolePanel.hpp"
#include "MainShared.hpp"
#include "VisualScriptEditor.hpp"
#include "EditorPanelViews.hpp"   // the Dear ImGui body (the model is shared/EditorPanelModels.hpp)
#include "OkayPanels.hpp"         // the OkayUI port (opt-in backend)
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

// Demo bookkeeping. The dev hook builds the scene; the panel opens and fixes one row on set frames.
bool g_demo = false;
int g_demoFrame = 0;

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

bool ObjectHasDiag(const GameObject* go) {
    return go && !EditorDiagnostics().ForObject(go->Guid()).empty();
}

bool ComponentHasDiag(const GameObject* go, const char* label) {
    if (!go || !label) return false;
    for (const Diagnostic& d : EditorDiagnostics().ForObject(go->Guid()))
        if (ComponentMatches(d, label)) return true;
    return false;
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

int ComponentDiagLevel(const GameObject* go, const char* label, std::string* tip) {
    if (!go || !label) return 0;
    std::vector<Diagnostic> rows;
    for (const Diagnostic& d : EditorDiagnostics().ForObject(go->Guid()))
        if (ComponentMatches(d, label)) rows.push_back(d);
    if (rows.empty()) return 0;
    bool error = false;
    for (const Diagnostic& d : rows) if (d.severity == DiagSeverity::Error) error = true;
    if (tip) *tip = TipFor(rows);
    return error ? 2 : 1;
}

int ObjectDiagLevel(const GameObject* go, std::string* tip) {
    if (!go) return 0;
    const std::vector<Diagnostic> rows = EditorDiagnostics().ForObject(go->Guid());
    if (rows.empty()) return 0;
    bool error = false;
    for (const Diagnostic& d : rows) if (d.severity == DiagSeverity::Error) error = true;
    if (tip) *tip = TipFor(rows);
    return error ? 2 : 1;
}

int SceneDiagLevel(std::string* tip) {
    std::vector<Diagnostic> rows;
    for (const Diagnostic& d : EditorDiagnostics().Items())
        if (d.source.objectGuid.empty() && d.source.objectPath.empty()) rows.push_back(d);
    if (rows.empty()) return 0;
    bool error = false;
    for (const Diagnostic& d : rows) if (d.severity == DiagSeverity::Error) error = true;
    if (tip) *tip = TipFor(rows);
    return error ? 2 : 1;
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
    if (g_demo && g_demoFrame < 6)
        ImGui::SetWindowFocus("Console###Console");
    else if (g_demo && g_demoFrame < 34)
        ImGui::SetNextWindowFocus();

    // The model both versions draw (shared/EditorPanelModels.hpp): the diagnostics as rows, in the store's order.
    const std::vector<Diagnostic> diags = EditorDiagnostics().Items();
    okaypanels::ProblemsModel model;
    model.rows.reserve(diags.size());
    for (const Diagnostic& d : diags) {
        okaypanels::ProblemRow r;
        r.id = d.id; r.severity = (int)d.severity; r.code = d.code; r.message = d.message; r.hint = d.hint;
        r.objectPath = d.source.objectPath; r.objectGuid = d.source.objectGuid; r.nodeId = d.source.nodeId;
        r.group = GroupOf(d); r.canFix = d.fix.kind != DiagFixKind::None;
        model.rows.push_back(std::move(r));
    }
    model.errors = totals.error; model.warnings = totals.warning; model.infos = totals.info;
    static okaypanels::ProblemsState state;   // the filter and the three severity pills, kept for the editor's lifetime

    okaypanels::ProblemsIntent intent;
    if (!(UseOkayUIBackend() && DrawProblemsOkayUI(title, &g_showProblems, model, state, intent))) {
        if (ImGui::Begin(title, &g_showProblems)) {
            PanelViewHooks hooks;
            hooks.searchField = &SearchField;
            intent = DrawProblemsBodyImGui(model, state, hooks);
        }
        ImGui::End();
    }

    // The demo (OKAY_UI_DIAG=1) opens and fixes the player's missing collider the way a click on its row and on its Fix would.
    int demoRow = -1;
    if (g_demo)
        for (std::size_t i = 0; i < diags.size(); ++i) if (diags[i].code == "player.missing_collider") demoRow = (int)i;
    if (g_demo && g_demoFrame == 18 && demoRow >= 0) intent.open = demoRow;
    if (g_demo && g_demoFrame == 26 && demoRow >= 0) intent.fix = demoRow;

    if (intent.open >= 0 && intent.open < (int)diags.size()) OpenDiagnostic(ed, diags[(std::size_t)intent.open]);
    if (intent.fix >= 0 && intent.fix < (int)diags.size()) {
        const Diagnostic& d = diags[(std::size_t)intent.fix];
        ed.PushUndo();
        std::string err;
        if (ApplyDiagnosticFix(ed.scene(), d, &err)) {
            ed.dirty = true;
            ConsoleLog("Fixed: " + d.message);
        } else {
            ConsoleLog(err.empty() ? "Could not fix that." : err, 1);
        }
    }
    if (intent.copy >= 0 && intent.copy < (int)model.rows.size())
        ImGui::SetClipboardText(okaypanels::ProblemCopyText(model.rows[(std::size_t)intent.copy]).c_str());

    if (g_demo) {
        const std::string dir = "/opt/cursor/artifacts";
        if (g_demoFrame == 5) RequestShot(dir + "/diag-console.png", 1);
        if (g_demoFrame == 8) RequestShot(dir + "/diag-problems.png", 1);
        if (g_demoFrame == 20) RequestShot(dir + "/diag-select.png", 1);
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

    GameObject* missing = ed.CreateEmpty("MissingScriptComponentFile");
    missing->AddComponent<ScriptComponent>()->SetPath("Assets/MissingScript.okay");

    GameObject* graph = ed.CreateEmpty("BadGraph");
    graph->AddComponent<VisualScriptComponent>()->LoadFromText(
        "node 0 OnStart\nnode 1 Transform.Translate/1\nnode 2 Const \"hello\"\n"
        "pos 0 40 60\npos 1 300 40\npos 2 300 200\n"
        "data 1 0 2 0\nexec 0 0 1\nentry OnStart 0\n");
    ed.Select(nullptr);
    ed.dirty = true;
    ConsoleLog("[script] Player.okay:4 the player script failed");
    ConsoleLog("[assets] ghost.png is not in the project");
    ConsoleLog("[net] peer timed out");
    ConsolePreferCategory("script");
}

} // namespace oked
