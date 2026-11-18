#pragma once
// Base64, PEM and private-key file helpers for our own test-key signing (docs/own_signing_plan.md, stage 3).
//
// NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. Written from RFC 4648 (base64) and RFC 7468 (PEM text format), in our own words.
// Key material rule (CLAUDE.md rule 15): nothing here prints or logs bytes of a key. The key file writer creates the file
// owner-only on POSIX (mode 0600, refuses to overwrite) and, on Windows, plain "create new file" (the file inherits the
// permissions of its folder: put it in the user's own profile; an explicit ACL is NOT set; unverified there).
#include <cstdint>
#include <string>
#include <vector>

#include "okay/Sign/Rsa.hpp"

namespace okay::sign {

std::string Base64Encode(const std::vector<std::uint8_t>& in);
/// Strict: only the base64 alphabet and correct '=' padding; whitespace (spaces, tabs, CR, LF) is skipped. False otherwise.
bool Base64Decode(const std::string& in, std::vector<std::uint8_t>& out);

/// "-----BEGIN <label>-----", base64 in 64-character lines, "-----END <label>-----", trailing newline.
std::string PemEncode(const std::string& label, const std::vector<std::uint8_t>& der);
/// Finds the first block with this label; false if there is none or its base64 is bad.
bool PemDecode(const std::string& text, const std::string& label, std::vector<std::uint8_t>& der);

/// Writes `data` to a NEW file (fails if the path exists). POSIX: mode 0600 from the first byte. Returns false on any error.
bool WriteNewPrivateFile(const std::string& path, const std::string& data);
/// Writes the key as PEM "RSA PRIVATE KEY" (PKCS #1) with WriteNewPrivateFile.
bool WriteRsaPrivateKeyPemFile(const std::string& path, const RsaPrivateKey& key);
/// Plain file write for PUBLIC data (certificates): creates or replaces the file.
bool WritePublicFile(const std::string& path, const std::vector<std::uint8_t>& data);

}  // namespace okay::sign
