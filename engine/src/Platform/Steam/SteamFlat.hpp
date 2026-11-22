#pragma once
// The result records of Steam's asynchronous leaderboard calls, as the Store Kit's run-time Steam backend
// (DynamicSteamService.cpp) reads them from the developer's steam_api64.dll, and as the test stand-in library
// (tests/fake_steam_api.cpp) writes them.
//
// Written by us from Valve's public Steamworks documentation (ISteamUserStats: FindOrCreateLeaderboard,
// UploadLeaderboardScore, DownloadLeaderboardEntries, GetDownloadedLeaderboardEntry and their result callbacks;
// ISteamUtils: IsAPICallCompleted, GetAPICallResult). No SDK header was copied or consulted while writing it.
//
// UNCONFIRMED (from memory of the documentation, never checked against the real library): the callback numbers
// (1104, 1105, 1106 = the user-stats base 1100 + 4, 5, 6) and the field layout below. The layout is the plain
// 64-bit Windows one (8-byte alignment, which is what Valve's 64-bit Windows packing gives); 64-bit Linux and macOS
// builds of Steam pack callbacks to 4 bytes, so these records would differ there (Linux and macOS are parked).
#include <cstdint>

namespace okay::steamflat {

using APICall = std::uint64_t;               // SteamAPICall_t (0 = invalid)
using LeaderboardHandle = std::uint64_t;     // SteamLeaderboard_t
using EntriesHandle = std::uint64_t;         // SteamLeaderboardEntries_t

constexpr int kLeaderboardFindResult = 1104;
constexpr int kLeaderboardScoresDownloaded = 1105;
constexpr int kLeaderboardScoreUploaded = 1106;

constexpr int kSortDescending = 2;           // ELeaderboardSortMethod: higher is better
constexpr int kDisplayNumeric = 1;           // ELeaderboardDisplayType
constexpr int kUploadKeepBest = 1;           // ELeaderboardUploadScoreMethod
constexpr int kRequestGlobal = 0;            // ELeaderboardDataRequest

struct LeaderboardFindResult {
    LeaderboardHandle board;
    std::uint8_t found;
};
struct LeaderboardScoresDownloaded {
    LeaderboardHandle board;
    EntriesHandle entries;
    std::int32_t count;
};
struct LeaderboardScoreUploaded {
    std::uint8_t success;
    LeaderboardHandle board;
    std::int32_t score;
    std::uint8_t changed;
    std::int32_t rankNew;
    std::int32_t rankPrevious;
};
struct LeaderboardEntry {                    // LeaderboardEntry_t
    std::uint64_t steamId;
    std::int32_t globalRank;
    std::int32_t score;
    std::int32_t detailCount;
    std::uint64_t ugc;
};

}  // namespace okay::steamflat
