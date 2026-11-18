// /MAP writes a text file with the preferred base, section RVAs, public
// symbols and the entry point. The layout of that text is ours; the flag
// name comes from the linker option list.

#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <sys/wait.h>

namespace {

using okayms_test::Tally;

bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

std::string slurp(const std::string& path) {
    std::ifstream in(path.c_str());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int run(const std::string& cmd) {
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

okayms::ObjectFile ret_object() {
    okayms::ObjectFile object;
    okayms::Section text;
    text.name = ".text";
    text.data = {0xB8, 7, 0, 0, 0, 0xC3};
    text.virtual_size = 6;
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead;
    object.sections.push_back(text);
    okayms::Symbol entry;
    entry.name = "entry";
    entry.section = 1;
    entry.type = 0x20;
    entry.storage = okayms::kClassExternal;
    object.symbols.push_back(entry);
    return object;
}

}  // namespace

static void test_map_text(Tally& tally) {
    std::printf("test_map_text\n");
    std::string map;
    const auto image = okayms::link_executable({ret_object()}, {}, &map);
    CHECK(tally, image.ok());
    CHECK(tally, map.find("base 0000000140000000\n") != std::string::npos);
    CHECK(tally, map.find("section .text 00001000\n") != std::string::npos);
    CHECK(tally, map.find("public entry 00001000\n") != std::string::npos);
    CHECK(tally, map.find("entry 00001000\n") != std::string::npos);
}

static void test_map_flag(Tally& tally, const std::string& link) {
    std::printf("test_map_flag\n");
    const auto bytes = okayms::write_coff(ret_object());
    CHECK(tally, bytes.ok());
    if (!bytes.ok()) return;
    CHECK(tally, write_bytes("/tmp/okayms-map/ret.obj", bytes.value));
    const int rc = run(link + " /OUT:/tmp/okayms-map/ret.exe /MAP:/tmp/okayms-map/ret.map /tmp/okayms-map/ret.obj > /tmp/okayms-map/log.txt 2>&1");
    CHECK(tally, rc == 0);
    const std::string map = slurp("/tmp/okayms-map/ret.map");
    CHECK(tally, map.find("public entry 00001000\n") != std::string::npos);
    if (map.find("public entry") == std::string::npos) std::fprintf(stderr, "  map: %s\n", map.c_str());
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    std::system("mkdir -p /tmp/okayms-map");
    Tally tally;
    test_map_text(tally);
    test_map_flag(tally, root + "/tools/okayms/build/okay-link");
    return tally.finish(2);
}
