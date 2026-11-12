// tests/test_net_crypto.cpp compiled against OUR crypto backend (okay::crypto, unreviewed),
// whatever the engine itself was configured with. The class is renamed so this copy of the
// header-only SecureChannel never shares symbols with the engine's.
#ifndef OKAY_OWN_CRYPTO
#define OKAY_OWN_CRYPTO
#endif
#ifndef OKAY_NET_CRYPTO
#define OKAY_NET_CRYPTO
#endif
#define SecureChannel SecureChannelOwnCrypto
#include "test_net_crypto.cpp"
