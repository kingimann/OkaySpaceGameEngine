// Core audit: input plumbing. The first test must run first in a fresh process
// (it checks the very first mouse sample).
#include "test_framework.hpp"
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdlib>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
// Application is the only friend of the terminal session hooks; tests reach them directly.
#define private public
#include "okay/Input/Input.hpp"
#undef private

using namespace okay;

int main() {
    RUN_SUITE("core_audit_input");

    // The first mouse sample is a position, not a movement: a mouse-look camera
    // must not jump by the cursor's absolute position on frame one.
    {
        Input::FeedMouse({640.0f, 360.0f}, 0);
        CHECK_NEAR(Input::MouseDelta().x, 0.0, 1e-6);
        CHECK_NEAR(Input::MouseDelta().y, 0.0, 1e-6);
        Input::FeedMouse({650.0f, 360.0f}, 0);
        CHECK_NEAR(Input::MouseDelta().x, 10.0, 1e-6);
    }

    // Leaving a terminal session restores blocking reads on stdin.
    {
        int master = posix_openpt(O_RDWR | O_NOCTTY);
        CHECK(master >= 0);
        if (master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0) {
            int slave = open(ptsname(master), O_RDWR | O_NOCTTY);
            CHECK(slave >= 0);
            int savedStdin = dup(STDIN_FILENO);
            dup2(slave, STDIN_FILENO);
            CHECK((fcntl(STDIN_FILENO, F_GETFL, 0) & O_NONBLOCK) == 0);
            Input::BeginSession();
            CHECK(Input::s_interactive);
            CHECK((fcntl(STDIN_FILENO, F_GETFL, 0) & O_NONBLOCK) != 0);
            Input::EndSession();
            CHECK((fcntl(STDIN_FILENO, F_GETFL, 0) & O_NONBLOCK) == 0);
            dup2(savedStdin, STDIN_FILENO);
            close(savedStdin);
            close(slave);
        }
        if (master >= 0) close(master);
    }

    TEST_MAIN_RESULT();
}
