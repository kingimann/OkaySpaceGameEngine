// Which scene the editor opens from its command line (editor/LaunchArgs.hpp). Grok found that
// `OkayEngine.exe examples\vehicle\vehicle_test.okayscene` opened an empty Untitled scene: only `--open <scene>`
// was read. The checks with a bare path fail on the old rule (which took only --open).
#include "test_framework.hpp"
#include "LaunchArgs.hpp"
#include <string>
#include <vector>

using okay::editor::LaunchSceneFromArgs;
using V = std::vector<std::string>;

int main() {
    RUN_SUITE("editor launch args: --open and a bare .okayscene path");
    CHECK(LaunchSceneFromArgs({}) == "");
    CHECK(LaunchSceneFromArgs(V{"--open", "a/b.okayscene"}) == "a/b.okayscene");
    CHECK(LaunchSceneFromArgs(V{"--open=c.okayscene"}) == "c.okayscene");
    // the bug: a bare path, as Windows passes it for "Open with" or a drop on the exe
    CHECK(LaunchSceneFromArgs(V{"examples\\vehicle\\vehicle_test.okayscene"}) == "examples\\vehicle\\vehicle_test.okayscene");
    CHECK(LaunchSceneFromArgs(V{"C:\\Games\\My Level.OkayScene"}) == "C:\\Games\\My Level.OkayScene");   // any case
    CHECK(LaunchSceneFromArgs(V{"--dev", "level.okayscene"}) == "level.okayscene");
    // --open wins over a bare path, wherever it is
    CHECK(LaunchSceneFromArgs(V{"x.okayscene", "--open", "y.okayscene"}) == "y.okayscene");
    // the value of a flag is never taken for the scene
    CHECK(LaunchSceneFromArgs(V{"--template", "t.okayscene"}) == "");
    CHECK(LaunchSceneFromArgs(V{"--new-project", "p.okayscene", "--launcher", "123"}) == "");
    CHECK(LaunchSceneFromArgs(V{"--launcher", "77", "real.okayscene"}) == "real.okayscene");
    // not a scene
    CHECK(LaunchSceneFromArgs(V{"notes.txt"}) == "");
    CHECK(LaunchSceneFromArgs(V{".okayscene"}) == "");
    CHECK(LaunchSceneFromArgs(V{"-x.okayscene"}) == "");
    CHECK(LaunchSceneFromArgs(V{"--open"}) == "");   // a flag with no value
    // two bare scenes: the first
    CHECK(LaunchSceneFromArgs(V{"one.okayscene", "two.okayscene"}) == "one.okayscene");
    TEST_MAIN_RESULT();
}
