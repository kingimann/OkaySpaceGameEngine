// okay-playtest: runs every play-mode test linked into this executable.
//
//   okay-playtest                       run everything
//   okay-playtest --list                list the tests
//   okay-playtest movement              only tests whose "suite.name" contains "movement"
//   okay-playtest --filter='selftest.*' --junit=out/playtest.xml --text=out/playtest.txt
//
// Exit code: 0 all passed, 1 a test failed, 2 usage error, 3 nothing matched.
// A game project makes its own runner the same way: write tests with OKAY_PLAYTEST
// (okay/Testing/PlayTest.hpp), link them with okay::engine and call RunMain from main().
#include "okay/Testing/TestRegistry.hpp"

int main(int argc, char** argv) {
    return okay::testing::RunMain(argc, argv);
}
