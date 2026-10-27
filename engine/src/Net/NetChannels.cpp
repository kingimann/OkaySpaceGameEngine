#include "okay/Net/NetChannels.hpp"
#include "okay/Net/Packet.hpp"
#include <algorithm>

namespace okay::net {

bool ChannelNameOk(const std::string& channel) {
    if (channel.empty() || channel.size() > kChannelNameMax) return false;
    return channel.find('\n') == std::string::npos && channel.find('\0') == std::string::npos;
}

namespace {

void WriteCookie(Packet& p, std::uint64_t cookie) {
    if (!cookie) return;
    p.Write(static_cast<std::uint32_t>(cookie));
    p.Write(static_cast<std::uint32_t>(cookie >> 32));
}

std::vector<std::uint8_t> Bytes(const Packet& p) {
    return std::vector<std::uint8_t>(p.Data(), p.Data() + p.Size());
}

} // namespace

std::vector<std::uint8_t> BuildChanRel(std::uint32_t seq, const std::string& channel,
                                       const std::string& data, std::uint64_t cookie) {
    Packet p(kChanRel);
    p.Write(seq);
    p.Write(channel);
    p.Write(data);
    WriteCookie(p, cookie);
    return Bytes(p);
}

std::vector<std::uint8_t> BuildChanRelAck(const std::string& channel, std::uint32_t ack) {
    Packet p(kChanRelAck);
    p.Write(channel);
    p.Write(ack);
    return Bytes(p);
}

std::vector<std::uint8_t> BuildChanSeq(std::uint32_t sender, std::uint32_t seq, std::uint32_t target,
                                       const std::string& channel, const std::string& data,
                                       std::uint64_t cookie) {
    Packet p(kChanSeq);
    p.Write(sender);
    p.Write(seq);
    p.Write(target);
    p.Write(channel);
    p.Write(data);
    WriteCookie(p, cookie);
    return Bytes(p);
}

std::vector<std::uint8_t> OrderedEnd::Push(const std::string& channel, const std::string& data,
                                           std::uint64_t cookie) {
    if (!ChannelNameOk(channel) || data.size() > kChannelPayloadMax) return {};
    if (!m_out.count(channel) && m_out.size() >= kMaxChannels) return {};
    OutStream& st = m_out[channel];
    if (st.pending.size() >= kOrderedWindow || st.next == 0xFFFFFFFFu) return {};
    const std::uint32_t seq = st.next++;
    std::vector<std::uint8_t> bytes = BuildChanRel(seq, channel, data, cookie);
    st.pending.emplace(seq, Slot{bytes, 0.0f});
    return bytes;
}

void OrderedEnd::CollectResend(float dt, std::vector<std::vector<std::uint8_t>>& due) {
    for (auto& stream : m_out) {
        for (auto& item : stream.second.pending) {
            item.second.age += dt;
            if (item.second.age >= kChannelResend) {
                item.second.age = 0.0f;
                due.push_back(item.second.bytes);
            }
        }
    }
}

void OrderedEnd::Ack(const std::string& channel, std::uint32_t ack) {
    auto it = m_out.find(channel);
    if (it == m_out.end()) return;
    auto& pending = it->second.pending;
    while (!pending.empty() && pending.begin()->first <= ack) pending.erase(pending.begin());
}

bool OrderedEnd::Seen(const std::string& channel, std::uint32_t seq) const {
    auto it = m_in.find(channel);
    if (it == m_in.end() || seq == 0) return false;
    if (seq < it->second.next) return true;
    return it->second.ahead.count(seq) != 0;
}

bool OrderedEnd::Receive(const std::string& channel, std::uint32_t seq, std::string data, bool deliver,
                         std::uint32_t from, std::vector<ChanMsg>& out, std::uint32_t& ack) {
    ack = 0;
    if (seq == 0 || !ChannelNameOk(channel)) return false;
    if (!m_in.count(channel) && m_in.size() >= kMaxChannels) return false;
    InStream& in = m_in[channel];
    if (seq > in.next && seq - in.next >= kOrderedWindow) return false;
    if (seq < in.next) {
        ack = in.next - 1;
        return true;
    }
    if (seq > in.next) {
        in.ahead.emplace(seq, Held{std::move(data), deliver});
        while (in.ahead.size() > kOrderedWindow) in.ahead.erase(std::prev(in.ahead.end()));
        if (in.next > 1) ack = in.next - 1;
        return ack != 0;
    }
    auto take = [&](Held& held) {
        if (!held.deliver) return;
        ChanMsg msg;
        msg.from = from;
        msg.channel = channel;
        msg.data = std::move(held.data);
        out.push_back(std::move(msg));
    };
    Held first{std::move(data), deliver};
    take(first);
    ++in.next;
    for (;;) {
        auto it = in.ahead.find(in.next);
        if (it == in.ahead.end()) break;
        take(it->second);
        in.ahead.erase(it);
        ++in.next;
    }
    ack = in.next - 1;
    return true;
}

void OrderedEnd::Clear() {
    m_out.clear();
    m_in.clear();
}

std::string SequencedRx::Key(std::uint32_t from, const std::string& channel) {
    std::string key = std::to_string(from);
    key.push_back('\n');
    key += channel;
    return key;
}

std::uint32_t SequencedTx::Next(const std::string& channel) {
    if (!ChannelNameOk(channel)) return 0;
    if (!m_next.count(channel) && m_next.size() >= kMaxChannels) return 0;
    std::uint32_t& n = m_next[channel];
    if (n == 0xFFFFFFFFu) return 0;
    n = n == 0 ? 1u : n + 1u;
    return n;
}

bool SequencedRx::IsStale(std::uint32_t from, const std::string& channel, std::uint32_t seq) const {
    if (seq == 0 || !ChannelNameOk(channel)) return true;
    auto it = m_last.find(Key(from, channel));
    if (it == m_last.end()) return false;
    return static_cast<std::int32_t>(seq - it->second) <= 0;
}

bool SequencedRx::Accept(std::uint32_t from, const std::string& channel, std::uint32_t seq) {
    if (seq == 0 || !ChannelNameOk(channel)) return false;
    const std::string key = Key(from, channel);
    auto it = m_last.find(key);
    if (it == m_last.end()) {
        if (m_last.size() >= kMaxSeqKeys) return false;
        m_last.emplace(key, seq);
        return true;
    }
    if (static_cast<std::int32_t>(seq - it->second) <= 0) return false;
    it->second = seq;
    return true;
}

} // namespace okay::net
