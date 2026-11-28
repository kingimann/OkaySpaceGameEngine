// shared/TestHooks.hpp: the script parser, the events each command makes, and a Runner pushing them into a real backend's queue.
#include "TestHooks.hpp"
#include <cstdio>
#include <cstdlib>

using namespace okay::testhooks;
using namespace okay::platform;
static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main() {
    std::vector<std::string> err;
    auto cmds = ParseScript("# comment\n"
                            "500 click 120 40\n"
                            "100 move 5 6\n"
                            "200 type hello #1 world\n"
                            "250 key ctrl+shift+a\n"
                            "260 key Return down\n"
                            "270 key Return up\n"
                            "300 wheel -3\n"
                            "310 drop /tmp/a b.png\n"
                            "320 click 7 8 3\n"
                            "900 shot /tmp/x.png\n"
                            "950 quit\n960 close\n970 exit 3\r\n", &err);
    CHECK(err.empty());
    CHECK(cmds.size() == 13);
    CHECK(cmds[0].atMs == 100 && cmds[0].verb == "move" && cmds[0].x == 5 && cmds[0].y == 6);   // sorted by time
    CHECK(cmds[1].verb == "type" && cmds[1].text == "hello #1 world");
    CHECK(cmds[2].verb == "key" && cmds[2].key == (int)Key::A && (cmds[2].mods & ModCtrl) && (cmds[2].mods & ModShift) && !(cmds[2].mods & ModAlt));
    CHECK(cmds[3].keyDown && !cmds[3].keyUp && cmds[4].keyUp && !cmds[4].keyDown);
    CHECK(cmds[5].verb == "wheel" && cmds[5].y == -3);
    CHECK(cmds[6].verb == "drop" && cmds[6].text == "/tmp/a b.png");
    CHECK(cmds[7].verb == "click" && cmds[7].button == 3);
    CHECK(cmds[8].verb == "click" && cmds[8].atMs == 500 && cmds[8].button == 1);
    CHECK(cmds[12].verb == "exit" && cmds[12].code == 3);

    err.clear();
    ParseScript("x click 1 2\n10 jump\n20 click 1\n30 key Nope\n40 click 1 2 9\n50 type\n60 key a sideways\n", &err);
    CHECK(err.size() == 7);
    CHECK(KeyFromName("F5") == (int)Key::F5 && KeyFromName("z") == (int)Key::Z && KeyFromName("0") == (int)Key::N0 && KeyFromName("F13") == 0 && KeyFromName("Pause") == 0);

    auto ev = ToEvents(ParseScript("0 click 10 20 3")[0], 7);
    CHECK(ev.size() == 3 && ev[0].type == EventType::MouseMove && ev[1].type == EventType::MouseButtonDown && ev[2].type == EventType::MouseButtonUp);
    CHECK(ev[1].button == 3 && ev[1].x == 10 && ev[1].y == 20 && ev[1].windowId == 7 && ev[1].clicks == 1);
    ev = ToEvents(ParseScript("0 key shift+Tab")[0], 7);
    CHECK(ev.size() == 4 && ev[0].type == EventType::KeyDown && ev[0].key == Key::LShift && ev[1].key == Key::Tab && (ev[1].mods & ModShift));
    CHECK(ev[2].type == EventType::KeyUp && ev[2].key == Key::Tab && ev[3].type == EventType::KeyUp && ev[3].key == Key::LShift);
    ev = ToEvents(ParseScript("0 quit")[0], 7);
    CHECK(ev.size() == 1 && ev[0].type == EventType::Quit);
    ev = ToEvents(ParseScript("0 drop /a/b")[0], 7);
    CHECK(ev.size() == 1 && ev[0].type == EventType::DropFile && ev[0].text == "/a/b" && ev[0].windowId == 7);

    // The runner on a real backend's queue (no display needed): the commands arrive in order, once each, when due.
    const char* path = "/tmp/okay_test_hooks_script.txt";
    if (FILE* f = std::fopen(path, "w")) { std::fputs("0 click 3 4\n0 type hi\n60000 quit\n0 shot /tmp/s.png\n0 exit 5\n", f); std::fclose(f); }
    setenv("OKAY_TEST_SCRIPT", path, 1);
    Backend* b = Get(BackendKind::Own);
    CHECK(b != nullptr);
    if (b) {
        InitFlags fl; fl.video = false; fl.audio = false; fl.gamepad = false;
        b->Init(fl);
        Event drain; while (b->PollEvent(drain)) {}
        Runner r;
        CHECK(r.Start(b, 9) && r.Active() && !r.QuitNow());
        r.Frame();
        std::vector<Event> got; Event e;
        while (b->PollEvent(e)) got.push_back(e);
        CHECK(got.size() == 4 && got[0].type == EventType::MouseMove && got[1].type == EventType::MouseButtonDown && got[2].type == EventType::MouseButtonUp && got[3].type == EventType::TextInput);
        CHECK(got.size() == 4 && got[3].text == "hi" && got[3].windowId == 9);
        const auto shots = r.TakeShots();
        CHECK(shots.size() == 1 && shots[0] == "/tmp/s.png");
        CHECK(r.QuitNow() && r.ExitCode() == 5 && !r.Finished());   // the 60 s quit is not due yet
        r.Frame();
        CHECK(!b->PollEvent(e));
    }
    std::remove(path);
    unsetenv("OKAY_TEST_SCRIPT");
    Runner none;
    CHECK(!none.Start(b, 1) && !none.Active());
    std::printf("%d checks, %d failures\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
