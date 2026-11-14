#pragma once
// Our own X3D (.x3d, XML encoding) reader. See docs/own_x3d_reader.md.
//
// Reads the scene graph's static geometry: Group / Transform / Collision / Anchor / Billboard / StaticGroup / Switch / LOD
// (the first level) with DEF / USE reuse, Shape with Appearance > Material (diffuse colour, transparency as alpha, emissive,
// shininess) and ImageTexture (the first url), and the geometry IndexedFaceSet (coordIndex, ccw, creaseAngle, normals and
// texture coordinates with their index arrays), IndexedTriangleSet, TriangleSet, Box, Sphere, Cylinder, Cone and Extrusion (no UVs). Every
// Transform is baked into the vertices (one node per shape). Y-up like the engine, so there is no axis change.
// Everything not read (ElevationGrid, lines and points, Text, colours, animation and routes, scripts, lights,
// Inline, protos, ...) is listed by node name in `out.notes`. The classic VRML encoding (.x3dv, .wrl) and the binary
// encoding (.x3db) are not read.
#include "okay/Render/ModelData.hpp"

namespace okay {
/// `baseDir` ends with a slash or is empty (texture files are looked up relative to it).
bool ReadX3dBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err = nullptr, bool flatten = false);
bool ReadX3dFile(const std::string& path, ImportedModel& out, std::string* err = nullptr, bool flatten = false);
}
