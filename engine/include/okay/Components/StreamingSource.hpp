#pragma once
// A place the streamed world is seen from (feature map 27.4). Every World Streamer, Level Streaming
// Volume and Voxel World in the scene keeps the content near each enabled Streaming Source loaded, as
// well as near its own target (the main camera by default). Put one on each player of a split-screen or
// networked game, on a cutscene camera, or on a vehicle that drives ahead of the player.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <vector>

namespace okay {

class StreamingSource : public Behaviour {
public:
    /// Ranks cells in front of this source first (the World Streamer's view-direction priority).
    bool useViewDirection = true;

    /// Every enabled source on an active object in `scene`: its position and (when used) its forward.
    struct Point { Vec3 position; Vec3 direction; };
    static std::vector<Point> Collect(const Scene* scene) {
        std::vector<Point> out;
        if (!scene) return out;
        for (StreamingSource* s : scene->FindObjectsOfType<StreamingSource>()) {
            if (!s || !s->enabled || !s->gameObject || !s->gameObject->active || !s->gameObject->transform) continue;
            const Transform* t = s->gameObject->transform;
            out.push_back({t->Position(), s->useViewDirection ? t->Forward() : Vec3{0, 0, 0}});
        }
        return out;
    }
};

} // namespace okay
