# Project Wycheproof vectors (copied unmodified)

Files in this directory are copied, unmodified, from the public Project Wycheproof repository
(https://github.com/C2SP/wycheproof, `testvectors_v1/`, commit 65ca494727a52944c204441c860b039e6786b99b).
Licence: Apache-2.0 (the `LICENSE` file here is that repository's, verbatim). They are test data only; nothing in the
engine links or includes them.

| File | Used for |
|---|---|
| `x25519_test.json` | X25519, 518 vectors |
| `ed25519_test.json` | Ed25519 verification, 210 vectors |
| `chacha20_poly1305_test.json`, `xchacha20_poly1305_test.json` | the two AEADs, 325 + 315 vectors |
| `hmac_sha256_test.json`, `hmac_sha512_test.json`, `hkdf_sha256_test.json`, `hkdf_sha512_test.json` | our SHA-256/512 through HMAC and HKDF built in the test |
| `chacha20_counter_test.json` | ChaCha20 counter carry and exhaustion boundaries |

`tests/test_crypto_wycheproof.cpp` runs every vector in them (ctest `crypto_wycheproof`). To run against another copy, for example
a fresh clone: `OKAY_WYCHEPROOF_DIR=/path/to/wycheproof/testvectors_v1 build/tests/test_crypto_wycheproof` (the test skips and
passes if neither the variable's directory nor this one can be read).
