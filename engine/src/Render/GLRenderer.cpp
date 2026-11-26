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


bool GLRenderer::LoadGL(GLGetProc gp) {
    if (g.ok) return true;
    if (!gp) return false;
    bool r = true;
    r &= load(g.CreateShader, gp, "glCreateShader");
    r &= load(g.ShaderSource, gp, "glShaderSource");
    r &= load(g.CompileShader, gp, "glCompileShader");
    r &= load(g.GetShaderiv, gp, "glGetShaderiv");
    r &= load(g.GetShaderInfoLog, gp, "glGetShaderInfoLog");
    r &= load(g.CreateProgram, gp, "glCreateProgram");
    r &= load(g.AttachShader, gp, "glAttachShader");
    r &= load(g.BindAttribLocation, gp, "glBindAttribLocation");
    r &= load(g.LinkProgram, gp, "glLinkProgram");
    r &= load(g.GetProgramiv, gp, "glGetProgramiv");
    r &= load(g.GetProgramInfoLog, gp, "glGetProgramInfoLog");
    r &= load(g.DeleteShader, gp, "glDeleteShader");
    r &= load(g.DeleteProgram, gp, "glDeleteProgram");
    r &= load(g.UseProgram, gp, "glUseProgram");
    r &= load(g.GetUniformLocation, gp, "glGetUniformLocation");
    r &= load(g.UniformMatrix4fv, gp, "glUniformMatrix4fv");
    r &= load(g.Uniform3f, gp, "glUniform3f");
    r &= load(g.Uniform1f, gp, "glUniform1f");
    r &= load(g.Uniform3fv, gp, "glUniform3fv");
    r &= load(g.Uniform1fv, gp, "glUniform1fv");
    r &= load(g.GenBuffers, gp, "glGenBuffers");
    r &= load(g.BindBuffer, gp, "glBindBuffer");
    r &= load(g.BufferData, gp, "glBufferData");
    r &= load(g.DeleteBuffers, gp, "glDeleteBuffers");
    r &= load(g.VertexAttribPointer, gp, "glVertexAttribPointer");
    r &= load(g.EnableVertexAttribArray, gp, "glEnableVertexAttribArray");
    r &= load(g.DisableVertexAttribArray, gp, "glDisableVertexAttribArray");
    r &= load(g.GenFramebuffers, gp, "glGenFramebuffers");
    r &= load(g.BindFramebuffer, gp, "glBindFramebuffer");
    r &= load(g.DeleteFramebuffers, gp, "glDeleteFramebuffers");
    r &= load(g.GenRenderbuffers, gp, "glGenRenderbuffers");
    r &= load(g.BindRenderbuffer, gp, "glBindRenderbuffer");
    r &= load(g.DeleteRenderbuffers, gp, "glDeleteRenderbuffers");
    r &= load(g.RenderbufferStorage, gp, "glRenderbufferStorage");
    r &= load(g.RenderbufferStorageMultisample, gp, "glRenderbufferStorageMultisample");
    r &= load(g.FramebufferRenderbuffer, gp, "glFramebufferRenderbuffer");
    r &= load(g.FramebufferTexture2D, gp, "glFramebufferTexture2D");
    r &= load(g.CheckFramebufferStatus, gp, "glCheckFramebufferStatus");
    r &= load(g.BlitFramebuffer, gp, "glBlitFramebuffer");
    r &= load(g.GenTextures, gp, "glGenTextures");
    r &= load(g.BindTexture, gp, "glBindTexture");
    r &= load(g.TexImage2D, gp, "glTexImage2D");
    r &= load(g.TexParameteri, gp, "glTexParameteri");
    r &= load(g.DeleteTextures, gp, "glDeleteTextures");
    r &= load(g.Viewport, gp, "glViewport");
    r &= load(g.ClearColor, gp, "glClearColor");
    r &= load(g.Clear, gp, "glClear");
    r &= load(g.Enable, gp, "glEnable");
    r &= load(g.Disable, gp, "glDisable");
    r &= load(g.BlendFunc, gp, "glBlendFunc");
    r &= load(g.DepthMask, gp, "glDepthMask");
    r &= load(g.DepthFunc, gp, "glDepthFunc");
    r &= load(g.DrawArrays, gp, "glDrawArrays");
    r &= load(g.ReadPixels, gp, "glReadPixels");
    r &= load(g.GetError, gp, "glGetError");
    r &= load(g.Uniform1i, gp, "glUniform1i");
    r &= load(g.Uniform2f, gp, "glUniform2f");
    r &= load(g.ActiveTexture, gp, "glActiveTexture");
    load(g.GenerateMipmap, gp, "glGenerateMipmap");   // optional (GL 3.0+); mips skipped if absent
    // Optional (present on core 3.0+; absent on 2.1 compatibility where VAO 0 works):
    load(g.GenVertexArrays, gp, "glGenVertexArrays");
    load(g.BindVertexArray, gp, "glBindVertexArray");
    load(g.DeleteVertexArrays, gp, "glDeleteVertexArrays");
    // Optional: depth-only shadow-map FBO needs these to declare "no colour buffer".
    // If absent, shadow mapping is skipped (renderer still works without cast shadows).
    load(g.DrawBuffer, gp, "glDrawBuffer");
    load(g.ReadBuffer, gp, "glReadBuffer");
    // Optional: GPU skinning (bone matrices as a vec4 uniform array).
    load(g.Uniform4fv, gp, "glUniform4fv");
    load(g.GetIntegerv, gp, "glGetIntegerv");
    // Optional: Custom (.okayshader) programs copy the frame's uniforms with these.
    load(g.GetActiveUniform, gp, "glGetActiveUniform");
    load(g.GetUniformfv, gp, "glGetUniformfv");
    load(g.GetUniformiv, gp, "glGetUniformiv");
    // Optional: compressed (.dds) uploads. Without them every .dds is decoded on the CPU and uploaded as RGBA8.
    load(g.CompressedTexImage2D, gp, "glCompressedTexImage2D");
    load(g.GetString, gp, "glGetString");
    load(g.GetStringi, gp, "glGetStringi");
    // Optional: the RHI OpenGL backend (rhi::CreateOpenGLDevice) needs these too.
    load(g.ColorMask, gp, "glColorMask");
    load(g.CullFace, gp, "glCullFace");
    load(g.DrawElements, gp, "glDrawElements");
    load(g.BlendFuncSeparate, gp, "glBlendFuncSeparate");
    load(g.BufferSubData, gp, "glBufferSubData");
    load(g.ClearDepth, gp, "glClearDepth");
    g.ok = r;
    if (!r) Log::Error("[gl] required OpenGL entry points missing — using software renderer");
    return r;
}

bool GLRenderer::Available() { return g.ok; }

GLRenderer::~GLRenderer() { /* GL objects freed via Destroy() with context current */ }










// Opaque geometry as packed linear depth at half resolution (the SSAO and DoF
// passes read it). Leaves the depth FBO bound and the depth test on.
void GLRenderer::DrawLinearDepth(const Scene& scene, const Mat4& vp, const Vec3& eye, const GameObject* ignore,
                                 const Frustum& viewFr) {
    (void)viewFr;   // the visibility frame already applied this frustum
    g.BindFramebuffer(GL_FRAMEBUFFER, m_ssaoDepthFbo);
    g.Viewport(0, 0, m_ssaoW, m_ssaoH);
    g.ClearColor(1.0f, 1.0f, 0.0f, 1.0f);            // far plane = no occlusion
    g.Enable(GL_DEPTH_TEST); g.DepthFunc(GL_LEQUAL); g.DepthMask(GL_TRUE);
    g.Disable(GL_BLEND);
    g.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g.UseProgram(m_ssaoDepthProg);
    g.Uniform3f(m_usdEye, eye.x, eye.y, eye.z);
    g.Uniform1f(m_usdFar, kSsaoFar);
    g.DisableVertexAttribArray(1); g.DisableVertexAttribArray(2);
    g.DisableVertexAttribArray(3); g.DisableVertexAttribArray(4);
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->ActiveInHierarchy() || go->HiddenFromCamera(ignore)) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || mr->wireframe || !mr->enabled) continue;
        if (mr->color.a < 0.999f) continue;           // transparents don't occlude
        if (visibility::Current().ready && !visibility::Current().Visible(go)) continue;
        const Mesh* shown = visibility::Current().ready ? visibility::Current().MeshFor(go) : &mr->DisplayMesh();
        if (!shown) continue;
        const Mesh& mesh = *shown;
        if (mesh.vertices.empty() || mesh.triangles.size() < 3) continue;
        Mat4 model = go->transform->LocalToWorldMatrix();
        Mat4 mvp = vp * model;
        g.UniformMatrix4fv(m_usdMVP, 1, GL_FALSE, mvp.m);
        g.UniformMatrix4fv(m_usdModel, 1, GL_FALSE, model.m);
        const bool deformed = go->GetComponent<Character>() != nullptr;
        if (!deformed) {
            const auto* skin = go->GetComponent<SkinnedMesh>();
            MeshVB* mv = EnsureMeshVB(mesh, MeshKey(skin));
            if (!mv) continue;
            g.BindBuffer(GL_ARRAY_BUFFER, mv->vbo);
            g.EnableVertexAttribArray(0);
            g.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (const void*)0);
            BindSkin(skin, mesh, mv, m_usdSkin, m_usdBones);
            g.DrawArrays(GL_TRIANGLES, 0, (GLsizei)mv->vertCount);
            continue;
        }
        BindSkin(nullptr, mesh, nullptr, m_usdSkin, m_usdBones);
        const auto& V = mesh.vertices; const auto& T = mesh.triangles;
        const int nv = (int)V.size();
        m_verts.clear(); m_verts.reserve(T.size() * 3);
        for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
            int a = T[i], b = T[i + 1], c = T[i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) continue;
            const Vec3& pa = V[a]; const Vec3& pb = V[b]; const Vec3& pc = V[c];
            m_verts.push_back(pa.x); m_verts.push_back(pa.y); m_verts.push_back(pa.z);
            m_verts.push_back(pb.x); m_verts.push_back(pb.y); m_verts.push_back(pb.z);
            m_verts.push_back(pc.x); m_verts.push_back(pc.y); m_verts.push_back(pc.z);
        }
        if (m_verts.empty()) continue;
        g.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
        g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)(m_verts.size() * sizeof(float)), m_verts.data(), GL_DYNAMIC_DRAW);
        g.EnableVertexAttribArray(0);
        g.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (const void*)0);
        g.DrawArrays(GL_TRIANGLES, 0, (GLsizei)(m_verts.size() / 3));
    }
    g.DisableVertexAttribArray(5); g.DisableVertexAttribArray(6);
}


// Draw every shadow-casting mesh's depth with `lightVP` into the bound depth
// target (the caller sets the viewport to the tile). `local` = the spot/point
// light being rendered, so casters out of its range are skipped.
void GLRenderer::DrawShadowCasters(const Scene& scene, const Mat4& lightVP, const GameObject* ignore,
                                   const LightSample* local) {
    const Frustum lightFr = Frustum::FromVP(lightVP);   // only casters inside the light's view
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->ActiveInHierarchy() || go->firstPersonOnly || go->HiddenFromCamera(ignore)) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || mr->wireframe || !mr->enabled || !mr->castShadows) continue;
        if (mr->color.a < 0.999f) continue;   // transparent meshes don't cast
        const Mesh* lodMesh = visibility::Current().ShadowMesh(go, mr);
        if (!lodMesh) continue;               // this camera's LOD dropped it
        const Mesh& mesh = *lodMesh;
        const auto& V = mesh.vertices; const auto& T = mesh.triangles;
        if (V.empty() || T.size() < 3) continue;
        if (CulledByFrustum(lightFr, go, mr)) continue;
        if (local) {
            Vec3 c; float r;
            if (ObjectWorldSphere(go, mr, c, r) && !CasterInLightRange(*local, c, r)) continue;
        }
        Mat4 dmvp = lightVP * go->transform->LocalToWorldMatrix();
        g.UniformMatrix4fv(m_uDepthMVP, 1, GL_FALSE, dmvp.m);
        // Static meshes: draw positions straight from the cached interleaved
        // buffer (stride 14). Per-frame-deformed meshes rebuild dynamically.
        // Skinned meshes are cached too, keyed on SkinnedMesh::version.
        const bool deformed = go->GetComponent<Character>() != nullptr;
        if (!deformed) {
            const auto* skin = go->GetComponent<SkinnedMesh>();
            MeshVB* mv = EnsureMeshVB(mesh, MeshKey(skin));
            if (!mv) continue;
            g.BindBuffer(GL_ARRAY_BUFFER, mv->vbo);
            g.EnableVertexAttribArray(0);
            g.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (const void*)0);
            BindSkin(skin, mesh, mv, m_udSkin, m_udBones);
            g.DrawArrays(GL_TRIANGLES, 0, (GLsizei)mv->vertCount);
            continue;
        }
        BindSkin(nullptr, mesh, nullptr, m_udSkin, m_udBones);
        const int nv = (int)V.size();
        m_verts.clear(); m_verts.reserve(T.size() * 3);
        for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
            int a = T[i], b = T[i + 1], c = T[i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) continue;
            const Vec3& pa = V[a]; const Vec3& pb = V[b]; const Vec3& pc = V[c];
            m_verts.push_back(pa.x); m_verts.push_back(pa.y); m_verts.push_back(pa.z);
            m_verts.push_back(pb.x); m_verts.push_back(pb.y); m_verts.push_back(pb.z);
            m_verts.push_back(pc.x); m_verts.push_back(pc.y); m_verts.push_back(pc.z);
        }
        if (m_verts.empty()) continue;
        g.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
        g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)(m_verts.size() * sizeof(float)), m_verts.data(), GL_DYNAMIC_DRAW);
        g.EnableVertexAttribArray(0);
        g.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (const void*)0);
        g.DrawArrays(GL_TRIANGLES, 0, (GLsizei)(m_verts.size() / 3));
    }
}

const std::uint32_t* GLRenderer::RenderToPixels(const Scene& scene, const Mat4& vp, const Vec3& eye,
                                                int w, int h, int samples,
                                                float clearR, float clearG, float clearB, float clearA,
                                                const GameObject* ignore) {
    if (!g.ok || w < 1 || h < 1) return nullptr;
    // The scene's post chain (bloom, grading, exposure, ...) wants a float target
    // and linear colour out of the mesh shader; with everything off the frame
    // takes the plain RGBA8 path exactly as before.
    const auto& rsTop = scene.renderSettings;
    const bool chain = PostActive(rsTop) && EnsurePostChain();
    const bool wantHdr = chain && rsTop.tonemap;
    if (!EnsureProgram() || !EnsureTargets(w, h, samples, wantHdr)) return nullptr;
    const bool hdrOut = chain && m_hdr;   // the mesh shader leaves its colour linear
    while (g.GetError() != 0) {}   // drain any stale GL error state before we render

    // Advance the mesh-VB cache clock and evict buffers unused for ~5s of frames
    // (deleted objects, replaced meshes), so GPU memory doesn't grow unbounded.
    ++m_frame;
    if ((m_frame % 300u) == 0u) {
        for (auto it = m_meshVB.begin(); it != m_meshVB.end();) {
            if (m_frame - it->second.lastUse > 300u) {
                if (it->second.vbo) g.DeleteBuffers(1, &it->second.vbo);
                if (it->second.skinVbo) g.DeleteBuffers(1, &it->second.skinVbo);
                it = m_meshVB.erase(it);
            } else ++it;
        }
    }

    // LOD / occlusion / batching for this camera, before shadows so casters use
    // the same mesh the colour pass will (a culled LOD does not cast).
    visibility::Build(scene, vp, eye, w, h, visibility::RenderViewFovY(), ignore);

    // ---- Shadow-map depth pre-passes -------------------------------------------
    // Sun: one ortho cascade per tile of the sun atlas. Local lights: one
    // perspective tile per spot light, six per point/area light (cube faces).
    // PlanShadows (ShadowSetup.hpp) decides the cascades and tile assignment.
    ShadowPlan plan;
    bool shadowOn = false, localOn = false;
    if (ShadowsEnabled() && EnsureDepthProgram()) {
        GLint maxTex = 8192;
        if (g.GetIntegerv) g.GetIntegerv(0x0D33 /*GL_MAX_TEXTURE_SIZE*/, &maxTex);
        if (maxTex < 1024) maxTex = 1024;
        PlanShadows(scene, vp, eye, plan, maxTex < 8192 ? (int)maxTex : 8192);
        auto beginPass = [&](unsigned int fbo) {
            g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
            g.Enable(GL_DEPTH_TEST); g.DepthFunc(GL_LEQUAL); g.DepthMask(GL_TRUE);
            g.Disable(GL_BLEND);
            g.Clear(GL_DEPTH_BUFFER_BIT);
            g.UseProgram(m_depthProg);
            if (g.BindVertexArray && m_vao) g.BindVertexArray(m_vao);
            // Only attribute 0 (position) is fed in these passes; make sure no stale
            // arrays from a previous frame get fetched against the position buffer.
            g.DisableVertexAttribArray(1); g.DisableVertexAttribArray(2);
            g.DisableVertexAttribArray(3); g.DisableVertexAttribArray(4);
        };
        const int S = plan.cascadeSize;
        if (plan.cascades > 0 &&
            EnsureDepthTarget(m_shadowFbo, m_shadowTex, m_shadowW, m_shadowH, S * plan.cascades, S)) {
            beginPass(m_shadowFbo);
            for (int c = 0; c < plan.cascades; ++c) {
                g.Viewport(c * S, 0, S, S);
                DrawShadowCasters(scene, plan.cascade[c].vp, ignore, nullptr);
            }
            shadowOn = true;
        }
        const int T = plan.tileSize, G = ShadowPlan::kLocalGrid;
        if (!plan.local.empty() &&
            EnsureDepthTarget(m_localFbo, m_localTex, m_localW, m_localH, T * G, T * G)) {
            beginPass(m_localFbo);
            const auto& L = SceneLights::List();
            for (const LocalShadowGpu& ls : plan.local)
                for (int f = 0; f < ls.tiles; ++f) {
                    int t = ls.firstTile + f;
                    g.Viewport((t % G) * T, (t / G) * T, T, T);
                    DrawShadowCasters(scene, ls.vp[f], ignore, &L[ls.light]);
                }
            localOn = true;
        }
        g.DisableVertexAttribArray(5); g.DisableVertexAttribArray(6);   // skin inputs off
    }

    g.BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    g.Viewport(0, 0, w, h);
    g.ClearColor(clearR, clearG, clearB, clearA);
    g.Enable(GL_DEPTH_TEST);
    g.DepthFunc(GL_LEQUAL);
    g.Enable(GL_MULTISAMPLE);
    g.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (g.BindVertexArray && m_vao) g.BindVertexArray(m_vao);

    // The sky as a fullscreen pass behind everything (no depth): the Procedural
    // sky (coloured by the sun's height, with the disc, haze, stars and clouds),
    // or, while the post chain runs, the Colour gradient the host would paint,
    // so the chain's grading, bloom and vignette cover the sky as well.
    const bool procedural = rsTop.skyMode == Scene::RenderSettings::SkyProcedural;
    if (rsTop.skybox && SkyPassEnabled() && (procedural || chain) && EnsureSkyProgram() && EnsureQuad()) {
        const auto now = std::chrono::steady_clock::now();
        if (!m_clockSet) { m_clock0 = now; m_clockSet = true; }
        const float t = std::chrono::duration<float>(now - m_clock0).count();
        const float dt = t - m_skyTime; m_skyTime = t;
        const Vec3 wind = WindVector();
        // Clouds drift with the wind (and a little on their own), as a running offset
        // so changing the speed never makes them jump.
        m_cloudOffset.x += (0.012f * rsTop.cloudSpeed + wind.x * 0.004f) * dt;
        m_cloudOffset.y += (0.005f * rsTop.cloudSpeed + wind.z * 0.004f) * dt;
        const Vec3 sun = SunDirection();
        const SkyPalette pal = SkyPaletteFor(sun.y);
        const Mat4 inv = vp.Inverse();
        g.Disable(GL_DEPTH_TEST); g.DepthMask(GL_FALSE); g.Disable(GL_BLEND);
        g.UseProgram(m_skyProg);
        g.UniformMatrix4fv(m_uskInvVP, 1, GL_FALSE, inv.m);
        g.Uniform3f(m_uskEye, eye.x, eye.y, eye.z);
        g.Uniform3f(m_uskSun, sun.x, sun.y, sun.z);
        g.Uniform3f(m_uskZenith, pal.zenith.x, pal.zenith.y, pal.zenith.z);
        g.Uniform3f(m_uskHorizon, pal.horizon.x, pal.horizon.y, pal.horizon.z);
        g.Uniform3f(m_uskGround, pal.ground.x, pal.ground.y, pal.ground.z);
        g.Uniform3f(m_uskSunColor, pal.sun.x, pal.sun.y, pal.sun.z);
        g.Uniform1f(m_uskSunSize, rsTop.skySunDisc * 0.0174532925f);
        g.Uniform1f(m_uskSunHalo, rsTop.skySunHalo);
        g.Uniform1f(m_uskSunDisc, pal.sunDisc);
        g.Uniform1f(m_uskHaze, rsTop.skyHaze);
        g.Uniform1f(m_uskNight, pal.night);
        g.Uniform1f(m_uskStarDensity, rsTop.skyStarDensity);
        g.Uniform1f(m_uskStarBright, rsTop.skyStarBright);
        g.Uniform1f(m_uskCloudCover, rsTop.cloudCover);
        g.Uniform1f(m_uskCloudDensity, rsTop.cloudDensity);
        g.Uniform1f(m_uskCloudScale, rsTop.cloudScale);
        g.Uniform2f(m_uskCloudOffset, m_cloudOffset.x, m_cloudOffset.y);
        g.Uniform1f(m_uskTime, t);
        g.Uniform1f(m_uskLin, hdrOut ? 1.0f : 0.0f);
        g.Uniform1f(m_uskMode, procedural ? 1.0f : 0.0f);
        g.Uniform3f(m_uskTop, rsTop.skyTop.r, rsTop.skyTop.g, rsTop.skyTop.b);
        g.Uniform3f(m_uskHor, rsTop.skyHorizon.r, rsTop.skyHorizon.g, rsTop.skyHorizon.b);
        g.Uniform3f(m_uskBot, rsTop.skyBottom.r, rsTop.skyBottom.g, rsTop.skyBottom.b);
        g.Uniform1f(m_uskHorizonPos, rsTop.skyHorizonPos);
        g.Uniform1f(m_uskSunOn, rsTop.skySun ? 1.0f : 0.0f);
        g.Uniform2f(m_uskSunPos, rsTop.skySunX, rsTop.skySunY);
        g.Uniform1f(m_uskSunRad, rsTop.skySunSize);
        if (!procedural) g.Uniform3f(m_uskSunColor, rsTop.skySunColor.r, rsTop.skySunColor.g, rsTop.skySunColor.b);
        g.Uniform1f(m_uskStarsOn, rsTop.skyStars ? 1.0f : 0.0f);
        g.Uniform2f(m_uskViewSize, (float)w, (float)h);
        DrawQuad();
        g.Enable(GL_DEPTH_TEST); g.DepthMask(GL_TRUE);
    }

    g.UseProgram(m_prog);
    Vec3 toLight = (SceneLight::Direction() * -1.0f).Normalized();
    Vec3 lightTravel = SceneLight::Direction();   // direction light travels (for ground shadows)
    g.Uniform3f(m_uLightDir, toLight.x, toLight.y, toLight.z);
    g.Uniform1f(m_uShadowAlpha, -1.0f);           // normal (lit) draws; flipped on for the shadow pass
    g.Uniform1f(m_uAlpha, 1.0f);                   // opaque by default; set per material below
    g.Uniform3f(m_uLightColor, 0.85f, 0.85f, 0.85f);
    // Hemisphere/colored ambient from the scene's gathered lights (falls back to grey).
    Vec3 amb = SceneLights::AmbientColor();
    if (amb.x + amb.y + amb.z < 1e-4f) amb = Vec3{0.35f, 0.36f, 0.40f};
    g.Uniform3f(m_uAmbient, amb.x, amb.y, amb.z);
    {
        Vec3 aSky, aGround;
        SceneAmbientColors(scene.renderSettings, amb, aSky, aGround);   // the scene's Ambient Mode
        g.Uniform3f(m_uAmbSky, aSky.x, aSky.y, aSky.z);
        g.Uniform3f(m_uAmbGround, aGround.x, aGround.y, aGround.z);
    }
    g.Uniform3f(m_uEye, eye.x, eye.y, eye.z);
    // Sky gradient for environment reflections. Refresh the shared global from the
    // scene first (the software RenderMeshes does this too, but it may not run on the
    // GPU path), so reflective/metal materials see the right sky in the editor + game.
    {
        const auto& rs = scene.renderSettings;
        EnvSky().enabled = rs.skybox;
        EffectiveSkyColors(rs, EnvSky().top, EnvSky().horizon, EnvSky().bottom);   // gradient, or the sun's palette
    }
    const EnvSkyData& sky = EnvSky();
    g.Uniform3f(m_uSkyTop, sky.top.x, sky.top.y, sky.top.z);
    g.Uniform3f(m_uSkyHor, sky.horizon.x, sky.horizon.y, sky.horizon.z);
    g.Uniform3f(m_uSkyBot, sky.bottom.x, sky.bottom.y, sky.bottom.z);
    g.Uniform1f(m_uEnvOn, sky.enabled ? 1.0f : 0.0f);

    // Fog from the scene's render settings (the same maths as FogFactor / the software renderer).
    {
        const auto& rs = scene.renderSettings;
        const Vec3 fc = EffectiveFogColor(rs);
        g.Uniform1f(m_uFogOn, FogActive(rs) ? 1.0f : 0.0f);
        g.Uniform3f(m_uFogColor, fc.x, fc.y, fc.z);
        g.Uniform1f(m_uFogStart, rs.fogStart);
        g.Uniform1f(m_uFogEnd, rs.fogEnd);
        g.Uniform1f(m_uFogMode, (float)rs.fogMode);
        g.Uniform1f(m_uFogDensity, rs.fogDensity);
        g.Uniform1f(m_uFogHeight, rs.fogHeight);
        g.Uniform1f(m_uFogFalloff, rs.fogFalloff);
        g.Uniform1f(m_uTonemap, rs.tonemap ? 1.0f : 0.0f);
        g.Uniform1f(m_uHdrOut, hdrOut ? 1.0f : 0.0f);
    }

    // Shadow atlases: sun cascades on texture unit 4, local light tiles on unit 5.
    g.Uniform1f(m_uShadowOn, shadowOn ? 1.0f : 0.0f);
    g.Uniform1f(m_uShadowSoft, ShadowSoftness() < 0.0f ? 0.0f : ShadowSoftness());
    {
        const LightSample* sun = SceneLights::Sun();
        g.Uniform1f(m_uSunIndex, (float)plan.sunLight);
        g.Uniform1f(m_uSunStrength, sun ? sun->shadowStrength : 1.0f);
        g.Uniform1f(m_uSunBias, sun ? sun->shadowBias : 1.0f);
        if (m_uSunSoft >= 0) g.Uniform1f(m_uSunSoft, sun ? sun->shadowSoftness : 0.5f);
        if (m_uFadeDist >= 0) g.Uniform1f(m_uFadeDist, ShadowDistance() > 0.0f ? ShadowDistance() : 0.0f);
    }
    if (shadowOn) {
        float cvp[16 * ShadowPlan::kMaxCascades] = {0}, csph[4 * ShadowPlan::kMaxCascades] = {0};
        float ctx[ShadowPlan::kMaxCascades] = {0};
        for (int c = 0; c < plan.cascades; ++c) {
            std::memcpy(&cvp[c * 16], plan.cascade[c].vp.m, sizeof(float) * 16);
            csph[c * 4 + 0] = plan.cascade[c].center.x; csph[c * 4 + 1] = plan.cascade[c].center.y;
            csph[c * 4 + 2] = plan.cascade[c].center.z; csph[c * 4 + 3] = plan.cascade[c].radius;
            ctx[c] = plan.cascade[c].texelWorld;
        }
        g.UniformMatrix4fv(m_uCasVP, plan.cascades, GL_FALSE, cvp);
        if (g.Uniform4fv) g.Uniform4fv(m_uCasSphere, plan.cascades, csph);
        g.Uniform1fv(m_uCasTexel, plan.cascades, ctx);
        g.Uniform1f(m_uCasCount, (float)plan.cascades);
        g.Uniform1f(m_uCasSize, (float)plan.cascadeSize);
        g.ActiveTexture(GL_TEXTURE0 + 4);
        g.BindTexture(GL_TEXTURE_2D, m_shadowTex);
        g.ActiveTexture(GL_TEXTURE0);
    }
    {
        // Per light: x = first tile (-1 = no shadow), y = strength, z = bias, w = texel at 1m.
        float lsh[16 * 4];
        for (int i = 0; i < 16; ++i) { lsh[i * 4] = -1.0f; lsh[i * 4 + 1] = lsh[i * 4 + 2] = lsh[i * 4 + 3] = 0.0f; }
        float lvp[16 * ShadowPlan::kMaxLocalTiles] = {0};
        if (localOn) {
            const auto& L = SceneLights::List();
            for (const LocalShadowGpu& ls : plan.local) {
                if (ls.light < 0 || ls.light >= 16) continue;
                lsh[ls.light * 4 + 0] = (float)ls.firstTile;
                lsh[ls.light * 4 + 1] = L[ls.light].shadowStrength;
                lsh[ls.light * 4 + 2] = L[ls.light].shadowBias;
                lsh[ls.light * 4 + 3] = ls.texelAt1;
                for (int f = 0; f < ls.tiles; ++f)
                    std::memcpy(&lvp[(ls.firstTile + f) * 16], ls.vp[f].m, sizeof(float) * 16);
            }
            g.UniformMatrix4fv(m_uLocalVP, ShadowPlan::kMaxLocalTiles, GL_FALSE, lvp);
            g.Uniform1f(m_uLocalSize, (float)plan.tileSize);
            g.ActiveTexture(GL_TEXTURE0 + 5);
            g.BindTexture(GL_TEXTURE_2D, m_localTex);
            g.ActiveTexture(GL_TEXTURE0);
        }
        if (g.Uniform4fv) g.Uniform4fv(m_uLShadow, 16, lsh);
    }

    // Flat ground contact shadows stand in for real shadows only when the sun's
    // shadow map isn't drawn (Shadows off / unsupported) but the sun does cast.
    const LightSample* sunL = SceneLights::Sun();
    const bool sunCasts = !sunL || (sunL->castShadows && sunL->shadowStrength > 0.0f);
    const bool mapsWork = ShadowsEnabled() && m_depthProg && (plan.cascades == 0 || shadowOn);
    const bool planarShadows = sunCasts && !mapsWork;

    // Upload every gathered scene light (directional + point + spot) so all lights
    // shade on the GPU exactly like the software renderer — not just the sun.
    {
        const auto& L = SceneLights::List();
        int n = (int)L.size(); if (n > 16) n = 16;
        float lt[16] = {0}, lr[16] = {0}, lco[16] = {0}, lci[16] = {0}, lfo[16] = {0};
        float lp[48] = {0}, ld[48] = {0}, lc[48] = {0};
        for (int i = 0; i < n; ++i) {
            const LightSample& s = L[i];
            lt[i] = (float)s.type;
            lfo[i] = (float)s.falloff;
            lp[i*3+0] = s.pos.x; lp[i*3+1] = s.pos.y; lp[i*3+2] = s.pos.z;
            Vec3 d = s.dir.Normalized();
            ld[i*3+0] = d.x; ld[i*3+1] = d.y; ld[i*3+2] = d.z;
            lc[i*3+0] = s.color.x; lc[i*3+1] = s.color.y; lc[i*3+2] = s.color.z;
            lr[i] = s.range; lco[i] = s.cosOuter; lci[i] = s.cosInner;
        }
        // Panel size, panel axes and light mask row per light (see okay::Lighting: rectangle Area lights and masks).
        float lrect[16 * 4] = {0}, lright[48] = {0}, lup[48] = {0}; bool anyCookie = false;
        for (int i = 0; i < n; ++i) {
            const LightSample& s = L[i];
            lrect[i*4+0] = s.halfW; lrect[i*4+1] = s.halfH;
            lrect[i*4+2] = (s.cookie >= 0 && s.cookie < 8) ? (float)s.cookie : -1.0f; if (lrect[i*4+2] >= 0.0f) anyCookie = true;
            lrect[i*4+3] = s.cookieScale;
            lright[i*3+0] = s.right.x; lright[i*3+1] = s.right.y; lright[i*3+2] = s.right.z;
            lup[i*3+0] = s.up.x; lup[i*3+1] = s.up.y; lup[i*3+2] = s.up.z;
        }
        for (int i = n; i < 16; ++i) lrect[i*4+2] = -1.0f;
        if (m_uLRect >= 0 && g.Uniform4fv) g.Uniform4fv(m_uLRect, 16, lrect);
        if (m_uLRight >= 0) g.Uniform3fv(m_uLRight, 16, lright);
        if (m_uLUp >= 0) g.Uniform3fv(m_uLUp, 16, lup);
        if (m_uCookieRows >= 0) g.Uniform1f(m_uCookieRows, 8.0f);
        if (anyCookie) {   // the mask atlas: 64 px wide, 8 rows of 64 px, the mask in every channel (unit 8)
            if (!m_cookieTex || m_cookieCount != LightCookies::Store().size()) {
                std::vector<unsigned char> px((size_t)64 * 64 * 8 * 4, 255);
                const auto& st = LightCookies::Store();
                for (int r = 0; r < 8 && r < (int)st.size(); ++r) {
                    const LightCookie& ck = st[(size_t)r];
                    if (ck.w <= 0 || ck.h <= 0) continue;
                    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
                        float v = ck.At(x * ck.w / 64, y * ck.h / 64); v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                        const unsigned char b = (unsigned char)(v * 255.0f + 0.5f);
                        unsigned char* d = &px[(((size_t)r * 64 + y) * 64 + x) * 4]; d[0] = d[1] = d[2] = d[3] = b;
                    }
                }
                if (!m_cookieTex) g.GenTextures(1, &m_cookieTex);
                g.BindTexture(GL_TEXTURE_2D, m_cookieTex);
                g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                g.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64 * 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                m_cookieCount = st.size();
            }
            g.ActiveTexture(GL_TEXTURE0 + 8);
            g.BindTexture(GL_TEXTURE_2D, m_cookieTex);
            g.ActiveTexture(GL_TEXTURE0);
        }
        g.Uniform1i(m_uLightCount, n);
        if (n > 0) {
            g.Uniform1fv(m_uLType, n, lt);
            g.Uniform3fv(m_uLPos, n, lp);
            g.Uniform3fv(m_uLDir, n, ld);
            g.Uniform3fv(m_uLCol, n, lc);
            g.Uniform1fv(m_uLRange, n, lr);
            g.Uniform1fv(m_uLCosOut, n, lco);
            g.Uniform1fv(m_uLCosIn, n, lci);
            if (m_uLFalloff >= 0) g.Uniform1fv(m_uLFalloff, n, lfo);
        }
    }

    // Frustum, occlusion, LOD and batching (Visibility.hpp). One camera with no
    // LOD group and nothing safe to merge submits the same objects as before.
    const Frustum viewFr = Frustum::FromVP(vp);
    const visibility::Frame& vis = visibility::Current();
    int drawn = 0;
    for (const visibility::Item& it : vis.items) {
        GameObject* go = it.go;
        auto* mr = it.mr;
        const Mesh& mesh = *it.mesh;
        const auto& V = mesh.vertices; const auto& T = mesh.triangles;
        if (V.empty() || T.size() < 3) continue;
        ++drawn;
        unsigned int tex = TextureFor(mr->texture);
        // Custom (.okayshader) surface: its own program, or the magenta error material when it does not compile.
        CustomGL* cust = nullptr;
        bool custBad = false;
        if (mr->shader == MeshRenderer::Shader::Custom) {
            cust = EnsureCustomProgram(mr->customShader);
            custBad = cust == nullptr;
            if (custBad) tex = 0;
        }

        // Geometry source: static meshes draw from the per-mesh cached GPU buffer
        // (expanded + uploaded once — see EnsureMeshVB); meshes deformed on the CPU
        // every frame by a Character rebuild + re-upload dynamically.
        // SkinnedMesh is cached as well: re-expanded only when its version moves
        // (once per animated frame, shared by every view and pass).
        const bool deformed = !it.batched && go->GetComponent<Character>() != nullptr;
        MeshVB* mvb = nullptr;
        const SkinnedMesh* gpuSkin = nullptr;
        if (!deformed) {
            const auto* skin = it.batched ? nullptr : go->GetComponent<SkinnedMesh>();
            gpuSkin = skin;
            mvb = EnsureMeshVB(mesh, MeshKey(skin));
            if (!mvb) continue;                    // no drawable triangles
        } else {
            BuildExpandedVerts(mesh);
            if (m_verts.empty()) continue;
        }

        Mat4 model = it.model;
        Mat4 mvp = vp * model;
        if (cust) BeginCustom(*cust, *mr);
        g.UniformMatrix4fv(m_uMVP, 1, GL_FALSE, mvp.m);
        g.UniformMatrix4fv(m_uModel, 1, GL_FALSE, model.m);
        bool unlit = cust ? cust->unlit : (custBad || mr->unlit || mr->shader == MeshRenderer::Shader::Unlit);
        float tintR = 1, tintG = 1, tintB = 1;
        if (it.lod >= 0) if (auto* lg = go->GetComponent<LODGroup>(); lg) lg->DebugTint(it.lod, tintR, tintG, tintB);
        if (custBad) {
            g.Uniform3f(m_uColor, oshader::kErrorColor[0], oshader::kErrorColor[1], oshader::kErrorColor[2]);
            g.Uniform3f(m_uEmissive, 0.0f, 0.0f, 0.0f);
        } else {
        g.Uniform3f(m_uColor, mr->color.r * tintR, mr->color.g * tintG, mr->color.b * tintB);
        g.Uniform3f(m_uEmissive, mr->emissive.r, mr->emissive.g, mr->emissive.b);
        }
        g.Uniform1f(m_uSpecular, unlit ? 0.0f : mr->specular);
        g.Uniform1f(m_uShininess, mr->shininess);
        g.Uniform1f(m_uUnlit, unlit ? 1.0f : 0.0f);
        g.Uniform1f(m_uReceive, mr->receiveShadows ? 1.0f : 0.0f);
        g.Uniform1f(m_uShaderMode, (cust || custBad) ? 0.0f : (float)(int)mr->shader);
        g.Uniform1f(m_uToonBands, (float)mr->toonBands);
        g.Uniform1f(m_uRimStr, mr->rimStrength);
        g.Uniform3f(m_uRimColor, mr->rimColor.r, mr->rimColor.g, mr->rimColor.b);
        g.Uniform3f(m_uGradTop, mr->gradientTop.r, mr->gradientTop.g, mr->gradientTop.b);
        g.Uniform3f(m_uGradBot, mr->gradientBottom.r, mr->gradientBottom.g, mr->gradientBottom.b);
        // The base picture: the object's own texture, or (below) a slot's own picture for that slot's triangles.
        auto applyBaseTex = [&](unsigned int bt) {
        if (bt) {
            g.ActiveTexture(GL_TEXTURE0);
            g.BindTexture(GL_TEXTURE_2D, bt);
            // Per-material filter: Pixel = crisp nearest (no up-close blur); Smooth =
            // trilinear. Set on the bound texture each draw so one cache serves both.
            // The picture's own import settings (Filter / Mipmaps) apply too: a Point picture is crisp
            // whatever the material says, and a picture without mipmaps must not ask for them.
            TextureSampling ts;
            if (auto ti = m_texInfo.find(bt); ti != m_texInfo.end()) ts = ti->second;
            const bool pixel = mr->texFilter == MeshRenderer::TexFilter::Pixel || ts.filter == TextureSampling::Point;
            const int minF = !ts.mipmaps ? (pixel ? GL_NEAREST : GL_LINEAR)
                           : pixel ? GL_NEAREST_MIPMAP_NEAREST
                           : ts.filter == TextureSampling::Bilinear ? 0x2701 /*GL_LINEAR_MIPMAP_NEAREST*/
                                                                    : GL_LINEAR_MIPMAP_LINEAR;
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, pixel ? GL_NEAREST : GL_LINEAR);
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minF);
            g.Uniform1i(m_uTex, 0);
            g.Uniform1f(m_uUseTex, 1.0f);
            g.Uniform2f(m_uTiling, mr->tiling.x, mr->tiling.y);
        } else {
            g.Uniform1f(m_uUseTex, 0.0f);
            g.Uniform2f(m_uTiling, 1.0f, 1.0f);
        }
        };
        applyBaseTex(tex);
        if (cust) g.Uniform2f(m_uTiling, mr->tiling.x, mr->tiling.y);   // a custom surface's uv is tiled with or without a picture

        // Normal/bump map on texture unit 1: perturbs the surface normal per fragment
        // so flat faces show sculpted detail under lighting, matching the software path.
        unsigned int ntex = mr->normalMap.empty() ? 0u : TextureFor(mr->normalMap);
        if (ntex) {
            g.ActiveTexture(GL_TEXTURE1);
            g.BindTexture(GL_TEXTURE_2D, ntex);
            g.Uniform1i(m_uNormalTex, 1);
            g.Uniform1f(m_uHasNormal, 1.0f);
            g.Uniform1f(m_uNormalStrength, mr->normalStrength);
            g.ActiveTexture(GL_TEXTURE0);
        } else {
            g.Uniform1f(m_uHasNormal, 0.0f);
        }

        // Gloss/specular map on unit 2 (per-texel specular + reflection scale).
        unsigned int stex = mr->specularMap.empty() ? 0u : TextureFor(mr->specularMap);
        if (stex) {
            g.ActiveTexture(GL_TEXTURE2);
            g.BindTexture(GL_TEXTURE_2D, stex);
            g.Uniform1i(m_uSpecTex, 2);
            g.Uniform1f(m_uHasSpecMap, 1.0f);
            g.ActiveTexture(GL_TEXTURE0);
        } else {
            g.Uniform1f(m_uHasSpecMap, 0.0f);
        }

        // Ambient-occlusion map on unit 3 (darken ambient + diffuse in creases).
        unsigned int aotex = mr->aoMap.empty() ? 0u : TextureFor(mr->aoMap);
        if (aotex) {
            g.ActiveTexture(GL_TEXTURE3);
            g.BindTexture(GL_TEXTURE_2D, aotex);
            g.Uniform1i(m_uAoTex, 3);
            g.Uniform1f(m_uHasAo, 1.0f);
            g.Uniform1f(m_uAoStrength, mr->aoStrength);
            g.ActiveTexture(GL_TEXTURE0);
        } else {
            g.Uniform1f(m_uHasAo, 0.0f);
        }

        unsigned int mrtex = mr->metallicRoughnessMap.empty() ? 0u : TextureFor(mr->metallicRoughnessMap);
        if (mrtex) {
            g.ActiveTexture(GL_TEXTURE0 + 6);
            g.BindTexture(GL_TEXTURE_2D, mrtex);
            g.Uniform1f(m_uHasMR, 1.0f);
            g.ActiveTexture(GL_TEXTURE0);
        } else g.Uniform1f(m_uHasMR, 0.0f);
        unsigned int etex = mr->emissiveMap.empty() ? 0u : TextureFor(mr->emissiveMap);
        if (etex) {
            g.ActiveTexture(GL_TEXTURE0 + 7);
            g.BindTexture(GL_TEXTURE_2D, etex);
            g.Uniform1f(m_uHasEmis, 1.0f);
            g.ActiveTexture(GL_TEXTURE0);
        } else g.Uniform1f(m_uHasEmis, 0.0f);

        // PBR scalars + texture offset (slide/atlas). Metalness/reflection tint by albedo.
        g.Uniform1f(m_uMetallic, unlit ? 0.0f : mr->metallic);
        g.Uniform1f(m_uReflectivity, unlit ? 0.0f : mr->reflectivity);
        g.Uniform2f(m_uTexOffset, mr->texOffset.x, mr->texOffset.y);

        GLsizei drawCount;
        if (mvb) {                                  // cached static buffer: just bind
            g.BindBuffer(GL_ARRAY_BUFFER, mvb->vbo);
            drawCount = (GLsizei)mvb->vertCount;
        } else {                                    // deformed: stream this frame's verts
            g.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
            g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)(m_verts.size() * sizeof(float)), m_verts.data(), GL_DYNAMIC_DRAW);
            drawCount = (GLsizei)(m_verts.size() / 14);
        }
        const GLsizei stride = 14 * sizeof(float);
        g.EnableVertexAttribArray(0);
        g.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)0);
        g.EnableVertexAttribArray(1);
        g.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(3 * sizeof(float)));
        g.EnableVertexAttribArray(2);
        g.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(6 * sizeof(float)));
        g.EnableVertexAttribArray(3);
        g.VertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(8 * sizeof(float)));
        g.EnableVertexAttribArray(4);
        g.VertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(11 * sizeof(float)));
        BindSkin(mvb ? gpuSkin : nullptr, mesh, mvb, m_uSkin, m_uBones);
        // Transparency: a material alpha < 1 (water, glass) alpha-blends over the
        // scene and doesn't write depth, so geometry behind shows through. (Objects
        // draw in scene order — keep transparent meshes after opaque ones.)
        float matAlpha = custBad ? 1.0f : mr->color.a;
        bool transparent = matAlpha < 0.999f || (cust && cust->writesAlpha);   // a shader that sets alpha blends
        g.Uniform1f(m_uAlpha, matAlpha);
        if (transparent) {
            g.Enable(GL_BLEND);
            g.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            g.DepthMask(GL_FALSE);
        }
        // Per-slot pictures: with material slots that have their own picture, draw one range per slot, each with its own texture.
        bool slotDraw = false;
        if (mvb && !mvb->slotRanges.empty() && !mr->slotTextures.empty())
            for (const std::string& st : mr->slotTextures) if (!st.empty()) slotDraw = true;
        if (slotDraw) {
            unsigned int bound = tex;
            for (const auto& rg : mvb->slotRanges) {
                if (rg[2] <= 0) continue;
                unsigned int want = tex;
                if (rg[0] >= 0 && rg[0] < (int)mr->slotTextures.size() && !mr->slotTextures[rg[0]].empty()) {
                    const unsigned int st = TextureFor(mr->slotTextures[rg[0]]);
                    if (st) want = st;
                }
                if (want != bound) { applyBaseTex(want); bound = want; }
                g.DrawArrays(GL_TRIANGLES, rg[1], rg[2]);
            }
            if (bound != tex) applyBaseTex(tex);              // leave the state as the next object expects it
        } else
        g.DrawArrays(GL_TRIANGLES, 0, drawCount);
        if (transparent) {
            g.DepthMask(GL_TRUE);
            g.Disable(GL_BLEND);
            g.Uniform1f(m_uAlpha, 1.0f);
        }

        // Ground contact shadow: re-draw the SAME geometry flattened onto the ground
        // plane (a planar projection along the light) as a flat translucent black, so
        // the object reads as sitting on the ground instead of floating.
        // Only a fallback: with real shadow maps on, it would double-darken the
        // shadow and show through anything between the object and the plane.
        if (planarShadows && !go->firstPersonOnly && mr->groundShadow && mr->castShadows && mr->groundShadowStrength > 0.001f) {
            Mat4 shadowModel = Mat4::PlanarShadow(mr->groundShadowY + 0.01f, lightTravel) * model;
            Mat4 smvp = vp * shadowModel;
            g.UniformMatrix4fv(m_uMVP, 1, GL_FALSE, smvp.m);
            g.Uniform1f(m_uShadowAlpha, mr->groundShadowStrength > 1.0f ? 1.0f : mr->groundShadowStrength);
            g.Enable(GL_BLEND);
            g.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            // Every flattened triangle lies on the same plane: with a strict depth
            // test + depth writes, overlapping triangles (front and back faces,
            // other objects' shadows) are blended once instead of stacking darker.
            g.DepthFunc(GL_LESS);
            g.DrawArrays(GL_TRIANGLES, 0, drawCount);
            g.DepthFunc(GL_LEQUAL);
            g.Disable(GL_BLEND);
            g.Uniform1f(m_uShadowAlpha, -1.0f);
        }
        if (cust) EndCustom(*cust);
    }

    LastCullCounters().drawn = drawn;
    g.DisableVertexAttribArray(5); g.DisableVertexAttribArray(6);   // skin inputs off for the post passes
    // Resolve MSAA into the single-sample texture.
    g.BindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
    g.BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    g.BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    const int hw = w / 2 > 0 ? w / 2 : 1, hh = h / 2 > 0 ? h / 2 : 1;
    // One fullscreen pass: program + source texture on unit 0 -> target FBO.
    auto quadPass = [&](unsigned int prog, unsigned int fbo, int vw, int vh, unsigned int srcTex, int uSrc) {
        g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        g.Viewport(0, 0, vw, vh);
        g.UseProgram(prog);
        g.ActiveTexture(GL_TEXTURE0);
        g.BindTexture(GL_TEXTURE_2D, srcTex);
        if (uSrc >= 0) g.Uniform1i(uSrc, 0);
    };
    // AO estimate + one blur round from the linear-depth texture into m_ssaoTexA.
    auto computeAo = [&](float radius) {
        g.Disable(GL_DEPTH_TEST);
        quadPass(m_ssaoProg, m_ssaoFboA, m_ssaoW, m_ssaoH, m_ssaoDepthTex, m_usDepth);
        g.Uniform2f(m_usTexel, 1.0f / (float)m_ssaoW, 1.0f / (float)m_ssaoH);
        g.Uniform1f(m_usRadius, radius);
        g.Uniform1f(m_usFar, kSsaoFar);
        DrawQuad();
        quadPass(m_blurProg, m_ssaoFboB, m_ssaoW, m_ssaoH, m_ssaoTexA, m_ublSrc);
        g.Uniform2f(m_ublDir, 1.0f / (float)m_ssaoW, 0.0f);
        DrawQuad();
        quadPass(m_blurProg, m_ssaoFboA, m_ssaoW, m_ssaoH, m_ssaoTexB, m_ublSrc);
        g.Uniform2f(m_ublDir, 0.0f, 1.0f / (float)m_ssaoH);
        DrawQuad();
    };
    // Multiply the AO onto a target (dst *= src).
    auto multiplyAo = [&](unsigned int fbo, float strength, float power) {
        quadPass(m_mulProg, fbo, w, h, m_ssaoTexA, m_umSrc);
        g.Uniform1f(m_umStrength, strength > 1.0f ? 1.0f : strength);
        g.Uniform1f(m_umPower, power);
        g.Enable(GL_BLEND);
        g.BlendFunc(GL_DST_COLOR, GL_ZERO);
        DrawQuad();
        g.Disable(GL_BLEND);
    };

    const bool useChain = chain && EnsurePostTargets(w, h, m_hdr);
    PostVignetteHandled() = useChain && rsTop.vignette + rsTop.vignettePulse > 0.0f;
    unsigned int readFbo = m_resolveFbo;
    if (useChain) {
        const auto& ps = rsTop.post;
        g.Disable(GL_BLEND);
        // 1) Linear depth (half res) for SSAO and depth of field.
        const bool ssaoOn = ps.ssao || SSAOEnabled();
        bool haveDepth = false;
        if ((ssaoOn || ps.dof) && EnsureSsaoProgs() && EnsureSsaoTargets(hw, hh)) {
            DrawLinearDepth(scene, vp, eye, ignore, viewFr);
            haveDepth = true;
        }
        g.Disable(GL_DEPTH_TEST);
        if (ssaoOn && haveDepth) {
            computeAo(ps.ssao ? ps.ssaoRadius : SSAORadius());
            multiplyAo(m_resolveFbo, ps.ssao ? ps.ssaoIntensity : SSAOStrength(), ps.ssao ? ps.ssaoPower : 1.0f);
        }
        // 2) Bloom: bright-pass to half res, blur, down to quarter, blur. Both
        //    levels are added in the uber pass (a wide soft glow + a tight one).
        const bool bloomOn = ps.bloom || BloomEnabled();
        const float bloomThresh = ps.bloom ? ps.bloomThreshold : BloomThreshold();
        const float bloomIntensity = ps.bloom ? ps.bloomIntensity : BloomIntensity();
        const float bloomRadius = ps.bloom ? ps.bloomRadius : 1.0f;
        const int qw = m_bloomW / 2 > 0 ? m_bloomW / 2 : 1, qh = m_bloomH / 2 > 0 ? m_bloomH / 2 : 1;
        if (bloomOn) {
            quadPass(m_downProg, m_bloomFboA, m_bloomW, m_bloomH, m_resolveTex, m_udnSrc);
            g.Uniform2f(m_udnTexel, 1.0f / (float)w, 1.0f / (float)h);
            g.Uniform1f(m_udnThresh, bloomThresh);
            g.Uniform1f(m_udnSoft, ps.bloom ? ps.bloomSoftness : 0.0f);
            g.Uniform1f(m_udnRadius, 1.0f);
            DrawQuad();
            for (int it = 0; it < 2; ++it) {
                quadPass(m_blurProg, m_bloomFboB, m_bloomW, m_bloomH, m_bloomTexA, m_ublSrc);
                g.Uniform2f(m_ublDir, bloomRadius / (float)m_bloomW, 0.0f);
                DrawQuad();
                quadPass(m_blurProg, m_bloomFboA, m_bloomW, m_bloomH, m_bloomTexB, m_ublSrc);
                g.Uniform2f(m_ublDir, 0.0f, bloomRadius / (float)m_bloomH);
                DrawQuad();
            }
            quadPass(m_downProg, m_bloomFboC, qw, qh, m_bloomTexA, m_udnSrc);
            g.Uniform2f(m_udnTexel, 1.0f / (float)m_bloomW, 1.0f / (float)m_bloomH);
            g.Uniform1f(m_udnThresh, 0.0f);
            g.Uniform1f(m_udnSoft, 0.0f);
            g.Uniform1f(m_udnRadius, 1.0f);
            DrawQuad();
            for (int it = 0; it < 2; ++it) {
                quadPass(m_blurProg, m_bloomFboD, qw, qh, m_bloomTexC, m_ublSrc);
                g.Uniform2f(m_ublDir, bloomRadius * 1.5f / (float)qw, 0.0f);
                DrawQuad();
                quadPass(m_blurProg, m_bloomFboC, qw, qh, m_bloomTexD, m_ublSrc);
                g.Uniform2f(m_ublDir, 0.0f, bloomRadius * 1.5f / (float)qh);
                DrawQuad();
            }
        }
        // 3) Exposure, bloom composite, tonemap, grading, vignette.
        const bool dofOn = ps.dof && haveDepth;
        int after = (dofOn ? 1 : 0) + (ps.fxaa ? 1 : 0);
        int ping = 0;
        auto nextTarget = [&]() -> unsigned int {   // where the current pass writes
            if (after == 0) return m_outFbo;
            --after;
            return m_pingFbo[ping];
        };
        unsigned int cur = m_resolveTex;
        {
            unsigned int dst = nextTarget();
            quadPass(m_uberProg, dst, w, h, cur, m_uuSrc);
            g.ActiveTexture(GL_TEXTURE1); g.BindTexture(GL_TEXTURE_2D, m_bloomTexA); g.Uniform1i(m_uuBloomA, 1);
            g.ActiveTexture(GL_TEXTURE2); g.BindTexture(GL_TEXTURE_2D, m_bloomTexC); g.Uniform1i(m_uuBloomC, 2);
            g.ActiveTexture(GL_TEXTURE0);
            g.Uniform1f(m_uuExposure, ExposureScale(rsTop));
            g.Uniform1f(m_uuLinearIn, hdrOut ? 1.0f : 0.0f);
            g.Uniform1f(m_uuTonemap, (float)ps.tonemap);
            g.Uniform1f(m_uuBloomOn, bloomOn ? 1.0f : 0.0f);
            g.Uniform1f(m_uuBloomIntensity, bloomIntensity);
            g.Uniform1f(m_uuGradeOn, ps.grading ? 1.0f : 0.0f);
            g.Uniform1f(m_uuTemperature, ps.temperature);
            g.Uniform1f(m_uuTint, ps.tint);
            g.Uniform1f(m_uuSaturation, ps.saturation);
            g.Uniform1f(m_uuContrast, ps.contrast);
            g.Uniform3f(m_uuLift, ps.lift.x, ps.lift.y, ps.lift.z);
            g.Uniform3f(m_uuGamma, ps.gamma.x, ps.gamma.y, ps.gamma.z);
            g.Uniform3f(m_uuGain, ps.gain.x, ps.gain.y, ps.gain.z);
            g.Uniform1f(m_uuVignette, rsTop.vignette + rsTop.vignettePulse > 1.0f ? 1.0f : rsTop.vignette + rsTop.vignettePulse);
            g.Uniform1f(m_uuVignetteSmooth, ps.vignetteSmooth);
            g.Uniform1f(m_uuAspect, (float)w / (float)h);
            DrawQuad();
            cur = dst == m_outFbo ? m_outTex : m_pingTex[ping];
            if (dst != m_outFbo) ping ^= 1;
        }
        // 4) Depth of field: a blurred copy (half res, two blur rounds) mixed in
        //    by the distance from the focus plane.
        if (dofOn) {
            quadPass(m_dofCopyProg, m_bloomFboA, m_bloomW, m_bloomH, cur, m_udcSrc);   // premultiplied half-res copy
            DrawQuad();
            for (int it = 0; it < 2; ++it) {
                quadPass(m_dofBlurProg, m_bloomFboB, m_bloomW, m_bloomH, m_bloomTexA, m_udbSrc);
                g.Uniform2f(m_udbDir, 1.0f / (float)m_bloomW, 0.0f);
                DrawQuad();
                quadPass(m_dofBlurProg, m_bloomFboA, m_bloomW, m_bloomH, m_bloomTexB, m_udbSrc);
                g.Uniform2f(m_udbDir, 0.0f, 1.0f / (float)m_bloomH);
                DrawQuad();
            }
            unsigned int dst = nextTarget();
            quadPass(m_dofProg, dst, w, h, cur, m_udfSrc);
            g.ActiveTexture(GL_TEXTURE1); g.BindTexture(GL_TEXTURE_2D, m_bloomTexA); g.Uniform1i(m_udfBlur, 1);
            g.ActiveTexture(GL_TEXTURE2); g.BindTexture(GL_TEXTURE_2D, m_ssaoDepthTex); g.Uniform1i(m_udfDepth, 2);
            g.ActiveTexture(GL_TEXTURE0);
            g.Uniform1f(m_udfFocus, ps.dofFocus);
            g.Uniform1f(m_udfRange, ps.dofRange);
            g.Uniform1f(m_udfFar, kSsaoFar);
            DrawQuad();
            cur = dst == m_outFbo ? m_outTex : m_pingTex[ping];
            if (dst != m_outFbo) ping ^= 1;
        }
        // 5) FXAA last, on the display image.
        if (ps.fxaa) {
            unsigned int dst = nextTarget();
            quadPass(m_fxaaProg, dst, w, h, cur, m_ufSrc);
            g.Uniform2f(m_ufTexel, 1.0f / (float)w, 1.0f / (float)h);
            DrawQuad();
        }
        g.Enable(GL_DEPTH_TEST);
        g.BindTexture(GL_TEXTURE_2D, 0);
        g.ClearColor(clearR, clearG, clearB, clearA);     // restore the caller's clear
        readFbo = m_outFbo;
    } else {
        // SSAO: darken corners/creases/contact points. Re-render opaque geometry at
        // half res as packed linear depth, estimate occlusion from depth differences,
        // blur once, multiply onto the frame. Runs BEFORE bloom (darken, then glow).
        if (SSAOEnabled() && EnsurePostProgs() && EnsureSsaoProgs() && EnsureSsaoTargets(hw, hh)) {
            DrawLinearDepth(scene, vp, eye, ignore, viewFr);
            computeAo(SSAORadius());
            multiplyAo(m_resolveFbo, SSAOStrength(), 1.0f);
            g.Enable(GL_DEPTH_TEST);
            g.BindTexture(GL_TEXTURE_2D, 0);
            g.ClearColor(clearR, clearG, clearB, clearA);     // restore the caller's clear
        }

        // Bloom: bright-pass the resolved frame at half res, blur, add back on top.
        // Any failure just skips the effect (the frame is already complete without it).
        if (BloomEnabled() && EnsurePostProgs() && EnsureBloomTargets(hw, hh)) {
            g.Disable(GL_DEPTH_TEST);
            g.Disable(GL_BLEND);
            // 1) bright-pass: resolve texture -> half-res A
            quadPass(m_brightProg, m_bloomFboA, m_bloomW, m_bloomH, m_resolveTex, m_ubSrc);
            g.Uniform1f(m_ubThresh, BloomThreshold());
            DrawQuad();
            // 2) two separable gaussian rounds: A -> B (horizontal), B -> A (vertical)
            for (int it = 0; it < 2; ++it) {
                quadPass(m_blurProg, m_bloomFboB, m_bloomW, m_bloomH, m_bloomTexA, m_ublSrc);
                g.Uniform2f(m_ublDir, 1.0f / (float)m_bloomW, 0.0f);
                DrawQuad();
                quadPass(m_blurProg, m_bloomFboA, m_bloomW, m_bloomH, m_bloomTexB, m_ublSrc);
                g.Uniform2f(m_ublDir, 0.0f, 1.0f / (float)m_bloomH);
                DrawQuad();
            }
            // 3) additive composite onto the resolved frame. Alpha stays as it
            //    was: the editor paints the sky behind pixels the 3D pass left
            //    clear, and an opaque add turned that sky black and hazy.
            quadPass(m_addProg, m_resolveFbo, w, h, m_bloomTexA, m_uaSrc);
            g.Uniform1f(m_uaStrength, BloomIntensity());
            g.Enable(GL_BLEND);
            g.BlendFunc(GL_ONE, GL_ONE);   // shader alpha is 0, so the clear/sky alpha is kept
            DrawQuad();
            g.Disable(GL_BLEND);
            g.Enable(GL_DEPTH_TEST);
            g.BindTexture(GL_TEXTURE_2D, 0);
        }
    }

    g.BindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    m_pixels.assign((std::size_t)w * h, 0u);
    g.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
    g.BindFramebuffer(GL_FRAMEBUFFER, 0);

    // If anything in the pipeline errored (unsupported feature, bad state), bail to
    // the caller so it can fall back to the software renderer instead of showing junk.
    if (g.GetError() != 0) { Log::Error("[gl] render produced a GL error; falling back to software"); return nullptr; }

    // GL's origin is bottom-left; the editor/SDL texture is top-left — flip rows.
    for (int y = 0; y < h / 2; ++y) {
        std::uint32_t* a = m_pixels.data() + (std::size_t)y * w;
        std::uint32_t* b = m_pixels.data() + (std::size_t)(h - 1 - y) * w;
        for (int x = 0; x < w; ++x) std::swap(a[x], b[x]);
    }
    return m_pixels.data();
}

// Free only the size-dependent render targets (rebuilt on viewport/sample change).
// Crucially this does NOT touch the VBO/VAO/program — those persist for the life of
// the renderer; deleting the VBO here (the old Destroy() did) left the draw calls
// bound to buffer 0, i.e. a client array at address 0, segfaulting on the next draw.
void GLRenderer::DestroyTargets() {
    if (m_colorRb) { g.DeleteRenderbuffers(1, &m_colorRb); m_colorRb = 0; }
    if (m_depthRb) { g.DeleteRenderbuffers(1, &m_depthRb); m_depthRb = 0; }
    if (m_fbo) { g.DeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
    if (m_resolveTex) { g.DeleteTextures(1, &m_resolveTex); m_resolveTex = 0; }
    if (m_resolveFbo) { g.DeleteFramebuffers(1, &m_resolveFbo); m_resolveFbo = 0; }
    m_w = m_h = m_samples = 0;
}

void GLRenderer::Destroy() {
    for (SizeSet& ps : m_parkedSets) DeleteSizeSet(ps);
    m_parkedSets.clear();
    DestroyTargets();
    DestroyShadow();
    DestroyBloom();
    DestroySsao();
    DestroyPostTargets();
    for (auto& kv : m_texCache) if (kv.second) g.DeleteTextures(1, &kv.second);
    for (auto& kv : m_uploadInfo) if (kv.second.gpuBytes) MemoryTracker::Global().Remove(GpuTextureCategory(), std::int64_t(kv.second.gpuBytes), 1);
    m_texCache.clear();
    m_texInfo.clear();
    m_uploadInfo.clear();
    m_texCapsKnown = false;
    for (auto& kv : m_meshVB) if (kv.second.vbo) g.DeleteBuffers(1, &kv.second.vbo);
    m_meshVB.clear();
    for (auto& kv : m_custom) if (kv.second.prog) g.DeleteProgram(kv.second.prog);
    m_custom.clear();
    if (m_whiteTex) { g.DeleteTextures(1, &m_whiteTex); m_whiteTex = 0; }
    if (m_vbo) { g.DeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_quadVbo) { g.DeleteBuffers(1, &m_quadVbo); m_quadVbo = 0; }
    if (m_vao && g.DeleteVertexArrays) { g.DeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_depthProg) { g.DeleteProgram(m_depthProg); m_depthProg = 0; }
    if (m_brightProg) { g.DeleteProgram(m_brightProg); m_brightProg = 0; }
    if (m_blurProg) { g.DeleteProgram(m_blurProg); m_blurProg = 0; }
    if (m_addProg) { g.DeleteProgram(m_addProg); m_addProg = 0; }
    if (m_skyProg) { g.DeleteProgram(m_skyProg); m_skyProg = 0; }
    if (m_uberProg) { g.DeleteProgram(m_uberProg); m_uberProg = 0; }
    if (m_fxaaProg) { g.DeleteProgram(m_fxaaProg); m_fxaaProg = 0; }
    if (m_dofProg) { g.DeleteProgram(m_dofProg); m_dofProg = 0; }
    if (m_dofCopyProg) { g.DeleteProgram(m_dofCopyProg); m_dofCopyProg = 0; }
    if (m_dofBlurProg) { g.DeleteProgram(m_dofBlurProg); m_dofBlurProg = 0; }
    if (m_downProg) { g.DeleteProgram(m_downProg); m_downProg = 0; }
    if (m_ssaoDepthProg) { g.DeleteProgram(m_ssaoDepthProg); m_ssaoDepthProg = 0; }
    if (m_ssaoProg) { g.DeleteProgram(m_ssaoProg); m_ssaoProg = 0; }
    if (m_mulProg) { g.DeleteProgram(m_mulProg); m_mulProg = 0; }
    if (m_prog) { g.DeleteProgram(m_prog); m_prog = 0; }
}

} // namespace okay
