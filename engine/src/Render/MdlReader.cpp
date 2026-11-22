// Our own Mdl reader: not written yet (the tests in tests/test_mdl_reader.cpp and tests/test_e4_hostile.cpp come first).
#include "okay/Render/ModelData.hpp"
#include <string>

namespace okay {
bool ReadMdlBuffer(const std::string&, const std::string&, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    if (err) *err = "Mdl: not written yet";
    return false;
}
bool ReadMdlFile(const std::string&, ImportedModel& out, std::string* err) { return ReadMdlBuffer(std::string(), std::string(), out, err); }
}   // namespace okay
