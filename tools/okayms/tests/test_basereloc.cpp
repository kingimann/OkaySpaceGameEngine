// Base relocations are IMAGE_REL_BASED_DIR64 (type 10) in the PE base
// relocation blocks. A DLL whose preferred base is the same as the exe
// must still run, because the loader adds the delta to every absolute
// address recorded here.

#include "check.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/wait.h>

namespace {

using okayms_test::Tally;

uint32_t u32at(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24));
}
uint16_t u16at(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

void test_dir64_block(Tally& tally) {
    std::printf("test_dir64_block\n");
    okayms::ObjectFile object;
    okayms::Section text;
    text.name = ".text";
    text.virtual_size = 1;
    text.data = {0xC3};
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead;
    object.sections.push_back(text);
    okayms::Section data;
    data.name = ".data";
    data.virtual_size = 8;
    data.data.assign(8, 0);
    data.characteristics = okayms::kScnCntInitData | okayms::kScnMemRead | okayms::kScnMemWrite;
    okayms::Relocation reloc;
    reloc.offset = 0;
    reloc.symbol = 1;
    reloc.type = okayms::kRelAmd64Addr64;
    data.relocations.push_back(reloc);
    object.sections.push_back(data);
    okayms::Symbol entry;
    entry.name = "entry";
    entry.section = 1;
    entry.type = 0x20;
    entry.storage = okayms::kClassExternal;
    object.symbols.push_back(entry);
    okayms::Symbol marker;
    marker.name = "marker";
    marker.section = 1;
    marker.value = 0;
    marker.storage = okayms::kClassExternal;
    object.symbols.push_back(marker);
    const auto linked = okayms::link_executable({object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  link: %s\n", linked.error.c_str());
        return;
    }
    const std::vector<uint8_t>& image = linked.value;
    // Relocations are present, so the stripped flag stays off and the
    // image may move. Directory 5 describes the table.
    CHECK(tally, (u16at(image, 0x56) & 0x0001) == 0);
    CHECK(tally, (u16at(image, 0x9E) & 0x0040) != 0);
    const uint32_t reloc_rva = u32at(image, 0xC8 + 40);
    const uint32_t reloc_size = u32at(image, 0xC8 + 44);
    CHECK(tally, reloc_rva != 0);
    CHECK(tally, reloc_size >= 12);
    // The absolute pointer sits at the start of .data, RVA 0x2000.
    // Type 10, offset 0, then an ABSOLUTE pad so the block is 4-aligned.
    bool found = false;
    for (size_t i = 0; i + 8 < image.size(); ++i) {
        if (std::memcmp(image.data() + i, ".reloc", 6) != 0) continue;
        const uint32_t raw = u32at(image, i + 20);
        CHECK(tally, u32at(image, raw) == 0x2000);
        CHECK(tally, u32at(image, raw + 4) == 12);
        CHECK(tally, u16at(image, raw + 8) == 0xA000);
        CHECK(tally, u16at(image, raw + 10) == 0);
        found = true;
        break;
    }
    CHECK(tally, found);
}

void test_collide_under_wine(Tally& tally) {
    std::printf("test_collide_under_wine\n");
    // value() loads an absolute pointer and returns the dword it points at.
    okayms::ObjectFile dll_obj;
    okayms::Section text;
    text.name = ".text";
    text.data = {0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x00, 0xC3,
                 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3};
    text.virtual_size = static_cast<uint32_t>(text.data.size());
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead;
    okayms::Relocation load;
    load.offset = 3;
    load.symbol = 2;
    load.type = okayms::kRelAmd64Rel32;
    text.relocations.push_back(load);
    dll_obj.sections.push_back(text);
    okayms::Section data;
    data.name = ".data";
    data.virtual_size = 8;
    data.data.assign(8, 0);
    data.characteristics = okayms::kScnCntInitData | okayms::kScnMemRead | okayms::kScnMemWrite;
    okayms::Relocation abs;
    abs.offset = 0;
    abs.symbol = 3;
    abs.type = okayms::kRelAmd64Addr64;
    data.relocations.push_back(abs);
    dll_obj.sections.push_back(data);
    okayms::Section marker_sec;
    marker_sec.name = ".rdata";
    marker_sec.virtual_size = 4;
    marker_sec.data = {42, 0, 0, 0};
    marker_sec.characteristics = okayms::kScnCntInitData | okayms::kScnMemRead;
    dll_obj.sections.push_back(marker_sec);
    auto ext = [](const char* name, int16_t section, uint32_t value) {
        okayms::Symbol symbol;
        symbol.name = name;
        symbol.section = section;
        symbol.value = value;
        symbol.type = section == 1 ? 0x20 : 0;
        symbol.storage = okayms::kClassExternal;
        return symbol;
    };
    dll_obj.symbols.push_back(ext("value", 1, 0));
    dll_obj.symbols.push_back(ext("entry", 1, 10));
    dll_obj.symbols.push_back(ext("slot", 2, 0));
    dll_obj.symbols.push_back(ext("marker", 3, 0));

    okayms::LinkOptions dll_opt;
    dll_opt.dll = true;
    dll_opt.image_base = 0x140000000ULL;
    dll_opt.image_name = "collide.dll";
    dll_opt.exports.push_back("value");
    const auto dll = okayms::link_executable({dll_obj}, dll_opt);
    CHECK(tally, dll.ok());
    if (!dll.ok()) {
        std::fprintf(stderr, "  dll: %s\n", dll.error.c_str());
        return;
    }

    okayms::ObjectFile exe;
    okayms::Section exe_text;
    exe_text.name = ".text";
    exe_text.data = {0x48, 0x83, 0xEC, 0x28, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x89, 0xC1, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00};
    exe_text.virtual_size = static_cast<uint32_t>(exe_text.data.size());
    exe_text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead;
    okayms::Relocation r0;
    r0.offset = 6;
    r0.symbol = 0;
    r0.type = okayms::kRelAmd64Rel32;
    okayms::Relocation r1;
    r1.offset = 14;
    r1.symbol = 1;
    r1.type = okayms::kRelAmd64Rel32;
    exe_text.relocations.push_back(r0);
    exe_text.relocations.push_back(r1);
    exe.sections.push_back(exe_text);
    okayms::Symbol imp;
    imp.name = "__imp_value";
    imp.storage = okayms::kClassExternal;
    exe.symbols.push_back(imp);
    okayms::Symbol imp_exit;
    imp_exit.name = "__imp_ExitProcess";
    imp_exit.storage = okayms::kClassExternal;
    exe.symbols.push_back(imp_exit);
    okayms::Symbol entry;
    entry.name = "entry";
    entry.section = 1;
    entry.type = 0x20;
    entry.storage = okayms::kClassExternal;
    exe.symbols.push_back(entry);
    okayms::LinkOptions exe_opt;
    exe_opt.image_base = 0x140000000ULL;
    exe_opt.imports.push_back({"collide.dll", "value", 0});
    exe_opt.imports.push_back({"kernel32.dll", "ExitProcess", 0});
    const auto image = okayms::link_executable({exe}, exe_opt);
    CHECK(tally, image.ok());
    if (!image.ok()) {
        std::fprintf(stderr, "  exe: %s\n", image.error.c_str());
        return;
    }

    const std::string dir = "/tmp/okayms-reloc";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    CHECK(tally, write_bytes(dir + "/collide.dll", dll.value));
    CHECK(tally, write_bytes(dir + "/use.exe", image.value));
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + dir +
                                 "/use.exe >/tmp/okayms-reloc-wine.txt 2>&1")
                                    .c_str());
    int code = -1;
    if (rc != -1 && WIFEXITED(rc)) code = WEXITSTATUS(rc);
    CHECK(tally, code == 42);
    if (code != 42) std::fprintf(stderr, "  wine exit %d\n", code);
}

}  // namespace

int main() {
    Tally tally;
    test_dir64_block(tally);
    test_collide_under_wine(tally);
    return tally.finish(2);
}
