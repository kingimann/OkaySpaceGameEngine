// Automation (okay/Automation/*.hpp, editor/AutomationEditor.hpp): the command table, its checks and structured errors,
// the MCP server (JSON-RPC 2.0) in process, the editor link (127.0.0.1, token), and for every command group a check that
// it reaches the real engine or editor system (the scene text, the editor's undo history, the property table, the
// script bindings, physics, animation, the software renderer, modeling, UI layout, visual script files, the log).
// The end-to-end run through a real `okay mcp` process is tests/scripts/test_automation_mcp.py.
#include "test_framework.hpp"

#include "AutomationEditor.hpp"
#include "AutomationOps.hpp"
#include "ClaudeConnect.hpp"
#include "HierarchyContextModel.hpp"
#include "MenuBarModel.hpp"
#include "UIMenuModel.hpp"
#include "MenuActions.hpp"
#include "okay/Automation/Automation.hpp"
#include "okay/Automation/Link.hpp"
#include "okay/Automation/Mcp.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/Json.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okayscript/Runtime.hpp"   // okayscript::StepBudget

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
using namespace okay;
using automation::Result;
using json::Value;

namespace {

/// A scratch folder for the files the commands write; main() removes it.
struct TempDir {
    std::string root;
    std::string Sub(const std::string& name) const { return root + "/" + name; }
};
TempDir MakeScratch() {
    std::error_code ec;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    // Forward slashes, so a path can go into a JSON string as it is on Windows too.
    TempDir t{(fs::temp_directory_path(ec) / ("okay_automation_" + std::to_string((long long)stamp))).generic_string()};
    fs::create_directories(t.root, ec);
    return t;
}

Value J(const std::string& text) {
    Value v;
    std::string why;
    if (!json::Parse(text, v, &why)) { std::cout << "  bad JSON in the test: " << why << "\n"; }
    return v;
}

/// A host with a bare scene, for the table's own checks.
struct TestHost : automation::Host {
    Scene scene_{"Test"};
    bool playing = false, editor = false;
    int begins = 0, ends = 0;
    bool lastOk = false;
    Scene* scene() override { return &scene_; }
    bool isPlaying() const override { return playing; }
    bool hasEditor() const override { return editor; }
    void BeginEdit(const std::string&) override { ++begins; }
    void EndEdit(bool ok) override { ++ends; lastOk = ok; }
};

automation::Command Simple(const char* name, unsigned flags, automation::Handler h) {
    automation::Command c;
    c.name = name;
    c.summary = "A test command.";
    c.flags = flags;
    c.handler = std::move(h);
    return c;
}

void TestJsonWriter() {
    RUN_SUITE("JSON writer: Write reads back with Parse, numbers and escapes as RFC 8259 writes them");
    Value v = Value::Obj();
    v.Set("a", Value::Num(3)).Set("b", Value::Num(-0.5)).Set("c", Value::Str("line\n\"q\"\\ \x01")).Set("d", Value::Boolean(true));
    v.Set("e", Value::Arr().Push(Value::Num(1e21)).Push(Value()).Push(Value::Num(NAN)));
    const std::string text = json::Write(v);
    CHECK(text.find("\"a\":3,") != std::string::npos);
    CHECK(text.find("\\n\\\"q\\\"\\\\ \\u0001") != std::string::npos);
    CHECK(text.find("null,null") != std::string::npos);   // the NaN has no JSON form
    Value back;
    CHECK(json::Parse(text, back));
    CHECK(back.Get("b") && back.Get("b")->number == -0.5);
    CHECK(back.Get("c") && back.Get("c")->string == "line\n\"q\"\\ \x01");
    CHECK(back.Get("e") && back.Get("e")->array.size() == 3 && back.Get("e")->array[0].number == 1e21);
    v.Set("a", Value::Num(4));   // Set replaces
    CHECK(v.object.size() == 5 && v.Get("a")->number == 4);
    double third = 1.0 / 3.0;
    Value t;
    CHECK(json::Parse(json::Write(Value::Num(third)), t) && t.number == third);   // the shortest text that reads back the same
}

void TestRegistry() {
    RUN_SUITE("Registry: names, schemas, parameter checks, structured errors, one edit bracket per undoable command");
    automation::Registry r;
    int ran = 0;
    automation::Command c = Simple("test.echo", 0, [&ran](automation::Call& call) {
        ++ran;
        float v[3];
        call.Vec("where", v, 9, 9, 9);
        return Result::Ok(Value::Obj().Set("text", Value::Str(call.Str("text", "none"))).Set("x", Value::Num(v[0])));
    });
    automation::Param p;
    p.name = "text"; p.type = automation::ParamType::String; p.required = true; p.options = {"a", "b"};
    c.params.push_back(p);
    p = automation::Param(); p.name = "where"; p.type = automation::ParamType::Vec3;
    c.params.push_back(p);
    CHECK(r.Add(c));
    CHECK(!r.Add(c));                                                        // the name is taken
    CHECK(r.Add(Simple("test.echo2", 0, c.handler)));
    CHECK(!r.Add(Simple("Test.Bad", 0, c.handler)));                         // upper case
    CHECK(!r.Add(Simple("nodot", 0, c.handler)));                            // no namespace
    CHECK(!r.Add(Simple("a..b", 0, c.handler)));
    CHECK(!r.Add(Simple("test.nohandler", 0, nullptr)));
    automation::Command clash = Simple("test.echo2", 0, c.handler);
    clash.name = "test_echo2.z";
    CHECK(r.Add(clash));   // test_echo2_z differs from test_echo2
    automation::Command same = Simple("test_echo.z", 0, c.handler);
    automation::Command same2 = Simple("test.echo_z", 0, c.handler);
    CHECK(r.Add(same));
    CHECK(!r.Add(same2));  // both would be the MCP tool test_echo_z
    automation::Command dupParam = Simple("test.dup", 0, c.handler);
    dupParam.params = {p, p};
    CHECK(!r.Add(dupParam));

    const Value schema = automation::Registry::InputSchema(*r.Find("test.echo"));
    CHECK(schema.Get("type")->string == "object");
    CHECK(schema.Get("required")->array.size() == 1 && schema.Get("required")->array[0].string == "text");
    CHECK(schema.Get("properties")->Get("text")->Get("enum")->array.size() == 2);
    CHECK(schema.Get("properties")->Get("where")->Get("required")->array.size() == 3);
    CHECK(schema.Get("additionalProperties") && !schema.Get("additionalProperties")->boolean);
    const Value d = automation::Registry::Describe(*r.Find("test.echo"));
    CHECK(d.Get("params")->array.size() == 2 && d.Get("errors")->array.size() >= 2);

    TestHost h;
    Result res = r.Run(h, "test.echo", J(R"({"text":"a","where":[1,2,3]})"));
    CHECK(res.ok && res.data.Get("text")->string == "a" && res.data.Get("x")->number == 1);
    res = r.Run(h, "test.echo", J(R"({"text":"b","where":{"x":4,"y":5,"z":6}})"));
    CHECK(res.ok && res.data.Get("x")->number == 4);
    const int before = ran;
    res = r.Run(h, "test.nothing", Value());
    CHECK(!res.ok && res.error.code == automation::code::UnknownCommand);
    CHECK(res.error.details.Get("similar") && res.error.details.Get("similar")->array.size() >= 2);
    res = r.Run(h, "test.echo", Value());
    CHECK(!res.ok && res.error.code == automation::code::InvalidParams && res.error.message.find("'text'") != std::string::npos);
    res = r.Run(h, "test.echo", J(R"({"text":"c"})"));
    CHECK(!res.ok && res.error.code == automation::code::InvalidParams && res.error.message.find("one of") != std::string::npos);
    res = r.Run(h, "test.echo", J(R"({"text":"a","where":[1,2]})"));
    CHECK(!res.ok && res.error.code == automation::code::InvalidParams);
    res = r.Run(h, "test.echo", J(R"({"text":"a","colour":1})"));
    CHECK(!res.ok && res.error.message.find("'colour' is not a parameter") != std::string::npos);
    res = r.Run(h, "test.echo", J(R"([1])"));
    CHECK(!res.ok && res.error.code == automation::code::InvalidParams);
    CHECK(ran == before);   // none of the refused calls reached the handler

    CHECK(r.Add(Simple("test.edit", automation::flag::Undoable, [](automation::Call&) { return Result::Ok(); })));
    CHECK(r.Add(Simple("test.throws", automation::flag::Undoable, [](automation::Call&) -> Result { throw std::runtime_error("boom"); })));
    CHECK(r.Add(Simple("test.needs_editor", automation::flag::NeedsEditor, [](automation::Call&) { return Result::Ok(); })));
    CHECK(r.Find("test.edit")->flags & automation::flag::Mutates);   // Undoable implies Mutates
    res = r.Run(h, "test.edit", Value());
    CHECK(res.ok && h.begins == 1 && h.ends == 1 && h.lastOk);
    res = r.Run(h, "test.throws", Value());
    CHECK(!res.ok && res.error.code == automation::code::Internal && res.error.message.find("boom") != std::string::npos);
    CHECK(h.begins == 2 && h.ends == 2 && !h.lastOk);   // the edit bracket closes even when the handler throws
    res = r.Run(h, "test.needs_editor", Value());
    CHECK(!res.ok && res.error.code == automation::code::Unavailable);
    h.playing = true;
    res = r.Run(h, "test.edit", Value());
    CHECK(!res.ok && res.error.code == automation::code::Refused && h.begins == 2);
    h.playing = false;
    const Value out = res.ToJson();
    CHECK(out.Get("ok") && !out.Get("ok")->boolean && out.Get("error")->Get("code")->string == "refused");

    // The process table: every command is well formed and describable, with a plain summary.
    oked::automation::RegisterEditorCommands();
    automation::Registry& all = automation::Commands();
    CHECK(all.Size() >= 60);
    for (const automation::Command* k : all.List()) {
        if (k->summary.size() < 10 || k->result.empty()) { std::cout << "  thin command: " << k->name << "\n"; CHECK(false); }
        const Value desc = automation::Registry::Describe(*k);
        CHECK(desc.Get("inputSchema") && desc.Get("inputSchema")->Get("properties"));
    }
    for (const char* need : {"scene.hierarchy", "object.create", "component.set", "component.call", "script.call", "undo.undo",
                             "play.step", "viewport.screenshot", "model.apply", "vs.connect", "ui.create", "physics.raycast",
                             "anim.sample", "log.read", "prefab.instantiate", "api.functions"})
        if (!all.Find(need)) { std::cout << "  missing: " << need << "\n"; CHECK(false); }
}

/// Run a command on the editor host, report a failure in the test output.
struct Driver {
    okay::editor::EditorState ed;
    oked::automation::EditorHost host{ed, "headless"};
    Result Run(const std::string& name, const std::string& params = "{}") {
        Result r = automation::Commands().Run(host, name, J(params));
        if (!r.ok) std::cout << "  (" << name << ": " << r.error.code << " " << r.error.message << ")\n";
        return r;
    }
    const Value* D(const Result& r, const char* k) { return r.data.Get(k); }
};

void TestSceneRoundTrip(Driver& d, const TempDir& tmp) {
    RUN_SUITE("Scene round trip through the editor's state: create, add, set, inspect, undo, redo, save, reload");
    CHECK(d.Run("scene.new", R"({"template":"empty","discard":true})").ok);
    Result r = d.Run("object.create", R"({"kind":"cube","name":"Box","position":{"x":1,"y":2,"z":3}})");
    CHECK(r.ok);
    const std::string id = r.ok ? r.data.Get("id")->string : std::string();
    GameObject* box = d.ed.scene().FindByGuid(id);
    CHECK(box && box->GetComponent<MeshRenderer>() && box->transform->localPosition.y == 2.0f);
    CHECK(d.ed.selected() == box);
    CHECK(d.ed.UndoDepth() == 1);   // one command, one step
    CHECK(d.Run("component.add", R"({"object":"Box","type":"Light"})").ok);
    CHECK(d.ed.scene().FindByGuid(id)->GetComponent<Light>() != nullptr);
    r = d.Run("component.set", R"({"object":"Box","path":"Light.intensity","value":3.5})");
    CHECK(r.ok && r.data.Get("value")->number == 3.5);
    CHECK(PropertyRegistry::GetProperty(*d.ed.scene().FindByGuid(id), "Light.intensity").AsFloat() == 3.5f);
    r = d.Run("component.set", R"({"object":"Box","path":"Light.color","value":{"r":1,"g":0,"b":0,"a":1}})");
    CHECK(r.ok);
    r = d.Run("component.get", R"({"object":"Box","path":"Light.intensity"})");
    CHECK(r.ok && r.data.Get("value")->number == 3.5);
    r = d.Run("object.inspect", "{\"object\":\"" + id + "\"}");
    CHECK(r.ok && r.data.Get("components")->array.size() >= 3);
    bool sawLight = false;
    if (r.ok) for (const Value& c : r.data.Get("components")->array)
        if (c.Get("type")->string == "Light") sawLight = c.Get("properties") && c.Get("properties")->Get("intensity") && c.Get("properties")->Get("intensity")->number == 3.5;
    CHECK(sawLight);
    CHECK(d.ed.UndoDepth() == 4);
    const std::string afterEdits = SceneSerializer::Serialize(d.ed.scene());

    r = d.Run("undo.undo", R"({"steps":2})");   // the colour and the intensity
    CHECK(r.ok && r.data.Get("undone")->number == 2);
    GameObject* again = d.ed.scene().FindByGuid(id);   // undo rebuilt the scene: same GUID, new object
    CHECK(again && again->GetComponent<Light>() && std::fabs(PropertyRegistry::GetProperty(*again, "Light.intensity").AsFloat() - 3.5f) > 0.01f);
    r = d.Run("undo.history");
    CHECK(r.ok && r.data.Get("redo")->array.size() == 2);
    CHECK(d.Run("undo.redo", R"({"steps":2})").ok);
    CHECK(SceneSerializer::Serialize(d.ed.scene()) == afterEdits);

    r = d.Run("transform.set", R"({"object":"Box","position":{"x":0,"y":5,"z":0},"rotation":[0,90,0]})");
    CHECK(r.ok && std::fabs(r.data.Get("rotation")->Get("y")->number - 90) < 0.01);
    r = d.Run("component.call", R"({"object":"Box","component":"Transform","method":"Translate","args":[{"x":1,"y":0,"z":0}]})");
    CHECK(r.ok);
    r = d.Run("transform.get", R"({"object":"Box"})");
    CHECK(r.ok && std::fabs(r.data.Get("worldPosition")->Get("x")->number - 1.0) < 1e-4);
    r = d.Run("component.call", R"({"object":"Box","component":"Transform","method":"NoSuchMethod"})");
    CHECK(!r.ok && r.error.code == automation::code::NotFound);

    // Hierarchy: a child, reparent, duplicate, rename, delete.
    r = d.Run("object.create", R"({"kind":"empty","name":"Holder"})");
    CHECK(r.ok);
    CHECK(d.Run("object.reparent", R"({"object":"Box","parent":"Holder"})").ok);
    CHECK(d.ed.scene().FindByPath("Holder/Box") != nullptr);
    r = d.Run("object.reparent", R"({"object":"Holder","parent":"Holder/Box"})");
    CHECK(!r.ok && r.error.code == automation::code::Refused);
    r = d.Run("object.duplicate", R"({"object":"Holder/Box"})");
    CHECK(r.ok && r.data.Get("name")->string != "Box");
    const std::string copy = r.ok ? r.data.Get("id")->string : std::string();
    CHECK(d.Run("object.rename", "{\"object\":\"" + copy + "\",\"name\":\"Box2\"}").ok);
    r = d.Run("scene.hierarchy", R"({"deep":true})");
    int holderKids = -1, roots = 0;
    if (r.ok) for (const Value& o : r.data.Get("objects")->array) {
        ++roots;
        if (o.Get("name")->string == "Holder") holderKids = (int)o.Get("children")->array.size();
    }
    CHECK(roots == 2 && holderKids == 2);   // the new scene's Main Camera and Holder; Box and its copy under Holder
    r = d.Run("scene.find", R"({"component":"MeshRenderer"})");
    CHECK(r.ok && r.data.Get("count")->number == 2);
    CHECK(d.Run("object.delete", R"({"objects":["Box2"]})").ok);
    CHECK(d.ed.scene().Find("Box2") == nullptr);
    r = d.Run("object.inspect", R"({"object":"Nobody"})");
    CHECK(!r.ok && r.error.code == automation::code::NotFound);
    d.Run("object.create", R"({"kind":"empty","name":"Twin"})");
    d.Run("object.create", R"({"kind":"empty","name":"Twin"})");
    r = d.Run("object.inspect", R"({"object":"Twin"})");
    CHECK(!r.ok && r.error.code == automation::code::Ambiguous && r.error.details.Get("ids")->array.size() == 2);

    // Save, open something else, open it again: the same scene.
    const std::string path = tmp.Sub("round.okayscene");
    CHECK(d.Run("scene.save", "{\"path\":\"" + path + "\"}").ok && fs::exists(path));
    const std::string saved = SceneSerializer::Serialize(d.ed.scene());
    r = d.Run("scene.new", R"({"template":"empty"})");
    CHECK(r.ok);   // just saved: nothing to lose
    CHECK(d.ed.scene().Find("Holder") == nullptr);
    CHECK(d.Run("object.create", R"({"kind":"empty"})").ok);
    r = d.Run("scene.open", "{\"path\":\"" + path + "\"}");
    CHECK(!r.ok && r.error.code == automation::code::Refused);   // unsaved changes protected
    CHECK(d.Run("scene.open", "{\"path\":\"" + path + "\",\"discard\":true}").ok);
    CHECK(SceneSerializer::Serialize(d.ed.scene()) == saved);
    CHECK(d.ed.scene().FindByGuid(id) != nullptr);   // the GUIDs survive the file
    r = d.Run("scene.open", "{\"path\":\"" + tmp.Sub("missing.okayscene") + "\",\"discard\":true}");
    CHECK(!r.ok && r.error.code == automation::code::Failed && d.ed.scene().FindByGuid(id) != nullptr);
}

void TestScriptsAndReflection(Driver& d) {
    RUN_SUITE("Reflection: the shared script table and the generated bindings are reachable");
    Result r = d.Run("api.functions", R"({"filter":"volume"})");
    CHECK(r.ok && r.data.Get("count")->number >= 2);
    r = d.Run("api.classes");
    CHECK(r.ok && r.data.Get("count")->number >= 200);
    r = d.Run("component.describe", R"({"type":"Light"})");
    CHECK(r.ok && r.data.Get("properties")->array.size() >= 3 && r.data.Get("methods")->array.size() >= 1);
    r = d.Run("component.types", R"({"filter":"Rigidbody"})");
    CHECK(r.ok && r.data.Get("count")->number >= 2);
    CHECK(d.Run("script.call", R"({"function":"set_group_volume","args":["Music",0.25]})").ok);
    r = d.Run("script.call", R"({"function":"group_volume","args":["Music"]})");
    CHECK(r.ok && std::fabs(r.data.Get("value")->number - 0.25) < 1e-6);
    d.Run("script.call", R"({"function":"set_group_volume","args":["Music",1]})");
    d.Run("object.create", R"({"kind":"empty","name":"NoBody"})");
    r = d.Run("script.call", R"({"function":"add_force","object":"NoBody","args":[0,5,0]})");
    CHECK(!r.ok && r.error.code == automation::code::Failed && r.error.message.find("Rigidbody") != std::string::npos);
    r = d.Run("script.call", R"({"function":"no_such_function"})");
    CHECK(!r.ok && r.error.code == automation::code::NotFound);
}

void TestPhysicsAndPlay(Driver& d) {
    RUN_SUITE("Physics and Play: raycast, gravity as an undo step, a body falls while play.step runs, Stop restores it");
    CHECK(d.Run("scene.new", R"({"template":"empty","discard":true})").ok);
    CHECK(d.Run("object.create", R"({"kind":"cube","name":"Crate","position":{"x":0,"y":10,"z":0}})").ok);
    Result r = d.Run("physics.raycast", R"({"origin":{"x":0,"y":30,"z":0},"direction":{"x":0,"y":-1,"z":0}})");
    CHECK(r.ok && r.data.Get("hit")->boolean && r.data.Get("name")->string == "Crate");
    CHECK(r.ok && std::fabs(r.data.Get("point")->Get("y")->number - 10.5) < 0.05);
    r = d.Run("physics.raycast", R"({"origin":{"x":50,"y":30,"z":0},"direction":{"x":0,"y":-1,"z":0}})");
    CHECK(r.ok && !r.data.Get("hit")->boolean);
    r = d.Run("physics.overlap", R"({"center":{"x":0,"y":10,"z":0},"radius":1})");
    CHECK(r.ok && r.data.Get("count")->number == 1);
    const int depth = d.ed.UndoDepth();
    r = d.Run("physics.settings", R"({"gravity":{"x":0,"y":-20,"z":0}})");
    CHECK(r.ok && d.ed.scene().physics3D().gravity.y == -20.0f && d.ed.UndoDepth() == depth + 1);
    CHECK(d.Run("undo.undo").ok && std::fabs(d.ed.scene().physics3D().gravity.y + 9.81f) < 1e-4f);
    CHECK(d.Run("component.add", R"({"object":"Crate","type":"Rigidbody3D"})").ok);

    r = d.Run("play.step");
    CHECK(!r.ok && r.error.code == automation::code::Refused);   // not playing
    CHECK(d.Run("play.start").ok && d.ed.isPlaying());
    r = d.Run("component.set", R"({"object":"Crate","path":"Light.intensity","value":1})");
    CHECK(!r.ok && r.error.code == automation::code::Refused);   // edits refused while playing
    r = d.Run("play.step", R"({"frames":30,"keys":["w"],"tap":["space"]})");
    CHECK(r.ok && r.data.Get("frames")->number == 30);
    r = d.Run("physics.bodies");
    double y = 99;
    if (r.ok) for (const Value& b : r.data.Get("bodies")->array) if (b.Get("name")->string == "Crate") y = b.Get("position")->Get("y")->number;
    CHECK(y < 9.5);   // half a second of gravity
    r = d.Run("play.step", R"({"keys":["not a key"]})");
    CHECK(!r.ok && r.error.code == automation::code::InvalidParams);
    CHECK(d.Run("play.stop").ok && !d.ed.isPlaying());
    r = d.Run("transform.get", R"({"object":"Crate"})");
    CHECK(r.ok && std::fabs(r.data.Get("position")->Get("y")->number - 10.0) < 1e-4);   // Stop put the edit scene back
}

void TestAnimationModelUi(Driver& d) {
    RUN_SUITE("Animation, modeling and UI: keys and samples, a mesh tool as one undo step, a widget laid out on screen");
    CHECK(d.Run("scene.new", R"({"template":"empty","discard":true})").ok);
    CHECK(d.Run("object.create", R"({"kind":"cube","name":"Mover"})").ok);
    CHECK(d.Run("anim.set_key", R"({"object":"Mover","track":"position.x","time":0,"value":0})").ok);
    CHECK(d.Run("anim.set_key", R"({"object":"Mover","track":"position.x","time":2,"value":10})").ok);
    Result r = d.Run("anim.sample", R"({"object":"Mover","time":1})");
    CHECK(r.ok && std::fabs(r.data.Get("position")->Get("x")->number - 5.0) < 1.0);
    r = d.Run("anim.list");
    CHECK(r.ok && r.data.Get("clips")->array.size() == 1 && r.data.Get("clips")->array[0].Get("length")->number == 2);

    GameObject* mover = d.ed.scene().Find("Mover");
    const int tris = mover->GetComponent<MeshRenderer>()->mesh.TriangleCount();
    r = d.Run("model.ops");
    CHECK(r.ok && r.data.Get("ops")->array.size() >= 10);
    const std::string plain = SceneSerializer::Serialize(d.ed.scene());
    r = d.Run("model.apply", R"({"object":"Mover","op":"shape.subdivide"})");
    CHECK(r.ok && r.data.Get("changed")->boolean && r.data.Get("mesh")->Get("triangles")->number == tris * 4);
    CHECK(SceneSerializer::Serialize(d.ed.scene()) != plain);   // the edited mesh is part of the scene (saved, and undone with it)
    CHECK(d.Run("undo.undo").ok);
    CHECK(d.ed.scene().Find("Mover")->GetComponent<MeshRenderer>()->mesh.TriangleCount() == tris);   // the mesh is in the scene text
    r = d.Run("model.apply", R"({"object":"Mover","op":"push.out"})");
    CHECK(!r.ok && r.error.code == automation::code::Failed);   // needs faces
    CHECK(d.Run("model.apply", R"({"object":"Mover","op":"push.out","faces":[0]})").ok);
    r = d.Run("model.info", R"({"object":"Mover"})");
    CHECK(r.ok && r.data.Get("triangles")->number > tris);

    r = d.Run("ui.create", R"({"type":"UIButton","name":"Play","text":"Play","position":{"x":40,"y":60,"z":0},"size":{"x":200,"y":50,"z":0}})");
    CHECK(r.ok && r.data.Get("set")->array.size() >= 1);
    r = d.Run("ui.tree", R"({"width":1280,"height":720})");
    bool found = false;
    if (r.ok) for (const Value& w : r.data.Get("widgets")->array)
        if (w.Get("name")->string == "Play") found = w.Get("w")->number > 1 && w.Get("type")->string == "UIButton";
    CHECK(found);
    r = d.Run("ui.create", R"({"type":"Light"})");
    CHECK(!r.ok);
}

void TestScreenshotLogValidate(Driver& d, const TempDir& tmp) {
    RUN_SUITE("Screenshot from the software renderer, the log ring, the Problems checks");
    CHECK(d.Run("scene.new", R"({"template":"3d","discard":true})").ok);
    const std::string png = tmp.Sub("shot.png");
    Result r = d.Run("viewport.screenshot", "{\"width\":160,\"height\":90,\"path\":\"" + png + "\",\"inline\":true}");
    CHECK(r.ok && r.data.Get("width")->number == 160 && r.data.Get("height")->number == 90);
    CHECK(r.ok && r.data.Get("drawnPixels")->number > 100);   // the ground and the cube are there
    CHECK(r.attachments.size() == 1 && r.attachments[0].mimeType == "image/png" && r.attachments[0].base64.size() > 100);
    std::ifstream f(png, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(bytes.size() > 33 && bytes.compare(1, 3, "PNG") == 0);
    if (bytes.size() > 24) {   // IHDR width and height, big-endian
        auto be = [&](std::size_t at) { return ((unsigned char)bytes[at] << 24) | ((unsigned char)bytes[at + 1] << 16) | ((unsigned char)bytes[at + 2] << 8) | (unsigned char)bytes[at + 3]; };
        CHECK(be(16) == 160 && be(20) == 90);
    }
    d.ed.view3D = true;
    r = d.Run("viewport.screenshot", "{\"view\":\"editor\",\"width\":64,\"height\":64,\"path\":\"" + tmp.Sub("editor.png") + "\"}");
    CHECK(r.ok && r.data.Get("drawnPixels")->number > 0);
    r = d.Run("viewport.screenshot", "{\"view\":\"custom\",\"eye\":{\"x\":0,\"y\":500,\"z\":0.01},\"target\":{\"x\":0,\"y\":0,\"z\":0},"
                                     "\"width\":32,\"height\":32,\"path\":\"" + tmp.Sub("top.png") + "\"}");
    CHECK(r.ok);
    r = d.Run("viewport.screenshot", R"({"camera":"Nobody"})");
    CHECK(!r.ok && r.error.code == automation::code::NotFound);

    Log::Warning("automation test marker 4711");
    r = d.Run("log.read", R"({"level":"warning"})");
    bool seen = false;
    std::uint64_t last = 0;
    if (r.ok) { for (const Value& l : r.data.Get("lines")->array) if (l.Get("text")->string.find("marker 4711") != std::string::npos) seen = true; last = (std::uint64_t)r.data.Get("last")->number; }
    CHECK(seen && last > 0);
    r = d.Run("log.read", "{\"after\":" + std::to_string(last) + "}");
    CHECK(r.ok && r.data.Get("lines")->array.empty());
    r = d.Run("scene.validate");
    CHECK(r.ok && r.data.Get("ok"));
    r = d.Run("automation.host");
    CHECK(r.ok && r.data.Get("kind")->string == "headless" && r.data.Get("editor")->boolean);
}

void TestVisualScripts(Driver& d, const TempDir& tmp) {
    RUN_SUITE("Visual scripts: a graph made, wired and checked through the editor's model; Undo puts the file back");
    const std::string g = tmp.Sub("Mover.okayvs");
    Result r = d.Run("vs.create", "{\"graph\":\"" + g + "\"}");
    CHECK(r.ok && fs::exists(g));
    r = d.Run("vs.add_node", "{\"graph\":\"" + g + "\",\"type\":\"OnStart\"}");
    CHECK(r.ok);
    const int start = r.ok ? (int)r.data.Get("node")->number : -1;
    r = d.Run("vs.add_node", "{\"graph\":\"" + g + "\",\"type\":\"Print\"}");
    const int print = r.ok ? (int)r.data.Get("node")->number : -1;
    r = d.Run("vs.add_node", "{\"graph\":\"" + g + "\",\"type\":\"Const\",\"arg\":\"5\"}");
    const int num = r.ok ? (int)r.data.Get("node")->number : -1;
    r = d.Run("vs.connect", "{\"graph\":\"" + g + "\",\"kind\":\"exec\",\"from\":" + std::to_string(start) + ",\"pin\":0,\"to\":" + std::to_string(print) + "}");
    CHECK(r.ok && r.data.Get("errors")->number == 0);
    r = d.Run("vs.connect", "{\"graph\":\"" + g + "\",\"kind\":\"data\",\"from\":" + std::to_string(num) + ",\"pin\":0,\"to\":" + std::to_string(print) + ",\"toPin\":1}");
    CHECK(r.ok);
    r = d.Run("vs.connect", "{\"graph\":\"" + g + "\",\"kind\":\"exec\",\"from\":" + std::to_string(print) + ",\"pin\":7,\"to\":" + std::to_string(start) + "}");
    CHECK(!r.ok && r.error.code == automation::code::Refused);
    r = d.Run("vs.inspect", "{\"graph\":\"" + g + "\"}");
    CHECK(r.ok && r.data.Get("nodes")->array.size() == 3 && r.data.Get("exec")->array.size() == 1 && r.data.Get("data")->array.size() == 1);
    const std::string wired = [&] { std::ifstream f(g); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }();
    CHECK(d.Run("vs.remove_node", "{\"graph\":\"" + g + "\",\"node\":" + std::to_string(num) + "}").ok);
    CHECK(d.Run("undo.undo").ok);   // the file edit was an editor undo step
    const std::string back = [&] { std::ifstream f(g); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }();
    CHECK(back == wired);
    r = d.Run("vs.node_types", R"({"filter":"Translate"})");
    CHECK(r.ok && r.data.Get("count")->number >= 1);
    r = d.Run("vs.add_node", "{\"graph\":\"" + g + "\",\"type\":\"NoSuchNode\"}");
    CHECK(!r.ok && r.error.code == automation::code::NotFound);
}

void TestMcp(Driver& d, const TempDir& tmp) {
    RUN_SUITE("MCP over JSON-RPC 2.0: initialize, tools/list, tools/call, notifications, errors, batches");
    automation::McpServer s(automation::Commands(), d.host);
    std::string a = s.Handle(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");
    Value v = J(a);
    CHECK(v.Get("id") && v.Get("id")->number == 1 && v.Get("result"));
    CHECK(v.Get("result") && v.Get("result")->Get("protocolVersion")->string == "2025-06-18");
    CHECK(v.Get("result") && v.Get("result")->Get("capabilities")->Get("tools"));
    CHECK(s.Handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").empty());
    v = J(s.Handle(R"({"jsonrpc":"2.0","id":"x","method":"initialize","params":{"protocolVersion":"1999-01-01"}})"));
    CHECK(v.Get("result") && v.Get("result")->Get("protocolVersion")->string == "2025-06-18");   // ours, for a version we do not know
    v = J(s.Handle(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"));
    const Value* tools = v.Get("result") ? v.Get("result")->Get("tools") : nullptr;
    CHECK(tools && tools->array.size() == automation::Commands().Size());
    bool named = false;
    if (tools) for (const Value& t : tools->array) {
        const std::string& n = t.Get("name")->string;
        for (char c : n) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) { CHECK(false); break; }
        if (n == "scene_hierarchy") named = t.Get("inputSchema") && t.Get("annotations")->Get("readOnlyHint")->boolean;
    }
    CHECK(named);
    v = J(s.Handle(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"scene_info","arguments":{}}})"));
    const Value* res = v.Get("result");
    CHECK(res && !res->Get("isError")->boolean && res->Get("structuredContent")->Get("ok")->boolean);
    CHECK(res && res->Get("content")->array[0].Get("type")->string == "text");
    v = J(s.Handle(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"object_inspect","arguments":{"object":"Nobody"}}})"));
    res = v.Get("result");
    CHECK(res && res->Get("isError")->boolean && res->Get("structuredContent")->Get("error")->Get("code")->string == "not_found");
    v = J(s.Handle(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"no_such_tool"}})"));
    CHECK(v.Get("error") && v.Get("error")->Get("code")->number == -32602);
    v = J(s.Handle(R"({"jsonrpc":"2.0","id":6,"method":"resources/subscribe"})"));
    CHECK(v.Get("error") && v.Get("error")->Get("code")->number == -32601);
    v = J(s.Handle("{not json"));
    CHECK(v.Get("error") && v.Get("error")->Get("code")->number == -32700 && v.Get("id")->IsNull());
    v = J(s.Handle(R"({"id":7,"method":"ping"})"));
    CHECK(v.Get("error") && v.Get("error")->Get("code")->number == -32600);
    v = J(s.Handle(R"([{"jsonrpc":"2.0","id":8,"method":"ping"},{"jsonrpc":"2.0","method":"notifications/x"},{"jsonrpc":"2.0","id":9,"method":"ping"}])"));
    CHECK(v.IsArray() && v.array.size() == 2);
    v = J(s.Handle("{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"tools/call\",\"params\":{\"name\":\"viewport_screenshot\",\"arguments\":"
                   "{\"width\":32,\"height\":32,\"inline\":true,\"path\":\"" + tmp.Sub("mcp.png") + "\"}}}"));
    res = v.Get("result");
    CHECK(res && res->Get("content")->array.size() == 2 && res->Get("content")->array[1].Get("type")->string == "image");
}

void TestLink(Driver& d, const TempDir& tmp) {
    RUN_SUITE("The editor link: 127.0.0.1, a token file only this user reads, wrong tokens closed, MCP through it");
    const std::string info = tmp.Sub("link/editor_link.json");
    automation::LinkServer server;
    std::string err;
    CHECK(server.Start(info, "project-x", &err));
    if (!server.Running()) { std::cout << "  link did not start: " << err << "\n"; return; }
    automation::LinkInfo li;
    CHECK(automation::ReadLinkInfo(info, li, &err) && li.port == server.Port() && li.token == server.Token() && li.token.size() == 64 && li.project == "project-x");
#if !defined(_WIN32)
    struct stat st{};
    CHECK(::stat(info.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
#endif
    automation::McpServer mcp(automation::Commands(), d.host);
    auto pump = [&] { server.Poll([&](int, const std::string& line) { return mcp.Handle(line); }); };

    automation::LinkClient bad;
    CHECK(!bad.Connect(li.port, std::string(64, '0'), &err, 2000, pump));   // a wrong token is turned away
    automation::LinkClient good;
    CHECK(good.Connect(li.port, li.token, &err, 2000, pump));
    CHECK(server.Clients() == 1);
    CHECK(good.Send(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"automation_host"}})"));
    std::string line;
    CHECK(good.Receive(line, 3000, pump));
    Value v = J(line);
    CHECK(v.Get("result") && v.Get("result")->Get("structuredContent")->Get("data")->Get("kind")->string == "headless");
    CHECK(good.Send(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));   // no answer comes for it
    CHECK(good.Send(R"({"jsonrpc":"2.0","id":2,"method":"ping"})"));
    CHECK(good.Receive(line, 3000, pump) && J(line).Get("id")->number == 2);
    good.Close();
    for (int i = 0; i < 20 && server.Clients() > 0; ++i) pump();
    CHECK(server.Clients() == 0);
    server.Stop();
    CHECK(!fs::exists(info));   // the token file goes with the link
    CHECK(!automation::ReadLinkInfo(info, li, &err));

    // The editor's own frame hook: off by default, on by the flag, and it answers between frames.
#if defined(_WIN32)
    _putenv_s("OKAY_AUTOMATION_LINK_FILE", info.c_str());
#else
    setenv("OKAY_AUTOMATION_LINK_FILE", info.c_str(), 1);
#endif
    CHECK(!oked::automation::LinkFlag());
    oked::automation::LinkFrame(d.ed);
    CHECK(!fs::exists(info));
    oked::automation::LinkFlag() = true;
    oked::automation::LinkFrame(d.ed);
    CHECK(fs::exists(info) && oked::automation::LinkStatus().find("on (port") != std::string::npos);
    CHECK(automation::ReadLinkInfo(info, li, &err));
    automation::LinkClient c;
    CHECK(c.Connect(li.port, li.token, &err, 2000, [&] { oked::automation::LinkFrame(d.ed); }));
    CHECK(c.Send(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"editor_selection"}})"));
    CHECK(c.Receive(line, 3000, [&] { oked::automation::LinkFrame(d.ed); }) && J(line).Get("result"));
    oked::automation::LinkFlag() = false;
    oked::automation::LinkFrame(d.ed);
    CHECK(!fs::exists(info));
}

// ---- Ops: every menu item a command -------------------------------------------------------------------------------

/// Every Item and Toggle of `es` (the ops list them; notes and per-project prefab entries are not ops).
void ItemIds(const std::vector<oked::menubar::Entry>& es, std::vector<std::string>& out) {
    oked::menubar::Walk(es, [&](const oked::menubar::Entry& e, int) {
        if ((e.kind != oked::menubar::Kind::Item && e.kind != oked::menubar::Kind::Toggle) || e.id.empty()) return;
        if (e.id.find(':') != std::string::npos) return;
        auto ends = [&](const std::string& t) { return e.id.size() > t.size() && e.id.compare(e.id.size() - t.size(), t.size(), t) == 0; };
        if (ends(".none") || ends(".more")) return;
        out.push_back(e.id);
    });
}

void TestOps(Driver& d) {
    RUN_SUITE("Ops: every menu item is a command (op.<id>), runs the click's code, refuses greyed-out items, needs_ui for dialogs");
    automation::Registry& all = automation::Commands();
    // 1. Every item of every menu model is an op: the menu bar, the Hierarchy's three menus, GameObject > UI, the Scene view's.
    std::vector<std::string> ids;
    oked::menubar::State ms; ms.selectedCount = 1; ms.objectCount = 1; ms.canUndo = ms.canRedo = true; ms.clipboardHasObject = true; ms.recentCount = 1; ms.scenePath = "a";
    for (const oked::menubar::Top& t : oked::menubar::TopMenus()) ItemIds(oked::menubar::BuildMenu(t.id, ms), ids);
    oked::hiermenu::RowState rs; rs.childCount = 1; rs.hasParent = true; rs.inPrefab = true; rs.modelTools = true; rs.clipboardHasObject = true;
    ItemIds(oked::hiermenu::RowMenu(rs), ids);
    oked::hiermenu::EmptyState es; es.objectCount = 1; es.selectedCount = 1; es.clipboardHasObject = true;
    ItemIds(oked::hiermenu::EmptyMenu(es), ids);
    ItemIds(oked::hiermenu::CreatePopup(es), ids);
    ItemIds(oked::uimenu::UIMenuFull(oked::menuactions::UIScreenGroups()), ids);
    int missing = 0;
    for (const std::string& id : ids)
        if (!all.Find(oked::automation::OpCommandName(id))) { std::cout << "  no op for menu item " << id << "\n"; ++missing; }
    CHECK(missing == 0);
    CHECK(ids.size() > 250);
    CHECK(oked::automation::OpCount() > 250);
    int registered = 0;
    for (const automation::Command* k : all.List("op.")) if (k->name != "op.list" && k->name != "op.run") ++registered;
    std::cout << "  ops: " << oked::automation::OpCount() << " menu items, " << registered << " op commands, " << all.Size() << " commands in all\n";
    CHECK(registered == oked::automation::OpCount());
    // A plain copy of a richer command is an alias of it (rule 19c), not a second handler.
    CHECK(all.Find("op.edit.undo") && all.Find("op.edit.undo")->aliasOf == "undo.undo");
    CHECK(all.Find("op.go.empty") && all.Find("op.go.empty")->aliasOf == "object.create");
    CHECK(all.Find("op.scene.duplicate") && all.Find("op.scene.duplicate")->aliasOf == "op.h.duplicate");

    // 2. A new entry in a menu model is an op with no other code: it shows in MCP's tools/list and runs.
    int pinged = 0;
    oked::automation::AddOpMenu({"test.menu", "a test menu", false, [](okay::editor::EditorState&, GameObject*, bool) {
        std::vector<oked::menubar::Entry> m;
        m.push_back(oked::menubar::Sub("test", "Test", {oked::menubar::Item("test.ping", "Ping", "", "A test entry added to a model.")}));
        return m;
    }});
    CHECK(oked::automation::RefreshOps(all) == 1);
    {
        automation::McpServer s(all, d.host);
        const Value v = J(s.Handle(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"));
        bool listed = false;
        if (const Value* t = v.Get("result") ? v.Get("result")->Get("tools") : nullptr)
            for (const Value& tool : t->array) if (tool.Get("name")->string == "op_test_ping") listed = tool.Get("description")->string.find("Test > Ping") != std::string::npos;
        CHECK(listed);
    }
    oked::automation::Hooks().run = [&pinged](okay::editor::EditorState&, const std::string& menu, const std::string& id, GameObject*) {
        if (menu != "test.menu") return false;
        pinged += id == "test.ping";
        return true;
    };
    CHECK(d.Run("op.test.ping").ok && pinged == 1);
    oked::automation::Hooks().run = nullptr;
    CHECK(d.Run("op.run", R"({"id":"test.ping"})").error.code == "unavailable");   // no host runs it now: said plainly

    // 3. Ops run the menus' own code, one undo step each, and undo.
    Result r = d.Run("scene.new", R"({"template":"empty","discard":true})");
    CHECK(r.ok && d.Run("object.create", R"({"name":"Box","kind":"cube","position":[0,2,0]})").ok);
    const int depth0 = d.ed.UndoDepth();
    r = d.Run("op.go.emptychild");   // GameObject > Create Empty Child, on the selected Box
    CHECK(r.ok);
    GameObject* box = d.ed.scene().Find("Box");
    CHECK(box && box->transform->ChildCount() == 1 && d.ed.UndoDepth() == depth0 + 1);
    CHECK(d.Run("undo.undo").ok);
    box = d.ed.scene().Find("Box");
    CHECK(box && box->transform->ChildCount() == 0);
    r = d.Run("op.h.tf.resetpos", R"({"object":"Box"})");   // the Hierarchy row's Transform > Reset Position
    box = d.ed.scene().Find("Box");
    CHECK(r.ok && box && box->transform->localPosition.y == 0.0f && d.ed.UndoDepth() == depth0 + 1);
    CHECK(d.Run("undo.undo").ok);
    box = d.ed.scene().Find("Box");
    CHECK(box && box->transform->localPosition.y == 2.0f);
    // Greyed out: refused with the menu's own reason.
    r = d.Run("op.edit.paste");
    CHECK(!r.ok && r.error.code == "refused" && r.error.message == oked::menubar::kNothingCopied);
    CHECK(d.Run("op.edit.copy").ok);
    const std::size_t objs = d.ed.scene().Objects().size();
    r = d.Run("op.edit.paste");
    CHECK(r.ok && d.ed.scene().Objects().size() > objs);
    const std::string pasted = r.ok && !r.data.Get("selection")->array.empty() ? r.data.Get("selection")->array[0].string : std::string();
    CHECK(d.Run("op.h.delete", "{\"object\":\"" + pasted + "\"}").ok);   // the row's Delete: done at once (no rows are being drawn)
    CHECK(d.ed.scene().FindGameObjectsWithName("Box").size() == 1);
    // op.list: the live state, as the menu would show it.
    r = d.Run("op.list", R"({"prefix":"edit."})");
    bool redoOff = false;
    if (r.ok) for (const Value& o : r.data.Get("ops")->array)
        if (o.Get("id")->string == "edit.selectall") redoOff = o.Get("enabled") && o.Get("enabled")->boolean;
    CHECK(r.ok && r.data.Get("count")->number >= 10 && redoOff);

    // 4. Dialogs never wait: needs_ui, or the parameter that does it without the dialog.
    r = d.Run("op.file.open");
    CHECK(!r.ok && r.error.code == "needs_ui" && r.error.details.Get("param") && r.error.details.Get("param")->string == "path");
    r = d.Run("op.go.arraydup");
    CHECK(!r.ok && r.error.code == "needs_ui");
    CHECK(all.Find("op.go.arraydup")->summary.find("needs a person") != std::string::npos);
    CHECK(d.Run("op.h.rename", R"({"object":"Box","name":"Crate"})").ok && d.ed.scene().Find("Crate"));
    CHECK(d.Run("op.file.exit").error.code == "refused");
    // A window toggle needs the editor's window: the headless host says so.
    CHECK(d.Run("op.panel.console").error.code == "unavailable");
    r = d.Run("op.file.new3d");   // unsaved changes: the save prompt would need a person
    CHECK(!r.ok && r.error.code == "needs_ui");
    CHECK(d.Run("op.file.new3d", R"({"discard":true})").ok && d.ed.scene().Objects().size() >= 2);
}

// ---- script.run -------------------------------------------------------------------------------------------------

void TestScriptRun(Driver& d) {
    RUN_SUITE("script.run: OkayScript, Lua and JavaScript in the host: output, value, errors with lines, a budget, one undo step, automation_call");
    CHECK(d.Run("scene.new", R"({"template":"empty","discard":true})").ok);
    auto run = [&](const std::string& lang, const std::string& src, const std::string& extra = "") {
        Value p = Value::Obj().Set("language", Value::Str(lang)).Set("source", Value::Str(src));
        if (!extra.empty()) for (auto& kv : J(extra).object) p.Set(kv.first, kv.second);
        return automation::Commands().Run(d.host, "script.run", p);
    };
    auto printed = [](const Result& r, const std::string& what) {
        const Value* o = r.ok ? r.data.Get("output") : r.error.details.Get("output");
        if (o) for (const Value& l : o->array) if (l.string.find(what) != std::string::npos) return true;
        return false;
    };
    Result r = run("okayscript", "print(\"hello okay\")\nresult = 1 + 2");
    CHECK(r.ok && printed(r, "hello okay") && r.data.Get("value")->number == 3);
    if (!r.ok) std::cout << "  " << r.error.message << "\n";
    r = run("lua", "print('hello lua')\nreturn 40 + 2");
    CHECK(r.ok && printed(r, "hello lua") && r.data.Get("value")->number == 42);
    if (!r.ok) std::cout << "  " << r.error.message << "\n";
    r = run("javascript", "console.log('hello js');\nreturn 6 * 7;");
    CHECK(r.ok && printed(r, "hello js") && r.data.Get("value")->number == 42);
    if (!r.ok) std::cout << "  " << r.error.message << "\n";
    // Errors with the script's own line.
    r = run("lua", "local x = 1\nerror('boom')");
    CHECK(!r.ok && r.error.code == "failed" && r.error.details.Get("line")->number == 2);
    std::cout << "  lua error: " << r.error.message << "\n";
    r = run("javascript", "let a = 1;\nlet b = null;\nb.x = 2;");
    CHECK(!r.ok && r.error.details.Get("line")->number == 3);
    std::cout << "  js error: " << r.error.message << "\n";
    r = run("okayscript", "x = 1\ny = (2 +\n");
    CHECK(!r.ok && r.error.details.Get("line")->number >= 2);
    std::cout << "  okayscript error: " << r.error.message << "\n";
    // A runaway script stops at its budget.
    r = run("lua", "while true do end", R"({"budget":100000})");
    CHECK(!r.ok && r.error.details.Get("stopped")->boolean);
    r = run("javascript", "while (true) {}", R"({"budget":100000})");
    CHECK(!r.ok && r.error.details.Get("stopped")->boolean);
    r = run("okayscript", "while (true) { for (i = 0; i < 10; i = i + 1) { } }", R"({"budget":100000})");   // nested: the per-loop cap alone would not stop it soon
    CHECK(!r.ok && r.error.details.Get("stopped")->boolean);
    std::cout << "  okayscript budget: " << r.error.message << "\n";
    CHECK(okayscript::StepBudget() == -1);   // game scripts never get a budget from it
    // automation_call in each language; a script's changes are one undo step.
    const int depth0 = d.ed.UndoDepth();
    r = run("lua", "local a = automation_call('object.create', {kind = 'cube', name = 'LuaBox'})\n"
                   "local b = automation_call('object.create', {kind = 'sphere', name = 'LuaBall'})\n"
                   "return a.ok and b.ok and a.data.name");
    CHECK(r.ok && r.data.Get("value")->string == "LuaBox");
    if (!r.ok) std::cout << "  " << r.error.message << "\n";
    CHECK(d.ed.scene().Find("LuaBox") && d.ed.scene().Find("LuaBall") && d.ed.UndoDepth() == depth0 + 1);
    CHECK(d.Run("undo.undo").ok && !d.ed.scene().Find("LuaBox") && !d.ed.scene().Find("LuaBall"));
    r = run("javascript", "const r = automation_call('object.create', {kind: 'cube', name: 'JsBox'});\nreturn r.data.name;");
    CHECK(r.ok && r.data.Get("value")->string == "JsBox" && d.ed.scene().Find("JsBox"));
    if (!r.ok) std::cout << "  " << r.error.message << "\n";
    r = run("okayscript", "r = automation_call(\"object.create\", {\"kind\": \"cube\", \"name\": \"OkBox\"})\nresult = r.data.name");
    CHECK(r.ok && d.ed.scene().Find("OkBox"));
    if (!r.ok) std::cout << "  " << r.error.message << "\n";
    // The notice the editor shows in its Console.
    bool notice = false;
    for (const automation::LogLine& l : automation::LogSince(0, 0, 0)) if (l.text.find("Automation ran a script") != std::string::npos) notice = true;
    CHECK(notice);
    // Outside script.run, automation_call is refused; a script cannot start another script.
    r = d.Run("script.call", R"({"function":"automation_call_json","args":["scene.info","{}"]})");
    CHECK(!r.ok && r.error.message.find("only in a script run by") != std::string::npos);
    r = run("lua", "return automation_call('script.run', {source = '1'}).ok");
    CHECK(!r.ok || (r.data.Get("value") && !r.data.Get("value")->boolean));
    // "Allow scripts from Automation" off: the running editor refuses.
    {
        oked::automation::EditorHost editorHost(d.ed, "editor");
        oked::automation::ScriptsFlag() = false;
        r = automation::Commands().Run(editorHost, "script.run", J(R"({"source":"result = 1"})"));
        CHECK(!r.ok && r.error.code == "refused");
        oked::automation::ScriptsFlag() = true;
        CHECK(automation::Commands().Run(editorHost, "script.run", J(R"({"source":"result = 1"})")).ok);
    }
}

// ---- Connect to Claude (shared/ClaudeConnect.hpp) ------------------------------------------------------------------

void TestClaudeConnect(const TempDir& tmp) {
    RUN_SUITE("Connect to Claude: the Claude Desktop config merge, its backup, broken JSON refused, the Claude Code line");
    namespace cc = okay::claudeconnect;
    std::string out, err;
    // Other servers and settings stay; only okayspace is added.
    const std::string before = R"({"globalShortcut":"Ctrl+Space","mcpServers":{"files":{"command":"npx","args":["-y","fs"]}},"theme":"dark"})";
    CHECK(cc::MergeDesktopConfig(before, "C:/Program Files/OkaySpace/Tools/okay.exe", out, err));
    Value v = J(out);
    CHECK(v.Get("globalShortcut") && v.Get("globalShortcut")->string == "Ctrl+Space" && v.Get("theme")->string == "dark");
    CHECK(v.Get("mcpServers")->Get("files") && v.Get("mcpServers")->Get("files")->Get("command")->string == "npx");
    const Value* ours = v.Get("mcpServers")->Get("okayspace");
    CHECK(ours && ours->Get("command")->string == "C:/Program Files/OkaySpace/Tools/okay.exe" && ours->Get("args")->array[0].string == "mcp");
    CHECK(v.object.front().first == "globalShortcut");   // the order of keys is kept
    // Run again with another path: updated, not doubled.
    CHECK(cc::MergeDesktopConfig(out, "D:/Okay/okay.exe", out, err));
    v = J(out);
    CHECK(v.Get("mcpServers")->object.size() == 2 && v.Get("mcpServers")->Get("okayspace")->Get("command")->string == "D:/Okay/okay.exe");
    // Broken JSON is refused with the reason; a config that is not an object too.
    CHECK(!cc::MergeDesktopConfig("{\"mcpServers\": {", "okay", out, err) && err.find("not valid JSON") != std::string::npos);
    CHECK(!cc::MergeDesktopConfig("[1,2]", "okay", out, err));
    CHECK(!cc::MergeDesktopConfig(R"({"mcpServers":[]})", "okay", out, err));
    // On disk: a missing file (and folder) is made; an existing one is backed up first; a broken one is left alone.
    const std::string path = tmp.Sub("Claude/claude_desktop_config.json");
    cc::SetupResult res = cc::AddToDesktopConfig(path, "/opt/okay space/okay");
    CHECK(res.ok && res.created && fs::exists(path) && res.backup.empty() && res.message.find(cc::kAfterSetup) != std::string::npos);
    { std::ofstream f(path, std::ios::trunc); f << before; }
    res = cc::AddToDesktopConfig(path, "/opt/okay space/okay");
    CHECK(res.ok && !res.created && fs::exists(path + ".bak"));
    { std::ifstream b(path + ".bak"); std::string t((std::istreambuf_iterator<char>(b)), {}); CHECK(t == before); }
    { std::ifstream f(path); std::string t((std::istreambuf_iterator<char>(f)), {}); Value w = J(t); CHECK(w.Get("mcpServers")->Get("files") && w.Get("mcpServers")->Get("okayspace")); }
    { std::ofstream f(path, std::ios::trunc); f << "{ broken"; }
    res = cc::AddToDesktopConfig(path, "/opt/okay space/okay");
    { std::ifstream f(path); std::string t((std::istreambuf_iterator<char>(f)), {}); CHECK(!res.ok && t == "{ broken"); }
    CHECK(!cc::AddToDesktopConfig(path, "").ok);
    // The Claude Code line: a path with spaces in quotes.
    CHECK(cc::ClaudeCodeCommand("C:\\Program Files\\OkaySpace\\Tools\\okay.exe") == "claude mcp add okayspace -- \"C:\\Program Files\\OkaySpace\\Tools\\okay.exe\" mcp");
    CHECK(cc::ClaudeCodeCommand("/usr/local/bin/okay") == "claude mcp add okayspace -- /usr/local/bin/okay mcp");
    // okay.exe is found beside the program or in its Tools folder.
    fs::create_directories(tmp.Sub("install/Tools"));
#if defined(_WIN32)
    const char* exe = "okay.exe";
#else
    const char* exe = "okay";
#endif
    { std::ofstream f(tmp.Sub(std::string("install/Tools/") + exe)); f << "x"; }
    CHECK(!cc::FindOkayProgram(tmp.Sub("install")).empty() && !cc::FindOkayProgram(tmp.Sub("install/Tools")).empty());
    CHECK(cc::FindOkayProgram(tmp.Sub("nowhere")).empty());
}

} // namespace

int main() {
    RUN_SUITE("automation");
    Log::console = false;
    Game::Reset();
    Time::ResetForTesting();
    automation::InstallLogRing();
    const TempDir tmp = MakeScratch();
    TestJsonWriter();
    TestRegistry();
    {
        Driver d;
        TestSceneRoundTrip(d, tmp);
        TestScriptsAndReflection(d);
        TestPhysicsAndPlay(d);
        TestAnimationModelUi(d);
        TestScreenshotLogValidate(d, tmp);
        TestVisualScripts(d, tmp);
        TestMcp(d, tmp);
        TestLink(d, tmp);
        TestOps(d);
        TestScriptRun(d);
    }
    TestClaudeConnect(tmp);
    std::error_code ec;
    fs::remove_all(tmp.root, ec);
    TEST_MAIN_RESULT();
}
