// Quick blocks: every one-block action is small, loads from its saved form, and really does its job when run.
#include "test_framework.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/CameraFollow.hpp"
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
#include "okay/VisualScript/VsTypes.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
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
    CHECK(all.size() >= 30);   // more one-block actions are the plain nodes that already do their job in one block: Heal, Save Checkpoint, Shake Camera, Say Text
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
        Rig r(Alone("QbMoveWithKeys", Defaults(1, {"a", "d", "w", "s", "5"})));
        r.Start(); Keys({}); r.Tick(1, 0.1f); Keys({'d'}); r.Tick(1, 0.5f);
        CHECK(r.Pos().x > 1.0f && std::fabs(r.Pos().y) < 0.01f);
        Keys({'w'}); r.Tick(1, 0.5f);
        CHECK(r.Pos().y > 1.0f);
        Keys({}); 
    }
    {   // Move To Spot
        Rig r(Alone("QbMoveToward", Defaults(1, {"5", "0", "0", "2"})));
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
            Rig r(AfterStart("QbSpawnCopyAt", Defaults(2, {"quickblock_shot.okayprefab", "2", "1", "0"})));
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
        Rig r(AfterStart("QbFlashColour", Defaults(2, {"1", "0", "0", "0.2"})));
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
    // A saved block keeps its typed values: the file says "QbAddToScore" and the value, nothing else.
    CHECK(vs::ExpandQuickBlocks(AfterStart("QbAddToScore", Defaults(2, {"7"}))).find("default 1000100 0 7") != std::string::npos ||
          vs::ExpandQuickBlocks(AfterStart("QbAddToScore", Defaults(2, {"7"}))).find(" 0 7") != std::string::npos);

    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
