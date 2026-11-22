// The OpenGL renderer drawing from the shared texture asset (okay/Render/TextureAsset.hpp), on a real GL context where
// the machine can make one (Mesa's llvmpipe through EGL, no display needed). Where no context can be made the test says
// SKIPPED and passes.
// Checked: an ordinary picture goes up with our own mip chain (every level, gamma-correct: a black / white checker's
// level 1 reads 188, not the 128 a plain byte average gives), a linear (data) picture averages to 128, a hot reload
// uploads the new pixels under a new serial, the per-texture anisotropy reaches GL, and a texture the library lets go
// of leaves GL memory too (MemoryTracker "Textures (GPU)").
#include "test_framework.hpp"
#include "../okayui/tests/RenderGLContext.hpp"

#include "okay/Render/GLRenderer.hpp"
#include "okay/Render/TextureAsset.hpp"
#include "okay/Debug/MemoryTracker.hpp"
#include "okay/Graphics/Image.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace okay;
namespace fs = std::filesystem;

namespace {
gltest::Context* g_ctx = nullptr;
void* Proc(const char* n) { return g_ctx ? g_ctx->Proc(n) : nullptr; }
void (*glBindTexture_)(unsigned, unsigned) = nullptr;
void (*glGetTexImage_)(unsigned, int, unsigned, unsigned, void*) = nullptr;
void (*glGetTexLevelParameteriv_)(unsigned, int, unsigned, int*) = nullptr;
void (*glGetTexParameterfv_)(unsigned, unsigned, float*) = nullptr;
void (*glPixelStorei_)(unsigned, int) = nullptr;
unsigned (*glGetError_)() = nullptr;

Image Checker(int n) {
    Image im(n, n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            std::uint8_t* p = im.Data() + (y * n + x) * 4;
            p[0] = p[1] = p[2] = ((x + y) & 1) ? 255 : 0; p[3] = 255;
        }
    return im;
}
Image Solid(int w, int h, std::uint8_t r) {
    Image im(w, h);
    for (int i = 0; i < w * h; ++i) { im.Data()[i * 4] = r; im.Data()[i * 4 + 1] = 0; im.Data()[i * 4 + 2] = 0; im.Data()[i * 4 + 3] = 255; }
    return im;
}
void Meta(const std::string& png, const std::string& sets) {
    std::ofstream f(png + ".okaymeta", std::ios::binary | std::ios::trunc);
    f << "okaymeta 1\ntype texture\nimporter texture 2\n" << sets;
}
int LevelWidth(unsigned tex, int level) {
    int v = 0;
    glBindTexture_(0x0DE1, tex);
    glGetTexLevelParameteriv_(0x0DE1, level, 0x1000 /*GL_TEXTURE_WIDTH*/, &v);
    return v;
}
std::vector<std::uint8_t> Read(unsigned tex, int level, int w, int h) {
    std::vector<std::uint8_t> got(std::size_t(w) * h * 4, 0);
    glBindTexture_(0x0DE1, tex);
    glPixelStorei_(0x0D05 /*GL_PACK_ALIGNMENT*/, 1);
    glGetTexImage_(0x0DE1, level, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, got.data());
    return got;
}
std::int64_t GpuCount() {
    MemoryTracker& m = MemoryTracker::Global();
    const int c = m.FindCategory("Textures (GPU)");
    return c < 0 ? 0 : m.Count(c);
}
} // namespace

int main() {
    RUN_SUITE("gl texture asset (EGL, Mesa)");
    gltest::Context ctx; std::string why;
    if (!ctx.Create(false, &why)) { std::cout << "  SKIPPED: " << why << "\n"; TEST_MAIN_RESULT(); }
    g_ctx = &ctx;
    std::cout << "  context: " << ctx.Info() << "\n";
    CHECK(GLRenderer::LoadGL(&Proc));
    glBindTexture_ = (decltype(glBindTexture_))Proc("glBindTexture");
    glGetTexImage_ = (decltype(glGetTexImage_))Proc("glGetTexImage");
    glGetTexLevelParameteriv_ = (decltype(glGetTexLevelParameteriv_))Proc("glGetTexLevelParameteriv");
    glGetTexParameterfv_ = (decltype(glGetTexParameterfv_))Proc("glGetTexParameterfv");
    glPixelStorei_ = (decltype(glPixelStorei_))Proc("glPixelStorei");
    glGetError_ = (decltype(glGetError_))Proc("glGetError");
    if (!glBindTexture_ || !glGetTexImage_ || !glGetTexLevelParameteriv_ || !glPixelStorei_) { std::cout << "  SKIPPED: no glGetTexImage\n"; TEST_MAIN_RESULT(); }

    textures::ResetForTests();
    fs::create_directories("gl_texasset");
    const std::string col = "gl_texasset/checker.png", lin = "gl_texasset/linear.png";
    CHECK(Checker(8).SavePNG(col));
    CHECK(Checker(8).SavePNG(lin));
    Meta(lin, "set srgb 0\n");
    const std::int64_t gpu0 = GpuCount();
    {
        GLRenderer r;
        // Our own chain: 4 levels (8, 4, 2, 1), level 1 of a colour checker is sRGB 188, of a data checker 128.
        const unsigned id = r.TextureId(col);
        const GLRenderer::TextureUploadInfo* info = r.UploadInfo(col);
        CHECK(id != 0 && info);
        if (id && info) {
            CHECK(!info->fromDds);
            CHECK(info->uploadedLevels == 4);
            CHECK(LevelWidth(id, 3) == 1);
            CHECK(info->serial == textures::Find(col).Serial());
            CHECK(info->gpuBytes == std::size_t(8 * 8 + 4 * 4 + 2 * 2 + 1) * 4);
            CHECK(info->anisotropy == 8);
            const std::vector<std::uint8_t> l1 = Read(id, 1, 4, 4);
            CHECK(l1[0] >= 186 && l1[0] <= 190);
            std::printf("  colour checker level 1 reads %d (gamma-correct average of 0 and 255 is 188)\n", l1[0]);
        }
        const unsigned idl = r.TextureId(lin);
        if (idl) {
            const std::vector<std::uint8_t> l1 = Read(idl, 1, 4, 4);
            CHECK(l1[0] >= 127 && l1[0] <= 128);
            std::printf("  linear checker level 1 reads %d\n", l1[0]);
        }
        CHECK(GpuCount() == gpu0 + 2);

        // Hot reload: rewrite the picture, the next TextureId uploads the new pixels under the new serial.
        textures::SetHotReload(true, 0.0);
        const std::uint64_t s0 = textures::Find(col).Serial();
        CHECK(Solid(4, 4, 222).SavePNG(col));
        fs::last_write_time(col, fs::last_write_time(col) + std::chrono::seconds(20));
        bool seen = false;
        for (int i = 0; i < 2500 && !seen; ++i) {
            textures::Pump();
            seen = textures::Find(col).Serial() != s0;
            if (!seen) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(seen);
        textures::SetHotReload(false);
        const unsigned id2 = r.TextureId(col);
        CHECK(id2 != 0);
        if (id2) {
            const std::vector<std::uint8_t> l0 = Read(id2, 0, 4, 4);
            CHECK(l0[0] == 222);
            CHECK(r.UploadInfo(col) && r.UploadInfo(col)->serial == textures::Find(col).Serial());
            CHECK(r.UploadInfo(col) && r.UploadInfo(col)->uploadedLevels == 3);
        }
        CHECK(GpuCount() == gpu0 + 2);   // the old copy was freed, the new one counted

        // Per-texture anisotropy and point filtering reach GL.
        Meta(col, "set filter trilinear\nset anisotropy 4\n");
        CHECK(textures::Reload(col));
        const unsigned id3 = r.TextureId(col);
        CHECK(r.UploadInfo(col) && r.UploadInfo(col)->anisotropy == 4);
        if (id3 && glGetTexParameterfv_ && glGetError_) {
            while (glGetError_() != 0) {}
            float a = 0.0f;
            glBindTexture_(0x0DE1, id3);
            glGetTexParameterfv_(0x0DE1, 0x84FE, &a);
            if (glGetError_() == 0) { CHECK(a == 4.0f); std::printf("  GL_TEXTURE_MAX_ANISOTROPY reads %.0f\n", a); }
            else std::printf("  driver has no anisotropic filtering: not checked\n");
        }
        Meta(col, "set filter point\nset anisotropy 16\n");
        CHECK(textures::Reload(col));
        r.TextureId(col);
        CHECK(r.UploadInfo(col) && r.UploadInfo(col)->anisotropy == 1);
        // Mipmaps off: one level on the GPU.
        Meta(col, "set mipmaps 0\n");
        CHECK(textures::Reload(col));
        r.TextureId(col);
        CHECK(r.UploadInfo(col) && r.UploadInfo(col)->uploadedLevels == 1);

        // The library lets go of a name: the next use of the renderer frees its GL copy too.
        textures::Drop(lin);
        r.TextureId(col);
        CHECK(r.UploadInfo(lin) == nullptr);
        CHECK(GpuCount() == gpu0 + 1);
        r.Destroy();
        CHECK(GpuCount() == gpu0);
    }
    textures::ResetForTests();
    fs::remove_all("gl_texasset");
    CHECK(ctx.g.GetError() == 0);
    TEST_MAIN_RESULT();
}
