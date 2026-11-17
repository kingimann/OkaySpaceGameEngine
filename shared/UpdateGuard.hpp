#pragma once
// Two small guards around the signed release list (SEC1 item 3). Docs: docs/security.md "Updater signing" and
// docs/updater.md "Signed releases". Header only, no threads, free of engine / SDL / ImGui code.
//
//  1. SignatureStatus(): the one-line answer to "is the update signature check on?", for the start-up log line and the
//     Settings page of the launcher and the editor. With no public key built in the check is OFF and the line says so
//     ("update signature check: OFF (no key configured)"): an unsigned feed is accepted, as in every build before this
//     one, but it is never reported as if it were verified.
//  2. The replay floor: a program that has once accepted a VALIDLY SIGNED list remembers the newest version that list
//     called "latest" (okay_update_floor.txt beside the program). A later list whose "latest" is OLDER is refused: an
//     attacker who can only replay an old, really signed list (a stale mirror, a hijacked cache) cannot roll the
//     program back to a version that was published and later withdrawn for a security fix. Only a list that verified
//     under a trusted key can raise the floor, so an unsigned or forged list can never make it unreasonably high.
//     To withdraw a bad release, publish a NEWER fixed version; never republish an older head. A person who really
//     needs to go back deletes the floor file.
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include "ProjectVersion.hpp"
#include "UpdateSignature.hpp"

namespace okayupdate {

enum class SigState { Off, On, Broken };
struct SigStatus {
    SigState state = SigState::Off;
    int keys = 0;
    std::string line;   ///< "update signature check: ON (1 key built in)" and so on
};

inline SigStatus SignatureStatus(const std::string& trustedKeys) {
    SigStatus s;
    if (trustedKeys.empty()) {
        s.line = "update signature check: OFF (no key configured)";
        return s;
    }
    std::vector<okayac::ed25519::PublicKey> keys;
    std::string why;
    if (!ParseKeys(trustedKeys, keys, &why)) {
        s.state = SigState::Broken;
        s.line = "update signature check: BROKEN (" + why + "); every update is refused until a fresh installer is used";
        return s;
    }
    s.state = SigState::On;
    s.keys = static_cast<int>(keys.size());
    s.line = "update signature check: ON (" + std::to_string(s.keys) + (s.keys == 1 ? " key built in)" : " keys built in)");
    return s;
}

inline constexpr const char* kFloorName = "okay_update_floor.txt";

/// The remembered version ("" = none, or unreadable / not a version: never a reason to refuse).
inline std::string ReadFloor(const std::filesystem::path& file) {
    std::ifstream f(file);
    std::string line;
    if (!f || !std::getline(f, line)) return {};
    if (line.size() > 64) return {};
    const std::string v = okayver::NormalizeVersion(line);
    return okayver::CompareVersions(v, "0.0.1") > 0 ? v : std::string();
}

inline bool WriteFloor(const std::filesystem::path& file, const std::string& version, std::string* err = nullptr) {
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::trunc);
    if (f) f << version << "\n";
    if (f && f.flush()) return true;
    if (err) *err = "could not write " + file.string();
    return false;
}

/// "" when a signed list whose newest version is `latest` is acceptable against `floor`, else the words for the person.
inline std::string ReplayProblem(const std::string& floor, const std::string& latest) {
    if (floor.empty() || latest.empty()) return {};
    if (okayver::CompareVersions(latest, floor) >= 0) return {};
    return "Update refused: the release list is older than one this program already accepted (it names v" + latest +
           " as the newest, but v" + floor + " was seen before). A validly signed old list may be being replayed. Nothing was installed and "
           "your current version is unchanged. Try again later; if it keeps happening, download the full installer from the official release page.";
}

}  // namespace okayupdate
