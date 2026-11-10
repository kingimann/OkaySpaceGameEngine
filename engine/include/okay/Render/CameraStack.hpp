#pragma once
// Camera stack (feature map 2.10). Scene::CameraStack() lists the cameras that
// can render, lowest depth first. A scene with one camera keeps the host's
// existing full-frame path (same pixels). Two or more cameras are drawn in
// order: each into its own viewport-sized target, then composited.
//
// ClearPlan decides the composite:
//   * Skybox / Solid Color replace the rectangle (a skybox leaves alpha 0 where
//     nothing was drawn, so the host's sky shows through).
//   * Depth Only / Don't Clear keep the cameras underneath and draw this
//     camera's geometry over them (its clear is transparent).
// The first camera always clears, whatever its flag says.
#include "okay/Components/Camera.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

/// Pixel rectangle of a camera's viewport inside a framebuffer (origin top-left).
inline void CameraPixelRect(const Camera& cam, int fbW, int fbH, int& x, int& y, int& w, int& h) {
    const float rw = std::clamp(cam.rectW, 0.01f, 1.0f);
    const float rh = std::clamp(cam.rectH, 0.01f, 1.0f);
    const float rx = std::clamp(cam.rectX, 0.0f, 1.0f - rw);
    const float ry = std::clamp(cam.rectY, 0.0f, 1.0f - rh);
    w = std::max(1, (int)std::lround(rw * (float)std::max(fbW, 1)));
    h = std::max(1, (int)std::lround(rh * (float)std::max(fbH, 1)));
    x = (int)std::lround(rx * (float)std::max(fbW, 1));
    y = std::max(0, fbH - h - (int)std::lround(ry * (float)std::max(fbH, 1)));
    if (x + w > fbW) w = std::max(1, fbW - x);
    if (y + h > fbH) h = std::max(1, fbH - y);
}

/// One camera's slice of a stacked frame.
struct CameraPass {
    Camera* camera = nullptr;
    int x = 0, y = 0, w = 0, h = 0;     ///< framebuffer rect, origin top-left
    Mat4 vp;
    Vec3 eye{0, 0, 0};
    float fovY = 60.0f;
    float clearR = 0, clearG = 0, clearB = 0, clearA = 0;
    bool sky = true;                    ///< draw the scene sky into this pass
    bool overlay = false;               ///< composite with alpha instead of replacing the rect
    bool clearColor = true;
    bool clearDepth = true;
    int cullingMask = ~0;
    const GameObject* ignore = nullptr;
    std::string target;                 ///< optional named render target
    float depth = 0;
};

struct CameraStackPlan {
    int width = 0, height = 0;
    std::vector<CameraPass> passes;     ///< lowest depth first
    /// True when the host should keep its existing one-camera call. That path
    /// is what every current scene renders with; the stack composite runs only
    /// when more than one camera can see.
    bool singleFullFrame = true;
};

/// Build the stack for a framebuffer of `fbW` x `fbH`. `playIgnore` is hidden
/// from the scene's main camera only (a first-person body still shows in an
/// overlay). Cameras with no usable transform are skipped.
inline CameraStackPlan PlanCameraStack(const Scene& scene, int fbW, int fbH,
                                       const GameObject* playIgnore = nullptr) {
    CameraStackPlan plan;
    plan.width = std::max(fbW, 1);
    plan.height = std::max(fbH, 1);
    const auto stack = scene.CameraStack();
    for (Camera* cam : stack) {
        if (!cam || !cam->gameObject || !cam->gameObject->transform) continue;
        CameraPass p;
        p.camera = cam;
        p.depth = cam->depth;
        p.cullingMask = cam->cullingMask;
        p.ignore = (cam == scene.mainCamera) ? playIgnore : cam->ignoreObject;
        p.target = cam->renderTarget;
        CameraPixelRect(*cam, plan.width, plan.height, p.x, p.y, p.w, p.h);
        const float aspect = p.h > 0 ? (float)p.w / (float)p.h : 1.0f;
        p.vp = cam->ProjectionMatrix(aspect) * cam->ViewMatrix();
        p.eye = cam->RenderPosition();
        p.fovY = cam->projection == Camera::Projection::Perspective
                     ? cam->VerticalFovDegrees(aspect) : 60.0f;
        const bool first = plan.passes.empty();
        cam->ClearPlan(first, p.clearColor, p.clearDepth);
        const bool solid = cam->clearFlags == Camera::ClearFlags::SolidColor;
        p.sky = p.clearColor && !solid && cam->clearFlags != Camera::ClearFlags::DepthOnly
                && cam->clearFlags != Camera::ClearFlags::DontClear;
        // The base camera that asked not to clear still needs a background; use
        // its background colour so the framebuffer is not leftover memory.
        if (p.clearColor && (solid || (first && !p.sky))) {
            p.clearR = cam->backgroundColor.r; p.clearG = cam->backgroundColor.g;
            p.clearB = cam->backgroundColor.b; p.clearA = 1.0f;
            p.sky = false;
        }
        p.overlay = !p.clearColor;
        plan.passes.push_back(std::move(p));
    }
    plan.singleFullFrame = plan.passes.size() <= 1;
    return plan;
}

/// Last picture rendered for a camera that named a render target. Empty when
/// that name has not been drawn yet. The image is RGBA8.
inline Image* CameraTarget(const std::string& name) {
    static std::unordered_map<std::string, Image> images;
    if (name.empty()) return nullptr;
    auto it = images.find(name);
    return it == images.end() || !it->second.Valid() ? nullptr : &it->second;
}

/// Copy an ABGR8888 renderer buffer into the named target (RGBA).
inline void StoreCameraTarget(const std::string& name, const std::uint32_t* abgr, int w, int h) {
    if (name.empty() || !abgr || w < 1 || h < 1) return;
    static std::unordered_map<std::string, Image>& images = []() -> auto& {
        static std::unordered_map<std::string, Image> m;
        return m;
    }();
    Image& img = images[name];
    std::vector<std::uint8_t> rgba((std::size_t)w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        const std::uint32_t p = abgr[i];
        rgba[(std::size_t)i * 4 + 0] = (std::uint8_t)(p & 0xFF);
        rgba[(std::size_t)i * 4 + 1] = (std::uint8_t)((p >> 8) & 0xFF);
        rgba[(std::size_t)i * 4 + 2] = (std::uint8_t)((p >> 16) & 0xFF);
        rgba[(std::size_t)i * 4 + 3] = (std::uint8_t)((p >> 24) & 0xFF);
    }
    img.Set(w, h, std::move(rgba));
}

namespace camera_stack_detail {
inline void BlitPass(std::vector<std::uint32_t>& dst, int dw, int dh,
                     const std::uint32_t* src, const CameraPass& p) {
    if (!src || p.w < 1 || p.h < 1) return;
    for (int row = 0; row < p.h; ++row) {
        const int dy = p.y + row;
        if (dy < 0 || dy >= dh) continue;
        for (int col = 0; col < p.w; ++col) {
            const int dx = p.x + col;
            if (dx < 0 || dx >= dw) continue;
            const std::uint32_t s = src[(std::size_t)row * p.w + col];
            const int sa = (int)((s >> 24) & 255);
            std::uint32_t& d = dst[(std::size_t)dy * dw + dx];
            if (!p.overlay) {
                // A solid clear (and the first camera's forced clear) has to paint
                // the rectangle. The software rasterizer leaves untouched pixels at
                // alpha 0; without this fill a Solid Color overlay was invisible
                // and the host sky showed through the whole inset.
                if (sa == 0 && !p.sky && p.clearA > 0.5f) {
                    auto bch = [](float v) {
                        v = v < 0 ? 0 : (v > 1 ? 1 : v);
                        return (std::uint32_t)(v * 255.0f + 0.5f);
                    };
                    d = bch(p.clearR) | (bch(p.clearG) << 8) | (bch(p.clearB) << 16) | (0xFFu << 24);
                    continue;
                }
                if (sa != 0) d = s;          // sky holes stay transparent for the host sky
                continue;
            }
            if (sa <= 0) continue;
            if (sa >= 255) { d = s; continue; }
            const int da = (int)((d >> 24) & 255);
            const float a = sa / 255.0f, ia = 1.0f - a;
            auto ch = [&](int shift) {
                const float sv = (float)((s >> shift) & 255);
                const float dv = (float)((d >> shift) & 255);
                int o = (int)(sv * a + dv * ia + 0.5f);
                return o < 0 ? 0 : (o > 255 ? 255 : o);
            };
            const int oa = (int)(sa + da * ia + 0.5f);
            d = (std::uint32_t)ch(0) | ((std::uint32_t)ch(8) << 8) |
                ((std::uint32_t)ch(16) << 16) | ((std::uint32_t)(oa > 255 ? 255 : oa) << 24);
        }
    }
}
} // namespace camera_stack_detail

/// Render every pass through `renderOne` (returns an ABGR buffer of pass.w*pass.h,
/// or null) and composite. The returned buffer lives in `out`.
template <class RenderFn>
bool CompositeCameraStack(const CameraStackPlan& plan, RenderFn&& renderOne,
                          std::vector<std::uint32_t>& out) {
    out.assign((std::size_t)plan.width * plan.height, 0u);
    bool any = false;
    for (const CameraPass& p : plan.passes) {
        const std::uint32_t* src = renderOne(p);
        if (!src) continue;
        any = true;
        camera_stack_detail::BlitPass(out, plan.width, plan.height, src, p);
        if (!p.target.empty()) StoreCameraTarget(p.target, src, p.w, p.h);
    }
    return any;
}

} // namespace okay
