// Our own Ogex reader: not written yet (the tests in tests/test_ogex_reader.cpp and tests/test_e4_hostile.cpp come first).
#include "okay/Render/ModelData.hpp"
#include <string>

namespace okay {
bool ReadOgexBuffer(const std::string&, const std::string&, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    if (err) *err = "Ogex: not written yet";
    return false;
}
bool ReadOgexFile(const std::string&, ImportedModel& out, std::string* err) { return ReadOgexBuffer(std::string(), std::string(), out, err); }
}   // namespace okay
