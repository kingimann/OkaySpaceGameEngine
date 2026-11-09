#pragma once
// ---------------------------------------------------------------------------
// CPU software occlusion culling (feature map 27.2).
//
// A small depth buffer (256 x 128 by default) that big occluders (walls,
// terrain chunks, buildings) are rasterized into. Bounding boxes are then
// tested against it and rejected when something nearer covers every pixel
// they could touch. Pure geometry: it needs a view-projection matrix and
// triangles, nothing from the scene or the renderers.
//
// Conservative by construction, so it never hides something that is visible:
//   - the buffer stores 1/w (inverse view depth), which is exactly linear in
//     screen space for a planar triangle, and per pixel the FARTHEST depth
//     the occluder has anywhere in that pixel;
//   - triangles use a watertight top-left fill rule on 1/256 pixel fixed
//     point, so two triangles of a quad leave no gap;
//   - a box is tested over its screen rectangle grown by one pixel, which
//     covers the half-pixel error of centre sampling;
//   - a box that touches the near plane (camera inside or next to it) is
//     treated as visible;
//   - a box has to be behind the occluder by `depthBias` (a fraction of its
//     distance) before it counts as hidden.
//
// Limit: coverage is sampled at pixel centres, so a gap narrower than about one
// pixel between two occluders (1/256 of the screen width by default) is not
// resolved and a box that is only visible through it can be culled.
//
// Occluders are drawn double sided, so a room's walls hide what is outside it
// when the camera is inside the room.
// ---------------------------------------------------------------------------
#include "okay/Math/Bounds.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Render/Mesh.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay::optimize {

enum class OcclusionResult {
    Visible,        ///< not hidden (some pixel it can touch is not covered by something nearer)
    Occluded,       ///< hidden behind the rasterized occluders
    OffScreen,      ///< entirely outside the view (frustum culling's job; not reported as occluded)
    NearClipped,    ///< touches the near plane or is behind the camera partly: kept visible
};

struct OcclusionStats {
    int trianglesIn = 0;         ///< triangles submitted
    int trianglesRasterized = 0; ///< triangles that survived clipping and had area
    long long pixelsWritten = 0;
    int queries = 0;
    int occluded = 0;
};

class OcclusionBuffer {
public:
    explicit OcclusionBuffer(int width = 256, int height = 128);

    void Resize(int width, int height);
    int  Width() const { return w_; }
    int  Height() const { return h_; }

    /// Forget every occluder (and the stats). Keeps the camera.
    void Clear();

    /// clip = vp * (world, 1), OpenGL clip space as Mat4::Perspective / LookAt produce.
    void SetViewProjection(const Mat4& vp) { vp_ = vp; }
    const Mat4& ViewProjection() const { return vp_; }
    /// Convenience: build the view-projection from a look-at camera.
    void SetCamera(const Vec3& eye, const Vec3& target, const Vec3& up, float fovYDeg, float aspect,
                   float nearPlane, float farPlane);

    /// How far behind an occluder (as a fraction of its distance) a box must be to count as hidden.
    void  SetDepthBias(float relative) { bias_ = relative < 0.0f ? 0.0f : relative; }
    float DepthBias() const { return bias_; }

    // --- occluders (world space) ---------------------------------------
    void RasterizeTriangle(const Vec3& a, const Vec3& b, const Vec3& c);
    void RasterizeQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d);
    /// A solid axis-aligned box.
    void RasterizeBox(const Bounds& box);
    /// A box in local space under a model matrix (rotated / scaled walls).
    void RasterizeBox(const Bounds& localBox, const Mat4& model);
    /// Every triangle of a mesh under `model`. `cullBackfaces` is only safe for closed meshes.
    void RasterizeMesh(const Mesh& mesh, const Mat4& model = Mat4::Identity(), bool cullBackfaces = false);

    // --- occludees --------------------------------------------------------
    OcclusionResult Test(const Bounds& worldAabb) const;
    bool IsOccluded(const Bounds& worldAabb) const { return Test(worldAabb) == OcclusionResult::Occluded; }
    /// Test a point-sized sphere-ish object by its bounding box.
    bool IsOccludedSphere(const Vec3& center, float radius) const {
        return IsOccluded(Bounds(center, Vec3(radius * 2.0f)));
    }

    // --- inspection ------------------------------------------------------
    /// Inverse view depth stored for a pixel: 0 = nothing there, larger = nearer.
    float InverseDepthAt(int x, int y) const;
    bool  Covered(int x, int y) const { return InverseDepthAt(x, y) > 0.0f; }
    float Coverage() const;                 ///< fraction of pixels covered
    /// Counters are plain ints updated by Rasterize*/Test; if you Test from several threads, ignore them.
    const OcclusionStats& Stats() const { return stats_; }
    /// A small text picture of the buffer ('#' covered, '.' empty), for logs and tests.
    std::string ToAscii(int columns = 64, int rows = 16) const;

private:
    struct ClipV { double x, y, z, w; };
    void RasterClipped(const ClipV* v, int n, bool cullBack);
    void FillTriangle(const double* sx, const double* sy, const double* iw);

    int w_ = 0, h_ = 0;
    std::vector<float> inv_;       // per pixel: min over occluders of... see .cpp (max of nearest guard)
    Mat4 vp_;
    float bias_ = 0.002f;
    mutable OcclusionStats stats_;
};

} // namespace okay::optimize
