// okay-link: /OUT: /ENTRY: /SUBSYSTEM: /LIBPATH: plus an object and a .lib.

#include "check.hpp"
#include "okayms/archive.hpp"
#include "okayms/coff.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

std::vector<uint8_t> read_bytes(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int run_cmd(const std::string& cmd) {
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
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
    object.sections.push_back(std::move(text));
    okayms::Symbol imp;
    imp.name = "__imp_ExitProcess";
    imp.storage = okayms::kClassExternal;
    object.symbols.push_back(imp);
    okayms::Symbol entry;
    entry.name = "entry";
    entry.section = 1;
    entry.type = 0x20;
    entry.storage = okayms::kClassExternal;
    object.symbols.push_back(entry);
    return object;
}

okayms::Archive kernel32() {
    okayms::Archive archive;
    okayms::ArchiveMember member;
    member.name = "ExitProcess";
    member.is_import = true;
    member.import.symbol = "ExitProcess";
    member.import.dll = "kernel32.dll";
    member.import.import_type = okayms::kImportCode;
    member.import.name_type = okayms::kImportByName;
    archive.members.push_back(member);
    return archive;
}

void test_driver(Tally& tally, const std::string& driver) {
    std::printf("test_driver\n");
    struct stat st;
    CHECK(tally, stat(driver.c_str(), &st) == 0);
    if (stat(driver.c_str(), &st) != 0) {
        std::fprintf(stderr, "  missing %s\n", driver.c_str());
        return;
    }
    const std::string dir = "/tmp/okayms-driver";
    run_cmd("rm -rf " + dir + " && mkdir -p " + dir + "/libs");
    const auto obj = okayms::write_coff(exit_object());
    const auto lib = okayms::write_archive(kernel32());
    CHECK(tally, obj.ok() && lib.ok());
    CHECK(tally, write_bytes(dir + "/exit.obj", obj.value));
    CHECK(tally, write_bytes(dir + "/libs/kernel32.lib", lib.value));

    const int bad = run_cmd(driver + " /NOTAFLAG:1 /OUT:" + dir + "/x.exe");
    CHECK(tally, bad != 0);

    const std::string out = dir + "/exit.exe";
    const std::string cmd = driver + " /OUT:" + out + " /ENTRY:entry /SUBSYSTEM:CONSOLE /LIBPATH:" + dir +
                            "/libs " + dir + "/exit.obj kernel32.lib";
    const int linked = run_cmd(cmd + " >/tmp/okayms-driver-out.txt 2>/tmp/okayms-driver-err.txt");
    CHECK(tally, linked == 0);
    if (linked != 0) {
        const auto err = read_bytes("/tmp/okayms-driver-err.txt");
        std::fprintf(stderr, "  driver said: %.*s\n", static_cast<int>(err.size()),
                     reinterpret_cast<const char*>(err.data()));
        return;
    }
    const auto image = read_bytes(out);
    CHECK(tally, image.size() > 0x9C);
    CHECK(tally, image.size() > 0x9D && image[0x9C] == 3 && image[0x9D] == 0);

    const std::string gui = dir + "/gui.exe";
    const int gui_rc = run_cmd(driver + " /OUT:" + gui + " /ENTRY:entry /SUBSYSTEM:WINDOWS /LIBPATH:" + dir +
                               "/libs " + dir + "/exit.obj kernel32.lib");
    CHECK(tally, gui_rc == 0);
    const auto gui_image = read_bytes(gui);
    CHECK(tally, gui_image.size() > 0x9D && gui_image[0x9C] == 2);

    const int wine = run_cmd("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 90 wine " + out +
                             " >/tmp/okayms-driver-wine.txt 2>&1");
    CHECK(tally, wine == 42);
    if (wine != 42) std::fprintf(stderr, "  wine exit %d\n", wine);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Tally tally;
    test_driver(tally, root + "/tools/okayms/build/okay-link");
    return tally.finish(1);
}
