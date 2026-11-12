#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

/// Random-access, read-only bytes the incremental audio decoders pull from: a
/// file read in small pieces (so a long track is never loaded whole) or a block
/// of memory. Reads past the end return fewer bytes; nothing throws.
class AudioByteSource {
public:
    virtual ~AudioByteSource() = default;
    virtual std::uint64_t Size() const = 0;
    /// Copies up to `n` bytes starting at `offset` into `dst`; returns how many.
    virtual std::size_t Read(std::uint64_t offset, void* dst, std::size_t n) = 0;

    /// Non-owning view of `size` bytes at `data`: they must outlive the source.
    static std::shared_ptr<AudioByteSource> FromPointer(const unsigned char* data, std::size_t size);
    /// Shared memory block (kept alive by the source).
    static std::shared_ptr<AudioByteSource> FromMemory(std::shared_ptr<const std::vector<unsigned char>> bytes);
    /// A file on disk, read on demand. Null (with `*error` set) when it cannot be opened.
    static std::shared_ptr<AudioByteSource> FromFile(const std::string& path, std::string* error = nullptr);
};

} // namespace okay
