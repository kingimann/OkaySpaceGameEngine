#pragma once
// Per-channel delivery on top of the UDP transport.
//
// The original reliable stream (ReliableMsg) is one sequence per peer and is
// delivered as soon as it arrives. These streams are separate:
//   * reliable ordered — one sequence per channel, held until the gap before
//     it is filled, resent until a cumulative ack
//   * unreliable sequenced — a newer sequence replaces an older one; anything
//     older or repeated is dropped, and nothing is resent
// Movement, hit claims and the anti-cheat heartbeat stay on their own message
// types. Nothing here changes those.
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace okay::net {

inline constexpr std::uint8_t kChanRel = 30;      // reliable ordered payload
inline constexpr std::uint8_t kChanRelAck = 31;   // cumulative ack for one channel
inline constexpr std::uint8_t kChanSeq = 32;      // unreliable sequenced payload
inline constexpr std::uint32_t kChanBroadcast = 0xFFFFFFFFu;

inline constexpr std::size_t kChannelNameMax = 64;
inline constexpr std::size_t kChannelPayloadMax = 48u * 1024u;
inline constexpr std::uint32_t kOrderedWindow = 32;   // in-flight and reorder depth
inline constexpr std::size_t kMaxChannels = 64;
inline constexpr std::size_t kMaxSeqKeys = 256;       // sender/channel pairs remembered
inline constexpr float kChannelResend = 0.25f;

bool ChannelNameOk(const std::string& channel);

struct ChanMsg {
    std::uint32_t from = 0;
    std::string channel;
    std::string data;
};

// One peer's reliable-ordered streams, both directions.
class OrderedEnd {
public:
    // Packet to send now, or empty if the channel, size or window refuses it.
    // `cookie` (client sessions) is appended so the server can match it; 0 omits it.
    std::vector<std::uint8_t> Push(const std::string& channel, const std::string& data,
                                   std::uint64_t cookie);
    void CollectResend(float dt, std::vector<std::vector<std::uint8_t>>& due);
    void Ack(const std::string& channel, std::uint32_t ack);
    // True if this seq was already delivered or is sitting in the reorder buffer.
    bool Seen(const std::string& channel, std::uint32_t seq) const;
    // Fold `seq` into the channel. Newly contiguous messages are appended to
    // `out` (only those queued with deliver=true). On success `ack` is the
    // highest contiguous seq (0 if the gap is still open). False = ignore it.
    bool Receive(const std::string& channel, std::uint32_t seq, std::string data, bool deliver,
                 std::uint32_t from, std::vector<ChanMsg>& out, std::uint32_t& ack);
    void Clear();

private:
    struct Slot { std::vector<std::uint8_t> bytes; float age = 0.0f; };
    struct OutStream { std::uint32_t next = 1; std::map<std::uint32_t, Slot> pending; };
    struct Held { std::string data; bool deliver = false; };
    struct InStream { std::uint32_t next = 1; std::map<std::uint32_t, Held> ahead; };
    std::unordered_map<std::string, OutStream> m_out;
    std::unordered_map<std::string, InStream> m_in;
};

// Outgoing unreliable-sequenced sequence numbers, one per channel.
class SequencedTx {
public:
    std::uint32_t Next(const std::string& channel);   // 0 if refused
    void Clear() { m_next.clear(); }
private:
    std::unordered_map<std::string, std::uint32_t> m_next;
};

// Incoming unreliable-sequenced filter, keyed by sender and channel.
class SequencedRx {
public:
    bool IsStale(std::uint32_t from, const std::string& channel, std::uint32_t seq) const;
    bool Accept(std::uint32_t from, const std::string& channel, std::uint32_t seq);
    void Clear() { m_last.clear(); }
private:
    static std::string Key(std::uint32_t from, const std::string& channel);
    std::unordered_map<std::string, std::uint32_t> m_last;
};

struct ChannelEnd {
    OrderedEnd ordered;
    SequencedTx sequencedTx;
    SequencedRx sequencedRx;
    void Clear() { ordered.Clear(); sequencedTx.Clear(); sequencedRx.Clear(); }
};

std::vector<std::uint8_t> BuildChanRel(std::uint32_t seq, const std::string& channel,
                                       const std::string& data, std::uint64_t cookie);
std::vector<std::uint8_t> BuildChanRelAck(const std::string& channel, std::uint32_t ack);
std::vector<std::uint8_t> BuildChanSeq(std::uint32_t sender, std::uint32_t seq, std::uint32_t target,
                                       const std::string& channel, const std::string& data,
                                       std::uint64_t cookie);

} // namespace okay::net
