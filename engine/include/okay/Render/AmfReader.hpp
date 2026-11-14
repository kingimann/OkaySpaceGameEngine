#pragma once
// Our own AMF (.amf, Additive Manufacturing File Format) reader: XML, optionally zip-compressed. See docs/own_amf_reader.md.
//
// Reads objects with their meshes (vertices, volumes of triangles), materials and colours (one part per resolved
// colour: triangle colour, else the volume's material or colour, else the object's colour), constellations
// (instances with a delta and an x, y, z rotation in degrees) and object names. Everything else (textures, vertex
// colours, vertex normals, formula colours, curved edges, nested constellations) is listed in `out.notes`.
// Z-up like 3MF: the root node carries the Z-up to Y-up rotation, `flatten` bakes it. Smooth normals with a 45 degree
// crease angle (the format's own vertex normals are not used). Units are kept as written.
#include "okay/Render/ModelData.hpp"

namespace okay {
bool ReadAmfBuffer(const std::string& bytes, ImportedModel& out, std::string* err = nullptr, bool flatten = false);
bool ReadAmfFile(const std::string& path, ImportedModel& out, std::string* err = nullptr, bool flatten = false);
}
