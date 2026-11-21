// Our own NDO (Nendo) reader: SEAM ONLY, it reads no geometry yet. Nendo (Izware's subdivision modeler) files start with the
// text "nendo 1.x"; that is all this file knows. No public description of the body of the format was found (the only
// descriptions that exist are other projects' importer source code, which our rule 19a forbids following), and no real .ndo file
// is available, so the body is NOT guessed. The reader recognises the header, says which version it saw and refuses the file
// with a message that says so. Bounds-checked; nothing is allocated from the file. See docs/content/model_readers.md.
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <cstring>
#include <string>

namespace okay {

namespace {
constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
}

bool ReadNdoBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    (void)baseDir;
    out = ImportedModel{};
    if (bytes.size() < 9 || std::memcmp(bytes.data(), "nendo ", 6) != 0) {
        if (err) *err = "NDO: not a Nendo file (the text \"nendo 1.x\" is missing at the start)";
        return false;
    }
    std::string ver;
    for (std::size_t i = 6; i < 9 && i < bytes.size(); ++i) {
        const char c = bytes[i];
        if ((c >= '0' && c <= '9') || c == '.' || (c >= 'a' && c <= 'z')) ver += c; else break;
    }
    if (err) *err = "NDO: Nendo file, version \"" + ver + "\", recognised but not read: our own NDO reader has no geometry code yet because no public description of the file body exists that we may follow (docs/content/model_readers.md)";
    return false;
}

bool ReadNdoFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, kMaxFileBytes, bytes, err)) return false;
    return ReadNdoBuffer(bytes, std::string(), out, err);
}

} // namespace okay
