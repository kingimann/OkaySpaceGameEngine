#include "okayms/link.hpp"

#include "okayms/archive.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace okayms {
namespace {

constexpr uint16_t kFileExecutable = 0x0002;
constexpr uint16_t kFileLargeAddress = 0x0020;
constexpr uint16_t kRelAbsolute = 0;
constexpr uint32_t kAlignMask = 0x00F00000u;
constexpr uint32_t kImageFlagDrop = kAlignMask | kScnLnkInfo | kScnLnkRemove | kScnLnkComdat | 0x01000000u | 0x8u;

uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t read_u64(const uint8_t* p) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<uint64_t>(p[i]) << (8 * i);
    return value;
}
void write_u32(uint8_t* p, uint32_t value) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}
void write_u64(uint8_t* p, uint64_t value) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}

bool power_of_two(uint32_t value) {
    return value != 0 && (value & (value - 1u)) == 0;
}

bool align_up(uint32_t value, uint32_t align, uint32_t* out, std::string* error) {
    if (!power_of_two(align)) {
        *error = "alignment is not a power of two";
        return false;
    }
    const uint32_t mask = align - 1u;
    if (value > 0xFFFFFFFFu - mask) {
        *error = "alignment overflow";
        return false;
    }
    *out = (value + mask) & ~mask;
    return true;
}

// PE Format section flags: a zero alignment nibble means 16 bytes.
bool contribution_alignment(uint32_t characteristics, uint32_t* align, std::string* error) {
    const uint32_t nibble = (characteristics >> 20) & 0xFu;
    if (nibble == 0) {
        *align = 16;
        return true;
    }
    if (nibble > 14) {
        *error = "invalid section alignment";
        return false;
    }
    *align = 1u << (nibble - 1u);
    return true;
}

struct OutSection {
    std::string name;
    uint32_t characteristics = 0;
    std::vector<uint8_t> bytes;
    uint32_t virtual_size = 0;
    bool any_data = false;
    uint32_t rva = 0;
    uint32_t raw_pointer = 0;
    uint32_t raw_size = 0;
};

struct Placed {
    bool defined = false;
    bool absolute = false;
    uint32_t out_section = 0;
    uint32_t offset = 0;
    uint32_t absolute_value = 0;
};

struct Contrib {
    bool present = false;
    uint32_t out_section = 0;
    uint32_t offset = 0;
};

struct ImportSlot {
    std::string name;
    uint16_t hint = 0;
    uint32_t hint_at = 0;
    uint32_t ilt_at = 0;
    uint32_t iat_at = 0;
    uint32_t thunk_at = 0;
};

struct ImportGroup {
    std::string dll;
    uint32_t desc_at = 0;
    uint32_t ilt_at = 0;
    uint32_t name_at = 0;
    std::vector<ImportSlot> slots;
};

struct ExportLayout {
    uint32_t functions_at = 0;
    uint32_t names_at = 0;
    uint32_t ordinals_at = 0;
    uint32_t dll_name_at = 0;
    std::vector<uint32_t> name_at;
    std::vector<std::string> names;
    std::string dll_name;
};

}  // namespace

namespace {

std::set<std::string> undefined_externals(const std::vector<ObjectFile>& objects, const LinkOptions& options) {
    std::set<std::string> defined;
    std::set<std::string> pending;
    for (const ObjectFile& object : objects) {
        for (const Symbol& symbol : object.symbols) {
            if (symbol.storage != kClassExternal) continue;
            if (symbol.section > 0 || symbol.section == kSectionAbsolute) defined.insert(symbol.name);
            else if (symbol.section == 0) pending.insert(symbol.name);
        }
    }
    for (const ImportedSymbol& imported : options.imports) {
        defined.insert(imported.name);
        defined.insert("__imp_" + imported.name);
    }
    std::set<std::string> need;
    for (const std::string& name : pending) {
        if (defined.find(name) == defined.end()) need.insert(name);
    }
    return need;
}

bool member_provides(const ArchiveMember& member, const std::string& name) {
    if (member.is_import) {
        return name == member.import.symbol || name == "__imp_" + member.import.symbol;
    }
    for (const Symbol& symbol : member.object.symbols) {
        if (symbol.storage == kClassExternal && (symbol.section > 0 || symbol.section == kSectionAbsolute) &&
            symbol.name == name) {
            return true;
        }
    }
    return false;
}

}  // namespace

Result<std::vector<uint8_t>> link_with_libraries(std::vector<ObjectFile> objects, const std::vector<Archive>& libraries,
                                                 LinkOptions options) {
    std::vector<std::vector<char>> taken;
    for (const Archive& library : libraries) taken.emplace_back(library.members.size(), 0);
    bool progressed = true;
    while (progressed) {
        progressed = false;
        const std::set<std::string> need = undefined_externals(objects, options);
        if (need.empty()) break;
        for (size_t li = 0; li < libraries.size(); ++li) {
            for (size_t mi = 0; mi < libraries[li].members.size(); ++mi) {
                if (taken[li][mi]) continue;
                const ArchiveMember& member = libraries[li].members[mi];
                bool useful = false;
                for (const std::string& name : need) {
                    if (member_provides(member, name)) useful = true;
                }
                if (!useful) continue;
                taken[li][mi] = 1;
                progressed = true;
                if (member.is_import) {
                    if (member.import.machine != kMachineAmd64) {
                        return Result<std::vector<uint8_t>>::fail("archive member machine is not AMD64");
                    }
                    ImportedSymbol imported;
                    imported.dll = member.import.dll;
                    imported.name = member.import.symbol;
                    imported.hint = member.import.hint_or_ordinal;
                    options.imports.push_back(std::move(imported));
                } else {
                    if (member.object.machine != kMachineAmd64) {
                        return Result<std::vector<uint8_t>>::fail("archive member machine is not AMD64");
                    }
                    objects.push_back(member.object);
                }
            }
        }
    }
    return link_executable(std::move(objects), options);
}

Result<std::vector<uint8_t>> link_executable(const std::vector<ObjectFile>& objects, const LinkOptions& options) {
    if (!power_of_two(options.file_alignment) || options.file_alignment < 512 || options.file_alignment > 65536) {
        return Result<std::vector<uint8_t>>::fail("file alignment must be a power of two from 512 to 65536");
    }
    if (!power_of_two(options.section_alignment) || options.section_alignment < options.file_alignment) {
        return Result<std::vector<uint8_t>>::fail("section alignment must be a power of two and at least the file alignment");
    }
    if (options.image_base == 0 || (options.image_base & 0xFFFFULL) != 0) {
        return Result<std::vector<uint8_t>>::fail("image base must be a multiple of 64 KiB");
    }

    struct ComdatKept {
        uint8_t selection = 0;
        uint32_t size = 0;
    };
    std::map<std::string, ComdatKept> comdat_kept;

    std::vector<OutSection> outs;
    std::map<std::string, uint32_t> section_index;
    std::vector<std::vector<Contrib>> contribs(objects.size());

    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const ObjectFile& object = objects[oi];
        contribs[oi].resize(object.sections.size());
        for (size_t si = 0; si < object.sections.size(); ++si) {
            const Section& section = object.sections[si];
            if ((section.characteristics & kScnLnkRemove) != 0) continue;
            if ((section.characteristics & kScnLnkComdat) != 0) {
                uint8_t selection = 0;
                std::string key;
                for (const Symbol& symbol : object.symbols) {
                    if (symbol.section != static_cast<int16_t>(si + 1)) continue;
                    if (key.empty() && symbol.storage == kClassExternal && !symbol.name.empty()) key = symbol.name;
                    if (selection == 0 && symbol.aux.size() >= 15 &&
                        (symbol.storage == kClassStatic || symbol.storage == kClassSection)) {
                        selection = symbol.aux[14];
                    }
                }
                if (key.empty()) return Result<std::vector<uint8_t>>::fail("COMDAT section has no symbol");
                if (selection != 2 && selection != 3) {
                    return Result<std::vector<uint8_t>>::fail("COMDAT selection is not supported");
                }
                const uint32_t comdat_size = section.data.empty() ? section.virtual_size
                                                                  : static_cast<uint32_t>(section.data.size());
                const auto found_comdat = comdat_kept.find(key);
                if (found_comdat != comdat_kept.end()) {
                    if (found_comdat->second.selection != selection) {
                        return Result<std::vector<uint8_t>>::fail("COMDAT selections disagree for " + key);
                    }
                    if (selection == 3 && found_comdat->second.size != comdat_size) {
                        return Result<std::vector<uint8_t>>::fail("COMDAT section size does not match for " + key);
                    }
                    continue;
                }
                ComdatKept kept;
                kept.selection = selection;
                kept.size = comdat_size;
                comdat_kept.emplace(key, kept);
            }
            // A '$' in a COFF section name groups contributions. The image
            // section keeps only the part before the '$'.
            std::string base_name = section.name;
            const size_t dollar = base_name.find('$');
            if (dollar != std::string::npos) base_name = base_name.substr(0, dollar);
            if (base_name.empty() || base_name.size() > 8) {
                return Result<std::vector<uint8_t>>::fail("section name is too long for an image");
            }
            uint32_t align = 0;
            std::string error;
            if (!contribution_alignment(section.characteristics, &align, &error)) {
                return Result<std::vector<uint8_t>>::fail(error);
            }
            uint32_t index = 0;
            const auto found = section_index.find(base_name);
            if (found == section_index.end()) {
                index = static_cast<uint32_t>(outs.size());
                section_index[base_name] = index;
                OutSection created;
                created.name = base_name;
                outs.push_back(std::move(created));
            } else {
                index = found->second;
            }
            OutSection& out = outs[index];
            out.characteristics |= section.characteristics;

            uint32_t at = 0;
            if (!align_up(out.virtual_size, align, &at, &error)) {
                return Result<std::vector<uint8_t>>::fail(error);
            }
            const uint32_t length = section.data.empty()
                                        ? section.virtual_size
                                        : static_cast<uint32_t>(section.data.size());
            if (section.data.size() > 0xFFFFFFFFu || at > 0xFFFFFFFFu - length) {
                return Result<std::vector<uint8_t>>::fail("section is too large");
            }
            if (!section.data.empty()) {
                if (out.bytes.size() < out.virtual_size) out.bytes.resize(out.virtual_size, 0);
                if (out.bytes.size() < at) out.bytes.resize(at, 0);
                out.bytes.insert(out.bytes.end(), section.data.begin(), section.data.end());
                out.any_data = true;
                out.virtual_size = static_cast<uint32_t>(out.bytes.size());
            } else {
                out.virtual_size = at + length;
                if (out.any_data) out.bytes.resize(out.virtual_size, 0);
            }
            contribs[oi][si].present = true;
            contribs[oi][si].out_section = index;
            contribs[oi][si].offset = at;
        }
    }
    std::vector<ImportGroup> import_groups;
    ExportLayout exp;
    int text_section = -1;
    int idata_section = -1;
    int edata_section = -1;
    uint32_t import_iat_begin = 0;
    uint32_t import_iat_size = 0;
    std::string error;
    if (!options.imports.empty()) {
        for (const ImportedSymbol& imported : options.imports) {
            if (imported.dll.empty() || imported.name.empty() ||
                imported.dll.find('\0') != std::string::npos ||
                imported.name.find('\0') != std::string::npos) {
                return Result<std::vector<uint8_t>>::fail("import is missing a dll or a name");
            }
            ImportGroup* group = nullptr;
            for (ImportGroup& existing : import_groups) {
                if (existing.dll == imported.dll) group = &existing;
            }
            if (group == nullptr) {
                import_groups.push_back(ImportGroup{});
                import_groups.back().dll = imported.dll;
                group = &import_groups.back();
            }
            ImportSlot slot;
            slot.name = imported.name;
            slot.hint = imported.hint;
            group->slots.push_back(std::move(slot));
        }
        uint32_t cursor = static_cast<uint32_t>(import_groups.size() + 1u) * 20u;
        for (size_t gi = 0; gi < import_groups.size(); ++gi) {
            ImportGroup& group = import_groups[gi];
            group.desc_at = static_cast<uint32_t>(gi) * 20u;
            group.ilt_at = cursor;
            if (cursor > 0xFFFFFFFFu - (static_cast<uint32_t>(group.slots.size()) + 1u) * 8u) {
                return Result<std::vector<uint8_t>>::fail("import table is too large");
            }
            for (size_t s = 0; s < group.slots.size(); ++s) {
                group.slots[s].ilt_at = cursor + static_cast<uint32_t>(s) * 8u;
            }
            cursor += (static_cast<uint32_t>(group.slots.size()) + 1u) * 8u;
            for (ImportSlot& slot : group.slots) {
                slot.hint_at = cursor;
                uint32_t hint_bytes = 2u + static_cast<uint32_t>(slot.name.size()) + 1u;
                if ((hint_bytes & 1u) != 0) ++hint_bytes;
                if (cursor > 0xFFFFFFFFu - hint_bytes) {
                    return Result<std::vector<uint8_t>>::fail("import table is too large");
                }
                cursor += hint_bytes;
            }
            group.name_at = cursor;
            if (cursor > 0xFFFFFFFFu - (static_cast<uint32_t>(group.dll.size()) + 1u)) {
                return Result<std::vector<uint8_t>>::fail("import table is too large");
            }
            cursor += static_cast<uint32_t>(group.dll.size()) + 1u;
        }
        if (!align_up(cursor, 8, &cursor, &error)) return Result<std::vector<uint8_t>>::fail(error);
        import_iat_begin = cursor;
        for (ImportGroup& group : import_groups) {
            for (ImportSlot& slot : group.slots) {
                slot.iat_at = cursor;
                cursor += 8u;
            }
            cursor += 8u;
        }
        import_iat_size = cursor - import_iat_begin;

        for (size_t i = 0; i < outs.size(); ++i) {
            if (outs[i].name == ".text") text_section = static_cast<int>(i);
        }
        if (text_section < 0) {
            OutSection created;
            created.name = ".text";
            created.characteristics = kScnCntCode | kScnMemExecute | kScnMemRead;
            created.any_data = true;
            outs.push_back(std::move(created));
            text_section = static_cast<int>(outs.size()) - 1;
        }
        OutSection& text = outs[static_cast<size_t>(text_section)];
        text.characteristics |= kScnCntCode | kScnMemExecute | kScnMemRead;
        if (text.bytes.size() < text.virtual_size) text.bytes.resize(text.virtual_size, 0);
        for (ImportGroup& group : import_groups) {
            for (ImportSlot& slot : group.slots) {
                slot.thunk_at = text.virtual_size;
                text.bytes.resize(static_cast<size_t>(text.virtual_size) + 6u, 0);
                text.virtual_size += 6u;
                text.any_data = true;
            }
        }
        OutSection idata;
        idata.name = ".idata";
        idata.bytes.assign(cursor, 0);
        idata.virtual_size = cursor;
        idata.any_data = true;
        idata.characteristics = kScnCntInitData | kScnMemRead | kScnMemWrite;
        outs.push_back(std::move(idata));
        idata_section = static_cast<int>(outs.size()) - 1;
    }
    if (!options.exports.empty()) {
        exp.names = options.exports;
        exp.dll_name = options.image_name.empty() ? std::string("output.dll") : options.image_name;
        uint32_t cursor = 40;
        exp.functions_at = cursor;
        cursor += static_cast<uint32_t>(exp.names.size()) * 4u;
        exp.names_at = cursor;
        cursor += static_cast<uint32_t>(exp.names.size()) * 4u;
        exp.ordinals_at = cursor;
        cursor += static_cast<uint32_t>(exp.names.size()) * 2u;
        for (const std::string& name : exp.names) {
            exp.name_at.push_back(cursor);
            cursor += static_cast<uint32_t>(name.size()) + 1u;
        }
        exp.dll_name_at = cursor;
        cursor += static_cast<uint32_t>(exp.dll_name.size()) + 1u;
        OutSection edata;
        edata.name = ".edata";
        edata.bytes.assign(cursor, 0);
        edata.virtual_size = cursor;
        edata.any_data = true;
        edata.characteristics = kScnCntInitData | kScnMemRead;
        outs.push_back(std::move(edata));
        edata_section = static_cast<int>(outs.size()) - 1;
    }
    if (outs.empty()) return Result<std::vector<uint8_t>>::fail("image has no sections");

    // Reserve the base-relocation section before addresses are assigned so
    // the header size includes it. The bytes are replaced once every
    // absolute address is known. IMAGE_REL_BASED_DIR64 is type 10.
    int reloc_section = -1;
    uint32_t addr64_count = 0;
    for (size_t oi = 0; oi < objects.size(); ++oi) {
        for (size_t si = 0; si < objects[oi].sections.size(); ++si) {
            if (!contribs[oi][si].present) continue;
            for (const Relocation& reloc : objects[oi].sections[si].relocations) {
                if (reloc.type == kRelAmd64Addr64) ++addr64_count;
            }
        }
    }
    {
        const uint32_t reserve = addr64_count == 0 ? 12u : addr64_count * 12u;
        OutSection rel;
        rel.name = ".reloc";
        rel.bytes.assign(reserve, 0);
        rel.virtual_size = reserve;
        rel.any_data = true;
        rel.characteristics = kScnCntInitData | kScnMemRead | 0x02000000u;
        outs.push_back(std::move(rel));
        reloc_section = static_cast<int>(outs.size()) - 1;
    }

    const uint32_t header_bytes = 64u + 4u + 20u + 240u + 40u * static_cast<uint32_t>(outs.size());
    uint32_t size_of_headers = 0;
    if (!align_up(header_bytes, options.file_alignment, &size_of_headers, &error)) {
        return Result<std::vector<uint8_t>>::fail(error);
    }
    uint32_t rva = 0;
    if (!align_up(size_of_headers, options.section_alignment, &rva, &error)) {
        return Result<std::vector<uint8_t>>::fail(error);
    }
    uint32_t raw_cursor = size_of_headers;
    uint32_t size_of_code = 0;
    uint32_t size_of_init = 0;
    uint32_t size_of_uninit = 0;
    uint32_t base_of_code = 0;
    bool saw_code = false;
    for (OutSection& out : outs) {
        out.rva = rva;
        const uint32_t span = out.virtual_size == 0 ? options.section_alignment : out.virtual_size;
        if (out.rva > 0xFFFFFFFFu - span) return Result<std::vector<uint8_t>>::fail("image is too large");
        uint32_t next = 0;
        if (!align_up(out.rva + span, options.section_alignment, &next, &error)) {
            return Result<std::vector<uint8_t>>::fail(error);
        }
        rva = next;
        out.characteristics &= ~kImageFlagDrop;
        if (out.any_data) {
            if (!align_up(static_cast<uint32_t>(out.bytes.size()), options.file_alignment, &out.raw_size, &error)) {
                return Result<std::vector<uint8_t>>::fail(error);
            }
            out.raw_pointer = raw_cursor;
            if (raw_cursor > 0xFFFFFFFFu - out.raw_size) return Result<std::vector<uint8_t>>::fail("image is too large");
            raw_cursor += out.raw_size;
        }
        if ((out.characteristics & kScnCntCode) != 0) {
            size_of_code += out.raw_size;
            if (!saw_code) {
                base_of_code = out.rva;
                saw_code = true;
            }
        } else if (!out.any_data) {
            size_of_uninit += out.virtual_size;
        } else {
            size_of_init += out.raw_size;
        }
    }
    uint32_t size_of_image = rva;
    std::vector<uint32_t> fixup_rvas;

    if (idata_section >= 0) {
        OutSection& idata = outs[static_cast<size_t>(idata_section)];
        auto write32 = [&](uint32_t at, uint32_t value) {
            write_u32(idata.bytes.data() + at, value);
        };
        for (const ImportGroup& group : import_groups) {
            write32(group.desc_at, idata.rva + group.ilt_at);
            write32(group.desc_at + 12, idata.rva + group.name_at);
            write32(group.desc_at + 16, idata.rva + group.slots.front().iat_at);
            std::memcpy(idata.bytes.data() + group.name_at, group.dll.data(), group.dll.size());
            for (const ImportSlot& slot : group.slots) {
                const uint32_t hint_rva = idata.rva + slot.hint_at;
                write32(slot.ilt_at, hint_rva);
                write32(slot.iat_at, hint_rva);
                idata.bytes[slot.hint_at] = static_cast<uint8_t>(slot.hint);
                idata.bytes[slot.hint_at + 1] = static_cast<uint8_t>(slot.hint >> 8);
                std::memcpy(idata.bytes.data() + slot.hint_at + 2, slot.name.data(), slot.name.size());
            }
        }
    }

    std::vector<std::vector<Placed>> placed(objects.size());
    std::map<std::string, Placed> globals;
    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const ObjectFile& object = objects[oi];
        placed[oi].resize(object.symbols.size());
        for (size_t si = 0; si < object.symbols.size(); ++si) {
            const Symbol& symbol = object.symbols[si];
            if (symbol.storage == kClassWeakExternal) continue;
            Placed slot;
            if (symbol.section == kSectionAbsolute) {
                slot.defined = true;
                slot.absolute = true;
                slot.absolute_value = symbol.value;
            } else if (symbol.section > 0) {
                if (static_cast<size_t>(symbol.section) > object.sections.size()) {
                    return Result<std::vector<uint8_t>>::fail("symbol section is outside the object");
                }
                const Contrib& contrib = contribs[oi][static_cast<size_t>(symbol.section) - 1u];
                if (!contrib.present) {
                    const Section& home = object.sections[static_cast<size_t>(symbol.section) - 1u];
                    if ((home.characteristics & kScnLnkComdat) != 0) continue;
                    return Result<std::vector<uint8_t>>::fail("symbol is in a section that was not linked");
                }
                if (symbol.value > 0xFFFFFFFFu - contrib.offset) {
                    return Result<std::vector<uint8_t>>::fail("symbol offset overflows");
                }
                slot.defined = true;
                slot.out_section = contrib.out_section;
                slot.offset = contrib.offset + symbol.value;
            }
            placed[oi][si] = slot;
            if (slot.defined && symbol.storage == kClassExternal) {
                const auto already = globals.find(symbol.name);
                if (already != globals.end()) {
                    return Result<std::vector<uint8_t>>::fail("duplicate symbol " + symbol.name);
                }
                globals[symbol.name] = slot;
            }
        }
    }
    if (idata_section >= 0) {
        for (const ImportGroup& group : import_groups) {
            for (const ImportSlot& slot : group.slots) {
                const std::string imp_name = "__imp_" + slot.name;
                if (globals.find(imp_name) != globals.end() || globals.find(slot.name) != globals.end()) {
                    return Result<std::vector<uint8_t>>::fail("duplicate symbol " + slot.name);
                }
                Placed at_iat;
                at_iat.defined = true;
                at_iat.out_section = static_cast<uint32_t>(idata_section);
                at_iat.offset = slot.iat_at;
                globals[imp_name] = at_iat;
                Placed at_thunk;
                at_thunk.defined = true;
                at_thunk.out_section = static_cast<uint32_t>(text_section);
                at_thunk.offset = slot.thunk_at;
                globals[slot.name] = at_thunk;
            }
        }
    }
    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const ObjectFile& object = objects[oi];
        std::vector<int32_t> raw_to_logical;
        for (size_t si = 0; si < object.symbols.size(); ++si) {
            if (raw_to_logical.size() > 1000000u) break;
            raw_to_logical.push_back(static_cast<int32_t>(si));
            const size_t aux_records = object.symbols[si].aux.size() / 18u;
            raw_to_logical.insert(raw_to_logical.end(), aux_records, -1);
        }
        for (size_t si = 0; si < object.symbols.size(); ++si) {
            const Symbol& symbol = object.symbols[si];
            if (symbol.storage != kClassWeakExternal) continue;
            if (globals.find(symbol.name) != globals.end()) continue;
            if (symbol.aux.size() < 8) {
                return Result<std::vector<uint8_t>>::fail("weak external is missing its auxiliary record");
            }
            const uint32_t tag = static_cast<uint32_t>(symbol.aux[0]) | (static_cast<uint32_t>(symbol.aux[1]) << 8) |
                                 (static_cast<uint32_t>(symbol.aux[2]) << 16) | (static_cast<uint32_t>(symbol.aux[3]) << 24);
            if (tag >= raw_to_logical.size() || raw_to_logical[tag] < 0) {
                return Result<std::vector<uint8_t>>::fail("weak external tag is outside the symbol table");
            }
            const uint32_t logical = static_cast<uint32_t>(raw_to_logical[tag]);
            const Symbol& alt = object.symbols[logical];
            Placed target;
            if (alt.section > 0 || alt.section == kSectionAbsolute) {
                target = placed[oi][logical];
            } else if (alt.storage == kClassExternal) {
                const auto found = globals.find(alt.name);
                if (found == globals.end()) {
                    return Result<std::vector<uint8_t>>::fail("undefined symbol " + alt.name);
                }
                target = found->second;
            } else {
                return Result<std::vector<uint8_t>>::fail("weak external tag is not a symbol");
            }
            if (!target.defined) return Result<std::vector<uint8_t>>::fail("undefined symbol " + alt.name);
            globals[symbol.name] = target;
            placed[oi][si] = target;
        }
    }
    if (edata_section >= 0) {
        OutSection& edata = outs[static_cast<size_t>(edata_section)];
        auto write32 = [&](uint32_t at, uint32_t value) { write_u32(edata.bytes.data() + at, value); };
        auto write16 = [&](uint32_t at, uint16_t value) {
            edata.bytes[at] = static_cast<uint8_t>(value);
            edata.bytes[at + 1] = static_cast<uint8_t>(value >> 8);
        };
        write32(16, 1);
        write32(20, static_cast<uint32_t>(exp.names.size()));
        write32(24, static_cast<uint32_t>(exp.names.size()));
        write32(28, edata.rva + exp.functions_at);
        write32(32, edata.rva + exp.names_at);
        write32(36, edata.rva + exp.ordinals_at);
        write32(12, edata.rva + exp.dll_name_at);
        std::memcpy(edata.bytes.data() + exp.dll_name_at, exp.dll_name.data(), exp.dll_name.size());
        for (size_t i = 0; i < exp.names.size(); ++i) {
            const auto found = globals.find(exp.names[i]);
            if (found == globals.end() || !found->second.defined || found->second.absolute) {
                return Result<std::vector<uint8_t>>::fail("export is not defined: " + exp.names[i]);
            }
            const uint32_t rva = outs[found->second.out_section].rva + found->second.offset;
            write32(exp.functions_at + static_cast<uint32_t>(i) * 4u, rva);
            write32(exp.names_at + static_cast<uint32_t>(i) * 4u, edata.rva + exp.name_at[i]);
            write16(exp.ordinals_at + static_cast<uint32_t>(i) * 2u, static_cast<uint16_t>(i));
            std::memcpy(edata.bytes.data() + exp.name_at[i], exp.names[i].data(), exp.names[i].size());
        }
    }

    int entry_matches = 0;
    Placed entry;
    for (size_t oi = 0; oi < objects.size(); ++oi) {
        for (size_t si = 0; si < objects[oi].symbols.size(); ++si) {
            const Symbol& symbol = objects[oi].symbols[si];
            if (symbol.name != options.entry || symbol.section <= 0) continue;
            ++entry_matches;
            entry = placed[oi][si];
        }
    }
    if (entry_matches == 0) return Result<std::vector<uint8_t>>::fail("entry symbol is not defined");
    if (entry_matches > 1) return Result<std::vector<uint8_t>>::fail("entry symbol is ambiguous");
    if (!entry.defined || entry.absolute) return Result<std::vector<uint8_t>>::fail("entry symbol is not in a section");
    if (entry.offset > 0xFFFFFFFFu - outs[entry.out_section].rva) {
        return Result<std::vector<uint8_t>>::fail("entry point overflows");
    }
    const uint32_t entry_rva = outs[entry.out_section].rva + entry.offset;

    auto resolve = [&](size_t oi, uint32_t symbol_index, Placed* out, std::string* err) -> bool {
        const ObjectFile& object = objects[oi];
        if (symbol_index >= object.symbols.size()) {
            *err = "relocation symbol index is outside the symbol table";
            return false;
        }
        const Symbol& symbol = object.symbols[symbol_index];
        if (symbol.storage == kClassWeakExternal) {
            if (placed[oi][symbol_index].defined) {
                *out = placed[oi][symbol_index];
                return true;
            }
            const auto found = globals.find(symbol.name);
            if (found == globals.end() || !found->second.defined) {
                *err = "undefined symbol " + symbol.name;
                return false;
            }
            *out = found->second;
            return true;
        }
        if (symbol.section > 0 || symbol.section == kSectionAbsolute) {
            *out = placed[oi][symbol_index];
            if (!out->defined) {
                *err = "undefined symbol " + symbol.name;
                return false;
            }
            return true;
        }
        if (symbol.section == 0 && symbol.storage == kClassExternal) {
            const auto found = globals.find(symbol.name);
            if (found == globals.end()) {
                *err = "undefined symbol " + symbol.name;
                return false;
            }
            *out = found->second;
            return true;
        }
        *err = "undefined symbol " + symbol.name;
        return false;
    };

    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const ObjectFile& object = objects[oi];
        for (size_t si = 0; si < object.sections.size(); ++si) {
            const Section& section = object.sections[si];
            const Contrib& contrib = contribs[oi][si];
            if (!contrib.present) continue;
            if (section.relocations.empty()) continue;
            OutSection& out = outs[contrib.out_section];
            if (!out.any_data) {
                return Result<std::vector<uint8_t>>::fail("relocation in an uninitialized section");
            }
            for (const Relocation& reloc : section.relocations) {
                if (reloc.type == kRelAbsolute) continue;
                const bool relative = reloc.type >= kRelAmd64Rel32 && reloc.type <= static_cast<uint16_t>(kRelAmd64Rel32 + 5);
                const bool known = reloc.type == kRelAmd64Addr64 || reloc.type == kRelAmd64Addr32 ||
                                   reloc.type == kRelAmd64Addr32Nb || relative;
                if (!known) {
                    return Result<std::vector<uint8_t>>::fail(
                        "unsupported relocation " + std::to_string(reloc.type));
                }
                const uint32_t width = reloc.type == kRelAmd64Addr64 ? 8u : 4u;
                if (static_cast<uint64_t>(reloc.offset) + width > section.data.size()) {
                    return Result<std::vector<uint8_t>>::fail("relocation offset is outside the section");
                }
                Placed target;
                if (!resolve(oi, reloc.symbol, &target, &error)) return Result<std::vector<uint8_t>>::fail(error);
                const uint32_t location = contrib.offset + reloc.offset;
                if (static_cast<uint64_t>(location) + width > out.bytes.size()) {
                    return Result<std::vector<uint8_t>>::fail("relocation offset is outside the section");
                }
                uint64_t target_rva = 0;
                uint64_t target_va = 0;
                if (target.absolute) {
                    target_va = target.absolute_value;
                } else {
                    target_rva = static_cast<uint64_t>(outs[target.out_section].rva) + target.offset;
                    target_va = options.image_base + target_rva;
                }
                uint8_t* slot = out.bytes.data() + location;
                if (reloc.type == kRelAmd64Addr64) {
                    write_u64(slot, read_u64(slot) + target_va);
                    if (!target.absolute) fixup_rvas.push_back(out.rva + location);
                } else if (reloc.type == kRelAmd64Addr32) {
                    const uint64_t sum = static_cast<uint64_t>(read_u32(slot)) + target_va;
                    if (sum > 0xFFFFFFFFu) {
                        return Result<std::vector<uint8_t>>::fail("ADDR32 relocation does not fit");
                    }
                    write_u32(slot, static_cast<uint32_t>(sum));
                } else if (reloc.type == kRelAmd64Addr32Nb) {
                    if (target.absolute) {
                        return Result<std::vector<uint8_t>>::fail("ADDR32NB relocation against an absolute symbol");
                    }
                    const uint64_t sum = static_cast<uint64_t>(read_u32(slot)) + target_rva;
                    if (sum > 0xFFFFFFFFu) {
                        return Result<std::vector<uint8_t>>::fail("ADDR32NB relocation does not fit");
                    }
                    write_u32(slot, static_cast<uint32_t>(sum));
                } else {
                    // REL32 is from the byte after the field. REL32_1 .. REL32_5
                    // are from 1 .. 5 bytes beyond that.
                    const int extra = static_cast<int>(reloc.type - kRelAmd64Rel32);
                    const uint64_t site_va = options.image_base + out.rva + location;
                    const int32_t addend = static_cast<int32_t>(read_u32(slot));
                    const int64_t result = static_cast<int64_t>(addend) +
                                           (static_cast<int64_t>(target_va) -
                                            (static_cast<int64_t>(site_va) + 4 + extra));
                    if (result < static_cast<int64_t>(-2147483647 - 1) || result > 2147483647LL) {
                        return Result<std::vector<uint8_t>>::fail("REL32 relocation does not fit");
                    }
                    write_u32(slot, static_cast<uint32_t>(static_cast<int32_t>(result)));
                }
            }
        }
    }
    if (idata_section >= 0) {
        OutSection& text = outs[static_cast<size_t>(text_section)];
        const OutSection& idata = outs[static_cast<size_t>(idata_section)];
        for (const ImportGroup& group : import_groups) {
            for (const ImportSlot& slot : group.slots) {
                const uint32_t at = slot.thunk_at;
                text.bytes[at] = 0xFF;
                text.bytes[at + 1] = 0x25;
                const uint64_t target = options.image_base + idata.rva + slot.iat_at;
                const uint64_t site = options.image_base + text.rva + at + 2u;
                const int64_t disp = static_cast<int64_t>(target) - (static_cast<int64_t>(site) + 4);
                if (disp < static_cast<int64_t>(-2147483647 - 1) || disp > 2147483647LL) {
                    return Result<std::vector<uint8_t>>::fail("REL32 relocation does not fit");
                }
                write_u32(text.bytes.data() + at + 2, static_cast<uint32_t>(static_cast<int32_t>(disp)));
            }
        }
    }
    if (reloc_section >= 0) {
        std::sort(fixup_rvas.begin(), fixup_rvas.end());
        fixup_rvas.erase(std::unique(fixup_rvas.begin(), fixup_rvas.end()), fixup_rvas.end());
        std::vector<uint8_t> built;
        auto append32 = [&](uint32_t value) {
            for (int i = 0; i < 4; ++i) built.push_back(static_cast<uint8_t>(value >> (8 * i)));
        };
        auto append_block = [&](uint32_t page, const std::vector<uint16_t>& entries) {
            append32(page);
            append32(8u + static_cast<uint32_t>(entries.size()) * 2u);
            for (uint16_t entry : entries) {
                built.push_back(static_cast<uint8_t>(entry));
                built.push_back(static_cast<uint8_t>(entry >> 8));
            }
        };
        if (fixup_rvas.empty()) {
            append_block(0, {0, 0});
        } else {
            size_t i = 0;
            while (i < fixup_rvas.size()) {
                const uint32_t page = fixup_rvas[i] & ~0xFFFu;
                std::vector<uint16_t> entries;
                while (i < fixup_rvas.size() && (fixup_rvas[i] & ~0xFFFu) == page) {
                    entries.push_back(static_cast<uint16_t>((10u << 12) | (fixup_rvas[i] & 0xFFFu)));
                    ++i;
                }
                if ((entries.size() & 1u) != 0) entries.push_back(0);
                append_block(page, entries);
            }
        }
        OutSection& rel = outs[static_cast<size_t>(reloc_section)];
        if (built.size() > rel.bytes.size()) {
            return Result<std::vector<uint8_t>>::fail("base relocation table is larger than reserved");
        }
        const uint32_t old_raw = rel.raw_size;
        rel.bytes = std::move(built);
        rel.virtual_size = static_cast<uint32_t>(rel.bytes.size());
        uint32_t new_raw = 0;
        if (!align_up(rel.virtual_size, options.file_alignment, &new_raw, &error)) {
            return Result<std::vector<uint8_t>>::fail(error);
        }
        size_of_init = size_of_init - old_raw + new_raw;
        rel.raw_size = new_raw;
        size_of_image = 0;
        for (const OutSection& out : outs) {
            const uint32_t span = out.virtual_size == 0 ? options.section_alignment : out.virtual_size;
            uint32_t end = 0;
            if (!align_up(out.rva + span, options.section_alignment, &end, &error)) {
                return Result<std::vector<uint8_t>>::fail(error);
            }
            if (end > size_of_image) size_of_image = end;
        }
    }

    std::vector<uint8_t> image(size_of_headers, 0);
    image[0] = 'M';
    image[1] = 'Z';
    image[0x3C] = 64;
    image[0x40] = 'P';
    image[0x41] = 'E';
    auto put16 = [&](size_t at, uint16_t value) {
        image[at] = static_cast<uint8_t>(value);
        image[at + 1] = static_cast<uint8_t>(value >> 8);
    };
    auto put32 = [&](size_t at, uint32_t value) {
        for (int i = 0; i < 4; ++i) image[at + i] = static_cast<uint8_t>(value >> (8 * i));
    };
    auto put64 = [&](size_t at, uint64_t value) {
        for (int i = 0; i < 8; ++i) image[at + i] = static_cast<uint8_t>(value >> (8 * i));
    };
    put16(0x44, kMachineAmd64);
    put16(0x46, static_cast<uint16_t>(outs.size()));
    put32(0x54, 240);
    put16(0x56, static_cast<uint16_t>(kFileExecutable | kFileLargeAddress | (options.dll ? 0x2000 : 0)));
    put16(0x58, 0x20B);
    image[0x5B] = 1;
    put32(0x5C, size_of_code);
    put32(0x60, size_of_init);
    put32(0x64, size_of_uninit);
    put32(0x68, entry_rva);
    put32(0x6C, base_of_code);
    put64(0x70, options.image_base);
    put32(0x78, options.section_alignment);
    put32(0x7C, options.file_alignment);
    put16(0x80, options.major_operating_system);
    put16(0x82, options.minor_operating_system);
    put16(0x88, options.major_subsystem);
    put16(0x8A, options.minor_subsystem);
    put32(0x90, size_of_image);
    put32(0x94, size_of_headers);
    put16(0x9C, options.subsystem);
    put16(0x9E, static_cast<uint16_t>(options.dll_characteristics | 0x0040));
    put64(0xA0, options.stack_reserve);
    put64(0xA8, options.stack_commit);
    put64(0xB0, options.heap_reserve);
    put64(0xB8, options.heap_commit);
    put32(0xC4, 16);
    if (idata_section >= 0) {
        put32(0xC8 + 8, outs[static_cast<size_t>(idata_section)].rva);
        put32(0xC8 + 12, static_cast<uint32_t>(import_groups.size() + 1u) * 20u);
        put32(0xC8 + 96, outs[static_cast<size_t>(idata_section)].rva + import_iat_begin);
        put32(0xC8 + 100, import_iat_size);
    }
    for (const OutSection& out : outs) {
        if (out.name == ".pdata") {
            put32(0xC8 + 24, out.rva);
            put32(0xC8 + 28, out.virtual_size);
        }
    }
    if (edata_section >= 0) {
        put32(0xC8, outs[static_cast<size_t>(edata_section)].rva);
        put32(0xC8 + 4, 40);
    }
    if (reloc_section >= 0) {
        put32(0xC8 + 40, outs[static_cast<size_t>(reloc_section)].rva);
        put32(0xC8 + 44, outs[static_cast<size_t>(reloc_section)].virtual_size);
    }

    size_t section_at = 0x148;
    for (const OutSection& out : outs) {
        std::memset(image.data() + section_at, 0, 40);
        std::memcpy(image.data() + section_at, out.name.data(), out.name.size());
        put32(section_at + 8, out.virtual_size);
        put32(section_at + 12, out.rva);
        put32(section_at + 16, out.raw_size);
        put32(section_at + 20, out.raw_pointer);
        put32(section_at + 36, out.characteristics);
        section_at += 40;
    }

    for (const OutSection& out : outs) {
        if (!out.any_data) continue;
        const size_t end = static_cast<size_t>(out.raw_pointer) + out.raw_size;
        if (image.size() < end) image.resize(end, 0);
        std::memcpy(image.data() + out.raw_pointer, out.bytes.data(), out.bytes.size());
    }
    return Result<std::vector<uint8_t>>::success(std::move(image));
}

}  // namespace okayms
