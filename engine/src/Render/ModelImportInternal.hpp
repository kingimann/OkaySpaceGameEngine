#pragma once
// Private to the engine: helpers shared by the model import files.
#include "okay/Render/ModelData.hpp"
#include <string>

namespace okay {
namespace model_import {
/// The two small helpers ModelImport.cpp and ModelImportCharacter.cpp both use (defined in ModelImport.cpp).
std::string Lower(std::string s);
std::string BaseName(const std::string& path);
} // namespace model_import

} // namespace okay
