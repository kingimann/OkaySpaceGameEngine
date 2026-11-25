// Typed script API, Dialogue, Story, Quest and BehaviorTree: the Bind<Topic> functions of the binding table (see EngineApiInternal.hpp).
#include "EngineApiInternal.hpp"

namespace okay::script::engine_api {

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

} // namespace okay::script::engine_api
