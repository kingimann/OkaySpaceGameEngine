#pragma once
// Our own PNG writer: CRC32, Adler32, a DEFLATE compressor (LZ77 with hash chains plus
// dynamic Huffman blocks, stored-block fallback) and the zlib and PNG containers.
// No third-party code. Output is a standard PNG any decoder opens.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {
namespace ImageWrite {

/// CRC-32 (IEEE, as used by PNG). Pass a previous result as `crc` to continue a running checksum.
std::uint32_t Crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0);
/// Adler-32 (zlib). Pass a previous result as `adler` to continue.
std::uint32_t Adler32(const std::uint8_t* data, std::size_t size, std::uint32_t adler = 1);
/// Raw DEFLATE stream (RFC 1951).
std::vector<std::uint8_t> Deflate(const std::uint8_t* data, std::size_t size);
/// zlib stream (RFC 1950): header + DEFLATE + Adler-32.
std::vector<std::uint8_t> ZlibCompress(const std::uint8_t* data, std::size_t size);

/// Encode a PNG. `channels` is 1 (grey), 2 (grey+alpha), 3 (RGB) or 4 (RGBA), 8 bits each,
/// rows tightly packed top to bottom. Returns an empty vector on invalid arguments.
std::vector<std::uint8_t> EncodePNG(int width, int height, int channels, const std::uint8_t* pixels);
/// Encode and write a PNG file. Returns false on invalid arguments or I/O failure.
bool WritePNG(const std::string& path, int width, int height, int channels, const std::uint8_t* pixels);

} // namespace ImageWrite
} // namespace okay
