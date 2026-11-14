#pragma once
// A frame of OkayUI draw data on disk (".okd"), plus the software reference picture of it (".ref.rgba"), so a render backend
// can be checked on a machine that cannot build or run OkayUI itself (for example the Direct3D 11 backend on Windows, or
// under Wine): okayui_render_dump (any OS, writes the files) and okayui_render_d3d11_check (Windows, reads them, draws with
// Direct3D 11 and compares with the reference). Little-endian, no padding; not a stable file format.
//
//   "OKD1", int w, h, float dpi, int vertexCount, indexCount, cmdCount, imageCount,
//   u64 atlasKey, int atlasW, atlasH, [atlas RGBA8], vertices (20 bytes each), indices (int32), commands
//   (int idxOffset, idxCount, clipOn, clipX, clipY, clipW, clipH, u64 texture), then per image: u64 key, int w, h, [RGBA8].
//   The images are the host images and the fallback-font atlases the frame's commands name.
#include "okay/UI/OkayUI.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace okd {

struct Image { std::uint64_t key = 0; int w = 0, h = 0; std::vector<std::uint8_t> rgba; };
struct Frame {
    int w = 0, h = 0; float dpi = 1.0f;
    std::uint64_t atlasKey = 0; int atlasW = 0, atlasH = 0; std::vector<std::uint8_t> atlas;
    std::vector<OkayUI::DrawVertex> v; std::vector<int> idx; std::vector<OkayUI::DrawCmd> cmds;
    std::vector<Image> images;
    OkayUI::DrawData Data() const {
        OkayUI::DrawData d;
        d.vertices = v.data(); d.vertexCount = (int)v.size(); d.vertexStride = (int)sizeof(OkayUI::DrawVertex);
        d.indices = idx.data(); d.indexCount = (int)idx.size(); d.cmds = cmds.data(); d.cmdCount = (int)cmds.size();
        if (atlasKey) { d.atlasTexture = (const void*)(std::uintptr_t)atlasKey; d.atlasRgba = atlas.data(); d.atlasW = atlasW; d.atlasH = atlasH; }
        return d;
    }
};

namespace detail {
template <class T> void Put(std::FILE* f, const T& x) { std::fwrite(&x, sizeof x, 1, f); }
template <class T> bool Get(std::FILE* f, T& x) { return std::fread(&x, sizeof x, 1, f) == 1; }
inline void PutBytes(std::FILE* f, const void* p, size_t n) { if (n) std::fwrite(p, 1, n, f); }
inline bool GetBytes(std::FILE* f, void* p, size_t n) { return n == 0 || std::fread(p, 1, n, f) == n; }
}

inline bool Write(const std::string& path, const Frame& fr) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    using namespace detail;
    PutBytes(f, "OKD1", 4);
    Put<int>(f, fr.w); Put<int>(f, fr.h); Put<float>(f, fr.dpi);
    Put<int>(f, (int)fr.v.size()); Put<int>(f, (int)fr.idx.size()); Put<int>(f, (int)fr.cmds.size()); Put<int>(f, (int)fr.images.size());
    Put<std::uint64_t>(f, fr.atlasKey); Put<int>(f, fr.atlasW); Put<int>(f, fr.atlasH);
    PutBytes(f, fr.atlas.data(), fr.atlas.size());
    PutBytes(f, fr.v.data(), fr.v.size() * sizeof(OkayUI::DrawVertex));
    PutBytes(f, fr.idx.data(), fr.idx.size() * sizeof(int));
    for (const OkayUI::DrawCmd& c : fr.cmds) {
        Put<int>(f, c.idxOffset); Put<int>(f, c.idxCount); Put<int>(f, c.clipOn ? 1 : 0);
        Put<int>(f, c.clipX); Put<int>(f, c.clipY); Put<int>(f, c.clipW); Put<int>(f, c.clipH);
        Put<std::uint64_t>(f, (std::uint64_t)(std::uintptr_t)c.texture);
    }
    for (const Image& im : fr.images) { Put<std::uint64_t>(f, im.key); Put<int>(f, im.w); Put<int>(f, im.h); PutBytes(f, im.rgba.data(), im.rgba.size()); }
    std::fclose(f);
    return true;
}

inline bool Read(const std::string& path, Frame& fr) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    using namespace detail;
    char magic[4];
    bool ok = GetBytes(f, magic, 4) && std::memcmp(magic, "OKD1", 4) == 0;
    int nv = 0, ni = 0, nc = 0, nimg = 0;
    ok = ok && Get(f, fr.w) && Get(f, fr.h) && Get(f, fr.dpi) && Get(f, nv) && Get(f, ni) && Get(f, nc) && Get(f, nimg)
         && Get(f, fr.atlasKey) && Get(f, fr.atlasW) && Get(f, fr.atlasH);
    ok = ok && nv >= 0 && ni >= 0 && nc >= 0 && nimg >= 0 && nv < (1 << 24) && ni < (1 << 25) && nc < 100000 && nimg < 1000 && fr.atlasW >= 0 && fr.atlasH >= 0 && fr.atlasW < 16385 && fr.atlasH < 16385;
    if (ok) {
        fr.atlas.resize(fr.atlasKey ? (size_t)fr.atlasW * fr.atlasH * 4 : 0);
        fr.v.resize((size_t)nv); fr.idx.resize((size_t)ni); fr.cmds.resize((size_t)nc);
        ok = GetBytes(f, fr.atlas.data(), fr.atlas.size()) && GetBytes(f, fr.v.data(), fr.v.size() * sizeof(OkayUI::DrawVertex)) && GetBytes(f, fr.idx.data(), fr.idx.size() * sizeof(int));
        for (OkayUI::DrawCmd& c : fr.cmds) {
            int on = 0; std::uint64_t t = 0;
            ok = ok && Get(f, c.idxOffset) && Get(f, c.idxCount) && Get(f, on) && Get(f, c.clipX) && Get(f, c.clipY) && Get(f, c.clipW) && Get(f, c.clipH) && Get(f, t);
            c.clipOn = on != 0; c.texture = (const void*)(std::uintptr_t)t;
        }
        fr.images.resize((size_t)nimg);
        for (Image& im : fr.images) {
            ok = ok && Get(f, im.key) && Get(f, im.w) && Get(f, im.h) && im.w > 0 && im.h > 0 && im.w < 16385 && im.h < 16385;
            if (ok) { im.rgba.resize((size_t)im.w * im.h * 4); ok = GetBytes(f, im.rgba.data(), im.rgba.size()); }
        }
    }
    std::fclose(f);
    return ok;
}

} // namespace okd
