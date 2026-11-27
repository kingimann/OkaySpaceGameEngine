#pragma once
// Anti-cheat backend pages (AntiCheatPages.cpp): Admin > Anti-cheat, the player's own ban and appeal, "Report cheating"
// and the session-ticket check before a join. Moved out of launcher/main.cpp unchanged (docs/engine/modularization_plan.md,
// Part C section 19, batch L2).
#include "LauncherShared.hpp"
#include "okay/AntiCheat/AntiCheatBackend.hpp"   // okayac::backend (acb)
#include "okay/Net/JoinLink.hpp"

namespace okl {

namespace acb = okayac::backend;
struct AcMeJob { bool hasAction = false; acb::Request action; std::string okMsg; };

// What main.cpp uses from AntiCheatPages.cpp.
void DrawAntiCheatAdmin();                 // Admin > Anti-cheat
void AcMeQueue(AcMeJob* j);                // send one of the player's own anti-cheat requests (report, appeal) in the background
void PumpAcMyBans(bool signedIn);          // keep the player's own anti-cheat bans fresh (each frame)
bool TicketPreflightHolds(const okay::JoinLink& l, const std::string& scene);   // true while the ban / ticket check of a join runs
void PumpTicketPreflight();                // finish a ticket check (each frame)
void DrawAcBanBanner();                    // the banner for the player's own ban, with "Appeal"
} // namespace okl
