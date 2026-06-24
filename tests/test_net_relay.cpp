#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/RelayServer.hpp"
#include <chrono>
#include <string>
#include <thread>

using namespace okay;

// A host and a client reach each other entirely through an in-process relay —
// neither one binds a public listening port (the NAT-traversal path). The relay,
// the host and the client are all pumped in the same loop. We assert the client
// completes the join handshake *through* the relay and that an application
// message is forwarded host-ward, proving the slot-routing is end to end.
int main() {
    RUN_SUITE("net_relay");

    okay::net::RelayServer relay;
    CHECK(relay.Start(0));                 // ephemeral port
    std::uint16_t port = relay.Port();
    CHECK(port != 0);

    Scene host("Host"); host.physicsEnabled = false;
    NetworkManager* hn = host.CreateGameObject("Net")->AddComponent<NetworkManager>();
    hn->serverName = "RelayHost";
    CHECK(hn->HostViaRelay("127.0.0.1", port, "ROOM-42"));
    CHECK(hn->Relayed());

    Scene client("Client"); client.physicsEnabled = false;
    NetworkManager* cn = client.CreateGameObject("Net")->AddComponent<NetworkManager>();
    cn->SetLocalName("Visitor");
    CHECK(cn->JoinViaRelay("127.0.0.1", port, "ROOM-42"));
    CHECK(cn->Relayed());

    host.Start();
    client.Start();

    bool sent = false, delivered = false;
    for (int i = 0; i < 800 && !delivered; ++i) {
        relay.Poll(0.02f);
        host.Update(0.02f);
        client.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (cn->Joined() && !sent) { cn->Send("ping", "hello"); sent = true; }
        NetworkManager::NetMessage m;
        while (hn->PopMessage(m))
            if (m.channel == "ping" && m.data == "hello") delivered = true;
    }

    // The relay paired both peers (a session with two slots).
    CHECK(relay.SessionCount() >= 1);
    CHECK(relay.PeerCount() >= 2);
    // The client joined through the relay and the host sees it.
    CHECK(cn->Joined());
    CHECK(cn->RelayReady());
    CHECK(hn->PeerCount() >= 1);
    // And a game message rode the relay all the way to the host.
    CHECK(delivered);

    hn->Stop();
    cn->Stop();
    relay.Stop();
    TEST_MAIN_RESULT();
}
