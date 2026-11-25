#pragma once

// COFF archive (.lib), from the archive section of Microsoft's "PE Format"
// document. Short import members use the import-library header in that
// document: signature 0xFFFF0000, then the symbol and DLL names.

#include "okayms/coff.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace okayms {

constexpr uint16_t kImportCode = 0;
constexpr uint16_t kImportData = 1;
constexpr uint16_t kImportConst = 2;
constexpr uint16_t kImportByOrdinal = 0;
constexpr uint16_t kImportByName = 1;

struct ImportMember {
    uint16_t machine = kMachineAmd64;
    uint16_t hint_or_ordinal = 0;
    uint16_t import_type = kImportCode;
    uint16_t name_type = kImportByName;
    std::string symbol;
    std::string dll;
};

struct ArchiveMember {
    std::string name;
    bool is_import = false;
    ObjectFile object;
    ImportMember import;
};

struct Archive {
    std::vector<ArchiveMember> members;
    // Write the first and second linker members (the "/" members that index every public symbol, "PE Format",
    // "First Linker Member" and "Second Linker Member") before the members, so other linkers (link.exe, GNU ld) can
    // find what each member defines. okay-link does not need them; read_archive skips them.
    bool symbol_index = false;
};

// The public symbols of a member, as the linker members list them: __imp_name and (for code) name for an import, the
// external defined symbols of an object.
std::vector<std::string> member_symbols(const ArchiveMember& member);

Result<std::vector<uint8_t>> write_archive(const Archive& archive);
Result<Archive> read_archive(const uint8_t* data, size_t size);

}  // namespace okayms
