#include "test_framework.hpp"
#include <Okay.hpp>
#include <chrono>
#include <thread>

using namespace okay;

// Two scripted scenes (a host and a client) talk over the net_* OkayScript
// builtins: start the session, send a message, and poll it on the other side.
int main() {
    RUN_SUITE("script_net");

    const char* hostSrc =
        "var got = \"\";\n"
        "function start() { net_name(\"Host\"); net_host(45055); }\n"
        "function update(d) {\n"
        "  while (net_poll()) {\n"
        "    if (net_msg_channel() == \"ping\") { got = net_msg_data(); }\n"
        "  }\n"
        "}\n";
    const char* clientSrc =
        "var sent = 0;\n"
        "function start() { net_name(\"Client\"); net_join(\"127.0.0.1\", 45055); }\n"
        "function update(d) {\n"
        "  if (net_is_client() == 1 && net_id() > 0 && sent == 0) {\n"
        "    net_send(\"ping\", \"hello\"); sent = 1;\n"
        "  }\n"
        "}\n";

    Scene host("Host"); host.physicsEnabled = false;
    GameObject* hgo = host.CreateGameObject("HostLogic");
    hgo->AddComponent<ScriptComponent>("okayscript")->LoadSource(hostSrc);

    Scene client("Client"); client.physicsEnabled = false;
    GameObject* cgo = client.CreateGameObject("ClientLogic");
    cgo->AddComponent<ScriptComponent>("okayscript")->LoadSource(clientSrc);

    host.Start();
    client.Start();

    // The host's NetworkManager exists and is a server.
    NetworkManager* hn = host.FindObjectOfType<NetworkManager>();
    CHECK(hn != nullptr);
    CHECK(hn->IsServer());

    bool delivered = false;
    for (int i = 0; i < 300; ++i) {
        host.Update(0.02f);
        client.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        auto* sc = hgo->GetComponent<ScriptComponent>();
        if (sc && sc->VM()) {
            auto g = sc->VM()->GetGlobal("got");
            if (g.IsString() && g.AsString() == "hello") { delivered = true; break; }
        }
    }
    CHECK(delivered);

    // The client joined and got a non-zero id; the host sees one peer.
    NetworkManager* cn = client.FindObjectOfType<NetworkManager>();
    CHECK(cn != nullptr);
    CHECK(cn->IsClient());
    CHECK(cn->Joined());
    CHECK(hn->PeerCount() >= 1);

    hn->Stop();
    cn->Stop();
    TEST_MAIN_RESULT();
}
