#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Multi-light shading: directional always lights a facing surface; point/spot
// fall off with distance and (for spot) outside the cone.
int main() {
    RUN_SUITE("lighting");

    // --- Directional: a surface whose normal opposes the light is lit --------
    {
        SceneLights::Clear();
        SceneLights::SetAmbient(0.0f);
        LightSample d; d.type = 0; d.dir = Vec3{0, -1, 0}; d.color = Vec3{1, 1, 1};
        SceneLights::Add(d);
        Vec3 lit = SceneLights::ShadeColor(Vec3{0, 0, 0}, Vec3{0, 1, 0}); // normal up, light down
        CHECK_NEAR(lit.x, 1.0f, 0.001f);
        Vec3 dark = SceneLights::ShadeColor(Vec3{0, 0, 0}, Vec3{0, -1, 0}); // facing away
        CHECK_NEAR(dark.x, 0.0f, 0.001f);
    }

    // --- Point: brightness falls off with distance, colored --------------------
    {
        SceneLights::Clear();
        SceneLights::SetAmbient(0.0f);
        LightSample p; p.type = 1; p.pos = Vec3{0, 2, 0}; p.color = Vec3{1, 0, 0}; p.range = 10.0f;
        SceneLights::Add(p);
        Vec3 near = SceneLights::ShadeColor(Vec3{0, 0, 0}, Vec3{0, 1, 0}); // 2 units away
        Vec3 far  = SceneLights::ShadeColor(Vec3{0, -6, 0}, Vec3{0, 1, 0}); // 8 units away
        CHECK(near.x > far.x);          // closer is brighter
        CHECK(near.x > 0.0f);
        CHECK_NEAR(near.y, 0.0f, 0.001f); // red light -> no green
    }

    // --- Spot: lit inside the cone, dark outside ------------------------------
    {
        SceneLights::Clear();
        SceneLights::SetAmbient(0.0f);
        LightSample s; s.type = 2; s.pos = Vec3{0, 5, 0}; s.dir = Vec3{0, -1, 0};
        s.color = Vec3{1, 1, 1}; s.range = 20.0f; s.cosOuter = 0.9f; // narrow cone
        SceneLights::Add(s);
        Vec3 under = SceneLights::ShadeColor(Vec3{0, 0, 0}, Vec3{0, 1, 0});   // directly under
        Vec3 side  = SceneLights::ShadeColor(Vec3{8, 0, 0}, Vec3{0, 1, 0});   // off to the side
        CHECK(under.x > 0.2f);
        CHECK(side.x < under.x);
    }

    // --- Ambient floor with no facing light -----------------------------------
    {
        SceneLights::Clear();
        SceneLights::SetAmbient(0.3f);
        LightSample d; d.type = 0; d.dir = Vec3{0, -1, 0}; d.color = Vec3{1, 1, 1};
        SceneLights::Add(d);
        Vec3 lit = SceneLights::ShadeColor(Vec3{0, 0, 0}, Vec3{0, -1, 0}); // facing away
        CHECK_NEAR(lit.x, 0.3f, 0.001f);  // only ambient
    }

    // --- Tinted (colored) ambient floor ---------------------------------------
    {
        SceneLights::Clear();
        SceneLights::SetAmbientColor(Vec3{0.1f, 0.2f, 0.4f});   // cool blue floor
        LightSample d; d.type = 0; d.dir = Vec3{0, -1, 0}; d.color = Vec3{1, 1, 1};
        SceneLights::Add(d);
        Vec3 dark = SceneLights::ShadeColor(Vec3{0, 0, 0}, Vec3{0, -1, 0}); // facing away
        CHECK_NEAR(dark.x, 0.1f, 0.001f);
        CHECK_NEAR(dark.y, 0.2f, 0.001f);
        CHECK_NEAR(dark.z, 0.4f, 0.001f);   // blue ambient comes through
    }

    // --- Color temperature: warm is redder, cool is bluer ---------------------
    {
        Color warm = Light::KelvinToColor(2700.0f);
        Color cool = Light::KelvinToColor(12000.0f);
        CHECK(warm.r >= warm.b);        // incandescent: red >= blue
        CHECK(warm.b < cool.b);         // cooler light has more blue
    }

    SceneLights::Clear();
    TEST_MAIN_RESULT();
}
