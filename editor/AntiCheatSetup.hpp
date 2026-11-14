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

// ---- the checklist -----------------------------------------------------------------------------

enum class Status { Ok, Warn, Missing, Info };   ///< Info = optional, nothing wrong
enum class Fix {
    None,
    AddManager,        ///< add a Network Manager (it starts doing nothing: Auto Start None)
    AddConfig,         ///< add an Anti-Cheat Config with the Casual co-op preset
    EnableConfig,      ///< tick Enabled
    EnableEncryption,  ///< require encryption
    ServerAuthority,   ///< owner-only SyncVars on, and a max speed when there is none
    ManifestReport,    ///< Build Settings "Anti-cheat" -> Report (checks files, never blocks a player)
    DetectObserve,     ///< detection engine on, in observe mode (no kicks)
};

struct Item {
    const char* id;          ///< stable id ("encryption", "tickets", ...)
    std::string title;
    Status status = Status::Info;
    std::string detail;      ///< plain words: what it is, why it matters, what to do
    Fix fix = Fix::None;
    const char* fixLabel = "";
    std::vector<std::string> objects;   ///< scene objects the item is about (unclaimed list)
};

/// What the checklist looks at: a snapshot, so the rule logic needs no scene.
struct Facts {
    bool hasManager = false;
    bool hasConfig = false;
    AntiCheatSettings rules;           ///< the effective rules (the .okayac file's when it loads, else the component's)
    std::string rulesFile;             ///< the component's `file` ("" = inline)
    bool rulesFromFile = false;        ///< the rules above came from the .okayac file, so the component's own fields do nothing
    std::string rulesFileError;        ///< set when `rulesFile` does not load (the server then refuses every join)
    bool authoritativeMovement = false;
    bool libsodium = false;
    int  manifestMode = 0;             ///< Build Settings "Anti-cheat": 0 Off, 1 Report, 2 Enforce
    std::vector<std::string> unclaimed;   ///< "Name (netId)" of scene objects nobody owns and players cannot claim
};

inline std::string ResolveRulesFile(const std::string& file, const std::string& projectDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (file.empty()) return file;
    if (fs::is_regular_file(file, ec)) return file;
    for (const char* sub : {"", "Assets"}) {
        const fs::path c = fs::path(projectDir) / sub / file;
        if (!projectDir.empty() && fs::is_regular_file(c, ec)) return c.string();
    }
    return file;
}

/// A scene object a player might drive (not "the server drives it") that no player owns and none may claim.
/// With anti-cheat on, such an object belongs to the server: players cannot move it or write its variables.
/// Off-by-default objects are skipped (a player template is cloned at run time and each clone gets an owner).
inline bool IsUnclaimed(const GameObject& go, const NetworkSync& ns) {
    return ns.authority != NetworkSync::Authority::Host && !ns.clientClaimable && go.ActiveInHierarchy();
}

inline Facts GatherFacts(Scene& scene, int manifestMode, const std::string& projectDir) {
    Facts f;
    f.libsodium = net::SecureChannel::Available();
    f.manifestMode = std::max(0, std::min(2, manifestMode));
    if (NetworkManager* nm = FindManager(scene)) { f.hasManager = true; f.authoritativeMovement = nm->authoritativeMovement; }
    if (AntiCheatConfig* ac = FindConfig(scene)) {
        f.hasConfig = true;
        f.rules = ac->settings;
        f.rulesFile = ac->file;
        if (!ac->file.empty()) {
            AntiCheatSettings fromFile;
            if (fromFile.LoadFile(ResolveRulesFile(ac->file, projectDir), &f.rulesFileError)) { f.rules = fromFile; f.rulesFromFile = true; f.rulesFileError.clear(); }
        }
    }
    for (const auto& go : scene.Objects())
        for (auto* ns : go->GetComponents<NetworkSync>())
            if (IsUnclaimed(*go, *ns))
                f.unclaimed.push_back(go->name + (ns->netId.empty() ? std::string() : " (" + ns->netId + ")"));
    return f;
}

/// The checklist, in the order a game maker should read it. Pure.
inline std::vector<Item> Evaluate(const Facts& f) {
    std::vector<Item> v;
    auto add = [&](const char* id, const std::string& title, Status st, const std::string& detail, Fix fix = Fix::None, const char* label = "") -> Item& {
        Item it; it.id = id; it.title = title; it.status = st; it.detail = detail; it.fix = fix; it.fixLabel = label;
        v.push_back(std::move(it));
        return v.back();
    };

    add("manager", "Network Manager in the scene", f.hasManager ? Status::Ok : Status::Missing,
        f.hasManager ? "The scene has a Network Manager, so the checks have something to guard."
                     : "Anti-cheat guards a Network Manager's server. Without one in this scene nothing is checked.",
        f.hasManager ? Fix::None : Fix::AddManager, "Add Network Manager");

    const bool on = f.hasConfig && f.rules.enabled;
    if (!f.hasConfig)
        add("config", "Anti-Cheat Config", Status::Missing,
            "Anti-cheat is off. Players could move other players' objects, spawn anything and write any variable.",
            Fix::AddConfig, "Add Anti-Cheat (Casual co-op)");
    else if (!f.rules.enabled)
        add("config", "Anti-Cheat Config", Status::Missing, "The Anti-Cheat Config is there but Enabled is off, so nothing is checked.", Fix::EnableConfig, "Turn on");
    else
        add("config", "Anti-Cheat Config", Status::Ok, "On: the server refuses what a client may not do (moving others' objects, forged spawns, speed hacks).");

    if (!f.rulesFile.empty()) {
        if (!f.rulesFileError.empty())
            add("file", "Rules file loads", Status::Missing,
                "The rules file \"" + f.rulesFile + "\" does not load (" + f.rulesFileError + "). The server then refuses every join. Fix the file or clear the Rules file field.");
        else
            add("file", "Rules file loads", Status::Ok, "\"" + f.rulesFile + "\" loads, and its rules replace the component's own numbers.");
    }

    // Server authority: clients cannot write each other's variables, and the server checks how fast things move.
    if (on) {
        const bool ownerOnly = f.rules.ownerOnlySyncVars;
        const bool moveCheck = f.rules.maxSyncSpeed > 0.0f || f.authoritativeMovement;
        if (ownerOnly && moveCheck)
            add("authority", "Server authority", Status::Ok,
                std::string("The server decides: variables are owner-only and movement is checked") +
                (f.rules.maxSyncSpeed > 0.0f ? " (max " + std::to_string(static_cast<int>(f.rules.maxSyncSpeed)) + " u/s)." : " (the server moves players)."));
        else
            add("authority", "Server authority", Status::Warn,
                std::string(ownerOnly ? "" : "Any player may write another player's variables. ") +
                    (moveCheck ? "" : "Nothing checks how fast players move, so a speed hack goes through. ") + "Turn on the safe defaults.",
                Fix::ServerAuthority, "Turn on server authority");
    }

    if (on) {
        if (f.rules.requireEncryption)
            add("encryption", "Encryption required", Status::Ok, "Only encrypted clients may join, and replayed packets are dropped.");
        else if (f.libsodium)
            add("encryption", "Encryption required", Status::Warn,
                "Players can join without encryption, so someone on the network can read or replay their packets.", Fix::EnableEncryption, "Require encryption");
        else
            add("encryption", "Encryption required", Status::Warn,
                "Off, and this editor build has no encryption library (libsodium), so it cannot be switched on here. Use a build with it before you ship.");
    }

    if (on) {
        const bool key = !f.rules.ticketKey.empty();
        if (key && f.rules.ticketAudience.empty())
            add("tickets", "Session tickets", Status::Missing,
                "A ticket key is set but no Ticket Audience (this server's id). The server refuses every join and a dedicated server will not start. Set ticketAudience in the .okayac rules.");
        else if (key)
            add("tickets", "Session tickets", Status::Ok,
                "Players must hold a ticket signed by your ticket service (server id \"" + f.rules.ticketAudience + "\").");
        else
            add("tickets", "Session tickets", Status::Info,
                "Optional. Needs a ticket service you deploy yourself, then its public key as ticketKey in the .okayac rules. Without it, anyone can join.");
    }

    {
        static const char* modes[3] = {"Off", "Report", "Enforce"};
        const std::string title = std::string("Client file check (Build Settings: ") + modes[f.manifestMode] + ")";
        if (f.manifestMode == 0)
            add("manifest", title, Status::Warn,
                "Builds do not check that the player's game files are the ones you shipped. Report lists changed files and never blocks a player: a safe first step.",
                Fix::ManifestReport, "Set to Report");
        else if (f.manifestMode == 1)
            add("manifest", title, Status::Ok, "The player checks its files against a signed list and reports changes without blocking anyone. Switch to Enforce when you trust it.");
        else
            add("manifest", title, Status::Ok, "The player refuses to start a game with changed files.");
    }

    if (on) {
        const DetectSettings& d = f.rules.detect;
        if (!d.enabled)
            add("detect", "Detection engine", Status::Warn,
                "Off. It scores suspicious behaviour (teleports, impossible fire rates, money from nowhere) and keeps evidence. Start in observe mode: it never kicks anyone.",
                Fix::DetectObserve, "Turn on (observe mode)");
        else if (detectui::IsObserveMode(d))
            add("detect", "Detection engine", Status::Ok,
                "On, in observe mode: it keeps evidence and kicks nobody. Read it after real matches, then raise a threshold when you trust it.");
        else
            add("detect", "Detection engine", Status::Ok, "On, and it may kick or ban-request players above its thresholds.");
    }

    if (on) {
        if (f.unclaimed.empty())
            add("unclaimed", "Scene objects nobody owns", Status::Ok, "Every Network Sync object is either driven by the server or marked claimable.");
        else {
            Item& it = add("unclaimed", "Scene objects nobody owns", Status::Warn,
                           std::to_string(f.unclaimed.size()) + " object(s) that a player might drive have no owner and are not claimable, so they belong to the server: players cannot move them "
                           "or write their variables. For a ball or a pickup that players should grab, tick \"Players can claim it\" on its Network Sync. "
                           "Players that the Network Player Spawner creates are fine.");
            it.objects = f.unclaimed;
        }
    }
    if (f.rulesFromFile)   // those numbers live in the file: a button on the component would change nothing
        for (Item& it : v)
            if (it.fix == Fix::EnableConfig || it.fix == Fix::EnableEncryption || it.fix == Fix::ServerAuthority || it.fix == Fix::DetectObserve) {
                it.fix = Fix::None;
                it.detail += " These rules come from the rules file: edit them there.";
            }
    return v;
}

struct Summary { int ok = 0, warn = 0, missing = 0, info = 0; };
inline Summary Summarize(const std::vector<Item>& items) {
    Summary s;
    for (const auto& i : items) {
        switch (i.status) { case Status::Ok: ++s.ok; break; case Status::Warn: ++s.warn; break;
                            case Status::Missing: ++s.missing; break; case Status::Info: ++s.info; break; }
    }
    return s;
}

/// Do a checklist item's fix. `manifestMode` is Build Settings' Anti-cheat combo (0..2). True when
/// something was changed. Each fix only makes the project safer; none turns a kick or ban on.
inline bool ApplyFix(Scene& scene, Fix fix, int& manifestMode, const std::string& projectDir, GameObject* fallback = nullptr) {
    switch (fix) {
    case Fix::None: return false;
    case Fix::AddManager: {
        if (FindManager(scene)) return false;
        GameObject* host = fallback ? fallback : scene.CreateGameObject("Network Manager");
        host->AddComponent<NetworkManager>();
        return true;
    }
    case Fix::AddConfig: return AddAntiCheat(scene, fallback, Preset::CasualCoop, projectDir).added;
    case Fix::ManifestReport: if (manifestMode != 0) return false; manifestMode = 1; return true;
    default: break;
    }
    AntiCheatConfig* ac = FindConfig(scene);
    if (!ac) return false;
    AntiCheatSettings& s = ac->settings;
    switch (fix) {
    case Fix::EnableConfig: if (s.enabled) return false; s.enabled = true; return true;
    case Fix::EnableEncryption:
        if (s.requireEncryption || !net::SecureChannel::Available()) return false;
        s.requireEncryption = true; return true;
    case Fix::ServerAuthority: {
        bool changed = !s.ownerOnlySyncVars || s.maxSyncSpeed <= 0.0f;
        s.ownerOnlySyncVars = true;
        if (s.maxSyncSpeed <= 0.0f) s.maxSyncSpeed = 20.0f;
        return changed;
    }
    case Fix::DetectObserve: {
        if (s.detect.enabled && detectui::IsObserveMode(s.detect)) return false;
        const bool was = s.detect.enabled;
        s.detect.enabled = true;
        detectui::ObserveMemory mem;
        detectui::SetObserveMode(s.detect, true, mem);
        (void)was;
        return true;
    }
    default: return false;
    }
}

} // namespace acsetup
} // namespace okay
