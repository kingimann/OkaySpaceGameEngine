#pragma once
// Helpers for the render-backend tests: hand-made draw data, a backend-neutral way to render a scene on any
// render::Backend and read the pixels back, and report lines.
#include "OkayUITestKit.hpp"
#include "RenderScenes.hpp"
#include "okay/UI/OkayUIRender.hpp"
#include "okay/UI/OkayUIRenderBackends.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace rtk {

namespace R = OkayUI::render;
using OkayUI::DrawCmd;
using OkayUI::DrawData;
using OkayUI::DrawVertex;

/// Draw data built by hand: quads with a colour, a clip and a texture key.
struct Hand {
    std::vector<DrawVertex> v;
    std::vector<int> idx;
    std::vector<DrawCmd> cmds;
    const void* atlasKey = nullptr;
    std::vector<std::uint8_t> atlas; int atlasW = 0, atlasH = 0;

    void BeginSpan(bool clipOn = false, int cx = 0, int cy = 0, int cw = 0, int ch = 0, const void* tex = nullptr) {
        DrawCmd c; c.idxOffset = (int)idx.size(); c.idxCount = 0; c.clipOn = clipOn; c.clipX = cx; c.clipY = cy; c.clipW = cw; c.clipH = ch; c.texture = tex;
        cmds.push_back(c);
    }
    void Quad(float x, float y, float w, float h, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255,
              float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
        if (cmds.empty()) BeginSpan();
        const int base = (int)v.size();
        v.push_back(DrawVertex{x, y, r, g, b, a, u0, v0});
        v.push_back(DrawVertex{x + w, y, r, g, b, a, u1, v0});
        v.push_back(DrawVertex{x + w, y + h, r, g, b, a, u1, v1});
        v.push_back(DrawVertex{x, y + h, r, g, b, a, u0, v1});
        const int ix[6] = {0, 1, 2, 0, 2, 3};
        for (int i : ix) idx.push_back(base + i);
        cmds.back().idxCount += 6;
    }
    DrawData Get() const {
        DrawData d;
        d.vertices = v.empty() ? nullptr : v.data(); d.vertexCount = (int)v.size(); d.vertexStride = (int)sizeof(DrawVertex);
        d.indices = idx.empty() ? nullptr : idx.data(); d.indexCount = (int)idx.size();
        d.cmds = cmds.empty() ? nullptr : cmds.data(); d.cmdCount = (int)cmds.size();
        if (atlasKey) { d.atlasTexture = atlasKey; d.atlasRgba = atlas.data(); d.atlasW = atlasW; d.atlasH = atlasH; }
        return d;
    }
};

inline unsigned Px(const std::vector<std::uint8_t>& img, int w, int x, int y) {
    const std::uint8_t* p = &img[((size_t)y * w + x) * 4];
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

inline double MsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

/// Copy the current OkayUI frame's draw data so it stays valid while other things are rendered.
struct Frozen {
    std::vector<DrawVertex> v; std::vector<int> idx; std::vector<DrawCmd> cmds;
    std::vector<std::uint8_t> atlas;
    DrawData d;
    explicit Frozen(const DrawData& s) {
        d = s;
        if (s.vertices && s.vertexCount > 0) { v.assign((const DrawVertex*)s.vertices, (const DrawVertex*)s.vertices + s.vertexCount); d.vertices = v.data(); }
        if (s.indices && s.indexCount > 0) { idx.assign(s.indices, s.indices + s.indexCount); d.indices = idx.data(); }
        if (s.cmds && s.cmdCount > 0) { cmds.assign(s.cmds, s.cmds + s.cmdCount); d.cmds = cmds.data(); }
        if (s.atlasRgba && s.atlasW > 0) { atlas.assign(s.atlasRgba, s.atlasRgba + (size_t)s.atlasW * s.atlasH * 4); d.atlasRgba = atlas.data(); }
    }
};

} // namespace rtk
