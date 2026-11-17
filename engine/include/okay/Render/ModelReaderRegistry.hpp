#pragma once
#include "okay/Render/ModelData.hpp"
#include <functional>
#include <string>

namespace okay {

/// A model-format reader as the importer calls it: read `path` into `out`; `flatten` bakes node transforms for the
/// readers that have them; `loader` lets a scene format (Irrlicht, LightWave) read the meshes it names.
using ModelReaderFn = std::function<bool(const std::string& path, ImportedModel& out, std::string* err, bool flatten,
                                         const IrrMeshLoader* loader)>;

/// Readers are registered by slot number (the numbers in ModelImport.cpp's OwnSlot table). The model_import module
/// registers all of them from its register.cpp; OBJ, STL and PLY (slots 0 to 2) are read by core code and need no entry.
/// A build without the module has no entries and the importer says so.
void RegisterModelReader(int slot, ModelReaderFn fn);
const ModelReaderFn* FindModelReader(int slot);

} // namespace okay
