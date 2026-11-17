#pragma once
// Browsing the .okaymesh parts in a folder (the project's Assets), so a modeler can pick one and drop it into the scene.
#include <cstdint>
#include <string>
#include <vector>

namespace okay::modeling {

struct MeshAssetInfo {
    std::string path;        ///< full path as found
    std::string name;        ///< file name without the folder and the ".okaymesh"
    std::string folder;      ///< the folder it is in, relative to the folder that was searched ("" = directly inside)
    int vertices = 0, faces = 0;   ///< from the file's header; 0 when it is unreadable
    std::uint64_t bytes = 0;
    bool readable = false;   ///< the file starts with a valid "okaymesh 1" header and has counts
};

/// Every .okaymesh under `root` (sub-folders too, at most `maxFiles` of them, `maxDepth` levels deep), sorted by folder then name.
/// Only the header is read, so a folder of big meshes lists quickly. A missing folder gives an empty list.
std::vector<MeshAssetInfo> ListMeshAssets(const std::string& root, std::size_t maxFiles = 500, int maxDepth = 6);

/// The entries whose name or folder contains `filter` (case-insensitive; empty keeps all).
std::vector<MeshAssetInfo> FilterMeshAssets(const std::vector<MeshAssetInfo>& all, const std::string& filter);

} // namespace okay::modeling
