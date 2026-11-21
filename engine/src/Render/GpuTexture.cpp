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

bool LoadCompressedCopy(const std::string& imagePath, tex::DdsImage* out, std::string* error, bool* rebuilt) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (!out) return fail("no output");
    if (rebuilt) *rebuilt = false;
    std::error_code ec;
    fs::file_time_type newest = fs::last_write_time(imagePath, ec);
    if (ec) return fail("cannot read " + imagePath);
    const fs::file_time_type metaTime = fs::last_write_time(imagePath + ".okaymeta", ec);
    if (!ec && metaTime > newest) newest = metaTime;
    const std::string cache = CompressedCachePath(imagePath);
    const fs::file_time_type cacheTime = fs::last_write_time(cache, ec);
    if (!ec && cacheTime >= newest) {
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
    const std::vector<std::uint8_t> bytes = tex::WriteDds(built);
    if (!bytes.empty()) {   // keep it for next time; a folder we may not write to only costs the rebuild
        fs::create_directories(fs::path(cache).parent_path(), ec);
        const std::string tmp = cache + ".tmp";
        bool ok = false;
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (f) { f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); ok = bool(f); }
        }
        if (ok) { fs::rename(tmp, cache, ec); if (ec) fs::remove(tmp, ec); }
        else fs::remove(tmp, ec);
    }
    *out = std::move(built);
    if (rebuilt) *rebuilt = true;
    return true;
}

bool ShipCompressedCopy(const std::string& sourceImage, const std::string& shippedImage, std::string* error) {
    (void)sourceImage; (void)shippedImage; (void)error;
    return false;
}

bool Source(const std::string& name, tex::DdsImage* out, std::string* error) {
    if (name.empty() || name[0] == '@' || name.rfind("proc:", 0) == 0 || !out) return false;
    if (tex::IsDdsPath(name)) return tex::ReadDdsFile(name, out, error);
    if (!ImportFlagOn(ReadImportSetting(name, "texture", "compress"))) return false;
    return LoadCompressedCopy(name, out, error);
}

} // namespace okay::gputex
