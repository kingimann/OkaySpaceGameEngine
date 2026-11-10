// Every node in the registry is built in a graph and run in a scene that has the usual
// components. A node is proven when something observable differs from the same graph run
// without it: a transform, a component value, a variable, a log line, a runtime error that
// names the node (a clear "can't do that here"), or a value read into a variable.
// Nodes that cannot run headless (hosting, Steam, PlayFab) must still build; events are
// built and registered. A node that is none of these and not on the known list fails.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/Components/AudioSource.hpp"
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

static std::vector<std::string> g_errs;
static int g_logLines = 0;
static void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e.nodeType); }

static std::string Str(const vs::VsValue& v) {
    char b[96];
    std::snprintf(b, sizeof b, "%.4f|%d|", (double)v.AsFloat(), (int)v.AsBool());
    return std::string(b) + v.AsString() + "|" + std::to_string((unsigned long long)v.AsObjectId());
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
        if (auto* rb = g->GetComponent<Rigidbody3D>()) { std::snprintf(b, sizeof b, "R%.2f,%.2f,%.2f,%.2f,%.2f", rb->velocity.x, rb->velocity.y, rb->velocity.z, rb->mass, rb->gravityScale); out += b; }
        if (auto* t = g->GetComponent<TextRenderer>()) { std::snprintf(b, sizeof b, "T%s|%.2f,%.2f,%.2f,%.2f|%.2f", t->text.c_str(), t->color.r, t->color.g, t->color.b, t->color.a, t->pixelSize); out += b; }
        if (auto* a = g->GetComponent<AudioSource>()) { std::snprintf(b, sizeof b, "A%.2f,%.2f,%d,%d", a->volume, a->pitch, (int)a->loop, (int)a->IsPlaying()); out += b; }
        if (auto* c = g->GetComponent<Camera>()) { std::snprintf(b, sizeof b, "C%.2f,%.2f,%.2f,%.2f", c->zoom, c->fieldOfView, c->dutch, c->ShakeTrauma()); out += b; }
        if (auto* sr = g->GetComponent<SpriteRenderer>()) { std::snprintf(b, sizeof b, "S%.2f,%.2f,%.2f,%.2f", sr->color.r, sr->color.g, sr->color.b, sr->color.a); out += b; }
        if (auto* mr = g->GetComponent<MeshRenderer>()) { std::snprintf(b, sizeof b, "M%.2f,%.2f,%.2f", mr->color.r, mr->color.g, mr->color.b); out += b; }
        if (auto* q = g->GetComponent<QuestLog>()) for (const auto& id : q->Ids()) out += id + QuestStatusName(q->Status(id));
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
static bool IsBinding(const std::string& t) { return t.rfind("Fn.", 0) == 0 || t.find('.') != std::string::npos; }
static bool Dangerous(const std::string& t) {
    for (const char* k : {"Quit", "Exit", "LoadScene", "RestartScene", "Fn.quit", "Fn.load_scene", "Fn.exit"})
        if (t.find(k) != std::string::npos) return true;
    return false;
}

int main() {
    RUN_SUITE("vs_registry_effect");
    auto oldSink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) { ++g_logLines; };
    vs::SetRuntimeErrorHook(&OnErr);
    std::set<std::string> known(std::begin(kKnownUnproven), std::end(kKnownUnproven));
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
        } else {
            g += line + "\n" + pins + "node 3 SetVar out\nexec 1 0 3\ndata 3 0 2 0\n";
        }
        std::string gAlt = g;
        if (d.param) { const std::size_t at = gAlt.find(line); if (at != std::string::npos) gAlt.replace(at, line.size(), "node 2 " + type + " " + ArgFor(d, true)); }
        if (!pins.empty()) { const std::size_t at = gAlt.find(pins); if (at != std::string::npos) gAlt.replace(at, pins.size(), pinsAlt); }
        std::string base = "node 1 OnUpdate\nentry OnUpdate 1\n";
        if (sig->execIn) base += "node 10 AddVar flow0\ndefault 10 0 1\nexec 1 0 10\n";   // the flow simply passes
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
        if (NeedsOnline(d)) { onlineBuilt.push_back(type); continue; }
        if (Dangerous(type)) { skipped.push_back(type); continue; }
        if (ProvenElsewhere(type)) { ++elsewhere; continue; }
        std::string snap[3];
        int errs[3] = {0, 0, 0};
        for (int pass = 0; pass < 3; ++pass) {
            Time::SetTimeScale(1.0f); Time::CancelHitStop();
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
        const bool changed = snap[0] != snap[1] || snap[0] != snap[2];
        const bool reportedErr = errs[0] > errs[1];
        if (IsBinding(type)) { ++bindingsRun; if (changed || reportedErr) ++bindingsProven; continue; }
        if (changed || reportedErr) { ++proven; if (reportedErr && !changed) ++reported; continue; }
        unproven.push_back(type);
    }
    Log::sink = oldSink;
    vs::SetRuntimeErrorHook(nullptr);
    std::printf("  engine and text-script bindings: %d run headless, %d changed the scene or reported (not enforced one by one; vs_script_coverage runs every one)\n", bindingsRun, bindingsProven);
    std::printf("  %d gameplay library nodes are proven in vs_gameplay_library\n", elsewhere);
    std::printf("  %d nodes: %d proven (%d by a clear runtime message), %d online (build only), %d events (build only), %d skipped, %d unproven\n",
                total, proven, reported, (int)onlineBuilt.size(), (int)eventsBuilt.size(), (int)skipped.size(), (int)unproven.size());
    for (const auto& b : brokenBuild) { std::printf("  does not build: %s\n", b.c_str()); }
    CHECK(brokenBuild.empty());
    std::set<std::string> now(unproven.begin(), unproven.end());
    if (printGaps) for (const auto& u : unproven) std::printf("    \"%s\",\n", u.c_str());
    if (printGaps) for (const VsDef& d : Registry()) if (now.count(d.type) && d.param) std::printf("  hint %s: %s\n", d.type, d.paramHint ? d.paramHint : "");
    for (const auto& u : unproven) if (!known.count(u)) { std::printf("  unproven, not on the known list: %s\n", u.c_str()); CHECK(false); }
    for (const auto& k : known) if (!now.count(k)) { std::printf("  on the known list but now proven: %s (remove it)\n", k.c_str()); CHECK(false); }
    TEST_MAIN_RESULT();
}
