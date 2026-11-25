// SignInGate.hpp - the launcher is used signed in only (owner, 2026-11-25: "i want users to be forced to sign in or they cant
// use anything", and "fully blocked": no offline use). Our own code, pure (no ImGui, no network), so the rules are tested
// headless (tests/test_signin_gate.cpp).
//
// What the window shows:
//   SignIn    - nobody is signed in: the sign-in screen (Sign in / Create account / Forgot password) and nothing else.
//   Checking  - a saved session ("stay signed in") that this start has not confirmed with the server yet:
//               "Checking your sign-in..." (no pages yet). Every start confirms the session with the server.
//   Open      - signed in and confirmed by the server this start.
//   Blocked   - a saved session, but the server could not be reached to confirm it: "Can't reach the OkaySpace server to
//               check your sign-in" with Try again. There is no offline use.
// Always allowed, whatever the state: the sign-in screens, the launcher's own update check and install, quitting.
#pragma once

namespace okay::signingate {

enum class State { SignIn, Checking, Open, Blocked };

/// `loggedIn`: a session is saved; `needsServer`: the account lives on a server (false for a local, on-device account in a
/// build with no account server, which has nothing to check); `check`: this start's check of the session: -1 not answered
/// yet, 0 the server could not be reached, 1 the server confirmed it. (A session the server rejects is signed out, so it
/// arrives here as loggedIn == false.)
inline State Decide(bool loggedIn, bool needsServer, int check) {
    if (!loggedIn) return State::SignIn;
    if (!needsServer || check == 1) return State::Open;
    return check == 0 ? State::Blocked : State::Checking;
}

/// May the pages (sidebar, every tab, starting the editor or a game) be used in this state?
inline bool PagesAllowed(State s) { return s == State::Open; }

}  // namespace okay::signingate
