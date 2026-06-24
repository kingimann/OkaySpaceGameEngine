// OkaySpace reference relay — a tiny TURN-style UDP forwarder that lets players
// behind NATs reach each other without port-forwarding. Run it on a host with a
// public IP; point games at it with NetworkManager::HostViaRelay / JoinViaRelay
// (or the net_host_relay / net_join_relay script builtins) using a shared code.
//
//   okayspace-relay [port]        (default 45100)
//
// It forwards datagrams only — it never inspects or decrypts game traffic, so
// end-to-end encryption still holds through it.
#include "okay/Net/RelayServer.hpp"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <iostream>

int main(int argc, char** argv) {
    std::uint16_t port = 45100;
    if (argc > 1) port = static_cast<std::uint16_t>(std::atoi(argv[1]));

    okay::net::RelayServer relay;
    if (!relay.Start(port)) {
        std::cerr << "relay: failed to start on port " << port << "\n";
        return 1;
    }
    std::cout << "OkaySpace relay running on UDP " << relay.Port()
              << " — Ctrl+C to stop.\n";

    // Poll at ~120 Hz: cheap, and snappy enough to forward without adding latency.
    const auto tick = std::chrono::milliseconds(8);
    float dt = 0.008f;
    for (;;) {
        relay.Poll(dt, /*timeout=*/30.0f);
        std::this_thread::sleep_for(tick);
    }
    return 0;
}
