// Our own Quake III map reader: not written yet (the tests in tests/test_q3bsp_reader.cpp and tests/test_q3bsp_hostile.cpp come first).
#include "okay/Render/ModelData.hpp"
#include <string>

namespace okay {
bool ReadQ3bspBuffer(const std::string&, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    if (err) *err = "Q3BSP: not written yet";
    return false;
}
bool ReadQ3bspFile(const std::string&, ImportedModel& out, std::string* err) { return ReadQ3bspBuffer(std::string(), out, err); }
}   // namespace okay
