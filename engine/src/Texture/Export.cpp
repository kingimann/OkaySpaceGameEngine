// okay::tex export glue; see Export.hpp.
#include "okay/Texture/Export.hpp"
#include "okay/Graphics/Image.hpp"
#include <cstdio>
#include <fstream>

namespace okay::tex {

BcFormat ChooseBcFormat(const std::uint8_t* rgba, int w, int h, bool normalMap) {
    if (normalMap) return BcFormat::BC5;
    if (rgba && w > 0 && h > 0)
        for (std::size_t i = 0; i < std::size_t(w) * h; ++i) if (rgba[i * 4 + 3] != 255) return BcFormat::BC3;
    return BcFormat::BC1;
}

bool BuildDds(const std::uint8_t* rgba, int w, int h, const TextureBuildOptions& opt, DdsImage* out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (!rgba || w <= 0 || h <= 0 || !out) return fail("The picture is empty.");
    if (w > 16384 || h > 16384) return fail("The picture is larger than 16384 pixels on a side.");
    std::vector<std::vector<std::uint8_t>> levels;
    if (opt.mips) levels = GenerateMipsRGBA8(rgba, w, h, opt.mip);
    else levels.emplace_back(rgba, rgba + std::size_t(w) * h * 4);
    if (levels.empty()) return fail("Could not build the mip chain.");
    DdsImage img;
    img.width = w; img.height = h;
    if (opt.compress) {
        BcFormat f = opt.autoFormat ? ChooseBcFormat(rgba, w, h, opt.mip.normalMap) : opt.format;
        img.format = f == BcFormat::BC1 ? DdsFormat::BC1 : f == BcFormat::BC3 ? DdsFormat::BC3 : f == BcFormat::BC4 ? DdsFormat::BC4 : DdsFormat::BC5;
        for (std::size_t l = 0; l < levels.size(); ++l) {
            int lw, lh; MipSize(w, h, int(l), &lw, &lh);
            img.mips.push_back(EncodeBc(levels[l].data(), lw, lh, f, opt.encode));
            if (img.mips.back().empty()) return fail("Compression failed.");
        }
    } else {
        img.format = DdsFormat::RGBA8;
        img.mips = std::move(levels);
    }
    *out = std::move(img);
    return true;
}

std::string DdsPathFor(const std::string& imagePath, bool compressed) {
    std::string base = imagePath;
    std::size_t dot = base.find_last_of('.'), slash = base.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) base.erase(dot);
    return base + (compressed ? ".dds" : ".mips.dds");
}

static bool WriteFile(const std::string& path, const void* data, std::size_t n) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(static_cast<const char*>(data), std::streamsize(n));
    return bool(f);
}

bool BuildDdsFile(const std::string& imagePath, std::string* outPath, const TextureBuildOptions& opt, std::string* error) {
    okay::Image im;
    std::string e;
    if (!im.Load(imagePath, &e)) { if (error) *error = "Could not read the picture: " + e; return false; }
    DdsImage dds;
    if (!BuildDds(im.Data(), im.Width(), im.Height(), opt, &dds, error)) return false;
    std::vector<std::uint8_t> bytes = WriteDds(dds);
    if (bytes.empty()) { if (error) *error = "Could not build the DDS file."; return false; }
    std::string path = (outPath && !outPath->empty()) ? *outPath : DdsPathFor(imagePath, opt.compress);
    if (!WriteFile(path, bytes.data(), bytes.size())) { if (error) *error = "Could not write " + path; return false; }
    if (outPath) *outPath = path;
    return true;
}

int WriteMipPngs(const std::string& imagePath, const std::string& prefix, const MipOptions& opt, std::string* error) {
    okay::Image im;
    std::string e;
    if (!im.Load(imagePath, &e)) { if (error) *error = "Could not read the picture: " + e; return 0; }
    auto chain = GenerateMipsRGBA8(im.Data(), im.Width(), im.Height(), opt);
    if (chain.empty()) { if (error) *error = "Could not build the mip chain."; return 0; }
    int n = 0;
    for (std::size_t l = 0; l < chain.size(); ++l) {
        int w, h; MipSize(im.Width(), im.Height(), int(l), &w, &h);
        okay::Image out; out.Set(w, h, chain[l]);
        if (!out.SavePNG(prefix + "_" + std::to_string(l) + ".png")) { if (error) *error = "Could not write " + prefix + "_" + std::to_string(l) + ".png"; return 0; }
        ++n;
    }
    return n;
}

bool BuildAtlasFiles(const std::vector<std::string>& paths, const std::string& sheetPng, const std::string& manifestJson,
                     const AtlasOptions& opt, std::string* error) {
    std::vector<okay::Image> imgs(paths.size());
    std::vector<AtlasItem> items;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        std::string e;
        if (!imgs[i].Load(paths[i], &e)) { if (error) *error = "Could not read " + paths[i] + ": " + e; return false; }
        std::string name = paths[i];
        std::size_t slash = name.find_last_of("/\\"); if (slash != std::string::npos) name.erase(0, slash + 1);
        std::size_t dot = name.find_last_of('.'); if (dot != std::string::npos) name.erase(dot);
        items.push_back({name, imgs[i].Width(), imgs[i].Height()});
    }
    AtlasResult r = PackAtlas(items, opt);
    if (!r.ok) {
        if (error) {
            *error = "Not everything fits in " + std::to_string(opt.maxWidth) + "x" + std::to_string(opt.maxHeight) + " (" +
                     std::to_string(r.failed.size()) + " picture(s) left out";
            if (!r.failed.empty()) *error += ", first: " + items[std::size_t(r.failed[0])].name;
            *error += ").";
        }
        return false;
    }
    std::vector<AtlasImage> ai;
    for (auto& im : imgs) ai.push_back({im.Data(), im.Width(), im.Height()});
    std::vector<std::uint8_t> sheet = ComposeAtlasRGBA8(r, ai, opt.bleed);
    if (sheet.empty()) { if (error) *error = "Could not compose the sheet."; return false; }
    okay::Image out; out.Set(r.width, r.height, std::move(sheet));
    if (!out.SavePNG(sheetPng)) { if (error) *error = "Could not write " + sheetPng; return false; }
    std::string imageName = sheetPng;
    std::size_t slash = imageName.find_last_of("/\\"); if (slash != std::string::npos) imageName.erase(0, slash + 1);
    const std::string js = AtlasToJson(r, imageName, opt);
    if (!WriteFile(manifestJson, js.data(), js.size())) { if (error) *error = "Could not write " + manifestJson; return false; }
    return true;
}

} // namespace okay::tex
