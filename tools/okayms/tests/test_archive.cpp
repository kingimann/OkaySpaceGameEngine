// COFF archive bytes. The header is 60 bytes, the signature is "!<arch>\n",
// and a short import member starts with 00 00 FF FF, as the PE Format
// document describes.

#include "check.hpp"
#include "okayms/archive.hpp"
#include "okayms/link.hpp"

#include <cstring>

namespace {

using okayms_test::Tally;

std::string header_for(const char* name, size_t payload) {
    std::string header(60, ' ');
    const std::string decorated = std::string(name) + "/";
    if (decorated.size() <= 16) {
        std::memcpy(header.data(), decorated.data(), decorated.size());
    }
    const std::string size = std::to_string(payload);
    std::memcpy(header.data() + 48, size.data(), size.size());
    header[58] = '`';
    header[59] = '\n';
    return header;
}

okayms::ObjectFile answer_object() {
    okayms::ObjectFile object;
    okayms::Section text;
    text.name = ".text";
    text.virtual_size = 6;
    text.data = {0xB8, 0x07, 0x00, 0x00, 0x00, 0xC3};
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead | okayms::kScnAlign16;
    object.sections.push_back(text);
    okayms::Symbol symbol;
    symbol.name = "answer";
    symbol.section = 1;
    symbol.type = 0x20;
    symbol.storage = okayms::kClassExternal;
    object.symbols.push_back(symbol);
    return object;
}

void test_coff_member_bytes(Tally& tally) {
    std::printf("test_coff_member_bytes\n");
    const auto coff = okayms::write_coff(answer_object());
    CHECK(tally, coff.ok());
    if (!coff.ok()) return;
    okayms::Archive archive;
    okayms::ArchiveMember member;
    member.name = "hello.obj";
    member.object = answer_object();
    archive.members.push_back(member);
    const auto bytes = okayms::write_archive(archive);
    CHECK(tally, bytes.ok());
    if (!bytes.ok()) {
        std::fprintf(stderr, "  archive error: %s\n", bytes.error.c_str());
        return;
    }
    const std::string expect_sig = "!<arch>\n";
    CHECK(tally, bytes.value.size() >= 8 + 60 + coff.value.size());
    CHECK(tally, std::memcmp(bytes.value.data(), expect_sig.data(), 8) == 0);
    const std::string header = header_for("hello.obj", coff.value.size());
    CHECK(tally, std::memcmp(bytes.value.data() + 8, header.data(), 60) == 0);
    CHECK(tally, std::memcmp(bytes.value.data() + 68, coff.value.data(), coff.value.size()) == 0);

    const auto read = okayms::read_archive(bytes.value.data(), bytes.value.size());
    CHECK(tally, read.ok());
    if (!read.ok()) return;
    CHECK(tally, read.value.members.size() == 1);
    CHECK(tally, read.value.members[0].name == "hello.obj");
    CHECK(tally, !read.value.members[0].is_import);
    CHECK(tally, read.value.members[0].object.symbols.size() == 1);
    CHECK(tally, read.value.members[0].object.symbols[0].name == "answer");
    const auto again = okayms::write_coff(read.value.members[0].object);
    CHECK(tally, again.ok() && again.value == coff.value);
}

void test_long_name_and_import(Tally& tally) {
    std::printf("test_long_name_and_import\n");
    okayms::Archive archive;
    okayms::ArchiveMember object_member;
    object_member.name = "a_very_long_member_name.obj";
    object_member.object = answer_object();
    archive.members.push_back(object_member);
    okayms::ArchiveMember imported;
    imported.name = "ExitProcess";
    imported.is_import = true;
    imported.import.machine = okayms::kMachineAmd64;
    imported.import.hint_or_ordinal = 42;
    imported.import.import_type = okayms::kImportCode;
    imported.import.name_type = okayms::kImportByName;
    imported.import.symbol = "ExitProcess";
    imported.import.dll = "kernel32.dll";
    archive.members.push_back(imported);

    const auto bytes = okayms::write_archive(archive);
    CHECK(tally, bytes.ok());
    if (!bytes.ok()) return;
    CHECK(tally, bytes.value.size() >= 8 && std::memcmp(bytes.value.data(), "!<arch>\n", 8) == 0);
    // The long name is stored in the "//" member and referenced as "/0".
    bool saw_longnames = false;
    bool saw_slash_zero = false;
    for (size_t i = 8; i + 16 < bytes.value.size(); ++i) {
        if (std::memcmp(bytes.value.data() + i, "//              ", 16) == 0) saw_longnames = true;
        if (std::memcmp(bytes.value.data() + i, "/0              ", 16) == 0) saw_slash_zero = true;
    }
    CHECK(tally, saw_longnames);
    CHECK(tally, saw_slash_zero);

    const auto read = okayms::read_archive(bytes.value.data(), bytes.value.size());
    CHECK(tally, read.ok());
    if (!read.ok()) {
        std::fprintf(stderr, "  read error: %s\n", read.error.c_str());
        return;
    }
    CHECK(tally, read.value.members.size() == 2);
    CHECK(tally, read.value.members[0].name == "a_very_long_member_name.obj");
    CHECK(tally, read.value.members[1].is_import);
    CHECK(tally, read.value.members[1].import.machine == okayms::kMachineAmd64);
    CHECK(tally, read.value.members[1].import.hint_or_ordinal == 42);
    CHECK(tally, read.value.members[1].import.import_type == okayms::kImportCode);
    CHECK(tally, read.value.members[1].import.name_type == okayms::kImportByName);
    CHECK(tally, read.value.members[1].import.symbol == "ExitProcess");
    CHECK(tally, read.value.members[1].import.dll == "kernel32.dll");

    // The import header itself is 20 bytes and starts with 00 00 FF FF.
    const char* needle = "\x00\x00\xFF\xFF";
    bool saw_sig = false;
    for (size_t i = 0; i + 4 <= bytes.value.size(); ++i) {
        if (std::memcmp(bytes.value.data() + i, needle, 4) == 0) saw_sig = true;
    }
    CHECK(tally, saw_sig);
}

void test_rejects(Tally& tally) {
    std::printf("test_rejects\n");
    const uint8_t tiny[] = {'!', '<', 'a'};
    const auto bad = okayms::read_archive(tiny, sizeof tiny);
    CHECK(tally, !bad.ok() && bad.error.find("truncated") != std::string::npos);
}

void test_link_archive(Tally& tally) {
    std::printf("test_link_archive\n");
    okayms::ObjectFile caller;
    okayms::Section text;
    text.name = ".text";
    text.virtual_size = 6;
    text.data = {0xE8, 0x00, 0x00, 0x00, 0x00, 0xC3};
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead | okayms::kScnAlign16;
    okayms::Relocation reloc;
    reloc.offset = 1;
    reloc.symbol = 0;
    reloc.type = okayms::kRelAmd64Rel32;
    text.relocations.push_back(reloc);
    caller.sections.push_back(text);
    okayms::Symbol undef;
    undef.name = "answer";
    undef.storage = okayms::kClassExternal;
    caller.symbols.push_back(undef);
    okayms::Symbol entry;
    entry.name = "entry";
    entry.section = 1;
    entry.type = 0x20;
    entry.storage = okayms::kClassExternal;
    caller.symbols.push_back(entry);

    okayms::Archive archive;
    okayms::ArchiveMember member;
    member.name = "answer.obj";
    member.object = answer_object();
    archive.members.push_back(member);
    okayms::ArchiveMember imported;
    imported.name = "ExitProcess";
    imported.is_import = true;
    imported.import.symbol = "ExitProcess";
    imported.import.dll = "kernel32.dll";
    imported.import.import_type = okayms::kImportCode;
    imported.import.name_type = okayms::kImportByName;
    archive.members.push_back(imported);

    const auto via_lib = okayms::link_with_libraries({caller}, {archive}, {});
    const auto direct = okayms::link_executable({caller, answer_object()}, {});
    CHECK(tally, via_lib.ok());
    CHECK(tally, direct.ok());
    if (!via_lib.ok() || !direct.ok()) {
        if (!via_lib.ok()) std::fprintf(stderr, "  lib link: %s\n", via_lib.error.c_str());
        return;
    }
    // The import member is not referenced, so it stays out and the image
    // matches linking the two objects directly.
    CHECK(tally, via_lib.value == direct.value);

    caller.symbols[0].name = "ExitProcess";
    const auto imported_link = okayms::link_with_libraries({caller}, {archive}, {});
    CHECK(tally, imported_link.ok());
    if (!imported_link.ok()) {
        std::fprintf(stderr, "  import link: %s\n", imported_link.error.c_str());
        return;
    }
    bool saw_kernel = false;
    const char dll[] = "kernel32.dll";
    for (size_t i = 0; i + sizeof dll <= imported_link.value.size(); ++i) {
        if (std::memcmp(imported_link.value.data() + i, dll, sizeof dll) == 0) saw_kernel = true;
    }
    CHECK(tally, saw_kernel);
}

}  // namespace

int main() {
    Tally tally;
    test_coff_member_bytes(tally);
    test_long_name_and_import(tally);
    test_rejects(tally);
    test_link_archive(tally);
    return tally.finish(4);
}
