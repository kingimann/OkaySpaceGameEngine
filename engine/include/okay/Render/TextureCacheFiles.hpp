#pragma once
// Reloading a picture that changed on disk. The renderers' texture cache (SoftwareRenderer.hpp: TextureCacheMap and
// MipCacheMap) is keyed by the path a MeshRenderer or Sprite names, which is often relative ("Assets/Art/wall.png",
// read against the working folder), so one file can sit in it under several names. This finds them all.
// Header note: keep this header free of <mutex>, <thread> and <condition_variable>.
#include <string>

namespace okay {

/// Forget every cached picture (and its mip chain) that was loaded from `file`, under whatever name it was asked for:
/// a relative path, "./" and ".." forms, other separators, or the absolute path. Names that are not files ("@..."
/// registered pictures, "proc:..." generated ones) are never touched. Returns how many pictures were dropped; when any
/// were, TextureCacheGeneration() goes up so the GPU renderers upload again. The editor calls it for each file the
/// Project panel saw change on disk or was told to reimport.
int InvalidateTextureCacheForFile(const std::string& file);

} // namespace okay
