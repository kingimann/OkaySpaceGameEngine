// The OpenGL renderer loading .dds textures and compressed import copies, on a real GL context where the machine can
// make one (Mesa's llvmpipe through EGL, no display needed; the same helper the OkayUI GL test uses). Where no context
// can be made the test says SKIPPED and passes.
// What is checked: the texture the renderer made has the compressed internal format when the driver offers S3TC / RGTC
// (asked from GL itself), every level is there, and reading it back (glGetTexImage decodes it on the driver's side)
// gives our own CPU decode within a small tolerance (drivers may round the 1/3 and 2/3 palette entries differently).
// With OKAY_GL_NO_COMPRESSED=1 the same files go the CPU-decode way and read back exactly as our decoder gives them.
#include "test_framework.hpp"
#include "gl_readback_setup.hpp"

#include "okay/Render/GLRenderer.hpp"
#include "okay/Render/GpuTexture.hpp"
#include "okay/Texture/Export.hpp"
#include "okay/Texture/Texture.hpp"
#include "okay/Graphics/Image.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace okay;
using namespace glread;
using Bytes = std::vector<std::uint8_t>;

namespace {

Bytes Picture(int w, int h, bool alpha) {
    Bytes px(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        std::uint8_t* p = &px[(std::size_t(y) * w + x) * 4];
        p[0] = std::uint8_t(x * 255 / std::max(1, w - 1)); p[1] = std::uint8_t(y * 255 / std::max(1, h - 1));
        p[2] = std::uint8_t(((x / 4 + y / 4) & 1) ? 200 : 40); p[3] = alpha ? std::uint8_t(255 - x * 255 / std::max(1, w - 1)) : 255;
    }
    return px;
}

std::string WriteDdsFile(const std::string& path, tex::DdsFormat f, int w, int h, bool mips) {
    const Bytes px = Picture(w, h, f == tex::DdsFormat::BC2 || f == tex::DdsFormat::BC3);
    tex::TextureBuildOptions bo;
    bo.mips = mips;
    bo.compress = f != tex::DdsFormat::RGBA8;
    tex::BcFormat bc = tex::BcFormat::BC1;
    if (tex::DdsBcFormat(f, &bc)) { bo.autoFormat = false; bo.format = bc; }
    tex::DdsImage img; std::string err;
    if (!tex::BuildDds(px.data(), w, h, bo, &img, &err)) { std::cerr << "  build failed: " << err << "\n"; return ""; }
    const Bytes bytes = tex::WriteDds(img);
    std::ofstream o(path, std::ios::binary);
    o.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return path;
}

int InternalFormat(unsigned tex, int level) {
    int v = 0;
    glBindTexture_(0x0DE1, tex);
    glGetTexLevelParameteriv_(0x0DE1, level, 0x1003 /*GL_TEXTURE_INTERNAL_FORMAT*/, &v);
    return v;
}

// Largest channel difference between what GL holds at `level` and our own decode of that level.
int ReadBackDiff(unsigned tex, const tex::DdsImage& img, int level) {
    int w, h; tex::MipSize(img.width, img.height, level, &w, &h);
    Bytes want;
    if (!tex::DdsLevelToRGBA8(img, level, &want)) return 9999;
    Bytes got(std::size_t(w) * h * 4, 0);
    glBindTexture_(0x0DE1, tex);
    glPixelStorei_(0x0D05 /*GL_PACK_ALIGNMENT*/, 1);
    glGetTexImage_(0x0DE1, level, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, got.data());
    int worst = 0;
    for (std::size_t i = 0; i < got.size(); ++i) worst = std::max(worst, std::abs(int(got[i]) - int(want[i])));
    return worst;
}

struct Case { const char* file; tex::DdsFormat format; int w, h; bool mips; };

void RunCases(bool allowCompressed, const gputex::Caps& caps) {
    GLRenderer r;
    const Case cases[] = {
        {"gl_dds_bc1.dds", tex::DdsFormat::BC1, 32, 32, true},
        {"gl_dds_bc2.dds", tex::DdsFormat::BC2, 32, 16, true},
        {"gl_dds_bc3.dds", tex::DdsFormat::BC3, 16, 16, true},
        {"gl_dds_bc4.dds", tex::DdsFormat::BC4, 16, 16, true},
        {"gl_dds_bc5.dds", tex::DdsFormat::BC5, 16, 32, false},
        {"gl_dds_rgba.dds", tex::DdsFormat::RGBA8, 8, 8, true},
        {"gl_dds_odd.dds", tex::DdsFormat::BC1, 6, 6, true},   // not a multiple of 4: always decoded on the CPU
    };
    for (const Case& c : cases) {
        const std::string path = WriteDdsFile(c.file, c.format, c.w, c.h, c.mips);
        tex::DdsImage img; std::string err;
        CHECK(tex::ReadDdsFile(path, &img, &err));
        const unsigned id = r.TextureId(path);
        CHECK(id != 0);
        const GLRenderer::TextureUploadInfo* info = r.UploadInfo(path);
        CHECK(info != nullptr);
        if (!id || !info) continue;
        const bool wantCompressed = allowCompressed && gputex::Supported(caps, c.format) && c.w % 4 == 0 && c.h % 4 == 0;
        CHECK(info->fromDds);
        CHECK(info->compressed == wantCompressed);
        CHECK(info->levels == int(img.mips.size()));
        const int fmt = InternalFormat(id, 0);
        if (wantCompressed) CHECK(unsigned(fmt) == gputex::GlCompressedFormat(c.format));
        else CHECK(unsigned(fmt) == gputex::kGlRgba8);
        int worst = 0;
        for (int l = 0; l < int(img.mips.size()); ++l) worst = std::max(worst, ReadBackDiff(id, img, l));
        // drivers may round the in-between palette entries their own way; our CPU path must match exactly
        CHECK(wantCompressed ? worst <= 8 : worst == 0);
        std::printf("  %-16s %-5s %3dx%-3d levels %2d  %-10s internal 0x%04X  read-back max diff %d%s%s\n", c.file, tex::DdsFormatName(c.format),
                    c.w, c.h, info->levels, info->compressed ? "compressed" : "RGBA8", unsigned(fmt), worst,
                    info->note.empty() ? "" : "  (", info->note.empty() ? "" : (info->note + ")").c_str());
        std::remove(path.c_str());
    }

    // a PNG whose import settings say `compress`: the renderer gets the compressed copy from the import cache
    {
        namespace fs = std::filesystem;
        fs::create_directories("gl_dds_assets");
        const std::string png = "gl_dds_assets/wall.png";
        const Bytes px = Picture(32, 32, false);
        Image im(32, 32); std::memcpy(im.Data(), px.data(), px.size());
        CHECK(im.SavePNG(png));
        { std::ofstream m(png + ".okaymeta"); m << "okaymeta 1\ntype texture\nimporter texture 2\nset compress 1\n"; }
        const unsigned id = r.TextureId(png);
        const GLRenderer::TextureUploadInfo* info = r.UploadInfo(png);
        CHECK(id != 0 && info && info->fromDds);
        CHECK(fs::exists(gputex::CompressedCachePath(png)));
        if (info) {
            CHECK(info->compressed == (allowCompressed && caps.s3tc));
            CHECK(info->levels == 6);
            std::printf("  wall.png (compress on)  levels %d  %s internal 0x%04X\n", info->levels, info->compressed ? "compressed" : "RGBA8", unsigned(InternalFormat(id, 0)));
        }
        // compress off: today's way (an RGBA8 upload of the picture itself)
        { std::ofstream m(png + ".okaymeta"); m << "okaymeta 1\ntype texture\nimporter texture 2\nset compress 0\n"; }
        GLRenderer r2;
        const unsigned id2 = r2.TextureId(png);
        const GLRenderer::TextureUploadInfo* info2 = r2.UploadInfo(png);
        CHECK(id2 != 0 && info2 && !info2->fromDds && !info2->compressed);
        CHECK(id2 && unsigned(InternalFormat(id2, 0)) == gputex::kGlRgba8);
        r2.Destroy();
        fs::remove_all("gl_dds_assets");
    }
    r.Destroy();
}

} // namespace

int main() {
    RUN_SUITE("gl dds upload (EGL, Mesa)");
    gltest::Context ctx;
    if (!glread::Open(ctx)) TEST_MAIN_RESULT();
    const gputex::Caps caps = GLRenderer::QueryTextureCaps();
    std::cout << "  driver: S3TC " << (caps.s3tc ? "yes" : "no") << ", RGTC " << (caps.rgtc ? "yes" : "no") << "\n";

    RUN_SUITE("gl dds upload: compressed where the driver can");
    RunCases(true, caps);
    RUN_SUITE("gl dds upload: OKAY_GL_NO_COMPRESSED=1 (CPU decode)");
    setenv("OKAY_GL_NO_COMPRESSED", "1", 1);
    RunCases(false, caps);
    unsetenv("OKAY_GL_NO_COMPRESSED");
    CHECK(ctx.g.GetError() == 0);
    TEST_MAIN_RESULT();
}
