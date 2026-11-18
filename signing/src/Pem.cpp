// Base64, PEM, key files. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. See Pem.hpp. Written from RFC 4648 and RFC 7468.
#include "okay/Sign/Pem.hpp"

#include <cstdio>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace okay::sign {

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const std::vector<std::uint8_t>& in) {
    std::string out;
    std::size_t i = 0;
    for (; i + 3 <= in.size(); i += 3) {
        const std::uint32_t v = (static_cast<std::uint32_t>(in[i]) << 16) | (static_cast<std::uint32_t>(in[i + 1]) << 8) | in[i + 2];
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += kB64[v & 63];
    }
    if (i + 1 == in.size()) {
        const std::uint32_t v = static_cast<std::uint32_t>(in[i]) << 16;
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        const std::uint32_t v = (static_cast<std::uint32_t>(in[i]) << 16) | (static_cast<std::uint32_t>(in[i + 1]) << 8);
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += '=';
    }
    return out;
}

bool Base64Decode(const std::string& in, std::vector<std::uint8_t>& out) {
    out.clear();
    std::uint32_t acc = 0;
    int n = 0, pad = 0;
    for (char c : in) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=') { ++pad; v = 0; }
        else { out.clear(); return false; }
        if (pad > 0 && c != '=') { out.clear(); return false; }   // data after padding
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        if (++n == 4) {
            if (pad > 2) { out.clear(); return false; }
            out.push_back(static_cast<std::uint8_t>(acc >> 16));
            if (pad < 2) out.push_back(static_cast<std::uint8_t>(acc >> 8));
            if (pad < 1) out.push_back(static_cast<std::uint8_t>(acc));
            acc = 0; n = 0;
        }
    }
    if (n != 0) { out.clear(); return false; }
    return true;
}

std::string PemEncode(const std::string& label, const std::vector<std::uint8_t>& der) {
    const std::string b = Base64Encode(der);
    std::string out = "-----BEGIN " + label + "-----\n";
    for (std::size_t i = 0; i < b.size(); i += 64) out += b.substr(i, 64) + "\n";
    return out + "-----END " + label + "-----\n";
}

bool PemDecode(const std::string& text, const std::string& label, std::vector<std::uint8_t>& der) {
    der.clear();
    const std::string begin = "-----BEGIN " + label + "-----", end = "-----END " + label + "-----";
    const std::size_t a = text.find(begin);
    if (a == std::string::npos) return false;
    const std::size_t b = text.find(end, a);
    if (b == std::string::npos) return false;
    return Base64Decode(text.substr(a + begin.size(), b - a - begin.size()), der);
}

bool WriteNewPrivateFile(const std::string& path, const std::string& data) {
#if defined(_WIN32)
    if (FILE* t = std::fopen(path.c_str(), "rb")) { std::fclose(t); return false; }   // refuse to overwrite
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return (std::fclose(f) == 0) && ok;
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);   // owner read/write only, never replaces a file
    if (fd < 0) return false;
    std::size_t done = 0;
    bool ok = true;
    while (done < data.size()) {
        const ssize_t w = ::write(fd, data.data() + done, data.size() - done);
        if (w <= 0) { ok = false; break; }
        done += static_cast<std::size_t>(w);
    }
    if (::close(fd) != 0) ok = false;
    if (!ok) ::unlink(path.c_str());
    return ok;
#endif
}

bool WriteRsaPrivateKeyPemFile(const std::string& path, const RsaPrivateKey& key) {
    return WriteNewPrivateFile(path, PemEncode("RSA PRIVATE KEY", RsaPrivateKeyToDer(key)));
}

bool WritePublicFile(const std::string& path, const std::vector<std::uint8_t>& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return (std::fclose(f) == 0) && ok;
}

}  // namespace okay::sign
