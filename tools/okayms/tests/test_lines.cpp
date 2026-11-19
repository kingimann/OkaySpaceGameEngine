// CodeView line records (/Z7): a .debug$S section with signature 4, one
// DEBUG_S_LINES subsection per function (code range named by SECREL and
// SECTION relocations), a file checksum table and a string table. The test
// reads the bytes back with its own small parser, checks the line numbers
// against the source, links the object and runs it under Wine.

#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <fstream>
#include <map>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

uint32_t u32at(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24));
}

int run_shell(const std::string& cmd) {
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

struct Block {
    std::string file;
    std::vector<std::pair<uint32_t, uint32_t>> lines;  // offset, line
};
struct Lines {
    uint32_t code_size = 0;
    uint32_t payload_at = 0;
    std::vector<Block> blocks;
};
struct Parsed {
    bool ok = false;
    std::vector<Lines> functions;
};

Parsed parse_cv(const std::vector<uint8_t>& data) {
    Parsed out;
    if (data.size() < 4 || u32at(data, 0) != 4) return out;
    std::vector<uint8_t> strings;
    std::vector<uint8_t> sums;
    struct Raw {
        size_t at;
        size_t len;
    };
    std::vector<Raw> lines;
    size_t at = 4;
    while (at + 8 <= data.size()) {
        const uint32_t kind = u32at(data, at);
        const uint32_t len = u32at(data, at + 4);
        at += 8;
        if (at + len > data.size()) return out;
        if (kind == 0xF3) strings.assign(data.begin() + static_cast<long>(at), data.begin() + static_cast<long>(at + len));
        else if (kind == 0xF4) sums.assign(data.begin() + static_cast<long>(at), data.begin() + static_cast<long>(at + len));
        else if (kind == 0xF2) lines.push_back({at, len});
        else return out;
        at += (len + 3u) & ~3u;
    }
    if (at != data.size()) return out;
    for (const Raw& raw : lines) {
        Lines fn;
        fn.payload_at = static_cast<uint32_t>(raw.at);
        fn.code_size = u32at(data, raw.at + 8);
        size_t p = raw.at + 12;
        const size_t end = raw.at + raw.len;
        while (p + 12 <= end) {
            const uint32_t file_id = u32at(data, p);
            const uint32_t count = u32at(data, p + 4);
            const uint32_t block_size = u32at(data, p + 8);
            if (block_size != 12 + count * 8 || p + block_size > end) return out;
            Block block;
            if (file_id + 8 > sums.size()) return out;
            const uint32_t name_at = u32at(sums, file_id);
            if (name_at >= strings.size()) return out;
            block.file = reinterpret_cast<const char*>(strings.data() + name_at);
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t offset = u32at(data, p + 12 + i * 8);
                const uint32_t packed = u32at(data, p + 16 + i * 8);
                // Statement bit set, no extra lines.
                if ((packed & 0xFF000000u) != 0x80000000u) return out;
                block.lines.push_back({offset, packed & 0x00FFFFFFu});
            }
            fn.blocks.push_back(block);
            p += block_size;
        }
        if (p != end) return out;
        out.functions.push_back(fn);
    }
    out.ok = true;
    return out;
}

std::vector<uint32_t> line_numbers(const Lines& fn, const std::string& file) {
    std::vector<uint32_t> numbers;
    for (const Block& block : fn.blocks)
        if (block.file == file)
            for (const auto& item : block.lines) numbers.push_back(item.second);
    return numbers;
}

}  // namespace

static void test_line_table(Tally& tally) {
    std::printf("test_line_table\n");
    run_shell("mkdir -p /tmp/okayms-lines");
    {
        std::ofstream header("/tmp/okayms-lines/twice.h");
        header << "int twice(int v) {\n"
                  "  return v + v;\n"
                  "}\n";
    }
    okayms::CompileInput in;
    in.name = "lines.c";
    in.include_paths.push_back("/tmp/okayms-lines");
    in.debug_lines = true;
    in.source =
        "int sq(int x) {\n"
        "  int y;\n"
        "  y = x * x;\n"
        "  /* a comment\n"
        "     over two lines */\n"
        "  return y;\n"
        "}\n"
        "#include \"twice.h\"\n"
        "int entry(void) {\n"
        "  int a;\n"
        "  a = sq(3);\n"
        "  a = twice(a);\n"
        "  return a + 1;\n"
        "}\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    const okayms::Section* debug = nullptr;
    const okayms::Section* text = nullptr;
    for (const auto& section : compiled.value.object.sections) {
        if (section.name == ".debug$S") debug = &section;
        if (section.name == ".text") text = &section;
    }
    CHECK(tally, debug != nullptr);
    CHECK(tally, text != nullptr);
    if (debug == nullptr || text == nullptr) return;
    CHECK(tally, (debug->characteristics & 0x02000000u) != 0);
    const Parsed parsed = parse_cv(debug->data);
    CHECK(tally, parsed.ok);
    CHECK(tally, parsed.functions.size() == 3);
    if (!parsed.ok || parsed.functions.size() != 3) return;
    // Functions come in source order: sq, twice, entry.
    CHECK(tally, line_numbers(parsed.functions[0], "lines.c") == (std::vector<uint32_t>{1, 3, 6, 7}));
    CHECK(tally, line_numbers(parsed.functions[1], "twice.h") == (std::vector<uint32_t>{1, 2, 3}));
    CHECK(tally, line_numbers(parsed.functions[2], "lines.c") == (std::vector<uint32_t>{9, 11, 12, 13, 14}));
    uint32_t total = 0;
    for (const Lines& fn : parsed.functions) {
        total += fn.code_size;
        CHECK(tally, fn.code_size > 0);
        CHECK(tally, !fn.blocks.empty() && fn.blocks[0].lines[0].first == 0);
        uint32_t previous = 0;
        bool increasing = true;
        for (const Block& block : fn.blocks)
            for (size_t i = 0; i < block.lines.size(); ++i) {
                if (i > 0 && block.lines[i].first <= previous) increasing = false;
                if (block.lines[i].first >= fn.code_size) increasing = false;
                previous = block.lines[i].first;
            }
        CHECK(tally, increasing);
    }
    CHECK(tally, total <= text->data.size());
    // Two relocations per function: SECREL then SECTION, both on the function symbol.
    CHECK(tally, debug->relocations.size() == 6);
    const char* names[3] = {"sq", "twice", "entry"};
    for (size_t f = 0; f < 3 && debug->relocations.size() == 6; ++f) {
        const auto& secrel = debug->relocations[f * 2];
        const auto& section = debug->relocations[f * 2 + 1];
        CHECK(tally, secrel.type == okayms::kRelAmd64Secrel);
        CHECK(tally, section.type == okayms::kRelAmd64Section);
        CHECK(tally, secrel.offset == parsed.functions[f].payload_at);
        CHECK(tally, section.offset == parsed.functions[f].payload_at + 4);
        CHECK(tally, compiled.value.object.symbols[secrel.symbol].name == names[f]);
        CHECK(tally, secrel.symbol == section.symbol);
    }
    // The object still links, with /DEBUG, and runs: sq(3) = 9, twice = 18, + 1.
    okayms::LinkOptions options;
    options.debug = true;
    const auto linked = okayms::link_executable({compiled.value.object}, options);
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  link: %s\n", linked.error.c_str());
        return;
    }
    {
        std::ofstream out("/tmp/okayms-lines/lines.exe", std::ios::binary);
        out.write(reinterpret_cast<const char*>(linked.value.data()), static_cast<std::streamsize>(linked.value.size()));
    }
    const int code = run_shell("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine /tmp/okayms-lines/lines.exe >/dev/null 2>&1");
    CHECK(tally, code == 19);
    if (code != 19) std::fprintf(stderr, "  wine %d\n", code);
}

static void test_off_by_default(Tally& tally) {
    std::printf("test_off_by_default\n");
    okayms::CompileInput in;
    in.source = "int entry(void) {\n  return 3;\n}\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) return;
    for (const auto& section : compiled.value.object.sections) CHECK(tally, section.name != ".debug$S");
}

static void test_driver_flag(Tally& tally, const std::string& root) {
    std::printf("test_driver_flag\n");
    run_shell("mkdir -p /tmp/okayms-lines");
    {
        std::ofstream src("/tmp/okayms-lines/drv.c");
        src << "int entry(void) {\n  int a;\n  a = 4;\n  return a;\n}\n";
    }
    const std::string cl = root + "/tools/okayms/build/okay-cl";
    CHECK(tally, run_shell(cl + " /c /Z7 /Fo/tmp/okayms-lines/drv.obj /tmp/okayms-lines/drv.c >/tmp/okayms-lines/drv.log 2>&1") == 0);
    std::ifstream in("/tmp/okayms-lines/drv.obj", std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto object = okayms::read_coff(bytes.data(), bytes.size());
    CHECK(tally, object.ok());
    if (!object.ok()) return;
    bool found = false;
    for (const auto& section : object.value.sections) {
        if (section.name != ".debug$S") continue;
        found = true;
        const Parsed parsed = parse_cv(section.data);
        CHECK(tally, parsed.ok && parsed.functions.size() == 1);
        if (parsed.ok && parsed.functions.size() == 1)
            CHECK(tally, line_numbers(parsed.functions[0], "/tmp/okayms-lines/drv.c") == (std::vector<uint32_t>{1, 3, 4, 5}));
    }
    CHECK(tally, found);
    // The linker takes the object with /DEBUG and writes an image.
    CHECK(tally, run_shell(root + "/tools/okayms/build/okay-link /DEBUG /ENTRY:entry /OUT:/tmp/okayms-lines/drv.exe /tmp/okayms-lines/drv.obj >/tmp/okayms-lines/link.log 2>&1") == 0);
    CHECK(tally, run_shell("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine /tmp/okayms-lines/drv.exe >/dev/null 2>&1") == 4);
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Tally tally;
    test_line_table(tally);
    test_off_by_default(tally);
    test_driver_flag(tally, root);
    return tally.finish(3);
}
