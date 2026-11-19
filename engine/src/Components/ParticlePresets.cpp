// The one-click particle presets. Every preset starts from the same clean baseline (so presets never inherit
// leftovers from each other), then sets what makes its look. Values were tuned by eye against headless renders
// (docs/particles_upgrade.md); nothing here was copied from another engine.
#include "okay/Components/ParticleSystem.hpp"
#include <initializer_list>
#include <utility>

namespace okay {

const char* ParticleSystem::PresetName(Preset p) {
    static const char* kNames[] = {"Fire", "Smoke", "Sparks", "Explosion", "Rain", "Snow", "Magic sparkle", "Dust",
                                   "Campfire", "Steam", "Leaves", "Confetti", "Hit spark", "Muzzle flash", "Fireflies",
                                   "Portal", "Waterfall mist"};
    const int i = (int)p;
    return (i >= 0 && i < (int)Preset::Count) ? kNames[i] : "?";
}

const char* ParticleSystem::PresetHint(Preset p) {
    switch (p) {
        case Preset::Fire:          return "Flames: a white-hot core that turns orange, then red, and flickers.";
        case Preset::Smoke:         return "Slow grey puffs that grow, swirl and fade away.";
        case Preset::Sparks:        return "Fast glowing streaks that arc down and cool.";
        case Preset::Explosion:     return "A flash, a fireball and drifting smoke, over and over.";
        case Preset::Rain:          return "Streaks of rain falling over a wide area. It leans in the wind.";
        case Preset::Snow:          return "Soft flakes drifting down on a gentle breeze.";
        case Preset::Magic:         return "Glowing violet and blue motes circling and twinkling.";
        case Preset::Dust:          return "Faint specks hanging in the air.";
        case Preset::Campfire:      return "A small, steady fire that is already burning when the scene starts.";
        case Preset::Steam:         return "Thin white wisps that rise, spread and vanish.";
        case Preset::Leaves:        return "Leaves tumbling down and landing on the ground.";
        case Preset::Confetti:      return "A burst of coloured pieces that tumble and fall. Repeats every few seconds.";
        case Preset::HitSpark:      return "A quick spray of sparks for a hit. Repeats so you can see it.";
        case Preset::MuzzleFlash:   return "A very short bright flash, like a gun going off. Repeats so you can see it.";
        case Preset::Fireflies:     return "Little lights that float around and twinkle.";
        case Preset::Portal:        return "A glowing ring whose sparks spiral in with light trails.";
        case Preset::WaterfallMist: return "A cool white mist that falls and spreads at the foot of a waterfall.";
        case Preset::Count: break;
    }
    return "";
}

void ParticleSystem::ApplyPreset(Preset which) {
    // ---- the clean baseline ----
    shape = Shape::Point; shapeRadius = 1.0f; shapeAngle = 25.0f; boxSize = {1.0f, 1.0f, 1.0f}; shapeSurfaceOnly = false;
    burstCount = 0; burstTime = 0.0f; duration = 0.0f; loop = true; extraBurstCount = 0;
    for (Burst& b : extraBursts) b = Burst{};
    emitPerDistance = 0.0f; prewarm = false; capPolicy = CapPolicy::DropNew;
    startLifetimeRandom = 0.0f; startSizeRandom = 0.0f; velocityRandom = 0.0f; speedRandom = 0.2f;
    startRotation = 0.0f; startRotationRandom = 0.0f; rotationSpeed = 0.0f; rotationSpeedRandom = 0.0f;
    randomStartColor = false; randomHue = false; startColor2 = Color::White;
    renderMode = RenderMode::Billboard; stretchScale = 0.15f; blendMode = BlendMode::Alpha; sortMode = SortMode::Distance;
    sheetColumns = 1; sheetRows = 1; sheetMode = SheetMode::OverLife; sheetCycles = 1.0f; sheetBlendFrames = true;
    softDistance = 0.0f; lit = false; trailLength = 0; trailInterval = 0.03f; trailWidth = 0.6f; trailFade = 1.0f;
    colorOverLife = true; sizeOverLife = false; fadeOverLife = false; speedOverLife = false; rotationOverLife = false;
    colorGradient.Clear();
    sizeCurve.Clear(); sizeCurve.smooth = true; speedCurve.Clear(); speedCurve.smooth = true;
    rotationCurve.Clear(); velocityCurve.Clear(); forceCurve.Clear(); gravityCurve.Clear();
    linearVelocity = {0, 0, 0}; radialVelocity = 0.0f; force = {0, 0, 0};
    endSize = 0.0f; endColor = Color::White;
    gravity = {0.0f, -2.0f, 0.0f}; gravityModifier = 1.0f; damping = 0.0f;
    simulationSpace = SimulationSpace::World; inheritVelocity = 0.0f;
    noiseStrength = 0.0f; noiseFrequency = 1.0f; noiseScroll = 0.5f; noiseOctaves = 1;
    orbitSpeed = 0.0f; orbitAxis = {0.0f, 1.0f, 0.0f}; vortexStrength = 0.0f; vortexPull = 0.0f;
    collision = false; dieOnCollision = false; worldCollision = false; bounce = 0.5f; collisionFriction = 0.1f; collisionLifeLoss = 0.0f;
    subEmitterBirth.clear(); subEmitterDeath.clear(); subEmitterCollision.clear();
    ParticleGradient& G = colorGradient;
    auto grad = [&](std::initializer_list<std::pair<float, Color>> stops) {
        for (const auto& s : stops) G.Add(s.first, s.second);
        startColor = G.stops[0].color;
    };
    switch (which) {
        case Preset::Fire:
            emissionRate = 60; maxParticles = 400;
            shape = Shape::Circle; shapeRadius = 0.25f;
            startLifetime = 0.9f; startLifetimeRandom = 0.3f;
            startSize = 0.45f; startSizeRandom = 0.15f;
            startVelocity = {0, 1.8f, 0}; velocityRandom = 0.4f;
            gravity = {0, 1.2f, 0};
            grad({{0.0f, {1.0f, 0.95f, 0.6f, 1.0f}}, {0.25f, {1.0f, 0.65f, 0.15f, 1.0f}}, {0.6f, {0.9f, 0.2f, 0.05f, 0.8f}}, {1.0f, {0.3f, 0.05f, 0.02f, 0.0f}}});
            sizeOverLife = true; sizeCurve.Add(0.0f, 0.6f); sizeCurve.Add(0.2f, 1.0f); sizeCurve.Add(1.0f, 0.15f);
            noiseStrength = 0.6f; noiseFrequency = 1.5f; noiseScroll = 1.5f;
            blendMode = BlendMode::Additive; prewarm = true;
            break;
        case Preset::Campfire:
            emissionRate = 38; maxParticles = 260;
            shape = Shape::Circle; shapeRadius = 0.18f;
            startLifetime = 0.8f; startLifetimeRandom = 0.25f;
            startSize = 0.3f; startSizeRandom = 0.1f;
            startVelocity = {0, 1.3f, 0}; velocityRandom = 0.3f;
            gravity = {0, 1.0f, 0}; damping = 0.4f;
            grad({{0.0f, {1.0f, 0.9f, 0.5f, 1.0f}}, {0.3f, {1.0f, 0.55f, 0.1f, 1.0f}}, {0.7f, {0.8f, 0.15f, 0.04f, 0.7f}}, {1.0f, {0.2f, 0.04f, 0.02f, 0.0f}}});
            sizeOverLife = true; sizeCurve.Add(0.0f, 0.7f); sizeCurve.Add(0.25f, 1.0f); sizeCurve.Add(1.0f, 0.1f);
            noiseStrength = 0.5f; noiseFrequency = 2.0f; noiseScroll = 2.0f; noiseOctaves = 2;
            blendMode = BlendMode::Additive; prewarm = true;
            lodNear = 15.0f; lodFar = 60.0f; lodMinimum = 0.3f;
            break;
        case Preset::Smoke:
            emissionRate = 20; maxParticles = 300;
            shape = Shape::Circle; shapeRadius = 0.2f;
            startLifetime = 2.8f; startLifetimeRandom = 0.8f;
            startSize = 0.5f; startSizeRandom = 0.2f;
            startVelocity = {0, 1.2f, 0}; velocityRandom = 0.3f;
            gravity = {0, 0.3f, 0}; damping = 0.3f;
            grad({{0.0f, {0.5f, 0.5f, 0.52f, 0.0f}}, {0.15f, {0.45f, 0.45f, 0.48f, 0.6f}}, {1.0f, {0.25f, 0.25f, 0.28f, 0.0f}}});
            startColor = G.stops[1].color;
            sizeOverLife = true; sizeCurve.Add(0.0f, 0.5f); sizeCurve.Add(1.0f, 2.8f);
            noiseStrength = 0.8f; noiseFrequency = 0.6f; noiseScroll = 0.4f;
            rotationSpeed = 20.0f; startRotationRandom = 180.0f;
            lit = true; softDistance = 0.4f; prewarm = true;
            break;
        case Preset::Steam:
            emissionRate = 26; maxParticles = 260;
            shape = Shape::Circle; shapeRadius = 0.12f;
            startLifetime = 1.8f; startLifetimeRandom = 0.5f;
            startSize = 0.3f; startSizeRandom = 0.1f;
            startVelocity = {0, 1.0f, 0}; velocityRandom = 0.2f;
            gravity = {0, 0.4f, 0}; damping = 0.5f;
            grad({{0.0f, {1.0f, 1.0f, 1.0f, 0.0f}}, {0.12f, {0.95f, 0.97f, 1.0f, 0.35f}}, {1.0f, {0.9f, 0.93f, 1.0f, 0.0f}}});
            startColor = G.stops[1].color;
            sizeOverLife = true; sizeCurve.Add(0.0f, 0.4f); sizeCurve.Add(1.0f, 3.0f);
            noiseStrength = 0.7f; noiseFrequency = 0.8f; noiseScroll = 0.6f; noiseOctaves = 2;
            rotationSpeed = 25.0f; startRotationRandom = 180.0f; softDistance = 0.3f; prewarm = true;
            break;
        case Preset::Sparks:
            emissionRate = 90; maxParticles = 500;
            startLifetime = 0.6f; startLifetimeRandom = 0.2f;
            startSize = 0.08f; startSizeRandom = 0.03f;
            startVelocity = {0, 4.0f, 0}; velocityRandom = 3.5f;
            gravity = {0, -9.0f, 0}; damping = 0.5f;
            grad({{0.0f, {1.0f, 1.0f, 0.85f, 1.0f}}, {0.4f, {1.0f, 0.75f, 0.25f, 1.0f}}, {1.0f, {1.0f, 0.3f, 0.05f, 0.0f}}});
            renderMode = RenderMode::Stretch; blendMode = BlendMode::Additive;
            break;
        case Preset::Explosion:
            emissionRate = 0; maxParticles = 360;
            burstCount = 150; burstTime = 0.0f; duration = 2.0f; loop = true;
            extraBurstCount = 1; extraBursts[0] = {0.08f, 50};
            shape = Shape::Sphere; shapeRadius = 0.3f;
            startLifetime = 1.1f; startLifetimeRandom = 0.4f;
            startSize = 0.5f; startSizeRandom = 0.2f;
            startVelocity = {0, 6.0f, 0}; speedRandom = 0.4f; velocityRandom = 0.5f;
            gravity = {0, 0.8f, 0}; damping = 2.5f;
            grad({{0.0f, {1.0f, 1.0f, 0.85f, 1.0f}}, {0.12f, {1.0f, 0.8f, 0.3f, 1.0f}}, {0.35f, {1.0f, 0.35f, 0.08f, 0.9f}},
                  {0.7f, {0.3f, 0.28f, 0.27f, 0.6f}}, {1.0f, {0.15f, 0.15f, 0.15f, 0.0f}}});
            sizeOverLife = true; sizeCurve.Add(0.0f, 0.4f); sizeCurve.Add(0.3f, 1.2f); sizeCurve.Add(1.0f, 1.8f);
            noiseStrength = 0.5f; noiseFrequency = 1.0f; noiseScroll = 0.8f;
            blendMode = BlendMode::Additive;
            break;
        case Preset::Rain:
            emissionRate = 300; maxParticles = 2000;
            shape = Shape::Box; boxSize = {12, 0.1f, 12};
            startLifetime = 1.6f; startLifetimeRandom = 0.2f;
            startSize = 0.06f;
            startColor = {0.60f, 0.72f, 1.0f, 0.8f}; endColor = {0.60f, 0.72f, 1.0f, 0.4f};
            startVelocity = {0, -9.0f, 0}; velocityRandom = 0.4f;
            gravityModifier = 0.0f;
            renderMode = RenderMode::Stretch;
            lodNear = 20.0f; lodFar = 80.0f; lodMinimum = 0.4f;
            break;
        case Preset::Snow:
            emissionRate = 80; maxParticles = 1500;
            shape = Shape::Box; boxSize = {12, 0.1f, 12};
            startLifetime = 6.0f; startLifetimeRandom = 2.0f;
            startSize = 0.09f; startSizeRandom = 0.04f;
            startColor = {1, 1, 1, 0.95f}; endColor = {1, 1, 1, 0.0f};
            startVelocity = {0, -0.8f, 0}; velocityRandom = 0.4f;
            gravityModifier = 0.0f; damping = 0.4f;
            noiseStrength = 0.5f; noiseFrequency = 0.8f; noiseScroll = 0.3f;
            lodNear = 20.0f; lodFar = 80.0f; lodMinimum = 0.4f;
            break;
        case Preset::Magic:
            emissionRate = 40; maxParticles = 400;
            shape = Shape::Sphere; shapeRadius = 0.6f;
            startLifetime = 1.6f; startLifetimeRandom = 0.5f;
            startSize = 0.15f; startSizeRandom = 0.08f;
            startVelocity = {0, 0.6f, 0}; velocityRandom = 0.3f;
            gravityModifier = 0.0f; rotationSpeed = 90.0f;
            grad({{0.0f, {0.55f, 0.40f, 1.0f, 1.0f}}, {0.6f, {0.20f, 0.90f, 1.0f, 0.9f}}, {1.0f, {0.9f, 0.95f, 1.0f, 0.0f}}});
            sizeOverLife = true; sizeCurve.Add(0.0f, 0.0f); sizeCurve.Add(0.2f, 1.0f); sizeCurve.Add(1.0f, 0.0f);
            orbitSpeed = 120.0f; vortexPull = 0.5f;
            noiseStrength = 0.3f; noiseFrequency = 1.2f; noiseScroll = 1.0f;
            blendMode = BlendMode::Additive;
            break;
        case Preset::Dust:
            emissionRate = 15; maxParticles = 200;
            shape = Shape::Box; boxSize = {4, 0.5f, 4};
            startLifetime = 4.0f; startLifetimeRandom = 1.5f;
            startSize = 0.06f; startSizeRandom = 0.03f;
            startVelocity = {0, 0.05f, 0}; velocityRandom = 0.1f;
            gravityModifier = 0.0f; damping = 0.5f;
            grad({{0.0f, {0.8f, 0.72f, 0.55f, 0.0f}}, {0.3f, {0.8f, 0.72f, 0.55f, 0.5f}}, {1.0f, {0.8f, 0.72f, 0.55f, 0.0f}}});
            startColor = G.stops[1].color;
            noiseStrength = 0.35f; noiseFrequency = 0.7f; noiseScroll = 0.2f;
            prewarm = true;
            break;
        case Preset::Leaves:
            emissionRate = 9; maxParticles = 120;
            shape = Shape::Box; boxSize = {8, 0.2f, 8};
            startLifetime = 7.0f; startLifetimeRandom = 2.0f;
            startSize = 0.16f; startSizeRandom = 0.05f;
            startVelocity = {0, -0.6f, 0}; velocityRandom = 0.3f;
            gravityModifier = 0.0f; damping = 0.6f;
            startColor = {0.35f, 0.65f, 0.2f, 1.0f}; startColor2 = {0.85f, 0.5f, 0.1f, 1.0f}; randomStartColor = true;
            colorOverLife = false; fadeOverLife = true;
            startRotationRandom = 180.0f; rotationSpeed = 0.0f; rotationSpeedRandom = 120.0f;
            noiseStrength = 0.9f; noiseFrequency = 0.5f; noiseScroll = 0.4f;
            force = {0.3f, 0, 0};
            collision = true; collisionY = 0.0f; bounce = 0.05f; collisionFriction = 0.8f; collisionLifeLoss = 0.5f;
            renderMode = RenderMode::Horizontal; prewarm = true;
            break;
        case Preset::Confetti:
            emissionRate = 0; maxParticles = 300;
            burstCount = 120; burstTime = 0.0f; duration = 3.0f; loop = true;
            shape = Shape::Cone; shapeAngle = 35.0f;
            startLifetime = 2.5f; startLifetimeRandom = 0.5f;
            startSize = 0.09f; startSizeRandom = 0.03f;
            startVelocity = {0, 7.0f, 0}; speedRandom = 0.35f; velocityRandom = 0.3f;
            gravity = {0, -7.0f, 0}; damping = 1.4f;
            startColor = {1.0f, 0.3f, 0.5f, 1.0f}; randomHue = true; randomStartColor = true;
            colorOverLife = false; fadeOverLife = true;
            startRotationRandom = 180.0f; rotationSpeedRandom = 360.0f;
            renderMode = RenderMode::Mesh;
            noiseStrength = 0.4f; noiseFrequency = 1.0f;
            collision = true; collisionY = 0.0f; bounce = 0.2f; collisionFriction = 0.7f;
            break;
        case Preset::HitSpark:
            emissionRate = 0; maxParticles = 120;
            burstCount = 22; burstTime = 0.0f; duration = 0.8f; loop = true;
            shape = Shape::Sphere; shapeRadius = 0.04f;
            startLifetime = 0.28f; startLifetimeRandom = 0.1f;
            startSize = 0.06f; startSizeRandom = 0.02f;
            startVelocity = {0, 5.0f, 0}; speedRandom = 0.5f;
            gravity = {0, -8.0f, 0}; damping = 1.5f;
            grad({{0.0f, {1.0f, 1.0f, 0.9f, 1.0f}}, {0.5f, {1.0f, 0.8f, 0.3f, 1.0f}}, {1.0f, {1.0f, 0.4f, 0.1f, 0.0f}}});
            renderMode = RenderMode::Stretch; stretchScale = 0.12f; blendMode = BlendMode::Additive;
            break;
        case Preset::MuzzleFlash:
            emissionRate = 0; maxParticles = 60;
            burstCount = 7; burstTime = 0.0f; duration = 0.6f; loop = true;
            shape = Shape::Cone; shapeAngle = 14.0f;
            startLifetime = 0.07f; startLifetimeRandom = 0.02f;
            startSize = 0.35f; startSizeRandom = 0.1f;
            startVelocity = {0, 0, 6.0f}; speedRandom = 0.3f;
            gravityModifier = 0.0f;
            grad({{0.0f, {1.0f, 0.98f, 0.8f, 1.0f}}, {0.4f, {1.0f, 0.75f, 0.3f, 0.9f}}, {1.0f, {1.0f, 0.4f, 0.1f, 0.0f}}});
            startRotationRandom = 180.0f; blendMode = BlendMode::Additive;
            sizeOverLife = true; sizeCurve.Add(0.0f, 0.6f); sizeCurve.Add(0.3f, 1.0f); sizeCurve.Add(1.0f, 0.3f);
            break;
        case Preset::Fireflies:
            emissionRate = 6; maxParticles = 80;
            shape = Shape::Box; boxSize = {8, 2.0f, 8};
            startLifetime = 6.0f; startLifetimeRandom = 2.0f;
            startSize = 0.1f; startSizeRandom = 0.03f;
            startVelocity = {0, 0.0f, 0}; velocityRandom = 0.15f;
            gravityModifier = 0.0f; damping = 0.3f;
            grad({{0.0f, {0.8f, 1.0f, 0.3f, 0.0f}}, {0.1f, {0.8f, 1.0f, 0.3f, 1.0f}}, {0.9f, {0.8f, 1.0f, 0.3f, 1.0f}}, {1.0f, {0.8f, 1.0f, 0.3f, 0.0f}}});
            startColor = G.stops[1].color;
            sizeOverLife = true; sizeCurve.Add(0.0f, 0.3f); sizeCurve.Add(0.25f, 1.0f); sizeCurve.Add(0.5f, 0.35f); sizeCurve.Add(0.75f, 1.0f); sizeCurve.Add(1.0f, 0.3f);
            noiseStrength = 0.9f; noiseFrequency = 0.4f; noiseScroll = 0.3f; noiseOctaves = 2;
            blendMode = BlendMode::Additive; prewarm = true;
            lodNear = 15.0f; lodFar = 50.0f; lodMinimum = 0.3f;
            break;
        case Preset::Portal:
            emissionRate = 70; maxParticles = 420;
            shape = Shape::Circle; shapeRadius = 1.1f;
            startLifetime = 1.6f; startLifetimeRandom = 0.4f;
            startSize = 0.1f; startSizeRandom = 0.03f;
            startVelocity = {0, 0, 0}; velocityRandom = 0.05f;
            gravityModifier = 0.0f; orbitSpeed = 140.0f; radialVelocity = -0.6f;
            grad({{0.0f, {0.4f, 0.9f, 1.0f, 0.0f}}, {0.15f, {0.4f, 0.9f, 1.0f, 1.0f}}, {0.7f, {0.7f, 0.4f, 1.0f, 0.8f}}, {1.0f, {0.9f, 0.5f, 1.0f, 0.0f}}});
            startColor = G.stops[1].color;
            sizeOverLife = true; sizeCurve.Add(0.0f, 0.6f); sizeCurve.Add(0.2f, 1.0f); sizeCurve.Add(1.0f, 0.2f);
            trailLength = 8; trailInterval = 0.04f; trailWidth = 0.5f; trailFade = 1.0f;
            noiseStrength = 0.15f; noiseFrequency = 1.5f;
            blendMode = BlendMode::Additive; prewarm = true;
            break;
        case Preset::WaterfallMist:
            emissionRate = 48; maxParticles = 420;
            shape = Shape::Edge; shapeRadius = 1.2f;
            startLifetime = 2.2f; startLifetimeRandom = 0.6f;
            startSize = 0.5f; startSizeRandom = 0.15f;
            startVelocity = {0, -1.5f, 0.6f}; velocityRandom = 0.3f;
            gravityModifier = 0.0f; damping = 0.8f;
            grad({{0.0f, {0.85f, 0.93f, 1.0f, 0.0f}}, {0.2f, {0.85f, 0.93f, 1.0f, 0.3f}}, {1.0f, {0.85f, 0.93f, 1.0f, 0.0f}}});
            startColor = G.stops[1].color;
            sizeOverLife = true; sizeCurve.Add(0.0f, 0.5f); sizeCurve.Add(1.0f, 2.6f);
            noiseStrength = 0.6f; noiseFrequency = 0.7f; noiseScroll = 0.7f; noiseOctaves = 2;
            startRotationRandom = 180.0f; rotationSpeedRandom = 20.0f; softDistance = 0.4f; prewarm = true;
            break;
        case Preset::Count: break;
    }
    if (G.Active()) endColor = G.Evaluate(1.0f);
    if (sizeCurve.Active()) endSize = startSize * sizeCurve.Evaluate(1.0f);
    Restart();
}

} // namespace okay
