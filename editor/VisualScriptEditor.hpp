#pragma once
// A node-based visual scripting editor for OkayVS graphs (.okayvs). You drag
// nodes onto a canvas, wire execution (triangles) and data (circles) pins, edit
// per-node values, and save a .okayvs file the engine's VisualScriptComponent
// runs at play time. Self-contained: it keeps its own editable model and writes
// the same text format the engine parses (node/pos/exec/data/entry lines).
#include "imgui.h"
#include "EditorState.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace okay::editor::vsed {

// ---- Node registry: describes each node type the editor can place ----------
enum VsCat { CAT_EVENT, CAT_FLOW, CAT_VAR, CAT_MATH, CAT_LOGIC, CAT_INPUT, CAT_XFORM, CAT_ACTION, CAT_DATA, CAT_COUNT };

inline ImU32 CatColor(int cat) {
    switch (cat) {
        case CAT_EVENT:  return IM_COL32(120, 84, 84, 255);
        case CAT_FLOW:   return IM_COL32(96, 96, 112, 255);
        case CAT_VAR:    return IM_COL32(74, 102, 92, 255);
        case CAT_MATH:   return IM_COL32(70, 92, 110, 255);
        case CAT_LOGIC:  return IM_COL32(96, 86, 112, 255);
        case CAT_INPUT:  return IM_COL32(104, 96, 72, 255);
        case CAT_XFORM:  return IM_COL32(78, 104, 104, 255);
        case CAT_ACTION: return IM_COL32(108, 88, 72, 255);
        default:         return IM_COL32(88, 88, 96, 255);
    }
}

struct VsDef {
    const char* type;                 // serialization name
    const char* title;                // shown in the node header
    int         cat;
    bool        execIn;
    std::vector<const char*> execOut; // exec output pin labels
    std::vector<const char*> dataIn;  // data input pin labels
    std::vector<const char*> dataOut; // data output pin labels
    int         param;                // 0 none, 1 text field, 2 compare-op combo
    const char* paramHint;
};

// A curated, genuinely-useful set of nodes (all supported by the engine runtime).
inline const std::vector<VsDef>& Registry() {
    static const std::vector<VsDef> defs = {
        // Events
        {"OnStart",  "On Start",  CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnUpdate", "On Update", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        // Flow
        {"Branch",   "Branch",    CAT_FLOW, true, {"True","False"}, {"Cond"}, {}, 0, nullptr},
        {"Sequence", "Sequence",  CAT_FLOW, true, {"0","1"}, {}, {}, 0, nullptr},
        {"Once",     "Once",      CAT_FLOW, true, {""}, {}, {}, 0, nullptr},
        {"Timer",    "Every (s)", CAT_FLOW, true, {""}, {}, {}, 1, "seconds"},
        // Variables
        {"GetVar",   "Get Var",   CAT_VAR, false, {}, {}, {"Value"}, 1, "name"},
        {"SetVar",   "Set Var",   CAT_VAR, true, {""}, {"Value"}, {}, 1, "name"},
        {"AddVar",   "Add Var",   CAT_VAR, true, {""}, {"Amount"}, {}, 1, "name"},
        {"Const",    "Const",     CAT_DATA, false, {}, {}, {"Value"}, 1, "value"},
        // Math
        {"Add", "Add (+)", CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Sub", "Sub (-)", CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Mul", "Mul (x)", CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Div", "Div (/)", CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Min", "Min",     CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Max", "Max",     CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Compare", "Compare", CAT_LOGIC, false, {}, {"A","B"}, {"Bool"}, 2, nullptr},
        // Logic
        {"And", "And", CAT_LOGIC, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Or",  "Or",  CAT_LOGIC, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Not", "Not", CAT_LOGIC, false, {}, {"In"}, {"Out"}, 0, nullptr},
        // Input
        {"Key",     "Key Held",  CAT_INPUT, false, {}, {}, {"Down"}, 1, "key"},
        {"KeyDown", "Key Down",  CAT_INPUT, false, {}, {}, {"Pressed"}, 1, "key"},
        {"AxisX",   "Axis X",    CAT_INPUT, false, {}, {}, {"X"}, 0, nullptr},
        {"AxisY",   "Axis Y",    CAT_INPUT, false, {}, {}, {"Y"}, 0, nullptr},
        {"Time",      "Time",      CAT_INPUT, false, {}, {}, {"t"}, 0, nullptr},
        {"DeltaTime", "Delta Time",CAT_INPUT, false, {}, {}, {"dt"}, 0, nullptr},
        // Transform
        {"GetX", "Get X", CAT_XFORM, false, {}, {}, {"X"}, 0, nullptr},
        {"GetY", "Get Y", CAT_XFORM, false, {}, {}, {"Y"}, 0, nullptr},
        {"Translate",   "Translate",   CAT_XFORM, true, {""}, {"X","Y","Z"}, {}, 0, nullptr},
        {"SetPosition", "Set Position", CAT_XFORM, true, {""}, {"X","Y","Z"}, {}, 0, nullptr},
        {"Rotate",      "Rotate",      CAT_XFORM, true, {""}, {"Deg"}, {}, 0, nullptr},
        // Actions
        {"Print",     "Print",     CAT_ACTION, true, {""}, {"Value"}, {}, 0, nullptr},
        {"PlaySound", "Play Sound",CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"Spawn",     "Spawn",     CAT_ACTION, true, {""}, {"X","Y"}, {}, 1, "prefab"},
        {"Destroy",   "Destroy",   CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        // --- More math (data) ---
        {"Mod",   "Mod (%)",  CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Pow",   "Pow (^)",  CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Abs",   "Abs",      CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Neg",   "Negate",   CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Sqrt",  "Sqrt",     CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Sin",   "Sin",      CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Cos",   "Cos",      CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Floor", "Floor",    CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Round", "Round",    CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Sign",  "Sign",     CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Clamp", "Clamp",    CAT_MATH, false, {}, {"Value","Min","Max"}, {"Out"}, 0, nullptr},
        {"Lerp",  "Lerp",     CAT_MATH, false, {}, {"A","B","T"}, {"Out"}, 0, nullptr},
        {"Random","Random 0-1", CAT_MATH, false, {}, {}, {"Out"}, 0, nullptr},
        {"RandomRange","Random Range", CAT_MATH, false, {}, {"Min","Max"}, {"Out"}, 0, nullptr},
        // --- More logic ---
        {"Xor",       "Xor",        CAT_LOGIC, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Equals",    "Equals",     CAT_LOGIC, false, {}, {"A","B"}, {"Bool"}, 0, nullptr},
        {"NotEquals", "Not Equals", CAT_LOGIC, false, {}, {"A","B"}, {"Bool"}, 0, nullptr},
        // --- More input ---
        {"KeyUp",  "Key Up",  CAT_INPUT, false, {}, {}, {"Released"}, 1, "key"},
        {"MouseX", "Mouse X", CAT_INPUT, false, {}, {}, {"X"}, 0, nullptr},
        {"MouseY", "Mouse Y", CAT_INPUT, false, {}, {}, {"Y"}, 0, nullptr},
        // --- Vectors / transform getters ---
        {"MakeVec3",    "Make Vec3",    CAT_DATA,  false, {}, {"X","Y","Z"}, {"Vec3"}, 0, nullptr},
        {"GetPosition", "Get Position", CAT_XFORM, false, {}, {}, {"Pos"}, 0, nullptr},
        {"GetZ",        "Get Z",        CAT_XFORM, false, {}, {}, {"Z"}, 0, nullptr},
        // --- More transform (exec) ---
        {"SetScale",    "Set Scale",    CAT_XFORM, true, {""}, {"X","Y","Z"}, {}, 0, nullptr},
        {"SetRotation", "Set Rotation", CAT_XFORM, true, {""}, {"Deg"}, {}, 0, nullptr},
        // --- More flow / actions ---
        {"Wait",     "Wait (s)",    CAT_FLOW,   true, {""}, {}, {}, 1, "seconds"},
        {"LogWarn",  "Log Warning", CAT_ACTION, true, {""}, {"Value"}, {}, 0, nullptr},
        {"LogError", "Log Error",   CAT_ACTION, true, {""}, {"Value"}, {}, 0, nullptr},
        {"AddForce", "Add Force",   CAT_ACTION, true, {""}, {"X","Y"}, {}, 0, nullptr},
        {"SetVelocity","Set Velocity",CAT_ACTION, true, {""}, {"X","Y"}, {}, 0, nullptr},
        {"AddImpulse","Add Impulse", CAT_ACTION, true, {""}, {"X","Y"}, {}, 0, nullptr},
        // --- Logic / selection ---
        {"Approx",  "Approx Equal", CAT_LOGIC, false, {}, {"A","B"}, {"Bool"}, 0, nullptr},
        {"Between", "Between",      CAT_LOGIC, false, {}, {"Value","Min","Max"}, {"Bool"}, 0, nullptr},
        {"Chance",  "Chance",       CAT_LOGIC, false, {}, {}, {"Bool"}, 1, "0..1"},
        {"HasVar",  "Has Var",      CAT_LOGIC, false, {}, {}, {"Bool"}, 1, "name"},
        {"Select",  "Select",       CAT_LOGIC, false, {}, {"Cond","A","B"}, {"Out"}, 0, nullptr},
        {"Concat",  "Concat (text)",CAT_DATA,  false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        // --- Vectors ---
        {"VecX", "Vec X", CAT_MATH, false, {}, {"Vec"}, {"X"}, 0, nullptr},
        {"VecY", "Vec Y", CAT_MATH, false, {}, {"Vec"}, {"Y"}, 0, nullptr},
        {"VecZ", "Vec Z", CAT_MATH, false, {}, {"Vec"}, {"Z"}, 0, nullptr},
        // --- Physics reads + tween ---
        {"VelX", "Velocity X", CAT_INPUT, false, {}, {}, {"X"}, 0, nullptr},
        {"VelY", "Velocity Y", CAT_INPUT, false, {}, {}, {"Y"}, 0, nullptr},
        {"EaseIn",    "Ease In",     CAT_MATH, false, {}, {"T"}, {"Out"}, 0, nullptr},
        {"EaseOut",   "Ease Out",    CAT_MATH, false, {}, {"T"}, {"Out"}, 0, nullptr},
        {"EaseInOut", "Ease In-Out", CAT_MATH, false, {}, {"T"}, {"Out"}, 0, nullptr},
    };
    return defs;
}

inline const VsDef* FindDef(const std::string& type) {
    for (const auto& d : Registry()) if (type == d.type) return &d;
    return nullptr;
}

// ---- Editable graph model --------------------------------------------------
struct VsEditNode { int id; std::string type; std::string arg; float x = 0, y = 0; };
struct VsExecLink { int from, pin, to; };
struct VsDataLink { int consumer, inPin, producer, outPin; };

struct VsEditGraph {
    std::vector<VsEditNode> nodes;
    std::vector<VsExecLink> execLinks;
    std::vector<VsDataLink> dataLinks;
    int nextId = 1;
    std::string path;      // .okayvs file (empty = unsaved)
    bool dirty = false;

    VsEditNode* find(int id) { for (auto& n : nodes) if (n.id == id) return &n; return nullptr; }
    int add(const std::string& type, float x, float y) {
        VsEditNode n; n.id = nextId++; n.type = type; n.x = x; n.y = y;
        const VsDef* d = FindDef(type);
        if (d && d->type == std::string("Compare")) n.arg = ">";
        else if (d && d->type == std::string("Timer")) n.arg = "1";
        else if (d && d->type == std::string("Const")) n.arg = "0";
        nodes.push_back(n); dirty = true; return n.id;
    }
    void removeNode(int id) {
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                    [&](const VsEditNode& n){ return n.id == id; }), nodes.end());
        execLinks.erase(std::remove_if(execLinks.begin(), execLinks.end(),
                    [&](const VsExecLink& l){ return l.from == id || l.to == id; }), execLinks.end());
        dataLinks.erase(std::remove_if(dataLinks.begin(), dataLinks.end(),
                    [&](const VsDataLink& l){ return l.consumer == id || l.producer == id; }), dataLinks.end());
        dirty = true;
    }
    void linkExec(int from, int pin, int to) {
        execLinks.erase(std::remove_if(execLinks.begin(), execLinks.end(),
                    [&](const VsExecLink& l){ return l.from == from && l.pin == pin; }), execLinks.end());
        execLinks.push_back({from, pin, to}); dirty = true;
    }
    void linkData(int consumer, int inPin, int producer, int outPin) {
        dataLinks.erase(std::remove_if(dataLinks.begin(), dataLinks.end(),
                    [&](const VsDataLink& l){ return l.consumer == consumer && l.inPin == inPin; }), dataLinks.end());
        dataLinks.push_back({consumer, inPin, producer, outPin}); dirty = true;
    }

    std::string toText() const {
        std::ostringstream o;
        o << "# OkayVS graph (edited in the Visual Script editor)\n";
        for (const auto& n : nodes) {
            o << "node " << n.id << ' ' << n.type;
            if (!n.arg.empty()) o << ' ' << n.arg;
            o << "\n";
            o << "pos " << n.id << ' ' << (int)n.x << ' ' << (int)n.y << "\n";
            if (n.type == "OnStart")  o << "entry OnStart "  << n.id << "\n";
            if (n.type == "OnUpdate") o << "entry OnUpdate " << n.id << "\n";
        }
        for (const auto& l : execLinks) o << "exec " << l.from << ' ' << l.pin << ' ' << l.to << "\n";
        for (const auto& l : dataLinks) o << "data " << l.consumer << ' ' << l.inPin << ' ' << l.producer << ' ' << l.outPin << "\n";
        return o.str();
    }
    bool save(const std::string& p) {
        std::ofstream f(p, std::ios::binary);
        if (!f) return false;
        f << toText();
        path = p; dirty = false; return true;
    }
    void clear() { nodes.clear(); execLinks.clear(); dataLinks.clear(); nextId = 1; path.clear(); dirty = false; }
    bool load(const std::string& p) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        clear(); path = p;
        std::string line;
        int maxId = 0;
        while (std::getline(f, line)) {
            if (auto h = line.find('#'); h != std::string::npos) line.erase(h);
            std::istringstream ls(line); std::string kind; ls >> kind;
            if (kind == "node") {
                VsEditNode n; ls >> n.id >> n.type; std::string a; if (ls >> a) n.arg = a;
                nodes.push_back(n); maxId = std::max(maxId, n.id);
            } else if (kind == "pos") {
                int id; float x, y; ls >> id >> x >> y;
                if (auto* n = find(id)) { n->x = x; n->y = y; }
            } else if (kind == "exec") {
                VsExecLink l; ls >> l.from >> l.pin >> l.to; execLinks.push_back(l);
            } else if (kind == "data") {
                VsDataLink l; ls >> l.consumer >> l.inPin >> l.producer >> l.outPin; dataLinks.push_back(l);
            }
        }
        nextId = maxId + 1; dirty = false;
        return true;
    }
};

// ---- Interaction state -----------------------------------------------------
struct VsEditState {
    VsEditGraph g;
    ImVec2 pan{40, 40};
    int    selected = -1;
    // Pending wire drag: a pin the user grabbed.
    bool   dragging = false;
    bool   dragExec = false;   // true = exec pin, false = data pin
    bool   dragFromOutput = false;
    int    dragNode = -1, dragPin = -1;
    bool   openPalette = false;
    ImVec2 paletteWorld{0, 0};
};

inline VsEditState& State() { static VsEditState s; return s; }

// Optional log sink (the host wires this to its console); no hard dependency on
// the host's logging so this header can be included at global scope.
inline void (*LogFn)(const std::string&, int) = nullptr;
inline void Log(const std::string& m, int lv = 0) { if (LogFn) LogFn(m, lv); }

// Load a .okayvs into the editor's live graph (used to open one on double-click).
inline void OpenFile(const std::string& path) { State().g.load(path); State().selected = -1; }

// Pin geometry helpers (screen positions computed during draw).
struct PinKey { int node; int kind; int side; int index; };  // kind:0 exec,1 data; side:0 in,1 out

// ---- The editor window -----------------------------------------------------
// Returns nothing; manages its own state. `ed` is used for "Attach to selected".
inline void Draw(EditorState& ed, bool* open) {
    if (!ImGui::Begin("Visual Script", open)) { ImGui::End(); return; }
    VsEditState& st = State();
    VsEditGraph& g = st.g;

    // Toolbar.
    if (ImGui::Button("New")) { g.clear(); st.selected = -1; }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string p = g.path;
        if (p.empty()) {
            // default into the project Assets folder.
            std::string dir = ed.projectDir();
            p = (dir.empty() ? std::string("Script") : dir + "/Assets/Script") + ".okayvs";
        }
        if (g.save(p)) Log("Saved visual script " + p);
        else Log("Save failed: " + p, 2);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!ed.selected());
    if (ImGui::Button("Attach to Selected")) {
        if (GameObject* o = ed.selected()) {
            if (g.path.empty()) {
                std::string dir = ed.projectDir();
                std::string p = (dir.empty() ? std::string("Script") : dir + "/Assets/" + o->name + "_vs") + ".okayvs";
                g.save(p);
            }
            auto* vc = o->GetComponent<VisualScriptComponent>();
            if (!vc) vc = o->AddComponent<VisualScriptComponent>();
            vc->LoadFromFile(g.path);
            ed.dirty = true;
            Log("Attached visual script to " + o->name);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", g.path.empty() ? "(unsaved)" : g.path.c_str(), g.dirty ? " *" : "");
    ImGui::SameLine();
    ImGui::TextDisabled("  |  right-click: add node  ·  drag pins to wire  ·  Del: delete");
    // Lightweight validation: a graph with nodes but no event entry never runs.
    {
        bool hasEvent = false;
        for (const auto& n : g.nodes) if (n.type == "OnStart" || n.type == "OnUpdate") { hasEvent = true; break; }
        if (!g.nodes.empty() && !hasEvent) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.40f, 1.0f), "  \xE2\x9A\xA0 add an On Start / On Update node");
        }
    }

    // Canvas.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.105f, 0.105f, 0.112f, 1.0f));
    ImGui::BeginChild("vscanvas", ImVec2(0, 0), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 canvasMin = origin, canvasMax = ImVec2(origin.x + ImGui::GetContentRegionAvail().x,
                                                  origin.y + ImGui::GetContentRegionAvail().y);
    ImGui::InvisibleButton("vscbg", ImVec2(canvasMax.x - canvasMin.x, canvasMax.y - canvasMin.y),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool canvasHovered = ImGui::IsItemHovered();

    // Grid.
    const float grid = 26.0f;
    ImU32 gridc = IM_COL32(255, 255, 255, 10);
    for (float x = std::fmod(st.pan.x, grid); x < canvasMax.x - canvasMin.x; x += grid)
        dl->AddLine(ImVec2(canvasMin.x + x, canvasMin.y), ImVec2(canvasMin.x + x, canvasMax.y), gridc);
    for (float y = std::fmod(st.pan.y, grid); y < canvasMax.y - canvasMin.y; y += grid)
        dl->AddLine(ImVec2(canvasMin.x, canvasMin.y + y), ImVec2(canvasMax.x, canvasMin.y + y), gridc);

    // Pan with middle or right drag on empty canvas.
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        st.pan.x += ImGui::GetIO().MouseDelta.x; st.pan.y += ImGui::GetIO().MouseDelta.y;
    }

    auto W2S = [&](float wx, float wy) { return ImVec2(canvasMin.x + st.pan.x + wx, canvasMin.y + st.pan.y + wy); };

    // Compute pin screen positions this frame.
    std::map<long long, ImVec2> pinPos;
    auto pk = [](int node, int kind, int side, int idx) -> long long {
        return ((long long)node << 20) | ((long long)kind << 18) | ((long long)side << 16) | idx;
    };

    const float NODE_W = 172.0f, HEADER_H = 24.0f, ROW_H = 20.0f, PIN_R = 5.0f;

    // Lay out + draw each node.
    for (auto& n : g.nodes) {
        const VsDef* d = FindDef(n.type);
        if (!d) continue;
        int leftRows  = (d->execIn ? 1 : 0) + (int)d->dataIn.size();
        int rightRows = (int)d->execOut.size() + (int)d->dataOut.size();
        int rows = std::max(leftRows, rightRows);
        float bodyH = rows * ROW_H + (d->param ? ROW_H + 4 : 0) + 8;
        float h = HEADER_H + bodyH;
        ImVec2 p0 = W2S(n.x, n.y), p1 = ImVec2(p0.x + NODE_W, p0.y + h);

        // Body + header.
        dl->AddRectFilled(p0, p1, IM_COL32(34, 34, 38, 240), 6.0f);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + HEADER_H), CatColor(d->cat), 6.0f, ImDrawFlags_RoundCornersTop);
        if (st.selected == n.id) dl->AddRect(p0, p1, IM_COL32(210, 210, 220, 255), 6.0f, 0, 2.0f);
        else                     dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 160), 6.0f);
        dl->AddText(ImVec2(p0.x + 9, p0.y + 4), IM_COL32(236, 238, 244, 255), d->title);

        // Node drag handle: an invisible button over the header.
        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(n.id);
        ImGui::InvisibleButton("##hdr", ImVec2(NODE_W, HEADER_H));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            n.x += ImGui::GetIO().MouseDelta.x; n.y += ImGui::GetIO().MouseDelta.y; g.dirty = true;
        }
        if (ImGui::IsItemClicked()) st.selected = n.id;
        if (ImGui::BeginPopupContextItem("##nctx")) {
            if (ImGui::MenuItem("Delete")) { g.removeNode(n.id); ImGui::EndPopup(); ImGui::PopID(); continue; }
            ImGui::EndPopup();
        }

        float ly = p0.y + HEADER_H + 6;
        // Left pins (exec-in then data-in).
        int li = 0;
        if (d->execIn) {
            ImVec2 pp(p0.x, ly + li * ROW_H + ROW_H * 0.5f);
            dl->AddTriangleFilled(ImVec2(pp.x - 5, pp.y - 5), ImVec2(pp.x - 5, pp.y + 5), ImVec2(pp.x + 4, pp.y),
                                  IM_COL32(220, 220, 225, 255));
            pinPos[pk(n.id, 0, 0, 0)] = pp;
            ++li;
        }
        for (int i = 0; i < (int)d->dataIn.size(); ++i, ++li) {
            ImVec2 pp(p0.x, ly + li * ROW_H + ROW_H * 0.5f);
            dl->AddCircleFilled(pp, PIN_R, IM_COL32(150, 190, 150, 255), 12);
            dl->AddText(ImVec2(p0.x + 10, pp.y - 8), IM_COL32(190, 194, 200, 255), d->dataIn[i]);
            pinPos[pk(n.id, 1, 0, i)] = pp;
        }
        // Right pins (exec-out then data-out).
        int ri = 0;
        for (int i = 0; i < (int)d->execOut.size(); ++i, ++ri) {
            ImVec2 pp(p1.x, ly + ri * ROW_H + ROW_H * 0.5f);
            dl->AddTriangleFilled(ImVec2(pp.x - 4, pp.y - 5), ImVec2(pp.x - 4, pp.y + 5), ImVec2(pp.x + 5, pp.y),
                                  IM_COL32(220, 220, 225, 255));
            if (d->execOut[i][0]) {
                ImVec2 ts = ImGui::CalcTextSize(d->execOut[i]);
                dl->AddText(ImVec2(p1.x - 12 - ts.x, pp.y - 8), IM_COL32(190, 194, 200, 255), d->execOut[i]);
            }
            pinPos[pk(n.id, 0, 1, i)] = pp;
        }
        for (int i = 0; i < (int)d->dataOut.size(); ++i, ++ri) {
            ImVec2 pp(p1.x, ly + ri * ROW_H + ROW_H * 0.5f);
            dl->AddCircleFilled(pp, PIN_R, IM_COL32(150, 190, 150, 255), 12);
            ImVec2 ts = ImGui::CalcTextSize(d->dataOut[i]);
            dl->AddText(ImVec2(p1.x - 12 - ts.x, pp.y - 8), IM_COL32(190, 194, 200, 255), d->dataOut[i]);
            pinPos[pk(n.id, 1, 1, i)] = pp;
        }

        // Param editor (inline) in the body.
        if (d->param) {
            float py = p0.y + HEADER_H + std::max(leftRows, rightRows) * ROW_H + 6;
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, py));
            ImGui::SetNextItemWidth(NODE_W - 16);
            char buf[96]; std::snprintf(buf, sizeof(buf), "%s", n.arg.c_str());
            if (d->param == 2) {   // compare op combo
                const char* ops[] = {">", "<", ">=", "<=", "==", "!="};
                if (ImGui::BeginCombo("##op", n.arg.c_str())) {
                    for (const char* o : ops) if (ImGui::Selectable(o, n.arg == o)) { n.arg = o; g.dirty = true; }
                    ImGui::EndCombo();
                }
            } else {
                if (ImGui::InputTextWithHint("##arg", d->paramHint ? d->paramHint : "", buf, sizeof(buf)))
                    { n.arg = buf; g.dirty = true; }
            }
        }

        // Start a wire from a pin: a small invisible button over each pin.
        auto pinButton = [&](long long key, int kind, int side, int idx) {
            auto it = pinPos.find(key); if (it == pinPos.end()) return;
            ImVec2 pp = it->second;
            ImGui::SetCursorScreenPos(ImVec2(pp.x - 7, pp.y - 7));
            char id[24]; std::snprintf(id, sizeof(id), "p%d%d%d", kind, side, idx);
            ImGui::InvisibleButton(id, ImVec2(14, 14));
            if (ImGui::IsItemActivated()) {
                st.dragging = true; st.dragExec = (kind == 0);
                st.dragFromOutput = (side == 1); st.dragNode = n.id; st.dragPin = idx;
            }
            if (ImGui::IsItemHovered())
                dl->AddCircle(pp, PIN_R + 2, IM_COL32(255, 255, 255, 200), 14, 1.5f);
        };
        if (d->execIn) pinButton(pk(n.id, 0, 0, 0), 0, 0, 0);
        for (int i = 0; i < (int)d->dataIn.size(); ++i)  pinButton(pk(n.id, 1, 0, i), 1, 0, i);
        for (int i = 0; i < (int)d->execOut.size(); ++i) pinButton(pk(n.id, 0, 1, i), 0, 1, i);
        for (int i = 0; i < (int)d->dataOut.size(); ++i) pinButton(pk(n.id, 1, 1, i), 1, 1, i);

        ImGui::PopID();
    }

    // Draw existing wires (behind nodes would be nicer, but on top is fine/clear).
    auto bezier = [&](ImVec2 a, ImVec2 b, ImU32 col) {
        float dx = std::max(40.0f, std::fabs(b.x - a.x) * 0.5f);
        dl->AddBezierCubic(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, col, 2.4f);
    };
    for (const auto& l : g.execLinks) {
        auto a = pinPos.find(pk(l.from, 0, 1, l.pin)), b = pinPos.find(pk(l.to, 0, 0, 0));
        if (a != pinPos.end() && b != pinPos.end()) bezier(a->second, b->second, IM_COL32(230, 230, 235, 220));
    }
    for (const auto& l : g.dataLinks) {
        auto a = pinPos.find(pk(l.producer, 1, 1, l.outPin)), b = pinPos.find(pk(l.consumer, 1, 0, l.inPin));
        if (a != pinPos.end() && b != pinPos.end()) bezier(a->second, b->second, IM_COL32(150, 200, 150, 220));
    }

    // Live drag wire + release handling.
    if (st.dragging) {
        auto from = pinPos.find(pk(st.dragNode, st.dragExec ? 0 : 1, st.dragFromOutput ? 1 : 0, st.dragPin));
        if (from != pinPos.end()) {
            ImVec2 m = ImGui::GetIO().MousePos;
            bezier(st.dragFromOutput ? from->second : m, st.dragFromOutput ? m : from->second,
                   IM_COL32(255, 255, 255, 150));
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            // Find a pin under the mouse of the opposite side + same kind.
            ImVec2 m = ImGui::GetIO().MousePos;
            int bestNode = -1, bestIdx = -1; float bestD = 14 * 14;
            int wantSide = st.dragFromOutput ? 0 : 1;   // connect to an input if we came from an output
            int wantKind = st.dragExec ? 0 : 1;
            for (auto& kvp : pinPos) {
                int node = (int)(kvp.first >> 20);
                int kind = (int)((kvp.first >> 18) & 3);
                int side = (int)((kvp.first >> 16) & 3);
                int idx  = (int)(kvp.first & 0xFFFF);
                if (kind != wantKind || side != wantSide || node == st.dragNode) continue;
                float dx = kvp.second.x - m.x, dy = kvp.second.y - m.y;
                float dd = dx * dx + dy * dy;
                if (dd < bestD) { bestD = dd; bestNode = node; bestIdx = idx; }
            }
            if (bestNode >= 0) {
                if (st.dragExec) {
                    if (st.dragFromOutput) g.linkExec(st.dragNode, st.dragPin, bestNode);
                    else                   g.linkExec(bestNode, bestIdx, st.dragNode);
                } else {
                    if (st.dragFromOutput) g.linkData(bestNode, bestIdx, st.dragNode, st.dragPin);
                    else                   g.linkData(st.dragNode, st.dragPin, bestNode, bestIdx);
                }
            }
            st.dragging = false;
        }
    }

    // Right-click empty canvas: node palette.
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !st.dragging) {
        st.openPalette = true;
        st.paletteWorld = ImVec2(ImGui::GetIO().MousePos.x - canvasMin.x - st.pan.x,
                                 ImGui::GetIO().MousePos.y - canvasMin.y - st.pan.y);
        ImGui::OpenPopup("##vspalette");
    }
    if (ImGui::BeginPopup("##vspalette")) {
        ImGui::TextDisabled("Add node");
        ImGui::Separator();
        static const char* catNames[CAT_COUNT] = {"Events","Flow","Variables","Math","Logic","Input","Transform","Actions","Data"};
        for (int c = 0; c < CAT_COUNT; ++c) {
            bool any = false;
            for (const auto& d : Registry()) if (d.cat == c) { any = true; break; }
            if (!any) continue;
            if (ImGui::BeginMenu(catNames[c])) {
                for (const auto& d : Registry())
                    if (d.cat == c && ImGui::MenuItem(d.title))
                        st.selected = g.add(d.type, st.paletteWorld.x, st.paletteWorld.y);
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }

    // Delete selected node with the Del key when the canvas is focused.
    if (st.selected >= 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        g.removeNode(st.selected); st.selected = -1;
    }

    if (g.nodes.empty())
        dl->AddText(ImVec2(canvasMin.x + 18, canvasMin.y + 16), IM_COL32(150, 152, 160, 255),
                    "Right-click to add a node. Start with an On Start or On Update event.");

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

} // namespace okay::editor::vsed
