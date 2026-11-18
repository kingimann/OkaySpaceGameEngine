// COMDAT pick-any (selection 2) and same-size (selection 3), and a weak
// external that uses its auxiliary symbol when no strong definition exists.
// Written from the PE Format document's COMDAT section and weak-external record.

#include "check.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <vector>

namespace {

using okayms::ObjectFile;
using okayms::Relocation;
using okayms::Section;
using okayms::Symbol;
using okayms_test::Tally;

bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

int run(const std::string& cmd, const std::string& out_path) {
    const int rc = std::system((cmd + " >" + out_path + " 2>&1").c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

ObjectFile function_object(const std::string& name, const std::vector<uint8_t>& code, uint8_t selection) {
    ObjectFile obj;
    Section text;
    text.name = ".text";
    text.virtual_size = static_cast<uint32_t>(code.size());
    text.data = code;
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead | okayms::kScnAlign16;
    if (selection != 0) text.characteristics |= okayms::kScnLnkComdat;
    obj.sections.push_back(text);
    if (selection != 0) {
        Symbol sec;
        sec.name = ".text";
        sec.section = 1;
        sec.storage = okayms::kClassStatic;
        sec.aux.assign(18, 0);
        sec.aux[14] = selection;
        obj.symbols.push_back(sec);
    }
    Symbol fn;
    fn.name = name;
    fn.section = 1;
    fn.type = 0x20;
    fn.storage = okayms::kClassExternal;
    obj.symbols.push_back(fn);
    return obj;
}

ObjectFile caller(const std::string& name) {
    ObjectFile obj;
    Section text;
    text.name = ".text";
    text.virtual_size = 6;
    text.data = {0xE8, 0, 0, 0, 0, 0xC3};
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead | okayms::kScnAlign16;
    Relocation rel;
    rel.offset = 1;
    rel.symbol = 0;
    rel.type = okayms::kRelAmd64Rel32;
    text.relocations.push_back(rel);
    obj.sections.push_back(text);
    Symbol target;
    target.name = name;
    target.storage = okayms::kClassExternal;
    obj.symbols.push_back(target);
    Symbol entry;
    entry.name = "entry";
    entry.section = 1;
    entry.type = 0x20;
    entry.storage = okayms::kClassExternal;
    obj.symbols.push_back(entry);
    return obj;
}

int wine_exit(const std::string& path) {
    return run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + path, path + ".wine.txt");
}

const std::vector<uint8_t> kRet5 = {0xB8, 5, 0, 0, 0, 0xC3};
const std::vector<uint8_t> kRet5Long = {0xB8, 5, 0, 0, 0, 0x90, 0xC3};

}  // namespace

static void test_pick_any(Tally& tally) {
    std::printf("test_pick_any\n");
    const auto a = function_object("pick", kRet5, 2);
    const auto b = function_object("pick", kRet5, 2);
    const auto image = okayms::link_executable({caller("pick"), a, b}, {});
    CHECK(tally, image.ok());
    if (!image.ok()) {
        std::fprintf(stderr, "  link error: %s\n", image.error.c_str());
        return;
    }
    CHECK(tally, write_bytes("/tmp/okayms-comdat/pick.exe", image.value));
    CHECK(tally, wine_exit("/tmp/okayms-comdat/pick.exe") == 5);
}

static void test_same_size(Tally& tally) {
    std::printf("test_same_size\n");
    const auto a = function_object("pick", kRet5, 3);
    const auto b = function_object("pick", kRet5, 3);
    const auto image = okayms::link_executable({caller("pick"), a, b}, {});
    CHECK(tally, image.ok());
    if (!image.ok()) {
        std::fprintf(stderr, "  link error: %s\n", image.error.c_str());
        return;
    }
    CHECK(tally, write_bytes("/tmp/okayms-comdat/same.exe", image.value));
    CHECK(tally, wine_exit("/tmp/okayms-comdat/same.exe") == 5);

    const auto longer = function_object("pick", kRet5Long, 3);
    const auto rejected = okayms::link_executable({caller("pick"), a, longer}, {});
    CHECK(tally, !rejected.ok());
    CHECK(tally, !rejected.ok() && rejected.error.find("size") != std::string::npos);
}

static void test_weak_fallback(Tally& tally) {
    std::printf("test_weak_fallback\n");
    ObjectFile weak;
    Section text;
    text.name = ".text";
    text.virtual_size = 6;
    text.data = {0xB8, 7, 0, 0, 0, 0xC3};
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead | okayms::kScnAlign16;
    weak.sections.push_back(text);
    Symbol foo;
    foo.name = "foo";
    foo.storage = okayms::kClassWeakExternal;
    foo.aux.assign(18, 0);
    foo.aux[0] = 2;
    foo.aux[4] = 1;
    weak.symbols.push_back(foo);
    Symbol bar;
    bar.name = "bar";
    bar.section = 1;
    bar.type = 0x20;
    bar.storage = okayms::kClassExternal;
    weak.symbols.push_back(bar);

    const auto image = okayms::link_executable({caller("foo"), weak}, {});
    CHECK(tally, image.ok());
    if (!image.ok()) {
        std::fprintf(stderr, "  link error: %s\n", image.error.c_str());
        return;
    }
    CHECK(tally, write_bytes("/tmp/okayms-comdat/weak.exe", image.value));
    CHECK(tally, wine_exit("/tmp/okayms-comdat/weak.exe") == 7);

    const auto strong = function_object("foo", {0xB8, 9, 0, 0, 0, 0xC3}, 0);
    const auto replaced = okayms::link_executable({caller("foo"), weak, strong}, {});
    CHECK(tally, replaced.ok());
    if (!replaced.ok()) {
        std::fprintf(stderr, "  link error: %s\n", replaced.error.c_str());
        return;
    }
    CHECK(tally, write_bytes("/tmp/okayms-comdat/strong.exe", replaced.value));
    CHECK(tally, wine_exit("/tmp/okayms-comdat/strong.exe") == 9);
}

int main() {
    std::system("mkdir -p /tmp/okayms-comdat");
    Tally tally;
    test_pick_any(tally);
    test_same_size(tally);
    test_weak_fallback(tally);
    return tally.finish(3);
}
