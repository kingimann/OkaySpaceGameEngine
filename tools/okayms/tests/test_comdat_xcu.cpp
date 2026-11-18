// Round 32: the compiler's COMDAT inline functions (/okay:comdat) and the .CRT$XCU table of global constructors (/okay:xcu), checked
// WITHOUT running anything (unverified: not run under Wine): the objects are inspected, linked, and the linked image's section
// table is read. COMDAT: two files that both define the same inline function leave one copy in the image (the .text of the
// image is smaller than when each file keeps its own copy). XCU: each file with a global constructor leaves one .CRT$XCU pointer
// with an ADDR64 relocation to its __okay_init, and the linker's .CRT section holds one pointer per such file.
#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <string>
#include <vector>

namespace {
using okayms_test::Tally;

bool compile(const std::string& src, bool comdat, bool xcu, okayms::ObjectFile* out, const char* name) {
    okayms::CompileInput in;
    in.source = src;
    in.name = name;
    in.comdat_inline = comdat;
    in.crt_init_table = xcu;
    const auto r = okayms::compile_c(in);
    if (!r.ok()) {
        std::fprintf(stderr, "  compile of %s failed: %s\n", name, r.error.c_str());
        return false;
    }
    *out = r.value.object;
    return true;
}

uint32_t rd32(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) | (static_cast<uint32_t>(b[at + 2]) << 16) |
           (static_cast<uint32_t>(b[at + 3]) << 24);
}

// The VirtualSize of the image section with this name, or -1.
long long image_section_size(const std::vector<uint8_t>& img, const std::string& name) {
    if (img.size() < 0x100) return -1;
    const size_t pe = rd32(img, 0x3C);
    const size_t count = img[pe + 6] | (img[pe + 7] << 8);
    const size_t opt = img[pe + 20] | (img[pe + 21] << 8);
    size_t sh = pe + 24 + opt;
    for (size_t i = 0; i < count; ++i, sh += 40) {
        std::string n(reinterpret_cast<const char*>(&img[sh]), 8);
        n = n.substr(0, n.find('\0'));
        if (n == name) return rd32(img, sh + 8);
    }
    return -1;
}
}  // namespace

int main() {
    Tally t;
    int tests = 0;
    const std::string inline_a =
        "inline int sq(int x) { int a = x * x; int b = a + x; int c = b - x; return c + a - a; }\nstruct W { int f(int v) { return v * 3 + 1; } };\n"
        "int main() { W w; return sq(3) + w.f(2); }\n";
    const std::string inline_b =
        "inline int sq(int x) { int a = x * x; int b = a + x; int c = b - x; return c + a - a; }\nstruct W { int f(int v) { return v * 3 + 1; } };\n"
        "int other() { W w; return sq(4) + w.f(5); }\n";
    ++tests;
    {
        okayms::ObjectFile a, b, a0, b0;
        const bool ok = compile(inline_a, true, false, &a, "a.cpp") && compile(inline_b, true, false, &b, "b.cpp") &&
                        compile(inline_a, false, false, &a0, "a.cpp") && compile(inline_b, false, false, &b0, "b.cpp");
        CHECK(t, ok);
        if (ok) {
            size_t comdat_sections = 0;
            for (const auto& s : a.sections)
                if (s.name == ".text$mn" && (s.characteristics & okayms::kScnLnkComdat) != 0) ++comdat_sections;
            CHECK(t, comdat_sections >= 1);  // W::f (a free inline function keeps its internal copy: it has no decorated name)
            okayms::LinkOptions opt;
            opt.entry = "main";
            const auto with = okayms::link_executable({a, b}, opt);
            const auto without = okayms::link_executable({a0, b0}, opt);
            CHECK(t, with.ok());
            CHECK(t, without.ok());
            if (!with.ok()) std::fprintf(stderr, "  link with COMDAT: %s\n", with.error.c_str());
            if (with.ok() && without.ok()) {
                const long long sw = image_section_size(with.value, ".text");
                const long long so = image_section_size(without.value, ".text");
                std::printf("  .text with COMDAT %lld bytes, without %lld bytes\n", sw, so);
                CHECK(t, sw > 0 && so > 0 && sw < so);
            }
        }
    }
    ++tests;
    {
        const std::string ctor_a = "struct G { int v; G() { v = 1; } };\nG ga;\nextern char __okay_xcu_begin;\nextern char __okay_xcu_end;\nint main() { return (int)(&__okay_xcu_end - &__okay_xcu_begin) + ga.v; }\n";
        const std::string ctor_b = "struct H { int v; H() { v = 2; } };\nH hb;\nint other() { return hb.v; }\n";
        okayms::ObjectFile a, b;
        const bool ok = compile(ctor_a, false, true, &a, "a.cpp") && compile(ctor_b, false, true, &b, "b.cpp");
        CHECK(t, ok);
        if (ok) {
            size_t tables = 0;
            bool reloc_ok = false;
            for (const auto& s : b.sections)
                if (s.name == ".CRT$XCU") {
                    ++tables;
                    reloc_ok = s.data.size() == 8 && s.relocations.size() == 1 && s.relocations[0].type == okayms::kRelAmd64Addr64 &&
                               s.relocations[0].symbol < b.symbols.size() && b.symbols[s.relocations[0].symbol].name == "__okay_init";
                }
            CHECK(t, tables == 1 && reloc_ok);
            bool init_static = false;
            for (const auto& sym : b.symbols)
                if (sym.name == "__okay_init" && sym.storage == okayms::kClassStatic) init_static = true;
            CHECK(t, init_static);
            okayms::LinkOptions opt;
            opt.entry = "main";
            const auto img = okayms::link_executable({a, b}, opt);
            CHECK(t, img.ok());
            if (!img.ok()) std::fprintf(stderr, "  link with XCU: %s\n", img.error.c_str());
            else CHECK(t, image_section_size(img.value, ".CRT") == 16);  // two pointers
        }
    }
    return t.finish(tests);
}
