// Byte-level tests for the COFF writer and reader.
// The expected arrays are calculated from the PE Format document:
// 20-byte file header, 40-byte section header, 10-byte relocation,
// 18-byte symbol, string table size word, little-endian. They are not
// the output of another toolchain.

#include "check.hpp"
#include "okayms/coff.hpp"

#include <cstring>
#include <string>

namespace {

using okayms::ObjectFile;
using okayms::Relocation;
using okayms::Result;
using okayms::Section;
using okayms::Symbol;
using okayms_test::Tally;

std::vector<uint8_t> unhex(const char* text) {
    std::vector<uint8_t> out;
    int high = -1;
    for (const char* p = text; *p; ++p) {
        const char c = *p;
        int value = -1;
        if (c >= '0' && c <= '9') value = c - '0';
        else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
        else continue;
        if (high < 0) high = value;
        else {
            out.push_back(static_cast<uint8_t>((high << 4) | value));
            high = -1;
        }
    }
    return out;
}

Symbol sym(const char* name, uint32_t value, int16_t section, uint16_t type, uint8_t storage) {
    Symbol s;
    s.name = name;
    s.value = value;
    s.section = section;
    s.type = type;
    s.storage = storage;
    return s;
}

// One section ".text", six code bytes, one short symbol "entry".
// Symbol table at byte 66, string table size word only (value 4) at byte 84.
const char kMinimal[] =
    "64 86 01 00 00 00 00 00 42 00 00 00 01 00 00 00 00 00 00 00"
    "2E 74 65 78 74 00 00 00 06 00 00 00 00 00 00 00 06 00 00 00"
    "3C 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 20 00 00 60"
    "B8 2A 00 00 00 C3"
    "65 6E 74 72 79 00 00 00 00 00 00 00 01 00 20 00 02 00"
    "04 00 00 00";

// Two sections, a REL32 relocation, an 8-byte name, a long section name
// and a long symbol name. String "initialized" is at string offset 4.
// "a_much_longer_symbol_name" is at string offset 16. Symbol table at 120.
const char kLong[] =
    "64 86 02 00 00 00 00 00 78 00 00 00 03 00 00 00 00 00 00 00"
    "2E 74 65 78 74 00 00 00 06 00 00 00 00 00 00 00 06 00 00 00"
    "64 00 00 00 6A 00 00 00 00 00 00 00 01 00 00 00 20 00 00 60"
    "2F 34 00 00 00 00 00 00 04 00 00 00 00 10 00 00 04 00 00 00"
    "74 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 40 00 00 C0"
    "E8 00 00 00 00 C3"
    "01 00 00 00 01 00 00 00 04 00"
    "2A 00 00 00"
    "65 6E 74 72 79 00 00 00 00 00 00 00 01 00 20 00 02 00"
    "00 00 00 00 10 00 00 00 00 00 00 00 02 00 00 00 02 00"
    "41 42 43 44 45 46 47 48 05 00 00 00 01 00 00 00 03 00"
    "2A 00 00 00"
    "69 6E 69 74 69 61 6C 69 7A 65 64 00"
    "61 5F 6D 75 63 68 5F 6C 6F 6E 67 65 72 5F 73 79 6D 62 6F 6C 5F 6E 61 6D 65 00";

// No sections. Symbol section number is -1 (absolute), stored as FF FF.
const char kAbsolute[] =
    "64 86 00 00 00 00 00 00 14 00 00 00 01 00 00 00 00 00 00 00"
    "61 62 73 00 00 00 00 00 2A 00 00 00 FF FF 00 00 02 00"
    "04 00 00 00";

// One symbol plus one 18-byte auxiliary record. NumberOfSymbols is 2.
// Relocation symbol index 0 is the symbol, not the auxiliary record.
const char kAux[] =
    "64 86 01 00 00 00 00 00 47 00 00 00 02 00 00 00 00 00 00 00"
    "2E 74 65 78 74 00 00 00 01 00 00 00 00 00 00 00 01 00 00 00"
    "3C 00 00 00 3D 00 00 00 00 00 00 00 01 00 00 00 20 00 00 60"
    "C3"
    "00 00 00 00 00 00 00 00 00 00"
    "66 00 00 00 00 00 00 00 00 00 00 00 01 00 20 00 02 01"
    "A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5"
    "04 00 00 00";

// Long section name and no symbols. PointerToSymbolTable is the string
// table (byte 60) and NumberOfSymbols is zero. VirtualAddress is 0x7B.
const char kLongSectionOnly[] =
    "64 86 01 00 00 00 00 00 3C 00 00 00 00 00 00 00 00 00 00 00"
    "2F 34 00 00 00 00 00 00 00 00 00 00 7B 00 00 00 00 00 00 00"
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 40 00 00 40"
    "10 00 00 00 69 6E 69 74 69 61 6C 69 7A 65 64 00";

void expect_write(Tally& tally, const ObjectFile& object, const char* hex) {
    const std::vector<uint8_t> expect = unhex(hex);
    const Result<std::vector<uint8_t>> written = okayms::write_coff(object);
    CHECK(tally, written.ok());
    if (!written.ok()) {
        std::fprintf(stderr, "  write error: %s\n", written.error.c_str());
        return;
    }
    CHECK(tally, okayms_test::same_bytes(written.value, expect.data(), expect.size()));
}

void test_minimal(Tally& tally) {
    std::printf("test_minimal\n");
    ObjectFile object;
    Section section;
    section.name = ".text";
    section.virtual_size = 6;
    section.data = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
    section.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead;
    object.sections.push_back(section);
    object.symbols.push_back(sym("entry", 0, 1, 0x20, okayms::kClassExternal));
    expect_write(tally, object, kMinimal);

    const std::vector<uint8_t> bytes = unhex(kMinimal);
    const Result<ObjectFile> read = okayms::read_coff(bytes.data(), bytes.size());
    CHECK(tally, read.ok());
    if (!read.ok()) {
        std::fprintf(stderr, "  read error: %s\n", read.error.c_str());
        return;
    }
    CHECK(tally, read.value.machine == okayms::kMachineAmd64);
    CHECK(tally, read.value.sections.size() == 1);
    CHECK(tally, read.value.sections[0].name == ".text");
    CHECK(tally, read.value.sections[0].data.size() == 6);
    CHECK(tally, read.value.sections[0].data[0] == 0xB8);
    CHECK(tally, read.value.sections[0].data[5] == 0xC3);
    CHECK(tally, read.value.sections[0].characteristics == 0x60000020u);
    CHECK(tally, read.value.symbols.size() == 1);
    CHECK(tally, read.value.symbols[0].name == "entry");
    CHECK(tally, read.value.symbols[0].section == 1);
    CHECK(tally, read.value.symbols[0].type == 0x20);
    CHECK(tally, read.value.symbols[0].storage == okayms::kClassExternal);

    const Result<std::vector<uint8_t>> again = okayms::write_coff(read.value);
    CHECK(tally, again.ok());
    if (again.ok()) CHECK(tally, again.value == bytes);
}

void test_long_names(Tally& tally) {
    std::printf("test_long_names\n");
    ObjectFile object;
    Section text;
    text.name = ".text";
    text.virtual_size = 6;
    text.data = {0xE8, 0x00, 0x00, 0x00, 0x00, 0xC3};
    text.characteristics = 0x60000020u;
    Relocation reloc;
    reloc.offset = 1;
    reloc.symbol = 1;
    reloc.type = okayms::kRelAmd64Rel32;
    text.relocations.push_back(reloc);
    object.sections.push_back(text);

    Section data;
    data.name = "initialized";
    data.virtual_size = 4;
    data.virtual_address = 0x1000;
    data.data = {0x2A, 0x00, 0x00, 0x00};
    data.characteristics = okayms::kScnCntInitData | okayms::kScnMemRead | okayms::kScnMemWrite;
    object.sections.push_back(data);

    object.symbols.push_back(sym("entry", 0, 1, 0x20, okayms::kClassExternal));
    object.symbols.push_back(sym("a_much_longer_symbol_name", 0, 2, 0, okayms::kClassExternal));
    object.symbols.push_back(sym("ABCDEFGH", 5, 1, 0, okayms::kClassStatic));
    expect_write(tally, object, kLong);

    const std::vector<uint8_t> bytes = unhex(kLong);
    const Result<ObjectFile> read = okayms::read_coff(bytes.data(), bytes.size());
    CHECK(tally, read.ok());
    if (!read.ok()) return;
    CHECK(tally, read.value.sections.size() == 2);
    CHECK(tally, read.value.sections[1].name == "initialized");
    CHECK(tally, read.value.sections[1].virtual_address == 0x1000u);
    CHECK(tally, read.value.sections[0].relocations.size() == 1);
    CHECK(tally, read.value.sections[0].relocations[0].offset == 1u);
    CHECK(tally, read.value.sections[0].relocations[0].symbol == 1u);
    CHECK(tally, read.value.sections[0].relocations[0].type == okayms::kRelAmd64Rel32);
    CHECK(tally, read.value.symbols[1].name == "a_much_longer_symbol_name");
    CHECK(tally, read.value.symbols[2].name == "ABCDEFGH");
    CHECK(tally, read.value.symbols[2].value == 5u);
    CHECK(tally, read.value.symbols[2].storage == okayms::kClassStatic);
    // The 8-byte name occupies the whole name field, with no extra zero.
    CHECK(tally, bytes[120 + 36] == 'A');
    CHECK(tally, bytes[120 + 36 + 7] == 'H');

    const Result<std::vector<uint8_t>> again = okayms::write_coff(read.value);
    CHECK(tally, again.ok() && again.value == bytes);
}

void test_absolute(Tally& tally) {
    std::printf("test_absolute\n");
    ObjectFile object;
    object.symbols.push_back(sym("abs", 42, okayms::kSectionAbsolute, 0, okayms::kClassExternal));
    expect_write(tally, object, kAbsolute);
    const std::vector<uint8_t> bytes = unhex(kAbsolute);
    const Result<ObjectFile> read = okayms::read_coff(bytes.data(), bytes.size());
    CHECK(tally, read.ok());
    if (!read.ok()) return;
    CHECK(tally, read.value.sections.empty());
    CHECK(tally, read.value.symbols[0].section == okayms::kSectionAbsolute);
    CHECK(tally, read.value.symbols[0].value == 42u);
    const Result<std::vector<uint8_t>> again = okayms::write_coff(read.value);
    CHECK(tally, again.ok() && again.value == bytes);
}

void test_aux(Tally& tally) {
    std::printf("test_aux\n");
    ObjectFile object;
    Section section;
    section.name = ".text";
    section.virtual_size = 1;
    section.data = {0xC3};
    section.characteristics = 0x60000020u;
    Relocation reloc;
    reloc.offset = 0;
    reloc.symbol = 0;
    reloc.type = okayms::kRelAmd64Absolute;
    section.relocations.push_back(reloc);
    object.sections.push_back(section);
    Symbol symbol = sym("f", 0, 1, 0x20, okayms::kClassExternal);
    symbol.aux.assign(18, 0xA5);
    object.symbols.push_back(symbol);
    expect_write(tally, object, kAux);

    const std::vector<uint8_t> bytes = unhex(kAux);
    const Result<ObjectFile> read = okayms::read_coff(bytes.data(), bytes.size());
    CHECK(tally, read.ok());
    if (!read.ok()) return;
    CHECK(tally, read.value.symbols.size() == 1);
    CHECK(tally, read.value.symbols[0].aux.size() == 18u);
    CHECK(tally, read.value.symbols[0].aux[0] == 0xA5);
    CHECK(tally, read.value.symbols[0].aux[17] == 0xA5);
    const Result<std::vector<uint8_t>> again = okayms::write_coff(read.value);
    CHECK(tally, again.ok() && again.value == bytes);
}

void test_long_section_only(Tally& tally) {
    std::printf("test_long_section_only\n");
    ObjectFile object;
    Section section;
    section.name = "initialized";
    section.virtual_address = 0x7B;
    section.characteristics = okayms::kScnCntInitData | okayms::kScnMemRead;
    object.sections.push_back(section);
    expect_write(tally, object, kLongSectionOnly);
    const std::vector<uint8_t> bytes = unhex(kLongSectionOnly);
    const Result<ObjectFile> read = okayms::read_coff(bytes.data(), bytes.size());
    CHECK(tally, read.ok());
    if (!read.ok()) return;
    CHECK(tally, read.value.symbols.empty());
    CHECK(tally, read.value.sections[0].name == "initialized");
    CHECK(tally, read.value.sections[0].virtual_address == 0x7Bu);
    const Result<std::vector<uint8_t>> again = okayms::write_coff(read.value);
    CHECK(tally, again.ok() && again.value == bytes);
}

void test_round_trip_more(Tally& tally) {
    std::printf("test_round_trip_more\n");
    ObjectFile object;
    object.characteristics = 0x0004;
    object.time_date_stamp = 0x11223344;
    Section section;
    section.name = "12345678";
    section.virtual_size = 2;
    section.data = {0x90, 0x90};
    section.characteristics = okayms::kScnAlign16 | okayms::kScnCntCode;
    object.sections.push_back(section);
    object.symbols.push_back(sym("name", 0, okayms::kSectionDebug, 0, okayms::kClassFile));
    object.symbols.push_back(sym("name_extra", 1, 1, 0, okayms::kClassLabel));
    object.symbols.push_back(sym("shared_long_name", 2, 1, 0, okayms::kClassExternal));
    object.symbols.push_back(sym("shared_long_name", 3, okayms::kSectionUndefined, 0, okayms::kClassExternal));

    const Result<std::vector<uint8_t>> first = okayms::write_coff(object);
    CHECK(tally, first.ok());
    if (!first.ok()) {
        std::fprintf(stderr, "  write error: %s\n", first.error.c_str());
        return;
    }
    // File-header characteristics and timestamp sit at the documented offsets.
    CHECK(tally, first.value.size() > 20);
    CHECK(tally, first.value[4] == 0x44 && first.value[5] == 0x33 && first.value[6] == 0x22 && first.value[7] == 0x11);
    CHECK(tally, first.value[18] == 0x04 && first.value[19] == 0x00);
    // Eight-character section name, no terminating zero inside the field.
    CHECK(tally, std::memcmp(first.value.data() + 20, "12345678", 8) == 0);

    // The shared long name is stored once.
    const std::string needle = "shared_long_name";
    int found = 0;
    for (size_t i = 0; i + needle.size() < first.value.size(); ++i) {
        if (std::memcmp(first.value.data() + i, needle.data(), needle.size()) == 0 &&
            first.value[i + needle.size()] == 0) {
            ++found;
        }
    }
    CHECK(tally, found == 1);

    // Debug section number -2 is stored as FE FF.
    bool saw_debug = false;
    for (size_t i = 0; i + 1 < first.value.size(); ++i) {
        if (first.value[i] == 0xFE && first.value[i + 1] == 0xFF) saw_debug = true;
    }
    CHECK(tally, saw_debug);

    const Result<ObjectFile> read = okayms::read_coff(first.value.data(), first.value.size());
    CHECK(tally, read.ok());
    if (!read.ok()) return;
    CHECK(tally, read.value.time_date_stamp == 0x11223344u);
    CHECK(tally, read.value.characteristics == 0x0004);
    CHECK(tally, read.value.sections[0].name == "12345678");
    CHECK(tally, read.value.symbols.size() == 4);
    CHECK(tally, read.value.symbols[0].section == okayms::kSectionDebug);
    CHECK(tally, read.value.symbols[0].storage == okayms::kClassFile);
    CHECK(tally, read.value.symbols[1].name == "name_extra");
    CHECK(tally, read.value.symbols[1].storage == okayms::kClassLabel);
    CHECK(tally, read.value.symbols[2].name == "shared_long_name");
    CHECK(tally, read.value.symbols[3].name == "shared_long_name");
    CHECK(tally, read.value.symbols[3].section == okayms::kSectionUndefined);
    CHECK(tally, read.value.symbols[2].value == 2u && read.value.symbols[3].value == 3u);

    const Result<std::vector<uint8_t>> second = okayms::write_coff(read.value);
    CHECK(tally, second.ok() && second.value == first.value);
}

void test_empty(Tally& tally) {
    std::printf("test_empty\n");
    const char* hex =
        "64 86 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00";
    expect_write(tally, ObjectFile{}, hex);
    const std::vector<uint8_t> bytes = unhex(hex);
    const Result<ObjectFile> read = okayms::read_coff(bytes.data(), bytes.size());
    CHECK(tally, read.ok());
    if (!read.ok()) return;
    CHECK(tally, read.value.sections.empty());
    CHECK(tally, read.value.symbols.empty());
    CHECK(tally, read.value.machine == okayms::kMachineAmd64);
}

bool fails_with(const std::string& error, const char* needle) {
    return !error.empty() && error.find(needle) != std::string::npos;
}

void test_rejects(Tally& tally) {
    std::printf("test_rejects\n");
    const uint8_t tiny[] = {0x64, 0x86, 0x00, 0x00};
    const Result<ObjectFile> short_file = okayms::read_coff(tiny, sizeof tiny);
    CHECK(tally, fails_with(short_file.error, "truncated"));

    // One section claimed, but the section header is not in the file.
    const uint8_t header_only[] = {
        0x64, 0x86, 0x01, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const Result<ObjectFile> missing_section = okayms::read_coff(header_only, sizeof header_only);
    CHECK(tally, fails_with(missing_section.error, "truncated"));

    // A huge symbol count must be rejected from the size, not allocated.
    uint8_t huge[20] = {0x64, 0x86, 0, 0, 0, 0, 0, 0, 20, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0x00, 0, 0, 0, 0};
    const Result<ObjectFile> too_many = okayms::read_coff(huge, sizeof huge);
    CHECK(tally, fails_with(too_many.error, "symbol"));

    std::vector<uint8_t> bad_string = unhex(kMinimal);
    // Turn "entry" into a string-table reference at offset 99. The table is 4 bytes.
    if (bad_string.size() >= 74) {
        for (int i = 0; i < 8; ++i) bad_string[66 + i] = 0;
        bad_string[70] = 99;
    }
    const Result<ObjectFile> bad_off = okayms::read_coff(bad_string.data(), bad_string.size());
    CHECK(tally, fails_with(bad_off.error, "string"));

    // String table claims 8 bytes and the payload has no zero terminator.
    std::vector<uint8_t> no_nul = unhex(kAbsolute);
    no_nul.resize(38 + 8, 0);
    no_nul[38] = 8;
    no_nul[39] = 0;
    no_nul[40] = 0;
    no_nul[41] = 0;
    no_nul[42] = 'a';
    no_nul[43] = 'b';
    no_nul[44] = 'c';
    no_nul[45] = 'd';
    // Point the symbol at string offset 4.
    for (int i = 0; i < 8; ++i) no_nul[20 + i] = 0;
    no_nul[24] = 4;
    const Result<ObjectFile> missing_nul = okayms::read_coff(no_nul.data(), no_nul.size());
    CHECK(tally, fails_with(missing_nul.error, "terminator"));

    std::vector<uint8_t> aux_reloc = unhex(kAux);
    // Relocation's symbol index is at file offset 65. Index 1 is the auxiliary record.
    if (aux_reloc.size() > 65) aux_reloc[65] = 1;
    const Result<ObjectFile> at_aux = okayms::read_coff(aux_reloc.data(), aux_reloc.size());
    CHECK(tally, fails_with(at_aux.error, "auxiliary"));

    ObjectFile bad_name;
    bad_name.symbols.push_back(sym("has\0nul", 0, 0, 0, okayms::kClassExternal));
    // The C string above stops at the embedded NUL, so build it explicitly.
    bad_name.symbols[0].name = std::string("has\0nul", 7);
    const Result<std::vector<uint8_t>> wrote_nul = okayms::write_coff(bad_name);
    CHECK(tally, fails_with(wrote_nul.error, "name"));

    ObjectFile bad_aux;
    bad_aux.symbols.push_back(sym("f", 0, 1, 0, okayms::kClassExternal));
    bad_aux.symbols[0].aux.assign(3, 1);
    const Result<std::vector<uint8_t>> wrote_aux = okayms::write_coff(bad_aux);
    CHECK(tally, fails_with(wrote_aux.error, "auxiliary"));

    ObjectFile bad_reloc;
    Section section;
    section.name = ".text";
    section.data = {0xC3};
    Relocation reloc;
    reloc.symbol = 5;
    section.relocations.push_back(reloc);
    bad_reloc.sections.push_back(section);
    const Result<std::vector<uint8_t>> wrote_reloc = okayms::write_coff(bad_reloc);
    CHECK(tally, fails_with(wrote_reloc.error, "symbol"));
}

}  // namespace

int main() {
    Tally tally;
    test_minimal(tally);
    test_long_names(tally);
    test_absolute(tally);
    test_aux(tally);
    test_long_section_only(tally);
    test_round_trip_more(tally);
    test_empty(tally);
    test_rejects(tally);
    return tally.finish(8);
}
