// Particles module: a debug-overlay line, the scene-validator texture scan, and a
// one-object scene template. The overlay is appended by DebugOverlay::BuildLines.
// The texture scan used to be a hard-coded loop in SceneValidator.
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Modules/ModuleMenus.hpp"
#include "okay/Render/RenderContributions.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Scene/Scene.hpp"

namespace okay {
namespace modules {

namespace {

std::string ParticleOverlay(const Scene* scene) {
    if (!scene) return {};
    int n = 0;
    for (ParticleSystem* ps : scene->FindObjectsOfType<ParticleSystem>()) if (ps) ++n;
    if (n <= 0) return {};
    return "Particles: " + std::to_string(n);
}

void ScanParticleAssets(const GameObject& go, std::vector<ModuleAssetNote>& out) {
    for (auto* c : go.GetComponents<ParticleSystem>()) {
        if (!c || c->texture.empty()) continue;
        out.push_back({"ParticleSystem", "texture", "texture", c->texture});
    }
}

void BuildParticleTemplate(Scene& scene) {
    GameObject* go = scene.CreateGameObject("Particles");
    go->AddComponent<ParticleSystem>();
}

} // namespace

void Register_particles(ModuleContext&) {
    RenderContribution c;
    c.name = "particles";
    c.module = "particles";
    c.overlay = &ParticleOverlay;
    AddRenderContribution(std::move(c));
    AddModuleAssetScan("particles", &ScanParticleAssets);
    RegisterSceneTemplate({"particle_system", "particles", &BuildParticleTemplate});
}

} // namespace modules
} // namespace okay
