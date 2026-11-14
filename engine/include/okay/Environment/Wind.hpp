#pragma once
// The global environment state components read every frame. Kept in a header of
// its own (no scene dependency) so ParticleSystem and Water can include it.
#include "okay/Math/Vec3.hpp"

namespace okay {

/// World-space wind (units / second): the scene's weather wind plus any storm
/// gust. Particle systems drift with it and Water's waves follow it. Refreshed
/// by UpdateWeather every frame; a scene without weather leaves it at zero.
inline Vec3& WindVector() { static Vec3 w{0, 0, 0}; return w; }
/// A lightning strike's brightness, 1 at the flash and decaying to 0 over a few
/// frames. The renderers add it to the exposure so the whole frame blinks.
inline float& LightningFlash() { static float f = 0.0f; return f; }

} // namespace okay
