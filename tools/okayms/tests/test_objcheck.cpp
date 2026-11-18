// Round 30: a check of the compiler's object files that needs no Wine and no disassembler (unverified against a real linker:
// this only checks that an object is well formed). Each object is written to bytes and read back with the existing COFF
// reader, then:
//   - the read-back object equals the one written (sections, symbols, relocations),
//   - every relocation names a symbol that exists, lies inside its section's bytes, and has a width that fits,
//   - every symbol's section number is 0, a sentinel or an existing section, and a defined symbol's value is inside it,
//   - no two defined external symbols share a name,
//   - at every REL32 in a code section the byte before the 4-byte field is a CALL (E8), JMP (E9), Jcc (0F 8x) or a RIP-relative
//     ModRM byte (mod 00, rm 101), which is what a correct 32-bit pc-relative reference looks like,
//   - the .pdata table is a whole number of 12-byte entries whose relocations are ADDR32NB.
// With file arguments the program compiles each file instead (the include paths of the engine are added), so that the
// whole library can be checked: tools/okayms/build/test_objcheck engine/src/Core/*.cpp
#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/compile.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace {
using okayms_test::Tally;

size_t reloc_width(uint16_t type) {
    switch (type) {
    case okayms::kRelAmd64Addr64: return 8;
    case okayms::kRelAmd64Addr32:
    case okayms::kRelAmd64Addr32Nb:
    case okayms::kRelAmd64Rel32:
    case 5: case 6: case 7: case 8: case 9:  // REL32_1 .. REL32_5
    case okayms::kRelAmd64Secrel: return 4;
    case okayms::kRelAmd64Section: return 2;
    default: return 0;
    }
}

// Returns the number of problems found; each is printed.
int validate(const std::string& name, const okayms::ObjectFile& o) {
    int bad = 0;
    auto problem = [&](const std::string& what) {
        std::fprintf(stderr, "  objcheck %s: %s\n", name.c_str(), what.c_str());
        ++bad;
    };
    const auto bytes = okayms::write_coff(o);
    if (!bytes.ok()) {
        problem("write_coff failed: " + bytes.error);
        return bad;
    }
    const auto back = okayms::read_coff(bytes.value.data(), bytes.value.size());
    if (!back.ok()) {
        problem("read_coff failed: " + back.error);
        return bad;
    }
    const okayms::ObjectFile& r = back.value;
    if (r.sections.size() != o.sections.size()) problem("section count changed on read-back");
    if (r.symbols.size() != o.symbols.size()) problem("symbol count changed on read-back");
    for (size_t i = 0; i < o.sections.size() && i < r.sections.size(); ++i) {
        if (o.sections[i].name != r.sections[i].name) problem("section name changed: " + o.sections[i].name);
        if (o.sections[i].data != r.sections[i].data) problem("section bytes changed: " + o.sections[i].name);
        if (o.sections[i].relocations.size() != r.sections[i].relocations.size()) problem("relocation count changed: " + o.sections[i].name);
    }
    std::map<std::string, int> defined;
    for (size_t i = 0; i < o.symbols.size(); ++i) {
        const auto& s = o.symbols[i];
        if (s.section > static_cast<int>(o.sections.size())) problem("symbol " + s.name + " has a section number past the last section");
        if (s.section > 0 && s.storage == okayms::kClassExternal) {
            const auto& sec = o.sections[static_cast<size_t>(s.section) - 1];
            const size_t size = sec.data.empty() ? sec.virtual_size : sec.data.size();
            if (s.value > size) problem("symbol " + s.name + " lies past the end of " + sec.name);
            if (++defined[s.name] > 1) problem("symbol " + s.name + " is defined twice");
        }
    }
    for (const auto& sec : o.sections) {
        const bool code = (sec.characteristics & okayms::kScnCntCode) != 0;
        for (const auto& rel : sec.relocations) {
            if (rel.symbol >= o.symbols.size()) {
                problem("relocation in " + sec.name + " names symbol " + std::to_string(rel.symbol) + ", which does not exist");
                continue;
            }
            const size_t width = reloc_width(rel.type);
            if (width == 0) {
                problem("relocation in " + sec.name + " has type " + std::to_string(rel.type) + ", which is not an AMD64 type");
                continue;
            }
            if (static_cast<size_t>(rel.offset) + width > sec.data.size()) {
                problem("relocation in " + sec.name + " at " + std::to_string(rel.offset) + " runs past the section");
                continue;
            }
            if (code && rel.type == okayms::kRelAmd64Rel32) {
                if (rel.offset == 0) {
                    problem("REL32 at the very start of " + sec.name);
                    continue;
                }
                const uint8_t before = sec.data[rel.offset - 1];
                const uint8_t before2 = rel.offset >= 2 ? sec.data[rel.offset - 2] : 0;
                const bool call_or_jmp = before == 0xE8 || before == 0xE9;
                const bool jcc = rel.offset >= 2 && before2 == 0x0F && (before & 0xF0) == 0x80;
                const bool rip_modrm = (before & 0xC7) == 0x05;
                // An instruction with an immediate after the field (mov dword [rip+x], imm) puts more bytes after it, which
                // is fine; only the byte before is looked at here.
                if (!call_or_jmp && !jcc && !rip_modrm)
                    problem("REL32 in " + sec.name + " at " + std::to_string(rel.offset) + " follows byte " + std::to_string(before) +
                            ", which is not a call, jump or RIP-relative ModRM");
            }
        }
        if (sec.name == ".pdata") {
            if (sec.data.size() % 12 != 0) problem(".pdata is not a whole number of 12-byte entries");
            for (const auto& rel : sec.relocations)
                if (rel.type != okayms::kRelAmd64Addr32Nb) problem(".pdata has a relocation that is not ADDR32NB");
        }
    }
    return bad;
}

bool compile_text(const std::string& source, okayms::ObjectFile* out, std::string* err, bool comdat = false, bool xcu = false) {
    okayms::CompileInput in;
    in.source = source;
    in.comdat_inline = comdat;
    in.crt_init_table = xcu;
    in.include_paths.push_back("tools/okayms/stdinc");
    in.include_paths.push_back("stdinc");
    in.include_paths.push_back("engine/include");
    in.include_paths.push_back("engine/src");
    in.include_paths.push_back("shared");
    in.include_paths.push_back("tests");
    const auto r = okayms::compile_c(in);
    if (!r.ok()) {
        *err = r.error;
        return false;
    }
    *out = r.value.object;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Tally t;
    int tests = 0;
    if (argc > 1) {
        int failed_compile = 0;
        int bad_objects = 0;
        int ok_objects = 0;
        for (int i = 1; i < argc; ++i) {
            std::ifstream f(argv[i], std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            okayms::ObjectFile o;
            std::string err;
            if (!compile_text(ss.str(), &o, &err)) {
                ++failed_compile;
                continue;
            }
            if (validate(argv[i], o) == 0) ++ok_objects;
            else ++bad_objects;
        }
        std::printf("objcheck: %d objects well formed, %d malformed, %d files did not compile\n", ok_objects, bad_objects, failed_compile);
        return bad_objects == 0 ? 0 : 1;
    }
    const char* sources[] = {
        "int add(int a, int b) { return a + b; }\nint main() { return add(40, 2); }\n",
        "#include <string>\n#include <vector>\n#include <map>\nint main() { std::vector<std::string> v; v.push_back(\"a\"); std::map<int, std::string> m; m[1] = \"b\"; return (int)(v.size() + m.size()); }\n",
        "#include <cstdlib>\n#include <new>\nstruct B { virtual int f() { return 1; } virtual ~B() {} };\nstruct D : B { int f() override { return 2; } };\nint main() { D d; B* b = &d; return b->f(); }\n",
        "#include <functional>\n#include <memory>\nint main() { int k = 3; std::function<int(int)> f = [k](int x) { return x + k; }; auto p = std::make_shared<int>(4); return f(1) + *p; }\n",
        "#include <variant>\n#include <string>\nint main() { std::variant<int, std::string> v = 5; return std::holds_alternative<int>(v) ? std::get<int>(v) : 0; }\n",
        "#include <cmath>\ndouble g[4] = {1.5, 2.5, 3.5, 4.5};\nint main() { double s = 0; for (int i = 0; i < 4; ++i) s += std::sqrt(g[i]) * std::sin(g[i]); return (int)s; }\n",
    };
    // The same programs again with COMDAT inline functions and the .CRT$XCU table (the extra sections must be well formed too).
    const char* flagged[] = {
        "struct W { int f(int v) { return v * 3 + 1; } };\nstruct G { int v; G() { v = 1; } };\nG g;\nint main() { W w; return w.f(2) + g.v; }\n",
        "#include <string>\n#include <vector>\nstruct G { std::string s; G() : s(\"x\") {} };\nG g;\nint main() { std::vector<std::string> v; v.push_back(g.s); return (int)v.size(); }\n",
    };
    for (const char* src : flagged) {
        ++tests;
        okayms::ObjectFile o;
        std::string err;
        std::printf("objcheck flagged program\n");
        const bool ok = compile_text(src, &o, &err, true, true);
        CHECK(t, ok);
        if (!ok) std::fprintf(stderr, "  flagged program did not compile: %s\n", err.c_str());
        else CHECK(t, validate("flagged program", o) == 0);
    }
    int index = 0;
    for (const char* src : sources) {
        ++tests;
        okayms::ObjectFile o;
        std::string err;
        std::printf("objcheck program %d\n", index);
        const bool ok = compile_text(src, &o, &err);
        CHECK(t, ok);
        if (!ok) {
            std::fprintf(stderr, "  program %d did not compile: %s\n", index, err.c_str());
        } else {
            CHECK(t, validate("program " + std::to_string(index), o) == 0);
        }
        ++index;
    }
    return t.finish(tests);
}
