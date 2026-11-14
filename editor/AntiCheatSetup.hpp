#pragma once
// ---------------------------------------------------------------------------
// Easy anti-cheat setup for the editor: presets, the "Add Anti-Cheat" action and the
// "Anti-cheat checklist". All of it is plain logic over a Scene and a project folder, with no
// UI, so it can be unit tested (tests/test_anticheat_setup.cpp). The editor's windows and menu
// entries that use it are in editor/AntiCheatSetup.cpp.
//
// What this does NOT do: it never turns anti-cheat on by itself for an existing project, and it
// touches no cryptography. Anti-cheat stays opt-in: nothing here runs until a game maker clicks.
// ---------------------------------------------------------------------------
#include "okay/Components/AntiCheatConfig.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Net/AntiCheat.hpp"
#include "okay/Net/DetectInspector.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/SecureChannel.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace okay {
namespace acsetup {

// ---- presets -----------------------------------------------------------------------------

enum class Preset { CasualCoop = 0, Competitive = 1, Economy = 2 };
constexpr int kPresetCount = 3;

struct PresetInfo {
    const char* name;
    const char* blurb;   ///< one plain sentence, shown under the picker
    const char* tip;     ///< the tooltip: what it sets and why
};

inline const PresetInfo& Info(Preset p) {
    static const PresetInfo v[kPresetCount] = {
        {"Casual co-op", "Friends playing together. Generous limits, so honest players are never in the way.",
         "Max speed 20 u/s, 60 SyncVar writes/s, 30 calls/s per RPC, kick after 10 refusals a minute. Only the server may write "
         "'score' and 'match.' variables. No clock check. The detection engine watches and keeps evidence but never kicks."},
        {"Competitive", "Players race or fight each other. Tighter limits and a clock check.",
         "Max speed 12 u/s, 30 SyncVar writes/s, 20 calls/s per RPC, 3 spawns/s, kick after 5 refusals a minute, game clock may "
         "run 5% off. Only the server may write score, health, ammo, kills, deaths and 'match.' variables. The detection engine "
         "watches and keeps evidence but never kicks until you read it and raise a threshold."},
        {"Economy game", "Money, items or a shop. The server owns every balance.",
         "Max speed 20 u/s, 20 SyncVar writes/s, 10 calls/s per RPC, 2 spawns/s, kick after 3 refusals a minute. Only the server may "
         "write money, gold, coins, gems, inventory., shop. and score. The detection engine watches and keeps evidence but never kicks."},
    };
    return v[static_cast<int>(p)];
}

/// What the player's session is allowed to be spawned from, kept apart from the numbers: a
/// project's networked prefabs, as Assets-relative paths ("prefabs/crate.okayprefab").
using PrefabList = std::vector<std::string>;

/// Fill `s` from a preset. Keeps what a preset has no opinion on: the ticket check, the weapon
/// list and the "tickets" fields. Turns the detection engine ON in observe mode (no kicks, no bans).
/// `libsodium`: whether this build can encrypt (requireEncryption is only set when it can).
inline void ApplyPreset(AntiCheatSettings& s, Preset p, const PrefabList& spawnPrefabs,
                        bool libsodium = net::SecureChannel::Available()) {
    const AntiCheatSettings keep = s;   // what survives the preset
    s = AntiCheatConfig::DefaultSettings();
    s.enabled = true;
    s.requireEncryption = libsodium;
    s.ownerOnlySyncVars = true;
    s.spawnAllowlist = spawnPrefabs;
    s.ticketKey = keep.ticketKey; s.ticketAudience = keep.ticketAudience; s.ticketAnyAudience = keep.ticketAnyAudience;
    s.weapons = keep.weapons;
    s.clientVars = keep.clientVars;   // a game maker's own "clients may write" list is theirs
    switch (p) {
    case Preset::CasualCoop:
        s.maxSyncSpeed = 20.0f; s.maxSyncVarsPerSecond = 60; s.maxRpcsPerSecond = 30; s.kickAfterViolations = 10;
        s.maxSpawnsPerSecond = 5; s.maxLiveSpawnsPerPeer = 50; s.maxClockDrift = 0.0f;
        s.serverVars = {"score", "match."};
        break;
    case Preset::Competitive:
        s.maxSyncSpeed = 12.0f; s.maxSyncVarsPerSecond = 30; s.maxRpcsPerSecond = 20; s.kickAfterViolations = 5;
        s.maxSpawnsPerSecond = 3; s.maxLiveSpawnsPerPeer = 20; s.maxClockDrift = 0.05f;
        s.serverVars = {"score", "health", "ammo", "kills", "deaths", "match."};
        break;
    case Preset::Economy:
        s.maxSyncSpeed = 20.0f; s.maxSyncVarsPerSecond = 20; s.maxRpcsPerSecond = 10; s.kickAfterViolations = 3;
        s.maxSpawnsPerSecond = 2; s.maxLiveSpawnsPerPeer = 10; s.maxClockDrift = 0.0f;
        s.serverVars = {"money", "gold", "coins", "gems", "inventory.", "shop.", "score"};
        break;
    }
    // The detection engine: on, watching only. Nobody is kicked, restricted or reported for a ban
    // until the game maker has read the evidence from real matches and raised a threshold.
    s.detect = DetectSettings{};
    s.detect.enabled = true;
    detectui::ObserveMemory mem;
    detectui::SetObserveMode(s.detect, true, mem);
}

// ---- finding the project's networked prefabs -------------------------------------------------

/// True when a prefab's text holds a Network Sync (the saved line is "netsync ...").
inline bool PrefabTextIsNetworked(const std::string& text) {
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        std::size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (line.compare(i, 7, "netsync") == 0 && (i + 7 == line.size() || line[i + 7] == ' ' || line[i + 7] == '\r')) return true;
    }
    return false;
}

/// Prefabs a game can spawn over the network: the Network Manager's own list first (what the game
/// already spawns), then every .okayprefab under <projectDir>/Assets that carries a Network Sync.
/// Assets-relative, forward slashes, no repeats, sorted after the manager's own.
inline PrefabList NetworkedPrefabs(const std::string& projectDir, const NetworkManager* nm) {
    PrefabList out;
    auto add = [&](const std::string& p) {
        if (!p.empty() && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    };
    if (nm) for (const auto& p : nm->spawnablePrefabs) add(p);
    if (projectDir.empty()) return out;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::path(projectDir) / "Assets";
    if (!fs::is_directory(root, ec)) return out;
    PrefabList found;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".okayprefab") continue;
        std::ifstream f(it->path(), std::ios::binary);
        if (!f) continue;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (text.size() > (8u << 20) || !PrefabTextIsNetworked(text)) continue;
        found.push_back(it->path().lexically_relative(root).generic_string());
    }
    std::sort(found.begin(), found.end());
    for (const auto& p : found) add(p);
    return out;
}

// ---- finding things in the scene ---------------------------------------------------------------

inline NetworkManager* FindManager(Scene& scene) {
    for (const auto& go : scene.Objects())
        if (auto* nm = go->GetComponent<NetworkManager>()) return nm;
    return nullptr;
}
inline AntiCheatConfig* FindConfig(Scene& scene) {
    AntiCheatConfig* first = nullptr;
    for (const auto& go : scene.Objects())
        for (auto* ac : go->GetComponents<AntiCheatConfig>()) {
            if (ac->settings.enabled) return ac;   // the one that does something
            if (!first) first = ac;
        }
    return first;
}

struct AddResult {
    AntiCheatConfig* config = nullptr;
    bool added = false;          ///< false: the scene already had one (nothing was changed)
    bool hasManager = false;     ///< a Network Manager exists in the scene (without one nothing is checked)
    std::string message;         ///< one plain sentence for the status line
};

/// "Add Anti-Cheat": a safe preset on the object with the scene's Network Manager (else on `fallback`).
/// Does nothing when the scene already has an Anti-Cheat Config. Never adds a Network Manager.
inline AddResult AddAntiCheat(Scene& scene, GameObject* fallback, Preset preset, const std::string& projectDir) {
    AddResult r;
    NetworkManager* nm = FindManager(scene);
    r.hasManager = nm != nullptr;
    if (AntiCheatConfig* have = FindConfig(scene)) {
        r.config = have;
        r.message = "This scene already has an Anti-Cheat Config. Change it in the Inspector, or pick a preset there.";
        return r;
    }
    GameObject* host = nm ? nm->gameObject : fallback;
    if (!host) { r.message = "Select an object first (or add a Network Manager), then add anti-cheat."; return r; }
    auto* ac = host->AddComponent<AntiCheatConfig>();
    ApplyPreset(ac->settings, preset, NetworkedPrefabs(projectDir, nm));
    r.config = ac; r.added = true;
    r.message = std::string("Added Anti-Cheat Config with the \"") + Info(preset).name + "\" preset" +
                (nm ? "." : ". This scene has no Network Manager yet: add one, or nothing is checked.");
    return r;
}

} // namespace acsetup
} // namespace okay
