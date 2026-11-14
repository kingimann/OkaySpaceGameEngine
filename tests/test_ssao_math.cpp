#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Render/SsaoMath.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace okay;

// Screen-space ambient occlusion maths (SsaoMath.hpp), on the CPU:
//   * the pieces: screen radius, tap spiral, occlusion falloff, blur weights;
//   * a flat wall is never darkened, a floor/wall crease is, sky is not;
//   * a depth image taken from the SOFTWARE renderer (view depth / far, the same
//     value the Direct3D 11 depth pass writes) gets its contact shadow under a box;
//   * the numbers in the OpenGL shader, the Direct3D 11 shader and this header
//     have not drifted apart (the GL/D3D11 source files are read as text).
namespace {
std::string Slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
std::string Num(double v) {   // 150 -> "150.0" like the shaders write it? no: plain %g style
    char b[64]; std::snprintf(b, sizeof(b), "%g", v); return b;
}
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
} // namespace

int main() {
    RUN_SUITE("ssao_math");
    using namespace okay::ssao;

    // --- the pieces -------------------------------------------------------------
    {
        // Screen radius shrinks with distance and is clamped.
        CHECK_NEAR(ScreenRadius(0.45f, 10.0f / kFar), 0.45f / 10.0f, 1e-6f);
        CHECK(ScreenRadius(0.45f, 40.0f / kFar) < ScreenRadius(0.45f, 10.0f / kFar));
        CHECK_NEAR(ScreenRadius(0.45f, 0.0f), 0.12f, 1e-6f);               // very close: capped at 0.12
        CHECK_NEAR(ScreenRadius(0.45f, 1.0f), 0.004f, 1e-6f);              // very far: floor of 0.004
        CHECK_NEAR(ScreenRadius(0.0f, 0.2f), 0.004f, 1e-6f);

        // The spiral: tap 0 sits at 25% of the radius, the last tap at 100%, all
        // distinct, and a wide target stretches x so the disc stays round.
        float ox, oy;
        TapOffset(0, 0.1f, 1.0f, ox, oy);
        CHECK_NEAR(std::sqrt(ox * ox + oy * oy), 0.025f, 1e-6f);
        TapOffset(kTaps - 1, 0.1f, 1.0f, ox, oy);
        CHECK_NEAR(std::sqrt(ox * ox + oy * oy), 0.1f, 1e-5f);
        float ax, ay; TapOffset(5, 0.1f, 2.0f, ax, ay);
        float bx, by; TapOffset(5, 0.1f, 1.0f, bx, by);
        CHECK_NEAR(ax, bx * 2.0f, 1e-6f); CHECK_NEAR(ay, by, 1e-6f);
        for (int i = 0; i < kTaps; ++i)
            for (int j = i + 1; j < kTaps; ++j) {
                float x0, y0, x1, y1; TapOffset(i, 0.1f, 1.0f, x0, y0); TapOffset(j, 0.1f, 1.0f, x1, y1);
                CHECK(std::fabs(x0 - x1) + std::fabs(y0 - y1) > 1e-4f);
            }

        // A tap adds occlusion only when nearer by 0.03 .. 1.6 x radius, fading out.
        const float r = 0.5f, d = 0.2f;                                    // d = 30 world units
        CHECK_NEAR(TapOcclusion(d, d, r), 0.0f, 1e-9f);                    // same depth
        CHECK_NEAR(TapOcclusion(d, d + 0.01f, r), 0.0f, 1e-9f);            // tap further away
        CHECK_NEAR(TapOcclusion(d, d - 0.02f / kFar, r), 0.0f, 1e-9f);     // gap 0.02 < 0.03: ignored
        float near1 = TapOcclusion(d, d - 0.1f / kFar, r);                 // gap 0.1: nearly full
        float near2 = TapOcclusion(d, d - 0.4f / kFar, r);                 // gap 0.4: weaker
        CHECK(near1 > 0.85f && near1 < 1.0f);
        CHECK(near2 < near1 && near2 > 0.0f);
        CHECK_NEAR(near2, 1.0f - 0.4f / (r * 1.6f), 1e-4f);
        CHECK_NEAR(TapOcclusion(d, d - 0.9f / kFar, r), 0.0f, 1e-9f);      // gap 0.9 > 0.8: out of range

        // Apply: strength blends 1 -> ao and is capped at 1.
        CHECK_NEAR(Apply(0.4f, 0.0f), 1.0f, 1e-6f);
        CHECK_NEAR(Apply(0.4f, 1.0f), 0.4f, 1e-6f);
        CHECK_NEAR(Apply(0.4f, 0.5f), 0.7f, 1e-6f);
        CHECK_NEAR(Apply(0.4f, 3.0f), 0.4f, 1e-6f);

        // Blur weights add to 1, so a flat image stays flat; an impulse spreads
        // but keeps its total and stays symmetric.
        CHECK_NEAR(kBlurW0 + 2 * kBlurW1 + 2 * kBlurW2, 1.0f, 1e-5f);
        Grid flat(16, 16, 0.6f);
        Grid fb = Blur(Blur(flat, 1, 0), 0, 1);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) CHECK_NEAR(fb.at(x, y), 0.6f, 1e-4f);
        Grid imp(21, 1, 0.0f); imp.at(10, 0) = 1.0f;
        Grid ib = Blur(imp, 1, 0);
        float total = 0; for (int x = 0; x < 21; ++x) total += ib.at(x, 0);
        CHECK_NEAR(total, 1.0f, 0.02f);
        CHECK_NEAR(ib.at(8, 0), ib.at(12, 0), 1e-4f);
        CHECK(ib.at(10, 0) > ib.at(9, 0) && ib.at(9, 0) > ib.at(6, 0));
        CHECK(ib.at(10, 0) < 1.0f);
    }

    // --- synthetic depth images ---------------------------------------------------
    {
        const int N = 64;
        // A flat wall facing the camera, 20 units away: no occlusion anywhere.
        Grid wall(N, N, 20.0f / kFar);
        Grid ao = Estimate(wall, 0.5f);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) CHECK_NEAR(ao.at(x, y), 1.0f, 1e-6f);

        // Sky (depth 1) is never occluded, even next to geometry.
        Grid sky(N, N, 1.0f);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N / 2; ++x) sky.at(x, y) = 12.0f / kFar;
        Grid sao = Estimate(sky, 0.5f);
        for (int y = 0; y < N; ++y) for (int x = N / 2; x < N; ++x) CHECK_NEAR(sao.at(x, y), 1.0f, 1e-6f);

        // A wall in front of a far floor: the far side, just beside the edge, is
        // occluded (the near wall is within range of the radius); far from the
        // edge it is open.
        Grid step(N, N, 10.2f / kFar);                       // far surface 10.2 away
        for (int y = 0; y < N; ++y) for (int x = 0; x < N / 2; ++x) step.at(x, y) = 10.0f / kFar;   // near surface 10.0
        Grid sa = Estimate(step, 0.5f);
        float nextToEdge = sa.at(N / 2, N / 2);
        float farAway = sa.at(N - 2, N / 2);
        CHECK(nextToEdge < 0.9f);
        CHECK_NEAR(farAway, 1.0f, 1e-6f);
        CHECK_NEAR(sa.at(2, N / 2), 1.0f, 1e-6f);             // the near surface itself is not occluded by the far one
        // More radius, more darkening; the blur smooths but keeps the order.
        Grid sa2 = Estimate(step, 1.0f);
        CHECK(sa2.at(N / 2 + 1, N / 2) <= nextToEdge + 1e-6f);
        Grid sb = Blur(Blur(sa, 1, 0), 0, 1);
        CHECK(sb.at(N / 2 + 1, N / 2) < sb.at(N - 2, N / 2));
        // Larger distance: the same world radius covers fewer pixels.
        CHECK(ScreenRadius(0.5f, 10.0f / kFar) > ScreenRadius(0.5f, 40.0f / kFar));
    }

    // --- a real frame from the software renderer ----------------------------------
    {
        Scene s("frame");
        GameObject* ground = s.CreateGameObject("Ground");
        ground->AddComponent<MeshRenderer>()->mesh = Mesh::Plane(40.0f);
        GameObject* box = s.CreateGameObject("Box");
        box->transform->localPosition = {0, 1, 0};
        box->AddComponent<MeshRenderer>()->mesh = Mesh::Cube(2.0f);
        const Vec3 eye{0, 7, 9};
        const Mat4 vp = Mat4::Perspective(60.0f, 1.0f, 0.1f, 200.0f) * Mat4::LookAt(eye, Vec3{0, 0, 0}, Vec3{0, 1, 0});
        const int W = 128;
        Raster r; r.Resize(W, W); r.Clear(0xFF000000u);
        RenderMeshes(r, s, vp, eye);
        // The software W-buffer holds 1/w (larger = nearer); view depth / far is
        // what the Direct3D 11 depth pass writes. 0 = nothing drawn = sky.
        Grid depth(W, W, 1.0f);
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < W; ++x) {
                float iw = r.Depth(x, y);
                if (iw > 1e-6f) { float z = 1.0f / iw / kFar; depth.at(x, y) = z > 1.0f ? 1.0f : z; }
            }
        Grid ao = Blur(Blur(Estimate(depth, 0.45f), 1, 0), 0, 1);
        auto pix = [&](const Vec3& p, int& px, int& py) {
            Vec4 c = vp * Vec4{p, 1.0f};
            px = (int)((c.x / c.w * 0.5f + 0.5f) * W); py = (int)((1.0f - (c.y / c.w * 0.5f + 0.5f)) * W);
        };
        int cx, cy, ox, oy;
        pix(Vec3{0, 0, 1.08f}, cx, cy);                  // floor right at the box's front foot
        pix(Vec3{3, 0, 3.0f}, ox, oy);                   // open floor, away from the box
        CHECK(cx >= 0 && cx < W && cy >= 0 && cy < W && ox >= 0 && ox < W && oy >= 0 && oy < W);
        CHECK(depth.at(cx, cy) < 1.0f && depth.at(ox, oy) < 1.0f);   // both are real surfaces
        // No normals are used (as on OpenGL), so a floor seen at a slant darkens a
        // little by itself; the crease at the foot must be darker than that.
        CHECK(ao.at(ox, oy) > 0.6f);                     // open floor stays bright
        CHECK(ao.at(cx, cy) < ao.at(ox, oy) - 0.03f);    // the crease at the foot is darker
        // Floor never gets lighter than open, nothing leaves 0..1.
        float lo = 1.0f, hi = 0.0f;
        for (float v : ao.px) { lo = std::fmin(lo, v); hi = std::fmax(hi, v); }
        CHECK(lo >= 0.0f && hi <= 1.0001f && lo < 0.7f);
    }

    // --- shared constants: header, HLSL, GL and D3D11 sources ------------------------
    {
        const std::string hlsl = OKAY_SSAO_HLSL;
        CHECK(Has(hlsl, "PSAoDepth") && Has(hlsl, "PSAo(") && Has(hlsl, "PSAoMul"));
        CHECK(Has(hlsl, "k < " + std::to_string(kTaps)));
        CHECK(Has(hlsl, "* " + std::string(OKAY_SSAO_STR(OKAY_SSAO_GOLDEN))));
        CHECK(Has(hlsl, "d >= " + std::string(OKAY_SSAO_STR(OKAY_SSAO_BACKGROUND))));
        CHECK(Has(hlsl, "uP7.x * " + std::string(OKAY_SSAO_STR(OKAY_SSAO_RANGE_SCALE))));
        CHECK(Has(hlsl, "diffW > " + std::string(OKAY_SSAO_STR(OKAY_SSAO_MIN_DIFF))));
        CHECK(Has(hlsl, std::string(OKAY_SSAO_STR(OKAY_SSAO_SR_MIN)) + ", " + OKAY_SSAO_STR(OKAY_SSAO_SR_MAX)));
        CHECK(Has(hlsl, std::string(OKAY_SSAO_STR(OKAY_SSAO_RING_MIN)) + " + " + OKAY_SSAO_STR(OKAY_SSAO_RING_SPAN) + " * float(k) / " + std::to_string(kTaps - 1)));

        // The OpenGL shader and the Direct3D 11 renderer must say the same things.
        std::string here = __FILE__;
        std::string root = here.substr(0, here.find_last_of("/\\") + 1) + "../engine/src/Render/";
        const std::string gl = Slurp(root + "GLRenderer.cpp");
        const std::string dx = Slurp(root + "D3D11Renderer.cpp");
        CHECK(!gl.empty() && !dx.empty());
        if (!gl.empty()) {
            CHECK(Has(gl, "kSsaoFar = 150.0f"));
            CHECK(Has(gl, "i < 12"));
            CHECK(Has(gl, "float(i) * 2.618034"));
            CHECK(Has(gl, "d >= 0.995"));
            CHECK(Has(gl, "max(d * uFar, 0.5)"));
            CHECK(Has(gl, "clamp(uRadius / dist, 0.004, 0.12)"));
            CHECK(Has(gl, "uRadius * 1.6"));
            CHECK(Has(gl, "diffW > 0.03 && diffW < range"));
            CHECK(Has(gl, "0.25 + 0.75 * float(i) / 11.0"));
            CHECK(Has(gl, "occ / 12.0"));
            CHECK(Has(gl, "0.227027") && Has(gl, "0.316216") && Has(gl, "0.070270"));
            CHECK(Has(gl, "1.384615") && Has(gl, "3.230769"));
        }
        if (!dx.empty()) {
            CHECK(Has(dx, "0.227027") && Has(dx, "0.316216") && Has(dx, "0.070270"));   // PSBlur, reused for the AO blur
            CHECK(Has(dx, "1.384615") && Has(dx, "3.230769"));
            CHECK(Has(dx, "OKAY_SSAO_HLSL"));
            CHECK(Has(dx, "PSAoDepth") && Has(dx, "PSAoMul"));
            CHECK(Has(dx, "ssao::kFar"));
            CHECK(Has(dx, "rsTop.post.ssao || SSAOEnabled()"));   // only when an AO switch is on
        }
        // The blur weights in the header are the ones the shaders use.
        CHECK(Num(kBlurW0) == "0.227027" && Num(kBlurW1) == "0.316216" && Num(kBlurW2) == "0.07027");
    }

    TEST_MAIN_RESULT();
}
