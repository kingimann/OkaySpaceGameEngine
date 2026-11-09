// CPU software occlusion buffer. See OcclusionBuffer.hpp.
#include "okay/Optimize/OcclusionBuffer.hpp"
#include <algorithm>
#include <cmath>

namespace okay::optimize {
namespace {

constexpr double kFixed = 256.0;      // 1/256 pixel sub-pixel precision
constexpr double kGuard = 4.0;        // guard band: clip x,y to +-4 w so fixed point never overflows
constexpr double kMinW = 1e-6;

struct PlaneDist { double a, b, c, d; };   // distance = a*x + b*y + c*z + d*w >= 0 is inside
const PlaneDist kPlanes[5] = {
    {0, 0, 1, 1},          // near:  z + w >= 0
    {1, 0, 0, kGuard},     // left   (guard band)
    {-1, 0, 0, kGuard},    // right
    {0, 1, 0, kGuard},     // bottom
    {0, -1, 0, kGuard},    // top
};

}  // namespace

OcclusionBuffer::OcclusionBuffer(int width, int height) { Resize(width, height); }

void OcclusionBuffer::Resize(int width, int height) {
    w_ = std::max(width, 1);
    h_ = std::max(height, 1);
    inv_.assign(static_cast<std::size_t>(w_) * h_, 0.0f);
    stats_ = OcclusionStats();
}

void OcclusionBuffer::Clear() {
    std::fill(inv_.begin(), inv_.end(), 0.0f);
    stats_ = OcclusionStats();
}

void OcclusionBuffer::SetCamera(const Vec3& eye, const Vec3& target, const Vec3& up, float fovYDeg, float aspect,
                                float nearPlane, float farPlane) {
    vp_ = Mat4::Perspective(fovYDeg, aspect, nearPlane, farPlane) * Mat4::LookAt(eye, target, up);
}

// Sutherland-Hodgman against the near plane and the guard band, then raster each fan triangle.
void OcclusionBuffer::RasterClipped(const ClipV* v, int n, bool cullBack) {
    ClipV a[16], b[16];
    int na = n;
    for (int i = 0; i < n; ++i) a[i] = v[i];
    ClipV* in = a; ClipV* out = b;
    for (const PlaneDist& p : kPlanes) {
        int nout = 0;
        for (int i = 0; i < na; ++i) {
            const ClipV& cur = in[i];
            const ClipV& nxt = in[(i + 1) % na];
            const double dc = p.a * cur.x + p.b * cur.y + p.c * cur.z + p.d * cur.w;
            const double dn = p.a * nxt.x + p.b * nxt.y + p.c * nxt.z + p.d * nxt.w;
            if (dc >= 0.0) out[nout++] = cur;
            if ((dc >= 0.0) != (dn >= 0.0)) {
                const double t = dc / (dc - dn);
                out[nout++] = {cur.x + (nxt.x - cur.x) * t, cur.y + (nxt.y - cur.y) * t,
                               cur.z + (nxt.z - cur.z) * t, cur.w + (nxt.w - cur.w) * t};
            }
        }
        std::swap(in, out);
        na = nout;
        if (na < 3) return;
    }
    // Every surviving vertex has z + w >= 0; require a usable w for the divide.
    for (int i = 0; i < na; ++i) if (in[i].w < kMinW) return;
    double sx[16], sy[16], iw[16];
    for (int i = 0; i < na; ++i) {
        const double inv = 1.0 / in[i].w;
        sx[i] = (in[i].x * inv * 0.5 + 0.5) * w_;
        sy[i] = (in[i].y * inv * 0.5 + 0.5) * h_;
        iw[i] = inv;
    }
    for (int i = 1; i + 1 < na; ++i) {
        const double tx[3] = {sx[0], sx[i], sx[i + 1]};
        const double ty[3] = {sy[0], sy[i], sy[i + 1]};
        const double tw[3] = {iw[0], iw[i], iw[i + 1]};
        if (cullBack) {
            const double area = (tx[1] - tx[0]) * (ty[2] - ty[0]) - (ty[1] - ty[0]) * (tx[2] - tx[0]);
            if (area <= 0.0) continue;   // clip space keeps CCW triangles counter-clockwise on screen
        }
        FillTriangle(tx, ty, tw);
    }
}

void OcclusionBuffer::FillTriangle(const double* sx, const double* sy, const double* iw) {
    long long X[3], Y[3];
    for (int i = 0; i < 3; ++i) { X[i] = std::llround(sx[i] * kFixed); Y[i] = std::llround(sy[i] * kFixed); }
    long long area = (X[1] - X[0]) * (Y[2] - Y[0]) - (Y[1] - Y[0]) * (X[2] - X[0]);
    if (area == 0) return;
    double w0 = iw[0], w1 = iw[1], w2 = iw[2];
    if (area < 0) {                       // make it counter-clockwise: double sided
        std::swap(X[1], X[2]); std::swap(Y[1], Y[2]); std::swap(w1, w2);
        area = -area;
    }
    ++stats_.trianglesRasterized;
    const long long minX = std::min({X[0], X[1], X[2]}), maxX = std::max({X[0], X[1], X[2]});
    const long long minY = std::min({Y[0], Y[1], Y[2]}), maxY = std::max({Y[0], Y[1], Y[2]});
    const int x0 = std::max(0, static_cast<int>(std::floor(static_cast<double>(minX) / kFixed - 0.5)));
    const int x1 = std::min(w_ - 1, static_cast<int>(std::ceil(static_cast<double>(maxX) / kFixed)));
    const int y0 = std::max(0, static_cast<int>(std::floor(static_cast<double>(minY) / kFixed - 0.5)));
    const int y1 = std::min(h_ - 1, static_cast<int>(std::ceil(static_cast<double>(maxY) / kFixed)));
    if (x0 > x1 || y0 > y1) return;
    // Edge i is opposite vertex i: e0 = edge(v1,v2), e1 = edge(v2,v0), e2 = edge(v0,v1).
    const int ia[3] = {1, 2, 0}, ib[3] = {2, 0, 1};
    long long dx[3], dy[3];
    bool inclusive[3];
    for (int e = 0; e < 3; ++e) {
        dx[e] = X[ib[e]] - X[ia[e]];
        dy[e] = Y[ib[e]] - Y[ia[e]];
        inclusive[e] = dy[e] > 0 || (dy[e] == 0 && dx[e] < 0);   // top-left style tie break
    }
    const double invArea = 1.0 / static_cast<double>(area);
    // Gradient of 1/w per pixel (fixed-point units -> pixels).
    double gx = 0.0, gy = 0.0;
    const double wv[3] = {w0, w1, w2};
    for (int e = 0; e < 3; ++e) {
        gx += wv[e] * (-static_cast<double>(dy[e])) * invArea * kFixed;
        gy += wv[e] * static_cast<double>(dx[e]) * invArea * kFixed;
    }
    const double halfSpread = 0.5 * (std::fabs(gx) + std::fabs(gy));
    const double minW = std::min({w0, w1, w2});
    for (int y = y0; y <= y1; ++y) {
        const long long py = static_cast<long long>(y) * 256 + 128;
        for (int x = x0; x <= x1; ++x) {
            const long long px = static_cast<long long>(x) * 256 + 128;
            long long e[3];
            bool inside = true;
            for (int k = 0; k < 3; ++k) {
                e[k] = dx[k] * (py - Y[ia[k]]) - dy[k] * (px - X[ia[k]]);
                if (e[k] < 0 || (e[k] == 0 && !inclusive[k])) { inside = false; break; }
            }
            if (!inside) continue;
            const double centre = (w0 * static_cast<double>(e[0]) + w1 * static_cast<double>(e[1]) +
                                   w2 * static_cast<double>(e[2])) * invArea;
            // Lower bound of 1/w over the pixel footprint = the farthest the occluder is in this pixel.
            const double farthest = std::max(centre - halfSpread, minW);
            float& dst = inv_[static_cast<std::size_t>(y) * w_ + x];
            const float f = static_cast<float>(farthest);
            if (f > dst) { dst = f; ++stats_.pixelsWritten; }
        }
    }
}

void OcclusionBuffer::RasterizeTriangle(const Vec3& a, const Vec3& b, const Vec3& c) {
    ++stats_.trianglesIn;
    ClipV v[3];
    const Vec3* p[3] = {&a, &b, &c};
    for (int i = 0; i < 3; ++i) {
        const double x = p[i]->x, y = p[i]->y, z = p[i]->z;
        const float* m = vp_.m;
        v[i] = {m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13],
                m[2] * x + m[6] * y + m[10] * z + m[14], m[3] * x + m[7] * y + m[11] * z + m[15]};
    }
    RasterClipped(v, 3, false);
}

void OcclusionBuffer::RasterizeQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
    RasterizeTriangle(a, b, c);
    RasterizeTriangle(a, c, d);
}

void OcclusionBuffer::RasterizeBox(const Bounds& box) { RasterizeBox(box, Mat4::Identity()); }

void OcclusionBuffer::RasterizeBox(const Bounds& box, const Mat4& model) {
    const Vec3 mn = box.Min(), mx = box.Max();
    Vec3 c[8];
    for (int i = 0; i < 8; ++i)
        c[i] = model.MultiplyPoint({(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z});
    static const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 2, 6, 4}, {1, 5, 7, 3}, {0, 4, 5, 1}, {2, 3, 7, 6}};
    for (const auto& f : faces) RasterizeQuad(c[f[0]], c[f[1]], c[f[2]], c[f[3]]);
}

void OcclusionBuffer::RasterizeMesh(const Mesh& mesh, const Mat4& model, bool cullBackfaces) {
    const int nv = static_cast<int>(mesh.vertices.size());
    std::vector<Vec3> world(mesh.vertices.size());
    for (int i = 0; i < nv; ++i) world[i] = model.MultiplyPoint(mesh.vertices[i]);
    const int nt = mesh.TriangleCount();
    for (int t = 0; t < nt; ++t) {
        const int i = mesh.triangles[t * 3], j = mesh.triangles[t * 3 + 1], k = mesh.triangles[t * 3 + 2];
        if (i < 0 || j < 0 || k < 0 || i >= nv || j >= nv || k >= nv) continue;
        ++stats_.trianglesIn;
        ClipV v[3];
        const Vec3* p[3] = {&world[i], &world[j], &world[k]};
        for (int q = 0; q < 3; ++q) {
            const double x = p[q]->x, y = p[q]->y, z = p[q]->z;
            const float* m = vp_.m;
            v[q] = {m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13],
                    m[2] * x + m[6] * y + m[10] * z + m[14], m[3] * x + m[7] * y + m[11] * z + m[15]};
        }
        RasterClipped(v, 3, cullBackfaces);
    }
}

OcclusionResult OcclusionBuffer::Test(const Bounds& box) const {
    ++stats_.queries;
    const Vec3 mn = box.Min(), mx = box.Max();
    double sx[8], sy[8], iw[8];
    int behind = 0;
    for (int i = 0; i < 8; ++i) {
        const double x = (i & 1) ? mx.x : mn.x, y = (i & 2) ? mx.y : mn.y, z = (i & 4) ? mx.z : mn.z;
        const float* m = vp_.m;
        const double cx = m[0] * x + m[4] * y + m[8] * z + m[12];
        const double cy = m[1] * x + m[5] * y + m[9] * z + m[13];
        const double cz = m[2] * x + m[6] * y + m[10] * z + m[14];
        const double cw = m[3] * x + m[7] * y + m[11] * z + m[15];
        if (cz + cw < 0.0 || cw < kMinW) { ++behind; continue; }
        const double inv = 1.0 / cw;
        sx[i] = (cx * inv * 0.5 + 0.5) * w_;
        sy[i] = (cy * inv * 0.5 + 0.5) * h_;
        iw[i] = inv;
    }
    if (behind == 8) return OcclusionResult::OffScreen;
    if (behind > 0) return OcclusionResult::NearClipped;
    double lx = sx[0], hx = sx[0], ly = sy[0], hy = sy[0], nearest = iw[0];
    for (int i = 1; i < 8; ++i) {
        lx = std::min(lx, sx[i]); hx = std::max(hx, sx[i]);
        ly = std::min(ly, sy[i]); hy = std::max(hy, sy[i]);
        nearest = std::max(nearest, iw[i]);       // largest 1/w = the box's nearest point
    }
    if (hx < 0.0 || hy < 0.0 || lx > w_ || ly > h_) return OcclusionResult::OffScreen;
    const int x0 = std::max(0, static_cast<int>(std::floor(lx)) - 1);
    const int x1 = std::min(w_ - 1, static_cast<int>(std::ceil(hx)) + 1);
    const int y0 = std::max(0, static_cast<int>(std::floor(ly)) - 1);
    const int y1 = std::min(h_ - 1, static_cast<int>(std::ceil(hy)) + 1);
    // Hidden only if every pixel holds an occluder clearly nearer than the box's nearest point.
    const float needed = static_cast<float>(nearest * (1.0 + static_cast<double>(bias_)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (!(inv_[static_cast<std::size_t>(y) * w_ + x] > needed)) return OcclusionResult::Visible;
    ++stats_.occluded;
    return OcclusionResult::Occluded;
}

float OcclusionBuffer::InverseDepthAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0.0f;
    return inv_[static_cast<std::size_t>(y) * w_ + x];
}

float OcclusionBuffer::Coverage() const {
    if (inv_.empty()) return 0.0f;
    std::size_t n = 0;
    for (float f : inv_) if (f > 0.0f) ++n;
    return static_cast<float>(n) / static_cast<float>(inv_.size());
}

std::string OcclusionBuffer::ToAscii(int columns, int rows) const {
    columns = std::max(1, std::min(columns, w_));
    rows = std::max(1, std::min(rows, h_));
    std::string s;
    for (int r = rows - 1; r >= 0; --r) {
        for (int c = 0; c < columns; ++c) {
            const int x0 = c * w_ / columns, x1 = std::max(x0 + 1, (c + 1) * w_ / columns);
            const int y0 = r * h_ / rows, y1 = std::max(y0 + 1, (r + 1) * h_ / rows);
            int hit = 0, total = 0;
            for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { ++total; if (Covered(x, y)) ++hit; }
            s += hit * 2 >= total ? '#' : '.';
        }
        s += '\n';
    }
    return s;
}

} // namespace okay::optimize
