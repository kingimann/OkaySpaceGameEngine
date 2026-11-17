#include "okayms/unwind.hpp"

namespace okayms {

Result<std::vector<uint8_t>> encode_frame_unwind(uint32_t stack_bytes) {
    // UWOP_ALLOC_SMALL covers 8 to 128 bytes. The info field is size/8 - 1.
    if (stack_bytes < 8 || stack_bytes > 128 || (stack_bytes % 8u) != 0) {
        return Result<std::vector<uint8_t>>::fail(
            "stack allocation must be a multiple of 8 from 8 to 128");
    }
    const uint8_t opinfo = static_cast<uint8_t>(stack_bytes / 8u - 1u);
    // Codes are stored last-operation first. An odd count is padded so the
    // record stays a multiple of 4 bytes, which is where a handler would go.
    const uint8_t bytes[] = {
        0x01, 0x08, 0x03, 0x05,
        0x08, static_cast<uint8_t>((opinfo << 4) | 0x02),
        0x04, 0x03,
        0x01, 0x50,
        0x00, 0x00};
    return Result<std::vector<uint8_t>>::success(std::vector<uint8_t>(bytes, bytes + sizeof bytes));
}

}  // namespace okayms
