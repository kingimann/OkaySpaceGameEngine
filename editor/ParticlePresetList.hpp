#pragma once
// The particle presets as plain data for the menus (no engine, no ImGui), so the Scene view's right-click menu, GameObject > Effects
// and Add Component can list them without linking the engine. The order and names are the engine's ParticleSystem::Preset order
// (tests/test_particles_upgrade.cpp checks they agree).
#include <cstddef>

namespace oked {
namespace particlepresets {

struct Item { const char* name; const char* hint; };

inline const Item* Items(std::size_t& count) {
    static const Item kItems[] = {
        {"Fire", "Flames: a white-hot core that turns orange, then red, and flickers."},
        {"Smoke", "Slow grey puffs that grow, swirl and fade away."},
        {"Sparks", "Fast glowing streaks that arc down and cool."},
        {"Explosion", "A flash, a fireball and drifting smoke, over and over."},
        {"Rain", "Streaks of rain falling over a wide area. It leans in the wind."},
        {"Snow", "Soft flakes drifting down on a gentle breeze."},
        {"Magic sparkle", "Glowing violet and blue motes circling and twinkling."},
        {"Dust", "Faint specks hanging in the air."},
        {"Campfire", "A small, steady fire that is already burning when the scene starts."},
        {"Steam", "Thin white wisps that rise, spread and vanish."},
        {"Leaves", "Leaves tumbling down and landing on the ground."},
        {"Confetti", "A burst of coloured pieces that tumble and fall. Repeats every few seconds."},
        {"Hit spark", "A quick spray of sparks for a hit. Repeats so you can see it."},
        {"Muzzle flash", "A very short bright flash, like a gun going off. Repeats so you can see it."},
        {"Fireflies", "Little lights that float around and twinkle."},
        {"Portal", "A glowing ring whose sparks spiral in with light trails."},
        {"Waterfall mist", "A cool white mist that falls and spreads at the foot of a waterfall."},
    };
    count = sizeof(kItems) / sizeof(kItems[0]);
    return kItems;
}

} // namespace particlepresets
} // namespace oked
