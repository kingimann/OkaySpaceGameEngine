#include "okayms/archive.hpp"

#include <algorithm>
#include <cstring>

namespace okayms {
namespace {

void append_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}
void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void append_member(std::vector<uint8_t>& out, const std::string& name_field, const std::vector<uint8_t>& payload) {
    std::string header(60, ' ');
    const size_t n = name_field.size() < 16 ? name_field.size() : 16;
    std::memcpy(header.data(), name_field.data(), n);
    const std::string size = std::to_string(payload.size());
    std::memcpy(header.data() + 48, size.data(), size.size());
    header[58] = '`';
    header[59] = '\n';
    out.insert(out.end(), header.begin(), header.end());
    out.insert(out.end(), payload.begin(), payload.end());
    if ((payload.size() & 1u) != 0) out.push_back('\n');
}

std::string name_field_for(const std::string& text) {
    std::string field(16, ' ');
    std::memcpy(field.data(), text.data(), text.size() < 16 ? text.size() : 16);
    return field;
}

bool range_ok(size_t offset, size_t length, size_t file_size) {
    return length <= file_size && offset <= file_size - length;
}

}  // namespace

Result<std::vector<uint8_t>> write_archive(const Archive& archive) {
    std::string longnames;
    std::vector<uint32_t> long_at(archive.members.size(), 0);
    std::vector<char> is_long(archive.members.size(), 0);
    for (size_t i = 0; i < archive.members.size(); ++i) {
        const std::string& name = archive.members[i].name;
        if (name.empty() || name.find('\0') != std::string::npos || name.find('/') != std::string::npos) {
            return Result<std::vector<uint8_t>>::fail("archive member name is empty or contains '/'");
        }
        if (name.size() + 1 > 16) {
            is_long[i] = 1;
            long_at[i] = static_cast<uint32_t>(longnames.size());
            longnames += name;
            longnames.push_back('\0');
        }
    }

    // Each member's bytes first: the linker members need the file offset of every member header.
    std::vector<std::vector<uint8_t>> payloads(archive.members.size());
    for (size_t i = 0; i < archive.members.size(); ++i) {
        const ArchiveMember& member = archive.members[i];
        std::vector<uint8_t>& payload = payloads[i];
        if (member.is_import) {
            if (member.import.symbol.empty() || member.import.dll.empty()) {
                return Result<std::vector<uint8_t>>::fail("import member is missing a symbol or a dll");
            }
            const uint32_t size_of_data = static_cast<uint32_t>(member.import.symbol.size() + 1 + member.import.dll.size() + 1);
            append_u16(payload, 0);
            append_u16(payload, 0xFFFF);
            append_u16(payload, 0);
            append_u16(payload, member.import.machine);
            append_u32(payload, 0);
            append_u32(payload, size_of_data);
            append_u16(payload, member.import.hint_or_ordinal);
            const uint16_t type = static_cast<uint16_t>((member.import.name_type << 2) | (member.import.import_type & 3u));
            append_u16(payload, type);
            payload.insert(payload.end(), member.import.symbol.begin(), member.import.symbol.end());
            payload.push_back(0);
            payload.insert(payload.end(), member.import.dll.begin(), member.import.dll.end());
            payload.push_back(0);
        } else {
            const auto coff = write_coff(member.object);
            if (!coff.ok()) return Result<std::vector<uint8_t>>::fail(coff.error);
            payload = coff.value;
        }
    }
    auto padded = [](size_t n) { return n + (n & 1u); };

    // The linker members ("PE Format", "Archive (Library) File Format"): the first lists the symbols in member order
    // with big-endian counts and offsets; the second lists the members' offsets once and the symbols sorted by name,
    // each with the 1-based number of its member, little-endian.
    std::vector<uint8_t> first;
    std::vector<uint8_t> second;
    if (archive.symbol_index) {
        std::vector<std::pair<std::string, uint32_t>> syms;  // (name, member index)
        for (size_t i = 0; i < archive.members.size(); ++i)
            for (const std::string& name : member_symbols(archive.members[i])) syms.push_back({name, static_cast<uint32_t>(i)});
        size_t names_size = 0;
        for (const auto& sym : syms) names_size += sym.first.size() + 1;
        const size_t first_size = 4 + 4 * syms.size() + names_size;
        const size_t second_size = 4 + 4 * archive.members.size() + 4 + 2 * syms.size() + names_size;
        size_t offset = 8 + 60 + padded(first_size) + 60 + padded(second_size);
        if (!longnames.empty()) offset += 60 + padded(longnames.size());
        std::vector<uint32_t> member_offset(archive.members.size());
        for (size_t i = 0; i < archive.members.size(); ++i) {
            member_offset[i] = static_cast<uint32_t>(offset);
            offset += 60 + padded(payloads[i].size());
        }
        if (offset > 0xFFFFFFFFu) return Result<std::vector<uint8_t>>::fail("archive is larger than 4 GiB");
        auto append_be32 = [](std::vector<uint8_t>& out, uint32_t v) {
            for (int b = 3; b >= 0; --b) out.push_back(static_cast<uint8_t>(v >> (8 * b)));
        };
        append_be32(first, static_cast<uint32_t>(syms.size()));
        for (const auto& sym : syms) append_be32(first, member_offset[sym.second]);
        for (const auto& sym : syms) {
            first.insert(first.end(), sym.first.begin(), sym.first.end());
            first.push_back(0);
        }
        std::vector<std::pair<std::string, uint32_t>> sorted = syms;
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const std::pair<std::string, uint32_t>& a, const std::pair<std::string, uint32_t>& b) {
                             return a.first < b.first;
                         });
        append_u32(second, static_cast<uint32_t>(archive.members.size()));
        for (uint32_t off : member_offset) append_u32(second, off);
        append_u32(second, static_cast<uint32_t>(sorted.size()));
        for (const auto& sym : sorted) {
            if (sym.second + 1 > 0xFFFFu) return Result<std::vector<uint8_t>>::fail("archive has more than 65535 members");
            append_u16(second, static_cast<uint16_t>(sym.second + 1));
        }
        for (const auto& sym : sorted) {
            second.insert(second.end(), sym.first.begin(), sym.first.end());
            second.push_back(0);
        }
    }

    std::vector<uint8_t> out;
    const char sig[] = "!<arch>\n";
    out.insert(out.end(), sig, sig + 8);
    if (archive.symbol_index) {
        append_member(out, name_field_for("/"), first);
        append_member(out, name_field_for("/"), second);
    }
    if (!longnames.empty()) {
        std::vector<uint8_t> payload(longnames.begin(), longnames.end());
        append_member(out, name_field_for("//"), payload);
    }
    for (size_t i = 0; i < archive.members.size(); ++i) {
        std::string field;
        if (is_long[i]) field = name_field_for("/" + std::to_string(long_at[i]));
        else field = name_field_for(archive.members[i].name + "/");
        append_member(out, field, payloads[i]);
    }
    return Result<std::vector<uint8_t>>::success(std::move(out));
}

std::vector<std::string> member_symbols(const ArchiveMember& member) {
    std::vector<std::string> names;
    if (member.is_import) {
        names.push_back("__imp_" + member.import.symbol);
        if (member.import.import_type == kImportCode) names.push_back(member.import.symbol);
        return names;
    }
    for (const Symbol& symbol : member.object.symbols)
        if (symbol.storage == kClassExternal && (symbol.section > 0 || symbol.section == kSectionAbsolute))
            names.push_back(symbol.name);
    return names;
}

Result<Archive> read_archive(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 8) return Result<Archive>::fail("truncated archive");
    if (std::memcmp(data, "!<arch>\n", 8) != 0) return Result<Archive>::fail("not an archive");
    Archive archive;
    std::string longnames;
    size_t at = 8;
    while (at < size) {
        if (!range_ok(at, 60, size)) return Result<Archive>::fail("truncated archive header");
        const uint8_t* header = data + at;
        if (header[58] != '`' || header[59] != '\n') return Result<Archive>::fail("archive header is not terminated");
        size_t payload_size = 0;
        bool any_digit = false;
        for (int i = 0; i < 10; ++i) {
            const char c = static_cast<char>(header[48 + i]);
            if (c == ' ') continue;
            if (c < '0' || c > '9') return Result<Archive>::fail("archive size is not a number");
            any_digit = true;
            payload_size = payload_size * 10u + static_cast<size_t>(c - '0');
        }
        if (!any_digit) return Result<Archive>::fail("archive size is not a number");
        at += 60;
        if (!range_ok(at, payload_size, size)) return Result<Archive>::fail("truncated archive member");
        const uint8_t* payload = data + at;
        at += payload_size;
        if ((payload_size & 1u) != 0 && at < size) ++at;

        const bool longnames_member = header[0] == '/' && header[1] == '/';
        const bool linker_member = header[0] == '/' && header[1] == ' ';
        if (longnames_member) {
            longnames.assign(reinterpret_cast<const char*>(payload), payload_size);
            continue;
        }
        if (linker_member) continue;

        ArchiveMember member;
        if (header[0] == '/') {
            uint32_t offset = 0;
            bool digit = false;
            for (int i = 1; i < 16 && header[i] != ' '; ++i) {
                if (header[i] < '0' || header[i] > '9') return Result<Archive>::fail("archive member name is not a long-name offset");
                digit = true;
                offset = offset * 10u + static_cast<uint32_t>(header[i] - '0');
            }
            if (!digit || offset >= longnames.size()) return Result<Archive>::fail("long archive name is outside the string table");
            size_t end = offset;
            while (end < longnames.size() && longnames[end] != '\0') ++end;
            if (end >= longnames.size()) return Result<Archive>::fail("long archive name is missing a terminator");
            member.name = longnames.substr(offset, end - offset);
        } else {
            size_t end = 0;
            while (end < 16 && header[end] != '/' && header[end] != ' ') ++end;
            member.name.assign(reinterpret_cast<const char*>(header), end);
        }

        const bool import = payload_size >= 4 && payload[0] == 0 && payload[1] == 0 && payload[2] == 0xFF && payload[3] == 0xFF;
        if (import) {
            if (payload_size < 20) return Result<Archive>::fail("truncated import member");
            member.is_import = true;
            member.import.machine = read_u16(payload + 6);
            const uint32_t size_of_data = read_u32(payload + 12);
            member.import.hint_or_ordinal = read_u16(payload + 16);
            const uint16_t type = read_u16(payload + 18);
            member.import.import_type = static_cast<uint16_t>(type & 3u);
            member.import.name_type = static_cast<uint16_t>(type >> 2);
            if (size_of_data < 2 || 20u + size_of_data > payload_size) {
                return Result<Archive>::fail("truncated import member");
            }
            const char* text = reinterpret_cast<const char*>(payload + 20);
            size_t symbol_end = 0;
            while (symbol_end < size_of_data && text[symbol_end] != '\0') ++symbol_end;
            if (symbol_end >= size_of_data) return Result<Archive>::fail("import symbol is missing a terminator");
            member.import.symbol.assign(text, symbol_end);
            const size_t dll_at = symbol_end + 1;
            size_t dll_end = dll_at;
            while (dll_end < size_of_data && text[dll_end] != '\0') ++dll_end;
            if (dll_end >= size_of_data) return Result<Archive>::fail("import dll is missing a terminator");
            member.import.dll.assign(text + dll_at, dll_end - dll_at);
        } else {
            const auto object = read_coff(payload, payload_size);
            if (!object.ok()) return Result<Archive>::fail(object.error);
            member.object = object.value;
        }
        archive.members.push_back(std::move(member));
    }
    return Result<Archive>::success(std::move(archive));
}

}  // namespace okayms
