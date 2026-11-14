#include "okay/Testing/Regression.hpp"

#include "okay/Components/Camera.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Scene/Scene.hpp"

#include <algorithm>
#include <vector>

namespace okay::testing {

bool RenderSceneToImage(const Scene& scene, Image& out, const SceneRenderOptions& options, std::string* error) {
    Camera* cam = options.camera;
    if (!cam) {
        cam = scene.mainCamera;
        if (!CameraUsable(cam)) {
            cam = nullptr;
            for (Camera* c : scene.FindObjectsOfType<Camera>())
                if (CameraUsable(c)) { cam = c; break; }
        }
    }
    if (!cam || !cam->transform) {
        if (error) *error = "the scene has no enabled camera to draw from";
        return false;
    }
    const int w = std::max(1, std::min(options.width, 4096));
    const int h = std::max(1, std::min(options.height, 4096));
    const int ss = std::max(1, std::min(options.supersample, 8));

    ApplySceneLight(scene);
    const Mat4 vp = cam->ProjectionMatrix((float)w / (float)h) * cam->ViewMatrix();
    Raster work;
    std::vector<std::uint32_t> averaged;
    const std::uint32_t* px = RenderMeshesSS(work, averaged, scene, vp, cam->transform->Position(), w, h, ss, cam->ignoreObject);

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
