#pragma once
// StreamingSource: marks a point that wants the world around it loaded (a player, a camera, a
// vehicle, a trigger). Any number may exist; a cell stays loaded while any source wants it. The source
// itself does nothing: PartitionStreamer reads it (docs/foundations/world_streaming.md).
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"

namespace okay::worldpart {

class StreamingSource : public Component {
public:
    /// Cells whose box is within this distance of the source are wanted (world units).
    float loadRadius = 128.0f;
    /// Loaded cells are kept until they are farther than this. Never below loadRadius (the streamer
    /// uses the larger): the gap is the hysteresis that stops a cell loading and unloading while the
    /// source stands on a boundary.
    float unloadRadius = 160.0f;
    /// More than 1 loads this source's cells sooner (the queue sorts by distance / priority). Not a
    /// number above 0 reads as 1.
    float priority = 1.0f;

    /// Radii the streamer will use: a NaN or negative radius reads as 0, the unload radius is raised to
    /// the load radius.
    float EffectiveLoadRadius() const;
    float EffectiveUnloadRadius() const;
    float EffectivePriority() const;
};

/// Register the "streamingsource" record with the scene serializer, so the component saves and loads
/// with a scene or chunk. Safe to call many times and from any thread; the chunk code calls it itself.
void RegisterStreamingSerializers();

} // namespace okay::worldpart
