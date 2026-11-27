#include "test_framework.hpp"
#include "okay/Platform/Messages.hpp"
#include "okay/Platform/Servers.hpp"
#include "okay/Platform/Forums.hpp"
#include "okay/Platform/Workshop.hpp"

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
    // Forum posts and marketplace descriptions (also listed on Admin's review tabs).
    CheckSplit([](const std::string& s) { return okay::Forums::SplitObjects(s); });
    CheckSplit([](const std::string& s) { return okay::Workshop::SplitObjects(s); });
    // #888 F9: a marketplace listing titled "storage_path" whose description is an outside link. Field used to find the
    // first "storage_path" text anywhere (here the title's value) and return the description as the download link.
    {
        const std::string row = "{\"id\":7,\"author\":\"u1\",\"author_name\":\"bob\",\"title\":\"storage_path\","
                                "\"description\":\"https://evil.example/game.zip\",\"kind\":\"downloads\","
                                "\"storage_path\":\"https://p.supabase.co/storage/v1/object/public/workshop/u1/game.zip\","
                                "\"downloads\":12,\"created_at\":\"2026-10-25T10:00:00Z\"}";
        CHECK(okay::Workshop::Field(row, "storage_path") == "https://p.supabase.co/storage/v1/object/public/workshop/u1/game.zip");
        CHECK(okay::Workshop::Field(row, "kind") == "downloads");
        CHECK(okay::Workshop::Field(row, "downloads") == "12");
        CHECK(okay::Workshop::Field(row, "id") == "7");
        CHECK(okay::Workshop::Field(row, "nothing").empty());
        // The list keeps only links into our own 'workshop' bucket; an outside link is dropped ("no file to download").
        const std::string srv = "https://p.supabase.co";
        auto items = okay::Workshop::ParseItems("[" + row + "]", srv + "/", false);
        CHECK(items.size() == 1 && items[0].title == "storage_path" && items[0].downloads == 12 && items[0].id == 7 &&
              items[0].url == "https://p.supabase.co/storage/v1/object/public/workshop/u1/game.zip");
        std::string outside = row;
        outside.replace(outside.find("https://p.supabase.co/storage"), std::string("https://p.supabase.co").size(), "https://evil.example");
        items = okay::Workshop::ParseItems("[" + outside + "]", srv, false);
        CHECK(items.size() == 1 && items[0].url.empty());
        CHECK(okay::Workshop::IsOwnStorageUrl(srv, srv + "/storage/v1/object/public/workshop/u1/a.zip"));
        for (const std::string& bad : {std::string("https://evil.example/storage/v1/object/public/workshop/u1/a.zip"),
                                       srv + ".evil.example/storage/v1/object/public/workshop/u1/a.zip",
                                       srv + "/storage/v1/object/public/workshop/",
                                       srv + "/storage/v1/object/public/workshop/../avatars/a.png",
                                       srv + "/storage/v1/object/public/avatars/u1/a.png",
                                       srv + "/storage/v1/object/public/workshop/u1/a b.zip",
                                       srv + "/storage/v1/object/public/workshop/u1/a\\b.zip", std::string()})
            CHECK(!okay::Workshop::IsOwnStorageUrl(srv, bad));
        CHECK(!okay::Workshop::IsOwnStorageUrl("", "/storage/v1/object/public/workshop/u1/a.zip"));
        CHECK(okay::Workshop::Field("not json", "id").empty() && okay::Workshop::Field("{\"id\":null}", "id").empty() &&
              okay::Workshop::Field("{\"f\":true,\"x\":1.5}", "f") == "true" && okay::Workshop::Field("{\"x\":1.5}", "x") == "1.5");
    }
    CHECK(okay::Messages::SplitObjects("[]").empty());
    CHECK(okay::Servers::SplitObjects("").empty());
    TEST_MAIN_RESULT();
}
