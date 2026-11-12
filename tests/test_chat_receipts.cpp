#include "test_framework.hpp"
#include "okay/Platform/Messages.hpp"

#include <filesystem>
#include <string>

// Typing indicator / read receipts need docs/sql/chat_receipts.sql. The launcher
// must tell "tables not created yet" apart from "refused" or "offline", so the
// settings toggles are replaced by a note only in the first case.
using okay::Messages;

int main() {
    RUN_SUITE("chat_receipts");
    // PostgREST answers 404 (PGRST205) for a table that is not in the schema cache.
    CHECK(Messages::IsMissingTable(404, "{\"code\":\"PGRST205\",\"message\":\"Could not find the table 'public.typing_state' in the schema cache\"}"));
    CHECK(Messages::IsMissingTable(404, ""));
    CHECK(Messages::IsMissingTable(400, "{\"code\":\"42P01\",\"message\":\"relation \\\"public.message_reads\\\" does not exist\"}"));
    // Not "missing": a bad request for another reason, an RLS refusal, auth, outage, success.
    CHECK(!Messages::IsMissingTable(400, "{\"code\":\"22P02\",\"message\":\"invalid input syntax for type uuid\"}"));
    CHECK(!Messages::IsMissingTable(401, "{\"message\":\"JWT expired\"}"));
    CHECK(!Messages::IsMissingTable(403, "{\"code\":\"42501\",\"message\":\"new row violates row-level security policy\"}"));
    CHECK(!Messages::IsMissingTable(500, "relation does not exist"));
    CHECK(!Messages::IsMissingTable(200, "[]"));
    CHECK(!Messages::IsMissingTable(0, ""));   // never reached the server

    // The note tells the owner exactly which file to run.
    CHECK(std::string(Messages::ReceiptsSetupNote()).find("chat_receipts.sql") != std::string::npos);

    // Signed out: nothing is sent, and the support state stays Unknown (so the
    // launcher keeps showing the normal toggles rather than a false warning).
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "okay_chat_receipts_test";
    std::filesystem::create_directories(dir);
    {
        okay::account::AccountService svc(dir.string());
        Messages m(svc);
        CHECK(m.TypingSupport() == Messages::Support::Unknown);
        CHECK(m.ReadMarksSupport() == Messages::Support::Unknown);
        CHECK(!m.SetTyping("u"));
        CHECK(!m.PeerTyping("u"));
        CHECK(!m.MarkRead("u", 5));
        CHECK(!m.PeerStateOf("u", false).ok);
        CHECK(m.PeerReadId("u") == 0);
        CHECK(m.TypingSupport() == Messages::Support::Unknown);
        CHECK(m.ReadMarksSupport() == Messages::Support::Unknown);
    }
    std::filesystem::remove_all(dir);
    TEST_MAIN_RESULT();
}
