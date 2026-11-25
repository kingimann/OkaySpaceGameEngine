#pragma once
// The editor's sign-in gate (owner, 2026-11-25: "i want users to be forced to sign in or they cant use anything"; then "fully
// blocked", no offline grace). The pure half, so tests can run it: which phase the gate is in, what moves it on, and whether a
// project may open. The drawing half (the "Sign in to OkaySpace" window) is DrawSignInGate in editor/main.cpp.
//
//  - The editor checks the saved OkaySpace session (the launcher's session.txt in the per-user folder, read by
//    okay::account::AccountService) with the server when it starts. Only a session the server confirms opens the editor.
//  - No session, or one the server rejected: the sign-in window (email + password, Create account and Forgot password
//    open the launcher). The server cannot be reached, or answers with an error: "Can't reach the OkaySpace server" with
//    Try again. There is no offline grace.
//  - Until it is open no project opens (the launcher's --open / --new-project, the project chooser, a hand-off) and the
//    window is modal, so nothing else in the editor can be used.
//  - Not gated: games (the player runtime, an exported game; their players need no OkaySpace account; the gate lives only
//    in the editor and GateAppliesTo says so), the command-line modes that leave before the window opens (--selftest,
//    --build-game, --vs-shot), and our tests and CI with OKAY_TEST_NO_SIGNIN=1.
#include "okay/Platform/Account/AccountService.hpp"

#include <string>

namespace oked::signin {

/// The programs OkaySpace ships. Only the editor asks for an OkaySpace account.
enum class App { Editor, Launcher, Player, ExportedGame, DedicatedServer };
inline bool GateAppliesTo(App a) { return a == App::Editor; }

/// The test and CI switch: OKAY_TEST_NO_SIGNIN=1 (only our tests and CI set it).
inline bool BypassRequested(const char* env) { return env && env[0] == '1' && env[1] == 0; }

enum class Phase {
    Off,          // not gated (the test switch): everything works as before
    Checking,     // asking the server about the saved session
    Open,         // the server confirmed the session: the editor works
    NeedSignIn,   // nobody signed in, or the session was rejected: the sign-in window
    SigningIn,    // a sign-in is on its way to the server
    Unreachable,  // the server did not answer (or answered with an error): Try again
};

/// The editor works (and projects open) only here.
inline bool ProjectsAllowed(Phase p) { return p == Phase::Off || p == Phase::Open; }

/// At start: the test switch turns the gate off; a saved session is checked; no session asks to sign in.
inline Phase Start(bool bypass, bool hasSavedSession) {
    if (bypass) return Phase::Off;
    return hasSavedSession ? Phase::Checking : Phase::NeedSignIn;
}

/// The server's answer about the saved session. `message` is what the window says under its heading (empty: nothing extra);
/// Unreachable is shown under the heading "Can't reach the OkaySpace server".
inline Phase AfterCheck(okay::account::SessionCheck c, std::string& message) {
    using C = okay::account::SessionCheck;
    switch (c) {
        case C::Valid: message.clear(); return Phase::Open;
        case C::Rejected: message = "Your sign-in has expired. Sign in again to keep working."; return Phase::NeedSignIn;
        case C::NoSession: message.clear(); return Phase::NeedSignIn;
        case C::ServerError: message = "The OkaySpace server answered with an error, so your sign-in could not be confirmed."; return Phase::Unreachable;
        case C::Unreachable: default: message = "Your sign-in could not be confirmed. Check your internet connection."; return Phase::Unreachable;
    }
}

/// A sign-in attempt came back. A sign-in that worked is confirmed by the server already (it handed out the token).
inline Phase AfterSignIn(const okay::account::Result& r, std::string& message) {
    if (r.ok) { message.clear(); return Phase::Open; }
    message = r.error.empty() ? std::string("Couldn't sign in.") : r.error;
    return Phase::NeedSignIn;
}

}  // namespace oked::signin
