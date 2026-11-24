// net::Rollback: input delay, input exchange, saved states and resimulation for
// deterministic games (see okay/Net/Rollback.hpp). Written for this engine from
// the general idea of rollback netcode: predict the missing inputs, go back to
// the saved state of the first wrong frame when the real input comes, run
// forward again. No outside code.
//
// Wire ("__rb", unreliable, every player to every player through the host):
//   [u8 version 2][u8 run][u8 inputDelay][u8 maxInputBytes][u32 tickRate x 1000][u8 players][u32 players hash]
//   [u32 first frame][u8 n] n x ([u8 len][len bytes])
//   [u8 acks] acks x ([u32 player][u32 frames of that player's inputs held])
//   [u32 checksum frame][u32 checksum lo][u32 checksum hi]   (frame 0xFFFFFFFF = none yet)
// The inputs are the sender's own, for frames first .. first+n-1: everything
// some player has not confirmed yet (at most kMaxFramesPerPacket), so a lost
// packet is covered by the next one. The settings and the player set travel in
// every packet: a player who started differently stops the game in plain words
// instead of drifting apart.
#include "okay/Net/Rollback.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Core/StringId.hpp"
#include "okay/Core/Log.hpp"
#include <algorithm>
#include <cmath>

namespace okay::net {

namespace {
constexpr std::uint8_t kWireVersion = 2;
constexpr std::uint32_t kNone = 0xFFFFFFFFu;
constexpr int kMaxStepsPerTick = 8;          // catching up after a stall: at most this many frames per Update
constexpr std::size_t kKeepHashes = 256;

std::uint32_t TickMilli(float hz) { return (std::uint32_t)std::lround((double)hz * 1000.0); }
std::string HzText(std::uint32_t milli) {
    std::string t = std::to_string(milli / 1000);
    if (milli % 1000) { std::string f = std::to_string(1000 + milli % 1000).substr(1); while (f.back() == '0') f.pop_back(); t += "." + f; }
    return t;
}
void PutU8(std::string& s, std::uint8_t v) { s.push_back((char)v); }
void PutU32(std::string& s, std::uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back((char)((v >> (8 * i)) & 0xFF)); }
struct Reader {
    const std::string& s; std::size_t at = 0; bool ok = true;
    explicit Reader(const std::string& str) : s(str) {}
    std::uint8_t U8() { if (at + 1 > s.size()) { ok = false; return 0; } return (std::uint8_t)s[at++]; }
    std::uint32_t U32() {
        if (at + 4 > s.size()) { ok = false; return 0; }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= (std::uint32_t)(std::uint8_t)s[at++] << (8 * i);
        return v;
    }
    std::string Bytes(std::size_t n) { if (at + n > s.size()) { ok = false; return {}; } std::string r = s.substr(at, n); at += n; return r; }
};
} // namespace

bool Rollback::Start(const Settings& settings, SaveFn save, LoadFn load, InputFn input, StepFn step) {
    Stop();
    m_lastError.clear();
    if (!save || !load || !input || !step) { m_lastError = "Rollback needs all four: save, load, input and step."; return false; }
    const bool live = m_nm.IsServer() || (m_nm.IsClient() && m_nm.Joined());
    if (!live) { m_lastError = "Rollback can only start in a network session (Host or Join first)."; return false; }
    m_settings = settings;
    m_settings.tickRate = std::isfinite(settings.tickRate) ? std::min(240.0f, std::max(1.0f, settings.tickRate)) : 60.0f;
    m_settings.inputDelay = std::min(30, std::max(0, settings.inputDelay));
    m_settings.maxPrediction = std::min(120, std::max(1, settings.maxPrediction));
    m_settings.maxInputBytes = std::min<std::size_t>(255, settings.maxInputBytes);
    m_save = std::move(save); m_load = std::move(load); m_input = std::move(input); m_step = std::move(step);
    m_me = m_nm.LocalId();
    m_players.clear();
    for (const auto& p : m_nm.Players()) m_players.push_back(p.id);
    if (std::find(m_players.begin(), m_players.end(), m_me) == m_players.end()) m_players.push_back(m_me);
    std::sort(m_players.begin(), m_players.end());
    {
        std::string ids;
        for (std::uint32_t id : m_players) ids += std::to_string(id) + ",";
        m_playersHash = (std::uint32_t)StringId::HashOf(ids);
    }
    m_peers.clear();
    const std::uint32_t delay = (std::uint32_t)m_settings.inputDelay;
    for (std::uint32_t id : m_players) {
        Peer& p = m_peers[id];
        for (std::uint32_t f = 0; f < delay; ++f) p.inputs[f] = std::string();   // the first frames: nobody pressed anything
        p.have = delay;
    }
    m_frame = 0; m_rollbackTo = kNone; m_hashedUpTo = 0; m_accum = 0.0f;
    ++m_run;
    m_rollbacks = m_resimFrames = m_stalls = m_desyncs = m_compared = 0;
    m_running = true;
    OKAY_INFO("net: rollback started: ", m_players.size(), " players, ", m_settings.tickRate, " Hz, input delay ",
              m_settings.inputDelay, ", up to ", m_settings.maxPrediction, " frames predicted");
    return true;
}

void Rollback::Stop(const std::string& why) {
    if (m_running && !why.empty()) { m_lastError = why; OKAY_WARN("net: rollback stopped: ", why); }
    m_running = false; m_resim = false;
    m_states.clear(); m_used.clear(); m_myHashes.clear(); m_theirHashes.clear(); m_peers.clear();
}

std::uint32_t Rollback::ConfirmedFrame() const {
    std::uint32_t c = m_frame;
    for (const auto& [id, p] : m_peers) c = std::min(c, p.have);
    return c;
}

bool Rollback::StateAt(std::uint32_t frame, std::string& out) const {
    auto it = m_states.find(frame);
    if (it == m_states.end()) return false;
    out = it->second;
    return true;
}

std::string Rollback::InputFor(std::uint32_t player, std::uint32_t frame, bool& predicted) const {
    predicted = false;
    auto p = m_peers.find(player);
    if (p == m_peers.end()) { predicted = true; return std::string(); }
    auto it = p->second.inputs.find(frame);
    if (it != p->second.inputs.end()) return it->second;
    predicted = true;   // not here yet: the newest input of theirs before this frame
    auto lb = p->second.inputs.lower_bound(frame);
    if (lb == p->second.inputs.begin()) return std::string();
    return std::prev(lb)->second;
}

void Rollback::RunFrame(std::uint32_t frame) {
    m_states[frame] = m_save();
    std::vector<FrameInput> in;
    std::vector<std::string> used;
    in.reserve(m_players.size());
    for (std::uint32_t id : m_players) {
        FrameInput fi; fi.player = id;
        fi.input = InputFor(id, frame, fi.predicted);
        used.push_back(fi.input);
        in.push_back(std::move(fi));
    }
    m_used[frame] = std::move(used);
    m_step(frame, in);
}

void Rollback::Tick(float dt) {
    if (!m_running) return;
    const bool live = m_nm.IsServer() || (m_nm.IsClient() && m_nm.Joined());
    if (!live) { Stop("this player left the network session"); return; }
    {   // a player who left: the others can't go on without their inputs
        const auto now = m_nm.Players();
        for (std::uint32_t id : m_players) {
            if (id == m_me) continue;
            if (std::none_of(now.begin(), now.end(), [id](const NetworkManager::Player& p) { return p.id == id; })) {
                Stop("player " + std::to_string(id) + " left, so the rollback game cannot go on");
                return;
            }
        }
    }
    const float step = 1.0f / m_settings.tickRate;
    if (std::isfinite(dt) && dt > 0.0f) m_accum += dt;
    m_accum = std::min(m_accum, step * kMaxStepsPerTick);
    auto applyRollback = [&] {
        if (m_rollbackTo >= m_frame) { m_rollbackTo = kNone; return; }
        auto it = m_states.find(m_rollbackTo);
        if (it == m_states.end()) { Stop("a late input is older than the saved states (frame " + std::to_string(m_rollbackTo) + ")"); return; }
        m_load(it->second);
        m_resim = true;
        for (std::uint32_t f = m_rollbackTo; f < m_frame; ++f) RunFrame(f);
        m_resim = false;
        ++m_rollbacks;
        m_resimFrames += m_frame - m_rollbackTo;
        m_rollbackTo = kNone;
    };
    applyRollback();
    if (!m_running) return;
    Peer& me = m_peers[m_me];
    while (m_accum >= step) {
        std::uint32_t slowest = kNone;
        for (const auto& [id, p] : m_peers) if (id != m_me) slowest = std::min(slowest, p.have);
        if (slowest != kNone && m_frame >= slowest + (std::uint32_t)m_settings.maxPrediction) { ++m_stalls; break; }
        m_accum -= step;
        // This player's input, used inputDelay frames from now.
        std::string in = m_input();
        if (in.size() > m_settings.maxInputBytes) in.resize(m_settings.maxInputBytes);
        const std::uint32_t at = m_frame + (std::uint32_t)m_settings.inputDelay;
        me.inputs[at] = std::move(in);
        me.have = std::max(me.have, at + 1);
        RunFrame(m_frame);
        ++m_frame;
    }
    HashConfirmed();
    SendInputs();
    Prune();
}

void Rollback::SendInputs() {
    const Peer& me = m_peers[m_me];
    std::uint32_t first = me.have;
    for (const auto& [id, p] : m_peers) if (id != m_me) first = std::min(first, p.acked);
    if (me.have - first > kMaxFramesPerPacket) first = me.have - (std::uint32_t)kMaxFramesPerPacket;   // the newest; older ones follow as acks move
    std::string w;
    PutU8(w, kWireVersion);
    PutU8(w, m_run);
    PutU8(w, (std::uint8_t)m_settings.inputDelay);
    PutU8(w, (std::uint8_t)m_settings.maxInputBytes);
    PutU32(w, TickMilli(m_settings.tickRate));
    PutU8(w, (std::uint8_t)std::min<std::size_t>(255, m_players.size()));
    PutU32(w, m_playersHash);
    PutU32(w, first);
    std::vector<const std::string*> list;
    for (std::uint32_t f = first; f < me.have; ++f) {
        auto it = me.inputs.find(f);
        if (it == me.inputs.end()) break;
        list.push_back(&it->second);
    }
    PutU8(w, (std::uint8_t)list.size());
    for (const std::string* s : list) { PutU8(w, (std::uint8_t)s->size()); w += *s; }
    PutU8(w, (std::uint8_t)std::min<std::size_t>(255, m_peers.size() - 1));
    int acks = 0;
    for (const auto& [id, p] : m_peers) {
        if (id == m_me || acks >= 255) continue;
        PutU32(w, id); PutU32(w, p.have); ++acks;
    }
    if (m_myHashes.empty()) { PutU32(w, kNone); PutU32(w, 0); PutU32(w, 0); }
    else {
        const auto& last = *m_myHashes.rbegin();
        PutU32(w, last.first); PutU32(w, (std::uint32_t)last.second); PutU32(w, (std::uint32_t)(last.second >> 32));
    }
    m_nm.Send("__rb", w);
}

void Rollback::OnMessage(std::uint32_t from, const std::string& data) {
    if (!m_running || from == m_me) return;
    auto pit = m_peers.find(from);
    if (pit == m_peers.end()) return;   // not one of the players who started
    Peer& p = pit->second;
    Reader r(data);
    if (r.U8() != kWireVersion || !r.ok) return;
    if (r.U8() != m_run) return;   // an earlier (or later) game of theirs
    const std::uint8_t delay = r.U8();
    const std::uint8_t maxBytes = r.U8();
    const std::uint32_t tickMilli = r.U32();
    const std::uint8_t nPlayers = r.U8();
    const std::uint32_t playersHash = r.U32();
    if (!r.ok) return;
    {   // every player must have started with the same settings and the same players
        const std::string who = "player " + std::to_string(from);
        const char* same = "; every player must start rollback with the same settings";
        std::string why;
        if (delay != (std::uint8_t)m_settings.inputDelay)
            why = who + " uses input delay " + std::to_string(delay) + " and this player " + std::to_string(m_settings.inputDelay) + same;
        else if (tickMilli != TickMilli(m_settings.tickRate))
            why = who + " runs at " + HzText(tickMilli) + " Hz and this player at " + HzText(TickMilli(m_settings.tickRate)) + " Hz" + same;
        else if (maxBytes != (std::uint8_t)m_settings.maxInputBytes)
            why = who + " allows inputs of " + std::to_string(maxBytes) + " bytes and this player " +
                  std::to_string(m_settings.maxInputBytes) + same;
        else if (nPlayers != (std::uint8_t)std::min<std::size_t>(255, m_players.size()) || playersHash != m_playersHash)
            why = (nPlayers == m_players.size()
                       ? who + " started with other players than this player (" + std::to_string(nPlayers) + " each)"
                       : who + " started with " + std::to_string(nPlayers) + " players and this player with " + std::to_string(m_players.size())) +
                  "; start rollback once every player is in the session (Players() lists the same players everywhere)";
        if (!why.empty()) { Stop(why); return; }
    }
    const std::uint32_t first = r.U32();
    const std::uint8_t n = r.U8();
    std::vector<std::string> inputs;
    for (std::uint8_t i = 0; i < n && r.ok; ++i) {
        const std::uint8_t len = r.U8();
        inputs.push_back(r.Bytes(len));
    }
    const std::uint8_t acks = r.U8();
    std::uint32_t theirAckOfMe = 0; bool sawAck = false;
    for (std::uint8_t i = 0; i < acks && r.ok; ++i) {
        const std::uint32_t player = r.U32(), have = r.U32();
        if (player == m_me) { theirAckOfMe = have; sawAck = true; }
    }
    const std::uint32_t ckFrame = r.U32();
    const std::uint64_t ck = (std::uint64_t)r.U32() | ((std::uint64_t)r.U32() << 32);
    if (!r.ok) return;
    // No player can honestly be further ahead than its input delay plus how far it may predict past
    // our inputs (a margin for timing): later frames are not kept, so a peer cannot park inputs for
    // frames far in the future (they are sent again once they are due).
    const std::uint32_t horizon = std::max(m_frame, p.have) + (std::uint32_t)m_settings.inputDelay * 2u +
                                  (std::uint32_t)m_settings.maxPrediction + kAheadMargin;
    if (first >= horizon) return;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const std::uint32_t f = first + (std::uint32_t)i;
        if (f >= horizon) break;
        if (f < (std::uint32_t)m_settings.inputDelay || p.inputs.count(f)) continue;   // the first frames are fixed; a repeat changes nothing
        if (f < p.have) continue;   // already had (and maybe pruned)
        std::string in = inputs[i];
        if (in.size() > m_settings.maxInputBytes) in.resize(m_settings.maxInputBytes);
        // Already run with a guess? A wrong guess sends us back to this frame.
        if (f < m_frame) {
            auto u = m_used.find(f);
            const std::size_t idx = (std::size_t)(std::find(m_players.begin(), m_players.end(), from) - m_players.begin());
            if (u == m_used.end() || idx >= u->second.size() || u->second[idx] != in) m_rollbackTo = std::min(m_rollbackTo, f);
        }
        p.inputs[f] = std::move(in);
    }
    while (p.inputs.count(p.have)) ++p.have;
    if (sawAck) p.acked = std::max(p.acked, theirAckOfMe);
    if (ckFrame != kNone) {
        auto mine = m_myHashes.find(ckFrame);
        if (mine != m_myHashes.end()) {
            ++m_compared;
            if (mine->second != ck) { ++m_desyncs; OKAY_WARN("net: rollback desync at frame ", ckFrame, " with player ", from, " (the game is not deterministic)"); }
        } else if (ckFrame >= m_hashedUpTo && m_theirHashes.size() < kKeepHashes) {
            m_theirHashes[ckFrame] = ck;
        }
    }
}

std::size_t Rollback::HeldInputs() const {
    std::size_t n = 0;
    for (const auto& [id, p] : m_peers) n += p.inputs.size();
    return n;
}

std::size_t Rollback::HeldInputBytes() const {
    std::size_t n = 0;
    for (const auto& [id, p] : m_peers) for (const auto& [f, in] : p.inputs) n += in.size();
    return n;
}

void Rollback::HashConfirmed() {
    if (m_rollbackTo != kNone) return;   // a resimulation is due: the states may change
    const std::uint32_t conf = ConfirmedFrame();
    while (m_hashedUpTo + 1 <= conf && m_hashedUpTo + 1 < m_frame) {
        const std::uint32_t f = m_hashedUpTo + 1;
        auto it = m_states.find(f);
        m_hashedUpTo = f;
        if (it == m_states.end()) continue;
        m_myHashes[f] = StringId::HashOf(it->second);
        Compare(f);
    }
    while (m_myHashes.size() > kKeepHashes) m_myHashes.erase(m_myHashes.begin());
}

void Rollback::Compare(std::uint32_t frame) {
    auto t = m_theirHashes.find(frame);
    if (t == m_theirHashes.end()) return;
    ++m_compared;
    if (t->second != m_myHashes[frame]) { ++m_desyncs; OKAY_WARN("net: rollback desync at frame ", frame, " (the game is not deterministic)"); }
    m_theirHashes.erase(t);
}

void Rollback::Prune() {
    const std::uint32_t conf = ConfirmedFrame();
    const std::uint32_t keepFrom = std::min(conf, m_rollbackTo);
    while (m_states.size() > kKeepStates && m_states.begin()->first < keepFrom) m_states.erase(m_states.begin());
    while (!m_used.empty() && m_used.begin()->first + 1 < keepFrom) m_used.erase(m_used.begin());
    while (!m_theirHashes.empty() && m_theirHashes.begin()->first < m_hashedUpTo && m_theirHashes.size() > kKeepHashes / 2)
        m_theirHashes.erase(m_theirHashes.begin());
    std::uint32_t ackMin = m_peers[m_me].have;
    for (const auto& [id, p] : m_peers) if (id != m_me) ackMin = std::min(ackMin, p.acked);
    for (auto& [id, p] : m_peers) {
        // Keep the newest input below `have` (the prediction), what may still be resimulated, and ours until acked.
        std::uint32_t keep = keepFrom > 1 ? keepFrom - 1 : 0;
        if (id == m_me) keep = std::min(keep, ackMin);
        while (!p.inputs.empty() && p.inputs.begin()->first < keep && p.inputs.begin()->first + 1 < p.have) p.inputs.erase(p.inputs.begin());
    }
}

} // namespace okay::net
