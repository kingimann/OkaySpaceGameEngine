#pragma once

// x64 unwind info, from Microsoft's "x64 exception handling" document.
// encode_frame_unwind describes this prologue:
//   push rbp
//   mov rbp, rsp
//   sub rsp, stack_bytes
// stack_bytes is the operand of sub, from 8 to 128 and a multiple of 8.

#include "okayms/result.hpp"

#include <cstdint>
#include <vector>

namespace okayms {

Result<std::vector<uint8_t>> encode_frame_unwind(uint32_t stack_bytes);

}  // namespace okayms
