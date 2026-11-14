#pragma once
// ---------------------------------------------------------------------------
// The editor's inspector section for the Anti-Cheat Config's `detect.*` settings
// (editor/main.cpp "Detection engine"): what it shows, kept apart from the UI so it
// can be unit tested (tests/test_anticheat_wiring.cpp):
//   * a field table (key, label, tooltip from docs/anticheat.md, range) that the
//     inspector draws, grouped, over DetectSettings members;
//   * the observe-mode toggle (no kick / ban request / shadow / rate limit: the
//     detectors still score and the evidence is kept, nothing is done to a player);
//   * the settings as ".okayac" text and back (the "detect.*" lines the scene file,
//     the .okayac file and the inspector's text box all share).
// Header-only; no UI, no engine state.
// ---------------------------------------------------------------------------
#include "okay/Net/CheatDetect.hpp"

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace okay {
namespace detectui {

/// One number the inspector shows. Exactly one of `f` / `i` is set.
struct Field {
    const char* group;       ///< section heading
    const char* key;         ///< the ".okayac" key after "detect."
    const char* label;
    const char* tip;         ///< tooltip (docs/anticheat.md, "Detection engine")
    float DetectSettings::* f = nullptr;
    int   DetectSettings::* i = nullptr;
    float lo = 0.0f, hi = 1000.0f, speed = 0.1f;
    const char* fmt = "%.2f";
};

inline const std::vector<Field>& Fields() {
    static const std::vector<Field> v = {
        // movement
        {"Movement", "maxSpeed", "Max speed", "Fastest legitimate horizontal speed in units/s: sprint + slope + any permanent buff, not the walking speed. 0 = the speed checks fall back to the Anti-Cheat Config's max speed.", &DetectSettings::maxSpeed, nullptr, 0, 1000, 0.1f, "%.1f u/s"},
        {"Movement", "speedTolerance", "Speed tolerance", "Allowed overshoot over a short window (0.5 s), for network noise: 1.25 = 25% over. The mid and long windows have their own (speedToleranceMid, speedToleranceLong).", &DetectSettings::speedTolerance, nullptr, 1, 10, 0.01f},
        {"Movement", "burstBankSeconds", "Burst bank", "How many seconds of travel a lag burst may bunch up (paid for by the silence before it).", &DetectSettings::burstBankSeconds, nullptr, 0, 30, 0.05f, "%.2f s"},
        {"Movement", "teleportDistance", "Teleport distance", "A jump this far beyond what the max speed allows is a teleport (heavy: flags on the first record).", &DetectSettings::teleportDistance, nullptr, 0, 10000, 0.5f, "%.1f u"},
        {"Movement", "gravity", "Gravity", "World gravity (units/s^2). 0 = the vertical checks (fly, hover) are off. Leave 0 for top-down games.", &DetectSettings::gravity, nullptr, 0, 1000, 0.1f, "%.1f"},
        {"Movement", "maxJumpHeight", "Max jump height", "Highest legitimate jump. 0 = the vertical checks are off.", &DetectSettings::maxJumpHeight, nullptr, 0, 1000, 0.05f, "%.2f u"},
        {"Movement", "stepHeight", "Step height", "Stairs and ledges walked up without a jump.", &DetectSettings::stepHeight, nullptr, 0, 100, 0.05f, "%.2f u"},
        {"Movement", "maxJumpsPerSecond", "Max jumps/s", "Jumps per second a player may make (0 = off).", &DetectSettings::maxJumpsPerSecond, nullptr, 0, 100, 0.1f, "%.1f"},
        // combat
        {"Combat", "fireRateTolerance", "Fire-rate tolerance", "A weapon's token bucket refills at shots/s x this. Weapons are listed in the text box below (detect.weapon name shots/s max damage ...).", &DetectSettings::fireRateTolerance, nullptr, 1, 10, 0.01f},
        {"Combat", "accuracyWindow", "Accuracy window", "Shots in the sliding accuracy window. 0 = the accuracy checks are off.", nullptr, &DetectSettings::accuracyWindow, 0, 1000, 1.0f, "%d shots"},
        {"Combat", "maxAccuracy", "Max accuracy", "Hit ratio above which a full window is suspicious. A skilled honest player reaches about 75%; the default 95% leaves a wide margin. Never below what your best players do.", &DetectSettings::maxAccuracy, nullptr, 0, 1, 0.01f},
        {"Combat", "maxHeadshotRatio", "Max headshot ratio", "Headshot share of hits above which it is suspicious (about 35% for a skilled honest player; default 80%).", &DetectSettings::maxHeadshotRatio, nullptr, 0, 1, 0.01f},
        {"Combat", "aimSnapDegrees", "Aim snap", "A one-sample turn at least this far, followed by a hit, is a snap-and-shoot; it takes aimSnapConfirm of them within aimSnapWithin seconds.", &DetectSettings::aimSnapDegrees, nullptr, 0, 360, 0.5f, "%.1f deg"},
        // economy
        {"Economy", "cooldownTolerance", "Cooldown tolerance", "An ability may be used again at cooldown x (1 - this), minus ping slack. Abilities: detect.ability name cooldown.", &DetectSettings::cooldownTolerance, nullptr, 0, 1, 0.01f},
        {"Economy", "economyTolerance", "Economy tolerance", "A claimed counter may exceed what the server's grants explain by this. Counters: detect.economy key [max gain/s].", &DetectSettings::economyTolerance, nullptr, 0, 100000, 0.5f},
        {"Economy", "maxItemsPerSecond", "Max items/s", "The same item acquired this fast is a duplication pattern (0 = off).", &DetectSettings::maxItemsPerSecond, nullptr, 0, 1000, 0.1f, "%.1f"},
        // network
        {"Network", "maxMessagesPerSecond", "Messages/s", "Sustained messages per second a client may send (0 = off).", &DetectSettings::maxMessagesPerSecond, nullptr, 0, 100000, 1.0f, "%.0f"},
        {"Network", "maxMalformedPerMinute", "Malformed/min", "Malformed packets a minute before it scores (0 = off).", &DetectSettings::maxMalformedPerMinute, nullptr, 0, 10000, 0.5f, "%.0f"},
        {"Network", "clockDrift", "Clock drift", "How far a client's game clock may run off the server's, as a fraction (0.10 = 10%).", &DetectSettings::clockDrift, nullptr, 0, 10, 0.01f},
        // suspicion model
        {"Risk", "riskHalfLife", "Risk half-life", "Risk halves every this many seconds, so honest players' noise fades.", &DetectSettings::riskHalfLife, nullptr, 1, 100000, 1.0f, "%.0f s"},
        {"Risk", "confirmations", "Confirmations", "Soft detectors need this many hits within confirmWindow before they score: one flick shot or one bumped wall does not.", nullptr, &DetectSettings::confirmations, 1, 100, 1.0f, "%d"},
        {"Risk", "evidenceCap", "Evidence kept", "Evidence records kept per player (what a kick / ban request report carries).", nullptr, &DetectSettings::evidenceCap, 1, 1000, 1.0f, "%d"},
        // responses
        {"Responses", "logAt", "Log at", "Risk at which the player is logged. 0 = that rung is off.", &DetectSettings::logAt, nullptr, 0, 100000, 1.0f, "%.0f"},
        {"Responses", "flagAt", "Flag at", "Risk at which the player is flagged for review. 0 = off.", &DetectSettings::flagAt, nullptr, 0, 100000, 1.0f, "%.0f"},
        {"Responses", "rateLimitAt", "Rate limit at", "Risk at which the player's message budget is cut (rateLimitFactor). 0 = off.", &DetectSettings::rateLimitAt, nullptr, 0, 100000, 1.0f, "%.0f"},
        {"Responses", "shadowAt", "Shadow-restrict at", "Risk at which the player's effect on others is restricted. 0 = off.", &DetectSettings::shadowAt, nullptr, 0, 100000, 1.0f, "%.0f"},
        {"Responses", "kickAt", "Kick at", "Risk at which the server kicks the player (a report is filed with --report-kicks). 0 = never.", &DetectSettings::kickAt, nullptr, 0, 100000, 1.0f, "%.0f"},
        {"Responses", "banAt", "Ban request at", "Risk at which the server asks for a ban (never banned automatically: an admin decides). 0 = never.", &DetectSettings::banAt, nullptr, 0, 100000, 1.0f, "%.0f"},
        // false positives
        {"False positives", "graceJoin", "Join grace", "Warm-up for a new player, in seconds, before the detectors judge.", &DetectSettings::graceJoin, nullptr, 0, 600, 0.1f, "%.1f s"},
        {"False positives", "pingTolerance", "Ping tolerance", "Every window and bucket loosens by this x the player's round trip (at most maxToleratedRtt, 0.4 s) plus 0.1 s.", &DetectSettings::pingTolerance, nullptr, 0, 10, 0.05f},
    };
    return v;
}

/// The field for a key ("" / unknown: nullptr).
inline const Field* FindField(const std::string& key) {
    for (const Field& f : Fields()) if (key == f.key) return &f;
    return nullptr;
}

/// A field's current value as the text a ".okayac" line carries.
inline std::string FieldValueText(const DetectSettings& d, const Field& f) {
    std::ostringstream o;
    if (f.i) o << d.*f.i; else o << d.*f.f;
    return o.str();
}
/// Set a field the same way a ".okayac" line would (same validation). False + error on a bad value.
inline bool SetField(DetectSettings& d, const std::string& key, const std::string& value, std::string* error = nullptr) {
    if (!FindField(key)) { if (error) *error = "unknown detect field " + key; return false; }
    return d.ParseLine(key, value, error);
}

// ---- observe mode ---------------------------------------------------------------
/// The rungs observe mode turns off: everything that does something to a player.
/// (Log and flag stay on: they only record.)
inline bool IsObserveMode(const DetectSettings& d) {
    return d.kickAt == 0.0f && d.banAt == 0.0f && d.shadowAt == 0.0f && d.rateLimitAt == 0.0f;
}
/// What was there before observe mode was switched on, so switching it off restores it.
struct ObserveMemory {
    float rateLimitAt = -1.0f, shadowAt = -1.0f, kickAt = -1.0f, banAt = -1.0f;   // -1 = none remembered
};
inline void SetObserveMode(DetectSettings& d, bool on, ObserveMemory& mem) {
    if (on) {
        if (!IsObserveMode(d)) { mem.rateLimitAt = d.rateLimitAt; mem.shadowAt = d.shadowAt; mem.kickAt = d.kickAt; mem.banAt = d.banAt; }
        d.rateLimitAt = d.shadowAt = d.kickAt = d.banAt = 0.0f;
        return;
    }
    const DetectSettings def;   // nothing remembered (a file said so): back to the defaults
    d.rateLimitAt = mem.rateLimitAt >= 0.0f ? mem.rateLimitAt : def.rateLimitAt;
    d.shadowAt = mem.shadowAt >= 0.0f ? mem.shadowAt : def.shadowAt;
    d.kickAt = mem.kickAt >= 0.0f ? mem.kickAt : def.kickAt;
    d.banAt = mem.banAt >= 0.0f ? mem.banAt : def.banAt;
    mem = ObserveMemory{};
}

// ---- text round trip ---------------------------------------------------------------
/// The settings as editable text: one "detect.<key> <value>" per line, only what differs
/// from the defaults (DetectSettings::ToText), with "detect.enabled" always first.
inline std::string ToText(const DetectSettings& d) {
    std::string t = std::string("detect.enabled ") + (d.enabled ? "true" : "false") + "\n";
    std::istringstream in(d.ToText());
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("detect.enabled", 0) == 0 || line.empty()) continue;
        t += line + "\n";
    }
    return t;
}
/// Apply text written by ToText (or by hand: "#" comments, blank lines, "detect." optional
/// is NOT accepted: every line starts with detect.). All or nothing: on a bad line `d` is
/// left as it was and `error` names the line. Keys not in the text return to their defaults
/// (the text is the whole picture), except that nothing else in `d` is touched.
inline bool FromText(DetectSettings& d, const std::string& text, std::string* error = nullptr) {
    DetectSettings next;   // defaults + the text
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const std::size_t a = line.find_first_not_of(" \t\r"), b = line.find_last_not_of(" \t\r");
        if (a == std::string::npos) continue;
        line = line.substr(a, b - a + 1);
        if (line.rfind("detect.", 0) != 0) { if (error) *error = "line " + std::to_string(n) + ": expected detect.<key> <value>"; return false; }
        const std::size_t sp = line.find_first_of(" \t");
        const std::string key = line.substr(7, sp == std::string::npos ? std::string::npos : sp - 7);
        std::string value = sp == std::string::npos ? std::string() : line.substr(sp + 1);
        const std::size_t v0 = value.find_first_not_of(" \t");
        value = v0 == std::string::npos ? std::string() : value.substr(v0);
        std::string why;
        if (!next.ParseLine(key, value, &why)) { if (error) *error = "line " + std::to_string(n) + ": " + why; return false; }
    }
    d = std::move(next);
    return true;
}

} // namespace detectui
} // namespace okay
