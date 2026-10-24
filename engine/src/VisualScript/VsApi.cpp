// Visual-script nodes generated from the C#-style language's binding table: one
// node per property / method, each calling that binding (see VsApi.hpp).
#include "okay/VisualScript/VsApi.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/Nodes.hpp"   // VsResolveObject
#include "okay/Scene/GameObject.hpp"
#include "okay/Core/Log.hpp"
#include "../Script/ScriptInternal.hpp"
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <variant>

namespace okay::vs {
namespace {

using script::ApiMethod;
using script::ApiProperty;
using script::ApiType;
using script::EngineApi;
using script::TK;
using script::Value;

PinType PinOf(const script::Type& t) {
    switch (t.kind) {
        case TK::Bool:    return PinType::Bool;
        case TK::Int:
        case TK::Float:   return PinType::Number;
        case TK::String:  return PinType::Text;
        case TK::Vector3: return PinType::Vec3;
        case TK::Object:  return PinType::Object;
        default:          return PinType::Any;
    }
}

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

/// Which binding a generated node runs.
struct Binding {
    int typeIndex = -1;
    enum Kind { Get, Set, Call } kind = Get;
    bool isStatic = false;
    const ApiProperty* prop = nullptr;
    const ApiMethod* method = nullptr;
};

struct Built {
    std::vector<VsApiEntry> entries;
    std::vector<Binding> bindings;   // parallel to entries
    std::unordered_map<std::string, std::size_t> index;
};

const Built& Data() {
    static const Built built = [] {
        Built b;
        const EngineApi& api = EngineApi::Get();   // static: the pointers below stay valid
        for (std::size_t ti = 0; ti < api.types.size(); ++ti) {
            const ApiType& t = api.types[ti];
            // How a member of this type is reached from a script attached to the object.
            const std::string recv = t.name == "GameObject" ? "gameObject"
                                   : t.name == "Transform"  ? "transform"
                                   : t.isComponent          ? "GetComponent<" + t.name + ">()"
                                   : t.isValueType          ? "vector" : t.name;
            auto add = [&](VsApiEntry e, Binding bd, bool isStatic) {
                bd.typeIndex = (int)ti;
                bd.isStatic = isStatic;
                if (b.index.count(e.type)) {
                    Log::Error("Visual Script: two engine bindings would both be node '", e.type, "'; the second is left out");
                    return;
                }
                e.apiType = t.name;
                e.sig.execIn = e.exec;
                for (const auto& p : e.ins) e.sig.ins += (char)p.type;
                for (const auto& p : e.outs) e.sig.outs += (char)p.type;
                b.index[e.type] = b.entries.size();
                b.entries.push_back(std::move(e));
                b.bindings.push_back(bd);
            };
            auto receiver = [&](VsApiEntry& e, bool isStatic) {
                if (isStatic) return;
                if (t.isValueType) e.ins.push_back({t.name, PinType::Vec3});
                else { e.ins.push_back({"Target", PinType::Object}); e.hasTarget = true; }
            };
            for (int pass = 0; pass < 2; ++pass) {
                const bool isStatic = pass == 1;
                const std::string prefix = isStatic ? t.name : recv;
                for (const ApiProperty& p : isStatic ? t.staticProps : t.props) {
                    VsApiEntry g;
                    g.type = t.name + "." + p.name; g.member = p.name;
                    g.title = t.name + "." + p.name; g.doc = p.doc;
                    g.code = prefix + "." + p.name;
                    receiver(g, isStatic);
                    g.outs.push_back({p.name, PinOf(p.type)});
                    Binding bg; bg.kind = Binding::Get; bg.prop = &p;
                    add(g, bg, isStatic);
                    if (!p.set) continue;
                    VsApiEntry s;
                    s.type = t.name + "." + p.name + "="; s.member = p.name;
                    s.title = "Set " + t.name + "." + p.name; s.doc = p.doc;
                    s.code = prefix + "." + p.name + " = " + p.name;
                    s.exec = true;
                    s.ins.push_back({p.name, PinOf(p.type)});
                    receiver(s, isStatic);
                    Binding bs; bs.kind = Binding::Set; bs.prop = &p;
                    add(s, bs, isStatic);
                }
                for (const ApiMethod& m : isStatic ? t.staticMethods : t.methods) {
                    VsApiEntry e;
                    e.type = t.name + "." + m.name + "/" + std::to_string(m.params.size()); e.member = m.name;
                    e.title = t.name + "." + m.name + "(" + Join(m.paramNames) + ")"; e.doc = m.doc;
                    e.code = prefix + "." + m.name + "(" + Join(m.paramNames) + ")";
                    e.exec = !m.pure;
                    for (std::size_t i = 0; i < m.params.size(); ++i)
                        e.ins.push_back({i < m.paramNames.size() ? m.paramNames[i] : "arg" + std::to_string(i), PinOf(m.params[i])});
                    receiver(e, isStatic);
                    if (m.ret.kind != TK::Void) e.outs.push_back({"Result", PinOf(m.ret)});
                    Binding bm; bm.kind = Binding::Call; bm.method = &m;
                    add(e, bm, isStatic);
                }
            }
        }
        return b;
    }();
    return built;
}

/// A wire's value as the binding expects it.
Value ToScript(const VsValue& v, const script::Type& t, VsContext& ctx) {
    switch (t.kind) {
        case TK::Bool:    return Value{v.AsBool()};
        case TK::Int:     return Value{(std::int32_t)std::lround(v.AsFloat())};
        case TK::Float:   return Value{v.AsFloat()};
        case TK::String:  return Value{v.AsString()};
        case TK::Vector3: return Value{v.AsVec3()};
        case TK::Object: {
            GameObject* o = (v.IsObject() || !v.AsString().empty()) ? VsResolveObject(ctx, v) : nullptr;
            return Value{script::ObjRef{o ? o->InstanceId() : 0, t.id}};
        }
        default:
            if (v.IsBool())   return Value{v.AsBool()};
            if (v.IsNumber()) return Value{v.AsFloat()};
            if (v.IsVec3())   return Value{v.AsVec3()};
            if (v.IsObject()) return Value{script::ObjRef{v.AsObjectId(), EngineApi::Get().gameObject}};
            return Value{v.AsString()};
    }
}

/// A binding's result as a wire value.
VsValue ToVs(const Value& v, const script::Type& t) {
    if (auto* b = std::get_if<bool>(&v)) return VsValue(*b);
    if (auto* i = std::get_if<std::int32_t>(&v)) return VsValue((float)*i);
    if (auto* f = std::get_if<float>(&v)) return VsValue(*f);
    if (auto* s = std::get_if<std::string>(&v)) return VsValue(*s);
    if (auto* x = std::get_if<Vec3>(&v)) return VsValue(*x);
    if (auto* r = std::get_if<script::ObjRef>(&v)) return VsValue::Object(r->go);
    return t.kind == TK::Object ? VsValue::Object(0) : VsValue();
}

class ApiNode : public VsNode {
public:
    ApiNode(const VsApiEntry& e, const Binding& b) : m_entry(e), m_bind(b) {}
    int ExecOutCount() const override { return m_entry.exec ? 1 : 0; }
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        if (m_entry.exec) return pin == 0 ? m_last : VsValue();   // the result of its last run
        return Invoke(g, ctx);
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        m_last = Invoke(g, ctx);
        return 0;
    }

private:
    const VsApiEntry& m_entry;
    const Binding& m_bind;
    VsValue m_last;

    VsValue Empty() const {
        return !m_entry.outs.empty() && m_entry.outs[0].type == PinType::Object ? VsValue::Object(0) : VsValue();
    }
    VsValue Invoke(NodeGraph& g, VsContext& ctx) {
        const EngineApi& api = EngineApi::Get();
        const ApiType& t = api.types[(std::size_t)m_bind.typeIndex];
        script::CallContext c;
        c.self = ctx.gameObject;
        c.deltaTime = ctx.deltaTime;
        Value self;
        if (!m_bind.isStatic) {
            // The receiver is the last input (after the value / parameters).
            if (t.isValueType) {
                self = Value{In((int)m_entry.ins.size() - 1, g, ctx).AsVec3()};
            } else {
                // A wired Target has already been swapped in as ctx.gameObject.
                GameObject* go = ctx.gameObject;
                if (!go) { g.Error(this, ctx, "there's no object for it to act on"); return Empty(); }
                if (t.find && !t.find(go)) {
                    g.Error(this, ctx, "'" + go->name + "' has no " + t.name + " (add one in the Inspector)");
                    return Empty();
                }
                self = Value{script::ObjRef{go->InstanceId(), m_bind.typeIndex}};
            }
        }
        try {
            switch (m_bind.kind) {
                case Binding::Get:
                    return ToVs(m_bind.prop->get(self, c), m_bind.prop->type);
                case Binding::Set:
                    m_bind.prop->set(self, ToScript(In(0, g, ctx), m_bind.prop->type, ctx), c);
                    return VsValue();
                case Binding::Call: {
                    const ApiMethod& m = *m_bind.method;
                    std::vector<Value> args;
                    args.reserve(m.params.size());
                    for (std::size_t i = 0; i < m.params.size(); ++i)
                        args.push_back(ToScript(In((int)i, g, ctx), m.params[i], ctx));
                    return ToVs(m.fn(self, args, c), m.ret);
                }
            }
        } catch (const script::RuntimeError& e) {
            g.Error(this, ctx, e.what());
        } catch (const std::bad_variant_access&) {
            g.Error(this, ctx, "it was given a value of the wrong kind");
        }
        return Empty();
    }
};

} // namespace

const std::vector<VsApiEntry>& VsApiEntries() { return Data().entries; }

const VsApiEntry* VsApiFind(const std::string& type) {
    if (type.find('.') == std::string::npos) return nullptr;   // every generated type is "Type.member"
    const Built& b = Data();
    auto it = b.index.find(type);
    return it != b.index.end() ? &b.entries[it->second] : nullptr;
}

std::unique_ptr<VsNode> MakeApiNode(const std::string& type) {
    if (type.find('.') == std::string::npos) return nullptr;
    const Built& b = Data();
    auto it = b.index.find(type);
    if (it == b.index.end()) return nullptr;
    return std::make_unique<ApiNode>(b.entries[it->second], b.bindings[it->second]);
}

std::string VsApiCheckLiteral(const std::string& type, int pin, const std::string& literal) {
    if (type.find('.') == std::string::npos) return "";
    const Built& b = Data();
    auto it = b.index.find(type);
    if (it == b.index.end()) return "";
    const Binding& bd = b.bindings[it->second];
    if (bd.kind != Binding::Call || !bd.method->checkLiteral) return "";
    if (pin < 0 || pin >= (int)bd.method->params.size()) return "";   // parameters come first
    return bd.method->checkLiteral(pin, literal);
}

std::string VsApiReference() {
    auto pins = [](const std::vector<VsApiPin>& v) {
        std::string s;
        for (const auto& p : v) s += (s.empty() ? "" : ", ") + p.name + " (" + PinTypeName(p.type) + ")";
        return s.empty() ? std::string("-") : s;
    };
    std::string md =
        "# Engine API reference\n\n"
        "Generated from the engine's binding table (`engine/src/Script/EngineApi.cpp`): the same bindings\n"
        "typed scripts (`okayscript-next`, `class Name : Behaviour`) call and the visual-script\n"
        "**Engine API** nodes run. Don't edit this page by hand;\n"
        "after changing the table, regenerate it with `OKAY_WRITE_API_DOCS=1 ./tests/test_vs_types`\n"
        "(run from the build folder). The `vs_types` test fails while it's out of date.\n\n"
        "In a graph, members of GameObject, Transform and Rigidbody act on the graph's own object, or on\n"
        "whatever is wired into (or typed into) their last input, **Target**. Steps run in the flow;\n"
        "value nodes give a value whenever something reads them.\n";
    const EngineApi& api = EngineApi::Get();
    for (const ApiType& t : api.types) {
        md += "\n## " + t.name + "\n\n" + t.doc + "\n\n";
        md += "| In a script | Node | Kind | Inputs | Gives | What it does |\n|---|---|---|---|---|---|\n";
        for (const VsApiEntry& e : VsApiEntries()) {
            if (e.apiType != t.name) continue;
            md += "| `" + e.code + "` | `" + e.type + "` | " + (e.exec ? "step" : "value") + " | " + pins(e.ins) + " | " +
                  pins(e.outs) + " | " + e.doc + " |\n";
        }
    }
    return md;
}

} // namespace okay::vs
