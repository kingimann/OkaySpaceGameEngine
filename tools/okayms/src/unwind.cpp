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

Result<std::vector<uint8_t>> encode_unwind(const UnwindPrologue& prologue) {
    if (prologue.alloc != 0 && (prologue.alloc % 8u) != 0) {
        return Result<std::vector<uint8_t>>::fail("stack allocation must be a multiple of 8");
    }
    struct Slot {
        uint8_t first;
        uint8_t second;
    };
    std::vector<Slot> slots;
    auto add = [&](uint8_t offset, uint8_t op, uint8_t info) {
        slots.push_back({offset, static_cast<uint8_t>((info << 4) | op)});
    };
    if (prologue.alloc != 0) {
        if (prologue.alloc <= 128) {
            add(prologue.alloc_at, 2, static_cast<uint8_t>(prologue.alloc / 8u - 1u));
        } else if (prologue.alloc < (512u * 1024u)) {
            add(prologue.alloc_at, 1, 0);
            const uint32_t scaled = prologue.alloc / 8u;
            slots.push_back({static_cast<uint8_t>(scaled), static_cast<uint8_t>(scaled >> 8)});
        } else {
            add(prologue.alloc_at, 1, 1);
            slots.push_back({static_cast<uint8_t>(prologue.alloc), static_cast<uint8_t>(prologue.alloc >> 8)});
            slots.push_back({static_cast<uint8_t>(prologue.alloc >> 16), static_cast<uint8_t>(prologue.alloc >> 24)});
        }
    }
    if (prologue.frame_register != 0) add(prologue.frame_at, 3, 0);
    for (size_t i = prologue.pushes.size(); i > 0; --i) {
        const UnwindPush& push = prologue.pushes[i - 1];
        add(push.at, 0, push.reg);
    }
    const uint8_t count = static_cast<uint8_t>(slots.size());
    if ((slots.size() & 1u) != 0) slots.push_back({0, 0});
    std::vector<uint8_t> bytes;
    bytes.push_back(0x01);
    bytes.push_back(prologue.size);
    bytes.push_back(count);
    bytes.push_back(prologue.frame_register);
    for (const Slot& slot : slots) {
        bytes.push_back(slot.first);
        bytes.push_back(slot.second);
    }
    return Result<std::vector<uint8_t>>::success(std::move(bytes));
}

}  // namespace okayms
