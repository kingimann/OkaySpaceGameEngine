---
id: own-signing-1
group: switches
---
## own-signing-1. Own test-key signing, stages 1 to 4 (big integers, Miller-Rabin, DER, RSA keys and signatures, self-signed test certificate, CMS SignedData): CANNOT BE TRIED IN THE WINDOWS RELEASE YET

**Not reachable from the release.** These are only the first building blocks (stage 2 adds RSA key generation and RSASSA-PKCS1-v1_5 SHA-256 sign and verify; stage 3 adds a self-signed code-signing test certificate writer and checker, PEM, and key files; stage 4 adds a generic CMS / PKCS #7 SignedData writer and verifier) of our own signing for Xbox Developer Mode test
packages (`docs/own_signing_plan.md`). It is a small library with tests. Nothing in the launcher, editor or player uses it,
no package is signed yet, and releases refuse the switch. It becomes something you can try at stage 5 (the signed package
itself) and stage 6 (checking it on your PC and Xbox). **NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY.**

What you can run today, on a Linux machine with a compiler (this is how it was checked):

```
cmake -S . -B build-sign -G Ninja -DOKAY_OWN_SIGNING=ON
ninja -C build-sign test_sign_bigint test_sign_prime test_sign_der test_sign_openssl test_sign_rsa test_sign_rsa_openssl test_sign_cert test_sign_cert_openssl test_sign_cms test_sign_cms_openssl
ctest --test-dir build-sign -R "^sign_(bigint|prime|der|openssl_crosscheck|rsa|rsa_openssl_crosscheck|cert|cert_openssl_crosscheck|cms|cms_openssl_crosscheck)$"
```

You should see ten tests pass; `./build-sign/signing/test_sign_rsa` also prints the 2048-bit key generation, sign and verify times. The switch is `OKAY_OWN_SIGNING` (CMake, default OFF; it prints a warning when ON).
`scripts/check_release_own_stack.sh` refuses it in a release (environment variable or `-D` argument). The last test also
asks the `openssl` command line, if installed, to parse our DER output and to judge primes; without `openssl` it prints
"skipped" and passes. Nothing from OpenSSL is in the repository.
