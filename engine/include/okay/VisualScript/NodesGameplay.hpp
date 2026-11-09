#pragma once
// ---------------------------------------------------------------------------
// High-level gameplay nodes: one node does a job a beginner would otherwise
// build from math, a timer and several transforms. They call the public
// component APIs (health, audio, animation, navigation, save, input) and
// report a node error when an object or component is missing.
// ---------------------------------------------------------------------------
#include "okay/VisualScript/NodesExtra.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/AnimControl.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Scene/SceneManager.hpp"
#include "okay/Core/SaveData.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay::vs {

inline bool GpHasBody(VsNode* n, NodeGraph& g, VsContext& ctx) {
    if (ctx.gameObject && ctx.transform) return true;
    g.Error(n, ctx, "this node needs an object with a transform");
    return false;
}

inline GameObject* GpPin(VsNode* n, int pin, NodeGraph& g, VsContext& ctx, bool required, const char* what) {
    if (!n->HasInput(pin)) {
        if (!required) return ctx.gameObject;
        g.Error(n, ctx, std::string("wire ") + what);
        return nullptr;
    }
    const VsValue v = n->InputValue(pin, g, ctx);
    GameObject* o = VsResolveObject(ctx, v);
    if (!o) g.Error(n, ctx, std::string(what) + " is missing or was destroyed");
    return o;
}

inline HealthStat* GpHealth(VsNode* n, GameObject* o, NodeGraph& g, VsContext& ctx) {
    if (!o) return nullptr;
    if (auto* h = o->GetComponent<HealthStat>()) return h;
    g.Error(n, ctx, "its object has no Health component");
    return nullptr;
}

inline void GpStep(Transform* t, const Vec3& dest, float speed, float dt) {
    Vec3 d = dest - t->Position();
    const float dist = d.Magnitude();
    const float step = std::max(0.0f, speed) * std::max(0.0f, dt);
    if (dist <= 1e-4f) return;
    if (step >= dist) t->SetPosition(dest);
    else t->SetPosition(t->Position() + d * (step / dist));
}

inline float GpEase(float t, const std::string& mode) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    if (mode == "linear") return t;
    if (mode == "in") return t * t;
    if (mode == "out") { const float u = 1.0f - t; return 1.0f - u * u; }
    return t * t * (3.0f - 2.0f * t);
}

inline bool GpParseVec(const std::string& s, Vec3& o) {
    float x = 0, y = 0, z = 0;
    if (std::sscanf(s.c_str(), "%f,%f,%f", &x, &y, &z) == 3 ||
        std::sscanf(s.c_str(), "%f %f %f", &x, &y, &z) == 3) {
        o = {x, y, z};
        return true;
    }
    return false;
}

inline std::vector<Vec3> GpPoints(const std::string& arg) {
    std::vector<Vec3> out;
    std::string cur;
    for (char c : arg) {
        if (c == ';' || c == '|') {
            Vec3 v;
            if (GpParseVec(cur, v)) out.push_back(v);
            cur.clear();
        } else cur.push_back(c);
    }
    Vec3 v;
    if (GpParseVec(cur, v)) out.push_back(v);
    return out;
}

inline std::vector<Vec3> GpPointList(VsNode* n, int pin, NodeGraph& g, VsContext& ctx, const std::string& arg) {
    if (n->HasInput(pin)) {
        std::vector<Vec3> out;
        if (auto a = n->InputValue(pin, g, ctx).AsArray())
            for (const auto& v : *a) out.push_back(v.AsVec3());
        if (!out.empty()) return out;
    }
    return GpPoints(arg);
}

inline void GpScoreAdd(VsContext& ctx, float amount) {
    auto& m = SceneVariables(ctx.gameObject ? ctx.gameObject->scene() : nullptr);
    m["score"] = VsValue(m["score"].AsFloat() + amount);
}

inline Color GpReadColor(GameObject* o) {
    if (!o) return Color(1, 1, 1, 1);
    if (auto* sr = o->GetComponent<SpriteRenderer>()) return sr->color;
    if (auto* tr = o->GetComponent<TextRenderer>()) return tr->color;
    if (auto* mr = o->GetComponent<MeshRenderer>()) return mr->color;
    return Color(1, 1, 1, 1);
}

inline void GpWriteColor(GameObject* o, const Color& c) {
    if (!o) return;
    if (auto* sr = o->GetComponent<SpriteRenderer>()) sr->color = c;
    if (auto* tr = o->GetComponent<TextRenderer>()) tr->color = c;
    if (auto* mr = o->GetComponent<MeshRenderer>()) mr->color = c;
}

// ---- Movement --------------------------------------------------------------
struct RotateTowardsNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        GameObject* to = GpPin(this, 1, g, ctx, true, "a target");
        if (!to || !to->transform) return 0;
        Vec3 d = to->transform->Position() - ctx.transform->Position();
        d.y = 0;
        if (d.Magnitude() < 1e-4f) return 0;
        const float speed = In(0, g, ctx, VsValue{180.0f}).AsFloat();
        const Quat want = Quat::LookRotation(Vec3{-d.x, 0, -d.z}.Normalized());
        const float t = std::min(1.0f, std::max(0.0f, speed) * ctx.deltaTime / 180.0f);
        ctx.transform->localRotation = Quat::Slerp(ctx.transform->localRotation, want, t);
        return 0;
    }
};

struct OrbitAroundNode : VsNode {
    float angle = 0;
    int ExecOutCount() const override { return 1; }
    bool SaveFlow(VsFlowRecord& r) const override { r.f0 = angle; return true; }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { angle = r.f0; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        GameObject* to = GpPin(this, 0, g, ctx, true, "a centre");
        if (!to || !to->transform) return 0;
        const float radius = std::max(0.1f, In(1, g, ctx, VsValue{3.0f}).AsFloat());
        const float speed = In(2, g, ctx, VsValue{40.0f}).AsFloat();
        angle += speed * ctx.deltaTime;
        const float rad = angle * 0.0174533f;
        Vec3 p = to->transform->Position();
        p.x += std::cos(rad) * radius;
        p.z += std::sin(rad) * radius;
        ctx.transform->SetPosition(p);
        return 0;
    }
};

struct FollowPathNode : VsNode {
    std::string arg;
    int index = 0;
    explicit FollowPathNode(std::string a) : arg(std::move(a)) {}
    int ExecOutCount() const override { return 2; }
    bool SaveFlow(VsFlowRecord& r) const override { r.i0 = index; return index > 0; }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { index = r.i0; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 1;
        const auto pts = GpPointList(this, 1, g, ctx, arg);
        if (pts.empty()) { g.Error(this, ctx, "give it waypoints (a list, or x,y,z; x,y,z in the text)"); return 1; }
        if (index >= (int)pts.size()) return 1;
        const float speed = In(0, g, ctx, VsValue{2.0f}).AsFloat();
        GpStep(ctx.transform, pts[(std::size_t)index], speed, ctx.deltaTime);
        if ((ctx.transform->Position() - pts[(std::size_t)index]).Magnitude() <= 0.08f) ++index;
        return index >= (int)pts.size() ? 1 : 0;
    }
};

struct BobNode : VsNode {
    float base = 0, phase = 0;
    bool have = false;
    int ExecOutCount() const override { return 1; }
    bool SaveFlow(VsFlowRecord& r) const override { if (!have) return false; r.b0 = true; r.f0 = base; r.f1 = phase; return true; }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { have = r.b0; base = r.f0; phase = r.f1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        if (!have) { base = ctx.transform->Position().y; have = true; }
        const float height = In(0, g, ctx, VsValue{0.25f}).AsFloat();
        const float speed = In(1, g, ctx, VsValue{2.0f}).AsFloat();
        phase += speed * std::max(0.0f, ctx.deltaTime);
        Vec3 p = ctx.transform->Position();
        p.y = base + std::sin(phase) * height;
        ctx.transform->SetPosition(p);
        return 0;
    }
};

struct SpinNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        const float deg = In(0, g, ctx, VsValue{90.0f}).AsFloat() * ctx.deltaTime;
        ctx.transform->localRotation = ctx.transform->localRotation * Quat::Euler(0, deg, 0);
        return 0;
    }
};

struct KnockbackNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        Vec3 dir = -ctx.transform->Forward();
        if (HasInput(0)) {
            if (GameObject* from = GpPin(this, 0, g, ctx, true, "a source")) {
                if (!from->transform) return 0;
                dir = ctx.transform->Position() - from->transform->Position();
            } else return 0;
        }
        if (dir.Magnitude() < 1e-4f) dir = Vec3{0, 0, 1};
        dir = dir.Normalized();
        const float force = In(1, g, ctx, VsValue{6.0f}).AsFloat();
        if (auto* r3 = ctx.gameObject->GetComponent<Rigidbody3D>()) r3->AddImpulse(dir * force);
        else if (auto* r2 = ctx.gameObject->GetComponent<Rigidbody2D>()) r2->AddImpulse({dir.x * force, dir.y * force});
        else ctx.transform->SetPosition(ctx.transform->Position() + dir * force * 0.15f);
        return 0;
    }
};

struct SmoothFollowNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        GameObject* to = GpPin(this, 0, g, ctx, true, "a target");
        if (!to || !to->transform) return 0;
        const float s = std::max(0.0f, In(1, g, ctx, VsValue{5.0f}).AsFloat());
        const float t = s <= 0.0f ? 1.0f : (1.0f - std::exp(-s * std::max(0.0f, ctx.deltaTime)));
        ctx.transform->SetPosition(Vec3::Lerp(ctx.transform->Position(), to->transform->Position(), t));
        return 0;
    }
};

// ---- Tweens (the caller waits until the blend finishes) -------------------
struct GpTween : VsNode {
    std::string ease;
    float acc = 0, seconds = 0.5f;
    bool waiting = false, captured = false;
    explicit GpTween(std::string e) : ease(std::move(e)) {}
    int ExecOutCount() const override { return 1; }
    void OnSuspendRejected() override { waiting = false; }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (!waiting) return false;
        r.b0 = true; r.f0 = acc; r.f1 = seconds; r.texts = {ease}; return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override {
        waiting = r.b0; acc = r.f0; if (r.f1 > 0) seconds = r.f1;
        if (!r.texts.empty()) ease = r.texts[0];
    }
    // Returns true while the tween should keep parking. `u` is the eased 0..1.
    bool Advance(NodeGraph& g, VsContext& ctx, float& u) {
        if (g.Resuming() && waiting) {
            acc += std::max(0.0f, ctx.deltaTime);
            const float lin = seconds <= 0 ? 1.0f : std::min(1.0f, acc / seconds);
            u = GpEase(lin, ease);
            if (lin >= 1.0f) { waiting = false; return false; }
            return true;
        }
        if (waiting) return true;
        seconds = std::max(0.0f, Seconds(g, ctx));
        acc = 0; waiting = true; captured = false; u = 0;
        return seconds > 0.0f;
    }
    virtual float Seconds(NodeGraph& g, VsContext& ctx) = 0;
};

struct TweenPositionNode : GpTween {
    Vec3 from{}, to{};
    using GpTween::GpTween;
    float Seconds(NodeGraph& g, VsContext& ctx) override { return In(1, g, ctx, VsValue{0.5f}).AsFloat(); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        float u = 0;
        const bool park = Advance(g, ctx, u);
        if (!captured) { from = ctx.transform->Position(); to = In(0, g, ctx, VsValue(from)).AsVec3(); captured = true; }
        ctx.transform->SetPosition(Vec3::Lerp(from, to, u));
        return park ? NodeGraph::kSuspend : 0;
    }
};

struct TweenRotationNode : GpTween {
    float from = 0, to = 0;
    using GpTween::GpTween;
    float Seconds(NodeGraph& g, VsContext& ctx) override { return In(1, g, ctx, VsValue{0.5f}).AsFloat(); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        float u = 0;
        const bool park = Advance(g, ctx, u);
        if (!captured) { from = ctx.transform->localRotation.ToEuler().y; to = In(0, g, ctx, VsValue{90.0f}).AsFloat(); captured = true; }
        const Vec3 e = ctx.transform->localRotation.ToEuler();
        ctx.transform->localRotation = Quat::Euler(e.x, from + (to - from) * u, e.z);
        return park ? NodeGraph::kSuspend : 0;
    }
};

struct TweenScaleNode : GpTween {
    Vec3 from{1, 1, 1}, to{1, 1, 1};
    using GpTween::GpTween;
    float Seconds(NodeGraph& g, VsContext& ctx) override { return In(1, g, ctx, VsValue{0.5f}).AsFloat(); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        float u = 0;
        const bool park = Advance(g, ctx, u);
        if (!captured) { from = ctx.transform->localScale; to = In(0, g, ctx, VsValue(Vec3{2, 2, 2})).AsVec3(); captured = true; }
        ctx.transform->SetScale(Vec3::Lerp(from, to, u));
        return park ? NodeGraph::kSuspend : 0;
    }
};

struct TweenColorNode : GpTween {
    Color from{1, 1, 1, 1}, to{1, 1, 1, 1};
    using GpTween::GpTween;
    float Seconds(NodeGraph& g, VsContext& ctx) override { return In(4, g, ctx, VsValue{0.5f}).AsFloat(); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!ctx.gameObject) { g.Error(this, ctx, "this node needs an object"); return 0; }
        float u = 0;
        const bool park = Advance(g, ctx, u);
        if (!captured) {
            from = GpReadColor(ctx.gameObject);
            to = Color(In(0, g, ctx, VsValue{1.0f}).AsFloat(), In(1, g, ctx, VsValue{1.0f}).AsFloat(),
                       In(2, g, ctx, VsValue{1.0f}).AsFloat(), In(3, g, ctx, VsValue{1.0f}).AsFloat());
            captured = true;
        }
        GpWriteColor(ctx.gameObject, Color(from.r + (to.r - from.r) * u, from.g + (to.g - from.g) * u,
                                            from.b + (to.b - from.b) * u, from.a + (to.a - from.a) * u));
        return park ? NodeGraph::kSuspend : 0;
    }
};

// ---- Animation -------------------------------------------------------------
struct CrossfadeClipNode : VsNode {
    std::string clip;
    explicit CrossfadeClipNode(std::string c) : clip(std::move(c)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!ctx.gameObject) { g.Error(this, ctx, "this node needs an object"); return 0; }
        if (clip.empty()) { g.Error(this, ctx, "type the clip name"); return 0; }
        const float blend = std::max(0.0f, In(0, g, ctx, VsValue{0.25f}).AsFloat());
        if (auto* ma = FindAnimComponent<ModelAnimator>(ctx.gameObject)) {
            for (int i = 0; i < (int)ma->clips.size(); ++i)
                if (ma->clips[(std::size_t)i].name == clip) { ma->PlayIndex(i, false, blend); return 0; }
            g.Error(this, ctx, "no clip named \"" + clip + "\" on its model");
            return 0;
        }
        bool warned = false;
        if (!PlayClipFrom(ctx.gameObject, clip, warned))
            g.Error(this, ctx, "no clip named \"" + clip + "\" on this object");
        return 0;
    }
};

struct StopAnimationNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!ctx.gameObject) { g.Error(this, ctx, "this node needs an object"); return 0; }
        StopClipFrom(ctx.gameObject);
        return 0;
    }
};

struct WaitAnimEndNode : VsNode {
    bool waiting = false;
    int ExecOutCount() const override { return 1; }
    void OnSuspendRejected() override { waiting = false; }
    bool SaveFlow(VsFlowRecord& r) const override { if (!waiting) return false; r.b0 = true; return true; }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { waiting = r.b0; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!ctx.gameObject) { g.Error(this, ctx, "this node needs an object"); return 0; }
        const float nrm = ClipNormalizedFrom(ctx.gameObject);
        if (!g.Resuming()) {
            if (waiting) return -1;
            if (nrm <= 0.0f) { g.Error(this, ctx, "nothing is playing on this object"); return 0; }
            waiting = true;
            return NodeGraph::kSuspend;
        }
        if (nrm >= 0.98f) { waiting = false; return 0; }
        return NodeGraph::kSuspend;
    }
};

// ---- Interaction -----------------------------------------------------------
struct PickUpNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        GameObject* item = GpPin(this, 0, g, ctx, true, "the object to pick up");
        if (!item || !item->transform) return 0;
        item->transform->SetParent(ctx.transform, true);
        item->transform->localPosition = Vec3{0, 1, 0.6f};
        if (auto* r3 = item->GetComponent<Rigidbody3D>()) r3->bodyType = Rigidbody3D::BodyType::Kinematic;
        if (auto* r2 = item->GetComponent<Rigidbody2D>()) r2->bodyType = Rigidbody2D::BodyType::Kinematic;
        return 0;
    }
};

struct DropNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        GameObject* item = GpPin(this, 0, g, ctx, true, "the object to drop");
        if (!item || !item->transform) return 0;
        item->transform->SetParent(nullptr, true);
        item->transform->SetPosition(ctx.transform->Position() + ctx.transform->Forward() * 1.2f);
        if (auto* r3 = item->GetComponent<Rigidbody3D>()) r3->bodyType = Rigidbody3D::BodyType::Dynamic;
        if (auto* r2 = item->GetComponent<Rigidbody2D>()) r2->bodyType = Rigidbody2D::BodyType::Dynamic;
        return 0;
    }
};

struct DoorSwingNode : GpTween {
    bool open;
    float from = 0, span = 90;
    explicit DoorSwingNode(bool o, std::string) : GpTween("inout"), open(o) {}
    float Seconds(NodeGraph& g, VsContext& ctx) override {
        return In(open ? 1 : 0, g, ctx, VsValue{0.4f}).AsFloat();
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        float u = 0;
        const bool park = Advance(g, ctx, u);
        if (!captured) {
            from = ctx.transform->localRotation.ToEuler().y;
            if (!ctx.variables.count("vs_door_yaw")) ctx.variables["vs_door_yaw"] = VsValue(from);
            const float closed = ctx.variables["vs_door_yaw"].AsFloat();
            const float deg = open ? In(0, g, ctx, VsValue{90.0f}).AsFloat() : 0.0f;
            span = (closed + deg) - from;
            captured = true;
        }
        const Vec3 e = ctx.transform->localRotation.ToEuler();
        ctx.transform->localRotation = Quat::Euler(e.x, from + span * u, e.z);
        return park ? NodeGraph::kSuspend : 0;
    }
};

struct LeverNode : VsNode {
    bool on = false;
    int ExecOutCount() const override { return 2; }
    bool SaveFlow(VsFlowRecord& r) const override { r.b0 = on; return true; }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { on = r.b0; }
    int Exec(NodeGraph&, VsContext&) override { on = !on; return on ? 0 : 1; }
};

struct CollectNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GpScoreAdd(ctx, In(0, g, ctx, VsValue{1.0f}).AsFloat());
        GameObject* o = ctx.other ? ctx.other : ctx.gameObject;
        if (o && o->scene()) o->scene()->Destroy(o);
        return o == ctx.gameObject ? -1 : 0;
    }
};

struct ProximityNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        if (!ctx.transform) return false;
        GameObject* to = GpPin(this, 0, g, ctx, true, "a target");
        if (!to || !to->transform) return false;
        const float d = In(1, g, ctx, VsValue{2.0f}).AsFloat();
        return (ctx.transform->Position() - to->transform->Position()).Magnitude() <= d;
    }
};

// ---- Health and combat -----------------------------------------------------
struct DamageNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = GpPin(this, 1, g, ctx, false, "a target");
        if (HealthStat* h = GpHealth(this, o, g, ctx))
            h->Damage(std::max(0.0f, In(0, g, ctx, VsValue{10.0f}).AsFloat()));
        return 0;
    }
};

struct HealNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = GpPin(this, 1, g, ctx, false, "a target");
        if (HealthStat* h = GpHealth(this, o, g, ctx))
            h->Heal(std::max(0.0f, In(0, g, ctx, VsValue{25.0f}).AsFloat()));
        return 0;
    }
};

struct IsDeadNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        GameObject* o = GpPin(this, 0, g, ctx, false, "a target");
        HealthStat* h = GpHealth(this, o, g, ctx);
        return h && h->IsDead();
    }
};

struct RespawnNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = GpPin(this, 0, g, ctx, false, "a target");
        if (HealthStat* h = GpHealth(this, o, g, ctx)) h->Revive();
        return 0;
    }
};

struct SpawnProjectileNode : VsNode {
    std::string path;
    std::uint64_t id = 0;
    explicit SpawnProjectileNode(std::string p) : path(std::move(p)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return VsValue::Object(id); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        id = 0;
        if (!GpHasBody(this, g, ctx) || !ctx.gameObject->scene()) return 0;
        if (path.empty()) { g.Error(this, ctx, "type a prefab path"); return 0; }
        GameObject* b = SceneSerializer::InstantiateFromFile(*ctx.gameObject->scene(), path);
        if (!b) { g.Error(this, ctx, "couldn't spawn '" + path + "'"); return 0; }
        const float speed = In(0, g, ctx, VsValue{12.0f}).AsFloat();
        if (b->transform) b->transform->SetPosition(ctx.transform->Position() + ctx.transform->Forward() * 0.8f);
        const Vec3 vel = ctx.transform->Forward() * speed;
        if (auto* r3 = b->GetComponent<Rigidbody3D>()) r3->velocity = vel;
        id = b->InstanceId();
        return 0;
    }
};

struct ShootNode : VsNode {
    bool hit = false;
    std::uint64_t id = 0;
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int pin, NodeGraph&, VsContext&) override { return pin == 0 ? VsValue(hit) : VsValue::Object(id); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        hit = false; id = 0;
        if (!GpHasBody(this, g, ctx) || !ctx.gameObject->scene()) return 0;
        const float dist = std::max(0.1f, In(0, g, ctx, VsValue{30.0f}).AsFloat());
        const float dmg = std::max(0.0f, In(1, g, ctx, VsValue{10.0f}).AsFloat());
        Scene* s = ctx.gameObject->scene();
        RaycastHit3D h = s->physics3D().Raycast(*s, ctx.transform->Position(), ctx.transform->Forward(), dist, ctx.gameObject);
        if (!h.hit || !h.gameObject) return 0;
        hit = true; id = h.gameObject->InstanceId();
        if (auto* hp = h.gameObject->GetComponent<HealthStat>()) hp->Damage(dmg, ctx.transform->Position());
        return 0;
    }
};

struct AmmoNode : VsNode {
    int left;
    explicit AmmoNode(int n) : left(n > 0 ? n : 6) {}
    int ExecOutCount() const override { return 2; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return (float)left; }
    bool SaveFlow(VsFlowRecord& r) const override { r.i0 = left; return true; }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { left = r.i0; }
    int Exec(NodeGraph&, VsContext&) override {
        if (left <= 0) return 1;
        --left;
        return 0;
    }
};

// ---- AI --------------------------------------------------------------------
struct PatrolNode : VsNode {
    std::string arg;
    int index = 0;
    explicit PatrolNode(std::string a) : arg(std::move(a)) {}
    int ExecOutCount() const override { return 1; }
    bool SaveFlow(VsFlowRecord& r) const override { r.i0 = index; return true; }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { index = r.i0; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        auto pts = GpPointList(this, 1, g, ctx, arg);
        if (pts.empty()) { g.Error(this, ctx, "give it waypoints (a list, or x,y,z; x,y,z in the text)"); return 0; }
        if (index < 0 || index >= (int)pts.size()) index = 0;
        const float speed = In(0, g, ctx, VsValue{2.0f}).AsFloat();
        const Vec3 dest = pts[(std::size_t)index];
        if (auto* a = ctx.gameObject->GetComponent<NavMeshAgent>()) {
            a->speed = speed > 0 ? speed : a->speed;
            a->SetDestination(dest);
            if (a->Arrived()) index = (index + 1) % (int)pts.size();
        } else {
            GpStep(ctx.transform, dest, speed, ctx.deltaTime);
            if ((ctx.transform->Position() - dest).Magnitude() <= 0.15f) index = (index + 1) % (int)pts.size();
        }
        return 0;
    }
};

struct ChaseNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        GameObject* to = GpPin(this, 0, g, ctx, true, "a target");
        if (!to || !to->transform) return 0;
        const float speed = In(1, g, ctx, VsValue{3.0f}).AsFloat();
        if (auto* a = ctx.gameObject->GetComponent<NavMeshAgent>()) {
            a->speed = speed > 0 ? speed : a->speed;
            a->SetDestination(to->transform->Position());
        } else GpStep(ctx.transform, to->transform->Position(), speed, ctx.deltaTime);
        return 0;
    }
};

struct FleeNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        GameObject* to = GpPin(this, 0, g, ctx, true, "a threat");
        if (!to || !to->transform) return 0;
        Vec3 away = ctx.transform->Position() - to->transform->Position();
        if (away.Magnitude() < 1e-3f) away = Vec3{1, 0, 0};
        const float speed = In(1, g, ctx, VsValue{3.5f}).AsFloat();
        GpStep(ctx.transform, ctx.transform->Position() + away.Normalized() * 4.0f, speed, ctx.deltaTime);
        return 0;
    }
};

struct WanderNode : VsNode {
    Vec3 home{}, goal{};
    float wait = 0;
    bool have = false;
    int ExecOutCount() const override { return 1; }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (!have) return false;
        r.b0 = true; r.f0 = wait; r.values = {VsValue(home), VsValue(goal)}; return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override {
        have = r.b0; wait = r.f0;
        if (r.values.size() >= 2) { home = r.values[0].AsVec3(); goal = r.values[1].AsVec3(); }
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        if (!have) { home = ctx.transform->Position(); goal = home; have = true; }
        wait -= ctx.deltaTime;
        const float radius = std::max(0.5f, In(0, g, ctx, VsValue{4.0f}).AsFloat());
        const float speed = In(1, g, ctx, VsValue{1.5f}).AsFloat();
        if (wait <= 0.0f || (ctx.transform->Position() - goal).Magnitude() < 0.2f) {
            goal = home + Vec3{(VsRandom01() * 2 - 1) * radius, 0, (VsRandom01() * 2 - 1) * radius};
            wait = 1.5f + VsRandom01() * 2.0f;
        }
        GpStep(ctx.transform, goal, speed, ctx.deltaTime);
        return 0;
    }
};

struct CanSeeNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        if (!ctx.transform) return false;
        GameObject* to = GpPin(this, 0, g, ctx, true, "a target");
        if (!to || !to->transform) return false;
        const float maxD = std::max(0.1f, In(1, g, ctx, VsValue{12.0f}).AsFloat());
        const float cone = In(2, g, ctx, VsValue{70.0f}).AsFloat();
        Vec3 d = to->transform->Position() - ctx.transform->Position();
        const float dist = d.Magnitude();
        if (dist > maxD || dist < 1e-4f) return false;
        d = d * (1.0f / dist);
        const float need = std::cos(cone * 0.5f * 0.0174533f);
        if (Vec3::Dot(ctx.transform->Forward().Normalized(), d) < need) return false;
        if (ctx.gameObject && ctx.gameObject->scene() && ctx.gameObject->scene()->physicsEnabled) {
            Scene* s = ctx.gameObject->scene();
            RaycastHit3D h = s->physics3D().Raycast(*s, ctx.transform->Position(), d, dist, ctx.gameObject);
            if (h.hit && h.gameObject && h.gameObject != to && h.distance < dist - 0.05f) return false;
        }
        return true;
    }
};

// ---- Spawning --------------------------------------------------------------
inline GameObject* GpSpawn(VsNode* n, NodeGraph& g, VsContext& ctx, const std::string& path) {
    if (!ctx.gameObject || !ctx.gameObject->scene()) { g.Error(n, ctx, "this node needs an object in a scene"); return nullptr; }
    if (path.empty()) { g.Error(n, ctx, "type a prefab path"); return nullptr; }
    GameObject* o = SceneSerializer::InstantiateFromFile(*ctx.gameObject->scene(), path);
    if (!o) g.Error(n, ctx, "couldn't spawn '" + path + "'");
    return o;
}

struct SpawnEveryNode : VsNode {
    std::string path;
    float acc = 0;
    std::uint64_t id = 0;
    explicit SpawnEveryNode(std::string p) : path(std::move(p)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return VsValue::Object(id); }
    bool SaveFlow(VsFlowRecord& r) const override { r.f0 = acc; return acc > 0; }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { acc = r.f0; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        id = 0;
        const float every = std::max(0.05f, In(0, g, ctx, VsValue{2.0f}).AsFloat());
        acc += std::max(0.0f, ctx.deltaTime);
        if (acc < every) return -1;
        acc = 0;
        if (GameObject* o = GpSpawn(this, g, ctx, path)) {
            if (o->transform && ctx.transform) o->transform->SetPosition(ctx.transform->Position());
            id = o->InstanceId();
            return 0;
        }
        return 0;
    }
};

struct DestroyAfterNode : GpTween {
    using GpTween::GpTween;
    float Seconds(NodeGraph& g, VsContext& ctx) override { return In(0, g, ctx, VsValue{2.0f}).AsFloat(); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        float u = 0;
        const bool park = Advance(g, ctx, u);
        if (park) return NodeGraph::kSuspend;
        GameObject* o = GpPin(this, 1, g, ctx, false, "a target");
        if (o && o->scene()) o->scene()->Destroy(o);
        return o == ctx.gameObject ? -1 : 0;
    }
};

inline std::unordered_map<std::string, std::vector<std::uint64_t>>& GpPool() {
    static std::unordered_map<std::string, std::vector<std::uint64_t>> p;
    return p;
}

struct PoolGetNode : VsNode {
    std::string path;
    std::uint64_t id = 0;
    explicit PoolGetNode(std::string p) : path(std::move(p)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return VsValue::Object(id); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        id = 0;
        auto& bag = GpPool()[path];
        while (!bag.empty()) {
            GameObject* o = GameObject::FromInstanceId(bag.back());
            bag.pop_back();
            if (!o) continue;
            o->active = true;
            if (o->transform && ctx.transform) o->transform->SetPosition(ctx.transform->Position());
            id = o->InstanceId();
            return 0;
        }
        if (GameObject* o = GpSpawn(this, g, ctx, path)) {
            if (o->transform && ctx.transform) o->transform->SetPosition(ctx.transform->Position());
            id = o->InstanceId();
        }
        return 0;
    }
};

struct PoolReleaseNode : VsNode {
    std::string path;
    explicit PoolReleaseNode(std::string p) : path(std::move(p)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = GpPin(this, 0, g, ctx, true, "the object to release");
        if (!o) return 0;
        o->active = false;
        GpPool()[path.empty() ? o->name : path].push_back(o->InstanceId());
        return 0;
    }
};

// ---- Game flow -------------------------------------------------------------
struct ScoreAddNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GpScoreAdd(ctx, In(0, g, ctx, VsValue{1.0f}).AsFloat());
        return 0;
    }
};
struct ScoreSetNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        SceneVariables(ctx.gameObject ? ctx.gameObject->scene() : nullptr)["score"] =
            VsValue(In(0, g, ctx, VsValue{0.0f}).AsFloat());
        return 0;
    }
};
struct ScoreGetNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        return SceneVariables(ctx.gameObject ? ctx.gameObject->scene() : nullptr)["score"];
    }
};

struct CountdownNode : VsNode {
    float seconds, acc = 0;
    bool waiting = false;
    explicit CountdownNode(float s) : seconds(s > 0 ? s : 5.0f) {}
    int ExecOutCount() const override { return 1; }
    void OnSuspendRejected() override { waiting = false; }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (!waiting) return false;
        r.b0 = true; r.f0 = acc; r.f1 = seconds; return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override {
        waiting = r.b0; acc = r.f0; if (r.f1 > 0) seconds = r.f1;
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (g.Resuming() && waiting) {
            acc += std::max(0.0f, ctx.deltaTime);
            if (acc >= seconds) { waiting = false; return 0; }
            return NodeGraph::kSuspend;
        }
        if (waiting) return -1;
        if (HasInput(0)) seconds = std::max(0.0f, In(0, g, ctx).AsFloat());
        if (seconds <= 0) return 0;
        acc = 0; waiting = true;
        return NodeGraph::kSuspend;
    }
};

struct OutcomeNode : VsNode {
    bool win;
    explicit OutcomeNode(bool w) : win(w) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        SceneVariables(ctx.gameObject ? ctx.gameObject->scene() : nullptr)["outcome"] = VsValue(win ? std::string("win") : std::string("lose"));
        Time::SetTimeScale(0.0f);
        if (ctx.gameObject && ctx.gameObject->scene())
            if (GameObject* label = ctx.gameObject->scene()->Find(win ? "WinText" : "LoseText"))
                if (auto* t = label->GetComponent<TextRenderer>()) t->text = win ? "You win" : "You lose";
        return 0;
    }
};

struct RestartSceneNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        Scene* s = ctx.gameObject ? ctx.gameObject->scene() : nullptr;
        if (!s) { g.Error(this, ctx, "there is no scene to restart"); return 0; }
        if (!SceneManager::ReloadScene(*s)) g.Error(this, ctx, "no scene has been loaded yet, so there is nothing to restart");
        return -1;
    }
};

struct CheckpointSaveNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!GpHasBody(this, g, ctx)) return 0;
        Save::SetVec3("vs_checkpoint", ctx.transform->Position());
        if (auto* h = ctx.gameObject->GetComponent<HealthStat>()) Save::SetFloat("vs_checkpoint_hp", h->health);
        Save::SetString("vs_checkpoint_ok", "1");
        return 0;
    }
};

struct CheckpointLoadNode : VsNode {
    int ExecOutCount() const override { return 2; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (Save::GetString("vs_checkpoint_ok", "") != "1") return 1;
        if (!GpHasBody(this, g, ctx)) return 1;
        ctx.transform->SetPosition(Save::GetVec3("vs_checkpoint", ctx.transform->Position()));
        if (auto* h = ctx.gameObject->GetComponent<HealthStat>()) {
            h->health = Save::GetFloat("vs_checkpoint_hp", h->health);
            if (h->health > 0 && h->IsDead()) h->Revive();
        }
        return 0;
    }
};

// ---- UI, camera, audio, input ----------------------------------------------
struct SetProgressNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = GpPin(this, 1, g, ctx, false, "a bar");
        if (!o) return 0;
        auto* bar = o->GetComponent<UIProgressBar>();
        if (!bar) { g.Error(this, ctx, "its object has no progress bar"); return 0; }
        bar->SetValue(In(0, g, ctx, VsValue{1.0f}).AsFloat());
        return 0;
    }
};

struct ShowFloatingTextNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = GpPin(this, 1, g, ctx, false, "a label");
        if (!o) return 0;
        auto* t = o->GetComponent<TextRenderer>();
        if (!t) { g.Error(this, ctx, "its object has no text"); return 0; }
        t->text = In(0, g, ctx, VsValue(std::string("+1"))).AsString();
        if (o->transform && ctx.transform && o != ctx.gameObject)
            o->transform->SetPosition(ctx.transform->Position() + Vec3{0, 1.6f, 0});
        return 0;
    }
};

struct FadeScreenNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = nullptr;
        if (HasInput(1)) o = GpPin(this, 1, g, ctx, true, "a panel");
        else if (ctx.gameObject && ctx.gameObject->scene()) o = ctx.gameObject->scene()->Find("Fade");
        if (!o) o = ctx.gameObject;
        auto* p = o ? o->GetComponent<UIPanel>() : nullptr;
        if (!p) { g.Error(this, ctx, "needs a UI panel (name it Fade, or wire it in)"); return 0; }
        p->color.a = std::max(0.0f, std::min(1.0f, In(0, g, ctx, VsValue{1.0f}).AsFloat()));
        return 0;
    }
};

struct SetCameraTargetNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        Scene* s = ctx.gameObject ? ctx.gameObject->scene() : nullptr;
        Camera* c = s ? s->mainCamera : nullptr;
        if (!c || !c->gameObject) { g.Error(this, ctx, "the scene has no main camera"); return 0; }
        GameObject* to = GpPin(this, 0, g, ctx, true, "a target");
        if (!to) return 0;
        if (to->name.empty()) { g.Error(this, ctx, "the camera follows an object by name; name the target"); return 0; }
        auto* f = c->gameObject->GetComponent<CameraFollow>();
        if (!f) f = c->gameObject->AddComponent<CameraFollow>();
        f->targetName = to->name;
        if (HasInput(1)) f->smoothing = In(1, g, ctx).AsFloat();
        return 0;
    }
};

struct ZoomNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        Scene* s = ctx.gameObject ? ctx.gameObject->scene() : nullptr;
        Camera* c = s ? s->mainCamera : nullptr;
        if (!c) { g.Error(this, ctx, "the scene has no main camera"); return 0; }
        c->fieldOfView = std::max(1.0f, std::min(179.0f, c->fieldOfView + In(0, g, ctx, VsValue{-10.0f}).AsFloat()));
        return 0;
    }
};

struct SwitchCameraNode : VsNode {
    std::string name;
    explicit SwitchCameraNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        Scene* s = ctx.gameObject ? ctx.gameObject->scene() : nullptr;
        if (!s) { g.Error(this, ctx, "there is no scene"); return 0; }
        GameObject* o = name.empty() ? nullptr : s->Find(name);
        Camera* c = o ? o->GetComponent<Camera>() : nullptr;
        if (!c) { g.Error(this, ctx, "no camera named \"" + name + "\""); return 0; }
        s->mainCamera = c;
        return 0;
    }
};

struct PlaySoundAtNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = GpPin(this, 0, g, ctx, false, "a source");
        auto* au = o ? o->GetComponent<AudioSource>() : nullptr;
        if (!au) { g.Error(this, ctx, "its object has no Audio Source"); return 0; }
        au->spatial = true;
        au->Play();
        return 0;
    }
};

struct StopSoundNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        GameObject* o = GpPin(this, 0, g, ctx, false, "a source");
        auto* au = o ? o->GetComponent<AudioSource>() : nullptr;
        if (!au) { g.Error(this, ctx, "its object has no Audio Source"); return 0; }
        au->Stop();
        return 0;
    }
};

struct GetMoveAxisNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext&) override {
        const Vec2 a = Input::AxisWASD();
        return VsValue(Vec3{a.x, a.y, 0});
    }
};

// ---- Lists -----------------------------------------------------------------
struct RandomPickNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        auto a = In(0, g, ctx).AsArray();
        if (!a || a->empty()) { g.Error(this, ctx, "the list is empty"); return {}; }
        const int i = (int)(VsRandom01() * (float)a->size());
        return (*a)[(std::size_t)std::min(i, (int)a->size() - 1)];
    }
};

struct RandomPointNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        Vec3 c = HasInput(0) ? In(0, g, ctx).AsVec3() : (ctx.transform ? ctx.transform->Position() : Vec3{0, 0, 0});
        Vec3 sz = In(1, g, ctx, VsValue(Vec3{4, 0, 4})).AsVec3();
        return VsValue(Vec3{c.x + (VsRandom01() * 2 - 1) * sz.x * 0.5f,
                            c.y + (VsRandom01() * 2 - 1) * sz.y * 0.5f,
                            c.z + (VsRandom01() * 2 - 1) * sz.z * 0.5f});
    }
};

struct ShuffleListNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        VsValue out = VsValue::MakeArray();
        auto src = In(0, g, ctx).AsArray();
        auto dst = out.AsArray();
        if (!src) { g.Error(this, ctx, "wire a list"); return out; }
        *dst = *src;
        for (int i = (int)dst->size() - 1; i > 0; --i) {
            const int j = (int)(VsRandom01() * (float)(i + 1));
            std::swap((*dst)[(std::size_t)i], (*dst)[(std::size_t)std::min(j, i)]);
        }
        return out;
    }
};

struct ListExtremeNode : VsNode {
    bool least;
    explicit ListExtremeNode(bool l) : least(l) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        auto a = In(0, g, ctx).AsArray();
        if (!a || a->empty()) { g.Error(this, ctx, "the list is empty"); return 0.0f; }
        float m = (*a)[0].AsFloat();
        for (const auto& v : *a) m = least ? std::min(m, v.AsFloat()) : std::max(m, v.AsFloat());
        return m;
    }
};

inline std::string GpJoin(const std::vector<std::string>& args) {
    std::string s;
    for (const auto& a : args) { if (!s.empty()) s += ' '; s += a; }
    return s;
}

inline std::unique_ptr<VsNode> MakeGameplayNode(const std::string& type, const std::vector<std::string>& args) {
    const std::string a = GpJoin(args);
    auto argOr = [&](const char* d) { return a.empty() ? std::string(d) : a; };
    if (type == "OnInteract" || type == "OnTagEnter" || type == "OnTagStay" || type == "OnTagExit" ||
        type == "OnCollisionTag" || type == "OnDeath" || type == "OnHealthChanged" ||
        type == "OnAction" || type == "OnActionUp" || type == "OnActionHeld")
        return std::make_unique<EventNode>();
    if (type == "RotateTowards") return std::make_unique<RotateTowardsNode>();
    if (type == "OrbitAround") return std::make_unique<OrbitAroundNode>();
    if (type == "FollowPath") return std::make_unique<FollowPathNode>(argOr("0,0,0; 2,0,0"));
    if (type == "Bob") return std::make_unique<BobNode>();
    if (type == "Spin") return std::make_unique<SpinNode>();
    if (type == "Knockback") return std::make_unique<KnockbackNode>();
    if (type == "SmoothFollow") return std::make_unique<SmoothFollowNode>();
    if (type == "TweenPosition") return std::make_unique<TweenPositionNode>(argOr("inout"));
    if (type == "TweenRotation") return std::make_unique<TweenRotationNode>(argOr("inout"));
    if (type == "TweenScale") return std::make_unique<TweenScaleNode>(argOr("inout"));
    if (type == "TweenColor") return std::make_unique<TweenColorNode>(argOr("inout"));
    if (type == "CrossfadeClip") return std::make_unique<CrossfadeClipNode>(argOr(""));
    if (type == "StopAnimation") return std::make_unique<StopAnimationNode>();
    if (type == "WaitAnimEnd") return std::make_unique<WaitAnimEndNode>();
    if (type == "PickUp") return std::make_unique<PickUpNode>();
    if (type == "Drop") return std::make_unique<DropNode>();
    if (type == "DoorOpen") return std::make_unique<DoorSwingNode>(true, a);
    if (type == "DoorClose") return std::make_unique<DoorSwingNode>(false, a);
    if (type == "Lever") return std::make_unique<LeverNode>();
    if (type == "Collect") return std::make_unique<CollectNode>();
    if (type == "Proximity") return std::make_unique<ProximityNode>();
    if (type == "Damage") return std::make_unique<DamageNode>();
    if (type == "Heal") return std::make_unique<HealNode>();
    if (type == "IsDead") return std::make_unique<IsDeadNode>();
    if (type == "Respawn") return std::make_unique<RespawnNode>();
    if (type == "SpawnProjectile") return std::make_unique<SpawnProjectileNode>(argOr(""));
    if (type == "Shoot") return std::make_unique<ShootNode>();
    if (type == "Ammo") return std::make_unique<AmmoNode>(a.empty() ? 6 : (int)std::lround(std::atof(a.c_str())));
    if (type == "Patrol") return std::make_unique<PatrolNode>(argOr("0,0,0; 3,0,0; 3,0,3"));
    if (type == "Chase") return std::make_unique<ChaseNode>();
    if (type == "Flee") return std::make_unique<FleeNode>();
    if (type == "Wander") return std::make_unique<WanderNode>();
    if (type == "CanSee") return std::make_unique<CanSeeNode>();
    if (type == "SpawnEvery") return std::make_unique<SpawnEveryNode>(argOr(""));
    if (type == "DestroyAfter") return std::make_unique<DestroyAfterNode>(argOr("inout"));
    if (type == "PoolGet") return std::make_unique<PoolGetNode>(argOr(""));
    if (type == "PoolRelease") return std::make_unique<PoolReleaseNode>(argOr(""));
    if (type == "ScoreAdd") return std::make_unique<ScoreAddNode>();
    if (type == "ScoreSet") return std::make_unique<ScoreSetNode>();
    if (type == "ScoreGet") return std::make_unique<ScoreGetNode>();
    if (type == "Countdown") return std::make_unique<CountdownNode>(a.empty() ? 5.0f : (float)std::atof(a.c_str()));
    if (type == "Win") return std::make_unique<OutcomeNode>(true);
    if (type == "Lose") return std::make_unique<OutcomeNode>(false);
    if (type == "RestartScene") return std::make_unique<RestartSceneNode>();
    if (type == "CheckpointSave") return std::make_unique<CheckpointSaveNode>();
    if (type == "CheckpointLoad") return std::make_unique<CheckpointLoadNode>();
    if (type == "SetProgress") return std::make_unique<SetProgressNode>();
    if (type == "ShowFloatingText") return std::make_unique<ShowFloatingTextNode>();
    if (type == "FadeScreen") return std::make_unique<FadeScreenNode>();
    if (type == "SetCameraTarget") return std::make_unique<SetCameraTargetNode>();
    if (type == "Zoom") return std::make_unique<ZoomNode>();
    if (type == "SwitchCamera") return std::make_unique<SwitchCameraNode>(argOr(""));
    if (type == "PlaySoundAt") return std::make_unique<PlaySoundAtNode>();
    if (type == "StopSound") return std::make_unique<StopSoundNode>();
    if (type == "GetMoveAxis") return std::make_unique<GetMoveAxisNode>();
    if (type == "RandomPick") return std::make_unique<RandomPickNode>();
    if (type == "RandomPoint") return std::make_unique<RandomPointNode>();
    if (type == "ShuffleList") return std::make_unique<ShuffleListNode>();
    if (type == "ListMin") return std::make_unique<ListExtremeNode>(true);
    if (type == "ListMax") return std::make_unique<ListExtremeNode>(false);
    return nullptr;
}

} // namespace okay::vs
