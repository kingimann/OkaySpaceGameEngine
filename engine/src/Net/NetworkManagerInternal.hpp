#pragma once
// Private to engine/src/Net/NetworkManager*.cpp: the message types, constants and small packet helpers every
// part of NetworkManager uses. They sit in an anonymous namespace, so each .cpp keeps its own file-local copy
// (they hold no state). Moved unchanged from NetworkManager.cpp (docs/engine/modularization_plan.md, batch N1).
#include "okay/Net/Packet.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/UdpSocket.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

namespace {
    enum Msg : std::uint8_t {
        Join = 1, Welcome = 2, State = 3, Snapshot = 4, Leave = 5,
        Message = 6, DirectMessage = 7, SyncVar = 8, Ping = 9, Pong = 10,
        Ready = 11, Reject = 12, Kicked = 13, ReliableMsg = 14, ReliableAck = 15,
        Fragment = 16,  // one piece of a message too large for a single datagram
        Query = 17, QueryInfo = 18,  // server browser: "who are you?" without joining
        Correct = 19,                // server -> client: you're here (move rejected / teleported)
        PlayerQuery = 20, PlayerList = 21, PlayerChallenge = 22,   // server browser: who's playing
        RosterMsg = 23,              // server -> clients: everyone in the room, scores, time
        JoinHello = 24,              // client -> server: [u8 version][string clientPk], padded
        JoinKey = 25,                // server -> client: [u8 version][string serverPk][string signature]
        ClockPing = 26,              // server -> client: [u64 nonce][u32 seq][string key] (anti-cheat clock)
        ClockPong = 27,              // client -> server: [u64 nonce][u32 seq][u64 clientMs][string tag]
        InputCmd = 28,               // client -> server: [string pawn][u8 n] n x [u32 tick][u32 seq][f32 mx my yaw pitch][u32 held][u32 pressed]
        PawnAck = 29,                // server -> a client: [string pawn][u32 seq][f32 x y z] where its input seq left the pawn
        // 30-32 NetChannels, 33-34 NetDelta.
        FragAck = 35                 // either way, plaintext: [u32 msgId][u32 count][bitmask] the pieces of a message that arrived
    };
    constexpr float kClockQuietLimit = 10.0f;   // no good clock answer for this long: reported
    constexpr float kClockReportEvery = 2.0f;   // one drift report per heartbeat window
    inline void WriteU64(net::Packet& p, std::uint64_t v) { p.Write(static_cast<std::uint32_t>(v)); p.Write(static_cast<std::uint32_t>(v >> 32)); }
    inline std::uint64_t ReadU64(net::Packet& p) { const std::uint64_t lo = p.ReadU32(); return lo | (static_cast<std::uint64_t>(p.ReadU32()) << 32); }
    // The sealed join: the client trades keys first (JoinHello / JoinKey) and
    // sends its Join encrypted. Version 2 is the first such version (a plain
    // Join is version 1).
    constexpr std::uint8_t kJoinVersion = 2;
    constexpr std::size_t kJoinHelloSize = 128;    // at least JoinKey's size: no amplification
    constexpr int kSealedJoinTries = 4;            // unanswered hellos before the plaintext Join
    constexpr float kPendingJoinTtl = 10.0f;
    // What a pinned client checks the server's signature over.
    inline std::string JoinKeyMessage(const std::string& serverPk, const std::string& clientPk) {
        return "okayjoin2|" + serverPk + clientPk;
    }
    constexpr float kFragmentTtl = 5.0f;        // drop half-assembled messages after this
    // Outer tag marking an encrypted datagram (nonce||ciphertext follows). Distinct
    // from every Msg type (1..16) and from a plaintext message's leading type byte.
    constexpr std::uint8_t kEncEnvelope = 0xEE;
    constexpr float kClientTimeout = 5.0f;
    // The start of the refusal an encryption-only host sends a plaintext join;
    // a client that can encrypt joins again, sealed.
    constexpr const char* kNeedsEncryption = "encryption required (update the game)";
    constexpr int kHitClaimsPerSecond = 20;    // a client's hit claims without the anti-cheat on
    constexpr float kReliableResend = 0.25f;   // resend an unacked message this often

    inline std::vector<std::uint8_t> BuildReliable(std::uint32_t seq, const std::string& channel,
                                            const std::string& data) {
        net::Packet p(ReliableMsg);
        p.Write(seq); p.Write(channel); p.Write(data);
        return std::vector<std::uint8_t>(p.Data(), p.Data() + p.Size());
    }

    // A relay peer is identified by a slot id carried in the Endpoint's address,
    // with a sentinel port so it can never be confused with a real address. The
    // server keys its client table by these, so relay routing needs no other change.
    inline net::Endpoint SlotEndpoint(std::uint32_t slot) {
        return net::Endpoint{slot, net::kRelaySlotPort};
    }
}

} // namespace okay
