#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Render/Renderer.hpp"

namespace okay {

void SpriteRenderer::OnRender(IRenderer& renderer) {
    if (!transform || color.a <= 0.0f) return;
    Vec3 worldPos   = transform->Position();
    Vec3 worldScale = transform->LossyScale();
    Vec2 worldSize{size.x * worldScale.x, size.y * worldScale.y};
    renderer.DrawQuad(worldPos, worldSize, color, glyph);
}

} // namespace okay
