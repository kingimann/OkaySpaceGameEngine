#include "okay/Components/BehaviorTreeRunner.hpp"

#include "okay/AI/NavMesh.hpp"
#include "okay/AI/Navigation.hpp"
#include "okay/Audio/AudioLoad.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/AnimControl.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/DialogueRunner.hpp"
#include "okay/Components/MeleeAttacker.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/SurvivalAfflictions.hpp"   // DamageHealthOn
#include "okay/Components/SurvivalComponents.hpp"    // HealthStat
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace okay {

namespace {

float Dist2D(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}
Vec3 Dir2D(const Vec3& from, const Vec3& to) {
    Vec3 d{to.x - from.x, 0.0f, to.z - from.z};
    const float m = std::sqrt(d.x * d.x + d.z * d.z);
    return m > 1e-5f ? Vec3{d.x / m, 0.0f, d.z / m} : Vec3{0, 0, 0};
}
std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
std::vector<std::string> SplitList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) { item = Trim(item); if (!item.empty()) out.push_back(item); }
    return out;
}
// The Character to animate: on the object, else the first on a child.
Character* FindBody(GameObject* go) {
    if (!go) return nullptr;
    if (Character* c = go->GetComponent<Character>()) return c;
    if (go->transform)
        for (Transform* t : go->transform->Children())
            if (t && t->gameObject) if (Character* c = t->gameObject->GetComponent<Character>()) return c;
    return nullptr;
}
AnimStateMachine* FindMachine(GameObject* go) {
    if (!go) return nullptr;
    if (auto* m = go->GetComponent<AnimStateMachine>()) return m;
    if (go->transform)
        for (Transform* t : go->transform->Children())
            if (t && t->gameObject) if (auto* m = t->gameObject->GetComponent<AnimStateMachine>()) return m;
    return nullptr;
}
AudioSource* FindAudio(GameObject* go) {
    if (!go) return nullptr;
    if (auto* a = go->GetComponent<AudioSource>()) return a;
    if (go->transform)
        for (Transform* t : go->transform->Children())
            if (t && t->gameObject) if (auto* a = t->gameObject->GetComponent<AudioSource>()) return a;
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void BehaviorTreeRunner::SetTree(std::shared_ptr<const BehaviorTree> tree) {
    Stop();
    m_tree = std::move(tree);
    m_rt.assign(m_tree ? m_tree->nodes.size() : 0, Rt{});
    m_error.clear();
}

bool BehaviorTreeRunner::LoadTree(const std::string& path, std::string* error) {
    if (!path.empty()) treePath = path;
    if (treePath.empty()) { m_error = "no tree file"; if (error) *error = m_error; return false; }
    std::string err;
    std::shared_ptr<const BehaviorTree> t = BehaviorTree::Load(treePath, &err);
    if (!t) {
        m_error = err;
        if (error) *error = err;
        return false;
    }
    SetTree(t);
    return true;
}

int BehaviorTreeRunner::Index(int id) const { return m_tree ? m_tree->IndexOf(id) : -1; }

const BehaviorTreeRunner::NodeState* BehaviorTreeRunner::State(int nodeId) const {
    const int i = Index(nodeId);
    return (i >= 0 && i < (int)m_rt.size()) ? &m_rt[(std::size_t)i] : nullptr;
}

std::vector<int> BehaviorTreeRunner::ActivePath() const {
    std::vector<int> out;
    if (!m_tree) return out;
    for (int id : m_tree->Order())
        if (const NodeState* s = State(id); s && s->running) out.push_back(id);
    return out;
}

Scene* BehaviorTreeRunner::SceneOf() const { return gameObject ? gameObject->scene() : nullptr; }
Vec3 BehaviorTreeRunner::Position() const { return transform ? transform->Position() : Vec3{0, 0, 0}; }
Vec3 BehaviorTreeRunner::Forward() const { return transform ? transform->Rotation() * Vec3::Forward : Vec3::Forward; }
Character* BehaviorTreeRunner::Body() const { return FindBody(gameObject); }

float BehaviorTreeRunner::Rand() {
    m_seed = m_seed * 1664525u + 1013904223u;
    return (float)((m_seed >> 8) & 0xFFFFFF) / (float)0x1000000;
}

void BehaviorTreeRunner::Start() {
    m_started = true;
    m_npc = gameObject ? gameObject->GetComponent<NPCController>() : nullptr;
    m_agent = gameObject ? gameObject->GetComponent<NavMeshAgent>() : nullptr;
    if (m_agent) m_agentSpeed0 = m_agent->speed;
    if (m_npc) m_npc->SetExternalBrain(true);
    m_home = homeOverride ? homePoint : Position();
    m_seed = (std::uint32_t)(std::fabs(m_home.x) * 73856093.0f + std::fabs(m_home.z) * 19349663.0f) + 7u;
    if (!m_tree && !treePath.empty()) {
        std::string err;
        if (!LoadTree(std::string(), &err))
            Log::Warning("[bt] '", gameObject ? gameObject->name : std::string("?"), "': ", err);
    }
    if (runOnStart && m_tree) Run();
}

void BehaviorTreeRunner::Run() {
    if (!m_tree || m_tree->root < 0 || m_tree->IndexOf(m_tree->root) < 0) { m_running = false; return; }
    if (m_running) Abort(Index(m_tree->root));
    m_rt.assign(m_tree->nodes.size(), Rt{});
    blackboard.Clear();
    for (const auto& k : m_tree->keys) blackboard.Declare(k.name, k.value);
    for (const auto& ov : overrides) {
        if (ov.first.empty()) continue;
        if (const BtValue* cur = blackboard.Find(ov.first)) blackboard.Set(ov.first, BtValue::FromText(cur->type, ov.second));
        else blackboard.Set(ov.first, BtValue::Infer(ov.second));
    }
    m_time = 0.0f; m_acc = 0.0f;
    m_lastResult = BtStatus::Invalid;
    m_completions = 0;
    m_running = true;
    if (m_started && !homeOverride) m_home = Position();
}

void BehaviorTreeRunner::Stop() {
    if (m_running && m_tree && m_tree->root >= 0) {
        const int r = Index(m_tree->root);
        if (r >= 0) Abort(r);
    }
    m_running = false;
    StopMoving();
}

void BehaviorTreeRunner::OnDisable() { StopMoving(); }

void BehaviorTreeRunner::OnDestroy() {
    m_running = false;
    if (m_npc) m_npc->SetExternalBrain(false);
}

void BehaviorTreeRunner::Update(float dt) {
    if (!transform || !gameObject || dt <= 0.0f) return;
    if (!IsLocallyControlled(gameObject)) return;   // MP: the authority runs the tree; others follow via NetworkSync
    if (m_running && m_tree && m_tree->root >= 0) {
        m_time += dt;
        m_acc += dt;
        const float period = tickRate > 0.0f ? 1.0f / tickRate : 0.0f;
        if (m_acc >= period) {
            const float tdt = m_acc;
            m_acc = 0.0f;
            const int r = Index(m_tree->root);
            if (r >= 0) {
                const BtStatus s = Tick(r, tdt);
                if (s != BtStatus::Running) {
                    m_lastResult = s;
                    ++m_completions;
                    if (debug) Log::Info("[bt] '", gameObject->name, "': tree finished with ", BtStatusName(s), " (run ", m_completions, ")");
                }
            }
        }
    }
    StepMover(dt);
}

// ---------------------------------------------------------------------------
// Ticking
// ---------------------------------------------------------------------------
void BehaviorTreeRunner::Enter(int idx) {
    Rt& r = m_rt[(std::size_t)idx];
    r.running = true;
    r.started = m_time;
    r.ticks = 0;
    r.status = BtStatus::Running;
    r.cur = 0;
    r.order.clear();
    r.results.clear();
    r.count = 0;
    r.timer = 0.0f;
    r.flag = false;
    r.hasGoal = false;
    r.entered = false;
}

void BehaviorTreeRunner::Finish(int idx, BtStatus s) {
    Rt& r = m_rt[(std::size_t)idx];
    r.running = false;
    r.status = s;
    const BtNode& n = m_tree->nodes[(std::size_t)idx];
    for (int sv : m_tree->Services(n.id)) if (const int si = Index(sv); si >= 0) m_rt[(std::size_t)si].running = false;
    // A finished node's children are done too (a child still marked running was
    // left behind by a composite that gave up on it).
    for (int c : m_tree->FlowChildren(n.id)) if (const int ci = Index(c); ci >= 0 && m_rt[(std::size_t)ci].running) Abort(ci);
    if (debug && (s == BtStatus::Success || s == BtStatus::Failure))
        Log::Info("[bt] '", gameObject ? gameObject->name : std::string("?"), "': ", n.Def().title,
                  n.name.empty() ? std::string() : " '" + n.name + "'", " -> ", BtStatusName(s));
}

void BehaviorTreeRunner::Abort(int idx) {
    if (idx < 0 || idx >= (int)m_rt.size()) return;
    Rt& r = m_rt[(std::size_t)idx];
    if (!r.running) return;
    const BtNode& n = m_tree->nodes[(std::size_t)idx];
    for (int c : m_tree->FlowChildren(n.id)) if (const int ci = Index(c); ci >= 0) Abort(ci);
    for (int sv : m_tree->Services(n.id)) if (const int si = Index(sv); si >= 0) m_rt[(std::size_t)si].running = false;
    switch (n.type) {
        case BtNodeType::MoveTo: case BtNodeType::Patrol: case BtNodeType::Wander:
        case BtNodeType::Chase: case BtNodeType::Flee:
            StopMoving();
            break;
        case BtNodeType::Custom:
            if (r.entered) CallCustom(n, r, "abort");
            break;
        case BtNodeType::PlayAnimation:
            if (n.Flag("wait")) StopClipFrom(gameObject);
            break;
        case BtNodeType::StartDialogue:   // end the conversation this task started
            if (r.flag) if (auto* dr = gameObject->GetComponent<DialogueRunner>(); dr && dr->IsActive()) dr->Stop();
            break;
        default: break;
    }
    r.running = false;
    r.status = BtStatus::Aborted;
}

BtStatus BehaviorTreeRunner::Tick(int idx, float dt) {
    if (idx < 0 || idx >= (int)m_rt.size()) return BtStatus::Failure;
    Rt& r = m_rt[(std::size_t)idx];
    const bool first = !r.running;
    if (first) Enter(idx);
    ++r.ticks;
    r.lastTick = m_time;
    RunServices(idx, dt);
    const BtStatus s = TickNode(idx, dt, first);
    if (s != BtStatus::Running) Finish(idx, s);
    else r.status = BtStatus::Running;
    return s;
}

BtStatus BehaviorTreeRunner::TickNode(int idx, float dt, bool first) {
    const BtNode& n = m_tree->nodes[(std::size_t)idx];
    switch (n.Kind()) {
        case BtNodeKind::Composite: return TickComposite(idx, dt);
        case BtNodeKind::Decorator: return TickDecorator(idx, dt);
        case BtNodeKind::Task:      return TickTask(idx, dt, first);
        case BtNodeKind::Service:   return BtStatus::Failure;   // a service in the flow does nothing
    }
    return BtStatus::Failure;
}

void BehaviorTreeRunner::RunServices(int idx, float dt) {
    const BtNode& n = m_tree->nodes[(std::size_t)idx];
    for (int sv : m_tree->Services(n.id)) {
        const int si = Index(sv);
        if (si < 0) continue;
        Rt& sr = m_rt[(std::size_t)si];
        const BtNode& s = m_tree->nodes[(std::size_t)si];
        sr.running = true;
        const float interval = s.Num("interval");
        if (sr.stamp >= 0.0f && interval > 0.0f && m_time - sr.stamp < interval) continue;
        const float elapsed = sr.stamp < 0.0f ? dt : m_time - sr.stamp;
        sr.stamp = m_time;
        sr.lastTick = m_time;
        ++sr.ticks;
        RunService(si, elapsed);
    }
}

void BehaviorTreeRunner::RunService(int idx, float elapsed) {
    const BtNode& n = m_tree->nodes[(std::size_t)idx];
    Scene* sc = SceneOf();
    if (n.type == BtNodeType::Perception) {
        const std::string tk = n.Str("targetKey"), sk = n.Str("seeKey"), lk = n.Str("lastSeenKey"), nk = n.Str("sinceKey");
        GameObject* tgt = nullptr;
        const std::string tn = n.Str("targetName");
        if (sc && !tn.empty()) tgt = sc->Find(tn);
        if (!tgt && !tk.empty()) tgt = blackboard.GetGameObject(tk, sc);
        bool noticed = false;
        if (tgt && tgt->transform && tgt != gameObject) {
            const Vec3 pos = Position(), tp = tgt->transform->Position();
            const float d = Dist2D(pos, tp);
            const float range = n.Num("range"), angle = n.Num("angle"), hearing = n.Num("hearing");
            bool inCone = angle >= 359.0f;
            if (!inCone) {
                const Vec3 to = Dir2D(pos, tp);
                if (to.x == 0.0f && to.z == 0.0f) inCone = true;
                else {
                    Vec3 fwd = Forward();
                    const float fm = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
                    if (fm < 1e-5f) inCone = true;
                    else {
                        float dot = (to.x * fwd.x + to.z * fwd.z) / fm;
                        dot = std::clamp(dot, -1.0f, 1.0f);
                        inCone = std::acos(dot) * 57.29578f <= angle * 0.5f;
                    }
                }
            }
            const bool seen = d <= range && inCone && (!n.Flag("lineOfSight") || LineOfSight(pos, tgt, n.Num("eyeHeight")));
            const bool heard = hearing > 0.0f && d <= hearing;
            noticed = seen || heard;
            if (noticed) {
                if (!tk.empty()) blackboard.Set(tk, BtValue::Object(tgt->name, tgt->InstanceId()));
                if (!lk.empty()) blackboard.Set(lk, BtValue::Vector(tp));
                if (!nk.empty()) blackboard.Set(nk, BtValue::Number(0.0f));
            }
        }
        if (!noticed && !nk.empty()) blackboard.Set(nk, BtValue::Number(std::min(9999.0f, blackboard.GetNumber(nk, 9999.0f) + elapsed)));
        if (!sk.empty()) blackboard.Set(sk, BtValue::Bool(noticed));
        if (m_npc) m_npc->SetAlert(noticed);
    } else if (n.type == BtNodeType::HealthWatcher) {
        float hp = 0.0f, max = 0.0f;
        if (m_npc) { hp = m_npc->health; max = m_npc->maxHealth; }
        else if (auto* s = gameObject->GetComponent<SurvivalStats>()) { hp = s->health; max = s->maxHealth; }
        else if (auto* h = gameObject->GetComponent<HealthStat>()) { hp = h->health; max = h->maxHealth; }
        else return;
        const float pct = max > 0.0f ? std::clamp(hp / max, 0.0f, 1.0f) : 1.0f;
        if (const std::string k = n.Str("healthKey"); !k.empty()) blackboard.Set(k, BtValue::Number(hp));
        if (const std::string k = n.Str("pctKey"); !k.empty()) blackboard.Set(k, BtValue::Number(pct));
        if (const std::string k = n.Str("lowKey"); !k.empty()) blackboard.Set(k, BtValue::Bool(pct < n.Num("threshold")));
    }
}

// ---- composites -------------------------------------------------------------
BtStatus BehaviorTreeRunner::TickComposite(int idx, float dt) {
    const BtNode& n = m_tree->nodes[(std::size_t)idx];
    Rt& r = m_rt[(std::size_t)idx];
    std::vector<int> kids;
    for (int c : m_tree->FlowChildren(n.id)) if (const int ci = Index(c); ci >= 0) kids.push_back(ci);
    if (kids.empty()) return BtStatus::Failure;
    const int count = (int)kids.size();
    // An earlier Guard child watches while a later sibling runs: in a selector its
    // test turning true takes the run over, in a sequence its test turning false
    // ends the run.
    auto guardWatches = [&](int ci) {
        const BtNode& g = m_tree->nodes[(std::size_t)ci];
        if (g.type != BtNodeType::Guard) return false;
        const std::string a = g.Str("abort");
        return a == "lower" || a == "both";
    };
    switch (n.type) {
        case BtNodeType::Sequence: {
            for (int i = 0; i < r.cur && i < count; ++i)
                if (guardWatches(kids[(std::size_t)i]) && !Test(m_tree->nodes[(std::size_t)kids[(std::size_t)i]])) {
                    if (r.cur < count) Abort(kids[(std::size_t)r.cur]);
                    return BtStatus::Failure;
                }
            while (r.cur < count) {
                const BtStatus s = Tick(kids[(std::size_t)r.cur], dt);
                if (s == BtStatus::Running) return s;
                if (s != BtStatus::Success) return BtStatus::Failure;
                ++r.cur;
            }
            return BtStatus::Success;
        }
        case BtNodeType::Selector: {
            for (int i = 0; i < r.cur && i < count; ++i)
                if (guardWatches(kids[(std::size_t)i]) && Test(m_tree->nodes[(std::size_t)kids[(std::size_t)i]])) {
                    if (r.cur < count) Abort(kids[(std::size_t)r.cur]);
                    r.cur = i;
                    break;
                }
            while (r.cur < count) {
                const BtStatus s = Tick(kids[(std::size_t)r.cur], dt);
                if (s == BtStatus::Running) return s;
                if (s == BtStatus::Success) return s;
                ++r.cur;
            }
            return BtStatus::Failure;
        }
        case BtNodeType::RandomSelector: {
            if (r.order.empty()) {
                // Weighted draw without replacement: heavier children tend to go first.
                std::vector<int> left(kids.begin(), kids.end());
                std::vector<float> w;
                for (int ci : left) w.push_back(std::max(0.0f, m_tree->nodes[(std::size_t)ci].Has("weight") ? m_tree->nodes[(std::size_t)ci].Num("weight") : 1.0f));
                while (!left.empty()) {
                    float total = 0.0f;
                    for (float x : w) total += x;
                    std::size_t pick = 0;
                    if (total > 0.0f) {
                        float roll = Rand() * total;
                        for (pick = 0; pick + 1 < left.size(); ++pick) { roll -= w[pick]; if (roll < 0.0f) break; }
                    } else pick = (std::size_t)(Rand() * (float)left.size()) % left.size();
                    r.order.push_back(left[pick]);
                    left.erase(left.begin() + (std::ptrdiff_t)pick);
                    w.erase(w.begin() + (std::ptrdiff_t)pick);
                }
                r.cur = 0;
            }
            while (r.cur < (int)r.order.size()) {
                const BtStatus s = Tick(r.order[(std::size_t)r.cur], dt);
                if (s == BtStatus::Running) return s;
                if (s == BtStatus::Success) return s;
                ++r.cur;
            }
            return BtStatus::Failure;
        }
        case BtNodeType::Parallel: {
            if ((int)r.results.size() != count) r.results.assign((std::size_t)count, BtStatus::Invalid);
            int succ = 0, fail = 0, running = 0;
            for (int i = 0; i < count; ++i) {
                BtStatus& s = r.results[(std::size_t)i];
                if (s == BtStatus::Invalid || s == BtStatus::Running) s = Tick(kids[(std::size_t)i], dt);
                if (s == BtStatus::Success) ++succ;
                else if (s == BtStatus::Running) ++running;
                else ++fail;
            }
            const bool succOne = n.Str("success") == "one", failOne = n.Str("failure") == "one";
            auto abortRest = [&]() { for (int i = 0; i < count; ++i) if (r.results[(std::size_t)i] == BtStatus::Running) Abort(kids[(std::size_t)i]); };
            if (failOne && fail > 0) { abortRest(); return BtStatus::Failure; }
            if (succOne && succ > 0) { abortRest(); return BtStatus::Success; }
            if (running > 0) return BtStatus::Running;
            if (succOne) return BtStatus::Failure;          // nobody succeeded
            return succ == count ? BtStatus::Success : BtStatus::Failure;
        }
        default: return BtStatus::Failure;
    }
}

// ---- decorators -------------------------------------------------------------
BtStatus BehaviorTreeRunner::TickDecorator(int idx, float dt) {
    const BtNode& n = m_tree->nodes[(std::size_t)idx];
    Rt& r = m_rt[(std::size_t)idx];
    const std::vector<int> flow = m_tree->FlowChildren(n.id);
    const int child = flow.empty() ? -1 : Index(flow.front());
    const bool childRunning = child >= 0 && m_rt[(std::size_t)child].running;
    switch (n.type) {
        case BtNodeType::Inverter: {
            if (child < 0) return BtStatus::Failure;
            const BtStatus s = Tick(child, dt);
            return s == BtStatus::Running ? s : (s == BtStatus::Success ? BtStatus::Failure : BtStatus::Success);
        }
        case BtNodeType::Succeeder: {
            if (child < 0) return BtStatus::Success;
            return Tick(child, dt) == BtStatus::Running ? BtStatus::Running : BtStatus::Success;
        }
        case BtNodeType::Repeat: {
            if (child < 0) return BtStatus::Failure;
            const int want = n.Int("count");
            const BtStatus s = Tick(child, dt);
            if (s == BtStatus::Running) return s;
            ++r.count;
            if (want > 0 && r.count >= want) return BtStatus::Success;
            return BtStatus::Running;   // again next tick
        }
        case BtNodeType::UntilFail: {
            if (child < 0) return BtStatus::Failure;
            const BtStatus s = Tick(child, dt);
            if (s == BtStatus::Failure) return BtStatus::Success;
            return BtStatus::Running;
        }
        case BtNodeType::Cooldown: {
            if (child < 0) return BtStatus::Failure;
            if (!childRunning && m_time < r.until) return BtStatus::Failure;
            const BtStatus s = Tick(child, dt);
            if (s != BtStatus::Running) r.until = m_time + n.Num("seconds");
            return s;
        }
        case BtNodeType::TimeLimit: {
            if (child < 0) return BtStatus::Failure;
            if (m_time - r.started > n.Num("seconds")) { Abort(child); return BtStatus::Failure; }
            return Tick(child, dt);
        }
        case BtNodeType::Condition:
        case BtNodeType::Guard: {
            const bool watchSelf = n.type == BtNodeType::Condition || n.Str("abort") != "lower";
            if (childRunning && !watchSelf) return Tick(child, dt);
            const bool ok = Test(n);
            if (child < 0) return ok ? BtStatus::Success : BtStatus::Failure;
            if (!ok) { if (childRunning) Abort(child); return BtStatus::Failure; }
            return Tick(child, dt);
        }
        default: return BtStatus::Failure;
    }
}

// ---- conditions -------------------------------------------------------------
bool BehaviorTreeRunner::ResolveTarget(const std::string& spec, Vec3& pos, GameObject** obj) const {
    if (obj) *obj = nullptr;
    if (spec.empty()) return false;
    Scene* sc = SceneOf();
    if (const BtValue* v = blackboard.Find(spec)) {
        if (v->type == BtValue::Type::Vector) { pos = v->vector; return true; }
        GameObject* g = v->AsObject(sc);
        if (!g || !g->transform) return false;
        pos = g->transform->Position();
        if (obj) *obj = g;
        return true;
    }
    GameObject* g = sc ? sc->Find(spec) : nullptr;
    if (!g || !g->transform) return false;
    pos = g->transform->Position();
    if (obj) *obj = g;
    return true;
}

bool BehaviorTreeRunner::LineOfSight(const Vec3& from, GameObject* target, float eyeHeight) const {
    Scene* sc = SceneOf();
    if (!sc || !target || !target->transform) return false;
    const Vec3 tp = target->transform->Position();
    const Vec3 o{from.x, from.y + eyeHeight, from.z};
    const Vec3 e{tp.x, tp.y + eyeHeight, tp.z};
    Vec3 d = e - o;
    const float m = d.Magnitude();
    if (m < 1e-4f) return true;
    d = d * (1.0f / m);
    const RaycastHit3D hit = sc->physics3D().Raycast(*sc, o, d, m, gameObject, Physics3D::kAllLayers, false);
    if (!hit) return true;
    if (hit.gameObject && (hit.gameObject == target || hit.gameObject->IsSelfOrDescendantOf(target))) return true;
    return hit.distance >= m - 0.3f;
}

float BehaviorTreeRunner::HealthFraction() const {
    if (m_npc) return m_npc->maxHealth > 0.0f ? m_npc->health / m_npc->maxHealth : 1.0f;
    if (auto* s = gameObject->GetComponent<SurvivalStats>()) return s->Fraction();
    if (auto* h = gameObject->GetComponent<HealthStat>()) return h->maxHealth > 0.0f ? h->health / h->maxHealth : 1.0f;
    return 1.0f;
}

bool BehaviorTreeRunner::Test(const BtNode& n) const {
    const std::string kind = n.Str("kind");
    if (kind == "distance") {
        Vec3 p;
        if (!ResolveTarget(n.Str("target"), p)) return false;
        const float d = Dist2D(Position(), p), want = n.Num("distance");
        const std::string op = n.Str("op");
        return (op == ">" || op == ">=") ? d > want : d < want;
    }
    if (kind == "lineOfSight") {
        Vec3 p; GameObject* g = nullptr;
        if (!ResolveTarget(n.Str("target"), p, &g) || !g) return false;
        const float d = Dist2D(Position(), p), want = n.Num("distance");
        if (want > 0.0f && d > want) return false;
        return LineOfSight(Position(), g, 1.2f);
    }
    if (kind == "health") return HealthFraction() < n.Num("fraction");
    // blackboard
    const std::string key = n.Str("key"), op = n.Str("op");
    const BtValue* v = blackboard.Find(key);
    if (!v) return op == "unset";
    if (op == "set") return v->IsSet() && (v->type != BtValue::Type::String || !v->text.empty());
    if (op == "unset") return !(v->IsSet() && (v->type != BtValue::Type::String || !v->text.empty()));
    const std::string text = n.Str("value");
    if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        const float a = v->AsNumber(), b = BtValue::Infer(text).AsNumber();
        if (op == "<") return a < b;
        if (op == "<=") return a <= b;
        if (op == ">") return a > b;
        return a >= b;
    }
    bool eq = false;
    switch (v->type) {
        case BtValue::Type::Number: eq = v->number == BtValue::Infer(text).AsNumber(); break;
        case BtValue::Type::Bool:   eq = v->boolean == BtValue::FromText(BtValue::Type::Bool, text).boolean; break;
        case BtValue::Type::Vector: eq = (v->vector - BtValue::FromText(BtValue::Type::Vector, text).vector).Magnitude() < 1e-4f; break;
        case BtValue::Type::Object: eq = text.empty() ? v->IsSet() : v->AsString() == Trim(text); break;
        default: eq = v->text == text; break;
    }
    return op == "!=" ? !eq : eq;
}

// ---- tasks ------------------------------------------------------------------
BtStatus BehaviorTreeRunner::ScriptStatus(const vs::VsValue& v) const {
    if (v.IsString()) {
        const std::string s = Lower(Trim(v.AsString()));
        if (s == "running" || s == "run") return BtStatus::Running;
        if (s == "failure" || s == "fail" || s == "failed" || s == "false") return BtStatus::Failure;
        return BtStatus::Success;
    }
    return v.AsBool() ? BtStatus::Success : BtStatus::Failure;
}

BtStatus BehaviorTreeRunner::CallCustom(const BtNode& n, Rt& r, const char* phase) {
    r.entered = true;
    if (onCustomTask) return onCustomTask(*this, n, phase);
    const std::string fn = n.Str("function");
    if (fn.empty() || !gameObject || !ScriptsRun(gameObject)) return BtStatus::Failure;
    bool found = false;
    BtStatus out = BtStatus::Failure;
    for (ScriptComponent* sc : gameObject->GetComponents<ScriptComponent>()) {
        if (!sc || !sc->VM() || !sc->VM()->HasEvent(fn)) continue;
        found = true;
        std::string err;
        const vs::VsValue ret = sc->VM()->CallEventArgs(fn, {vs::VsValue(std::string(phase))}, &err);
        if (!err.empty()) { Log::Warning("[bt] ", fn, "(\"", phase, "\"): ", err); return BtStatus::Failure; }
        out = ScriptStatus(ret);
    }
    return found ? out : BtStatus::Failure;
}

void BehaviorTreeRunner::SendEvent(const std::string& name, bool broadcast, GameObject* to) {
    if (name.empty()) return;
    auto deliver = [&](GameObject* g) {
        if (!g || !ScriptsRun(g)) return;
        for (ScriptComponent* sc : g->GetComponents<ScriptComponent>())
            if (sc && sc->VM()) sc->VM()->CallEventArgs("on_bt_event", {vs::VsValue(name)});
        for (VisualScriptComponent* vc : g->GetComponents<VisualScriptComponent>())
            if (vc) vc->SendEvent(name, vs::VsValue(name), gameObject);
    };
    Scene* sc = SceneOf();
    if (broadcast && sc) {
        for (const auto& up : sc->Objects()) if (up && up->active) deliver(up.get());
        for (ActionList* al : sc->FindObjectsOfType<ActionList>()) if (al) al->ReceiveMessage(name);
    } else deliver(to ? to : gameObject);
    if (onEvent) onEvent(name);
}

BtStatus BehaviorTreeRunner::TickTask(int idx, float dt, bool first) {
    const BtNode& n = m_tree->nodes[(std::size_t)idx];
    Rt& r = m_rt[(std::size_t)idx];
    Scene* sc = SceneOf();
    switch (n.type) {
        case BtNodeType::Wait: {
            if (first) {
                const float lo = std::max(0.0f, n.Num("min")), hi = n.Num("max");
                r.timer = hi > lo ? lo + Rand() * (hi - lo) : lo;
            }
            return m_time - r.started >= r.timer ? BtStatus::Success : BtStatus::Running;
        }
        case BtNodeType::MoveTo: {
            Vec3 goal;
            const std::string spec = n.Str("target");
            if (spec.empty()) goal = n.Vec("position");
            else if (!ResolveTarget(spec, goal)) return BtStatus::Failure;
            const Move m = MoveToward(goal, n.Num("stopDistance"), n.Num("speed"), n.Flag("run"), dt);
            return m == Move::Arrived ? BtStatus::Success : m == Move::Blocked ? BtStatus::Failure : BtStatus::Running;
        }
        case BtNodeType::Patrol: {
            const std::vector<std::string> names = SplitList(n.Str("waypoints"));
            if (names.empty() || !sc) return BtStatus::Failure;
            const int count = (int)names.size();
            if (r.index < 0 || r.index >= count) { r.index = 0; r.dir = 1; }
            auto advance = [&]() {
                ++r.count;
                if (count <= 1) return;
                if (n.Str("mode") == "pingpong") {
                    r.index += r.dir;
                    if (r.index >= count) { r.index = count - 2; r.dir = -1; }
                    else if (r.index < 0) { r.index = 1; r.dir = 1; }
                } else r.index = (r.index + 1) % count;
            };
            if (r.flag) {   // waiting at a waypoint
                if (m_time < r.timer) return BtStatus::Running;
                r.flag = false;
                advance();
                if (n.Flag("once") && r.count >= count) return BtStatus::Success;
            }
            GameObject* wp = nullptr;
            for (int tries = 0; tries < count && !wp; ++tries) {
                wp = sc->Find(names[(std::size_t)r.index]);
                if (!wp || !wp->transform) { wp = nullptr; advance(); }
            }
            if (!wp) return BtStatus::Failure;
            const Move m = MoveToward(wp->transform->Position(), n.Num("stopDistance"), n.Num("speed"), false, dt);
            if (m == Move::Arrived) {
                const float wait = n.Num("wait");
                if (wait > 0.0f) { r.flag = true; r.timer = m_time + wait; }
                else { advance(); if (n.Flag("once") && r.count >= count) return BtStatus::Success; }
            } else if (m == Move::Blocked) advance();
            return BtStatus::Running;
        }
        case BtNodeType::Wander: {
            if (r.flag) {   // pausing at a point
                if (m_time < r.timer) return BtStatus::Running;
                r.flag = false; r.hasGoal = false;
                if (n.Flag("once")) return BtStatus::Success;
            }
            if (!r.hasGoal) {
                const float radius = std::max(0.1f, n.Num("radius"));
                Vec3 p;
                if (!(sc && Navigation::RandomPoint(*sc, m_home, radius, m_seed, p))) {
                    const float a = Rand() * 6.2831853f, rr = std::sqrt(Rand()) * radius;
                    p = {m_home.x + std::cos(a) * rr, m_home.y, m_home.z + std::sin(a) * rr};
                }
                r.goal = p; r.hasGoal = true;
            }
            const Move m = MoveToward(r.goal, 0.5f, n.Num("speed"), false, dt);
            if (m == Move::Arrived) {
                const float lo = std::max(0.0f, n.Num("minPause")), hi = n.Num("maxPause");
                r.flag = true; r.timer = m_time + (hi > lo ? lo + Rand() * (hi - lo) : lo);
            } else if (m == Move::Blocked) r.hasGoal = false;
            return BtStatus::Running;
        }
        case BtNodeType::Chase: {
            Vec3 tp;
            if (!ResolveTarget(n.Str("target"), tp)) return BtStatus::Failure;
            const float giveUp = n.Num("giveUp");
            if (giveUp > 0.0f && Dist2D(Position(), tp) > giveUp) { StopMoving(); return BtStatus::Failure; }
            const Move m = MoveToward(tp, n.Num("stopDistance"), n.Num("speed"), n.Flag("run"), dt);
            return m == Move::Arrived ? BtStatus::Success : m == Move::Blocked ? BtStatus::Failure : BtStatus::Running;
        }
        case BtNodeType::Flee: {
            Vec3 tp;
            if (!ResolveTarget(n.Str("target"), tp)) return BtStatus::Failure;
            const Vec3 pos = Position();
            if (Dist2D(pos, tp) >= n.Num("distance")) { StopMoving(); return BtStatus::Success; }
            const Vec3 away = Dir2D(tp, pos);
            if (away.x == 0.0f && away.z == 0.0f) return BtStatus::Running;
            Vec3 goal{pos.x + away.x * 4.0f, pos.y, pos.z + away.z * 4.0f};
            Vec3 on;
            if (sc && Navigation::SamplePosition(*sc, goal, 3.0f, on)) goal = on;
            const Move m = MoveToward(goal, 0.3f, n.Num("speed"), n.Flag("run"), dt);
            return m == Move::Blocked ? BtStatus::Failure : BtStatus::Running;
        }
        case BtNodeType::LookAt: {
            Vec3 tp; GameObject* g = nullptr;
            if (!ResolveTarget(n.Str("target"), tp, &g)) return BtStatus::Failure;
            const Vec3 dir = Dir2D(Position(), tp);
            if (dir.x == 0.0f && dir.z == 0.0f) return BtStatus::Success;
            FaceToward(dir, n.Num("turnSpeed"), dt);
            if (n.Flag("head") && g) if (Character* ch = Body()) ch->lookAtTarget = g->name;
            Vec3 fwd = Forward(); fwd.y = 0.0f;
            const float fm = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
            if (fm < 1e-5f) return BtStatus::Success;
            const float dot = std::clamp((fwd.x * dir.x + fwd.z * dir.z) / fm, -1.0f, 1.0f);
            return std::acos(dot) * 57.29578f <= std::max(0.5f, n.Num("tolerance")) ? BtStatus::Success : BtStatus::Running;
        }
        case BtNodeType::PlayAnimation: {
            if (first) {
                r.flag = false;
                if (const std::string clip = n.Str("clip"); !clip.empty()) {
                    bool warned = false;
                    r.flag = PlayClipFrom(gameObject, clip, warned);
                }
                if (AnimStateMachine* sm = FindMachine(gameObject)) {
                    if (const std::string tr = n.Str("trigger"); !tr.empty()) sm->SetTrigger(tr);
                    if (const std::string p = n.Str("param"); !p.empty()) {
                        const BtValue v = BtValue::Infer(n.Str("value"));
                        if (v.type == BtValue::Type::Bool) sm->SetBool(p, v.boolean);
                        else sm->SetFloat(p, v.AsNumber());
                    }
                }
                r.stamp = m_time + 10.0f;   // never wait longer than this for a clip
            }
            if (!n.Flag("wait") || !r.flag) return BtStatus::Success;
            if (ClipNormalizedFrom(gameObject) >= 0.99f || m_time >= r.stamp) { StopClipFrom(gameObject); return BtStatus::Success; }
            return BtStatus::Running;
        }
        case BtNodeType::Attack: {
            Vec3 tp; GameObject* g = nullptr;
            if (!ResolveTarget(n.Str("target"), tp, &g) || !g) return BtStatus::Failure;
            float range = n.Num("range");
            if (range <= 0.0f) range = m_npc ? m_npc->attackRange : 1.6f;
            const Vec3 pos = Position();
            if (Dist2D(pos, tp) > range + 0.05f) return BtStatus::Failure;
            FaceToward(Dir2D(pos, tp), 720.0f, dt);
            if (auto* ma = gameObject->GetComponent<MeleeAttacker>()) {
                if (!ma->Ready()) return BtStatus::Running;
                ma->Swing();
                SendEvent("attack");
                return BtStatus::Success;
            }
            if (m_npc) {
                if (!m_npc->AttackNow(g)) return BtStatus::Running;
                SendEvent("attack");
                return BtStatus::Success;
            }
            if (m_time < r.until) return BtStatus::Running;
            const float windup = n.Num("windup");
            if (windup > 0.0f && !r.flag) { r.flag = true; r.timer = m_time + windup; return BtStatus::Running; }
            if (r.flag && m_time < r.timer) return BtStatus::Running;
            float damage = n.Num("damage");
            if (damage <= 0.0f) damage = 10.0f;
            DamageHealthOn(g, damage);
            r.flag = false;
            r.until = m_time + std::max(0.0f, n.Num("interval"));
            if (sc) for (ActionList* al : sc->FindObjectsOfType<ActionList>()) if (al) al->ReceiveMessage("npc_attack");
            SendEvent("attack");
            return BtStatus::Success;
        }
        case BtNodeType::SetBlackboard: {
            const std::string key = n.Str("key");
            if (key.empty()) return BtStatus::Failure;
            const std::string text = n.Str("value");
            if (const BtValue* cur = blackboard.Find(key)) blackboard.Set(key, BtValue::FromText(cur->type, text));
            else blackboard.Set(key, BtValue::Infer(text));
            return BtStatus::Success;
        }
        case BtNodeType::SendEvent: {
            const std::string ev = n.Str("event");
            if (ev.empty()) return BtStatus::Failure;
            GameObject* to = nullptr;
            if (const std::string tn = n.Str("target"); !tn.empty()) {
                to = sc ? sc->Find(tn) : nullptr;
                if (!to) return BtStatus::Failure;
            }
            SendEvent(ev, n.Flag("broadcast"), to);
            return BtStatus::Success;
        }
        case BtNodeType::RunScript: {
            const std::string fn = n.Str("function");
            if (fn.empty() || !ScriptsRun(gameObject)) return BtStatus::Failure;
            bool found = false, failed = false;
            for (ScriptComponent* scpt : gameObject->GetComponents<ScriptComponent>()) {
                if (!scpt || !scpt->VM() || !scpt->VM()->HasEvent(fn)) continue;
                found = true;
                std::string err;
                const vs::VsValue ret = scpt->VM()->CallEventArgs(fn, {vs::VsValue(n.Str("arg"))}, &err);
                if (!err.empty()) { Log::Warning("[bt] ", fn, "(): ", err); failed = true; }
                else if (ret.IsBool() && !ret.AsBool()) failed = true;
                else if (ret.IsString() && ScriptStatus(ret) == BtStatus::Failure) failed = true;
            }
            return (found && !failed) ? BtStatus::Success : BtStatus::Failure;
        }
        case BtNodeType::PlayAudio: {
            AudioSource* au = FindAudio(gameObject);
            if (!au) return BtStatus::Failure;
            if (first) {
                if (const std::string clip = n.Str("clip"); !clip.empty() && clip != au->clipPath) {
                    if (auto loaded = LoadAudioFile(BehaviorTree::ResolvePath(clip))) { au->clip = *loaded; au->clipPath = clip; }
                    else Log::Warning("[bt] Play Sound: cannot load '", clip, "'");
                }
                if (n.Flag("oneShot")) au->PlayOneShot(); else au->Play();
                r.stamp = m_time + 60.0f;
            }
            if (!n.Flag("wait")) return BtStatus::Success;
            return (au->IsPlaying() && au->RemainingSeconds() > 0.0f && m_time < r.stamp) ? BtStatus::Running : BtStatus::Success;
        }
        case BtNodeType::StartDialogue: {
            DialogueRunner* dr = gameObject->GetComponent<DialogueRunner>();
            if (!dr) return BtStatus::Failure;
            if (first && !dr->IsActive()) {   // one the player already started is only waited on
                if (const std::string g = n.Str("graph"); !g.empty() && g != dr->graphFile) {
                    dr->graphFile = g;
                    std::string err;
                    if (!dr->LoadGraph(&err)) { Log::Warning("[bt] Start Dialogue: ", err); return BtStatus::Failure; }
                }
                if (!dr->StartDialogue()) return BtStatus::Failure;
                r.flag = true;
            }
            if (!n.Flag("wait")) return BtStatus::Success;
            return dr->IsActive() ? BtStatus::Running : BtStatus::Success;
        }
        case BtNodeType::Custom:
            return CallCustom(n, r, first ? "enter" : "tick");
        default:
            return BtStatus::Failure;
    }
}

// ---------------------------------------------------------------------------
// Moving the body
// ---------------------------------------------------------------------------
void BehaviorTreeRunner::FaceToward(const Vec3& dir, float degPerSec, float dt) {
    if (!transform || (dir.x == 0.0f && dir.z == 0.0f)) return;
    const Quat want = Quat::LookRotation(Vec3{dir.x, 0.0f, dir.z});
    if (degPerSec <= 0.0f) { transform->SetRotation(want); return; }
    float t = (degPerSec * dt) / 180.0f;
    if (t > 1.0f) t = 1.0f;
    transform->SetRotation(Quat::Slerp(transform->Rotation(), want, t));
}

BehaviorTreeRunner::Move BehaviorTreeRunner::MoveToward(const Vec3& goal, float stop, float speed, bool run, float dt) {
    Scene* sc = SceneOf();
    if (!sc || !transform) return Move::Blocked;
    const Vec3 pos = Position();
    if (Dist2D(pos, goal) <= std::max(0.05f, stop)) { StopMoving(); return Move::Arrived; }
    if (m_agent && !m_npc) {
        const bool fresh = !m_cmdActive || Dist2D(goal, m_cmdGoal) > 0.3f;
        m_agent->speed = speed > 0.0f ? speed : m_agentSpeed0;
        if (fresh) {
            m_agent->SetDestination(goal);
            m_cmdGoal = goal; m_cmdActive = true; m_cmdTicks = 0;
            return Move::Moving;
        }
        ++m_cmdTicks;
        if (m_agent->Arrived()) { m_cmdActive = false; return Move::Arrived; }
        if (!m_agent->HasPath() && m_cmdTicks > 1) { m_cmdActive = false; return Move::Blocked; }
        if (m_agent->IsStopped()) m_agent->Resume();
        return Move::Moving;
    }
    if (m_npc) {
        const bool fresh = !m_cmdActive || Dist2D(goal, m_cmdGoal) > 0.5f;
        if (fresh) {
            if (Navigation::Mesh(*sc) && Navigation::FindPath(*sc, pos, goal, 0.4f, gameObject).empty()) { m_cmdActive = false; return Move::Blocked; }
            m_npc->CommandGoTo(goal, run ? m_npc->runSpeed : speed);
            m_cmdGoal = goal; m_cmdActive = true;
            return Move::Moving;
        }
        if (!m_npc->Commanded()) { m_cmdActive = false; return Move::Arrived; }
        return Move::Moving;
    }
    // The built-in mover: a path over the navigation, followed every frame by StepMover.
    (void)dt;
    const bool fresh = !m_moving || Dist2D(goal, m_pathGoal) > 0.5f || m_repath <= 0.0f || m_path.empty();
    if (fresh) {
        if (!Navigation::FindPath(*sc, pos, goal, m_path, 0.4f, gameObject)) { m_path.clear(); m_moving = false; return Move::Blocked; }
        m_pathIdx = 0; m_pathGoal = goal; m_repath = 0.6f;
    }
    m_moveGoal = goal;
    m_moveStop = std::max(0.05f, stop);
    m_moveSpeed = speed > 0.0f ? speed : (run ? 5.0f : 3.0f);
    m_moving = true;
    return Move::Moving;
}

void BehaviorTreeRunner::StopMoving() {
    if (m_agent && !m_npc) {
        if (m_cmdActive) m_agent->Stop();
        m_agent->speed = m_agentSpeed0 > 0.0f ? m_agentSpeed0 : m_agent->speed;
    }
    if (m_npc) m_npc->CancelCommand();
    m_cmdActive = false;
    if (m_moving) {
        m_moving = false;
        m_velocity = Vec3{0, 0, 0};
        if (auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr)
            if (rb->bodyType != Rigidbody3D::BodyType::Static) { rb->velocity.x = 0.0f; rb->velocity.z = 0.0f; }
    }
    m_path.clear(); m_pathIdx = 0;
}

void BehaviorTreeRunner::StepMover(float dt) {
    if (m_agent || m_npc || !transform) return;
    if (!m_moving) { if (m_velocity.x != 0.0f || m_velocity.z != 0.0f) { m_velocity = Vec3{0, 0, 0}; DriveBody(false, 0.0f, dt); } return; }
    m_repath -= dt;
    const Vec3 pos = Position();
    const float remaining = Dist2D(pos, m_moveGoal);
    if (remaining <= m_moveStop) { StopMoving(); DriveBody(false, 0.0f, dt); return; }
    // Corners are passed closely (a wide reach cuts them and clips the walls they hug).
    while (m_pathIdx + 1 < (int)m_path.size() && Dist2D(pos, m_path[(std::size_t)m_pathIdx]) < 0.12f) ++m_pathIdx;
    const Vec3 target = m_path.empty() ? m_moveGoal : m_path[(std::size_t)m_pathIdx];
    Vec3 dir = Dir2D(pos, target);
    if (dir.x == 0.0f && dir.z == 0.0f) dir = Dir2D(pos, m_moveGoal);
    float step = m_moveSpeed * dt;
    if (step > remaining) step = remaining;
    m_velocity = Vec3{dir.x * m_moveSpeed, 0.0f, dir.z * m_moveSpeed};
    auto* rb = gameObject->GetComponent<Rigidbody3D>();
    if (rb && rb->bodyType != Rigidbody3D::BodyType::Static) {
        rb->velocity.x = m_velocity.x; rb->velocity.z = m_velocity.z;
        rb->WakeUp();
    } else {
        transform->SetPosition(pos + Vec3{dir.x * step, 0.0f, dir.z * step});
    }
    FaceToward(dir, 360.0f, dt);
    DriveBody(true, m_moveSpeed, dt);
}

void BehaviorTreeRunner::DriveBody(bool moving, float speed, float dt) {
    Character* ch = Body();
    if (!ch) return;
    Character::Locomotion lo;
    lo.velocity = moving ? m_velocity : Vec3{0, 0, 0};
    lo.facing = transform->Rotation() * Vec3{0, 0, -1};
    lo.onGround = true;
    lo.walkSpeed = std::max(0.1f, moving ? speed : 3.0f);
    lo.runSpeed = lo.walkSpeed * 2.0f;
    lo.dt = dt;
    ch->DriveLocomotion(lo);
}

} // namespace okay
