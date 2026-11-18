// The own-code switch table (shared/OwnSwitches.hpp): parse / serialize, master off, environment precedence, status text.
// Uses its own fake items and variable names, so it never touches the real OKAY_* variables.
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
    CHECK(Find("wav", Items()) != nullptr);
}

static void own_switches_parse_serialize() {
    CHECK(Parse("", kFake).empty());
    CHECK(Parse("on=\n", kFake).empty());
    auto v = Parse("# x\non= b , a,zzz,a\r\n", kFake);   // unknown dropped, repeats dropped, table order
    CHECK(JoinIds(v) == "a,b");
    CHECK(Serialize(v, kFake).find("on=a,b\n") != std::string::npos);
    CHECK(JoinIds(Parse(Serialize(v, kFake), kFake)) == "a,b");
    CHECK(Parse("on=a", kFake).size() == 1);
}

static void own_switches_set_and_master_off() {
    std::vector<std::string> on;
    Set(on, "a", true); Set(on, "b", true); Set(on, "a", true);
    CHECK(on.size() == 2);
    Set(on, "a", false);
    CHECK(JoinIds(on) == "b");
    AllOff(on);
    CHECK(on.empty());
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
    CHECK(LoadFile("no_such_dir/none.cfg", kFake).empty());
    CHECK(SaveFile(p, {"b", "a"}, kFake));
    CHECK(JoinIds(LoadFile(p, kFake)) == "a,b");
    CHECK(SaveFile(p, {}, kFake));
    CHECK(LoadFile(p, kFake).empty());
    std::remove(p.c_str());
}

int main() {
    RUN_SUITE("own switches table");
    own_switches_real_table_is_sane();
    own_switches_parse_serialize();
    own_switches_set_and_master_off();
    own_switches_apply_sets_and_records();
    own_switches_user_environment_wins();
    own_switches_unavailable_items_stay_off();
    own_switches_status_text();
    own_switches_file_round_trip();
    TEST_MAIN_RESULT();
}
