#pragma once
// A very small header so a cache that lives in a header-only or low-level file (the picture cache, the audio decode cache)
// can report its size to the AssetManager and be trimmed by its memory budget, without pulling the manager's own header
// (and its thread includes) into that file. Written for this engine; see docs/asset_manager.md.
#include <cstddef>
#include <functional>
#include <string>

namespace okay {

/// Registers (or replaces, by name) a cache with AssetManager::Instance(). `trim(target)` should drop the least useful items
/// until the cache is at most `target` bytes (best effort); `clear()` drops everything. Callbacks run on the thread that
/// calls AssetManager::Pump() or Trim() (the main thread), never while a frame is drawing.
void RegisterAssetCache(const std::string& name, std::function<std::size_t()> bytes, std::function<std::size_t()> count,
                        std::function<void(std::size_t targetBytes)> trim, std::function<void()> clear);

} // namespace okay
