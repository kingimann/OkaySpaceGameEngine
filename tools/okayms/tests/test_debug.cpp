// /DEBUG adds one IMAGE_DEBUG_DIRECTORY of type CodeView (2) and an RSDS
// record. There is no PDB file. Written from the PE Format document.

#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/link.hpp"

#include <cstring>
#include <fstream>
#include <string>
#include <sys/wait.h>

namespace {

using okayms_test::Tally;

uint32_t u32at(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24));
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

bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

int run(const std::string& cmd) {
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

void check_debug(Tally& tally, const std::vector<uint8_t>& image) {
    CHECK(tally, image.size() > 0xF8 + 8);
    const uint32_t dir_rva = u32at(image, 0xC8 + 48);
    const uint32_t dir_size = u32at(image, 0xC8 + 52);
    CHECK(tally, dir_rva != 0);
    CHECK(tally, dir_size == 28);
    const uint16_t nsec = static_cast<uint16_t>(image[0x46] | (image[0x47] << 8));
    bool found = false;
    for (uint16_t i = 0; i < nsec; ++i) {
        const size_t at = 0x148 + static_cast<size_t>(i) * 40u;
        if (std::memcmp(image.data() + at, ".debug", 6) != 0) continue;
        const uint32_t raw = u32at(image, at + 20);
        const uint32_t rva = u32at(image, at + 12);
        CHECK(tally, rva == dir_rva);
        CHECK(tally, u32at(image, raw + 12) == 2);
        CHECK(tally, u32at(image, raw + 16) == 33);
        CHECK(tally, u32at(image, raw + 20) == rva + 28);
        CHECK(tally, std::memcmp(image.data() + raw + 28, "RSDS", 4) == 0);
        CHECK(tally, u32at(image, raw + 28 + 20) == 1);
        CHECK(tally, std::memcmp(image.data() + raw + 28 + 24, "okay.pdb", 8) == 0);
        found = true;
    }
    CHECK(tally, found);
}

}  // namespace

static void test_codeview_directory(Tally& tally) {
    std::printf("test_codeview_directory\n");
    okayms::LinkOptions options;
    options.debug = true;
    const auto image = okayms::link_executable({ret_object()}, options);
    CHECK(tally, image.ok());
    if (!image.ok()) {
        std::fprintf(stderr, "  link: %s\n", image.error.c_str());
        return;
    }
    check_debug(tally, image.value);
}

static void test_debug_flag(Tally& tally, const std::string& link) {
    std::printf("test_debug_flag\n");
    const auto bytes = okayms::write_coff(ret_object());
    CHECK(tally, bytes.ok());
    if (!bytes.ok()) return;
    CHECK(tally, write_bytes("/tmp/okayms-debug/ret.obj", bytes.value));
    const int rc = run(link + " /DEBUG /OUT:/tmp/okayms-debug/ret.exe /tmp/okayms-debug/ret.obj > /tmp/okayms-debug/log.txt 2>&1");
    CHECK(tally, rc == 0);
    std::ifstream in("/tmp/okayms-debug/ret.exe", std::ios::binary);
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check_debug(tally, image);
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    std::system("mkdir -p /tmp/okayms-debug");
    Tally tally;
    test_codeview_directory(tally);
    test_debug_flag(tally, root + "/tools/okayms/build/okay-link");
    return tally.finish(2);
}
