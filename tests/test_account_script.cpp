// Tests the engine's process-wide account facade (okay::Account) and the
// account_* OkayScript builtins, so a game can sign a player in from script.
#include "test_framework.hpp"
#include <Okay.hpp>

#include <filesystem>

namespace fs = std::filesystem;
using namespace okay;

int main() {
    RUN_SUITE("account_script");

    // Point the shared service at a throwaway local config dir (no server).
    fs::path dir = fs::temp_directory_path() /
                   ("okay_acct_script_" + account::detail::RandomHex(8));
    fs::remove_all(dir);
    Account::Shutdown();
    Account::Configure(dir.string());          // local backend

    CHECK(!Account::IsOnline());
    CHECK(!Account::IsLoggedIn());

    // Register + login + logout, all from script.
    Scene s("Account"); s.physicsEnabled = false;
    GameObject* go = s.CreateGameObject("Logic");
    auto* sc = go->AddComponent<ScriptComponent>("okayscript");
    sc->LoadSource(
        "function start() {\n"
        "  account_register(\"player1\", \"s3cret!\");\n"
        "  account_logout();\n"
        "  account_login(\"player1\", \"wrong\");\n"   // fails, sets account_error()
        "  account_login(\"player1\", \"s3cret!\");\n"  // succeeds
        "}\n");
    s.Start();
    s.Update(0.016f);

    // The shared facade reflects what the script did: the final account_login
    // in the script succeeded, so we end signed in (and LastError is cleared).
    CHECK(Account::IsLoggedIn());
    CHECK(Account::Username() == "player1");
    CHECK(!Account::Token().empty());
    CHECK(Account::LastError().empty());

    // Wrong password / unknown user fail at the facade too, and a failed
    // attempt leaves a readable error for the UI / account_error().
    Account::Logout();
    CHECK(!Account::Login("player1", "nope").ok);
    CHECK(!Account::LastError().empty());
    CHECK(Account::Login("player1", "s3cret!").ok);
    CHECK(Account::LastError().empty());       // cleared on success

    Account::Shutdown();
    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
