#include "test_framework.hpp"
#include <okay/Net/SecureChannel.hpp>
#include <string>
#include <vector>

using namespace okay::net;

static std::vector<std::uint8_t> Bytes(const std::string& s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

int main() {
    RUN_SUITE("net_crypto");

    if (!SecureChannel::Available()) {
        // Built without libsodium: encryption is intentionally a no-op. Confirm the
        // stub reports unavailable and refuses to seal, then pass (nothing to test).
        SecureChannel s;
        std::vector<std::uint8_t> out;
        CHECK(!s.Ready());
        CHECK(!s.Seal((const std::uint8_t*)"x", 1, out));
        TEST_MAIN_RESULT();
    }

    // --- Key exchange: both ends derive matching directional session keys ------
    SecureChannel server, client;
    server.GenerateKeys();
    client.GenerateKeys();
    auto spk = server.PublicKey(), cpk = client.PublicKey();
    CHECK(spk.size() == SecureChannel::kPublicKeyBytes);
    CHECK(cpk.size() == SecureChannel::kPublicKeyBytes);
    CHECK(server.Establish(cpk, /*asServer=*/true));
    CHECK(client.Establish(spk, /*asServer=*/false));
    CHECK(server.Ready() && client.Ready());

    // --- Round-trip client -> server -------------------------------------------
    {
        auto plain = Bytes("hello server, this is an encrypted payload");
        std::vector<std::uint8_t> sealed, opened;
        CHECK(client.Seal(plain.data(), plain.size(), sealed));
        CHECK(sealed.size() > plain.size());          // nonce + tag overhead present
        CHECK(sealed != plain);                       // actually transformed (not plaintext)
        CHECK(server.Open(sealed.data(), sealed.size(), opened));
        CHECK(opened == plain);
    }

    // --- Round-trip server -> client -------------------------------------------
    {
        auto plain = Bytes("reply from the host");
        std::vector<std::uint8_t> sealed, opened;
        CHECK(server.Seal(plain.data(), plain.size(), sealed));
        CHECK(client.Open(sealed.data(), sealed.size(), opened));
        CHECK(opened == plain);
    }

    // --- Tampering is detected and rejected ------------------------------------
    {
        auto plain = Bytes("integrity matters");
        std::vector<std::uint8_t> sealed, opened;
        CHECK(client.Seal(plain.data(), plain.size(), sealed));
        sealed[sealed.size() - 1] ^= 0x01;            // flip a ciphertext/tag bit
        CHECK(!server.Open(sealed.data(), sealed.size(), opened));   // must fail
    }

    // --- The wrong peer's key can't open it (forgery resistance) ---------------
    {
        SecureChannel stranger; stranger.GenerateKeys();
        stranger.Establish(spk, /*asServer=*/false);  // not the real client
        auto plain = Bytes("secret");
        std::vector<std::uint8_t> sealed, opened;
        CHECK(server.Seal(plain.data(), plain.size(), sealed));
        CHECK(!stranger.Open(sealed.data(), sealed.size(), opened));
    }

    // --- Nonces differ per message (no reuse) ----------------------------------
    {
        auto plain = Bytes("same text twice");
        std::vector<std::uint8_t> a, b;
        CHECK(client.Seal(plain.data(), plain.size(), a));
        CHECK(client.Seal(plain.data(), plain.size(), b));
        CHECK(a != b);                                // random nonce => different ciphertext
    }

    TEST_MAIN_RESULT();
}
