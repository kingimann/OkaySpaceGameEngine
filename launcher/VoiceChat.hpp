// Voice chat for the launcher: Discord-style calls and server voice rooms.
//
// Audio goes peer -> OkaySpace relay -> peers over UDP (the same TURN-style
// relay games use; see docs/relay.md), so nobody needs an open port. Everyone
// in a room registers with the relay under the room code; whoever holds the
// relay's host slot is the room's hub and fans each frame out to the others
// (if it leaves, the next hello makes someone else the hub).
//
//   capture 16 kHz mono -> 20 ms frames -> voice-activity gate -> IMA ADPCM
//   (4:1, ~64 kbit/s) -> ChaCha20 with the room key -> relay -> hub -> peers
//   -> per-speaker jitter buffer -> mixer -> speakers
//
// The room key comes from the call invite (a private DM) or, for server rooms,
// from the server's invite code, so the relay only ever sees ciphertext.
// No echo cancellation: headphones are recommended.
#pragma once
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <random>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace okay::voice {

// ---- SHA-256 (room key derivation) ------------------------------------------
inline std::array<std::uint8_t, 32> Sha256(const std::string& msg) {
    static const std::uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::vector<std::uint8_t> m(msg.begin(), msg.end());
    const std::uint64_t bits = (std::uint64_t)m.size() * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back((std::uint8_t)(bits >> (i * 8)));
    auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t off = 0; off < m.size(); off += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t)m[off + i * 4] << 24 | (std::uint32_t)m[off + i * 4 + 1] << 16 |
                   (std::uint32_t)m[off + i * 4 + 2] << 8 | m[off + i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::array<std::uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) for (int j = 0; j < 4; ++j) out[(std::size_t)(i * 4 + j)] = (std::uint8_t)(h[i] >> (24 - j * 8));
    return out;
}

// ---- ChaCha20 (RFC 8439) stream cipher ----------------------------------------
inline void ChaCha20Xor(const std::array<std::uint8_t, 32>& key, const std::uint8_t nonce[12],
                        std::uint8_t* data, std::size_t len) {
    auto rd = [](const std::uint8_t* p) { return (std::uint32_t)p[0] | (std::uint32_t)p[1] << 8 | (std::uint32_t)p[2] << 16 | (std::uint32_t)p[3] << 24; };
    std::uint32_t st[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
    for (int i = 0; i < 8; ++i) st[4 + i] = rd(key.data() + i * 4);
    st[12] = 1;
    for (int i = 0; i < 3; ++i) st[13 + i] = rd(nonce + i * 4);
    auto rotl = [](std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    for (std::size_t off = 0; off < len; off += 64, ++st[12]) {
        std::uint32_t x[16]; std::memcpy(x, st, sizeof(x));
        auto qr = [&](int a, int b, int c, int d) {
            x[a] += x[b]; x[d] ^= x[a]; x[d] = rotl(x[d], 16);
            x[c] += x[d]; x[b] ^= x[c]; x[b] = rotl(x[b], 12);
            x[a] += x[b]; x[d] ^= x[a]; x[d] = rotl(x[d], 8);
            x[c] += x[d]; x[b] ^= x[c]; x[b] = rotl(x[b], 7);
        };
        for (int r = 0; r < 10; ++r) {
            qr(0, 4, 8, 12); qr(1, 5, 9, 13); qr(2, 6, 10, 14); qr(3, 7, 11, 15);
            qr(0, 5, 10, 15); qr(1, 6, 11, 12); qr(2, 7, 8, 13); qr(3, 4, 9, 14);
        }
        std::uint8_t ks[64];
        for (int i = 0; i < 16; ++i) {
            const std::uint32_t v = x[i] + st[i];
            ks[i * 4] = (std::uint8_t)v; ks[i * 4 + 1] = (std::uint8_t)(v >> 8);
            ks[i * 4 + 2] = (std::uint8_t)(v >> 16); ks[i * 4 + 3] = (std::uint8_t)(v >> 24);
        }
        for (std::size_t i = 0; i < 64 && off + i < len; ++i) data[off + i] ^= ks[i];
    }
}

// ---- IMA ADPCM (16-bit PCM <-> 4-bit codes, 4:1) ------------------------------
static constexpr int kFrame = 320;   // 20 ms at 16 kHz
static constexpr int kRate = 16000;
inline const int* AdpcmSteps() {
    static const int s[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,
        130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,
        1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,
        11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
    return s;
}
inline int AdpcmIndexAdj(int code) { static const int t[8] = {-1,-1,-1,-1,2,4,6,8}; return t[code & 7]; }
struct AdpcmState { int pred = 0; int index = 0; };
inline int AdpcmStep(AdpcmState& s, int code) {
    const int step = AdpcmSteps()[s.index];
    int diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    s.pred += (code & 8) ? -diff : diff;
    s.pred = std::clamp(s.pred, -32768, 32767);
    s.index = std::clamp(s.index + AdpcmIndexAdj(code), 0, 88);
    return s.pred;
}
inline void AdpcmEncode(const std::int16_t* in, int n, AdpcmState& s, std::uint8_t* out) {
    for (int i = 0; i < n; ++i) {
        const int step = AdpcmSteps()[s.index];
        int diff = in[i] - s.pred, code = 0;
        if (diff < 0) { code = 8; diff = -diff; }
        if (diff >= step) { code |= 4; diff -= step; }
        if (diff >= step >> 1) { code |= 2; diff -= step >> 1; }
        if (diff >= step >> 2) code |= 1;
        AdpcmStep(s, code);
        if (i & 1) out[i >> 1] |= (std::uint8_t)(code << 4); else out[i >> 1] = (std::uint8_t)code;
    }
}
inline void AdpcmDecode(const std::uint8_t* in, int n, AdpcmState& s, std::int16_t* out) {
    for (int i = 0; i < n; ++i) out[i] = (std::int16_t)AdpcmStep(s, (i & 1) ? (in[i >> 1] >> 4) : (in[i >> 1] & 15));
}

// ---- A tiny non-blocking UDP socket --------------------------------------------
class Udp {
public:
    ~Udp() { Close(); }
    bool Open() {
#if defined(_WIN32)
        WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false;
        m_wsa = true;
#endif
        m_s = (long long)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_s < 0) return false;
#if defined(_WIN32)
        u_long nb = 1; ioctlsocket((SOCKET)m_s, FIONBIO, &nb);
#else
        fcntl((int)m_s, F_SETFL, fcntl((int)m_s, F_GETFL, 0) | O_NONBLOCK);
#endif
        return true;
    }
    void Close() {
        if (m_s >= 0) {
#if defined(_WIN32)
            closesocket((SOCKET)m_s);
#else
            close((int)m_s);
#endif
            m_s = -1;
        }
#if defined(_WIN32)
        if (m_wsa) { WSACleanup(); m_wsa = false; }
#endif
    }
    bool Resolve(const std::string& host, std::uint16_t port) {
        addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
        std::memcpy(&m_to, res->ai_addr, sizeof(sockaddr_in));
        m_to.sin_port = htons(port);
        freeaddrinfo(res);
        return true;
    }
    void Send(const std::uint8_t* d, std::size_t n) {
        if (m_s >= 0) sendto((decltype(socket(0, 0, 0)))m_s, (const char*)d, (int)n, 0, (const sockaddr*)&m_to, sizeof(m_to));
    }
    int Recv(std::uint8_t* d, std::size_t cap) {
        if (m_s < 0) return -1;
        sockaddr_in from{}; socklen_t fl = sizeof(from);
        const int n = (int)recvfrom((decltype(socket(0, 0, 0)))m_s, (char*)d, (int)cap, 0, (sockaddr*)&from, &fl);
        if (n <= 0) return 0;
        if (from.sin_addr.s_addr != m_to.sin_addr.s_addr || from.sin_port != m_to.sin_port) return 0;   // only the relay
        return n;
    }
private:
    long long m_s = -1;
    sockaddr_in m_to{};
#if defined(_WIN32)
    bool m_wsa = false;
#endif
};

struct PeerView { std::uint32_t slot = 0; std::string uid, name; bool speaking = false, muted = false, deafened = false; };

// ---- One voice room session ------------------------------------------------------
class Session {
public:
    enum State { Idle = 0, Connecting = 1, Connected = 2, Failed = 3 };

    ~Session() { Stop(); }

    // Join `room` on the relay at host:port. `keyText` derives the encryption key.
    bool Start(const std::string& relayHost, std::uint16_t relayPort, const std::string& room,
               const std::string& keyText, const std::string& myUid, const std::string& myName) {
        Stop();
        if (!m_mx) m_mx = SDL_CreateMutex();
        m_room = room; m_uid = myUid; m_name = myName;
        m_key = Sha256("okayspace-voice-v1|" + keyText);
        std::random_device rd; m_sid = ((std::uint32_t)rd() << 1) | 1u; m_seq = 0;
        m_state = Connecting; m_error.clear(); m_since = 0; m_startTicks = SDL_GetTicks();
        m_mySlot = m_hubSlot = 0;
        if (!m_udp.Open() || !m_udp.Resolve(relayHost, relayPort)) { m_state = Failed; m_error = "Can't reach the voice server " + relayHost; m_udp.Close(); return false; }
        // Audio devices (SDL converts to/from our 16 kHz mono s16).
        SDL_AudioSpec want{}; want.freq = kRate; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = kFrame;
        want.callback = &Session::CaptureCb; want.userdata = this;
        m_cap = SDL_OpenAudioDevice(nullptr, 1, &want, nullptr, 0);
        SDL_AudioSpec pw = want; pw.callback = &Session::PlayCb;
        m_play = SDL_OpenAudioDevice(nullptr, 0, &pw, nullptr, 0);
        if (!m_play) { m_state = Failed; m_error = "No audio output device"; Stop(); return false; }
        if (!m_cap) m_error = "No microphone found \xE2\x80\x94 you can listen but not talk";
        m_run = true;
        m_thread = SDL_CreateThread(&Session::NetThread, "okay-voice", this);
        if (m_cap) SDL_PauseAudioDevice(m_cap, 0);
        SDL_PauseAudioDevice(m_play, 0);
        return true;
    }
    void Stop() {
        if (m_run) {
            m_run = false;
            if (m_thread) { SDL_WaitThread(m_thread, nullptr); m_thread = nullptr; }
        }
        if (m_cap) { SDL_CloseAudioDevice(m_cap); m_cap = 0; }
        if (m_play) { SDL_CloseAudioDevice(m_play); m_play = 0; }
        m_udp.Close();
        if (m_mx) { SDL_LockMutex(m_mx); m_peers.clear(); m_capQ.clear(); SDL_UnlockMutex(m_mx); }
        if (m_state != Failed) m_state = Idle;
        m_room.clear();
    }
    bool Active() const { return m_state == Connecting || m_state == Connected; }
    int StateNow() const { return m_state; }
    const std::string& Error() const { return m_error; }
    const std::string& Room() const { return m_room; }
    Uint32 ConnectedSince() const { return m_since; }
    void SetMuted(bool m) { m_muted = m; }
    void SetDeafened(bool d) { m_deaf = d; }
    bool Muted() const { return m_muted; }
    bool Deafened() const { return m_deaf; }
    bool SelfSpeaking() const { return m_selfSpeaking && !m_muted && !m_deaf; }
    float SelfLevel() const { return m_selfLevel; }
    // Mic sensitivity in dBFS (frames quieter than this are not sent).
    void SetGate(float dbfs) { m_gateDb = dbfs; }
    std::vector<PeerView> Peers() {
        std::vector<PeerView> out;
        if (!m_mx) return out;
        SDL_LockMutex(m_mx);
        const Uint32 now = SDL_GetTicks();
        for (auto& [slot, p] : m_peers) {
            if (now - p.lastHeard > 5000) continue;
            PeerView v; v.slot = slot; v.uid = p.uid; v.name = p.name.empty() ? "Connecting\xE2\x80\xA6" : p.name;
            v.speaking = now - p.lastVoice < 300; v.muted = p.muted; v.deafened = p.deaf;
            out.push_back(v);
        }
        SDL_UnlockMutex(m_mx);
        return out;
    }

private:
    struct Peer {
        std::string uid, name; bool muted = false, deaf = false;
        Uint32 lastHeard = 0, lastVoice = 0;
        AdpcmState dec; std::uint32_t lastSeq = 0;
        std::deque<std::array<std::int16_t, kFrame>> jitter; bool primed = false; int readPos = 0;
        std::uint32_t sid = 0;
    };
    enum : std::uint8_t { kHello = 0x90, kWelcome = 0x91, kData = 0x92, kBye = 0x93 };
    enum : std::uint8_t { kAudio = 1, kInfo = 2, kLeave = 3 };

    static void Put32(std::vector<std::uint8_t>& v, std::uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((std::uint8_t)(x >> (i * 8))); }
    static std::uint32_t Get32(const std::uint8_t* p) { return (std::uint32_t)p[0] | (std::uint32_t)p[1] << 8 | (std::uint32_t)p[2] << 16 | (std::uint32_t)p[3] << 24; }

    static void CaptureCb(void* ud, Uint8* stream, int len) {
        Session* s = (Session*)ud;
        const std::int16_t* smp = (const std::int16_t*)stream;
        const int n = len / 2;
        SDL_LockMutex(s->m_mx);
        for (int i = 0; i < n; ++i) {
            s->m_capBuf[(std::size_t)s->m_capFill++] = smp[i];
            if (s->m_capFill == kFrame) {
                if (s->m_capQ.size() < 25) s->m_capQ.push_back(s->m_capBuf);
                s->m_capFill = 0;
            }
        }
        SDL_UnlockMutex(s->m_mx);
    }
    static void PlayCb(void* ud, Uint8* stream, int len) {
        Session* s = (Session*)ud;
        std::int16_t* out = (std::int16_t*)stream;
        const int n = len / 2;
        std::vector<int> mix((std::size_t)n, 0);
        SDL_LockMutex(s->m_mx);
        for (auto& [slot, p] : s->m_peers) {
            if (!p.primed) { if (p.jitter.size() >= 3) p.primed = true; else continue; }   // 60 ms cushion
            for (int i = 0; i < n; ++i) {
                if (p.jitter.empty()) { p.primed = false; break; }   // underrun: rebuffer
                mix[(std::size_t)i] += p.jitter.front()[(std::size_t)p.readPos++];
                if (p.readPos == kFrame) { p.jitter.pop_front(); p.readPos = 0; }
            }
        }
        SDL_UnlockMutex(s->m_mx);
        const bool deaf = s->m_deaf;
        for (int i = 0; i < n; ++i) out[i] = deaf ? 0 : (std::int16_t)std::clamp(mix[(std::size_t)i], -32768, 32767);
    }

    // Wrap an app message for the relay and send it to `dest`.
    void SendTo(std::uint32_t dest, std::uint8_t kind, std::uint32_t origin, std::vector<std::uint8_t> body) {
        std::vector<std::uint8_t> pk; pk.push_back(kData); Put32(pk, dest);
        AppendApp(pk, kind, origin, body);
        m_udp.Send(pk.data(), pk.size());
    }
    // App message: 'V' kind origin sid seq | encrypted("OKV1" + body)
    void AppendApp(std::vector<std::uint8_t>& pk, std::uint8_t kind, std::uint32_t origin, std::vector<std::uint8_t>& body) {
        const std::uint32_t seq = ++m_seq;
        pk.push_back('V'); pk.push_back(kind); Put32(pk, origin); Put32(pk, m_sid); Put32(pk, seq);
        std::vector<std::uint8_t> enc = {'O', 'K', 'V', '1'};
        enc.insert(enc.end(), body.begin(), body.end());
        std::uint8_t nonce[12] = {}; std::memcpy(nonce, &m_sid, 4); std::memcpy(nonce + 4, &seq, 4);
        ChaCha20Xor(m_key, nonce, enc.data(), enc.size());
        pk.insert(pk.end(), enc.begin(), enc.end());
    }
    // Forward a received app message unchanged except its origin (hub only).
    void Forward(std::uint32_t dest, const std::uint8_t* app, std::size_t n, std::uint32_t origin) {
        std::vector<std::uint8_t> pk; pk.push_back(kData); Put32(pk, dest);
        pk.insert(pk.end(), app, app + n);
        std::memcpy(pk.data() + 5 + 2, &origin, 4);
        m_udp.Send(pk.data(), pk.size());
    }
    void Broadcast(std::uint8_t kind, std::vector<std::uint8_t> body) {
        if (m_mySlot == 0 || m_hubSlot == 0) return;
        if (m_mySlot != m_hubSlot) { SendTo(m_hubSlot, kind, 0, body); return; }
        // I'm the hub: send to every member I've heard from.
        std::vector<std::uint32_t> dests;
        SDL_LockMutex(m_mx);
        const Uint32 now = SDL_GetTicks();
        for (auto& [slot, p] : m_peers) if (now - p.lastHeard < 5000) dests.push_back(slot);
        SDL_UnlockMutex(m_mx);
        for (std::uint32_t d : dests) SendTo(d, kind, m_mySlot, body);
    }
    void SendHello() {
        std::vector<std::uint8_t> pk = {kHello, 0 /* host role: first one in is the hub */};
        Put32(pk, (std::uint32_t)m_room.size());
        pk.insert(pk.end(), m_room.begin(), m_room.end());
        m_udp.Send(pk.data(), pk.size());
    }
    void HandleApp(std::uint32_t src, const std::uint8_t* app, std::size_t n) {
        if (n < 14 + 4 || app[0] != 'V') return;
        const std::uint8_t kind = app[1];
        std::uint32_t origin = Get32(app + 2);
        const std::uint32_t sid = Get32(app + 6), seq = Get32(app + 10);
        if (origin == 0) origin = src;   // sent by the peer itself (to me as hub)
        if (origin == m_mySlot) return;
        const bool amHub = m_mySlot != 0 && m_mySlot == m_hubSlot;
        if (amHub && src != m_mySlot) {   // relay it to everyone else in the room
            std::vector<std::uint32_t> dests;
            SDL_LockMutex(m_mx);
            const Uint32 now = SDL_GetTicks();
            m_peers[src].lastHeard = now;
            for (auto& [slot, p] : m_peers) if (slot != src && now - p.lastHeard < 5000) dests.push_back(slot);
            SDL_UnlockMutex(m_mx);
            for (std::uint32_t d : dests) Forward(d, app, n, origin);
        }
        std::vector<std::uint8_t> body(app + 14, app + n);
        std::uint8_t nonce[12] = {}; std::memcpy(nonce, &sid, 4); std::memcpy(nonce + 4, &seq, 4);
        ChaCha20Xor(m_key, nonce, body.data(), body.size());
        if (body.size() < 4 || std::memcmp(body.data(), "OKV1", 4) != 0) { ++m_badKey; return; }   // wrong key
        const std::uint8_t* b = body.data() + 4; const std::size_t bl = body.size() - 4;
        SDL_LockMutex(m_mx);
        Peer& p = m_peers[origin];
        p.lastHeard = SDL_GetTicks();
        if (p.sid != sid) { p.sid = sid; p.dec = AdpcmState{}; p.lastSeq = 0; p.jitter.clear(); p.primed = false; }
        if (kind == kInfo && bl >= 3) {
            p.muted = b[0] & 1; p.deaf = (b[0] & 2) != 0;
            const std::size_t ul = b[1];
            if (2 + ul < bl) {
                p.uid.assign((const char*)b + 2, ul);
                const std::size_t nl = b[2 + ul];
                if (3 + ul + nl <= bl) p.name.assign((const char*)b + 3 + ul, nl);
            }
        } else if (kind == kAudio && bl >= 4 + kFrame / 2) {
            if (seq > p.lastSeq || p.lastSeq - seq > 1000) {   // drop late/duplicate frames
                p.lastSeq = seq;
                p.dec.pred = (std::int16_t)(b[1] | (b[2] << 8)); p.dec.index = std::min<int>(b[3], 88);
                std::array<std::int16_t, kFrame> pcm{};
                AdpcmDecode(b + 4, kFrame, p.dec, pcm.data());
                if (b[0] > 0) p.lastVoice = SDL_GetTicks();
                p.jitter.push_back(pcm);
                while (p.jitter.size() > 12) { p.jitter.pop_front(); p.readPos = 0; }   // cap latency
            }
        } else if (kind == kLeave) {
            m_peers.erase(origin);
        }
        SDL_UnlockMutex(m_mx);
    }

    static int NetThread(void* ud) {
        Session* s = (Session*)ud;
        std::vector<std::uint8_t> buf(2048);
        Uint32 nextHello = 0, nextInfo = 0, lastWelcome = 0, voiceUntil = 0;
        AdpcmState enc;
        while (s->m_run) {
            const Uint32 now = SDL_GetTicks();
            if (now >= nextHello) { s->SendHello(); nextHello = now + 1000; }
            int n;
            while ((n = s->m_udp.Recv(buf.data(), buf.size())) > 0) {
                if (buf[0] == kWelcome && n >= 9) {
                    const std::uint32_t mine = Get32(&buf[1]), hub = Get32(&buf[5]);
                    if (hub != s->m_hubSlot) { SDL_LockMutex(s->m_mx); s->m_peers.clear(); SDL_UnlockMutex(s->m_mx); }
                    s->m_mySlot = mine; s->m_hubSlot = hub; lastWelcome = now;
                    if (s->m_state != Connected) { s->m_state = Connected; s->m_since = now; nextInfo = 0; }
                } else if (buf[0] == kData && n >= 5) {
                    s->HandleApp(Get32(&buf[1]), buf.data() + 5, (std::size_t)n - 5);
                }
            }
            if (s->m_state == Connecting && now - s->m_startTicks > 8000 && lastWelcome == 0) {
                s->m_error = "The voice server isn't answering"; s->m_state = Failed;
            }
            if (s->m_state == Connected && now >= nextInfo) {   // who I am + mute state, every 2 s
                std::vector<std::uint8_t> b;
                b.push_back((std::uint8_t)((s->m_muted ? 1 : 0) | (s->m_deaf ? 2 : 0)));
                const std::string u = s->m_uid.substr(0, 64), nm = s->m_name.substr(0, 64);
                b.push_back((std::uint8_t)u.size()); b.insert(b.end(), u.begin(), u.end());
                b.push_back((std::uint8_t)nm.size()); b.insert(b.end(), nm.begin(), nm.end());
                s->Broadcast(kInfo, b);
                nextInfo = now + 2000;
            }
            // Microphone frames: gate on loudness (with a short hang-over), encode, send.
            for (;;) {
                std::array<std::int16_t, kFrame> fr;
                SDL_LockMutex(s->m_mx);
                const bool have = !s->m_capQ.empty();
                if (have) { fr = s->m_capQ.front(); s->m_capQ.pop_front(); }
                SDL_UnlockMutex(s->m_mx);
                if (!have) break;
                double e = 0; for (std::int16_t v : fr) e += (double)v * v;
                const float rms = (float)std::sqrt(e / kFrame);
                const float db = 20.0f * std::log10(std::max(1.0f, rms) / 32768.0f);
                s->m_selfLevel = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
                if (db > s->m_gateDb) voiceUntil = SDL_GetTicks() + 350;
                const bool talk = SDL_GetTicks() < voiceUntil;
                s->m_selfSpeaking = talk;
                if (!talk || s->m_muted || s->m_deaf || s->m_state != Connected) continue;
                std::vector<std::uint8_t> b(4 + kFrame / 2, 0);
                b[0] = (std::uint8_t)std::max(1, (int)(s->m_selfLevel * 255));
                b[1] = (std::uint8_t)(enc.pred & 0xFF); b[2] = (std::uint8_t)((enc.pred >> 8) & 0xFF); b[3] = (std::uint8_t)enc.index;
                AdpcmEncode(fr.data(), kFrame, enc, b.data() + 4);
                s->Broadcast(kAudio, b);
            }
            SDL_Delay(4);
        }
        if (s->m_mySlot) { s->Broadcast(kLeave, {}); std::uint8_t bye = kBye; s->m_udp.Send(&bye, 1); }
        return 0;
    }

    Udp m_udp;
    SDL_mutex* m_mx = nullptr;
    SDL_Thread* m_thread = nullptr;
    volatile bool m_run = false;
    SDL_AudioDeviceID m_cap = 0, m_play = 0;
    std::array<std::uint8_t, 32> m_key{};
    std::string m_room, m_uid, m_name, m_error;
    std::uint32_t m_sid = 0, m_seq = 0;
    volatile std::uint32_t m_mySlot = 0, m_hubSlot = 0;
    volatile int m_state = Idle;
    Uint32 m_since = 0, m_startTicks = SDL_GetTicks();
    volatile bool m_muted = false, m_deaf = false, m_selfSpeaking = false;
    volatile float m_selfLevel = 0.0f, m_gateDb = -46.0f;
    int m_badKey = 0;
    std::map<std::uint32_t, Peer> m_peers;                    // (m_mx) by relay slot
    std::deque<std::array<std::int16_t, kFrame>> m_capQ;      // (m_mx) mic frames to send
    std::array<std::int16_t, kFrame> m_capBuf{}; int m_capFill = 0;
};

} // namespace okay::voice
