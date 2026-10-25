#include "test_framework.hpp"
#include "okay/Platform/Messages.hpp"
#include "okay/Platform/Servers.hpp"

#include <string>
#include <vector>

// The launcher's chat (DMs, group chats and server channels) reads PostgREST's
// JSON arrays with SplitObjects. A brace inside a message body ("{" in code, a
// ":-}" smiley) must not end or swallow the message: it used to hide that
// message and every newer one in the conversation.
template <class SplitFn>
static void CheckSplit(SplitFn split) {
    const std::string rows =
        "[{\"id\":1,\"body\":\"hello\",\"created_at\":\"2026-10-25T10:00:00Z\"},"
        " {\"id\":2,\"body\":\"int main() { return 0; }\",\"created_at\":\"2026-10-25T10:01:00Z\"},"
        " {\"id\":3,\"body\":\"only an opening { here\"},"
        " {\"id\":4,\"body\":\"and a closing } :-}\"},"
        " {\"id\":5,\"body\":\"quote \\\" then { brace\"},"
        " {\"id\":6,\"body\":\"last\",\"meta\":{\"nested\":true}}]";
    const std::vector<std::string> o = split(rows);
    CHECK(o.size() == 6);
    if (o.size() != 6) return;
    CHECK(okay::Messages::Field(o[0], "body") == "hello");
    CHECK(okay::Messages::Field(o[1], "body") == "int main() { return 0; }");
    CHECK(okay::Messages::Field(o[1], "created_at") == "2026-10-25T10:01:00Z");
    CHECK(okay::Messages::Field(o[2], "id") == "3");
    CHECK(okay::Messages::Field(o[2], "body") == "only an opening { here");
    CHECK(okay::Messages::Field(o[3], "body") == "and a closing } :-}");
    CHECK(okay::Messages::Field(o[4], "body") == "quote \" then { brace");
    CHECK(okay::Messages::Field(o[5], "id") == "6");
    CHECK(o[5].find("\"nested\":true}}") != std::string::npos);   // nested objects stay whole
}

int main() {
    RUN_SUITE("chat_json");
    CheckSplit([](const std::string& s) { return okay::Messages::SplitObjects(s); });
    CheckSplit([](const std::string& s) { return okay::Servers::SplitObjects(s); });
    CHECK(okay::Messages::SplitObjects("[]").empty());
    CHECK(okay::Servers::SplitObjects("").empty());
    TEST_MAIN_RESULT();
}
