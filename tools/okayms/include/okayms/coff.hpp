#pragma once

// COFF object files, as described by Microsoft's "PE Format" document.
// This header is the in-memory form used by the writer and the reader.
// An executable image is a different file and is not built here.

#include "okayms/result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace okayms {

// IMAGE_FILE_MACHINE_AMD64
constexpr uint16_t kMachineAmd64 = 0x8664;

// AMD64 relocation type numbers from the PE Format document.
constexpr uint16_t kRelAmd64Absolute = 0;
constexpr uint16_t kRelAmd64Addr64 = 1;
constexpr uint16_t kRelAmd64Addr32 = 2;
constexpr uint16_t kRelAmd64Addr32Nb = 3;
constexpr uint16_t kRelAmd64Rel32 = 4;
// Used by the CodeView line tables in .debug$S (PE Format, AMD64 relocations).
constexpr uint16_t kRelAmd64Section = 10;
constexpr uint16_t kRelAmd64Secrel = 11;

// IMAGE_SYM_CLASS_* storage classes we accept.
constexpr uint8_t kClassExternal = 2;
constexpr uint8_t kClassStatic = 3;
constexpr uint8_t kClassLabel = 6;
constexpr uint8_t kClassFile = 103;
constexpr uint8_t kClassSection = 104;
constexpr uint8_t kClassWeakExternal = 105;

// Section-number sentinels. A positive section number is 1-based.
constexpr int16_t kSectionUndefined = 0;
constexpr int16_t kSectionAbsolute = -1;
constexpr int16_t kSectionDebug = -2;

// Section flags used by later stages. The reader keeps any value.
constexpr uint32_t kScnCntCode = 0x00000020;
constexpr uint32_t kScnCntInitData = 0x00000040;
constexpr uint32_t kScnCntUninitData = 0x00000080;
constexpr uint32_t kScnLnkInfo = 0x00000200;
constexpr uint32_t kScnLnkRemove = 0x00000800;
constexpr uint32_t kScnLnkComdat = 0x00001000;
constexpr uint32_t kScnMemExecute = 0x20000000;
constexpr uint32_t kScnMemRead = 0x40000000;
constexpr uint32_t kScnMemWrite = 0x80000000;
constexpr uint32_t kScnAlign1 = 0x00100000;
constexpr uint32_t kScnAlign8 = 0x00400000;
constexpr uint32_t kScnAlign16 = 0x00500000;

// A relocation's symbol field is an index into ObjectFile::symbols
// (a logical symbol). Auxiliary records are not addressable this way.
// The file stores the raw table index, which counts auxiliary records.
struct Relocation {
    uint32_t offset = 0;
    uint32_t symbol = 0;
    uint16_t type = 0;
};

// aux holds zero or more 18-byte auxiliary records, kept opaque.
// The COFF symbol's NumberOfAuxSymbols is aux.size() / 18.
struct Symbol {
    std::string name;
    uint32_t value = 0;
    int16_t section = kSectionUndefined;
    uint16_t type = 0;
    uint8_t storage = kClassExternal;
    std::vector<uint8_t> aux;
};

struct Section {
    std::string name;
    uint32_t virtual_size = 0;
    uint32_t virtual_address = 0;
    std::vector<uint8_t> data;
    std::vector<Relocation> relocations;
    uint32_t characteristics = 0;
};

struct ObjectFile {
    uint16_t machine = kMachineAmd64;
    uint32_t time_date_stamp = 0;
    uint16_t characteristics = 0;
    std::vector<Section> sections;
    std::vector<Symbol> symbols;
};

// Write a COFF object. The layout is packed, little-endian, with no
// padding between the header, section headers, raw bytes, relocations,
// the symbol table and the string table. A string table is always
// written when there is at least one symbol. A long section name with
// no symbols still writes a string table and points PointerToSymbolTable
// at it, with NumberOfSymbols left at zero.
Result<std::vector<uint8_t>> write_coff(const ObjectFile& object);

// Read a COFF object. Rejects a truncated file, a count that does not
// fit, a relocation that points at an auxiliary record, and a string
// that runs off the string table.
Result<ObjectFile> read_coff(const uint8_t* data, size_t size);

}  // namespace okayms
