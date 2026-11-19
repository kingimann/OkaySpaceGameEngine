// One-click recipes: each is small (node and wire budgets), loads clean, uses plain words, and really works when
// run in a headless scene (the object moves, jumps, scores, opens, follows, speaks, sounds, loses).
#include "test_framework.hpp"
#include "VisualScriptRecipes.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "VisualScriptTemplates.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Core/Time.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include <Okay.hpp>
#include <cmath>
#include <filesystem>
#include <string>

using namespace okay;
using namespace okay::editor::vsed;

namespace {
const VsRecipe& R(const char* name) {
    for (const VsRecipe& r : VsRecipes()) if (std::strcmp(r.name, name) == 0) return r;
    CHECK(false);
    return VsRecipes()[0];
}
VisualScriptComponent* Attach(Scene& scene, const VsRecipe& r) {
    auto* go = scene.CreateGameObject("Thing");
    go->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
    VsAddTemplateComponents(go, r.add);
    auto* vs = go->AddComponent<VisualScriptComponent>();
    std::string err;
    CHECK(vs->LoadFromText(r.graph, &err));
    if (!err.empty()) std::printf("  %s: %s\n", r.name, err.c_str());
    return vs;
}
void Keys(std::initializer_list<char> down) { Input::FeedKeys(std::vector<char>(down)); }
} // namespace

int main() {
#ifdef OKAY_SOURCE_DIR
    std::filesystem::current_path(OKAY_SOURCE_DIR);
#endif
    RUN_SUITE("vs_recipes");
    auto sink = okay::Log::sink;
    okay::Log::sink = [](okay::Log::Level, const std::string&) {};

    CHECK(VsRecipeCount() >= 6 && VsRecipeCount() <= 16);
    int totalNodes = 0, totalWires = 0, oldNodes = 0, oldWires = 0;
    for (const VsRecipe& r : VsRecipes()) {
        const int n = VsGraphNodeCount(r.graph), w = VsGraphWireCount(r.graph);
        CHECK(n <= r.maxNodes);
        CHECK(w <= r.maxWires);
        if (n > r.maxNodes || w > r.maxWires) std::printf("  over budget: %s has %d nodes, %d wires\n", r.name, n, w);
        CHECK(r.maxNodes <= 3 && r.maxWires <= 2);   // a recipe never needs more than 3 blocks and 2 wires
        CHECK(VsGlossaryViolation(r.name).empty());
        CHECK(VsGlossaryViolation(r.blurb).empty());
        CHECK(VsGlossaryViolation(r.needs).empty());
        CHECK(std::string(r.blurb).find("Example:") != std::string::npos);
        // Values are typed on the block: no separate number, text or "this object" blocks, no wire for them.
        for (const char* bad : {"Const", "MakeNumber", "MakeText", "Self"}) {
            std::size_t at = 0;
            while ((at = r.graph.find("node ", at)) != std::string::npos) {
                const std::size_t sp = r.graph.find(' ', at + 5), end = r.graph.find_first_of(" \n", sp + 1);
                CHECK(r.graph.substr(sp + 1, end - sp - 1) != bad);
                at = end;
            }
        }
        // Every wire fits: the checker reports no errors.
        for (const auto& is : okay::vs::ValidateGraphText(r.graph)) CHECK(is.severity != 0);
        int oldN = 0, oldW = 0;
        if (r.oldTemplate[0]) {
            const VsTemplate* t = VsTemplateNamed(r.oldTemplate);
            CHECK(t != nullptr);
            if (t) { oldN = VsGraphNodeCount(t->graph); oldW = VsGraphWireCount(t->graph); }
            // At least half as many nodes and wires as the long way.
            // Fewer than the long way always; the long ways of 6 or more blocks are cut by at least 40 percent.
            CHECK(n < oldN);
            CHECK(w < oldW);
            if (oldN >= 6) { CHECK(n * 10 <= oldN * 6); CHECK(w * 10 <= oldW * 6); }
            oldNodes += oldN; oldWires += oldW; totalNodes += n; totalWires += w;
        }
        std::printf("  %-28s %d nodes, %2d wires   (before: %s %d nodes, %d wires)\n", r.name, n, w,
                    r.oldTemplate[0] ? r.oldTemplate : "-", oldN, oldW);
    }
    std::printf("  compared recipes: %d nodes and %d wires now, %d nodes and %d wires before\n", totalNodes, totalWires, oldNodes, oldWires);

    // The longer templates that have no one-click recipe, for the counts note (docs/visual_script_category_audit.md).
    for (const char* name : {"Win or lose", "Health", "Spawner", "Shooter", "Score readout", "Message", "Camera follow", "Patrol", "Win at score", "Move with WASD", "Pickup"})
        if (const VsTemplate* t = VsTemplateNamed(name))
            std::printf("  template %-16s %2d nodes, %2d wires\n", name, VsGraphNodeCount(t->graph), VsGraphWireCount(t->graph));

    {   // Move with arrow keys: D slides it to the right, with no wire for the key or the speed.
        Scene scene("Move");
        auto* vs = Attach(scene, R("Move with arrow keys"));
        scene.Start();
        Keys({}); scene.Update(0.1f);
        Keys({Input::KeyFromName("right")}); scene.Update(0.5f);
        CHECK(vs->gameObject->transform->localPosition.x > 0.5f);
        Keys({});
    }
    {   // Jump with Space: the body gets an upward speed.
        Scene scene("Jump");
        auto* vs = Attach(scene, R("Jump with Space"));
        scene.Start();
        Keys({}); scene.Update(0.016f);
        Keys({' '}); scene.Update(0.016f);
        Keys({}); scene.Update(0.016f);
        auto* rb = vs->gameObject->GetComponent<Rigidbody3D>();
        CHECK(rb != nullptr);
        CHECK(vs->gameObject->transform->localPosition.y > 0.0f || (rb && rb->velocity.y > 0.0f));
    }
    {   // Collect coins: the score goes up by 1 and the coin hides.
        Scene scene("Coin");
        auto* vs = Attach(scene, R("Collect coins and score"));
        scene.Start();
        vs->OnTriggerEnter3D(nullptr);
        CHECK(okay::vs::SceneVariables(&scene)["score"].AsFloat() == 1.0f);
        CHECK(!vs->gameObject->active);
    }
    {   // Open a door: it moves up by 2.
        Scene scene("Door");
        auto* vs = Attach(scene, R("Open a door when touched"));
        scene.Start();
        vs->OnTriggerEnter3D(nullptr);
        CHECK(vs->gameObject->transform->localPosition.y > 1.5f);
    }
    {   // Enemy follows the player.
        Scene scene("Follow");
        auto* vs = Attach(scene, R("Enemy follows the player"));
        auto* player = scene.CreateGameObject("Player");
        player->transform->localPosition = {4, 0, 0};
        scene.Start();
        scene.Update(0.5f);
        CHECK(vs->gameObject->transform->localPosition.x > 0.5f);
    }
    {   // Say hello: the text says Hello!
        Scene scene("Hello");
        auto* vs = Attach(scene, R("Say hello"));
        scene.Start();
        auto* tr = vs->gameObject->GetComponent<TextRenderer>();
        CHECK(tr != nullptr);
        CHECK(tr && tr->text == "Hello!");
    }
    {   // Play a sound on click.
        Scene scene("Click");
        auto* vs = Attach(scene, R("Play a sound on click"));
        scene.Start();
        auto* au = vs->gameObject->GetComponent<AudioSource>();
        CHECK(au != nullptr);
        CHECK(au && !au->IsPlaying());
        vs->OnMouseDown();
        CHECK(au && au->IsPlaying());
    }
    {   // Game over: nothing at full health, You lose at 0 health.
        Time::SetTimeScale(1.0f);
        Scene scene("Over");
        auto* vs = Attach(scene, R("Game over when health is 0"));
        auto* hp = vs->gameObject->GetComponent<HealthStat>();
        CHECK(hp != nullptr);
        scene.Start();
        scene.Update(0.016f);
        CHECK(Time::TimeScale() == 1.0f);
        if (hp) hp->Damage(hp->maxHealth * 2);
        scene.Update(0.016f);
        CHECK(Time::TimeScale() == 0.0f);
        CHECK(okay::vs::SceneVariables(&scene)["outcome"].AsString() == "lose");
        Time::SetTimeScale(1.0f);
    }
    {   // Spawn a copy when I click: one more object in the scene.
        {
            Scene proto("Proto");
            auto* shot = proto.CreateGameObject("Marker");
            shot->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
            std::filesystem::create_directories("examples/node_playground");
            if (!std::filesystem::exists("examples/node_playground/marker.okayprefab"))
                CHECK(SceneSerializer::SaveObjectToFile(*shot, "examples/node_playground/marker.okayprefab"));
        }
        Scene scene("Click");
        auto* vs = Attach(scene, R("Spawn a copy when I click"));
        scene.Start();
        const std::size_t before = scene.Objects().size();
        vs->OnMouseDown();
        scene.Update(0.016f);
        CHECK(scene.Objects().size() == before + 1);
    }
    {   // Change scene when touched: a scene load is asked for.
        Scene scene("Door");
        auto* vs = Attach(scene, R("Change scene when touched"));
        scene.Start();
        CHECK(!scene.HasPendingLoad());
        vs->OnTriggerEnter3D(nullptr);
        CHECK(scene.HasPendingLoad());
    }
    {   // Lava that hurts / Heart that heals: the OTHER object's health changes.
        for (int heal = 0; heal < 2; ++heal) {
            Scene scene("Touch");
            scene.physicsEnabled = false;
            auto* vs = Attach(scene, R(heal ? "Heart that heals" : "Lava that hurts"));
            auto* other = scene.CreateGameObject("Hero");
            auto* box = other->AddComponent<BoxCollider3D>();
            auto* hp = other->AddComponent<HealthStat>(); hp->playDeathAnim = false; hp->maxHealth = 100;
            scene.Start(); scene.Update(0.016f);
            if (heal) hp->health = 50;
            vs->OnTriggerEnter3D(box);
            CHECK(heal ? std::fabs(hp->health - 75.0f) < 0.5f : std::fabs(hp->health - 90.0f) < 0.5f);
        }
    }
    {   // Guard walks back and forth, Enemy runs away, Bobbing coin.
        Scene a("Guard"); a.physicsEnabled = false;
        auto* g = Attach(a, R("Guard walks back and forth")); a.Start(); a.Update(0.4f);
        CHECK(std::fabs(g->gameObject->transform->localPosition.x) > 0.05f);
        Scene b("Run"); b.physicsEnabled = false;
        auto* r2 = Attach(b, R("Enemy runs away")); auto* pl = b.CreateGameObject("Player"); pl->transform->localPosition = {2, 0, 0};
        b.Start(); b.Update(0.3f); b.Update(0.3f);
        CHECK(r2->gameObject->transform->localPosition.x < -0.3f);
        Scene c("Float"); c.physicsEnabled = false;
        auto* f = Attach(c, R("Bobbing coin")); c.Start(); c.Update(0.4f);
        CHECK(std::fabs(f->gameObject->transform->localPosition.y) > 0.02f);
    }
    {   // The small complete game: counts, then play it.
        int nodes = 0, wires = 0;
        for (const VsSampleScript& sc : VsSampleGame()) {
            nodes += VsGraphNodeCount(sc.graph); wires += VsGraphWireCount(sc.graph);
        }
        std::printf("  small game (4 scripts): %d blocks, %d wires\n", nodes, wires);
        CHECK(nodes <= 6 && wires <= 5);   // the owner's target: 6 blocks and 5 wires
        Time::SetTimeScale(1.0f);
        Scene scene("Game");
        scene.physicsEnabled = false;
        auto make = [&](const char* name, const std::string& graph, Vec3 at) {
            auto* go = scene.CreateGameObject(name);
            go->transform->localPosition = at;
            go->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
            return go;
        };
        const auto& game = VsSampleGame();
        auto* player = make("Player", game[0].graph, {0, 0, 0});
        auto* coin = make("Coin", game[1].graph, {3, 0, 0});
        auto* enemy = make("Enemy", game[2].graph, {0, 5, 0});
        auto* board = make("Scoreboard", game[3].graph, {0, 8, 0});
        board->AddComponent<TextRenderer>();
        coin->AddComponent<AudioSource>();
        player->AddComponent<VisualScriptComponent>()->LoadFromText(game[0].graph);
        auto* cvs = coin->AddComponent<VisualScriptComponent>(); cvs->LoadFromText(game[1].graph);
        auto* evs = enemy->AddComponent<VisualScriptComponent>(); evs->LoadFromText(game[2].graph);
        board->AddComponent<VisualScriptComponent>()->LoadFromText(game[3].graph);
        scene.Start();
        Keys({Input::KeyFromName("right")}); scene.Update(0.5f); Keys({});
        CHECK(player->transform->localPosition.x > 1.0f);           // the player moved
        for (int i = 0; i < 4; ++i) scene.Update(0.3f);
        cvs->OnTriggerEnter3D(nullptr); scene.Update(0.016f);       // the coin is touched
        CHECK(vs::SceneVariables(&scene)["score"].AsFloat() == 1.0f && !coin->active);
        CHECK(board->GetComponent<TextRenderer>()->text.find("Score: 1") != std::string::npos);
        CHECK(std::fabs(enemy->transform->localPosition.x) > 0.05f); // the enemy walked
        CHECK(Time::TimeScale() == 1.0f);
        evs->OnTriggerEnter3D(nullptr);                              // the enemy touches the player
        CHECK(Time::TimeScale() == 0.0f);
        CHECK(vs::SceneVariables(&scene)["outcome"].AsString() == "lose");
        Time::SetTimeScale(1.0f);
    }
    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
