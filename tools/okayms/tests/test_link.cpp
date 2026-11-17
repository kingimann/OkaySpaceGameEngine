// Byte checks for a PE32+ console image. Offsets are the ones in the
// PE Format document: DOS header, PE signature, COFF header, optional
// header (magic 0x20B), section table, file alignment and section alignment.

#include "check.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <cstring>
#include <stdlib.h>
#include <unistd.h>

namespace {

using okayms::ObjectFile;
using okayms::Relocation;
using okayms::Section;
using okayms::Symbol;
using okayms_test::Tally;

uint16_t u16at(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}
uint32_t u32at(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24));
}
uint64_t u64at(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint64_t>(u32at(b, at)) | (static_cast<uint64_t>(u32at(b, at + 4)) << 32);
}

Symbol sym(const char* name, uint32_t value, int16_t section, uint8_t storage) {
    Symbol s;
    s.name = name;
    s.value = value;
    s.section = section;
    s.type = section > 0 ? 0x20 : 0;
    s.storage = storage;
    return s;
}

Section text_section(std::vector<uint8_t> bytes) {
    Section s;
    s.name = ".text";
    s.virtual_size = static_cast<uint32_t>(bytes.size());
    s.data = std::move(bytes);
    s.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead | okayms::kScnAlign16;
    return s;
}

void test_static_image(Tally& tally) {
    std::printf("test_static_image\n");
    // call entry; ret; entry: mov eax, 42; ret
    // REL32 at the displacement. Target is 6 bytes from the start,
    // the next instruction is at byte 5, so the displacement is 1.
    ObjectFile object;
    Section text = text_section({0xE8, 0x00, 0x00, 0x00, 0x00, 0xC3, 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3});
    Relocation reloc;
    reloc.offset = 1;
    reloc.symbol = 0;
    reloc.type = okayms::kRelAmd64Rel32;
    text.relocations.push_back(reloc);
    object.sections.push_back(text);
    object.symbols.push_back(sym("entry", 6, 1, okayms::kClassExternal));

    const auto linked = okayms::link_executable({object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  link error: %s\n", linked.error.c_str());
        return;
    }
    const std::vector<uint8_t>& image = linked.value;
    CHECK(tally, image.size() == 0x400);
    CHECK(tally, image[0] == 'M' && image[1] == 'Z');
    CHECK(tally, u32at(image, 0x3C) == 64);
    CHECK(tally, image[0x40] == 'P' && image[0x41] == 'E' && image[0x42] == 0 && image[0x43] == 0);
    CHECK(tally, u16at(image, 0x44) == 0x8664);
    CHECK(tally, u16at(image, 0x46) == 1);
    CHECK(tally, u32at(image, 0x48) == 0);
    CHECK(tally, u32at(image, 0x4C) == 0);
    CHECK(tally, u32at(image, 0x50) == 0);
    CHECK(tally, u16at(image, 0x54) == 240);
    CHECK(tally, u16at(image, 0x56) == 0x0023);
    CHECK(tally, u16at(image, 0x58) == 0x20B);
    CHECK(tally, image[0x5A] == 0 && image[0x5B] == 1);
    CHECK(tally, u32at(image, 0x5C) == 0x200);
    CHECK(tally, u32at(image, 0x60) == 0);
    CHECK(tally, u32at(image, 0x64) == 0);
    CHECK(tally, u32at(image, 0x68) == 0x1006);
    CHECK(tally, u32at(image, 0x6C) == 0x1000);
    CHECK(tally, u64at(image, 0x70) == 0x140000000ULL);
    CHECK(tally, u32at(image, 0x78) == 0x1000);
    CHECK(tally, u32at(image, 0x7C) == 0x200);
    CHECK(tally, u16at(image, 0x80) == 6 && u16at(image, 0x82) == 0);
    CHECK(tally, u16at(image, 0x88) == 6 && u16at(image, 0x8A) == 0);
    CHECK(tally, u32at(image, 0x90) == 0x2000);
    CHECK(tally, u32at(image, 0x94) == 0x200);
    CHECK(tally, u32at(image, 0x98) == 0);
    CHECK(tally, u16at(image, 0x9C) == 3);
    CHECK(tally, u16at(image, 0x9E) == 0x0100);
    CHECK(tally, u64at(image, 0xA0) == 0x100000);
    CHECK(tally, u64at(image, 0xA8) == 0x1000);
    CHECK(tally, u64at(image, 0xB0) == 0x100000);
    CHECK(tally, u64at(image, 0xB8) == 0x1000);
    CHECK(tally, u32at(image, 0xC0) == 0);
    CHECK(tally, u32at(image, 0xC4) == 16);
    bool directories_clear = true;
    for (size_t i = 0xC8; i < 0x148; ++i) if (image[i] != 0) directories_clear = false;
    CHECK(tally, directories_clear);
    CHECK(tally, std::memcmp(image.data() + 0x148, ".text", 5) == 0);
    CHECK(tally, image[0x148 + 5] == 0);
    CHECK(tally, u32at(image, 0x148 + 8) == 12);
    CHECK(tally, u32at(image, 0x148 + 12) == 0x1000);
    CHECK(tally, u32at(image, 0x148 + 16) == 0x200);
    CHECK(tally, u32at(image, 0x148 + 20) == 0x200);
    CHECK(tally, u32at(image, 0x148 + 36) == 0x60000020u);
    // Header padding up to file alignment is zero.
    bool header_pad = true;
    for (size_t i = 0x170; i < 0x200; ++i) if (image[i] != 0) header_pad = false;
    CHECK(tally, header_pad);
    const uint8_t code[] = {0xE8, 0x01, 0x00, 0x00, 0x00, 0xC3, 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
    CHECK(tally, std::memcmp(image.data() + 0x200, code, sizeof code) == 0);
    bool tail_pad = true;
    for (size_t i = 0x200 + sizeof code; i < 0x400; ++i) if (image[i] != 0) tail_pad = false;
    CHECK(tally, tail_pad);

    char path[] = "/tmp/okayms-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    CHECK(tally, fd >= 0);
    if (fd >= 0) {
        const ssize_t wrote = write(fd, image.data(), image.size());
        close(fd);
        CHECK(tally, wrote == static_cast<ssize_t>(image.size()));
        std::string cmd = std::string("file -b ") + path;
        FILE* pipe = popen(cmd.c_str(), "r");
        CHECK(tally, pipe != nullptr);
        if (pipe) {
            char buf[256] = {};
            if (!std::fgets(buf, sizeof buf, pipe)) buf[0] = 0;
            pclose(pipe);
            const std::string got(buf);
            CHECK(tally, got.find("PE32+ executable (console) x86-64") != std::string::npos);
            if (got.find("PE32+ executable (console) x86-64") == std::string::npos) {
                std::fprintf(stderr, "  file said: %s\n", buf);
            }
        }
        unlink(path);
    }
}

void test_two_objects(Tally& tally) {
    std::printf("test_two_objects\n");
    ObjectFile caller;
    Section call = text_section({0xE8, 0x00, 0x00, 0x00, 0x00, 0xC3});
    Relocation reloc;
    reloc.offset = 1;
    reloc.symbol = 0;
    reloc.type = okayms::kRelAmd64Rel32;
    call.relocations.push_back(reloc);
    caller.sections.push_back(call);
    caller.symbols.push_back(sym("answer", 0, 0, okayms::kClassExternal));
    caller.symbols.push_back(sym("entry", 0, 1, okayms::kClassExternal));

    ObjectFile callee;
    callee.sections.push_back(text_section({0xB8, 0x07, 0x00, 0x00, 0x00, 0xC3}));
    callee.symbols.push_back(sym("answer", 0, 1, okayms::kClassExternal));

    const auto linked = okayms::link_executable({caller, callee}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  link error: %s\n", linked.error.c_str());
        return;
    }
    // Second contribution is aligned to 16. Displacement from the byte
    // after the reloc field (rva 0x1005) to rva 0x1010 is 0x0B.
    const uint8_t expect[] = {
        0xE8, 0x0B, 0x00, 0x00, 0x00, 0xC3,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0xB8, 0x07, 0x00, 0x00, 0x00, 0xC3};
    CHECK(tally, linked.value.size() > 0x200 + sizeof expect);
    CHECK(tally, std::memcmp(linked.value.data() + 0x200, expect, sizeof expect) == 0);
    CHECK(tally, u32at(linked.value, 0x68) == 0x1000);
    CHECK(tally, u32at(linked.value, 0x148 + 8) == 22);
}

void test_two_sections(Tally& tally) {
    std::printf("test_two_sections\n");
    ObjectFile object;
    object.sections.push_back(text_section({0xC3}));
    Section data;
    data.name = ".data";
    data.virtual_size = 1;
    data.data = {0x2A};
    data.characteristics = okayms::kScnCntInitData | okayms::kScnMemRead | okayms::kScnMemWrite;
    object.sections.push_back(data);
    object.symbols.push_back(sym("entry", 0, 1, okayms::kClassExternal));
    const auto linked = okayms::link_executable({object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    const std::vector<uint8_t>& image = linked.value;
    CHECK(tally, u16at(image, 0x46) == 2);
    CHECK(tally, u32at(image, 0x94) == 0x200);
    CHECK(tally, image[0x198] == 0 && image[0x1FF] == 0);
    CHECK(tally, std::memcmp(image.data() + 0x148, ".text", 5) == 0);
    CHECK(tally, u32at(image, 0x148 + 12) == 0x1000);
    CHECK(tally, u32at(image, 0x148 + 20) == 0x200);
    CHECK(tally, std::memcmp(image.data() + 0x170, ".data", 5) == 0);
    CHECK(tally, u32at(image, 0x170 + 8) == 1);
    CHECK(tally, u32at(image, 0x170 + 12) == 0x2000);
    CHECK(tally, u32at(image, 0x170 + 16) == 0x200);
    CHECK(tally, u32at(image, 0x170 + 20) == 0x400);
    CHECK(tally, u32at(image, 0x170 + 36) == 0xC0000040u);
    CHECK(tally, image[0x200] == 0xC3);
    CHECK(tally, image[0x400] == 0x2A);
    CHECK(tally, u32at(image, 0x90) == 0x3000);
    CHECK(tally, u32at(image, 0x5C) == 0x200);
    CHECK(tally, u32at(image, 0x60) == 0x200);
    CHECK(tally, image.size() == 0x600);
}

bool error_has(const okayms::Result<std::vector<uint8_t>>& result, const char* needle) {
    return !result.ok() && result.error.find(needle) != std::string::npos;
}

void test_errors(Tally& tally) {
    std::printf("test_errors\n");
    ObjectFile no_entry;
    no_entry.sections.push_back(text_section({0xC3}));
    CHECK(tally, error_has(okayms::link_executable({no_entry}, {}), "entry"));

    ObjectFile dup_a;
    dup_a.sections.push_back(text_section({0xC3}));
    dup_a.symbols.push_back(sym("entry", 0, 1, okayms::kClassExternal));
    ObjectFile dup_b = dup_a;
    CHECK(tally, error_has(okayms::link_executable({dup_a, dup_b}, {}), "duplicate"));

    ObjectFile missing;
    Section call = text_section({0xE8, 0x00, 0x00, 0x00, 0x00, 0xC3});
    Relocation reloc;
    reloc.offset = 1;
    reloc.symbol = 0;
    reloc.type = okayms::kRelAmd64Rel32;
    call.relocations.push_back(reloc);
    missing.sections.push_back(call);
    missing.symbols.push_back(sym("gone", 0, 0, okayms::kClassExternal));
    missing.symbols.push_back(sym("entry", 0, 1, okayms::kClassExternal));
    CHECK(tally, error_has(okayms::link_executable({missing}, {}), "undefined"));

    ObjectFile only_static;
    only_static.sections.push_back(text_section({0xC3}));
    only_static.symbols.push_back(sym("gone", 0, 1, okayms::kClassStatic));
    ObjectFile wants;
    Section wants_text = text_section({0xE8, 0x00, 0x00, 0x00, 0x00, 0xC3});
    Relocation wants_reloc;
    wants_reloc.offset = 1;
    wants_reloc.symbol = 0;
    wants_reloc.type = okayms::kRelAmd64Rel32;
    wants_text.relocations.push_back(wants_reloc);
    wants.sections.push_back(wants_text);
    wants.symbols.push_back(sym("gone", 0, 0, okayms::kClassExternal));
    wants.symbols.push_back(sym("entry", 0, 1, okayms::kClassExternal));
    CHECK(tally, error_has(okayms::link_executable({wants, only_static}, {}), "undefined"));

    ObjectFile comdat;
    comdat.sections.push_back(text_section({0xC3}));
    comdat.sections[0].characteristics |= okayms::kScnLnkComdat;
    comdat.symbols.push_back(sym("entry", 0, 1, okayms::kClassExternal));
    CHECK(tally, error_has(okayms::link_executable({comdat}, {}), "COMDAT"));

    ObjectFile bad_type;
    Section bad = text_section({0x00, 0x00, 0x00, 0x00});
    Relocation odd;
    odd.offset = 0;
    odd.symbol = 0;
    odd.type = 0xFFFF;
    bad.relocations.push_back(odd);
    bad_type.sections.push_back(bad);
    bad_type.symbols.push_back(sym("entry", 0, 1, okayms::kClassExternal));
    CHECK(tally, error_has(okayms::link_executable({bad_type}, {}), "unsupported"));
}

}  // namespace

int main() {
    Tally tally;
    test_static_image(tally);
    test_two_objects(tally);
    test_two_sections(tally);
    test_errors(tally);
    return tally.finish(4);
}
