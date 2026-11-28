// Automation on the editor's state (AutomationEditor.hpp). Each command calls what the editor itself does:
//   scene.new / open / save     EditorState::NewScene*, Load, Save (all or nothing, refused while playing)
//   object.*, editor.select     EditorState::Create*, DuplicateSelected, DeleteSelected, Select; Transform::SetParent
//   prefab.instantiate          SceneSerializer::InstantiateFromFile (the Project panel's drop)
//   undo.*                      EditorState::Undo / Redo and its history (okay::UndoStack under SceneDeltaCommand)
//   play.*                      EditorState::Play / Stop / PlayFrame (the editor's Play loop), input fed as PlayTest feeds it
//   model.*                     the Modeling window's tools: oked::easymodel::RunMeshTool and okay::modeling::ApplyShapeOp
//   ui.*                        the UI widget components, GetUIRect / GetUIScreenRect (the editor's UI selection code)
// The link (View > Automation Link) runs okay::automation::McpServer on the editor's own state, between frames.
// Our own code, written on those APIs.
#include "AutomationEditor.hpp"

#include "EditorTabs.hpp"          // okay::edtabs::UserFile: the editor's per-user folder
#include "ModelingEasy.hpp"
#include "ModelingEasyOps.hpp"
#include "okay/Automation/Link.hpp"
#include "okay/Automation/Mcp.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Modeling/ModelOpUndo.hpp"   // SameMesh
#include "okay/Modeling/ShapeOps.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>

namespace oked::automation {

using okay::automation::Call;
using okay::automation::Command;
using okay::automation::Error;
using okay::automation::Param;
using okay::automation::ParamType;
using okay::automation::Registry;
using okay::automation::Result;
using okay::json::Value;
namespace code = okay::automation::code;
namespace flag = okay::automation::flag;
using okay::GameObject;
using okay::editor::EditorState;

// ---- EditorHost --------------------------------------------------------------------------------------------------------

void EditorHost::BeginEdit(const std::string& name) {
    if (m_depth++ > 0) return;   // a command run from inside another one is part of its step
    m_ed.PushUndo(name);
    m_revision = m_ed.undoStack().Revision();
}

void EditorHost::EndEdit(bool ok) {
    if (m_depth == 0 || --m_depth > 0) return;
    (void)ok;
    // What the command changed becomes one step even when it failed half way (so Undo can take it back); a command
    // that changed nothing records nothing (SyncHistory compares the scene text).
    m_ed.CommitUndo();
    if (m_ed.undoStack().Revision() != m_revision) m_ed.dirty = true;   // a step was recorded: the title bar and autosave see it
}

bool EditorHost::EditorView(float eye[3], float target[3], bool& orthographic, float& size) const {
    if (m_ed.view3D) {
        // The Scene view's orbit camera: yaw and pitch around camTarget at camDist (as CreateCamera places a camera).
        const float yr = m_ed.camYaw * okay::Mathf::Deg2Rad, pr = m_ed.camPitch * okay::Mathf::Deg2Rad;
        const okay::Vec3 dir{okay::Mathf::Cos(pr) * okay::Mathf::Sin(yr), okay::Mathf::Sin(pr), okay::Mathf::Cos(pr) * okay::Mathf::Cos(yr)};
        const okay::Vec3 e = m_ed.camTarget + dir * m_ed.camDist;
        eye[0] = e.x; eye[1] = e.y; eye[2] = e.z;
        target[0] = m_ed.camTarget.x; target[1] = m_ed.camTarget.y; target[2] = m_ed.camTarget.z;
        orthographic = false;
        size = 5.0f;
        return true;
    }
    eye[0] = m_ed.cameraPos.x; eye[1] = m_ed.cameraPos.y; eye[2] = 10.0f;
    target[0] = m_ed.cameraPos.x; target[1] = m_ed.cameraPos.y; target[2] = 0.0f;
    orthographic = true;
    size = m_ed.cameraZoom * 0.5f;
    return true;
}

using okay::automation::Arg;
using okay::automation::ArgChoice;
using okay::automation::ArgDefault;
using okay::automation::Fail;

EditorState* EditorOf(Call& c) {
    auto* h = dynamic_cast<EditorHost*>(&c.host());
    return h ? &h->ed() : nullptr;
}

Command EditorCommand(const char* name, const char* summary, std::vector<Param> params, const char* result, unsigned flags,
                      EditorHandler handler, std::vector<std::string> errors, const char* undoName) {
    Command c = okay::automation::MakeCommand(name, summary, std::move(params), result, flags | flag::NeedsEditor,
        [handler](Call& call) {
            EditorState* ed = EditorOf(call);
            if (!ed) return Result::Fail(code::Unavailable, "This host has no editor state.");
            return handler(call, *ed);
        }, std::move(errors));
    c.undoName = undoName;
    return c;
}

std::string ProjectFile(const EditorState& ed, const std::string& given, bool fallbackInAssets) {
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::u8path(given), ec) || ed.projectDir().empty()) return given;
    for (const char* sub : {"/", "/Assets/"}) {
        const std::string p = ed.projectDir() + sub + given;
        if (std::filesystem::exists(std::filesystem::u8path(p), ec)) return p;
    }
    if (fallbackInAssets && !std::filesystem::u8path(given).is_absolute()) return ed.projectDir() + "/Assets/" + given;
    return given;
}

namespace {

const Param kObject = okay::automation::Arg("object", ParamType::String, "The object: its id (GUID), a path such as Parent/Child, or a unique name.", true);

GameObject* Find(Call& c, EditorState& ed, const char* param, Error& e) { return okay::automation::FindObject(ed.scene(), c.Str(param), e); }

Value SelectionJson(EditorState& ed) {
    Value ids = Value::Arr();
    for (GameObject* g : ed.MultiSelection()) if (g) ids.Push(Value::Str(g->Guid()));
    return Value::Obj().Set("selected", std::move(ids)).Set("primary", ed.selected() ? Value::Str(ed.selected()->Guid()) : Value());
}

Value HistoryJson(EditorState& ed) {
    Value u = Value::Arr(), r = Value::Arr();
    for (const auto& n : ed.UndoNames(50)) u.Push(Value::Str(n));
    for (const auto& n : ed.RedoNames(50)) r.Push(Value::Str(n));
    return Value::Obj().Set("undo", std::move(u)).Set("redo", std::move(r)).Set("canUndo", Value::Boolean(ed.CanUndo()))
        .Set("canRedo", Value::Boolean(ed.CanRedo())).Set("unsaved", Value::Boolean(ed.dirty));
}

/// True when the open scene has changes `discard` does not allow to lose.
bool Unsaved(EditorState& ed, Call& c) { return !c.Bool("discard", false) && ed.HasUnsavedChanges(); }

// ---- scene.* -------------------------------------------------------------------------------------------------------

void AddSceneCommands(Registry& r) {
    r.Add(EditorCommand("scene.new", "Start a new scene from one of the editor's starts (empty, 2D, 3D), as File > New Scene does; the undo history starts again.",
              {ArgChoice("template", {"empty", "2d", "3d"}, "empty: nothing; 2d: an orthographic camera and a sprite; 3d: a camera, ground and a cube.", "3d"),
               ArgDefault("discard", ParamType::Boolean, "Throw away unsaved changes of the open scene (otherwise refused when there are some).", Value::Boolean(false))},
              "{scene, objects}", flag::Mutates, [](Call& c, EditorState& ed) {
                  if (Unsaved(ed, c)) return Result::Fail(code::Refused, "The open scene has unsaved changes: save it (scene.save) or pass discard true.");
                  const std::string t = c.Str("template", "3d");
                  if (t == "empty") ed.NewScene(); else if (t == "2d") ed.NewScene2D(); else ed.NewScene3D();
                  return Result::Ok(Value::Obj().Set("scene", Value::Str(ed.scene().Name())).Set("objects", Value::Num((double)ed.scene().Objects().size())));
              }));
    r.Add(EditorCommand("scene.open", "Open a scene file (all or nothing: a missing or damaged file leaves the open scene as it was); the undo history starts again.",
              {Arg("path", ParamType::String, "The .okayscene file (absolute, or inside the project).", true),
               ArgDefault("discard", ParamType::Boolean, "Throw away unsaved changes of the open scene.", Value::Boolean(false))},
              "{path, scene, objects}", flag::Mutates, [](Call& c, EditorState& ed) {
                  if (Unsaved(ed, c)) return Result::Fail(code::Refused, "The open scene has unsaved changes: save it (scene.save) or pass discard true.");
                  const std::string path = ProjectFile(ed, c.Str("path"));
                  std::string err;
                  if (!ed.Load(path, &err)) return Result::Fail(code::Failed, err.empty() ? "The scene could not be opened." : err);
                  return Result::Ok(Value::Obj().Set("path", Value::Str(ed.path())).Set("scene", Value::Str(ed.scene().Name()))
                                        .Set("objects", Value::Num((double)ed.scene().Objects().size())));
              }, {code::Failed}));
    r.Add(EditorCommand("scene.save", "Save the open scene (to its own file, or to a new path), as File > Save does; meshes edited since they were loaded are written back too.",
              {Arg("path", ParamType::String, "Save to this .okayscene file instead (the scene then uses it).")},
              "{path, saved}", flag::Mutates, [](Call& c, EditorState& ed) {
                  const std::string path = c.Has("path") ? c.Str("path") : ed.path();
                  if (path.empty()) return Result::Fail(code::InvalidParams, "The scene has no file yet: give a path.");
                  std::string err;
                  if (!ed.Save(path, &err)) return Result::Fail(code::Failed, err.empty() ? "The scene could not be saved." : err);
                  return Result::Ok(Value::Obj().Set("path", Value::Str(path)).Set("saved", Value::Boolean(true)));
              }, {code::Failed}));
    r.Add(EditorCommand("scene.state", "The editor's document: the scene's file, whether it has unsaved changes, prefab mode, and what Play would refuse.",
              {}, "{path, unsaved, prefabMode, playing, playBlocked}", 0, [](Call&, EditorState& ed) {
                  return Result::Ok(Value::Obj().Set("path", Value::Str(ed.path())).Set("unsaved", Value::Boolean(ed.HasUnsavedChanges()))
                                        .Set("prefabMode", Value::Boolean(ed.InPrefabMode())).Set("playing", Value::Boolean(ed.isPlaying()))
                                        .Set("playBlocked", Value::Str(ed.PlayBlockedReason())));
              }));
}

// ---- object.*, editor.select, prefab.instantiate --------------------------------------------------------------------

void AddObjectCommands(Registry& r) {
    r.Add(EditorCommand("object.create", "Create an object as GameObject > Create does: empty, a primitive mesh (with a fitted collider), a camera or a sprite; it is selected.",
              {ArgChoice("kind", {"empty", "cube", "sphere", "plane", "cylinder", "pyramid", "camera", "sprite"}, "What to make.", "empty"),
               Arg("name", ParamType::String, "Its name (default: the kind's name)."),
               Arg("parent", ParamType::String, "Put it under this object (default: at the top of the scene)."),
               Arg("position", ParamType::Vec3, "Its local position (default: where the editor places new objects).")},
              "{id, name, path, components}", flag::Undoable, [](Call& c, EditorState& ed) {
                  const std::string kind = c.Str("kind", "empty");
                  GameObject* parent = nullptr;
                  if (c.Has("parent")) {
                      Error e;
                      parent = Find(c, ed, "parent", e);
                      if (!parent) return Fail(e);
                  }
                  GameObject* go = nullptr;
                  static const std::map<std::string, std::string> kMesh = {{"cube", "Cube"}, {"sphere", "Sphere"}, {"plane", "Plane"}, {"cylinder", "Cylinder"}, {"pyramid", "Pyramid"}};
                  if (kind == "empty") go = ed.CreateEmpty(c.Str("name", "GameObject"));
                  else if (kind == "camera") go = ed.CreateCamera(c.Str("name", "Camera"));
                  else if (kind == "sprite") go = ed.CreateSprite(c.Str("name", "Sprite"));
                  else go = ed.CreateMesh(kMesh.at(kind));
                  if (!go) return Result::Fail(code::Failed, "The object could not be made.");
                  if (c.Has("name")) go->name = c.Str("name");
                  if (parent && go->transform && parent->transform) go->transform->SetParent(parent->transform, false);
                  if (c.Has("position") && go->transform) { float f[3]; c.Vec("position", f); go->transform->localPosition = okay::Vec3{f[0], f[1], f[2]}; }
                  ed.Select(go);
                  return Result::Ok(okay::automation::ObjectSummary(*go));
              }, {code::NotFound, code::Failed}));
    r.Add(EditorCommand("object.delete", "Delete objects (and what is below them), as the Hierarchy's Delete does.",
              {Arg("objects", ParamType::Array, "The objects (ids, paths or names).", true)},
              "{deleted}", flag::Undoable, [](Call& c, EditorState& ed) {
                  std::vector<GameObject*> list;
                  for (const Value& v : c.Get("objects")->array) {
                      if (!v.IsString()) return Result::Fail(code::InvalidParams, "objects holds ids, paths or names (text).");
                      Error e;
                      GameObject* g = okay::automation::FindObject(ed.scene(), v.string, e);
                      if (!g) return Fail(e);
                      list.push_back(g);
                  }
                  if (list.empty()) return Result::Fail(code::InvalidParams, "Name at least one object.");
                  ed.Select(list[0]);
                  for (std::size_t i = 1; i < list.size(); ++i) ed.ToggleSelect(list[i]);
                  ed.DeleteSelected();
                  return Result::Ok(Value::Obj().Set("deleted", Value::Num((double)list.size())));
              }, {code::NotFound}));
    r.Add(EditorCommand("object.duplicate", "Duplicate an object (with its children) as Edit > Duplicate does: the copy sits next to it with a free name and is selected.",
              {kObject}, "{id, name, path, components}", flag::Undoable, [](Call& c, EditorState& ed) {
                  Error e;
                  GameObject* g = Find(c, ed, "object", e);
                  if (!g) return Fail(e);
                  ed.Select(g);
                  GameObject* copy = ed.DuplicateSelected();
                  if (!copy) return Result::Fail(code::Failed, "The object could not be duplicated.");
                  return Result::Ok(okay::automation::ObjectSummary(*copy));
              }, {code::NotFound, code::Failed}));
    r.Add(EditorCommand("object.rename", "Rename an object.", {kObject, Arg("name", ParamType::String, "The new name.", true)},
              "{id, name, path}", flag::Undoable, [](Call& c, EditorState& ed) {
                  Error e;
                  GameObject* g = Find(c, ed, "object", e);
                  if (!g) return Fail(e);
                  if (c.Str("name").empty()) return Result::Fail(code::InvalidParams, "The name must not be empty.");
                  g->name = c.Str("name");
                  return Result::Ok(okay::automation::ObjectSummary(*g));
              }, {code::NotFound}));
    r.Add(EditorCommand("object.reparent", "Move an object under another one (or to the top of the scene), keeping where it is in the world unless keepWorld is false.",
              {kObject, Arg("parent", ParamType::String, "The new parent; leave out for the top of the scene."),
               ArgDefault("keepWorld", ParamType::Boolean, "Keep its world position, rotation and scale.", Value::Boolean(true)),
               Arg("index", ParamType::Integer, "Its place among the new parent's children (default: last).")},
              "{id, name, path, parent}", flag::Undoable, [](Call& c, EditorState& ed) {
                  Error e;
                  GameObject* g = Find(c, ed, "object", e);
                  if (!g || !g->transform) return Fail(e);
                  okay::Transform* parent = nullptr;
                  if (c.Has("parent")) {
                      GameObject* p = Find(c, ed, "parent", e);
                      if (!p || !p->transform) return Fail(e);
                      if (p->IsSelfOrDescendantOf(g)) return Result::Fail(code::Refused, "An object cannot go under itself or its own children.");
                      parent = p->transform;
                  }
                  const bool ok = c.Has("index") ? g->transform->SetParent(parent, c.Bool("keepWorld", true), (int)c.Int("index", -1))
                                                 : g->transform->SetParent(parent, c.Bool("keepWorld", true));
                  if (!ok) return Result::Fail(code::Failed, "The object could not be moved there.");
                  Value v = okay::automation::ObjectSummary(*g);
                  v.Set("parent", parent && parent->gameObject ? Value::Str(parent->gameObject->Guid()) : Value());
                  return Result::Ok(std::move(v));
              }, {code::NotFound, code::Refused}));
    r.Add(EditorCommand("object.set_active", "Switch an object on or off (with what is below it).", {kObject, Arg("active", ParamType::Boolean, "On or off.", true)},
              "{id, active}", flag::Undoable, [](Call& c, EditorState& ed) {
                  Error e;
                  GameObject* g = Find(c, ed, "object", e);
                  if (!g) return Fail(e);
                  g->active = c.Bool("active", true);
                  return Result::Ok(Value::Obj().Set("id", Value::Str(g->Guid())).Set("active", Value::Boolean((bool)g->active)));
              }, {code::NotFound}));
    r.Add(EditorCommand("editor.select", "Select objects in the editor (the Inspector shows the last one); an empty list clears the selection.",
              {Arg("objects", ParamType::Array, "The objects (ids, paths or names).", true)},
              "{selected, primary}", 0, [](Call& c, EditorState& ed) {
                  std::vector<GameObject*> list;
                  for (const Value& v : c.Get("objects")->array) {
                      if (!v.IsString()) return Result::Fail(code::InvalidParams, "objects holds ids, paths or names (text).");
                      Error e;
                      GameObject* g = okay::automation::FindObject(ed.scene(), v.string, e);
                      if (!g) return Fail(e);
                      list.push_back(g);
                  }
                  ed.Select(list.empty() ? nullptr : list[0]);
                  for (std::size_t i = 1; i < list.size(); ++i) ed.ToggleSelect(list[i]);
                  return Result::Ok(SelectionJson(ed));
              }, {code::NotFound}));
    r.Add(EditorCommand("editor.selection", "What is selected in the editor.", {}, "{selected, primary}", 0,
              [](Call&, EditorState& ed) { return Result::Ok(SelectionJson(ed)); }));
    r.Add(EditorCommand("prefab.instantiate", "Place a prefab file in the scene (as dropping it from the Project panel does), optionally under a parent and at a position; it is selected.",
              {Arg("path", ParamType::String, "The .okayprefab file (absolute, or inside the project).", true),
               Arg("parent", ParamType::String, "Put it under this object."), Arg("position", ParamType::Vec3, "Its local position.")},
              "{id, name, path, components, warnings}", flag::Undoable, [](Call& c, EditorState& ed) {
                  const std::string path = ProjectFile(ed, c.Str("path"));
                  GameObject* parent = nullptr;
                  if (c.Has("parent")) {
                      Error e;
                      parent = Find(c, ed, "parent", e);
                      if (!parent) return Fail(e);
                  }
                  std::string err;
                  std::vector<std::string> warn;
                  GameObject* go = okay::SceneSerializer::InstantiateFromFile(ed.scene(), path, &err, &warn);
                  if (!go) return Result::Fail(code::Failed, err.empty() ? "The prefab could not be placed." : err);
                  if (parent && go->transform) go->transform->SetParent(parent->transform, false);
                  if (c.Has("position") && go->transform) { float f[3]; c.Vec("position", f); go->transform->localPosition = okay::Vec3{f[0], f[1], f[2]}; }
                  ed.Select(go);
                  Value v = okay::automation::ObjectSummary(*go);
                  Value w = Value::Arr();
                  for (const auto& s : warn) w.Push(Value::Str(s));
                  v.Set("warnings", std::move(w));
                  return Result::Ok(std::move(v));
              }, {code::NotFound, code::Failed}, "Place Prefab"));
}

// ---- undo.* ----------------------------------------------------------------------------------------------------------

void AddUndoCommands(Registry& r) {
    r.Add(EditorCommand("undo.undo", "Undo the newest steps of the editor's history (what Edit > Undo does).",
              {ArgDefault("steps", ParamType::Integer, "How many steps.", Value::Num(1))}, "{undone, history}", flag::Mutates, [](Call& c, EditorState& ed) {
                  const long long n = std::max<long long>(1, c.Int("steps", 1));
                  int done = 0;
                  for (long long i = 0; i < n; ++i) { if (!ed.CanUndo() || !ed.Undo()) break; ++done; }
                  if (done == 0) return Result::Fail(code::Refused, ed.UndoError().empty() ? "There is nothing to undo." : ed.UndoError());
                  return Result::Ok(Value::Obj().Set("undone", Value::Num(done)).Set("history", HistoryJson(ed)));
              }, {code::Refused}));
    r.Add(EditorCommand("undo.redo", "Redo steps that were undone (Edit > Redo).",
              {ArgDefault("steps", ParamType::Integer, "How many steps.", Value::Num(1))}, "{redone, history}", flag::Mutates, [](Call& c, EditorState& ed) {
                  const long long n = std::max<long long>(1, c.Int("steps", 1));
                  int done = 0;
                  for (long long i = 0; i < n; ++i) { if (!ed.CanRedo() || !ed.Redo()) break; ++done; }
                  if (done == 0) return Result::Fail(code::Refused, ed.UndoError().empty() ? "There is nothing to redo." : ed.UndoError());
                  return Result::Ok(Value::Obj().Set("redone", Value::Num(done)).Set("history", HistoryJson(ed)));
              }, {code::Refused}));
    r.Add(EditorCommand("undo.history", "The editor's undo history: step names, newest first, and what Redo would bring back.",
              {}, "{undo: [names], redo: [names], canUndo, canRedo, unsaved}", 0, [](Call&, EditorState& ed) {
                  ed.CaptureUnannouncedEdits();
                  return Result::Ok(HistoryJson(ed));
              }));
}

// ---- play.* (runtime testing) ----------------------------------------------------------------------------------------

void AddPlayCommands(Registry& r) {
    r.Add(EditorCommand("play.start", "Start Play as the editor's Play button does: the scene is put aside and runs as the game; play.stop brings it back unchanged.",
              {}, "{playing}", flag::Mutates, [](Call&, EditorState& ed) {
                  const std::string why = ed.PlayBlockedReason();
                  if (!why.empty()) return Result::Fail(code::Refused, why);
                  ed.Play();
                  if (!ed.isPlaying()) return Result::Fail(code::Failed, "Play did not start.");
                  return Result::Ok(Value::Obj().Set("playing", Value::Boolean(true)));
              }, {code::Refused, code::Failed}));
    r.Add(EditorCommand("play.stop", "Stop Play: the scene goes back to how it was before Play.", {}, "{playing}", flag::Mutates | flag::AllowInPlay | flag::NeedsPlay,
              [](Call&, EditorState& ed) {
                  ed.Stop();
                  return Result::Ok(Value::Obj().Set("playing", Value::Boolean(ed.isPlaying())));
              }));
    r.Add(EditorCommand("play.step", "Run frames of the playing game with scripted input held down (keys, mouse), through the editor's Play loop; then read the state with object.inspect, physics.bodies or log.read.",
              {ArgDefault("frames", ParamType::Integer, "How many frames (1 to 36000).", Value::Num(1)),
               ArgDefault("dt", ParamType::Number, "Seconds per frame.", Value::Num(1.0 / 60.0)),
               ArgDefault("keys", ParamType::Array, "Keys held for every frame: \"w\", \"space\", \"left\", \"shift\" ...", Value::Arr()),
               ArgDefault("tap", ParamType::Array, "Keys pressed for the first frame only.", Value::Arr()),
               Arg("mouse", ParamType::Object, "{\"x\":..,\"y\":..,\"buttons\":[0,1,2]}: the pointer in screen pixels and the buttons held.")},
              "{frames, time, quit}", flag::Mutates | flag::AllowInPlay | flag::NeedsPlay | flag::Slow, [](Call& c, EditorState& ed) {
                  const long long frames = c.Int("frames", 1);
                  const double dt = c.Num("dt", 1.0 / 60.0);
                  if (frames < 1 || frames > 36000) return Result::Fail(code::InvalidParams, "frames must be 1 to 36000.");
                  if (!(dt > 0.0 && dt <= 0.1)) return Result::Fail(code::InvalidParams, "dt must be above 0 and at most 0.1 seconds.");
                  auto keyList = [&](const char* name, std::vector<char>& out) -> std::string {
                      if (const Value* v = c.Get(name)) for (const Value& k : v->array) {
                          if (!k.IsString()) return std::string(name) + " holds key names (text).";
                          const char ch = okay::Input::IsKnownKeyName(k.string) ? okay::Input::KeyFromName(k.string) : 0;
                          if (!ch) return "'" + k.string + "' is not a key name (a-z, 0-9, space, enter, shift, up, f1 ...).";
                          out.push_back(ch);
                      }
                      return {};
                  };
                  std::vector<char> held, tap;
                  std::string bad = keyList("keys", held);
                  if (bad.empty()) bad = keyList("tap", tap);
                  if (!bad.empty()) return Result::Fail(code::InvalidParams, bad);
                  okay::Vec2 mouse{0, 0};
                  unsigned buttons = 0;
                  if (const Value* m = c.Get("mouse")) {
                      if (const Value* x = m->Get("x")) if (x->IsNumber()) mouse.x = (float)x->number;
                      if (const Value* y = m->Get("y")) if (y->IsNumber()) mouse.y = (float)y->number;
                      if (const Value* b = m->Get("buttons")) for (const Value& k : b->array) if (k.IsNumber() && k.number >= 0 && k.number <= 2) buttons |= 1u << (int)k.number;
                  }
                  long long ran = 0;
                  bool quit = false;
                  for (; ran < frames && ed.isPlaying(); ++ran) {
                      // The order PlayTest and the player use: feed every device, tick the action map, then the frame.
                      std::vector<char> down = held;
                      if (ran == 0) down.insert(down.end(), tap.begin(), tap.end());
                      okay::Input::ClearTypedText();
                      okay::Input::FeedKeys(down);
                      okay::Input::FeedMouse(mouse, buttons);
                      okay::InputActions::Tick();
                      if (!ed.PlayFrame(dt, false, 0.0f)) { quit = okay::Game::QuitRequested(); ++ran; break; }
                  }
                  okay::Input::FeedKeys({});
                  okay::Input::FeedMouse(mouse, 0);
                  return Result::Ok(Value::Obj().Set("frames", Value::Num((double)ran)).Set("time", Value::Num(okay::Time::ElapsedTimeD()))
                                        .Set("quit", Value::Boolean(quit)).Set("playing", Value::Boolean(ed.isPlaying())));
              }, {code::InvalidParams}));
    r.Add(EditorCommand("play.state", "Whether the game is playing or paused, its game time and frame count, and why Play is refused if it is.",
              {}, "{playing, paused, time, frames, playBlocked}", 0, [](Call&, EditorState& ed) {
                  return Result::Ok(Value::Obj().Set("playing", Value::Boolean(ed.isPlaying())).Set("paused", Value::Boolean(ed.isPlaying() && ed.paused()))
                                        .Set("time", Value::Num(okay::Time::ElapsedTimeD())).Set("frames", Value::Num((double)okay::Time::FrameCount()))
                                        .Set("playBlocked", Value::Str(ed.PlayBlockedReason())));
              }));
}

// ---- model.* -----------------------------------------------------------------------------------------------------------

okay::MeshRenderer* MeshOf(Call& c, EditorState& ed, Error& e, GameObject** out = nullptr) {
    GameObject* g = Find(c, ed, "object", e);
    if (!g) return nullptr;
    if (out) *out = g;
    auto* mr = g->GetComponent<okay::MeshRenderer>();
    if (!mr) e = {code::NotFound, g->name + " has no Mesh Renderer.", {}};
    return mr;
}

Value MeshJson(const okay::Mesh& m) {
    const auto topo = m.Topology();
    return Value::Obj().Set("vertices", Value::Num((double)m.vertices.size())).Set("triangles", Value::Num(m.TriangleCount()))
        .Set("openEdges", Value::Num((double)topo.boundaryEdges)).Set("name", Value::Str(m.name));
}

std::vector<int> IntList(Call& c, const char* name) {
    std::vector<int> v;
    if (const Value* a = c.Get(name)) for (const Value& x : a->array) if (x.IsNumber()) v.push_back((int)x.number);
    return v;
}

void AddModelCommands(Registry& r) {
    r.Add(EditorCommand("model.ops", "List the modeling operations model.apply runs: the Modeling window's tools and its detail / pivot buttons.",
              {}, "{ops: [{id, label, needs, about}]}", 0, [](Call&, EditorState&) {
                  Value ops = Value::Arr();
                  okay::Mesh probe = okay::Mesh::FromName("Cube");
                  for (const auto& t : oked::easymodel::Tools()) {
                      okay::Mesh m = probe;
                      oked::easymodel::ToolArgs a;
                      a.faces = {0};
                      a.corners = {0};
                      if (!oked::easymodel::RunMeshTool(m, t.id, a).handled) continue;   // tools the window runs itself (shapes, gizmos, save)
                      const char* needs = t.needs == oked::easymodel::Needs::Faces ? "faces" : t.needs == oked::easymodel::Needs::Corners ? "corners" : "model";
                      ops.Push(Value::Obj().Set("id", Value::Str(t.id)).Set("label", Value::Str(t.name)).Set("needs", Value::Str(needs)).Set("about", Value::Str(t.tip)));
                  }
                  for (int i = 0; i < (int)okay::modeling::ShapeOp::Count; ++i) {
                      const auto op = (okay::modeling::ShapeOp)i;
                      std::string id = okay::modeling::ShapeOpLabel(op);
                      for (char& ch : id) ch = ch == ' ' ? '_' : (char)std::tolower((unsigned char)ch);
                      ops.Push(Value::Obj().Set("id", Value::Str("shape." + id)).Set("label", Value::Str(okay::modeling::ShapeOpLabel(op)))
                                   .Set("needs", Value::Str("model")).Set("about", Value::Str(okay::modeling::ShapeOpTip(op))));
                  }
                  return Result::Ok(Value::Obj().Set("ops", std::move(ops)));
              }));
    r.Add(EditorCommand("model.apply", "Run a modeling operation on an object's mesh (model.ops lists them: smooth, push.out, round.it, shape.subdivide ...), as one undo step.",
              {kObject, Arg("op", ParamType::String, "The operation's id.", true),
               ArgDefault("faces", ParamType::Array, "Triangle numbers for the face tools.", Value::Arr()),
               ArgDefault("corners", ParamType::Array, "Vertex numbers for the corner tools.", Value::Arr()),
               ArgDefault("strength", ParamType::Number, "0.1 to 1, for smooth and round.", Value::Num(0.5)),
               Arg("color", ParamType::Object, "{r,g,b} for the paint tools.")},
              "{op, changed, message, mesh: {vertices, triangles, openEdges}}", flag::Undoable, [](Call& c, EditorState& ed) {
                  Error e;
                  okay::MeshRenderer* mr = MeshOf(c, ed, e);
                  if (!mr) return Fail(e);
                  const std::string op = c.Str("op");
                  const okay::Mesh before = mr->mesh;
                  std::string message;
                  if (op.rfind("shape.", 0) == 0) {
                      bool found = false;
                      for (int i = 0; i < (int)okay::modeling::ShapeOp::Count && !found; ++i) {
                          std::string id = okay::modeling::ShapeOpLabel((okay::modeling::ShapeOp)i);
                          for (char& ch : id) ch = ch == ' ' ? '_' : (char)std::tolower((unsigned char)ch);
                          if ("shape." + id != op) continue;
                          found = true;
                          message = okay::modeling::ApplyShapeOp(mr->mesh, (okay::modeling::ShapeOp)i, IntList(c, "faces"));
                      }
                      if (!found) return Result::Fail(code::NotFound, "There is no operation '" + op + "' (model.ops lists them).");
                  } else {
                      oked::easymodel::ToolArgs a;
                      a.faces = IntList(c, "faces");
                      a.corners = IntList(c, "corners");
                      a.strength = (float)c.Num("strength", 0.5);
                      if (const Value* col = c.Get("color")) {
                          if (const Value* v = col->Get("r")) if (v->IsNumber()) a.r = (float)v->number;
                          if (const Value* v = col->Get("g")) if (v->IsNumber()) a.g = (float)v->number;
                          if (const Value* v = col->Get("b")) if (v->IsNumber()) a.b = (float)v->number;
                      }
                      const oked::easymodel::ToolResult tr = oked::easymodel::RunMeshTool(mr->mesh, op, a);
                      if (!tr.handled) return Result::Fail(code::NotFound, "There is no operation '" + op + "' (model.ops lists them).");
                      if (!tr.ok) { mr->mesh = before; return Result::Fail(code::Failed, tr.message.empty() ? "The operation did not work on this mesh." : tr.message); }
                      message = tr.message;
                  }
                  mr->modifiedMesh.reset();   // the drawn mesh is rebuilt from the edited one
                  const bool changed = !okay::modeling::SameMesh(before, mr->mesh);
                  return Result::Ok(Value::Obj().Set("op", Value::Str(op)).Set("changed", Value::Boolean(changed)).Set("message", Value::Str(message))
                                        .Set("mesh", MeshJson(mr->mesh)));
              }, {code::NotFound, code::Failed}));
    r.Add(EditorCommand("model.info", "An object's mesh: vertices, triangles, open edges, bounds and colours.", {kObject},
              "{vertices, triangles, openEdges, name, boundsMin, boundsMax, faceColors}", 0, [](Call& c, EditorState& ed) {
                  Error e;
                  okay::MeshRenderer* mr = MeshOf(c, ed, e);
                  if (!mr) return Fail(e);
                  Value v = MeshJson(mr->mesh);
                  okay::Vec3 lo, hi;
                  mr->mesh.Bounds(lo, hi);
                  v.Set("boundsMin", okay::automation::Vec3Json(lo.x, lo.y, lo.z)).Set("boundsMax", okay::automation::Vec3Json(hi.x, hi.y, hi.z));
                  v.Set("faceColors", Value::Boolean(mr->mesh.HasFaceColors()));
                  return Result::Ok(std::move(v));
              }, {code::NotFound}));
}

// ---- ui.* --------------------------------------------------------------------------------------------------------------

void AddUiCommands(Registry& r) {
    r.Add(EditorCommand("ui.tree", "The screen-space UI widgets in the scene with the rectangle each one takes on a screen of the given size (the editor's UI selection uses the same layout).",
              {ArgDefault("width", ParamType::Number, "Screen width in pixels.", Value::Num(1920)), ArgDefault("height", ParamType::Number, "Screen height in pixels.", Value::Num(1080))},
              "{widgets: [{id, name, path, type, x, y, w, h, active}]}", 0, [](Call& c, EditorState& ed) {
                  const float w = (float)c.Num("width", 1920), h = (float)c.Num("height", 1080);
                  Value out = Value::Arr();
                  for (const auto& up : ed.scene().Objects()) {
                      GameObject* g = up.get();
                      if (!g || g->IsDestroyed() || g->transient) continue;
                      if (!okay::GetUIRect(g).valid) continue;
                      okay::Vec2 o, s;
                      if (!okay::GetUIScreenRect(g, w, h, o, s)) continue;
                      std::string type;
                      for (const auto& k : g->Components()) {
                          if (!k) continue;
                          auto info = okay::PropertyRegistry::FindFor(*k);
                          if (info && info->name.rfind("UI", 0) == 0) { type = info->name; break; }
                      }
                      out.Push(Value::Obj().Set("id", Value::Str(g->Guid())).Set("name", Value::Str(g->name)).Set("path", Value::Str(g->Path()))
                                   .Set("type", Value::Str(type)).Set("x", Value::Num(o.x)).Set("y", Value::Num(o.y)).Set("w", Value::Num(s.x)).Set("h", Value::Num(s.y))
                                   .Set("active", Value::Boolean(g->ActiveInHierarchy())));
                  }
                  return Result::Ok(Value::Obj().Set("widgets", std::move(out)));
              }));
    r.Add(EditorCommand("ui.create", "Create a UI widget object (a UI component type such as UIButton, UIPanel, UIImage, UISlider, UIToggle, UIText), optionally with its text and place, as one undo step.",
              {Arg("type", ParamType::String, "The UI component type (component.types with filter \"UI\" lists them).", true),
               Arg("name", ParamType::String, "The object's name (default: the type)."), Arg("parent", ParamType::String, "Put it under this object (a panel)."),
               Arg("text", ParamType::String, "Its text (the widget's text or label property)."),
               Arg("position", ParamType::Vec3, "Its offset in pixels (x, y)."), Arg("size", ParamType::Vec3, "Its size in pixels (x, y).")},
              "{id, name, path, components, set: [property paths written]}", flag::Undoable, [](Call& c, EditorState& ed) {
                  auto info = okay::PropertyRegistry::FindByName(c.Str("type"));
                  if (!info || !info->add) return Result::Fail(code::NotFound, "There is no UI component type '" + c.Str("type") + "' that can be added.");
                  if (info->name.rfind("UI", 0) != 0 && info->category.find("UI") == std::string::npos)
                      return Result::Fail(code::InvalidParams, info->name + " is not a UI widget type.");
                  GameObject* parent = nullptr;
                  if (c.Has("parent")) {
                      Error e;
                      parent = Find(c, ed, "parent", e);
                      if (!parent) return Fail(e);
                  }
                  GameObject* go = ed.CreateEmpty(c.Str("name", info->name));
                  if (!go || !info->add(*go)) return Result::Fail(code::Failed, info->name + " could not be added.");
                  if (parent && go->transform) go->transform->SetParent(parent->transform, false);
                  Value set = Value::Arr();
                  auto write = [&](const char* prop, const std::string& text) {
                      if (!info->Find(prop)) return;
                      const auto w = okay::PropertyRegistry::WritePropertyFromText(*go, info->name + "." + prop, text);
                      if (w.ok) set.Push(Value::Str(w.path));
                  };
                  if (c.Has("text")) for (const char* prop : {"text", "label", "placeholder"}) if (info->Find(prop)) { write(prop, c.Str("text")); break; }
                  float f[3];
                  if (c.Has("position")) { c.Vec("position", f); write("position", std::to_string(f[0]) + " " + std::to_string(f[1])); }
                  if (c.Has("size")) { c.Vec("size", f); write("size", std::to_string(f[0]) + " " + std::to_string(f[1])); }
                  ed.Select(go);
                  Value v = okay::automation::ObjectSummary(*go);
                  v.Set("set", std::move(set));
                  return Result::Ok(std::move(v));
              }, {code::NotFound, code::Failed}));
}

void AddEditorCommands(Registry& r) {
    AddSceneCommands(r);
    AddObjectCommands(r);
    AddUndoCommands(r);
    AddPlayCommands(r);
    AddModelCommands(r);
    AddUiCommands(r);
    AddVsCommands(r);
}

} // namespace

void RegisterEditorCommands() { okay::automation::AddProvider("editor", &AddEditorCommands); }

// ---- The link ------------------------------------------------------------------------------------------------------

#if defined(OKAY_HAVE_CLI_PANEL)
}  // namespace oked::automation
namespace okaycli { void AddAutomationCommands(okay::automation::Registry& r); }   // tools/okay-cli/AutomationCli.cpp
namespace oked::automation {
#endif

namespace {
struct Link {
    okay::automation::LinkServer server;
    std::map<int, std::unique_ptr<okay::automation::McpServer>> sessions;
    std::unique_ptr<EditorHost> host;
    okay::editor::EditorState* ed = nullptr;
    bool loaded = false;
    bool flag = false;
    bool savedFlag = false;
    std::string error;
};
Link& TheLink() { static Link l; return l; }

std::string FlagFile() { return okay::edtabs::UserFile("automation_link.txt"); }
}

bool& LinkFlag() {
    Link& l = TheLink();
    if (!l.loaded) {
        l.loaded = true;
        if (!okay::edtabs::UserFilesDir().empty()) {   // only the editor has a per-user folder; tools and tests keep nothing
            std::ifstream f(FlagFile());
            std::string word;
            l.flag = (f >> word) && word == "on";
        }
        l.savedFlag = l.flag;
    }
    return l.flag;
}

void LinkFrame(EditorState& ed) {
    Link& l = TheLink();
    const bool want = LinkFlag();
    if (want != l.savedFlag) {   // remember the choice for the next start
        if (!okay::edtabs::UserFilesDir().empty()) std::ofstream(FlagFile(), std::ios::trunc) << (want ? "on\n" : "off\n");
        l.savedFlag = want;
    }
    if (!LinkPoll(ed, want) && want && !l.error.empty()) l.flag = false;   // the menu shows it off again; the log says why
}

bool LinkPoll(EditorState& ed, bool on, const std::string& kind) {
    Link& l = TheLink();
    if (!on) {
        if (l.server.Running()) { l.server.Stop(); l.sessions.clear(); okay::Log::Info("Automation link off."); }
        return false;
    }
    if (!l.server.Running() || l.ed != &ed) {
        l.server.Stop();
        l.sessions.clear();
        RegisterEditorCommands();
#if defined(OKAY_HAVE_CLI_PANEL)
        okay::automation::AddProvider("cli", &okaycli::AddAutomationCommands);
#endif
        okay::automation::InstallLogRing();
        l.ed = &ed;
        l.host = std::make_unique<EditorHost>(ed, kind);
        l.server.onClose = [&l](int id) { l.sessions.erase(id); };
        std::string err;
        if (!l.server.Start(okay::automation::LinkInfoPath(), ed.projectDir(), &err)) {
            if (err != l.error) okay::Log::Warning("Automation link: ", err);
            l.error = err;
            return false;
        }
        l.error.clear();
        okay::Log::Info("Automation link on: 127.0.0.1 port ", l.server.Port(), ", details in ", l.server.InfoPath(), " (this user only).");
    }
    l.server.Poll([&l](int id, const std::string& line) {
        auto& s = l.sessions[id];
        if (!s) {
            okay::automation::McpOptions o;
            o.serverName = "okay-editor";
            s = std::make_unique<okay::automation::McpServer>(okay::automation::Commands(), *l.host, o);
        }
        return s->Handle(line);
    });
    return true;
}

std::string LinkStatus() {
    Link& l = TheLink();
    if (!l.server.Running()) return l.error.empty() ? std::string() : "Automation link off: " + l.error;
    const int n = l.server.Clients();
    return "Automation link on (port " + std::to_string(l.server.Port()) + ", " + std::to_string(n) + (n == 1 ? " client)" : " clients)");
}

void LinkShutdown() {
    Link& l = TheLink();
    l.server.Stop();
    l.sessions.clear();
}

} // namespace oked::automation
