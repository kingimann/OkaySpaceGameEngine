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
              << " — Ctrl+C to stop." << std::endl;   // flush: shows up in docker/journal logs

    // Sleep in the socket until a datagram arrives (forwarded with no added
    // latency), waking at least every 250 ms to expire silent peers.
    auto last = std::chrono::steady_clock::now();
    for (;;) {
        relay.Wait(250);
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        relay.Poll(dt, /*timeout=*/30.0f);
    }
    return 0;
}
