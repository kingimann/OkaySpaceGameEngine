#pragma once
// Private to the engine: the Assimp backend's entry points. Only ModelImport.cpp
// calls these, and only when the build has Assimp (OKAY_HAVE_ASSIMP). Assimp's own
// headers are included in ModelImportAssimp.cpp and nowhere else.
#include "okay/Render/ModelData.hpp"
#include <string>

namespace okay {
namespace assimp_backend {

/// Read `path` with Assimp into our own types. `flatten`: bake node transforms into
/// the vertices and return no skins or clips (the one-static-mesh mode).
bool Read(const std::string& path, bool flatten, ImportedModel& out, std::string* err);

/// Assimp's importable extensions, lower case, each with a leading dot, space separated.
std::string Extensions();

} // namespace assimp_backend
} // namespace okay
