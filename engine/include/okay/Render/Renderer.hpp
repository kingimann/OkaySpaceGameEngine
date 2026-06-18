#pragma once
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Color.hpp"

namespace okay {

struct Viewport {
    int width  = 0;
    int height = 0;
    float Aspect() const { return height > 0 ? float(width) / float(height) : 1.0f; }
};

/// Backend-agnostic rendering interface. The engine talks only to this; a
/// console rasterizer ships in the box, and an OpenGL/Vulkan backend could be
/// dropped in by implementing the same five calls.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /// Begin a frame, clearing to the given color.
    virtual void BeginFrame(const Color& clear) = 0;
    /// Present the finished frame.
    virtual void EndFrame() = 0;

    /// Configure the active orthographic camera. `orthoSize` is the half-height
    /// of the view in world units (matching Unity's orthographic camera).
    virtual void SetCamera(const Vec3& position, float orthoSize, float aspect) = 0;

    /// Draw an axis-aligned, world-space quad centered at `center`.
    virtual void DrawQuad(const Vec3& center, const Vec2& size,
                          const Color& color, char glyph = '#') = 0;

    virtual Viewport GetViewport() const = 0;
};

} // namespace okay
