// Our own glTF 1.0 reader: not written yet (the tests in tests/test_gltf1_reader.cpp and tests/test_gltf1_hostile.cpp come first).
#include "okay/Render/ModelData.hpp"
#include <string>

namespace okay {
bool ReadGltf1Buffer(const std::string&, const std::string&, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    if (err) *err = "glTF 1.0: not written yet";
    return false;
}
bool ReadGltf1File(const std::string&, ImportedModel& out, std::string* err) { return ReadGltf1Buffer(std::string(), std::string(), out, err); }
bool IsGltf1(const std::string&) { return false; }
bool GltfFileIsVersion1(const std::string&) { return false; }
}   // namespace okay
