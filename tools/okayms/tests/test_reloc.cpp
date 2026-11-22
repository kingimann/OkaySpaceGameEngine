// AMD64 relocations from the PE Format document.
// ADDR64 adds the 64-bit virtual address. ADDR32NB adds the address
// without the image base. The addend already stored in the section is kept.

#include "check.hpp"
#include "okayms/link.hpp"

#include <cstring>

namespace {

using okayms_test::Tally;

okayms::ObjectFile image_with_data(std::vector<uint8_t> data, uint16_t reloc_type) {
    okayms::ObjectFile object;
    okayms::Section text;
    text.name = ".text";
    text.virtual_size = 1;
    text.data = {0xC3};
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead | okayms::kScnAlign16;
    object.sections.push_back(text);
    okayms::Section raw;
    raw.name = ".data";
    raw.virtual_size = static_cast<uint32_t>(data.size());
    raw.characteristics = okayms::kScnCntInitData | okayms::kScnMemRead | okayms::kScnMemWrite;
    okayms::Relocation reloc;
    reloc.offset = 0;
    reloc.symbol = 0;
    reloc.type = reloc_type;
    raw.relocations.push_back(reloc);
    raw.data = std::move(data);
    object.sections.push_back(raw);
    okayms::Symbol entry;
    entry.name = "entry";
    entry.section = 1;
    entry.type = 0x20;
    entry.storage = okayms::kClassExternal;
    object.symbols.push_back(entry);
    return object;
}

void test_addr64(Tally& tally) {
    std::printf("test_addr64\n");
    const auto linked = okayms::link_executable(
        {image_with_data(std::vector<uint8_t>(8, 0), okayms::kRelAmd64Addr64)}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  link error: %s\n", linked.error.c_str());
        return;
    }
    // Image base 0x140000000 plus the entry RVA 0x1000.
    const uint8_t expect[] = {0x00, 0x10, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00};
    CHECK(tally, std::memcmp(linked.value.data() + 0x400, expect, sizeof expect) == 0);
}

void test_addr32nb(Tally& tally) {
    std::printf("test_addr32nb\n");
    // Addend 0x10 plus RVA 0x1000 is 0x1010.
    const auto linked = okayms::link_executable(
        {image_with_data({0x10, 0x00, 0x00, 0x00}, okayms::kRelAmd64Addr32Nb)}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  link error: %s\n", linked.error.c_str());
        return;
    }
    const uint8_t expect[] = {0x10, 0x10, 0x00, 0x00};
    CHECK(tally, std::memcmp(linked.value.data() + 0x400, expect, sizeof expect) == 0);
}

void test_addr32_does_not_fit(Tally& tally) {
    std::printf("test_addr32_does_not_fit\n");
    const auto linked = okayms::link_executable(
        {image_with_data({0, 0, 0, 0}, okayms::kRelAmd64Addr32)}, {});
    CHECK(tally, !linked.ok());
    if (!linked.ok()) CHECK(tally, linked.error.find("ADDR32") != std::string::npos);
}

}  // namespace

int main() {
    Tally tally;
    test_addr64(tally);
    test_addr32nb(tally);
    test_addr32_does_not_fit(tally);
    return tally.finish(3);
}
