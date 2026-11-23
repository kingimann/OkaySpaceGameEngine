// The shared script-function table (okay/Scripting/ScriptApiRegistry.hpp). Written for this engine from
// the two existing binding tables' behaviour (OkayScriptVM's builtin map and Script/EngineApi); no outside code.
#include "okay/Scripting/ScriptApiRegistry.hpp"
#include "okay/Core/Log.hpp"
#include <atomic>
#include <cmath>
#include <utility>

namespace okay {

namespace {

std::vector<ScriptApiEntry>& Table() {
    static std::vector<ScriptApiEntry> t = [] {
        std::vector<ScriptApiEntry> v;
        detail::RegisterCoreScriptApis(v);
        return v;
    }();
    return t;
}

std::atomic<int> g_duplicates{0};

vs::VsValue Empty(const ScriptApiEntry& e) {
    switch (e.ret) {
        case ScriptApiType::Bool:    return e.boolAsNumber ? vs::VsValue(0.0f) : vs::VsValue(false);
        case ScriptApiType::String:  return vs::VsValue(std::string());
        case ScriptApiType::Vector3: return vs::VsValue(Vec3::Zero);
        default:                     return vs::VsValue(0.0f);
    }
}

vs::VsValue As(ScriptApiType t, const vs::VsValue& v) {
    switch (t) {
        case ScriptApiType::Bool:    return vs::VsValue(v.AsBool());
        case ScriptApiType::Int: {
            const float f = v.AsFloat();
            return vs::VsValue(std::isfinite(f) ? std::trunc(f) : 0.0f);
        }
        case ScriptApiType::Float:   return vs::VsValue(v.AsFloat());
        case ScriptApiType::String:  return vs::VsValue(v.AsString());
        case ScriptApiType::Vector3: return vs::VsValue(v.AsVec3());
        default:                     return v;
    }
}

} // namespace

void AddScriptApi(ScriptApiEntry entry) {
    auto& t = Table();
    for (auto& e : t)
        if (e.name == entry.name) { e = std::move(entry); return; }
    t.push_back(std::move(entry));
}

void AddScriptApi(const std::string& name, ScriptApiFn fn) {
    ScriptApiEntry e;
    e.name = name;
    e.fn = std::move(fn);
    e.ret = ScriptApiType::Void;   // OkayScript-only: the result is passed through as it is
    AddScriptApi(std::move(e));
}

const std::vector<ScriptApiEntry>& ScriptApis() { return Table(); }

const ScriptApiEntry* FindScriptApi(const std::string& okayName) {
    for (const auto& e : Table()) {
        if (e.name == okayName) return &e;
        for (const auto& a : e.aliases) if (a == okayName) return &e;
    }
    return nullptr;
}

vs::VsValue CallScriptApiDynamic(const ScriptApiEntry& e, GameObject* self, const std::vector<vs::VsValue>& raw,
                                 bool* warned) {
    // An OkayScript-only entry from AddScriptApi(name, fn) declares nothing: it gets the arguments as passed.
    const bool typed = !e.typedType.empty();
    try {
        std::vector<vs::VsValue> args;
        if (!typed) args = raw;
        else {
            args.reserve(e.params.size());
            std::size_t j = 0;
            for (const ScriptApiParam& p : e.params) {
                if (j >= raw.size()) {
                    if (!p.optional) throw ScriptApiError("needs its '" + p.name + "' argument");
                    args.push_back(p.def);
                    continue;
                }
                if (p.type == ScriptApiType::Vector3 && !raw[j].IsVec3()) {
                    // Three numbers in a row are one vector: add_force(0, 5, 0) as well as add_force(v).
                    Vec3 v;
                    v.x = raw[j].AsFloat();
                    v.y = j + 1 < raw.size() ? raw[j + 1].AsFloat() : 0.0f;
                    v.z = j + 2 < raw.size() ? raw[j + 2].AsFloat() : 0.0f;
                    args.push_back(vs::VsValue(v));
                    j += 3;
                    continue;
                }
                args.push_back(As(p.type, raw[j]));
                ++j;
            }
        }
        vs::VsValue r = e.fn(self, args);
        if (!typed) return r;
        if (e.ret == ScriptApiType::Void) return vs::VsValue{};
        if (e.ret == ScriptApiType::Bool && e.boolAsNumber) return vs::VsValue(r.AsBool() ? 1.0f : 0.0f);
        return As(e.ret, r);
    } catch (const ScriptApiError& ex) {
        if (!warned || !*warned) Log::Warning("[script] ", e.name, ": ", ex.what());
        if (warned) *warned = true;
        return Empty(e);
    }
}

int ScriptApiDuplicateCount() { return g_duplicates.load(); }

void NoteScriptApiDuplicate(const std::string& language, const std::string& name) {
    ++g_duplicates;
    Log::Error("Script API: ", language, " defines '", name,
               "' itself, but the shared table (ScriptApiRegistry) already has it; the shared one is used");
}

} // namespace okay
