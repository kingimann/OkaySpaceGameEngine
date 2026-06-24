#include "test_framework.hpp"
#include <okay/Net/Matchmaking.hpp>
#include <okay/Platform/Account/Account.hpp>

using namespace okay;

// Matchmaking rides the authenticated account REST layer. With no account server
// configured / nobody signed in, every call must no-op gracefully (so a game that
// uses it offline simply finds no sessions instead of crashing). The live request
// shapes are covered by docs/supabase-backend.md + a manual check against the
// project; here we lock the offline contract and that the header compiles/links.
int main() {
    RUN_SUITE("matchmaking");

    Account::Configure(".", "", "");          // local backend, signed out
    CHECK(!Account::IsOnline());

    CHECK(Matchmaking::Host("game", "127.0.0.1", 45000).empty());  // can't advertise offline
    CHECK(!Matchmaking::Heartbeat("some-id", 3));
    CHECK(!Matchmaking::Unregister("some-id"));
    CHECK(Matchmaking::List().empty());
    CHECK(Matchmaking::List("arena").empty());

    // Empty id is rejected without any request.
    CHECK(!Matchmaking::Heartbeat("", 1));
    CHECK(!Matchmaking::Unregister(""));

    TEST_MAIN_RESULT();
}
