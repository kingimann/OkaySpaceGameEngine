#include "okay/Net/NetSim.hpp"
#include "okay/Net/Packet.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <unordered_map>
#include <utility>

namespace okay::net {
namespace {

struct Held {
    const UdpSocket* sock = nullptr;   // the socket this datagram belongs to (two managers can share a process)
    Endpoint to;
    std::vector<std::uint8_t> bytes;
    double release = 0.0;
    bool duplicate = false;
};

SimSettings g_sim;
std::vector<Held> g_out;
std::vector<Held> g_in;
std::mt19937 g_rng{0x4E657431u};

double Now() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

float Unit() {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(g_rng);
}

float DelaySeconds() {
    float ms = g_sim.latencyMs + (Unit() * 2.0f - 1.0f) * g_sim.jitterMs;
    if (!(ms > 0.0f)) ms = 0.0f;
    if (ms > 5000.0f) ms = 5000.0f;
    return ms / 1000.0f;
}

bool Dropped() {
    return g_sim.loss > 0.0f && Unit() < g_sim.loss;
}

// Each socket may have this many datagrams waiting (in and out each). Past it the oldest of that
// socket's goes, and counts as an overflow, not as simulated loss. (It was 256 for every socket
// together, so a busy test lost far more than the loss setting said.)
constexpr std::size_t kHeldPerSocket = 4096;
std::size_t g_overflow = 0;
void Trim(std::vector<Held>& q, const UdpSocket* sock) {
    if (q.size() <= kHeldPerSocket) return;
    std::size_t mine = 0;
    for (const Held& h : q) if (h.sock == sock) ++mine;
    for (auto it = q.begin(); mine > kHeldPerSocket && it != q.end(); ) {
        if (it->sock == sock) { it = q.erase(it); --mine; ++g_overflow; }
        else ++it;
    }
}

// Sockets in this process that read through the simulator, with their ports. A datagram to one
// of them (on this computer) gets its latency and loss when it is read, so it is not given them
// on the way out as well: two managers in one program then see the setting once, not twice.
std::vector<std::pair<const UdpSocket*, std::uint16_t>> g_readers;
void NoteReader(const UdpSocket& socket) {
    for (auto& r : g_readers) if (r.first == &socket) { r.second = socket.LocalPort(); return; }
    g_readers.emplace_back(&socket, socket.LocalPort());
}
bool SimulatedOnArrival(const Endpoint& to) {
    if ((to.address >> 24) != 127 || to.port == 0) return false;
    for (const auto& r : g_readers) if (r.second == to.port) return true;
    return false;
}

// ---- the NAT model (see NetSim.hpp) ----
struct NatMap {
    Endpoint dest;                          // the destination this mapping is for (Symmetric only)
    std::unique_ptr<UdpSocket> outside;     // the public side: its port is what others see
    std::vector<Endpoint> sentTo;           // where it sent (the PortRestricted / Symmetric filter)
};
struct Nat {
    const UdpSocket* inside = nullptr;
    NatType type = NatType::None;
    std::vector<NatMap> maps;
};
std::vector<Nat> g_nats;
std::size_t g_natDrops = 0;
constexpr std::size_t kNatMapsMax = 256, kNatPeersMax = 256;

Nat* NatOf(const UdpSocket& s) {
    for (Nat& n : g_nats) if (n.inside == &s) return &n;
    return nullptr;
}

NatMap* NatMapFor(Nat& n, const Endpoint& to) {
    const bool perDest = n.type == NatType::Symmetric;
    for (NatMap& m : n.maps) if (!perDest || m.dest == to) return &m;
    if (n.maps.size() >= kNatMapsMax) return nullptr;
    NatMap m;
    m.dest = to;
    m.outside = std::make_unique<UdpSocket>();
    if (!m.outside->Open() || !m.outside->Bind(0)) return nullptr;
    n.maps.push_back(std::move(m));
    return &n.maps.back();
}

bool NatSend(Nat& n, const Endpoint& to, const std::uint8_t* data, std::size_t size) {
    NatMap* m = NatMapFor(n, to);
    if (!m) return false;
    if (std::find(m->sentTo.begin(), m->sentTo.end(), to) == m->sentTo.end()) {
        if (m->sentTo.size() >= kNatPeersMax) m->sentTo.erase(m->sentTo.begin());
        m->sentTo.push_back(to);
    }
    return m->outside->SendTo(to, data, size);
}

void SendNow(UdpSocket& socket, const Endpoint& to, const std::uint8_t* data, std::size_t size) {
    if (Nat* n = NatOf(socket)) { if (NatSend(*n, to, data, size)) NoteWire(size, false); return; }
    if (socket.SendTo(to, data, size)) NoteWire(size, false);
}

struct Acc { double bytes = 0; int msgs = 0; };
std::unordered_map<std::string, Acc> g_acc;
double g_accT = 0.0;
double g_wireIn = 0.0;
double g_wireOut = 0.0;
TrafficSample g_sample;

} // namespace

SimSettings& Simulator() { return g_sim; }

void SetSimulatedNat(const UdpSocket& inside, NatType type) {
    g_nats.erase(std::remove_if(g_nats.begin(), g_nats.end(), [&](const Nat& n) { return n.inside == &inside; }), g_nats.end());
    if (type == NatType::None) return;
    Nat n;
    n.inside = &inside;
    n.type = type;
    g_nats.push_back(std::move(n));
}

NatType SimulatedNat(const UdpSocket& inside) {
    const Nat* n = NatOf(inside);
    return n ? n->type : NatType::None;
}

void RebindSimulatedNat(const UdpSocket& inside) {
    if (Nat* n = NatOf(inside)) n->maps.clear();
}

std::uint16_t SimulatedNatPort(const UdpSocket& inside, const Endpoint& to) {
    Nat* n = NatOf(inside);
    if (!n) return 0;
    for (const NatMap& m : n->maps)
        if (n->type != NatType::Symmetric || m.dest == to) return m.outside->LocalPort();
    return 0;
}

std::size_t SimulatedNatDrops() { return g_natDrops; }

void ClearSimulatedNats() { g_nats.clear(); }

bool SendThroughNat(const UdpSocket& socket, const Endpoint& to, const void* data, std::size_t size) {
    Nat* n = NatOf(socket);
    if (!n || !data || size == 0) return false;
    if (NatSend(*n, to, static_cast<const std::uint8_t*>(data), size)) NoteWire(size, false);
    return true;
}

std::size_t SimulatorOverflows() { return g_overflow; }

bool SimulatorActive() {
    if (!g_sim.enabled) return false;
    return g_sim.latencyMs > 0.0f || g_sim.jitterMs > 0.0f || g_sim.loss > 0.0f || g_sim.duplicate > 0.0f;
}

void ResetSimulator() {
    g_out.clear();
    g_in.clear();
}

void ResetSimulator(const UdpSocket& socket) {
    g_nats.erase(std::remove_if(g_nats.begin(), g_nats.end(), [&](const Nat& n) { return n.inside == &socket; }), g_nats.end());
    g_readers.erase(std::remove_if(g_readers.begin(), g_readers.end(), [&](const auto& r) { return r.first == &socket; }),
                    g_readers.end());
    auto mine = [&](const Held& h) { return h.sock == &socket; };
    g_out.erase(std::remove_if(g_out.begin(), g_out.end(), mine), g_out.end());
    g_in.erase(std::remove_if(g_in.begin(), g_in.end(), mine), g_in.end());
}

void PumpSimulator(UdpSocket& socket) {
    if (!socket.IsOpen()) { ResetSimulator(socket); return; }
    const double now = Now();
    std::vector<Held> keep;
    keep.reserve(g_out.size());
    for (auto& h : g_out) {
        if (h.sock != &socket) keep.push_back(std::move(h));          // another socket's: its own pump sends it
        else if (h.release <= now) SendNow(socket, h.to, h.bytes.data(), h.bytes.size());
        else keep.push_back(std::move(h));
    }
    g_out.swap(keep);
}

bool HoldDatagram(UdpSocket& socket, const Endpoint& to, const void* data, std::size_t size) {
    if (!data || size == 0) return false;
    // Not simulating, or its reader applies the delay and loss: send it as is (through the router, if any).
    if (!SimulatorActive() || SimulatedOnArrival(to)) return SendThroughNat(socket, to, data, size);
    if (Dropped()) return true;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const double release = Now() + (double)DelaySeconds();
    if (release <= Now()) {
        SendNow(socket, to, bytes, size);
    } else {
        Held h;
        h.sock = &socket;
        h.to = to;
        h.bytes.assign(bytes, bytes + size);
        h.release = release;
        g_out.push_back(std::move(h));
        Trim(g_out, &socket);
    }
    if (g_sim.duplicate > 0.0f && Unit() < g_sim.duplicate) {
        Held copy;
        copy.sock = &socket;
        copy.to = to;
        copy.bytes.assign(bytes, bytes + size);
        copy.release = Now() + (double)DelaySeconds() + 0.001;
        copy.duplicate = true;
        g_out.push_back(std::move(copy));
        Trim(g_out, &socket);
    }
    return true;
}

int TakeDatagram(UdpSocket& socket, void* buffer, std::size_t capacity, Endpoint& from) {
    if (socket.IsOpen()) NoteReader(socket);
    if (Nat* nat = NatOf(socket)) {
        // Behind a router: only what reaches a mapping, and passes its filter, comes in (at once: the
        // sender's side applied any simulated delay); the private port itself is unreachable.
        std::vector<std::uint8_t> tmp(kMaxDatagram);
        Endpoint ep;
        int n;
        while ((n = socket.RecvFrom(tmp.data(), tmp.size(), ep)) > 0) ++g_natDrops;
        for (NatMap& m : nat->maps)
            while ((n = m.outside->RecvFrom(tmp.data(), tmp.size(), ep)) > 0) {
                const bool allowed = nat->type == NatType::FullCone ||
                                     std::find(m.sentTo.begin(), m.sentTo.end(), ep) != m.sentTo.end();
                if (!allowed) { ++g_natDrops; continue; }
                NoteWire(static_cast<std::size_t>(n), true);
                Held h;
                h.sock = &socket;
                h.to = ep;
                h.bytes.assign(tmp.begin(), tmp.begin() + n);
                h.release = 0.0;
                g_in.push_back(std::move(h));
            }
        Trim(g_in, &socket);
        for (auto it = g_in.begin(); it != g_in.end(); ++it) {
            if (it->sock != &socket || (SimulatorActive() && it->release > Now())) continue;
            const std::size_t len = std::min(capacity, it->bytes.size());
            if (buffer && len) std::memcpy(buffer, it->bytes.data(), len);
            from = it->to;
            g_in.erase(it);
            return (int)len;
        }
        return 0;
    }
    if (SimulatorActive()) {
        std::vector<std::uint8_t> tmp(kMaxDatagram);
        Endpoint ep;
        for (;;) {
            const int n = socket.RecvFrom(tmp.data(), tmp.size(), ep);
            if (n <= 0) break;
            NoteWire(static_cast<std::size_t>(n), true);
            if (Dropped()) continue;
            Held h;
            h.sock = &socket;
            h.to = ep;
            h.bytes.assign(tmp.begin(), tmp.begin() + n);
            h.release = Now() + (double)DelaySeconds();
            g_in.push_back(std::move(h));
            if (g_sim.duplicate > 0.0f && Unit() < g_sim.duplicate) {
                Held copy = g_in.back();
                copy.release += 0.001;
                copy.duplicate = true;
                g_in.push_back(std::move(copy));
            }
        }
        Trim(g_in, &socket);
    }
    if (!g_in.empty()) {
        const double now = Now();
        bool any = false;
        for (auto it = g_in.begin(); it != g_in.end(); ++it) {
            if (it->sock != &socket) continue;   // held for another socket
            any = true;
            if (SimulatorActive() && it->release > now) continue;
            const std::size_t n = std::min(capacity, it->bytes.size());
            if (buffer && n) std::memcpy(buffer, it->bytes.data(), n);
            from = it->to;
            g_in.erase(it);
            return (int)n;
        }
        if (SimulatorActive() && any) return 0;   // socket is drained; nothing is due yet
    }
    const int n = socket.RecvFrom(buffer, capacity, from);
    if (n > 0) NoteWire(static_cast<std::size_t>(n), true);
    return n;
}

std::string ObjectKey(const std::string& channel, const std::string& data) {
    auto head = [&]() {
        const auto cut = data.find('|');
        const std::size_t n = std::min(cut == std::string::npos ? data.size() : cut, (std::size_t)64);
        return data.substr(0, n);
    };
    if (channel == "__xform" || channel == "__despawn" || channel == "__hit") {
        std::string id = head();
        if (!id.empty()) return id;
    }
    if (channel.size() > 6 && channel.compare(0, 6, "__rpc/") == 0) {
        std::string name = channel.substr(6);
        if (name.size() > 64) name.resize(64);
        if (!name.empty()) return name;
    }
    if (channel.size() > 64) return channel.substr(0, 64);
    return channel.empty() ? std::string("(other)") : channel;
}

void NoteObject(const std::string& name, std::size_t bytes, bool /*inbound*/) {
    std::string key = name.empty() ? std::string("(other)") : name;
    if (key.size() > 64) key.resize(64);
    if (g_acc.size() >= 128 && !g_acc.count(key)) key = "(other)";
    Acc& a = g_acc[key];
    a.bytes += (double)bytes;
    a.msgs += 1;
}

void NoteWire(std::size_t bytes, bool inbound) {
    if (inbound) g_wireIn += (double)bytes;
    else g_wireOut += (double)bytes;
}

void ProfileAdvance(float dt) {
    if (!(dt > 0.0f)) return;
    g_accT += (double)dt;
    if (g_accT < 1.0) return;
    TrafficSample s;
    s.wireInPerSec = (float)(g_wireIn / g_accT);
    s.wireOutPerSec = (float)(g_wireOut / g_accT);
    s.rows.reserve(g_acc.size());
    for (const auto& [name, a] : g_acc)
        s.rows.push_back(TrafficRow{name, (float)(a.msgs / g_accT), (float)(a.bytes / g_accT)});
    std::sort(s.rows.begin(), s.rows.end(), [](const TrafficRow& a, const TrafficRow& b) {
        return a.bytesPerSec > b.bytesPerSec;
    });
    g_sample = std::move(s);
    g_acc.clear();
    g_wireIn = 0.0;
    g_wireOut = 0.0;
    g_accT = 0.0;
}

const TrafficSample& Traffic() { return g_sample; }

} // namespace okay::net
