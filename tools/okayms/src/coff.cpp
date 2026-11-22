#include "okayms/coff.hpp"

#include <cstring>

namespace okayms {
namespace {

constexpr uint32_t kMaxSections = 96;
constexpr uint32_t kMaxSymbolRecords = 100000;
constexpr uint32_t kMaxSectionBytes = 32u * 1024u * 1024u;
constexpr uint32_t kMaxStringBytes = 16u * 1024u * 1024u;

void append_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 24));
}

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool range_ok(size_t offset, size_t length, size_t file_size) {
    return length <= file_size && offset <= file_size - length;
}

bool has_embedded_nul(const std::string& name) {
    return name.find('\0') != std::string::npos;
}

// The string table's first four bytes are its size, so the first real
// string starts at offset 4. Identical names share one entry.
class StringTable {
public:
    uint32_t intern(const std::string& text) {
        size_t at = 0;
        while (at < bytes_.size()) {
            const size_t end = bytes_.find('\0', at);
            if (end == std::string::npos) break;
            if (bytes_.compare(at, end - at, text) == 0) {
                return static_cast<uint32_t>(4 + at);
            }
            at = end + 1;
        }
        const uint32_t offset = static_cast<uint32_t>(4 + bytes_.size());
        bytes_ += text;
        bytes_.push_back('\0');
        return offset;
    }

    const std::string& bytes() const { return bytes_; }

private:
    std::string bytes_;
};

std::string read_cstring(const uint8_t* table, uint32_t table_size, uint32_t offset, std::string* error) {
    if (offset == 0) return "";
    if (offset < 4 || offset >= table_size) {
        *error = "string table offset is outside the string table";
        return "";
    }
    uint32_t end = offset;
    while (end < table_size && table[end] != 0) ++end;
    if (end >= table_size) {
        *error = "string table entry is missing a terminator";
        return "";
    }
    return std::string(reinterpret_cast<const char*>(table + offset), end - offset);
}

}  // namespace

Result<std::vector<uint8_t>> write_coff(const ObjectFile& object) {
    if (object.sections.size() > kMaxSections) {
        return Result<std::vector<uint8_t>>::fail("too many sections");
    }
    for (const Section& section : object.sections) {
        if (has_embedded_nul(section.name)) {
            return Result<std::vector<uint8_t>>::fail("section name contains an embedded NUL");
        }
        if (section.data.size() > kMaxSectionBytes) {
            return Result<std::vector<uint8_t>>::fail("section is too large");
        }
        if (section.relocations.size() > 65535u) {
            return Result<std::vector<uint8_t>>::fail("too many relocations in one section");
        }
        for (const Relocation& reloc : section.relocations) {
            if (reloc.symbol >= object.symbols.size()) {
                return Result<std::vector<uint8_t>>::fail(
                    "relocation symbol index is outside the symbol table");
            }
        }
    }

    uint32_t raw_symbols = 0;
    for (const Symbol& symbol : object.symbols) {
        if (has_embedded_nul(symbol.name)) {
            return Result<std::vector<uint8_t>>::fail("symbol name contains an embedded NUL");
        }
        if (symbol.aux.size() % 18u != 0u) {
            return Result<std::vector<uint8_t>>::fail(
                "auxiliary record length must be a multiple of 18");
        }
        const size_t aux_records = symbol.aux.size() / 18u;
        if (aux_records > 255u) {
            return Result<std::vector<uint8_t>>::fail("too many auxiliary records on one symbol");
        }
        raw_symbols += 1u + static_cast<uint32_t>(aux_records);
    }
    if (raw_symbols > kMaxSymbolRecords) {
        return Result<std::vector<uint8_t>>::fail("too many symbols");
    }

    StringTable strings;
    std::vector<uint32_t> section_string_at(object.sections.size(), 0);
    for (size_t i = 0; i < object.sections.size(); ++i) {
        if (object.sections[i].name.size() > 8) {
            section_string_at[i] = strings.intern(object.sections[i].name);
        }
    }
    std::vector<uint32_t> symbol_string_at(object.symbols.size(), 0);
    for (size_t i = 0; i < object.symbols.size(); ++i) {
        if (object.symbols[i].name.size() > 8) {
            symbol_string_at[i] = strings.intern(object.symbols[i].name);
        }
    }
    if (strings.bytes().size() > kMaxStringBytes) {
        return Result<std::vector<uint8_t>>::fail("string table is too large");
    }

    const bool emit_strings = raw_symbols > 0 || !strings.bytes().empty();
    std::vector<uint32_t> raw_pointer(object.sections.size(), 0);
    std::vector<uint32_t> reloc_pointer(object.sections.size(), 0);
    uint32_t cursor = 20u + 40u * static_cast<uint32_t>(object.sections.size());
    for (size_t i = 0; i < object.sections.size(); ++i) {
        const Section& section = object.sections[i];
        if (!section.data.empty()) {
            raw_pointer[i] = cursor;
            cursor += static_cast<uint32_t>(section.data.size());
        }
        if (!section.relocations.empty()) {
            reloc_pointer[i] = cursor;
            cursor += 10u * static_cast<uint32_t>(section.relocations.size());
        }
    }
    const uint32_t symbol_pointer = emit_strings ? cursor : 0;

    std::vector<uint32_t> logical_to_raw(object.symbols.size(), 0);
    uint32_t raw_index = 0;
    for (size_t i = 0; i < object.symbols.size(); ++i) {
        logical_to_raw[i] = raw_index;
        raw_index += 1u + static_cast<uint32_t>(object.symbols[i].aux.size() / 18u);
    }

    std::vector<uint8_t> out;
    out.reserve(cursor + raw_symbols * 18u + 4u + strings.bytes().size());
    append_u16(out, object.machine);
    append_u16(out, static_cast<uint16_t>(object.sections.size()));
    append_u32(out, object.time_date_stamp);
    append_u32(out, symbol_pointer);
    append_u32(out, raw_symbols);
    append_u16(out, 0);
    append_u16(out, object.characteristics);

    for (size_t i = 0; i < object.sections.size(); ++i) {
        const Section& section = object.sections[i];
        uint8_t name[8] = {};
        if (section.name.size() <= 8) {
            std::memcpy(name, section.name.data(), section.name.size());
        } else {
            const std::string slash = "/" + std::to_string(section_string_at[i]);
            if (slash.size() > 8) {
                return Result<std::vector<uint8_t>>::fail("section name offset does not fit");
            }
            std::memcpy(name, slash.data(), slash.size());
        }
        out.insert(out.end(), name, name + 8);
        append_u32(out, section.virtual_size);
        append_u32(out, section.virtual_address);
        append_u32(out, static_cast<uint32_t>(section.data.size()));
        append_u32(out, raw_pointer[i]);
        append_u32(out, reloc_pointer[i]);
        append_u32(out, 0);
        append_u16(out, static_cast<uint16_t>(section.relocations.size()));
        append_u16(out, 0);
        append_u32(out, section.characteristics);
    }

    for (size_t i = 0; i < object.sections.size(); ++i) {
        const Section& section = object.sections[i];
        out.insert(out.end(), section.data.begin(), section.data.end());
        for (const Relocation& reloc : section.relocations) {
            append_u32(out, reloc.offset);
            append_u32(out, logical_to_raw[reloc.symbol]);
            append_u16(out, reloc.type);
        }
    }

    for (size_t i = 0; i < object.symbols.size(); ++i) {
        const Symbol& symbol = object.symbols[i];
        if (symbol.name.size() <= 8) {
            uint8_t name[8] = {};
            std::memcpy(name, symbol.name.data(), symbol.name.size());
            out.insert(out.end(), name, name + 8);
        } else {
            append_u32(out, 0);
            append_u32(out, symbol_string_at[i]);
        }
        append_u32(out, symbol.value);
        append_u16(out, static_cast<uint16_t>(symbol.section));
        append_u16(out, symbol.type);
        out.push_back(symbol.storage);
        out.push_back(static_cast<uint8_t>(symbol.aux.size() / 18u));
        out.insert(out.end(), symbol.aux.begin(), symbol.aux.end());
    }

    if (emit_strings) {
        append_u32(out, 4u + static_cast<uint32_t>(strings.bytes().size()));
        out.insert(out.end(), strings.bytes().begin(), strings.bytes().end());
    }
    return Result<std::vector<uint8_t>>::success(std::move(out));
}

Result<ObjectFile> read_coff(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 20) {
        return Result<ObjectFile>::fail("truncated COFF header");
    }
    ObjectFile object;
    object.machine = read_u16(data + 0);
    const uint16_t section_count = read_u16(data + 2);
    object.time_date_stamp = read_u32(data + 4);
    const uint32_t symbol_pointer = read_u32(data + 8);
    const uint32_t raw_symbol_count = read_u32(data + 12);
    const uint16_t optional_size = read_u16(data + 16);
    object.characteristics = read_u16(data + 18);
    if (optional_size != 0) {
        return Result<ObjectFile>::fail("COFF optional header is not supported in an object");
    }
    if (section_count > kMaxSections) {
        return Result<ObjectFile>::fail("too many sections");
    }
    const size_t headers = 20u + static_cast<size_t>(section_count) * 40u;
    if (!range_ok(0, headers, size)) {
        return Result<ObjectFile>::fail("truncated section headers");
    }
    if (raw_symbol_count > kMaxSymbolRecords) {
        return Result<ObjectFile>::fail("too many symbols");
    }

    struct RawSection {
        std::string name_field;
        uint32_t virtual_size;
        uint32_t virtual_address;
        uint32_t raw_size;
        uint32_t raw_pointer;
        uint32_t reloc_pointer;
        uint16_t reloc_count;
        uint32_t characteristics;
        bool long_name;
        uint32_t name_offset;
    };
    std::vector<RawSection> raw_sections(section_count);
    for (uint16_t i = 0; i < section_count; ++i) {
        const uint8_t* header = data + 20 + static_cast<size_t>(i) * 40;
        RawSection& section = raw_sections[i];
        section.virtual_size = read_u32(header + 8);
        section.virtual_address = read_u32(header + 12);
        section.raw_size = read_u32(header + 16);
        section.raw_pointer = read_u32(header + 20);
        section.reloc_pointer = read_u32(header + 24);
        section.reloc_count = read_u16(header + 32);
        section.characteristics = read_u32(header + 36);
        if (header[0] == '/') {
            if (header[1] < '0' || header[1] > '9') {
                return Result<ObjectFile>::fail("section name is not a string-table offset");
            }
            uint32_t offset = 0;
            for (int c = 1; c < 8 && header[c] != 0; ++c) {
                if (header[c] < '0' || header[c] > '9') {
                    return Result<ObjectFile>::fail("section name is not a string-table offset");
                }
                offset = offset * 10u + static_cast<uint32_t>(header[c] - '0');
            }
            section.long_name = true;
            section.name_offset = offset;
        } else {
            size_t length = 0;
            while (length < 8 && header[length] != 0) ++length;
            section.name_field.assign(reinterpret_cast<const char*>(header), length);
        }
        if (section.raw_size > kMaxSectionBytes) {
            return Result<ObjectFile>::fail("section is too large");
        }
        if (section.raw_size != 0 && !range_ok(section.raw_pointer, section.raw_size, size)) {
            return Result<ObjectFile>::fail("truncated section data");
        }
        const size_t reloc_bytes = static_cast<size_t>(section.reloc_count) * 10u;
        if (section.reloc_count != 0 && !range_ok(section.reloc_pointer, reloc_bytes, size)) {
            return Result<ObjectFile>::fail("truncated relocation table");
        }
    }

    const uint8_t* string_table = nullptr;
    uint32_t string_size = 0;
    if (symbol_pointer != 0 || raw_symbol_count != 0) {
        if (symbol_pointer == 0) {
            return Result<ObjectFile>::fail("symbol table pointer is missing");
        }
        const size_t symbol_bytes = static_cast<size_t>(raw_symbol_count) * 18u;
        if (symbol_bytes / 18u != raw_symbol_count || !range_ok(symbol_pointer, symbol_bytes, size)) {
            return Result<ObjectFile>::fail("symbol table does not fit in the file");
        }
        const size_t string_at = symbol_pointer + symbol_bytes;
        if (!range_ok(string_at, 4, size)) {
            return Result<ObjectFile>::fail("truncated string table");
        }
        string_size = read_u32(data + string_at);
        if (string_size < 4 || string_size > kMaxStringBytes || !range_ok(string_at, string_size, size)) {
            return Result<ObjectFile>::fail("string table does not fit in the file");
        }
        string_table = data + string_at;
    }

    object.sections.resize(section_count);
    for (uint16_t i = 0; i < section_count; ++i) {
        const RawSection& raw = raw_sections[i];
        Section& section = object.sections[i];
        if (raw.long_name) {
            if (string_table == nullptr) {
                return Result<ObjectFile>::fail("section name needs a string table");
            }
            std::string error;
            section.name = read_cstring(string_table, string_size, raw.name_offset, &error);
            if (!error.empty()) return Result<ObjectFile>::fail(error);
        } else {
            section.name = raw.name_field;
        }
        section.virtual_size = raw.virtual_size;
        section.virtual_address = raw.virtual_address;
        section.characteristics = raw.characteristics;
        if (raw.raw_size != 0) {
            section.data.assign(data + raw.raw_pointer, data + raw.raw_pointer + raw.raw_size);
        }
        section.relocations.resize(raw.reloc_count);
        for (uint16_t r = 0; r < raw.reloc_count; ++r) {
            const uint8_t* rec = data + raw.reloc_pointer + static_cast<size_t>(r) * 10u;
            section.relocations[r].offset = read_u32(rec);
            section.relocations[r].symbol = read_u32(rec + 4);
            section.relocations[r].type = read_u16(rec + 8);
        }
    }

    std::vector<int> raw_to_logical(raw_symbol_count, -1);
    if (raw_symbol_count != 0) {
        size_t raw = 0;
        while (raw < raw_symbol_count) {
            const uint8_t* rec = data + symbol_pointer + raw * 18u;
            const uint8_t aux_count = rec[17];
            if (raw + 1u + aux_count > raw_symbol_count) {
                return Result<ObjectFile>::fail("auxiliary records run past the symbol table");
            }
            Symbol symbol;
            const uint32_t prefix = read_u32(rec);
            if (prefix == 0) {
                if (string_table == nullptr) {
                    return Result<ObjectFile>::fail("symbol name needs a string table");
                }
                std::string error;
                symbol.name = read_cstring(string_table, string_size, read_u32(rec + 4), &error);
                if (!error.empty()) return Result<ObjectFile>::fail(error);
            } else {
                size_t length = 0;
                while (length < 8 && rec[length] != 0) ++length;
                symbol.name.assign(reinterpret_cast<const char*>(rec), length);
            }
            symbol.value = read_u32(rec + 8);
            symbol.section = static_cast<int16_t>(read_u16(rec + 12));
            symbol.type = read_u16(rec + 14);
            symbol.storage = rec[16];
            if (aux_count != 0) {
                symbol.aux.assign(rec + 18, rec + 18 + static_cast<size_t>(aux_count) * 18u);
            }
            raw_to_logical[raw] = static_cast<int>(object.symbols.size());
            object.symbols.push_back(std::move(symbol));
            raw += 1u + aux_count;
        }
    }

    for (Section& section : object.sections) {
        for (Relocation& reloc : section.relocations) {
            if (reloc.symbol >= raw_to_logical.size()) {
                return Result<ObjectFile>::fail("relocation symbol index is outside the symbol table");
            }
            if (raw_to_logical[reloc.symbol] < 0) {
                return Result<ObjectFile>::fail("relocation targets an auxiliary record");
            }
            reloc.symbol = static_cast<uint32_t>(raw_to_logical[reloc.symbol]);
        }
    }
    return Result<ObjectFile>::success(std::move(object));
}

}  // namespace okayms
