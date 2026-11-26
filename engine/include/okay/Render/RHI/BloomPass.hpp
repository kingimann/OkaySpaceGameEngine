#pragma once
// ---------------------------------------------------------------------------
// The bloom (glow) pass on the RHI, the first rendering feature moved onto it
// (docs/rendering/rhi.md, "Migration order"). It is the OpenGL renderer's Bloom
// for frames without the scene post chain (View > Lighting "Bloom"): a bright pass
// of the frame into a half-size target, two rounds of a separable 9-tap gaussian
// (horizontal then vertical), and the blurred glow added back onto the frame with
// additive blending (alpha kept, so the host's sky behind empty pixels stays).
// The same pass runs on every RHI backend; on Software it is the reference the
// OpenGL picture is checked against (tests/test_rhi_gl.cpp).
//
// kFullscreenVertGlsl and kGaussianBlurGlsl are also the OpenGL renderer's
// full-screen vertex shader and blur (SSAO and the post chain use them), so the
// text lives here once.
// ---------------------------------------------------------------------------
#include "okay/Render/RHI/RHI.hpp"

namespace okay {
namespace rhi {

extern const char* const kFullscreenVertGlsl;   ///< attribute vec2 aPos (clip space) -> varying vec2 vUV
extern const char* const kGaussianBlurGlsl;     ///< uniform sampler2D uSrc; uniform vec2 uDir (one texel)

class BloomPass {
public:
    BloomPass() = default;
    ~BloomPass() { Shutdown(); }
    BloomPass(const BloomPass&) = delete;
    BloomPass& operator=(const BloomPass&) = delete;

    /// Make the shaders, pipelines, sampler and quad on `dev`. False when the device
    /// refused one (dev.LastError() says why).
    bool Init(Device& dev);
    /// Free everything made on the device (the device must still exist).
    void Shutdown();
    bool Ready() const { return m_dev != nullptr; }

    /// Add the glow of `frame` (a w x h RGBA8 or RGBA16F render-target texture) onto it.
    /// threshold: luminance (0..1) above which a pixel glows; intensity: how strongly the
    /// glow is added. Uses two half-size targets kept between calls, one pair per frame
    /// size for the last kMaxSizes sizes (the editor draws several views of different
    /// sizes each frame; their targets are not made again every frame).
    bool Apply(TextureHandle frame, int w, int h, float threshold, float intensity);

    static constexpr int kMaxSizes = 4;
    int TargetSizes() const { return (int)m_targets.size(); }

private:
    struct Targets { int hw = 0, hh = 0; TextureHandle a, b; std::uint64_t lastUse = 0; };
    Targets* EnsureTargets(int hw, int hh);
    Device* m_dev = nullptr;
    ShaderHandle m_brightSh, m_blurSh, m_addSh;
    PipelineHandle m_bright, m_blur, m_add;
    BufferHandle m_quad;
    SamplerHandle m_linear;
    std::vector<Targets> m_targets;
    std::uint64_t m_use = 0;
    CommandList m_list;
};

} // namespace rhi
} // namespace okay
