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

// One nonvolatile register saved by a push. `reg` is the x64 register
// number (rbp is 5, rbx is 3). `at` is the prolog offset just after the push.
struct UnwindPush {
    uint8_t reg = 0;
    uint8_t at = 0;
};

// A prologue the unwind record describes. `alloc` is the byte count of
// `sub rsp`. Allocations above 128 bytes use UWOP_ALLOC_LARGE.
struct UnwindPrologue {
    uint8_t size = 0;
    uint8_t frame_register = 0;
    uint8_t frame_at = 0;
    uint32_t alloc = 0;
    uint8_t alloc_at = 0;
    std::vector<UnwindPush> pushes;
};

Result<std::vector<uint8_t>> encode_unwind(const UnwindPrologue& prologue);

}  // namespace okayms
