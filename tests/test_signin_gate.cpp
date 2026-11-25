// The launcher's sign-in gate (launcher/SignInGate.hpp) and its welcome screen rules (launcher/AuthForm.hpp): signed out shows
// only the sign-in screen, every page is blocked until the server confirms the session this start (no offline use), signing in
// lets you through, signing out goes back; the fields of each step and their Tab order, the checks under the fields, the
// password strength, server errors in plain words, and where the art, the logo and the card go at 960 x 600 and 1280 x 800
// (UI scale 1.0 and 1.5).
#include "test_framework.hpp"
#include "SignInGate.hpp"
#include "AuthForm.hpp"

#include <string>

namespace G = okay::signingate;
namespace A = okay::authform;

int main() {
    RUN_SUITE("signin_gate");
    using S = G::State;
    // ---- The gate ----
    CHECK(G::Decide(false, true, -1) == S::SignIn);    // a signed-out start: the sign-in screen only
    CHECK(G::Decide(false, true, 1) == S::SignIn);
    CHECK(G::Decide(false, false, -1) == S::SignIn);   // a local account too
    CHECK(G::Decide(true, true, -1) == S::Checking);   // a saved session: checked with the server before any page
    CHECK(G::Decide(true, true, 1) == S::Open);        // confirmed (or just signed in): through
    CHECK(G::Decide(true, true, 0) == S::Blocked);     // the server can't be reached: blocked, even with a saved session
    CHECK(G::Decide(true, false, -1) == S::Open);      // a local, on-device account has nothing to check
    for (S st : {S::SignIn, S::Checking, S::Blocked}) CHECK(!G::PagesAllowed(st));   // every page route is closed
    CHECK(G::PagesAllowed(S::Open));
    // Sign in, use, sign out: the session goes, the gate closes again.
    {
        bool loggedIn = false; int check = -1;
        CHECK(G::Decide(loggedIn, true, check) == S::SignIn);
        loggedIn = true; check = 1;                    // a successful sign-in answered the server
        CHECK(G::PagesAllowed(G::Decide(loggedIn, true, check)));
        loggedIn = false;                               // Sign out
        CHECK(G::Decide(loggedIn, true, check) == S::SignIn);
    }

    // ---- The welcome screen's steps and Tab order ----
    {
        const auto si = A::FieldOrder(A::Step::SignIn, true);
        CHECK(si.size() == 2 && si[0] == A::Field::Identifier && si[1] == A::Field::Password);
        const auto su = A::FieldOrder(A::Step::SignUp, true);
        CHECK(su.size() == 5 && su[0] == A::Field::Username && su[1] == A::Field::Email && su[2] == A::Field::Password && su[3] == A::Field::Confirm);
        CHECK(A::FieldOrder(A::Step::SignUp, false).size() == 3);   // username accounts: no email field
        CHECK(A::FieldOrder(A::Step::Forgot, true).size() == 1 && A::FieldOrder(A::Step::Forgot, true)[0] == A::Field::Email);
        CHECK(A::FieldOrder(A::Step::CheckEmail, true).empty());
    }
    // ---- The checks under the fields ----
    CHECK(A::CheckEmail("", false).empty());                    // nothing to say until you try
    CHECK(!A::CheckEmail("", true).empty());
    CHECK(A::CheckEmail("star@example.com", true).empty());
    CHECK(A::CheckEmail(" star@example.com ", true).empty());
    CHECK(!A::CheckEmail("star@pilot", true).empty());
    CHECK(!A::CheckEmail("@example.com", true).empty());
    CHECK(!A::CheckEmail("a b@example.com", true).empty());
    CHECK(A::CheckUsername("starpilot", true).empty());
    CHECK(A::CheckUsername("star.pilot-2_x", true).empty());
    CHECK(!A::CheckUsername("x", true).empty());
    CHECK(!A::CheckUsername("star pilot", true).empty());
    CHECK(!A::CheckUsername(std::string(33, 'a'), true).empty());
    CHECK(!A::CheckPassword("abc", true, false).empty());       // sign-up: 6 or more
    CHECK(A::CheckPassword("abc", false, true).empty());        // sign-in: whatever the account has
    CHECK(!A::CheckConfirm("Orbit42!", "Orbit42?", false).empty());
    CHECK(A::CheckConfirm("Orbit42!", "Orbit42!", true).empty());
    CHECK(!A::CanSubmit(A::Step::SignUp, true, "", "x", "star@pilot", "abc", "abd"));
    CHECK(A::CanSubmit(A::Step::SignUp, true, "", "starpilot", "star@example.com", "Orbit42!", "Orbit42!"));
    CHECK(A::CanSubmit(A::Step::SignIn, true, "starpilot", "", "", "pw", ""));
    CHECK(!A::CanSubmit(A::Step::SignIn, true, "  ", "", "", "pw", ""));
    CHECK(A::CanSubmit(A::Step::Forgot, true, "", "", "star@example.com", "", ""));
    // ---- Password strength ----
    CHECK(A::PasswordStrength("") == 0);
    CHECK(A::PasswordStrength("abc") == 1);
    CHECK(A::PasswordStrength("abcdef") == 1);
    CHECK(A::PasswordStrength("abcdef1") == 2);
    CHECK(A::PasswordStrength("Orbit42!") >= 3);
    CHECK(A::PasswordStrength("Orbit42!Orbit42!") == 4);
    {
        const auto r = A::PasswordRules("Orbit42!");
        CHECK(r.size() == 4); CHECK(r[0].required && r[0].met);
        for (const auto& x : r) CHECK(x.met);
        CHECK(!A::PasswordRules("abc")[0].met);
    }
    // ---- Server errors in plain words (never codes, HTTP numbers or setup notes) ----
    auto plain = [](const std::string& t) { return t.find("HTTP") == std::string::npos && t.find("Supabase") == std::string::npos && t.find("(") == std::string::npos; };
    {
        const auto wrong = A::FriendlyError("Invalid login credentials");
        CHECK(wrong.outcome == A::Outcome::Error && wrong.text.find("don't match") != std::string::npos);
        CHECK(A::FriendlyError("Invalid username/email or password.").text == wrong.text);
        const auto taken = A::FriendlyError("User already registered");
        CHECK(taken.text.find("already an account") != std::string::npos);
        const auto off = A::FriendlyError("Couldn't reach the account server (offline or unreachable).");
        CHECK(off.outcome == A::Outcome::Unreachable && plain(off.text));
        const auto chk = A::FriendlyError("Account created \xE2\x80\x94 check your email to confirm, then sign in. (Turn off email confirmation in Supabase for instant sign-in.)");
        CHECK(chk.outcome == A::Outcome::CheckYourEmail && plain(chk.text));
        CHECK(plain(A::FriendlyError("Account server rejected the request (HTTP 500).").text));
        CHECK(plain(A::FriendlyError("Couldn't start password reset (HTTP 429).").text));
        CHECK(A::FriendlyError("Email not confirmed").text.find("Confirm your email") != std::string::npos);
        CHECK(A::FriendlyError("That username is already taken.").text == "That username is taken. Try another one.");
        CHECK(A::FriendlyError("Choose a username.").text == "Choose a username.");   // our own sentences pass through
        CHECK(plain(A::FriendlyError("").text));
    }
    // ---- Where things go: no overlap, the card inside the window ----
    for (float scale : {1.0f, 1.5f}) {
        for (const auto& wh : {std::pair<float, float>{960, 600}, std::pair<float, float>{1280, 800}, std::pair<float, float>{1920, 1080}}) {
            const float cardH = 420.0f * scale, logoH = 96.0f * scale;
            const A::Welcome l = A::LayoutWelcome(wh.first, wh.second, 420.0f * scale, cardH, logoH, 24.0f * scale, 1100.0f * scale);
            CHECK(l.logo.B() <= l.card.y);                         // the logo above the card, never on it
            CHECK(l.card.x >= l.artPanel.R() - 0.5f);              // the card beside the art, never on it
            CHECK(l.card.R() <= wh.first + 0.5f && l.card.x >= -0.5f);
            if (cardH + logoH + 72.0f * scale <= wh.second) CHECK(l.card.B() <= wh.second + 0.5f);   // fits when it can (else the page scrolls)
            CHECK(l.card.w >= std::min(420.0f * scale, wh.first - l.artPanel.w - 48.0f * scale) - 0.5f);
            if (wh.first >= 1920 && scale == 1.0f) CHECK(l.art);   // the art panel at the wide size
            if (wh.first <= 960) CHECK(!l.art);                    // never at the small one
        }
    }
    TEST_MAIN_RESULT();
}
