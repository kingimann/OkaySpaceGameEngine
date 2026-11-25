// The name shown to other people (okay::account::PublicName, engine/include/okay/Platform/Account/AccountService.hpp): never an
// email address. Every name the platform clients read from the server (forum posts and comments, chat, server members, friends
// and their activity, profiles, parties, Marketplace and Community sellers) goes through it. Owner bug 2026-11-25: a forum post
// showed its author's email address.
#include "test_framework.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/Forums.hpp"
#include "okay/Platform/Friends.hpp"
#include "okay/Platform/Workshop.hpp"

#include <string>

using okay::account::PublicName;

int main() {
    RUN_SUITE("public_name");
    CHECK(PublicName("starpilot") == "starpilot");                    // a username stays as it is
    CHECK(PublicName("Star Pilot") == "Star Pilot");                  // so does a display name
    CHECK(PublicName("") == "");                                      // empty stays empty (the caller's "unknown")
    CHECK(PublicName("alex.doe@hotmail.com") == "alex.doe");          // an email: the part before the @
    CHECK(PublicName("  alex@x.com") == "alex");
    CHECK(PublicName("@x.com") == "Player");                          // nothing before the @
    CHECK(PublicName("@x.com", "3f2a9c-41") == "Player-3f2a");
    CHECK(PublicName("a@b@c.com") == "a");
    // Whatever the server sends, the result never holds an "@".
    for (const char* raw : {"x@y.z", "@", "@@", " @ ", "name@", "first.last+tag@mail.example.org", "\xC3\xA9@x.com"})
        for (const char* id : {"", "abcd1234", "@@@@"}) CHECK(PublicName(raw, id).find('@') == std::string::npos);
    // The clients' parsers: an author_name that is an email comes out as the name only.
    {
        const std::string row = "{\"id\":1,\"title\":\"test\",\"author_name\":\"someone@hotmail.com\",\"created_at\":\"2026-10-20T10:00:00Z\"}";
        CHECK(PublicName(okay::Forums::Field(row, "author_name")) == "someone");
        CHECK(PublicName(okay::Workshop::Field(row, "author_name")) == "someone");
    }
    TEST_MAIN_RESULT();
}
