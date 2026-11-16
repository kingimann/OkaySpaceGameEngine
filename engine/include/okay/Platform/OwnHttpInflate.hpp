#pragma once
// ---------------------------------------------------------------------------
// okay::ownhttp inflate: gzip and zlib bodies for the HTTP client.
//
// This file used to hold its own DEFLATE decoder. There is now ONE decoder for the whole engine, in
// okay/Core/Inflate.hpp (written from RFC 1950, 1951 and 1952 only; see docs/provenance_audit.md). This header
// is the thin string-based wrapper the HTTP client calls, with the strict options: Adler-32 / CRC-32 and size
// checked, extra bytes refused, incomplete codes refused, output capped at `maxOut`.
// Memory: the working buffer never grows past `maxOut`, but a successful decode copies it into the caller's
// string, and the caller holds the encoded body too (see Limits::maxEncodedBytes). So the peak is about three
// times the cap, not one.
// ---------------------------------------------------------------------------
#include "okay/Core/Inflate.hpp"

#include <string>
#include <vector>

namespace okay::ownhttp {

// Kept under the old name: the HTTP tests (and OwnHttp.hpp) use Crc32/Adler32 and the decoder pieces from here.
namespace inflate_detail = ::okay::inflate::detail;

/// Decode a gzip member (`gzip` true) or a zlib stream (`gzip` false). false with `why` set on any problem.
inline bool InflateBody(const std::string& in, bool gzip, size_t maxOut, std::string& out, std::string& why) {
    std::string().swap(out);
    ::okay::inflate::Options op;   // strict defaults
    op.maxOutput = maxOut;
    std::vector<uint8_t> buf;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(in.data());
    const bool ok = gzip ? ::okay::inflate::Gzip(p, in.size(), buf, op, why) : ::okay::inflate::Zlib(p, in.size(), buf, op, why);
    if (!ok) { std::string().swap(out); return false; }   // give the memory back, as before
    out.assign(reinterpret_cast<const char*>(buf.data()), buf.size());
    return true;
}

} // namespace okay::ownhttp
