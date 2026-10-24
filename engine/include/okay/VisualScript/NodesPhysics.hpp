#pragma once
// ---------------------------------------------------------------------------
// Visual-script physics nodes: 3D raycasts and shape casts, overlaps, and the
// contact details of the collision / trigger event that started the chain.
// Casts start at this object (Origin unwired) and point along its forward
// (Direction unwired), and never hit this object's own colliders. Their text
// setting is an optional layer list ("0,3" / "Default,Enemy"; blank = all).
// ---------------------------------------------------------------------------
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Layers.hpp"
#include "okay/Core/Time.hpp"
#include <string>
#include <vector>

namespace okay::vs {

// Shared bits for nodes that look at the scene from this object.
struct VsPhysicsNode : VsNode {
    std::uint32_t mask = Physics3D::kAllLayers;
    explicit VsPhysicsNode(const std::string& layers) : mask(Layers::MaskFromList(layers)) {}
    static Scene* SceneOf(VsContext& ctx) { return ctx.gameObject ? ctx.gameObject->scene() : nullptr; }
    static Vec3 SelfPos(VsContext& ctx) { return ctx.transform ? ctx.transform->Position() : Vec3::Zero; }
    static Vec3 SelfFwd(VsContext& ctx) { return ctx.transform ? ctx.transform->Forward() : Vec3{0, 0, 1}; }
    static VsValue HitPin(int pin, const RaycastHit3D& h) {
        switch (pin) {
            case 0:  return h.hit;
            case 1:  return h.point;
            case 2:  return h.normal;
            case 3:  return h.distance;
            default: return h.gameObject ? h.gameObject->name : std::string();
        }
    }
    static VsValue Names(const std::vector<GameObject*>& objs) {
        VsValue out = VsValue::MakeArray();
        for (GameObject* o : objs) out.AsArray()->push_back(VsValue(o ? o->name : std::string()));
        return out;
    }
};

// Raycast 3D: in0 Origin, in1 Direction, in2 Max Dist, in3 Ignore Triggers ->
// out Hit, Point, Normal, Distance, Object (name).
struct Raycast3DNode : VsPhysicsNode {
    using VsPhysicsNode::VsPhysicsNode;
    std::uint64_t recordedFrame = ~0ull;   // record the debug ray once per frame
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        Scene* s = SceneOf(ctx);
        if (!s) return HitPin(pin, RaycastHit3D{});
        Vec3 o = In(0, g, ctx, VsValue(SelfPos(ctx))).AsVec3();
        Vec3 d = In(1, g, ctx, VsValue(SelfFwd(ctx))).AsVec3();
        float dist = In(2, g, ctx, VsValue(100.0f)).AsFloat();
        bool skipTriggers = In(3, g, ctx, VsValue(false)).AsBool();
        if (dist <= 0.0f) dist = 1e9f;
        RaycastHit3D h = s->physics3D().Raycast(*s, o, d, dist, ctx.gameObject, mask, !skipTriggers);
        if (recordedFrame != Time::FrameCount()) { recordedFrame = Time::FrameCount(); s->physics3D().RecordQueryRay(o, d, dist, h); }
        return HitPin(pin, h);
    }
};

// Sphere Cast: in0 Origin, in1 Direction, in2 Radius, in3 Max Dist -> same outs
// as Raycast 3D. A thick ray: skips triggers, won't slip through small gaps.
struct SphereCast3DNode : VsPhysicsNode {
    using VsPhysicsNode::VsPhysicsNode;
    std::uint64_t recordedFrame = ~0ull;
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        Scene* s = SceneOf(ctx);
        if (!s) return HitPin(pin, RaycastHit3D{});
        Vec3 o = In(0, g, ctx, VsValue(SelfPos(ctx))).AsVec3();
        Vec3 d = In(1, g, ctx, VsValue(SelfFwd(ctx))).AsVec3();
        float r = In(2, g, ctx, VsValue(0.5f)).AsFloat();
        float dist = In(3, g, ctx, VsValue(100.0f)).AsFloat();
        if (dist <= 0.0f) dist = 1e9f;
        RaycastHit3D h = s->physics3D().SphereCast(*s, o, d, r, dist, ctx.gameObject, mask, false);
        if (recordedFrame != Time::FrameCount()) { recordedFrame = Time::FrameCount(); s->physics3D().RecordQueryRay(o, d, dist, h, r); }
        return HitPin(pin, h);
    }
};

// Raycast All 3D: in0 Origin, in1 Direction, in2 Max Dist -> out Count, Objects
// (names, nearest first), Nearest (name).
struct RaycastAll3DNode : VsPhysicsNode {
    using VsPhysicsNode::VsPhysicsNode;
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        Scene* s = SceneOf(ctx);
        std::vector<GameObject*> objs;
        if (s) {
            Vec3 o = In(0, g, ctx, VsValue(SelfPos(ctx))).AsVec3();
            Vec3 d = In(1, g, ctx, VsValue(SelfFwd(ctx))).AsVec3();
            float dist = In(2, g, ctx, VsValue(100.0f)).AsFloat();
            if (dist <= 0.0f) dist = 1e9f;
            for (const RaycastHit3D& h : s->physics3D().RaycastAll(*s, o, d, dist, ctx.gameObject, mask))
                objs.push_back(h.gameObject);
        }
        if (pin == 0) return (float)objs.size();
        if (pin == 1) return Names(objs);
        return objs.empty() || !objs.front() ? std::string() : objs.front()->name;
    }
};

// Overlap Sphere / Overlap Box: in0 Center, in1 Radius (sphere) or Half Size
// (box, a vector) -> out Count, Objects (names). This object is left out.
struct Overlap3DNode : VsPhysicsNode {
    bool box;
    Overlap3DNode(bool isBox, const std::string& layers) : VsPhysicsNode(layers), box(isBox) {}
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        Scene* s = SceneOf(ctx);
        std::vector<GameObject*> objs;
        if (s) {
            Vec3 c = In(0, g, ctx, VsValue(SelfPos(ctx))).AsVec3();
            std::vector<Collider3D*> cols = box
                ? s->physics3D().OverlapBox(*s, c, In(1, g, ctx, VsValue(Vec3{0.5f, 0.5f, 0.5f})).AsVec3(),
                                            Quat::Identity, mask, true, ctx.gameObject)
                : s->physics3D().OverlapSphere(*s, c, In(1, g, ctx, VsValue(1.0f)).AsFloat(), mask, true, ctx.gameObject);
            for (Collider3D* col : cols) {
                bool dup = false;   // one entry per object, even with several colliders
                for (GameObject* o : objs) if (o == col->gameObject) { dup = true; break; }
                if (!dup) objs.push_back(col->gameObject);
            }
        }
        if (pin == 0) return (float)objs.size();
        return Names(objs);
    }
};

// Contact Point / Contact Normal / Impact Speed: details of the collision or
// trigger that started this chain (VisualScriptComponent leaves them on the
// blackboard before running On Collision / On Trigger ... entries).
struct ContactInfoNode : VsNode {
    enum class What { Point, Normal, Speed } what;
    explicit ContactInfoNode(What w) : what(w) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        const char* key = what == What::Point ? "contact_point" : what == What::Normal ? "contact_normal" : "impact_speed";
        auto it = ctx.variables.find(key);
        if (it != ctx.variables.end()) return it->second;
        return what == What::Speed ? VsValue(0.0f) : VsValue(Vec3::Zero);
    }
};

} // namespace okay::vs
