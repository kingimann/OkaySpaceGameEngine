// Benchmark for okay::crypto (not a test; run by hand: build/tests/bench_crypto [seconds-per-case]).
// Prints microseconds per operation for our code and, when libsodium is installed
// (OKAY_BENCH_SODIUM), for libsodium next to it. UNREVIEWED code; see docs/crypto.md.
#include <okay/Crypto/Crypto.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>
#ifdef OKAY_BENCH_SODIUM
#include <sodium.h>
#endif

using Clock = std::chrono::steady_clock;

static double Bench(const std::function<void()>& f, double seconds) {
    for (int i = 0; i < 3; ++i) f();   // warm up (also builds the lazily-initialised tables)
    long n = 0;
    const auto t0 = Clock::now();
    double el = 0;
    do {
        for (int i = 0; i < 8; ++i) f();
        n += 8;
        el = std::chrono::duration<double>(Clock::now() - t0).count();
    } while (el < seconds);
    return el / double(n) * 1e6;
}

int main(int argc, char** argv) {
    const double secs = argc > 1 ? std::atof(argv[1]) : 0.5;
#ifdef OKAY_BENCH_SODIUM
    sodium_init();
#endif
    using namespace okay::crypto;
    std::uint8_t sk[32], pk[32], out[32], seed[32], esk[64], epk[32], sig[64];
    std::uint8_t msg[64], key[32], nonce[24], hash[64];
    for (int i = 0; i < 32; ++i) { sk[i] = (std::uint8_t)(i * 7 + 1); seed[i] = (std::uint8_t)(i * 13 + 5); key[i] = (std::uint8_t)i; }
    for (int i = 0; i < 64; ++i) msg[i] = (std::uint8_t)i;
    for (int i = 0; i < 24; ++i) nonce[i] = (std::uint8_t)(i + 3);
    X25519Base(pk, sk);
    Ed25519SeedKeypair(epk, esk, seed);
    Ed25519Sign(sig, msg, 64, esk);
    std::vector<std::uint8_t> buf(1024 + 16, 0x5a), buf2(1024 + 16);

    std::printf("%-34s %12s %12s\n", "operation (microseconds/op)", "okay::crypto", "libsodium");
    auto row = [&](const char* name, const std::function<void()>& ours, const std::function<void()>& theirs) {
        const double a = Bench(ours, secs);
        if (theirs) std::printf("%-34s %12.2f %12.2f\n", name, a, Bench(theirs, secs));
        else std::printf("%-34s %12.2f %12s\n", name, a, "-");
    };
    std::function<void()> none;
#ifdef OKAY_BENCH_SODIUM
#define THEIRS(...) std::function<void()>([&] { __VA_ARGS__; })
#else
#define THEIRS(...) none
#endif
    row("X25519 (variable point)", [&] { X25519(out, sk, pk); },
        THEIRS(crypto_scalarmult(out, sk, pk)));
    row("X25519 base (public key)", [&] { X25519Base(out, sk); },
        THEIRS(crypto_scalarmult_base(out, sk)));
    row("Ed25519 public key from seed", [&] { Ed25519PublicFromSeed(epk, seed); },
        THEIRS(unsigned char s2[64]; unsigned char p2[32]; crypto_sign_seed_keypair(p2, s2, seed)));
    row("Ed25519 sign (64-byte message)", [&] { Ed25519Sign(sig, msg, 64, esk); },
        THEIRS(crypto_sign_detached(sig, nullptr, msg, 64, esk)));
    row("Ed25519 verify", [&] { volatile bool b = Ed25519Verify(sig, msg, 64, epk); (void)b; },
        THEIRS(volatile int b = crypto_sign_verify_detached(sig, msg, 64, epk); (void)b));
    row("Kx client session keys", [&] { std::uint8_t rx[32], tx[32]; KxClientSessionKeys(rx, tx, pk, sk, pk); },
        THEIRS(unsigned char rx[32], tx[32]; crypto_kx_client_session_keys(rx, tx, pk, sk, pk)));
    row("XChaCha20-Poly1305 seal 1 KiB", [&] { XChaCha20Poly1305Seal(buf2.data(), buf.data(), 1024, nullptr, 0, nonce, key); },
        THEIRS(unsigned long long l; crypto_aead_xchacha20poly1305_ietf_encrypt(buf2.data(), &l, buf.data(), 1024, nullptr, 0, nullptr, nonce, key)));
    row("Blake2b-512 of 1 KiB", [&] { Blake2bHash(hash, 64, buf.data(), 1024); },
        THEIRS(crypto_generichash(hash, 64, buf.data(), 1024, nullptr, 0)));
    row("SHA-512 of 1 KiB", [&] { Sha512Hash(hash, buf.data(), 1024); },
        THEIRS(crypto_hash_sha512(hash, buf.data(), 1024)));
    row("SHA-256 of 1 KiB", [&] { Sha256Hash(hash, buf.data(), 1024); },
        THEIRS(crypto_hash_sha256(hash, buf.data(), 1024)));
    return 0;
}
