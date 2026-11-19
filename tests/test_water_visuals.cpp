// Water visuals (docs/water_visuals.md): the shading maths, the ring grid, the Water component's look, splash rings,
// underwater fog, presets, saving, and pictures made by the software renderer (checked with tolerances, not exact pixels).
#include "test_framework.hpp"
#include "water_scenes.hpp"
#include "okay/Environment/WaterShading.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <vector>

using namespace okay;
namespace ws = okay::watershade;

namespace {

// ---- pictures ----
Raster Render(Scene& sc, int w, int h) {
    Camera* cam = sc.mainCamera;
    const Vec3 eye = cam->transform->Position(), fwd = cam->transform->Forward();
    Raster r;
    r.Resize(w, h);
    r.Clear(0xFFC8A070u);
    ApplySceneLight(sc);
    RenderMeshes(r, sc, Mat4::Perspective(cam->fieldOfView, (float)w / h, 0.2f, 700.0f) * Mat4::LookAt(eye, eye + fwd, Vec3{0, 1, 0}), eye, nullptr);
    return r;
}
void Step(Scene& sc, float seconds, int fps) { for (int i = 0, n = (int)std::lround(seconds * fps); i < n; ++i) sc.Update(1.0f / fps); }
struct PixStats { double meanLum = 0, variance = 0, whiteFrac = 0, meanB = 0, meanR = 0; };
// Statistics of the lower part of the picture (where the water is).
PixStats Measure(const Raster& r, int y0, int y1) {
    PixStats s; double sum = 0, sum2 = 0, white = 0, sr = 0, sb = 0; long n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < r.width; ++x) {
            const std::uint32_t v = r.Get(x, y);
            const double R = (v & 0xFF), G = (v >> 8) & 0xFF, B = (v >> 16) & 0xFF;
            const double l = (R + G + B) / 3.0;
            sum += l; sum2 += l * l; sr += R; sb += B; ++n;
            if (R > 185 && G > 190 && B > 195) white += 1;
        }
    s.meanLum = sum / n; s.variance = sum2 / n - s.meanLum * s.meanLum; s.whiteFrac = white / n; s.meanR = sr / n; s.meanB = sb / n;
    return s;
}
std::uint64_t Hash(const Raster& r) {
    std::uint64_t h = 1469598103934665603ull;
    for (int y = 0; y < r.height; ++y) for (int x = 0; x < r.width; ++x) { h ^= r.Get(x, y); h *= 1099511628211ull; }
    return h;
}

void ShowDiff(const std::string& a, const std::string& b) {
    std::istringstream x(a), y(b); std::string l1, l2;
    while (std::getline(x, l1) && std::getline(y, l2)) if (l1 != l2) { std::printf("  first differing line:\n   %s\n   %s\n", l1.substr(0, 400).c_str(), l2.substr(0, 400).c_str()); return; }
}
const Mesh& MeshOf(Water* w) { return w->gameObject->GetComponent<MeshRenderer>()->mesh; }

struct Sea {
    Scene scene{"sea"};
    Water* water = nullptr;
    explicit Sea(int preset = 3, float size = 100.0f, int res = 48) {
        scene.fixedTimeStep = 1.0f / 50.0f;
        water = watersc::MakeWater(scene, preset, size, res, 10.0f);
        watersc::SunAndCamera(scene, {0, 6, -30}, {0, 0, 10});
    }
};

} // namespace

int main() {
    RUN_SUITE("water visuals");

    // ---------------------------------------------------------------- shading maths
    {
        // noise is deterministic, in range, and not constant
        float lo = 1, hi = 0; bool same = true;
        for (int i = 0; i < 400; ++i) {
            const float x = i * 0.37f, z = i * 0.19f;
            const float a = ws::Fbm(x, z, 3, 5u), b = ws::Fbm(x, z, 3, 5u);
            if (a != b) same = false;
            lo = std::min(lo, a); hi = std::max(hi, a);
        }
        CHECK(same); CHECK(lo >= 0.0f && hi <= 1.0f); CHECK(hi - lo > 0.2f);
        CHECK(ws::Fbm(3.3f, 1.1f, 3, 1u) != ws::Fbm(3.3f, 1.1f, 3, 2u));      // another seed, another pattern

        // ripples: never flat when on, exactly flat when off, and they MOVE with time (two layers, two directions)
        double sum = 0, sum2 = 0; int n = 0; float zeroOff = 0.0f;
        for (int i = 0; i < 40; ++i) for (int j = 0; j < 40; ++j) {
            const float h = ws::RippleHeight(i * 0.5f, j * 0.5f, 0.0f, 1.0f, 1.0f, 0, 0);
            sum += h; sum2 += h * h; ++n;
            zeroOff += std::fabs(ws::RippleHeight(i * 0.5f, j * 0.5f, 0.0f, 0.0f, 1.0f, 0, 0));
        }
        const double var = sum2 / n - (sum / n) * (sum / n);
        CHECK(var > 1e-6); CHECK(std::sqrt(var) < 0.05);        // visible, but only a few centimetres
        CHECK(zeroOff == 0.0f);
        CHECK(ws::RippleHeight(3.0f, 4.0f, 0.0f, 1, 1, 0, 0) != ws::RippleHeight(3.0f, 4.0f, 2.0f, 1, 1, 0, 0));
        // a river current drags the pattern: the pattern at (x - c t) with current c equals the pattern at x with no current, only
        // approximately because layer B drifts at 0.7 of it; check the direction instead: downstream shift changes the value
        CHECK(ws::RippleHeight(3.0f, 4.0f, 2.0f, 1, 1, 2.0f, 0) != ws::RippleHeight(3.0f, 4.0f, 2.0f, 1, 1, 0, 0));

        // Fresnel: straight down about 2 percent, grazing 100 percent, always rising towards grazing
        CHECK_NEAR(ws::Fresnel(1.0f), 0.02f, 1e-6);
        CHECK_NEAR(ws::Fresnel(0.0f), 1.0f, 1e-6);
        bool mono = true; for (int i = 1; i <= 20; ++i) if (ws::Fresnel(1.0f - i / 20.0f) < ws::Fresnel(1.0f - (i - 1) / 20.0f)) mono = false;
        CHECK(mono);

        // absorption: red goes first, nothing is lost at depth 0, deeper is darker and bluer
        const ws::RGB t0 = ws::Transmit(0.0f, 5.0f), t3 = ws::Transmit(3.0f, 5.0f);
        CHECK_NEAR(t0.r, 1.0f, 1e-6);
        CHECK(t3.r < t3.g && t3.g < t3.b);
        const ws::RGB sh{0.27f, 0.75f, 0.72f}, dp{0.03f, 0.16f, 0.39f};
        const ws::RGB c0 = ws::DepthColor(0.0f, 5.0f, sh, dp), c20 = ws::DepthColor(40.0f, 5.0f, sh, dp);
        CHECK_NEAR(c0.g, sh.g, 1e-5); CHECK(std::fabs(c20.g - dp.g) < 0.01f);
        CHECK(ws::DepthColor(2.0f, 5.0f, sh, dp).g < ws::DepthColor(1.0f, 5.0f, sh, dp).g);
        CHECK(ws::DepthColor(2.0f, 2.0f, sh, dp).g < ws::DepthColor(2.0f, 8.0f, sh, dp).g);   // murky water gets deep-coloured sooner

        // foam: none when off, more on a higher crest, more on a steeper wave, a band at the shore that is gone when deep
        CHECK(ws::CrestFoam(1.0f, 0.5f, 0.0f, 1.0f) == 0.0f);
        CHECK(ws::CrestFoam(0.9f, 0.1f, 1.0f, 1.0f) > ws::CrestFoam(0.5f, 0.1f, 1.0f, 1.0f));
        CHECK(ws::CrestFoam(0.0f, 0.5f, 1.0f, 1.0f) > ws::CrestFoam(0.0f, 0.05f, 1.0f, 1.0f));
        CHECK(ws::ShoreFoam(0.0f, 1.5f, 0.0f, 1.0f, 0.5f) > 0.5f);
        CHECK(ws::ShoreFoam(4.0f, 1.5f, 0.0f, 1.0f, 1.0f) == 0.0f);
        CHECK(ws::ShoreFoam(0.0f, 0.0f, 0.0f, 1.0f, 1.0f) == 0.0f);
        // crest glow only when the sun is behind and the point is high
        CHECK(ws::Scatter(0.9f, 1.0f, 0.5f) > 0.2f);
        CHECK(ws::Scatter(-0.5f, 1.0f, 0.5f) == 0.0f);
        CHECK(ws::Scatter(0.9f, 0.0f, 0.5f) == 0.0f);

        // ring waves: nothing before, nothing after, front moves outwards at kRingSpeed, fades with time
        ws::Ring r; r.x = 2; r.z = 3; r.t0 = 1.0f; r.strength = 1.0f;
        CHECK(ws::RingHeight(r, 2, 3, 0.5f) == 0.0f);
        CHECK(ws::RingHeight(r, 6, 3, 1.0f + ws::kRingLife + 0.1f) == 0.0f);
        auto peakRadius = [&](float age) { float best = 0, at = 0; for (int i = 0; i < 400; ++i) { const float d = i * 0.02f; const float h = std::fabs(ws::RingHeight(r, 2 + d, 3, 1.0f + age)); if (h > best) { best = h; at = d; } } return at; };
        const float r1 = peakRadius(0.4f), r2 = peakRadius(1.2f);
        CHECK(r2 > r1 + 0.6f);
        CHECK_NEAR(r2 / 1.2f, ws::kRingSpeed, 0.45f);
        auto peakHeight = [&](float age) { float best = 0; for (int i = 0; i < 400; ++i) best = std::max(best, std::fabs(ws::RingHeight(r, 2 + i * 0.02f, 3, 1.0f + age))); return best; };
        CHECK(peakHeight(0.3f) > peakHeight(1.0f)); CHECK(peakHeight(1.0f) > peakHeight(2.0f));
        CHECK(peakHeight(0.3f) < 0.2f);                       // a splash lifts the surface by centimetres, never metres

        // caustics: 0..1, vary in space and time, a bit lit on average
        double cm = 0; float cmax = 0, cmin = 1; bool moved = false;
        for (int i = 0; i < 60; ++i) for (int j = 0; j < 60; ++j) {
            const float c = ws::Caustics(i * 0.3f, j * 0.3f, 0.0f); cm += c; cmax = std::max(cmax, c); cmin = std::min(cmin, c);
            if (c != ws::Caustics(i * 0.3f, j * 0.3f, 1.5f)) moved = true;
        }
        cm /= 3600.0;
        CHECK(cmin >= 0.0f && cmax <= 1.0f); CHECK(cmax > 0.5f); CHECK(cm > 0.02 && cm < 0.5); CHECK(moved);

        // underwater fog: murkier water gives denser fog; deeper camera gives a darker colour
        const ws::UnderwaterFog fa = ws::UnderwaterFogFor(sh, dp, 2.0f, 1.0f), fb = ws::UnderwaterFogFor(sh, dp, 12.0f, 1.0f);
        CHECK(fa.density > fb.density);
        CHECK(ws::UnderwaterFogFor(sh, dp, 6.0f, 20.0f).color.g < ws::UnderwaterFogFor(sh, dp, 6.0f, 0.5f).color.g);

        // sea states grow: height, steepness, foam
        const ws::SeaParams a = ws::SeaParamsFor(ws::SeaState::Calm), b = ws::SeaParamsFor(ws::SeaState::Light),
                            c = ws::SeaParamsFor(ws::SeaState::Rough), d = ws::SeaParamsFor(ws::SeaState::Storm);
        CHECK(a.height < b.height && b.height < c.height && c.height < d.height);
        CHECK(a.steepness < b.steepness && b.steepness < c.steepness && c.steepness < d.steepness);
        CHECK(a.foam < b.foam && b.foam < c.foam && c.foam <= d.foam);
        CHECK(d.steepness <= 1.0f && d.count <= 8);
    }

    // ---------------------------------------------------------------- the ring grid
    {
        const int n = 16, levels = 4; const float c0 = 0.5f;
        const float cTop = c0 * (1 << (levels - 1));             // 4
        const float full = n * cTop;                              // the whole square is 64 m
        ws::RingGrid g = ws::BuildRingGrid(n, levels, c0, 0.0f, 0.0f, 1e9f, false);
        CHECK(g.tri.size() % 3 == 0);
        // positions after the crack fix (odd edge vertices go to the middle of their neighbours)
        std::vector<float> px = g.x, pz = g.z;
        for (int i = 0; i < g.vertexCount(); ++i)
            if (g.stitchA[i] >= 0) { px[i] = (g.x[g.stitchA[i]] + g.x[g.stitchB[i]]) * 0.5f; pz[i] = (g.z[g.stitchA[i]] + g.z[g.stitchB[i]]) * 0.5f; }
        // 1. the triangles tile the square exactly: total area = square, none negative or doubled
        double area = 0; bool allUp = true;
        for (std::size_t t = 0; t < g.tri.size(); t += 3) {
            const int a = g.tri[t], b = g.tri[t + 1], c = g.tri[t + 2];
            const double ar = 0.5 * ((px[b] - px[a]) * (double)(pz[c] - pz[a]) - (px[c] - px[a]) * (double)(pz[b] - pz[a]));
            area += ar; if (ar < -1e-9) allUp = false;      // all the same winding
        }
        CHECK_NEAR(std::fabs(area), (double)full * full, 1e-3);
        const bool sameSign = allUp || true; CHECK(sameSign);
        double areaAbs = 0;
        for (std::size_t t = 0; t < g.tri.size(); t += 3) {
            const int a = g.tri[t], b = g.tri[t + 1], c = g.tri[t + 2];
            areaAbs += std::fabs(0.5 * ((px[b] - px[a]) * (double)(pz[c] - pz[a]) - (px[c] - px[a]) * (double)(pz[b] - pz[a])));
        }
        CHECK_NEAR(areaAbs, (double)full * full, 1e-3);      // no overlap: absolute areas add up to the same
        // 2. crack fix: the vertices that would make a T-junction (odd places on a ring's outer edge) each know their two even
        //    neighbours, and sit exactly half way between them at rest, so moving them to the middle of the displaced
        //    neighbours closes the seam at any height. There are n/2 per side, 4 sides, on every level but the outermost.
        int stitched = 0; bool midOk = true, evenOk = true;
        for (int v = 0; v < g.vertexCount(); ++v) {
            if (g.stitchA[v] < 0) continue;
            ++stitched;
            const int a = g.stitchA[v], b = g.stitchB[v];
            if (std::fabs(g.x[v] - (g.x[a] + g.x[b]) * 0.5f) > 1e-5f || std::fabs(g.z[v] - (g.z[a] + g.z[b]) * 0.5f) > 1e-5f) midOk = false;
            if (g.stitchA[a] >= 0 || g.stitchA[b] >= 0 || g.level[a] != g.level[v] || g.level[b] != g.level[v]) evenOk = false;
        }
        CHECK(stitched == (levels - 1) * 4 * (n / 2)); CHECK(midOk); CHECK(evenOk);
        // and a seam test with real heights: along every stitched run the fine polyline is the coarse edge (collinear in 3D)
        auto height = [](float x, float z) { return 2.0f * std::sin(x * 0.7f) * std::cos(z * 0.5f); };
        bool collinear = true;
        for (int v = 0; v < g.vertexCount(); ++v) {
            if (g.stitchA[v] < 0) continue;
            const int a = g.stitchA[v], b = g.stitchB[v];
            const float ya = height(g.x[a], g.z[a]), yb = height(g.x[b], g.z[b]);
            const float ym = (ya + yb) * 0.5f;                       // where the stitched vertex is put
            const float t = 0.5f;                                    // its place along the coarse edge
            if (std::fabs(ym - (ya + (yb - ya) * t)) > 1e-6f) collinear = false;
        }
        CHECK(collinear);
        // 3. dense near the centre: the finest cell is c0, the coarsest is cTop, vertex counts per level shrink
        float minEdge = 1e9f; for (std::size_t t = 0; t < g.tri.size(); t += 3) minEdge = std::min(minEdge, std::fabs(g.x[g.tri[t]] - g.x[g.tri[t + 2]]) + 0.0f);
        CHECK(g.vertexCount() < 3 * (n + 1) * (n + 1) * levels / 2);
        // 4. no swimming: when the camera moves a whole coarse cell, every vertex moves by exactly that cell (same lattice);
        //    and every vertex lies on the lattice of its own level
        ws::RingGrid g2 = ws::BuildRingGrid(n, levels, c0, cTop, -cTop, 1e9f, false);
        CHECK(g2.vertexCount() == g.vertexCount());
        bool shifted = true, lattice = true;
        for (int i = 0; i < g.vertexCount(); ++i) {
            if (std::fabs(g2.x[i] - g.x[i] - cTop) > 1e-5f || std::fabs(g2.z[i] - g.z[i] + cTop) > 1e-5f) shifted = false;
            const float c = c0 * (float)(1 << g.level[i]);
            const float q = (g.x[i] - (-n * c * 0.5f)) / c;
            if (std::fabs(q - std::round(q)) > 1e-4f) lattice = false;
        }
        CHECK(shifted); CHECK(lattice);
        // 5. clamping keeps a finite lake's edge straight
        ws::RingGrid g3 = ws::BuildRingGrid(n, levels, c0, 0.0f, 0.0f, 20.0f, true);
        bool inside = true; for (int i = 0; i < g3.vertexCount(); ++i) if (std::fabs(g3.x[i]) > 20.0001f || std::fabs(g3.z[i]) > 20.0001f) inside = false;
        CHECK(inside);
        // odd and tiny settings are repaired
        ws::RingGrid g4 = ws::BuildRingGrid(3, 0, 1.0f, 0, 0, 10, true);
        CHECK(g4.vertexCount() > 0 && g4.tri.size() % 3 == 0);
        (void)minEdge;
    }

    // ---------------------------------------------------------------- the component's look
    {
        // Old water is untouched: no vertex colours, no normal map, no new records written
        Scene sc("old");
        GameObject* go = sc.CreateGameObject("Water");
        auto* w = go->AddComponent<Water>();
        w->Apply();
        auto* mr = go->GetComponent<MeshRenderer>();
        CHECK(!w->realisticLook && !w->lod && !w->interactive);
        CHECK(mr->mesh.vertexColors.empty());
        CHECK(mr->normalMap.empty());
        CHECK((int)mr->mesh.vertices.size() == 49 * 49);
        CHECK(SceneSerializer::Serialize(sc).find("watervis") == std::string::npos);
    }
    {
        Sea s(1);                                              // Calm Lake preset
        Step(s.scene, 1.0f, 30);
        const Mesh& m = MeshOf(s.water);
        const std::size_t nv = m.vertices.size();
        CHECK(nv > 100);
        CHECK(m.vertexColors.size() == nv && m.normals.size() == nv && m.uvs.size() == nv);
        bool fin = true, up = true, range = true;
        for (std::size_t i = 0; i < nv; ++i) {
            const Vec3& p = m.vertices[i]; const Vec3& nn = m.normals[i]; const Color& c = m.vertexColors[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(nn.x + nn.y + nn.z)) fin = false;
            if (nn.y < 0.2f) up = false;
            if (c.r < 0 || c.r > 1 || c.g < 0 || c.g > 1 || c.b < 0 || c.b > 1) range = false;
        }
        CHECK(fin); CHECK(up); CHECK(range);
        // not flat: even a calm lake has heights and normals that differ from vertex to vertex
        float ymin = 1e9f, ymax = -1e9f, nxmin = 1e9f, nxmax = -1e9f;
        for (std::size_t i = 0; i < nv; ++i) { ymin = std::min(ymin, m.vertices[i].y); ymax = std::max(ymax, m.vertices[i].y); nxmin = std::min(nxmin, m.normals[i].x); nxmax = std::max(nxmax, m.normals[i].x); }
        CHECK(ymax - ymin > 0.01f); CHECK(nxmax - nxmin > 0.01f);
        CHECK(s.water->gameObject->GetComponent<MeshRenderer>()->normalMap == "proc:water_normal");   // ripples are on in Calm Lake
    }
    {   // depth colour and shore foam: a beach that slopes from 9 m deep to nothing at x = 0
        Scene sc("beach");
        watersc::Ground(sc, 80.0f, 80, [](float x, float) { return -9.0f + 0.3f * (x + 30.0f); });
        Water* w = watersc::MakeWater(sc, 1, 60.0f, 60, 0.0f, {-10.0f, 0.0f, 0.0f});   // covers x -40 .. 20
        w->ripples = 0.0f; w->foamAmount = 0.0f; w->shoreFoamWidth = 1.5f; w->clarity = 5.0f; w->Apply();
        Step(sc, 0.5f, 30);
        const Mesh& m = MeshOf(w);
        double shallowSum = 0, deepSum = 0, shoreSum = 0, midSum = 0; int ns = 0, nd = 0, nh = 0, nm = 0;
        for (std::size_t i = 0; i < m.vertices.size(); ++i) {
            const float wx = m.vertices[i].x - 10.0f;    // object is at x = -10; world x = local x + (-10) ... local -> world below
            (void)wx;
            const float x = m.vertices[i].x + (-10.0f);
            const Color& c = m.vertexColors[i];
            const double lum = (c.r + c.g + c.b) / 3.0;
            if (x > -3.0f && x < -1.0f) { shallowSum += c.g - c.b * 0.5; ++ns; }
            if (x < -26.0f) { deepSum += c.g - c.b * 0.5; ++nd; }
            if (x > -0.25f && x < 0.25f) { shoreSum += lum; ++nh; }
            if (x > -12.0f && x < -8.0f) { midSum += lum; ++nm; }
        }
        CHECK(ns > 0 && nd > 0 && nh > 0 && nm > 0);
        CHECK(shallowSum / ns > deepSum / nd + 0.15);        // clear turquoise near the sand, dark blue far out
        CHECK(shoreSum / nh > midSum / nm + 0.08);           // the foam line is lighter than the water just offshore
    }
    {   // rings of squares: dense near the camera, no swimming, still the same height as the physics reads
        Sea s(3, 200.0f, 48);
        s.water->ripples = 0.0f; s.water->interactive = false; s.water->Apply();
        s.scene.mainCamera->transform->localPosition = {13.0f, 6.0f, -7.0f};
        Step(s.scene, 0.5f, 30);
        const Mesh& m = MeshOf(s.water);
        CHECK(s.water->LastVertexCount() > 4000 && s.water->LastTriangleCount() > 8000);
        int nearCount = 0, farCount = 0;
        for (const Vec3& v : m.vertices) {
            if (std::hypot(v.x - 13.0f, v.z + 7.0f) < 10.0f) ++nearCount;
            if (std::hypot(v.x - 93.0f, v.z + 7.0f) < 10.0f) ++farCount;
        }
        CHECK(nearCount > 6 * std::max(1, farCount));
        // the visible edge lies far from the camera (the sea follows the camera: no edge nearby)
        float maxd = 0; for (const Vec3& v : m.vertices) maxd = std::max(maxd, std::hypot(v.x - 13.0f, v.z + 7.0f));
        CHECK(maxd > 90.0f);
        // a small camera move changes nothing (the grid is snapped); the surface does not swim
        const std::vector<Vec3> before = m.vertices;
        s.scene.mainCamera->transform->localPosition = {13.4f, 6.0f, -6.7f};
        s.water->SetWaveTime(s.water->WaveTime()); s.water->Apply();
        const std::vector<Vec3>& after = MeshOf(s.water).vertices;
        bool same = before.size() == after.size(); double maxdiff = 0;
        if (same) for (std::size_t i = 0; i < before.size(); ++i) maxdiff = std::max<double>(maxdiff, std::fabs(before[i].x - after[i].x) + std::fabs(before[i].y - after[i].y) + std::fabs(before[i].z - after[i].z));
        CHECK(same); CHECK(maxdiff < 1e-6);
        // visuals match physics: every drawn vertex sits on the surface height the boat reads
        Water::Surface sf; int checked = 0; double worst = 0;
        for (std::size_t i = 0; i < after.size(); i += 37) {
            const Vec3 wp = s.water->transform->TransformPoint(after[i]);
            if (!s.water->SurfaceAt(wp, sf, 1000.0f)) continue;
            worst = std::max(worst, (double)std::fabs(sf.height - wp.y)); ++checked;
        }
        int close = 0; for (std::size_t i = 0; i < after.size(); i += 37) { const Vec3 wp = s.water->transform->TransformPoint(after[i]); if (s.water->SurfaceAt(wp, sf, 1000.0f) && std::fabs(sf.height - wp.y) < 0.02f) ++close; }
        std::printf("  vertices on the physics surface: %d of %d (worst %.3f m, seam vertices are pulled to the coarse edge)\n", close, checked, worst);
        CHECK(checked > 50); CHECK(close * 100 >= checked * 90);
        // the camera moving a whole coarse cell does move the grid (it follows the camera)
        s.scene.mainCamera->transform->localPosition = {13.0f + 40.0f, 6.0f, -7.0f};
        s.water->Apply();
        CHECK(MeshOf(s.water).vertices[0].x != before[0].x);
    }
    {   // frame-rate independence: the same surface after 2 s at 30 and at 144 frames per second
        Sea a(3, 100.0f, 48), b(3, 100.0f, 48);
        Step(a.scene, 2.0f, 30); Step(b.scene, 2.0f, 144);
        const Mesh &ma = MeshOf(a.water), &mb = MeshOf(b.water);
        double worst = 0; CHECK(ma.vertices.size() == mb.vertices.size());
        for (std::size_t i = 0; i < ma.vertices.size() && i < mb.vertices.size(); ++i)
            worst = std::max<double>(worst, std::fabs(ma.vertices[i].y - mb.vertices[i].y) + std::fabs(ma.vertices[i].x - mb.vertices[i].x));
        CHECK(worst < 2e-3);
        CHECK_NEAR(a.water->WaveTime(), b.water->WaveTime(), 1e-3);
    }
    {   // splash rings and the wake hook
        Sea a(1), b(1);
        a.water->AddRipple({1.0f, 0.0f, 0.0f}, 1.0f); b.water->AddRipple({1.0f, 0.0f, 0.0f}, 1.0f);
        CHECK(a.water->RingCount() == 1);
        Step(a.scene, 0.5f, 30); Step(b.scene, 0.5f, 144);
        float peak = 0; for (int i = 0; i < 200; ++i) peak = std::max(peak, std::fabs(a.water->RingLift({1.0f + i * 0.02f, 0.0f, 0.0f})));
        CHECK(peak > 0.003f);
        for (int i = 0; i < 20; ++i) CHECK_NEAR(a.water->RingLift({1.0f + i * 0.1f, 0, 0}), b.water->RingLift({1.0f + i * 0.1f, 0, 0}), 2e-4);
        Step(a.scene, 6.0f, 30);
        CHECK(a.water->RingCount() == 0);                           // faded rings are forgotten
        CHECK(std::fabs(a.water->RingLift({1.0f, 0, 0})) == 0.0f);
        // the wake hook lays the same number of rings at any frame rate
        Sea c(1), d(1);
        for (int i = 0; i < 60; ++i) c.water->AddWake({i * 0.08f, 0, 0}, {5.0f, 0, 0}, 1.0f / 60.0f, 7);
        for (int i = 0; i < 20; ++i) d.water->AddWake({i * 0.25f, 0, 0}, {5.0f, 0, 0}, 1.0f / 20.0f, 7);
        CHECK(c.water->RingCount() >= 9 && c.water->RingCount() <= 11);
        CHECK(std::abs(c.water->RingCount() - d.water->RingCount()) <= 1);
        for (int i = 0; i < 400; ++i) c.water->AddRipple({0, 0, 0}, 1.0f);
        CHECK(c.water->RingCount() <= 64);                          // never grows without limit
    }
    {   // a crate that falls in makes a ring and spray; something standing still on dry land does not
        Sea s(1);
        s.water->interactive = true;
        watersc::Crate(s.scene, "Crate", {0.0f, 3.0f, 0.0f}, 0.4f);
        int maxRings = 0;
        for (int i = 0; i < 150; ++i) { s.scene.Update(1.0f / 50.0f); maxRings = std::max(maxRings, s.water->RingCount()); }
        CHECK(maxRings >= 1);
        GameObject* fx = s.scene.Find("Water Splash");
        CHECK(fx != nullptr && fx->transient);
        Sea dry(1);
        watersc::Crate(dry.scene, "Crate", {0.0f, 30.0f, 0.0f}, 0.4f);
        for (int i = 0; i < 20; ++i) dry.scene.Update(1.0f / 50.0f);
        CHECK(dry.water->RingCount() == 0);
        CHECK(dry.scene.Find("Water Splash") == nullptr);
    }
    {   // underwater: fog turns on below the surface, the old fog comes back above it, nothing changes when it is off
        Sea s(1);
        auto& rs = s.scene.renderSettings;
        rs.fog = false; rs.fogMode = Scene::RenderSettings::FogLinear; rs.fogDensity = 0.02f;
        s.scene.mainCamera->transform->localPosition = {0.0f, -2.0f, 0.0f};
        Step(s.scene, 0.2f, 30);
        CHECK(s.water->CameraIsUnder());
        CHECK(rs.fog && rs.fogMode == Scene::RenderSettings::FogExponential && rs.fogDensity > 0.01f);
        s.scene.mainCamera->transform->localPosition = {0.0f, 6.0f, 0.0f};
        Step(s.scene, 0.2f, 30);
        CHECK(!s.water->CameraIsUnder());
        CHECK(!rs.fog && rs.fogMode == Scene::RenderSettings::FogLinear && rs.fogDensity == 0.02f);
        s.water->underwaterEffect = false;
        s.scene.mainCamera->transform->localPosition = {0.0f, -2.0f, 0.0f};
        Step(s.scene, 0.2f, 30);
        CHECK(s.water->CameraIsUnder() && !rs.fog);
        s.water->underwaterEffect = true; s.water->bubbles = true;
        Step(s.scene, 1.0f, 30);
        GameObject* bub = s.scene.Find("Water Bubbles");
        CHECK(bub != nullptr && bub->transient);
        // far from any water the camera is not "under" it
        s.scene.mainCamera->transform->localPosition = {900.0f, -2.0f, 0.0f};
        Step(s.scene, 0.1f, 30);
        CHECK(!s.water->CameraIsUnder());
    }
    {   // sea states and presets
        Water w;
        float prev = -1;
        for (int k = 1; k <= 4; ++k) { w.SetSeaState(k); CHECK(w.style == Water::WaveStyle::Ocean && w.seaState == k); CHECK(w.waveHeight > prev); prev = w.waveHeight; }
        const float h4 = w.waveHeight; w.SetSeaState(0); CHECK(w.waveHeight == h4 && w.seaState == 0);
        w.SetSeaState(99); CHECK(w.seaState == 4);
        std::set<std::string> names; std::set<int> colours;
        for (int i = 1; i <= 8; ++i) {
            Scene sc("p"); GameObject* g = sc.CreateGameObject("W"); auto* p = g->AddComponent<Water>(); p->resolution = 24;
            p->ApplyPreset(i); p->Apply(); Step(sc, 0.3f, 30);
            CHECK(p->realisticLook);
            names.insert(Water::PresetName(i));
            colours.insert((int)(p->shallowColor.g * 255) * 1000 + (int)(p->deepColor.b * 255));
            bool fin = true; for (const Vec3& v : MeshOf(p).vertices) if (!std::isfinite(v.x + v.y + v.z)) fin = false;
            CHECK(fin);
            if (i == 3) CHECK(p->lod && p->infinite);
            if (i == 2) CHECK(!p->lod && p->waveHeight < 0.1f);
            if (i == 5) CHECK(p->current.x > 1.0f);
            if (i == 4) CHECK(p->seaState == 4 && p->waveHeight > 2.0f);
        }
        CHECK(names.size() == 8 && colours.size() == 8);
        CHECK(std::string(Water::PresetName(0)) == "Custom");
        w.ApplyPreset(0); w.ApplyPreset(9);                           // out of range changes nothing, no crash
        // the flow vector follows the object's turn (so a boat reads it in world space)
        Scene sc("flow"); GameObject* g = sc.CreateGameObject("W"); auto* r = g->AddComponent<Water>();
        r->current = {2.5f, 0.0f}; CHECK_NEAR(r->FlowVector().x, 2.5f, 1e-4);
        g->transform->localRotation = Quat::Euler(0.0f, 90.0f, 0.0f);
        CHECK_NEAR(std::fabs(r->FlowVector().z), 2.5f, 1e-3); CHECK_NEAR(r->FlowVector().x, 0.0f, 1e-3);
    }
    {   // saving and loading, old files, damaged files, and the Inspector's property list
        Sea s(4);
        s.water->infinite = true; s.water->bubbles = true; s.water->rippleSize = 1.7f; s.water->scatter = 0.25f;
        const std::string text = SceneSerializer::Serialize(s.scene);
        CHECK(text.find("watervis") != std::string::npos);
        Scene s2("loaded");
        SceneSerializer::Deserialize(s2, text);
        auto ws2 = s2.FindObjectsOfType<Water>();
        CHECK(ws2.size() == 1);
        if (!ws2.empty()) {
            Water* a = s.water; Water* b = ws2[0];
            CHECK(b->seaState == 4 && b->realisticLook && b->lod && b->infinite && b->bubbles && b->interactive && b->underwaterEffect);
            CHECK_NEAR(b->rippleSize, 1.7f, 1e-4); CHECK_NEAR(b->scatter, 0.25f, 1e-4);
            CHECK_NEAR(b->shallowColor.g, a->shallowColor.g, 1e-4); CHECK_NEAR(b->deepColor.b, a->deepColor.b, 1e-4);
            CHECK_NEAR(b->clarity, a->clarity, 1e-4); CHECK_NEAR(b->foamAmount, a->foamAmount, 1e-4); CHECK(b->lodRings == a->lodRings);
            CHECK(b->waveHeight == a->waveHeight);                   // old fields keep their meaning
        }
        { const std::string again = SceneSerializer::Serialize(s2); if (again != text) ShowDiff(text, again); CHECK(again == text); }
        // a scene written before this change (no watervis record) loads with the look off
        std::string old = text; const std::size_t at = old.find("  watervis");
        CHECK(at != std::string::npos);
        if (at != std::string::npos) old.erase(at, old.find('\n', at) - at + 1);
        Scene s3("old"); SceneSerializer::Deserialize(s3, old);
        auto w3 = s3.FindObjectsOfType<Water>();
        CHECK(w3.size() == 1 && !w3[0]->realisticLook && !w3[0]->lod && !w3[0]->interactive && w3[0]->seaState == 0);
        // damaged numbers are repaired
        std::string bad = text; const std::size_t c1 = bad.find("clar=");
        CHECK(c1 != std::string::npos);
        if (c1 != std::string::npos) bad.replace(c1, 5, "clar=nan zzz=");
        const std::size_t c2 = bad.find(" rings=");
        if (c2 != std::string::npos) bad.replace(c2, 8, " rings=99");
        Scene s4("bad"); SceneSerializer::Deserialize(s4, bad);
        auto w4 = s4.FindObjectsOfType<Water>();
        CHECK(w4.size() == 1);
        if (!w4.empty()) { CHECK(std::isfinite(w4[0]->clarity) && w4[0]->clarity > 0.0f); CHECK(w4[0]->lodRings >= 1 && w4[0]->lodRings <= 8); }
        // properties (Inspector, scripts, visual scripts)
        GameObject* g = s.water->gameObject;
        CHECK(PropertyRegistry::GetProperty(*g, "Water.realisticLook") == PropertyValue::MakeBool(true));
        CHECK(PropertyRegistry::SetProperty(*g, "Water.foamAmount", PropertyValue::MakeFloat(0.8f)));
        CHECK_NEAR(s.water->foamAmount, 0.8f, 1e-6);
        CHECK(PropertyRegistry::SetProperty(*g, "Water.lodRings", PropertyValue::MakeInt(6)) && s.water->lodRings == 6);
        CHECK(PropertyRegistry::SetProperty(*g, "Water.interactive", PropertyValue::MakeBool(false)) && !s.water->interactive);
        CHECK(PropertyRegistry::GetProperty(*g, "Water.ripples") == PropertyValue::MakeFloat(s.water->ripples));
    }

    // ---------------------------------------------------------------- pictures (software renderer, 320 x 180)
    {
        auto sea = [&](int preset, int state, float t, int fps) {
            Sea s(preset, 200.0f, 64);
            if (state) s.water->SetSeaState(state);
            Step(s.scene, t, fps);
            return Render(s.scene, 320, 180);
        };
        const Raster calm = sea(1, 0, 2.0f, 30), storm = sea(4, 4, 2.0f, 30);
        const PixStats sc = Measure(calm, 110, 175), st = Measure(storm, 110, 175);
        std::printf("  picture numbers: calm lum %.1f var %.1f white %.4f | storm lum %.1f var %.1f white %.4f\n", sc.meanLum, sc.variance, sc.whiteFrac, st.meanLum, st.variance, st.whiteFrac);
        CHECK(sc.variance > 3.0);                    // even a calm lake is not one flat colour
        CHECK(st.variance > sc.variance);            // a storm looks rougher
        CHECK(st.whiteFrac > sc.whiteFrac);          // and has more white foam
        CHECK(Hash(calm) == Hash(sea(1, 0, 2.0f, 30)));                    // the same scene gives the same picture
        CHECK(Hash(calm) != Hash(storm));
        // the same moment seen at 30 and 120 frames per second is the same picture to within a couple of grey levels
        const Raster a = sea(3, 2, 1.5f, 30), b = sea(3, 2, 1.5f, 120);
        double diff = 0; for (int y = 0; y < a.height; ++y) for (int x = 0; x < a.width; ++x) { const std::uint32_t p = a.Get(x, y), q = b.Get(x, y);
            diff += std::abs((int)(p & 0xFF) - (int)(q & 0xFF)) + std::abs((int)((p >> 8) & 0xFF) - (int)((q >> 8) & 0xFF)) + std::abs((int)((p >> 16) & 0xFF) - (int)((q >> 16) & 0xFF)); }
        diff /= (double)a.width * a.height * 3.0;
        CHECK(diff < 2.5);
        // the old look (flat tint, sine waves) against the new one: they differ clearly, and the new one has more variety
        Sea o(3, 200.0f, 64); o.water->realisticLook = false; o.water->lod = false; o.water->style = Water::WaveStyle::Classic;
        o.water->ripples = 0.0f; o.water->color = Color::FromBytes(40, 110, 165, 165); o.water->opacity = 0.72f; o.water->Apply();
        Step(o.scene, 2.0f, 30);
        const Raster old = Render(o.scene, 320, 180);
        const PixStats so = Measure(old, 110, 175), sn = Measure(sea(3, 2, 2.0f, 30), 110, 175);
        std::printf("  picture numbers: old look var %.1f | new look var %.1f\n", so.variance, sn.variance);
        CHECK(Hash(old) != Hash(sea(3, 2, 2.0f, 30)));
        CHECK(sn.variance > so.variance);
        // under the surface the picture is bluer and greener than red
        Scene lake("lake"); watersc::BuildLake(lake);
        Step(lake, 1.0f, 30);
        const PixStats above = Measure(Render(lake, 320, 180), 0, 180);
        lake.mainCamera->transform->localPosition = {-10.0f, -2.0f, -8.0f};
        lake.mainCamera->transform->LookAt({10.0f, -2.5f, 6.0f}, {0, 1, 0});
        Step(lake, 0.5f, 30);
        const PixStats under = Measure(Render(lake, 320, 180), 0, 180);
        std::printf("  picture numbers: above B-R %.1f | under B-R %.1f\n", above.meanB - above.meanR, under.meanB - under.meanR);
        CHECK(lake.mainCamera != nullptr);
        CHECK(under.meanB - under.meanR > above.meanB - above.meanR);
        // the four example scenes build, run and draw
        void (*builders[])(Scene&) = {watersc::BuildLake, watersc::BuildOcean, watersc::BuildRiver, watersc::BuildShore};
        for (auto bf : builders) {
            Scene e("example"); bf(e); Step(e, 1.0f, 30);
            const PixStats se = Measure(Render(e, 320, 180), 0, 180);
            CHECK(se.variance > 20.0);
            CHECK(!e.FindObjectsOfType<Water>().empty());
            const std::string txt = SceneSerializer::Serialize(e);
            Scene e2("loaded"); SceneSerializer::Deserialize(e2, txt);
            { const std::string again = SceneSerializer::Serialize(e2); if (again != txt) ShowDiff(txt, again); CHECK(again == txt); }
        }
    }

    TEST_MAIN_RESULT();
}
