// The own-code switch table (shared/OwnSwitches.hpp): on by default, parse / serialize, the old on= file, master on / off, environment
// precedence, status text. Uses its own fake items and variable names, except the cases that check the real table.
#include "test_framework.hpp"
#include "OwnSwitches.hpp"

#include <cstdio>

using namespace okay::ownswitches;

static const std::vector<Item> kFake = {
    {"a", "A", "alpha", "OKAY_TEST_OWNSW_A", "1", kNone},
    {"b", "B", "beta",  "OKAY_TEST_OWNSW_B", "own", kNone},
    {"w", "W", "not on windows", "OKAY_TEST_OWNSW_W", "1", kNotWindows},
    {"u", "U", "needs okayui",   "OKAY_TEST_OWNSW_U", "okayui", kNeedsOkayUi},
};
static void Clear() {
    for (const Item& i : kFake) DropEnv(i.env);
    DropEnv(AppliedVar());
}
static const char* Get(const char* n) { return std::getenv(n); }

static void own_switches_real_table_is_sane() {
    std::vector<std::string> seen;
    for (const Item& i : Items()) {
        CHECK(i.id[0] != 0 && i.label[0] != 0 && i.desc[0] != 0 && i.env[0] != 0 && i.value[0] != 0);
        CHECK(!Has(seen, i.id));
        seen.push_back(i.id);
    }
    CHECK(Find("http", Items()) == nullptr);   // our own web client is the only one now (no curl): no switch, an old off=http line is ignored
    CHECK(JoinIds(Parse("off=http\n", Items())) == JoinIds(Parse("", Items())));
    CHECK(Find("wav", Items()) == nullptr);   // our own WAV reader is the only one now: no switch (an old off=wav line is ignored)
    CHECK(JoinIds(Parse("off=wav\n", Items())) == JoinIds(Parse("", Items())));
    // The editor and the launcher are OkayUI only (Dear ImGui left the repository): "OkayUI screens" and "OkayUI Inspector" chose
    // between two copies and are gone; an old off=okayui or off=inspector line is ignored, and nothing sets their variables.
    CHECK(Find("okayui", Items()) == nullptr && Find("inspector", Items()) == nullptr);
    CHECK(JoinIds(Parse("off=okayui,inspector\n", Items())) == JoinIds(Parse("", Items())));
    for (const Item& i : Items()) CHECK(std::string(i.env) != "OKAY_UI_BACKEND" && std::string(i.env) != "OKAY_UI_INSPECTOR");
}

static void own_switches_parse_serialize() {
    // never chosen (no text, a comment only, damage): everything on, in table order
    CHECK(JoinIds(Parse("", kFake)) == "a,b,w,u");
    CHECK(JoinIds(Parse("# only a comment\ngarbage\n", kFake)) == "a,b,w,u");
    // off= decides: unknown ids and repeats dropped, the rest stays on
    CHECK(JoinIds(Parse("# x\noff= b , zzz,b\r\n", kFake)) == "a,w,u");
    CHECK(JoinIds(Parse("off=\n", kFake)) == "a,b,w,u");          // an empty off= line: nothing off
    CHECK(Parse("off=a,b,w,u\n", kFake).empty());                // everything off
    // the file says both lines; reading it back gives the same choice
    const std::vector<std::string> v = {"b", "u"};
    const std::string text = Serialize(v, kFake);
    CHECK(text.find("\noff=a,w\n") != std::string::npos);
    CHECK(text.find("\non=b,u\n") != std::string::npos);        // what the older off-by-default programs read
    CHECK(JoinIds(Parse(text, kFake)) == "b,u");
    CHECK(JoinIds(Parse(Serialize({}, kFake), kFake)).empty());
}

static void own_switches_old_on_file_means_never_chosen() {
    // A file written by the off-by-default version has only an on= line. An id missing there meant "never chosen", not "turned off",
    // so such a file must not turn anything off: everything is on.
    CHECK(JoinIds(Parse("# Experimental: our own code\non=wav\n", kFake)) == "a,b,w,u");
    CHECK(JoinIds(Parse("on=\n", kFake)) == "a,b,w,u");
    // once an off= line is there, it wins over any on= line
    CHECK(JoinIds(Parse("on=a,b\noff=b\n", kFake)) == "a,w,u");
}

static void own_switches_set_and_master_off() {
    std::vector<std::string> on;
    Set(on, "a", true); Set(on, "b", true); Set(on, "a", true);
    CHECK(on.size() == 2);
    Set(on, "a", false);
    CHECK(JoinIds(on) == "b");
    AllOff(on);
    CHECK(on.empty());
    AllOn(on, kFake);
    CHECK(JoinIds(on) == "a,b,w,u");
    CHECK(JoinIds(Parse("", kFake)) == JoinIds(on));   // "All on" is the never-chosen default
}

static void own_switches_apply_sets_and_records() {
    Clear();
    Context ctx;   // not windows, no okayui
    auto got = Apply({"a", "b"}, kFake, ctx);
    CHECK(JoinIds(got) == "a,b");
    CHECK(std::string(Get("OKAY_TEST_OWNSW_A")) == "1");
    CHECK(std::string(Get("OKAY_TEST_OWNSW_B")) == "own");
    CHECK(JoinIds(AppliedNow()) == "a,b");
    // turned off again (a child that inherited the variables, then reads a file without them)
    got = Apply({"a"}, kFake, ctx);
    CHECK(JoinIds(got) == "a");
    CHECK(Get("OKAY_TEST_OWNSW_B") == nullptr);
    got = Apply({}, kFake, ctx);
    CHECK(got.empty() && Get("OKAY_TEST_OWNSW_A") == nullptr && Get(AppliedVar()) == nullptr);
    Clear();
}

static void own_switches_user_environment_wins() {
    Clear();
    PutEnv("OKAY_TEST_OWNSW_A", "0");   // the person's own value
    Context ctx;
    auto got = Apply({"a", "b"}, kFake, ctx);
    CHECK(JoinIds(got) == "b");   // a left alone
    CHECK(std::string(Get("OKAY_TEST_OWNSW_A")) == "0");
    CHECK(SetByUser(kFake[0], AppliedNow()));
    CHECK(!SetByUser(kFake[1], AppliedNow()));
    CHECK(StatusOf(kFake[0], {"a"}, got, true) == Status::SetByEnvironment);
    // master off must not remove the person's variable either
    Apply({}, kFake, ctx);
    CHECK(Get("OKAY_TEST_OWNSW_A") != nullptr && std::string(Get("OKAY_TEST_OWNSW_A")) == "0");
    Clear();
}

static void own_switches_unavailable_items_stay_off() {
    Clear();
    Context win; win.windows = true;
    auto got = Apply({"w", "u"}, kFake, win);
    CHECK(got.empty() && Get("OKAY_TEST_OWNSW_W") == nullptr && Get("OKAY_TEST_OWNSW_U") == nullptr);
    Context ui; ui.okayUi = true;
    got = Apply({"w", "u"}, kFake, ui);
    CHECK(JoinIds(got) == "w,u");
    CHECK(Available(kFake[3], ui) && !Available(kFake[3], Context()));
    CHECK(Available(kFake[2], Context()) && !Available(kFake[2], win));
    Clear();
}

static void own_switches_status_text() {
    const Item& a = kFake[0];
    CHECK(StatusOf(a, {}, {}, false) == Status::Off);
    CHECK(StatusOf(a, {"a"}, {"a"}, false) == Status::OnNow);
    CHECK(StatusOf(a, {"a"}, {}, false) == Status::OnAfterRestart);
    CHECK(StatusOf(a, {}, {"a"}, false) == Status::OffAfterRestart);
    CHECK(std::string(StatusText(Status::OnNow)) == "on now");
    CHECK(std::string(StatusText(Status::OnAfterRestart)) == "on after restart");
    CHECK(std::string(StatusText(Status::SetByEnvironment)) == "set by an environment variable");
}

static void own_switches_file_round_trip() {
    const std::string p = "own_switches_test.cfg";
    CHECK(JoinIds(LoadFile("no_such_dir/none.cfg", kFake)) == "a,b,w,u");   // no file: everything on
    CHECK(SaveFile(p, {"b", "a"}, kFake));
    CHECK(JoinIds(LoadFile(p, kFake)) == "a,b");
    CHECK(SaveFile(p, {}, kFake));
    CHECK(LoadFile(p, kFake).empty());
    {   // the person turned one off by hand: off=b, the rest on
        std::FILE* f = std::fopen(p.c_str(), "wb");
        CHECK(f != nullptr);
        if (f) { std::fputs("off=b\n", f); std::fclose(f); }
        CHECK(JoinIds(LoadFile(p, kFake)) == "a,w,u");
    }
    std::remove(p.c_str());
}

static void default_table_turns_everything_on() {
    // The real table, never chosen, on a Windows machine with OkayUI: every offered item is on and sets its variable.
    const char* names[] = {"OKAY_UI_BACKEND", "OKAY_UI_INSPECTOR", "OKAY_UI_OWN_RENDER", "OKAY_OWN_WAV", "OKAY_OWN_HTTP", "OKAY_PLATFORM_BACKEND", "OKAY_BROWSER", "OKAY_DEV"};
    std::vector<std::string> saved(sizeof(names) / sizeof(names[0])); std::vector<bool> had(saved.size());
    for (std::size_t i = 0; i < saved.size(); ++i) { const char* e = std::getenv(names[i]); had[i] = e != nullptr; saved[i] = e ? e : ""; DropEnv(names[i]); }
    DropEnv(AppliedVar());
    Context ctx; ctx.windows = true; ctx.okayUi = true;
    auto got = Apply(Parse("", Items()), Items(), ctx);
    CHECK(JoinIds(got) == "browser");                                          // platform and ownrender went with SDL2, okayui and inspector with Dear ImGui
    CHECK(Get("OKAY_BROWSER") && std::string(Get("OKAY_BROWSER")) == "1");     // the Browser page (launcher/BrowserPage.hpp) starts on
    CHECK(Get("OKAY_OWN_HTTP") == nullptr && Get("OKAY_PLATFORM_BACKEND") == nullptr);
    CHECK(Get("OKAY_UI_BACKEND") == nullptr && Get("OKAY_UI_INSPECTOR") == nullptr);   // the removed items set nothing
    CHECK(Get("OKAY_UI_OWN_RENDER") == nullptr);   // the SDL renderer host it chose is gone: nothing sets the variable
    CHECK(Get("OKAY_OWN_WAV") == nullptr);   // the WAV switch was deleted: nothing sets its variable
    for (std::size_t i = 0; i < saved.size(); ++i) { if (had[i]) PutEnv(names[i], saved[i].c_str()); else DropEnv(names[i]); }
    DropEnv(AppliedVar());
}

static void DefaultPathNamesTheFile() {
    const std::string p = DefaultPath();
    CHECK(p.size() > 16 && p.compare(p.size() - 16, 16, "own_switches.cfg") == 0);
}

int main() {
    RUN_SUITE("own switches table");
    own_switches_real_table_is_sane();
    own_switches_parse_serialize();
    own_switches_old_on_file_means_never_chosen();
    own_switches_set_and_master_off();
    own_switches_apply_sets_and_records();
    own_switches_user_environment_wins();
    own_switches_unavailable_items_stay_off();
    own_switches_status_text();
    own_switches_file_round_trip();
    default_table_turns_everything_on();
    DefaultPathNamesTheFile();
    TEST_MAIN_RESULT();
}
