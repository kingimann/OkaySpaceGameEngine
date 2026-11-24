#include "okay/Render/GLRenderer.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/Culling.hpp"
#include "okay/Render/MeshHash.hpp"
#include "okay/Render/Visibility.hpp"
#include "okay/Components/LODGroup.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // GetCachedTexture: shares the texture cache
#include "okay/Render/ShadowSetup.hpp"
#include "okay/Render/Atmosphere.hpp"         // sky palette, fog modes, post settings
#include "okay/Render/PostQualityMath.hpp"   // shared bloom soft knee + AO power (UNVERIFIED on a GPU)
#include "okay/Scene/Transform.hpp"
#include "okay/Components/SkinnedMesh.hpp"    // skinned meshes: VB cache keyed on SkinnedMesh::version
#include "okay/Components/Character.hpp"
#include <utility>
#include "okay/Core/Log.hpp"
#include "okay/Debug/MemoryTracker.hpp"   // "Textures (GPU)": bytes of uploaded textures
#include "okay/Core/Time.hpp"
#include "okay/Render/OkayShader.hpp"     // Custom (.okayshader) surfaces

#include "GLRendererInternal.hpp"

namespace okay {
using namespace gl_render;

void GLRenderer::SwapSizeSet(SizeSet& s) {
    unsigned int* f[26] = { &m_fbo, &m_colorRb, &m_depthRb, &m_resolveFbo, &m_resolveTex,
                            &m_bloomFboA, &m_bloomTexA, &m_bloomFboB, &m_bloomTexB,
                            &m_ssaoDepthFbo, &m_ssaoDepthTex, &m_ssaoDepthRb,
                            &m_ssaoFboA, &m_ssaoTexA, &m_ssaoFboB, &m_ssaoTexB,
                            &m_outFbo, &m_outTex, &m_pingFbo[0], &m_pingTex[0], &m_pingFbo[1], &m_pingTex[1],
                            &m_bloomFboC, &m_bloomTexC, &m_bloomFboD, &m_bloomTexD };
    for (int i = 0; i < 26; ++i) std::swap(*f[i], s.ids[i]);
    std::swap(m_w, s.w); std::swap(m_h, s.h); std::swap(m_samples, s.samples);
    std::swap(m_bloomW, s.bloomW); std::swap(m_bloomH, s.bloomH);
    std::swap(m_ssaoW, s.ssaoW); std::swap(m_ssaoH, s.ssaoH);
    std::swap(m_postW, s.postW); std::swap(m_postH, s.postH);
    std::swap(m_hdr, s.hdr); std::swap(m_postHdr, s.postHdr);
}

void GLRenderer::DeleteSizeSet(SizeSet& s) {
    SwapSizeSet(s);                    // live <- s
    DestroyTargets(); DestroyBloom(); DestroySsao(); DestroyPostTargets();
    SwapSizeSet(s);                    // live back, s <- emptied
}

bool GLRenderer::EnsureTargets(int w, int h, int samples, bool hdr) {
    ++m_targetUse;
    if (hdr && m_hdrUnsupported) hdr = false;
    if (m_fbo && m_w == w && m_h == h && m_samples == samples && m_hdr == hdr) return true;
    // Park the live set, then swap in a parked one of this size if there is one.
    if (m_fbo) {
        SizeSet park;
        SwapSizeSet(park);
        park.lastUse = m_targetUse;
        m_parkedSets.push_back(park);
    }
    for (std::size_t i = 0; i < m_parkedSets.size(); ++i) {
        SizeSet& ps = m_parkedSets[i];
        if (ps.w != w || ps.h != h || ps.samples != samples || ps.hdr != hdr) continue;
        DestroyTargets(); DestroyBloom(); DestroySsao(); DestroyPostTargets();   // (already empty)
        SwapSizeSet(ps);
        m_parkedSets.erase(m_parkedSets.begin() + (std::ptrdiff_t)i);
        return true;
    }
    while (m_parkedSets.size() > 4) {   // a resized view leaves a trail of stale sizes
        std::size_t oldest = 0;
        for (std::size_t i = 1; i < m_parkedSets.size(); ++i)
            if (m_parkedSets[i].lastUse < m_parkedSets[oldest].lastUse) oldest = i;
        DeleteSizeSet(m_parkedSets[oldest]);
        m_parkedSets.erase(m_parkedSets.begin() + (std::ptrdiff_t)oldest);
    }
    DestroyTargets();   // size/sample/format change -> rebuild ONLY the render targets
    // The post chain renders into a float (RGBA16F) target so highlights above
    // 1.0 survive for bloom and the tonemap. A GPU that can't build one falls
    // back to RGBA8 for good (the chain then works on display-range colour).
    const bool wantHdr = hdr;
    for (int attempt = 0; attempt < 2; ++attempt) {
        hdr = wantHdr && attempt == 0;
        m_w = w; m_h = h; m_samples = samples < 1 ? 1 : samples; m_hdr = hdr;
        const GLenum fmt = hdr ? GL_RGBA16F : GL_RGBA8;
        while (g.GetError() != 0) {}
        g.GenFramebuffers(1, &m_fbo);
        g.BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        g.GenRenderbuffers(1, &m_colorRb);
        g.BindRenderbuffer(GL_RENDERBUFFER, m_colorRb);
        g.RenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, fmt, w, h);
        g.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_colorRb);
        g.GenRenderbuffers(1, &m_depthRb);
        g.BindRenderbuffer(GL_RENDERBUFFER, m_depthRb);
        g.RenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_DEPTH_COMPONENT24, w, h);
        g.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRb);
        bool ok = g.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (ok) {
            g.GenFramebuffers(1, &m_resolveFbo);
            g.BindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
            g.GenTextures(1, &m_resolveTex);
            g.BindTexture(GL_TEXTURE_2D, m_resolveTex);
            g.TexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, GL_RGBA, hdr ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_resolveTex, 0);
            ok = g.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }
        if (ok && g.GetError() != 0) ok = false;
        g.BindFramebuffer(GL_FRAMEBUFFER, 0);
        if (ok) return true;
        DestroyTargets();
        if (hdr) { m_hdrUnsupported = true; Log::Warning("[gl] float render target unavailable; post-processing runs on RGBA8"); continue; }
        Log::Error("[gl] MSAA / resolve FBO incomplete");
        return false;
    }
    return false;
}

// GL memory of uploaded textures, for the Profiler and the F3 numbers (MemoryTracker "Textures (GPU)").
int gl_render::GpuTextureCategory() { static const int id = MemoryTracker::Global().Category("Textures (GPU)"); return id; }

void GLRenderer::FreeTexture(const std::string& name) {
    auto it = m_texCache.find(name);
    if (it == m_texCache.end()) return;
    if (it->second) { g.DeleteTextures(1, &it->second); m_texInfo.erase(it->second); }
    auto ui = m_uploadInfo.find(name);
    if (ui != m_uploadInfo.end()) {
        if (ui->second.gpuBytes) MemoryTracker::Global().Remove(GpuTextureCategory(), std::int64_t(ui->second.gpuBytes), 1);
        m_uploadInfo.erase(ui);
    }
    m_texCache.erase(it);
}

// The GL texture for a material texture name. The pixels come from the shared texture library (textures::ForRender,
// TextureAsset.hpp), so every renderer draws the same decode; this keeps only the GL object, and uploads again when the
// asset's serial changes (hot reload, reimport, a painted terrain splat). Returns 0 when the texture is missing.
unsigned int GLRenderer::TextureFor(const std::string& name) {
    if (name.empty()) return 0;
    if (m_texGen != TextureCacheGeneration() || m_texLibGen != textures::Generation()) {
        // The library let go of some names (Drop, InvalidateAll, a budget trim): free our copies of those.
        std::vector<std::string> gone;
        for (const auto& kv : m_texCache) if (!textures::IsHeld(kv.first)) gone.push_back(kv.first);
        for (const std::string& n : gone) FreeTexture(n);
        m_texGen = TextureCacheGeneration();
        m_texLibGen = textures::Generation();
    }
    const TextureAsset* asset = textures::ForRender(name);
    const std::uint64_t serial = asset ? asset->serial : 0;
    auto it = m_texCache.find(name);
    if (it != m_texCache.end()) {
        auto ui = m_uploadInfo.find(name);
        const std::uint64_t had = ui != m_uploadInfo.end() ? ui->second.serial : 0;
        if (had == serial) return it->second;
        FreeTexture(name);   // a new version of the picture: upload it again
    }
    // The picture's own Filter / Wrap / Mipmaps / anisotropy import settings (defaults: repeat, trilinear, mipmapped).
    TextureSampling ts = asset ? TextureSamplingOf(*asset) : TextureSampling();
    TextureUploadInfo info;
    info.serial = serial;
    unsigned int tex = 0;
    if (asset) {
        // A .dds file, or the compressed import copy of a picture set to `compress`: its own levels, kept compressed when
        // the GPU takes the format (see GpuTexture.hpp). Anything else: the asset's RGBA8 pixels and our own mip chain.
        tex = UploadDdsTexture(name, ts, info);
        if (tex) {
            info.uploadedLevels = info.levels;
            for (int l = 0; l < info.levels; ++l) {   // estimate: block formats are 4 or 8 bits per texel
                const int w = std::max(1, asset->Width() >> l), h = std::max(1, asset->Height() >> l);
                const std::size_t texels = std::size_t((w + 3) / 4 * 4) * std::size_t((h + 3) / 4 * 4);
                info.gpuBytes += info.compressed ? (info.glFormat == gputex::kGlCompressedRgbaS3tcDxt1 || info.glFormat == gputex::kGlCompressedRedRgtc1 ? texels / 2 : texels)
                                                 : std::size_t(w) * h * 4;
            }
        } else if (asset->Valid()) {
            // Our own chain (gamma-correct box or Kaiser, renormalised normal maps): the same levels the software
            // renderer samples, instead of glGenerateMipmap's driver-chosen filter on gamma-encoded bytes.
            std::vector<Image> built;
            const std::vector<Image>* levels = &asset->UploadLevels(built);
            g.GenTextures(1, &tex);
            g.BindTexture(GL_TEXTURE_2D, tex);
            for (std::size_t l = 0; l < levels->size(); ++l) {
                const Image& lv = (*levels)[l];
                g.TexImage2D(GL_TEXTURE_2D, GLint(l), GL_RGBA8, lv.Width(), lv.Height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, lv.Data());
                info.gpuBytes += lv.Pixels().size();
            }
            g.TexParameteri(GL_TEXTURE_2D, 0x813C /*GL_TEXTURE_BASE_LEVEL*/, 0);
            g.TexParameteri(GL_TEXTURE_2D, 0x813D /*GL_TEXTURE_MAX_LEVEL*/, GLint(levels->size()) - 1);
            info.glFormat = GL_RGBA8;
            info.uploadedLevels = int(levels->size());
            if (levels->size() < 2) ts.mipmaps = false;
        }
    }
    if (tex) {
        info.anisotropy = ts.anisotropy;
        m_texInfo[tex] = ts;
        const int wrapMode = ts.wrap == TextureSampling::Clamp ? GL_CLAMP_TO_EDGE
                           : ts.wrap == TextureSampling::Mirror ? 0x8370 /*GL_MIRRORED_REPEAT*/ : GL_REPEAT;
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
        const bool point = ts.filter == TextureSampling::Point;
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, point ? 0x2600 /*GL_NEAREST*/ : GL_LINEAR);
        if (ts.mipmaps) {
            const int minF = point ? 0x2700 /*GL_NEAREST_MIPMAP_NEAREST*/
                           : ts.filter == TextureSampling::Bilinear ? 0x2701 /*GL_LINEAR_MIPMAP_NEAREST*/
                                                                    : GL_LINEAR_MIPMAP_LINEAR;
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minF);
        } else {
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, point ? 0x2600 : GL_LINEAR);
        }
        // Anisotropic filtering (the picture's `anisotropy` setting, 8 by default): keeps textured floors and terrain
        // sharp at grazing angles. GL_TEXTURE_MAX_ANISOTROPY (EXT_texture_filter_anisotropic, core in 4.6); a driver
        // without it ignores the call, and the error it raises is cleared so later checks are not confused.
        if (!point && ts.anisotropy > 1) {
            g.TexParameteri(GL_TEXTURE_2D, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY*/, ts.anisotropy);
            if (g.GetError) for (int i = 0; i < 4 && g.GetError() != 0; ++i) {}
        }
        if (info.gpuBytes) MemoryTracker::Global().Add(GpuTextureCategory(), std::int64_t(info.gpuBytes), 1);
    }
    m_uploadInfo[name] = info;
    m_texCache[name] = tex;
    return tex;
}

gputex::Caps GLRenderer::QueryTextureCaps() {
    std::vector<std::string> ext;
    const char* version = g.GetString ? reinterpret_cast<const char*>(g.GetString(GL_VERSION)) : nullptr;
    GLint n = 0;
    if (g.GetStringi && g.GetIntegerv) g.GetIntegerv(0x821D /*GL_NUM_EXTENSIONS*/, &n);
    if (n > 0 && n < 100000) {
        for (GLint i = 0; i < n; ++i) {
            const GLubyte* s = g.GetStringi(GL_EXTENSIONS, GLuint(i));
            if (s) ext.emplace_back(reinterpret_cast<const char*>(s));
        }
    } else if (g.GetString) {   // GL 2.1: one space-separated string (a core profile refuses this; the list stays empty)
        const GLubyte* s = g.GetString(GL_EXTENSIONS);
        std::string all = s ? reinterpret_cast<const char*>(s) : "";
        std::size_t p = 0;
        while (p < all.size()) {
            std::size_t q = all.find(' ', p);
            if (q == std::string::npos) q = all.size();
            if (q > p) ext.push_back(all.substr(p, q - p));
            p = q + 1;
        }
    }
    if (g.GetError) for (int i = 0; i < 16 && g.GetError() != 0; ++i) {}
    return gputex::CapsFrom(version, ext);
}

const GLRenderer::TextureUploadInfo* GLRenderer::UploadInfo(const std::string& name) const {
    auto it = m_uploadInfo.find(name);
    return it == m_uploadInfo.end() ? nullptr : &it->second;
}

// Upload a plan's levels; 0 (nothing left behind) when GL reports an error.
static unsigned int UploadPlanLevels(const gputex::Plan& p) {
    if (p.levels.empty() || (p.compressed && !g.CompressedTexImage2D)) return 0;
    for (int i = 0; i < 16 && g.GetError() != 0; ++i) {}   // start from a clean error state
    GLuint t = 0;
    g.GenTextures(1, &t);
    g.BindTexture(GL_TEXTURE_2D, t);
    for (std::size_t l = 0; l < p.levels.size(); ++l) {
        const gputex::Level& lv = p.levels[l];
        if (p.compressed)
            g.CompressedTexImage2D(GL_TEXTURE_2D, GLint(l), GLenum(p.glFormat), lv.width, lv.height, 0, GLsizei(lv.bytes.size()), lv.bytes.data());
        else
            g.TexImage2D(GL_TEXTURE_2D, GLint(l), GL_RGBA8, lv.width, lv.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, lv.bytes.data());
    }
    g.TexParameteri(GL_TEXTURE_2D, 0x813C /*GL_TEXTURE_BASE_LEVEL*/, 0);
    g.TexParameteri(GL_TEXTURE_2D, 0x813D /*GL_TEXTURE_MAX_LEVEL*/, GLint(p.levels.size()) - 1);   // a short chain is complete
    if (g.GetError() != 0) { g.DeleteTextures(1, &t); return 0; }
    return t;
}

unsigned int GLRenderer::UploadDdsTexture(const std::string& name, TextureSampling& ts, TextureUploadInfo& info) {
    tex::DdsImage dds;
    std::string why;
    if (!gputex::Source(name, &dds, &why)) {
        if (!why.empty()) Log::Warning("[gl] texture '", name, "': ", why, "; using the picture itself");
        return 0;
    }
    if (!m_texCapsKnown) { m_texCaps = QueryTextureCaps(); m_texCapsKnown = true; }
    gputex::Caps caps = m_texCaps;
    bool forcedOff = false;
    if (const char* off = std::getenv("OKAY_GL_NO_COMPRESSED")) if (off[0] == '1') { caps = gputex::Caps(); forcedOff = true; }
    const tex::DdsFormat fmt = dds.format;
    gputex::Plan plan;
    if (!gputex::PlanUpload(std::move(dds), caps, &plan)) return 0;
    if (forcedOff && !plan.compressed && !plan.note.empty() && plan.note.find("multiple of 4") == std::string::npos)
        plan.note = "OKAY_GL_NO_COMPRESSED=1; decoded on the CPU";
    unsigned int t = UploadPlanLevels(plan);
    if (!t && plan.compressed) {   // the driver refused the compressed data after all: decode it on the CPU
        tex::DdsImage again;
        if (gputex::Source(name, &again, nullptr) && gputex::PlanUpload(std::move(again), gputex::Caps(), &plan)) {
            plan.note = "the driver refused the compressed upload; decoded on the CPU";
            t = UploadPlanLevels(plan);
        }
    }
    if (!t) return 0;
    info.fromDds = true;
    info.compressed = plan.compressed;
    info.glFormat = plan.glFormat;
    info.levels = int(plan.levels.size());
    info.note = plan.note;
    if (plan.levels.size() == 1 && ts.mipmaps) {
        if (!plan.compressed && g.GenerateMipmap) {   // one RGBA8 level: GL builds the rest, as for a PNG
            g.TexParameteri(GL_TEXTURE_2D, 0x813D /*GL_TEXTURE_MAX_LEVEL*/, 1000);
            g.GenerateMipmap(GL_TEXTURE_2D);
        } else ts.mipmaps = false;                    // compressed without levels: sample level 0 only
    }
    Log::Info("[gl] texture '", name, "': ", tex::DdsFormatName(fmt), plan.compressed ? " kept compressed on the GPU" : " uploaded as RGBA8",
              " (", info.levels, info.levels == 1 ? " level)" : " levels)", info.note.empty() ? "" : "; ", info.note);
    return t;
}

// ---- Per-mesh vertex-buffer cache -------------------------------------------
// The per-mesh change hashes live in MeshHash.hpp, shared with the other GPU renderer: a cheap sample every frame,
// the full hash now and then (a lone-vertex edit).
static std::uint64_t MeshSampleHash(const Mesh& m) { return meshhash::MeshSample(m); }
static std::uint64_t MeshFullHash(const Mesh& m) { return meshhash::MeshFull(m); }

// Expand indexed triangles into the interleaved non-indexed layout the shader
// consumes: pos(3)+normal(3)+uv(2)+color(3)+tangent(3) = 14 floats per vertex.
// Flat shading via the face normal when the mesh has none; box-projected UVs when
// it has no UVs; per-triangle face colors honored (matches the software renderer).
void GLRenderer::BuildExpandedVerts(const Mesh& mesh, bool groupBySlot) {
    const auto& V = mesh.vertices; const auto& T = mesh.triangles;
    const bool hasN = mesh.HasNormals();
    const bool hasUV = !mesh.uvs.empty() && mesh.uvs.size() == V.size();
    const bool faceCols = mesh.HasFaceColors();
    const bool vcols = mesh.HasVertexColors();   // per-vertex colours blend smoothly across each face
    const int nv = (int)V.size();
    m_verts.resize(T.size() / 3 * 3 * 14);   // written through a raw pointer, trimmed at the end
    float* out = m_verts.data();
    // Per-slot drawing: with material slots the triangles are written grouped by slot (a stable order), and the first vertex
    // and count of each group are kept, so each slot can be drawn with its own picture. Skinned meshes keep their order.
    m_slotRanges.clear();
    std::vector<int> order;
    const bool bySlot = groupBySlot && mesh.HasSlots();
    if (bySlot) {
        order = mesh.GroupBySlot().order;
    }
    const std::size_t triCount = T.size() / 3;
    for (std::size_t oi = 0; oi < triCount; ++oi) {
        const std::size_t i = (bySlot ? (std::size_t)order[oi] : oi) * 3;
        if (bySlot) {
            const int slot = mesh.triSlot[i / 3];
            const int firstVert = (int)((out - m_verts.data()) / 14);
            if (m_slotRanges.empty() || m_slotRanges.back()[0] != slot) m_slotRanges.push_back({slot, firstVert, 0});
        }
        int a = T[i], b = T[i + 1], c = T[i + 2];
        if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) continue; // skip bad tris
        // Face normal: used for flat shading AND for box-projecting UVs.
        Vec3 fn = Vec3::Cross(V[b] - V[a], V[c] - V[a]);
        { float m = fn.Magnitude(); fn = m > 1e-8f ? fn * (1.0f / m) : Vec3{0, 1, 0}; }
        const float ax = std::fabs(fn.x), ay = std::fabs(fn.y), az = std::fabs(fn.z);
        float cr = 1.0f, cg = 1.0f, cb = 1.0f;  // white = no-op when there are no face colors
        if (faceCols) { const Color& fc = mesh.triColors[i / 3]; cr = fc.r; cg = fc.g; cb = fc.b; }
        const int idx[3] = {a, b, c};
        // Per-vertex UVs (mesh or box-projected) first, so we can derive the face tangent.
        float uu[3], vv[3];
        for (int k = 0; k < 3; ++k) {
            const Vec3& p = V[idx[k]];
            if (hasUV) { uu[k] = mesh.uvs[idx[k]].x; vv[k] = mesh.uvs[idx[k]].y; }
            else if (ax >= ay && ax >= az) { uu[k] = p.z + 0.5f; vv[k] = p.y + 0.5f; }
            else if (ay >= ax && ay >= az) { uu[k] = p.x + 0.5f; vv[k] = p.z + 0.5f; }
            else                           { uu[k] = p.x + 0.5f; vv[k] = p.y + 0.5f; }
        }
        // Local-space per-face tangent (edges + UV deltas); transformed to world by
        // mat3(uModel) in the vertex shader. Mirrors the software renderer's TBN.
        Vec3 e1 = V[b] - V[a], e2 = V[c] - V[a];
        float du1 = uu[1] - uu[0], dv1 = vv[1] - vv[0];
        float du2 = uu[2] - uu[0], dv2 = vv[2] - vv[0];
        float det = du1 * dv2 - du2 * dv1;
        Vec3 tan = std::fabs(det) > 1e-8f ? (e1 * dv2 - e2 * dv1) * (1.0f / det) : e1;
        { float m = tan.Magnitude(); tan = m > 1e-6f ? tan * (1.0f / m) : Vec3{1, 0, 0}; }
        for (int k = 0; k < 3; ++k) {
            const Vec3& p = V[idx[k]];
            Vec3 n = hasN ? mesh.normals[idx[k]] : fn;
            out[0] = p.x;   out[1] = p.y;   out[2] = p.z;
            out[3] = n.x;   out[4] = n.y;   out[5] = n.z;
            out[6] = uu[k]; out[7] = vv[k];
            out[8] = cr;    out[9] = cg;    out[10] = cb;
            if (vcols) { const Color& vc = mesh.vertexColors[idx[k]]; out[8] *= vc.r; out[9] *= vc.g; out[10] *= vc.b; }
            out[11] = tan.x; out[12] = tan.y; out[13] = tan.z;
            out += 14;
        }
    }
    m_verts.resize((std::size_t)(out - m_verts.data()));
    if (bySlot) {                                         // close the ranges: each runs up to the start of the next
        const int total = (int)(m_verts.size() / 14);
        for (std::size_t r = 0; r < m_slotRanges.size(); ++r) m_slotRanges[r][2] = (r + 1 < m_slotRanges.size() ? m_slotRanges[r + 1][1] : total) - m_slotRanges[r][1];
    }
}

// Return the (up-to-date) cached GPU buffer for a mesh, expanding + uploading only
// when the mesh actually changed. nullptr = mesh has no drawable triangles.
GLRenderer::MeshVB* GLRenderer::EnsureMeshVB(const Mesh& mesh, std::uint64_t version) {
    MeshVB& e = m_meshVB[&mesh];
    e.lastUse = m_frame;
    bool same = e.vbo != 0 &&
        e.vp  == (const void*)mesh.vertices.data()  && e.vn  == mesh.vertices.size()  &&
        e.tp  == (const void*)mesh.triangles.data() && e.tn  == mesh.triangles.size() &&
        e.np  == (const void*)mesh.normals.data()   && e.nn  == mesh.normals.size()   &&
        e.uvp == (const void*)mesh.uvs.data()       && e.uvn == mesh.uvs.size()       &&
        e.fcp == (const void*)mesh.triColors.data() && e.fcn == mesh.triColors.size();
    std::uint64_t sh = 0;
    if (version) {   // skinned: the deformer says when it changed, no hashing needed
        if (same && e.version == version) return e.vertCount > 0 ? &e : nullptr;
        same = false;
    }
    if (same) { sh = MeshSampleHash(mesh); if (sh != e.sampleHash) same = false; }
    if (same && m_frame - e.lastFullHash >= 60) {   // exact check ~once a second
        std::uint64_t fh = MeshFullHash(mesh);
        e.lastFullHash = m_frame;
        if (fh != e.fullHash) same = false; else e.fullHash = fh;
    }
    if (same) return e.vertCount > 0 ? &e : nullptr;
    BuildExpandedVerts(mesh, version == 0);
    e.slotRanges = m_slotRanges;
    e.vp  = mesh.vertices.data();  e.vn  = mesh.vertices.size();
    e.tp  = mesh.triangles.data(); e.tn  = mesh.triangles.size();
    e.np  = mesh.normals.data();   e.nn  = mesh.normals.size();
    e.uvp = mesh.uvs.data();       e.uvn = mesh.uvs.size();
    e.fcp = mesh.triColors.data(); e.fcn = mesh.triColors.size();
    e.version = version;
    if (!version) {
        e.sampleHash = sh ? sh : MeshSampleHash(mesh);
        e.fullHash = MeshFullHash(mesh);
        e.lastFullHash = m_frame;
    }
    e.vertCount = (int)(m_verts.size() / 14);
    ++e.buildSerial;
    if (e.vertCount == 0) return nullptr;
    if (!e.vbo) g.GenBuffers(1, &e.vbo);
    if (!e.vbo) { e.vertCount = 0; return nullptr; }
    g.BindBuffer(GL_ARRAY_BUFFER, e.vbo);
    g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)(m_verts.size() * sizeof(float)),
                 m_verts.data(), version ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    return &e;
}

bool GLRenderer::SupportsGpuSkinning() const {
    return g.ok && g.Uniform4fv && m_prog && m_uSkin >= 0 && m_uBones >= 0 && g_skinBones >= 32;
}
int GLRenderer::GpuSkinJoints() const { return g_skinBones; }

// Which version keys a mesh's cached buffer: a GPU-skinned mesh draws its bind
// pose (changes only with bindVersion); a CPU-skinned one changes every frame.
std::uint64_t gl_render::MeshKey(const SkinnedMesh* skin) {
    if (!skin) return 0;
    return skin->UsesGpuSkinning() ? skin->bindVersion : skin->version;
}

// Feed (or stop feeding) GPU skinning for the next draw in the current program:
// per-vertex joints/weights from a static buffer expanded like the mesh's, and
// the bone rows. Leaves GL_ARRAY_BUFFER bound to the skin buffer when skinning.
void GLRenderer::BindSkin(const SkinnedMesh* skin, const Mesh& mesh, MeshVB* mv, int uSkin, int uBones) {
    const bool on = skin && mv && skin->UsesGpuSkinning() && g.Uniform4fv && uBones >= 0 &&
                    (int)skin->palette.size() <= g_skinBones && !skin->palette.empty() &&
                    skin->jointIdx.size() == mesh.vertices.size() && skin->jointWt.size() == mesh.vertices.size();
    if (!on) {
        g.DisableVertexAttribArray(5); g.DisableVertexAttribArray(6);
        if (uSkin >= 0) g.Uniform1f(uSkin, 0.0f);
        return;
    }
    // Joints + weights in the same expanded order as BuildExpandedVerts (8 floats per vertex).
    if (!mv->skinVbo || mv->skinFor != mv->vbo || mv->skinVer != mv->buildSerial) {
        const auto& T = mesh.triangles;
        const int nv = (int)mesh.vertices.size();
        std::vector<float> sv;
        sv.reserve((std::size_t)mv->vertCount * 8);
        for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
            int a = T[i], b = T[i + 1], c = T[i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) continue;
            for (int v : {a, b, c}) {
                const auto& ji = skin->jointIdx[v]; const auto& jw = skin->jointWt[v];
                // Same rule as the D3D11 path: an index outside the palette would read
                // past uBones[] in the shader, so clamp it and drop its weight.
                const int maxJ = (int)skin->palette.size() - 1;
                for (int k = 0; k < 4; ++k) sv.push_back((float)(ji[k] < 0 ? 0 : ji[k] > maxJ ? maxJ : ji[k]));
                for (int k = 0; k < 4; ++k) sv.push_back(ji[k] < 0 || ji[k] > maxJ ? 0.0f : jw[k]);
            }
        }
        if (!mv->skinVbo) g.GenBuffers(1, &mv->skinVbo);
        g.BindBuffer(GL_ARRAY_BUFFER, mv->skinVbo);
        g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)(sv.size() * sizeof(float)), sv.data(), GL_STATIC_DRAW);
        mv->skinFor = mv->vbo; mv->skinVer = mv->buildSerial;
    }
    g.BindBuffer(GL_ARRAY_BUFFER, mv->skinVbo);
    g.EnableVertexAttribArray(5);
    g.VertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (const void*)0);
    g.EnableVertexAttribArray(6);
    g.VertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (const void*)(4 * sizeof(float)));
    // Bone rows (Mat4 is column-major: row r = m[r], m[4+r], m[8+r], m[12+r]).
    m_boneRows.resize(skin->palette.size() * 12);
    float* o = m_boneRows.data();
    for (const Mat4& M : skin->palette)
        for (int r = 0; r < 3; ++r) { *o++ = M.m[r]; *o++ = M.m[4 + r]; *o++ = M.m[8 + r]; *o++ = M.m[12 + r]; }
    g.Uniform4fv(uBones, (GLsizei)(skin->palette.size() * 3), m_boneRows.data());
    g.Uniform1f(uSkin, 1.0f);
}

} // namespace okay
