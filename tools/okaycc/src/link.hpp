#pragma once

#include <string>
#include <vector>

namespace okaycc {

// Link relocatable ELF64 objects into one static executable. Undefined names
// must be defined in some object. If main exists and _start does not, a small
// _start is added that calls main and exits with its status.
std::vector<unsigned char> linkObjects(const std::vector<std::vector<unsigned char>>& objs, std::string& error);

// The start code's machine code for main's arguments (both linkObjects and compileToElf use it).
// kStartArgs, first: rbx = argc ([rsp]), r12 = argv (rsp + 8), r13 = envp (argv + argc + 1).
// kMainArgs, just before the call of main: edi = ebx, rsi = r12, rdx = r13.
extern const unsigned char kStartArgs[14];
extern const unsigned char kMainArgs[8];

}  // namespace okaycc
