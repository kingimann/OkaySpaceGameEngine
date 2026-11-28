#include "okay/Testing/Regression.hpp"

#include "okay/Components/Camera.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Scene/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace okay::testing {

bool RenderSceneToImage(const Scene& scene, Image& out, const SceneRenderOptions& options, std::string* error) {
    Camera* cam = options.camera;
    const SceneRenderOptions::View& view = options.view;
    if (view.use) cam = nullptr;
    else if (!cam) {
        cam = scene.mainCamera;
        if (!CameraUsable(cam)) {
            cam = nullptr;
            for (Camera* c : scene.FindObjectsOfType<Camera>())
                if (CameraUsable(c)) { cam = c; break; }
        }
    }
    if (!view.use && (!cam || !cam->transform)) {
        if (error) *error = "the scene has no enabled camera to draw from";
        return false;
    }
    const int w = std::max(1, std::min(options.width, 4096));
    const int h = std::max(1, std::min(options.height, 4096));
    const int ss = std::max(1, std::min(options.supersample, 8));

    ApplySceneLight(scene);
    const float aspect = (float)w / (float)h;
    Mat4 vp;
    Vec3 eye;
    if (view.use) {
        // Up is world up unless the view looks straight up or down.
        const Vec3 d = view.target - view.eye;
        const Vec3 up = (std::fabs(d.x) < 1e-4f && std::fabs(d.z) < 1e-4f) ? Vec3{0, 0, -1} : Vec3::Up;
        const float nearP = view.nearClip > 1e-3f ? view.nearClip : 1e-3f, farP = view.farClip > nearP + 0.01f ? view.farClip : nearP + 0.01f;
        const Mat4 proj = view.orthographic
            ? Mat4::Ortho(-view.orthographicSize * aspect, view.orthographicSize * aspect, -view.orthographicSize, view.orthographicSize, nearP, farP)
            : Mat4::Perspective(std::max(1.0f, std::min(170.0f, view.fovDegrees)), aspect, nearP, farP);
        vp = proj * Mat4::LookAt(view.eye, view.target, up);
        eye = view.eye;
    } else {
        vp = cam->ProjectionMatrix(aspect) * cam->ViewMatrix();
        eye = cam->transform->Position();
    }
    Raster work;
    std::vector<std::uint32_t> averaged;
    const std::uint32_t* px = RenderMeshesSS(work, averaged, scene, vp, eye, w, h, ss, cam ? cam->ignoreObject : nullptr);

    // The pass leaves pixels it never touched transparent; show `background` there. The
    // colour channels are already weighted by coverage, so the background fills what is left.
    const float bgR = options.background.r * 255.0f, bgG = options.background.g * 255.0f, bgB = options.background.b * 255.0f;
    out = Image(w, h);
    std::uint8_t* dst = out.Data();
    for (int i = 0; i < w * h; ++i) {
        const std::uint32_t p = px[i];
        const float r = (float)(p & 255u), g = (float)((p >> 8) & 255u), b = (float)((p >> 16) & 255u);
        const float a = (float)((p >> 24) & 255u) / 255.0f;
        auto mix = [&](float c, float bg) { return (std::uint8_t)std::min(255.0f, c + bg * (1.0f - a) + 0.5f); };
        dst[i * 4 + 0] = mix(r, bgR);
        dst[i * 4 + 1] = mix(g, bgG);
        dst[i * 4 + 2] = mix(b, bgB);
        dst[i * 4 + 3] = 255;
    }
    return true;
}

} // namespace okay::testing
