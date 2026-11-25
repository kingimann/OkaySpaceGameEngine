// module:particles — overlay line, asset scan and scene template from the particles module.
#include "test_framework.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Modules/ModuleMenus.hpp"
#include "okay/Render/RenderContributions.hpp"
#include "okay/Core/DebugOverlay.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include <iostream>
#include <string>

using namespace okay;

static bool HasLine(const std::vector<std::string>& lines, const std::string& s) {
    for (const std::string& l : lines) if (l == s) return true;
    return false;
}

int main() {
    RUN_SUITE("module:particles");
    ModuleRegistry::Get();
    Scene scene("fx");
    ParticleSystem* ps = scene.CreateGameObject("Spark")->AddComponent<ParticleSystem>();
    ps->texture = "spark.png";
    auto lines = RenderContributionLines(&scene);
    CHECK(HasLine(lines, "Particles: 1"));
    auto overlay = DebugOverlay::BuildLines(DebugOverlay::Capture(&scene), false, true, &scene);
    CHECK(HasLine(overlay, "Particles: 1"));
    std::vector<ModuleAssetNote> notes;
    CollectModuleAssets(*ps->gameObject, notes);
    CHECK(notes.size() == 1 && notes[0].path == "spark.png");
    const SceneTemplateEntry* tpl = FindSceneTemplate("particle_system");
    CHECK(tpl && tpl->build);
    Scene built("built");
    tpl->build(built);
    CHECK(built.Find("Particles") && built.Find("Particles")->GetComponent<ParticleSystem>());
    std::cout << "particles on: overlay \"" << lines[0] << "\", texture " << notes[0].path
              << ", template " << tpl->id << "\n";

    ModuleManifest saved = ModuleRegistry::Get().Find("particles")->manifest;
    ModuleRegistry::Get().Add(saved, false);
    CHECK(!HasLine(RenderContributionLines(&scene), "Particles: 1"));
    CHECK(FindSceneTemplate("particle_system") == nullptr);
    notes.clear();
    CollectModuleAssets(*ps->gameObject, notes);
    CHECK(notes.empty());
    auto offLines = RenderContributionLines(&scene);
    std::cout << "particles off: overlay lines " << offLines.size()
              << ", template " << (FindSceneTemplate("particle_system") ? "yes" : "no")
              << ", texture notes " << notes.size() << "\n";
    ModuleRegistry::Get().Add(saved, true);
    TEST_MAIN_RESULT();
}
