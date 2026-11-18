// OPTIONAL cross-check of our DER writer and Miller-Rabin against the `openssl` command line, if it is installed.
// NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. For TESTING ONLY: nothing from OpenSSL (code, headers, test data, output
// format) is copied into the repository, and nothing links to it; we only read what it prints. If openssl is missing,
// or on Windows, the test prints "skipped" and exits 0.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.hpp"
#include "okay/Sign/Der.hpp"
#include "okay/Sign/Prime.hpp"

#if defined(_WIN32)
int main() { std::printf("test_sign_openssl: skipped (Windows)\n"); return 0; }
#else
#include <unistd.h>

static std::string Run(const std::string& cmd) {
    std::string out;
    FILE* f = popen((cmd + " 2>&1").c_str(), "r");
    if (!f) return out;
    char buf[512];
    while (std::fgets(buf, sizeof buf, f)) out += buf;
    pclose(f);
    return out;
}

int main() {
    if (Run("openssl version").find("OpenSSL") == std::string::npos && Run("openssl version").find("LibreSSL") == std::string::npos) {
        std::printf("test_sign_openssl: skipped (no openssl on PATH)\n");
        return 0;
    }
    using namespace okay::sign;
    using namespace okay::sign::der;
    // 1. DER: build a structure with our writer, let openssl parse it, look for the pieces.
    BigInt big; BigInt::FromHex("00c0ffee00112233445566778899aabbccddeeff", big);
    const Bytes doc = Sequence({
        IntegerU64(65537), Integer(big),
        Sequence({Oid("1.2.840.113549.1.1.11"), Null()}),
        Oid("1.3.6.1.5.5.7.3.3"), Oid("2.16.840.1.101.3.4.2.1"), Oid("1.3.6.1.4.1.311.2.1.4"),
        Boolean(true), Utf8String("Okay Test"), PrintableString("US"), UtcTime(2026, 11, 18, 9, 5, 7),
        BitString(Bytes{0xA0}, 5), OctetString(Bytes{1, 2, 3}),
        ContextExplicit(0, IntegerU64(2)), SetOf({IntegerU64(5), IntegerU64(1)}), Null()});
    char path[] = "/tmp/okay_sign_der_XXXXXX";
    const int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd >= 0) {
        const ssize_t w = write(fd, doc.data(), doc.size());
        close(fd);
        CHECK(w == static_cast<ssize_t>(doc.size()));
        const std::string out = Run(std::string("openssl asn1parse -inform DER -in ") + path);
        std::remove(path);
        CHECK(out.find("sha256WithRSAEncryption") != std::string::npos);
        CHECK(out.find("Code Signing") != std::string::npos);
        CHECK(out.find("sha256") != std::string::npos);
        CHECK(out.find("Okay Test") != std::string::npos);
        CHECK(out.find("261118090507Z") != std::string::npos);
        CHECK(out.find("C0FFEE00112233445566778899AABBCCDDEEFF") != std::string::npos || out.find("c0ffee") != std::string::npos);
        CHECK(out.find("BOOLEAN") != std::string::npos);
        CHECK(out.find("Error") == std::string::npos && out.find("error") == std::string::npos);
        if (g_failures) std::printf("openssl said:\n%s\n", out.c_str());
    }
    // 2. Primality: for a few numbers our answer must match `openssl prime`.
    const char* tests[] = {"1000003", "1000001", "7fffffffffffffffffffffffffffffff", "7ffffffffffffffffffffffffffffffd",
                           "c90fdaa22168c234c4c6628b80dc1cd129024e088a67cc74020bbea63b139b22514a08798e3404dd",
                           "bc1b0e0f0e0d0b0a0908070605040302010", "ffffffffffffffffc90fdaa22168c234c4c6628b80dc1cd129024e088a67cc74"};
    for (const char* hx : tests) {
        BigInt n; BigInt::FromHex(hx, n);
        const std::string o = Run(std::string("openssl prime -hex ") + hx);
        const bool theirs = o.find("is not prime") == std::string::npos && o.find("is prime") != std::string::npos;
        const bool unknown = o.find("is prime") == std::string::npos && o.find("is not prime") == std::string::npos;
        CHECK(!unknown);
        CHECK(okay::sign::IsProbablePrime(n) == theirs);
    }
    FINISH("test_sign_openssl");
}
#endif
