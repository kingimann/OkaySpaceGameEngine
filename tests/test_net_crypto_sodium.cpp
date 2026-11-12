// tests/test_net_crypto.cpp compiled against libsodium, whatever the engine itself was
// configured with (the other half of test_net_crypto_own.cpp).
#undef OKAY_OWN_CRYPTO
#ifndef OKAY_HAVE_SODIUM
#define OKAY_HAVE_SODIUM
#endif
#ifndef OKAY_NET_CRYPTO
#define OKAY_NET_CRYPTO
#endif
#define SecureChannel SecureChannelSodium
#include "test_net_crypto.cpp"
