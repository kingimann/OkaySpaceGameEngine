// okay-texture: command line front end of our texture library (okay::tex, docs/rendering/own_texture_library.md).
//   okay-texture mips     <in.png> <prefix> [--filter box|lanczos|kaiser] [--srgb] [--normal] [--cutout] [--repeat]
//   okay-texture compress <in.png> [out.dds] [--format auto|bc1|bc3|bc4|bc5] [--quality 0|1|2] [--no-mips] [--raw]
//                         [--filter ...] [--srgb] [--normal] [--cutout]
//   okay-texture atlas    <sheet.png> <manifest.json> <a.png> <b.png> ... [--pad N] [--bleed N] [--max N] [--rotate] [--npot]
//   okay-texture info     <file.dds>
//   okay-texture stream   <file.dds> <screenPixels>       (which mip the streaming planner keeps for that on-screen size)
// Windows build: ships in Tools\okay-texture.exe; the example in examples\texture shows every command.
#include "okay/Texture/Export.hpp"
#include "okay/Texture/Stream.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace okay::tex;

static int Usage() {
    std::puts(
        "okay-texture: mipmaps, atlases and compression (our own texture library)\n"
        "  okay-texture mips     <in.png> <prefix> [--filter box|lanczos|kaiser] [--srgb] [--normal] [--cutout] [--repeat]\n"
        "        writes <prefix>_0.png, <prefix>_1.png ... one picture per mip level\n"
        "  okay-texture compress <in.png> [out.dds] [--format auto|bc1|bc3|bc4|bc5] [--quality 0|1|2] [--no-mips] [--raw]\n"
        "        writes a .dds with mips, block compressed (--raw keeps it uncompressed RGBA8)\n"
        "  okay-texture atlas    <sheet.png> <manifest.json> <a.png> <b.png> ... [--pad N] [--bleed N] [--max N] [--rotate] [--npot]\n"
        "  okay-texture info     <file.dds>\n"
        "  okay-texture stream   <file.dds> <screenPixels>\n"
        "common: --filter box|lanczos|kaiser  --srgb (colour picture, filter in linear light)  --normal (normal map)\n"
        "        --cutout (keep alpha-test coverage down the chain)  --repeat (tiling texture)");
    return 2;
}

struct Args {
    std::vector<std::string> pos;
    TextureBuildOptions build;
    AtlasOptions atlas;
    bool raw = false;
    bool ok = true;
};

static Args Parse(int argc, char** argv, int from) {
    Args a;
    a.build.compress = true;
    for (int i = from; i < argc; ++i) {
        std::string s = argv[i];
        auto next = [&]() -> const char* { if (i + 1 < argc) return argv[++i]; a.ok = false; return ""; };
        if (s == "--filter") {
            std::string f = next();
            if (f == "box") a.build.mip.filter = MipFilter::Box;
            else if (f == "lanczos") a.build.mip.filter = MipFilter::Lanczos3;
            else if (f == "kaiser") a.build.mip.filter = MipFilter::Kaiser;
            else a.ok = false;
        } else if (s == "--srgb") a.build.mip.srgb = true;
        else if (s == "--normal") a.build.mip.normalMap = true;
        else if (s == "--cutout") a.build.mip.alphaCoverage = true;
        else if (s == "--repeat") a.build.mip.wrap = MipWrap::Repeat;
        else if (s == "--no-mips") a.build.mips = false;
        else if (s == "--raw") { a.raw = true; a.build.compress = false; }
        else if (s == "--format") {
            std::string f = next();
            if (f == "auto") a.build.autoFormat = true;
            else if (ParseBcFormat(f.c_str(), &a.build.format)) a.build.autoFormat = false;
            else a.ok = false;
        } else if (s == "--quality") a.build.encode.quality = std::atoi(next());
        else if (s == "--pad") a.atlas.padding = std::atoi(next());
        else if (s == "--bleed") a.atlas.bleed = std::atoi(next());
        else if (s == "--max") a.atlas.maxWidth = a.atlas.maxHeight = std::atoi(next());
        else if (s == "--rotate") a.atlas.allowRotate = true;
        else if (s == "--npot") a.atlas.powerOfTwo = false;
        else if (s.size() > 1 && s[0] == '-' && s[1] == '-') { std::fprintf(stderr, "unknown option %s\n", s.c_str()); a.ok = false; }
        else a.pos.push_back(s);
    }
    return a;
}

static bool ReadAll(const std::string& path, std::vector<std::uint8_t>* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

static const char* DdsName(DdsFormat f) { return DdsFormatName(f); }

int main(int argc, char** argv) {
    if (argc < 2) return Usage();
    const std::string cmd = argv[1];
    Args a = Parse(argc, argv, 2);
    if (!a.ok) return Usage();
    std::string err;
    if (cmd == "mips") {
        if (a.pos.size() != 2) return Usage();
        int n = WriteMipPngs(a.pos[0], a.pos[1], a.build.mip, &err);
        if (!n) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        std::printf("wrote %d mip level(s): %s_0.png ... %s_%d.png\n", n, a.pos[1].c_str(), a.pos[1].c_str(), n - 1);
        return 0;
    }
    if (cmd == "compress") {
        if (a.pos.empty() || a.pos.size() > 2) return Usage();
        std::string out = a.pos.size() == 2 ? a.pos[1] : DdsPathFor(a.pos[0], a.build.compress);
        if (!BuildDdsFile(a.pos[0], &out, a.build, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        std::vector<std::uint8_t> bytes; DdsImage d;
        if (ReadAll(out, &bytes) && ReadDds(bytes.data(), bytes.size(), &d))
            std::printf("wrote %s: %s %dx%d, %d mip level(s), %zu bytes\n", out.c_str(), DdsName(d.format), d.width, d.height, int(d.mips.size()), bytes.size());
        else std::printf("wrote %s\n", out.c_str());
        return 0;
    }
    if (cmd == "atlas") {
        if (a.pos.size() < 3) return Usage();
        std::vector<std::string> pics(a.pos.begin() + 2, a.pos.end());
        if (!BuildAtlasFiles(pics, a.pos[0], a.pos[1], a.atlas, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        std::printf("packed %zu picture(s) into %s (manifest %s)\n", pics.size(), a.pos[0].c_str(), a.pos[1].c_str());
        return 0;
    }
    if (cmd == "info" || cmd == "stream") {
        if (a.pos.empty()) return Usage();
        std::vector<std::uint8_t> bytes;
        if (!ReadAll(a.pos[0], &bytes)) { std::fprintf(stderr, "error: cannot read %s\n", a.pos[0].c_str()); return 1; }
        DdsImage d;
        if (!ReadDds(bytes.data(), bytes.size(), &d, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        if (cmd == "info") {
            std::printf("%s: %s %dx%d, %d mip level(s)\n", a.pos[0].c_str(), DdsName(d.format), d.width, d.height, int(d.mips.size()));
            for (std::size_t l = 0; l < d.mips.size(); ++l) {
                int w, h; MipSize(d.width, d.height, int(l), &w, &h);
                std::printf("  level %zu: %dx%d, %zu bytes\n", l, w, h, d.mips[l].size());
            }
            return 0;
        }
        if (a.pos.size() != 2) return Usage();
        const float px = float(std::atof(a.pos[1].c_str()));
        StreamTexture t; t.id = 1; t.width = d.width; t.height = d.height; t.mipCount = int(d.mips.size());
        t.bytesPerPixel = float(d.mips[0].size()) / float(std::size_t(d.width) * d.height);
        StreamCamera cam; cam.screenHeight = 1000; cam.fovY = 1.5707964f;
        StreamInstance s; s.textureId = 1; s.z = 1.0f; s.worldSize = px / 500.0f;   // 1000 px tall screen, 90 degree fov: size/(2*1)*1000 = px
        StreamPlanner sp;
        StreamPlan p = sp.Plan(cam, {t}, {s});
        int w, h; MipSize(d.width, d.height, p.requests[0].mip, &w, &h);
        std::printf("at %.0f pixels on screen keep mip %d (%dx%d) and below resident: %.0f bytes of %zu\n", px, p.requests[0].mip, w, h,
                    p.residentBytes, [&] { std::size_t s2 = 0; for (auto& m : d.mips) s2 += m.size(); return s2; }());
        return 0;
    }
    return Usage();
}
