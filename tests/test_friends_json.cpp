#include "test_framework.hpp"
#include "okay/Platform/Friends.hpp"

#include <string>
#include <vector>

// Friends.hpp reads and writes profile rows (username, bio, status, ...) as
// JSON. The bio is a multi-line text box in the launcher, and any field may hold
// quotes or braces.
int main() {
    RUN_SUITE("friends_json");
    using F = okay::Friends;

    // Writing: a newline (Enter in the bio box) must be escaped, or PostgREST
    // rejects the PATCH and the profile never saves.
    CHECK(F::JStr("line one\nline two") == "\"line one\\nline two\"");
    CHECK(F::JStr("tab\there\r") == "\"tab\\there\\r\"");
    CHECK(F::JStr("say \"hi\" \\o/") == "\"say \\\"hi\\\" \\\\o/\"");
    CHECK(F::JStr(std::string("bell\x07", 5)) == "\"bell\\u0007\"");
    CHECK(F::JStr("caf\xC3\xA9") == "\"caf\xC3\xA9\"");   // UTF-8 passes through

    // Reading: escapes are decoded, an escaped quote doesn't end the string.
    const std::string row =
        "[{\"username\":\"bob\",\"bio\":\"He said \\\"hi\\\"\\nSecond line\",\"status\":\"\\ud83c\\udfae on\","
        "\"accent\":3,\"location\":null}]";
    auto objs = F::SplitObjects(row);
    CHECK(objs.size() == 1);
    if (!objs.empty()) {
        CHECK(F::Field(objs[0], "username") == "bob");
        CHECK(F::Field(objs[0], "bio") == "He said \"hi\"\nSecond line");
        CHECK(F::Field(objs[0], "status") == "\xF0\x9F\x8E\xAE on");
        CHECK(F::Field(objs[0], "accent") == "3");
        CHECK(F::Field(objs[0], "location").empty());
    }

    // A brace inside a bio used to unbalance SplitObjects: the profile came back
    // empty and that friend vanished from the friends list.
    const std::string braces =
        "[{\"username\":\"ann\",\"bio\":\"I code { all day\",\"status\":\"x\"},"
        "{\"username\":\"cy\",\"bio\":\"smile :-}\",\"status\":\"y\"}]";
    objs = F::SplitObjects(braces);
    CHECK(objs.size() == 2);
    if (objs.size() == 2) {
        CHECK(F::Field(objs[0], "username") == "ann" && F::Field(objs[0], "status") == "x");
        CHECK(F::Field(objs[1], "username") == "cy" && F::Field(objs[1], "bio") == "smile :-}");
    }

    // What JStr writes, Field reads back.
    const std::string bio = "Two\nlines with \"quotes\", a \\ and {braces}";
    objs = F::SplitObjects("[{\"bio\":" + F::JStr(bio) + "}]");
    CHECK(objs.size() == 1 && F::Field(objs[0], "bio") == bio);

    TEST_MAIN_RESULT();
}
