#include "okay/Net/NetSim.hpp"
#include "okay/Net/Packet.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <unordered_map>
#include <utility>

namespace okay::net {
namespace {

struct Held {
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

void Trim(std::vector<Held>& q) {
    if (q.size() > 256) q.erase(q.begin(), q.begin() + (q.size() - 256));
}

void SendNow(UdpSocket& socket, const Endpoint& to, const std::uint8_t* data, std::size_t size) {
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

bool SimulatorActive() {
    if (!g_sim.enabled) return false;
    return g_sim.latencyMs > 0.0f || g_sim.jitterMs > 0.0f || g_sim.loss > 0.0f || g_sim.duplicate > 0.0f;
}

void ResetSimulator() {
    g_out.clear();
    g_in.clear();
}

void PumpSimulator(UdpSocket& socket) {
    if (!socket.IsOpen()) { g_out.clear(); return; }
    const double now = Now();
    std::vector<Held> keep;
    keep.reserve(g_out.size());
    for (auto& h : g_out) {
        if (h.release <= now) SendNow(socket, h.to, h.bytes.data(), h.bytes.size());
        else keep.push_back(std::move(h));
    }
    g_out.swap(keep);
}

bool HoldDatagram(UdpSocket& socket, const Endpoint& to, const void* data, std::size_t size) {
    if (!SimulatorActive() || !data || size == 0) return false;
    if (Dropped()) return true;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const double release = Now() + (double)DelaySeconds();
    if (release <= Now()) {
        SendNow(socket, to, bytes, size);
    } else {
        Held h;
        h.to = to;
        h.bytes.assign(bytes, bytes + size);
        h.release = release;
        g_out.push_back(std::move(h));
        Trim(g_out);
    }
    if (g_sim.duplicate > 0.0f && Unit() < g_sim.duplicate) {
        Held copy;
        copy.to = to;
        copy.bytes.assign(bytes, bytes + size);
        copy.release = Now() + (double)DelaySeconds() + 0.001;
        copy.duplicate = true;
        g_out.push_back(std::move(copy));
        Trim(g_out);
    }
    return true;
}

int TakeDatagram(UdpSocket& socket, void* buffer, std::size_t capacity, Endpoint& from) {
    if (SimulatorActive()) {
        std::vector<std::uint8_t> tmp(kMaxDatagram);
        Endpoint ep;
        for (;;) {
            const int n = socket.RecvFrom(tmp.data(), tmp.size(), ep);
            if (n <= 0) break;
            NoteWire(static_cast<std::size_t>(n), true);
            if (Dropped()) continue;
            Held h;
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
        Trim(g_in);
    }
    if (!g_in.empty()) {
        const double now = Now();
        for (auto it = g_in.begin(); it != g_in.end(); ++it) {
            if (SimulatorActive() && it->release > now) continue;
            const std::size_t n = std::min(capacity, it->bytes.size());
            if (buffer && n) std::memcpy(buffer, it->bytes.data(), n);
            from = it->to;
            g_in.erase(it);
            return (int)n;
        }
        if (SimulatorActive()) return 0;   // socket is drained; nothing is due yet
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
