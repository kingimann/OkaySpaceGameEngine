// okay::tex DDS writer / reader, written from the public file layout (see Dds.hpp).
#include "okay/Texture/Dds.hpp"
#include "okay/Texture/Texture.hpp"
#include <cstring>

namespace okay::tex {

namespace {

void Put32(std::vector<std::uint8_t>& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
std::uint32_t Get32(const std::uint8_t* p) { return p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
std::uint32_t Four(const char* s) { return Get32(reinterpret_cast<const std::uint8_t*>(s)); }

const std::uint32_t DDSD_CAPS = 0x1, DDSD_HEIGHT = 0x2, DDSD_WIDTH = 0x4, DDSD_PITCH = 0x8, DDSD_PIXELFORMAT = 0x1000,
                    DDSD_MIPMAPCOUNT = 0x20000, DDSD_LINEARSIZE = 0x80000;
const std::uint32_t DDPF_ALPHAPIXELS = 0x1, DDPF_FOURCC = 0x4, DDPF_RGB = 0x40;
const std::uint32_t DDSCAPS_COMPLEX = 0x8, DDSCAPS_TEXTURE = 0x1000, DDSCAPS_MIPMAP = 0x400000;
const int kMaxDim = 16384;

bool IsBc(DdsFormat f) { return f != DdsFormat::RGBA8; }

BcFormat ToBc(DdsFormat f) {
    switch (f) { case DdsFormat::BC1: return BcFormat::BC1; case DdsFormat::BC3: return BcFormat::BC3;
                 case DdsFormat::BC4: return BcFormat::BC4; default: return BcFormat::BC5; }
}

} // namespace

std::size_t DdsLevelBytes(DdsFormat f, int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    if (f == DdsFormat::RGBA8) return std::size_t(w) * std::size_t(h) * 4;
    return BcImageBytes(ToBc(f), w, h);
}

std::vector<std::uint8_t> WriteDds(const DdsImage& img) {
    std::vector<std::uint8_t> out;
    if (img.width <= 0 || img.height <= 0 || img.width > kMaxDim || img.height > kMaxDim) return out;
    const int n = int(img.mips.size());
    if (n < 1 || n > 16) return out;
    for (int i = 0; i < n; ++i) {
        int w, h; MipSize(img.width, img.height, i, &w, &h);
        if (img.mips[i].size() != DdsLevelBytes(img.format, w, h)) return out;
    }
    const bool dx10 = img.format == DdsFormat::BC4 || img.format == DdsFormat::BC5;
    std::uint32_t flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | (IsBc(img.format) ? DDSD_LINEARSIZE : DDSD_PITCH);
    if (n > 1) flags |= DDSD_MIPMAPCOUNT;
    out.reserve(128 + (dx10 ? 20 : 0));
    Put32(out, Four("DDS "));
    Put32(out, 124); Put32(out, flags); Put32(out, std::uint32_t(img.height)); Put32(out, std::uint32_t(img.width));
    Put32(out, IsBc(img.format) ? std::uint32_t(img.mips[0].size()) : std::uint32_t(img.width) * 4);
    Put32(out, 0); Put32(out, std::uint32_t(n));
    for (int i = 0; i < 11; ++i) Put32(out, 0);
    // pixel format (32 bytes)
    Put32(out, 32);
    switch (img.format) {
        case DdsFormat::RGBA8:
            Put32(out, DDPF_RGB | DDPF_ALPHAPIXELS); Put32(out, 0); Put32(out, 32);
            Put32(out, 0x000000FF); Put32(out, 0x0000FF00); Put32(out, 0x00FF0000); Put32(out, 0xFF000000); break;
        case DdsFormat::BC1: Put32(out, DDPF_FOURCC); Put32(out, Four("DXT1")); for (int i = 0; i < 5; ++i) Put32(out, 0); break;
        case DdsFormat::BC3: Put32(out, DDPF_FOURCC); Put32(out, Four("DXT5")); for (int i = 0; i < 5; ++i) Put32(out, 0); break;
        default: Put32(out, DDPF_FOURCC); Put32(out, Four("DX10")); for (int i = 0; i < 5; ++i) Put32(out, 0); break;
    }
    Put32(out, DDSCAPS_TEXTURE | (n > 1 ? (DDSCAPS_COMPLEX | DDSCAPS_MIPMAP) : 0));
    Put32(out, 0); Put32(out, 0); Put32(out, 0); Put32(out, 0);
    if (dx10) {
        Put32(out, img.format == DdsFormat::BC4 ? 80 : 83);   // DXGI_FORMAT_BC4_UNORM / BC5_UNORM
        Put32(out, 3);                                         // D3D10_RESOURCE_DIMENSION_TEXTURE2D
        Put32(out, 0); Put32(out, 1); Put32(out, 0);           // misc flags, array size, misc flags 2
    }
    for (const auto& m : img.mips) out.insert(out.end(), m.begin(), m.end());
    return out;
}

bool ReadDds(const std::uint8_t* d, std::size_t size, DdsImage* out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (!d || !out) return fail("no data");
    if (size < 128) return fail("file too short");
    if (Get32(d) != Four("DDS ")) return fail("not a DDS file (missing magic)");
    const std::uint8_t* h = d + 4;
    if (Get32(h) != 124) return fail("bad header size");
    const std::uint32_t flags = Get32(h + 4);
    const std::uint32_t height = Get32(h + 8), width = Get32(h + 12);
    if (!(flags & DDSD_WIDTH) || !(flags & DDSD_HEIGHT)) return fail("size flags missing");
    if (width == 0 || height == 0 || width > std::uint32_t(kMaxDim) || height > std::uint32_t(kMaxDim)) return fail("size out of range");
    if (Get32(h + 72) != 32) return fail("bad pixel format size");
    std::uint32_t mipCount = (flags & DDSD_MIPMAPCOUNT) ? Get32(h + 24) : 1;
    if (mipCount == 0) mipCount = 1;
    if (mipCount > 16) return fail("too many mip levels");
    const std::uint32_t pf = Get32(h + 76), fourcc = Get32(h + 80), bits = Get32(h + 84);
    const std::uint32_t rm = Get32(h + 88), gm = Get32(h + 92), bm = Get32(h + 96), am = Get32(h + 100);
    std::size_t pos = 128;
    DdsFormat fmt;
    if (pf & DDPF_FOURCC) {
        if (fourcc == Four("DXT1")) fmt = DdsFormat::BC1;
        else if (fourcc == Four("DXT5")) fmt = DdsFormat::BC3;
        else if (fourcc == Four("ATI1") || fourcc == Four("BC4U")) fmt = DdsFormat::BC4;
        else if (fourcc == Four("ATI2") || fourcc == Four("BC5U")) fmt = DdsFormat::BC5;
        else if (fourcc == Four("DX10")) {
            if (size < 148) return fail("file too short for the DX10 header");
            const std::uint32_t dxgi = Get32(d + 128);
            if (Get32(d + 128 + 4) != 3) return fail("only 2D textures are supported");
            if (Get32(d + 128 + 12) > 1) return fail("texture arrays are not supported");
            if (dxgi == 71) fmt = DdsFormat::BC1;
            else if (dxgi == 77) fmt = DdsFormat::BC3;
            else if (dxgi == 80) fmt = DdsFormat::BC4;
            else if (dxgi == 83) fmt = DdsFormat::BC5;
            else if (dxgi == 28) fmt = DdsFormat::RGBA8;
            else return fail("unsupported DXGI format");
            pos = 148;
        } else return fail("unsupported FourCC");
    } else if ((pf & DDPF_RGB) && bits == 32 && rm == 0x000000FF && gm == 0x0000FF00 && bm == 0x00FF0000 && (am == 0xFF000000 || am == 0)) {
        fmt = DdsFormat::RGBA8;
    } else return fail("unsupported pixel format");
    if (Get32(h + 108) & 0x200) return fail("cube maps are not supported");
    DdsImage img;
    img.format = fmt; img.width = int(width); img.height = int(height);
    for (std::uint32_t i = 0; i < mipCount; ++i) {
        int w, hh; MipSize(img.width, img.height, int(i), &w, &hh);
        const std::size_t need = DdsLevelBytes(fmt, w, hh);
        if (need == 0 || size - pos < need) {
            if (i == 0) return fail("pixel data truncated");
            break;                                    // a cut-off tail: keep the levels that are complete
        }
        img.mips.emplace_back(d + pos, d + pos + need);
        pos += need;
    }
    *out = std::move(img);
    return true;
}

} // namespace okay::tex
