#pragma once

#include <string>
#include <vector>

namespace okaycc {

// Link relocatable ELF64 objects into one static executable. Undefined names
// must be defined in some object. If main exists and _start does not, a small
// _start is added that calls main and exits with its status.
std::vector<unsigned char> linkObjects(const std::vector<std::vector<unsigned char>>& objs, std::string& error);

}  // namespace okaycc
