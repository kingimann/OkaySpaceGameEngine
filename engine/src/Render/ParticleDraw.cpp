#include "okay/Render/ParticleDraw.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // SceneOcclusionDepth
#include "okay/UI/UIPainter.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

namespace {

constexpr float kNear = 0.05f;
constexpr float kDeg2Rad = 0.0174532925f;

struct Cam {
    Vec3 right, up, fwd;
};

Vec3 Unproject(const Mat4& inv, float x, float y, float z) {
    const Vec4 r = inv * Vec4{x, y, z, 1.0f};
    const float iw = std::fabs(r.w) > 1e-12f ? 1.0f / r.w : 1.0f;
    return Vec3{r.x * iw, r.y * iw, r.z * iw};
}

Vec3 Norm(const Vec3& v, const Vec3& fallback) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 1e-9f ? v * (1.0f / l) : fallback;
}

struct Projector {
    const Mat4* m;
    float x0, y0, w, h;
    // Returns false when the point is behind the near plane.
    bool Project(const Vec3& p, float& sx, float& sy, float& cw) const {
        const float* a = m->m;   // column-major: element (col, row) = a[col * 4 + row]
        const float cx = a[0] * p.x + a[4] * p.y + a[8] * p.z + a[12];
        const float cy = a[1] * p.x + a[5] * p.y + a[9] * p.z + a[13];
        const float ww = a[3] * p.x + a[7] * p.y + a[11] * p.z + a[15];
        cw = ww;
        if (ww <= kNear) return false;
        const float iw = 1.0f / ww;
        sx = x0 + (cx * iw * 0.5f + 0.5f) * w;
        sy = y0 + (0.5f - cy * iw * 0.5f) * h;
        return true;
    }
};

// Is the sphere (c, r) wholly outside one of the view planes?
bool SphereOutside(const Mat4& vp, const Vec3& c, float r) {
    const float* a = vp.m;
    auto row = [&](int i, float& x, float& y, float& z, float& w) { x = a[0 * 4 + i]; y = a[1 * 4 + i]; z = a[2 * 4 + i]; w = a[3 * 4 + i]; };
    float rx[4], ry[4], rz[4], rw[4];
    row(0, rx[0], ry[0], rz[0], rw[0]); row(1, rx[1], ry[1], rz[1], rw[1]);
    row(2, rx[2], ry[2], rz[2], rw[2]); row(3, rx[3], ry[3], rz[3], rw[3]);
    // planes: w+x, w-x, w+y, w-y, w+z (near)
    for (int k = 0; k < 5; ++k) {
        float px, py, pz, pw;
        const int axis = k / 2;                      // 0 = x, 1 = y; k = 4 -> z
        const float sgn = (k % 2 == 0) ? 1.0f : -1.0f;
        if (k == 4) { px = rx[3] + rx[2]; py = ry[3] + ry[2]; pz = rz[3] + rz[2]; pw = rw[3] + rw[2]; }
        else { px = rx[3] + sgn * rx[axis]; py = ry[3] + sgn * ry[axis]; pz = rz[3] + sgn * rz[axis]; pw = rw[3] + sgn * rw[axis]; }
        const float d = px * c.x + py * c.y + pz * c.z + pw;
        const float len = std::sqrt(px * px + py * py + pz * pz);
        if (d < -r * len) return true;
    }
    return false;
}

float SoftFactor(float nx, float ny, float clipW, float softDist) {
    const OcclusionDepth& od = SceneOcclusionDepth();
    if (!od.valid || od.w <= 0 || od.h <= 0 || softDist <= 0.0f) return 1.0f;
    const int x = (int)(nx * od.w), y = (int)(ny * od.h);
    if (x < 0 || y < 0 || x >= od.w || y >= od.h) return 1.0f;
    const float sceneNear = od.d[(std::size_t)y * od.w + x];   // 1 / w of the nearest solid, 0 = nothing
    if (sceneNear <= 0.0f) return 1.0f;
    const float gap = 1.0f / sceneNear - clipW;                 // how far the solid is behind the particle
    return Mathf::Clamp01(gap / softDist);
}

struct BuildCtx {
    const ParticleView* view;
    Cam cam;
    Projector P;
    bool haveDepth;
    ParticleDrawStats* st;
    std::vector<ParticleDrawItem>* out;
};

BuildCtx MakeCtx(const ParticleView& view, std::vector<ParticleDrawItem>& out, ParticleDrawStats& st) {
    const Mat4 inv = view.vp.Inverse();
    Cam cam;
    {
        const Vec3 a = Unproject(inv, -1, 0, 0), b = Unproject(inv, 1, 0, 0);
        const Vec3 c = Unproject(inv, 0, -1, 0), d = Unproject(inv, 0, 1, 0);
        const Vec3 e = Unproject(inv, 0, 0, -1), f = Unproject(inv, 0, 0, 1);
        cam.right = Norm(b - a, Vec3{1, 0, 0}); cam.up = Norm(d - c, Vec3{0, 1, 0}); cam.fwd = Norm(f - e, Vec3{0, 0, -1});
    }
    return BuildCtx{&view, cam, Projector{nullptr, view.x0, view.y0, view.w, view.h}, view.useDepth && SceneOcclusionDepth().valid, &st, &out};
}

// One system's shapes, appended to the list (also feeds it the camera distance and the culled flag).
void AppendSystem(BuildCtx& cx, ParticleSystem* ps, const Vec3& origin) {
    const ParticleView& view = *cx.view; const Cam& cam = cx.cam; Projector P = cx.P; P.m = &view.vp;
    const bool haveDepth = cx.haveDepth; ParticleDrawStats& st = *cx.st; std::vector<ParticleDrawItem>& out = *cx.out;
    ps->SetViewDistance(std::sqrt((origin - view.camPos).SqrMagnitude()));
    const int n = ps->AliveCount();
    if (n <= 0) { ps->SetCulled(false); return; }
    ++st.systems;
    if (view.frustumCull) {
        const Vec3 lo = ps->BoundsMin(), hi = ps->BoundsMax();
        const Vec3 ctr = (lo + hi) * 0.5f;
        const float pad = std::max(ps->startSize + ps->startSizeRandom, ps->startSize * 4.0f) + 0.5f;
        const float r = std::sqrt((hi - lo).SqrMagnitude()) * 0.5f + pad;
        const bool outside = SphereOutside(view.vp, ctr, r);
        ps->SetCulled(outside);
        if (outside) { ++st.culled; return; }
    } else ps->SetCulled(false);

    const float* px = ps->Channel(ParticleSystem::PX); const float* py = ps->Channel(ParticleSystem::PY); const float* pz = ps->Channel(ParticleSystem::PZ);
    const float* vx = ps->Channel(ParticleSystem::VX); const float* vy = ps->Channel(ParticleSystem::VY); const float* vz = ps->Channel(ParticleSystem::VZ);
    const float* sz = ps->Channel(ParticleSystem::SIZE); const float* rot = ps->Channel(ParticleSystem::ROT);
    const float* cr = ps->Channel(ParticleSystem::R); const float* cg = ps->Channel(ParticleSystem::G);
    const float* cb = ps->Channel(ParticleSystem::B); const float* ca = ps->Channel(ParticleSystem::A);
    const float* rnd = ps->Channel(ParticleSystem::RND);
    const float* lifeA = ps->Channel(ParticleSystem::LIFE); const float* maxLifeA = ps->Channel(ParticleSystem::MAXLIFE);

    const auto mode = ps->renderMode;
    const auto blend = ps->blendMode;
    const bool additive = blend == ParticleSystem::BlendMode::Additive;
    const int cols = std::max(1, ps->sheetColumns), rows = std::max(1, ps->sheetRows);
    const int frames = cols * rows;
    const bool sheet = frames > 1 && !ps->texture.empty();
    const std::string* tex = ps->texture.empty() ? nullptr : &ps->texture;
    float lightMul[3] = {1, 1, 1};
    if (ps->lit) {
        const Vec3 toCam = Norm(view.camPos - origin, Vec3{0, 1, 0});
        const Vec3 nrm = Norm(Vec3{0, 0.6f, 0} + toCam, Vec3{0, 1, 0});
        const Vec3 l = SceneLights::ShadeColor(origin, nrm);
        lightMul[0] = std::min(1.0f, l.x); lightMul[1] = std::min(1.0f, l.y); lightMul[2] = std::min(1.0f, l.z);
    }
    const int trailPts = ps->SafeTrail();
    const float softD = ps->softDistance;
    const auto sortMode = ps->sortMode;
    const float sysDepth = Vec3::Dot(origin - view.camPos, cam.fwd);
    const bool streakMode = mode == ParticleSystem::RenderMode::Stretch;

    for (int i = 0; i < n; ++i) {
        const Vec3 pos{px[i], py[i], pz[i]};
        float sx, sy, cw;
        if (!P.Project(pos, sx, sy, cw)) continue;
        float alpha = ca[i];
        if (alpha <= 0.002f) continue;
        const float hs = sz[i] * 0.5f;
        if (hs <= 0.0f) continue;
        if (haveDepth) {
            const float nx = (sx - view.x0) / view.w, ny = (sy - view.y0) / view.h;
            if (softD > 0.0f) alpha *= SoftFactor(nx, ny, cw, softD);
            else if (ParticleOccluded(nx, ny, cw)) continue;
            if (alpha <= 0.002f) continue;
        }
        const float age01 = 1.0f - Mathf::Clamp01(lifeA[i] / maxLifeA[i]);
        float key = cw;
        if (sortMode == ParticleSystem::SortMode::OldestFirst) key = sysDepth + (age01 - 0.5f) * 0.001f;
        else if (sortMode == ParticleSystem::SortMode::YoungestFirst) key = sysDepth - (age01 - 0.5f) * 0.001f;
        else if (sortMode == ParticleSystem::SortMode::None) key = sysDepth;

        float r = cr[i] * lightMul[0], g = cg[i] * lightMul[1], b = cb[i] * lightMul[2];
        if (additive) { r = std::min(1.0f, r * 1.25f); g = std::min(1.0f, g * 1.25f); b = std::min(1.0f, b * 1.25f); alpha *= 0.85f; }
        const float theta = rot[i] * kDeg2Rad;
        const float ct = std::cos(theta), stt = std::sin(theta);

        // ---- ribbon trail (drawn just behind the particle) ----
        if (trailPts > 0) {
            Vec3 pts[ParticleSystem::kMaxTrailPoints + 1];
            pts[0] = pos;
            const int tn = ps->TrailPoints(i, pts + 1);
            if (tn > 0) {
                float sxs[ParticleSystem::kMaxTrailPoints + 1], sys[ParticleSystem::kMaxTrailPoints + 1];
                bool ok[ParticleSystem::kMaxTrailPoints + 1];
                float wpx;   // half width in pixels at the head
                {
                    float ex, ey, ew;
                    wpx = P.Project(pos + cam.right * hs, ex, ey, ew) ? std::sqrt((ex - sx) * (ex - sx) + (ey - sy) * (ey - sy)) : 1.0f;
                    wpx *= ps->trailWidth;
                }
                const int pn = tn + 1;
                for (int k = 0; k < pn; ++k) { float w2; ok[k] = P.Project(pts[k], sxs[k], sys[k], w2); }
                for (int k = 0; k + 1 < pn; ++k) {
                    if (!ok[k] || !ok[k + 1]) continue;
                    float dx = sxs[k + 1] - sxs[k], dy = sys[k + 1] - sys[k];
                    const float dl = std::sqrt(dx * dx + dy * dy);
                    if (dl < 1e-3f) continue;
                    const float nxp = -dy / dl, nyp = dx / dl;
                    const float f0 = 1.0f - (float)k / (float)pn, f1 = 1.0f - (float)(k + 1) / (float)pn;
                    const float w0 = std::max(0.4f, wpx * f0), w1 = std::max(0.2f, wpx * f1);
                    ParticleDrawItem it;
                    it.kind = ParticleDrawItem::Poly; it.npts = 4; it.blend = blend; it.key = key + 0.0001f;
                    it.px[0] = sxs[k] + nxp * w0; it.py[0] = sys[k] + nyp * w0;
                    it.px[1] = sxs[k] - nxp * w0; it.py[1] = sys[k] - nyp * w0;
                    it.px[2] = sxs[k + 1] - nxp * w1; it.py[2] = sys[k + 1] - nyp * w1;
                    it.px[3] = sxs[k + 1] + nxp * w1; it.py[3] = sys[k + 1] + nyp * w1;
                    it.color = Color{r, g, b, alpha * (1.0f - ps->trailFade * ((float)k + 0.5f) / (float)pn) * 0.9f};
                    it.sx = sxs[k]; it.sy = sys[k];
                    out.push_back(it); ++st.ribbonPieces;
                }
            }
        }

        // ---- cubes ----
        if (mode == ParticleSystem::RenderMode::Mesh) {
            const float yaw = theta, pitch = theta * 0.7f + rnd[i] * 6.2831853f;
            const float cy2 = std::cos(yaw), sy2 = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
            auto rotv = [&](float x, float y, float z) {   // pitch about X, then yaw about Y
                const float y1 = y * cp - z * sp, z1 = y * sp + z * cp;
                return Vec3{x * cy2 + z1 * sy2, y1, -x * sy2 + z1 * cy2};
            };
            Vec3 cs[8]; float scx[8], scy[8]; bool cok = true;
            for (int k = 0; k < 8; ++k) {
                const Vec3 o = rotv((k & 1) ? hs : -hs, (k & 2) ? hs : -hs, (k & 4) ? hs : -hs);
                cs[k] = pos + o;
                float w2; if (!P.Project(cs[k], scx[k], scy[k], w2)) { cok = false; break; }
            }
            if (cok) {
                static const int kFace[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
                static const Vec3 kN[6] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}};
                static const Vec3 kLight = Vec3{0.4f, 0.8f, 0.45f}.Normalized();
                const Vec3 toCam = view.camPos - pos;
                for (int f = 0; f < 6; ++f) {
                    const Vec3 wn = rotv(kN[f].x, kN[f].y, kN[f].z);
                    if (Vec3::Dot(wn, toCam) <= 0.0f) continue;
                    const float shade = 0.45f + 0.55f * std::max(0.0f, Vec3::Dot(wn, kLight));
                    ParticleDrawItem it;
                    it.kind = ParticleDrawItem::Poly; it.npts = 4; it.blend = blend; it.key = key;
                    for (int k = 0; k < 4; ++k) { it.px[k] = scx[kFace[f][k]]; it.py[k] = scy[kFace[f][k]]; }
                    it.sx = sx; it.sy = sy;
                    it.color = Color{r * shade, g * shade, b * shade, alpha};
                    out.push_back(it);
                }
                ++st.particles;
            }
            continue;
        }

        // ---- flat shapes: two half-axes in the world, projected ----
        Vec3 A, B;
        float ax, ay, bx, by, w2;
        bool stretched = false;
        if (streakMode) {
            const Vec3 v{vx[i], vy[i], vz[i]};
            const float speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (speed > 1e-3f) {
                const Vec3 dir = v * (1.0f / speed);
                float hx, hy;
                const float halfLen = hs * (1.0f + ps->stretchScale * speed);
                float ex, ey;
                if (P.Project(pos + dir * halfLen, hx, hy, w2) && P.Project(pos + cam.right * hs, ex, ey, w2)) {
                    float sideR = std::sqrt((ex - sx) * (ex - sx) + (ey - sy) * (ey - sy));
                    ax = hx - sx; ay = hy - sy;
                    float len = std::sqrt(ax * ax + ay * ay);
                    if (len < sideR) { if (len > 1e-4f) { ax *= sideR / len; ay *= sideR / len; } else { ax = sideR; ay = 0.0f; } len = sideR; }
                    bx = -ay / len * sideR; by = ax / len * sideR;
                    stretched = true;
                }
            }
        }
        if (!stretched) {
            if (mode == ParticleSystem::RenderMode::Horizontal) {
                A = Vec3{ct, 0.0f, stt}; B = Vec3{-stt, 0.0f, ct};
            } else {
                Vec3 R = cam.right, U = cam.up;
                if (mode == ParticleSystem::RenderMode::Vertical) {
                    const Vec3 toCam = Norm(view.camPos - pos, Vec3{0, 0, 1});
                    R = Norm(Vec3::Cross(Vec3{0, 1, 0}, toCam), cam.right); U = Vec3{0, 1, 0};
                }
                A = R * ct + U * stt; B = R * (-stt) + U * ct;
            }
            float e1x, e1y, e2x, e2y;
            if (!P.Project(pos + A * hs, e1x, e1y, w2) || !P.Project(pos + B * hs, e2x, e2y, w2)) continue;
            ax = e1x - sx; ay = e1y - sy; bx = e2x - sx; by = e2y - sy;
        }
        float rad = 0.5f * (std::sqrt(ax * ax + ay * ay) + std::sqrt(bx * bx + by * by));
        if (rad < 1.0f) {                       // sub-pixel: keep one pixel, fade by its real size
            if (rad < 1e-4f) continue;
            const float k = 1.0f / rad;
            ax *= k; ay *= k; bx *= k; by *= k; alpha *= rad; rad = 1.0f;
        } else if (rad > view.maxRadius) {      // right on the lens: cap it and fade
            const float k = view.maxRadius / rad;
            ax *= k; ay *= k; bx *= k; by *= k; alpha *= k; rad = view.maxRadius;
        }
        // Cull what is wholly off the picture.
        if (sx + rad + std::fabs(ax) < view.x0 - 4 || sx - rad - std::fabs(ax) > view.x0 + view.w + 4 ||
            sy + rad + std::fabs(ay) < view.y0 - 4 || sy - rad - std::fabs(ay) > view.y0 + view.h + 4) continue;

        ParticleDrawItem it;
        it.kind = ParticleDrawItem::Quad; it.blend = blend; it.streak = stretched; it.key = key;
        it.sx = sx; it.sy = sy; it.axx = ax; it.axy = ay; it.ayx = bx; it.ayy = by; it.rad = rad;
        it.texture = tex;
        it.color = Color{r, g, b, alpha};
        if (sheet) {
            const float life = maxLifeA[i];
            float fi;
            switch (ps->sheetMode) {
                case ParticleSystem::SheetMode::Random: fi = rnd[i] * (float)frames; break;
                case ParticleSystem::SheetMode::Loop:   fi = age01 * life * std::max(0.0f, ps->sheetCycles); break;
                default:                                fi = age01 * std::max(0.0f, ps->sheetCycles) * (float)frames; break;
            }
            int f0; float fr;
            if (ps->sheetMode == ParticleSystem::SheetMode::OverLife) {
                const float c = std::min(fi, (float)frames - 0.0001f); f0 = (int)c; fr = c - (float)f0;
            } else { f0 = (int)std::floor(fi); fr = fi - (float)f0; f0 = ((f0 % frames) + frames) % frames; }
            auto cell = [&](ParticleDrawItem& d, int f) {
                const int cx = f % cols, cyy = f / cols;
                d.u0 = (float)cx / cols; d.u1 = (float)(cx + 1) / cols; d.v0 = (float)cyy / rows; d.v1 = (float)(cyy + 1) / rows;
            };
            cell(it, f0);
            if (ps->sheetBlendFrames && fr > 0.02f) {
                ParticleDrawItem it2 = it;
                const int f1 = (f0 + 1) % frames;
                if (ps->sheetMode != ParticleSystem::SheetMode::OverLife || f0 + 1 < frames) {
                    it.color.a = alpha * (1.0f - fr);
                    cell(it2, f1);
                    it2.color.a = alpha * fr;
                    out.push_back(it);
                    out.push_back(it2);
                    ++st.particles;
                    continue;
                }
            }
        }
        out.push_back(it);
        ++st.particles;
    }
}

} // namespace

void BuildParticleDraw(Scene& scene, const ParticleView& view, std::vector<ParticleDrawItem>& out,
                       ParticleDrawStats* stats, const std::function<bool(const GameObject*)>& visible) {
    out.clear();
    ParticleDrawStats local; ParticleDrawStats& st = stats ? *stats : local;
    st = ParticleDrawStats{};
    BuildCtx cx = MakeCtx(view, out, st);
    for (const auto& up : scene.Objects()) {
        if (!up || !up->active) continue;
        ParticleSystem* ps = up->GetComponent<ParticleSystem>();
        if (!ps) continue;
        if (visible && !visible(up.get())) continue;
        AppendSystem(cx, ps, up->transform ? up->transform->Position() : Vec3::Zero);
    }
    std::stable_sort(out.begin(), out.end(), [](const ParticleDrawItem& a, const ParticleDrawItem& b) { return a.key > b.key; });
    st.items = (int)out.size();
}

void BuildParticleDrawFor(ParticleSystem& ps, const Vec3& origin, const ParticleView& view, std::vector<ParticleDrawItem>& out,
                          ParticleDrawStats* stats) {
    out.clear();
    ParticleDrawStats local; ParticleDrawStats& st = stats ? *stats : local;
    st = ParticleDrawStats{};
    BuildCtx cx = MakeCtx(view, out, st);
    AppendSystem(cx, &ps, origin);
    std::stable_sort(out.begin(), out.end(), [](const ParticleDrawItem& a, const ParticleDrawItem& b) { return a.key > b.key; });
    st.items = (int)out.size();
}

// ---- headless raster ---------------------------------------------------------------------------------------

namespace {
inline void Blend(std::uint8_t* px, float r, float g, float b, float a, bool add) {
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    for (int c = 0; c < 3; ++c) {
        const float v = c == 0 ? r : (c == 1 ? g : b);
        const float d = px[c] / 255.0f;
        const float o = add ? std::min(1.0f, d + v * a) : d * (1.0f - a) + v * a;
        px[c] = (std::uint8_t)(std::min(1.0f, std::max(0.0f, o)) * 255.0f + 0.5f);
    }
    px[3] = 255;
}
}

void RasterizeParticleDraw(const std::vector<ParticleDrawItem>& items, int w, int h, std::uint8_t* rgba) {
    for (const ParticleDrawItem& it : items) {
        const bool add = it.blend != ParticleSystem::BlendMode::Alpha;
        if (it.kind == ParticleDrawItem::Poly) {
            float minx = it.px[0], maxx = it.px[0], miny = it.py[0], maxy = it.py[0];
            for (int k = 1; k < it.npts; ++k) { minx = std::min(minx, it.px[k]); maxx = std::max(maxx, it.px[k]); miny = std::min(miny, it.py[k]); maxy = std::max(maxy, it.py[k]); }
            const int x0 = std::max(0, (int)std::floor(minx)), x1 = std::min(w - 1, (int)std::ceil(maxx));
            const int y0 = std::max(0, (int)std::floor(miny)), y1 = std::min(h - 1, (int)std::ceil(maxy));
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
                const float fx = x + 0.5f, fy = y + 0.5f;
                int pos = 0, neg = 0;
                for (int k = 0; k < it.npts; ++k) {
                    const int k2 = (k + 1) % it.npts;
                    const float cr = (it.px[k2] - it.px[k]) * (fy - it.py[k]) - (it.py[k2] - it.py[k]) * (fx - it.px[k]);
                    if (cr > 0) ++pos; else if (cr < 0) ++neg;
                }
                if (pos == 0 || neg == 0) Blend(rgba + ((std::size_t)y * w + x) * 4, it.color.r, it.color.g, it.color.b, it.color.a, add);
            }
            continue;
        }
        const float det = it.axx * it.ayy - it.axy * it.ayx;
        if (std::fabs(det) < 1e-9f) continue;
        const float ext = std::fabs(it.axx) + std::fabs(it.ayx), exty = std::fabs(it.axy) + std::fabs(it.ayy);
        const int x0 = std::max(0, (int)std::floor(it.sx - ext)), x1 = std::min(w - 1, (int)std::ceil(it.sx + ext));
        const int y0 = std::max(0, (int)std::floor(it.sy - exty)), y1 = std::min(h - 1, (int)std::ceil(it.sy + exty));
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
            const float dx = x + 0.5f - it.sx, dy = y + 0.5f - it.sy;
            const float u = (dx * it.ayy - dy * it.ayx) / det;    // along axis 1, -1..1
            const float v = (it.axx * dy - it.axy * dx) / det;    // along axis 2, -1..1
            if (std::fabs(u) > 1.0f || std::fabs(v) > 1.0f) continue;
            float cov = 1.0f;
            if (!it.streak) {   // a soft round puff
                const float d = std::sqrt(u * u + v * v);
                if (d >= 1.0f) continue;
                const float t = 1.0f - d;
                cov = t * t * (3.0f - 2.0f * t);
            } else {            // a streak: bright core, soft ends
                cov = (1.0f - std::fabs(u) * 0.6f) * (1.0f - std::fabs(v) * 0.7f);
            }
            Blend(rgba + ((std::size_t)y * w + x) * 4, it.color.r, it.color.g, it.color.b, it.color.a * cov, add);
        }
    }
}


// ---- painter drawing ---------------------------------------------------------------------------------------

void DrawParticleDrawPainter(ui2d::Painter& r, const std::vector<ParticleDrawItem>& items,
                             const std::function<void*(const std::string&)>& loadTex) {
    auto rgba = [](const Color& c, float aMul) {
        auto q = [](float v) { return (std::uint8_t)(Mathf::Clamp01(v) * 255.0f + 0.5f); };
        return ui2d::RGBA{q(c.r), q(c.g), q(c.b), q(c.a * aMul)};
    };
    ui2d::Vertex v[4];
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (const ParticleDrawItem& it : items) {
        if (it.kind == ParticleDrawItem::Poly) {
            const ui2d::RGBA c = rgba(it.color, 1.0f);
            for (int k = 0; k < it.npts && k < 4; ++k) { v[k].position = {it.px[k], it.py[k]}; v[k].color = c; v[k].tex_coord = {0, 0}; }
            if (it.npts == 4) r.Geometry(nullptr, v, 4, idx, 6);
            continue;
        }
        ui2d::Tex tex = (it.texture && loadTex) ? (ui2d::Tex)loadTex(*it.texture) : nullptr;
        if (tex || it.streak) {
            const float cx[4] = {-1, 1, 1, -1}, cy[4] = {-1, -1, 1, 1};
            const float uu[4] = {it.u0, it.u1, it.u1, it.u0}, vv[4] = {it.v0, it.v0, it.v1, it.v1};
            const ui2d::RGBA c = rgba(it.color, 1.0f);
            for (int k = 0; k < 4; ++k) {
                v[k].position = {it.sx + cx[k] * it.axx + cy[k] * it.ayx, it.sy + cx[k] * it.axy + cy[k] * it.ayy};
                v[k].color = c; v[k].tex_coord = {uu[k], vv[k]};
            }
            r.Geometry(tex, v, 4, idx, 6);
        } else {
            const int cx = (int)it.sx, cy = (int)it.sy;
            ui2d::MMFillCircle(r, cx, cy, (int)(it.rad * 1.6f), rgba(it.color, 0.22f));    // soft outer glow
            ui2d::MMFillCircle(r, cx, cy, (int)(it.rad * 1.15f), rgba(it.color, 0.4f));
            ui2d::MMFillCircle(r, cx, cy, (int)(it.rad * 0.75f), rgba(it.color, 0.85f));   // bright core
        }
    }
}

} // namespace okay
