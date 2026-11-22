#pragma once
// Asset Streaming component (feature map 2.13): put one in a scene to stream that scene's assets while it plays.
//  * Textures start at a small mip and sharpen in the background as the camera comes close (Render/AssetStreaming.hpp),
//    within `budgetMB` of texture memory.
//  * With `backgroundMeshes`, the player reads the scene's model files on the job system and shows a placeholder cube
//    until each one is ready (the editor always loads them at once).
// It plans once a frame from the camera the renderers used last. Nothing happens in the editor until Play.
#include "okay/Scene/Component.hpp"

namespace okay {

class AssetStreamer : public Component {
public:
    bool  streamTextures = true;
    int   startSize = 64;            ///< longest side a picture is loaded at first (pixels)
    float budgetMB = 256.0f;         ///< texture memory to keep to; 0 = the texture library's budget
    float uploadMBPerFrame = 16.0f;  ///< new mip pixels queued per frame (0 = no cap)
    float mipBias = 0.0f;            ///< > 0 blurrier and cheaper, < 0 sharper
    bool  backgroundMeshes = true;   ///< read model files in the background (the player, on scene load)

    void Awake() override;
    void Update(float deltaTime) override;
    void OnDestroy() override;

private:
    bool m_turnedOn = false;
};

} // namespace okay
