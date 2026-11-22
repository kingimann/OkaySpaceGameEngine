// okay::tex DDS writer / reader, written from the public file layout (see Dds.hpp).
#include "okay/Texture/Dds.hpp"
#include "okay/Texture/Texture.hpp"
#include <cstring>
#include <fstream>

namespace okay::tex {

namespace {

void Put32(std::vector<std::uint8_t>& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
std::uint32_t Get32(const std::uint8_t* p) { return p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
std::uint32_t Four(const char* s) { return Get32(reinterpret_cast<const std::uint8_t*>(s)); }

const std::uint32_t DDSD_CAPS = 0x1, DDSD_HEIGHT = 0x2, DDSD_WIDTH = 0x4, DDSD_PITCH = 0x8, DDSD_PIXELFORMAT = 0x1000,
                    DDSD_MIPMAPCOUNT = 0x20000, DDSD_LINEARSIZE = 0x80000;
const std::uint32_t DDPF_ALPHAPIXELS = 0x1, DDPF_ALPHA = 0x2, DDPF_FOURCC = 0x4, DDPF_RGB = 0x40, DDPF_LUMINANCE = 0x20000;
const std::uint32_t DDSCAPS_COMPLEX = 0x8, DDSCAPS_TEXTURE = 0x1000, DDSCAPS_MIPMAP = 0x400000;
const int kMaxDim = 16384;

bool IsBc(DdsFormat f) { return f != DdsFormat::RGBA8; }

BcFormat ToBc(DdsFormat f) { BcFormat b = BcFormat::BC1; DdsBcFormat(f, &b); return b; }

} // namespace

bool DdsBcFormat(DdsFormat f, BcFormat* out) {
    BcFormat b;
    switch (f) {
        case DdsFormat::BC1: b = BcFormat::BC1; break;
        case DdsFormat::BC2: b = BcFormat::BC2; break;
        case DdsFormat::BC3: b = BcFormat::BC3; break;
        case DdsFormat::BC4: b = BcFormat::BC4; break;
        case DdsFormat::BC5: b = BcFormat::BC5; break;
        default: return false;
    }
    if (out) *out = b;
    return true;
}

DdsFormat DdsFormatFromBc(BcFormat f) {
    switch (f) {
        case BcFormat::BC1: return DdsFormat::BC1; case BcFormat::BC2: return DdsFormat::BC2; case BcFormat::BC3: return DdsFormat::BC3;
        case BcFormat::BC4: return DdsFormat::BC4; case BcFormat::BC5: return DdsFormat::BC5;
    }
    return DdsFormat::BC1;
}

const char* DdsFormatName(DdsFormat f) {
    switch (f) {
        case DdsFormat::RGBA8: return "RGBA8"; case DdsFormat::BC1: return "BC1"; case DdsFormat::BC2: return "BC2";
        case DdsFormat::BC3: return "BC3"; case DdsFormat::BC4: return "BC4"; case DdsFormat::BC5: return "BC5";
    }
    return "?";
}

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
        case DdsFormat::BC2: Put32(out, DDPF_FOURCC); Put32(out, Four("DXT3")); for (int i = 0; i < 5; ++i) Put32(out, 0); break;
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

namespace {

// One channel of an uncompressed pixel described by a bit mask: where it starts, how many bits, scaled to 0..255.
struct MaskChannel {
    std::uint32_t mask = 0; int shift = 0; std::uint64_t max = 0;
    bool Set(std::uint32_t m) {               // false when the set bits are not one run
        mask = m; shift = 0; max = 0;
        if (m == 0) return true;
        while (!((m >> shift) & 1u)) ++shift;
        const std::uint64_t run = std::uint64_t(m) >> shift;
        if ((run & (run + 1)) != 0) return false;
        max = run;
        return true;
    }
    std::uint8_t Get(std::uint32_t px, std::uint8_t absent) const {
        if (!mask) return absent;
        const std::uint64_t v = (std::uint64_t(px) & mask) >> shift;
        return std::uint8_t((v * 255 + max / 2) / max);
    }
};

// How the stored pixels of a file are laid out: a block format, or masked pixels expanded to RGBA8.
struct Source {
    DdsFormat format = DdsFormat::RGBA8;
    bool masked = false;
    int bytesPerPixel = 0;
    bool luminance = false;
    MaskChannel r, g, b, a;
};

std::size_t StoredLevelBytes(const Source& s, int w, int h) {
    if (!s.masked) return DdsLevelBytes(s.format, w, h);
    return std::size_t(w) * std::size_t(h) * std::size_t(s.bytesPerPixel);
}

void ExpandLevel(const Source& s, const std::uint8_t* in, int w, int h, std::vector<std::uint8_t>& out) {
    const std::size_t n = std::size_t(w) * std::size_t(h);
    out.resize(n * 4);
    for (std::size_t i = 0; i < n; ++i) {
        std::uint32_t px = 0;
        for (int k = 0; k < s.bytesPerPixel; ++k) px |= std::uint32_t(in[i * s.bytesPerPixel + k]) << (8 * k);
        std::uint8_t* o = &out[i * 4];
        if (s.luminance) { o[0] = o[1] = o[2] = s.r.Get(px, 0); }
        else { o[0] = s.r.Get(px, 0); o[1] = s.g.Get(px, 0); o[2] = s.b.Get(px, 0); }
        o[3] = s.a.Get(px, 255);
    }
}

bool MasksOverlap(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    return (a & b) || (a & c) || (a & d) || (b & c) || (b & d) || (c & d);
}

} // namespace

bool ReadDds(const std::uint8_t* d, std::size_t size, DdsImage* out, std::string* error) {
    return ReadDds(d, size, out, error, DdsLimits());
}

bool ReadDds(const std::uint8_t* d, std::size_t size, DdsImage* out, std::string* error, const DdsLimits& limits) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (!d || !out) return fail("no data");
    if (size < 128) return fail("file too short");
    if (Get32(d) != Four("DDS ")) return fail("not a DDS file (missing magic)");
    const std::uint8_t* h = d + 4;
    if (Get32(h) != 124) return fail("bad header size");
    const std::uint32_t flags = Get32(h + 4);
    const std::uint32_t height = Get32(h + 8), width = Get32(h + 12);
    if (!(flags & DDSD_WIDTH) || !(flags & DDSD_HEIGHT)) return fail("size flags missing");
    const std::uint32_t maxDim = std::uint32_t(limits.maxDimension > 0 ? limits.maxDimension : 1);
    if (width == 0 || height == 0 || width > maxDim || height > maxDim) return fail("size out of range");
    if (Get32(h + 72) != 32) return fail("bad pixel format size");
    std::uint32_t mipCount = (flags & DDSD_MIPMAPCOUNT) ? Get32(h + 24) : 1;
    if (mipCount == 0) mipCount = 1;
    if (mipCount > 16) return fail("too many mip levels");
    const std::uint32_t caps2 = Get32(h + 108);
    if (caps2 & 0x200) return fail("cube maps are not supported");
    if ((caps2 & 0x200000) || ((flags & 0x800000) && Get32(h + 20) > 1)) return fail("volume textures are not supported");
    const std::uint32_t pf = Get32(h + 76), fourcc = Get32(h + 80), bits = Get32(h + 84);
    const std::uint32_t rm = Get32(h + 88), gm = Get32(h + 92), bm = Get32(h + 96), am = Get32(h + 100);
    std::size_t pos = 128;
    Source src;
    bool srgb = false;
    auto masked = [&](int bpp, std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a, bool lum) {
        src.masked = true; src.bytesPerPixel = bpp; src.luminance = lum; src.format = DdsFormat::RGBA8;
        return src.r.Set(r) && src.g.Set(g) && src.b.Set(b) && src.a.Set(a);
    };
    if (pf & DDPF_FOURCC) {
        if (fourcc == Four("DXT1")) src.format = DdsFormat::BC1;
        else if (fourcc == Four("DXT3")) src.format = DdsFormat::BC2;
        else if (fourcc == Four("DXT5")) src.format = DdsFormat::BC3;
        else if (fourcc == Four("ATI1") || fourcc == Four("BC4U")) src.format = DdsFormat::BC4;
        else if (fourcc == Four("ATI2") || fourcc == Four("BC5U")) src.format = DdsFormat::BC5;
        else if (fourcc == Four("DXT2") || fourcc == Four("DXT4")) return fail("premultiplied DXT2 / DXT4 is not supported");
        else if (fourcc == Four("BC4S") || fourcc == Four("BC5S")) return fail("signed BC4 / BC5 is not supported");
        else if (fourcc == Four("DX10")) {
            if (size < 148) return fail("file too short for the DX10 header");
            const std::uint32_t dxgi = Get32(d + 128);
            if (Get32(d + 128 + 4) != 3) return fail("only 2D textures are supported");
            if (Get32(d + 128 + 8) & 0x4) return fail("cube maps are not supported");
            if (Get32(d + 128 + 12) > 1) return fail("texture arrays are not supported");
            switch (dxgi) {
                case 71: case 72: src.format = DdsFormat::BC1; srgb = dxgi == 72; break;
                case 74: case 75: src.format = DdsFormat::BC2; srgb = dxgi == 75; break;
                case 77: case 78: src.format = DdsFormat::BC3; srgb = dxgi == 78; break;
                case 80: src.format = DdsFormat::BC4; break;
                case 83: src.format = DdsFormat::BC5; break;
                case 28: case 29: masked(4, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000, false); srgb = dxgi == 29; break;
                case 87: case 91: masked(4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, false); srgb = dxgi == 91; break;
                case 88: case 93: masked(4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0, false); srgb = dxgi == 93; break;
                case 81: case 84: return fail("signed BC4 / BC5 is not supported");
                default: return fail("unsupported DXGI format");
            }
            pos = 148;
        } else return fail("unsupported FourCC");
    } else if (pf & (DDPF_RGB | DDPF_LUMINANCE | DDPF_ALPHA)) {
        if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return fail("unsupported bits per pixel");
        const std::uint64_t fits = (std::uint64_t(1) << bits) - 1;
        const bool hasAlpha = (pf & (DDPF_ALPHAPIXELS | DDPF_ALPHA)) != 0;
        const std::uint32_t a = hasAlpha ? am : 0;
        const bool lum = (pf & DDPF_LUMINANCE) != 0;
        const std::uint32_t r = (pf & (DDPF_RGB | DDPF_LUMINANCE)) ? rm : 0;
        const std::uint32_t g = (pf & DDPF_RGB) ? gm : 0, b = (pf & DDPF_RGB) ? bm : 0;
        if ((std::uint64_t(r | g | b | a) & ~fits) != 0) return fail("pixel mask wider than the pixel");
        if (MasksOverlap(r, g, b, a)) return fail("pixel masks overlap");
        if ((r | g | b | a) == 0) return fail("pixel masks are empty");
        if (!masked(int(bits / 8), r, g, b, a, lum)) return fail("pixel mask is not one run of bits");
    } else return fail("unsupported pixel format");

    const int W = int(width), H = int(height);
    const std::uint32_t possible = std::uint32_t(MipCount(W, H));
    if (mipCount > possible) mipCount = possible;     // some writers count past 1x1: those levels cannot exist
    // the budget, before anything is allocated
    std::size_t total = 0;
    for (std::uint32_t i = 0; i < mipCount; ++i) {
        int w, hh; MipSize(W, H, int(i), &w, &hh);
        total += src.masked ? std::size_t(w) * std::size_t(hh) * 4 : DdsLevelBytes(src.format, w, hh);
    }
    if (total > limits.maxBytes) return fail("texture larger than the memory budget");

    DdsImage img;
    img.format = src.format; img.width = W; img.height = H; img.srgb = srgb;
    for (std::uint32_t i = 0; i < mipCount; ++i) {
        int w, hh; MipSize(img.width, img.height, int(i), &w, &hh);
        const std::size_t need = StoredLevelBytes(src, w, hh);
        if (need == 0 || pos > size || size - pos < need) {
            if (i == 0) return fail("pixel data truncated");
            break;                                    // a cut-off tail: keep the levels that are complete
        }
        if (src.masked) { img.mips.emplace_back(); ExpandLevel(src, d + pos, w, hh, img.mips.back()); }
        else img.mips.emplace_back(d + pos, d + pos + need);
        pos += need;
    }
    *out = std::move(img);
    return true;
}

bool ReadDdsFile(const std::string& path, DdsImage* out, std::string* error, const DdsLimits& limits) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    const std::streamoff len = f.tellg();
    if (len < 0 || std::uint64_t(len) > std::uint64_t(limits.maxBytes) + 148) { if (error) *error = "file too large"; return false; }
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(len));
    f.seekg(0);
    if (!buf.empty() && !f.read(reinterpret_cast<char*>(buf.data()), std::streamsize(buf.size()))) { if (error) *error = "cannot read " + path; return false; }
    return ReadDds(buf.data(), buf.size(), out, error, limits);
}

bool DdsLevelToRGBA8(const DdsImage& img, int level, std::vector<std::uint8_t>* rgba) {
    if (!rgba || level < 0 || level >= int(img.mips.size())) return false;
    int w, h; MipSize(img.width, img.height, level, &w, &h);
    const auto& m = img.mips[std::size_t(level)];
    BcFormat bc;
    if (!DdsBcFormat(img.format, &bc)) {
        if (m.size() != std::size_t(w) * std::size_t(h) * 4) return false;
        *rgba = m;
        return true;
    }
    *rgba = DecodeBc(m.data(), m.size(), w, h, bc);
    return !rgba->empty();
}

bool IsDdsPath(const std::string& path) {
    if (path.size() < 4) return false;
    const std::string e = path.substr(path.size() - 4);
    return e[0] == '.' && (e[1] == 'd' || e[1] == 'D') && (e[2] == 'd' || e[2] == 'D') && (e[3] == 's' || e[3] == 'S');
}

} // namespace okay::tex
