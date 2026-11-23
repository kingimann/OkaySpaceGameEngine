// Typed script API, Animator and IK: the Bind<Topic> functions of the binding table (see EngineApiInternal.hpp).
#include "EngineApiInternal.hpp"

namespace okay::script::engine_api {

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

} // namespace okay::script::engine_api
