#include "okay/Components/AssetStreamer.hpp"
#include "okay/Render/AssetStreaming.hpp"
#include "okay/Render/TextureAsset.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"

namespace okay {

void AssetStreamer::Awake() {
    streaming::Settings& cfg = streaming::Config();
    cfg.budgetMB = budgetMB;
    cfg.uploadMBPerFrame = uploadMBPerFrame;
    cfg.mipBias = mipBias;
    if (streamTextures && !textures::Streaming()) {
        textures::SetStreamStartSize(startSize);
        textures::SetStreaming(true);
        m_turnedOn = true;
    }
}

void AssetStreamer::Update(float) {
    if (gameObject && gameObject->scene()) streaming::Update(*gameObject->scene());
}

void AssetStreamer::OnDestroy() {
    if (!m_turnedOn) return;
    // Back to whole pictures for whatever runs next (the editor after Stop): read the streamed ones again in full.
    textures::SetStreaming(false);
    textures::InvalidateAll();
    m_turnedOn = false;
}

} // namespace okay
