// OPTIONAL cross-check of our RSA keys and signatures against the `openssl` command line, if it is installed.
// NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. For TESTING ONLY: nothing from OpenSSL (code, headers, test data, output format)
// is copied into the repository and nothing links to it; we only run the program on files we write and read what it prints.
// Skips (prints "skipped", exit 0) when openssl is missing or on Windows. Temporary files live in a private mkdtemp directory that is
// removed at the end (the private key is a throw-away test key).
#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.hpp"
#include "okay/Sign/Rsa.hpp"

#if defined(_WIN32)
int main() { std::printf("test_sign_rsa_openssl: skipped (Windows)\n"); return 0; }
#else
#include <unistd.h>

using namespace okay::sign;
using Bytes = std::vector<std::uint8_t>;

static std::string Run(const std::string& cmd) {
    std::string out;
    FILE* f = popen((cmd + " 2>&1").c_str(), "r");
    if (!f) return out;
    char buf[512];
    while (std::fgets(buf, sizeof buf, f)) out += buf;
    pclose(f);
    return out;
}
static bool Write(const std::string& path, const Bytes& b) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    std::fclose(f);
    return ok;
}
static Bytes Read(const std::string& path) {
    Bytes b;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return b;
    int c;
    while ((c = std::fgetc(f)) != EOF) b.push_back(static_cast<std::uint8_t>(c));
    std::fclose(f);
    return b;
}

int main() {
    if (Run("openssl version").find("SSL") == std::string::npos) {
        std::printf("test_sign_rsa_openssl: skipped (no openssl on PATH)\n");
        return 0;
    }
    char dirTemplate[] = "/tmp/okay_sign_rsa_XXXXXX";
    if (!mkdtemp(dirTemplate)) { std::printf("test_sign_rsa_openssl: skipped (no temp dir)\n"); return 0; }
    const std::string dir = dirTemplate;
    const std::string keyDer = dir + "/key.der", keyPem = dir + "/key.pem", pubPem = dir + "/pub.pem", msgF = dir + "/msg",
                      sigF = dir + "/sig", osslSig = dir + "/ossl.sig";
    const Bytes msg = {'O', 'k', 'a', 'y', ' ', 'S', 'p', 'a', 'c', 'e', ' ', 't', 'e', 's', 't'};
    CHECK(Write(msgF, msg));

    for (std::size_t bits : {std::size_t(1024), std::size_t(2048)}) {
        RsaPrivateKey key;
        CHECK(RsaGenerateKey(bits, key));
        // 1. Our exported private key is a valid RSA key according to openssl.
        CHECK(Write(keyDer, RsaPrivateKeyToDer(key)));
        const std::string chk = Run("openssl rsa -inform DER -in " + keyDer + " -check -noout");
        CHECK(chk.find("RSA key ok") != std::string::npos);
        const std::string txt = Run("openssl rsa -inform DER -in " + keyDer + " -noout -text");
        CHECK(txt.find("Private-Key: (" + std::to_string(bits) + " bit") != std::string::npos);
        // 2. openssl verifies OUR signature (public key derived by openssl from our exported key).
        Bytes sig;
        CHECK(RsaSignSha256(key, msg.data(), msg.size(), sig));
        CHECK(Write(sigF, sig));
        Run("openssl rsa -inform DER -in " + keyDer + " -pubout -out " + pubPem);
        const std::string v = Run("openssl dgst -sha256 -verify " + pubPem + " -signature " + sigF + " " + msgF);
        CHECK(v.find("Verified OK") != std::string::npos);
        // and refuses it for another message
        Bytes other = msg; other[0] ^= 1;
        CHECK(Write(dir + "/other", other));
        CHECK(Run("openssl dgst -sha256 -verify " + pubPem + " -signature " + sigF + " " + dir + "/other").find("Verified OK") == std::string::npos);
        // 3. We verify openssl's signature made with OUR key (PEM written by openssl from our DER).
        Run("openssl rsa -inform DER -in " + keyDer + " -out " + keyPem);
        Run("openssl dgst -sha256 -sign " + keyPem + " -out " + osslSig + " " + msgF);
        const Bytes theirs = Read(osslSig);
        CHECK(theirs.size() == bits / 8);
        CHECK(RsaVerifySha256(key.Public(), msg.data(), msg.size(), theirs.data(), theirs.size()));
        CHECK(theirs == sig);   // deterministic scheme: the two signatures are the same bytes
        // 4. Public key DER round trip through openssl's parser.
        CHECK(Write(dir + "/pub.der", RsaPublicKeyToDer(key.Public())));
        CHECK(Run("openssl rsa -RSAPublicKey_in -inform DER -in " + dir + "/pub.der -noout -text").find("Public-Key: (" + std::to_string(bits)) != std::string::npos);
    }
    Run("rm -rf " + dir);   // our own mkdtemp directory, made above
    FINISH("test_sign_rsa_openssl");
}
#endif
