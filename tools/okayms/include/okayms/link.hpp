#pragma once

// Link COFF objects into a PE32+ image. Field layout follows Microsoft's
// "PE Format" document. Relocation numbers are the AMD64 values from
// that document.

#include "okayms/archive.hpp"
#include "okayms/coff.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace okayms {

constexpr uint16_t kSubsystemConsole = 3;
constexpr uint16_t kSubsystemWindowsGui = 2;

// One function imported by name from a DLL. The linker defines
// `__imp_<name>` on the import address table and `<name>` as a
// jmp-through-the-table thunk.
struct ImportedSymbol {
    std::string dll;
    std::string name;
    uint16_t hint = 0;
};

struct LinkOptions {
    std::string entry = "entry";
    std::vector<ImportedSymbol> imports;
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
    bool dll = false;
    // Names of defined symbols to put in the export table. The DLL's
    // file name (for example "math.dll") is image_name.
    std::vector<std::string> exports;
    std::string image_name;
};

Result<std::vector<uint8_t>> link_executable(const std::vector<ObjectFile>& objects,
                                             const LinkOptions& options);

// Reads archives, pulls members that define a symbol still missing from
// `objects`, and links the result. A short import member becomes an import.
Result<std::vector<uint8_t>> link_with_libraries(std::vector<ObjectFile> objects,
                                                 const std::vector<Archive>& libraries,
                                                 LinkOptions options);

}  // namespace okayms
