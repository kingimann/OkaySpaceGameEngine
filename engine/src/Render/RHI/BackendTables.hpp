#pragma once
// The bookkeeping every GPU backend of the RHI shares: its resources live in maps from handle id
// to the backend's own record (GLDevice, D3D11Device). These small helpers are the one copy of the
// lookups those backends make the same way. Internal to engine/src/Render/RHI. Written by us.
#include "okay/Render/RHI/RHI.hpp"

#include <cstddef>
#include <cstdint>

namespace okay {
namespace rhi {
namespace table {

/// Resources alive in all the maps given.
template <class... Maps> std::size_t Count(const Maps&... maps) { return (std::size_t{0} + ... + maps.size()); }

/// Width and height of a texture record (fields w, h); false for an unknown id.
template <class Map> bool TextureSize(const Map& textures, std::uint32_t id, int& w, int& h) {
    auto it = textures.find(id);
    if (it == textures.end()) return false;
    w = it->second.w; h = it->second.h;
    return true;
}

/// The back buffer texture of a swap chain (map: swap chain id -> texture id).
template <class Map> TextureHandle BackBuffer(const Map& swap, std::uint32_t id) {
    auto it = swap.find(id);
    return it == swap.end() ? TextureHandle{} : TextureHandle{it->second};
}

/// Free a record with `release(record)` and forget it; nothing for an unknown id.
template <class Map, class F> void Erase(Map& m, std::uint32_t id, F&& release) {
    auto it = m.find(id);
    if (it == m.end()) return;
    release(it->second);
    m.erase(it);
}

/// A pipeline and its shader for a SetPipeline command (records with desc.shader). Null and the
/// reason when either is gone.
template <class PipeMap, class ShaderMap, class P, class S>
const char* FindPipeline(PipeMap& pipes, ShaderMap& shaders, std::uint32_t id, P*& pipe, S*& shader) {
    auto it = pipes.find(id);
    if (it == pipes.end()) return "unknown pipeline";
    auto sh = shaders.find(it->second.desc.shader.id);
    if (sh == shaders.end()) return "the pipeline's shader was destroyed";
    pipe = &it->second; shader = &sh->second;
    return nullptr;
}

} // namespace table
} // namespace rhi
} // namespace okay
