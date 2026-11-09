// Play-mode tests of the play-test framework itself, run by okay-playtest. Each one builds
// a small scene in code, plays it headless with PlayTest and asserts on the result, the way
// a game's own play-tests would.
#include <Okay.hpp>
#include "okay/Testing/PlayTest.hpp"
#include "okay/Testing/Regression.hpp"
#include "okay/Testing/SceneValidator.hpp"
#include "okay/Testing/TestRegistry.hpp"

using namespace okay;
using namespace okay::testing;

namespace {

// An object that a script moves along +X at `speed` units per second.
GameObject* AddScriptedMover(Scene& scene, float speed = 2.0f, const char* name = "Mover") {
    GameObject* go = scene.CreateGameObject(name);
    auto* script = go->AddComponent<ScriptComponent>();
    std::string error;
    OKAY_REQUIRE(script->LoadSource("function update(dt) { move(" + std::to_string(speed) + " * dt, 0); }", &error));
    return go;
}

void AddCamera(Scene& scene) { scene.CreateGameObject("Main Camera")->AddComponent<Camera>(); }

// A ball dropped onto a static floor.
void AddBallAndFloor(Scene& scene) {
    GameObject* floor = scene.CreateGameObject("Floor");
    floor->transform->localPosition = {0, -1, 0};
    floor->AddComponent<BoxCollider2D>()->size = {20, 1};
    floor->AddComponent<Rigidbody2D>()->bodyType = Rigidbody2D::BodyType::Static;
    GameObject* ball = scene.CreateGameObject("Ball");
    ball->transform->localPosition = {0, 4, 0};
    ball->AddComponent<CircleCollider2D>()->radius = 0.5f;
    ball->AddComponent<Rigidbody2D>();
}

// Logs one error on its first update, like a script hitting a bad state.
struct Whiner : Component {
    void Update(float) override { if (!done) { Log::Error("whiner: something is wrong"); done = true; } }
    bool done = false;
};

} // namespace

OKAY_PLAYTEST_IN(selftest, script_moves_object) {
    PlayTest t;
    AddScriptedMover(t.scene());
    t.RunFrames(60);                                      // one second at 60 fps
    OKAY_EXPECT_NEAR(t.PositionOf("Mover").x, 2.0f, 1e-3);
    OKAY_EXPECT_NEAR(t.PositionOf("Mover").y, 0.0f, 1e-6);
    OKAY_EXPECT_EQ(t.Frame(), 60);
}

OKAY_PLAYTEST_IN(selftest, run_until_condition) {
    PlayTest t;
    AddScriptedMover(t.scene(), 4.0f);
    GameObject* mover = t.Require("Mover");
    OKAY_EXPECT(t.RunUntil([&] { return mover->transform->Position().x >= 3.0f; }, 2.0f));
    OKAY_EXPECT(t.SimulatedSeconds() > 0.7 && t.SimulatedSeconds() < 0.8);     // 3 units at 4 per second
    OKAY_EXPECT(!t.RunUntil([&] { return mover->transform->Position().x < 0.0f; }, 0.25f));   // never true: times out
}

OKAY_PLAYTEST_IN(selftest, rigidbody_falls_and_rests) {
    PlayTest t;
    AddBallAndFloor(t.scene());
    t.RunFrames(10);
    OKAY_EXPECT(t.PositionOf("Ball").y < 4.0f);           // falling
    t.RunSeconds(3.0f);
    // floor top is y = -0.5 and the ball has radius 0.5, so it settles near y = 0
    OKAY_EXPECT_NEAR(t.PositionOf("Ball").y, 0.0f, 0.1f);
}

OKAY_PLAYTEST_IN(selftest, input_drives_a_script) {
    PlayTest t;
    GameObject* go = t.scene().CreateGameObject("Player");
    std::string error;
    OKAY_REQUIRE(go->AddComponent<ScriptComponent>()->LoadSource(
        "function update(dt) { move(axis_x() * 3 * dt, axis_y() * 3 * dt); }", &error));
    t.SetMoveKeys({1, 0});                                // hold D
    t.RunFrames(30);
    OKAY_EXPECT_NEAR(t.PositionOf("Player").x, 1.5f, 1e-3);
    t.SetMoveKeys({0, 1});                                // switch to W
    t.RunFrames(30);
    OKAY_EXPECT_NEAR(t.PositionOf("Player").x, 1.5f, 1e-3);
    OKAY_EXPECT_NEAR(t.PositionOf("Player").y, 1.5f, 1e-3);
    t.ReleaseAll();
    t.RunFrames(30);
    OKAY_EXPECT_NEAR(t.PositionOf("Player").y, 1.5f, 1e-3);   // stopped
}

OKAY_PLAYTEST_IN(selftest, time_scale_slows_motion) {
    PlayTest t;
    AddScriptedMover(t.scene());
    t.SetTimeScale(0.5f);
    t.RunFrames(60);
    OKAY_EXPECT_NEAR(t.PositionOf("Mover").x, 1.0f, 1e-3);
    t.SetPaused(true);
    t.RunFrames(60);
    OKAY_EXPECT_NEAR(t.PositionOf("Mover").x, 1.0f, 1e-3);
}

OKAY_PLAYTEST_IN(selftest, scene_text_round_trip) {
    Scene source("Built in code");
    AddScriptedMover(source, 3.0f);
    const std::string text = SceneSerializer::Serialize(source);
    PlayTest t;
    OKAY_REQUIRE(t.LoadSceneText(text));
    t.RunFrames(60);
    OKAY_EXPECT_NEAR(t.PositionOf("Mover").x, 3.0f, 1e-3);     // the script travelled with the scene
}

OKAY_PLAYTEST_IN(selftest, runs_are_deterministic) {
    auto play = [] {
        PlayTest t;
        AddBallAndFloor(t.scene());
        AddScriptedMover(t.scene());
        t.RunFrames(90);
        return DigestScene(t.scene());
    };
    const SceneDigest a = play(), b = play();
    OKAY_EXPECT(a.hash == b.hash);
    OKAY_EXPECT_EQ(DiffDigests(a, b), std::string());
}

OKAY_PLAYTEST_IN(selftest, declared_errors_do_not_fail) {
    PlayTest t;
    t.scene().CreateGameObject("W")->AddComponent<Whiner>();
    t.ExpectError("something is wrong");                  // without this line the test would fail
    t.RunFrames(2);
    OKAY_EXPECT(t.LogContains("whiner"));
    OKAY_EXPECT(t.Problems().empty());
}

OKAY_PLAYTEST_IN(selftest, validated_scene_plays) {
    Scene source("Level");
    AddCamera(source);
    AddScriptedMover(source);
    ValidationReport report = SceneValidator::Validate(source);
    OKAY_EXPECT(!report.HasErrors());
    if (!report.Clean()) OKAY_NOTE(report.ToText());
    OKAY_EXPECT(report.Clean());
    PlayTest t;
    OKAY_REQUIRE(t.LoadSceneText(SceneSerializer::Serialize(source)));
    t.RunFrames(30);
    OKAY_EXPECT_NEAR(t.PositionOf("Mover").x, 1.0f, 1e-3);
}

// After one second the world must still look like the stored fingerprint. If a change to physics,
// scripting or transforms moves things on purpose, refresh it:  OKAY_UPDATE_GOLDEN=1 okay-playtest selftest.world
OKAY_PLAYTEST_IN(selftest, world_matches_golden) {
    PlayTest t;
    AddCamera(t.scene());
    AddScriptedMover(t.scene());
    t.RunFrames(60);
    const GoldenResult r = CheckSceneGolden(t.scene(), std::string(OKAY_SOURCE_DIR) + "/tests/golden/regression/mover_world.txt");
    if (!r.ok) OKAY_FAIL(r.message);
}
