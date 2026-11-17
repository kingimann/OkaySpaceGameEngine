// A namespace with an MSVC decorated name, and a reference that names an
// existing int.

#include "check.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int wine_image(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-cxx-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-cxx-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

}  // namespace

static void test_ns_ref(Tally& tally) {
    std::printf("test_ns_ref\n");
    okayms::CompileInput in;
    in.source =
        "namespace N {\n"
        "  int add(int a, int b) { return a + b; }\n"
        "}\n"
        "int entry(void) {\n"
        "  int x;\n"
        "  int &r = x;\n"
        "  x = 20;\n"
        "  r = r + 1;\n"
        "  return N::add(r, 21);\n"
        "}\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    bool saw = false;
    for (const auto& symbol : compiled.value.object.symbols)
        if (symbol.name == "?add@N@@YAHHH@Z") saw = true;
    CHECK(tally, saw);
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  %s\n", linked.error.c_str());
        return;
    }
    const int code = wine_image(linked.value);
    CHECK(tally, code == 42);
    if (code != 42) std::fprintf(stderr, "  wine %d\n", code);
}

int main() {
    Tally tally;
    test_ns_ref(tally);
    return tally.finish(1);
}
