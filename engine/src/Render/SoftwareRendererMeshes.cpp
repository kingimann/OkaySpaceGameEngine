// Software renderer scene mesh drawing.
// Bodies of okay:: free functions moved out of engine/include/okay/Render/SoftwareRenderer.hpp (modularization plan M2);
// the declarations (with their default arguments) stay in the header. The code is unchanged.
#include "okay/Render/SoftwareRenderer.hpp"

namespace okay {

void RenderMeshes(Raster& r, const Scene& scene, const Mat4& vp, const Vec3& eye,
                         const GameObject* ignore) {
    const float W = (float)r.width, H = (float)r.height;
    const auto& rs = scene.renderSettings;
    const bool  fogOn = FogActive(rs);
    const Vec3  fogC = EffectiveFogColor(rs);   // authored, or the sky's horizon (Match Sky)
    const float fogR = fogC.x, fogG = fogC.y, fogB = fogC.z;
    // Refresh the environment sky (for reflective materials) from the scene: the
    // authored gradient, or the sun's palette when the sky is Procedural.
    EnvSky().enabled = rs.skybox;
    EffectiveSkyColors(rs, EnvSky().top, EnvSky().horizon, EnvSky().bottom);
    // Hemisphere ambient: split the flat ambient into a sky-tinted (up) and
    // ground-tinted (down) term, keeping the midpoint equal to the current
    // ambient so the overall level is unchanged — only its direction is added.
    // The scene's Ambient Mode (shared with the GPU shaders).
    SceneLights::Hemisphere() = rs.ambientMode != Scene::RenderSettings::AmbientFlat;
    SceneAmbientColors(rs, SceneLights::AmbientColor(), SceneLights::SkyAmbient(), SceneLights::GroundAmbient());
    // (OKAY_PROFILE scopes: free while the profiler is off; with it on they split a frame into the editor's Profiler rows
    // and tests/bench_frame.cpp's phase table.)
    const visibility::Frame* visPtr = nullptr;
    { OKAY_PROFILE("Render.Visibility"); visPtr = &visibility::Build(scene, vp, eye, r.width, r.height, visibility::RenderViewFovY(), ignore); }
    const visibility::Frame& vis = *visPtr;
    {
        OKAY_PROFILE("Render.ShadowMaps");
        RenderShadowMap(scene, vp, eye);   // depth-from-light pre-pass for cast shadows
        RenderLocalShadowMaps(scene, vp, eye);   // spot / point / area light shadow maps
    }
    if (SSAOEnabled() || rs.post.ssao) {      // allocate the SSAO G-buffer for this frame (the process-wide switch or the scene's own Ambient occlusion)
        std::size_t n = (std::size_t)r.width * r.height;
        r.gpos.assign(n, Vec3{0, 0, 0}); r.gnrm.assign(n, Vec3{0, 0, 0});
        r.gvalid.assign(n, 0);
    } else if (!r.gvalid.empty()) {
        r.gpos.clear(); r.gnrm.clear(); r.gvalid.clear();
    }
    // Pre-warm the texture / mip caches single-threaded: the lazy caches insert
    // into shared unordered_maps, which is not safe to do from the worker threads
    // below. After this, the parallel band render only READS them.
    // Frustum culling is decided here too (its bounds cache isn't thread-safe);
    // the bands only read the result.
    const Frustum viewFrustum = Frustum::FromVP(vp);
    (void)viewFrustum;
    // What the bands need to know about each object, worked out here once instead of once per band:
    // whether the visibility pass dropped it, which screen rows its drawn vertices can reach (a band of rows that does not meet
    // them has nothing of it to draw), and which lights can reach it.
    struct ObjInfo {
        bool culled = false;                   // a mesh renderer the visibility pass dropped
        bool rowsKnown = false;                // yRow0..yRow1 are the rows the mesh can touch (with a margin)
        int  yRow0 = 0, yRow1 = -1;
        int  lightFirst = 0, lightCount = -1;  // lightCount < 0: every light; otherwise lightPool[lightFirst ..]
    };
    const auto& sceneObjs = scene.Objects();
    std::vector<ObjInfo> objInfo(sceneObjs.size());
    std::vector<int> lightPool;
    const bool bandSkipOn = BandSkipEnabled();
    const bool lightCullOn = LightCullingEnabled();
    const auto& sceneLights = SceneLights::List();
    // From the vertices that are actually drawn this frame (so a mesh that is skinned, edited or deformed in place is as
    // safe as a still one), the bounding box in the world: its eight corners give the rows it can reach, and its distance
    // to each light says whether the light can reach it.
    auto meshReach = [&](const GameObject* go, const MeshRenderer* mr, ObjInfo& oi) {
        const Mesh* dm = vis.MeshFor(go);
        if (!dm || dm->vertices.empty() || !go->transform) return;
        const std::vector<Vec3>& vs = dm->vertices;
        Vec3 lo = vs[0], hi = vs[0];
        for (const Vec3& p : vs) {
            lo.x = p.x < lo.x ? p.x : lo.x; lo.y = p.y < lo.y ? p.y : lo.y; lo.z = p.z < lo.z ? p.z : lo.z;
            hi.x = p.x > hi.x ? p.x : hi.x; hi.y = p.y > hi.y ? p.y : hi.y; hi.z = p.z > hi.z ? p.z : hi.z;
        }
        if (!std::isfinite(lo.x + lo.y + lo.z + hi.x + hi.y + hi.z)) return;   // NaN or infinity in the data: no shortcut
        const Mat4 model = go->transform->LocalToWorldMatrix();
        Vec3 wlo{1e30f, 1e30f, 1e30f}, whi{-1e30f, -1e30f, -1e30f};
        float yLo = 1e30f, yHi = -1e30f;
        bool inFront = true;
        for (int c = 0; c < 8; ++c) {
            const Vec3 w = model.MultiplyPoint({(c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z});
            wlo.x = std::fmin(wlo.x, w.x); wlo.y = std::fmin(wlo.y, w.y); wlo.z = std::fmin(wlo.z, w.z);
            whi.x = std::fmax(whi.x, w.x); whi.y = std::fmax(whi.y, w.y); whi.z = std::fmax(whi.z, w.z);
            const Vec4 cl = vp * Vec4{w, 1.0f};
            if (!(cl.w > 1e-3f)) { inFront = false; continue; }
            const float iw = 1.0f / cl.w;
            const float sy = (1.0f - (cl.y * iw * 0.5f + 0.5f)) * H;
            yLo = std::fmin(yLo, sy); yHi = std::fmax(yHi, sy);
        }
        // Rows: only when the whole box is in front of the camera, so the projection is a plain perspective divide of a convex
        // box (a box crossing the camera plane could reach any row). Two rows of margin for the float rounding of the
        // vertices. The outline shell is drawn outside the mesh by the outline width, so an outlined mesh is left alone.
        if (bandSkipOn && inFront && !(mr->outline && mr->outlineWidth > 0.0f) && std::fabs(yLo) < 1.0e8f && std::fabs(yHi) < 1.0e8f) {
            oi.rowsKnown = true;
            oi.yRow0 = (int)std::floor(yLo) - 2;
            oi.yRow1 = (int)std::ceil(yHi) + 2;
        }
        // Lights: a point, spot or area light whose range does not reach the box adds exactly nothing (see LightSubset).
        if (lightCullOn && !sceneLights.empty() && !(mr->unlit || mr->shader == MeshRenderer::Shader::Unlit)) {
            const float mag = std::fmax(std::fmax(std::fabs(wlo.x), std::fabs(whi.x)), std::fmax(std::fmax(std::fabs(wlo.y), std::fabs(whi.y)), std::fmax(std::fabs(wlo.z), std::fabs(whi.z))));
            const float margin = 1e-3f + 1e-4f * mag + 1e-4f * ((whi.x - wlo.x) + (whi.y - wlo.y) + (whi.z - wlo.z));
            const std::size_t first = lightPool.size();
            for (std::size_t li = 0; li < sceneLights.size(); ++li) {
                const LightSample& L = sceneLights[li];
                bool reaches = true;
                if (L.type != 0 && std::isfinite(L.range) && std::isfinite(L.pos.x + L.pos.y + L.pos.z) &&
                    std::isfinite(L.color.x + L.color.y + L.color.z)) {
                    auto gap = [margin](float p, float a, float b) { return p < a - margin ? (a - margin) - p : (p > b + margin ? p - (b + margin) : 0.0f); };
                    const float dx = gap(L.pos.x, wlo.x, whi.x), dy = gap(L.pos.y, wlo.y, whi.y), dz = gap(L.pos.z, wlo.z, whi.z);
                    const float panel = L.type == 3 ? std::sqrt(L.halfW * L.halfW + L.halfH * L.halfH) : 0.0f;   // a panel reaches a bit farther than its centre
                    const float reach = (L.range + panel) * 1.0001f + 1e-3f;
                    if (L.range <= 0.0f || dx * dx + dy * dy + dz * dz >= reach * reach) reaches = false;
                }
                if (reaches) lightPool.push_back((int)li);
            }
            if (lightPool.size() - first == sceneLights.size()) lightPool.resize(first);   // every light reaches it: use the plain list
            else { oi.lightFirst = (int)first; oi.lightCount = (int)(lightPool.size() - first); }
        }
    };
    int swDrawn = 0;
    // Custom (.okayshader) surfaces, resolved here (the file cache and the texture caches are not
    // touched from the band threads): the compiled shader, the material's values, its pictures.
    struct CustomBind {
        std::shared_ptr<const oshader::Compiled> compiled;   // keeps the program alive for the frame
        std::vector<float> props;
        CustomSurface cs;
        bool ok = false;
    };
    std::vector<CustomBind> customBind(sceneObjs.size());
    {
    OKAY_PROFILE("Render.Prewarm");
    for (std::size_t oi = 0; oi < sceneObjs.size(); ++oi) {
        const auto& go = sceneObjs[oi];
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr) continue;
        if (!vis.Visible(go.get())) { objInfo[oi].culled = true; continue; }
        ++swDrawn;
        if (!mr->texture.empty())     GetCachedMips(mr->texture);
        for (const std::string& st : mr->slotTextures) if (!st.empty()) GetCachedMips(st);   // per-slot pictures (loaded here, read by the bands)
        if (!mr->normalMap.empty())   GetCachedMips(mr->normalMap);
        if (!mr->specularMap.empty()) GetCachedMips(mr->specularMap);
        if (!mr->aoMap.empty())       GetCachedMips(mr->aoMap);
        if (!mr->metallicRoughnessMap.empty()) GetCachedMips(mr->metallicRoughnessMap);
        if (!mr->emissiveMap.empty()) GetCachedMips(mr->emissiveMap);
        if (!mr->matcap.empty() && !mr->unlit && mr->shader != MeshRenderer::Shader::Unlit) GetCachedTexture(mr->matcap);
        if (mr->shader == MeshRenderer::Shader::Custom) {
            CustomBind& cb = customBind[oi];
            const oshader::ShaderFile sf = oshader::LoadShaderFile(mr->customShader);
            if (sf.compiled && sf.compiled->ok && sf.compiled->cpu) {
                cb.compiled = sf.compiled;
                cb.props = oshader::ResolveProperties(*sf.compiled, mr->shaderParams);
                cb.cs.prog = sf.compiled->cpu.get();
                cb.cs.props = cb.props.data();
                cb.cs.time = Time::ElapsedTime();
                cb.cs.unlit = sf.compiled->unlit;
                for (int t = 0; t < (int)sf.compiled->textures.size() && t < oshader::kMaxTextures; ++t) {
                    const std::string pic = oshader::ResolveTexture(*sf.compiled, t, mr->shaderParams);
                    cb.cs.tex[t] = pic.empty() ? nullptr : GetCachedMips(pic);
                }
                cb.ok = true;
            }
        }
        if (bandSkipOn || lightCullOn) meshReach(go.get(), mr, objInfo[oi]);
    }
    }
    // Rasterize the meshes across CPU cores: each worker owns a horizontal band
    // of scanlines [bandY0, bandY1] and only fills pixels inside it, so colour /
    // depth / G-buffer writes never overlap between threads (no locks needed).
    // Vertex transforms are repeated per band, but pixel fill dominates.
    auto renderBand = [&](int bandY0, int bandY1) {
    for (std::size_t objIndex = 0; objIndex < sceneObjs.size(); ++objIndex) {
        const auto& go = sceneObjs[objIndex];
        const ObjInfo& objI = objInfo[objIndex];
        if (bandSkipOn && objI.rowsKnown && (objI.yRow1 < bandY0 || objI.yRow0 > bandY1)) continue;   // none of it falls in this band
        if (go->HiddenFromCamera(ignore)) continue;   // owner's body, except the parts that camera is meant to see (shirt, raised-arm copy)
        if (!(RenderCullingMask() & (1 << (go->layer & 31)))) continue;   // camera layer culling mask
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !go->ActiveInHierarchy() || !mr->enabled || mr->wireframe) continue;   // wireframe drawn as lines
        // The Unlit shader is equivalent to the unlit flag for the renderer. A Custom shader that
        // does not compile draws flat magenta (the error material); one that does lights itself.
        const CustomBind& cbind = customBind[objIndex];
        const bool customOk = mr->shader == MeshRenderer::Shader::Custom && cbind.ok;
        const bool customBad = mr->shader == MeshRenderer::Shader::Custom && !cbind.ok;
        const bool unlit = !customOk && (mr->unlit || mr->shader == MeshRenderer::Shader::Unlit || customBad);
        // Scrolling-UV offset (animated textures): advance the texture coordinates by
        // the scroll speed * elapsed time. Added after tiling, like offset.
        const float scrollU = mr->uvScroll.x * Time::ElapsedTime();
        const float scrollV = mr->uvScroll.y * Time::ElapsedTime();
        Mat4 model = go->transform->LocalToWorldMatrix();
        const Mesh* lodMesh = vis.MeshFor(go.get());
        if (!lodMesh) { continue; }
        const Mesh& drawMesh = *lodMesh;
        const auto& v = drawMesh.vertices;
        const auto& t = drawMesh.triangles;
        const bool hasUV = drawMesh.uvs.size() == v.size();
        const bool faceCols = drawMesh.HasFaceColors();   // per-triangle colors?
        const MipChain* tex = (mr->texture.empty() || customBad) ? nullptr : GetCachedMips(mr->texture);
        // Per-slot pictures: triangles whose material slot has a texture use it instead of the object's own.
        std::vector<const MipChain*> slotTex;
        if (drawMesh.HasSlots() && !mr->slotTextures.empty())
            for (const std::string& st : mr->slotTextures) slotTex.push_back(st.empty() ? nullptr : GetCachedMips(st));
        bool anySlotTex = false;
        for (const MipChain* m : slotTex) if (m && !m->empty()) anySlotTex = true;
        const MipChain* normalMips = mr->normalMap.empty() ? nullptr : GetCachedMips(mr->normalMap);
        const MipChain* specMips = mr->specularMap.empty() ? nullptr : GetCachedMips(mr->specularMap);
        const MipChain* aoMips = mr->aoMap.empty() ? nullptr : GetCachedMips(mr->aoMap);
        Image* mcap = (mr->matcap.empty() || unlit || customOk) ? nullptr : GetCachedTexture(mr->matcap);
        // Matcap shading needs per-vertex normals; fall back if the mesh has none.
        const bool useMatcap = mcap && drawMesh.HasNormals();
        // Smooth (Gouraud) shading when the mesh carries per-vertex normals and
        // isn't textured/unlit — interpolated lighting across each face. Matcap
        // also rides the smooth path (it interpolates a per-vertex sampled color).
        // Lit, untextured meshes shade via the smooth (Gouraud) path even without
        // per-vertex normals — they use the face normal, interpolated per vertex, so
        // there are no flat-shading facet lines (only truly unlit meshes stay flat).
        const bool smooth = useMatcap || (!tex && !anySlotTex && !unlit);
        // Normal matrix = inverse-transpose of the model's linear part. Using the
        // model matrix directly SKEWS normals under non-uniform scale (e.g. a ground
        // scaled {40,1,40} or a stretched sphere) — lighting then "acts up" as the
        // scale departs from uniform. The cofactor basis (cross products of the
        // transformed axes) is the inverse-transpose up to a positive scale, which
        // the per-vertex Normalized() removes — so it's correct for any scale/shear.
        const Vec3 mAxX = model.MultiplyVector({1, 0, 0});
        const Vec3 mAxY = model.MultiplyVector({0, 1, 0});
        const Vec3 mAxZ = model.MultiplyVector({0, 0, 1});
        const Vec3 nrmX = Vec3::Cross(mAxY, mAxZ);   // inverse-transpose basis columns
        const Vec3 nrmY = Vec3::Cross(mAxZ, mAxX);
        const Vec3 nrmZ = Vec3::Cross(mAxX, mAxY);
        auto xformNormal = [&](const Vec3& n) {
            return Vec3{nrmX.x * n.x + nrmY.x * n.y + nrmZ.x * n.z,
                        nrmX.y * n.x + nrmY.y * n.y + nrmZ.y * n.z,
                        nrmX.z * n.x + nrmY.z * n.y + nrmZ.z * n.z};
        };

        // --- Per-mesh frustum cull: drop off-screen meshes before any
        // per-triangle work. ---
        // (A bounding sphere from bounds cached per mesh — recomputing the box of
        // every mesh every frame cost a full pass over its vertices.)
        if (objI.culled) continue;
        // Inverted-hull outline pre-pass: draw an expanded shell of BACK faces in a
        // flat outline color behind the mesh, so only a clean silhouette edge survives
        // the main pass. A cosmetic pass — triangles crossing the near plane are
        // skipped rather than clipped.
        if (mr->outline && mr->outlineWidth > 0.0f) {
            std::uint32_t oabgr = Raster::PackRGB(mr->outlineColor.r, mr->outlineColor.g, mr->outlineColor.b);
            for (std::size_t i = 0; i + 2 < t.size(); i += 3) {
                int idx[3] = {t[i], t[i + 1], t[i + 2]};
                Vec3 wp0[3];
                for (int k = 0; k < 3; ++k) wp0[k] = model.MultiplyPoint(v[idx[k]]);
                Vec3 fn = Vec3::Cross(wp0[1] - wp0[0], wp0[2] - wp0[0]).Normalized();
                Vec3 cen = (wp0[0] + wp0[1] + wp0[2]) * (1.0f / 3.0f);
                // Render the same faces the main pass shows (its visible surface), then
                // expand them OUTWARD along the face normal (meshes wind outward) and
                // push them a touch farther, so the unexpanded mesh covers them
                // everywhere except a clean silhouette ring.
                bool mainVisible = Vec3::Dot(fn, eye - cen) >= 0.0f;
                if (!mainVisible) continue;
                Vec3 outward = fn;
                float sxo[3], syo[3], sdo[3]; bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    Vec4 c = vp * Vec4{wp0[k] + outward * mr->outlineWidth, 1.0f};
                    if (c.w <= 1e-4f) { ok = false; break; }
                    float iw = 1.0f / c.w;
                    sxo[k] = (c.x * iw * 0.5f + 0.5f) * W;
                    syo[k] = (1.0f - (c.y * iw * 0.5f + 0.5f)) * H;
                    // Push the shell slightly farther (smaller 1/w) so the mesh always
                    // wins where they overlap — the outline only survives at the rim.
                    sdo[k] = iw * 0.9f;
                }
                if (ok) r.Triangle(sxo[0], syo[0], sdo[0], sxo[1], syo[1], sdo[1],
                                   sxo[2], syo[2], sdo[2], oabgr, bandY0, bandY1);
            }
        }

        // A clip-space vertex carrying its (unprojected) UV, for near-plane
        // clipping. Clipping in homogeneous space before the /w divide is what
        // prevents triangles from exploding/vanishing when you zoom in close.
        struct CV { float x, y, z, w, u, v, lr, lg, lb, nx, ny, nz, wx, wy, wz, fo, sp, vr, vg, vb; };   // vr/vg/vb: the vertex colour (white without one)
        // Once per mesh. Doing this inside the triangle loop made a plain scene
        // about twice as slow (every band, every triangle, a component lookup).
        float lodR = 1, lodG = 1, lodB = 1;
        if (auto* lg = go->GetComponent<LODGroup>()) lg->DebugTint(lg->activeLevel, lodR, lodG, lodB);
        Color shaded = mr->color;
        if (customBad) shaded = Color{oshader::kErrorColor[0], oshader::kErrorColor[1], oshader::kErrorColor[2], 1.0f};
        shaded.r *= lodR; shaded.g *= lodG; shaded.b *= lodB;
        // Per-pixel (Phong) shading for lit meshes that carry normals — smooth
        // surfaces + correct specular, textured or not. Matcap/unlit meshes
        // keep their existing paths. Meshes WITHOUT normals still go per-pixel:
        // each vertex carries the (constant) face normal, so they stay flat
        // shaded but gain per-pixel shadows, reflections, rim and hemisphere
        // ambient — which the basic flat path can't do. (This is what lets
        // primitives like Cube/Plane and the flat-shaded Character receive
        // cast shadows.)
        // Toon needs the per-pixel path (that's where the cel banding lives), so
        // it forces per-pixel lighting for its mesh regardless of the global toggle.
        const bool perPixel = (PerPixelLighting() || mr->shader == MeshRenderer::Shader::Toon
                               || mr->shader == MeshRenderer::Shader::Gradient
                               || mr->shader == MeshRenderer::Shader::Fresnel
                               || mr->shader == MeshRenderer::Shader::Iridescent
                               || mr->shader == MeshRenderer::Shader::Hologram
                               || mr->shader == MeshRenderer::Shader::Posterize
                               || mr->shader == MeshRenderer::Shader::Velvet
                               || customOk
                               || mr->rimStrength > 0.0f || mr->triplanar
                               || (ShadowsUsePerPixel() && mr->receiveShadows
                                   && (Shadows().enabled || LocalShadowsActive())))
                              && !unlit && !useMatcap;
        const int  shaderMode = (int)mr->shader;
        const float gradTopC[3] = {mr->gradientTop.r, mr->gradientTop.g, mr->gradientTop.b};
        const float gradBotC[3] = {mr->gradientBottom.r, mr->gradientBottom.g, mr->gradientBottom.b};
        // The flat path (unlit, untextured) is the only one that reads the per triangle colour worked out below: every other
        // path shades per vertex or per pixel, so that colour, its lighting, specular and fog are not computed for them.
        const bool flatPath = !perPixel && !tex && !smooth;
        LightSubset lightSubsetStore;
        const LightSubset* lightSubset = nullptr;     // the lights that can reach this mesh (nullptr: all of them)
        if (objI.lightCount >= 0) {
            lightSubsetStore.idx = lightPool.data() + objI.lightFirst;
            lightSubsetStore.n = objI.lightCount;
            lightSubset = &lightSubsetStore;
        }
        for (std::size_t i = 0; i + 2 < t.size(); i += 3) {
            int idx[3] = {t[i], t[i + 1], t[i + 2]};
            // This triangle's picture: its slot's texture if it has one, else the object's.
            const MipChain* triTex = tex;
            if (anySlotTex) {
                const int sl = drawMesh.triSlot[i / 3];
                if (sl >= 0 && sl < (int)slotTex.size() && slotTex[sl] && !slotTex[sl]->empty()) triTex = slotTex[sl];
            }
            Vec3 wp[3];
            for (int k = 0; k < 3; ++k) wp[k] = model.MultiplyPoint(v[idx[k]]);
            Vec3 normal = Vec3::Cross(wp[1] - wp[0], wp[2] - wp[0]).Normalized();
            Vec3 centroid = (wp[0] + wp[1] + wp[2]) * (1.0f / 3.0f);
            float facing = Vec3::Dot(normal, eye - centroid);
            // Backface culling: for a solid (single-sided) mesh, skip triangles
            // that face away from the camera — typically ~half the triangles of a
            // closed mesh, for free. Double-sided meshes (planes, billboards) keep
            // both faces and flip the normal so the lit side always shows.
            // A Custom shader that discards shows the far inside of the mesh through its holes, as the GPU
            // renderers do (they do not cull back faces): those faces keep their outward normal.
            const bool seeInside = facing < 0.0f && !mr->doubleSided && customOk && cbind.compiled->usesDiscard;
            if (facing < 0.0f && !seeInside) {
                if (!mr->doubleSided) continue;
                normal = normal * -1.0f;
            }

            // Clip coordinates of the three corners (used below as before).
            Vec4 clip3[3];
            for (int k = 0; k < 3; ++k) clip3[k] = vp * Vec4{wp[k], 1.0f};
            // A triangle that is wholly in front of the near plane and whose rows miss this thread's band draws nothing in
            // it (the rasterizers only visit rows floor(min y) .. ceil(max y)): do not shade it here, another band has it. The
            // rows are worked out with the same arithmetic the rasterizers get them from.
            if (bandSkipOn && clip3[0].z + clip3[0].w > 1e-5f && clip3[1].z + clip3[1].w > 1e-5f && clip3[2].z + clip3[2].w > 1e-5f) {
                float yMin = 1e30f, yMax = -1e30f;
                for (int k = 0; k < 3; ++k) {
                    const float iwk = 1.0f / clip3[k].w;
                    const float syk = (1.0f - (clip3[k].y * iwk * 0.5f + 0.5f)) * H;
                    yMin = std::fmin(yMin, syk); yMax = std::fmax(yMax, syk);
                }
                if (std::fabs(yMin) < 1.0e8f && std::fabs(yMax) < 1.0e8f &&
                    ((int)std::floor(yMin) > bandY1 || (int)std::ceil(yMax) < bandY0)) continue;
            }

            // Per-corner clip coords + UV (planar/box projection unless the mesh
            // carries its own UVs).
            Vec3 ln = Vec3::Cross(v[idx[1]] - v[idx[0]], v[idx[2]] - v[idx[0]]);
            float ax = std::fabs(ln.x), ay = std::fabs(ln.y), az = std::fabs(ln.z);
            float faceMetal = mr->metallic, faceShine = mr->shininess, faceSpec = mr->specular;
            float faceEr = mr->emissive.r, faceEg = mr->emissive.g, faceEb = mr->emissive.b;
            if (!mr->metallicRoughnessMap.empty() || !mr->emissiveMap.empty()) {
                float su = 0.0f, sv = 0.0f;
                for (int k = 0; k < 3; ++k) {
                    float uu, vt;
                    if (hasUV) { uu = drawMesh.uvs[idx[k]].x; vt = drawMesh.uvs[idx[k]].y; }
                    else {
                        const Vec3& p = v[idx[k]];
                        if (ax >= ay && ax >= az) { uu = p.z + 0.5f; vt = p.y + 0.5f; }
                        else if (ay >= ax && ay >= az) { uu = p.x + 0.5f; vt = p.z + 0.5f; }
                        else { uu = p.x + 0.5f; vt = p.y + 0.5f; }
                    }
                    su += uu; sv += vt;
                }
                su = su / 3.0f * mr->tiling.x + mr->texOffset.x + scrollU;
                sv = sv / 3.0f * mr->tiling.y + mr->texOffset.y + scrollV;
                if (!mr->metallicRoughnessMap.empty())
                    if (const auto* mm = GetCachedMips(mr->metallicRoughnessMap); mm && !mm->empty()) {
                        const Color c = (*mm)[0].Sample(su, sv);
                        faceMetal *= c.b;
                        float rough0 = 1.0f - (faceShine - 4.0f) / 124.0f;
                        if (rough0 < 0.0f) rough0 = 0.0f; if (rough0 > 1.0f) rough0 = 1.0f;
                        float rough = rough0 * c.g; if (rough < 0.0f) rough = 0.0f; if (rough > 1.0f) rough = 1.0f;
                        faceShine = 4.0f + (1.0f - rough) * 124.0f;
                        float specScale = 1.0f - rough; if (specScale < 0.04f) specScale = 0.04f;
                        faceSpec *= specScale;
                    }
                if (!mr->emissiveMap.empty())
                    if (const auto* em = GetCachedMips(mr->emissiveMap); em && !em->empty()) {
                        const Color c = (*em)[0].Sample(su, sv);
                        faceEr *= c.r; faceEg *= c.g; faceEb *= c.b;
                    }
            }
            CV in[3];
            float uvx[3], uvy[3];
            for (int k = 0; k < 3; ++k) {
                const Vec4& c = clip3[k];
                float uu, vt;
                if (hasUV) { uu = drawMesh.uvs[idx[k]].x; vt = drawMesh.uvs[idx[k]].y; }
                else {
                    const Vec3& p = v[idx[k]];
                    if (ax >= ay && ax >= az) { uu = p.z + 0.5f; vt = p.y + 0.5f; }
                    else if (ay >= ax && ay >= az) { uu = p.x + 0.5f; vt = p.z + 0.5f; }
                    else { uu = p.x + 0.5f; vt = p.y + 0.5f; }
                }
                uvx[k] = uu; uvy[k] = vt;
                // Per-vertex world normal (smoothed if the mesh has normals, else
                // the face normal), flipped to match a culled back face.
                Vec3 nk = drawMesh.HasNormals()
                              ? xformNormal(drawMesh.normals[idx[k]]).Normalized()
                              : normal;
                if (facing < 0.0f && !seeInside) nk = nk * -1.0f;

                // Per-vertex light color (Gouraud) from that normal; used by the
                // matcap and Gouraud paths. Per-pixel (Phong) shading uses nk + the
                // world position directly instead.
                Vec3 lk{1.0f, 1.0f, 1.0f};
                if (perPixel) {
                    // (shaded per pixel: this per vertex colour is never read)
                } else if (useMatcap) {
                    // Matcap: sample the lit-sphere by the camera-space normal.
                    Vec3 fwd = (wp[k] - eye).Normalized();
                    Vec3 rgt = Vec3::Cross(Vec3{0, 1, 0}, fwd);
                    float rl = rgt.Magnitude();
                    rgt = rl > 1e-4f ? rgt * (1.0f / rl) : Vec3{1, 0, 0};
                    Vec3 upv = Vec3::Cross(fwd, rgt);
                    float mx = Vec3::Dot(nk, rgt), my = Vec3::Dot(nk, upv);
                    float mu = mx * 0.5f + 0.5f, mv = 1.0f - (my * 0.5f + 0.5f);
                    Color mc = mcap->Sample(mu, mv);
                    lk = {mc.r, mc.g, mc.b};
                } else if (!unlit) {
                    // Per-vertex light (Gouraud) from the vertex normal — or the flat
                    // FACE normal when the mesh has none. Computing it per vertex (vs a
                    // single per-face value) interpolates smoothly across each face, so
                    // a flat ground under a point light has no hard facet line on its
                    // triangle diagonal even without per-pixel lighting.
                    lk = lightSubset ? ShadeNormalizedSubset(wp[k], nk.Normalized(), nullptr, *lightSubset)
                                     : SceneLights::ShadeColor(wp[k], nk);
                }
                // Per-vertex fog factor (distance from the eye), interpolated across
                // the triangle by the rasterizer. A single per-triangle factor made a
                // hard diagonal seam on big quads (e.g. the ground) when fog was on.
                float fok = 0.0f;
                if (fogOn) fok = FogFactor(rs, (wp[k] - eye).Magnitude(), eye.y, wp[k].y);   // linear / exponential / height
                // Per-vertex Blinn-Phong specular, interpolated by the rasterizer. A
                // single per-triangle value (from the centroid) made a hard diagonal
                // highlight seam on big quads like a shiny ground.
                float spk = 0.0f;
                if (!perPixel && !unlit && mr->specular > 0.0f) {
                    Vec3 toL = SceneLight::Direction() * -1.0f;
                    Vec3 toE = (eye - wp[k]).Normalized();
                    Vec3 hv = (toL.Normalized() + toE).Normalized();
                    float nh = Vec3::Dot(nk, hv);
                    if (nh > 0.0f) spk = std::pow(nh, faceShine) * faceSpec;
                }
                const Color vcc = drawMesh.HasVertexColors() ? drawMesh.vertexColors[idx[k]] : Color{1, 1, 1, 1};
                in[k] = {c.x, c.y, c.z, c.w, uu * mr->tiling.x + scrollU, vt * mr->tiling.y + scrollV,
                         lk.x, lk.y, lk.z, nk.x, nk.y, nk.z, wp[k].x, wp[k].y, wp[k].z, fok, spk, vcc.r, vcc.g, vcc.b};
            }

            // Per-triangle world-space tangent (from edges + UV deltas) for normal
            // mapping; constant across the face, so it survives near-plane clipping.
            Vec3 triTangent{1, 0, 0};
            if (normalMips) {
                Vec3 e1 = wp[1] - wp[0], e2 = wp[2] - wp[0];
                float du1 = uvx[1] - uvx[0], dv1 = uvy[1] - uvy[0];
                float du2 = uvx[2] - uvx[0], dv2 = uvy[2] - uvy[0];
                float det = du1 * dv2 - du2 * dv1;
                Vec3 T = std::fabs(det) > 1e-8f ? (e1 * dv2 - e2 * dv1) * (1.0f / det) : e1;
                float tl = T.Magnitude();
                triTangent = tl > 1e-6f ? T * (1.0f / tl) : Vec3{1, 0, 0};
            }

            // Sutherland-Hodgman clip against the true near plane. The plane is
            // z_clip >= -w_clip (NDC z >= -1), NOT just w > 0. Clipping on w alone
            // left geometry BETWEEN the camera and the near plane in the pipeline,
            // where 1/w explodes (up to ~1e4x) and projects vertices to gigantic
            // screen coordinates — precision blows up and the surface glitches when
            // you zoom in very close. Using the real near plane removes that zone
            // and keeps every drawn fragment's depth inside [-1,1].
            const float CLIP_EPS = 1e-5f;
            auto nearDist = [](const CV& c) { return c.z + c.w; };   // >0 = in front of near
            CV poly[8]; int pn = 0;
            for (int e = 0; e < 3; ++e) {
                const CV& A = in[e];
                const CV& B = in[(e + 1) % 3];
                float dA = nearDist(A), dB = nearDist(B);
                bool inA = dA > CLIP_EPS, inB = dB > CLIP_EPS;
                if (inA) poly[pn++] = A;
                if (inA != inB) {
                    float tt = dA / (dA - dB);
                    poly[pn++] = {A.x + (B.x - A.x) * tt, A.y + (B.y - A.y) * tt,
                                  A.z + (B.z - A.z) * tt, A.w + (B.w - A.w) * tt,
                                  A.u + (B.u - A.u) * tt, A.v + (B.v - A.v) * tt,
                                  A.lr + (B.lr - A.lr) * tt, A.lg + (B.lg - A.lg) * tt,
                                  A.lb + (B.lb - A.lb) * tt,
                                  A.nx + (B.nx - A.nx) * tt, A.ny + (B.ny - A.ny) * tt,
                                  A.nz + (B.nz - A.nz) * tt, A.wx + (B.wx - A.wx) * tt,
                                  A.wy + (B.wy - A.wy) * tt, A.wz + (B.wz - A.wz) * tt,
                                  A.fo + (B.fo - A.fo) * tt, A.sp + (B.sp - A.sp) * tt,
                                  A.vr + (B.vr - A.vr) * tt, A.vg + (B.vg - A.vg) * tt, A.vb + (B.vb - A.vb) * tt};
                }
            }
            if (pn < 3) continue;   // entirely behind the camera

            // Per-face color when the mesh carries one (e.g. character skin /
            // outfit / hair); otherwise the renderer's albedo, including LOD tint.
            const Color& base = faceCols ? drawMesh.triColors[i / 3] : shaded;
            // Material for the flat path: diffuse (1 when unlit) + Blinn-Phong specular + self-illuminating emissive. An
            // unlit mesh has no lighting and no specular, so this is the same sum as ever; the other paths never read it.
            std::uint32_t abgr = 0;
            if (flatPath) {
                const Vec3 lit{1.0f, 1.0f, 1.0f};
                const float spec = 0.0f;
                const float metalK = mr->metallicRoughnessMap.empty() ? 1.0f : (1.0f - 0.9f * std::min(1.0f, std::max(0.0f, faceMetal)));
                const float vAvgR = (in[0].vr + in[1].vr + in[2].vr) * (1.0f / 3.0f), vAvgG = (in[0].vg + in[1].vg + in[2].vg) * (1.0f / 3.0f), vAvgB = (in[0].vb + in[1].vb + in[2].vb) * (1.0f / 3.0f);   // (the flat path only: the other paths blend the vertex colours across the face)
                float cr = base.r * vAvgR * lit.x * metalK + spec + faceEr;
                float cg = base.g * vAvgG * lit.y * metalK + spec + faceEg;
                float cb = base.b * vAvgB * lit.z * metalK + spec + faceEb;
                // Distance fog: blend the color toward the fog color by how far the
                // triangle is, between fogStart and fogEnd (per-tri factor).
                float fogF = 0.0f;
                if (fogOn) {
                    fogF = FogFactor(rs, (centroid - eye).Magnitude(), eye.y, centroid.y);
                    cr = cr * (1.0f - fogF) + fogR * fogF;
                    cg = cg * (1.0f - fogF) + fogG * fogF;
                    cb = cb * (1.0f - fogF) + fogB * fogF;
                }
                abgr = Raster::PackRGB(cr, cg, cb);
            }

            // Fan-triangulate the (possibly clipped) polygon and rasterize.
            auto project = [&](const CV& c, float& sx, float& sy, float& sd, float& iw,
                               float& uo, float& vo) {
                iw = 1.0f / c.w;
                sx = (c.x * iw * 0.5f + 0.5f) * W;
                sy = (1.0f - (c.y * iw * 0.5f + 0.5f)) * H;
                sd = iw;   // W-buffer depth (1/w): larger = nearer, uniform precision
                uo = c.u * iw; vo = c.v * iw;
            };
            for (int j = 1; j + 1 < pn; ++j) {
                const CV* tri[3] = {&poly[0], &poly[j], &poly[j + 1]};
                float sx[3], sy[3], sd[3], iw[3], uu[3], vv[3];
                for (int k = 0; k < 3; ++k)
                    project(*tri[k], sx[k], sy[k], sd[k], iw[k], uu[k], vv[k]);
                float fo[3] = {tri[0]->fo, tri[1]->fo, tri[2]->fo};   // per-vertex fog
                float sp3[3] = {tri[0]->sp, tri[1]->sp, tri[2]->sp};  // per-vertex specular
                if (perPixel) {
                    float nx[3] = {tri[0]->nx, tri[1]->nx, tri[2]->nx};
                    float ny[3] = {tri[0]->ny, tri[1]->ny, tri[2]->ny};
                    float nz[3] = {tri[0]->nz, tri[1]->nz, tri[2]->nz};
                    float wx[3] = {tri[0]->wx, tri[1]->wx, tri[2]->wx};
                    float wy[3] = {tri[0]->wy, tri[1]->wy, tri[2]->wy};
                    float wz[3] = {tri[0]->wz, tri[1]->wz, tri[2]->wz};
                    const float vc9[9] = {tri[0]->vr, tri[0]->vg, tri[0]->vb, tri[1]->vr, tri[1]->vg, tri[1]->vb, tri[2]->vr, tri[2]->vg, tri[2]->vb};
                    r.TrianglePhong(sx, sy, sd, iw, uu, vv, nx, ny, nz, wx, wy, wz,
                                    triTex, base, shaded, eye, faceShine, faceSpec,
                                    faceEr, faceEg, faceEb,
                                    fo, fogR, fogG, fogB,
                                    normalMips, triTangent, mr->normalStrength,
                                    mr->reflectivity, specMips, faceMetal,
                                    (mr->shader == MeshRenderer::Shader::Toon ? mr->toonBands : 0),
                                    mr->rimStrength, mr->rimPower,
                                    mr->rimColor.r, mr->rimColor.g, mr->rimColor.b,
                                    mr->triplanar, mr->tiling.x, mr->tiling.y,
                                    bandY0, bandY1, shaderMode, gradTopC, gradBotC,
                                    aoMips, mr->aoStrength, mr->receiveShadows, lightSubset,
                                    drawMesh.HasVertexColors() ? vc9 : nullptr,
                                    customOk ? &cbind.cs : nullptr);
                } else if (triTex) {
                    float lr[3] = {tri[0]->lr * tri[0]->vr, tri[1]->lr * tri[1]->vr, tri[2]->lr * tri[2]->vr};
                    float lg[3] = {tri[0]->lg * tri[0]->vg, tri[1]->lg * tri[1]->vg, tri[2]->lg * tri[2]->vg};
                    float lb[3] = {tri[0]->lb * tri[0]->vb, tri[1]->lb * tri[1]->vb, tri[2]->lb * tri[2]->vb};
                    r.TriangleTex(sx, sy, sd, iw, uu, vv, *triTex, shaded, lr, lg, lb, sp3,
                                  faceEr, faceEg, faceEb,
                                  fo, fogR, fogG, fogB, bandY0, bandY1);
                } else if (smooth) {
                    float lr[3] = {tri[0]->lr * tri[0]->vr, tri[1]->lr * tri[1]->vr, tri[2]->lr * tri[2]->vr};
                    float lg[3] = {tri[0]->lg * tri[0]->vg, tri[1]->lg * tri[1]->vg, tri[2]->lg * tri[2]->vg};
                    float lb[3] = {tri[0]->lb * tri[0]->vb, tri[1]->lb * tri[1]->vb, tri[2]->lb * tri[2]->vb};
                    r.TriangleSmooth(sx, sy, sd, lr, lg, lb, base, sp3,
                                     faceEr, faceEg, faceEb,
                                     fo, fogR, fogG, fogB, bandY0, bandY1);
                } else
                    r.Triangle(sx[0], sy[0], sd[0], sx[1], sy[1], sd[1],
                               sx[2], sy[2], sd[2], abgr, bandY0, bandY1);
            }
        }
    }
    };  // renderBand
    // Split the frame into horizontal bands across hardware threads.
    { OKAY_PROFILE("Render.Raster"); textures::ReadOnlyScope texturesReadOnly;   // the bands only read the textures loaded above
      ParallelRows(0, r.height, [&](int ya, int yb) { renderBand(ya, yb - 1); }); }
    OKAY_PROFILE("Render.Post");
    if ((SSAOEnabled() || rs.post.ssao) && !r.gvalid.empty()) {       // contact AO post-pass
        const float radSaved = SSAORadius(), strSaved = SSAOStrength(), powSaved = SSAOPower();
        if (rs.post.ssao) {   // the scene's own switch carries its radius, intensity and power (Lighting window > Post-processing)
            SSAORadius() = rs.post.ssaoRadius;
            SSAOStrength() = std::fmin(1.0f, std::fmax(0.0f, rs.post.ssaoIntensity * 0.6f));
            SSAOPower() = rs.post.ssaoPower;
        }
        ApplySSAO(r, vp, eye);
        SSAORadius() = radSaved; SSAOStrength() = strSaved; SSAOPower() = powSaved;
    }
    if (rs.post.bloom && !BloomEnabled()) {                          // the scene's own Bloom switch
        BloomEnabled() = true;
        const float t0 = BloomThreshold(), i0 = BloomIntensity();
        BloomThreshold() = std::fmin(rs.post.bloomThreshold, 0.95f);   // 8-bit frame: no values above 1
        BloomIntensity() = rs.post.bloomIntensity;
        ApplyBloom(r);
        BloomEnabled() = false; BloomThreshold() = t0; BloomIntensity() = i0;
    } else ApplyBloom(r);                                            // bright-pass glow
    const ScenePostPlan postPlan = PlanScenePost(rs);
    ApplyScenePost(r, postPlan);                                     // scene exposure, tonemap, grade, vignette
    ApplyColorGrade(r);                                              // process-wide brightness/contrast/sat/vignette/gamma
    if (rs.post.dof) ApplySceneDof(r, rs.post.dofFocus, rs.post.dofRange, vp);   // the scene's Depth of field (perspective cameras)
    if (rs.post.fxaa && !FXAAEnabled()) { FXAAEnabled() = true; ApplyFXAA(r); FXAAEnabled() = false; }
    else ApplyFXAA(r);                                               // edge anti-aliasing
    PostVignetteHandled() = postPlan.vignette > 0.0f;                // the host skips its own overlay
    LastCullCounters().drawn = swDrawn;
}

const std::uint32_t* RenderMeshesSS(Raster& work, std::vector<std::uint32_t>& out,
                                           const Scene& scene, const Mat4& vp, const Vec3& eye,
                                           int w, int h, int ss, const GameObject* ignore) {
    if (w < 1) w = 1; if (h < 1) h = 1; if (ss < 1) ss = 1;
    while (ss > 1 && ((long)w * ss > 4096 || (long)h * ss > 4096)) --ss;
    const int iw = w * ss, ih = h * ss;
    { OKAY_PROFILE("Render.Clear"); work.Resize(iw, ih); work.Clear(0u); }
    RenderMeshes(work, scene, vp, eye, ignore);
    // Publish the depth (1/w) at the OUTPUT resolution for particle occlusion: take the
    // nearest (max 1/w) sample over each supersample block so thin geometry still occludes.
    OKAY_PROFILE("Render.Resolve");
    {
        OcclusionDepth& od = SceneOcclusionDepth();
        od.w = w; od.h = h; od.valid = true;
        od.d.assign((std::size_t)w * h, 0.0f);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float best = 0.0f;
                for (int sy = 0; sy < ss; ++sy) {
                    const float* row = work.depth.data() + (std::size_t)(y * ss + sy) * iw + (std::size_t)x * ss;
                    for (int sx = 0; sx < ss; ++sx) if (row[sx] > best) best = row[sx];
                }
                od.d[(std::size_t)y * w + x] = best;
            }
    }
    if (ss == 1) return work.color.data();
    out.assign((std::size_t)w * h, 0u);
    const std::uint32_t* src = work.color.data();
    const int n = ss * ss;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < ss; ++sy) {
                const std::uint32_t* row = src + (std::size_t)(y * ss + sy) * iw + (std::size_t)x * ss;
                for (int sx = 0; sx < ss; ++sx) {
                    std::uint32_t p = row[sx];
                    r += p & 0xFFu; g += (p >> 8) & 0xFFu; b += (p >> 16) & 0xFFu; a += (p >> 24) & 0xFFu;
                }
            }
            out[(std::size_t)y * w + x] =
                (std::uint32_t)(r / n) | ((std::uint32_t)(g / n) << 8) |
                ((std::uint32_t)(b / n) << 16) | ((std::uint32_t)(a / n) << 24);
        }
    return out.data();
}

void FillOcclusionDepth(Raster& work, std::vector<std::uint32_t>& out,
                               const Scene& scene, const Mat4& vp, const Vec3& eye,
                               int w, int h, const GameObject* ignore) {
    const CullCounters saved = LastCullCounters();
    const bool vigSaved = PostVignetteHandled();   // this pass is never shown: it must not change who paints the vignette
    // Only the depth is wanted: shadow maps (three 2048 x 2048 cascades) were about 90 percent of this pass and change no depth.
    const bool shadowsSaved = ShadowsEnabled(), aaSaved = FXAAEnabled(), ssaoSaved = SSAOEnabled(), bloomSaved = BloomEnabled(),
               toneSaved = ToneMapEnabled(), gradeSaved = ColorGradeEnabled();
    ShadowsEnabled() = false; FXAAEnabled() = false; SSAOEnabled() = false; BloomEnabled() = false; ToneMapEnabled() = false; ColorGradeEnabled() = false;
    RenderMeshesSS(work, out, scene, vp, eye, w, h, 1, ignore);
    ShadowsEnabled() = shadowsSaved; FXAAEnabled() = aaSaved; SSAOEnabled() = ssaoSaved; BloomEnabled() = bloomSaved;
    ToneMapEnabled() = toneSaved; ColorGradeEnabled() = gradeSaved;
    LastCullCounters() = saved;
    PostVignetteHandled() = vigSaved;
}

} // namespace okay
