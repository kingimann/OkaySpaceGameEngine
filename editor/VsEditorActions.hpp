#pragma once
// What the Visual Script editor does besides drawing, for the Visual Script app (shared/VisualScriptApp: the program and the editor's
// Visual Script window): the person's remembered choices (the start cards, the hints done, the help card), putting a recipe or a
// template on the canvas, attaching the graph to an object, the outline's rows (events, functions, breakpoints, the values a graph
// uses, function inputs), a block's readiness and the component it is missing, Switch cases, a function's signature, a Quick block's
// presets, the live run's flashes, and the choice lists for a block's setting. Moved out of the editor's old Dear ImGui window
// (VisualScriptEditor.hpp, VisualScriptStartDraw.hpp, VisualScriptGuideDraw.hpp) when that window was replaced by the app; no
// drawing here, so the tests run it. Included at the end of VisualScriptModel.hpp. Written for this project.
#include "VisualScriptStart.hpp"
#include "VisualScriptUsagePrefs.hpp"
#include "okay/VisualScript/VsDecl.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace okay::editor::vsed {

// ---- what is remembered about the person ------------------------------------------------------------------------------------

/// Loaded once from okay_vs_usage.txt in the per-user folder, saved when it changes (VsPrefsSave).
inline VsUsagePrefs& VsPrefs() { static VsUsagePrefs p = VsUsagePrefsLoad(okay::edtabs::UserFile("okay_vs_usage.txt")); return p; }
inline void VsPrefsSave() { VsUsagePrefsSave(VsPrefs(), okay::edtabs::UserFile("okay_vs_usage.txt")); }

// ---- recipes and templates --------------------------------------------------------------------------------------------------

/// Put recipe `index` on the canvas, wired, selected and framed; with an editor and a selected object, that object gets the parts the
/// recipe needs. One undo step (the canvas records a graph change once).
inline void VsStartInsertRecipe(EditorState* ed, VsEditState& st, int index) {
    const auto& all = VsRecipes();
    if (index < 0 || index >= (int)all.size()) return;
    const VsRecipe& r = all[(std::size_t)index];
    const std::set<int> made = VsInsertRecipe(st.g, r.graph);
    st.sel = made;
    st.selected = made.empty() ? -1 : *made.begin();
    st.frameSel = !made.empty();
    if (ed) if (GameObject* go = ed->selected()) VsAddTemplateComponents(go, r.add);
}

/// A "Next steps" chip of the help card: `step.type` added beside `fromId` and wired the way the suggestion says. The new id (or -1).
inline int VsAddNextStep(VsEditGraph& g, int fromId, const VsNextStep& step) {
    const VsEditNode* from = g.find(fromId);
    if (!from) return -1;
    int below = 0;
    for (const VsExecLink& l : g.execLinks) if (l.from == fromId) ++below;
    for (const VsDataLink& l : g.dataLinks) if (l.producer == fromId) ++below;
    const int id = g.add(step.type, from->x + kVsEstWidth + kVsGapX, from->y + (float)below * 120.0f);
    if (step.exec) g.linkExec(fromId, step.outPin, id);
    else g.linkData(id, step.inPin, fromId, step.outPin);
    return id;
}

/// The "More recipes" of Pick a Recipe: small graphs that are not in VsRecipes() (no budget test of their own).
struct VsExtraRecipe { const char* name; const char* text; };
inline const std::vector<VsExtraRecipe>& VsExtraRecipes() {
    static const std::vector<VsExtraRecipe> recs = {
        {"Collect coins and score",
         "node 1 OnStart\npos 1 40 40\nentry OnStart 1\nnode 2 ScoreAdd\npos 2 280 40\ndefault 2 0 1\nexec 1 0 2\n"
         "node 3 OnTagEnter coin\npos 3 40 220\nentry OnTagEnter:coin 3\nnode 4 Collect\npos 4 300 220\ndefault 4 0 1\nexec 3 0 4\n"},
        {"Enemy chases and damages",
         "node 1 OnUpdate\npos 1 40 40\nentry OnUpdate 1\nnode 2 FindWithTag Player\npos 2 40 200\nnode 3 Chase\npos 3 300 40\n"
         "exec 1 0 3\ndata 3 0 2 0\nnode 4 OnTagEnter Player\npos 4 40 380\nentry OnTagEnter:Player 4\n"
         "node 5 Damage\npos 5 320 380\ndefault 5 0 10\nexec 4 0 5\n"},
        {"Door with a key",
         "node 1 OnStart\npos 1 40 40\nentry OnStart 1\nnode 2 Const true\npos 2 40 200\nnode 3 SetVar has_key\npos 3 280 40\nexec 1 0 3\ndata 3 0 2 0\n"
         "node 4 OnInteract\npos 4 40 400\nentry OnInteract 4\nnode 5 GetVar has_key\npos 5 40 560\nnode 6 Branch\npos 6 280 400\n"
         "exec 4 0 6\ndata 6 0 5 0\nnode 7 DoorOpen\npos 7 540 400\ndefault 7 0 90\ndefault 7 1 0.35\nexec 6 0 7\n"},
        {"Health pickup",
         "node 1 OnTagEnter Player\npos 1 40 40\nentry OnTagEnter:Player 1\nnode 2 Heal\npos 2 300 40\ndefault 2 0 25\nexec 1 0 2\n"
         "node 3 Destroy\npos 3 540 40\nexec 2 0 3\n"},
        {"Countdown then win",
         "node 1 OnStart\npos 1 40 40\nentry OnStart 1\nnode 2 Countdown 0.4\npos 2 300 40\nexec 1 0 2\nnode 3 Win\npos 3 560 40\nexec 2 0 3\n"},
    };
    return recs;
}

/// Graph text (a recipe, a "Try this node" graph) put on the canvas with its top-left at (x, y) in stored units: selected and framed.
inline std::set<int> VsPasteTemplate(VsEditState& st, const std::string& text, float x, float y) {
    std::set<int> made = VsPasteText(st.g, text, x, y);
    st.sel = made;
    st.selected = made.empty() ? -1 : *made.begin();
    st.frameSel = !made.empty();
    return made;
}

/// The templates whose name, sentence or group has `query` in it (any case), at the level `level` (a VsLevelFilter), in the
/// order they are listed.
inline std::vector<const VsTemplate*> VsTemplateMatches(const std::string& query, int level) {
    std::string needle = query;
    for (char& c : needle) c = (char)std::tolower((unsigned char)c);
    std::vector<const VsTemplate*> out;
    for (const VsTemplate& t : VsTemplates()) {
        if (!VsLevelPasses(t.level, (VsLevelFilter)level)) continue;
        std::string hay = std::string(t.name) + " " + t.blurb + " " + t.category;
        for (char& c : hay) c = (char)std::tolower((unsigned char)c);
        if (!needle.empty() && hay.find(needle) == std::string::npos) continue;
        out.push_back(&t);
    }
    return out;
}

/// The tab becomes template `t` (not saved to a file yet: the file that was open is not written over). With an editor and a selected
/// object, that object runs the template (an embedded graph) and gets the parts it needs.
inline void VsApplyTemplate(EditorState* ed, VsEditState& st, const VsTemplate& t) {
    VsEditGraph& g = st.g;
    g.fromText(t.graph);
    g.path.clear();
    ResetEditState(st);
    st.boundGuids.clear();
    st.frameAll = true;
    g.dirty = true;
    st.publishedText = g.toText();
    if (!ed) return;
    if (GameObject* go = ed->selected()) {
        auto* vs = go->GetComponent<VisualScriptComponent>();
        if (!vs) vs = go->AddComponent<VisualScriptComponent>();
        std::string err;
        vs->LoadFromText(g.toText(), &err);
        vs->SetPath("");
        BindObject(go);
        VsAddTemplateComponents(go, t.add);
        ed->dirty = true;
    }
}

/// Attach to Selected Object: the graph on screen is saved first (attaching loads the file, so an unsaved edit would otherwise not be
/// what runs), then `o` runs it. False with `why` when it could not be saved.
inline bool VsAttachToObject(EditorState& ed, GameObject* o, std::string& why) {
    if (!o) { why = "no object is selected"; return false; }
    BindObject(o);
    if (!PublishOpenGraph(ed, false)) { why = "the graph could not be saved"; return false; }
    VsEditGraph& g = State().g;
    auto* vc = o->GetComponent<VisualScriptComponent>();
    if (!vc) vc = o->AddComponent<VisualScriptComponent>();
    if (vc->Path() != g.path) vc->LoadFromFile(g.path);
    ed.dirty = true;
    return true;
}

/// File > Restore Previous Version: an earlier save's text on screen (not saved; Undo takes it back).
inline void VsRestoreVersion(VsEditState& st, const std::string& text) {
    st.undo.push_back(st.g.toText());
    st.redo.clear();
    st.g.fromText(text);
    VsRefreshDirty(st);
    st.lastText = st.g.toText();
}

/// The object a graph file is attached to: the selected object when it runs `path`, else the first object that does. Null: none.
inline GameObject* VsGraphOwner(EditorState& ed, const std::string& path) {
    if (path.empty()) return nullptr;
    GameObject* owner = nullptr;
    for (const auto& up : ed.scene().Objects()) {
        if (!up) continue;
        for (auto* vc : up->GetComponents<VisualScriptComponent>())
            if (VsSamePath(vc->Path(), path)) {
                if (up.get() == ed.selected()) return up.get();
                if (!owner) owner = up.get();
            }
    }
    return owner;
}

// ---- the outline ------------------------------------------------------------------------------------------------------------

/// One row of the outline: the block it jumps to, what it says, and what its tip says.
struct VsOutlineRow { int node = -1; std::string label, tip; };
/// Everything the outline lists for a graph: its events (with how many steps follow each), its functions (with how often each is
/// called), its breakpoints, the saved values its blocks use (by scope) and the inputs of its functions.
struct VsOutline {
    std::vector<VsOutlineRow> events, functions, breakpoints;
    std::vector<VsVarRef> values;
    std::vector<VsParamRef> inputs;
};
inline VsOutline VsBuildOutline(const VsEditGraph& g) {
    VsOutline o;
    for (const auto& n : g.nodes) {
        if (VsEntryName(n.type, n.arg).empty()) continue;
        const VsDef* d = FindDef(n.type);
        std::string lbl = d ? d->title : n.type;
        if (!n.arg.empty() && n.type != "OnStart" && n.type != "OnUpdate") lbl += ": " + (IsKeyNode(n.type) ? VsKeyLabel(n.arg) : n.arg);
        int steps = 0;   // how many steps hang off it (the first flow plug, followed)
        for (int cur = VsExecTarget(g, n.id, 0); cur >= 0 && steps < 99; cur = VsExecTarget(g, cur, 0)) ++steps;
        char tip[160];
        std::snprintf(tip, sizeof tip, kPanelEventTip, steps);
        o.events.push_back({n.id, lbl, tip});
    }
    for (const auto& n : g.nodes) {
        if (n.type != "FunctionStart") continue;
        const std::string nm = okay::vs::FunctionNameOf(n.arg);
        int calls = 0;
        for (const auto& m : g.nodes) if (m.type == "CallFunction" && okay::vs::FunctionNameOf(m.arg) == nm) ++calls;
        o.functions.push_back({n.id, nm.empty() ? std::string("(unnamed)") : nm,
                               n.arg + "\nCalled from " + std::to_string(calls) + " place(s). Click to jump there."});
    }
    if (!g.path.empty())
        for (const auto& bp : okay::vs::VsDebugger::Get().Breakpoints()) {
            if (bp.first != g.path) continue;
            const VsEditNode* bn = g.find(bp.second);
            const VsDef* bd = bn ? FindDef(bn->type) : nullptr;
            o.breakpoints.push_back({bp.second, std::string(bd ? bd->title : bn ? bn->type.c_str() : kPanelStopGone) + " #" + std::to_string(bp.second),
                                     kPanelStopTip});
        }
    std::vector<VsVarNode> refs;
    refs.reserve(g.nodes.size());
    for (const auto& n : g.nodes) {
        refs.push_back({n.type, n.arg, n.id});
        if (const okay::vs::QuickBlock* qb = okay::vs::FindQuickBlock(n.type)) {   // a Quick block's Name field is a saved value too
            const auto fields = okay::vs::QbAllFields(*qb);
            for (std::size_t fi = 0; fi < fields.size(); ++fi) {
                if (fields[fi].kind != okay::vs::QbKind::Name) continue;
                const auto it = n.pinDefault.find((int)fi);
                refs.push_back({"GetVar", okay::vs::QbNameBare(it != n.pinDefault.end() && !it->second.empty() ? it->second : std::string(fields[fi].def)), n.id});
            }
        }
    }
    o.values = VsCollectVariables(refs);
    o.inputs = VsCollectParameters(refs);
    return o;
}

// ---- a block's state ----------------------------------------------------------------------------------------------------------

/// True when block `n`'s `pin` input has a wire or a typed value.
inline bool VsPinFilled(const VsEditGraph& g, const VsEditNode& n, int pin) {
    if (VsInputWired(g, n.id, pin)) return true;
    const auto it = n.pinDefault.find(pin);
    return it != n.pinDefault.end() && !it->second.empty();
}
/// The badge on a block's header (Ready, Needs input, Error) and, for a block that needs an input, what to plug in (a sentence).
inline VsReady VsBlockReadiness(const VsEditGraph& g, const std::vector<okay::vs::VsIssue>& issues, const VsEditNode& n, const VsDef& d,
                                std::string& missing) {
    missing.clear();
    bool hasErr = false;
    for (const auto& is : issues) if (is.node == n.id && is.severity == 0) hasErr = true;
    VsReady ready = VsNodeReadiness(n.type, !n.arg.empty(), [&](int pin) { return VsPinFilled(g, n, pin); }, hasErr, &missing);
    if (ready != VsReady::Error)   // an empty input the block cannot work without: said on the block, not only in a tip
        for (int pi = 0; pi < (int)d.dataIn.size(); ++pi)
            if (VsPinNeedsInput(d, pi) && !VsPinFilled(g, n, pi)) {
                ready = VsReady::Missing;
                if (missing.empty() || missing.rfind("Required: ", 0) == 0) {
                    missing = VsMissingHint(d, pi) + ".";
                    missing[0] = (char)std::toupper((unsigned char)missing[0]);
                }
                break;
            }
    return ready;
}
/// True when block `n` has its Target input wired or typed (it may then act on another object: its own object is not warned).
inline bool VsTargetSet(const VsEditGraph& g, const VsEditNode& n, const VsDef& d) {
    for (int ti = 0; ti < (int)d.dataIn.size(); ++ti)
        if (d.dataIn[(std::size_t)ti] && std::strcmp(d.dataIn[(std::size_t)ti], "Target") == 0 && VsPinFilled(g, n, ti)) return true;
    return false;
}
/// A breakpoint can go on a step (a block the flow runs through), an event or a function; a value block runs when a step reads it.
inline bool VsCanBreakAt(const VsEditNode& n) {
    const VsDef* d = FindDef(n.type);
    return d && (d->execIn || !VsEntryName(n.type, n.arg).empty() || n.type == "FunctionStart");
}

// ---- editing a block's setting ------------------------------------------------------------------------------------------------

/// Switch cases typed as text ("a, b, c"): the Default plug stays the last one.
inline void VsSetSwitchCases(VsEditGraph& g, VsEditNode& n, const std::string& text) {
    VsMoveSwitchDefault(g, n.id, (int)VsEditorSwitchCases(n.arg).size(), (int)VsEditorSwitchCases(text).size());
    n.arg = text;
    g.dirty = true;
}
/// "+ case": one more case (the next number for Switch on Number, "caseN" for Switch on Text). False at 63 cases.
inline bool VsAddSwitchCase(VsEditGraph& g, VsEditNode& n) {
    std::vector<std::string> cases = VsEditorSwitchCases(n.arg);
    if ((int)cases.size() >= 63) return false;
    const int oldN = (int)cases.size();
    if (n.type == "SwitchCases") {
        int next = oldN;
        if (!cases.empty()) {
            char* end = nullptr;
            const long v = std::strtol(cases.back().c_str(), &end, 10);
            if (end && *end == '\0' && v > -1000000000L && v < 1000000000L) next = (int)v + 1;
        }
        cases.push_back(std::to_string(next));
    } else {
        cases.push_back("case" + std::to_string(oldN + 1));
    }
    std::string joined;
    for (const auto& c : cases) { if (!joined.empty()) joined += ", "; joined += c; }
    VsMoveSwitchDefault(g, n.id, oldN, oldN + 1);
    n.arg = joined;
    g.dirty = true;
    return true;
}
/// A Function block's signature typed: renaming the function renames the Call Function and Return blocks that use it.
inline void VsSetFunctionSignature(VsEditGraph& g, VsEditNode& n, const std::string& sig) {
    const std::string was = okay::vs::FunctionNameOf(n.arg), now = okay::vs::FunctionNameOf(sig);
    if (!was.empty() && !now.empty() && was != now)
        for (auto& m : g.nodes)
            if ((m.type == "CallFunction" || m.type == "Return") && okay::vs::FunctionNameOf(m.arg) == was) m.arg = now;
    n.arg = sig;
    g.dirty = true;
}
/// The names of the graph's functions (what a Call Function block can pick).
inline std::vector<std::string> VsFunctionNames(const VsEditGraph& g) {
    std::vector<std::string> out;
    for (const auto& m : g.nodes)
        if (m.type == "FunctionStart") {
            const std::string nm = okay::vs::FunctionNameOf(m.arg);
            if (!nm.empty() && std::find(out.begin(), out.end(), nm) == out.end()) out.push_back(nm);
        }
    return out;
}
/// A Quick block's preset `choice` applied: every field it names gets the preset's value.
inline bool VsApplyQuickPreset(VsEditGraph& g, VsEditNode& n, const okay::vs::QuickBlock& qb, const std::string& choice) {
    std::vector<std::string> values;
    for (std::size_t k = 0; k < qb.fields.size(); ++k) {
        const auto it = n.pinDefault.find((int)k);
        values.push_back(it != n.pinDefault.end() ? it->second : std::string(qb.fields[k].def));
    }
    if (!okay::vs::QbApplyPreset(qb, choice, values)) return false;
    for (std::size_t k = 0; k < values.size(); ++k) n.pinDefault[(int)k] = values[k];
    g.dirty = true;
    return true;
}
/// A colour field's "#rrggbb" as 0..1 parts (white when it is not a colour).
inline void VsColourParts(const std::string& hex, float rgb[3]) {
    rgb[0] = rgb[1] = rgb[2] = 1.0f;
    if (!okay::vs::QbValidate(okay::vs::QbKind::Colour, hex)) return;
    for (int k = 0; k < 3; ++k) rgb[k] = (float)std::strtol(hex.substr(1 + (std::size_t)k * 2, 2).c_str(), nullptr, 16) / 255.0f;
}
inline std::string VsColourHex(const float rgb[3]) {
    char b[16];
    auto part = [](float v) { return (int)(std::min(1.0f, std::max(0.0f, v)) * 255.0f + 0.5f); };
    std::snprintf(b, sizeof b, "#%02x%02x%02x", part(rgb[0]), part(rgb[1]), part(rgb[2]));
    return b;
}
/// Every key a key field can hold, by its runtime name: the named keys, then a..z and 0..9.
inline std::vector<std::string> VsAllKeyNames() {
    std::vector<std::string> out;
    for (const auto& k : VsNamedKeys()) out.push_back(k.name);
    for (char c = 'a'; c <= 'z'; ++c) out.push_back(std::string(1, c));
    for (char c = '0'; c <= '9'; ++c) out.push_back(std::string(1, c));
    return out;
}

/// A file or an object dropped on a slot that wants `kind`: what the slot stores (true), or why not in `why` (false). `objectRef` is
/// the dropped object's reference (VsGuidRef or its name) when an object was dropped, else empty; `file` the dropped file's path.
inline bool VsDropOnSlot(okay::vs::VsRefKind kind, const std::string& objectRef, const std::string& file, const std::string& projectDir,
                         std::string& value, std::string& why) {
    using okay::vs::VsRefKind;
    if (!objectRef.empty()) {
        if (kind != VsRefKind::Object) { why = "That is an object. This slot wants a file from the Project window."; return false; }
        value = objectRef;
        return true;
    }
    if (file.empty()) return false;
    if (kind == VsRefKind::Object) { why = "That is a file. Drag an object from the Hierarchy or Scene view."; return false; }
    if (!okay::vs::VsExtOk(kind, file)) { why = std::string("Wrong file. This slot wants ") + okay::vs::VsRefKindName(kind) + "."; return false; }
    value = okay::vs::VsAssetRef(kind, VsProjectRelative(file, projectDir));
    return true;
}
/// A file dropped on a choice list (clips, sounds, scenes): the name it picks, or false when the file is not that kind.
inline bool VsDropOnChoice(VsChoiceKind kind, const std::string& file, std::string& value) {
    const bool clip = kind == VsChoiceKind::Clip && okay::vs::VsExtOk(okay::vs::VsRefKind::Animation, file);
    const bool sound = kind == VsChoiceKind::Sound && okay::vs::VsExtOk(okay::vs::VsRefKind::Audio, file);
    const bool scene = kind == VsChoiceKind::Scene && okay::vs::VsExtOk(okay::vs::VsRefKind::Scene, file);
    if (!clip && !sound && !scene) return false;
    value = std::filesystem::u8path(file).stem().u8string();
    return true;
}
/// The files under `project`/Assets a slot of `kind` can hold, project-relative, at most `limit`.
inline std::vector<std::string> VsSlotFiles(okay::vs::VsRefKind kind, const std::string& project, std::size_t limit = 400) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    const fs::path assets = project.empty() ? fs::path("Assets") : fs::u8path(project) / "Assets";
    std::error_code ec;
    if (!fs::exists(assets, ec)) return out;
    for (auto it = fs::recursive_directory_iterator(assets, ec); it != fs::recursive_directory_iterator() && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string full = it->path().u8string();
        if (!okay::vs::VsExtOk(kind, full)) continue;
        out.push_back(VsProjectRelative(full, project));
        if (out.size() >= limit) break;
    }
    return out;
}

// ---- saved values (moved from VsVariablePanel.hpp: the Inspector and the app read them) -------------------------------------------

/// An old `var` row (or a typed one): its default as a value of its type.
inline okay::vs::VsValue VsExposedValue(const std::string& type, const std::string& value, bool typed,
                                        const std::vector<okay::vs::StructDef>& structs, okay::vs::VarType& vt) {
    using namespace okay::vs;
    if (!ParseVarType(type, structs, vt)) vt = VarType{};
    VsValue v;
    if (typed) {
        if (VsParseTyped(vt, value, structs, v)) return v;
        return VsDefaultFor(vt, structs);
    }
    if (vt.base == PinType::Number) { float f = 0; VsParseFinite(value, f); return VsValue(f); }
    if (vt.base == PinType::Bool) return VsValue(value == "true");
    return VsValue(value);
}

inline const std::vector<std::string>& VsVarTypeChoices() {
    static const std::vector<std::string> c = {
        "Number", "Bool", "Text", "Vector3", "Color", "Object", "List", "Map",
        "List<Number>", "List<Text>", "List<Bool>", "List<Vector3>", "List<Color>", "List<Object>",
        "Map<Number>", "Map<Text>", "Map<Bool>", "Map<Vector3>", "Map<Color>", "Map<Object>"};
    return c;
}

/// A graph's text with variable `name` (in `scope`) given the starting value `literal`. An old
/// `var` line becomes a typed `decl` line with the type its value had. Everything else in the
/// text is kept byte for byte. False when no such declaration is in `text`.
inline bool VsSetDeclDefaultInText(const std::string& text, const std::string& name, okay::vs::VarScope scope,
                                   const std::string& literal, const std::string& typeOfLegacy, std::string& out) {
    std::string result;
    bool done = false;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        const bool last = nl == std::string::npos;
        const std::string line = text.substr(pos, last ? std::string::npos : nl - pos);
        std::string repl = line;
        if (!done) {
            const std::vector<std::string> t = okay::vs::TokenizeLine(line);
            auto unq = [](std::string x) { if (x.size() >= 2 && x.front() == '"' && x.back() == '"') x = x.substr(1, x.size() - 2); return x; };
            if (t.size() >= 4 && t[0] == "decl" && unq(t[1]) == name) {
                okay::vs::VarScope sc;
                if (okay::vs::VarScopeFromName(unq(t[3]), sc) && sc == scope) {
                    repl = "decl " + name + " " + okay::vs::QuoteIfNeeded(unq(t[2])) + " " + okay::vs::VarScopeName(sc) + " " + okay::vs::QuoteText(literal);
                    done = true;
                }
            } else if (t.size() >= 2 && t[0] == "var" && unq(t[1]) == name && scope == okay::vs::VarScope::Object) {
                repl = "decl " + name + " " + typeOfLegacy + " object " + okay::vs::QuoteText(literal);
                done = true;
            }
        }
        result += repl;
        if (last) break;
        result += '\n';
        pos = nl + 1;
    }
    if (done) out = result;
    return done;
}

// ---- the live run -------------------------------------------------------------------------------------------------------------

/// The flashes of the blocks that ran: each fades over about 0.4 s (`dt` seconds have passed); a block whose run count went up since
/// the last call flashes again (a Quick block counts the runs of all the nodes it is made of).
inline void VsUpdateFlashes(VsEditState& st, const okay::vs::NodeGraph* live, float dt) {
    for (auto& kv : st.flash) kv.second = std::max(0.0f, kv.second - dt * 2.5f);
    if (!live) return;
    for (const auto& n : st.g.nodes) {
        const int rid = live->NodeByFileId(n.id);
        unsigned ran = 0;
        if (rid >= 0) { if (const okay::vs::VsNode* ln = live->Node(rid)) ran = ln->execCount; }
        else if (okay::vs::FindQuickBlock(n.type)) ran = live->BlockExecCount(n.id);
        else continue;
        unsigned& seen = st.seenExec[n.id];
        if (ran != seen) { st.flash[n.id] = 1.0f; seen = ran; }
    }
}
/// "Moved 3 steps right": the line under a block that ran, from the values that went into it last.
inline std::string VsBlockSaid(const VsEditGraph& g, const okay::vs::NodeGraph* live, const VsEditNode& n, const VsDef& d) {
    const okay::vs::VsNode* ln = VsLiveNode(live, n.id);
    if (!ln || ln->execCount == 0) return std::string();
    std::vector<std::string> shown;
    for (int pi = 0; pi < (int)d.dataIn.size(); ++pi) {
        std::string val;
        for (const auto& l : g.dataLinks)
            if (l.consumer == n.id && l.inPin == pi)
                if (const okay::vs::VsNode* pn = VsLiveNode(live, l.producer))
                    if (l.outPin < (int)pn->lastOut.size()) val = VsDebugText(pn->lastOut[(std::size_t)l.outPin]);
        if (val.empty()) { const auto it = n.pinDefault.find(pi); if (it != n.pinDefault.end()) val = it->second; }
        shown.push_back(val);
    }
    return VsWhatHappened(n.type, n.arg, shown, ln->execCount);
}

}  // namespace okay::editor::vsed
