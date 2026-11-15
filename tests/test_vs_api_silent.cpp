// The four engine-API nodes the registry-effect test lists as silent in a headless scene snapshot
// (kKnownSilentBindings in test_vs_registry_effect.cpp): Input.DefineAction, Input.DefineDirection,
// Audio.SaveSettings and Rigidbody.AddTorque. Their effects live outside the scene snapshot (the input map, Prefs,
// the physics body's spin after a step), so this test runs each one as a node in a VisualScriptComponent graph and
// checks that effect directly.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/InputActions.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace okay;

int main() {
    RUN_SUITE("vs_api_silent");
    Prefs::Clear();
    CHECK(!InputActions::Has("Dash"));
    CHECK(!InputActions::Has("Steer"));
    AudioMixer::masterVolume = 0.37f;

    Scene s("api");
    s.physicsEnabled = true;
    GameObject* body = s.CreateGameObject("Spinner");
    auto* rb = body->AddComponent<Rigidbody3D>();
    rb->gravityScale = 0.0f;
    rb->freezeRotation = false;   // on by default, and then a torque does nothing (the node's help says so)
    body->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    auto* vc = body->AddComponent<VisualScriptComponent>();
    const char* graph =
        "node 1 OnStart\n"
        "node 2 Input.DefineAction/3\n"
        "node 3 Const \"Dash\"\n"
        "node 4 Const \"key:q\"\n"
        "node 5 Const \"pad:rs\"\n"
        "node 6 Input.DefineDirection/3\n"
        "node 7 Const \"Steer\"\n"
        "node 8 Const \"keys:i,k,j,l\"\n"
        "node 9 Const \"stick:right\"\n"
        "node 10 Audio.SaveSettings/0\n"
        "node 11 Rigidbody.AddTorque/1\n"
        "node 12 Vector3.up\n"
        "exec 1 0 2\nexec 2 0 6\nexec 6 0 10\nexec 10 0 11\n"
        "data 2 0 3 0\ndata 2 1 4 0\ndata 2 2 5 0\n"
        "data 6 0 7 0\ndata 6 1 8 0\ndata 6 2 9 0\n"
        "data 11 0 12 0\n"
        "entry OnStart 1\n";
    std::string err;
    CHECK(vc->LoadFromText(graph, &err));
    if (!err.empty()) std::printf("  graph: %s\n", err.c_str());
    s.Start();
    for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);

    // Input.DefineAction: a button action with both bindings
    CHECK(InputActions::Has("Dash"));
    if (InputActions::Action* a = InputActions::Find("Dash")) {
        CHECK(!a->direction);
        CHECK(a->bindings.size() == 2);
    }
    // Input.DefineDirection: a direction action with both bindings
    CHECK(InputActions::Has("Steer"));
    if (InputActions::Action* a = InputActions::Find("Steer")) {
        CHECK(a->direction);
        CHECK(a->bindings.size() == 2);
    }
    // Audio.SaveSettings: the mixer's volume is in Prefs
    CHECK(Prefs::Has("audio.master"));
    CHECK_NEAR(Prefs::GetFloat("audio.master", -1.0f), 0.37f, 1e-5f);
    // Rigidbody.AddTorque: after a physics step the body spins about +Y
    std::printf("  angular velocity after the torque: %.4f %.4f %.4f\n", rb->angularVelocity.x, rb->angularVelocity.y, rb->angularVelocity.z);
    CHECK(rb->angularVelocity.y > 1e-4f);
    CHECK(std::fabs(rb->angularVelocity.x) < 1e-4f && std::fabs(rb->angularVelocity.z) < 1e-4f);
    AudioMixer::masterVolume = 1.0f;
    Prefs::Clear();
    InputActions::Restore();                                              // the graph defined Dash and Steer: put the defaults back
    CHECK(!InputActions::Has("Dash") && !InputActions::Has("Steer"));
    TEST_MAIN_RESULT();
}
