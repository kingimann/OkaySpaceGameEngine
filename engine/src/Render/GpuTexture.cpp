// okay::gputex: how block-compressed textures go to the GPU (see GpuTexture.hpp). No GL calls here.
#include "okay/Render/GpuTexture.hpp"

#include "okay/Core/ImportSidecar.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Texture/Export.hpp"
#include "okay/Texture/Texture.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace okay::gputex {

namespace fs = std::filesystem;

Caps CapsFrom(const char* version, const std::vector<std::string>& extensions) {
    Caps c;
    const std::string v = version ? version : "";
    const bool es = v.find("OpenGL ES") != std::string::npos;
    int major = 0;
    std::size_t i = es ? v.find("OpenGL ES") + 9 : 0;
    while (i < v.size() && (v[i] < '0' || v[i] > '9')) ++i;
    while (i < v.size() && v[i] >= '0' && v[i] <= '9') { major = major * 10 + (v[i] - '0'); ++i; if (major > 99) break; }
    if (!es && major >= 3) c.rgtc = true;             // RGTC is core in desktop OpenGL 3.0
    for (const std::string& e : extensions) {
        if (e == "GL_EXT_texture_compression_s3tc" || e == "GL_WEBGL_compressed_texture_s3tc") c.s3tc = true;
        else if (e == "GL_ARB_texture_compression_rgtc" || e == "GL_EXT_texture_compression_rgtc") c.rgtc = true;
    }
    return c;
}

unsigned GlCompressedFormat(tex::DdsFormat f) {
    switch (f) {
        case tex::DdsFormat::BC1: return kGlCompressedRgbaS3tcDxt1;
        case tex::DdsFormat::BC2: return kGlCompressedRgbaS3tcDxt3;
        case tex::DdsFormat::BC3: return kGlCompressedRgbaS3tcDxt5;
        case tex::DdsFormat::BC4: return kGlCompressedRedRgtc1;
        case tex::DdsFormat::BC5: return kGlCompressedRgRgtc2;
        default: return 0;
    }
}

bool Supported(const Caps& caps, tex::DdsFormat f) {
    switch (f) {
        case tex::DdsFormat::BC1: case tex::DdsFormat::BC2: case tex::DdsFormat::BC3: return caps.s3tc;
        case tex::DdsFormat::BC4: case tex::DdsFormat::BC5: return caps.rgtc;
        default: return false;
    }
}

bool PlanUpload(tex::DdsImage img, const Caps& caps, Plan* out) {
    if (!out || img.mips.empty() || img.width <= 0 || img.height <= 0) return false;
    Plan p;
    p.source = img.format;
    const unsigned f = GlCompressedFormat(img.format);
    const bool sized = img.width % 4 == 0 && img.height % 4 == 0;
    if (f && Supported(caps, img.format) && sized) {
        p.compressed = true;
        p.glFormat = f;
        for (std::size_t l = 0; l < img.mips.size(); ++l) {
            Level lv; tex::MipSize(img.width, img.height, int(l), &lv.width, &lv.height);
            if (img.mips[l].size() != tex::DdsLevelBytes(img.format, lv.width, lv.height)) { if (l == 0) return false; break; }
            lv.bytes = std::move(img.mips[l]);
            p.levels.push_back(std::move(lv));
        }
    } else {
        if (f) p.note = !Supported(caps, img.format)
                            ? std::string("the GPU has no ") + (caps.s3tc || img.format == tex::DdsFormat::BC4 || img.format == tex::DdsFormat::BC5 ? "RGTC" : "S3TC") + " support; decoded on the CPU"
                            : "size is not a multiple of 4; decoded on the CPU";
        for (std::size_t l = 0; l < img.mips.size(); ++l) {
            Level lv; tex::MipSize(img.width, img.height, int(l), &lv.width, &lv.height);
            if (!tex::DdsLevelToRGBA8(img, int(l), &lv.bytes)) { if (l == 0) return false; break; }
            p.levels.push_back(std::move(lv));
        }
    }
    *out = std::move(p);
    return true;
}

std::string CompressedCachePath(const std::string& imagePath) {
    const std::size_t slash = imagePath.find_last_of("/\\");
    const std::string dir = slash == std::string::npos ? std::string() : imagePath.substr(0, slash + 1);
    const std::string file = slash == std::string::npos ? imagePath : imagePath.substr(slash + 1);
    return dir + ".okaycache/" + file + ".dds";
}

namespace {

// FNV-1a, 64 bit (offset basis 14695981039346656037, prime 1099511628211), over a file's bytes.
bool HashFile(const std::string& path, std::uint64_t* hash, std::uint64_t* size) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    std::uint64_t h = 14695981039346656037ull, n = 0;
    char buf[1 << 15];
    while (f) {
        f.read(buf, sizeof buf);
        const std::streamsize got = f.gcount();
        for (std::streamsize i = 0; i < got; ++i) { h ^= std::uint8_t(buf[i]); h *= 1099511628211ull; }
        n += std::uint64_t(got);
    }
    if (f.bad()) return false;
    *hash = h;
    *size = n;
    return true;
}

// What a cached copy is made from: the picture and its .okaymeta (size and hash). "" when the picture cannot be read.
std::string SourceRecord(const std::string& imagePath) {
    std::uint64_t h = 0, n = 0;
    if (!HashFile(imagePath, &h, &n)) return std::string();
    std::string r = "okaycache 1 picture " + std::to_string(n) + " " + std::to_string(h);
    if (HashFile(imagePath + ".okaymeta", &h, &n)) r += " meta " + std::to_string(n) + " " + std::to_string(h);
    else r += " meta none";
    return r + "\n";
}

std::string RecordPath(const std::string& cache) { return cache + ".src"; }

std::string ReadSmallFile(const std::string& path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return std::string();
    std::string s(512, '\0');
    f.read(&s[0], std::streamsize(s.size()));
    s.resize(std::size_t(f.gcount()));
    return s;
}

// Write through a temporary file and a rename, so a reader never sees half a file.
bool WriteWhole(const std::string& path, const char* data, std::size_t size) {
    std::error_code ec;
    const std::string tmp = path + ".tmp";
    bool ok = false;
    {
        std::ofstream f(fs::u8path(tmp), std::ios::binary | std::ios::trunc);
        if (f) { f.write(data, std::streamsize(size)); ok = bool(f); }
    }
    if (ok) { fs::rename(fs::u8path(tmp), fs::u8path(path), ec); ok = !ec; }
    if (!ok) fs::remove(fs::u8path(tmp), ec);
    return ok;
}

// Keep `img` as the cached copy at `cache`, recorded as made from `record`. The record goes first out and last in,
// so a copy that was not finished never matches.
bool WriteCachedCopy(const std::string& cache, const tex::DdsImage& img, const std::string& record) {
    const std::vector<std::uint8_t> bytes = tex::WriteDds(img);
    if (bytes.empty() || record.empty()) return false;
    std::error_code ec;
    fs::create_directories(fs::u8path(cache).parent_path(), ec);
    fs::remove(fs::u8path(RecordPath(cache)), ec);
    if (!WriteWhole(cache, reinterpret_cast<const char*>(bytes.data()), bytes.size())) return false;
    return WriteWhole(RecordPath(cache), record.data(), record.size());
}

} // namespace

bool LoadCompressedCopy(const std::string& imagePath, tex::DdsImage* out, std::string* error, bool* rebuilt) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (!out) return fail("no output");
    if (rebuilt) *rebuilt = false;
    // The record, not the file times: a picture replaced by an older file (a backup, an unpacked archive) still differs.
    const std::string record = SourceRecord(imagePath);
    if (record.empty()) return fail("cannot read " + imagePath);
    const std::string cache = CompressedCachePath(imagePath);
    if (ReadSmallFile(RecordPath(cache)) == record) {
        tex::DdsImage cached;
        if (tex::ReadDdsFile(cache, &cached) && cached.format != tex::DdsFormat::RGBA8) { *out = std::move(cached); return true; }
    }
    // build it: the picture as the renderers load it (maxSize applied), its own sRGB and mipmaps settings
    Image img;
    std::string why;
    if (!img.Load(imagePath, &why)) return fail("cannot load " + imagePath + ": " + why);
    ApplyTextureImportSettings(imagePath, img);
    tex::TextureBuildOptions bo;
    bo.compress = true;
    bo.mips = TextureImportSampling(imagePath).mipmaps;
    const std::string srgb = ImportLower(ReadImportSetting(imagePath, "texture", "srgb"));
    bo.mip.srgb = !(srgb == "0" || srgb == "false" || srgb == "off");
    tex::DdsImage built;
    if (!tex::BuildDds(img.Data(), img.Width(), img.Height(), bo, &built, &why)) return fail(why);
    WriteCachedCopy(cache, built, record);   // keep it for next time; a folder we may not write to only costs the rebuild
    *out = std::move(built);
    if (rebuilt) *rebuilt = true;
    return true;
}

bool ShipCompressedCopy(const std::string& sourceImage, const std::string& shippedImage, std::string* error) {
    if (error) error->clear();
    if (!ImportFlagOn(ReadImportSetting(sourceImage, "texture", "compress"))) return false;
    tex::DdsImage img;
    if (!LoadCompressedCopy(sourceImage, &img, error)) return false;   // the project's own cache, built if needed
    // Recorded as made from the shipped files, so the game finds it current whatever times the copies get.
    const std::string record = SourceRecord(shippedImage);
    if (record.empty()) { if (error) *error = "cannot read " + shippedImage; return false; }
    const std::string cache = CompressedCachePath(shippedImage);
    if (!WriteCachedCopy(cache, img, record)) { if (error) *error = "cannot write " + cache; return false; }
    return true;
}

bool Source(const std::string& name, tex::DdsImage* out, std::string* error) {
    if (name.empty() || name[0] == '@' || name.rfind("proc:", 0) == 0 || !out) return false;
    if (tex::IsDdsPath(name)) return tex::ReadDdsFile(name, out, error);
    if (!ImportFlagOn(ReadImportSetting(name, "texture", "compress"))) return false;
    return LoadCompressedCopy(name, out, error);
}

} // namespace okay::gputex
