// Typed script API, Spline, LineRenderer and TrailRenderer: the Bind<Topic> functions of the binding table (see EngineApiInternal.hpp).
#include "EngineApiInternal.hpp"

namespace okay::script::engine_api {

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

} // namespace okay::script::engine_api
