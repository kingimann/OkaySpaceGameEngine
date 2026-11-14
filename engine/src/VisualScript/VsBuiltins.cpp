#include "okay/VisualScript/VsBuiltins.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include "okay/VisualScript/Nodes.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/GameObject.hpp"

#include <algorithm>
#include <cctype>

namespace okay::vs {
namespace {

OkayScriptVM& ScratchVm() {
    static OkayScriptVM vm;
    return vm;
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

VsValue PropertyToValue(const PropertyValue& v) {
    switch (v.Type()) {
        case PropertyType::Bool:   return VsValue(v.AsBool());
        case PropertyType::Int:    return VsValue((float)v.AsInt());
        case PropertyType::Float:  return VsValue(v.AsFloat());
        case PropertyType::Enum:   return VsValue(v.EnumName().empty() ? std::to_string(v.AsEnum()) : v.EnumName());
        case PropertyType::Vec2:   return VsValue(Vec3{v.AsVec2().x, v.AsVec2().y, 0});
        case PropertyType::Vec3:   return VsValue(v.AsVec3());
        case PropertyType::Color:  return VsValue(Vec3{v.AsColor().r, v.AsColor().g, v.AsColor().b});
        default:                   return VsValue(v.ToString());
    }
}

bool SetFromWire(GameObject& go, const std::string& path, const VsValue& in, std::string& error) {
    if (in.IsBool()) return PropertyRegistry::SetProperty(go, path, PropertyValue::MakeBool(in.AsBool()), &error);
    if (in.IsVec3()) return PropertyRegistry::SetProperty(go, path, PropertyValue::MakeVec3(in.AsVec3()), &error);
    if (in.IsNumber()) {
        if (PropertyRegistry::SetProperty(go, path, PropertyValue::MakeFloat(in.AsFloat()), &error)) return true;
        error.clear();
        if (PropertyRegistry::SetProperty(go, path, PropertyValue::MakeInt((long long)std::lround(in.AsFloat())), &error)) return true;
        error.clear();
    }
    return PropertyRegistry::SetPropertyFromText(go, path, in.AsString(), &error);
}

GameObject* TargetOf(VsNode* n, int pin, NodeGraph& g, VsContext& ctx, bool required, std::string& error) {
    GameObject* go = ctx.gameObject;
    if (n->HasInput(pin)) go = VsResolveObject(ctx, n->InputValue(pin, g, ctx));
    if (!go && required) error = "there's no object for this node";
    return go;
}

void SendTo(GameObject* go, const std::string& message, const VsValue& arg) {
    if (!go || message.empty()) return;
    if (auto* sc = go->GetComponent<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEventArgs(message, {arg}, nullptr);
    if (auto* vs = go->GetComponent<VisualScriptComponent>()) vs->Send(message);
}

struct BuiltinCallNode : VsNode {
    std::string name;
    VsValue last;
    explicit BuiltinCallNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return last; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        ScriptHost host;
        host.gameObject = ctx.gameObject;
        host.transform = ctx.transform;
        host.deltaTime = ctx.deltaTime;
        ScratchVm().Bind(&host);
        std::vector<VsValue> args;
        for (int i = 0; i < 4; ++i)
            if (HasInput(i) || (i < (int)hasDefault.size() && hasDefault[i]))
                args.push_back(In(i, g, ctx));
        std::string err;
        last = ScratchVm().CallBuiltin(name, std::move(args), &err);
        ScratchVm().Bind(nullptr);
        if (!err.empty()) g.Error(this, ctx, name + ": " + err);
        return 0;
    }
};

struct GetPropertyNode : VsNode {
    std::string path;
    explicit GetPropertyNode(std::string p) : path(std::move(p)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        std::string error;
        GameObject* go = TargetOf(this, 1, g, ctx, true, error);
        if (!go) return {};
        const std::string use = HasInput(0) ? In(0, g, ctx).AsString() : path;
        if (use.empty()) return {};
        PropertyValue v;
        if (!PropertyRegistry::TryGetProperty(*go, use, v, &error)) return VsValue(std::string());
        return PropertyToValue(v);
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        std::string error;
        GameObject* go = TargetOf(this, 1, g, ctx, true, error);
        if (!go) { g.Error(this, ctx, error); return 0; }
        const std::string use = HasInput(0) ? In(0, g, ctx).AsString() : path;
        if (use.empty()) { g.Error(this, ctx, "type a property path, like Transform.position or Light.intensity"); return 0; }
        PropertyValue v;
        if (!PropertyRegistry::TryGetProperty(*go, use, v, &error)) g.Error(this, ctx, error);
        return 0;
    }
};

struct SetPropertyNode : VsNode {
    std::string path;
    explicit SetPropertyNode(std::string p) : path(std::move(p)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        std::string error;
        GameObject* go = TargetOf(this, 2, g, ctx, true, error);
        if (!go) { g.Error(this, ctx, error); return 0; }
        const std::string use = HasInput(1) ? In(1, g, ctx).AsString() : path;
        if (use.empty()) { g.Error(this, ctx, "type a property path, like Light.intensity"); return 0; }
        if (!SetFromWire(*go, use, In(0, g, ctx), error)) g.Error(this, ctx, error.empty() ? "couldn't set " + use : error);
        return 0;
    }
};

struct CallMethodNode : VsNode {
    std::string spec;
    VsValue last;
    explicit CallMethodNode(std::string s) : spec(std::move(s)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return last; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const std::string use = HasInput(4) ? In(4, g, ctx).AsString() : spec;
        const auto dot = use.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= use.size()) {
            g.Error(this, ctx, "type Type.Method, like Mathf.Abs or Transform.Translate");
            return 0;
        }
        std::vector<VsValue> args;
        for (int i = 0; i < 4; ++i)
            if (HasInput(i) || (i < (int)hasDefault.size() && hasDefault[i])) args.push_back(In(i, g, ctx));
        std::string error;
        if (!VsApiInvoke(use.substr(0, dot), use.substr(dot + 1), args, ctx, last, error))
            g.Error(this, ctx, error);
        return 0;
    }
};

struct GetComponentNode : VsNode {
    std::string typeName;
    explicit GetComponentNode(std::string t) : typeName(std::move(t)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        std::string error;
        GameObject* go = TargetOf(this, 0, g, ctx, false, error);
        const std::string want = HasInput(1) ? In(1, g, ctx).AsString() : typeName;
        bool has = false;
        if (go && !want.empty()) {
            const std::string key = Lower(want);
            for (const auto& c : go->Components()) {
                if (!c) continue;
                auto info = PropertyRegistry::FindFor(*c);
                if (!info) continue;
                if (Lower(info->name) == key || Lower(info->recordName) == key) { has = true; break; }
            }
        }
        if (pin == 1) return VsValue(has);
        return VsValue::Object(has && go ? go->InstanceId() : 0);
    }
    int Exec(NodeGraph& g, VsContext& ctx) override { Eval(0, g, ctx); return 0; }
};

struct AddComponentNode : VsNode {
    std::string typeName;
    explicit AddComponentNode(std::string t) : typeName(std::move(t)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        std::string error;
        GameObject* go = TargetOf(this, 0, g, ctx, true, error);
        if (!go) { g.Error(this, ctx, error); return 0; }
        const std::string want = HasInput(1) ? In(1, g, ctx).AsString() : typeName;
        auto info = PropertyRegistry::FindByName(want);
        if (!info || !info->add) {
            g.Error(this, ctx, "no component type \"" + want + "\" can be added (check the name, like Light or AudioSource)");
            return 0;
        }
        info->add(*go);
        return 0;
    }
};

struct FindByTagNode : VsNode {
    std::string tag;
    explicit FindByTagNode(std::string t) : tag(std::move(t)) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const std::string want = HasInput(0) ? In(0, g, ctx).AsString() : tag;
        Scene* s = ctx.gameObject ? ctx.gameObject->scene() : nullptr;
        if (!s) return VsValue::Object(0);
        for (const auto& up : s->Objects())
            if (up && up->active && up->tag == want) return VsValue::Object(up->InstanceId());
        return VsValue::Object(0);
    }
};

struct SendMessageNode : VsNode {
    std::string message;
    explicit SendMessageNode(std::string m) : message(std::move(m)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        std::string error;
        GameObject* go = TargetOf(this, 1, g, ctx, true, error);
        if (!go) { g.Error(this, ctx, error); return 0; }
        const std::string msg = HasInput(0) ? In(0, g, ctx).AsString() : message;
        if (msg.empty()) { g.Error(this, ctx, "type the message name"); return 0; }
        SendTo(go, msg, VsValue());
        return 0;
    }
};

struct BroadcastNode : VsNode {
    std::string message;
    explicit BroadcastNode(std::string m) : message(std::move(m)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const std::string msg = HasInput(0) ? In(0, g, ctx).AsString() : message;
        if (msg.empty()) { g.Error(this, ctx, "type the message name"); return 0; }
        Scene* s = ctx.gameObject ? ctx.gameObject->scene() : nullptr;
        if (!s) { g.Error(this, ctx, "there's no scene to broadcast in"); return 0; }
        std::vector<GameObject*> all;
        for (const auto& up : s->Objects()) if (up && up->active) all.push_back(up.get());
        for (GameObject* go : all) SendTo(go, msg, VsValue());
        return 0;
    }
};

struct RunSnippetNode : VsNode {
    std::string code;
    VsValue last;
    explicit RunSnippetNode(std::string c) : code(std::move(c)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return last; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const std::string src = HasInput(0) ? In(0, g, ctx).AsString() : code;
        if (src.empty()) { g.Error(this, ctx, "type a line of text script, like 2 + 3"); return 0; }
        if (!ctx.gameObject) { g.Error(this, ctx, "there's no object to run the snippet on"); return 0; }
        ScriptHost host;
        host.gameObject = ctx.gameObject;
        host.transform = ctx.transform;
        host.deltaTime = ctx.deltaTime;
        OkayScriptVM vm;
        vm.Bind(&host);
        std::string err;
        const std::string wrapped = "function __snippet() {\nreturn (" + src + ")\n}\n";
        const std::string statements = "function __snippet() {\n" + src + "\n}\n";
        if (!vm.Load(wrapped, &err)) {
            err.clear();
            if (!vm.Load(statements, &err)) {
                g.Error(this, ctx, "the snippet didn't parse: " + err);
                return 0;
            }
        }
        last = vm.CallEventArgs("__snippet", {}, &err);
        if (!err.empty()) g.Error(this, ctx, "the snippet failed: " + err);
        return 0;
    }
};

} // namespace

const NodeSignature& BuiltinSignature() {
    static const NodeSignature sig{true, "AAAA", "A"};
    return sig;
}

const std::vector<BuiltinNodeInfo>& BuiltinNodes() {
    static const std::vector<BuiltinNodeInfo> nodes = [] {
        std::vector<BuiltinNodeInfo> out;
        OkayScriptVM vm;
        for (const std::string& name : vm.BuiltinNames()) {
            bool ok = !name.empty();
            for (char c : name)
                if (!(std::isalnum((unsigned char)c) || c == '_' || c == '.' )) ok = false;
            if (!ok) continue;   // a name that isn't one graph token can't be its own node
            BuiltinNodeInfo info;
            info.builtin = name;
            info.title = name;
            info.type = "Fn." + name;
            out.push_back(std::move(info));
        }
        std::sort(out.begin(), out.end(), [](const BuiltinNodeInfo& a, const BuiltinNodeInfo& b) { return a.type < b.type; });
        return out;
    }();
    return nodes;
}

bool IsBuiltinNode(const std::string& type) {
    if (type.rfind("Fn.", 0) != 0) return false;
    const std::string name = type.substr(3);
    for (const auto& n : BuiltinNodes()) if (n.builtin == name) return true;
    return false;
}

std::unique_ptr<VsNode> MakeCoverageNode(const std::string& type, const std::vector<std::string>& args) {
    if (type.rfind("Fn.", 0) == 0) {
        const std::string name = type.substr(3);
        for (const auto& n : BuiltinNodes())
            if (n.builtin == name) return std::make_unique<BuiltinCallNode>(name);
        return nullptr;
    }
    std::string a;
    for (const auto& s : args) { if (!a.empty()) a += ' '; a += s; }
    if (type == "GetProperty") return std::make_unique<GetPropertyNode>(a);
    if (type == "SetProperty") return std::make_unique<SetPropertyNode>(a);
    if (type == "CallMethod") return std::make_unique<CallMethodNode>(a);
    if (type == "GetComponent") return std::make_unique<GetComponentNode>(a);
    if (type == "AddComponent") return std::make_unique<AddComponentNode>(a);
    if (type == "FindByTag") return std::make_unique<FindByTagNode>(a);
    if (type == "SendMessage") return std::make_unique<SendMessageNode>(a);
    if (type == "Broadcast") return std::make_unique<BroadcastNode>(a);
    if (type == "RunSnippet") return std::make_unique<RunSnippetNode>(a);
    return nullptr;
}

std::string CoverageTableMarkdown() {
    std::string md;
    md += "# Visual script coverage\n\n";
    md += "How a graph reaches each engine area. Text-script builtins (`.okay`) are nodes named `Fn.<builtin>` ";
    md += "(search the palette for the builtin). The C#-style engine API is the **Engine API** category. ";
    md += "Anything still missing a dedicated node goes through the escape hatches.\n\n";
    md += "Escape hatches: **Get Property**, **Set Property**, **Call Method** (`Type.Method`), **Get Component**, ";
    md += "**Add Component**, **Find Object** (by name), **Get Object by Tag**, **All With Tag**, **Send Message**, ";
    md += "**Broadcast Message**, **Run Text Snippet**, **Call Script**. A wrong type or a missing component is an error on that node. ";
    md += "The raw path (`Light.intensity`, `Mathf.Abs`) stays a search word.\n\n";
    md += "| System | API a text script uses | Visual script node |\n| --- | --- | --- |\n";
    struct Row { const char* system; const char* api; const char* node; };
    const Row rows[] = {
        {"Core and scene", "GameObject, Transform, scene builtins", "Engine API GameObject/Transform, Fn.load_scene, Find Object, Get Object by Tag"},
        {"Rendering and materials", "Camera, Light, MeshRenderer, PostProcess, Fog, material properties", "Engine API Camera/Light/PostProcess/Fog, Get/Set Property"},
        {"Lights", "Light.*", "Engine API Light, Get/Set Property Light.intensity"},
        {"Camera", "Camera.*", "Engine API Camera, Set Camera Target, Zoom Camera, Switch Camera"},
        {"Physics queries and forces", "Rigidbody, raycast, add_force", "Engine API Rigidbody, Fn.raycast, Fn.add_force"},
        {"Collisions and triggers", "on_collision, on_trigger", "On Collision, On Trigger, On Tag Enter, On Collision With Tag"},
        {"Animation, Animator, IK", "Animator.*, IK.*, play_anim", "Engine API Animator/IK, Crossfade Animation, Fn.play_anim"},
        {"Navigation", "NavMeshAgent.*", "Engine API NavMeshAgent, Chase Target, Patrol Waypoints"},
        {"Audio", "Audio.*, play_sound", "Engine API Audio, Play Sound Here, Fn.play_sound"},
        {"Input", "Input.*, key", "Engine API Input, Get Move Direction, On Key, Fn.key"},
        {"UI", "UI components, ui_* builtins, get_clipboard_text / set_clipboard_text", "Engine API UI nodes, every Fn.ui_* builtin, Get Clipboard Text, Set Clipboard Text"},
        {"Particles", "particle builtins and properties", "Get/Set Property, plus Fn.particle_* builtins in the list below"},
        {"Terrain", "terrain properties", "Get/Set Property on a terrain component. No separate terrain builtin."},
        {"Splines", "Spline.*, SplineFollower.*", "Engine API Spline / SplineFollower / LineRenderer"},
        {"Networking", "net_* builtins", "Every Fn.net_* builtin in the list below"},
        {"Saves", "SaveGame.*, game_save / game_load / game_continue ...", "Engine API SaveGame, Save Checkpoint, Load Checkpoint, Fn.game_* save-slot nodes (Saveable component marks what is stored)"},
        {"Display", "get_monitor, set_display_mode, set_display_resolution, set_refresh_rate, begin_display_confirm", "Fn.get_monitor, Fn.set_display_mode, Fn.set_display_resolution, Fn.set_refresh_rate, Fn.begin_display_confirm, Fn.keep_display_settings, Fn.revert_display_settings"},
        {"Scenes and prefabs", "instantiate, load_scene", "Fn.instantiate, Fn.load_scene, Spawn Projectile, Run Another Graph"},
        {"Time", "Time.*, time", "Engine API Time, Fn.time, Delta Time"},
        {"Tweens and timers", "tween builtins, Every, Delay", "Fn.tween_* builtins, Tween Position, Every, Delay, Count Down"},
        {"Localization", "Localization.*", "Engine API Localization"},
        {"Debug and console", "print, Debug.Log", "Print, Fn.print, Fn.log_warn, Engine API Debug"},
        {"Math", "Mathf.*, clamp", "Math nodes, Engine API Mathf, Fn.clamp"},
        {"AI and story", "BehaviorTree, Dialogue, Quest", "Engine API BehaviorTree / Dialogue / Quest / Story"},
        {"Editor, launcher, GPU backends, anti-cheat internals", "no game-script API", "Not a graph node: those systems are editor or engine internals with no public script API"},
    };
    for (const Row& r : rows)
        md += "| " + std::string(r.system) + " | " + r.api + " | " + r.node + " |\n";
    md += "\n## Builtin parity\n\n";
    md += "Every text-script builtin whose name is a single token (letters, digits, `_`, `.`) is the node `Fn.<name>`. ";
    md += "It runs that builtin on this object. Wire only the arguments it takes. ";
    md += std::to_string(BuiltinNodes().size()) + " builtins are nodes.\n\n";
    md += "| Builtin | Node |\n| --- | --- |\n";
    for (const auto& n : BuiltinNodes())
        md += "| `" + n.builtin + "` | `" + n.type + "` |\n";
    md += "\n## Gaps\n\n";
    md += "Editor-only tools (undo, the launcher, modeling, the asset browser), GPU backend choice, ";
    md += "and anti-cheat internals have no text-script builtin and no engine-API binding, so there is nothing ";
    md += "for a graph to call. Get Property / Call Method still reach any component and method that *is* registered.\n";
    return md;
}

} // namespace okay::vs
