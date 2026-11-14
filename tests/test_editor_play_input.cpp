// Editor Play must feed the game the same input the built player does: the extended
// keys (F-keys, Tab, Alt, left / right modifiers) and every controller, not one.
// editor/main.cpp cannot be linked into a test, so this guards the source.
#include "test_framework.hpp"
#include <fstream>
#include <sstream>
#include <string>

static std::string Slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const std::string src = Slurp(std::string(OKAY_SOURCE_DIR) + "/editor/main.cpp");
    CHECK(!src.empty());
    CHECK(src.find("okay::AppendExtendedKeys(") != std::string::npos);
    CHECK(src.find("okay::PadSet g_pads") != std::string::npos);
    CHECK(src.find("g_pads.Feed(down)") != std::string::npos);
    CHECK(src.find("okay::FeedPad(g_pad.get()") == std::string::npos);   // the one-controller path
    TEST_MAIN_RESULT();
}
