// Every node in the registry is built in a graph and run in a scene that has the usual
// components. A node is proven when something observable differs from the same graph run
// without it: a transform, a component value, a variable, a log line, a runtime error that
// names the node (a clear "can't do that here"), or a value read into a variable.
// Nodes that cannot run headless (hosting, Steam, PlayFab) must still build; events are
// built and registered. A node that is none of these and not on the known list fails.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/BehaviorTreeRunner.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/LineRenderer.hpp"
#include "okay/Components/MusicPlayer.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Platform/Steam/Steam.hpp"
#include "okay/Inventory/InventoryEvents.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Input/Cursor.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Audio/AudioLoad.hpp"
#include "okay/Components/BlendShapes.hpp"
#include "okay/Components/DialogueRunner.hpp"
#include "okay/Components/Seat.hpp"
#include "okay/Components/Traversal.hpp"
#include "okay/Components/SequencePlayer.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include <algorithm>
#include <functional>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor::vsed;

static const char* kKnownUnproven[] = {
#include "vs_registry_known_gaps.inc"
};

// Engine-API bindings (Class.Method nodes) that do nothing a headless scene can see: they need a rumble
// motor, the input map, a settings file or a physics step with inertia. Everything else in the 1497 is
// proven above. This list is a ratchet: a new silent binding fails the test, and a proven one must leave.
static const char* kKnownSilentBindings[] = {
    "Rigidbody.AddTorque/1", "Input.Rumble/3", "Input.DefineAction/3", "Input.DefineDirection/3", "Audio.SaveSettings/0", "Fog.SetMode/2",
};

static std::vector<std::string> g_errs;
static int g_logLines = 0;
static void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e.nodeType); }

static std::string Str(const vs::VsValue& v) {
    char b[96];
    std::snprintf(b, sizeof b, "%.4f|%d|", (double)v.AsFloat(), (int)v.AsBool());
    return std::string(b) + v.AsString() + "|" + std::to_string((unsigned long long)v.AsObjectId());
}

// Process-wide state a node can change (the quit flag, pauses, the mixer): put back before every run so
// one run cannot leak into the next.
static void ResetStatics() {
    Game::CancelQuit();
    for (const std::string& r : Game::PauseReasons()) Game::ClearPause(r);
    AudioMixer::masterVolume = 1.0f; AudioMixer::muted = false;
    for (int i = 0; i < (int)AudioGroup::Count; ++i) { AudioMixer::groupVolume[i] = 1.0f; AudioMixer::groupMuted[i] = false; }
}

static std::string Snapshot(Scene& s) {
    std::string out;
    char b[512];
    for (const auto& up : s.Objects()) {
        if (!up) continue;
        GameObject* g = up.get();
        const Vec3 p = g->transform->Position(), e = g->transform->EulerAngles(), sc = g->transform->localScale;
        std::snprintf(b, sizeof b, "[%s|%s|%d|%.3f,%.3f,%.3f|%.2f,%.2f,%.2f|%.3f,%.3f,%.3f]", g->name.c_str(), std::string(g->tag).c_str(), (int)g->active,
                      p.x, p.y, p.z, e.x, e.y, e.z, sc.x, sc.y, sc.z);
        out += b;
        if (auto* h = g->GetComponent<HealthStat>()) { std::snprintf(b, sizeof b, "H%.2f,%.2f,%.2f,%d,%d", h->health, h->maxHealth, h->armor, (int)h->godMode, (int)h->dead); out += b; }
        if (auto* inv = g->GetComponent<Inventory>()) { for (const auto& sl : inv->slots) out += sl.item + "x" + std::to_string(sl.count) + ","; }
        if (auto* rb = g->GetComponent<Rigidbody3D>()) { std::snprintf(b, sizeof b, "R%.2f,%.2f,%.2f,%.2f,%.2f,w%.2f,%.2f,%.2f", rb->velocity.x, rb->velocity.y, rb->velocity.z, rb->mass, rb->gravityScale, rb->angularVelocity.x, rb->angularVelocity.y, rb->angularVelocity.z); out += b; }
        if (auto* t = g->GetComponent<TextRenderer>()) { std::snprintf(b, sizeof b, "T%s|%.2f,%.2f,%.2f,%.2f|%.2f", t->text.c_str(), t->color.r, t->color.g, t->color.b, t->color.a, t->pixelSize); out += b; }
        if (auto* a = g->GetComponent<AudioSource>()) { std::snprintf(b, sizeof b, "A%.2f,%.2f,%d,%d", a->volume, a->pitch, (int)a->loop, (int)a->IsPlaying()); out += b; }
        if (auto* c = g->GetComponent<Camera>()) { std::snprintf(b, sizeof b, "C%.2f,%.2f,%.2f,%.2f", c->zoom, c->fieldOfView, c->dutch, c->ShakeTrauma()); out += b; }
        if (auto* sr = g->GetComponent<SpriteRenderer>()) { std::snprintf(b, sizeof b, "S%.2f,%.2f,%.2f,%.2f,%d", sr->color.r, sr->color.g, sr->color.b, sr->color.a, (int)sr->enabled); out += b; }
        if (auto* mr = g->GetComponent<MeshRenderer>()) { std::snprintf(b, sizeof b, "M%.2f,%.2f,%.2f,%d", mr->color.r, mr->color.g, mr->color.b, (int)mr->enabled); out += b; }
        if (auto* q = g->GetComponent<QuestLog>()) for (const auto& id : q->Ids()) out += id + QuestStatusName(q->Status(id));
        if (auto* sm = g->GetComponent<AnimStateMachine>()) {
            std::snprintf(b, sizeof b, "AN%s", sm->Current().c_str()); out += b;
            for (const char* k : {"Other", "dead", "hitDir", "hitHeavy", "dodgeDir", "sitting", "sitType", "hanging", "divePending", "shimmy"}) {
                std::snprintf(b, sizeof b, ",%s=%.2f/%d", k, sm->GetFloat(k), (int)sm->GetBool(k)); out += b;
            }
        }
        if (TraverseBusy(g)) out += "TB";
        if (auto* tr = g->GetComponent<TrailRenderer>()) { std::snprintf(b, sizeof b, "TR%d", tr->PointCount()); out += b; }
        if (auto* bs = g->GetComponent<BlendShapes>()) { std::snprintf(b, sizeof b, "BS%.2f", bs->Weight("Other")); out += b; }
        if (auto* sp = g->GetComponent<SequencePlayer>()) { std::snprintf(b, sizeof b, "SQ%d", (int)sp->IsPlaying()); out += b; }
        if (auto* dr = g->GetComponent<DialogueRunner>()) { std::snprintf(b, sizeof b, "DR%d", (int)dr->IsActive()); out += b; }
        if (auto* st = g->GetComponent<Seat>()) { std::snprintf(b, sizeof b, "ST%d", (int)st->occupied); out += b; }
        if (auto* ch = g->GetComponent<Character>()) { std::snprintf(b, sizeof b, "CH%d", (int)ch->IsPlayingClip()); out += b; }
        if (auto* nv = g->GetComponent<NavMeshAgent>()) { const Vec3 d = nv->Destination(); std::snprintf(b, sizeof b, "NV%.2f,%.2f,%.2f,%d,%d", d.x, d.y, d.z, (int)nv->IsStopped(), (int)nv->Arrived()); out += b; }
        if (auto* bt = g->GetComponent<BehaviorTreeRunner>()) { for (const char* k : {"x", "Other", "1", "key"}) if (const BtValue* v = bt->blackboard.Find(k)) out += std::string("BT") + k + Str(v->ToVs()); }
        if (auto* ik = g->GetComponent<FullBodyIK>()) { std::snprintf(b, sizeof b, "IK%d,%.2f,%.2f,%.2f,%.2f", (int)ik->lookEnabled, ik->lookPosition.x, ik->lookPosition.y, ik->lookPosition.z, ik->lookWeight); out += b;
            for (int l : {(int)FullBodyIK::LeftHand, (int)FullBodyIK::RightHand}) { const auto& e = ik->effectors[l]; std::snprintf(b, sizeof b, "E%d,%.2f,%.2f,%.2f,%.2f", (int)e.enabled, e.position.x, e.position.y, e.position.z, e.positionWeight); out += b; } }
        if (auto* ps = g->GetComponent<ParticleSystem>()) { std::snprintf(b, sizeof b, "PS%d,%d", (int)ps->playing, ps->AliveCount()); out += b; }
        if (auto* lr = g->GetComponent<LineRenderer>()) { std::snprintf(b, sizeof b, "LR%d", lr->PositionCount()); out += b; }
        if (auto* mp = g->GetComponent<MusicPlayer>()) { std::snprintf(b, sizeof b, "MP%d,%d", (int)mp->IsPlaying(), mp->CurrentIndex()); out += b; }
        for (auto* vc : g->GetComponents<VisualScriptComponent>()) {
            std::vector<std::string> names;
            for (const auto& kv : vc->Variables()) names.push_back(kv.first + "=" + Str(kv.second));
            std::sort(names.begin(), names.end());
            for (const auto& n : names) out += n + ";";
        }
    }
    std::vector<std::string> sv;
    for (const auto& kv : vs::SceneVariables(&s)) sv.push_back(kv.first + "=" + Str(kv.second));
    for (const auto& kv : vs::GlobalVariables()) sv.push_back("g." + kv.first + "=" + Str(kv.second));
    std::sort(sv.begin(), sv.end());
    for (const auto& x : sv) out += x + ";";
    {
        const auto& rs = s.renderSettings;
        std::snprintf(b, sizeof b, "|rs %d,%.2f,%d,%.2f,%.2f,%.2f,%d|", (int)rs.weather.preset, rs.weather.intensity, (int)rs.fog, rs.fogStart, rs.fogEnd, rs.post.exposure, (int)rs.post.bloom);
        out += b;
        std::snprintf(b, sizeof b, "game %d,%d,%d,quit%d,load%d|", (int)Game::Paused(), (int)Cursor::IsLocked(), (int)Cursor::visible, (int)Game::QuitRequested(), (int)s.HasPendingLoad());
        out += b;
        out += "loaderr:" + s.LastLoadError() + "|";
        std::snprintf(b, sizeof b, "mix %.2f,%d,%.2f,%.2f,%.2f,%.2f|fog %d,%.2f,%.2f,%.2f|wind %.2f,%.2f,%.2f|", AudioMixer::masterVolume, (int)AudioMixer::muted, AudioMixer::groupVolume[0], AudioMixer::groupVolume[1],
                      AudioMixer::groupVolume[2], AudioMixer::groupVolume[3], (int)rs.fogMode, rs.fogColor.r, rs.fogColor.g, rs.fogColor.b, rs.weather.wind.x, rs.weather.wind.y, rs.weather.wind.z);
        out += b;
        for (const char* k : {"Other", "1", "score", "key", "vs_checkpoint_ok", "vs_checkpoint"}) if (Save::Has(k)) out += std::string("save.") + k + std::to_string(Save::GetFloat(k)) + Save::GetString(k) + ";";
    }
    std::snprintf(b, sizeof b, "|time %.3f %d|log %d", Time::BaseTimeScale(), (int)Time::HitStopActive(), g_logLines);
    return out + b;
}

static void Populate(Scene& s, GameObject*& me) {
    me = s.CreateGameObject("Me");
    me->AddComponent<HealthStat>();
    me->AddComponent<Inventory>();
    me->AddComponent<Rigidbody3D>();
    me->AddComponent<TextRenderer>();
    me->AddComponent<AudioSource>();
    me->AddComponent<Camera>();
    me->AddComponent<SpriteRenderer>();
    me->AddComponent<MeshRenderer>();
    GameObject* other = s.CreateGameObject("Other");
    other->tag = "tag";
    other->transform->SetPosition(Vec3(3, 0, 4));
    other->AddComponent<HealthStat>();
    other->AddComponent<MeshRenderer>();
    other->AddComponent<TextRenderer>();
    other->AddComponent<SpriteRenderer>();
    GameObject* qo = s.CreateGameObject("Quests");
    auto* log = qo->AddComponent<QuestLog>();
    log->drawUI = false; log->showNotifications = false;
    QuestDef qd; qd.id = "Other"; qd.title = "T";
    QuestObjective ob; ob.kind = QuestObjective::Kind::Custom; ob.count = 2; qd.objectives.push_back(ob);
    log->quests.push_back(qd);
}

// A 16-bit mono silence the music player can load (half a second).
static std::string MakeWav(const std::string& path) {
    std::vector<unsigned char> d(44 + 8000, 0);
    auto w32 = [&](int at, unsigned v) { for (int i = 0; i < 4; ++i) d[(std::size_t)(at + i)] = (unsigned char)((v >> (8 * i)) & 255); };
    auto w16 = [&](int at, unsigned v) { d[(std::size_t)at] = (unsigned char)(v & 255); d[(std::size_t)at + 1] = (unsigned char)((v >> 8) & 255); };
    std::memcpy(&d[0], "RIFF", 4); w32(4, 36 + 8000); std::memcpy(&d[8], "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1);
    w32(24, 8000); w32(28, 16000); w16(32, 2); w16(34, 16); std::memcpy(&d[36], "data", 4); w32(40, 8000);
    if (FILE* f = std::fopen(path.c_str(), "wb")) { std::fwrite(d.data(), 1, d.size(), f); std::fclose(f); }
    return path;
}
static std::string g_wav;

// The scene a node needs when it drives one specific component: an Animator, a nav agent, a behaviour
// tree, Full Body IK, a particle system, a line, a music player. variant 1 starts the components in the
// opposite state (particles stopped, music playing) so a node that stops what runs shows a change too.
static void RichPopulate(Scene& s, GameObject*& me, int variant) {
    Populate(s, me);
    auto* sm = me->AddComponent<AnimStateMachine>();
    AnimStateMachine::State a; a.name = "A"; a.clip = "a";
    AnimStateMachine::Transition t; t.to = "B"; t.cond = AnimStateMachine::Cond::Trigger; t.param = "Other"; t.blend = 0.0f;
    a.transitions.push_back(t);
    AnimStateMachine::State b; b.name = "B"; b.clip = "b";
    sm->states.push_back(a); sm->states.push_back(b); sm->entry = "A";
    for (const char* n : {"Roll", "Dodge", "Dive", "SitDown", "StandUp", "HitHeavy", "Hit_Crouch", "ClimbUp", "LetGo", "Vault", "MantleLow", "MantleHigh", "LedgeGrab", "Hang", "HangShimmy"}) {
        AnimStateMachine::State st; st.name = n; st.clip = n;
        sm->states.push_back(st);
    }
    me->AddComponent<NavMeshAgent>();
    me->AddComponent<BehaviorTreeRunner>();
    me->AddComponent<FullBodyIK>();
    auto* ps = me->AddComponent<ParticleSystem>();
    ps->playing = variant == 0;
    me->AddComponent<LineRenderer>();
    GameObject* mo = s.CreateGameObject("Music");
    auto* mp = mo->AddComponent<MusicPlayer>();
    mp->tracks = {g_wav, g_wav};
    mp->playOnStart = variant == 1;
}

// Nodes that need a network, Steam or PlayFab. They must build; running them headless is not safe.
static bool NeedsOnline(const VsDef& d) {
    const std::string t = d.type;
    return d.cat == CAT_NET || t.rfind("Net", 0) == 0 || t.rfind("Steam", 0) == 0 || t.rfind("Pf", 0) == 0 || t.rfind("PlayFab", 0) == 0 ||
           t.rfind("Fn.net_", 0) == 0 || t.rfind("Fn.steam", 0) == 0 || t.rfind("Fn.pf_", 0) == 0 || t.rfind("Fn.Steam", 0) == 0;
}
// Proven by their own suites: the gameplay library (scene with the node against one without,
// and value nodes against an empty scene) and, for the pure value nodes of the logic sets, test_vs_node_effect.
static bool ProvenElsewhere(const std::string& t) {
    return (t.size() > 2 && t.compare(0, 2, "Gl") == 0 && t[2] >= 'A' && t[2] <= 'Z');
}
// What to type into a node's text field: a number when its hint asks for one, else an object name.
static std::string ArgFor(const VsDef& d, bool alt) {
    if (std::string(d.type) == "Compare") return ">";
    const std::string h = d.paramHint ? d.paramHint : "";
    const bool numeric = h.find("second") != std::string::npos || h.find("number") != std::string::npos || h.find("times") != std::string::npos ||
                         h.find("count") != std::string::npos || (!h.empty() && std::isdigit((unsigned char)h[0]));
    if (numeric) return alt ? "0" : "1";
    return "Other";
}
// Online nodes that would open a socket, log in or reach a service when run: built, never run here.
static bool OnlineWouldConnect(const std::string& t) {
    std::string l;
    for (char c : t) l += (char)std::tolower((unsigned char)c);
    for (const char* k : {"host", "join", "login", "register", "relay", "invite", "queue_auto", "queue_accept", "browser_open", "browser_refresh", "lobby_create", "start_match", "startmatch",
                          "steam_overlay", "launch", "pf_save", "pf_load", "pf_delete", "pf_set_stat", "pf_leaderboard", "leaderboard", "cloud_write", "cloud_read", "cloud_delete"})
        if (l.find(k) != std::string::npos) return true;
    return false;
}
static bool IsBinding(const std::string& t) { return t.rfind("Fn.", 0) == 0 || t.find('.') != std::string::npos; }
static bool Dangerous(const std::string& t) {
    for (const char* k : {"Quit", "Exit", "LoadScene", "RestartScene", "Fn.quit", "Fn.load_scene", "Fn.exit"})
        if (t.find(k) != std::string::npos) return true;
    return false;
}

// ---- Targeted scenarios ----------------------------------------------------------------------
// A node the generic scene cannot prove (it needs a hurt body, a seat, a wall, a quest, a playing sound...)
// gets a scenario here: the setup it needs, and where the text of its graph differs from the plain one.
// The graph with the node is compared with the same graph without it, in the same setup.
struct Fix {
    const char* type;
    const char* arg = nullptr;       // the node's text field (default: ArgFor)
    const char* pinN = nullptr;      // what number pins get (default 1)
    const char* pinT = nullptr;      // what text pins get (default "Other")
    bool wireTarget = false;         // object pins are wired to the object named Other
    int variant = 0;                 // RichPopulate variant
    std::function<void(Scene&, GameObject*)> setup;
    const char* graph = nullptr;     // the whole graph with the node, when the plain shape does not fit
    const char* base = nullptr;      // ... and without it
};

static GameObject* Find(Scene& s, const char* n) { return s.Find(n); }
static void Geo(Scene& s, GameObject* me, float wallH, float meY) {
    if (auto* rb = me->GetComponent<Rigidbody3D>()) rb->gravityScale = 0.0f;
    me->transform->SetPosition(Vec3(0, meY, 0));
    GameObject* ground = s.CreateGameObject("Ground");
    ground->transform->SetPosition(Vec3(0, -0.5f, 0));
    ground->AddComponent<BoxCollider3D>()->size = Vec3(30, 1, 30);
    GameObject* wall = s.CreateGameObject("Wall");
    wall->transform->SetPosition(Vec3(0, wallH * 0.5f, -1.4f));
    wall->AddComponent<BoxCollider3D>()->size = Vec3(4, wallH, 2.0f);
}

static std::vector<Fix> BuildFixes() {
    std::vector<Fix> f;
    auto add = [&](Fix x) { f.push_back(std::move(x)); };
    // What a composite or flow node does is to run what hangs off its flow outputs; the plain graph has no such child.
    const char* kKid = "node 5 AddVar kid\ndefault 5 0 1\nexec 2 0 5\n";
    const std::string head = "node 1 OnUpdate\nentry OnUpdate 1\nexec 1 0 2\n";
    static std::string compositeGraph, passGraph;
    compositeGraph = head + "node 2 @T@\n" + kKid;
    passGraph = std::string("node 1 OnUpdate\nentry OnUpdate 1\n");
    { Fix x{"AnimTrigger"}; x.setup = [](Scene&, GameObject* me) { me->AddComponent<Character>(); }; add(x); }
    { Fix x{"SetPostEffect"}; x.arg = "exposure"; x.pinN = "2"; add(x); }
    { Fix x{"IkHand"}; add(x); }
    {
        Fix x{"SetLinePoints"};
        x.graph = "node 1 OnUpdate\nentry OnUpdate 1\nnode 20 AllWithTag tag\nnode 2 SetLinePoints\ndata 2 0 20 0\nexec 1 0 2\n";
        add(x);
    }
    { Fix x{"ClearTrail"}; x.setup = [](Scene&, GameObject* me) { me->AddComponent<TrailRenderer>(); if (auto* rb = me->GetComponent<Rigidbody3D>()) rb->velocity = Vec3(0, 0, -6); }; add(x); }
    { Fix x{"MusicResume"}; x.setup = [](Scene& s, GameObject*) { if (auto* mp = Find(s, "Music")->GetComponent<MusicPlayer>()) { mp->Play(0); mp->Pause(); } }; add(x); }
    { Fix x{"StopAnimation"}; x.setup = [](Scene&, GameObject* me) { auto* c = me->AddComponent<Character>(); AnimClip k; k.name = "wave"; k.loop = true; c->AddClip(k); c->PlayClip("wave"); }; add(x); }
    {
        Fix x{"Heal"};
        x.graph = "node 1 OnUpdate\nentry OnUpdate 1\nnode 3 Damage\ndefault 3 0 10\nexec 1 0 3\nnode 2 Heal\ndefault 2 0 20\nexec 3 0 2\n";
        x.base = "node 1 OnUpdate\nentry OnUpdate 1\nnode 3 Damage\ndefault 3 0 10\nexec 1 0 3\n";
        add(x);
    }
    { Fix x{"PlayAction"}; x.arg = "Roll"; x.setup = [](Scene&, GameObject* me) { me->AddComponent<Character>(); }; add(x); }
    { Fix x{"Respawn"}; x.wireTarget = true; x.setup = [](Scene& s, GameObject*) { Find(s, "Other")->GetComponent<HealthStat>()->Damage(100000.0f); }; add(x); }
    { Fix x{"Revive"}; x.setup = [](Scene&, GameObject* me) { me->GetComponent<AnimStateMachine>()->SetBool("dead", true); }; add(x); }
    { Fix x{"CheckpointSave"}; add(x); }
    { Fix x{"StopSound"}; x.setup = [](Scene&, GameObject* me) { auto* au = me->GetComponent<AudioSource>(); if (auto c = LoadAudioFile(g_wav)) au->clip = *c; au->loop = true; au->Play(); }; add(x); }
    for (const char* t : {"AllSucceed", "FlipOutcome", "AlwaysSucceed", "RepeatUntilFail", "LbRunAndCheck", "LbTrySeveral"}) {
        Fix x{t}; x.graph = nullptr; add(x);
    }
    {
        Fix x{"RememberValue"};
        x.graph = "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 RememberValue\ndefault 2 0 \"k\"\ndefault 2 1 5\nexec 1 0 2\nnode 3 RecallValue\ndefault 3 0 \"k\"\nnode 4 SetVar out\ndata 4 0 3 0\nexec 2 0 4\n";
        x.base = "node 1 OnUpdate\nentry OnUpdate 1\nnode 3 RecallValue\ndefault 3 0 \"k\"\nnode 4 SetVar out\ndata 4 0 3 0\nexec 1 0 4\n";
        add(x);
    }
    { Fix x{"LbPassAfter"}; x.pinN = "3"; add(x); }
    { Fix x{"LbEveryCalls"}; x.pinN = "3"; add(x); }
    { Fix x{"EveryN"}; x.pinN = "3"; add(x); }
    { Fix x{"SwitchCases"}; x.arg = "1,2"; x.pinN = "2"; add(x); }
    { Fix x{"Dive"}; x.setup = [](Scene&, GameObject* me) { auto* sm = me->GetComponent<AnimStateMachine>(); sm->SetBool("grounded", true); sm->SetBool("sprint", true); }; add(x); }
    { Fix x{"Sit"}; x.setup = [](Scene& s, GameObject*) { Find(s, "Other")->AddComponent<Seat>(); }; add(x); }
    { Fix x{"StandUp"}; x.setup = [](Scene&, GameObject* me) { me->GetComponent<AnimStateMachine>()->SetBool("sitting", true); }; add(x); }
    { Fix x{"Interact"}; x.setup = [](Scene& s, GameObject* me) { auto* o = Find(s, "Other"); o->transform->SetPosition(Vec3(0.3f, 0, 0.3f)); o->AddComponent<Seat>(); (void)me; }; add(x); }
    { Fix x{"HitReact"}; x.pinN = "1"; add(x); }
    { Fix x{"Roll"}; x.arg = "left"; add(x); }
    { Fix x{"Dodge"}; x.arg = "left"; add(x); }
    { Fix x{"Vault"}; x.setup = [](Scene& s, GameObject* me) { Geo(s, me, 0.9f, 0.0f); }; add(x); }
    { Fix x{"Mantle"}; x.setup = [](Scene& s, GameObject* me) { Geo(s, me, 0.5f, 0.0f); }; add(x); }
    { Fix x{"LedgeGrab"}; x.setup = [](Scene& s, GameObject* me) { Geo(s, me, 3.5f, 2.0f); }; add(x); }
    { Fix x{"ClimbUp"}; x.setup = [](Scene&, GameObject* me) { me->GetComponent<AnimStateMachine>()->SetBool("hanging", true); }; add(x); }
    { Fix x{"LetGo"}; x.setup = [](Scene&, GameObject* me) { me->GetComponent<AnimStateMachine>()->SetBool("hanging", true); }; add(x); }
    { Fix x{"Shimmy"}; add(x); }
    {
        Fix x{"SetBlendShape"};
        x.setup = [](Scene&, GameObject* me) { auto* bs = me->AddComponent<BlendShapes>(); bs->AddShape("Other", {Vec3(1, 0, 0), Vec3(0, 1, 0)}); };
        add(x);
    }
    { Fix x{"Action"}; x.arg = "log hello"; add(x); }
    { Fix x{"Actions"}; x.arg = "log one ; log two"; add(x); }
    { Fix x{"Condition"}; x.arg = "var_gt zz 5"; add(x); }
    {
        Fix x{"StopSequence"};
        x.setup = [](Scene& s, GameObject* me) {
            auto* sp = me->AddComponent<SequencePlayer>();
            Sequence q; q.length = 100.0f; sp->SetSequence(q);
            sp->Play();
            (void)s;
        };
        add(x);
    }
    {
        Fix x{"Sequence.Stop/0"};
        x.setup = [](Scene&, GameObject* me) { auto* sp = me->AddComponent<SequencePlayer>(); Sequence q; q.length = 100.0f; sp->SetSequence(q); sp->Play(); };
        add(x);
    }
    {
        Fix x{"Dialogue.Stop/0"};
        x.setup = [](Scene& s, GameObject* me) {
            (void)s;
            auto* dr = me->AddComponent<DialogueRunner>();
            DialogueGraph g; g.name = "T"; g.speakers = {"A"};
            auto& l = g.Add(DialogueNode::Kind::Line, 0, 0); l.speaker = "A"; l.text = "Hi"; l.next = 1;
            g.Add(DialogueNode::Kind::End, 200, 0); g.start = 0;
            dr->SetGraph(g);
            dr->StartDialogue();
        };
        add(x);
    }
    { Fix x{"Fog.SetMode/2"}; x.pinN = "2"; add(x); }
    { Fix x{"Rigidbody.AddTorque/1"}; x.pinN = "5"; add(x); }
    {
        Fix x{"StartDialogue"};
        x.setup = [](Scene& s, GameObject*) {
            auto* dr = Find(s, "Other")->AddComponent<DialogueRunner>();
            DialogueGraph g; g.name = "T"; g.speakers = {"A"};
            auto& l = g.Add(DialogueNode::Kind::Line, 0, 0); l.speaker = "A"; l.text = "Hi"; l.next = 1;
            g.Add(DialogueNode::Kind::End, 200, 0); g.start = 0;
            dr->SetGraph(g);
        };
        add(x);
    }
    { Fix x{"CompleteObjective"}; x.pinN = "0"; x.setup = [](Scene&, GameObject*) {}; add(x); }
    { Fix x{"ShowUI"}; x.setup = [](Scene& s, GameObject*) { Find(s, "Other")->active = false; }; add(x); }
    { Fix x{"AddGlobalVar"}; add(x); }
    { Fix x{"GetComponent"}; x.pinT = "HealthStat"; add(x); }
    { Fix x{"SetVisible"}; add(x); }
    {
        Fix x{"SteamSetStat"};
        x.graph = "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 SteamSetStat Other\ndefault 2 0 7\nexec 1 0 2\nnode 3 SteamGetStat Other\nnode 4 SetVar out\ndata 4 0 3 0\nexec 2 0 4\n";
        x.base = "node 1 OnUpdate\nentry OnUpdate 1\nnode 3 SteamGetStat Other\nnode 4 SetVar out\ndata 4 0 3 0\nexec 1 0 4\n";
        add(x);
    }
    return f;
}

// The graph with the node (g) and the one without it (base) for a targeted scenario.
static void FixGraphs(const VsDef& d, const vs::NodeSignature* sig, const Fix& fx, std::string& g, std::string& base) {
    const std::string type = d.type;
    std::string line = "node 2 " + type;
    std::string arg = fx.arg ? fx.arg : (d.param ? ArgFor(d, false) : std::string());
    if (!arg.empty()) line += " " + arg;
    const std::string head = "node 1 OnUpdate\nentry OnUpdate 1\n";
    if (fx.graph) {
        g = fx.graph;
        base = fx.base ? fx.base : head;
        return;
    }
    // Composite nodes: what they do is run the child on each flow output; only the graph with the node has a child.
    const bool composite = type == "AllSucceed" || type == "FlipOutcome" || type == "AlwaysSucceed" || type == "RepeatUntilFail" || type == "LbRunAndCheck" || type == "LbTrySeveral";
    std::string pins;
    for (int i = 0; i < (int)sig->ins.size(); ++i) {
        const char c = sig->ins[(std::size_t)i];
        const std::string at = "default 2 " + std::to_string(i) + " ";
        if (c == 'N') pins += at + (fx.pinN ? fx.pinN : "1") + "\n";
        else if (c == 'B') pins += at + "true\n";
        else if (c == 'T') pins += at + "\"" + (fx.pinT ? fx.pinT : "Other") + "\"\n";
        else if (c == 'A') pins += at + "1\n";
        else if (c == 'V') {
            const std::string id = std::to_string(20 + i);
            pins += "node " + id + " MakeVec3\ndefault " + id + " 0 1\ndefault " + id + " 1 2\ndefault " + id + " 2 3\ndata 2 " + std::to_string(i) + " " + id + " 0\n";
        } else if (c == 'O' && fx.wireTarget) {
            const std::string id = std::to_string(30 + i);
            pins += "node " + id + " FindObject Other\ndata 2 " + std::to_string(i) + " " + id + " 0\n";
        }
    }
    g = head + line + "\n" + pins + "exec 1 0 2\n";
    base = head;
    if (composite) {
        g += "node 5 AddVar kid\ndefault 5 0 1\nexec 2 0 5\n";
        return;
    }
    const int outs = std::max(1, (int)d.execOut.size());
    for (int k = 0; k < outs && k < 8; ++k)
        g += "node " + std::to_string(10 + k) + " AddVar flow" + std::to_string(k) + "\ndefault " + std::to_string(10 + k) + " 0 1\nexec 2 " + std::to_string(k) + " " + std::to_string(10 + k) + "\n";
    base += "node 10 AddVar flow0\ndefault 10 0 1\nexec 1 0 10\n";
    if (!sig->outs.empty()) {
        g += "node 4 SetVar out\ndata 4 0 2 0\nexec 10 0 4\n";
        base += "node 4 SetVar out\ndefault 4 0 -9999\nexec 10 0 4\n";
    }
}

// Run the graph in the scenario's scene and return what can be seen of it.
static std::string RunScenario(const Fix& fx, const std::string& text, int* errCount) {
    Time::SetTimeScale(1.0f); Time::CancelHitStop();
    ResetStatics();
    Steam::Shutdown();
    vs::GlobalVariables().clear();
    Save::Clear();
    TraverseJobs().clear(); HangPoints().clear(); HangGravity().clear();   // keyed by object address: a scene that ended mid-move leaves stale entries
    Scene s; s.physicsEnabled = true;
    GameObject* me; RichPopulate(s, me, fx.variant);
    if (fx.setup) fx.setup(s, me);
    if (std::string(fx.type) == "CompleteObjective") if (auto* q = QuestLog::Get(&s, false)) q->StartQuest("Other");
    auto* vc = me->AddComponent<VisualScriptComponent>();
    std::string err;
    vc->LoadFromText(text, &err);
    g_errs.clear(); g_logLines = 0;
    s.Start();
    for (int f = 0; f < 8; ++f) s.Update(0.25f);
    if (errCount) *errCount = (int)g_errs.size();
    if (std::getenv("OKAY_DEBUG_PROBE") && std::string(fx.type) == "Vault") {
        const RaycastHit3D h = s.physics3D().Raycast(s, Vec3(0, 0.4f, 0), Vec3(0, 0, -1), 0.85f, me, Physics3D::kAllLayers, false);
        const TraverseProbe p = ProbeTraverse(me, Vec3(0, 0, -1), false);
        std::printf("  probe: hit=%d kind=%d height=%.2f mePos=%.2f\n", (int)h.hit, (int)p.kind, p.height, me->transform->Position().y);
    }
    const std::string out = Snapshot(s);
    Time::SetTimeScale(1.0f); Time::CancelHitStop();
    return out;
}


// ---- Event nodes: each one fires from the thing that raises it -------------------------------
// The graph is  <event> -> Set Global Variable "fired". "how" says what raised it:
//   real   - the engine did it (a frame, a key, an inventory change, a quest, a conversation, a sequence)
//   engine - the engine's own callback on the script component (what physics, the mouse picker and the
//            animator call), invoked directly because headless there is no pointer or contact to make one
//   bus    - the engine-side delivery call (InventoryEvents::Fire / RaiseEvent) its subsystem uses
struct Ev {
    const char* type; const char* arg; const char* how; const char* host;
    std::function<void(Scene&, GameObject*, VisualScriptComponent*)> trigger;
};
static std::vector<Ev> BuildEvents() {
    using VC = VisualScriptComponent;
    std::vector<Ev> e;
    auto frames = [](Scene& s, int n, float dt) { for (int i = 0; i < n; ++i) s.Update(dt); };
    auto raise = [](const char* type, const char* arg) {
        return [=](Scene& s, GameObject*, VC* vc) { vc->RaiseEvent(VsEntryName(type, arg).c_str(), s.Find("Other"), "n", vs::VsValue(1.0f)); };
    };
    e.push_back({"OnStart", "", "real", "Me", [](Scene&, GameObject*, VC*) {}});
    for (const char* t : {"OnUpdate", "OnLateUpdate", "OnFixedUpdate"}) e.push_back({t, "", "real", "Me", [=](Scene& s, GameObject*, VC*) { for (int i = 0; i < 3; ++i) s.Update(0.1f); }});
    e.push_back({"OnEnable", "", "real", "Me", [=](Scene& s, GameObject* me, VC*) { me->active = false; s.Update(0.1f); me->active = true; s.Update(0.1f); }});
    e.push_back({"OnDisable", "", "real", "Me", [=](Scene& s, GameObject* me, VC*) { me->active = false; s.Update(0.1f); }});
    e.push_back({"OnDestroy", "", "real", "Me", [=](Scene& s, GameObject* me, VC*) { s.Destroy(me); s.Update(0.1f); }});
    e.push_back({"OnTimer", "0.5", "real", "Me", [=](Scene& s, GameObject*, VC*) { for (int i = 0; i < 6; ++i) s.Update(0.25f); }});
    e.push_back({"OnKey", "space", "real", "Me", [=](Scene& s, GameObject*, VC*) { Input::FeedKeys({' '}); s.Update(0.1f); Input::FeedKeys({}); s.Update(0.1f); }});
    e.push_back({"OnKeyUp", "space", "real", "Me", [=](Scene& s, GameObject*, VC*) { Input::FeedKeys({' '}); s.Update(0.1f); Input::FeedKeys({}); s.Update(0.1f); }});
    e.push_back({"OnCollision", "", "engine", "Me", [](Scene& s, GameObject*, VC* vc) { Collision3D c; c.gameObject = s.Find("Other"); vc->OnCollisionEnter3D(c); }});
    e.push_back({"OnCollisionStay", "", "engine", "Me", [](Scene& s, GameObject*, VC* vc) { Collision3D c; c.gameObject = s.Find("Other"); vc->OnCollisionStay3D(c); }});
    e.push_back({"OnCollisionExit", "", "engine", "Me", [](Scene& s, GameObject*, VC* vc) { Collision3D c; c.gameObject = s.Find("Other"); vc->OnCollisionExit3D(c); }});
    e.push_back({"OnTrigger", "", "engine", "Me", [](Scene& s, GameObject*, VC* vc) { vc->OnTriggerEnter3D(s.Find("Other")->GetComponent<Collider3D>()); }});
    e.push_back({"OnTriggerStay", "", "engine", "Me", [](Scene& s, GameObject*, VC* vc) { vc->OnTriggerStay3D(s.Find("Other")->GetComponent<Collider3D>()); }});
    e.push_back({"OnTriggerExit", "", "engine", "Me", [](Scene& s, GameObject*, VC* vc) { vc->OnTriggerExit3D(s.Find("Other")->GetComponent<Collider3D>()); }});
    e.push_back({"OnClick", "", "engine", "Me", [](Scene&, GameObject*, VC* vc) { vc->OnMouseDown(); }});
    e.push_back({"OnMouseDown", "", "engine", "Me", [](Scene&, GameObject*, VC* vc) { vc->OnMouseDown(); }});
    e.push_back({"OnMouseUp", "", "engine", "Me", [](Scene&, GameObject*, VC* vc) { vc->OnMouseUp(); }});
    e.push_back({"OnMouseOver", "", "engine", "Me", [](Scene&, GameObject*, VC* vc) { vc->OnMouseOver(); }});
    e.push_back({"OnMouseEnter", "", "engine", "Me", [](Scene&, GameObject*, VC* vc) { vc->OnMouseEnter(); }});
    e.push_back({"OnMouseExit", "", "engine", "Me", [](Scene&, GameObject*, VC* vc) { vc->OnMouseExit(); }});
    e.push_back({"OnAnimEvent", "", "engine", "Me", [](Scene&, GameObject*, VC* vc) { vc->OnAnimationEvent("step"); }});
    e.push_back({"OnItemAdded", "", "real", "Me", [](Scene&, GameObject* me, VC*) { me->GetComponent<Inventory>()->Add("apple", 1); }});
    e.push_back({"OnItemRemoved", "", "real", "Me", [](Scene&, GameObject* me, VC*) { auto* i = me->GetComponent<Inventory>(); i->Add("apple", 1); i->Remove("apple", 1); }});
    e.push_back({"OnInventoryFull", "", "real", "Me", [](Scene&, GameObject* me, VC*) { auto* i = me->GetComponent<Inventory>(); i->capacity = 1; i->Add("apple", 1); i->Add("pear", 1); }});
    e.push_back({"OnItemUsed", "", "bus", "Me", [](Scene&, GameObject* me, VC*) { InventoryEvents::Fire(me, "on_item_used", "OnItemUsed", {vs::VsValue(std::string("apple"))}); }});
    e.push_back({"OnEquip", "", "bus", "Me", [](Scene&, GameObject* me, VC*) { InventoryEvents::Fire(me, "on_equip", "OnEquip", {vs::VsValue(std::string("hand")), vs::VsValue(std::string("sword"))}); }});
    e.push_back({"OnCraft", "", "bus", "Me", [](Scene&, GameObject* me, VC*) { InventoryEvents::Fire(me, "on_craft", "OnCraft", {vs::VsValue(std::string("plank"))}); }});
    e.push_back({"OnQuestStarted", "", "real", "Quests", [](Scene& s, GameObject*, VC*) { QuestLog::Get(&s, false)->StartQuest("Other"); }});
    e.push_back({"OnQuestCompleted", "", "real", "Quests", [](Scene& s, GameObject*, VC*) { auto* q = QuestLog::Get(&s, false); q->StartQuest("Other"); q->CompleteQuest("Other"); }});
    e.push_back({"OnQuestFailed", "", "real", "Quests", [](Scene& s, GameObject*, VC*) { auto* q = QuestLog::Get(&s, false); q->StartQuest("Other"); q->FailQuest("Other"); }});
    e.push_back({"OnObjectiveCompleted", "", "real", "Quests", [](Scene& s, GameObject*, VC*) { auto* q = QuestLog::Get(&s, false); q->StartQuest("Other"); q->CompleteObjective("Other", 0); }});
    e.push_back({"OnQuestEvent", "", "bus", "Quests", [](Scene&, GameObject* me, VC*) { InventoryEvents::Fire(me, "on_quest_event", "OnQuestEvent", {vs::VsValue(std::string("boss"))}); }});
    e.push_back({"OnDialogueEnd", "", "real", "Other", [](Scene& s, GameObject*, VC*) {
        auto* dr = s.Find("Other")->GetComponent<DialogueRunner>();
        dr->StartDialogue();
        for (int i = 0; i < 8 && dr->IsActive(); ++i) dr->Advance();
    }});
    e.push_back({"OnDialogueEvent", "", "bus", "Other", [](Scene&, GameObject* me, VC*) { InventoryEvents::Fire(me, "on_dialogue_event", "OnDialogueEvent", {vs::VsValue(std::string("beat"))}); }});
    e.push_back({"OnSequenceEvent", "", "real", "Me", [](Scene& s, GameObject* me, VC*) {
        auto* sp = me->AddComponent<SequencePlayer>();
        Sequence q; q.length = 2.0f;
        SeqEventTrack tr; SeqEvent ev; ev.time = 0.1f; ev.kind = SeqEvent::Kind::Named; ev.name = "go"; tr.events.push_back(ev); q.events.push_back(tr);
        sp->SetSequence(q);
        sp->Play();
        for (int i = 0; i < 6; ++i) s.Update(0.1f);
    }});
    for (const char* t : {"OnObjectPlaced", "OnObjectRemoved", "OnLogicSignal", "OnCheatDetected", "OnValidateRpc", "OnValidateHit", "OnLanguageChanged"})
        e.push_back({t, "", "bus", "Me", raise(t, "")});
    e.push_back({"OnNetRpc", "ping", "bus", "Me", raise("OnNetRpc", "ping")});
    return e;
}

static void CheckEvents() {
    const std::vector<Ev> events = BuildEvents();
    std::set<std::string> seen;
    int real = 0, engine = 0, bus = 0;
    for (const Ev& ev : events) {
        seen.insert(ev.type);
        const std::string entry = VsEntryName(ev.type, ev.arg);
        std::string g = std::string("node 1 ") + ev.type + (*ev.arg ? std::string(" ") + ev.arg : std::string()) + "\nentry " + entry + " 1\n" +
                        "node 3 SetGlobalVar fired\ndefault 3 0 1\nexec 1 0 3\n";
        int fires[2] = {0, 0};
        for (int pass = 0; pass < 2; ++pass) {   // pass 0: the trigger is pulled; pass 1: it is not
            vs::GlobalVariables().clear();
            ResetStatics();
            Scene s; s.physicsEnabled = false;
            GameObject* me; Populate(s, me);
            if (auto* o = s.Find("Other")) o->AddComponent<BoxCollider3D>();
            if (std::string(ev.type) == "OnDialogueEnd" || std::string(ev.type) == "OnDialogueEvent") {
                auto* dr = s.Find("Other")->AddComponent<DialogueRunner>();
                DialogueGraph dg; dg.name = "T"; dg.speakers = {"A"};
                auto& l = dg.Add(DialogueNode::Kind::Line, 0, 0); l.speaker = "A"; l.text = "Hi"; l.next = 1;
                dg.Add(DialogueNode::Kind::End, 200, 0); dg.start = 0;
                dr->SetGraph(dg);
            }
            GameObject* host = s.Find(ev.host);
            if (!host) host = me;
            auto* vc = host->AddComponent<VisualScriptComponent>();
            std::string err;
            CHECK(vc->LoadFromText(g, &err));
            Input::FeedKeys({});
            s.Start();
            const bool atStart = vs::GlobalVariables().count("fired") != 0;
            if (pass == 0) ev.trigger(s, host, vc);
            fires[pass] = vs::GlobalVariables().count("fired") ? 1 : 0;
            if (pass == 1 && atStart) fires[pass] = 0;   // OnStart fires on its own at Start; that is its trigger
            Input::FeedKeys({});
        }
        const bool selfFiring = std::string(ev.type) == "OnStart";
        const bool ok = fires[0] == 1 && (selfFiring || fires[1] == 0);
        if (!ok) std::printf("  event %s (%s): fired with its trigger = %d, without = %d\n", ev.type, ev.how, fires[0], fires[1]);
        CHECK(ok);
        if (std::string(ev.how) == "real") ++real; else if (std::string(ev.how) == "engine") ++engine; else ++bus;
    }
    std::printf("  events: %d dispatched (%d from a real trigger, %d from the engine callback the system itself calls, %d through the delivery call the subsystem uses)\n",
                (int)events.size(), real, engine, bus);
    // Every registered event node must be in the table (FunctionStart is a function's entry, run by Call nodes in test_vs_flow_behaviour).
    static const std::set<std::string> covered = {"OnInteract", "OnTagEnter", "OnTagStay", "OnTagExit", "OnDeath", "OnHealthChanged", "OnAction", "OnActionUp", "OnActionHeld", "OnCollisionTag", "FunctionStart"};
    for (const VsDef& d : Registry()) {
        const vs::NodeSignature* sig = vs::SignatureFor(d.type);
        if (!sig || sig->execIn || !sig->outs.empty() || d.execOut.empty()) continue;
        const std::string t = d.type;
        if (t.rfind("On", 0) != 0 && t != "FunctionStart") continue;
        if (!seen.count(t) && !covered.count(t)) { std::printf("  event node with no dispatch test: %s\n", d.type); CHECK(false); }
    }
}

int main() {
    RUN_SUITE("vs_registry_effect");
    g_wav = MakeWav("registry_effect_silence.wav");
    auto oldSink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) { ++g_logLines; };
    vs::SetRuntimeErrorHook(&OnErr);
    const std::vector<Fix> fixes = BuildFixes();
    int scenarioProven = 0;
    std::set<std::string> known(std::begin(kKnownUnproven), std::end(kKnownUnproven));
    known.erase("");
    std::vector<std::string> onlineSilent, bindingSilent;
    int onlineRun = 0, onlineProven = 0, dangerousRun = 0;
    std::vector<std::string> unproven, brokenBuild, onlineBuilt, eventsBuilt, skipped;
    int proven = 0, total = 0, reported = 0, elsewhere = 0, bindingsRun = 0, bindingsProven = 0;
    const bool printGaps = std::getenv("OKAY_PRINT_GAPS") != nullptr;
    for (const VsDef& d : Registry()) {
        const std::string type = d.type;
        const vs::NodeSignature* sig = vs::SignatureFor(type);
        ++total;
        if (!sig) { brokenBuild.push_back(type + " (no pin table)"); continue; }
        const bool isEvent = !sig->execIn && sig->outs.empty() && d.execOut.size() > 0;
        std::string line = "node 2 " + type;
        if (d.param) line += " " + ArgFor(d, false);
        std::string g = "node 1 OnUpdate\nentry OnUpdate 1\n";
        std::string pins, pinsAlt;
        for (int i = 0; i < (int)sig->ins.size(); ++i) {
            const char c = sig->ins[(std::size_t)i];
            const std::string at = "default 2 " + std::to_string(i) + " ";
            if (c == 'N') { pins += at + "1\n"; pinsAlt += at + "0\n"; }
            else if (c == 'B') { pins += at + "true\n"; pinsAlt += at + "false\n"; }
            else if (c == 'T') { pins += at + "\"Other\"\n"; pinsAlt += at + "\"\"\n"; }
            else if (c == 'A') { pins += at + "1\n"; pinsAlt += at + "0\n"; }
            else if (c == 'V') {
                const std::string id = std::to_string(20 + i);
                for (int alt = 0; alt < 2; ++alt) {
                    std::string& o = alt ? pinsAlt : pins;
                    o += "node " + id + " MakeVec3\n";
                    for (int k = 0; k < 3; ++k) o += "default " + id + " " + std::to_string(k) + " " + (alt ? "0" : std::to_string(k + 1)) + "\n";
                    o += "data 2 " + std::to_string(i) + " " + id + " 0\n";
                }
            }
        }
        if (isEvent) {
            g = "";
            g += "node 1 " + type + (d.param ? " " + ArgFor(d, false) : std::string()) + "\nentry " + type + " 1\n";
            g += "node 3 SetVar fired\nexec 1 0 3\n";
        } else if (sig->execIn) {
            g += line + "\n" + pins + "exec 1 0 2\n";
            // Every flow output goes to a counter, so a flow node is proven by where the flow goes.
            const int outs = std::max(1, (int)d.execOut.size());
            for (int k = 0; k < outs && k < 8; ++k)
                g += "node " + std::to_string(10 + k) + " AddVar flow" + std::to_string(k) + "\ndefault " + std::to_string(10 + k) + " 0 1\nexec 2 " + std::to_string(k) + " " + std::to_string(10 + k) + "\n";
            // A flow node that also hands a value back: read it, after the flow, into a variable.
            if (!sig->outs.empty()) g += "node 4 SetVar out\ndata 4 0 2 0\nexec 10 0 4\n";
        } else {
            g += line + "\n" + pins + "node 3 SetVar out\nexec 1 0 3\ndata 3 0 2 0\n";
        }
        std::string gAlt = g;
        if (d.param) { const std::size_t at = gAlt.find(line); if (at != std::string::npos) gAlt.replace(at, line.size(), "node 2 " + type + " " + ArgFor(d, true)); }
        if (!pins.empty()) { const std::size_t at = gAlt.find(pins); if (at != std::string::npos) gAlt.replace(at, pins.size(), pinsAlt); }
        std::string base = "node 1 OnUpdate\nentry OnUpdate 1\n";
        if (sig->execIn) base += "node 10 AddVar flow0\ndefault 10 0 1\nexec 1 0 10\n";   // the flow simply passes
        if (sig->execIn && !sig->outs.empty()) base += "node 4 SetVar out\ndefault 4 0 -9999\nexec 10 0 4\n";
        if (!sig->execIn && !sig->outs.empty()) base += "node 3 SetVar out\ndefault 3 0 -9999\nexec 1 0 3\n";
        // Does it build?
        {
            Scene s; s.physicsEnabled = false;
            GameObject* me; Populate(s, me);
            auto* vc = me->AddComponent<VisualScriptComponent>();
            std::string err;
            if (!vc->LoadFromText(g, &err)) { brokenBuild.push_back(type + ": " + err); continue; }
        }
        if (isEvent) { eventsBuilt.push_back(type); continue; }
        if (NeedsOnline(d) && OnlineWouldConnect(type)) { onlineBuilt.push_back(type); continue; }
        if (ProvenElsewhere(type)) { ++elsewhere; continue; }
        if (Dangerous(type)) ++dangerousRun;
        std::string snap[3];
        int errs[3] = {0, 0, 0};
        for (int pass = 0; pass < 3; ++pass) {
            Time::SetTimeScale(1.0f); Time::CancelHitStop(); ResetStatics();
            Scene s; s.physicsEnabled = false;
            GameObject* me; Populate(s, me);
            auto* vc = me->AddComponent<VisualScriptComponent>();
            std::string err;
            vc->LoadFromText(pass == 0 ? g : (pass == 1 ? base : gAlt), &err);
            g_errs.clear(); g_logLines = 0;
            s.Start();
            for (int f = 0; f < 6; ++f) s.Update(0.25f);
            snap[pass] = Snapshot(s);
            errs[pass] = (int)g_errs.size();
            Time::SetTimeScale(1.0f); Time::CancelHitStop();
        }
        bool changed = snap[0] != snap[1] || snap[0] != snap[2];
        bool reportedErr = errs[0] > errs[1];
        // Still nothing: give it the component it drives (see RichPopulate), with physics on.
        for (int variant = 0; variant < 2 && !changed && !reportedErr; ++variant) {
            std::string rs[2]; int re[2] = {0, 0};
            for (int pass = 0; pass < 2; ++pass) {
                Time::SetTimeScale(1.0f); Time::CancelHitStop(); ResetStatics();
                Scene s; s.physicsEnabled = true;
                GameObject* me; RichPopulate(s, me, variant);
                auto* vc = me->AddComponent<VisualScriptComponent>();
                std::string err;
                vc->LoadFromText(pass == 0 ? g : base, &err);
                g_errs.clear(); g_logLines = 0;
                s.Start();
                for (int f = 0; f < 8; ++f) s.Update(0.25f);
                rs[pass] = Snapshot(s);
                re[pass] = (int)g_errs.size();
                Time::SetTimeScale(1.0f); Time::CancelHitStop();
            }
            if (rs[0] != rs[1]) changed = true;
            if (re[0] > re[1]) reportedErr = true;
        }
        auto byScenario = [&]() {
            for (const Fix& fx : fixes) {
                if (type != fx.type) continue;
                std::string fg, fb;
                FixGraphs(d, sig, fx, fg, fb);
                int e0 = 0, e1 = 0;
                if (RunScenario(fx, fg, &e0) != RunScenario(fx, fb, &e1) || e0 > e1) return true;
            }
            return false;
        };
        if (NeedsOnline(d)) {
            ++onlineRun;
            if (changed || reportedErr || byScenario()) ++onlineProven; else onlineSilent.push_back(type);
            continue;
        }
        if (IsBinding(type)) {
            ++bindingsRun;
            bool viaScenario = false;
            if (!changed && !reportedErr) {
                for (const Fix& fx : fixes) {
                    if (type != fx.type) continue;
                    std::string fg, fb;
                    FixGraphs(d, sig, fx, fg, fb);
                    int e0 = 0, e1 = 0;
                    if (RunScenario(fx, fg, &e0) != RunScenario(fx, fb, &e1) || e0 > e1) viaScenario = true;
                }
            }
            if (changed || reportedErr || viaScenario) ++bindingsProven; else bindingSilent.push_back(type);
            continue;
        }
        if (changed || reportedErr) { ++proven; if (reportedErr && !changed) ++reported; continue; }
        bool shown = false;
        for (const Fix& fx : fixes) {
            if (type != fx.type) continue;
            std::string fg, fb;
            FixGraphs(d, sig, fx, fg, fb);
            int e0 = 0, e1 = 0;
            const std::string a = RunScenario(fx, fg, &e0), b = RunScenario(fx, fb, &e1);
            if (a != b || e0 > e1) {
                shown = true;
                if (std::getenv("OKAY_PRINT_PROOF")) {
                    std::size_t i = 0; while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
                    const std::size_t from = i > 24 ? i - 24 : 0;
                    std::printf("  proof %s: with [%s] without [%s]\n", type.c_str(), a.substr(from, 70).c_str(), b.substr(from, 70).c_str());
                }
            }
            else if (printGaps) std::printf("  scenario %s: no difference (errors %d/%d)\n", type.c_str(), e0, e1);
        }
        if (shown) { ++proven; ++scenarioProven; continue; }
        unproven.push_back(type);
    }
    Log::sink = oldSink;
    vs::SetRuntimeErrorHook(nullptr);
    std::printf("  engine and text-script bindings: %d run headless, %d changed the scene or reported, the rest are on a short ratchet list in this file\n", bindingsRun, bindingsProven);
    std::printf("  %d gameplay library nodes are proven in vs_gameplay_library\n", elsewhere);
    if (printGaps) { for (const auto& e : eventsBuilt) std::printf("  EVENT %s\n", e.c_str()); for (const auto& e : onlineBuilt) std::printf("  ONLINE %s\n", e.c_str()); for (const auto& e : skipped) std::printf("  SKIPPED %s\n", e.c_str()); }
    std::printf("  online nodes: %d run offline, %d changed something, returned a value or reported, %d silent, %d built only (they would open a connection)\n", onlineRun, onlineProven, (int)onlineSilent.size(), (int)onlineBuilt.size());
    if (printGaps) for (const auto& e : bindingSilent) std::printf("  BINDING-SILENT %s\n", e.c_str());
    if (printGaps) for (const auto& e : onlineSilent) std::printf("  SILENT %s\n", e.c_str());
    std::printf("  %d quit / load-scene nodes were run in a sandbox scene (the quit flag and the scene's load request are watched)\n", dangerousRun);
    std::printf("  %d of those proven by a targeted scenario\n", scenarioProven);
    std::printf("  %d nodes: %d proven (%d by a clear runtime message), %d online (build only), %d events (dispatch checked in CheckEvents), %d skipped, %d unproven\n",
                total, proven, reported, (int)onlineBuilt.size(), (int)eventsBuilt.size(), (int)skipped.size(), (int)unproven.size());
    for (const auto& b : brokenBuild) { std::printf("  does not build: %s\n", b.c_str()); }
    CHECK(brokenBuild.empty());
    std::set<std::string> now(unproven.begin(), unproven.end());
    if (printGaps) for (const auto& u : unproven) std::printf("    \"%s\",\n", u.c_str());
    if (printGaps) for (const VsDef& d : Registry()) if (now.count(d.type)) { const vs::NodeSignature* sg = vs::SignatureFor(d.type); std::printf("  sig %s ins=%s outs=%s hint=%s\n", d.type, sg ? sg->ins.c_str() : "?", sg ? sg->outs.c_str() : "?", d.paramHint ? d.paramHint : ""); }
    {
        std::set<std::string> silentNow(bindingSilent.begin(), bindingSilent.end()), silentKnown(std::begin(kKnownSilentBindings), std::end(kKnownSilentBindings));
        for (const auto& b : silentNow) if (!silentKnown.count(b)) { std::printf("  binding does nothing and is not on the list: %s\n", b.c_str()); CHECK(false); }
        for (const auto& b : silentKnown) if (!silentNow.count(b)) { std::printf("  binding is proven now, remove it from the list: %s\n", b.c_str()); CHECK(false); }
    }
    for (const auto& u : unproven) if (!known.count(u)) { std::printf("  unproven, not on the known list: %s\n", u.c_str()); CHECK(false); }
    for (const auto& k : known) if (!now.count(k)) { std::printf("  on the known list but now proven: %s (remove it)\n", k.c_str()); CHECK(false); }
    CheckEvents();
    Game::CancelQuit();
    TEST_MAIN_RESULT();
}
