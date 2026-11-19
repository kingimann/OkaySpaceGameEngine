#include "test_framework.hpp"
#include "lighting_scenes.hpp"
#include "okay/Render/ShadowSetup.hpp"
#include <cmath>
#include <string>

using namespace okay;
using namespace lightscene;

// Lighting upgrade (docs/lighting_upgrade.md): shadow maths (stable cascades, slope bias, soft penumbra, distance fade),
// light shapes and masks (smooth spot, real rectangle area light, cookies), the light roll-off, real-world units, glow
// lights, save/load of the new settings, and deterministic software renders with a tolerance.
namespace {
float Luma(const Image& im, int x, int y) { Color c = im.GetPixel(x, y); return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }
float MeanLuma(const Image& im, int x0, int y0, int x1, int y1) {
    double s = 0; int n = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { s += Luma(im, x, y); ++n; }
    return (float)(s / n);
}
}

int main() {
    RUN_SUITE("lighting_upgrade");
    using namespace shadowmath;

    // ---- Stable cascades: the texel grid belongs to the world -------------------------------------------------
    {
        const float R = 8.0f; const int S = 1024; const float texel = 2 * R / S;
        const Vec3 L = Vec3{0.3f, -1.0f, 0.5f}.Normalized();
        auto texelOf = [&](const Vec3& ctr, const Vec3& P, float& fx, float& fy) {
            const Mat4 view = Mat4::LookAt(ctr - L * 40.0f, ctr, Vec3{0, 1, 0});
            const Mat4 vp = Mat4::Ortho(-R, R, -R, R, 0.05f, 100.0f) * Mat4::Translate(CascadeSnapShift(view, texel)) * view;
            const Vec4 c = vp * Vec4{P, 1.0f};
            fx = (c.x / c.w * 0.5f + 0.5f) * S; fy = (c.y / c.w * 0.5f + 0.5f) * S;
        };
        const Vec3 P{3.1f, 0.2f, -2.7f};
        float ax, ay; texelOf(Vec3{0, 0, 0}, P, ax, ay);
        for (int k = 1; k <= 6; ++k) {
            const Vec3 ctr{0.137f * k, 0.05f * k, -0.291f * k};   // the camera creeps along by odd fractions of a texel
            float bx, by; texelOf(ctr, P, bx, by);
            float dx = bx - ax, dy = by - ay;
            // the point may move by whole texels but never by a fraction: the grid does not slide under the shadow
            CHECK_NEAR(dx - std::round(dx), 0.0f, 2e-2f);
            CHECK_NEAR(dy - std::round(dy), 0.0f, 2e-2f);
        }
        // the same through the GPU plan's cascade fit
        ShadowCascadeGpu a, b;
        detail::FitCascade(a, Vec3{0, 0, 0}, R, L, 10.0f, S);
        detail::FitCascade(b, Vec3{0.37f, 0.1f, -0.9f}, R, L, 10.0f, S);
        auto tx = [&](const ShadowCascadeGpu& c) { Vec4 q = c.vp * Vec4{P, 1.0f}; return (q.x / q.w * 0.5f + 0.5f) * S; };
        CHECK_NEAR(tx(b) - tx(a) - std::round(tx(b) - tx(a)), 0.0f, 2e-2f);
    }

    // ---- Bias, penumbra and fade maths ------------------------------------------------------------------------
    {
        CHECK(SlopeTan(1.0f) < 1e-4f);
        CHECK_NEAR(SlopeTan(std::cos(0.7853982f)), 1.0f, 1e-3f);          // 45 degrees
        CHECK(SlopeTan(0.0f) <= 6.0001f);                                  // capped
        CHECK(NormalOffset(1.0f, 0.01f, 1.0f) < NormalOffset(0.3f, 0.01f, 1.0f));   // grazing pushes more
        CHECK_NEAR(NormalOffset(0.5f, 0.02f, 2.0f), 2.0f * NormalOffset(0.5f, 0.02f, 1.0f), 1e-6f);
        CHECK(DepthBias(0.5f, 0.01f, 3.0f, 1.0f) > DepthBias(1.0f, 0.01f, 3.0f, 1.0f));      // slope term
        CHECK(DepthBias(0.5f, 0.01f, 6.0f, 1.0f) > DepthBias(0.5f, 0.01f, 1.0f, 1.0f));      // wider filter
        CHECK_NEAR(DistanceFade(10, 100), 1.0f, 1e-6f);
        CHECK_NEAR(DistanceFade(80, 100), 1.0f, 1e-6f);
        CHECK_NEAR(DistanceFade(100, 100), 0.0f, 1e-6f);
        CHECK_NEAR(DistanceFade(90, 100), 0.5f, 1e-5f);
        CHECK_NEAR(DistanceFade(500, 0), 1.0f, 1e-6f);                      // 0 = no limit
        CHECK_NEAR(PenumbraTexels(0.0f, 0.01f, 1.0f, 2.5f), 2.5f, 1e-6f);   // touching = sharp minimum
        CHECK(PenumbraTexels(1.0f, 0.01f, 1.0f, 2.5f) > PenumbraTexels(0.2f, 0.01f, 1.0f, 2.5f));
        CHECK_NEAR(PenumbraTexels(5.0f, 0.01f, 0.0f, 2.5f), 2.5f, 1e-6f);   // softness 0 = even edge
        CHECK(PenumbraTexels(100.0f, 0.01f, 1.0f, 2.5f) <= 14.0001f);       // capped
        float sx = 0, sy = 0; double mx = 0, my = 0;
        for (int k = 0; k < 16; ++k) { DiscTap(k, 16, sx, sy); CHECK(sx * sx + sy * sy <= 1.0001f); mx += sx; my += sy; }
        CHECK(std::fabs(mx / 16) < 0.15 && std::fabs(my / 16) < 0.15);      // evenly spread, centred
    }

    // ---- Light roll-off, spot cone, units ---------------------------------------------------------------------
    {
        CHECK_NEAR(SoftClipLight(0.5f), 0.5f, 1e-6f);
        CHECK_NEAR(SoftClipLight(0.8f), 0.8f, 1e-6f);
        CHECK(SoftClipLight(1.0f) < SoftClipLight(2.0f) && SoftClipLight(2.0f) < SoftClipLight(6.0f));   // order kept
        CHECK(SoftClipLight(50.0f) <= 1.0f);
        CHECK(std::fabs(SoftClipLight(0.8f + 1e-3f) - SoftClipLight(0.8f - 1e-3f)) < 2.5e-3f);           // no kink
        const float co = std::cos(0.5f), ci = std::cos(0.25f);
        CHECK_NEAR(SpotCone(1.0f, co, ci), 1.0f, 1e-6f);
        CHECK_NEAR(SpotCone(co - 0.01f, co, ci), 0.0f, 1e-6f);
        CHECK_NEAR(SpotCone(0.5f * (co + ci), co, ci), 0.5f, 1e-5f);
        CHECK(SpotCone(co + 0.02f, co, ci) < 0.05f);                                    // gentle start (smoothstep)
        CHECK(LuxFromLumens(800.0f) > 60.0f && LuxFromLumens(800.0f) < 70.0f);          // 800 lm bulb = ~64 cd
        CHECK(LuxFromLumens(800.0f, 0.9f) > LuxFromLumens(800.0f));                      // a spot focuses it
        CHECK_NEAR(SimpleIntensityFromLux(20000, 20000), 1.0f, 1e-6f);
        CHECK_NEAR(AutoReferenceLux(100000), 100000.0f, 1e-3f);
    }

    // ---- Cookies ----------------------------------------------------------------------------------------------
    {
        int n = 0; const char* const* names = LightCookies::BuiltinNames(n);
        CHECK(n >= 5);
        for (int i = 0; i < n; ++i) {
            const int idx = LightCookies::Find(names[i]);
            CHECK(idx >= 0);
            const LightCookie* c = LightCookies::Get(idx);
            CHECK(c && c->w == 64);
            float lo = 1, hi = 0;
            for (float v : c->a) { lo = std::fmin(lo, v); hi = std::fmax(hi, v); }
            CHECK(lo >= 0.0f && hi <= 1.0f);
            CHECK(hi - lo > 0.5f);                                   // a real pattern: both open and blocked parts
        }
        CHECK(LightCookies::Find("") == -1);
        CHECK(LightCookies::Find("no/such/file.png") == -1);
        CHECK(LightCookies::Find("builtin:window") == LightCookies::Find("builtin:window"));   // cached
        const LightCookie* w = LightCookies::Get(LightCookies::Find("builtin:window"));
        CHECK(w->Sample(0.5f, 0.5f, false) < 0.2f);                  // the cross bar
        CHECK(w->Sample(0.25f, 0.25f, false) > 0.9f);                // an open pane
        CHECK(w->Sample(1.25f, 0.25f, true) > 0.9f);                 // repeats
        CHECK_NEAR(w->Sample(1.5f, 0.5f, false), 0.0f, 1e-6f);       // outside the picture: blocked
        // a spot's mask is centred on its axis and fits the cone
        LightSample sp; sp.type = 2; sp.pos = {0, 5, 0}; sp.dir = {0, -1, 0}; sp.right = {1, 0, 0}; sp.up = {0, 0, 1};
        sp.cosOuter = std::cos(0.5f); sp.cosInner = std::cos(0.25f);
        sp.cookie = LightCookies::Find("builtin:soft-circle");
        CHECK(CookieAt(sp, Vec3{0, 0, 0}) > 0.95f);                  // on the axis
        CHECK(CookieAt(sp, Vec3{4, 0, 0}) < 0.05f);                  // far to the side (outside the cone at this depth)
        sp.cookie = -1; CHECK_NEAR(CookieAt(sp, Vec3{4, 0, 0}), 1.0f, 1e-6f);
    }

    // ---- Rectangle area light ---------------------------------------------------------------------------------
    {
        LightSample a; a.type = 3; a.pos = {0, 3, 0}; a.dir = {0, -1, 0}; a.right = {1, 0, 0}; a.up = {0, 0, 1};
        a.halfW = 1.0f; a.halfH = 1.0f; a.range = 20.0f; a.falloff = 0;
        const Vec3 up{0, 1, 0};
        const float below = RectLightTerm(a, Vec3{0, 0, 0}, up);
        CHECK(below > 0.05f);
        CHECK(RectLightTerm(a, Vec3{0, 6, 0}, up * -1.0f) <= 1e-6f);               // behind the panel: nothing (one-sided)
        CHECK(RectLightTerm(a, Vec3{0, 0, 0}, up * -1.0f) <= 1e-6f);               // a surface facing away
        CHECK(RectLightTerm(a, Vec3{4, 0, 0}, up) < below);                         // fades to the side
        // far away, a panel behaves like a point light (the two agree within a few percent)
        LightSample pt = a; pt.halfW = pt.halfH = 0.0f;
        pt.type = 1;
        const Vec3 far{0, 0, 0};
        a.pos = {0, 12, 0}; pt.pos = a.pos;
        const float panel = RectLightTerm(a, far, up);
        const float d = 12.0f; const float point = LightAttenuation(d, a.range, 0) * 1.0f * 1.0f;
        CHECK_NEAR(panel, point, 0.02f);
        // near a wide panel is brighter overall than the same distance from a narrow one at an off-axis point
        LightSample wide = a, narrow = a; wide.pos = narrow.pos = {0, 2, 0}; wide.halfW = wide.halfH = 3.0f; narrow.halfW = narrow.halfH = 0.2f;
        CHECK(RectLightTerm(wide, Vec3{3, 0, 0}, up) > RectLightTerm(narrow, Vec3{3, 0, 0}, up));
    }

    // ---- Settings are saved and loaded -------------------------------------------------------------------------
    {
        Scene s("save");
        Light* l = AddLight(s, "L", Light::Type::Area, {0, 2, 0}, 90, 0, Color::White, 700.0f);
        l->shadowSoftness = 0.8f; l->areaWidth = 2.0f; l->areaHeight = 1.0f; l->cookie = "builtin:blinds"; l->cookieScale = 7.0f;
        l->unit = Light::Unit::RealWorld;
        GameObject* g = Box(s, "Glow", {0, 0, 0}, {1, 1, 1}, Color::White);
        g->GetComponent<MeshRenderer>()->emissive = Color{1, 0.5f, 0.2f, 1}; g->GetComponent<MeshRenderer>()->glowLightRange = 4.5f;
        s.renderSettings.autoBrightness = false;
        const std::string path = "lighting_upgrade_save.okayscene";
        CHECK(SceneSerializer::SaveToFile(s, path));
        Scene r("load");
        CHECK(SceneSerializer::LoadFromFile(r, path));
        bool foundLight = false, foundGlow = false;
        for (const auto& go : r.Objects()) {
            if (auto* rl = go->GetComponent<Light>()) {
                foundLight = true;
                CHECK_NEAR(rl->shadowSoftness, 0.8f, 1e-5f); CHECK_NEAR(rl->areaWidth, 2.0f, 1e-5f); CHECK_NEAR(rl->areaHeight, 1.0f, 1e-5f);
                CHECK(rl->cookie == "builtin:blinds"); CHECK_NEAR(rl->cookieScale, 7.0f, 1e-5f);
                CHECK(rl->unit == Light::Unit::RealWorld); CHECK_NEAR(rl->intensity, 700.0f, 1e-3f);
            }
            if (auto* mr = go->GetComponent<MeshRenderer>()) if (mr->glowLightRange > 0) { foundGlow = true; CHECK_NEAR(mr->glowLightRange, 4.5f, 1e-5f); }
        }
        CHECK(foundLight); CHECK(foundGlow);
        CHECK(!r.renderSettings.autoBrightness);
        std::remove(path.c_str());
    }

    // ---- Gathering: real-world units, auto brightness, glow lights, panel and mask data ----------------------------
    {
        Scene s("gather");
        Light* sun = AddLight(s, "Sun", Light::Type::Directional, {0, 5, 0}, 50, 0, Color::White, 100000.0f);
        sun->unit = Light::Unit::RealWorld;
        Light* lamp = AddLight(s, "Lamp", Light::Type::Point, {0, 1, 0}, 0, 0, Color::White, 800.0f);
        lamp->unit = Light::Unit::RealWorld;
        ApplySceneLight(s);
        CHECK_NEAR(SceneLights::List()[0].color.x, 1.0f, 1e-4f);                    // brightest = 1 with auto brightness
        CHECK(SceneLights::List()[1].color.x < 0.01f);                              // a bulb beside the sun is faint
        s.renderSettings.autoBrightness = false;
        ApplySceneLight(s);
        CHECK_NEAR(SceneLights::List()[0].color.x, 100000.0f / kDefaultReferenceLux, 1e-3f);
        sun->enabled = false;
        s.renderSettings.autoBrightness = true;
        ApplySceneLight(s);
        CHECK_NEAR(SceneLights::List()[0].color.x, 1.0f, 1e-4f);                    // at night the lamp is "fully lit": adaptation
        // glow
        GameObject* g = Box(s, "Glow", {0, 0, 0}, {1, 1, 1}, Color::White);
        g->GetComponent<MeshRenderer>()->emissive = Color{2, 1, 0.5f, 1};
        ApplySceneLight(s); const size_t before = SceneLights::List().size();
        g->GetComponent<MeshRenderer>()->glowLightRange = 5.0f;
        ApplySceneLight(s);
        CHECK(SceneLights::List().size() == before + 1);
        const LightSample& gl = SceneLights::List().back();
        CHECK(gl.type == 1 && !gl.castShadows && gl.range == 5.0f && gl.color.x > gl.color.z);
        // panel data and cookie index
        Scene t("shape");
        Light* ar = AddLight(t, "Panel", Light::Type::Area, {0, 2, 0}, 90, 0, Color::White, 1.0f);
        ar->areaWidth = 3.0f; ar->areaHeight = 1.0f; ar->cookie = "builtin:dots";
        ApplySceneLight(t);
        const LightSample& ps = SceneLights::List()[0];
        CHECK_NEAR(ps.halfW, 1.5f, 1e-5f); CHECK_NEAR(ps.halfH, 0.5f, 1e-5f);
        CHECK(ps.cookie >= 0);   // (an Area light carries the mask but does not apply it; spot and sun do)
        CHECK_NEAR(Vec3::Dot(ps.right, ps.up), 0.0f, 1e-4f);
    }

    // ---- Software renders: sanity and determinism -------------------------------------------------------------------
    {
        ShadowsEnabled() = true; ShadowDistance() = 80.0f; ShadowCascades() = 3;
        Scene s("sun"); View v = SunAndCubes(s);
        Image a = Render(s, v, 160, 100);
        Image b = Render(s, v, 160, 100);
        int diff = 0;
        for (int y = 0; y < 100; ++y) for (int x = 0; x < 160; ++x) if (std::fabs(Luma(a, x, y) - Luma(b, x, y)) > 1.0f / 255) ++diff;
        CHECK(diff == 0);                                                              // repeatable
        CHECK(MeanLuma(a, 0, 0, 160, 100) > 0.15f && MeanLuma(a, 0, 0, 160, 100) < 0.9f);
        // the sun's shadows exist: turning them off lights more pixels than it darkens
        ShadowsEnabled() = false;
        Image off = Render(s, v, 160, 100);
        ShadowsEnabled() = true;
        CHECK(MeanLuma(off, 0, 40, 160, 100) > MeanLuma(a, 0, 40, 160, 100) + 0.003f);

        // spot light: bright inside the cone, dark outside, smooth between
        Scene sp("spot"); View sv = SpotCone(sp);
        Image si = Render(sp, sv, 160, 100);
        float lo = 1, hi = 0;
        for (int y = 40; y < 100; ++y) for (int x = 0; x < 160; ++x) { float l = Luma(si, x, y); lo = std::fmin(lo, l); hi = std::fmax(hi, l); }
        CHECK(hi - lo > 0.25f);
        // a window mask on the spot makes a pattern: more contrast than without
        for (const auto& go : sp.Objects()) if (auto* l = go->GetComponent<Light>()) l->cookie = "builtin:window";
        Image sc = Render(sp, sv, 160, 100);
        double dev0 = 0, dev1 = 0; int n = 0;
        for (int y = 50; y < 100; ++y) for (int x = 40; x < 120; ++x) { dev0 += std::fabs(Luma(si, x, y) - Luma(si, x + 1, y)); dev1 += std::fabs(Luma(sc, x, y) - Luma(sc, x + 1, y)); ++n; }
        CHECK(dev1 > dev0);
        // the lamp room is lit near the lamp
        Scene rm("room"); View rv = IndoorRoom(rm);
        Image ri = Render(rm, rv, 160, 100);
        CHECK(MeanLuma(ri, 40, 20, 120, 90) > 0.12f);
    }

    SceneLight::Reset();
    TEST_MAIN_RESULT();
}
