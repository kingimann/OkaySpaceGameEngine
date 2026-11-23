// The engine binding registry: every type, property and method a script can use.
// Each one calls real engine code; there are no placeholders. The checker
// resolves names against this table and the editor's autocomplete lists it.
#include "EngineApiInternal.hpp"

namespace okay::script {

GameObject* Resolve(const ObjRef& r, SrcLoc loc) {
    if (r.go == 0) throw RuntimeError("This reference is null (it doesn't point at an object)", loc);
    GameObject* g = GameObject::FromInstanceId(r.go);
    if (!g) throw RuntimeError("This object was destroyed, so it can't be used any more", loc);
    return g;
}

Value MakeRef(GameObject* go, int apiType) {
    if (!go) return Value{};
    return Value{ObjRef{go->InstanceId(), apiType}};
}

int EngineApi::Find(const std::string& name) const {
    for (std::size_t i = 0; i < types.size(); ++i) if (types[i].name == name) return (int)i;
    return -1;
}

using namespace engine_api;

namespace {


// ---- The shared table (okay/Scripting/ScriptApiRegistry.hpp) ----
// A shared function is written once, for every script language; here it becomes a member of its type.
Type TypeOf(ScriptApiType t) {
    switch (t) {
        case ScriptApiType::Bool:    return Type::Of(TK::Bool);
        case ScriptApiType::Int:     return Type::Of(TK::Int);
        case ScriptApiType::Float:   return Type::Of(TK::Float);
        case ScriptApiType::String:  return Type::Of(TK::String);
        case ScriptApiType::Vector3: return Type::Of(TK::Vector3);
        default:                     return Type::Of(TK::Void);
    }
}
vs::VsValue ToShared(ScriptApiType t, const Value& v) {
    switch (t) {
        case ScriptApiType::Bool:    { auto* b = std::get_if<bool>(&v); return vs::VsValue(b ? *b : F(v) != 0.0f); }
        case ScriptApiType::Int:     return vs::VsValue((float)I(v));
        case ScriptApiType::String:  return vs::VsValue(S(v));
        case ScriptApiType::Vector3: return vs::VsValue(V(v));
        default:                     return vs::VsValue(F(v));
    }
}
Value FromShared(ScriptApiType t, const vs::VsValue& v) {
    switch (t) {
        case ScriptApiType::Void:    return Value{};
        case ScriptApiType::Bool:    return Value{v.AsBool()};
        case ScriptApiType::Int: {
            const float f = v.AsFloat();
            return Value{(std::int32_t)(std::isfinite(f) ? std::max(-2147483648.0f, std::min(2147483520.0f, f)) : 0.0f)};
        }
        case ScriptApiType::String:  return Value{v.AsString()};
        case ScriptApiType::Vector3: return Value{v.AsVec3()};
        default:                     return Value{v.AsFloat()};
    }
}
// Run a shared function from a typed call: `self` is the receiver for a Method, else the script's object.
Value CallShared(const ScriptApiEntry& e, const Value& self, std::vector<Value>& a, CallContext& c) {
    GameObject* g = e.kind == ScriptApiKind::Method ? Resolve(std::get<ObjRef>(self), c.loc) : c.self;
    std::vector<vs::VsValue> args;
    args.reserve(e.params.size());
    for (std::size_t i = 0; i < e.params.size(); ++i)
        args.push_back(i < a.size() ? ToShared(e.params[i].type, a[i]) : e.params[i].def);
    try {
        return FromShared(e.ret, e.fn(g, args));
    } catch (const ScriptApiError& ex) {
        throw RuntimeError(ex.what(), c.loc);
    }
}
void AddSharedBindings(EngineApi& api) {
    for (const ScriptApiEntry& src : ScriptApis()) {
        if (src.typedType.empty()) continue;   // OkayScript only
        auto e = std::make_shared<const ScriptApiEntry>(src);
        int ti = api.Find(e->typedType);
        if (ti < 0) {
            ApiType t; t.name = e->typedType; t.isStatic = true;
            api.types.push_back(std::move(t));
            ti = (int)api.types.size() - 1;
        }
        ApiType& t = api.types[(std::size_t)ti];
        if (e->kind == ScriptApiKind::StaticProperty) {
            for (const ApiProperty& p : t.staticProps)
                if (p.name == e->typedName) NoteScriptApiDuplicate("the typed language", t.name + "." + p.name);
            ApiProperty p; p.name = e->typedName; p.type = TypeOf(e->ret); p.doc = e->doc;
            p.get = [e](const Value& self, CallContext& c) { std::vector<Value> none; return CallShared(*e, self, none, c); };
            t.staticProps.push_back(std::move(p));
            continue;
        }
        std::vector<ApiMethod>& list = e->kind == ScriptApiKind::Method ? t.methods : t.staticMethods;
        const std::size_t all = e->params.size();
        const std::size_t fewest = e->typedMinParams < 0 ? all : std::min(all, (std::size_t)e->typedMinParams);
        for (std::size_t n = fewest; n <= all; ++n) {   // one overload per argument count
            for (const ApiMethod& m : list)
                if (m.name == e->typedName && m.params.size() == n)
                    NoteScriptApiDuplicate("the typed language", t.name + "." + m.name);
            ApiMethod m; m.name = e->typedName; m.ret = TypeOf(e->ret); m.doc = e->doc; m.pure = e->pure;
            for (std::size_t i = 0; i < n; ++i) { m.params.push_back(TypeOf(e->params[i].type)); m.paramNames.push_back(e->params[i].name); }
            m.checkLiteral = e->checkLiteral;
            m.fn = [e](const Value& self, std::vector<Value>& a, CallContext& c) { return CallShared(*e, self, a, c); };
            list.push_back(std::move(m));
        }
    }
}

} // namespace

namespace engine_api {

void Ctx::Init() {
    // ---- GameObject ----
    api.gameObject = (int)api.types.size();
    api.types.push_back({});
    // ---- Transform ----
    api.transform = (int)api.types.size();
    api.types.push_back({});
    // ---- Vector3 ----
    api.vector3 = (int)api.types.size();
    api.types.push_back({});
    tGO = Type::Obj(api.gameObject); tTr = Type::Obj(api.transform);
    goSlot = api.gameObject; trSlot = api.transform;
}

void Ctx::BindStory() {
    const int goIdx = goSlot;
    // ---- Dialogue / Story / Quest ----
    {
        auto sceneOf = [](CallContext& c) -> Scene* { return c.self ? c.self->scene() : nullptr; };
        ApiType dg; dg.name = "Dialogue"; dg.isStatic = true;
        dg.doc = "Conversations: start an object's Dialogue Runner (by the object's or the graph's name) and ask whether one runs.";
        dg.staticMethods.push_back(method("Start", {tString}, {"name"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                DialogueRunner* r = nullptr;
                if (S(a[0]).empty() && c.self) r = c.self->GetComponent<DialogueRunner>();
                if (!r) r = DialogueRunner::FindByName(sceneOf(c), S(a[0]));
                if (!r) { Log::Warning("[script] Dialogue.Start: no Dialogue Runner named ", S(a[0])); return Value{false}; }
                return Value{r->StartDialogue()};
            }, "Start the conversation on the object (or graph) with this name; \"\" means this object's runner. False when there is none."));
        dg.staticMethods.push_back(method("Stop", {}, {}, tVoid,
            [sceneOf](const Value&, std::vector<Value>&, CallContext& c) { if (DialogueRunner* r = DialogueRunner::ActiveIn(sceneOf(c))) r->Stop(); return Value{}; },
            "End the running conversation."));
        dg.staticMethods.push_back(method("IsActive", {}, {}, tBool,
            [sceneOf](const Value&, std::vector<Value>&, CallContext& c) { return Value{DialogueRunner::ActiveIn(sceneOf(c)) != nullptr}; },
            "True while a conversation runs."));
        dg.staticMethods.back().pure = true;
        api.types.push_back(std::move(dg));

        ApiType st; st.name = "Story"; st.isStatic = true;
        st.doc = "The story's global variables and flags (the scene's Story State; saved with the game).";
        st.staticMethods.push_back(method("Set", {tString, tFloat}, {"name", "value"}, tVoid,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                if (StoryState* s = StoryState::Get(sceneOf(c), true)) s->Set(S(a[0]), vs::VsValue(F(a[1])));
                return Value{};
            }, "Set a number variable."));
        st.staticMethods.push_back(method("SetText", {tString, tString}, {"name", "value"}, tVoid,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                if (StoryState* s = StoryState::Get(sceneOf(c), true)) s->Set(S(a[0]), vs::VsValue(S(a[1])));
                return Value{};
            }, "Set a text variable."));
        st.staticMethods.push_back(method("Get", {tString}, {"name"}, tFloat,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                StoryState* s = StoryState::Get(sceneOf(c), false);
                return Value{s ? s->Get(S(a[0])).AsFloat() : 0.0f};
            }, "A variable as a number (0 when unset)."));
        st.staticMethods.back().pure = true;
        st.staticMethods.push_back(method("GetText", {tString}, {"name"}, tString,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                StoryState* s = StoryState::Get(sceneOf(c), false);
                return Value{s ? s->Get(S(a[0])).AsString() : std::string()};
            }, "A variable as text (\"\" when unset)."));
        st.staticMethods.back().pure = true;
        st.staticMethods.push_back(method("Has", {tString}, {"name"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                StoryState* s = StoryState::Get(sceneOf(c), false);
                return Value{s && s->Has(S(a[0]))};
            }, "True once the variable was set."));
        st.staticMethods.back().pure = true;
        st.staticMethods.push_back(method("SetFlag", {tString, tBool}, {"name", "on"}, tVoid,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                if (StoryState* s = StoryState::Get(sceneOf(c), true)) s->SetFlag(S(a[0]), std::get<bool>(a[1]));
                return Value{};
            }, "Set or clear a flag."));
        st.staticMethods.push_back(method("HasFlag", {tString}, {"name"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                StoryState* s = StoryState::Get(sceneOf(c), false);
                return Value{s && s->HasFlag(S(a[0]))};
            }, "True while the flag is set."));
        st.staticMethods.back().pure = true;
        api.types.push_back(std::move(st));

        ApiType q; q.name = "Quest"; q.isStatic = true;
        q.doc = "Quests in the scene's Quest Log: start, complete objectives, read states.";
        q.staticMethods.push_back(method("Start", {tString}, {"id"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), true);
                return Value{l && l->StartQuest(S(a[0]))};
            }, "Start a quest. False when it is unknown, already active or its prerequisites aren't done."));
        q.staticMethods.push_back(method("Complete", {tString}, {"id"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), false);
                return Value{l && l->CompleteQuest(S(a[0]))};
            }, "Complete an active quest outright (its rewards are granted)."));
        q.staticMethods.push_back(method("CompleteObjective", {tString, tInt}, {"id", "index"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), true);
                return Value{l && l->CompleteObjective(S(a[0]), I(a[1]))};
            }, "Mark one objective (0-based) of an active quest done."));
        q.staticMethods.push_back(method("AddProgress", {tString, tInt, tInt}, {"id", "index", "amount"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), true);
                return Value{l && l->AddProgress(S(a[0]), I(a[1]), I(a[2]))};
            }, "Count toward a collect / kill style objective."));
        q.staticMethods.push_back(method("Fail", {tString}, {"id"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), false);
                return Value{l && l->FailQuest(S(a[0]))};
            }, "Fail an active quest."));
        q.staticMethods.push_back(method("State", {tString}, {"id"}, tString,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), false);
                return Value{std::string(l ? l->StatusName(S(a[0])) : "inactive")};
            }, "\"inactive\", \"active\", \"completed\" or \"failed\"."));
        q.staticMethods.back().pure = true;
        q.staticMethods.push_back(method("IsActive", {tString}, {"id"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), false);
                return Value{l && l->IsActive(S(a[0]))};
            }, "True while the quest is active."));
        q.staticMethods.back().pure = true;
        api.types.push_back(std::move(q));
    }
    // ---- BehaviorTree ----
    {
        ApiType bt; bt.name = "BehaviorTree"; bt.isStatic = true;
        bt.doc = "This object's Behavior Tree Runner: its blackboard, and starting or stopping its tree.";
        auto runnerOf = [](CallContext& c) -> BehaviorTreeRunner* {
            auto* r = c.self ? c.self->GetComponent<BehaviorTreeRunner>() : nullptr;
            if (!r) throw RuntimeError("'" + (c.self ? c.self->name : std::string("?")) + "' has no Behavior Tree Runner (add one in the Inspector)", c.loc);
            return r;
        };
        auto toBt = [](const Value& v) -> BtValue {
            if (auto* b = std::get_if<bool>(&v)) return BtValue::Bool(*b);
            if (auto* i = std::get_if<std::int32_t>(&v)) return BtValue::Number((float)*i);
            if (auto* f = std::get_if<float>(&v)) return BtValue::Number(*f);
            if (auto* s = std::get_if<std::string>(&v)) return BtValue::String(*s);
            if (auto* x = std::get_if<Vec3>(&v)) return BtValue::Vector(*x);
            if (auto* o = std::get_if<ObjRef>(&v)) {
                GameObject* g = o->go ? GameObject::FromInstanceId(o->go) : nullptr;
                return BtValue::Object(g ? g->name : std::string(), o->go);
            }
            return BtValue::String(ValueToString(v));
        };
        auto fromBt = [goIdx](const BtValue& v, CallContext& c) -> Value {
            switch (v.type) {
                case BtValue::Type::Number: return Value{v.number};
                case BtValue::Type::Bool:   return Value{v.boolean};
                case BtValue::Type::String: return Value{v.text};
                case BtValue::Type::Vector: return Value{v.vector};
                case BtValue::Type::Object: return MakeRef(v.AsObject(c.self ? c.self->scene() : nullptr), goIdx);
            }
            return Value{};
        };
        bt.staticMethods.push_back(method("Set", {tString, tAny}, {"key", "value"}, tVoid,
            [runnerOf, toBt](const Value&, std::vector<Value>& a, CallContext& c) { runnerOf(c)->blackboard.Set(S(a[0]), toBt(a[1])); return Value{}; },
            "Write a blackboard value: a number, bool, string, Vector3 or GameObject."));
        bt.staticMethods.push_back(method("Get", {tString}, {"key"}, tAny,
            [runnerOf, fromBt](const Value&, std::vector<Value>& a, CallContext& c) {
                const BtValue* v = runnerOf(c)->blackboard.Find(S(a[0]));
                return v ? fromBt(*v, c) : Value{};
            }, "Read a blackboard value as whatever it is (use GetFloat / GetBool / GetString / GetVector / GetObject for a typed one)."));
        bt.staticMethods.back().pure = true;
        bt.staticMethods.push_back(method("GetFloat", {tString}, {"key"}, tFloat,
            [runnerOf](const Value&, std::vector<Value>& a, CallContext& c) { return Value{runnerOf(c)->blackboard.GetNumber(S(a[0]))}; },
            "A blackboard number (0 when the key is missing)."));
        bt.staticMethods.back().pure = true;
        bt.staticMethods.push_back(method("GetBool", {tString}, {"key"}, tBool,
            [runnerOf](const Value&, std::vector<Value>& a, CallContext& c) { return Value{runnerOf(c)->blackboard.GetBool(S(a[0]))}; },
            "A blackboard bool (false when the key is missing)."));
        bt.staticMethods.back().pure = true;
        bt.staticMethods.push_back(method("GetString", {tString}, {"key"}, tString,
            [runnerOf](const Value&, std::vector<Value>& a, CallContext& c) { return Value{runnerOf(c)->blackboard.GetString(S(a[0]))}; },
            "A blackboard string (\"\" when the key is missing)."));
        bt.staticMethods.back().pure = true;
        bt.staticMethods.push_back(method("GetVector", {tString}, {"key"}, tVec,
            [runnerOf](const Value&, std::vector<Value>& a, CallContext& c) { return Value{runnerOf(c)->blackboard.GetVector(S(a[0]))}; },
            "A blackboard vector, or an object key's position."));
        bt.staticMethods.back().pure = true;
        bt.staticMethods.push_back(method("GetObject", {tString}, {"key"}, tGO,
            [runnerOf, goIdx](const Value&, std::vector<Value>& a, CallContext& c) {
                return MakeRef(runnerOf(c)->blackboard.GetGameObject(S(a[0]), c.self ? c.self->scene() : nullptr), goIdx);
            }, "The object a blackboard key refers to, or null."));
        bt.staticMethods.back().pure = true;
        bt.staticMethods.push_back(method("Run", {tString}, {"tree"}, tBool,
            [runnerOf](const Value&, std::vector<Value>& a, CallContext& c) {
                BehaviorTreeRunner* r = runnerOf(c);
                if (!S(a[0]).empty()) {
                    std::string err;
                    if (!r->LoadTree(S(a[0]), &err)) { Log::Warning("[script] BehaviorTree.Run: ", err); return Value{false}; }
                }
                r->Run();
                return Value{r->IsRunning()};
            }, "Load a .okaybt file and start it (\"\" starts the current tree over). False when the file can't be read."));
        bt.staticMethods.push_back(method("Stop", {}, {}, tVoid,
            [runnerOf](const Value&, std::vector<Value>&, CallContext& c) { runnerOf(c)->Stop(); return Value{}; },
            "Stop the tree; running tasks are aborted and the NPC stops."));
        bt.staticProps.push_back(ro("isRunning", tBool,
            [runnerOf](const Value&, CallContext& c) { return Value{runnerOf(c)->IsRunning()}; },
            "True while the tree is being ticked."));
        api.types.push_back(std::move(bt));
    }
}

void Ctx::BindSplines() {
    // ---- Spline ----
    {
        ApiType sp; sp.name = "Spline"; sp.isComponent = true;
        sp.doc = "A curve through control points (the object's Spline). t is a fraction of the length, 0 to 1.";
        sp.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<Spline>() : nullptr; };
        auto splineOf = [](const Value& self, CallContext& c) -> Spline* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* s = g->GetComponent<Spline>();
            if (!s) throw RuntimeError("'" + g->name + "' has no Spline (add one in the Inspector)", c.loc);
            return s;
        };
        ApiMethod pos = method("EvaluatePosition", {tFloat}, {"t"}, tVec,
            [splineOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                const Spline* p = splineOf(s, c); return Value{p->SampleAtDistance(F(a[0]) * p->Length()).position}; },
            "The world point a fraction t (0 to 1) of the way along. Even steps of t are even steps on the curve.");
        pos.pure = true; sp.methods.push_back(std::move(pos));
        ApiMethod tan = method("EvaluateTangent", {tFloat}, {"t"}, tVec,
            [splineOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                const Spline* p = splineOf(s, c); return Value{p->SampleAtDistance(F(a[0]) * p->Length()).tangent}; },
            "The direction of travel a fraction t of the way along (a unit vector).");
        tan.pure = true; sp.methods.push_back(std::move(tan));
        ApiMethod len = method("Length", {}, {}, tFloat,
            [splineOf](const Value& s, std::vector<Value>&, CallContext& c) { return Value{splineOf(s, c)->Length()}; },
            "The length of the curve in world units.");
        len.pure = true; sp.methods.push_back(std::move(len));
        ApiMethod nearest = method("NearestPoint", {tVec}, {"position"}, tVec,
            [splineOf](const Value& s, std::vector<Value>& a, CallContext& c) { return Value{splineOf(s, c)->NearestPoint(V(a[0]))}; },
            "The point on the curve closest to a world position.");
        nearest.pure = true; sp.methods.push_back(std::move(nearest));
        sp.props.push_back(ro("pointCount", tInt,
            [splineOf](const Value& s, CallContext& c) { return Value{(std::int32_t)splineOf(s, c)->Count()}; },
            "How many control points it has."));
        sp.props.push_back(prop("closed", tBool,
            [splineOf](const Value& s, CallContext& c) { return Value{splineOf(s, c)->closed}; },
            [splineOf](const Value& s, const Value& v, CallContext& c) { splineOf(s, c)->closed = std::get<bool>(v); },
            "Whether the last point joins back to the first."));
        api.types.push_back(std::move(sp));
    }
    // ---- SplineFollower ----
    {
        ApiType sf; sf.name = "SplineFollower"; sf.isComponent = true;
        sf.doc = "Moves the object along a Spline (platforms, rails, carts, patrols). The object needs a Spline Follower.";
        sf.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<SplineFollower>() : nullptr; };
        auto followerOf = [](const Value& self, CallContext& c) -> SplineFollower* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* f = g->GetComponent<SplineFollower>();
            if (!f) throw RuntimeError("'" + g->name + "' has no Spline Follower (add one in the Inspector)", c.loc);
            return f;
        };
        sf.methods.push_back(method("Play", {}, {}, tVoid,
            [followerOf](const Value& s, std::vector<Value>&, CallContext& c) { followerOf(s, c)->Play(); return Value{}; },
            "Start (or carry on) moving."));
        sf.methods.push_back(method("Stop", {}, {}, tVoid,
            [followerOf](const Value& s, std::vector<Value>&, CallContext& c) { followerOf(s, c)->Stop(); return Value{}; },
            "Stop where it is."));
        sf.props.push_back(prop("speed", tFloat,
            [followerOf](const Value& s, CallContext& c) { return Value{followerOf(s, c)->speed}; },
            [followerOf](const Value& s, const Value& v, CallContext& c) { followerOf(s, c)->speed = F(v); },
            "Units per second along the curve (negative runs backwards)."));
        sf.props.push_back(prop("distance", tFloat,
            [followerOf](const Value& s, CallContext& c) { return Value{followerOf(s, c)->Distance()}; },
            [followerOf](const Value& s, const Value& v, CallContext& c) { followerOf(s, c)->SetDistance(F(v)); },
            "How far along the curve it is (setting it jumps there)."));
        sf.props.push_back(ro("isPlaying", tBool,
            [followerOf](const Value& s, CallContext& c) { return Value{followerOf(s, c)->IsPlaying()}; },
            "True while it moves."));
        api.types.push_back(std::move(sf));
    }
    // ---- LineRenderer ----
    {
        ApiType lr; lr.name = "LineRenderer"; lr.isComponent = true;
        lr.doc = "Draws a line through points (lasers, ropes, aiming arcs). The object needs a Line Renderer.";
        lr.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<LineRenderer>() : nullptr; };
        auto lineOf = [](const Value& self, CallContext& c) -> LineRenderer* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* l = g->GetComponent<LineRenderer>();
            if (!l) throw RuntimeError("'" + g->name + "' has no Line Renderer (add one in the Inspector)", c.loc);
            return l;
        };
        lr.methods.push_back(method("SetPosition", {tInt, tVec}, {"index", "position"}, tVoid,
            [lineOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                const std::int32_t i = I(a[0]);
                if (i < 0) throw RuntimeError("A point index can't be negative", c.loc);
                lineOf(s, c)->SetPosition(i, V(a[1])); return Value{}; },
            "Move one point (the line grows to reach a new index)."));
        lr.methods.push_back(method("SetPositions", {tAny}, {"positions"}, tVoid,
            [lineOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                const auto* arr = std::get_if<ArrayRef>(&a[0]);
                if (!arr || !*arr) throw RuntimeError("SetPositions needs an array or List of Vector3", c.loc);
                std::vector<Vec3> pts;
                for (const Value& v : (*arr)->items) {
                    const Vec3* p = std::get_if<Vec3>(&v);
                    if (!p) throw RuntimeError("SetPositions: every item must be a Vector3", c.loc);
                    pts.push_back(*p);
                }
                lineOf(s, c)->SetPositions(pts); return Value{}; },
            "Replace every point with an array or List of Vector3."));
        ApiMethod getp = method("GetPosition", {tInt}, {"index"}, tVec,
            [lineOf](const Value& s, std::vector<Value>& a, CallContext& c) { return Value{lineOf(s, c)->GetPosition(I(a[0]))}; },
            "One point of the line.");
        getp.pure = true; lr.methods.push_back(std::move(getp));
        lr.props.push_back(prop("positionCount", tInt,
            [lineOf](const Value& s, CallContext& c) { return Value{(std::int32_t)lineOf(s, c)->PositionCount()}; },
            [lineOf](const Value& s, const Value& v, CallContext& c) { lineOf(s, c)->SetPositionCount(I(v)); },
            "How many points the line has (setting it adds or drops points at the end)."));
        lr.props.push_back(prop("width", tFloat,
            [lineOf](const Value& s, CallContext& c) { return Value{lineOf(s, c)->width}; },
            [lineOf](const Value& s, const Value& v, CallContext& c) { lineOf(s, c)->width = std::max(0.0f, F(v)); },
            "Width in world units (times the width curve)."));
        api.types.push_back(std::move(lr));
    }
}

void Ctx::BindAnimation() {
    // ---- Animator (graph on this object or its model) ----
    {
        ApiType an; an.name = "Animator"; an.isComponent = true;
        an.doc = "The animation graph. On a player it is often on the imported model, and these calls find it.";
        auto graphOf = [](GameObject* go) -> AnimStateMachine* {
            if (!go || !go->scene()) return nullptr;
            if (auto* sm = go->GetComponent<AnimStateMachine>()) if (sm->layer == 0) return sm;
            for (const auto& up : go->scene()->Objects())
                if (up && up->IsSelfOrDescendantOf(go))
                    if (auto* sm = up->GetComponent<AnimStateMachine>())
                        if (sm->layer == 0) return sm;
            return nullptr;
        };
        auto modelOf = [](GameObject* go) -> ModelAnimator* {
            if (!go || !go->scene()) return nullptr;
            if (auto* m = go->GetComponent<ModelAnimator>()) return m;
            for (const auto& up : go->scene()->Objects())
                if (up && up->IsSelfOrDescendantOf(go))
                    if (auto* m = up->GetComponent<ModelAnimator>()) return m;
            return nullptr;
        };
        an.find = [](GameObject* go) -> Component* { return FindAnimatorComponent(go); };
        an.methods.push_back(method("SetTrigger", {tString}, {"name"}, tVoid,
            [graphOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                if (!sm) throw RuntimeError("This object has no Animator graph", c.loc);
                sm->SetTrigger(S(a[0])); return Value{}; },
            "Fire a trigger parameter (jump, for example)."));
        an.methods.push_back(method("SetBool", {tString, tBool}, {"name", "on"}, tVoid,
            [graphOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                if (!sm) throw RuntimeError("This object has no Animator graph", c.loc);
                bool on = false;
                if (auto* b = std::get_if<bool>(&a[1])) on = *b; else on = F(a[1]) != 0.0f;
                sm->SetBool(S(a[0]), on); return Value{}; },
            "Set a bool parameter (crouch, prone, grounded)."));
        an.methods.push_back(method("SetFloat", {tString, tFloat}, {"name", "value"}, tVoid,
            [graphOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                if (!sm) throw RuntimeError("This object has no Animator graph", c.loc);
                sm->SetFloat(S(a[0]), F(a[1])); return Value{}; },
            "Set a float parameter (speed)."));
        an.methods.push_back(method("Play", {tString}, {"clip"}, tVoid,
            [modelOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                ModelAnimator* m = modelOf(Resolve(std::get<ObjRef>(s), c.loc));
                if (!m || !m->Play(S(a[0]))) throw RuntimeError("This object has no clip by that name", c.loc);
                return Value{}; },
            "Play one of the model's clips by name."));
        {
            ApiMethod m = method("GetFloat", {tString}, {"name"}, tFloat,
                [graphOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                    AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                    return Value{sm ? sm->GetFloat(S(a[0])) : 0.0f}; },
                "Read a float parameter (speed). 0 if the graph has no such name.");
            m.pure = true; an.methods.push_back(std::move(m));
        }
        {
            ApiMethod m = method("GetBool", {tString}, {"name"}, tBool,
                [graphOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                    AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                    return Value{sm && sm->GetBool(S(a[0]))}; },
                "Read a bool parameter (crouch, grounded).");
            m.pure = true; an.methods.push_back(std::move(m));
        }
        {
            ApiMethod m = method("GetInt", {tString}, {"name"}, tInt,
                [graphOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                    AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                    return Value{(std::int32_t)(sm ? sm->GetInt(S(a[0])) : 0)}; },
                "Read an int parameter.");
            m.pure = true; an.methods.push_back(std::move(m));
        }
        an.methods.push_back(method("SetLayerWeight", {tString, tFloat}, {"layer", "weight"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GameObject* g = Resolve(std::get<ObjRef>(s), c.loc);
                AnimStateMachine* sm = FindAnimLayer(g, true, S(a[0]), 0);
                if (!sm) return Value{false};
                sm->layerWeight = F(a[1]);
                return Value{true}; },
            "Set the mix weight of the Animator layer with this name (on this object or its model)."));
        an.methods.push_back(method("SetLayerWeight", {tInt, tFloat}, {"layer", "weight"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GameObject* g = Resolve(std::get<ObjRef>(s), c.loc);
                AnimStateMachine* sm = FindAnimLayer(g, false, {}, I(a[0]));
                if (!sm) return Value{false};
                sm->layerWeight = F(a[1]);
                return Value{true}; },
            "Set the mix weight of the Animator layer with this index (0 is the base)."));
        {
            ApiMethod m = method("GetLayerWeight", {tString}, {"layer"}, tFloat,
                [](const Value& s, std::vector<Value>& a, CallContext& c) {
                    AnimStateMachine* sm = FindAnimLayer(Resolve(std::get<ObjRef>(s), c.loc), true, S(a[0]), 0);
                    return Value{sm ? sm->layerWeight : 0.0f}; },
                "Mix weight of the Animator layer with this name. 0 if there is no such layer.");
            m.pure = true; an.methods.push_back(std::move(m));
        }
        {
            ApiMethod m = method("GetLayerWeight", {tInt}, {"layer"}, tFloat,
                [](const Value& s, std::vector<Value>& a, CallContext& c) {
                    AnimStateMachine* sm = FindAnimLayer(Resolve(std::get<ObjRef>(s), c.loc), false, {}, I(a[0]));
                    return Value{sm ? sm->layerWeight : 0.0f}; },
                "Mix weight of the Animator layer with this index. 0 if there is no such layer.");
            m.pure = true; an.methods.push_back(std::move(m));
        }
        an.methods.push_back(method("SetRootMotion", {tBool}, {"on"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                bool on = false;
                if (auto* b = std::get_if<bool>(&a[0])) on = *b; else on = F(a[0]) != 0.0f;
                return Value{SetRootMotionOn(Resolve(std::get<ObjRef>(s), c.loc), on)}; },
            "Turn root motion on or off for the model on this object (or its child)."));
        an.methods.push_back(method("SetCharacterModel", {tString}, {"path"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                return Value{SetCharacterModelPath(Resolve(std::get<ObjRef>(s), c.loc), S(a[0]))}; },
            "Swap the character model for the file at this path. False if the file does not import."));
        an.props.push_back(ro("state", tString,
            [graphOf](const Value& s, CallContext& c) {
                AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                return Value{sm ? sm->Current() : std::string{}}; },
            "Name of the state the graph is in."));
        api.types.push_back(std::move(an));
    }
    // ---- IK ----
    {
        ApiType ik; ik.name = "IK"; ik.isComponent = true;
        ik.doc = "Look-At, feet and full-body IK on this object.";
        ik.find = [](GameObject* go) -> Component* { return FindIkComponent(go); };
        ik.methods.push_back(method("LookAt", {tString}, {"objectName"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GameObject* g = Resolve(std::get<ObjRef>(s), c.loc);
                if (!g->scene()) throw RuntimeError("This object is not in a scene", c.loc);
                GameObject* tgt = g->scene()->Find(S(a[0]));
                if (!tgt) throw RuntimeError("No object named '" + S(a[0]) + "'", c.loc);
                LookAtIK* look = g->GetComponent<LookAtIK>();
                if (!look) look = g->AddComponent<LookAtIK>();
                look->targetObject = tgt->transform;
                look->targetName = tgt->name;
                look->ikActive = true;
                look->weight = look->weight > 0.0f ? look->weight : 1.0f;
                return Value{}; },
            "Aim Look-At IK at the object with this name."));
        ik.methods.push_back(method("SetWeight", {tFloat}, {"weight"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GameObject* g = Resolve(std::get<ObjRef>(s), c.loc);
                const float w = F(a[0]);
                if (auto* l = g->GetComponent<LookAtIK>()) l->weight = w;
                if (auto* f = g->GetComponent<FootIK>()) f->weight = w;
                if (auto* b = g->GetComponent<FullBodyIK>()) b->weight = w;
                return Value{}; },
            "Set the weight (0..1) of Look-At, Foot and Full Body IK on this object."));
        ik.methods.push_back(method("SetKindWeight", {tString, tFloat}, {"kind", "weight"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                return Value{SetIkKindWeight(Resolve(std::get<ObjRef>(s), c.loc), S(a[0]), F(a[1]))}; },
            "Set one IK weight: foot, look, aim, hand_l, hand_r or full. Also looks on children."));
        ik.methods.push_back(method("SetLimbTarget", {tString, tString}, {"limb", "objectName"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GameObject* g = Resolve(std::get<ObjRef>(s), c.loc);
                if (!g->scene()) return Value{false};
                GameObject* named = g->scene()->Find(S(a[1]));
                if (!named) return Value{false};
                return Value{SetIkLimbTarget(g, S(a[0]), named, nullptr)}; },
            "Reach or look at the object with this name. Limb is hand_l, hand_r, foot_l, foot_r, look, aim or head."));
        ik.methods.push_back(method("SetLimbTarget", {tString, tFloat, tFloat, tFloat}, {"limb", "x", "y", "z"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                Vec3 p{F(a[1]), F(a[2]), F(a[3])};
                return Value{SetIkLimbTarget(Resolve(std::get<ObjRef>(s), c.loc), S(a[0]), nullptr, &p)}; },
            "Reach or look at a world point. Limb is hand_l, hand_r, foot_l, foot_r, look, aim or head."));
        ik.methods.push_back(method("ClearLimbTarget", {tString}, {"limb"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                return Value{ClearIkLimbTarget(Resolve(std::get<ObjRef>(s), c.loc), S(a[0]))}; },
            "Drop the goal for that limb so the animation owns it again."));
        api.types.push_back(std::move(ik));
    }
}

void Ctx::BindTrail() {
    // ---- TrailRenderer ----
    {
        ApiType tr; tr.name = "TrailRenderer"; tr.isComponent = true;
        tr.doc = "Leaves a fading ribbon behind the moving object. The object needs a Trail Renderer.";
        tr.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<TrailRenderer>() : nullptr; };
        auto trailOf = [](const Value& self, CallContext& c) -> TrailRenderer* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* t = g->GetComponent<TrailRenderer>();
            if (!t) throw RuntimeError("'" + g->name + "' has no Trail Renderer (add one in the Inspector)", c.loc);
            return t;
        };
        tr.methods.push_back(method("Clear", {}, {}, tVoid,
            [trailOf](const Value& s, std::vector<Value>&, CallContext& c) { trailOf(s, c)->Clear(); return Value{}; },
            "Remove the trail at once (it starts again from where the object is)."));
        tr.props.push_back(prop("emitting", tBool,
            [trailOf](const Value& s, CallContext& c) { return Value{trailOf(s, c)->emitting}; },
            [trailOf](const Value& s, const Value& v, CallContext& c) { trailOf(s, c)->emitting = std::get<bool>(v); },
            "Whether new points are added as it moves (off: the trail fades away)."));
        api.types.push_back(std::move(tr));
    }
}

void Ctx::BindMathf() {
    // ---- Mathf ----
    {
        ApiType m; m.name = "Mathf"; m.isStatic = true; m.doc = "Maths helpers.";
        m.staticProps.push_back(ro("PI", tFloat, [](const Value&, CallContext&) { return Value{Mathf::PI}; }, "3.14159..."));
        auto f1 = [&](const char* n, float (*fn)(float), const char* doc) {
            m.staticMethods.push_back(method(n, {tFloat}, {"value"}, tFloat,
                [fn](const Value&, std::vector<Value>& a, CallContext&) { return Value{fn(F(a[0]))}; }, doc));
        };
        f1("Abs", [](float x) { return std::fabs(x); }, "The value without its sign.");
        f1("Sqrt", [](float x) { return std::sqrt(std::max(0.0f, x)); }, "Square root.");
        f1("Sin", [](float x) { return std::sin(x); }, "Sine (radians).");
        f1("Cos", [](float x) { return std::cos(x); }, "Cosine (radians).");
        f1("Tan", [](float x) { return std::tan(x); }, "Tangent (radians).");
        f1("Floor", [](float x) { return std::floor(x); }, "Round down.");
        f1("Ceil", [](float x) { return std::ceil(x); }, "Round up.");
        f1("Round", [](float x) { return std::round(x); }, "Round to the nearest whole number.");
        f1("Sign", [](float x) { return x < 0.0f ? -1.0f : 1.0f; }, "-1 for negative values, else 1.");
        f1("Clamp01", [](float x) { return Mathf::Clamp(x, 0.0f, 1.0f); }, "Keep between 0 and 1.");
        auto f2 = [&](const char* n, float (*fn)(float, float), const char* a, const char* b, const char* doc) {
            m.staticMethods.push_back(method(n, {tFloat, tFloat}, {a, b}, tFloat,
                [fn](const Value&, std::vector<Value>& x, CallContext&) { return Value{fn(F(x[0]), F(x[1]))}; }, doc));
        };
        f2("Min", [](float a, float b) { return std::min(a, b); }, "a", "b", "The smaller value.");
        f2("Max", [](float a, float b) { return std::max(a, b); }, "a", "b", "The larger value.");
        f2("Pow", [](float a, float b) { return std::pow(a, b); }, "value", "power", "value to the power.");
        f2("Atan2", [](float y, float x) { return std::atan2(y, x); }, "y", "x", "Angle of (x, y) in radians.");
        auto f3 = [&](const char* n, float (*fn)(float, float, float), std::vector<std::string> names, const char* doc) {
            m.staticMethods.push_back(method(n, {tFloat, tFloat, tFloat}, std::move(names), tFloat,
                [fn](const Value&, std::vector<Value>& x, CallContext&) { return Value{fn(F(x[0]), F(x[1]), F(x[2]))}; }, doc));
        };
        f3("Clamp", [](float v, float lo, float hi) { return Mathf::Clamp(v, lo, hi); }, {"value", "min", "max"}, "Keep between min and max.");
        f3("Lerp", [](float a, float b, float t) { return a + (b - a) * Mathf::Clamp(t, 0.0f, 1.0f); }, {"a", "b", "t"}, "Blend from a (t = 0) to b (t = 1).");
        f3("MoveTowards", [](float c, float t, float d) { return std::fabs(t - c) <= d ? t : c + (t > c ? d : -d); },
           {"current", "target", "maxDelta"}, "Step from current toward target by at most maxDelta.");
        api.types.push_back(std::move(m));
    }
}

} // namespace engine_api

namespace {

EngineApi Build() {
    Ctx ctx;
    EngineApi& api = ctx.api;
    ctx.Init();
    ctx.BindCore();
    ctx.BindGameplay();
    ctx.BindUiControls();
    ctx.BindUiApp();
    ctx.BindUiLibrary();
    ctx.BindInput();
    ctx.BindRuntime();
    ctx.BindRendering();
    ctx.BindNavigation();
    ctx.BindStory();
    ctx.BindSplines();
    ctx.BindAnimation();
    ctx.BindTrail();
    ctx.BindMathf();
    AddSharedBindings(api);
    // Queries and maths have no side effects: in a visual script they're value
    // nodes, not steps in the flow.
    for (ApiType& t : api.types)
        if (t.name == "GameObject" || t.name == "Vector3" || t.name == "Input" || t.name == "Mathf")
            for (ApiMethod& m : t.staticMethods) m.pure = m.name != "Rumble" && m.name != "RebindAction" &&
                                                      m.name != "DefineAction" && m.name != "DefineDirection";
    return std::move(api);
}

} // namespace


const EngineApi& EngineApi::Get() {
    static const EngineApi api = Build();
    return api;
}

} // namespace okay::script
