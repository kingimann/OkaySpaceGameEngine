#pragma once
// Our own 3MF (.3mf) reader: a zip package with one XML model part. See docs/own_3mf_reader.md.
//
// Reads the core specification: meshes (vertices, triangles), component trees and build items with their 3x4 transforms
// (mirrored transforms flip the winding), base materials and colour groups (one part per material, diffuse colour and
// alpha), object types (support objects are not output). Everything else (textures, composite and multi-properties,
// beam lattice, slices, other-file components, the production extension's references) is listed in `out.notes`.
// The package is Z-up; the root node carries the Z-up to Y-up rotation, `flatten` bakes it into the vertices.
// Without normals in the file, smooth normals are made with a 45 degree crease angle. Units are kept as written.
#include "okay/Render/ModelData.hpp"

namespace okay {
bool Read3mfBuffer(const std::string& bytes, ImportedModel& out, std::string* err = nullptr, bool flatten = false);
bool Read3mfFile(const std::string& path, ImportedModel& out, std::string* err = nullptr, bool flatten = false);
}
