#pragma once
// Binary delta snapshots for networked transforms.
//
// Poses are quantized (position in millimetres, quaternion to 15 bits per
// component). Each sender keeps a baseline the receiver has acknowledged and
// sends only what moved relative to it. A later packet still decodes against
// that baseline until the ack lands, so a lost ack just makes the next delta
// larger. The avatar State / InputCmd / PawnAck path is not this.
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay::net {

inline constexpr std::uint8_t kSnapDelta = 33;
inline constexpr std::uint8_t kSnapAck = 34;

struct QPose {
    std::int32_t x = 0, y = 0, z = 0;          // millimetres
    std::int16_t qx = 0, qy = 0, qz = 0, qw = 32767;
    bool operator==(const QPose& o) const {
        return x == o.x && y == o.y && z == o.z && qx == o.qx && qy == o.qy && qz == o.qz && qw == o.qw;
    }
    bool operator!=(const QPose& o) const { return !(*this == o); }
};

QPose QuantizePose(const Vec3& p, const Quat& q);
void DequantizePose(const QPose& q, Vec3& p, Quat& rot);

// One generation of absolute poses. `id` 0 means "nothing acknowledged yet".
struct DeltaBase {
    std::uint32_t id = 0;
    std::unordered_map<std::string, QPose> pose;
    std::unordered_map<std::string, std::string> extra;
};

struct SnapChange {
    std::string id;
    QPose pose;
    std::string extra;
    bool hasExtra = false;
};

// Outgoing side: deltas are from `acked` until the receiver acks `pending`.
struct SnapOut {
    DeltaBase acked;
    DeltaBase pending;
    std::uint32_t next = 1;
    std::vector<std::uint8_t> bytes;
    bool waiting = false;
    void Clear() { *this = {}; }
};

// Incoming side: `have` is the baseline we can still decode. `latest` is the
// last snapshot we applied and acked; it becomes `have` once the sender
// catches up (its next packet names that id as the baseline).
struct SnapIn {
    DeltaBase have;
    DeltaBase latest;
    std::uint32_t latestId = 0;
    bool haveLatest = false;
    void Clear() { *this = {}; }
};

bool SameBase(const DeltaBase& a, const DeltaBase& b);

// Full datagram, or empty when nothing differs from `base`.
std::vector<std::uint8_t> EncodeSnapshot(const DeltaBase& base, const DeltaBase& current,
                                         std::uint32_t snapId, double tick);
// Header only, so the receiver can promote its baseline before decoding deltas.
bool PeekSnapshot(const std::uint8_t* data, std::size_t size, std::uint32_t& baselineId, std::uint32_t& snapId);
// `changes` are absolute poses. False if the blob is truncated or a delta
// names an object the baseline doesn't have.
bool DecodeSnapshot(const std::uint8_t* data, std::size_t size, const DeltaBase& have,
                    std::uint32_t& baselineId, std::uint32_t& snapId, double& tick,
                    std::vector<SnapChange>& changes);

std::vector<std::uint8_t> EncodeSnapAck(std::uint32_t snapId);
bool DecodeSnapAck(const std::uint8_t* data, std::size_t size, std::uint32_t& snapId);

} // namespace okay::net
