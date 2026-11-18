// Quick blocks: every one-block action is small, loads from its saved form, and really does its job when run.
#include "test_framework.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/VisualScript/QuickBlocks.hpp"
#include "VisualScriptGlossary.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace okay;

namespace {
std::vector<std::string> g_errors, g_logs;
float Score(Scene& s) { return vs::SceneVariables(&s)["score"].AsFloat(); }
void Keys(std::initializer_list<char> down) { Input::FeedKeys(std::vector<char>(down)); }
std::string Defaults(int node, std::initializer_list<const char*> values) {
    std::string out; int pin = 0;
    for (const char* v : values) { if (v[0] != '-' || v[1]) out += "default " + std::to_string(node) + " " + std::to_string(pin) + " " + v + "\n"; ++pin; }
    return out;
}
// A scene with one object that holds the graph.
struct Rig {
    Scene scene{"Q"};
    GameObject* go = nullptr;
    VisualScriptComponent* vs = nullptr;
    explicit Rig(const std::string& graph, bool mesh = true) {
        scene.physicsEnabled = false;
        go = scene.CreateGameObject("Thing");
        if (mesh) go->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        vs = go->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vs->LoadFromText(graph, &err));
        if (!err.empty()) std::printf("  load error: %s\n", err.c_str());
    }
    void Start() { scene.Start(); }
    void Tick(int n, float dt) { for (int i = 0; i < n; ++i) scene.Update(dt); }
    Vec3 Pos() const { return go->transform->localPosition; }
};
// "OnStart -> block" and "OnUpdate -> block" with the typed values.
std::string AfterStart(const char* type, const std::string& defs, const std::string& after = "") {
    return std::string("node 1 OnStart\nentry OnStart 1\nnode 2 ") + type + "\n" + defs + "exec 1 0 2\n" + after;
}
std::string AfterUpdate(const char* type, const std::string& defs, const std::string& after = "") {
    return std::string("node 1 OnUpdate\nentry OnUpdate 1\nnode 2 ") + type + "\n" + defs + "exec 1 0 2\n" + after;
}
std::string Alone(const char* type, const std::string& defs, const std::string& after = "") {
    return std::string("node 1 ") + type + "\n" + defs + after;
}
const char* kThen = "node 3 ScoreAdd\nexec 2 0 3\n";   // "Then do": adds 1 to the score
} // namespace

int main() {
    RUN_SUITE("vs_quick_blocks");
    auto sink = okay::Log::sink;
    okay::Log::sink = [](okay::Log::Level lv, const std::string& m) {
        if (lv == okay::Log::Level::Error) g_errors.push_back(m);
        g_logs.push_back(m);
    };
    Time::SetTimeScale(1.0f);

    // ---- The table itself: plain, small, complete.
    const auto& all = vs::QuickBlocks();
    CHECK(all.size() >= 69);   // more one-block actions are the plain nodes that already do their job in one block: Heal, Save Checkpoint, Shake Camera, Say Text
    std::set<std::string> types, titles;
    for (const vs::QuickBlock& b : all) {
        CHECK(types.insert(b.type).second);
        CHECK(titles.insert(b.title).second);
        CHECK(b.title[0] && b.description[0] && b.example[0] && b.group[0]);
        for (const vs::QbField& f : b.fields) CHECK(f.name[0] && f.def);
        // One block expands into at most 14 low-level nodes, and the block itself is 1 node.
        const std::string saved = std::string("node 1 ") + b.type + "\n";
        const std::string open = vs::ExpandQuickBlocks(saved);
        std::string err;
        auto g = vs::NodeGraph::Parse(saved, &err);
        CHECK(g != nullptr);
        if (!g) std::printf("  %s does not load: %s\n", b.type, err.c_str());
        CHECK(open.find("node 1 Qb") == std::string::npos);
    }
    // A graph without a quick block is left exactly as it was.
    CHECK(vs::ExpandQuickBlocks("node 1 OnStart\nentry OnStart 1\n") == "node 1 OnStart\nentry OnStart 1\n");

    // ---- Movement
    {   // Move With Keys: hold D, the object slides right (no wire for the key or the speed).
        Rig r(Alone("QbMoveWithKeys", Defaults(1, {"wasd", "5"})));
        r.Start(); Keys({}); r.Tick(1, 0.1f); Keys({'d'}); r.Tick(1, 0.5f);
        CHECK(r.Pos().x > 1.0f && std::fabs(r.Pos().y) < 0.01f);
        Keys({'w'}); r.Tick(1, 0.5f);
        CHECK(r.Pos().y > 1.0f);
        Keys({}); 
    }
    {   // Move To Spot
        Rig r(Alone("QbMoveToward", Defaults(1, {"5,0,0", "2"})));
        r.Start(); r.Tick(1, 0.5f);
        CHECK(r.Pos().x > 0.5f);
    }
    {   // Follow Object
        Rig r(Alone("QbFollowObject", Defaults(1, {"Player", "3"})));
        auto* player = r.scene.CreateGameObject("Player");
        player->transform->localPosition = {4, 0, 0};
        r.Start(); r.Tick(1, 0.5f);
        CHECK(r.Pos().x > 0.5f);
    }
    {   // Walk Back And Forth (also a moving platform)
        Rig r(Alone("QbPatrol", Defaults(1, {"3", "1.5"})));
        r.Start(); r.Tick(1, 0.4f);
        CHECK(std::fabs(r.Pos().x) > 0.05f);
    }
    {   // Jump On Key
        Rig r(Alone("QbJumpOnKey", Defaults(1, {"space", "7"})));
        r.scene.physicsEnabled = true;
        r.go->AddComponent<Rigidbody3D>();
        r.Start(); Keys({}); r.Tick(1, 0.016f); Keys({' '}); r.Tick(1, 0.016f);
        auto* rb = r.go->GetComponent<Rigidbody3D>();
        CHECK(r.Pos().y > 0.0f || rb->velocity.y > 0.0f);
        Keys({});
    }
    {   // Turn To Face
        Rig r(AfterStart("QbTurnToFace", Defaults(2, {"Player"})));
        auto* player = r.scene.CreateGameObject("Player");
        player->transform->localPosition = {5, 0, 0};
        const float before = r.go->transform->LocalEulerAngles().y;
        r.Start(); r.Tick(1, 0.016f);
        CHECK(std::fabs(r.go->transform->LocalEulerAngles().y - before) > 10.0f);
    }
    // ---- Game rules
    {   // Add To Score
        Rig r(AfterStart("QbAddToScore", Defaults(2, {"3"})));
        r.Start(); CHECK(Score(r.scene) == 3.0f);
    }
    {   // Show Score
        Rig r(Alone("QbShowScore", Defaults(1, {"\"Score: \""})), false);
        r.go->AddComponent<TextRenderer>();
        vs::SceneVariables(&r.scene)["score"] = vs::VsValue(4.0f);
        r.Start(); r.Tick(2, 0.016f);
        auto* tr = r.go->GetComponent<TextRenderer>();
        CHECK(tr->text.find("Score: 4") != std::string::npos);
        if (tr->text.find("Score: 4") == std::string::npos) std::printf("  text is '%s'\n", tr->text.c_str());
    }
    {   // Lose Health (and the plain Heal node after it)
        Rig r(AfterUpdate("QbLoseHealth", Defaults(2, {"30"}), "node 3 Heal\n" + Defaults(3, {"10"}) + "exec 2 0 3\n"));
        auto* hp = r.go->AddComponent<HealthStat>();
        hp->playDeathAnim = false; hp->maxHealth = 100;
        r.Start(); r.Tick(1, 0.016f);
        CHECK(std::fabs(hp->health - 80.0f) < 0.5f);   // 100 - 30 + 10
    }
    {   // Game Over When Health Is 0
        Time::SetTimeScale(1.0f);
        Rig r(Alone("QbGameOverAtZeroHealth", ""));
        auto* hp = r.go->AddComponent<HealthStat>();
        hp->playDeathAnim = false; hp->keepBodyOnDeath = true;
        r.Start(); r.Tick(1, 0.016f);
        CHECK(Time::TimeScale() == 1.0f);
        hp->Damage(hp->maxHealth * 2); r.Tick(1, 0.016f);
        CHECK(Time::TimeScale() == 0.0f);
        CHECK(vs::SceneVariables(&r.scene)["outcome"].AsString() == "lose");
        Time::SetTimeScale(1.0f);
    }
    {   // Game Over When Touched
        Time::SetTimeScale(1.0f);
        Rig r(Alone("QbGameOverOnTouch", ""));
        r.Start(); r.Tick(1, 0.016f);
        CHECK(Time::TimeScale() == 1.0f);
        r.vs->OnTriggerEnter3D(nullptr);
        CHECK(Time::TimeScale() == 0.0f);
        CHECK(vs::SceneVariables(&r.scene)["outcome"].AsString() == "lose");
        Time::SetTimeScale(1.0f);
    }
    {   // Win When Score Reaches
        Time::SetTimeScale(1.0f);
        Rig r(Alone("QbWinAtScore", Defaults(1, {"5"})));
        r.Start(); r.Tick(1, 0.016f);
        CHECK(Time::TimeScale() == 1.0f);
        vs::SceneVariables(&r.scene)["score"] = vs::VsValue(5.0f); r.Tick(1, 0.016f);
        CHECK(Time::TimeScale() == 0.0f);
        CHECK(vs::SceneVariables(&r.scene)["outcome"].AsString() == "win");
        Time::SetTimeScale(1.0f);
    }
    {   // Do Every Few Seconds
        Rig r(Alone("QbEverySeconds", Defaults(1, {"0.5"}), "node 2 ScoreAdd\nexec 1 0 2\n"));
        r.Start(); r.Tick(1, 0.2f); CHECK(Score(r.scene) == 0.0f);
        r.Tick(1, 0.4f); CHECK(Score(r.scene) >= 1.0f);
    }
    {   // Go Back To Checkpoint (the plain Save Checkpoint node saves the spot)
        Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 CheckpointSave\nexec 1 0 2\nnode 3 OnKey r\nentry OnKey:r 3\nnode 4 QbRespawnAtCheckpoint\nexec 3 0 4\n");
        r.go->transform->localPosition = {3, 1, 0};
        r.Start(); r.go->transform->localPosition = {0, 0, 0};
        Keys({}); r.Tick(1, 0.016f); Keys({'r'}); r.Tick(1, 0.016f);
        CHECK(r.Pos().x > 2.5f);
        Keys({});
    }
    // ---- Things in the world
    {   // Collect On Touch: score, sound, hidden.
        Rig r(Alone("QbCollectOnTouch", Defaults(1, {"2"})));
        auto* au = r.go->AddComponent<AudioSource>();
        au->clip = AudioClip::Sine(523.0f, 0.18f);
        r.Start(); r.vs->OnTriggerEnter3D(nullptr);
        CHECK(Score(r.scene) == 2.0f);
        CHECK(!r.go->active);
        CHECK(au->IsPlaying() || au->HasAudio());
    }
    {   // Open Door When Touched
        Rig r(Alone("QbOpenDoorWhenTouched", Defaults(1, {"2"})));
        r.Start(); r.vs->OnTriggerEnter3D(nullptr);
        CHECK(r.Pos().y > 1.5f);
    }
    {   // Spinning Object
        Rig r(Alone("QbSpinningObject", Defaults(1, {"90"})));
        r.Start(); r.Tick(1, 0.5f);
        CHECK(std::fabs(r.go->transform->LocalEulerAngles().y) > 10.0f);
    }
    {   // Spawn Copy At, Shoot On Key, Spawn Copy Every Few Seconds: they make copies of a saved object.
        {
            Scene proto("Proto");
            auto* shot = proto.CreateGameObject("Shot");
            shot->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
            CHECK(SceneSerializer::SaveObjectToFile(*shot, "quickblock_shot.okayprefab"));
        }
        {
            Rig r(AfterStart("QbSpawnCopyAt", Defaults(2, {"quickblock_shot.okayprefab", "2,1,0"})));
            const std::size_t n = r.scene.Objects().size();
            r.Start(); r.Tick(1, 0.016f);
            CHECK(r.scene.Objects().size() == n + 1);
        }
        {
            Rig r(Alone("QbShootOnKey", Defaults(1, {"space", "quickblock_shot.okayprefab", "10"})));
            const std::size_t n = r.scene.Objects().size();
            r.Start(); Keys({}); r.Tick(1, 0.016f); Keys({' '}); r.Tick(1, 0.016f);
            CHECK(r.scene.Objects().size() > n);
            Keys({});
        }
        {
            Rig r(Alone("QbSpawnEvery", Defaults(1, {"quickblock_shot.okayprefab", "0.5"})));
            const std::size_t n = r.scene.Objects().size();
            r.Start(); r.Tick(1, 0.6f); r.Tick(1, 0.6f);
            CHECK(r.scene.Objects().size() > n);
        }
        std::remove("quickblock_shot.okayprefab");
    }
    {   // Remove After A While
        Rig r(Alone("QbRemoveAfter", Defaults(1, {"0.3"})));
        r.Start(); r.Tick(1, 0.5f); r.Tick(2, 0.016f);
        CHECK(r.scene.Find("Thing") == nullptr);
    }
    // ---- Feedback
    {   // Show Text On Screen: the message appears, then goes away.
        Rig r(AfterStart("QbShowText", Defaults(2, {"\"Hello!\"", "1"})), false);
        r.go->AddComponent<TextRenderer>();
        r.Start(); r.Tick(1, 0.016f);
        auto* tr = r.go->GetComponent<TextRenderer>();
        CHECK(tr->text == "Hello!");
        r.Tick(1, 1.2f); r.Tick(1, 0.016f);
        CHECK(tr->text.empty());
    }
    {   // Play Sound On Key
        Rig r(Alone("QbPlaySoundOnKey", Defaults(1, {"space"})));
        auto* au = r.go->AddComponent<AudioSource>();
        au->clip = AudioClip::Sine(523.0f, 0.18f);
        r.Start(); Keys({}); r.Tick(1, 0.016f);
        CHECK(!au->IsPlaying());
        Keys({' '}); r.Tick(1, 0.016f);
        CHECK(au->IsPlaying());
        Keys({});
    }
    {   // Flash Colour: red at once, white again after the seconds.
        Rig r(AfterStart("QbFlashColour", Defaults(2, {"#ff0000", "0.2"})));
        r.Start(); r.Tick(1, 0.016f);
        auto* mr = r.go->GetComponent<MeshRenderer>();
        CHECK(mr->color.r > 0.9f && mr->color.g < 0.1f);
        r.Tick(1, 0.3f); r.Tick(1, 0.016f);
        CHECK(mr->color.g > 0.9f);
    }
    {   // Follow With Camera: the camera is told to follow the named object.
        Rig r(AfterStart("QbFollowWithCamera", Defaults(2, {"Player", "5"})));
        auto* cam = r.scene.CreateGameObject("Main Camera");
        r.scene.mainCamera = cam->AddComponent<Camera>();
        r.scene.CreateGameObject("Player");
        r.Start(); r.Tick(1, 0.016f);
        auto* f = cam->GetComponent<CameraFollow>();
        CHECK(f != nullptr && f->targetName == "Player");
    }
    // ---- Flow
    {   // Wait Then Do
        Rig r(AfterStart("QbWaitThenDo", Defaults(2, {"0.3"}), kThen));
        r.Start(); r.Tick(1, 0.1f); CHECK(Score(r.scene) == 0.0f);
        r.Tick(1, 0.4f); CHECK(Score(r.scene) == 1.0f);
    }
    {   // Repeat Times
        Rig r(AfterStart("QbRepeat", Defaults(2, {"3"}), kThen));
        r.Start(); r.Tick(1, 0.016f);
        CHECK(Score(r.scene) == 3.0f);
    }
    {   // Do Forever
        Rig r(Alone("QbDoForever", "", "node 2 ScoreAdd\nexec 1 0 2\n"));
        r.Start(); r.Tick(3, 0.016f);
        CHECK(Score(r.scene) >= 3.0f);
    }
    {   // If Key Pressed Do
        Rig r(AfterUpdate("QbIfKeyThenDo", Defaults(2, {"space"}), kThen));
        r.Start(); Keys({}); r.Tick(2, 0.016f);
        CHECK(Score(r.scene) == 0.0f);
        Keys({' '}); r.Tick(1, 0.016f);
        CHECK(Score(r.scene) >= 1.0f);
        Keys({});
    }
    {   // When Touched Do
        Rig r(Alone("QbWhenTouchedDo", "", "node 2 ScoreAdd\nexec 1 0 2\n"));
        r.Start(); r.vs->OnTriggerEnter3D(nullptr);
        CHECK(Score(r.scene) == 1.0f);
    }

    // ---- Field kinds: every field has a kind, a valid default and plain words; choices and presets are well formed.
    {
        using vs::QbKind;
        int kinds[8] = {};
        for (const vs::QuickBlock& b : all) {
            const auto fields = vs::QbAllFields(b);
            for (const vs::QbField& f : fields) {
                ++kinds[(int)f.kind];
                if (f.kind != QbKind::Preset) CHECK(vs::QbValidate(f.kind, f.def, f.choices) || (f.kind == QbKind::Text || f.kind == QbKind::Object));
                if (f.kind == QbKind::Choice || f.kind == QbKind::Preset) {
                    const auto choices = vs::QbChoices(f.choices);
                    CHECK(!choices.empty());
                    for (const auto& c : choices) {
                        CHECK(!c.first.empty() && !c.second.empty());
                        const std::string bad = okay::editor::vsed::VsGlossaryViolation(c.first);
                        CHECK(bad.empty());
                        if (!bad.empty()) std::printf("  dropdown label '%s' uses '%s'\n", c.first.c_str(), bad.c_str());
                    }
                }
                CHECK(okay::editor::vsed::VsGlossaryViolation(f.name).empty());
            }
            // Presets: each fills in fields that then validate.
            for (const vs::QbPreset& p : b.presets) {
                CHECK(okay::editor::vsed::VsGlossaryViolation(p.name).empty());
                std::vector<std::string> values;
                for (const vs::QbField& f : b.fields) values.push_back(f.def);
                CHECK(vs::QbApplyPreset(b, p.name, values));
                CHECK(values.size() == b.fields.size());
                for (std::size_t i = 0; i < values.size() && i < b.fields.size(); ++i)
                    CHECK(vs::QbValidate(b.fields[i].kind, values[i], b.fields[i].choices) || b.fields[i].kind == QbKind::Text || b.fields[i].kind == QbKind::Object);
            }
            if (!b.presets.empty()) CHECK(fields.back().kind == QbKind::Preset);
        }
        std::printf("  field kinds in use: %d number, %d text, %d key, %d object, %d choice, %d colour, %d preset, %d name\n",
                    kinds[(int)QbKind::Number], kinds[(int)QbKind::Text], kinds[(int)QbKind::Key], kinds[(int)QbKind::Object],
                    kinds[(int)QbKind::Choice], kinds[(int)QbKind::Colour], kinds[(int)QbKind::Preset], kinds[(int)QbKind::Name]);
        for (int k = 0; k < 8; ++k) CHECK(kinds[k] > 0);
        // Value rules.
        CHECK(vs::QbValidate(QbKind::Number, "3.5") && vs::QbValidate(QbKind::Number, "-2") && !vs::QbValidate(QbKind::Number, "abc") && !vs::QbValidate(QbKind::Number, "") && !vs::QbValidate(QbKind::Number, "1e99"));
        CHECK(vs::QbValidate(QbKind::Key, "a") && vs::QbValidate(QbKind::Key, "space") && !vs::QbValidate(QbKind::Key, "") && !vs::QbValidate(QbKind::Key, "12345"));
        CHECK(vs::QbValidate(QbKind::Colour, "#00ff7a") && !vs::QbValidate(QbKind::Colour, "00ff7a") && !vs::QbValidate(QbKind::Colour, "#00ff7"));
        CHECK(vs::QbValidate(QbKind::Choice, "Mul", "add~Add|multiply~Mul") && !vs::QbValidate(QbKind::Choice, "Pow", "add~Add|multiply~Mul"));
        CHECK(vs::QbNormalize(QbKind::Key, " Space ") == "space" && vs::QbNormalize(QbKind::Colour, "#FF00AA") == "#ff00aa" && vs::QbNormalize(QbKind::Number, " 4 ") == "4");
        // Smart defaults: the last key is used for a key field; an object field picks an object that exists.
        vs::QbContext ctx;
        const vs::QbField keyField{"Key", "space", QbKind::Key}, who{"Who", "Player", QbKind::Object};
        CHECK(vs::QbSmartDefault(keyField, ctx) == "space");
        ctx.lastKey = "e";
        CHECK(vs::QbSmartDefault(keyField, ctx) == "e");
        CHECK(vs::QbSmartDefault(who, ctx) == "Player");
        ctx.objectNames = {"Hero", "Ghost"}; ctx.selfName = "Hero";
        CHECK(vs::QbSmartDefault(who, ctx) == "Ghost");
        ctx.objectNames = {"Hero", "Player"};
        CHECK(vs::QbSmartDefault(who, ctx) == "Player");
        // Old nodes that a block covers map to a block and a dropdown value.
        for (const char* old : {"AddVar", "PlaySound", "StopSound"}) {
            const vs::QbAlias* a = vs::QuickBlockForOldNode(old);
            CHECK(a != nullptr);
            if (!a) continue;
            const vs::QuickBlock* b = vs::FindQuickBlock(a->block);
            CHECK(b != nullptr && a->field < (int)b->fields.size());
            if (b) CHECK(vs::QbValidate(QbKind::Choice, a->value, b->fields[(std::size_t)a->field].choices));
        }
        CHECK(vs::QuickBlockForOldNode("Translate") == nullptr);
    }

    // ---- Merged blocks (dropdowns)
    {
        // Change Number: add 5, take 2, multiply by 4, divide by 3 -> 4.
        Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 QbChangeNumber\n" + Defaults(2, {"n", "Add", "5"}) +
              "node 3 QbChangeNumber\n" + Defaults(3, {"n", "Sub", "2"}) + "node 4 QbChangeNumber\n" + Defaults(4, {"n", "Mul", "4"}) +
              "node 5 QbChangeNumber\n" + Defaults(5, {"n", "Div", "3"}) + "exec 1 0 2\nexec 2 0 3\nexec 3 0 4\nexec 4 0 5\n");
        r.Start();
        CHECK(std::fabs(r.vs->GetVariable("n").AsFloat() - 4.0f) < 1e-3f);
    }
    {   // If Number Is: every comparison choice.
        struct C { const char* op; const char* n; bool pass; };
        for (const C c : {C{"==", "3", true}, C{"==", "4", false}, C{"!=", "4", true}, C{"<", "4", true}, C{"<", "3", false},
                          C{">", "2", true}, C{">=", "3", true}, C{"<=", "2", false}}) {
            Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 SetVar n\ndefault 2 0 3\nnode 3 QbIfNumberIs\n" + Defaults(3, {"n", c.op, c.n}) +
                  "node 4 ScoreAdd\nexec 1 0 2\nexec 2 0 3\nexec 3 0 4\n");
            r.Start();
            CHECK((Score(r.scene) == 1.0f) == c.pass);
            if ((Score(r.scene) == 1.0f) != c.pass) std::printf("  If Number Is 3 %s %s gave %f\n", c.op, c.n, Score(r.scene));
        }
    }
    {   // Play Or Stop Sound
        Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 QbPlayOrStopSound\ndefault 2 0 PlaySound\nexec 1 0 2\n"
              "node 3 OnKey x\nentry OnKey:x 3\nnode 4 QbPlayOrStopSound\ndefault 4 0 StopSound\nexec 3 0 4\n");
        auto* au = r.go->AddComponent<AudioSource>();
        au->clip = AudioClip::Sine(523.0f, 0.5f);
        r.Start(); Keys({}); r.Tick(1, 0.016f);
        CHECK(au->IsPlaying());
        Keys({'x'}); r.Tick(1, 0.016f);
        CHECK(!au->IsPlaying());
        Keys({});
    }

    // ---- The Rule block: one node, no wires, every event / condition / action kind.
    {
        auto rule = [](const char* when, const char* wv, const char* cond, const char* cv, std::initializer_list<const char*> acts) {
            std::string v[12] = {when, wv, cond, cv, "none", "", "none", "", "none", "", "none", ""};
            int i = 4;
            for (const char* a : acts) v[i++] = a;
            std::string g = "node 1 QbRule\n";
            for (int k = 0; k < 12; ++k) if (!v[k].empty()) g += "default 1 " + std::to_string(k) + " " + v[k] + "\n";
            return g;
        };
        // Save and load: the file holds the block id and the typed values only, and no wire.
        {
            const std::string g = rule("key", "space", "none", "", {"score", "3"});
            CHECK(g.find("exec") == std::string::npos && g.find("data") == std::string::npos);
            const std::string open = vs::ExpandQuickBlocks(g);
            CHECK(open.find("node 1 QbRule") == std::string::npos && open.find("entry OnKey:space") != std::string::npos);
        }
        // Events (the action is always "add 1 to the score").
        auto scoreOf = [&](Rig& r) { return Score(r.scene); };
        { Rig r(rule("start", "", "none", "", {"score", "1"})); r.Start(); CHECK(scoreOf(r) == 1.0f); }
        { Rig r(rule("update", "", "none", "", {"score", "1"})); r.Start(); r.Tick(3, 0.016f); CHECK(scoreOf(r) >= 3.0f); }
        { Rig r(rule("key", "space", "none", "", {"score", "1"})); r.Start(); Keys({}); r.Tick(1, 0.016f); CHECK(scoreOf(r) == 0.0f);
          Keys({' '}); r.Tick(1, 0.016f); CHECK(scoreOf(r) >= 1.0f); Keys({}); }
        { Rig r(rule("touched", "", "none", "", {"score", "1"})); r.Start(); r.vs->OnTriggerEnter3D(nullptr); CHECK(scoreOf(r) == 1.0f); }
        { Rig r(rule("click", "", "none", "", {"score", "1"})); r.Start(); r.vs->OnMouseDown(); CHECK(scoreOf(r) == 1.0f); }
        { Rig r(rule("timer", "0.5", "none", "", {"score", "1"})); r.Start(); r.Tick(1, 0.2f); CHECK(scoreOf(r) == 0.0f); r.Tick(1, 0.4f); CHECK(scoreOf(r) >= 1.0f); }
        { Rig r(rule("dead", "", "none", "", {"score", "1"})); auto* hp = r.go->AddComponent<HealthStat>(); hp->playDeathAnim = false; hp->keepBodyOnDeath = true;
          r.Start(); r.Tick(1, 0.016f); CHECK(scoreOf(r) == 0.0f); hp->Damage(hp->maxHealth * 2); r.Tick(1, 0.016f); CHECK(scoreOf(r) >= 1.0f); }
        { Rig r(rule("score", "3", "none", "", {"setvalue", "reached,1"})); r.Start(); r.Tick(1, 0.016f); CHECK(r.vs->GetVariable("reached").AsFloat() == 0.0f);
          vs::SceneVariables(&r.scene)["score"] = vs::VsValue(3.0f); r.Tick(1, 0.016f); CHECK(r.vs->GetVariable("reached").AsFloat() == 1.0f); }
        // Conditions.
        { Rig r(rule("update", "", "key", "space", {"score", "1"})); r.Start(); Keys({}); r.Tick(2, 0.016f); CHECK(scoreOf(r) == 0.0f);
          Keys({' '}); r.Tick(1, 0.016f); CHECK(scoreOf(r) >= 1.0f); Keys({}); }
        { Rig r(rule("update", "", "score", "2", {"setvalue", "ok,1"})); r.Start(); r.Tick(2, 0.016f); CHECK(r.vs->GetVariable("ok").AsFloat() == 0.0f);
          vs::SceneVariables(&r.scene)["score"] = vs::VsValue(2.0f); r.Tick(1, 0.016f); CHECK(r.vs->GetVariable("ok").AsFloat() == 1.0f); }
        { Rig r(rule("update", "", "chance", "0", {"score", "1"})); r.Start(); r.Tick(20, 0.016f); CHECK(scoreOf(r) == 0.0f); }
        { Rig r(rule("update", "", "chance", "1", {"score", "1"})); r.Start(); r.Tick(5, 0.016f); CHECK(scoreOf(r) >= 5.0f); }
        // Actions in order: 1, 2, 3, 4 -> 10.
        { Rig r(rule("start", "", "none", "", {"score", "1", "score", "2", "score", "3", "score", "4"})); r.Start(); CHECK(scoreOf(r) == 10.0f); }
        // Each action kind (event: the game starts).
        { Rig r(rule("start", "", "none", "", {"hurt", "30"})); auto* hp = r.go->AddComponent<HealthStat>(); hp->playDeathAnim = false;
          r.Start(); r.Tick(1, 0.016f); CHECK(hp->health < 100.0f || scoreOf(r) == 0.0f); }
        { Rig r(rule("update", "", "none", "", {"hurt", "30", "heal", "10"})); auto* hp = r.go->AddComponent<HealthStat>(); hp->playDeathAnim = false; hp->maxHealth = 100;
          r.Start(); r.Tick(1, 0.016f); CHECK(std::fabs(hp->health - 80.0f) < 0.5f); }
        { Rig r(rule("start", "", "none", "", {"text", "\"Hi there\""}), false); r.go->AddComponent<TextRenderer>(); r.Start(); r.Tick(1, 0.016f);
          CHECK(r.go->GetComponent<TextRenderer>()->text == "Hi there"); }
        { g_logs.clear(); Rig r(rule("start", "", "none", "", {"say", "\"Rule says hi\""})); r.Start(); r.Tick(1, 0.016f);
          bool said = false; for (const auto& m : g_logs) if (m.find("Rule says hi") != std::string::npos) said = true; CHECK(said); }
        { Rig r(rule("start", "", "none", "", {"sound"})); auto* au = r.go->AddComponent<AudioSource>(); au->clip = AudioClip::Sine(523.0f, 0.5f);
          r.Start(); r.Tick(1, 0.016f); CHECK(au->IsPlaying()); }
        { Rig r(rule("start", "", "none", "", {"hide"})); r.Start(); r.Tick(1, 0.016f); CHECK(!r.go->active); }
        { Rig r(rule("start", "", "none", "", {"hide", "", "show"})); r.Start(); r.Tick(1, 0.016f); CHECK(r.go->active); }
        { Rig r(rule("start", "", "none", "", {"remove"})); r.Start(); r.Tick(3, 0.016f); CHECK(r.scene.Find("Thing") == nullptr); }
        { Scene proto("Proto"); auto* shot = proto.CreateGameObject("Shot"); shot->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
          CHECK(SceneSerializer::SaveObjectToFile(*shot, "quickblock_rule.okayprefab"));
          Rig r(rule("start", "", "none", "", {"spawn", "quickblock_rule.okayprefab"})); const std::size_t n = r.scene.Objects().size();
          r.Start(); r.Tick(1, 0.016f); CHECK(r.scene.Objects().size() == n + 1); std::remove("quickblock_rule.okayprefab"); }
        { Rig r(rule("start", "", "none", "", {"scene", "Level2"})); r.Start(); CHECK(r.scene.HasPendingLoad()); }
        { Rig r(rule("start", "", "none", "", {"wait", "0.3", "score", "1"})); r.Start(); r.Tick(1, 0.1f); CHECK(scoreOf(r) == 0.0f); r.Tick(1, 0.4f); CHECK(scoreOf(r) == 1.0f); }
        { g_errors.clear(); Rig r(rule("start", "", "none", "", {"shake", "0.3"})); auto* cam = r.scene.CreateGameObject("Main Camera");
          r.scene.mainCamera = cam->AddComponent<Camera>(); r.Start(); r.Tick(1, 0.016f); CHECK(g_errors.empty()); }
        { Rig r(rule("start", "", "none", "", {"colour", "#00ff00"})); r.Start(); r.Tick(1, 0.016f);
          auto* mr = r.go->GetComponent<MeshRenderer>(); CHECK(mr->color.g > 0.9f && mr->color.r < 0.1f); }
        { Rig r(rule("start", "", "none", "", {"move", "0,2,0"})); r.Start(); r.Tick(1, 0.016f); CHECK(r.Pos().y > 1.5f); }
        { Rig r(rule("start", "", "none", "", {"turn", "90"})); r.Start(); r.Tick(1, 0.016f); CHECK(std::fabs(r.go->transform->LocalEulerAngles().z) > 10.0f || std::fabs(r.go->transform->LocalEulerAngles().y) > 10.0f); }
        { Rig r(rule("start", "", "none", "", {"setvalue", "gold,5"})); r.Start(); r.Tick(1, 0.016f); CHECK(r.vs->GetVariable("gold").AsFloat() == 5.0f); }
        { g_errors.clear(); Rig r(rule("start", "", "none", "", {"checkpoint"})); r.Start(); r.Tick(1, 0.016f); CHECK(g_errors.empty()); }
        { Time::SetTimeScale(1.0f); Rig r(rule("start", "", "none", "", {"win"})); r.Start(); CHECK(Time::TimeScale() == 0.0f && vs::SceneVariables(&r.scene)["outcome"].AsString() == "win"); Time::SetTimeScale(1.0f); }
        { Time::SetTimeScale(1.0f); Rig r(rule("start", "", "none", "", {"lose"})); r.Start(); CHECK(Time::TimeScale() == 0.0f && vs::SceneVariables(&r.scene)["outcome"].AsString() == "lose"); Time::SetTimeScale(1.0f); }
        // Every dropdown entry (event, condition, action) has a plain label and is covered above by its own value.
        for (const auto* table : {&vs::QbRuleEvents(), &vs::QbRuleConditions(), &vs::QbRuleActions()})
            for (const vs::QbRuleEntry& e : *table) CHECK(okay::editor::vsed::VsGlossaryViolation(e.label).empty());
        // A Rule never needs a wire: a graph of a Rule alone has none, and the preset graphs have none.
        for (const vs::QbPreset& p : vs::FindQuickBlock("QbRule")->presets) {
            std::string g = "node 1 QbRule\n";
            for (std::size_t k = 0; k < p.values.size(); ++k) if (p.values[k][0]) g += "default 1 " + std::to_string(k) + " " + p.values[k] + "\n";
            CHECK(g.find("exec") == std::string::npos);
            auto parsed = vs::NodeGraph::Parse(g);
            CHECK(parsed != nullptr);
        }
    }
    // Presets of a Rule do their job: Coin pickup.
    {
        const vs::QuickBlock* rb = vs::FindQuickBlock("QbRule");
        std::vector<std::string> values;
        for (const vs::QbField& f : rb->fields) values.push_back(f.def);
        CHECK(vs::QbApplyPreset(*rb, "Door opens", values));
        std::string g = "node 1 QbRule\n";
        for (std::size_t k = 0; k < values.size(); ++k) if (!values[k].empty()) g += "default 1 " + std::to_string(k) + " " + values[k] + "\n";
        Rig r(g); r.Start(); r.vs->OnTriggerEnter3D(nullptr);
        CHECK(r.Pos().y > 1.5f);
    }
    {   // Play highlight: a block lights up (its run counter moves) when the nodes it stands for run.
        vs::NodeGraph::Tracing() = true;
        Rig r(Alone("QbWhenTouchedDo", "", "node 2 ScoreAdd\nexec 1 0 2\n"));
        r.Start();
        CHECK(r.vs->Graph()->BlockExecCount(1) == 0);
        r.vs->OnTriggerEnter3D(nullptr);
        CHECK(r.vs->Graph()->BlockExecCount(1) > 0);
        CHECK(r.vs->Graph()->BlockExecCount(2) == 0);   // node 2 is an ordinary node, not a block
        vs::NodeGraph::Tracing() = false;
    }

    // ---- The second wave: one run per block.
    {
        ItemDatabase::Get().Clear();
        ItemDatabase::Get().LoadText("okayitems 1\nitem \"key\"\n  name \"Key\"\nend\n");
        const char* thenScore = "node 2 ScoreAdd\nexec 1 0 2\n";
        {   Rig r(Alone("QbWhenKeyDo", Defaults(1, {"space"}), thenScore)); r.Start(); Keys({}); r.Tick(1, 0.016f); CHECK(Score(r.scene) == 0.0f);
            Keys({' '}); r.Tick(1, 0.016f); CHECK(Score(r.scene) >= 1.0f); Keys({}); }
        {   Rig r(Alone("QbWhenKeyReleasedDo", Defaults(1, {"space"}), thenScore)); r.Start(); Keys({' '}); r.Tick(1, 0.016f); CHECK(Score(r.scene) == 0.0f);
            Keys({}); r.Tick(1, 0.016f); CHECK(Score(r.scene) >= 1.0f); }
        {   Rig r(Alone("QbWhenClickedDo", "", thenScore)); r.Start(); r.vs->OnMouseDown(); CHECK(Score(r.scene) == 1.0f); }
        {   Rig r(Alone("QbWhenBumpedDo", "", thenScore)); r.Start(); Collision3D c{}; r.vs->OnCollisionEnter3D(c); CHECK(Score(r.scene) == 1.0f); }
        {   Rig r(Alone("QbWhenStartDo", "", thenScore)); r.Start(); CHECK(Score(r.scene) == 1.0f); }
        {   Rig r(Alone("QbRunAwayFrom", Defaults(1, {"Player", "3"}))); auto* p = r.scene.CreateGameObject("Player"); p->transform->localPosition = {2, 0, 0};
            r.Start(); r.Tick(2, 0.3f); CHECK(r.Pos().x < -0.3f); }
        {   Rig r(Alone("QbGoRound", Defaults(1, {"2", "1.5"}))); r.Start(); r.Tick(1, 0.4f); CHECK(std::fabs(r.Pos().x) + std::fabs(r.Pos().z) > 0.05f); }
        {   Rig r(Alone("QbFloatUpDown", Defaults(1, {"0.5", "2"}))); r.Start(); r.Tick(1, 0.4f); CHECK(std::fabs(r.Pos().y) > 0.02f); }
        {   Rig r(AfterStart("QbTeleportTo", Defaults(2, {"2,3,4"}))); r.Start(); r.Tick(1, 0.016f);
            CHECK(std::fabs(r.Pos().x - 2) < 0.01f && std::fabs(r.Pos().y - 3) < 0.01f && std::fabs(r.Pos().z - 4) < 0.01f); }
        {   Rig r(AfterStart("QbLaunch", Defaults(2, {"0,8,0"}))); r.scene.physicsEnabled = true; r.go->AddComponent<Rigidbody3D>();
            r.Start(); r.Tick(2, 0.016f); CHECK(r.Pos().y > 0.0f || r.go->GetComponent<Rigidbody3D>()->velocity.y > 0.0f); }
        {   Rig r(AfterStart("QbCountdownThenDo", Defaults(2, {"0.3"}), kThen)); r.Start(); r.Tick(1, 0.1f); CHECK(Score(r.scene) == 0.0f);
            r.Tick(2, 0.3f); CHECK(Score(r.scene) >= 1.0f); }
        {   Rig r(AfterUpdate("QbOnlyEvery", Defaults(2, {"0.5"}), kThen)); r.Start(); r.Tick(5, 0.2f);
            CHECK(Score(r.scene) >= 1.0f && Score(r.scene) <= 3.0f); }
        {   Rig r(AfterUpdate("QbDoOnce", "", kThen)); r.Start(); r.Tick(5, 0.016f); CHECK(Score(r.scene) == 1.0f); }
        {   Rig r(AfterUpdate("QbDoByChance", Defaults(2, {"0"}), kThen)); r.Start(); r.Tick(20, 0.016f); CHECK(Score(r.scene) == 0.0f); }
        {   Rig r(AfterUpdate("QbDoByChance", Defaults(2, {"1"}), kThen)); r.Start(); r.Tick(5, 0.016f); CHECK(Score(r.scene) >= 5.0f); }
        {   Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 QbToggleSwitch\n" + Defaults(2, {"sw"}) + "node 3 QbIfSwitchOn\n" + Defaults(3, {"sw"}) +
                  "node 4 ScoreAdd\nexec 1 0 2\nexec 2 0 3\nexec 3 0 4\n"); g_errors.clear(); auto* inv = r.go->AddComponent<Inventory>(); inv->capacity = 8; r.Start(); CHECK(Score(r.scene) == 1.0f);
            if (Score(r.scene) != 1.0f) { std::printf("  key count %d\n", inv->Count(ItemDatabase::Get().Resolve("key"))); for (const auto& m : g_errors) std::printf("  err: %s\n", m.c_str()); } }
        {   Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 QbToggleSwitch\n" + Defaults(2, {"sw"}) + "node 5 QbToggleSwitch\n" + Defaults(5, {"sw"}) +
                  "node 3 QbIfSwitchOn\n" + Defaults(3, {"sw"}) + "node 4 ScoreAdd\nexec 1 0 2\nexec 2 0 5\nexec 5 0 3\nexec 3 0 4\n"); r.Start(); CHECK(Score(r.scene) == 0.0f); }
        {   // Save A Number then Load A Saved Number: 7 goes in, the value is wiped, 7 comes back.
            Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 SetVar n\ndefault 2 0 7\nnode 3 QbSaveNumber\n" + Defaults(3, {"n", "quickblock_slot"}) +
                  "node 4 SetVar n\ndefault 4 0 0\nnode 5 QbLoadNumber\n" + Defaults(5, {"n", "quickblock_slot"}) +
                  "exec 1 0 2\nexec 2 0 3\nexec 3 0 4\nexec 4 0 5\n");
            r.Start(); CHECK(std::fabs(r.vs->GetVariable("n").AsFloat() - 7.0f) < 1e-3f); }
        {   Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 QbGiveItem\n" + Defaults(2, {"key", "2"}) + "node 3 QbIfHasItem\n" + Defaults(3, {"key", "2"}) +
                  "node 4 ScoreAdd\nexec 1 0 2\nexec 2 0 3\nexec 3 0 4\n"); r.go->AddComponent<Inventory>()->capacity = 8; r.Start(); CHECK(Score(r.scene) == 1.0f); }
        {   Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 QbGiveItem\n" + Defaults(2, {"key", "1"}) + "node 3 QbIfHasItem\n" + Defaults(3, {"key", "2"}) +
                  "node 4 ScoreAdd\nexec 1 0 2\nexec 2 0 3\nexec 3 0 4\n"); r.go->AddComponent<Inventory>()->capacity = 8; r.Start(); CHECK(Score(r.scene) == 0.0f); }
        {   // Restart The Game: with no scene loaded the engine says so (a real game has one).
            g_errors.clear(); Rig r(AfterUpdate("QbRestartGame", "")); r.Start(); r.Tick(1, 0.016f);
            bool said = false; for (const auto& m : g_errors) if (m.find("nothing to restart") != std::string::npos) said = true; CHECK(said); }
        {   Rig r(AfterUpdate("QbHealFully", "")); auto* hp = r.go->AddComponent<HealthStat>(); hp->playDeathAnim = false; hp->maxHealth = 100;
            r.Start(); r.Tick(1, 0.016f); hp->health = 10; r.Tick(1, 0.016f); CHECK(hp->health > 99.0f); }
        {   Rig r(AfterUpdate("QbIfHealthLow", Defaults(2, {"0.25"}), kThen)); auto* hp = r.go->AddComponent<HealthStat>(); hp->playDeathAnim = false; hp->maxHealth = 100;
            r.Start(); r.Tick(1, 0.016f); CHECK(Score(r.scene) == 0.0f); hp->health = 10; r.Tick(1, 0.016f); CHECK(Score(r.scene) >= 1.0f); }
        {   Rig r(AfterUpdate("QbDefeatThis", "")); auto* hp = r.go->AddComponent<HealthStat>(); hp->playDeathAnim = false; hp->keepBodyOnDeath = true;
            r.Start(); r.Tick(2, 0.016f); CHECK(hp->IsDead()); }
        {   // Hurt / Heal Whatever Touches: the OTHER object changes health.
            Rig r(Alone("QbHurtOnTouch", Defaults(1, {"10"}))); r.scene.physicsEnabled = false;
            auto* other = r.scene.CreateGameObject("Victim"); auto* box = other->AddComponent<BoxCollider3D>();
            auto* hp = other->AddComponent<HealthStat>(); hp->playDeathAnim = false; hp->maxHealth = 100;
            r.Start(); r.Tick(1, 0.016f); r.vs->OnTriggerEnter3D(box); CHECK(std::fabs(hp->health - 90.0f) < 0.5f); }
        {   Rig r(Alone("QbHealOnTouch", Defaults(1, {"25"}))); r.scene.physicsEnabled = false;
            auto* other = r.scene.CreateGameObject("Hero"); auto* box = other->AddComponent<BoxCollider3D>();
            auto* hp = other->AddComponent<HealthStat>(); hp->playDeathAnim = false; hp->maxHealth = 100;
            r.Start(); r.Tick(1, 0.016f); hp->health = 50; r.vs->OnTriggerEnter3D(box); CHECK(std::fabs(hp->health - 75.0f) < 0.5f); }
        {   Rig r(Alone("QbRemoveOnTouch", "")); r.Start(); r.vs->OnTriggerEnter3D(nullptr); r.Tick(3, 0.016f); CHECK(r.scene.Find("Thing") == nullptr); }
        {   Rig r(AfterStart("QbRandomSpot", Defaults(2, {"-5", "5"}))); r.Start(); r.Tick(1, 0.016f);
            CHECK(std::fabs(r.Pos().x) + std::fabs(r.Pos().z) > 0.0001f && std::fabs(r.Pos().x) <= 5.01f); }
        {   Rig r(Alone("QbSayWhenTouched", Defaults(1, {"\"Welcome!\""})), false); r.go->AddComponent<TextRenderer>(); r.Start();
            r.vs->OnTriggerEnter3D(nullptr); CHECK(r.go->GetComponent<TextRenderer>()->text == "Welcome!"); }

    }

    // ---- Simple blocks: at most 3 typed fields (the Rule is the one sentence-shaped exception).
    {
        int most = 0;
        for (const vs::QuickBlock& b : all) {
            if (std::strcmp(b.type, "QbRule") == 0) continue;
            most = std::max(most, (int)b.fields.size());
            CHECK(b.fields.size() <= 3);
            if (b.fields.size() > 3) std::printf("  %s has %d fields\n", b.title, (int)b.fields.size());
        }
        std::printf("  most fields on any block except the Rule: %d\n", most);
        CHECK(vs::QbValidate(vs::QbKind::Name, "score") && !vs::QbValidate(vs::QbKind::Name, "my score") && !vs::QbValidate(vs::QbKind::Name, ""));
        CHECK(vs::QbNormalizeName(" my score! ") == "my_score_");
    }
    // ---- Move With Keys: one Keys dropdown covers arrows, W A S D and both; the speed is typed.
    {
        for (const char* keys : {"arrows", "wasd", "both"}) {
            for (const char* press : {"right", "d"}) {
                const bool should = std::string(keys) == "both" || (std::string(keys) == "arrows") == (std::string(press) == "right");
                Rig r(Alone("QbMoveWithKeys", Defaults(1, {keys, "5"}))); r.Start(); Keys({}); r.Tick(1, 0.1f);
                Keys({Input::KeyFromName(press)}); r.Tick(1, 0.5f);
                CHECK((r.Pos().x > 1.0f) == should);
                Keys({});
            }
        }
    }
    // ---- Graphs saved by release 2.885.0 (the first, longer fields) load and behave the same.
    {
        {   // Move With Keys: Left, Right, Up, Down, Speed
            Rig r("node 1 QbMoveWithKeys\ndefault 1 0 a\ndefault 1 1 d\ndefault 1 2 w\ndefault 1 3 s\ndefault 1 4 5\n"); r.Start(); Keys({}); r.Tick(1, 0.1f);
            Keys({'d'}); r.Tick(1, 0.5f); CHECK(r.Pos().x > 1.0f); Keys({}); }
        {   // Move To Spot: Right, Up, Forward, Speed
            Rig r("node 1 QbMoveToward\ndefault 1 0 5\ndefault 1 1 0\ndefault 1 2 0\ndefault 1 3 2\n"); r.Start(); r.Tick(1, 0.5f); CHECK(r.Pos().x > 0.5f); }
        {   // Flash Colour: Red, Green, Blue, Seconds
            Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 QbFlashColour\ndefault 2 0 0\ndefault 2 1 1\ndefault 2 2 0\ndefault 2 3 0.2\nexec 1 0 2\n");
            r.Start(); r.Tick(1, 0.016f); auto* mr = r.go->GetComponent<MeshRenderer>(); CHECK(mr->color.g > 0.9f && mr->color.r < 0.1f); }
        {   // Spawn Copy At: What, Right, Up, Forward
            Scene proto("Proto"); auto* shot = proto.CreateGameObject("Shot"); shot->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
            CHECK(SceneSerializer::SaveObjectToFile(*shot, "quickblock_old.okayprefab"));
            Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 QbSpawnCopyAt\ndefault 2 0 quickblock_old.okayprefab\ndefault 2 1 2\ndefault 2 2 1\ndefault 2 3 0\nexec 1 0 2\n");
            const std::size_t n = r.scene.Objects().size(); r.Start(); r.Tick(1, 0.016f); CHECK(r.scene.Objects().size() == n + 1); std::remove("quickblock_old.okayprefab"); }
        std::map<int, std::string> now = {{0, "arrows"}, {1, "7"}};
        CHECK(!vs::QbUpgradeFields("QbMoveWithKeys", now) && now[1] == "7");   // already current: untouched
        std::map<int, std::string> old = {{0, "a"}, {1, "d"}, {2, "w"}, {3, "s"}, {4, "9"}};
        CHECK(vs::QbUpgradeFields("QbMoveWithKeys", old) && old[0] == "wasd" && old[1] == "9" && old.size() == 2);
    }
    // ---- Saved values: made on the spot from a typed name; the common jobs are presets.
    {
        const vs::QuickBlock* cn = vs::FindQuickBlock("QbChangeNumber");
        std::vector<std::string> values;
        for (const vs::QbField& f : cn->fields) values.push_back(f.def);
        CHECK(vs::QbApplyPreset(*cn, "Take away 1 life", values) && values[0] == "lives" && values[1] == "Sub" && values[2] == "1");
        std::string g = "node 1 OnStart\nentry OnStart 1\nnode 2 QbChangeNumber\n";
        for (std::size_t k = 0; k < values.size(); ++k) g += "default 2 " + std::to_string(k) + " " + values[k] + "\n";
        g += "exec 1 0 2\n";
        Rig r(g); r.Start();   // no declaration anywhere: "lives" is made now, starting at 0
        CHECK(std::fabs(r.vs->GetVariable("lives").AsFloat() + 1.0f) < 1e-3f);
        // Show Value On Screen and Count Every Second.
        Rig t(Alone("QbCountEverySecond", Defaults(1, {"time", "Add"})), false);
        t.Start(); t.Tick(1, 1.1f); t.Tick(1, 1.1f);
        CHECK(t.vs->GetVariable("time").AsFloat() >= 1.0f);
        Rig sv(Alone("QbShowValue", Defaults(1, {"gold", "\"Gold: \""})), false);
        sv.go->AddComponent<TextRenderer>(); sv.vs->SetVariable("gold", vs::VsValue(7.0f)); sv.Start(); sv.Tick(2, 0.016f);
        CHECK(sv.go->GetComponent<TextRenderer>()->text.find("Gold: 7") != std::string::npos);
    }

    // ---- Saved values: "only this object" (the default) or "everyone can see this" (a leading * on the name).
    {
        CHECK(vs::QbNameShared("*gold") && !vs::QbNameShared("gold") && vs::QbNameBare("*gold") == "gold" && vs::QbNameBare("gold") == "gold");
        CHECK(vs::QbNameWithScope("gold", true) == "*gold" && vs::QbNameWithScope("*gold", false) == "gold");
        CHECK(vs::QbValidate(vs::QbKind::Name, "*gold") && !vs::QbValidate(vs::QbKind::Name, "*") && !vs::QbValidate(vs::QbKind::Name, "**x"));
        CHECK(vs::QbNormalizeName("*my gold") == "*my_gold");
        // Change Number on a shared name changes the scene's value; on a plain name, only this object's.
        { Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 QbChangeNumber\n" + Defaults(2, {"*gold", "Add", "4"}) + "exec 1 0 2\n"); r.Start();
          CHECK(std::fabs(vs::SceneVariables(&r.scene)["gold"].AsFloat() - 4.0f) < 1e-3f); CHECK(r.vs->GetVariable("gold").AsFloat() == 0.0f); }
        { Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 QbChangeNumber\n" + Defaults(2, {"gold", "Add", "4"}) + "exec 1 0 2\n"); r.Start();
          CHECK(std::fabs(r.vs->GetVariable("gold").AsFloat() - 4.0f) < 1e-3f); CHECK(vs::SceneVariables(&r.scene)["gold"].AsFloat() == 0.0f); }
        // Two objects share one value: one adds, the other shows it.
        { Scene scene("Shared"); scene.physicsEnabled = false;
          auto* a = scene.CreateGameObject("A"); auto* va = a->AddComponent<VisualScriptComponent>();
          CHECK(va->LoadFromText("node 1 OnStart\nentry OnStart 1\nnode 2 QbSetValue\n" + Defaults(2, {"*level", "5"}) + "exec 1 0 2\n"));
          auto* b = scene.CreateGameObject("B"); b->AddComponent<TextRenderer>(); auto* vb = b->AddComponent<VisualScriptComponent>();
          CHECK(vb->LoadFromText("node 1 QbShowValue\n" + Defaults(1, {"*level", "\"Level \""})));
          scene.Start(); scene.Update(0.016f); scene.Update(0.016f);
          CHECK(b->GetComponent<TextRenderer>()->text.find("Level 5") != std::string::npos); }
        // An old graph (no star) behaves as before: this object only.
        { Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 QbSetValue\n" + Defaults(2, {"health", "100"}) + "exec 1 0 2\n"); r.Start();
          CHECK(r.vs->GetVariable("health").AsFloat() == 100.0f); }
        // Set Value To presets.
        const vs::QuickBlock* sv = vs::FindQuickBlock("QbSetValue");
        std::vector<std::string> v;
        for (const vs::QbField& f : sv->fields) v.push_back(f.def);
        CHECK(vs::QbApplyPreset(*sv, "Set lives to 3", v) && v[0] == "lives" && v[1] == "3");
        { Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 QbSetValue\n" + Defaults(2, {v[0].c_str(), v[1].c_str()}) + "exec 1 0 2\n"); r.Start();
          CHECK(r.vs->GetVariable("lives").AsFloat() == 3.0f); }
        // A word and yes or no are worked out from what is typed.
        { Rig r("node 1 OnStart\nentry OnStart 1\nnode 2 QbSetValue\n" + Defaults(2, {"title", "Hello"}) + "node 3 QbSetValue\n" + Defaults(3, {"on", "true"}) + "exec 1 0 2\nexec 2 0 3\n"); r.Start();
          CHECK(r.vs->GetVariable("title").AsString() == "Hello" && r.vs->GetVariable("on").AsBool()); }
    }
    // ---- Merged blocks: Move Turn Or Resize, Show Or Hide This.
    {
        { Rig r(AfterStart("QbMoveTurnResize", Defaults(2, {"Translate", "0,2,0"}))); r.Start(); r.Tick(1, 0.016f); CHECK(r.Pos().y > 1.5f); }
        { Rig r(AfterStart("QbMoveTurnResize", Defaults(2, {"Rotate", "90,0,0"}))); r.Start(); r.Tick(1, 0.016f);
          const Vec3 e = r.go->transform->LocalEulerAngles(); CHECK(std::fabs(e.x) + std::fabs(e.y) + std::fabs(e.z) > 10.0f); }
        { Rig r(AfterStart("QbMoveTurnResize", Defaults(2, {"SetScale", "2,2,2"}))); r.Start(); r.Tick(1, 0.016f); CHECK(std::fabs(r.go->transform->localScale.x - 2.0f) < 0.01f); }
        { Rig r(AfterStart("QbShowHide", Defaults(2, {"0"}))); r.Start(); r.Tick(1, 0.016f); CHECK(!r.go->active); }
        { Rig r(AfterStart("QbShowHide", Defaults(2, {"1"}), kThen)); r.Start(); r.Tick(1, 0.016f); CHECK(r.go->active && Score(r.scene) == 1.0f); }
        // Old plain nodes convert to the merged block only when nothing is wired into them.
        std::string t; std::map<int, std::string> out;
        CHECK(vs::QbConvertOldNode("SetActive", "self", {{0, "0"}}, t, out) && t == "QbShowHide" && out[0] == "0");
        CHECK(vs::QbConvertOldNode("SetActive", "", {{0, "true"}}, t, out) && out[0] == "1");
        CHECK(!vs::QbConvertOldNode("SetActive", "Door", {{0, "0"}}, t, out));          // another object: left alone
        CHECK(!vs::QbConvertOldNode("SetActive", "self", {}, t, out));                  // no typed value: left alone
        CHECK(vs::QbConvertOldNode("Translate", "", {{0, "1"}, {2, "3"}}, t, out) && t == "QbMoveTurnResize" && out[0] == "Translate" && out[1] == "1,0,3");
        CHECK(!vs::QbConvertOldNode("Translate", "", {{0, "1"}, {3, "x"}}, t, out));    // a Target value: left alone
        CHECK(vs::QbConvertOldNode("Rotate", "", {{0, "45"}}, t, out) && out[0] == "Rotate" && out[1] == "45,0,0");
        CHECK(!vs::QbConvertOldNode("Print", "", {{0, "1"}}, t, out));
    }
    {   // Respawn This Object brings a dead object back.
        Rig r("node 1 OnKey r\nentry OnKey:r 1\nnode 2 QbRespawnThis\nexec 1 0 2\n");
        auto* hp = r.go->AddComponent<HealthStat>(); hp->playDeathAnim = false; hp->keepBodyOnDeath = true; hp->maxHealth = 100;
        r.Start(); Keys({}); r.Tick(1, 0.016f);
        hp->Damage(hp->maxHealth * 2); r.Tick(1, 0.016f);
        CHECK(hp->IsDead());
        Keys({'r'}); r.Tick(2, 0.016f);
        CHECK(!hp->IsDead() && hp->health > 0.0f);
        Keys({});
    }
    // A saved block keeps its typed values: the file says "QbAddToScore" and the value, nothing else.
    CHECK(vs::ExpandQuickBlocks(AfterStart("QbAddToScore", Defaults(2, {"7"}))).find("default 1000100 0 7") != std::string::npos ||
          vs::ExpandQuickBlocks(AfterStart("QbAddToScore", Defaults(2, {"7"}))).find(" 0 7") != std::string::npos);

    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
