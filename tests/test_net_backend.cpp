#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// The provider seams: INetBackend (online services) and INetTransport (realtime),
// so PlayFab / Photon can be dropped in later without touching game code.
int main() {
    RUN_SUITE("net_backend");

    // ---- INetBackend: Native is the default and forwards to Account/Matchmaking ----
    {
        auto native = CreateNetBackend(NetBackendProvider::Native);
        CHECK(native != nullptr);
        CHECK(std::string(native->BackendName()) == "Native");
        // Offline in the test env: services degrade gracefully, never crash.
        CHECK(native->IsLoggedIn() == false);
        CHECK(native->CloudSave("k", "v") == false || native->IsOnline());
        CHECK(native->ListSessions().empty() || native->IsOnline());
    }

    // ---- A not-yet-built provider returns a clear, safe stub ----
    {
        auto pf = CreateNetBackend(NetBackendProvider::PlayFab);
        CHECK(std::string(pf->BackendName()) == "PlayFab");
        CHECK(pf->IsOnline() == false);
        CHECK(pf->Login("u", "p") == false);
        CHECK(!pf->LastError().empty());                 // explains how to enable it
        CHECK(pf->ListSessions().empty());
    }

    // ---- Facade: default Native, switchable, lazy-rebuilt ----
    {
        NetBackend::Shutdown();
        CHECK(NetBackend::Provider() == NetBackendProvider::Native);
        CHECK(std::string(NetBackend::Get().BackendName()) == "Native");
        CHECK(NetBackend::Exists());

        NetBackend::Use(NetBackendProvider::PlayFab);
        CHECK(NetBackend::Provider() == NetBackendProvider::PlayFab);
        CHECK(std::string(NetBackend::Get().BackendName()) == "PlayFab");
        CHECK(NetBackend::Get().IsOnline() == false);

        NetBackend::Use(NetBackendProvider::Native);     // restore for other tests
        CHECK(std::string(NetBackend::Get().BackendName()) == "Native");
    }

    // ---- INetTransport: Native wraps a NetworkManager and forwards to it ----
    {
        Scene s("T");
        auto* nm = s.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto tr = CreateNetTransport(NetTransportProvider::Native, nm);
        CHECK(std::string(tr->BackendName()) == "Native");
        CHECK(tr->Available());
        CHECK(tr->StartServer(0));                       // ephemeral port
        s.Start();
        CHECK(tr->IsServer());
        CHECK(tr->IsConnected());
        tr->Stop();
    }

    // ---- A null-bound or not-built transport is a safe no-op ----
    {
        auto none = CreateNetTransport(NetTransportProvider::Native, nullptr);
        CHECK(none->Available() == false);
        CHECK(none->StartServer(0) == false);            // nothing to drive

        auto photon = CreateNetTransport(NetTransportProvider::Photon);
        CHECK(std::string(photon->BackendName()) == "Photon");
        CHECK(photon->Available() == false);
        CHECK(photon->StartClient("127.0.0.1", 5000) == false);
        CHECK(photon->PeerCount() == 0);
    }

    TEST_MAIN_RESULT();
}
