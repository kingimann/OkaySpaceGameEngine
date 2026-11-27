// Visual Script app (VisualScriptApp.hpp): the Details pane's fields for the selected block, each with the widget its kind calls for:
// what the block does and needs (with Add Component for a missing part, in the editor), its badge, its setting (a key you press, an
// object or a file in a slot that takes drops from the editor's Hierarchy and Project panels, a list of clips / sounds / scenes /
// tags, a compare sign, a function to call, a function's signature, Switch cases with "+ case", the Action steps of an Action, a
// Check or an Action Steps block), and the values typed into its unwired inputs (a Quick block's number you drag, its key, object,
// choice, colour swatch, name and presets). With no block selected: the graph's saved values with their names, scopes, types and
// start values (numbers, vectors, colours, lists, maps, bundles), and the bundles. The rules are the model's (VisualScriptModel.hpp,
// VsEditorActions.hpp, okay/VisualScript/QuickBlocks.hpp); this file draws the widgets and calls them.
//
// Written from general inspector behaviour (typed fields, drop slots, dropdowns) and the OkayUI interface; no code, names, icons or
// look from any other editor (rule 19a).
#include "VisualScriptAppImpl.hpp"

#include "okay/VisualScript/VsDecl.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vsapp {

namespace {
// The payloads a slot takes: a file dragged from the editor's Project panel (its path) and an object from the Hierarchy (what a slot
// stores for it: its @guid: reference or its name). The editor hands its own drags to OkayUI under these names
// (editor/VisualScriptAppHost.cpp).
constexpr const char* kDropFile = "ASSET_PATH";
constexpr const char* kDropObject = "OBJECT_REF";

std::string PayloadText(const UI::DragPayload* p) {
    if (!p || p->size <= 0) return std::string();
    return std::string((const char*)p->data, (std::size_t)p->size).c_str();   // up to the first 0
}
// The key pressed this frame, as the runtime names it ("" when none).
std::string CapturedKey() {
    for (int i = 0; i < 26; ++i) if (UI::IsKeyPressed((UI::KeyId)(UI::KeyId_A + i), false)) return std::string(1, (char)('a' + i));
    for (int i = 0; i < 10; ++i)
        if (UI::IsKeyPressed((UI::KeyId)(UI::KeyId_0 + i), false) || UI::IsKeyPressed((UI::KeyId)(UI::KeyId_Keypad0 + i), false))
            return std::string(1, (char)('0' + i));
    static const std::pair<UI::KeyId, const char*> named[] = {
        {UI::KeyId_Space, "space"}, {UI::KeyId_Enter, "enter"}, {UI::KeyId_Tab, "tab"}, {UI::KeyId_Backspace, "backspace"},
        {UI::KeyId_Shift, "shift"}, {UI::KeyId_RightShift, "shift"}, {UI::KeyId_Ctrl, "ctrl"}, {UI::KeyId_RightCtrl, "ctrl"},
        {UI::KeyId_Up, "up"}, {UI::KeyId_Down, "down"}, {UI::KeyId_Left, "left"}, {UI::KeyId_Right, "right"}};
    for (const auto& k : named) if (UI::IsKeyPressed(k.first, false)) return k.second;
    return std::string();
}
using okay::edtabs::Lower;   // the editor's one lower-case helper (EditorTabs.hpp)
}  // namespace

// ---- the fields: a pressed button, an object or a file, a choice ----

bool VisualScriptApp::Impl::keyField(const std::string& id, std::string& value, float width) {
    bool changed = false;
    const bool waiting = keyWait == id;
    UI::SetNextItemWidth(width);
    if (UI::Button(((waiting ? std::string("Press a key...") : "Key: " + vsed::VsKeyLabel(value)) + "##" + id).c_str(), UI::Vec2(width, 0))) {
        keyWait = waiting ? std::string() : id;
        if (!waiting) UI::OpenPopup(("##keys" + id).c_str());
    }
    markField(id);
    if (UI::IsItemHovered()) UI::SetTooltip("Click, then press the key you want (or pick it from the list).");
    if (waiting) {
        const std::string got = CapturedKey();
        if (UI::IsKeyPressed(UI::KeyId_Escape, false)) keyWait.clear();
        else if (!got.empty()) { value = got; changed = true; keyWait.clear(); }
    }
    if (UI::BeginPopup(("##keys" + id).c_str())) {
        if (keyWait != id) UI::CloseCurrentPopup();   // a key was pressed (or the wait ended): the list goes
        UI::TextDisabled("Press any key, or pick one (Esc closes)");
        for (const auto& k : vsed::VsNamedKeys())
            if (UI::Selectable(k.label, value == k.name)) { value = k.name; changed = true; keyWait.clear(); UI::CloseCurrentPopup(); }
        auto grid = [&](char from, char to) {
            int col = 0;
            for (char c = from; c <= to; ++c) {
                if (col++ % 9) UI::SameLine();
                const char shown[2] = {(char)std::toupper((unsigned char)c), 0};
                if (UI::SmallButton((std::string(shown) + "##k" + id).c_str())) {
                    value = std::string(1, c); changed = true; keyWait.clear(); UI::CloseCurrentPopup();
                }
            }
        };
        grid('a', 'z');
        grid('0', '9');
        UI::EndPopup();
    } else if (waiting && !UI::IsPopupOpen(("##keys" + id).c_str()) && UI::IsMouseClicked(0)) {
        keyWait.clear();   // clicked away
    }
    if (changed) vsed::VsQuickContext().lastKey = value;
    return changed;
}

bool VisualScriptApp::Impl::refSlot(const std::string& id, std::string& value, okay::vs::VsRefKind kind, bool defaultSelf, float width) {
    using okay::vs::VsRefKind;
    bool changed = false;
    okay::editor::EditorState* e = ed();
    std::string label;
    if (e) label = vsed::VsRefLabel(value, kind, defaultSelf, e->scene());
    else label = value.empty() ? (kind == VsRefKind::Object && defaultSelf ? std::string("Self") : std::string("None")) : value;
    const float xw = 24.0f;
    if (UI::Button((std::string(vsed::VsRefIcon(kind)) + " " + label + "##slot" + id).c_str(), UI::Vec2(std::max(40.0f, width - xw - 4.0f), 0)))
        UI::OpenPopup(("##pick" + id).c_str());
    markField(id);
    if (UI::IsItemHovered())
        UI::SetTooltip("%s", kind == VsRefKind::Object ? (e ? "Drag an object from the Hierarchy, or click to pick. Self is this object."
                                                            : "Click to pick Self or Other, or type an object's name in the list.")
                                                       : "Drag a file from the Project window, or click to pick.");
    if (UI::BeginDragDropTarget()) {   // the editor's Hierarchy and Project panels drop here
        const UI::DragPayload* obj = UI::AcceptDragDropPayload(kDropObject);
        const UI::DragPayload* file = obj ? nullptr : UI::AcceptDragDropPayload(kDropFile);
        if (obj || file) {
            std::string why;
            if (vsed::VsDropOnSlot(kind, PayloadText(obj), PayloadText(file), project(), value, why)) { changed = true; st().refHint.clear(); }
            else { st().refHint = why; note(why, 1); }
        }
        UI::EndDragDropTarget();
    }
    UI::SameLine();
    if (UI::Button(("x##clr" + id).c_str(), UI::Vec2(xw, 0))) { value.clear(); changed = true; }
    if (UI::IsItemHovered()) UI::SetTooltip("%s", defaultSelf ? "Clear (back to Self)" : "Clear");
    if (UI::BeginPopup(("##pick" + id).c_str())) {
        static char q[64] = "";
        UI::SetNextItemWidth(220);
        UI::InputTextWithHint(("##q" + id).c_str(), "Search", q, (int)sizeof q);
        const std::string needle = Lower(q);
        auto row = [&](const std::string& name, const std::string& stored) {
            if (!needle.empty() && Lower(name).find(needle) == std::string::npos) return;
            if (UI::Selectable((name + "##r" + id).c_str())) { value = stored; changed = true; UI::CloseCurrentPopup(); }
        };
        if (kind == VsRefKind::Object) {
            row("Self", "@self");
            row("Other", "@other");
            if (e) for (const auto& up : e->scene().Objects()) if (up) row(up->name, okay::vs::VsGuidRef(up->Guid()));
            else if (q[0]) row(std::string("Find \"") + q + "\" by name", q);
        } else {
            const std::vector<std::string> files = vsed::VsSlotFiles(kind, project());
            for (const std::string& rel : files) row(rel, okay::vs::VsAssetRef(kind, rel));
            if (files.empty()) UI::TextDisabled("Nothing of that kind in Assets.");
        }
        UI::EndPopup();
    }
    return changed;
}

bool VisualScriptApp::Impl::choiceField(const std::string& id, std::string& value, vsed::VsChoiceKind kind, float width) {
    // gathered once a frame per kind: a graph with many dropdowns of one kind does not walk the lists for each of them
    struct Gathered { vsed::VsChoiceLists lists; std::vector<std::string> rows; };
    static int frameSeen = -1;
    static std::map<int, Gathered> byKind;
    if (frameSeen != UI::GetFrameCount()) { frameSeen = UI::GetFrameCount(); byKind.clear(); }
    auto found = byKind.find((int)kind);
    if (found == byKind.end()) {
        Gathered gd;
        okay::editor::EditorState* e = ed();
        gd.lists = vsed::VsGatherChoices(e ? &e->scene() : nullptr, e ? e->selected() : nullptr, project(), st().g, kind);
        gd.rows = vsed::VsChoiceRows(kind, gd.lists);
        found = byKind.emplace((int)kind, std::move(gd)).first;
    }
    const vsed::VsChoiceLists& lists = found->second.lists;
    const std::vector<std::string>& rows = found->second.rows;
    const bool missing = vsed::VsChoiceMissing(value, rows);
    bool changed = false;
    const std::string preview = value.empty() ? std::string("(choose a ") + vsed::VsChoiceKindName(kind) + ")" : value;
    UI::SetNextItemWidth(width);
    if (UI::BeginCombo(("##choice" + id).c_str(), preview.c_str())) {
        auto section = [&](const char* title, const std::vector<std::string>& items) {
            if (items.empty()) return;
            UI::TextDisabled(title);
            for (const auto& c : items) if (UI::Selectable((c + "##c" + id).c_str(), c == value)) { value = c; changed = true; UI::CloseCurrentPopup(); }
        };
        if (kind == vsed::VsChoiceKind::Clip && !lists.onTarget.empty()) { section("On this object", lists.onTarget); section("Other clips", lists.fallback); }
        else section(kind == vsed::VsChoiceKind::Clip ? "Clips" : "Choices", rows);
        if (rows.empty()) UI::TextDisabled("Nothing to pick yet. You can still drop a file.");
        UI::EndCombo();
    }
    markField(id);
    if (UI::IsItemHovered()) {
        if (missing) UI::SetTooltip("\"%s\" is not in the list any more. Pick another, or drop a file.", value.c_str());
        else UI::SetTooltip("Pick a %s, or drop one from the Project window.", vsed::VsChoiceKindName(kind));
    }
    if (UI::BeginDragDropTarget()) {
        if (const UI::DragPayload* p = UI::AcceptDragDropPayload(kDropFile)) {
            if (vsed::VsDropOnChoice(kind, PayloadText(p), value)) changed = true;
            else { st().refHint = std::string("That file is not a ") + vsed::VsChoiceKindName(kind) + "."; note(st().refHint, 1); }
        }
        UI::EndDragDropTarget();
    }
    if (missing) UI::TextColored(255, 140, 115, "Not in the list");
    return changed;
}

// ---- a Quick block's field -------------------------------------------------------------------------------------------------------

bool VisualScriptApp::Impl::quickField(vsed::VsEditNode& n, const okay::vs::QuickBlock& qb, int i, std::string& value, float width) {
    using okay::vs::QbKind;
    const std::vector<okay::vs::QbField> fields = okay::vs::QbAllFields(qb);
    if (i < 0 || i >= (int)fields.size()) return false;
    const okay::vs::QbField& f = fields[(std::size_t)i];
    const std::string id = std::to_string(n.id) + "_" + std::to_string(i);
    const std::string cur = value.empty() ? std::string(f.def) : value;
    bool changed = false;
    UI::SetNextItemWidth(width);
    switch (f.kind) {
        case QbKind::Number: {   // drag it, or click to type
            float v = (float)std::atof(cur.c_str());
            if (UI::DragFloat(("##qn" + id).c_str(), &v, 0.1f)) { char b[32]; std::snprintf(b, sizeof b, "%g", v); value = b; changed = true; }
            markField("q" + id);
            if (UI::IsItemHovered()) UI::SetTooltip("%s: drag, or click to type a number", f.name);
            return changed;
        }
        case QbKind::Key: {
            std::string k = cur;
            if (keyField("qk" + id, k, width)) { value = k; changed = true; }
            return changed;
        }
        case QbKind::Object: {   // this object, or one of the scene's (the editor)
            const bool open = UI::BeginCombo(("##qo" + id).c_str(), value.empty() ? "This object" : value.c_str());
            if (!open) markField("q" + id);
            if (open) {
                if (UI::Selectable("This object", value.empty())) { value.clear(); changed = true; UI::CloseCurrentPopup(); }
                if (okay::editor::EditorState* e = ed())
                    for (const auto& up : e->scene().Objects())
                        if (up && UI::Selectable((up->name + "##qo" + id).c_str(), value == up->name)) { value = up->name; changed = true; UI::CloseCurrentPopup(); }
                UI::EndCombo();
            }
            if (UI::BeginDragDropTarget()) {
                if (const UI::DragPayload* p = UI::AcceptDragDropPayload(kDropObject)) {   // an object from the Hierarchy: its name
                    const std::string ref = PayloadText(p);
                    okay::editor::EditorState* e = ed();
                    value = e ? vsed::VsRefLabel(ref, okay::vs::VsRefKind::Object, false, e->scene()) : ref;
                    changed = true;
                }
                UI::EndDragDropTarget();
            }
            return changed;
        }
        case QbKind::Choice: {
            std::string label = cur;
            const auto choices = okay::vs::QbChoices(f.choices);
            for (const auto& c : choices) if (c.second == cur) label = c.first;
            const bool open = UI::BeginCombo(("##qc" + id).c_str(), label.c_str());
            if (!open) markField("q" + id);
            if (open) {
                for (const auto& c : choices)
                    if (UI::Selectable((c.first + "##qc" + id).c_str(), c.second == cur)) { value = c.second; changed = true; UI::CloseCurrentPopup(); }
                UI::EndCombo();
            }
            return changed;
        }
        case QbKind::Colour: {   // a swatch that opens the colour picker
            float rgb[3];
            vsed::VsColourParts(cur, rgb);
            if (UI::ColorEdit3(("##qcol" + id).c_str(), rgb)) { value = vsed::VsColourHex(rgb); changed = true; }
            markField("q" + id);
            return changed;
        }
        case QbKind::Name: {   // type a name, or pick one the graph already uses; who can see it: this object or everyone
            const bool shared = okay::vs::QbNameShared(cur);
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s", okay::vs::QbNameBare(cur).c_str());
            UI::SetNextItemWidth(std::max(40.0f, width - 76.0f));
            if (UI::InputTextWithHint(("##qname" + id).c_str(), "name, like score", buf, (int)sizeof buf)) {
                value = okay::vs::QbNameWithScope(okay::vs::QbNormalizeName(buf), shared); changed = true;
            }
            UI::SameLine();
            if (UI::SmallButton(("v##qnl" + id).c_str())) UI::OpenPopup(("##qnames" + id).c_str());
            if (UI::IsItemHovered()) UI::SetTooltip("Pick a saved value this script already uses");
            UI::SameLine();
            if (UI::SmallButton(((shared ? std::string("All") : std::string("Me")) + "##qns" + id).c_str())) {
                value = okay::vs::QbNameWithScope(buf, !shared); changed = true;
            }
            if (UI::IsItemHovered()) UI::SetTooltip("%s", shared ? "Everyone can see this. Click for only this object." : "Only this object. Click for everyone can see this.");
            if (UI::BeginPopup(("##qnames" + id).c_str())) {
                const std::vector<std::string> used = vsed::VsUsedValueNames(st().g);
                if (used.empty()) UI::TextDisabled("No saved values yet. Type a name.");
                for (const std::string& u : used)
                    if (UI::Selectable((u + "##qu" + id).c_str())) { value = okay::vs::QbNameWithScope(u, shared); changed = true; UI::CloseCurrentPopup(); }
                UI::EndPopup();
            }
            return changed;
        }
        case QbKind::Preset: {   // one pick fills several fields
            const bool open = UI::BeginCombo(("##qp" + id).c_str(), "Choose a preset");
            if (!open) markField("q" + id);
            if (open) {
                for (const auto& c : okay::vs::QbChoices(f.choices))
                    if (UI::Selectable((c.first + "##qp" + id).c_str())) {
                        vsed::VsApplyQuickPreset(st().g, n, qb, c.second);
                        value.clear(); changed = true; UI::CloseCurrentPopup();
                    }
                UI::EndCombo();
            }
            return changed;
        }
        default: return false;
    }
}

// ---- the steps of an Action, a Check and an Action Steps block ---------------------------------------------------------------------

namespace {
// One step: which step (from the editor's step list; a typed name in the program) and its values. 0, or 1 remove / 2 up / 3 down.
int StepRow(okay::ActionList::Item& it, bool cond, const std::string& id, float width, bool list, bool& changed) {
    int act = 0;
    const std::vector<okay::editor::vsed::VsStepInfo>* cat = okay::editor::vsed::StepCatalog ? okay::editor::vsed::StepCatalog(cond) : nullptr;
    const okay::editor::vsed::VsStepInfo* info = nullptr;
    if (cat) for (const auto& si : *cat) if (si.op == it.op) info = &si;
    if (cat) {
        UI::SetNextItemWidth(width);
        if (UI::BeginCombo(("##op" + id).c_str(), info ? info->label.c_str() : it.op.c_str())) {
            static char q[48] = "";
            UI::SetNextItemWidth(width - 10);
            UI::InputTextWithHint(("##opq" + id).c_str(), cond ? "Search checks..." : "Search actions...", q, (int)sizeof q);
            const std::string needle = Lower(q);
            std::string group;
            for (const auto& si : *cat) {
                if (si.listOnly && !list) continue;   // jumps, Wait and block openers only make sense in a list of steps
                if (!needle.empty() && Lower(si.label + " " + si.op + " " + si.desc).find(needle) == std::string::npos) continue;
                if (needle.empty() && si.group != group) { group = si.group; UI::TextDisabled(group.c_str()); }
                if (UI::Selectable((si.label + "##o" + id + si.op).c_str(), si.op == it.op)) { it.op = si.op; changed = true; UI::CloseCurrentPopup(); }
                if (UI::IsItemHovered() && !si.desc.empty()) UI::SetTooltip("%s", si.desc.c_str());
            }
            UI::EndCombo();
        }
        if (info && UI::IsItemHovered() && !info->desc.empty()) UI::SetTooltip("%s", info->desc.c_str());
    } else {   // the program has no step list: the step's name as text
        char ob[64];
        std::snprintf(ob, sizeof ob, "%s", it.op.c_str());
        UI::SetNextItemWidth(width);
        if (UI::InputTextWithHint(("##opt" + id).c_str(), cond ? "check, like has_tag" : "step, like move", ob, (int)sizeof ob)) { it.op = ob; changed = true; }
    }
    // its values: one box each, as many as its hint names (and one more to add)
    std::vector<std::string> names;
    if (info && !info->hint.empty()) {
        std::string w;
        for (char c : info->hint + " ") { if (c == ' ') { if (!w.empty()) names.push_back(w); w.clear(); } else w += c; }
    }
    const std::size_t boxes = std::max(names.size(), it.args.size() + (names.empty() ? 1 : 0));
    for (std::size_t a = 0; a < boxes && a < 12; ++a) {
        char vb[256];
        std::snprintf(vb, sizeof vb, "%s", a < it.args.size() ? it.args[a].c_str() : "");
        UI::SetNextItemWidth(width);
        const std::string hint = a < names.size() ? names[a] : std::string("value");
        if (UI::InputTextWithHint(("##arg" + id + "_" + std::to_string(a)).c_str(), hint.c_str(), vb, (int)sizeof vb)) {
            while (it.args.size() <= a) it.args.push_back(std::string());
            it.args[a] = vb;
            while (!it.args.empty() && it.args.back().empty()) it.args.pop_back();
            changed = true;
        }
    }
    if (list) {
        if (UI::SmallButton(("Up##su" + id).c_str())) act = 2;
        UI::SameLine();
        if (UI::SmallButton(("Down##sd" + id).c_str())) act = 3;
        UI::SameLine();
        if (UI::SmallButton(("Remove##sr" + id).c_str())) act = 1;
    }
    return act;
}
}  // namespace

void VisualScriptApp::Impl::drawSteps(vsed::VsEditNode& n, const vsed::VsDef& d, float W) {
    vsed::VsEditGraph& g = st().g;
    const bool cond = d.param == 5;
    if (d.param == 4) {   // a list of steps, run top to bottom (Wait pauses)
        auto list = okay::ActionList::DecodeItems(n.arg);
        UI::TextColored(150, 215, 255, "Steps (run top to bottom; Wait pauses)");
        bool ch = false;
        int act = 0, actAt = -1;
        for (std::size_t i = 0; i < list.size(); ++i) {
            UI::TextDisabled((std::to_string(i + 1) + ".").c_str());
            const int a = StepRow(list[i], false, std::to_string(n.id) + "_" + std::to_string(i), W, true, ch);
            if (a) { act = a; actAt = (int)i; }
            UI::Spacing(4);
        }
        if (act == 1 && actAt >= 0) { list.erase(list.begin() + actAt); ch = true; }
        if (act == 2 && actAt > 0) { std::swap(list[(std::size_t)actAt], list[(std::size_t)actAt - 1]); ch = true; }
        if (act == 3 && actAt >= 0 && actAt + 1 < (int)list.size()) { std::swap(list[(std::size_t)actAt], list[(std::size_t)actAt + 1]); ch = true; }
        if (UI::Button(("+ Add step##sa" + std::to_string(n.id)).c_str())) { list.push_back({"log", {"Hello"}}); ch = true; }
        markField("addstep" + std::to_string(n.id));
        if (ch) { n.arg = okay::ActionList::EncodeItems(list); g.dirty = true; }
        return;
    }
    okay::ActionList::Item it = okay::ActionList::DecodeItem(n.arg);
    if (it.op.empty()) it.op = cond ? "always" : "log";
    UI::TextColored(150, 215, 255, cond ? "Check" : "Action");
    bool ch = false;
    StepRow(it, cond, std::to_string(n.id), W, false, ch);
    if (ch) { n.arg = okay::ActionList::EncodeItem(it); g.dirty = true; }
}

// ---- the selected block's fields ---------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::drawBlockFields(vsed::VsEditNode& n, const vsed::VsDef& d, float W, bool modal) {
    vsed::VsEditState& s = st();
    vsed::VsEditGraph& g = s.g;
    const std::string sid = std::to_string(n.id);
    UI::TextUnformatted(vsed::VsNodeTitle(n, d).c_str());
    UI::TextDisabled((std::string(n.type) + "  #" + sid).c_str());
    std::string miss;
    const vsed::VsReady ready = vsed::VsBlockReadiness(g, s.issues, n, d, miss);
    if (ready == vsed::VsReady::Error) UI::TextColored(232, 102, 92, "Error: see Problems on this block");
    else if (ready == vsed::VsReady::Missing) UI::TextColored(255, 196, 140, miss.empty() ? "Needs input" : miss.c_str());
    if (const vsed::VsNodeDoc* doc = vsed::VsNodeDocFor(n.type)) {
        UI::Spacing(4);
        UI::TextWrapped(doc->description.c_str());
        if (!doc->requirements.empty()) { UI::TextDisabled("Requires"); UI::TextWrapped(doc->requirements.c_str()); }
        if (okay::editor::EditorState* e = ed()) {   // the object that runs this graph lacks the part this block needs: one click adds it
            const vsed::VsReqGap gap = vsed::VsRequirementGap(vsed::VsGraphOwner(*e, g.path), doc, vsed::VsTargetSet(g, n, d));
            if (gap.missing && vsed::VsAddComponentById && UI::Button((gap.addLabel + "##vsapp_addc").c_str()) && !modal)
                vsed::VsAddComponentById(vsed::VsGraphOwner(*e, g.path), gap.addId.c_str());
        }
        if (!doc->example.empty()) { UI::TextDisabled("Example"); UI::TextWrapped(doc->example.c_str()); }
    } else if (const char* help = vsed::VsNodeHelp(n.type.c_str())) { UI::Spacing(4); UI::TextWrapped(help); }

    // the setting, with the widget its kind calls for
    if (d.param) {
        UI::Spacing(6);
        UI::TextUnformatted(d.paramHint && *d.paramHint ? d.paramHint : "Setting");
        const bool isStep = d.param >= 3 && d.param <= 5;
        UI::SetNextItemWidth(W);
        if (isStep) drawSteps(n, d, W);
        else if (d.param == 6) {   // one of this graph's functions
            const std::string cur = okay::vs::FunctionNameOf(n.arg);
            if (UI::BeginCombo(("##fn" + sid).c_str(), cur.empty() ? "(pick a function)" : cur.c_str())) {
                const std::vector<std::string> names = vsed::VsFunctionNames(g);
                if (names.empty()) UI::TextDisabled("No functions yet: add a Function block first");
                for (const std::string& nm : names)
                    if (UI::Selectable((nm + "##fn" + sid).c_str(), nm == cur)) { n.arg = nm; g.dirty = true; UI::CloseCurrentPopup(); }
                UI::EndCombo();
            }
        } else if (n.type == "FunctionStart") {   // the signature; renaming renames the calls
            char sb[256];
            std::snprintf(sb, sizeof sb, "%s", n.arg.c_str());
            if (UI::InputTextWithHint(("##sig" + sid).c_str(), d.paramHint ? d.paramHint : "", sb, (int)sizeof sb)) vsed::VsSetFunctionSignature(g, n, sb);
            if (UI::IsItemHovered()) UI::SetTooltip("Name(param: Type, ...) -> result: Type\nTypes: Number, Bool, Text, Vector3, Object, List, Any");
        } else if (d.param == 2) {   // a compare sign
            const char* ops[] = {">", "<", ">=", "<=", "==", "!="};
            if (UI::BeginCombo(("##op" + sid).c_str(), n.arg.c_str())) {
                for (const char* o : ops) if (UI::Selectable(o, n.arg == o)) { n.arg = o; g.dirty = true; UI::CloseCurrentPopup(); }
                UI::EndCombo();
            }
        } else if (vsed::IsKeyNode(n.type)) {
            if (keyField("key" + sid, n.arg, W)) g.dirty = true;
        } else if (n.type == "PlayAction") {
            if (UI::BeginCombo(("##act" + sid).c_str(), n.arg.empty() ? "(action)" : n.arg.c_str())) {
                for (const auto& nm : okay::HumanoidActions::ActionNames())
                    if (UI::Selectable(nm.c_str(), n.arg == nm)) { n.arg = nm; g.dirty = true; UI::CloseCurrentPopup(); }
                UI::EndCombo();
            }
        } else if (n.type == "Die") {
            const char* dirs[] = {"forward", "back", "left", "right"};
            int cur = 0;
            for (int i = 0; i < 4; ++i) if (n.arg == dirs[i]) cur = i;
            if (UI::Combo(("##die" + sid).c_str(), dirs, 4, &cur)) { n.arg = dirs[cur]; g.dirty = true; }
        } else if (n.type == "MouseButton" || n.type == "MouseButtonDown") {
            const char* btns[] = {"Left button", "Right button", "Middle button"};
            int b = n.arg == "right" ? 1 : n.arg == "middle" ? 2 : std::atoi(n.arg.c_str());
            if (b < 0 || b > 2) b = 0;
            if (UI::Combo(("##mb" + sid).c_str(), btns, 3, &b)) { n.arg = std::to_string(b); g.dirty = true; }
        } else if (vsed::IsSwitchNode(n.type)) {   // the cases as text, and one more case
            char sb[512];
            std::snprintf(sb, sizeof sb, "%s", n.arg.c_str());
            if (UI::InputTextWithHint(("##sw" + sid).c_str(), d.paramHint ? d.paramHint : "", sb, (int)sizeof sb)) vsed::VsSetSwitchCases(g, n, sb);
            if (UI::SmallButton(("+ case##swc" + sid).c_str())) vsed::VsAddSwitchCase(g, n);
            markField("case" + sid);
            if (UI::IsItemHovered()) UI::SetTooltip("Add another case. Default stays the last plug.");
        } else if (vsed::VsChoiceKind ck = vsed::VsArgChoice(n.type); ck != vsed::VsChoiceKind::None) {
            if (choiceField("arg" + sid, n.arg, ck, W)) g.dirty = true;
        } else if (okay::vs::VsRefKind ak = vsed::VsArgKind(n.type); ak != okay::vs::VsRefKind::None) {
            if (refSlot("arg" + sid, n.arg, ak, vsed::VsArgDefaultsSelf(n.type), W)) g.dirty = true;
        } else {
            char buf[512];
            std::snprintf(buf, sizeof buf, "%s", n.arg.c_str());
            if (UI::InputText(("##vsapp_arg" + sid).c_str(), buf, (int)sizeof buf) && !modal) { n.arg = buf; g.dirty = true; }
            markField("arg" + sid);
        }
    }

    // the values typed into unwired inputs, each with its kind's widget
    bool anyInput = false;
    const okay::vs::QuickBlock* qb = okay::vs::FindQuickBlock(n.type);
    const std::vector<okay::vs::QbField> qfields = qb ? okay::vs::QbAllFields(*qb) : std::vector<okay::vs::QbField>();
    for (int i = 0; i < (int)d.dataIn.size(); ++i) {
        if (vsed::VsInputWired(g, n.id, i)) continue;
        if (!anyInput) { UI::Spacing(6); UI::TextUnformatted("Values (inputs with no wire)"); anyInput = true; }
        const char* label = d.dataIn[(std::size_t)i] ? d.dataIn[(std::size_t)i] : "?";
        const bool needs = vsed::VsPinNeedsInput(d, i) && !vsed::VsPinFilled(g, n, i);
        if (needs) UI::TextColored(255, 140, 122, (std::string(label) + "  (needed)").c_str());
        else UI::TextDisabled(label);
        if (UI::IsItemHovered()) UI::SetTooltip("%s", vsed::VsPinTip(d, true, false, i).c_str());
        std::string value;
        if (const auto it = n.pinDefault.find(i); it != n.pinDefault.end()) value = it->second;
        const std::string before = value;
        const okay::vs::PinType pt = vsed::VsInPinType(g, n.id, i);
        const okay::vs::VsRefKind rk = vsed::VsPinKind(n.type, i, pt, label);
        const std::string pid = sid + "_" + std::to_string(i);
        bool changed = false;
        if (qb && i < (int)qfields.size() && qfields[(std::size_t)i].kind != okay::vs::QbKind::Text) changed = quickField(n, *qb, i, value, W);
        else if (rk != okay::vs::VsRefKind::None) changed = refSlot("pin" + pid, value, rk, okay::vs::TargetPinFor(n.type) == i, W);
        else {
            char buf[256];
            std::snprintf(buf, sizeof buf, "%s", value.c_str());
            UI::SetNextItemWidth(W);
            changed = UI::InputTextWithHint(("##vsapp_pin" + pid).c_str(), needs ? "needed" : "value", buf, (int)sizeof buf) && !modal;
            markField("pin" + pid);
            if (changed) value = buf;
        }
        if (changed && value != before) {
            // a preset filled other fields itself; the field's own value is put back only when it was the one edited
            if (n.pinDefault.count(i) || !value.empty() || !before.empty()) { if (value.empty()) n.pinDefault.erase(i); else n.pinDefault[i] = value; }
            g.dirty = true;
        }
    }
}

// ---- the saved values (no block selected) ----------------------------------------------------------------------------------------

namespace {
// Any value edited with the widget its type calls for; lists and maps show their items with remove buttons and an add button,
// bundles their parts. True when something changed.
bool EditValue(const std::string& label, const okay::vs::VarType& t, okay::vs::VsValue& v, const std::vector<okay::vs::StructDef>& structs,
               float width, int depth = 0) {
    using namespace okay::vs;
    if (depth > 6) { UI::TextDisabled("(nested too deep to edit here)"); return false; }
    bool changed = false;
    const std::string id = "##ev" + label + std::to_string(depth);
    if (t.IsStruct()) {
        const StructDef* sd = FindStruct(structs, t.structName);
        if (!sd) { UI::TextDisabled(("unknown bundle " + t.structName).c_str()); return false; }
        if (!v.IsMap()) { v = VsDefaultFor(t, structs); return true; }
        if (UI::TreeNodeEx((label + "  (" + t.structName + ")" + id).c_str(), UI::TreeNode_DefaultOpen)) {
            auto mp = v.AsMap();
            for (const auto& f : sd->fields) {
                auto it = mp->find(f.name);
                if (it == mp->end()) it = mp->emplace(f.name, VsDeepCopy(f.def)).first;
                UI::TextDisabled(f.name.c_str());
                changed |= EditValue(label + "." + f.name, f.type, it->second, structs, width - 16, depth + 1);
            }
            UI::TreePop();
        }
        return changed;
    }
    UI::SetNextItemWidth(width);
    switch (t.base) {
        case PinType::Number: { float f = v.AsFloat(); if (UI::DragFloat(id.c_str(), &f, 0.05f)) { v = VsValue(f); changed = true; } break; }
        case PinType::Bool: { bool b = v.AsBool(); if (UI::Checkbox(("true" + id).c_str(), &b)) { v = VsValue(b); changed = true; } break; }
        case PinType::Vec3: {
            const okay::Vec3 p = v.AsVec3();
            float a[3] = {p.x, p.y, p.z};
            if (UI::DragFloat3(id.c_str(), a, 0.05f)) { v = VsValue(okay::Vec3{a[0], a[1], a[2]}); changed = true; }
            break;
        }
        case PinType::Color: {
            const VsColor c = v.AsColor();
            float a[4] = {c.r, c.g, c.b, c.a};
            if (UI::ColorEdit4(id.c_str(), a)) { v = VsValue::Color(a[0], a[1], a[2], a[3]); changed = true; }
            break;
        }
        case PinType::Text:
        case PinType::Object: {
            char buf[512];
            std::snprintf(buf, sizeof buf, "%s", v.IsObject() ? "" : v.AsString().c_str());
            const bool obj = t.base == PinType::Object;
            if (UI::InputTextWithHint(id.c_str(), obj ? "object name or @guid:..." : "", buf, (int)sizeof buf)) {
                v = (obj && !buf[0]) ? VsValue::Object(0) : VsValue(std::string(buf));
                changed = true;
            }
            break;
        }
        case PinType::List:
        case PinType::Map: {
            const bool isList = t.base == PinType::List;
            if (isList && !v.IsArray()) { v = VsValue::MakeArray(); changed = true; }
            if (!isList && !v.IsMap()) { v = VsValue::MakeMap(); changed = true; }
            auto kindOf = [&](const VsValue& e) {
                VarType et; et.base = t.elem;
                if (et.base == PinType::Any)   // untyped: each item as what it is
                    et.base = e.IsNumber() ? PinType::Number : e.IsBool() ? PinType::Bool : e.IsVec3() ? PinType::Vec3 : e.IsColor() ? PinType::Color
                            : e.IsArray() ? PinType::List : e.IsMap() ? PinType::Map : PinType::Text;
                return et;
            };
            const std::size_t count = isList ? v.AsArray()->size() : v.AsMap()->size();
            const std::string head = label + "  (" + std::to_string(count) + (isList ? (count == 1 ? " item)" : " items)") : (count == 1 ? " entry)" : " entries)"));
            if (UI::TreeNodeEx((head + id).c_str(), UI::TreeNode_DefaultOpen)) {
                if (isList) {
                    auto a = v.AsArray();
                    int removeAt = -1;
                    for (std::size_t i = 0; i < a->size(); ++i) {
                        const std::string row = "[" + std::to_string(i) + "]";
                        UI::TextDisabled(row.c_str());
                        UI::SameLine();
                        if (UI::SmallButton(("x##rm" + label + row).c_str())) removeAt = (int)i;
                        changed |= EditValue(label + row, kindOf((*a)[i]), (*a)[i], structs, width - 16, depth + 1);
                    }
                    if (removeAt >= 0) { a->erase(a->begin() + removeAt); changed = true; }
                    if (UI::SmallButton(("+ Add item" + id).c_str())) {
                        VarType et; et.base = t.elem == PinType::Any ? PinType::Number : t.elem;
                        a->push_back(VsDefaultFor(et, structs)); changed = true;
                    }
                } else {
                    auto mp = v.AsMap();
                    std::string removeKey;
                    bool doRemove = false;
                    for (auto& kv : *mp) {
                        UI::TextDisabled(kv.first.c_str());
                        UI::SameLine();
                        if (UI::SmallButton(("x##rm" + label + kv.first).c_str())) { removeKey = kv.first; doRemove = true; }
                        changed |= EditValue(label + "." + kv.first, kindOf(kv.second), kv.second, structs, width - 16, depth + 1);
                    }
                    if (doRemove) { mp->erase(removeKey); changed = true; }
                    static char newKey[64] = "";
                    UI::SetNextItemWidth(std::max(60.0f, width * 0.5f));
                    UI::InputTextWithHint(("##nk" + id).c_str(), "new key", newKey, (int)sizeof newKey);
                    UI::SameLine();
                    if (UI::SmallButton(("+ Add entry" + id).c_str()) && newKey[0] && !mp->count(newKey)) {
                        VarType et; et.base = t.elem == PinType::Any ? PinType::Number : t.elem;
                        (*mp)[newKey] = VsDefaultFor(et, structs); newKey[0] = 0; changed = true;
                    }
                }
                UI::TreePop();
            }
            break;
        }
        default: UI::TextDisabled(VsDescribe(v).c_str()); break;
    }
    return changed;
}
// A type picker: the fixed types, then the graph's bundles.
bool TypeCombo(const std::string& id, std::string& type, const std::vector<std::string>& structNames, float width) {
    bool changed = false;
    UI::SetNextItemWidth(width);
    if (UI::BeginCombo(id.c_str(), type.c_str())) {
        for (const auto& t : okay::editor::vsed::VsVarTypeChoices())
            if (UI::Selectable((t + id).c_str(), type == t)) { type = t; changed = true; UI::CloseCurrentPopup(); }
        for (const auto& sn : structNames)
            if (UI::Selectable((sn + id).c_str(), type == sn)) { type = sn; changed = true; UI::CloseCurrentPopup(); }
        UI::EndCombo();
    }
    return changed;
}
}  // namespace

void VisualScriptApp::Impl::drawSavedValues(float W, bool modal) {
    using namespace okay::vs;
    vsed::VsEditGraph& g = st().g;
    static const char* kScopeNames[] = {"object", "scene", "global"};
    UI::Spacing(6);
    UI::TextUnformatted(vsed::kPanelSaved);
    UI::TextDisabled(vsed::kPanelSavedHelp);
    if (!g.declError().empty()) { char b[512]; std::snprintf(b, sizeof b, vsed::kPanelSavedBad, g.declError().c_str()); UI::TextColored(255, 115, 102, b); }
    std::vector<std::string> structNames;
    for (const auto& sd : g.structs) structNames.push_back(sd.name);
    int removeAt = -1;
    for (std::size_t i = 0; i < g.vars.size(); ++i) {
        auto& e = g.vars[i];
        const std::string id = "##sv" + std::to_string(i);
        UI::Separator();
        char nb[64];
        std::snprintf(nb, sizeof nb, "%s", e.name.c_str());
        UI::SetNextItemWidth(W - 30);
        if (UI::InputText(("##svname" + id).c_str(), nb, (int)sizeof nb, UI::InputText_EnterReturnsTrue) && !modal) {
            std::string err; declMsg = g.renameVar(i, nb, &err) ? "" : err;   // the blocks that use it follow
        }
        if (UI::IsItemHovered()) UI::SetTooltip("%s", vsed::kPanelNameTip);
        UI::SameLine();
        if (UI::SmallButton(("x" + id).c_str()) && !modal) removeAt = (int)i;
        if (UI::IsItemHovered()) UI::SetTooltip("%s", vsed::kPanelRemoveTip);
        int sc = e.scope == "scene" ? 1 : e.scope == "global" ? 2 : 0;
        UI::SetNextItemWidth(W);
        const char* scopes[3] = {vsed::kPanelScopes[0], vsed::kPanelScopes[1], vsed::kPanelScopes[2]};
        if (UI::Combo(("##svscope" + id).c_str(), scopes, 3, &sc) && !modal) { std::string err; declMsg = g.setVarScope(i, kScopeNames[sc], &err) ? "" : err; }
        std::string ty = e.type;
        if (TypeCombo("##svtype" + id, ty, structNames, W) && !modal) { std::string err; declMsg = g.setVarType(i, ty, &err) ? "" : err; }
        VarType vt;
        VsValue val = vsed::VsExposedValue(e.type, e.value, e.typed, g.structDefs(), vt);
        if (EditValue(e.name, vt, val, g.structDefs(), W) && !modal) { e.typed = true; e.value = VsToLiteral(val); g.declsChanged(); }
        if (vt.base == PinType::List || vt.base == PinType::Map || vt.IsStruct() || vt.base == PinType::Object) {   // the same start value as text
            char lb[1024];
            std::snprintf(lb, sizeof lb, "%s", e.typed ? e.value.c_str() : VsToLiteral(val).c_str());
            UI::SetNextItemWidth(W);
            if (UI::InputTextWithHint(("##svlit" + id).c_str(), vsed::kPanelStartHint, lb, (int)sizeof lb, UI::InputText_EnterReturnsTrue) && !modal) {
                std::string err; declMsg = g.setVarDefault(i, lb, &err) ? "" : "start value: " + err;
            }
            if (UI::IsItemHovered()) UI::SetTooltip("%s", vsed::kPanelStartTip);
        }
    }
    if (removeAt >= 0) g.removeVar((std::size_t)removeAt);
    UI::Separator();
    UI::TextUnformatted(vsed::kPanelNewSaved);
    UI::SetNextItemWidth(W);
    UI::InputTextWithHint("##vsapp_newvar", vsed::kPanelNameHint, declName, (int)sizeof declName);
    markField("newvar");
    UI::SetNextItemWidth(W);
    const char* scopes[3] = {vsed::kPanelScopes[0], vsed::kPanelScopes[1], vsed::kPanelScopes[2]};
    UI::Combo("##vsapp_newscope", scopes, 3, &declScope);
    TypeCombo("##vsapp_newtype", declType, structNames, W);
    const bool addVar = UI::Button((std::string(vsed::kPanelAddSaved) + "##vsapp_addvar").c_str());
    markField("addvar");
    if (addVar && !modal && declName[0]) {
        std::string err;
        if (g.declare(declName, declType, kScopeNames[std::clamp(declScope, 0, 2)], &err)) { note(std::string("Declared ") + declName); declName[0] = 0; declMsg.clear(); }
        else declMsg = err;
    }
    if (!declMsg.empty()) UI::TextColored(255, 140, 115, declMsg.c_str());

    // bundles: a few named values kept together
    UI::Spacing(6);
    if (UI::CollapsingHeader(vsed::kPanelBundles, !g.structs.empty())) {
        UI::TextDisabled(vsed::kPanelBundlesHelp);
        int removeStruct = -1;
        for (std::size_t si = 0; si < g.structs.size(); ++si) {
            auto& sd = g.structs[si];
            const std::string id = "##sb" + std::to_string(si);
            const std::string head = sd.name + "  (" + std::to_string(sd.fields.size()) + (sd.fields.size() == 1 ? " part)" : " parts)");
            if (UI::TreeNodeEx((head + id).c_str(), UI::TreeNode_DefaultOpen)) {
                int removeField = -1;
                for (std::size_t fi = 0; fi < sd.fields.size(); ++fi) {
                    auto& f = sd.fields[fi];
                    const std::string fid = id + "_" + std::to_string(fi);
                    UI::TextUnformatted(f.name.c_str());
                    UI::SameLine();
                    if (UI::SmallButton(("x" + fid).c_str())) removeField = (int)fi;
                    std::string fty = f.type;
                    std::vector<std::string> before;   // a bundle's part can only use the bundles listed above it
                    for (std::size_t k = 0; k < si; ++k) before.push_back(g.structs[k].name);
                    if (TypeCombo("##ft" + fid, fty, before, W)) {
                        std::vector<StructDef> names;
                        for (const auto& b : before) names.push_back({b, {}});
                        VarType probe;
                        std::string err;
                        if (ParseVarType(fty, names, probe, &err)) { f.type = VarTypeText(probe); f.value.clear(); g.declsChanged(); declMsg.clear(); }
                        else declMsg = err;
                    }
                    char fb[256];
                    std::snprintf(fb, sizeof fb, "%s", f.value.c_str());
                    UI::SetNextItemWidth(W);
                    if (UI::InputTextWithHint(("##fd" + fid).c_str(), vsed::kPanelPartStart, fb, (int)sizeof fb)) { f.value = fb; g.declsChanged(); }
                }
                if (removeField >= 0) g.removeStructField(si, (std::size_t)removeField);
                UI::SetNextItemWidth(W * 0.55f);
                UI::InputTextWithHint(("##nf" + id).c_str(), vsed::kPanelPartName, newField, (int)sizeof newField);
                UI::SameLine();
                if (UI::SmallButton((std::string(vsed::kPanelAddPart) + id).c_str())) {
                    std::string err;
                    if (g.addStructField(si, newField, "Number", &err)) { newField[0] = 0; declMsg.clear(); } else declMsg = err;
                }
                if (UI::SmallButton((std::string(vsed::kPanelRemoveBundle) + id).c_str())) removeStruct = (int)si;
                UI::TreePop();
            }
        }
        if (removeStruct >= 0) g.removeStruct((std::size_t)removeStruct);
        UI::SetNextItemWidth(W * 0.6f);
        UI::InputTextWithHint("##vsapp_nstruct", vsed::kPanelBundleName, newStruct, (int)sizeof newStruct);
        UI::SameLine();
        if (UI::SmallButton((std::string(vsed::kPanelAddBundle) + "##vsapp_addb").c_str())) {
            std::string err;
            if (g.declareStruct(newStruct, &err)) { newStruct[0] = 0; declMsg.clear(); } else declMsg = err;
        }
    }
}

}  // namespace vsapp
