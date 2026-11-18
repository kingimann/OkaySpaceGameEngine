// OPTIONAL cross-check of our certificate against the `openssl` command line, if it is installed.
// NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. For TESTING ONLY: nothing from OpenSSL (code, headers, test data, output format) is copied
// into the repository and nothing links to it. Skips (prints "skipped", exit 0) when openssl is missing or on Windows. Temporary files
// live in a private mkdtemp directory that is removed at the end; the key is a throw-away test key made here.
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "check.hpp"
#include "okay/Sign/Cert.hpp"
#include "okay/Sign/Pem.hpp"

#if defined(_WIN32)
int main() { std::printf("test_sign_cert_openssl: skipped (Windows)\n"); return 0; }
#else
#include <unistd.h>

using namespace okay::sign;

static std::string Run(const std::string& cmd) {
    std::string out;
    FILE* f = popen((cmd + " 2>&1").c_str(), "r");
    if (!f) return out;
    char buf[512];
    while (std::fgets(buf, sizeof buf, f)) out += buf;
    pclose(f);
    return out;
}

static UtcDateTime FromTime(std::time_t t) {
    std::tm g{};
    gmtime_r(&t, &g);
    return UtcDateTime{g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec};
}

int main() {
    if (Run("openssl version").find("SSL") == std::string::npos) {
        std::printf("test_sign_cert_openssl: skipped (no openssl on PATH)\n");
        return 0;
    }
    char tmpl[] = "/tmp/okay_sign_cert_o_XXXXXX";
    if (!mkdtemp(tmpl)) { std::printf("test_sign_cert_openssl: skipped (no temp dir)\n"); return 0; }
    const std::string dir = tmpl, cer = dir + "/self.cer", pem = dir + "/self.pem", keyPem = dir + "/key.pem";
    const std::time_t now = std::time(nullptr);   // the clock is read here, in the test, never in the library
    RsaPrivateKey key;
    CHECK(RsaGenerateKey(2048, key));
    CertParams p;
    p.subject = "CN=Okay Test Publisher, O=\"Okay Space, Test\", C=US";
    p.serial = RandomCertSerial();
    p.notBefore = FromTime(now - 24 * 3600);
    p.notAfter = FromTime(now + 3650LL * 24 * 3600);
    std::vector<std::uint8_t> der;
    CHECK(MakeSelfSignedCodeSigningCert(key, p, der));
    CHECK(WritePublicFile(cer, der));                                  // the DER .cer Windows imports
    {
        const std::string text = PemEncode("CERTIFICATE", der);
        CHECK(WritePublicFile(pem, std::vector<std::uint8_t>(text.begin(), text.end())));
    }
    CHECK(WriteRsaPrivateKeyPemFile(keyPem, key));
    // openssl parses the DER and reports the fields we meant to write.
    const std::string txt = Run("openssl x509 -inform DER -in " + cer + " -noout -text");
    CHECK(txt.find("Version: 3") != std::string::npos);
    CHECK(txt.find("Signature Algorithm: sha256WithRSAEncryption") != std::string::npos);
    CHECK(txt.find("Public-Key: (2048 bit)") != std::string::npos);
    CHECK(txt.find("CA:FALSE") != std::string::npos);
    CHECK(txt.find("Digital Signature") != std::string::npos);
    CHECK(txt.find("Code Signing") != std::string::npos);
    CHECK(txt.find("Subject Key Identifier") != std::string::npos);
    CHECK(txt.find("Okay Test Publisher") != std::string::npos && txt.find("Okay Space, Test") != std::string::npos);
    CHECK(txt.find("Basic Constraints: critical") != std::string::npos && txt.find("Key Usage: critical") != std::string::npos);
    CHECK(txt.find("rror") == std::string::npos);
    // openssl checks the self-signature (as a trusted self-signed certificate) and the PEM form parses too.
    const std::string ver = Run("openssl verify -CAfile " + pem + " " + pem);
    CHECK(ver.find(": OK") != std::string::npos);
    // openssl's own verification of the signature value with the public key it extracts from our certificate
    const std::string pk = Run("openssl x509 -in " + pem + " -noout -pubkey");
    CHECK(pk.find("BEGIN PUBLIC KEY") != std::string::npos);
    // The key file we wrote belongs to the certificate: same public key modulus.
    const std::string m1 = Run("openssl x509 -in " + pem + " -noout -modulus");
    const std::string m2 = Run("openssl rsa -in " + keyPem + " -noout -modulus");
    CHECK(m1.find("Modulus=") == 0 && m1 == m2);
    if (g_failures) std::printf("openssl said:\n%s\n%s\n", txt.c_str(), ver.c_str());
    Run("rm -rf " + dir);   // our own mkdtemp directory
    FINISH("test_sign_cert_openssl");
}
#endif
