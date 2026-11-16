#pragma once
// ---------------------------------------------------------------------------
// CheatSim.hpp : a cheat simulator you can run on purpose.
//
// RunCheatSim starts a headless server on this machine (loopback, nothing leaves
// it) and points a "rogue client" at it. The rogue is raw network packets, so it
// can do what a modified game would: move at 60 units a second, jump 500 units,
// send NaN, write a server-only variable, flood messages, claim a hit with a
// weapon that doesn't exist, press a button on a perfect timer. Each cheat gets
// a fresh server with the anti-cheat rules on; the report says whether the server
// caught it and how. A normal client plays next to the cheat and must be left
// alone. A last scenario shows the ban list: a cheater is banned, the server is
// restarted, and the cheater is still refused. The ban list and audit log it
// writes are real files you can open.
//
// Run it from the dedicated server: `OkaySpacePlayer.exe --server --cheat-sim`
// (docs/try_it.md has the steps). The tests (tests/test_anticheat_hard.cpp) run it
// too. Written from docs/anticheat.md; no outside code.
// ---------------------------------------------------------------------------
#include <cstdio>
#include <string>
#include <vector>

namespace okay {

struct CheatSimOptions {
    /// .okayac rules text for the servers ("" = the built-in rules, listed in docs/anticheat.md).
    std::string rules;
    /// Which cheats to run, comma separated ("" = all): speed, teleport, garbage, servervar, flood,
    /// hitclaim, macro, ban. (Aliases: OKAY_CHEAT_SIM in the environment fills this in the dedicated server.)
    std::string only;
    /// Folder for the ban list and audit log files ("" = a new folder in the system temp folder).
    std::string dir;
    /// Print a line per step here (nullptr = quiet).
    std::FILE* out = nullptr;
};

struct CheatSimResult {
    std::string name;      ///< "speed", "teleport", ...
    std::string what;      ///< what the rogue did, in plain words
    bool caught = false;   ///< the server flagged, refused or kicked it
    std::string how;       ///< what the server did (log style)
    bool honestHarmed = false;   ///< the normal client was flagged or kicked (always a failure)
};

struct CheatSimReport {
    std::vector<CheatSimResult> results;
    std::string banFile, auditFile;   ///< where the last scenario wrote them
    std::string auditText;            ///< the audit log's lines
    bool Ok() const {
        for (const auto& r : results) if (!r.caught || r.honestHarmed) return false;
        return !results.empty();
    }
};

CheatSimReport RunCheatSim(const CheatSimOptions& opts);

/// The built-in rules the simulator's servers use.
const char* CheatSimDefaultRules();

} // namespace okay
