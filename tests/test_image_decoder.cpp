// Our own image decoder (Core/ImageDecode): every fixture in tests/data/images is decoded and
// compared with the pixels the reference decoders (libpng, libjpeg-turbo, ...) produce; plus the
// inflater, the encoder round trip, header-only queries, and abuse of the decoders with truncated,
// bit-flipped and absurd input (which must fail cleanly, never crash).
#include "test_framework.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Core/ImageDecode.hpp"
#include "okay/Core/ImageWrite.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

std::string DataDir() { return std::string(OKAY_SOURCE_DIR) + "/tests/data/images/"; }

std::vector<std::uint8_t> ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::uint64_t Fnv(const std::uint8_t* d, std::size_t n, std::uint64_t h = 0xcbf29ce484222325ULL) {
    for (std::size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 0x100000001b3ULL; }
    return h;
}

std::string Hex(std::uint64_t v) { char b[32]; std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(v)); return b; }

// Splitmix-style generator so the tests are deterministic everywhere.
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed) {}
    std::uint32_t Next() {
        s += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 16);
    }
};

// A decode that succeeds must hand back a consistent image.
bool Consistent(const DecodedImage& img) {
    return img.width >= 1 && img.height >= 1 && img.width <= kMaxImageDimension && img.height <= kMaxImageDimension &&
           img.rgba.size() == static_cast<std::size_t>(img.width) * img.height * 4;
}

void Put32be(std::string& s, std::uint32_t v) { for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); }

std::vector<std::uint8_t> Bytes(const std::string& s) { return std::vector<std::uint8_t>(s.begin(), s.end()); }

struct ManifestEntry { std::string kind, file, a, b, c, d; };

std::vector<ManifestEntry> ReadManifest() {
    std::vector<ManifestEntry> out;
    std::ifstream f(DataDir() + "manifest.txt");
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        ManifestEntry e;
        ss >> e.kind >> e.file >> e.a >> e.b >> e.c >> e.d;
        out.push_back(e);
    }
    return out;
}

} // namespace

int main() {
    RUN_SUITE("image_decoder");

    // ---------------------------------------------------------------- fixtures vs references
    const std::vector<ManifestEntry> manifest = ReadManifest();
    CHECK(manifest.size() > 200);
    int nExact = 0, nJpeg = 0, nGif = 0, nReject = 0;
    double worstJpegMax = 0, worstJpegMean = 0;
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> corpus;   // for the abuse tests
    for (const ManifestEntry& e : manifest) {
        const std::vector<std::uint8_t> bytes = ReadFile(DataDir() + e.file);
        if (bytes.empty()) { std::cerr << "  missing fixture " << e.file << "\n"; CHECK(!bytes.empty()); continue; }
        corpus.emplace_back(e.file, bytes);
        std::string err;
        if (e.kind == "exact") {
            ++nExact;
            DecodedImage img;
            const bool ok = DecodeImage(bytes.data(), bytes.size(), img, &err);
            if (!ok) std::cerr << "  decode failed: " << e.file << ": " << err << "\n";
            CHECK(ok);
            if (!ok) continue;
            const int w = std::atoi(e.a.c_str()), h = std::atoi(e.b.c_str());
            CHECK(img.width == w && img.height == h && Consistent(img));
            const std::string got = Hex(Fnv(img.rgba.data(), img.rgba.size()));
            if (got != e.c) std::cerr << "  pixel mismatch: " << e.file << " got " << got << " want " << e.c << "\n";
            CHECK(got == e.c);
            // Header-only query agrees.
            int iw = 0, ih = 0;
            CHECK(GetImageInfo(bytes.data(), bytes.size(), &iw, &ih) && iw == w && ih == h);
        } else if (e.kind == "jpeg") {
            ++nJpeg;
            const std::vector<std::uint8_t> refBytes = ReadFile(DataDir() + e.a);
            DecodedImage ref, img;
            CHECK(DecodeImage(refBytes.data(), refBytes.size(), ref, &err));
            const bool ok = DecodeImage(bytes.data(), bytes.size(), img, &err);
            if (!ok) std::cerr << "  decode failed: " << e.file << ": " << err << "\n";
            CHECK(ok);
            if (!ok || img.rgba.size() != ref.rgba.size()) { CHECK(ok && img.rgba.size() == ref.rgba.size()); continue; }
            CHECK(img.width == ref.width && img.height == ref.height);
            int maxd = 0; double sum = 0;
            for (std::size_t i = 0; i < img.rgba.size(); ++i) {
                const int d = std::abs(static_cast<int>(img.rgba[i]) - static_cast<int>(ref.rgba[i]));
                maxd = std::max(maxd, d); sum += d;
            }
            const double mean = sum / static_cast<double>(img.rgba.size());
            worstJpegMax = std::max(worstJpegMax, static_cast<double>(maxd));
            worstJpegMean = std::max(worstJpegMean, mean);
            if (maxd > 4 || mean > 0.25) std::cerr << "  jpeg error too large: " << e.file << " max " << maxd << " mean " << mean << "\n";
            CHECK(maxd <= 4);       // libjpeg's integer IDCT vs our exact one: measured max is 3
            CHECK(mean <= 0.25);    // measured worst is about 0.11
        } else if (e.kind == "gif") {
            ++nGif;
            DecodedAnimation anim;
            const bool ok = DecodeGifFrames(bytes.data(), bytes.size(), anim, &err);
            if (!ok) std::cerr << "  decode failed: " << e.file << ": " << err << "\n";
            CHECK(ok);
            if (!ok) continue;
            CHECK(anim.FrameCount() == std::atoi(e.a.c_str()));
            CHECK(anim.width == std::atoi(e.b.c_str()) && anim.height == std::atoi(e.c.c_str()));
            std::uint64_t h = Fnv(anim.frames.data(), anim.frames.size());
            for (int d : anim.delaysMs) { std::uint8_t b[4]; std::memcpy(b, &d, 4); h = Fnv(b, 4, h); }
            const std::string got = Hex(h);
            if (got != e.d) std::cerr << "  animation mismatch: " << e.file << " got " << got << " want " << e.d << "\n";
            CHECK(got == e.d);
            // The single-image path returns frame 0.
            DecodedImage first;
            CHECK(DecodeImage(bytes.data(), bytes.size(), first, &err));
            const std::size_t fb = static_cast<std::size_t>(anim.width) * anim.height * 4;
            CHECK(first.rgba.size() == fb && std::memcmp(first.rgba.data(), anim.frames.data(), fb) == 0);
        } else if (e.kind == "reject") {
            ++nReject;
            DecodedImage img;
            img.width = 77;
            CHECK(!DecodeImage(bytes.data(), bytes.size(), img, &err));
            CHECK(!err.empty());
            CHECK(img.width == 0 && img.rgba.empty());
        }
    }
    std::cout << "  fixtures: " << nExact << " exact, " << nJpeg << " jpeg (worst max err " << worstJpegMax
              << ", worst mean " << worstJpegMean << "), " << nGif << " animated gif, " << nReject << " rejected\n";
    CHECK(nExact > 150 && nJpeg > 50 && nGif >= 5 && nReject >= 4);

    // Image::Load goes through the same decoder and keeps its error reporting.
    {
        Image img; std::string err;
        CHECK(img.Load(DataDir() + "png_rgba8_16x16.png", &err) && img.Width() == 16 && img.Height() == 16 && img.Valid());
        CHECK(!img.Load(DataDir() + "does_not_exist.png", &err) && !err.empty() && !img.Valid());
        CHECK(!img.Load(DataDir() + "manifest.txt", &err) && !err.empty());
    }

    // ---------------------------------------------------------------- format detection
    {
        auto fmt = [&](const char* f) { const auto b = ReadFile(DataDir() + f); return DetectImageFormat(b.data(), b.size()); };
        CHECK(fmt("png_rgb8_16x16.png") == ImageFormat::PNG);
        CHECK(fmt("jpg_base_420_40x30.jpg") == ImageFormat::JPEG);
        CHECK(fmt("bmp_rgb24_7x6.bmp") == ImageFormat::BMP);
        CHECK(fmt("tga_rgb24_raw_5x3.tga") == ImageFormat::TGA);
        CHECK(fmt("gif_static_7x5.gif") == ImageFormat::GIF);
        CHECK(fmt("hdr_8x4.hdr") == ImageFormat::HDR);
        CHECK(fmt("psd_rgb8_none_5x3.psd") == ImageFormat::PSD);
        CHECK(fmt("manifest.txt") == ImageFormat::Unknown);
        CHECK(DetectImageFormat(nullptr, 0) == ImageFormat::Unknown);
    }

    // ---------------------------------------------------------------- inflate
    {
        Rng rng(7);
        std::vector<std::vector<std::uint8_t>> inputs;
        inputs.push_back({});
        inputs.push_back({42});
        inputs.push_back(std::vector<std::uint8_t>(100000, 0x55));                      // long matches, overlap copies
        std::vector<std::uint8_t> noise(70000);
        for (auto& b : noise) b = static_cast<std::uint8_t>(rng.Next());
        inputs.push_back(noise);                                                         // incompressible: stored blocks
        std::vector<std::uint8_t> text;
        for (int i = 0; i < 6000; ++i) for (const char* w = "okay space engine "; *w; ++w) text.push_back(static_cast<std::uint8_t>(*w + (i % 7 == 0)));
        inputs.push_back(text);                                                          // dynamic Huffman
        std::vector<std::uint8_t> mixed;
        for (int i = 0; i < 5000; ++i) { mixed.push_back(static_cast<std::uint8_t>(i % 13)); if (i % 97 == 0) for (int k = 0; k < 40; ++k) mixed.push_back(static_cast<std::uint8_t>(rng.Next())); }
        inputs.push_back(mixed);
        for (const auto& in : inputs) {
            const std::vector<std::uint8_t> z = ImageWrite::ZlibCompress(in.data(), in.size());
            std::vector<std::uint8_t> out;
            CHECK(Inflate(z.data(), z.size(), true, out) && out == in);
            const std::vector<std::uint8_t> raw = ImageWrite::Deflate(in.data(), in.size());
            CHECK(Inflate(raw.data(), raw.size(), false, out) && out == in);
            if (!in.empty()) {
                CHECK(!Inflate(z.data(), z.size(), true, out, in.size() - 1));   // output cap: decompression bombs
                std::vector<std::uint8_t> cut(z.begin(), z.begin() + static_cast<std::ptrdiff_t>(z.size() / 2));
                CHECK(!Inflate(cut.data(), cut.size(), true, out) && out.empty());   // truncated
            }
        }
        // Hand-made streams: stored block, fixed-Huffman "abc", and malformed ones.
        const std::uint8_t stored[] = {0x78, 0x01, 0x01, 0x03, 0x00, 0xFC, 0xFF, 'a', 'b', 'c', 0, 0, 0, 0};
        std::vector<std::uint8_t> out;
        CHECK(Inflate(stored, sizeof(stored), true, out) && out.size() == 3 && out[0] == 'a' && out[2] == 'c');
        const std::uint8_t badLen[] = {0x78, 0x01, 0x01, 0x03, 0x00, 0xFC, 0xFE, 'a', 'b', 'c'};
        CHECK(!Inflate(badLen, sizeof(badLen), true, out));
        const std::uint8_t badHdr[] = {0x79, 0x01, 0x01, 0x00, 0x00, 0xFF, 0xFF};
        CHECK(!Inflate(badHdr, sizeof(badHdr), true, out));
        const std::uint8_t reserved[] = {0x78, 0x01, 0x07};   // block type 3
        CHECK(!Inflate(reserved, sizeof(reserved), true, out));
        const std::uint8_t farBack[] = {0x03, 0x02};         // fixed block, distance code before any output
        CHECK(!Inflate(farBack, sizeof(farBack), false, out));
        CHECK(!Inflate(nullptr, 0, true, out));
        // Random garbage never crashes and never produces more than the cap.
        for (int i = 0; i < 3000; ++i) {
            std::vector<std::uint8_t> g(1 + rng.Next() % 64);
            for (auto& b : g) b = static_cast<std::uint8_t>(rng.Next());
            Inflate(g.data(), g.size(), (i & 1) != 0, out, 1 << 16);
            CHECK(out.size() <= (1u << 16));
        }
    }

    // ---------------------------------------------------------------- encoder -> decoder round trip
    {
        Rng rng(99);
        for (int channels = 1; channels <= 4; ++channels) {
            for (const auto& wh : {std::pair<int, int>{1, 1}, {3, 7}, {64, 33}, {200, 5}}) {
                const int w = wh.first, h = wh.second;
                std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * channels);
                for (std::size_t i = 0; i < px.size(); ++i) px[i] = static_cast<std::uint8_t>((i * 7) / (channels + 1) + (rng.Next() & 15));
                const auto png = ImageWrite::EncodePNG(w, h, channels, px.data());
                DecodedImage img;
                CHECK(DecodeImage(png.data(), png.size(), img) && img.width == w && img.height == h && Consistent(img));
                bool same = img.rgba.size() == static_cast<std::size_t>(w) * h * 4;
                for (int i = 0; same && i < w * h; ++i) {
                    const std::uint8_t* s = &px[static_cast<std::size_t>(i) * channels];
                    const std::uint8_t* d = &img.rgba[static_cast<std::size_t>(i) * 4];
                    const std::uint8_t r = s[0], g = channels >= 3 ? s[1] : s[0], b = channels >= 3 ? s[2] : s[0];
                    const std::uint8_t a = channels == 4 ? s[3] : (channels == 2 ? s[1] : 255);
                    same = d[0] == r && d[1] == g && d[2] == b && d[3] == a;
                }
                CHECK(same);
            }
        }
    }

    // ---------------------------------------------------------------- tolerant and strict corners
    {
        std::string err;
        DecodedImage img;
        // PNG: wrong CRCs and trailing junk after IEND still decode (only the pixel data is validated).
        auto png = ReadFile(DataDir() + "png_rgb8_16x16.png");
        auto bad = png;
        for (std::size_t i = 8 + 8 + 13; i < 8 + 8 + 13 + 4; ++i) bad[i] ^= 0xFF;   // IHDR CRC
        CHECK(DecodeImage(bad.data(), bad.size(), img, &err) && img.width == 16);
        auto junk = png; junk.insert(junk.end(), 37, 0xAB);
        CHECK(DecodeImage(junk.data(), junk.size(), img, &err) && img.width == 16);
        // PNG: unknown critical chunk is refused, unknown ancillary chunk is skipped.
        std::string c;
        {
            auto withChunk = [&](const char* type) {
                std::vector<std::uint8_t> v(png.begin(), png.begin() + 8 + 8 + 13 + 4);   // signature + IHDR
                const std::uint8_t chunk[] = {0, 0, 0, 1, (std::uint8_t)type[0], (std::uint8_t)type[1], (std::uint8_t)type[2], (std::uint8_t)type[3], 9, 0, 0, 0, 0};
                v.insert(v.end(), chunk, chunk + sizeof(chunk));
                v.insert(v.end(), png.begin() + 8 + 8 + 13 + 4, png.end());
                return v;
            };
            const auto crit = withChunk("ABCD"), anc = withChunk("abcd");
            CHECK(!DecodeImage(crit.data(), crit.size(), img, &err));
            CHECK(DecodeImage(anc.data(), anc.size(), img, &err) && img.width == 16);
        }
        // PNG: a bad filter byte, a palette image without PLTE, and zero dimensions.
        {
            auto flt = ImageWrite::EncodePNG(4, 4, 3, std::vector<std::uint8_t>(48, 1).data());
            // Rebuild with a bogus filter: decode the zlib stream, patch, re-encode.
            std::vector<std::uint8_t> raw;
            const std::size_t idatAt = 8 + 8 + 13 + 4;   // chunk length(4) type(4) then data
            const std::size_t len = (std::size_t(flt[idatAt]) << 24) | (flt[idatAt + 1] << 16) | (flt[idatAt + 2] << 8) | flt[idatAt + 3];
            CHECK(Inflate(&flt[idatAt + 8], len, true, raw));
            raw[0] = 9;   // filter type 9
            const auto z = ImageWrite::ZlibCompress(raw.data(), raw.size());
            std::string s(reinterpret_cast<const char*>(flt.data()), idatAt);
            Put32be(s, static_cast<std::uint32_t>(z.size()));
            s += "IDAT";
            s.append(reinterpret_cast<const char*>(z.data()), z.size());
            s.append(4, '\0');
            Put32be(s, 0); s += "IEND"; s.append(4, '\0');
            const auto b = Bytes(s);
            CHECK(!DecodeImage(b.data(), b.size(), img, &err));
        }
        auto p0 = png;   // width = 0
        p0[8 + 8 + 0] = p0[8 + 8 + 1] = p0[8 + 8 + 2] = p0[8 + 8 + 3] = 0;
        CHECK(!DecodeImage(p0.data(), p0.size(), img, &err));
        // JPEG: trailing junk is ignored; a truncated scan, a missing table and garbage are refused.
        auto jpg = ReadFile(DataDir() + "jpg_base_420_40x30.jpg");
        auto jt = jpg; jt.insert(jt.end(), 100, 0x00);
        CHECK(DecodeImage(jt.data(), jt.size(), img, &err) && img.width == 40 && img.height == 30);
        auto jnoeoi = jpg; jnoeoi.resize(jnoeoi.size() - 2);
        CHECK(DecodeImage(jnoeoi.data(), jnoeoi.size(), img, &err) && img.width == 40);   // EOI is optional
        auto jcut = jpg; jcut.resize(jcut.size() / 2);
        CHECK(!DecodeImage(jcut.data(), jcut.size(), img, &err) && !err.empty());
        std::vector<std::uint8_t> soiOnly = {0xFF, 0xD8, 0xFF, 0xD9};
        CHECK(!DecodeImage(soiOnly.data(), soiOnly.size(), img, &err));
        // CMYK, arithmetic and 12-bit JPEGs have clear messages.
        for (const char* f : {"jpg_cmyk_reject_40x30.jpg", "jpg_arith_reject.jpg", "jpg_12bit_reject.jpg"}) {
            const auto b = ReadFile(DataDir() + f);
            CHECK(!DecodeImage(b.data(), b.size(), img, &err) && err.size() > 8);
        }
        // Empty / tiny / null input.
        CHECK(!DecodeImage(nullptr, 0, img, &err) && !err.empty());
        const std::uint8_t one = 0x42;
        CHECK(!DecodeImage(&one, 1, img, &err));
        CHECK(!GetImageInfo(&one, 1, nullptr, nullptr));
        CHECK(!DecodeImageFile(DataDir() + "nonexistent", img, &err));
    }

    // ---------------------------------------------------------------- absurd headers do not allocate
    {
        std::string err;
        DecodedImage img;
        // PNG claiming 65535x65535, and 16384x16384 with a 100-byte body.
        for (std::uint32_t dim : {65535u, 16384u, 0x7FFFFFFFu}) {
            std::string s("\x89PNG\r\n\x1a\n", 8);
            Put32be(s, 13); s += "IHDR"; Put32be(s, dim); Put32be(s, dim); s += std::string("\x08\x06\x00\x00\x00", 5); Put32be(s, 0);
            Put32be(s, 4); s += "IDAT"; s += std::string(8, 'x'); Put32be(s, 0);
            const auto b = Bytes(s);
            CHECK(!DecodeImage(b.data(), b.size(), img, &err));
            int w = 0, h = 0;
            CHECK(dim > static_cast<std::uint32_t>(kMaxImageDimension) ? !GetImageInfo(b.data(), b.size(), &w, &h) : GetImageInfo(b.data(), b.size(), &w, &h));
        }
        // BMP, TGA, GIF, PSD, HDR and JPEG headers with giant sizes.
        std::string bmp("BM", 2); bmp += std::string(12, '\0'); bmp[10] = 54;
        bmp.resize(14);
        { std::string d(40, '\0'); d[0] = 40; d[4] = (char)0xFF; d[5] = (char)0xFF; d[8] = (char)0xFF; d[9] = (char)0xFF; d[12] = 1; d[14] = 24; bmp += d; bmp += std::string(64, 'x'); }
        { const auto b = Bytes(bmp); CHECK(!DecodeImage(b.data(), b.size(), img, &err)); }
        std::string tga(40, '\0'); tga[2] = 2; tga[12] = 0; tga[13] = 0x40; tga[14] = 0; tga[15] = 0x40; tga[16] = 32;
        { const auto b = Bytes(tga); CHECK(!DecodeImage(b.data(), b.size(), img, &err) && !err.empty()); }
        std::string gif = "GIF89a"; gif += std::string("\x00\x40\x00\x40\x00\x00\x00", 7); gif += std::string(30, '\0');
        { const auto b = Bytes(gif); CHECK(!DecodeImage(b.data(), b.size(), img, &err)); DecodedAnimation an; CHECK(!DecodeGifFrames(b.data(), b.size(), an, &err)); }
        std::string psd = "8BPS"; psd += std::string("\x00\x01\x00\x00\x00\x00\x00\x00\x00\x03", 10); Put32be(psd, 60000); Put32be(psd, 60000); psd += std::string("\x00\x08\x00\x03", 4);
        { const auto b = Bytes(psd); CHECK(!DecodeImage(b.data(), b.size(), img, &err)); }
        std::string hdr = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 60000 +X 60000\n"; hdr += std::string(30, '\0');
        { const auto b = Bytes(hdr); CHECK(!DecodeImage(b.data(), b.size(), img, &err)); }
        std::vector<std::uint8_t> jp = {0xFF, 0xD8, 0xFF, 0xC0, 0, 17, 8, 0xFF, 0xFF, 0xFF, 0xFF, 3, 1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1};
        CHECK(!DecodeImage(jp.data(), jp.size(), img, &err));
        // Valid 16384-wide strip (within limits) with a tiny body is refused by the ratio guard, not by allocating.
        std::vector<std::uint8_t> strip = ReadFile(DataDir() + "gif_static_32x20.gif");
        strip[6] = 0x00; strip[7] = 0x40; strip[8] = 0x00; strip[9] = 0x40;   // canvas 16384 x 16384
        CHECK(!DecodeImage(strip.data(), strip.size(), img, &err));
    }

    // ---------------------------------------------------------------- abuse: truncation and bit flips
    {
        Rng rng(2024);
        long decodes = 0, accepted = 0;
        for (const auto& item : corpus) {
            const std::vector<std::uint8_t>& orig = item.second;
            if (orig.size() > 12000) continue;
            // Every prefix length for small files, ~40 evenly spread ones for larger ones.
            const std::size_t step = orig.size() <= 300 ? 1 : orig.size() / 40;
            for (std::size_t cut = 0; cut < orig.size(); cut += step) {
                DecodedImage img; std::string err;
                const bool ok = DecodeImage(orig.data(), cut, img, &err);
                ++decodes;
                if (ok) { ++accepted; CHECK(Consistent(img)); }
                else CHECK(img.rgba.empty() && img.width == 0 && !err.empty());
                DecodedAnimation an;
                if (DecodeGifFrames(orig.data(), cut, an, &err)) CHECK(an.frames.size() == static_cast<std::size_t>(an.FrameCount()) * an.width * an.height * 4);
            }
            // Random single and multi byte corruptions.
            for (int m = 0; m < 120; ++m) {
                std::vector<std::uint8_t> v = orig;
                const int flips = 1 + (m % 4);
                for (int k = 0; k < flips; ++k) {
                    const std::size_t at = rng.Next() % v.size();
                    switch (rng.Next() % 3) {
                        case 0: v[at] ^= static_cast<std::uint8_t>(1u << (rng.Next() % 8)); break;
                        case 1: v[at] = static_cast<std::uint8_t>(rng.Next()); break;
                        default: v[at] = (rng.Next() & 1) ? 0xFF : 0x00; break;
                    }
                }
                DecodedImage img; std::string err;
                const bool ok = DecodeImage(v.data(), v.size(), img, &err);
                ++decodes;
                if (ok) { ++accepted; CHECK(Consistent(img)); } else CHECK(img.rgba.empty() && !err.empty());
                int w = 0, h = 0;
                GetImageInfo(v.data(), v.size(), &w, &h);
                DecodedAnimation an;
                DecodeGifFrames(v.data(), v.size(), an, &err);
            }
        }
        std::cout << "  abuse: " << decodes << " mangled inputs, " << accepted << " still decoded, none crashed\n";
        CHECK(decodes > 20000);
    }

    TEST_MAIN_RESULT();
}
