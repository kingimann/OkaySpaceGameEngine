#include "okay/Net/NetTransport.hpp"

namespace okay {

namespace {

// ---- Native: forwards to a NetworkManager (the built-in UDP/relay stack). ----
class NativeNetTransport : public INetTransport {
public:
    explicit NativeNetTransport(NetworkManager* nm) : m_nm(nm) {}
    const char* BackendName() const override { return "Native"; }
    bool Available() const override { return m_nm != nullptr; }

    bool StartServer(std::uint16_t port) override { return m_nm && m_nm->StartServer(port); }
    bool StartClient(const std::string& host, std::uint16_t port) override {
        return m_nm && m_nm->StartClient(host, port);
    }
    bool HostViaRelay(const std::string& h, std::uint16_t p, const std::string& code) override {
        return m_nm && m_nm->HostViaRelay(h, p, code);
    }
    bool JoinViaRelay(const std::string& h, std::uint16_t p, const std::string& code) override {
        return m_nm && m_nm->JoinViaRelay(h, p, code);
    }
    void Stop() override { if (m_nm) m_nm->Stop(); }

    bool IsServer() const override { return m_nm && m_nm->IsServer(); }
    bool IsClient() const override { return m_nm && m_nm->IsClient(); }
    bool IsConnected() const override { return m_nm && m_nm->IsConnected(); }
    std::uint32_t LocalId() const override { return m_nm ? m_nm->LocalId() : 0; }
    std::size_t PeerCount() const override { return m_nm ? m_nm->PeerCount() : 0; }

    void Send(const std::string& channel, const std::string& data) override {
        if (m_nm) m_nm->Send(channel, data);
    }
    void SendReliable(const std::string& channel, const std::string& data) override {
        if (m_nm) m_nm->SendReliable(channel, data);
    }
    void Rpc(const std::string& name, const std::string& data) override {
        if (m_nm) m_nm->Rpc(name, data);
    }
    void OnRpc(const std::string& name, std::function<void(std::uint32_t, const std::string&)> f) override {
        if (m_nm) m_nm->OnRpc(name, std::move(f));
    }
private:
    NetworkManager* m_nm;
};

// ---- Stub: a transport whose adapter (and SDK) hasn't been compiled in. ----
class StubNetTransport : public INetTransport {
public:
    explicit StubNetTransport(const char* name) : m_name(name) {}
    const char* BackendName() const override { return m_name; }
    bool Available() const override { return false; }
    bool StartServer(std::uint16_t) override { return false; }
    bool StartClient(const std::string&, std::uint16_t) override { return false; }
    bool HostViaRelay(const std::string&, std::uint16_t, const std::string&) override { return false; }
    bool JoinViaRelay(const std::string&, std::uint16_t, const std::string&) override { return false; }
    void Stop() override {}
    bool IsServer() const override { return false; }
    bool IsClient() const override { return false; }
    bool IsConnected() const override { return false; }
    std::uint32_t LocalId() const override { return 0; }
    std::size_t PeerCount() const override { return 0; }
    void Send(const std::string&, const std::string&) override {}
    void SendReliable(const std::string&, const std::string&) override {}
    void Rpc(const std::string&, const std::string&) override {}
    void OnRpc(const std::string&, std::function<void(std::uint32_t, const std::string&)>) override {}
private:
    const char* m_name;
};

} // namespace

std::unique_ptr<INetTransport> CreateNetTransport(NetTransportProvider provider, NetworkManager* native) {
    switch (provider) {
        case NetTransportProvider::Native: return std::make_unique<NativeNetTransport>(native);
        case NetTransportProvider::Photon: return std::make_unique<StubNetTransport>("Photon");
        case NetTransportProvider::Custom: return std::make_unique<StubNetTransport>("Custom");
    }
    return std::make_unique<NativeNetTransport>(native);
}

} // namespace okay
