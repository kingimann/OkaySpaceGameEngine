// Automation ops (AutomationOps.hpp): the editor's menus, read from their models, as automation commands. Our own code.
#include "AutomationOps.hpp"

#include "AutomationEditor.hpp"   // EditorCommand, EditorOf
#include "MenuActions.hpp"
#include "SceneAddMenu.hpp"
#include "UIMenuModel.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/Transform.hpp"

#include <algorithm>
#include <map>

namespace oked::automation {

using okay::GameObject;
using okay::automation::Call;
using okay::automation::Command;
using okay::automation::Param;
using okay::automation::ParamType;
using okay::automation::Registry;
using okay::automation::Result;
using okay::editor::EditorState;
using okay::json::Value;
namespace code = okay::automation::code;
namespace flag = okay::automation::flag;
namespace mb = oked::menubar;

namespace {

// ---- The headless side: state from the editor state alone, and the shared actions' plain surroundings ---------------

std::string& HeadlessClipboard() { static std::string s; return s; }
GameObject*& HeadlessIsolate() { static GameObject* g = nullptr; return g; }

mb::State DefaultMenuState(EditorState& ed) {
    mb::State s;
    s.playing = ed.isPlaying();
    s.inPrefabMode = ed.InPrefabMode();
    s.scenePath = ed.path();
    s.selectedCount = (int)ed.MultiSelection().size();
    if (s.selectedCount == 0 && ed.selected()) s.selectedCount = 1;
    s.objectCount = (int)ed.scene().Objects().size();
    s.canUndo = ed.CanUndo();
    s.canRedo = ed.CanRedo();
    s.undoName = ed.UndoName();
    s.redoName = ed.RedoName();
    s.clipboardHasObject = !HeadlessClipboard().empty();
    s.isolating = HeadlessIsolate() != nullptr;
    return s;
}

hiermenu::RowState DefaultRowState(EditorState& ed, GameObject* node) {
    hiermenu::RowState r;
    r.selectedCount = (int)menuactions::RowTargets(ed, node).size();
    r.active = node->active;
    r.locked = node->editorLocked;
    r.childCount = node->transform ? node->transform->ChildCount() : 0;
    okay::Transform* par = node->transform ? node->transform->Parent() : nullptr;
    r.hasParent = par != nullptr;
    int index = 0, count = 0;
    if (par) {
        for (okay::Transform* c : par->Children()) { if (c == node->transform) index = count; ++count; }
    } else {
        for (const auto& up : ed.scene().Objects())
            if (up->transform && !up->transform->Parent()) { if (up.get() == node) index = count; ++count; }
    }
    r.siblingIndex = index;
    r.siblingCount = count > 0 ? count : 1;
    r.clipboardHasObject = !HeadlessClipboard().empty();
    r.isolated = HeadlessIsolate() == node;
    return r;
}

hiermenu::EmptyState DefaultEmptyState(EditorState& ed) {
    hiermenu::EmptyState e;
    e.objectCount = (int)ed.scene().Objects().size();
    e.selectedCount = (int)ed.MultiSelection().size();
    if (e.selectedCount == 0 && ed.selected()) e.selectedCount = 1;
    e.clipboardHasObject = !HeadlessClipboard().empty();
    return e;
}

mb::State MenuStateOf(EditorState& ed) { return Hooks().menuState ? Hooks().menuState(ed) : DefaultMenuState(ed); }
hiermenu::RowState RowStateOf(EditorState& ed, GameObject* g) { return Hooks().rowState ? Hooks().rowState(ed, g) : DefaultRowState(ed, g); }
hiermenu::EmptyState EmptyStateOf(EditorState& ed) { return Hooks().emptyState ? Hooks().emptyState(ed) : DefaultEmptyState(ed); }

menuactions::Env& HeadlessEnv() {
    static menuactions::Env env = [] {
        menuactions::Env e;
        e.log = [](const std::string& text, int level) {
            if (level >= 2) okay::Log::Error(text); else if (level == 1) okay::Log::Warning(text); else okay::Log::Info(text);
        };
        e.clipboard = &HeadlessClipboard();
        e.isolate = &HeadlessIsolate();
        // No one to ask: with unsaved changes it is not run (the op says how to do it without the prompt).
        e.afterSavePrompt = [](EditorState& ed, const char*, std::function<void()> then) {
            if (ed.HasUnsavedChanges()) return false;
            then();
            return true;
        };
        return e;
    }();
    return env;
}

// ---- The editor's menus ----------------------------------------------------------------------------------------------

/// The menu bar state that shows the most (every item there, enabled where it can be).
mb::State FullMenuState() {
    mb::State s;
    s.scenePath = "scene.okayscene";
    s.selectedCount = 1;
    s.objectCount = 1;
    s.canUndo = s.canRedo = true;
    s.clipboardHasObject = true;
    s.recentCount = 1;
    return s;
}

hiermenu::RowState FullRowState() {
    hiermenu::RowState r;
    r.childCount = 1;
    r.hasParent = true;
    r.siblingIndex = 1;
    r.siblingCount = 3;
    r.clipboardHasObject = true;
    r.inPrefab = true;
    r.prefabName = "Prefab.okayprefab";
    r.prefabChanged = true;
    r.prefabHasNested = true;
    r.modelTools = true;
    r.sameKindOthers = 1;
    return r;
}

hiermenu::EmptyState FullEmptyState() {
    hiermenu::EmptyState e;
    e.objectCount = 1;
    e.selectedCount = 1;
    e.clipboardHasObject = true;
    return e;
}

std::vector<mb::Entry> MenuBarEntries(EditorState& ed, GameObject*, bool all) {
    const mb::State s = all ? FullMenuState() : MenuStateOf(ed);
    std::vector<mb::Entry> out;
    for (const mb::Top& t : mb::TopMenus()) {
        if (t.hostDrawn) continue;
        out.push_back(mb::Sub(t.id, t.label, mb::BuildMenu(t.id, s)));   // the top menu as a submenu: its label is the path's start
    }
    return out;
}

std::vector<OpMenu>& Menus() {
    static std::vector<OpMenu> v = [] {
        std::vector<OpMenu> m;
        m.push_back({"menubar", "the main menu bar", false, &MenuBarEntries});
        m.push_back({"hierarchy.row", "the Hierarchy: right-click an object", true,
                     [](EditorState& ed, GameObject* row, bool all) {
                         return hiermenu::RowMenu(all || !row ? FullRowState() : RowStateOf(ed, row));
                     }});
        m.push_back({"hierarchy.empty", "the Hierarchy: right-click the empty space", false,
                     [](EditorState& ed, GameObject*, bool all) { return hiermenu::EmptyMenu(all ? FullEmptyState() : EmptyStateOf(ed)); }});
        m.push_back({"hierarchy.create", "the Hierarchy: the [+] button", false,
                     [](EditorState& ed, GameObject*, bool all) { return hiermenu::CreatePopup(all ? FullEmptyState() : EmptyStateOf(ed)); }});
        m.push_back({"scene.add", "the Scene view: right-click", false,
                     [](EditorState& ed, GameObject*, bool all) {
                         sceneadd::MenuState s;
                         s.hasObjectUnder = true;   // the ops name the object themselves
                         s.clipboardHasObject = all || (MenuStateOf(ed).clipboardHasObject);
                         return sceneadd::BuildMenu(s);
                     }});
        m.push_back({"gameobject.ui", "GameObject > UI", false,
                     [](EditorState&, GameObject*, bool) { return uimenu::UIMenuFull(menuactions::UIScreenGroups()); }});
        return m;
    }();
    return v;
}

// ---- What the ops know about each item ------------------------------------------------------------------------------

struct OpInfo {
    std::string id, command, label, path, tip;
    mb::Kind kind = mb::Kind::Item;
    std::vector<std::string> menus;   // the menus that hold it, in OpMenus() order
    bool row = false;                 // a row menu holds it (it can act on a named object)
    bool plain = false;               // a menu that acts on the selection holds it
};
std::map<std::string, OpInfo>& Ops() { static std::map<std::string, OpInfo> m; return m; }   // by command name

/// Items that open a dialog a person answers. `param` (with `forward`, a command that does the same) lets an agent do
/// it without the dialog.
struct Dialog { const char* id; const char* what; const char* forward; const char* param; const char* paramText; };
const Dialog kDialogs[] = {
    {"file.open", "the Open Scene dialog", "scene.open", "path", "The .okayscene file to open (then no dialog: scene.open runs)."},
    {"file.saveas", "the Save Scene As dialog", "scene.save", "path", "Where to save the scene (then no dialog: scene.save runs)."},
    {"go.instantiate", "the prefab picker", "prefab.instantiate", "path", "The .okayprefab to place (then no dialog: prefab.instantiate runs)."},
    {"h.rename", "the rename box", "object.rename", "name", "The new name (then no box: object.rename runs)."},
    {"file.newproject", "the New Project dialog", nullptr, nullptr, nullptr},
    {"file.openproject", "a folder picker", nullptr, nullptr, nullptr},
    {"file.import", "a file picker and the import settings", nullptr, nullptr, nullptr},
    {"file.merge", "a file picker", nullptr, nullptr, nullptr},
    {"go.arraydup", "the Array Duplicate dialog", nullptr, nullptr, nullptr},
    {"h.saveprefab", "the prefab name box", nullptr, nullptr, nullptr},
    {"h.prefab.variant", "the variant name box", nullptr, nullptr, nullptr},
};
const Dialog* DialogOf(const std::string& id) {
    for (const Dialog& d : kDialogs) if (id == d.id) return &d;
    return nullptr;
}

/// Ops that are plain copies of a richer command (rule 19c: one handler, the other name an alias).
const std::pair<const char*, const char*> kAliases[] = {
    {"edit.undo", "undo.undo"}, {"edit.redo", "undo.redo"},
    {"go.empty", "object.create"}, {"h.createempty", "object.create"},
    // The Scene view's menu repeats the Hierarchy's object items.
    {"scene.empty", "op.h.createempty"}, {"scene.focus", "op.h.focus"}, {"scene.rename", "op.h.rename"},
    {"scene.duplicate", "op.h.duplicate"}, {"scene.copy", "op.h.copy"}, {"scene.paste", "op.h.paste"}, {"scene.delete", "op.h.delete"},
};
const char* AliasOf(const std::string& id) {
    for (const auto& a : kAliases) if (id == a.first) return a.second;
    return nullptr;
}

bool Listed(const mb::Entry& e) {
    if (e.kind != mb::Kind::Item && e.kind != mb::Kind::Toggle) return false;
    if (e.id.empty()) return false;
    auto ends = [&](const char* tail) { const std::string t = tail; return e.id.size() > t.size() && e.id.compare(e.id.size() - t.size(), t.size(), t) == 0; };
    return !ends(".none") && !ends(".more") && e.id.find(':') == std::string::npos;   // notes, and per-project prefab entries
}

void Collect(const OpMenu& menu, const std::vector<mb::Entry>& es, const std::string& path, std::vector<std::pair<mb::Entry, std::string>>& out) {
    for (const mb::Entry& e : es) {
        if (!e.children.empty()) { Collect(menu, e.children, path.empty() ? e.label : path + " > " + e.label, out); continue; }
        if (Listed(e)) out.push_back({e, path.empty() ? e.label : path + " > " + e.label});
    }
}

/// The entry `id` as the menu shows it now (null when it is not offered now).
const mb::Entry* FindLive(const std::vector<mb::Entry>& es, const std::string& id) { return mb::FindId(es, id); }

Value SelectionIds(EditorState& ed) {
    Value ids = Value::Arr();
    for (GameObject* g : ed.MultiSelection()) if (g) ids.Push(Value::Str(g->Guid()));
    if (ids.array.empty() && ed.selected()) ids.Push(Value::Str(ed.selected()->Guid()));
    return ids;
}

const OpMenu* MenuNamed(const std::string& name) {
    for (const OpMenu& m : Menus()) if (m.name == name) return &m;
    return nullptr;
}

std::string Summary(const OpInfo& o) {
    std::string where;
    for (const std::string& m : o.menus) if (const OpMenu* k = MenuNamed(m)) { where = k->where; break; }
    std::string s = (o.kind == mb::Kind::Toggle ? "Toggle '" : "Run '") + o.path + "' (" + where + "), as clicking it does";
    s += o.row && !o.plain ? ", on the object named (default: the selected one)." : ".";
    if (!o.tip.empty()) {
        std::string tip = o.tip;
        std::replace(tip.begin(), tip.end(), '\n', ' ');
        s += " " + tip;
    }
    if (const Dialog* d = DialogOf(o.id))
        s += std::string(" It opens ") + d->what + ", which needs a person" + (d->param ? std::string(", unless '") + d->param + "' is given." : std::string(": without one it answers needs_ui."));
    if (o.id == "file.new2d" || o.id == "file.new3d") s += " With unsaved changes it would ask to save first: pass discard true to drop them (scene.new runs), else it answers needs_ui.";
    return s;
}

/// Run op `o` on the editor `ed` with the call's parameters.
Result RunOp(const OpInfo& o, Call& c, EditorState& ed) {
    if (o.id == "file.exit") return Result::Fail(code::Refused, "An agent cannot close the editor; the person closes it.");
    // A dialog with a scripted equivalent: the parameter does it with no dialog.
    if (const Dialog* d = DialogOf(o.id)) {
        if (d->param && c.Has(d->param)) {
            Value p = Value::Obj().Set(d->param, *c.Get(d->param));
            if (o.row) {
                GameObject* g = nullptr;
                if (c.Has("object")) {
                    okay::automation::Error e;
                    g = okay::automation::FindObject(ed.scene(), c.Str("object"), e);
                    if (!g) return okay::automation::Fail(e);
                } else g = ed.selected();
                if (!g) return Result::Fail(code::InvalidParams, "Name the object (object), or select one first.");
                p.Set("object", Value::Str(g->Guid()));
            }
            return okay::automation::Commands().Run(c.host(), d->forward, p);
        }
        return Result::Fail(code::NeedsUi, std::string("'") + o.path + "' opens " + d->what + ", which needs a person." +
                                               (d->param ? std::string(" Give '") + d->param + "' to do it without the dialog." : std::string()),
                            d->param ? Value::Obj().Set("param", Value::Str(d->param)).Set("command", Value::Str(d->forward)) : Value());
    }
    if (o.id == "file.save" && (c.Has("path") || ed.path().empty())) {
        if (!c.Has("path")) return Result::Fail(code::NeedsUi, "The scene has no file yet, so Save opens the Save As dialog. Give 'path' to save without it.",
                                                Value::Obj().Set("param", Value::Str("path")).Set("command", Value::Str("scene.save")));
        return okay::automation::Commands().Run(c.host(), "scene.save", Value::Obj().Set("path", *c.Get("path")));
    }
    if ((o.id == "file.new2d" || o.id == "file.new3d") && ed.HasUnsavedChanges()) {
        if (!c.Bool("discard", false))
            return Result::Fail(code::NeedsUi, "The scene has unsaved changes, so this asks the person to save first. Save it (scene.save), or pass discard true.",
                                Value::Obj().Set("param", Value::Str("discard")));
        return okay::automation::Commands().Run(c.host(), "scene.new",
                                                Value::Obj().Set("template", Value::Str(o.id == "file.new2d" ? "2d" : "3d")).Set("discard", Value::Boolean(true)));
    }

    // Which menu: a named object means the row menu; otherwise the first menu that acts on the selection.
    GameObject* row = nullptr;
    std::string menuName;
    if (o.row && (c.Has("object") || !o.plain)) {
        if (c.Has("object")) {
            okay::automation::Error e;
            row = okay::automation::FindObject(ed.scene(), c.Str("object"), e);
            if (!row) return okay::automation::Fail(e);
        } else row = ed.selected();
        if (!row) return Result::Fail(code::InvalidParams, "Name the object (object), or select one first: this item acts on one object.");
        if (!ed.IsSelected(row)) ed.Select(row);   // right-clicking a row selects it (a multi-selection it is part of stays)
        for (const std::string& m : o.menus) if (const OpMenu* k = MenuNamed(m); k && k->rowTarget) { menuName = m; break; }
    } else {
        for (const std::string& m : o.menus) if (const OpMenu* k = MenuNamed(m); k && !k->rowTarget) { menuName = m; break; }
    }
    const OpMenu* menu = MenuNamed(menuName);
    if (!menu) return Result::Fail(code::Internal, "op " + o.id + " has no menu.");
    const std::vector<mb::Entry> live = menu->entries(ed, row, false);
    const mb::Entry* e = FindLive(live, o.id);
    if (!e) return Result::Fail(code::Refused, "'" + o.path + "' is not offered now" + (row ? " for " + row->name : std::string()) + ".");
    if (!e->enabled) return Result::Fail(code::Refused, e->reason.empty() ? "'" + o.path + "' is greyed out now." : e->reason);

    Value out = Value::Obj().Set("id", Value::Str(o.id)).Set("label", Value::Str(e->label));
    if (o.kind == mb::Kind::Toggle) {
        bool* f = Hooks().flag ? Hooks().flag(menu->name, o.id) : nullptr;
        if (!f) return Result::Fail(code::Unavailable, "'" + o.path + "' shows a window or a mode of the editor's window; this host has none (use okay mcp with the running editor).");
        *f = c.Has("on") ? c.Bool("on") : !*f;
        return Result::Ok(out.Set("checked", Value::Boolean(*f)));
    }
    if (!(Hooks().run && Hooks().run(ed, menu->name, o.id, row))) {
        menuactions::After after;
        const menuactions::Outcome r = menuactions::Run(ed, o.id, row, HeadlessEnv(), after);
        if (r == menuactions::Outcome::NotHandled)
            return Result::Fail(code::Unavailable, "'" + o.path + "' runs the editor window's own code; this host has no window (use okay mcp with the running editor).");
        if (r == menuactions::Outcome::NeedsPerson) return Result::Fail(code::NeedsUi, "'" + o.path + "' needs a person to answer a question first.");
        menuactions::ApplyAfter(ed, after, HeadlessEnv());
    }
    out.Set("selection", SelectionIds(ed)).Set("objects", Value::Num((double)ed.scene().Objects().size()));
    return Result::Ok(std::move(out));
}

Value OpJson(const OpInfo& o, EditorState* ed, GameObject* row) {
    Value v = Value::Obj();
    v.Set("command", Value::Str(o.command)).Set("id", Value::Str(o.id)).Set("label", Value::Str(o.label)).Set("path", Value::Str(o.path));
    v.Set("kind", Value::Str(o.kind == mb::Kind::Toggle ? "toggle" : "item"));
    Value menus = Value::Arr();
    for (const std::string& m : o.menus) menus.Push(Value::Str(m));
    v.Set("menus", std::move(menus));
    if (const Dialog* d = DialogOf(o.id)) v.Set("needsPerson", Value::Str(d->what)).Set("withoutDialog", d->param ? Value::Str(d->param) : Value());
    if (!ed) return v;
    // The live state, from the first menu that shows it (the row menu when an object was named).
    for (const std::string& m : o.menus) {
        const OpMenu* k = MenuNamed(m);
        if (!k || (k->rowTarget && !row) || (!k->rowTarget && row && o.row)) continue;
        const std::vector<mb::Entry> live = k->entries(*ed, row, false);
        const mb::Entry* e = FindLive(live, o.id);
        v.Set("offered", Value::Boolean(e != nullptr));
        if (e) {
            v.Set("enabled", Value::Boolean(e->enabled));
            if (!e->enabled && !e->reason.empty()) v.Set("reason", Value::Str(e->reason));
            v.Set("label", Value::Str(e->label));
        }
        if (o.kind == mb::Kind::Toggle) {
            bool* f = Hooks().flag ? Hooks().flag(m, o.id) : nullptr;
            v.Set("checked", f ? Value::Boolean(*f) : Value());
        }
        break;
    }
    return v;
}

}  // namespace

void AddOpMenu(OpMenu m) {
    for (OpMenu& k : Menus()) if (k.name == m.name) { k = std::move(m); return; }
    Menus().push_back(std::move(m));
}
const std::vector<OpMenu>& OpMenus() { return Menus(); }
OpHooks& Hooks() { static OpHooks h; return h; }
int OpCount() { return (int)Ops().size(); }

std::string OpCommandName(const std::string& id) {
    std::string n = "op.";
    for (char ch : id) {
        char c = (char)std::tolower((unsigned char)ch);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) c = '_';
        if (c == '.' && n.back() == '.') continue;
        n += c;
    }
    while (!n.empty() && (n.back() == '.' || n.back() == '_')) n.pop_back();
    return n;
}

int RefreshOps(Registry& r) {
    // Gather every listed entry of every menu (with the state that shows the most). The menus need an editor state only
    // for their live state, so a throwaway one is enough here.
    static EditorState* probe = new EditorState;   // kept to the end (no destruction order to get wrong at exit)
    std::vector<std::string> fresh;
    for (const OpMenu& menu : Menus()) {
        std::vector<std::pair<mb::Entry, std::string>> found;
        Collect(menu, menu.entries(*probe, nullptr, true), menu.name == "menubar" ? std::string() : std::string(), found);
        for (const auto& [e, path] : found) {
            const std::string cmd = OpCommandName(e.id);
            auto it = Ops().find(cmd);
            if (it == Ops().end()) {
                OpInfo o;
                o.id = e.id; o.command = cmd; o.label = e.label; o.path = path; o.tip = e.tip; o.kind = e.kind;
                it = Ops().emplace(cmd, std::move(o)).first;
                fresh.push_back(cmd);
            }
            OpInfo& o = it->second;
            if (std::find(o.menus.begin(), o.menus.end(), menu.name) == o.menus.end()) o.menus.push_back(menu.name);
            (menu.rowTarget ? o.row : o.plain) = true;
        }
    }
    int added = 0;
    std::vector<std::string> later;   // aliases go in once what they name is there
    for (const std::string& cmd : fresh) {
        const OpInfo& o = Ops().at(cmd);
        if (r.Find(cmd)) continue;
        if (AliasOf(o.id)) { later.push_back(cmd); continue; }
        std::vector<Param> params;
        if (o.row) params.push_back(okay::automation::Arg("object", ParamType::String,
            o.plain ? "Act on this object, as its Hierarchy row's menu does (left out: as the menu that acts on the selection does)."
                    : "The object (its id, path or name); left out: the selected one.", false));
        if (o.kind == mb::Kind::Toggle) params.push_back(okay::automation::Arg("on", ParamType::Boolean, "On or off (left out: the other way round).", false));
        if (const Dialog* d = DialogOf(o.id); d && d->param && !(o.row && std::string(d->param) == "object"))
            params.push_back(okay::automation::Arg(d->param, ParamType::String, d->paramText, false));
        if (o.id == "file.save") params.push_back(okay::automation::Arg("path", ParamType::String, "Save to this file (needed when the scene has none yet: then no dialog).", false));
        if (o.id == "file.new2d" || o.id == "file.new3d")
            params.push_back(okay::automation::ArgDefault("discard", ParamType::Boolean, "Drop unsaved changes instead of the save prompt.", Value::Boolean(false)));
        // A scene edit is one undo step; a file command manages the history itself; nothing is refused in Play by the
        // table: the menu's own greying decides, as for a click.
        const bool file = o.id.rfind("file.", 0) == 0;
        const unsigned flags = flag::AllowInPlay | (o.kind == mb::Kind::Toggle || file ? flag::Mutates : flag::Undoable);
        const std::string id = o.id;
        Command k = EditorCommand(cmd.c_str(), Summary(o).c_str(), std::move(params), "{id, label, selection, objects} (a toggle: {id, label, checked})", flags,
            [id](Call& c, EditorState& ed) {
                auto it = Ops().find(OpCommandName(id));
                if (it == Ops().end()) return Result::Fail(code::Internal, "op " + id + " is gone.");
                return RunOp(it->second, c, ed);
            }, {code::NotFound, code::Refused, code::Unavailable, code::NeedsUi}, o.label.c_str());
        if (r.Add(std::move(k))) ++added;
        else okay::Log::Warning("Automation: the menu item ", o.id, " could not become ", cmd, " (its name is taken).");
    }
    for (const std::string& cmd : later) {
        std::string target = AliasOf(Ops().at(cmd).id);
        if (r.AddAlias(cmd, target)) ++added;
    }
    return added;
}

void AddOpCommands(Registry& r) {
    r.Add(EditorCommand("op.list", "List the ops: every item of the editor's menus as a command (op.<id>), with its menu path and its live state (enabled, why not, checked) the way the menu shows it now.",
              {okay::automation::Arg("prefix", ParamType::String, "Only ids that start with this (\"edit.\", \"h.\", \"ui.\")."),
               okay::automation::Arg("object", ParamType::String, "Show the Hierarchy row menu's state for this object.")},
              "{count, ops: [{command, id, label, path, kind, menus, offered, enabled, reason, checked, needsPerson, withoutDialog}]}", 0,
              [](Call& c, EditorState& ed) {
                  GameObject* row = nullptr;
                  if (c.Has("object")) {
                      okay::automation::Error e;
                      row = okay::automation::FindObject(ed.scene(), c.Str("object"), e);
                      if (!row) return okay::automation::Fail(e);
                  }
                  const std::string prefix = c.Str("prefix");
                  Value list = Value::Arr();
                  for (const auto& kv : Ops()) {
                      if (kv.second.id.compare(0, prefix.size(), prefix) != 0) continue;
                      list.Push(OpJson(kv.second, &ed, row));
                  }
                  const double n = (double)list.array.size();
                  return Result::Ok(Value::Obj().Set("count", Value::Num(n)).Set("ops", std::move(list)));
              }, {code::NotFound}));
    r.Add(EditorCommand("op.run", "Run an op by its menu item id (\"edit.duplicate\", \"h.tf.resetpos\", \"ui.button\"), the same as calling op.<id>.",
              {okay::automation::Arg("id", ParamType::String, "The menu item id (op.list lists them).", true),
               okay::automation::ArgDefault("params", ParamType::Object, "The op's parameters (object, on, path, name, discard).", Value::Obj())},
              "what op.<id> answers", flag::AllowInPlay | flag::Mutates, [](Call& c, EditorState&) {
                  const std::string cmd = OpCommandName(c.Str("id"));
                  if (!okay::automation::Commands().Find(cmd)) return Result::Fail(code::NotFound, "There is no op '" + c.Str("id") + "' (op.list lists them).");
                  const Value* p = c.Get("params");
                  return okay::automation::Commands().Run(c.host(), cmd, p ? *p : Value::Obj());
              }, {code::NotFound}));
    RefreshOps(r);
}

}  // namespace oked::automation
