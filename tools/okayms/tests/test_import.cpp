// Import directory for kernel32!ExitProcess. The byte layout is the one
// in the PE Format document: import descriptor, lookup table, hint/name,
// DLL name, import address table. The call is a RIP-relative indirect
// call, relocated with REL32 from the next instruction.

#include "check.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <cstring>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

uint32_t u32at(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24));
}
uint16_t u16at_sections(const std::vector<uint8_t>& b) {
    return static_cast<uint16_t>(b[0x46] | (b[0x47] << 8));
}

okayms::ObjectFile exit_object() {
    okayms::ObjectFile object;
    okayms::Section text;
    text.name = ".text";
    text.virtual_size = 15;
    text.data = {0x48, 0x83, 0xEC, 0x28, 0xB9, 0x2A, 0x00, 0x00, 0x00,
                 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00};
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead | okayms::kScnAlign16;
    okayms::Relocation reloc;
    reloc.offset = 11;
    reloc.symbol = 0;
    reloc.type = okayms::kRelAmd64Rel32;
    text.relocations.push_back(reloc);
    object.sections.push_back(text);
    okayms::Symbol imp;
    imp.name = "__imp_ExitProcess";
    imp.storage = okayms::kClassExternal;
    object.symbols.push_back(imp);
    okayms::Symbol entry;
    entry.name = "entry";
    entry.section = 1;
    entry.value = 0;
    entry.type = 0x20;
    entry.storage = okayms::kClassExternal;
    object.symbols.push_back(entry);
    return object;
}

void test_import_bytes(Tally& tally) {
    std::printf("test_import_bytes\n");
    okayms::LinkOptions options;
    options.imports.push_back({"kernel32.dll", "ExitProcess", 0});
    const auto linked = okayms::link_executable({exit_object()}, options);
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  link error: %s\n", linked.error.c_str());
        return;
    }
    const std::vector<uint8_t>& image = linked.value;
    CHECK(tally, image.size() == 0x600);
    CHECK(tally, u32at(image, 0x68) == 0x1000);
    CHECK(tally, u32at(image, 0x90) == 0x3000);
    CHECK(tally, u16at_sections(image) == 2);
    const uint8_t code[] = {
        0x48, 0x83, 0xEC, 0x28, 0xB9, 0x2A, 0x00, 0x00, 0x00,
        0xFF, 0x15, 0x49, 0x10, 0x00, 0x00,
        0xFF, 0x25, 0x43, 0x10, 0x00, 0x00};
    CHECK(tally, std::memcmp(image.data() + 0x200, code, sizeof code) == 0);
    CHECK(tally, std::memcmp(image.data() + 0x170, ".idata", 6) == 0);
    CHECK(tally, u32at(image, 0x170 + 8) == 104);
    CHECK(tally, u32at(image, 0x170 + 12) == 0x2000);
    CHECK(tally, u32at(image, 0x170 + 20) == 0x400);
    CHECK(tally, u32at(image, 0x170 + 36) == 0xC0000040u);
    // Directory 1 is the import table. Directory 12 is the IAT.
    CHECK(tally, u32at(image, 0xD0) == 0x2000);
    CHECK(tally, u32at(image, 0xD4) == 40);
    CHECK(tally, u32at(image, 0x128) == 0x2058);
    CHECK(tally, u32at(image, 0x12C) == 16);

    std::vector<uint8_t> idata(104, 0);
    // Descriptor: ILT 0x2028, name 0x2046, IAT 0x2058.
    idata[0] = 0x28; idata[1] = 0x20;
    idata[12] = 0x46; idata[13] = 0x20;
    idata[16] = 0x58; idata[17] = 0x20;
    // ILT thunk -> hint/name at 0x2038.
    idata[40] = 0x38; idata[41] = 0x20;
    idata[56] = 0x00; idata[57] = 0x00;
    const char* fname = "ExitProcess";
    std::memcpy(idata.data() + 58, fname, 12);
    const char* dll = "kernel32.dll";
    std::memcpy(idata.data() + 70, dll, 13);
    idata[88] = 0x38; idata[89] = 0x20;
    CHECK(tally, std::memcmp(image.data() + 0x400, idata.data(), idata.size()) == 0);
}

void test_direct_thunk(Tally& tally) {
    std::printf("test_direct_thunk\n");
    okayms::ObjectFile object;
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
    object.sections.push_back(text);
    okayms::Symbol target;
    target.name = "ExitProcess";
    target.storage = okayms::kClassExternal;
    object.symbols.push_back(target);
    okayms::Symbol entry;
    entry.name = "entry";
    entry.section = 1;
    entry.type = 0x20;
    entry.storage = okayms::kClassExternal;
    object.symbols.push_back(entry);
    okayms::LinkOptions options;
    options.imports.push_back({"kernel32.dll", "ExitProcess", 0});
    const auto linked = okayms::link_executable({object}, options);
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  link error: %s\n", linked.error.c_str());
        return;
    }
    const uint8_t code[] = {0xE8, 0x01, 0x00, 0x00, 0x00, 0xC3, 0xFF, 0x25, 0x4C, 0x10, 0x00, 0x00};
    CHECK(tally, std::memcmp(linked.value.data() + 0x200, code, sizeof code) == 0);
}

void test_wine(Tally& tally) {
    std::printf("test_wine\n");
    okayms::LinkOptions options;
    options.imports.push_back({"kernel32.dll", "ExitProcess", 0});
    const auto linked = okayms::link_executable({exit_object()}, options);
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    char path[] = "/tmp/okayms-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    CHECK(tally, fd >= 0);
    if (fd < 0) return;
    const ssize_t wrote = write(fd, linked.value.data(), linked.value.size());
    close(fd);
    CHECK(tally, wrote == static_cast<ssize_t>(linked.value.size()));
    const std::string cmd =
        "WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 90 wine " + std::string(path) +
        " >/tmp/okayms-wine-out.txt 2>/tmp/okayms-wine-err.txt";
    const int rc = std::system(cmd.c_str());
    int code = -1;
    if (rc != -1 && WIFEXITED(rc)) code = WEXITSTATUS(rc);
    CHECK(tally, code == 42);
    if (code != 42) {
        std::fprintf(stderr, "  wine exit %d (raw %d)\n", code, rc);
        std::FILE* err = std::fopen("/tmp/okayms-wine-err.txt", "r");
        if (err) {
            char buf[400];
            const size_t n = std::fread(buf, 1, sizeof buf - 1, err);
            buf[n] = 0;
            std::fclose(err);
            std::fprintf(stderr, "  wine stderr: %s\n", buf);
        }
    }
    unlink(path);
}

}  // namespace

int main() {
    Tally tally;
    test_import_bytes(tally);
    test_direct_thunk(tally);
    test_wine(tally);
    return tally.finish(3);
}
