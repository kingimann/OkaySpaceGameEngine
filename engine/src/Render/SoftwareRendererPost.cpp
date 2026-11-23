// Software renderer post effects.
// Bodies of okay:: free functions moved out of engine/include/okay/Render/SoftwareRenderer.hpp (modularization plan M2);
// the declarations (with their default arguments) stay in the header. The code is unchanged.
#include "okay/Render/SoftwareRenderer.hpp"

namespace okay {

void ApplySSAO(Raster& r, const Mat4& vp, const Vec3& eye) {
    if (r.gvalid.empty()) return;
    const int W = r.width, H = r.height;
    const float radius = SSAORadius(), strength = SSAOStrength();
    static const int K = 8;
    static Vec3 kern[K];
    static bool init = false;
    if (!init) {
        init = true; unsigned s = 1469598103u;
        auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xFFFFFF) / 16777216.0f; };
        for (int k = 0; k < K; ++k) {
            float x = rnd() * 2 - 1, y = rnd() * 2 - 1, z = rnd() * 0.85f + 0.15f;
            float l = std::sqrt(x * x + y * y + z * z); if (l < 1e-4f) l = 1;
            float sc = 0.2f + 0.8f * ((float)(k + 1) / K) * ((float)(k + 1) / K);
            kern[k] = Vec3{x / l, y / l, z / l} * sc;
        }
    }
    // Compute AO at HALF resolution (1/4 the hemisphere evaluations), then
    // bilinear-upsample when modulating the full-res color. The G-buffer stays
    // full-res; each half-res texel samples the G-buffer pixel under it.
    const int hw = (W + 1) / 2, hh = (H + 1) / 2;
    std::vector<float> ao((std::size_t)hw * hh, 1.0f);
    ParallelRows(0, hh, [&](int hya, int hyb) {
        for (int hy = hya; hy < hyb; ++hy)
        for (int hx = 0; hx < hw; ++hx) {
            int x = hx * 2, y = hy * 2;
            std::size_t i = (std::size_t)y * W + x;
            if (!r.gvalid[i]) continue;
            Vec3 P = r.gpos[i], N = r.gnrm[i];
            Vec3 up = std::fabs(N.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
            Vec3 T = Vec3::Cross(up, N); { float l = T.Magnitude(); T = l > 1e-5f ? T * (1.0f / l) : Vec3{1, 0, 0}; }
            Vec3 B = Vec3::Cross(N, T);
            float occWeighted = 0.0f; int tot = 0;
            for (int k = 0; k < K; ++k) {
                Vec3 sp = P + (T * kern[k].x + B * kern[k].y + N * kern[k].z) * radius;
                Vec4 c = vp * Vec4{sp, 1.0f}; if (c.w <= 0) continue;
                float iw = 1.0f / c.w;
                int sx = (int)((c.x * iw * 0.5f + 0.5f) * W), sy = (int)((1.0f - (c.y * iw * 0.5f + 0.5f)) * H);
                if (sx < 0 || sy < 0 || sx >= W || sy >= H) continue;
                std::size_t j = (std::size_t)sy * W + sx; ++tot;
                if (!r.gvalid[j]) continue;
                float diff = (sp - eye).Magnitude() - (r.gpos[j] - eye).Magnitude();
                // Smooth range falloff so distant geometry doesn't cast wide halos.
                if (diff > 0.015f) {
                    float w = 1.0f - diff / radius;       // fades out past `radius`
                    if (w > 0.0f) occWeighted += w;
                }
            }
            if (tot > 0) { float a = 1.0f - strength * occWeighted / tot; ao[(std::size_t)hy * hw + hx] = a < 0 ? 0 : a; }
        }
    });
    // Blur the half-res AO (3x3 box) to kill the hemisphere-sampling noise — SSAO
    // is inherently grainy and needs this smoothing pass. Cheap at half resolution.
    {
        static std::vector<float> aob; aob = ao;
        ParallelRows(0, hh, [&](int ya, int yb) {
            for (int y = ya; y < yb; ++y)
            for (int x = 0; x < hw; ++x) {
                float s = 0; int c = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    int ny = y + dy; if (ny < 0 || ny >= hh) continue;
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = x + dx; if (nx < 0 || nx >= hw) continue;
                        s += aob[(std::size_t)ny * hw + nx]; ++c;
                    }
                }
                ao[(std::size_t)y * hw + x] = c ? s / c : 1.0f;
            }
        });
    }
    // Bilinear-upsample the half-res AO and modulate the full-res color.
    const float aoPower = SSAOPower();
    ParallelRows(0, H, [&](int ya, int yb) {
        for (int y = ya; y < yb; ++y) {
        float fy = y * 0.5f; int y0 = (int)fy; float ay = fy - y0; int y1 = y0 + 1;
        if (y1 >= hh) y1 = hh - 1;
        for (int x = 0; x < W; ++x) {
            std::size_t i = (std::size_t)y * W + x;
            if (!r.gvalid[i]) continue;
            float fx = x * 0.5f; int x0 = (int)fx; float ax = fx - x0; int x1 = x0 + 1;
            if (x1 >= hw) x1 = hw - 1;
            float a00 = ao[(std::size_t)y0 * hw + x0], a10 = ao[(std::size_t)y0 * hw + x1];
            float a01 = ao[(std::size_t)y1 * hw + x0], a11 = ao[(std::size_t)y1 * hw + x1];
            float a = (a00 + (a10 - a00) * ax) + ((a01 + (a11 - a01) * ax) - (a00 + (a10 - a00) * ax)) * ay;
            a = postq::AoCurve(a, aoPower);
            std::uint32_t v = r.color[i];
            std::uint32_t R = (std::uint32_t)((v & 0xFF) * a), G = (std::uint32_t)(((v >> 8) & 0xFF) * a), Bc = (std::uint32_t)(((v >> 16) & 0xFF) * a);
            r.color[i] = (0xFFu << 24) | (Bc << 16) | (G << 8) | R;
        }
        }
    });
}

void ApplyBloom(Raster& r) {
    if (!BloomEnabled()) return;
    int W = r.width, H = r.height;
    if (W < 8 || H < 8) return;
    const int DS = 4;                       // downsample factor
    int dw = W / DS, dh = H / DS;
    if (dw < 2 || dh < 2) return;
    std::size_t dN = (std::size_t)dw * dh;
    static std::vector<float> br, tmp;       // low-res bright pass (3ch interleaved)
    br.assign(dN * 3, 0.0f); tmp.assign(dN * 3, 0.0f);
    float t = BloomThreshold() * 255.0f;
    // Downsample + threshold: average each DS x DS block's over-threshold energy.
    const float invBlock = 1.0f / (float)(DS * DS);
    for (int dy = 0; dy < dh; ++dy)
        for (int dx = 0; dx < dw; ++dx) {
            float acc[3] = {0, 0, 0};
            int sy0 = dy * DS, sx0 = dx * DS;
            for (int yy = 0; yy < DS; ++yy) {
                const std::uint32_t* row = &r.color[(std::size_t)(sy0 + yy) * W + sx0];
                for (int xx = 0; xx < DS; ++xx) {
                    std::uint32_t v = row[xx];
                    float c0 = (float)(v & 0xFF) - t, c1 = (float)((v >> 8) & 0xFF) - t, c2 = (float)((v >> 16) & 0xFF) - t;
                    if (c0 > 0) acc[0] += c0; if (c1 > 0) acc[1] += c1; if (c2 > 0) acc[2] += c2;
                }
            }
            std::size_t di = ((std::size_t)dy * dw + dx) * 3;
            br[di] = acc[0] * invBlock; br[di + 1] = acc[1] * invBlock; br[di + 2] = acc[2] * invBlock;
        }
    // Separable box blur (3 passes ~ gaussian) at low res — cheap.
    int rad = 3;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < dh; ++y)                      // horizontal
            for (int k = 0; k < 3; ++k) {
                float sum = 0; int cnt = 2 * rad + 1;
                for (int x = -rad; x <= rad; ++x) { int xx = x < 0 ? 0 : (x >= dw ? dw - 1 : x); sum += br[((std::size_t)y * dw + xx) * 3 + k]; }
                for (int x = 0; x < dw; ++x) {
                    tmp[((std::size_t)y * dw + x) * 3 + k] = sum / cnt;
                    int add = x + rad + 1, sub = x - rad;
                    add = add >= dw ? dw - 1 : add; sub = sub < 0 ? 0 : sub;
                    sum += br[((std::size_t)y * dw + add) * 3 + k] - br[((std::size_t)y * dw + sub) * 3 + k];
                }
            }
        for (int x = 0; x < dw; ++x)                      // vertical
            for (int k = 0; k < 3; ++k) {
                float sum = 0; int cnt = 2 * rad + 1;
                for (int y = -rad; y <= rad; ++y) { int yy = y < 0 ? 0 : (y >= dh ? dh - 1 : y); sum += tmp[((std::size_t)yy * dw + x) * 3 + k]; }
                for (int y = 0; y < dh; ++y) {
                    br[((std::size_t)y * dw + x) * 3 + k] = sum / cnt;
                    int add = y + rad + 1, sub = y - rad;
                    add = add >= dh ? dh - 1 : add; sub = sub < 0 ? 0 : sub;
                    sum += tmp[((std::size_t)add * dw + x) * 3 + k] - tmp[((std::size_t)sub * dw + x) * 3 + k];
                }
            }
    }
    // Bilinear-upsample the low-res bloom and add it back at full resolution.
    float intensity = BloomIntensity();
    float sxScale = (float)dw / W, syScale = (float)dh / H;
    ParallelRows(0, H, [&](int ya, int yb) {
        for (int y = ya; y < yb; ++y) {
        float fy = (y + 0.5f) * syScale - 0.5f; int y0 = (int)std::floor(fy); float ay = fy - y0;
        int y1 = y0 + 1; if (y0 < 0) { y0 = 0; } if (y1 < 0) y1 = 0; if (y0 >= dh) y0 = dh - 1; if (y1 >= dh) y1 = dh - 1;
        for (int x = 0; x < W; ++x) {
            float fx = (x + 0.5f) * sxScale - 0.5f; int x0 = (int)std::floor(fx); float ax = fx - x0;
            int x1 = x0 + 1; if (x0 < 0) { x0 = 0; } if (x1 < 0) x1 = 0; if (x0 >= dw) x0 = dw - 1; if (x1 >= dw) x1 = dw - 1;
            std::size_t i00 = ((std::size_t)y0 * dw + x0) * 3, i10 = ((std::size_t)y0 * dw + x1) * 3;
            std::size_t i01 = ((std::size_t)y1 * dw + x0) * 3, i11 = ((std::size_t)y1 * dw + x1) * 3;
            std::uint32_t v = r.color[(std::size_t)y * W + x];
            float o[3];
            for (int k = 0; k < 3; ++k) {
                float top = br[i00 + k] + (br[i10 + k] - br[i00 + k]) * ax;
                float bot = br[i01 + k] + (br[i11 + k] - br[i01 + k]) * ax;
                float bl = top + (bot - top) * ay;
                float c = (float)((v >> (k * 8)) & 0xFF) + bl * intensity;
                o[k] = c > 255 ? 255 : c;
            }
            r.color[(std::size_t)y * W + x] = (v & 0xFF000000u) | ((std::uint32_t)o[2] << 16) | ((std::uint32_t)o[1] << 8) | (std::uint32_t)o[0];
        }
        }
    });
}

void ApplyToneMap(Raster& r) {
    if (!ToneMapEnabled()) return;
    int W = r.width, H = r.height;
    if (W < 1 || H < 1) return;
    float e = Exposure();
    auto aces = [](float x) {
        // x in linear light; returns display value in [0,1].
        float n = x * (2.51f * x + 0.03f);
        float d = x * (2.43f * x + 0.59f) + 0.14f;
        float o = d > 1e-6f ? n / d : 0.0f;
        return o < 0.0f ? 0.0f : (o > 1.0f ? 1.0f : o);
    };
    ParallelRows(0, H, [&](int ya, int yb) {
        for (std::size_t i = (std::size_t)ya * W, end = (std::size_t)yb * W; i < end; ++i) {
            std::uint32_t v = r.color[i];
            float c0 = (float)( v        & 0xFF) / 255.0f;
            float c1 = (float)((v >> 8)  & 0xFF) / 255.0f;
            float c2 = (float)((v >> 16) & 0xFF) / 255.0f;
            float o0 = aces(c0 * e), o1 = aces(c1 * e), o2 = aces(c2 * e);
            r.color[i] = (v & 0xFF000000u)            // keep alpha (transparent bg = skybox)
                       | ((std::uint32_t)(o2 * 255.0f + 0.5f) << 16)
                       | ((std::uint32_t)(o1 * 255.0f + 0.5f) << 8)
                       |  (std::uint32_t)(o0 * 255.0f + 0.5f);
        }
    });
}

void ApplyColorGrade(Raster& r) {
    if (!ColorGradeEnabled()) return;
    int W = r.width, H = r.height;
    if (W < 1 || H < 1) return;
    const float br = Brightness(), ct = Contrast(), sat = Saturation();
    const float vig = Vignette() < 0.0f ? 0.0f : (Vignette() > 1.0f ? 1.0f : Vignette());
    const float invG = Gamma() > 1e-3f ? 1.0f / Gamma() : 1.0f;
    const bool doGamma = Mathf::Abs(Gamma() - 1.0f) > 1e-3f;
    const float cx = (W - 1) * 0.5f, cy = (H - 1) * 0.5f;
    const float maxR2 = cx * cx + cy * cy + 1e-6f;
    ParallelRows(0, H, [&](int ya, int yb) {
        for (int y = ya; y < yb; ++y) {
            float dy = (y - cy);
            for (int x = 0; x < W; ++x) {
                std::size_t i = (std::size_t)y * W + x;
                std::uint32_t v = r.color[i];
                float cr = (float)( v        & 0xFF) / 255.0f;
                float cg = (float)((v >> 8)  & 0xFF) / 255.0f;
                float cb = (float)((v >> 16) & 0xFF) / 255.0f;
                cr *= br; cg *= br; cb *= br;                       // brightness
                cr = (cr - 0.5f) * ct + 0.5f;                       // contrast about mid-grey
                cg = (cg - 0.5f) * ct + 0.5f;
                cb = (cb - 0.5f) * ct + 0.5f;
                if (sat != 1.0f) {                                  // saturation toward luma
                    float l = 0.2126f * cr + 0.7152f * cg + 0.0722f * cb;
                    cr = l + (cr - l) * sat; cg = l + (cg - l) * sat; cb = l + (cb - l) * sat;
                }
                if (vig > 0.0f) {                                  // edge darkening
                    float dx = (x - cx);
                    float f = 1.0f - vig * ((dx * dx + dy * dy) / maxR2);
                    if (f < 0.0f) f = 0.0f;
                    cr *= f; cg *= f; cb *= f;
                }
                if (doGamma) { cr = std::pow(cr < 0 ? 0 : cr, invG);
                               cg = std::pow(cg < 0 ? 0 : cg, invG);
                               cb = std::pow(cb < 0 ? 0 : cb, invG); }
                auto cl = [](float c) { return (std::uint32_t)((c < 0 ? 0 : (c > 1 ? 1 : c)) * 255.0f + 0.5f); };
                r.color[i] = (v & 0xFF000000u) | (cl(cb) << 16) | (cl(cg) << 8) | cl(cr);
            }
        }
    });
}

void ApplyScenePost(Raster& r, const ScenePostPlan& p) {
    if (!p.any()) return;
    const int W = r.width, H = r.height;
    if (W < 1 || H < 1) return;
    const float aspect = (float)W / (float)H;
    ParallelRows(0, H, [&](int ya, int yb) {
        for (int y = ya; y < yb; ++y)
        for (int x = 0; x < W; ++x) {
            const std::size_t i = (std::size_t)y * W + x;
            const std::uint32_t px = r.color[i];
            Vec3 c = ScenePostColor(p, {(float)(px & 0xFF) / 255.0f, (float)((px >> 8) & 0xFF) / 255.0f,
                                        (float)((px >> 16) & 0xFF) / 255.0f});
            std::uint32_t a = px >> 24;
            if (p.vignette > 0.0f) {
                // Same coverage maths as the GPU pass: the frame's own alpha is its coverage, and the
                // vignette adds black coverage on top, so an empty (sky) pixel still darkens at the edge.
                const float v = ScenePostVignette(p, ((float)x + 0.5f) / W, ((float)y + 0.5f) / H, aspect);
                const float cov = (float)a / 255.0f;
                const float oa = 1.0f - (1.0f - v) * (1.0f - cov);
                const float k = oa > 1e-4f ? cov * (1.0f - v) / oa : 0.0f;
                c = c * k; a = (std::uint32_t)(oa * 255.0f + 0.5f);
            }
            auto cl = [](float f) { return (std::uint32_t)((f < 0 ? 0 : (f > 1 ? 1 : f)) * 255.0f + 0.5f); };
            r.color[i] = (a << 24) | (cl(c.z) << 16) | (cl(c.y) << 8) | cl(c.x);
        }
    });
}

void ApplySceneDof(Raster& r, float focus, float range, const Mat4& vp) {
    const int W = r.width, H = r.height;
    if (W < 4 || H < 4 || r.depth.size() != (std::size_t)W * H || !DofPerspective(vp)) return;
    // Circle of confusion per pixel; nothing to do when the whole frame is in focus.
    static std::vector<float> coc;
    coc.assign((std::size_t)W * H, 0.0f);
    const DofRays rays = DofRaysFor(vp);
    bool any = false;
    for (int y = 0; y < H; ++y) {
        const float ny = 1.0f - (y + 0.5f) * 2.0f / H;
        for (int x = 0; x < W; ++x) {
            const std::size_t i = (std::size_t)y * W + x;
            const float nx = (x + 0.5f) * 2.0f / W - 1.0f;
            coc[i] = dof::Coc(DofPixelDistance(r.depth[i], DofRayScale(rays, nx, ny)), focus, range);
            any = any || coc[i] > 0.0f;
        }
    }
    if (!any) return;
    // 1) Half-resolution premultiplied copy (2 x 2 box average; the last row / column repeats on odd sizes).
    const int hw = (W + 1) / 2, hh = (H + 1) / 2;
    static std::vector<float> half, tmp;
    half.assign((std::size_t)hw * hh * 4, 0.0f); tmp.assign(half.size(), 0.0f);
    ParallelRows(0, hh, [&](int ya, int yb) {
        for (int y = ya; y < yb; ++y)
        for (int x = 0; x < hw; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                int sx = x * 2 + (k & 1), sy = y * 2 + (k >> 1);
                if (sx >= W) sx = W - 1;
                if (sy >= H) sy = H - 1;
                const std::uint32_t v = r.color[(std::size_t)sy * W + sx];
                const float a = (float)(v >> 24) / 255.0f;
                acc[0] += (float)(v & 0xFF) / 255.0f * a;
                acc[1] += (float)((v >> 8) & 0xFF) / 255.0f * a;
                acc[2] += (float)((v >> 16) & 0xFF) / 255.0f * a;
                acc[3] += a;
            }
            float* o = &half[((std::size_t)y * hw + x) * 4];
            for (int c = 0; c < 4; ++c) o[c] = acc[c] * 0.25f;
        }
    });
    // 2) Two rounds of the separable 9-tap binomial blur (edges clamp).
    static const float kW[9] = {1 / 256.0f, 8 / 256.0f, 28 / 256.0f, 56 / 256.0f, 70 / 256.0f,
                                56 / 256.0f, 28 / 256.0f, 8 / 256.0f, 1 / 256.0f};
    for (int round = 0; round < 2; ++round) {
        ParallelRows(0, hh, [&](int ya, int yb) {           // horizontal: half -> tmp
            for (int y = ya; y < yb; ++y)
            for (int x = 0; x < hw; ++x) {
                float acc[4] = {0, 0, 0, 0};
                for (int t = -4; t <= 4; ++t) {
                    int xx = x + t; xx = xx < 0 ? 0 : (xx >= hw ? hw - 1 : xx);
                    const float* s = &half[((std::size_t)y * hw + xx) * 4];
                    for (int c = 0; c < 4; ++c) acc[c] += s[c] * kW[t + 4];
                }
                float* o = &tmp[((std::size_t)y * hw + x) * 4];
                for (int c = 0; c < 4; ++c) o[c] = acc[c];
            }
        });
        ParallelRows(0, hh, [&](int ya, int yb) {           // vertical: tmp -> half
            for (int y = ya; y < yb; ++y)
            for (int x = 0; x < hw; ++x) {
                float acc[4] = {0, 0, 0, 0};
                for (int t = -4; t <= 4; ++t) {
                    int yy = y + t; yy = yy < 0 ? 0 : (yy >= hh ? hh - 1 : yy);
                    const float* s = &tmp[((std::size_t)yy * hw + x) * 4];
                    for (int c = 0; c < 4; ++c) acc[c] += s[c] * kW[t + 4];
                }
                float* o = &half[((std::size_t)y * hw + x) * 4];
                for (int c = 0; c < 4; ++c) o[c] = acc[c];
            }
        });
    }
    // 3) Mix: bilinear sample of the blurred copy, blended in premultiplied space by the circle of confusion.
    ParallelRows(0, H, [&](int ya, int yb) {
        for (int y = ya; y < yb; ++y) {
            float fy = (y + 0.5f) * 0.5f - 0.5f; int y0 = (int)std::floor(fy); const float ay = fy - y0;
            int y1 = y0 + 1; y0 = y0 < 0 ? 0 : (y0 >= hh ? hh - 1 : y0); y1 = y1 < 0 ? 0 : (y1 >= hh ? hh - 1 : y1);
            for (int x = 0; x < W; ++x) {
                const std::size_t i = (std::size_t)y * W + x;
                const float k = coc[i];
                if (k <= 0.0f) continue;                    // in focus: the pixel is left exactly as it was
                float fx = (x + 0.5f) * 0.5f - 0.5f; int x0 = (int)std::floor(fx); const float ax = fx - x0;
                int x1 = x0 + 1; x0 = x0 < 0 ? 0 : (x0 >= hw ? hw - 1 : x0); x1 = x1 < 0 ? 0 : (x1 >= hw ? hw - 1 : x1);
                const float* p00 = &half[((std::size_t)y0 * hw + x0) * 4];
                const float* p10 = &half[((std::size_t)y0 * hw + x1) * 4];
                const float* p01 = &half[((std::size_t)y1 * hw + x0) * 4];
                const float* p11 = &half[((std::size_t)y1 * hw + x1) * 4];
                const std::uint32_t v = r.color[i];
                const float a = (float)(v >> 24) / 255.0f;
                const float sharp[4] = {(float)(v & 0xFF) / 255.0f * a, (float)((v >> 8) & 0xFF) / 255.0f * a,
                                        (float)((v >> 16) & 0xFF) / 255.0f * a, a};
                float o[4];
                for (int c = 0; c < 4; ++c) {
                    const float top = p00[c] + (p10[c] - p00[c]) * ax, bot = p01[c] + (p11[c] - p01[c]) * ax;
                    o[c] = dof::Mix(sharp[c], top + (bot - top) * ay, k);
                }
                const float oa = o[3] < 0.0f ? 0.0f : (o[3] > 1.0f ? 1.0f : o[3]);
                const float inv = oa > 1e-4f ? 1.0f / oa : 0.0f;
                auto cl = [](float f) { return (std::uint32_t)((f < 0 ? 0 : (f > 1 ? 1 : f)) * 255.0f + 0.5f); };
                r.color[i] = (cl(oa) << 24) | (cl(o[2] * inv) << 16) | (cl(o[1] * inv) << 8) | cl(o[0] * inv);
            }
        }
    });
}

void ApplyFXAA(Raster& r) {
    if (!FXAAEnabled()) return;
    int W = r.width, H = r.height;
    if (W < 3 || H < 3) return;
    static std::vector<std::uint32_t> src;
    src = r.color;
    auto luma = [](std::uint32_t v) {
        return 0.299f * (v & 0xFF) + 0.587f * ((v >> 8) & 0xFF) + 0.114f * ((v >> 16) & 0xFF);
    };
    ParallelRows(1, H - 1, [&](int ya, int yb) {
        for (int y = ya; y < yb; ++y)
        for (int x = 1; x < W - 1; ++x) {
            std::size_t i = (std::size_t)y * W + x;
            float m = luma(src[i]), n = luma(src[i - W]), s = luma(src[i + W]),
                  e = luma(src[i + 1]), w = luma(src[i - 1]);
            float lo = std::fmin(m, std::fmin(std::fmin(n, s), std::fmin(e, w)));
            float hi = std::fmax(m, std::fmax(std::fmax(n, s), std::fmax(e, w)));
            float contrast = hi - lo;
            if (contrast < 26.0f) continue;                 // not an edge
            float amt = contrast / (hi + 1.0f);
            if (amt > 0.7f) amt = 0.7f;
            auto ch = [&](std::uint32_t v, int sh) { return (float)((v >> sh) & 0xFF); };
            std::size_t nb[4] = {i - 1, i + 1, i - (std::size_t)W, i + (std::size_t)W};
            for (int c = 0; c < 3; ++c) {
                int sh = c * 8;
                float cv = ch(src[i], sh), av = cv;
                for (std::size_t k : nb) av += ch(src[k], sh);
                av /= 5.0f;
                float o = cv + (av - cv) * amt;
                std::uint32_t oi = (std::uint32_t)(o < 0 ? 0 : (o > 255 ? 255 : o));
                r.color[i] = (r.color[i] & ~(0xFFu << sh)) | (oi << sh);
            }
        }
    });
}

} // namespace okay
