#pragma once
#include <cstdint>
#include <cstring>
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
    std::uint8_t  ReadU8()  { std::uint8_t v = 0;  ReadPod(v); return v; }
    std::int32_t  ReadI32() { std::int32_t v = 0;  ReadPod(v); return v; }
    std::uint32_t ReadU32() { std::uint32_t v = 0; ReadPod(v); return v; }
    float         ReadF32() { float v = 0.0f;      ReadPod(v); return v; }
    std::string   ReadString() {
        std::uint32_t n = ReadU32();
        std::string s;
        if (m_read + n <= m_data.size()) {
            s.assign(reinterpret_cast<const char*>(&m_data[m_read]), n);
            m_read += n;
        }
        return s;
    }

    bool Ok() const { return !m_overflow; }
    const std::uint8_t* Data() const { return m_data.data(); }
    std::size_t Size() const { return m_data.size(); }

private:
    template <typename T> void WritePod(T v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        m_data.insert(m_data.end(), p, p + sizeof(T));
    }
    template <typename T> void ReadPod(T& v) {
        if (m_read + sizeof(T) > m_data.size()) { m_overflow = true; return; }
        std::memcpy(&v, &m_data[m_read], sizeof(T));
        m_read += sizeof(T);
    }

    std::vector<std::uint8_t> m_data;
    std::size_t m_read = 0;
    bool m_overflow = false;
};

} // namespace okay::net
