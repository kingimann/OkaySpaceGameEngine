#pragma once

// Link COFF objects into a PE32+ image. Field layout follows Microsoft's
// "PE Format" document. Relocation numbers are the AMD64 values from
// that document.

#include "okayms/coff.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace okayms {

constexpr uint16_t kSubsystemConsole = 3;
constexpr uint16_t kSubsystemWindowsGui = 2;

struct LinkOptions {
    std::string entry = "entry";
    uint64_t image_base = 0x140000000ULL;
    uint32_t section_alignment = 0x1000;
    uint32_t file_alignment = 0x200;
    uint16_t subsystem = kSubsystemConsole;
    uint16_t major_operating_system = 6;
    uint16_t minor_operating_system = 0;
    uint16_t major_subsystem = 6;
    uint16_t minor_subsystem = 0;
    uint64_t stack_reserve = 0x100000;
    uint64_t stack_commit = 0x1000;
    uint64_t heap_reserve = 0x100000;
    uint64_t heap_commit = 0x1000;
    // IMAGE_DLLCHARACTERISTICS_NX_COMPAT. Dynamic base is left off until
    // a base-relocation table is written.
    uint16_t dll_characteristics = 0x0100;
};

Result<std::vector<uint8_t>> link_executable(const std::vector<ObjectFile>& objects,
                                             const LinkOptions& options);

}  // namespace okayms
