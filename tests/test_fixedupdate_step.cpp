// FixedUpdate follows the physics step (issue #888, Finding 84), and component hotkeys stay
// quiet while a text field has focus (Finding 68, the part not covered by the input upgrade).
//
// 84: a script's FixedUpdate() and the visual-script On Physics Update must run once per
//     physics step, with the step's length as their delta time, so a force added there is
//     the same strength at any Physics Rate and any frame rate.
// 68: typing into a focused UIInputField must not toggle the flashlight, the grid bag, the
//     crafting menu or the quest journal, must not make the First / Third Person player jump,
//     and the Esc that leaves the field must not also pause the game.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace okay;

namespace {

const char* kCounterScript =
    "int fixedSteps = 0;\n"
    "float fixedDt = 0;\n"
    "float fixedStepSetting = 0;\n"
    "void FixedUpdate() { fixedSteps++; fixedDt = Time.deltaTime; fixedStepSetting = Time.fixedDeltaTime; }\n";

const char* kCounterGraph =
    "node 0 OnFixedUpdate\nnode 1 AddVar steps\nnode 2 Const 1\nnode 3 DeltaTime\nnode 4 SetVar dt\n"
    "data 1 0 2 0\ndata 4 0 3 0\nexec 0 0 1\nexec 1 0 4\nentry OnFixedUpdate 0\n";

// One scene with a text script and a visual script that count their fixed calls.
struct World {
    Scene scene{"Fixed"};
    ScriptComponent* sc = nullptr;
    VisualScriptComponent* vs = nullptr;
    int physicsSteps = 0;   // what the physics step loop reported, summed over the frames

    explicit World(float physicsHz) {
        scene.UseFixedStepPhysics(true);
        scene.SetPhysicsStepsPerSecond(physicsHz);
        GameObject* a = scene.CreateGameObject("Script");
        sc = a->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        const bool okScript = sc->LoadSource(kCounterScript, &err);
        CHECK(okScript);
        if (!okScript) std::cerr << "  script error: " << err << "\n";
        GameObject* b = scene.CreateGameObject("Graph");
        vs = b->AddComponent<VisualScriptComponent>();
        const bool okGraph = vs->LoadFromText(kCounterGraph, &err);
        CHECK(okGraph);
        if (!okGraph) std::cerr << "  graph error: " << err << "\n";
        scene.Start();
    }
    int ScriptSteps() { return (int)std::lround(sc->VM()->GetGlobal("fixedSteps").AsFloat()); }
    float ScriptDt() { return sc->VM()->GetGlobal("fixedDt").AsFloat(); }
    float ScriptFixedSetting() { return sc->VM()->GetGlobal("fixedStepSetting").AsFloat(); }
    int GraphSteps() { return (int)std::lround(vs->GetVariable("steps").AsFloat()); }
    float GraphDt() { return vs->GetVariable("dt").AsFloat(); }
    void Frame(float dt) { scene.Update(dt); physicsSteps += scene.PhysicsStepsLastFrame(); }
};

void PressKey(Scene& s, char key) {
    Input::FeedKeys({key});
    s.Update(1.0f / 60.0f);
    Input::ClearTypedText();   // the player clears the typed text after each frame
    Input::FeedKeys({});
    s.Update(1.0f / 60.0f);
}

} // namespace

int main() {
    RUN_SUITE("fixedupdate_step");

    // ---- 30 fps with a 60 Hz physics step: two fixed calls a frame, dt = 1/60 ------------------
    {
        World w(60.0f);
        int framesWithTwo = 0, framesMatching = 0;
        const int frames = 30;
        for (int i = 0; i < frames; ++i) {
            const int s0 = w.ScriptSteps(), g0 = w.GraphSteps();
            w.Frame(1.0f / 30.0f);
            const int ds = w.ScriptSteps() - s0, dg = w.GraphSteps() - g0;
            if (ds == 2 && dg == 2) ++framesWithTwo;
            if (ds == w.scene.PhysicsStepsLastFrame() && dg == w.scene.PhysicsStepsLastFrame()) ++framesMatching;
        }
        std::cout << "  30 fps @ 60 Hz: script " << w.ScriptSteps() << ", graph " << w.GraphSteps()
                  << ", physics steps " << w.physicsSteps << " over " << frames << " frames; frames with 2 calls: "
                  << framesWithTwo << "; script dt " << w.ScriptDt() << ", graph dt " << w.GraphDt()
                  << ", Time.fixedDeltaTime " << w.ScriptFixedSetting() << "\n";
        CHECK(framesWithTwo >= frames - 2);              // float rounding may move one step a frame later
        CHECK(framesMatching == frames);                 // every frame: one call per physics step
        CHECK(std::abs(w.ScriptSteps() - 2 * frames) <= 1);
        CHECK(std::abs(w.GraphSteps() - 2 * frames) <= 1);
        CHECK_NEAR(w.ScriptDt(), 1.0f / 60.0f, 1e-6f);
        CHECK_NEAR(w.GraphDt(), 1.0f / 60.0f, 1e-6f);
        CHECK_NEAR(w.ScriptFixedSetting(), 1.0f / 60.0f, 1e-6f);   // Time.fixedDeltaTime follows the rate
    }

    // ---- 240 fps with a 60 Hz step: a fixed call about every 4th frame --------------------------
    {
        World w(60.0f);
        int framesWithOne = 0, framesWithMore = 0, framesMatching = 0;
        const int frames = 240;
        for (int i = 0; i < frames; ++i) {
            const int s0 = w.ScriptSteps(), g0 = w.GraphSteps();
            w.Frame(1.0f / 240.0f);
            const int ds = w.ScriptSteps() - s0, dg = w.GraphSteps() - g0;
            if (ds == 1) ++framesWithOne;
            if (ds > 1) ++framesWithMore;
            if (ds == w.scene.PhysicsStepsLastFrame() && dg == w.scene.PhysicsStepsLastFrame()) ++framesMatching;
        }
        std::cout << "  240 fps @ 60 Hz: script " << w.ScriptSteps() << ", graph " << w.GraphSteps()
                  << ", physics steps " << w.physicsSteps << " over " << frames << " frames (1 s)\n";
        CHECK(framesMatching == frames);
        CHECK(framesWithMore == 0);
        CHECK(std::abs(framesWithOne - frames / 4) <= 1);   // about every 4th frame
        CHECK(std::abs(w.ScriptSteps() - 60) <= 1);
        CHECK(std::abs(w.GraphSteps() - 60) <= 1);
    }

    // ---- over one second the fixed calls equal the physics steps, at several rates ---------------
    {
        const float rates[] = {25.0f, 50.0f, 100.0f};
        const float fpsList[] = {20.0f, 60.0f, 144.0f};
        for (float hz : rates)
            for (float fps : fpsList) {
                World w(hz);
                const int frames = (int)fps;
                for (int i = 0; i < frames; ++i) w.Frame(1.0f / fps);
                std::cout << "  " << fps << " fps @ " << hz << " Hz over 1 s: script " << w.ScriptSteps()
                          << ", graph " << w.GraphSteps() << ", physics steps " << w.physicsSteps << "\n";
                CHECK(w.ScriptSteps() == w.physicsSteps);
                CHECK(w.GraphSteps() == w.physicsSteps);
                CHECK(std::abs(w.physicsSteps - (int)hz) <= 1);
                CHECK_NEAR(w.ScriptDt(), 1.0f / hz, 1e-6f);
                CHECK_NEAR(w.GraphDt(), 1.0f / hz, 1e-6f);
            }
    }

    // ---- a force added in FixedUpdate is the same strength at any Physics Rate ------------------
    {
        const float rates[] = {25.0f, 50.0f, 100.0f};
        for (float hz : rates) {
            Scene s("Force");
            s.UseFixedStepPhysics(true);
            s.SetPhysicsStepsPerSecond(hz);
            GameObject* ball = s.CreateGameObject("Ball");
            ball->AddComponent<SphereCollider3D>()->radius = 0.5f;
            auto* rb = ball->AddComponent<Rigidbody3D>();
            rb->mass = 1.0f;
            rb->gravityScale = 0.0f;
            auto* sc = ball->AddComponent<ScriptComponent>("okayscript");
            std::string err;
            CHECK(sc->LoadSource("void FixedUpdate() { add_force(10, 0, 0); }\n", &err));
            if (!err.empty()) std::cerr << "  script error: " << err << "\n";
            s.Start();
            for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);   // 2 s
            std::cout << "  push 10 N for 2 s @ " << hz << " Hz: vel.x " << rb->velocity.x << " (20 expected)\n";
            CHECK(std::abs(rb->velocity.x - 20.0f) < 0.6f);
        }
    }

    // ---- the shipped example (docs/try_it.md): keys 1 / 2 / 3 pick 25 / 50 / 100 steps a second ----
    {
        std::vector<std::string> logs;
        Log::sink = [&](Log::Level, const std::string& m) { logs.push_back(m); };
        const char keys[] = {'1', '2', '3'};
        const int rates[] = {25, 50, 100};
        for (int k = 0; k < 3; ++k) {
            Scene s("Example");
            const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/physics/fixed_step_push.okayscene";
            CHECK(SceneSerializer::LoadFromFile(s, path));
            s.UseFixedStepPhysics(true);   // what Play does
            s.Start();
            logs.clear();
            PressKey(s, keys[k]);           // restarts the ball at this rate
            for (int i = 0; i < 200; ++i) s.Update(1.0f / 60.0f);   // a bit over 3 s
            // "after 2 s: speed 19.8 (expected 20) at 100 steps a second": within a step or a frame of 20.
            const std::string want = "after 2 s: speed ";
            bool saw = false;
            for (const auto& l : logs) {
                if (l.find("after ") != std::string::npos) std::cout << "  example @ " << rates[k] << " Hz: " << l << "\n";
                const auto at = l.find(want);
                if (at == std::string::npos || l.find(std::to_string(rates[k]) + " steps") == std::string::npos) continue;
                const float speed = std::strtof(l.c_str() + at + want.size(), nullptr);
                if (std::abs(speed - 20.0f) <= 0.7f) saw = true;
            }
            CHECK(saw);
        }
        Log::sink = nullptr;
    }

    // ---- with physics off (fixedTimeStep 0) FixedUpdate runs once a frame, with the frame's dt ---
    {
        World w(60.0f);
        w.scene.UseFixedStepPhysics(false);
        for (int i = 0; i < 10; ++i) w.Frame(0.05f);
        CHECK(w.ScriptSteps() == 10);
        CHECK(w.GraphSteps() == 10);
        CHECK_NEAR(w.ScriptDt(), 0.05f, 1e-6f);
    }

    // ---- 68: component hotkeys while a text field has focus --------------------------------------
    {
        Game::SetPaused(false);
        Scene s("Typing");
        GameObject* player = s.CreateGameObject("Player");
        auto* torch = player->AddComponent<Flashlight>();
        auto* bag = player->AddComponent<GridInventory>();
        auto* craft = player->AddComponent<CraftingMenu>();
        auto* quests = player->AddComponent<QuestLog>();
        quests->drawUI = false;
        s.CreateGameObject("Pause")->AddComponent<PauseMenu>();
        GameObject* chat = s.CreateGameObject("Chat");
        auto* field = chat->AddComponent<UIInputField>();
        s.Start();
        s.Update(1.0f / 60.0f);

        const bool torch0 = torch->on, bag0 = bag->open, craft0 = craft->open, journal0 = quests->journalOpen;
        field->focused = true;
        for (char c : std::string("find kit ij")) {
            Input::FeedText(std::string(1, c));
            PressKey(s, c);
        }
        std::cout << "  typed into chat: '" << field->text << "'; torch " << torch0 << "->" << torch->on
                  << ", bag " << bag0 << "->" << bag->open << ", craft " << craft0 << "->" << craft->open
                  << ", journal " << journal0 << "->" << quests->journalOpen << "\n";
        CHECK(torch->on == torch0);
        CHECK(bag->open == bag0);
        CHECK(craft->open == craft0);
        CHECK(quests->journalOpen == journal0);

        // Esc leaves the field and does not also pause, whichever of the two updates first.
        PressKey(s, (char)27);
        std::cout << "  Esc: field focused " << field->focused << ", paused " << Game::Paused() << "\n";
        CHECK(!field->focused);
        CHECK(!Game::Paused());
        Game::SetPaused(false);

        // Not typing: the same keys work again.
        const bool torch1 = torch->on, bag1 = bag->open;
        PressKey(s, 'f');
        CHECK(torch->on != torch1);
        PressKey(s, 'i');
        CHECK(bag->open != bag1);
        Game::SetPaused(false);
        Input::ClearTypedText();
    }
    {   // the pause menu updating before the field (created first)
        Game::SetPaused(false);
        Scene s("TypingOrder");
        s.CreateGameObject("Pause")->AddComponent<PauseMenu>();
        auto* field = s.CreateGameObject("Chat")->AddComponent<UIInputField>();
        s.Start();
        s.Update(1.0f / 60.0f);
        field->focused = true;
        PressKey(s, (char)27);
        CHECK(!field->focused);
        CHECK(!Game::Paused());
        PressKey(s, (char)27);   // the next Esc, with no field focused, pauses
        CHECK(Game::Paused());
        Game::SetPaused(false);
    }
    {   // the field updating before the pause menu
        Game::SetPaused(false);
        Scene s("TypingOrder2");
        auto* field = s.CreateGameObject("Chat")->AddComponent<UIInputField>();
        s.CreateGameObject("Pause")->AddComponent<PauseMenu>();
        s.Start();
        s.Update(1.0f / 60.0f);
        field->focused = true;
        PressKey(s, (char)27);
        CHECK(!field->focused);
        CHECK(!Game::Paused());
        Game::SetPaused(false);
    }
    {   // First / Third Person players: no jump while UI owns the keys (the player sets UICaptured
        // from "a text field has focus" every frame)
        Scene s("Jump");
        auto* fp = s.CreateGameObject("FP")->AddComponent<FirstPersonController>();
        auto* tp = s.CreateGameObject("TP")->AddComponent<ThirdPersonController>();
        Input::FeedKeys({});
        Input::FeedKeys({' '});
        Input::SetUICaptured(true);
        const PlayerInput a = fp->ReadLocalInput();
        const PlayerInput b = tp->ReadLocalInput();
        CHECK((a.pressed & PlayerInput::Jump) == 0);
        CHECK((b.pressed & PlayerInput::Jump) == 0);
        CHECK((a.held & PlayerInput::Jump) == 0);
        Input::SetUICaptured(false);
        const PlayerInput c = fp->ReadLocalInput();
        CHECK((c.held & PlayerInput::Jump) != 0);   // not typing: Space still jumps
        Input::FeedKeys({});
    }

    TEST_MAIN_RESULT();
}
