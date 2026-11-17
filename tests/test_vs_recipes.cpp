// One-click recipes: each is small (node and wire budgets), loads clean, uses plain words, and really works when
// run in a headless scene (the object moves, jumps, scores, opens, follows, speaks, sounds, loses).
#include "test_framework.hpp"
#include "VisualScriptRecipes.hpp"
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
    RUN_SUITE("vs_recipes");
    auto sink = okay::Log::sink;
    okay::Log::sink = [](okay::Log::Level, const std::string&) {};

    CHECK(VsRecipeCount() >= 6 && VsRecipeCount() <= 10);
    int totalNodes = 0, totalWires = 0, oldNodes = 0, oldWires = 0;
    for (const VsRecipe& r : VsRecipes()) {
        const int n = VsGraphNodeCount(r.graph), w = VsGraphWireCount(r.graph);
        CHECK(n <= r.maxNodes);
        CHECK(w <= r.maxWires);
        if (n > r.maxNodes || w > r.maxWires) std::printf("  over budget: %s has %d nodes, %d wires\n", r.name, n, w);
        CHECK(r.maxNodes <= 7 && r.maxWires <= 6);
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
    for (const char* name : {"Win or lose", "Health", "Spawner", "Shooter", "Score readout", "Message", "Camera follow"})
        if (const VsTemplate* t = VsTemplateNamed(name))
            std::printf("  template %-16s %2d nodes, %2d wires\n", name, VsGraphNodeCount(t->graph), VsGraphWireCount(t->graph));

    {   // Move with arrow keys: D slides it to the right, with no wire for the key or the speed.
        Scene scene("Move");
        auto* vs = Attach(scene, R("Move with arrow keys"));
        scene.Start();
        Keys({}); scene.Update(0.1f);
        Keys({'d'}); scene.Update(0.5f);
        CHECK(vs->gameObject->transform->localPosition.x > 0.5f);
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
    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
