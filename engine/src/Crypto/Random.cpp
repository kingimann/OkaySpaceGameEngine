// okay::crypto::RandomBytes: the operating system's CSPRNG, nothing of our own.
// UNREVIEWED (see Crypto.hpp).
#include <okay/Crypto/Crypto.hpp>

#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <windows.h>
  #include <bcrypt.h>
#elif defined(__linux__)
  #include <errno.h>
  #include <sys/random.h>
#elif defined(__APPLE__)
  #include <sys/random.h>   // getentropy lives here on macOS (not verified on a Mac; the CI macOS job builds it)
  #include <unistd.h>
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__EMSCRIPTEN__)
  #include <unistd.h>
#else
  #include <cstdio>
#endif

namespace okay::crypto {

static void Die(const char* why) {
    std::fprintf(stderr, "okay::crypto: no OS randomness (%s); aborting\n", why);
    std::abort();
}

void RandomBytes(void* out, std::size_t n) {
    std::uint8_t* p = static_cast<std::uint8_t*>(out);
#if defined(_WIN32)
    while (n > 0) {
        const ULONG chunk = n > 0x40000000u ? 0x40000000u : static_cast<ULONG>(n);
        if (BCryptGenRandom(nullptr, p, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) Die("BCryptGenRandom");
        p += chunk; n -= chunk;
    }
#elif defined(__linux__)
    while (n > 0) {
        const ssize_t r = getrandom(p, n > 256 ? 256 : n, 0);
        if (r < 0) { if (errno == EINTR) continue; Die("getrandom"); }
        p += r; n -= static_cast<std::size_t>(r);
    }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__EMSCRIPTEN__)
    while (n > 0) {
        const std::size_t chunk = n > 256 ? 256 : n;      // getentropy's limit
        if (getentropy(p, chunk) != 0) Die("getentropy");
        p += chunk; n -= chunk;
    }
#else
    std::FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) Die("/dev/urandom");
    if (std::fread(p, 1, n, f) != n) Die("/dev/urandom read");
    std::fclose(f);
#endif
}

} // namespace okay::crypto
