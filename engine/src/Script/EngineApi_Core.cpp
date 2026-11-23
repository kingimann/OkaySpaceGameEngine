// Typed script API, GameObject, Transform, Vector3, Rigidbody, PlayerView and PlayerController: the Bind<Topic> functions of the binding table (see EngineApiInternal.hpp).
#include "EngineApiInternal.hpp"

namespace okay::script::engine_api {

void Ctx::BindCore() {
    const int goIdx = goSlot;
    const int trIdx = trSlot;
    {
        ApiType& g = api.types[(std::size_t)api.gameObject];
        g.name = "GameObject";
        g.doc = "An object in the scene.";
        g.props.push_back(prop("name", tString,
            [](const Value& s, CallContext& c) { return Value{Resolve(std::get<ObjRef>(s), c.loc)->name}; },
            [](const Value& s, const Value& v, CallContext& c) { Resolve(std::get<ObjRef>(s), c.loc)->name = S(v); },
            "The object's name (as shown in the Hierarchy)."));
        g.props.push_back(prop("tag", tString,
            [](const Value& s, CallContext& c) { return Value{Resolve(std::get<ObjRef>(s), c.loc)->tag}; },
            [](const Value& s, const Value& v, CallContext& c) { Resolve(std::get<ObjRef>(s), c.loc)->tag = S(v); },
            "The object's tag."));
        g.props.push_back(prop("active", tBool,
            [](const Value& s, CallContext& c) { return Value{Resolve(std::get<ObjRef>(s), c.loc)->active}; },
            [](const Value& s, const Value& v, CallContext& c) { Resolve(std::get<ObjRef>(s), c.loc)->active = std::get<bool>(v); },
            "Whether the object is active. Inactive objects don't update or draw."));
        g.props.push_back(ro("transform", tTr,
            [trIdx](const Value& s, CallContext& c) { return MakeRef(Resolve(std::get<ObjRef>(s), c.loc), trIdx); },
            "The object's Transform (position, rotation, scale)."));
        g.staticMethods.push_back(method("Find", {tString}, {"name"}, tGO,
            [goIdx](const Value&, std::vector<Value>& a, CallContext& c) {
                Scene* sc = c.self ? c.self->scene() : nullptr;
                return sc ? MakeRef(sc->Find(S(a[0])), goIdx) : Value{};
            }, "The first active object with this name, or null if there is none."));
        g.staticMethods.push_back(method("FindWithTag", {tString}, {"tag"}, tGO,
            [goIdx](const Value&, std::vector<Value>& a, CallContext& c) {
                Scene* sc = c.self ? c.self->scene() : nullptr;
                return sc ? MakeRef(sc->FindWithTag(S(a[0])), goIdx) : Value{};
            }, "The first active object with this tag, or null if there is none."));
    }
    {
        ApiType& t = api.types[(std::size_t)api.transform];
        t.name = "Transform";
        t.isComponent = true;
        t.doc = "Position, rotation and scale of an object.";
        t.find = [](GameObject* go) -> Component* { return go ? go->transform : nullptr; };
        t.props.push_back(prop("position", tVec,
            [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->Position()}; },
            [](const Value& s, const Value& v, CallContext& c) { SelfTransform(s, c)->SetPosition(V(v)); },
            "Position in the world."));
        t.props.push_back(prop("localPosition", tVec,
            [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->localPosition}; },
            [](const Value& s, const Value& v, CallContext& c) { SelfTransform(s, c)->localPosition = V(v); },
            "Position relative to the parent."));
        t.props.push_back(prop("eulerAngles", tVec,
            [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->Rotation().ToEuler()}; },
            [](const Value& s, const Value& v, CallContext& c) { SelfTransform(s, c)->SetRotation(Quat::Euler(V(v))); },
            "Rotation in the world, as X / Y / Z angles in degrees."));
        t.props.push_back(prop("localEulerAngles", tVec,
            [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->localRotation.ToEuler()}; },
            [](const Value& s, const Value& v, CallContext& c) { SelfTransform(s, c)->localRotation = Quat::Euler(V(v)); },
            "Rotation relative to the parent, in degrees."));
        t.props.push_back(prop("localScale", tVec,
            [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->localScale}; },
            [](const Value& s, const Value& v, CallContext& c) { SelfTransform(s, c)->localScale = V(v); },
            "Scale relative to the parent."));
        t.props.push_back(ro("forward", tVec, [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->Forward()}; },
            "The direction the object faces (its +Z), in the world."));
        t.props.push_back(ro("right", tVec, [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->Right()}; },
            "The object's right (+X), in the world."));
        t.props.push_back(ro("up", tVec, [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->Up()}; },
            "The object's up (+Y), in the world."));
        t.props.push_back(ro("gameObject", tGO,
            [goIdx](const Value& s, CallContext& c) { return MakeRef(Resolve(std::get<ObjRef>(s), c.loc), goIdx); },
            "The object this Transform belongs to."));
        t.methods.push_back(method("Translate", {tVec}, {"offset"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { SelfTransform(s, c)->Translate(V(a[0])); return Value{}; },
            "Move by an offset (relative to the parent)."));
        t.methods.push_back(method("Translate", {tFloat, tFloat, tFloat}, {"x", "y", "z"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { SelfTransform(s, c)->Translate({F(a[0]), F(a[1]), F(a[2])}); return Value{}; },
            "Move by x, y, z (relative to the parent)."));
        t.methods.push_back(method("Rotate", {tVec}, {"degrees"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { SelfTransform(s, c)->Rotate(V(a[0])); return Value{}; },
            "Turn by X / Y / Z degrees around the object's own axes."));
        t.methods.push_back(method("Rotate", {tFloat, tFloat, tFloat}, {"x", "y", "z"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { SelfTransform(s, c)->Rotate({F(a[0]), F(a[1]), F(a[2])}); return Value{}; },
            "Turn by x, y, z degrees around the object's own axes."));
        t.methods.push_back(method("LookAt", {tVec}, {"target"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                Transform* tr = SelfTransform(s, c);
                Vec3 d = V(a[0]) - tr->Position();
                if (d.Magnitude() > 1e-6f) tr->SetRotation(Quat::LookRotation(d));
                return Value{};
            }, "Turn so forward points at a world position."));
    }
    {
        ApiType& v = api.types[(std::size_t)api.vector3];
        v.name = "Vector3";
        v.isValueType = true;
        v.doc = "A point or direction: x, y, z.";
        v.props.push_back(ro("magnitude", tFloat, [](const Value& s, CallContext&) { return Value{V(s).Magnitude()}; }, "The length."));
        v.props.push_back(ro("normalized", tVec, [](const Value& s, CallContext&) { return Value{V(s).Normalized()}; }, "The same direction with length 1."));
        auto constant = [&](const char* n, Vec3 c, const char* doc) {
            v.staticProps.push_back(ro(n, tVec, [c](const Value&, CallContext&) { return Value{c}; }, doc));
        };
        constant("zero", Vec3::Zero, "(0, 0, 0)");
        constant("one", Vec3::One, "(1, 1, 1)");
        constant("up", Vec3::Up, "(0, 1, 0)");
        constant("down", Vec3::Down, "(0, -1, 0)");
        constant("right", Vec3::Right, "(1, 0, 0)");
        constant("left", Vec3::Left, "(-1, 0, 0)");
        constant("forward", Vec3::Forward, "(0, 0, 1)");
        constant("back", Vec3{0, 0, -1}, "(0, 0, -1)");
        v.staticMethods.push_back(method("Distance", {tVec, tVec}, {"a", "b"}, tFloat,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{Vec3::Distance(V(a[0]), V(a[1]))}; }, "Distance between two points."));
        v.staticMethods.push_back(method("Dot", {tVec, tVec}, {"a", "b"}, tFloat,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{Vec3::Dot(V(a[0]), V(a[1]))}; }, "Dot product."));
        v.staticMethods.push_back(method("Cross", {tVec, tVec}, {"a", "b"}, tVec,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{Vec3::Cross(V(a[0]), V(a[1]))}; }, "Cross product."));
        v.staticMethods.push_back(method("Lerp", {tVec, tVec, tFloat}, {"a", "b", "t"}, tVec,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{Vec3::LerpUnclamped(V(a[0]), V(a[1]), Mathf::Clamp(F(a[2]), 0.0f, 1.0f))}; },
            "Blend from a (t = 0) to b (t = 1)."));
        v.staticMethods.push_back(method("MoveTowards", {tVec, tVec, tFloat}, {"current", "target", "maxDistance"}, tVec,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{Vec3::MoveTowards(V(a[0]), V(a[1]), F(a[2]))}; },
            "Step from current toward target, moving at most maxDistance."));
    }
}

void Ctx::BindGameplay() {
    // ---- Rigidbody ----
    {
        api.rigidbody = (int)api.types.size();
        ApiType r; r.name = "Rigidbody"; r.isComponent = true;
        r.doc = "A physics body: velocity, mass and forces. The object needs a Rigidbody component.";
        r.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<Rigidbody3D>() : nullptr; };
        r.props.push_back(prop("velocity", tVec,
            [](const Value& s, CallContext& c) { return Value{Body(s, c)->velocity}; },
            [](const Value& s, const Value& v, CallContext& c) { Rigidbody3D* b = Body(s, c); b->velocity = V(v); b->WakeUp(); },
            "Speed and direction in units per second."));
        r.props.push_back(prop("angularVelocity", tVec,
            [](const Value& s, CallContext& c) { return Value{Body(s, c)->angularVelocity}; },
            [](const Value& s, const Value& v, CallContext& c) { Rigidbody3D* b = Body(s, c); b->angularVelocity = V(v); b->WakeUp(); },
            "Spin around X / Y / Z in radians per second."));
        r.props.push_back(prop("mass", tFloat,
            [](const Value& s, CallContext& c) { return Value{Body(s, c)->mass}; },
            [](const Value& s, const Value& v, CallContext& c) {
                if (F(v) <= 0.0f) throw RuntimeError("mass must be more than 0", c.loc);
                Body(s, c)->mass = F(v);
            }, "How heavy the body is (more than 0)."));
        r.props.push_back(prop("gravityScale", tFloat,
            [](const Value& s, CallContext& c) { return Value{Body(s, c)->gravityScale}; },
            [](const Value& s, const Value& v, CallContext& c) { Body(s, c)->gravityScale = F(v); },
            "How strongly gravity pulls it: 1 normal, 0 none."));
        r.props.push_back(prop("drag", tFloat,
            [](const Value& s, CallContext& c) { return Value{Body(s, c)->drag}; },
            [](const Value& s, const Value& v, CallContext& c) { Body(s, c)->drag = std::max(0.0f, F(v)); },
            "How quickly it slows down by itself (0 = never)."));
        r.props.push_back(prop("isKinematic", tBool,
            [](const Value& s, CallContext& c) { return Value{Body(s, c)->bodyType == Rigidbody3D::BodyType::Kinematic}; },
            [](const Value& s, const Value& v, CallContext& c) {
                Body(s, c)->bodyType = std::get<bool>(v) ? Rigidbody3D::BodyType::Kinematic : Rigidbody3D::BodyType::Dynamic;
            }, "When true, forces and gravity don't move it; only scripts do."));
        // AddForce, AddImpulse and AddTorque come from the shared table (Scripting/ScriptApiCore.cpp).
        r.methods.push_back(method("AddForceAtPosition", {tVec, tVec}, {"force", "point"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { Body(s, c)->AddForceAtPosition(V(a[0]), V(a[1])); return Value{}; },
            "Push it at a world point (off-centre pushes also spin it)."));
        api.types.push_back(std::move(r));
    }
    // ---- PlayerView (grok/view-toggle). Visual-script nodes come from this type. ----
    {
        ApiType pv; pv.name = "PlayerView"; pv.isComponent = true;
        pv.doc = "Switches one player between first person and third person. A Player Controller with View set to Both has one; PlayerController has the same calls.";
        pv.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<PlayerView>() : nullptr; };
        auto viewOf = [](const Value& self, CallContext& c) -> PlayerView* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* v = g->GetComponent<PlayerView>();
            if (!v) throw RuntimeError("'" + g->name + "' has no view toggle (set its Player Controller's View to Both)", c.loc);
            return v;
        };
        pv.methods.push_back(method("SetView", {tInt}, {"view"}, tVoid,
            [viewOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                viewOf(s, c)->SetView(I(a[0]));
                return Value{};
            }, "Ease to a view: 0 first person (camera at the eyes), 1 third person (over the shoulder)."));
        ApiMethod get = method("GetView", {}, {}, tInt,
            [viewOf](const Value& s, std::vector<Value>&, CallContext& c) {
                return Value{(std::int32_t)viewOf(s, c)->GetView()};
            }, "The view asked for: 0 first person, 1 third person. The camera may still be on its way there.");
        get.pure = true;
        pv.methods.push_back(std::move(get));
        api.types.push_back(std::move(pv));
    }
    // ---- PlayerController: the one first / third person controller. ----
    {
        ApiType pc; pc.name = "PlayerController"; pc.isComponent = true;
        pc.doc = "The first / third person controller. Its View says which views the player has; "
                 "SetView only switches when it is Both.";
        pc.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<PlayerController>() : nullptr; };
        auto ctrlOf = [](const Value& self, CallContext& c) -> PlayerController* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* p = g->GetComponent<PlayerController>();
            if (!p) throw RuntimeError("'" + g->name + "' has no Player Controller (add one in the Inspector)", c.loc);
            return p;
        };
        pc.methods.push_back(method("SetView", {tInt}, {"view"}, tVoid,
            [ctrlOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                PlayerController* p = ctrlOf(s, c);
                if (PlayerView* v = p->toggle()) v->SetView(I(a[0]));
                return Value{};
            }, "Ease to a view: 0 first person, 1 third person. Does nothing unless the View is Both."));
        ApiMethod get = method("GetView", {}, {}, tInt,
            [ctrlOf](const Value& s, std::vector<Value>&, CallContext& c) {
                return Value{(std::int32_t)ctrlOf(s, c)->CurrentView()};
            }, "The view the player is in: 0 first person, 1 third person.");
        get.pure = true;
        pc.methods.push_back(std::move(get));
        pc.methods.push_back(method("SetViewMode", {tInt}, {"mode"}, tVoid,
            [ctrlOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                const int m = I(a[0]);
                ctrlOf(s, c)->SetViewMode(m <= 0 ? PlayerController::ViewMode::First
                                        : m == 1 ? PlayerController::ViewMode::Third
                                                 : PlayerController::ViewMode::Both);
                return Value{};
            }, "Which views the player has: 0 first person only, 1 third person only, 2 both."));
        api.types.push_back(std::move(pc));
    }
}

} // namespace okay::script::engine_api
