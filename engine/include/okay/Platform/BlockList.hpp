#pragma once
// Server side of blocks + privacy settings (docs/sql/blocks.sql). Network calls:
// run them on a worker thread. The pure logic lives in Privacy.hpp.
#include "okay/Platform/Messages.hpp"
#include "okay/Platform/Privacy.hpp"

namespace okay {

class BlockList {
public:
    // What a call did. `missing`: the SQL file has not been run. `offline`: never
    // reached the server. `message` is a sentence for the UI (empty on success).
    struct Outcome { bool ok = false, missing = false, offline = false; std::string message; };

    explicit BlockList(okay::account::AccountService& acct) : acct_(acct) {}

    // Pure: turn a response into an Outcome (unit tested).
    static Outcome Classify(const okay::account::ApiResponse& r) {
        Outcome o;
        if (r.ok) { o.ok = true; return o; }
        if (!r.reached) { o.offline = true; o.message = "No connection to the server."; return o; }
        if (privacy::IsMissingTable(r.status, r.body)) { o.missing = true; o.message = privacy::SetupNote(); return o; }
        const privacy::Rule rule = privacy::ParseRule(r.body);
        if (rule != privacy::Rule::None) { o.message = privacy::RuleText(rule); return o; }
        o.message = r.status == 401 ? "Your sign-in expired. Sign in again."
                  : r.status == 403 ? "The server refused this (permission denied)."
                  : "The server returned an error (HTTP " + std::to_string(r.status) + ").";
        return o;
    }

    // The people I blocked (uid -> the name saved with the block).
    Outcome Fetch(std::set<std::string>& mine, std::map<std::string, std::string>& names) {
        if (!acct_.IsLoggedIn()) return {false, false, false, "Sign in to keep your block list on your account."};
        const std::string me = MyId();
        if (me.empty()) { Outcome o; o.offline = true; o.message = "Could not look up your account."; return o; }
        auto r = acct_.Api("/rest/v1/blocks?select=blocked,blocked_name&blocker=eq." + me, "GET");
        Outcome o = Classify(r);
        Note(o);
        if (!o.ok) return o;
        mine.clear(); names.clear();
        for (const auto& obj : Messages::SplitObjects(r.body)) {
            const std::string id = Messages::Field(obj, "blocked");
            if (id.empty()) continue;
            mine.insert(id);
            const std::string n = Messages::Field(obj, "blocked_name");
            if (!n.empty()) names[id] = n;
        }
        return o;
    }
    Outcome Block(const std::string& uid, const std::string& name) {
        if (!acct_.IsLoggedIn()) return {false, false, false, "Sign in to keep your block list on your account."};
        auto r = acct_.Api("/rest/v1/blocks?on_conflict=blocker,blocked", "POST",
                           "{\"blocked\":" + Messages::JStr(uid) + ",\"blocked_name\":" + Messages::JStr(name) + "}",
                           "Prefer: resolution=ignore-duplicates");
        Outcome o = Classify(r); Note(o); return o;
    }
    Outcome Unblock(const std::string& uid) {
        if (!acct_.IsLoggedIn()) return {false, false, false, "Sign in to keep your block list on your account."};
        auto r = acct_.Api("/rest/v1/blocks?blocked=eq." + uid, "DELETE");
        Outcome o = Classify(r); Note(o); return o;
    }

    // My saved privacy settings. `found` is false when I never saved any.
    Outcome FetchSettings(privacy::Settings& out, bool& found) {
        found = false;
        if (!acct_.IsLoggedIn()) return {false, false, false, "Sign in to keep privacy settings on your account."};
        auto r = acct_.Api("/rest/v1/privacy_settings?select=dm_from,profile_visibility,online_visibility,friend_requests,calls_from", "GET");
        Outcome o = Classify(r); NoteSettings(o);
        if (o.ok) found = privacy::SettingsFromBody(r.body, out);
        return o;
    }
    Outcome SaveSettings(const privacy::Settings& s) {
        if (!acct_.IsLoggedIn()) return {false, false, false, "Sign in to keep privacy settings on your account."};
        const std::string j = privacy::SettingsJson(s);
        auto r = acct_.Api("/rest/v1/privacy_settings?on_conflict=user_id", "POST", j, "Prefer: resolution=merge-duplicates");
        Outcome o = Classify(r); NoteSettings(o); return o;
    }

    privacy::Support BlocksSupport() const { return blocks_; }
    privacy::Support SettingsSupport() const { return settings_; }

private:
    okay::account::AccountService& acct_;
    privacy::Support blocks_ = privacy::Support::Unknown, settings_ = privacy::Support::Unknown;
    std::string myId_;
    void Note(const Outcome& o) { if (o.ok) blocks_ = privacy::Support::Yes; else if (o.missing) blocks_ = privacy::Support::Missing; }
    void NoteSettings(const Outcome& o) { if (o.ok) settings_ = privacy::Support::Yes; else if (o.missing) settings_ = privacy::Support::Missing; }
    std::string MyId() {
        if (myId_.empty()) {
            std::string enc;
            for (char c : acct_.CurrentSession().username) {
                if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') enc += c;
                else { char b[4]; std::snprintf(b, sizeof(b), "%%%02X", (unsigned char)c); enc += b; }
            }
            auto r = acct_.Api("/rest/v1/profiles?select=id&username=eq." + enc, "GET");
            if (r.ok) { auto o = Messages::SplitObjects(r.body); if (!o.empty()) myId_ = Messages::Field(o[0], "id"); }
        }
        return myId_;
    }
};

} // namespace okay
