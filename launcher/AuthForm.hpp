// AuthForm.hpp - the rules behind the launcher's welcome screen (sign in, create an account, forgot password): which fields
// each step has and in what order (Tab follows it), the checks shown under each field as you type, the password strength,
// the plain-words version of a server error, and where the art panel and the card go. Our own code, pure (no ImGui, no
// network), tested headless (tests/test_signin_gate.cpp). The requests themselves go through the one account client,
// okay::account::AccountService.
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace okay::authform {

enum class Step { SignIn, SignUp, Forgot, ForgotSent, CheckEmail };
enum class Field { Identifier, Username, Email, Password, Confirm, DisplayName };

/// The fields of a step, in the order they are drawn (and Tab moves through them).
inline std::vector<Field> FieldOrder(Step s, bool emailAccounts) {
    switch (s) {
    case Step::SignIn: return {Field::Identifier, Field::Password};
    case Step::SignUp:
        if (!emailAccounts) return {Field::Username, Field::Password, Field::Confirm};
        return {Field::Username, Field::Email, Field::Password, Field::Confirm, Field::DisplayName};
    case Step::Forgot: return {Field::Email};
    default: return {};
    }
}

inline std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

/// "" when fine, else one short sentence. Empty fields say nothing until the person tries to submit (`submitted`).
inline std::string CheckEmail(const std::string& raw, bool submitted) {
    const std::string e = Trim(raw);
    if (e.empty()) return submitted ? "Enter your email address." : "";
    const std::size_t at = e.find('@');
    if (at == std::string::npos || at == 0 || e.find('@', at + 1) != std::string::npos) return "That doesn't look like an email address.";
    const std::size_t dot = e.find('.', at + 2);
    if (dot == std::string::npos || dot + 1 >= e.size() || e.find(' ') != std::string::npos) return "That doesn't look like an email address.";
    return "";
}
inline std::string CheckUsername(const std::string& raw, bool submitted) {
    const std::string u = Trim(raw);
    if (u.empty()) return submitted ? "Choose a username." : "";
    if (u.size() < 2 || u.size() > 32) return "Use 2 to 32 characters.";
    for (unsigned char c : u)
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.')) return "Use letters, numbers, dots, dashes and underscores only.";
    return "";
}
inline std::string CheckIdentifier(const std::string& raw, bool submitted) {
    if (Trim(raw).empty()) return submitted ? "Enter your email or username." : "";
    return "";
}

/// The password rules shown under the field while signing up. `required` ones block "Create account".
struct Rule { const char* text; bool met; bool required; };
inline std::vector<Rule> PasswordRules(const std::string& p) {
    bool digit = false, lower = false, upper = false, symbol = false;
    for (unsigned char c : p) {
        if (std::isdigit(c)) digit = true;
        else if (std::islower(c)) lower = true;
        else if (std::isupper(c)) upper = true;
        else if (!std::isspace(c)) symbol = true;
    }
    return {{"At least 6 characters", p.size() >= 6, true},
            {"A number", digit, false},
            {"Upper and lower case letters", lower && upper, false},
            {"A symbol, like ! or #", symbol, false}};
}
/// 0 (none) .. 4 (strong), for the meter.
inline int PasswordStrength(const std::string& p) {
    if (p.empty()) return 0;
    if (p.size() < 6) return 1;
    int s = 1;
    for (const Rule& r : PasswordRules(p)) if (!r.required && r.met) ++s;
    if (p.size() >= 12) ++s;
    return std::min(4, s);
}
inline const char* StrengthWord(int s) { return s <= 1 ? "Too weak" : s == 2 ? "Fair" : s == 3 ? "Good" : "Strong"; }
inline std::string CheckPassword(const std::string& p, bool signUp, bool submitted) {
    if (p.empty()) return submitted ? "Enter your password." : "";
    if (signUp && p.size() < 6) return "Use at least 6 characters.";
    return "";
}
inline std::string CheckConfirm(const std::string& p, const std::string& c, bool submitted) {
    if (c.empty()) return submitted ? "Type the password again." : "";
    return p == c ? "" : "The passwords don't match.";
}

/// Can the step be sent? (Every check passes with `submitted` on.)
inline bool CanSubmit(Step s, bool emailAccounts, const std::string& id, const std::string& user, const std::string& email,
                      const std::string& pass, const std::string& confirm) {
    switch (s) {
    case Step::SignIn: return CheckIdentifier(id, true).empty() && CheckPassword(pass, false, true).empty();
    case Step::SignUp:
        return CheckUsername(user, true).empty() && (!emailAccounts || CheckEmail(email, true).empty()) &&
               CheckPassword(pass, true, true).empty() && CheckConfirm(pass, confirm, true).empty();
    case Step::Forgot: return CheckEmail(email, true).empty();
    default: return false;
    }
}

/// What a server answer means for the screen.
enum class Outcome { Error, CheckYourEmail, Unreachable };
struct Friendly { Outcome outcome = Outcome::Error; std::string text; };
inline std::string LowerCopy(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
/// The account client's error, in plain words (never codes, HTTP numbers or setup notes for developers).
inline Friendly FriendlyError(const std::string& raw) {
    const std::string e = LowerCopy(raw);
    auto has = [&](const char* k) { return e.find(k) != std::string::npos; };
    if (has("check your email to confirm")) return {Outcome::CheckYourEmail, "Check your email: we sent you a link to confirm your account."};
    if (has("couldn't reach") || has("unreachable") || has("offline"))
        return {Outcome::Unreachable, "Can't reach the OkaySpace server. Check your internet connection and try again."};
    if (has("invalid login") || has("invalid username/email or password") || has("incorrect password") || has("no account found") || has("invalid credentials"))
        return {Outcome::Error, "That email or username and password don't match. Check them and try again."};
    if (has("already registered") || has("already been registered") || has("already exists") || has("email address is already"))
        return {Outcome::Error, "There's already an account with that email. Sign in instead, or reset your password."};
    if (has("username") && (has("taken") || has("in use"))) return {Outcome::Error, "That username is taken. Try another one."};
    if (has("email not confirmed")) return {Outcome::Error, "Confirm your email first: open the link we sent you, then sign in."};
    if (has("password should be") || has("at least 6")) return {Outcome::Error, "Use at least 6 characters for your password."};
    if (has("rate limit") || has("too many") || has("429")) return {Outcome::Error, "Too many tries in a short time. Wait a minute, then try again."};
    if (has("valid email")) return {Outcome::Error, "That doesn't look like an email address."};
    if (raw.empty() || has("http") || has("supabase") || has("server response") || has("rejected"))
        return {Outcome::Error, "Something went wrong on the OkaySpace server. Try again in a moment."};
    return {Outcome::Error, raw};   // already a plain sentence from our own client ("Choose a username." ...)
}

/// Where the welcome screen's parts go in a window `w` x `h`: an art panel on the left at the wide sizes, the logo and the
/// card centred in the rest. The card is `cardW` wide (narrower when the window is) and `cardH` high.
struct Rect { float x = 0, y = 0, w = 0, h = 0; float R() const { return x + w; } float B() const { return y + h; } };
struct Welcome { bool art = false; Rect artPanel, logo, card; };
inline Welcome LayoutWelcome(float w, float h, float cardW, float cardH, float logoH, float gap, float artMinW) {
    Welcome l;
    l.art = w >= artMinW;
    const float artW = l.art ? std::floor(w * 0.38f) : 0.0f;
    l.artPanel = Rect{0, 0, artW, h};
    const float areaX = artW, areaW = w - artW;
    const float cw = std::min(cardW, std::max(0.0f, areaW - 2 * gap));
    const float total = logoH + gap + cardH;
    const float top = std::max(gap, (h - total) * 0.5f);
    l.logo = Rect{areaX + (areaW - cw) * 0.5f, top, cw, logoH};
    l.card = Rect{areaX + (areaW - cw) * 0.5f, top + logoH + gap, cw, cardH};
    return l;
}

}  // namespace okay::authform
