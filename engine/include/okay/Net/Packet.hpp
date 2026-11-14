#pragma once
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace okay::net {

/// The largest UDP datagram we ever receive into a single buffer. A UDP payload
/// can be up to ~65507 bytes; sizing the receive buffer to the full range means a
/// large message is never silently truncated. (Datagrams near this size rely on IP
/// fragmentation, which is reliable on LAN; internet-safe app-level fragmentation
/// keeps individual sends well under the path MTU — a separate transport step.)
inline constexpr std::size_t kMaxDatagram = 65536;

/// Sends larger than this are split into fragments, each kept under a conservative
/// path MTU so they survive the open internet (where a ~64 KB datagram relying on
/// IP fragmentation is often dropped). The receiver reassembles them.
inline constexpr std::size_t kFragmentThreshold = 1200;

/// Fixed-width little-endian stores and loads, built from shifts so they give the
/// same bytes on every host regardless of its own byte order.
inline void StoreU32LE(std::uint8_t* out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
inline std::uint32_t LoadU32LE(const std::uint8_t* in) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(in[i]) << (8 * i);
    return v;
}

/// A little-endian byte buffer for serializing network messages. Write values
/// in, send the bytes, then read them back out in the same order.
class Packet {
public:
    Packet() = default;
    explicit Packet(std::uint8_t messageType) { Write(messageType); }
    Packet(const void* data, std::size_t size)
        : m_data(static_cast<const std::uint8_t*>(data),
                 static_cast<const std::uint8_t*>(data) + size) {}

    // ---- Writing -------------------------------------------------------
    void Write(std::uint8_t v) { m_data.push_back(v); }
    void Write(std::int32_t v) { WritePod(v); }
    void Write(std::uint32_t v) { WritePod(v); }
    void Write(float v) { WritePod(v); }
    void Write(const std::string& s) {
        Write(static_cast<std::uint32_t>(s.size()));
        m_data.insert(m_data.end(), s.begin(), s.end());
    }

    // ---- Reading -------------------------------------------------------
    std::uint8_t  ReadU8() {
        if (m_read + 1 > m_data.size()) { m_overflow = true; return 0; }
        return m_data[m_read++];
    }
    std::int32_t  ReadI32() { std::int32_t v = 0;  ReadPod(v); return v; }
    std::uint32_t ReadU32() { std::uint32_t v = 0; ReadPod(v); return v; }
    float         ReadF32() { float v = 0.0f;      ReadPod(v); return v; }
    std::string   ReadString() {
        std::uint32_t n = ReadU32();
        if (m_overflow) return {};
        // A length past the end is a malformed packet, not a short string.
        if (n > m_data.size() - m_read) { m_overflow = true; return {}; }
        // data() + offset, not &m_data[m_read]: an empty string may end the packet.
        std::string s(reinterpret_cast<const char*>(m_data.data()) + m_read, n);
        m_read += n;
        return s;
    }

    bool Ok() const { return !m_overflow; }
    const std::uint8_t* Data() const { return m_data.data(); }
    std::size_t Size() const { return m_data.size(); }
    std::size_t Remaining() const { return m_read < m_data.size() ? m_data.size() - m_read : 0; }

private:
    // 32-bit values only (int32, uint32, float): always little-endian on the wire,
    // whatever the host's byte order. A float travels as its IEEE-754 bit pattern.
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                  "the wire format carries floats as IEEE-754 binary32");
    template <typename T> void WritePod(T v) {
        static_assert(sizeof(T) == 4, "WritePod handles 32-bit values");
        std::uint32_t u;
        std::memcpy(&u, &v, 4);
        std::uint8_t b[4];
        StoreU32LE(b, u);
        m_data.insert(m_data.end(), b, b + 4);
    }
    template <typename T> void ReadPod(T& v) {
        static_assert(sizeof(T) == 4, "ReadPod handles 32-bit values");
        if (m_read + 4 > m_data.size()) { m_overflow = true; return; }
        const std::uint32_t u = LoadU32LE(m_data.data() + m_read);
        std::memcpy(&v, &u, 4);
        m_read += 4;
    }

    std::vector<std::uint8_t> m_data;
    std::size_t m_read = 0;
    bool m_overflow = false;
};

} // namespace okay::net
