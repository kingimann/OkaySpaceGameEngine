// Photon Realtime transport adapter (opt-in).
//
// Photon's Realtime C++ SDK is a closed-source, per-platform download from
// dashboard.photonengine.com, so it can't be bundled with the engine. This
// whole file therefore compiles to an empty TU unless the build opts in:
//
//     cmake -DOKAY_WITH_PHOTON=ON -DOKAY_PHOTON_SDK_DIR=/path/to/photon-sdk ...
//
// which defines OKAY_WITH_PHOTON and puts the SDK's headers/libs on the path
// (see docs/photon.md for the full walkthrough). Without the flag the Photon
// provider stays a safe stub and the engine remains self-contained.
//
// This adapter targets the Photon Realtime C++ SDK v5.x "LoadBalancing" API.
// Photon's headers move a little between minor versions — if your SDK differs,
// the fixes are usually confined to the includes and the Listener signatures.
//
// Mapping onto INetTransport:
//   StartServer(port)          -> connect + create room "okay-<port>"
//   StartClient(host, port)    -> connect + join   room "okay-<port>"
//   HostViaRelay(appId,_,code) -> connect (appId overrides env) + create room <code>
//   JoinViaRelay(appId,_,code) -> connect + join room <code>
//   Send / SendReliable        -> opRaiseEvent(code 1), channel + payload packed
//   Rpc / OnRpc                -> opRaiseEvent(code 2), name + payload packed
//   Update()                   -> LoadBalancing::Client::service() (call per frame)
//
// The App ID comes from the OKAY_PHOTON_APPID environment variable unless a
// HostViaRelay/JoinViaRelay call passes one explicitly in its host argument.
#ifdef OKAY_WITH_PHOTON

#include "okay/Net/NetTransport.hpp"

#include "LoadBalancing-cpp/inc/Client.h"
#include "LoadBalancing-cpp/inc/Listener.h"

#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#if defined(_WIN32)
#include <windows.h>   // Sleep
#else
#include <ctime>       // nanosleep
#endif

namespace okay {

namespace {

// Event codes on the Photon room channel.
constexpr nByte kEvMessage = 1;   // [reliableFlag][channel]\0[data]
constexpr nByte kEvRpc     = 2;   // [name]\0[data]

class PhotonNetTransport : public INetTransport,
                           private ExitGames::LoadBalancing::Listener {
public:
    PhotonNetTransport() = default;
    ~PhotonNetTransport() override { Stop(); }

    const char* BackendName() const override { return "Photon"; }
    bool Available() const override { return true; }

    bool StartServer(std::uint16_t port) override {
        return Connect({}, "okay-" + std::to_string(port), /*create=*/true);
    }
    bool StartClient(const std::string&, std::uint16_t port) override {
        return Connect({}, "okay-" + std::to_string(port), /*create=*/false);
    }
    bool HostViaRelay(const std::string& appId, std::uint16_t, const std::string& code) override {
        return Connect(appId, code.empty() ? "okay-room" : code, /*create=*/true);
    }
    bool JoinViaRelay(const std::string& appId, std::uint16_t, const std::string& code) override {
        return Connect(appId, code.empty() ? "okay-room" : code, /*create=*/false);
    }

    void Stop() override {
        if (m_client) {
            m_client->opLeaveRoom();
            m_client->disconnect();
            // A few service pumps so leave/disconnect actually go out.
            for (int i = 0; i < 30 && m_client->getIsInRoom(); ++i) m_client->service();
            m_client.reset();
        }
        m_inRoom = false;
        m_isMaster = false;
    }

    void Update() override { if (m_client) m_client->service(); }

    bool IsServer() const override { return m_inRoom && m_isMaster; }
    bool IsClient() const override { return m_inRoom && !m_isMaster; }
    bool IsConnected() const override { return m_inRoom; }
    std::uint32_t LocalId() const override {
        return m_client ? (std::uint32_t)m_client->getLocalPlayer().getNumber() : 0;
    }
    std::size_t PeerCount() const override {
        if (!m_client || !m_inRoom) return 0;
        int n = m_client->getCurrentlyJoinedRoom().getPlayerCount();
        return n > 0 ? (std::size_t)(n - 1) : 0;   // peers = everyone but me
    }

    void Send(const std::string& channel, const std::string& data) override {
        Raise(kEvMessage, channel + '\0' + data, /*reliable=*/false);
    }
    void SendReliable(const std::string& channel, const std::string& data) override {
        Raise(kEvMessage, channel + '\0' + data, /*reliable=*/true);
    }
    void Rpc(const std::string& name, const std::string& data) override {
        Raise(kEvRpc, name + '\0' + data, /*reliable=*/true);
    }
    void OnRpc(const std::string& name,
               std::function<void(std::uint32_t, const std::string&)> f) override {
        m_rpcHandlers[name] = std::move(f);
    }

private:
    bool Connect(const std::string& appIdOverride, const std::string& room, bool create) {
        Stop();
        std::string appId = appIdOverride;
        if (appId.empty())
            if (const char* env = std::getenv("OKAY_PHOTON_APPID")) appId = env;
        if (appId.empty()) return false;   // no App ID -> can't reach Photon Cloud

        m_pendingRoom = room;
        m_pendingCreate = create;
        m_client = std::make_unique<ExitGames::LoadBalancing::Client>(
            *this, appId.c_str(), "1.0");
        if (!m_client->connect()) { m_client.reset(); return false; }
        // Pump until we're in the room or the attempt clearly failed (~5s).
        for (int i = 0; i < 500 && m_client && !m_inRoom && !m_failed; ++i) {
            m_client->service();
            SDL_DelayCompat();
        }
        return m_inRoom;
    }

    // 10ms sleep without dragging SDL in: portable this_thread wait.
    static void SDL_DelayCompat() {
        struct timespec ts { 0, 10 * 1000 * 1000 };
#if defined(_WIN32)
        Sleep(10);
        (void)ts;
#else
        nanosleep(&ts, nullptr);
#endif
    }

    void Raise(nByte code, const std::string& packed, bool reliable) {
        if (!m_client || !m_inRoom) return;
        m_client->opRaiseEvent(reliable,
                               reinterpret_cast<const nByte*>(packed.data()),
                               (int)packed.size(), code);
    }

    // ---- ExitGames::LoadBalancing::Listener --------------------------------
    void connectReturn(int errorCode, const ExitGames::Common::JString&,
                       const ExitGames::Common::JString&, const ExitGames::Common::JString&) override {
        if (errorCode) { m_failed = true; return; }
        // Connected to the master server: enter (or create) the room.
        ExitGames::LoadBalancing::RoomOptions opts;
        if (m_pendingCreate)
            m_client->opCreateRoom(m_pendingRoom.c_str(), opts);
        else
            m_client->opJoinOrCreateRoom(m_pendingRoom.c_str(), opts);
    }
    void createRoomReturn(int, const ExitGames::Common::Hashtable&,
                          const ExitGames::Common::Hashtable&, int errorCode,
                          const ExitGames::Common::JString&) override {
        if (errorCode) { m_failed = true; return; }
        m_inRoom = true; m_isMaster = true;
    }
    void joinOrCreateRoomReturn(int, const ExitGames::Common::Hashtable&,
                                const ExitGames::Common::Hashtable&, int errorCode,
                                const ExitGames::Common::JString&) override {
        if (errorCode) { m_failed = true; return; }
        m_inRoom = true;
        m_isMaster = m_client->getLocalPlayer().getIsMasterClient();
    }
    void leaveRoomReturn(int, const ExitGames::Common::JString&) override { m_inRoom = false; }
    void disconnectReturn() override { m_inRoom = false; }
    void connectionErrorReturn(int) override { m_failed = true; m_inRoom = false; }
    void clientErrorReturn(int) override {}
    void warningReturn(int) override {}
    void serverErrorReturn(int) override {}
    void joinRoomEventAction(int, const ExitGames::Common::JVector<int>&,
                             const ExitGames::LoadBalancing::Player&) override {}
    void leaveRoomEventAction(int, bool) override {}
    void customEventAction(int playerNr, nByte eventCode,
                           const ExitGames::Common::Object& eventContent) override {
        // Both event codes carry a raw byte payload: [key]\0[data].
        ExitGames::Common::ValueObject<nByte*> raw(eventContent);
        const nByte* bytes = *raw.getDataAddress();
        int size = *raw.getSizes();
        if (!bytes || size <= 0) return;
        std::string packed(reinterpret_cast<const char*>(bytes), (std::size_t)size);
        std::size_t sep = packed.find('\0');
        if (sep == std::string::npos) return;
        std::string key = packed.substr(0, sep), data = packed.substr(sep + 1);
        if (eventCode == kEvRpc) {
            auto it = m_rpcHandlers.find(key);
            if (it != m_rpcHandlers.end()) it->second((std::uint32_t)playerNr, data);
        }
        // kEvMessage: channel messages surface through the same RPC handler map
        // when a handler was registered under the channel name.
        else if (eventCode == kEvMessage) {
            auto it = m_rpcHandlers.find(key);
            if (it != m_rpcHandlers.end()) it->second((std::uint32_t)playerNr, data);
        }
    }
    // -------------------------------------------------------------------------

    std::unique_ptr<ExitGames::LoadBalancing::Client> m_client;
    std::unordered_map<std::string, std::function<void(std::uint32_t, const std::string&)>> m_rpcHandlers;
    std::string m_pendingRoom;
    bool m_pendingCreate = false;
    bool m_inRoom = false;
    bool m_isMaster = false;
    bool m_failed = false;
};

} // namespace

std::unique_ptr<INetTransport> CreatePhotonNetTransport() {
    return std::make_unique<PhotonNetTransport>();
}

} // namespace okay

#endif // OKAY_WITH_PHOTON
