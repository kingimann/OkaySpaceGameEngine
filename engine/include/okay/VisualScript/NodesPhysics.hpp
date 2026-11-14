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
#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
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
            default: return VsValue::Object(h.gameObject ? h.gameObject->InstanceId() : 0);   // reads as its name
        }
    }
    static VsValue Names(const std::vector<GameObject*>& objs) {
        VsValue out = VsValue::MakeArray();
        for (GameObject* o : objs) out.AsArray()->push_back(VsValue::Object(o ? o->InstanceId() : 0));
        return out;
    }
};

// Raycast 3D: in0 Origin, in1 Direction, in2 Max Dist, in3 Ignore Triggers ->
// out Hit, Point, Normal, Distance, Object (an object; reads as its name as text).
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
// (objects, nearest first), Nearest (object).
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
        return VsValue::Object(objs.empty() || !objs.front() ? 0 : objs.front()->InstanceId());
    }
};

// Overlap Sphere / Overlap Box: in0 Center, in1 Radius (sphere) or Half Size
// (box, a vector) -> out Count, Objects (objects). This object is left out.
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

// Flat (side-view) casts. Pins are the same shape as the 3D casts, and Z is ignored.
// Unwired, a cast starts at this object and points along its right (the side-view facing).
struct VsPhysics2DNode : VsPhysicsNode {
    using VsPhysicsNode::VsPhysicsNode;
    static Vec2 Flat(const VsValue& v) { Vec3 p = v.AsVec3(); return {p.x, p.y}; }
    static Vec2 SelfPos2(VsContext& ctx) {
        Vec3 p = SelfPos(ctx); return {p.x, p.y};
    }
    static Vec2 SelfRight2(VsContext& ctx) {
        if (!ctx.transform) return {1.0f, 0.0f};
        Vec3 r = ctx.transform->Right();
        Vec2 d{r.x, r.y};
        return d.SqrMagnitude() > 1e-8f ? d : Vec2{1.0f, 0.0f};
    }
    static VsValue HitPin2(int pin, const RaycastHit2D& h) {
        switch (pin) {
            case 0:  return h.hit;
            case 1:  return Vec3{h.point.x, h.point.y, 0.0f};
            case 2:  return Vec3{h.normal.x, h.normal.y, 0.0f};
            case 3:  return h.distance;
            default: return VsValue::Object(h.gameObject ? h.gameObject->InstanceId() : 0);
        }
    }
};

// Box Cast: in0 Center, in1 Half Size, in2 Direction, in3 Angle (degrees), in4 Reach.
// A flat box swept through the side view. Skips this object and skips triggers.
struct BoxCast2DNode : VsPhysics2DNode {
    using VsPhysics2DNode::VsPhysics2DNode;
    std::uint64_t recordedFrame = ~0ull;
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        Scene* s = SceneOf(ctx);
        if (!s) return HitPin2(pin, RaycastHit2D{});
        Vec2 c = Flat(In(0, g, ctx, VsValue(SelfPos(ctx))));
        Vec2 h = Flat(In(1, g, ctx, VsValue(Vec3{0.5f, 0.5f, 0.0f})));
        Vec2 d = Flat(In(2, g, ctx, VsValue(Vec3{SelfRight2(ctx).x, SelfRight2(ctx).y, 0.0f})));
        float angle = In(3, g, ctx, VsValue(0.0f)).AsFloat() * Mathf::Deg2Rad;
        float dist = In(4, g, ctx, VsValue(100.0f)).AsFloat();
        if (dist <= 0.0f) dist = 1e9f;
        RaycastHit2D hit = s->physics().BoxCast(*s, c, h, d, angle, dist, ctx.gameObject, mask, false);
        if (recordedFrame != Time::FrameCount()) {
            recordedFrame = Time::FrameCount();
            s->physics().RecordQueryRay(c, d, dist, hit, Mathf::Min(Mathf::Abs(h.x), Mathf::Abs(h.y)));
        }
        return HitPin2(pin, hit);
    }
};

// Capsule Cast: in0 Segment Start, in1 Segment End, in2 Radius, in3 Direction, in4 Reach.
struct CapsuleCast2DNode : VsPhysics2DNode {
    using VsPhysics2DNode::VsPhysics2DNode;
    std::uint64_t recordedFrame = ~0ull;
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        Scene* s = SceneOf(ctx);
        if (!s) return HitPin2(pin, RaycastHit2D{});
        Vec2 mid = SelfPos2(ctx);
        Vec2 a = Flat(In(0, g, ctx, VsValue(Vec3{mid.x, mid.y - 0.5f, 0.0f})));
        Vec2 b = Flat(In(1, g, ctx, VsValue(Vec3{mid.x, mid.y + 0.5f, 0.0f})));
        float r = In(2, g, ctx, VsValue(0.5f)).AsFloat();
        Vec2 d = Flat(In(3, g, ctx, VsValue(Vec3{SelfRight2(ctx).x, SelfRight2(ctx).y, 0.0f})));
        float dist = In(4, g, ctx, VsValue(100.0f)).AsFloat();
        if (dist <= 0.0f) dist = 1e9f;
        RaycastHit2D hit = s->physics().CapsuleCast(*s, a, b, r, d, dist, ctx.gameObject, mask, false);
        if (recordedFrame != Time::FrameCount()) {
            recordedFrame = Time::FrameCount();
            s->physics().RecordQueryRay((a + b) * 0.5f, d, dist, hit, r);
        }
        return HitPin2(pin, hit);
    }
};

// Overlap Rectangle: in0 Center, in1 Half Size, in2 Angle (degrees) -> Count, Objects.
struct OverlapBox2DNode : VsPhysics2DNode {
    using VsPhysics2DNode::VsPhysics2DNode;
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        Scene* s = SceneOf(ctx);
        std::vector<GameObject*> objs;
        if (s) {
            Vec2 c = Flat(In(0, g, ctx, VsValue(SelfPos(ctx))));
            Vec2 h = Flat(In(1, g, ctx, VsValue(Vec3{0.5f, 0.5f, 0.0f})));
            float angle = In(2, g, ctx, VsValue(0.0f)).AsFloat() * Mathf::Deg2Rad;
            for (Collider2D* col : s->physics().OverlapBox(*s, c, h, mask, true, ctx.gameObject, angle)) {
                bool dup = false;
                for (GameObject* o : objs) if (o == col->gameObject) { dup = true; break; }
                if (!dup) objs.push_back(col->gameObject);
            }
        }
        if (pin == 0) return (float)objs.size();
        return Names(objs);
    }
};

// Overlap Capsule: in0 Segment Start, in1 Segment End, in2 Radius -> Count, Objects.
struct OverlapCapsule2DNode : VsPhysics2DNode {
    using VsPhysics2DNode::VsPhysics2DNode;
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        Scene* s = SceneOf(ctx);
        std::vector<GameObject*> objs;
        if (s) {
            Vec2 mid = SelfPos2(ctx);
            Vec2 a = Flat(In(0, g, ctx, VsValue(Vec3{mid.x, mid.y - 0.5f, 0.0f})));
            Vec2 b = Flat(In(1, g, ctx, VsValue(Vec3{mid.x, mid.y + 0.5f, 0.0f})));
            float r = In(2, g, ctx, VsValue(0.5f)).AsFloat();
            for (Collider2D* col : s->physics().OverlapCapsule(*s, a, b, r, mask, true, ctx.gameObject)) {
                bool dup = false;
                for (GameObject* o : objs) if (o == col->gameObject) { dup = true; break; }
                if (!dup) objs.push_back(col->gameObject);
            }
        }
        if (pin == 0) return (float)objs.size();
        return Names(objs);
    }
};

// Add Explosion Force: in0 Center, in1 Force, in2 Radius, in3 Upward Bias.
// Shoves this object's Rigidbody2D away from Center. The push fades to nothing
// at Radius. Upward Bias pops debris up as well as out.
struct AddExplosionForceNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!ctx.gameObject) return 0;
        Vec3 c = In(0, g, ctx, VsValue(Vec3::Zero)).AsVec3();
        float force = In(1, g, ctx, VsValue(10.0f)).AsFloat();
        float radius = In(2, g, ctx, VsValue(5.0f)).AsFloat();
        float up = In(3, g, ctx, VsValue(0.0f)).AsFloat();
        if (auto* rb = ctx.gameObject->GetComponent<Rigidbody2D>())
            rb->AddExplosionForce(force, {c.x, c.y}, radius, up);
        return 0;
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
