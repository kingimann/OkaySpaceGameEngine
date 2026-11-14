#pragma once
// Our own LightWave object (.lwo) reader: LWO2 (and LWO3, which uses the same chunks) and LWO1 ("LWOB"). See docs/own_lwo_reader.md.
//
// Reads layers (one node each, parent layers kept when they come first), points, polygons (FACE and PTCH as plain polygons,
// convex and concave ones triangulated), surfaces (colour times diffuse, luminosity as emissive, glossiness as a Phong exponent,
// transparency as alpha, the smoothing angle as the crease angle, the first colour image map with its file name), per-point UVs
// (VMAP TXUV) and per-polygon-corner UVs (VMAD). One part per (layer, surface). Everything not read (envelopes, shader and
// procedural blocks, texture projections other than UV, curves, bones, vertex weights, colour maps, smoothing groups, hidden
// layers, pivots ...) is listed in `out.notes`. Coordinates are kept as written (no axis change, no handedness change).
#include "okay/Render/ModelData.hpp"

namespace okay {
/// `baseDir` ends with a slash or is empty (image files are looked up relative to it).
bool ReadLwoBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err = nullptr);
bool ReadLwoFile(const std::string& path, ImportedModel& out, std::string* err = nullptr);
}
