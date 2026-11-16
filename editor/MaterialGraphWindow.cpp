// The Material Graph window. See MaterialGraphWindow.hpp. Written from our own design (no other editor's code used).
#include "MaterialGraphWindow.hpp"
#include "ConsolePanel.hpp"
#include "ExamplesRoot.hpp"
#include "FileDialog.hpp"
#include "okay/Render/MaterialGraph.hpp"
#include "okay/Render/ProcTexture.hpp"
#include "imgui.h"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>

namespace oked {

bool g_showMaterialGraph = false;

namespace {

namespace mg = okay::matgraph;

struct State {
    mg::Graph graph;
    mg::Program prog;
    std::vector<mg::Issue> issues;
    std::string path;          // empty: unsaved
    bool dirty = false;        // changed since the last save
    bool recompile = true;
    bool stale = true;         // the preview picture needs redrawing
    bool animate = false;
    float time = 0.0f;
    SDL_Texture* tex = nullptr;
    int addType = 0;
    std::map<std::string, okay::Image> images;   // texture cache for the preview
};
State g;

void Recompile() {
    g.issues = mg::Validate(g.graph);
    mg::Compile(g.graph, g.prog);
    g.recompile = false;
}

void Changed() { g.dirty = true; g.recompile = true; g.stale = true; }

void SetGraph(const mg::Graph& gr, const std::string& path, bool dirty) {
    g.graph = gr; g.path = path; g.dirty = dirty; g.recompile = true; g.stale = true; g.images.clear();
    g_showMaterialGraph = true;
}

// The preview's texture reader: a project image, or "proc:<name>" for the built-in procedural textures.
mg::TextureSampler Sampler() {
    return [](const std::string& p, float u, float v, float* rgba) {
        if (p.empty()) return;
        auto it = g.images.find(p);
        if (it == g.images.end()) {
            okay::Image img;
            if (p.rfind("proc:", 0) == 0) img = okay::GenerateProcTexture(p.substr(5));
            else img.Load(p);
            it = g.images.emplace(p, std::move(img)).first;
        }
        if (!it->second.Valid()) return;
        okay::Color c = it->second.Sample(u - std::floor(u), v - std::floor(v));
        rgba[0] = c.r; rgba[1] = c.g; rgba[2] = c.b; rgba[3] = c.a;
    };
}

void UpdatePreview(SDL_Renderer* r) {
    if (!r || !g.prog.valid()) return;
    okay::Image img = mg::RenderPreviewSphere(g.prog, 160, g.time, Sampler());
    if (!g.tex) {
        g.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, 160, 160);
        if (g.tex) { SDL_SetTextureBlendMode(g.tex, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(g.tex, SDL_ScaleModeLinear); }
    }
    if (g.tex) SDL_UpdateTexture(g.tex, nullptr, img.Data(), 160 * 4);
}

// Where a pin sits on the canvas (relative to the node's top-left).
constexpr float kNodeW = 150.0f, kRowH = 18.0f, kHead = 22.0f;
ImVec2 PinPos(const mg::Node& n, bool output, int index) {
    return ImVec2(n.posX + (output ? kNodeW : 0.0f), n.posY + kHead + kRowH * (index + 0.5f));
}

void DrawCanvas() {
    ImGui::BeginChild("##mgcanvas", ImVec2(0, 260), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetCursorScreenPos();
    float maxX = 0, maxY = 0;
    auto at = [&](ImVec2 p) { return ImVec2(o.x + p.x + 8, o.y + p.y + 8); };
    for (const mg::Link& l : g.graph.links) {
        const mg::Node* a = g.graph.Find(l.fromNode); const mg::Node* b = g.graph.Find(l.toNode);
        const mg::NodeDef* da = a ? mg::FindNodeDef(a->type) : nullptr; const mg::NodeDef* db = b ? mg::FindNodeDef(b->type) : nullptr;
        if (!da || !db) continue;
        int oi = -1, ii = -1;
        for (size_t i = 0; i < da->outputs.size(); ++i) if (l.fromPin == da->outputs[i].name) oi = (int)i;
        for (size_t i = 0; i < db->inputs.size(); ++i) if (l.toPin == db->inputs[i].name) ii = (int)i;
        if (oi < 0 || ii < 0) continue;
        ImVec2 p0 = at(PinPos(*a, true, oi)), p1 = at(PinPos(*b, false, ii));
        float d = std::max(40.0f, std::fabs(p1.x - p0.x) * 0.5f);
        dl->AddBezierCubic(p0, ImVec2(p0.x + d, p0.y), ImVec2(p1.x - d, p1.y), p1, IM_COL32(120, 190, 255, 220), 2.0f);
    }
    for (const mg::Node& n : g.graph.nodes) {
        const mg::NodeDef* d = mg::FindNodeDef(n.type);
        int rows = d ? (int)std::max(d->inputs.size(), d->outputs.size()) : 1;
        ImVec2 a = at(ImVec2(n.posX, n.posY)), b = at(ImVec2(n.posX + kNodeW, n.posY + kHead + kRowH * rows + 4));
        maxX = std::max(maxX, n.posX + kNodeW + 24); maxY = std::max(maxY, n.posY + kHead + kRowH * rows + 24);
        dl->AddRectFilled(a, b, IM_COL32(38, 40, 48, 255), 5.0f);
        dl->AddRect(a, b, d ? IM_COL32(90, 95, 110, 255) : IM_COL32(220, 70, 70, 255), 5.0f);
        char title[96]; std::snprintf(title, sizeof(title), "%s  #%d", n.type.c_str(), n.id);
        dl->AddText(ImVec2(a.x + 6, a.y + 3), IM_COL32(235, 235, 240, 255), title);
        if (!d) continue;
        for (size_t i = 0; i < d->inputs.size(); ++i) {
            ImVec2 p = at(PinPos(n, false, (int)i));
            dl->AddCircleFilled(p, 4.0f, IM_COL32(240, 190, 90, 255));
            dl->AddText(ImVec2(p.x + 8, p.y - 7), IM_COL32(190, 192, 200, 255), d->inputs[i].name);
        }
        for (size_t i = 0; i < d->outputs.size(); ++i) {
            ImVec2 p = at(PinPos(n, true, (int)i));
            dl->AddCircleFilled(p, 4.0f, IM_COL32(120, 210, 150, 255));
            float w = ImGui::CalcTextSize(d->outputs[i].name).x;
            dl->AddText(ImVec2(p.x - 8 - w, p.y - 7), IM_COL32(190, 192, 200, 255), d->outputs[i].name);
        }
    }
    ImGui::Dummy(ImVec2(maxX + 16, maxY + 16));
    ImGui::EndChild();
}

// One input's source: "(value)" or a wire from another node's output. Returns true when something changed.
bool EditInput(mg::Node& n, const mg::NodeDef& d, int pin) {
    bool changed = false;
    const mg::PinDef& pd = d.inputs[pin];
    mg::Link* link = nullptr;
    for (mg::Link& l : g.graph.links) if (l.toNode == n.id && l.toPin == pd.name) link = &l;
    std::string cur = link ? "#" + std::to_string(link->fromNode) + "." + link->fromPin : "(value)";
    ImGui::PushID(pin);
    ImGui::SetNextItemWidth(130);
    if (ImGui::BeginCombo(pd.name, cur.c_str())) {
        if (ImGui::Selectable("(value)", !link) && link) {
            const std::string pn = pd.name;
            g.graph.links.erase(std::remove_if(g.graph.links.begin(), g.graph.links.end(),
                [&](const mg::Link& l) { return l.toNode == n.id && l.toPin == pn; }), g.graph.links.end());
            changed = true; link = nullptr;
        }
        for (const mg::Node& other : g.graph.nodes) {
            if (other.id == n.id) continue;
            const mg::NodeDef* od = mg::FindNodeDef(other.type);
            if (!od) continue;
            for (const mg::PinDef& op : od->outputs) {
                std::string label = "#" + std::to_string(other.id) + " " + other.type + "." + op.name;
                bool sel = link && link->fromNode == other.id && link->fromPin == op.name;
                if (ImGui::Selectable(label.c_str(), sel) && !sel) { g.graph.Connect(other.id, op.name, n.id, pd.name); changed = true; }
            }
        }
        ImGui::EndCombo();
    }
    if (!link) {   // unwired: edit the pin's own value
        ImGui::SameLine();
        std::array<float, 4> v{pd.def[0], pd.def[1], pd.def[2], pd.def[3]};
        auto it = n.defaults.find(pd.name);
        if (it != n.defaults.end()) v = it->second;
        int k = pd.type == mg::PinType::Any ? 1 : (int)pd.type;
        ImGui::SetNextItemWidth(150);
        if (pd.implicit) ImGui::TextDisabled("(surface %s unless set)", pd.implicit == 1 ? "UV" : "normal");
        else if (ImGui::DragScalarN("##v", ImGuiDataType_Float, v.data(), k, 0.01f)) { n.defaults[pd.name] = v; changed = true; }
    }
    ImGui::PopID();
    return changed;
}

void DrawNodeList() {
    ImGui::BeginChild("##mglist", ImVec2(0, 0), true);
    int removeId = 0;
    for (mg::Node& n : g.graph.nodes) {
        const mg::NodeDef* d = mg::FindNodeDef(n.type);
        ImGui::PushID(n.id);
        std::string head = "#" + std::to_string(n.id) + "  " + n.type;
        if (ImGui::TreeNodeEx(head.c_str(), n.type == "Output" ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            if (!d) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Unknown node type");
            else {
                if (d->help) ImGui::TextDisabled("%s", d->help);
                if (d->hasValue) {
                    int k = n.type == "Float" ? 1 : n.type == "Vec2" ? 2 : 3;
                    if (n.type == "Vec3") { if (ImGui::ColorEdit3("Value", n.value)) Changed(); }
                    else if (ImGui::DragScalarN("Value", ImGuiDataType_Float, n.value, k, 0.01f)) Changed();
                }
                if (d->hasPath) {
                    char buf[256]; std::snprintf(buf, sizeof(buf), "%s", n.path.c_str());
                    ImGui::SetNextItemWidth(240);
                    if (ImGui::InputText("Image path", buf, sizeof(buf))) { n.path = buf; g.images.erase(n.path); Changed(); }
                    ImGui::SameLine(); ImGui::TextDisabled("(or proc:checker, proc:brick ...)");
                }
                for (size_t i = 0; i < d->inputs.size(); ++i) if (EditInput(n, *d, (int)i)) Changed();
                if (n.type != "Output" && ImGui::SmallButton("Delete node")) removeId = n.id;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeId) {
        g.graph.nodes.erase(std::remove_if(g.graph.nodes.begin(), g.graph.nodes.end(), [&](const mg::Node& n) { return n.id == removeId; }), g.graph.nodes.end());
        g.graph.links.erase(std::remove_if(g.graph.links.begin(), g.graph.links.end(),
            [&](const mg::Link& l) { return l.fromNode == removeId || l.toNode == removeId; }), g.graph.links.end());
        Changed();
    }
    ImGui::EndChild();
}

} // namespace

std::vector<std::string> MaterialGraphExampleFiles() {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;
    auto walk = [&](fs::path d) {
        for (int i = 0; i < 5 && !d.empty(); ++i) {
            roots.push_back(d / "examples" / "material_graph");
            if (d == d.parent_path()) break;
            d = d.parent_path();
        }
    };
    std::error_code ec;
    walk(fs::current_path(ec));
    if (!okay::editor::ExamplesExeDir().empty()) walk(fs::path(okay::editor::ExamplesExeDir()));
    std::vector<std::string> out;
    for (const fs::path& root : roots) {
        if (!fs::is_directory(root, ec)) continue;
        for (const auto& f : fs::directory_iterator(root, ec)) {
            if (ec) break;
            if (f.path().extension() == ".okaymatgraph") out.push_back(f.path().lexically_normal().string());
        }
        if (!out.empty()) break;
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool OpenMaterialGraphFile(const std::string& path) {
    mg::Graph gr; std::string err;
    if (!mg::Graph::LoadFromFile(path, gr, &err)) { ConsoleLog("Material graph: " + path + ": " + err, 2); return false; }
    SetGraph(gr, path, false);
    return true;
}

void OpenMaterialGraphExample() { SetGraph(mg::ExampleGraph(), "", true); }

void DrawMaterialGraphWindow(SDL_Renderer* renderer) {
    if (!g_showMaterialGraph) return;
    ImGui::SetNextWindowSize(ImVec2(760, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Material Graph", &g_showMaterialGraph)) { ImGui::End(); return; }
    if (g.graph.nodes.empty()) {
        ImGui::TextWrapped("A material graph builds a surface from small nodes. This window checks the graph and draws it on a preview "
                           "sphere with the CPU. It does not change what the game draws yet.");
        if (ImGui::Button("Open Example")) OpenMaterialGraphExample();
        ImGui::SameLine();
        if (ImGui::Button("Open File...")) okay::filedialog::OpenOne("Open a material graph", "", okay::filedialog::Filters("Material graph", {"*.okaymatgraph"}),
                                                                      [](const std::string& p) { OpenMaterialGraphFile(p); });
        for (const std::string& f : MaterialGraphExampleFiles())
            if (ImGui::Selectable(std::filesystem::path(f).filename().string().c_str())) OpenMaterialGraphFile(f);
        ImGui::End();
        return;
    }
    if (g.recompile) Recompile();
    ImGui::TextDisabled("%s%s", g.path.empty() ? "(unsaved example)" : g.path.c_str(), g.dirty ? "  *" : "");
    // Left: preview. Right: buttons and checks.
    if (g.animate) { g.time += ImGui::GetIO().DeltaTime; g.stale = true; }
    if (g.prog.valid() && (g.stale || !g.tex)) { UpdatePreview(renderer); g.stale = false; }
    ImGui::BeginGroup();
    if (g.tex && g.prog.valid()) ImGui::Image((ImTextureID)g.tex, ImVec2(160, 160));
    else ImGui::Dummy(ImVec2(160, 160));
    ImGui::Checkbox("Animate Time", &g.animate);
    if (!g.animate) { ImGui::SetNextItemWidth(160); if (ImGui::SliderFloat("##t", &g.time, 0.0f, 10.0f, "Time %.2f")) g.stale = true; }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::Button("Save")) {
        if (g.path.empty()) okay::filedialog::SaveOne("Save the material graph", "material.okaymatgraph", okay::filedialog::Filters("Material graph", {"*.okaymatgraph"}),
                                                       [](const std::string& p) { if (g.graph.SaveToFile(p)) { g.path = p; g.dirty = false; ConsoleLog("Saved " + p); } else ConsoleLog("Save failed: " + p, 2); });
        else if (g.graph.SaveToFile(g.path)) { g.dirty = false; ConsoleLog("Saved " + g.path); }
        else ConsoleLog("Save failed: " + g.path, 2);
    }
    ImGui::SameLine();
    if (ImGui::Button("Open...")) okay::filedialog::OpenOne("Open a material graph", "", okay::filedialog::Filters("Material graph", {"*.okaymatgraph"}),
                                                            [](const std::string& p) { OpenMaterialGraphFile(p); });
    ImGui::SameLine();
    if (ImGui::Button("New")) { mg::Graph e; e.Add("Output", 600, 40); SetGraph(e, "", true); }
    ImGui::SetNextItemWidth(160);
    const auto& lib = mg::NodeLibrary();
    std::vector<const mg::NodeDef*> addable;
    for (const mg::NodeDef& d : lib) if (std::string(d.type) != "Output") addable.push_back(&d);
    g.addType = std::min(g.addType, (int)addable.size() - 1);
    if (ImGui::BeginCombo("##addtype", addable[g.addType]->type)) {
        for (size_t i = 0; i < addable.size(); ++i) {
            std::string label = std::string(addable[i]->category) + " / " + addable[i]->type;
            if (ImGui::Selectable(label.c_str(), (int)i == g.addType)) g.addType = (int)i;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Node")) {
        float y = 20; for (const mg::Node& n : g.graph.nodes) y = std::max(y, n.posY + 70);
        g.graph.Add(addable[g.addType]->type, 20, y > 400 ? 20 : y);
        Changed();
    }
    int errors = 0, warns = 0;
    for (const mg::Issue& i : g.issues) (i.error ? errors : warns)++;
    if (!errors) ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.55f, 1), "Graph is valid (%d warning%s)", warns, warns == 1 ? "" : "s");
    else ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1), "%d problem%s", errors, errors == 1 ? "" : "s");
    ImGui::BeginChild("##mgissues", ImVec2(0, 70), true);
    for (const mg::Issue& i : g.issues)
        ImGui::TextColored(i.error ? ImVec4(1, 0.55f, 0.5f, 1) : ImVec4(0.9f, 0.8f, 0.4f, 1), "%s", i.message.c_str());
    ImGui::EndChild();
    ImGui::EndGroup();
    DrawCanvas();
    DrawNodeList();
    ImGui::End();
}

} // namespace oked
