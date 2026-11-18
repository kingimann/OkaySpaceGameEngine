// OPTIONAL cross-check of our CMS SignedData against the `openssl` command line, if it is installed.
// NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. For TESTING ONLY: nothing from OpenSSL (code, headers, test data, output format) is copied into
// the repository and nothing links to it. Skips (prints "skipped", exit 0) when openssl is missing or on Windows. Temporary files live in a
// private mkdtemp directory that is removed at the end; the key is a throw-away test key made here.
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "check.hpp"
#include "okay/Sign/Cms.hpp"
#include "okay/Sign/Pem.hpp"

#if defined(_WIN32)
int main() { std::printf("test_sign_cms_openssl: skipped (Windows)\n"); return 0; }
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
static Bytes Read(const std::string& path) {
    Bytes b;
    if (FILE* f = std::fopen(path.c_str(), "rb")) { int c; while ((c = std::fgetc(f)) != EOF) b.push_back(static_cast<std::uint8_t>(c)); std::fclose(f); }
    return b;
}
static UtcDateTime FromTime(std::time_t t) {
    std::tm g{};
    gmtime_r(&t, &g);
    return UtcDateTime{g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec};
}

int main() {
    if (Run("openssl version").find("SSL") == std::string::npos) { std::printf("test_sign_cms_openssl: skipped (no openssl on PATH)\n"); return 0; }
    char tmpl[] = "/tmp/okay_sign_cms_o_XXXXXX";
    if (!mkdtemp(tmpl)) { std::printf("test_sign_cms_openssl: skipped (no temp dir)\n"); return 0; }
    const std::string dir = tmpl, certPem = dir + "/cert.pem", keyPem = dir + "/key.pem", content = dir + "/content", msgF = dir + "/msg.der",
                      outF = dir + "/out", ossl = dir + "/ossl.der";
    const std::time_t now = std::time(nullptr);   // the clock is read here, in the test, never in the library
    RsaPrivateKey key;
    CHECK(RsaGenerateKey(2048, key));
    CertParams cp;
    cp.subject = "CN=CMS Cross Check, O=Okay Space, C=US";
    cp.serial = RandomCertSerial();
    cp.notBefore = FromTime(now - 24 * 3600);
    cp.notAfter = FromTime(now + 3650LL * 24 * 3600);
    Bytes cert;
    CHECK(MakeSelfSignedCodeSigningCert(key, cp, cert));
    { const std::string t = PemEncode("CERTIFICATE", cert); CHECK(WritePublicFile(certPem, Bytes(t.begin(), t.end()))); }
    CHECK(WriteRsaPrivateKeyPemFile(keyPem, key));
    const Bytes data = {'O', 'k', 'a', 'y', ' ', 'c', 'm', 's', ' ', 'c', 'r', 'o', 's', 's', ' ', 'c', 'h', 'e', 'c', 'k', '\n'};
    CHECK(WritePublicFile(content, data));

    for (int detached = 0; detached < 2; ++detached) {
        CmsSignParams p;
        p.signingTime = FromTime(now);
        p.encapsulate = detached == 0;
        Bytes msg;
        CHECK(CmsSignSha256(key, cert, data, p, msg));
        CHECK(WritePublicFile(msgF, msg));
        // openssl structure view and signature verification (the certificate is not chain-checked: -noverify; our own checker did that)
        const std::string tree = Run("openssl asn1parse -inform DER -in " + msgF);
        CHECK(tree.find("pkcs7-signedData") != std::string::npos && tree.find("messageDigest") != std::string::npos && tree.find("signingTime") != std::string::npos);
        const std::string v = Run("openssl cms -verify -inform DER -in " + msgF + " -noverify -binary -out " + outF + (detached ? " -content " + content : std::string()));
        CHECK(v.find("Verification successful") != std::string::npos);
        CHECK(Read(outF) == data);
        // a changed detached content is refused by openssl too
        if (detached) {
            Bytes other = data; other[0] ^= 1;
            CHECK(WritePublicFile(dir + "/other", other));
            CHECK(Run("openssl cms -verify -inform DER -in " + msgF + " -noverify -binary -out " + outF + " -content " + dir + "/other").find("Verification successful") == std::string::npos);
        }
        const std::string txt = Run("openssl cms -cmsout -print -inform DER -in " + msgF);
        CHECK(txt.find("sha256") != std::string::npos && txt.find("CMS_ContentInfo") != std::string::npos);
    }

    // The other direction: a message signed by openssl with our key and certificate, checked by OUR verifier.
    const std::string sign = Run("openssl cms -sign -in " + content + " -signer " + certPem + " -inkey " + keyPem + " -md sha256 -binary -nodetach -outform DER -out " + ossl);
    const Bytes theirs = Read(ossl);
    CHECK(!theirs.empty());
    CmsInfo info;
    const std::string why = CmsVerifySelfSigned(theirs, nullptr, FromTime(now), &info);
    CHECK(why.empty());
    if (!why.empty()) std::printf("our verifier on openssl's message: %s (openssl said: %s)\n", why.c_str(), sign.c_str());
    CHECK(info.content == data);
    Run("rm -rf " + dir);   // our own mkdtemp directory
    FINISH("test_sign_cms_openssl");
}
#endif
