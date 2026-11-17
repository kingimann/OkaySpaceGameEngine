// .pdata is three RVAs (begin, end, unwind info). .xdata is the UNWIND_INFO
// for push rbp / mov rbp, rsp / sub rsp, 0x20. The exception directory is
// data directory 3.

#include "check.hpp"
#include "okayms/link.hpp"
#include "okayms/unwind.hpp"

#include <cstring>

namespace {

using okayms_test::Tally;

uint32_t u32at(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24));
}

void test_encode(Tally& tally) {
    std::printf("test_encode\n");
    const auto bytes = okayms::encode_frame_unwind(0x20);
    CHECK(tally, bytes.ok());
    if (!bytes.ok()) {
        std::fprintf(stderr, "  unwind error: %s\n", bytes.error.c_str());
        return;
    }
    // version 1, prolog 8, three codes, frame rbp.
    // Codes are in reverse: alloc 0x20, set fp, push rbp, then a pad slot.
    const uint8_t expect[] = {0x01, 0x08, 0x03, 0x05, 0x08, 0x32, 0x04, 0x03, 0x01, 0x50, 0x00, 0x00};
    CHECK(tally, bytes.value.size() == sizeof expect);
    CHECK(tally, std::memcmp(bytes.value.data(), expect, sizeof expect) == 0);
    const auto bad = okayms::encode_frame_unwind(7);
    CHECK(tally, !bad.ok());
}

void test_pdata_image(Tally& tally) {
    std::printf("test_pdata_image\n");
    const uint8_t xdata[] = {0x01, 0x08, 0x03, 0x05, 0x08, 0x32, 0x04, 0x03, 0x01, 0x50, 0x00, 0x00};
    okayms::ObjectFile object;
    okayms::Section text;
    text.name = ".text";
    text.virtual_size = 9;
    text.data = {0x55, 0x48, 0x89, 0xE5, 0x48, 0x83, 0xEC, 0x20, 0xC3};
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead;
    object.sections.push_back(text);

    okayms::Section xs;
    xs.name = ".xdata";
    xs.virtual_size = sizeof xdata;
    xs.data.assign(xdata, xdata + sizeof xdata);
    xs.characteristics = okayms::kScnCntInitData | okayms::kScnMemRead;
    object.sections.push_back(xs);

    okayms::Section pdata;
    pdata.name = ".pdata";
    pdata.virtual_size = 12;
    pdata.data.assign(12, 0);
    pdata.characteristics = okayms::kScnCntInitData | okayms::kScnMemRead;
    for (int i = 0; i < 3; ++i) {
        okayms::Relocation reloc;
        reloc.offset = static_cast<uint32_t>(i * 4);
        reloc.symbol = static_cast<uint32_t>(i);
        reloc.type = okayms::kRelAmd64Addr32Nb;
        pdata.relocations.push_back(reloc);
    }
    object.sections.push_back(pdata);

    auto add = [&](const char* name, int16_t section, uint32_t value) {
        okayms::Symbol symbol;
        symbol.name = name;
        symbol.section = section;
        symbol.value = value;
        symbol.storage = okayms::kClassExternal;
        if (section > 0) symbol.type = 0x20;
        object.symbols.push_back(symbol);
    };
    add("entry", 1, 0);
    add("entry_end", 1, 9);
    add("unwind", 2, 0);

    const auto linked = okayms::link_executable({object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  link error: %s\n", linked.error.c_str());
        return;
    }
    const std::vector<uint8_t>& image = linked.value;
    CHECK(tally, std::memcmp(image.data() + 0x200, text.data.data(), text.data.size()) == 0);
    CHECK(tally, std::memcmp(image.data() + 0x400, xdata, sizeof xdata) == 0);
    const uint8_t expect_pdata[] = {
        0x00, 0x10, 0x00, 0x00,
        0x09, 0x10, 0x00, 0x00,
        0x00, 0x20, 0x00, 0x00};
    CHECK(tally, std::memcmp(image.data() + 0x600, expect_pdata, sizeof expect_pdata) == 0);
    // Exception directory: RVA of .pdata and the 12-byte table.
    CHECK(tally, u32at(image, 0xE0) == 0x3000);
    CHECK(tally, u32at(image, 0xE4) == 12);
}

}  // namespace

int main() {
    Tally tally;
    test_encode(tally);
    test_pdata_image(tally);
    return tally.finish(2);
}
