#pragma once
// Our own DXF (.dxf, ASCII) reader. See docs/own_dxf_reader.md.
//
// Reads the surface entities of an ASCII DXF: 3DFACE (triangles and quads), POLYLINE polyface meshes and M x N polygon
// meshes (with their VERTEX records), and MESH (AC1021 and later); blocks (BLOCKS section) placed by INSERT with a
// position, scale and rotation about Z (nested up to 8 deep). One node and one part per layer; the layer name is the node
// name. Equal coordinates of 3DFACE entities are welded, then smooth normals are made with a 45 degree crease angle.
// Everything else (lines, points, arcs, circles, text, 2D polylines, splines, solids, hatches, dimensions, INSERT arrays,
// colours, extrusion directions other than +Z ...) is counted by entity name in `out.notes`. Binary DXF is refused.
// DXF world coordinates are Z-up: the root node carries the Z-up to Y-up rotation (x, y, z) -> (x, z, -y) like 3MF and the 3DS
// reader (Assimp's DXF importer does the same), `flatten` bakes it into the vertices. Units are not converted.
#include "okay/Render/ModelData.hpp"

namespace okay {
bool ReadDxfBuffer(const std::string& text, ImportedModel& out, std::string* err = nullptr, bool flatten = false);
bool ReadDxfFile(const std::string& path, ImportedModel& out, std::string* err = nullptr, bool flatten = false);
}
