// Direct3D 12 scene renderer (Windows only). Guarded so the Linux build / headless
// tests never reference Direct3D. Mirrors D3D11Renderer: an isolated device renders
// the scene's meshes into an OFFSCREEN target and reads it back as RGBA8. It REUSES
// the exact D3D11 HLSL + constant-buffer layout + per-mesh vertex/light packing, so
// shading matches; only the device/command/descriptor plumbing is D3D12-specific.
//
// This is an OPT-IN alternative to the default D3D11 backend. Feature parity with
// D3D11: up-to-4x MSAA (resolved before read-back), the planar ground-shadow pass,
// and real directional cast-shadow maps (depth pre-pass + PCF). Compile/link verified
// via MinGW; intended to be runtime-tested on Windows.
#if defined(_WIN32)

#include "okay/Render/D3D12Renderer.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // GetCachedTexture, EnvSky, SceneLights
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Log.hpp"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <unordered_map>

namespace okay {
namespace {

// The SAME shader the D3D11 backend uses (so shading is identical). t0..t3 textures,
// one sampler, the big shared constant buffer.
const char* kHLSL12 =
    "cbuffer CB : register(b0) {\n"
    "  float4x4 uMVP; float4x4 uModel;\n"
    "  float3 uColor;     float uSpecular;\n"
    "  float3 uEmissive;  float uShininess;\n"
    "  float3 uLightDir;  float uUnlit;\n"
    "  float3 uLightColor; float uUseTex;\n"
    "  float3 uAmbient;   float uHasNormal;\n"
    "  float3 uEye;       float uNormalStrength;\n"
    "  float2 uTiling;    float uShadowAlpha; float uAlpha;\n"
    "  float3 uRimColor;  float uShaderMode;\n"
    "  float3 uGradTop;   float uToonBands;\n"
    "  float3 uGradBot;   float uRimStr;\n"
    "  float4 uPbr;\n"
    "  float4 uPbr2;\n"
    "  float4 uSky[3];\n"
    "  float4 uLightCount;\n"
    "  float4 uLights[64];\n"
    "  float4 uFog;\n"
    "  float4 uFogCol;\n"
    "  float4x4 uLightVP;\n"                  // directional shadow-map view-projection
    "  float4 uShadow;\n"                     // x=on y=texelWorld z=mapSize w=unused
    "};\n"
    "Texture2D uTex : register(t0);\n"
    "Texture2D uNormalTex : register(t1);\n"
    "Texture2D uSpecTex : register(t2);\n"
    "Texture2D uAoTex : register(t3);\n"
    "Texture2D uShadowTex : register(t4);\n"        // directional shadow map (depth)
    "SamplerState uSamp : register(s0);\n"
    "SamplerState uShadowSamp : register(s1);\n"    // clamp sampler for the shadow map
    "float shadowFactor(float3 wpos, float3 n){\n"  // 0=shadowed .. 1=lit (4-tap PCF)
    "  if (uShadow.x < 0.5) return 1.0;\n"
    "  float3 wp = wpos + n * uShadow.y * 2.0;\n"
    "  float4 sc = mul(uLightVP, float4(wp,1.0));\n"
    "  if (sc.w <= 0.0) return 1.0;\n"
    "  float3 q = sc.xyz / sc.w;\n"
    "  float2 uv = float2(q.x*0.5+0.5, -q.y*0.5+0.5);\n"
    "  float pz = q.z*0.5+0.5;\n"
    "  if (uv.x<0.0||uv.x>1.0||uv.y<0.0||uv.y>1.0||pz>1.0) return 1.0;\n"
    "  float bias=0.0015; float t=1.0/max(uShadow.z,1.0); float s=0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2(-0.5,-0.5)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2( 0.5,-0.5)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2(-0.5, 0.5)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2( 0.5, 0.5)*t).r)?1.0:0.0;\n"
    "  return s*0.25;\n"
    "}\n"
    "float4 VSDepth(float3 pos:POSITION):SV_POSITION {\n"          // depth-only shadow pass
    "  float4 cp = mul(uMVP, float4(pos,1.0)); cp.z = (cp.z + cp.w) * 0.5; return cp;\n"
    "}\n"
    "struct VSIn { float3 pos:POSITION; float3 nrm:NORMAL; float2 uv:TEXCOORD; float3 col:COLOR; float3 tan:TANGENT; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float3 nrm:NORMAL; float3 world:TEXCOORD1; float2 uv:TEXCOORD0; float3 col:COLOR; float3 tan:TEXCOORD2; };\n"
    "PSIn VSMain(VSIn i){\n"
    "  PSIn o;\n"
    "  float4 cp = mul(uMVP, float4(i.pos,1.0));\n"
    "  cp.z = (cp.z + cp.w) * 0.5;\n"
    "  o.pos = cp;\n"
    "  o.nrm = mul((float3x3)uModel, i.nrm);\n"
    "  o.tan = mul((float3x3)uModel, i.tan);\n"
    "  o.world = mul(uModel, float4(i.pos,1.0)).xyz;\n"
    "  o.uv = i.uv * uTiling + uPbr2.zw;\n"
    "  o.col = i.col;\n"
    "  return o;\n"
    "}\n"
    "float4 PSMain(PSIn i):SV_TARGET {\n"
    "  if (uShadowAlpha >= 0.0) return float4(0,0,0,uShadowAlpha);\n"
    "  float3 N = normalize(i.nrm);\n"
    "  if (uHasNormal > 0.5) {\n"
    "    float3 tn = uNormalTex.Sample(uSamp, i.uv).rgb * 2.0 - 1.0;\n"
    "    tn.xy *= uNormalStrength;\n"
    "    float3 Tn = i.tan - N * dot(N, i.tan);\n"
    "    if (length(Tn) > 1e-5) {\n"
    "      Tn = normalize(Tn); float3 Bn = cross(N, Tn);\n"
    "      float3 pn = Tn * tn.x + Bn * tn.y + N * tn.z;\n"
    "      if (length(pn) > 1e-5) N = normalize(pn);\n"
    "    }\n"
    "  }\n"
    "  float3 base = uColor * i.col;\n"
    "  if (uUseTex > 0.5) base *= uTex.Sample(uSamp, i.uv).rgb;\n"
    "  float3 Vv = normalize(uEye - i.world);\n"
    "  float fz = 1.0 - max(dot(N, Vv), 0.0);\n"
    "  if (uShaderMode > 2.5 && uShaderMode < 3.5)\n"
    "    base = lerp(uGradBot, uGradTop, saturate(N.y * 0.5 + 0.5));\n"
    "  bool fres = uShaderMode > 3.5 && uShaderMode < 4.5;\n"
    "  if (fres) base *= 0.10;\n"
    "  if (uShaderMode > 4.5 && uShaderMode < 5.5) {\n"
    "    float ph = fz * 6.2831853;\n"
    "    base *= float3(0.5+0.5*cos(ph), 0.5+0.5*cos(ph+2.0944), 0.5+0.5*cos(ph+4.1888));\n"
    "  }\n"
    "  bool holo = uShaderMode > 5.5 && uShaderMode < 6.5;\n"
    "  if (holo) base *= 0.18 * (0.55 + 0.45 * sin(i.world.y * 40.0));\n"
    "  if (uShaderMode > 6.5) base = floor(base * 5.0) / 5.0;\n"
    "  if (uUnlit > 0.5) return float4(base + uEmissive, uAlpha);\n"
    "  bool toon = uShaderMode > 1.5 && uShaderMode < 2.5 && uToonBands > 0.5;\n"
    "  float3 amb = uAmbient * lerp(0.55, 1.15, saturate(N.y * 0.5 + 0.5));\n"
    "  float3 lit = amb; float spec = 0.0;\n"
    "  float sh = shadowFactor(i.world, N);\n"                   // sun occlusion (real cast shadows)
    "  int lc = (int)uLightCount.x;\n"
    "  if (lc < 1) {\n"
    "    float ndl = max(dot(N, uLightDir), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "    lit += uLightColor * ndl * sh;\n"
    "    if (uSpecular > 0.0) { float3 H = normalize(uLightDir + Vv);\n"
    "      spec = pow(max(dot(N,H),0.0), max(uShininess,1.0)) * uSpecular * sh; }\n"
    "  } else {\n"
    "    for (int li = 0; li < 16; li++) { if (li >= lc) break;\n"
    "      float4 d0 = uLights[li*4+0]; float4 d1 = uLights[li*4+1];\n"
    "      float4 d2 = uLights[li*4+2]; float4 d3 = uLights[li*4+3];\n"
    "      float3 ld; float at = 1.0;\n"
    "      if (d0.w < 0.5) { ld = normalize(-d0.xyz); }\n"
    "      else { float3 toL = d1.xyz - i.world; float dist = length(toL); ld = toL / max(dist, 1e-4);\n"
    "        at = max(1.0 - dist / max(d1.w, 1e-4), 0.0); at *= at;\n"
    "        if (d0.w > 1.5) { float cs = dot(normalize(d0.xyz), -ld);\n"
    "          float dn = d3.x - d2.w; float sp = dn > 1e-4 ? saturate((cs - d2.w) / dn) : (cs >= d2.w ? 1.0 : 0.0);\n"
    "          at *= sp * sp; } }\n"
    "      float lsh = (d0.w < 0.5) ? sh : 1.0;\n"               // cast shadows apply to the sun only
    "      float ndl = max(dot(N, ld), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "      lit += d2.xyz * ndl * at * lsh;\n"
    "      if (uSpecular > 0.0) { float3 H = normalize(ld + Vv);\n"
    "        spec += pow(max(dot(N,H),0.0), max(uShininess,1.0)) * uSpecular * at * lsh; }\n"
    "    }\n"
    "  }\n"
    "  if (uShaderMode > 7.5) lit += fz * fz * lit * 0.6;\n"
    "  float gloss = 1.0;\n"
    "  if (uPbr.z > 0.5) gloss = dot(uSpecTex.Sample(uSamp, i.uv).rgb, float3(0.2126, 0.7152, 0.0722));\n"
    "  if (uPbr.w > 0.5) {\n"
    "    float ao = dot(uAoTex.Sample(uSamp, i.uv).rgb, float3(0.2126, 0.7152, 0.0722));\n"
    "    lit *= (1.0 - uPbr2.x * (1.0 - ao));\n"
    "  }\n"
    "  float metal = saturate(uPbr.x);\n"
    "  float diffK = 1.0 - 0.9 * metal;\n"
    "  float3 f0 = lerp(float3(1,1,1), base, metal);\n"
    "  float3 diff = base * lit * diffK;\n"
    "  float3 rim = float3(0,0,0);\n"
    "  float rs = uRimStr; if ((fres || holo) && rs < 0.8) rs = 1.6;\n"
    "  if (rs > 0.0) rim = uRimColor * (fz*fz*fz * rs);\n"
    "  float3 col = diff + (spec * gloss) * f0 + rim + uEmissive;\n"
    "  float reflAmt = max(uPbr.y, metal);\n"
    "  float reflK = reflAmt * gloss;\n"
    "  if (reflK > 0.0 && uPbr2.y > 0.5) {\n"
    "    float ndv = max(dot(N, Vv), 0.0);\n"
    "    float3 R = reflect(-Vv, N); float ry = clamp(R.y, -1.0, 1.0);\n"
    "    float3 env = lerp(uSky[1].xyz, ry >= 0.0 ? uSky[0].xyz : uSky[2].xyz, abs(ry));\n"
    "    float fr2 = 1.0 - ndv; fr2 = fr2*fr2*fr2*fr2*fr2;\n"
    "    float kk = reflK + (1.0 - reflK) * fr2;\n"
    "    col = col * (1.0 - kk) + env * f0 * kk;\n"
    "  }\n"
    "  if (uFog.x > 0.5) {\n"
    "    float fd = length(uEye - i.world);\n"
    "    float ff = saturate((fd - uFog.y) / max(uFog.z - uFog.y, 1e-3));\n"
    "    col = lerp(col, uFogCol.xyz, ff);\n"
    "  }\n"
    "  return float4(col, uAlpha);\n"
    "}\n";

// Exact mirror of the D3D11 constant buffer.
struct CB {
    float mvp[16];
    float model[16];
    float color[3];     float specular;
    float emissive[3];  float shininess;
    float lightDir[3];  float unlit;
    float lightColor[3]; float useTex;
    float ambient[3];   float hasNormal;
    float eye[3];       float normalStrength;
    float tiling[2];    float shadowAlpha; float alpha;
    float rimColor[3];  float shaderMode;
    float gradTop[3];   float toonBands;
    float gradBot[3];   float rimStr;
    float pbr[4];
    float pbr2[4];
    float sky[12];
    float lightCount[4];
    float lights[256];
    float fog[4];
    float fogCol[4];
    float lightVP[16];         // directional shadow-map view-projection
    float shadowP[4];          // x=on y=texelWorld z=mapSize w=unused
};

inline UINT Align(UINT v, UINT a) { return (v + a - 1) & ~(a - 1); }

template <typename T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

} // namespace

struct D3D12Renderer::Impl {
    ID3D12Device*              dev   = nullptr;
    ID3D12CommandQueue*        queue = nullptr;
    ID3D12CommandAllocator*    alloc = nullptr;
    ID3D12GraphicsCommandList* list  = nullptr;
    ID3D12Fence*               fence = nullptr;
    HANDLE                     fenceEvent = nullptr;
    UINT64                     fenceValue = 0;
    ID3D12RootSignature*       rootSig = nullptr;
    ID3D12PipelineState*       psoOpaque = nullptr;
    ID3D12PipelineState*       psoBlend  = nullptr;
    ID3D12PipelineState*       psoDepth  = nullptr;   // depth-only shadow pass (single-sample)
    UINT                       msaa = 1;              // MSAA sample count for the main targets

    // Directional shadow map (single-sample depth texture, sampled in the main pass).
    ID3D12Resource*            shadowTex = nullptr;
    int                        shadowSize = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE shadowSrvCpu{};       // staging-heap SRV for the shadow map
    bool EnsureShadow(int size);

    // Descriptor heaps.
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    ID3D12DescriptorHeap* dsvHeap = nullptr;
    ID3D12DescriptorHeap* srvHeap = nullptr;      // shader-visible ring (4 SRVs / draw)
    ID3D12DescriptorHeap* srvStage = nullptr;     // non-shader-visible: cached texture SRVs
    UINT srvInc = 0, rtvInc = 0, dsvInc = 0;
    UINT srvRingCap = 0, srvRingNext = 0;         // ring cursor (reset per frame)
    UINT stageCap = 0, stageNext = 0;             // staging cursor (persistent)

    // Size-dependent targets.
    int w = 0, h = 0;
    ID3D12Resource* colorTex = nullptr;           // RGBA8 render target (multisampled when msaa>1)
    ID3D12Resource* depthTex = nullptr;           // depth (multisampled when msaa>1)
    ID3D12Resource* resolveTex = nullptr;         // single-sample MSAA resolve target (msaa>1 only)
    ID3D12Resource* readback = nullptr;           // CPU-readable copy buffer
    UINT readbackPitch = 0;

    // Per-frame dynamic upload buffers (persistently mapped; single-buffered — we wait
    // the GPU each frame so reuse is safe).
    ID3D12Resource* vbUpload = nullptr; UINT vbCap = 0; UINT8* vbPtr = nullptr; UINT vbNext = 0;
    ID3D12Resource* cbUpload = nullptr; UINT cbCap = 0; UINT8* cbPtr = nullptr; UINT cbNext = 0;
    UINT cbStride = 0;                             // aligned sizeof(CB)

    struct Tex { ID3D12Resource* res; D3D12_CPU_DESCRIPTOR_HANDLE srv; };
    std::unordered_map<std::string, Tex> texCache;
    D3D12_CPU_DESCRIPTOR_HANDLE whiteSrv{};       // 1x1 white default for unused slots

    std::vector<float> verts;
    std::vector<std::uint32_t> pixels;

    bool EnsureTargets(int W, int H);
    void DestroyTargets();
    D3D12_CPU_DESCRIPTOR_HANDLE TextureFor(const std::string& name);   // -> staging SRV handle
    void Barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);
    void WaitGPU();
};

void D3D12Renderer::Impl::Barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &b);
}

void D3D12Renderer::Impl::WaitGPU() {
    const UINT64 v = ++fenceValue;
    queue->Signal(fence, v);
    if (fence->GetCompletedValue() < v) {
        fence->SetEventOnCompletion(v, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

// Create / upload a texture once and return its (staging-heap) SRV handle. Failures
// fall back to the 1x1 white default so a slot is always bound.
D3D12_CPU_DESCRIPTOR_HANDLE D3D12Renderer::Impl::TextureFor(const std::string& name) {
    if (name.empty()) return whiteSrv;
    auto it = texCache.find(name);
    if (it != texCache.end()) return it->second.srv;

    D3D12_CPU_DESCRIPTOR_HANDLE handle = whiteSrv;
    ID3D12Resource* res = nullptr;
    Image* img = GetCachedTexture(name);
    if (img && img->Width() > 0 && stageNext < stageCap) {
        const UINT iw = (UINT)img->Width(), ih = (UINT)img->Height();
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = iw; rd.Height = ih; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_ID3D12Resource, (void**)&res)) && res) {
            // Upload buffer sized to the (256-aligned-pitch) footprint.
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT rows = 0; UINT64 rowBytes = 0, total = 0;
            dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowBytes, &total);
            D3D12_HEAP_PROPERTIES up{}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC bd{};
            bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1;
            bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.Format = DXGI_FORMAT_UNKNOWN;
            bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ID3D12Resource* upRes = nullptr;
            if (SUCCEEDED(dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_ID3D12Resource, (void**)&upRes)) && upRes) {
                UINT8* dst = nullptr; D3D12_RANGE nr{0, 0};
                if (SUCCEEDED(upRes->Map(0, &nr, (void**)&dst)) && dst) {
                    const UINT8* src = img->Data();
                    for (UINT y = 0; y < ih; ++y)
                        std::memcpy(dst + fp.Offset + (size_t)y * fp.Footprint.RowPitch,
                                    src + (size_t)y * iw * 4, (size_t)iw * 4);
                    upRes->Unmap(0, nullptr);
                }
                // Record the copy on a one-shot command list.
                alloc->Reset(); list->Reset(alloc, nullptr);
                D3D12_TEXTURE_COPY_LOCATION d{}; d.pResource = res;
                d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = 0;
                D3D12_TEXTURE_COPY_LOCATION sLoc{}; sLoc.pResource = upRes;
                sLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; sLoc.PlacedFootprint = fp;
                list->CopyTextureRegion(&d, 0, 0, 0, &sLoc, nullptr);
                Barrier(res, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                list->Close();
                ID3D12CommandList* lists[] = {list};
                queue->ExecuteCommandLists(1, lists);
                WaitGPU();
                upRes->Release();
            }
            // SRV in the staging heap.
            D3D12_CPU_DESCRIPTOR_HANDLE cpu = srvStage->GetCPUDescriptorHandleForHeapStart();
            cpu.ptr += (size_t)stageNext * srvInc; stageNext++;
            D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Texture2D.MipLevels = 1;
            dev->CreateShaderResourceView(res, &sd, cpu);
            handle = cpu;
        }
    }
    texCache[name] = Tex{res, handle};
    return handle;
}

// ===========================================================================
bool D3D12Renderer::Init() {
    if (m_impl) return true;
    m_impl = new Impl();
    Impl* p = m_impl;

    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_ID3D12Device, (void**)&p->dev))) {
        Log::Error("[d3d12] device creation failed"); Destroy(); return false;
    }
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(p->dev->CreateCommandQueue(&qd, IID_ID3D12CommandQueue, (void**)&p->queue)) ||
        FAILED(p->dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_ID3D12CommandAllocator, (void**)&p->alloc)) ||
        FAILED(p->dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, p->alloc, nullptr, IID_ID3D12GraphicsCommandList, (void**)&p->list)) ||
        FAILED(p->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, (void**)&p->fence))) {
        Log::Error("[d3d12] command objects failed"); Destroy(); return false;
    }
    p->list->Close();
    p->fenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);

    // Root signature: b0 = CBV (root descriptor), table of t0..t3 (SRV), static sampler s0.
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 5; srvRange.BaseShaderRegister = 0;   // t0..t3 material + t4 shadow map
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC samps[2]{};
    samps[0].Filter = D3D12_FILTER_ANISOTROPIC; samps[0].MaxAnisotropy = 8;
    samps[0].AddressU = samps[0].AddressV = samps[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samps[0].MaxLOD = D3D12_FLOAT32_MAX; samps[0].ShaderRegister = 0;
    samps[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samps[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;   // shadow map: clamp at borders (outside = lit)
    samps[1].AddressU = samps[1].AddressV = samps[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samps[1].MaxLOD = D3D12_FLOAT32_MAX; samps[1].ShaderRegister = 1;
    samps[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 2; rs.pParameters = params;
    rs.NumStaticSamplers = 2; rs.pStaticSamplers = samps;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* rsb = nullptr; ID3DBlob* rerr = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &rsb, &rerr))) {
        if (rerr) rerr->Release();
        Log::Error("[d3d12] root signature serialize failed"); Destroy(); return false;
    }
    p->dev->CreateRootSignature(0, rsb->GetBufferPointer(), rsb->GetBufferSize(), IID_ID3D12RootSignature, (void**)&p->rootSig);
    rsb->Release();
    if (!p->rootSig) { Log::Error("[d3d12] root signature create failed"); Destroy(); return false; }

    // Compile the shared HLSL (shader model 5.1 for D3D12).
    ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* err = nullptr;
    const SIZE_T n = (SIZE_T)std::strlen(kHLSL12);
    if (FAILED(D3DCompile(kHLSL12, n, "okay3d12", nullptr, nullptr, "VSMain", "vs_5_1", 0, 0, &vsb, &err)) ||
        FAILED(D3DCompile(kHLSL12, n, "okay3d12", nullptr, nullptr, "PSMain", "ps_5_1", 0, 0, &psb, &err))) {
        if (err) err->Release();
        if (vsb) vsb->Release();
        Log::Error("[d3d12] shader compile failed"); Destroy(); return false;
    }

    D3D12_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    // Pick the highest MSAA level (up to 4x) the device supports for BOTH the colour
    // and depth formats; fall back to 1 (no MSAA) when unsupported.
    p->msaa = 1;
    for (UINT s = 4; s >= 2; s >>= 1) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qc{}; qc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; qc.SampleCount = s;
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qd{}; qd.Format = DXGI_FORMAT_D32_FLOAT; qd.SampleCount = s;
        if (SUCCEEDED(p->dev->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qc, sizeof(qc))) && qc.NumQualityLevels > 0 &&
            SUCCEEDED(p->dev->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qd, sizeof(qd))) && qd.NumQualityLevels > 0) {
            p->msaa = s; break;
        }
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = p->rootSig;
    pso.VS = {vsb->GetBufferPointer(), vsb->GetBufferSize()};
    pso.PS = {psb->GetBufferPointer(), psb->GetBufferSize()};
    pso.InputLayout = {il, 5};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1; pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = p->msaa; pso.SampleMask = 0xFFFFFFFF;
    // Rasterizer (no cull, like the other backends).
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    // Depth (LESS_EQUAL, write on).
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    // Blend off (opaque).
    for (int i = 0; i < 8; ++i) pso.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(p->dev->CreateGraphicsPipelineState(&pso, IID_ID3D12PipelineState, (void**)&p->psoOpaque))) {
        vsb->Release(); psb->Release(); Log::Error("[d3d12] opaque PSO failed"); Destroy(); return false;
    }
    // Transparent variant: alpha blend + no depth write.
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    p->dev->CreateGraphicsPipelineState(&pso, IID_ID3D12PipelineState, (void**)&p->psoBlend);
    vsb->Release(); psb->Release();

    // Depth-only PSO for the shadow-map pass: position-only input, no pixel shader,
    // no render target, single-sample D32 depth, depth write on.
    ID3DBlob* vdb = nullptr; ID3DBlob* derr = nullptr;
    if (SUCCEEDED(D3DCompile(kHLSL12, n, "okay3d12", nullptr, nullptr, "VSDepth", "vs_5_1", 0, 0, &vdb, &derr)) && vdb) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC dp{};
        dp.pRootSignature = p->rootSig;
        dp.VS = {vdb->GetBufferPointer(), vdb->GetBufferSize()};
        dp.InputLayout = {il, 1};                 // POSITION only
        dp.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        dp.NumRenderTargets = 0;
        dp.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        dp.SampleDesc.Count = 1; dp.SampleMask = 0xFFFFFFFF;
        dp.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        dp.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        dp.RasterizerState.DepthClipEnable = TRUE;
        dp.DepthStencilState.DepthEnable = TRUE;
        dp.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        dp.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        p->dev->CreateGraphicsPipelineState(&dp, IID_ID3D12PipelineState, (void**)&p->psoDepth);
        vdb->Release();
    }
    if (derr) derr->Release();

    // Descriptor heaps.
    p->srvInc = p->dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    p->rtvInc = p->dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_DESCRIPTOR_HEAP_DESC rh{}; rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rh.NumDescriptors = 1;
    p->dev->CreateDescriptorHeap(&rh, IID_ID3D12DescriptorHeap, (void**)&p->rtvHeap);
    D3D12_DESCRIPTOR_HEAP_DESC dh{}; dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; dh.NumDescriptors = 2;  // [0]=main depth [1]=shadow
    p->dev->CreateDescriptorHeap(&dh, IID_ID3D12DescriptorHeap, (void**)&p->dsvHeap);
    p->dsvInc = p->dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    p->srvRingCap = 4096;   // 4 SRVs/draw -> up to 1024 draws/frame
    D3D12_DESCRIPTOR_HEAP_DESC sh{}; sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.NumDescriptors = p->srvRingCap; sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    p->dev->CreateDescriptorHeap(&sh, IID_ID3D12DescriptorHeap, (void**)&p->srvHeap);
    p->stageCap = 1024;
    D3D12_DESCRIPTOR_HEAP_DESC st{}; st.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; st.NumDescriptors = p->stageCap;
    p->dev->CreateDescriptorHeap(&st, IID_ID3D12DescriptorHeap, (void**)&p->srvStage);
    if (!p->rtvHeap || !p->dsvHeap || !p->srvHeap || !p->srvStage) { Destroy(); return false; }

    // 1x1 white default texture (for unused t1..t3 / textureless materials).
    {
        D3D12_HEAP_PROPERTIES up{}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = 256; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        // We just need a valid SRV; reuse a default-heap 1x1 texture seeded white.
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = 1; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1;
        ID3D12Resource* wt = nullptr;
        if (SUCCEEDED(p->dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_ID3D12Resource, (void**)&wt)) && wt) {
            ID3D12Resource* upRes = nullptr;
            if (SUCCEEDED(p->dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_ID3D12Resource, (void**)&upRes)) && upRes) {
                UINT8* dst = nullptr; D3D12_RANGE nr{0, 0};
                if (SUCCEEDED(upRes->Map(0, &nr, (void**)&dst)) && dst) { dst[0] = dst[1] = dst[2] = dst[3] = 255; upRes->Unmap(0, nullptr); }
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; p->dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, nullptr, nullptr, nullptr);
                p->alloc->Reset(); p->list->Reset(p->alloc, nullptr);
                D3D12_TEXTURE_COPY_LOCATION d{}; d.pResource = wt; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = 0;
                D3D12_TEXTURE_COPY_LOCATION sLoc{}; sLoc.pResource = upRes; sLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; sLoc.PlacedFootprint = fp;
                p->list->CopyTextureRegion(&d, 0, 0, 0, &sLoc, nullptr);
                p->Barrier(wt, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                p->list->Close();
                ID3D12CommandList* lists[] = {p->list}; p->queue->ExecuteCommandLists(1, lists); p->WaitGPU();
                upRes->Release();
            }
            D3D12_CPU_DESCRIPTOR_HANDLE cpu = p->srvStage->GetCPUDescriptorHandleForHeapStart();
            cpu.ptr += (size_t)p->stageNext * p->srvInc; p->stageNext++;
            D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Texture2D.MipLevels = 1;
            p->dev->CreateShaderResourceView(wt, &sd, cpu);
            p->whiteSrv = cpu;
            p->texCache["__white__"] = Impl::Tex{wt, cpu};
        }
    }

    // Constant-buffer ring (256-aligned stride, room for many draws/frame).
    p->cbStride = Align((UINT)sizeof(CB), 256);
    p->cbCap = p->cbStride * 1024;
    {
        D3D12_HEAP_PROPERTIES up{}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = p->cbCap; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (SUCCEEDED(p->dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_ID3D12Resource, (void**)&p->cbUpload)) && p->cbUpload) {
            D3D12_RANGE nr{0, 0}; p->cbUpload->Map(0, &nr, (void**)&p->cbPtr);
        }
    }
    return p->dev && p->rootSig && p->psoOpaque && p->cbPtr;
}

bool D3D12Renderer::Available() const { return m_impl && m_impl->dev && m_impl->psoOpaque; }

void D3D12Renderer::Impl::DestroyTargets() {
    Release(colorTex); Release(depthTex); Release(resolveTex); Release(readback);
}

bool D3D12Renderer::Impl::EnsureTargets(int W, int H) {
    if (colorTex && W == w && H == h) return true;
    WaitGPU();
    DestroyTargets();
    w = W; h = H;
    // When MSAA is on, colour rests in RESOLVE_SOURCE (resolved into a 1-sample target
    // before read-back); without MSAA it rests in COPY_SOURCE (copied straight to readback).
    const D3D12_RESOURCE_STATES colorRest = (msaa > 1) ? D3D12_RESOURCE_STATE_RESOLVE_SOURCE
                                                       : D3D12_RESOURCE_STATE_COPY_SOURCE;
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    // Color RT (multisampled when msaa>1).
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT)W; rd.Height = (UINT)H; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = msaa;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{}; cv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            colorRest, &cv, IID_ID3D12Resource, (void**)&colorTex))) return false;
    {
        D3D12_RENDER_TARGET_VIEW_DESC rv{}; rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rv.ViewDimension = (msaa > 1) ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
        dev->CreateRenderTargetView(colorTex, &rv, rtvHeap->GetCPUDescriptorHandleForHeapStart());
    }
    // Depth (multisampled to match the colour target).
    D3D12_RESOURCE_DESC dd = rd; dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{}; dcv.Format = DXGI_FORMAT_D32_FLOAT; dcv.DepthStencil.Depth = 1.0f;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &dd,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &dcv, IID_ID3D12Resource, (void**)&depthTex))) return false;
    D3D12_DEPTH_STENCIL_VIEW_DESC dvd{}; dvd.Format = DXGI_FORMAT_D32_FLOAT;
    dvd.ViewDimension = (msaa > 1) ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
    dev->CreateDepthStencilView(depthTex, &dvd, dsvHeap->GetCPUDescriptorHandleForHeapStart());   // slot 0
    // MSAA resolve target (single-sample): rests in COPY_SOURCE, resolved into each frame.
    if (msaa > 1) {
        D3D12_RESOURCE_DESC rr = rd; rr.SampleDesc.Count = 1; rr.Flags = D3D12_RESOURCE_FLAG_NONE;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rr,
                D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_ID3D12Resource, (void**)&resolveTex))) return false;
    }
    // Readback buffer (256-aligned row pitch).
    readbackPitch = Align((UINT)W * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    D3D12_HEAP_PROPERTIES rbp{}; rbp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rbd{}; rbd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rbd.Width = (UINT64)readbackPitch * H;
    rbd.Height = 1; rbd.DepthOrArraySize = 1; rbd.MipLevels = 1; rbd.Format = DXGI_FORMAT_UNKNOWN;
    rbd.SampleDesc.Count = 1; rbd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&rbp, D3D12_HEAP_FLAG_NONE, &rbd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_ID3D12Resource, (void**)&readback))) return false;
    return true;
}

// (Re)create the directional shadow map: a single-sample R32-typeless depth texture
// used as a DSV (depth pass, dsvHeap slot 1) and an SRV (main pass PCF lookup, in the
// staging heap). Returns false on failure (renderer still works, no shadows).
bool D3D12Renderer::Impl::EnsureShadow(int size) {
    if (shadowTex && shadowSize == size) return true;
    if (shadowTex) { WaitGPU(); Release(shadowTex); }
    shadowSize = 0;
    if (!psoDepth) return false;
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT)size; rd.Height = (UINT)size; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R32_TYPELESS; rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{}; dcv.Format = DXGI_FORMAT_D32_FLOAT; dcv.DepthStencil.Depth = 1.0f;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &dcv, IID_ID3D12Resource, (void**)&shadowTex))) return false;
    // DSV in dsvHeap slot 1.
    D3D12_CPU_DESCRIPTOR_HANDLE dsv1 = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    dsv1.ptr += (size_t)dsvInc;
    D3D12_DEPTH_STENCIL_VIEW_DESC dvd{}; dvd.Format = DXGI_FORMAT_D32_FLOAT; dvd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dev->CreateDepthStencilView(shadowTex, &dvd, dsv1);
    // SRV in the staging heap (copied into the per-draw ring as t4).
    if (shadowSrvCpu.ptr == 0) {
        shadowSrvCpu = srvStage->GetCPUDescriptorHandleForHeapStart();
        shadowSrvCpu.ptr += (size_t)stageNext * srvInc; stageNext++;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = DXGI_FORMAT_R32_FLOAT;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(shadowTex, &sd, shadowSrvCpu);
    shadowSize = size;
    return true;
}

const std::uint32_t* D3D12Renderer::RenderToPixels(const Scene& scene, const Mat4& vp, const Vec3& eye,
                                                   int w, int h, int /*samples*/,
                                                   float clearR, float clearG, float clearB, float clearA,
                                                   const GameObject* ignore) {
    if (!Available() || w < 1 || h < 1) return nullptr;
    Impl* p = m_impl;
    if (!p->EnsureTargets(w, h)) return nullptr;

    p->srvRingNext = 0; p->cbNext = 0; p->vbNext = 0;
    p->alloc->Reset();
    p->list->Reset(p->alloc, p->psoOpaque);

    // ---- Shadow-map depth pre-pass (directional cast shadows) -----------------
    // Render scene depth from the sun into the shadow texture, which the main pass
    // samples (PCF) for real cast shadows. Opt-in via ShadowsEnabled().
    Mat4 lightVP; float shadowTexel = 0.0f; bool shadowOn = false;
    if (ShadowsEnabled() && p->psoDepth &&
        ComputeDirectionalShadowVP(scene, vp, eye, lightVP, shadowTexel)) {
        int ssize = ShadowMapResolution(); if (ssize < 256) ssize = 256; if (ssize > 4096) ssize = 4096;
        if (p->EnsureShadow(ssize)) {
            p->Barrier(p->shadowTex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            D3D12_CPU_DESCRIPTOR_HANDLE sdsv = p->dsvHeap->GetCPUDescriptorHandleForHeapStart();
            sdsv.ptr += (size_t)p->dsvInc;   // slot 1
            p->list->OMSetRenderTargets(0, nullptr, FALSE, &sdsv);
            p->list->ClearDepthStencilView(sdsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            D3D12_VIEWPORT sv{}; sv.Width = (FLOAT)ssize; sv.Height = (FLOAT)ssize; sv.MaxDepth = 1.0f;
            D3D12_RECT ssc{0, 0, ssize, ssize};
            p->list->RSSetViewports(1, &sv); p->list->RSSetScissorRects(1, &ssc);
            p->list->SetGraphicsRootSignature(p->rootSig);
            p->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            p->list->SetPipelineState(p->psoDepth);
            const D3D12_GPU_VIRTUAL_ADDRESS cbBaseD = p->cbUpload->GetGPUVirtualAddress();
            for (const auto& upo : scene.Objects()) {
                GameObject* go = upo.get();
                if (!go || !go->active || (ignore && go->IsSelfOrDescendantOf(ignore))) continue;
                auto* mr = go->GetComponent<MeshRenderer>();
                if (!mr || mr->wireframe || !mr->enabled) continue;
                if (mr->color.a < 0.999f) continue;     // transparent meshes don't cast
                const Mesh& mesh = mr->mesh;
                const auto& Vv = mesh.vertices; const auto& T = mesh.triangles;
                if (Vv.empty() || T.size() < 3) continue;
                const int nv = (int)Vv.size();
                p->verts.clear(); p->verts.reserve(T.size() * 3);
                for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
                    int a = T[i], b = T[i+1], cc = T[i+2];
                    if (a < 0 || b < 0 || cc < 0 || a >= nv || b >= nv || cc >= nv) continue;
                    const Vec3& pa = Vv[a]; const Vec3& pb = Vv[b]; const Vec3& pc = Vv[cc];
                    p->verts.push_back(pa.x); p->verts.push_back(pa.y); p->verts.push_back(pa.z);
                    p->verts.push_back(pb.x); p->verts.push_back(pb.y); p->verts.push_back(pb.z);
                    p->verts.push_back(pc.x); p->verts.push_back(pc.y); p->verts.push_back(pc.z);
                }
                const int dvc = (int)(p->verts.size() / 3);
                if (dvc == 0) continue;
                const UINT bytes = (UINT)(p->verts.size() * sizeof(float));
                if (p->vbNext + bytes > p->vbCap) {
                    p->WaitGPU();
                    UINT need = p->vbNext + bytes + (1u << 20);
                    Release(p->vbUpload); p->vbPtr = nullptr;
                    D3D12_HEAP_PROPERTIES up{}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
                    D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = need; bd.Height = 1;
                    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1;
                    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                    if (FAILED(p->dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd,
                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_ID3D12Resource, (void**)&p->vbUpload))) return nullptr;
                    D3D12_RANGE nr{0, 0}; p->vbUpload->Map(0, &nr, (void**)&p->vbPtr); p->vbCap = need;
                }
                UINT vbOff = p->vbNext;
                std::memcpy(p->vbPtr + vbOff, p->verts.data(), bytes);
                p->vbNext = Align(vbOff + bytes, 16);
                D3D12_VERTEX_BUFFER_VIEW vbv{};
                vbv.BufferLocation = p->vbUpload->GetGPUVirtualAddress() + vbOff;
                vbv.SizeInBytes = bytes; vbv.StrideInBytes = 3 * sizeof(float);
                p->list->IASetVertexBuffers(0, 1, &vbv);
                if (p->cbNext + p->cbStride > p->cbCap) break;
                CB dcb; std::memset(&dcb, 0, sizeof(dcb));
                Mat4 dmvp = lightVP * go->transform->LocalToWorldMatrix();
                std::memcpy(dcb.mvp, dmvp.m, sizeof(dcb.mvp));
                UINT cbOff = p->cbNext;
                std::memcpy(p->cbPtr + cbOff, &dcb, sizeof(dcb)); p->cbNext += p->cbStride;
                p->list->SetGraphicsRootConstantBufferView(0, cbBaseD + cbOff);
                p->list->DrawInstanced((UINT)dvc, 1, 0, 0);
            }
            p->Barrier(p->shadowTex, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            shadowOn = true;
        }
    }

    p->Barrier(p->colorTex, (p->msaa > 1) ? D3D12_RESOURCE_STATE_RESOLVE_SOURCE : D3D12_RESOURCE_STATE_COPY_SOURCE,
               D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = p->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = p->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    p->list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    const float clear[4] = {clearR, clearG, clearB, clearA};
    p->list->ClearRenderTargetView(rtv, clear, 0, nullptr);
    p->list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT vpd{}; vpd.Width = (FLOAT)w; vpd.Height = (FLOAT)h; vpd.MaxDepth = 1.0f;
    D3D12_RECT sc{0, 0, w, h};
    p->list->RSSetViewports(1, &vpd); p->list->RSSetScissorRects(1, &sc);
    p->list->SetGraphicsRootSignature(p->rootSig);
    ID3D12DescriptorHeap* heaps[] = {p->srvHeap};
    p->list->SetDescriptorHeaps(1, heaps);
    p->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Light packing — identical layout to the D3D11 backend.
    float frameLights[256]; std::memset(frameLights, 0, sizeof(frameLights));
    int frameLightCount = 0;
    {
        const auto& LS = SceneLights::List();
        int n = (int)LS.size(); if (n > 16) n = 16; frameLightCount = n;
        for (int i = 0; i < n; ++i) {
            const LightSample& s = LS[i]; Vec3 d = s.dir.Normalized(); int b = i * 16;
            frameLights[b+0]=d.x; frameLights[b+1]=d.y; frameLights[b+2]=d.z; frameLights[b+3]=(float)s.type;
            frameLights[b+4]=s.pos.x; frameLights[b+5]=s.pos.y; frameLights[b+6]=s.pos.z; frameLights[b+7]=s.range;
            frameLights[b+8]=s.color.x; frameLights[b+9]=s.color.y; frameLights[b+10]=s.color.z; frameLights[b+11]=s.cosOuter;
            frameLights[b+12]=s.cosInner;
        }
    }
    Vec3 amb = SceneLights::AmbientColor();
    if (amb.x + amb.y + amb.z < 1e-4f) amb = Vec3{0.35f, 0.36f, 0.40f};
    Vec3 toLight = (SceneLight::Direction() * -1.0f).Normalized();
    {
        const auto& rs = scene.renderSettings;
        EnvSky().enabled = rs.skybox;
        EnvSky().top = {rs.skyTop.r, rs.skyTop.g, rs.skyTop.b};
        EnvSky().horizon = {rs.skyHorizon.r, rs.skyHorizon.g, rs.skyHorizon.b};
        EnvSky().bottom = {rs.skyBottom.r, rs.skyBottom.g, rs.skyBottom.b};
    }
    auto& sky = EnvSky();

    D3D12_GPU_VIRTUAL_ADDRESS cbBase = p->cbUpload->GetGPUVirtualAddress();
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu0 = p->srvHeap->GetGPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu0 = p->srvHeap->GetCPUDescriptorHandleForHeapStart();

    for (const auto& upo : scene.Objects()) {
        GameObject* go = upo.get();
        if (!go || !go->active || (ignore && go->IsSelfOrDescendantOf(ignore))) continue;
        if (!(RenderCullingMask() & (1 << (go->layer & 31)))) continue;   // camera layer cull
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || mr->wireframe || !mr->enabled) continue;
        const Mesh& mesh = mr->mesh;
        const auto& Vv = mesh.vertices; const auto& T = mesh.triangles;
        if (Vv.empty() || T.size() < 3) continue;
        const bool hasN = mesh.HasNormals();
        const bool hasUV = !mesh.uvs.empty() && mesh.uvs.size() == Vv.size();
        const bool faceCols = mesh.HasFaceColors();
        const int nv = (int)Vv.size();

        // Build the triangle-list vertices (same as the D3D11 backend).
        p->verts.clear(); p->verts.reserve(T.size() * 14);
        for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
            int a = T[i], b = T[i+1], cc = T[i+2];
            if (a < 0 || b < 0 || cc < 0 || a >= nv || b >= nv || cc >= nv) continue;
            Vec3 fn = Vec3::Cross(Vv[b] - Vv[a], Vv[cc] - Vv[a]);
            { float m = fn.Magnitude(); fn = m > 1e-8f ? fn * (1.0f / m) : Vec3{0, 1, 0}; }
            const float ax = std::fabs(fn.x), ay = std::fabs(fn.y), az = std::fabs(fn.z);
            float cr = 1, cg = 1, cb = 1;
            if (faceCols) { const Color& fc = mesh.triColors[i / 3]; cr = fc.r; cg = fc.g; cb = fc.b; }
            const int idx[3] = {a, b, cc};
            float uu[3], vvv[3];
            for (int k = 0; k < 3; ++k) {
                const Vec3& pos = Vv[idx[k]];
                if (hasUV) { uu[k] = mesh.uvs[idx[k]].x; vvv[k] = mesh.uvs[idx[k]].y; }
                else if (ax >= ay && ax >= az) { uu[k] = pos.z + 0.5f; vvv[k] = pos.y + 0.5f; }
                else if (ay >= ax && ay >= az) { uu[k] = pos.x + 0.5f; vvv[k] = pos.z + 0.5f; }
                else                           { uu[k] = pos.x + 0.5f; vvv[k] = pos.y + 0.5f; }
            }
            Vec3 e1 = Vv[b] - Vv[a], e2 = Vv[cc] - Vv[a];
            float du1 = uu[1]-uu[0], dv1 = vvv[1]-vvv[0], du2 = uu[2]-uu[0], dv2 = vvv[2]-vvv[0];
            float det = du1*dv2 - du2*dv1;
            Vec3 tan = std::fabs(det) > 1e-8f ? (e1*dv2 - e2*dv1) * (1.0f/det) : e1;
            { float m = tan.Magnitude(); tan = m > 1e-6f ? tan * (1.0f/m) : Vec3{1,0,0}; }
            for (int k = 0; k < 3; ++k) {
                const Vec3& pos = Vv[idx[k]];
                Vec3 nrm = hasN ? mesh.normals[idx[k]] : fn;
                float* o; (void)o;
                p->verts.push_back(pos.x); p->verts.push_back(pos.y); p->verts.push_back(pos.z);
                p->verts.push_back(nrm.x); p->verts.push_back(nrm.y); p->verts.push_back(nrm.z);
                p->verts.push_back(uu[k]); p->verts.push_back(vvv[k]);
                p->verts.push_back(cr); p->verts.push_back(cg); p->verts.push_back(cb);
                p->verts.push_back(tan.x); p->verts.push_back(tan.y); p->verts.push_back(tan.z);
            }
        }
        const int vcount = (int)(p->verts.size() / 14);
        if (vcount == 0) continue;
        const UINT bytes = (UINT)(p->verts.size() * sizeof(float));

        // Append the verts into the per-frame upload buffer (grow if needed).
        if (p->vbNext + bytes > p->vbCap) {
            // Grow: must wait the GPU (it may still reference the old buffer).
            p->WaitGPU();
            UINT need = p->vbNext + bytes + (1u << 20);
            Release(p->vbUpload); p->vbPtr = nullptr;
            D3D12_HEAP_PROPERTIES up{}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = need; bd.Height = 1;
            bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1;
            bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(p->dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_ID3D12Resource, (void**)&p->vbUpload))) return nullptr;
            D3D12_RANGE nr{0, 0}; p->vbUpload->Map(0, &nr, (void**)&p->vbPtr); p->vbCap = need;
        }
        UINT vbOff = p->vbNext;
        std::memcpy(p->vbPtr + vbOff, p->verts.data(), bytes);
        p->vbNext = Align(vbOff + bytes, 16);
        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = p->vbUpload->GetGPUVirtualAddress() + vbOff;
        vbv.SizeInBytes = bytes; vbv.StrideInBytes = 14 * sizeof(float);
        p->list->IASetVertexBuffers(0, 1, &vbv);

        // Fill the constant buffer for this draw.
        if (p->cbNext + p->cbStride > p->cbCap) continue;   // cap: skip extra draws
        CB cb; std::memset(&cb, 0, sizeof(cb));
        Mat4 model = go->transform->LocalToWorldMatrix();
        Mat4 mvp = vp * model;
        std::memcpy(cb.mvp, mvp.m, sizeof(cb.mvp));
        std::memcpy(cb.model, model.m, sizeof(cb.model));
        bool unlit = mr->unlit || mr->shader == MeshRenderer::Shader::Unlit;
        cb.color[0]=mr->color.r; cb.color[1]=mr->color.g; cb.color[2]=mr->color.b;
        cb.specular = unlit ? 0.0f : mr->specular; cb.shininess = mr->shininess;
        cb.emissive[0]=mr->emissive.r; cb.emissive[1]=mr->emissive.g; cb.emissive[2]=mr->emissive.b;
        cb.lightDir[0]=toLight.x; cb.lightDir[1]=toLight.y; cb.lightDir[2]=toLight.z;
        cb.unlit = unlit ? 1.0f : 0.0f;
        cb.lightColor[0]=0.85f; cb.lightColor[1]=0.85f; cb.lightColor[2]=0.85f;
        ID3D12Resource* dummy = nullptr; (void)dummy;
        std::string baseTex = mr->texture;
        cb.useTex = baseTex.empty() ? 0.0f : 1.0f;
        cb.ambient[0]=amb.x; cb.ambient[1]=amb.y; cb.ambient[2]=amb.z;
        cb.hasNormal = mr->normalMap.empty() ? 0.0f : 1.0f;
        cb.eye[0]=eye.x; cb.eye[1]=eye.y; cb.eye[2]=eye.z;
        cb.normalStrength = mr->normalStrength;
        cb.tiling[0]=baseTex.empty()?1.0f:mr->tiling.x; cb.tiling[1]=baseTex.empty()?1.0f:mr->tiling.y;
        cb.shadowAlpha = -1.0f; cb.alpha = mr->color.a;
        cb.rimColor[0]=mr->rimColor.r; cb.rimColor[1]=mr->rimColor.g; cb.rimColor[2]=mr->rimColor.b;
        cb.shaderMode = (float)(int)mr->shader; cb.toonBands = (float)mr->toonBands; cb.rimStr = mr->rimStrength;
        cb.gradTop[0]=mr->gradientTop.r; cb.gradTop[1]=mr->gradientTop.g; cb.gradTop[2]=mr->gradientTop.b;
        cb.gradBot[0]=mr->gradientBottom.r; cb.gradBot[1]=mr->gradientBottom.g; cb.gradBot[2]=mr->gradientBottom.b;
        cb.pbr[0]= unlit?0.0f:mr->metallic; cb.pbr[1]= unlit?0.0f:mr->reflectivity;
        cb.pbr[2]= mr->specularMap.empty()?0.0f:1.0f; cb.pbr[3]= mr->aoMap.empty()?0.0f:1.0f;
        cb.pbr2[0]=mr->aoStrength; cb.pbr2[1]= sky.enabled?1.0f:0.0f; cb.pbr2[2]=mr->texOffset.x; cb.pbr2[3]=mr->texOffset.y;
        cb.sky[0]=sky.top.x; cb.sky[1]=sky.top.y; cb.sky[2]=sky.top.z;
        cb.sky[4]=sky.horizon.x; cb.sky[5]=sky.horizon.y; cb.sky[6]=sky.horizon.z;
        cb.sky[8]=sky.bottom.x; cb.sky[9]=sky.bottom.y; cb.sky[10]=sky.bottom.z;
        cb.lightCount[0]=(float)frameLightCount;
        std::memcpy(cb.lights, frameLights, sizeof(frameLights));
        {
            const auto& rs = scene.renderSettings;
            bool fogOn = rs.fog && rs.fogEnd > rs.fogStart;
            cb.fog[0] = fogOn ? 1.0f : 0.0f; cb.fog[1] = rs.fogStart; cb.fog[2] = rs.fogEnd; cb.fog[3] = 0.0f;
            cb.fogCol[0] = rs.fogColor.r; cb.fogCol[1] = rs.fogColor.g; cb.fogCol[2] = rs.fogColor.b; cb.fogCol[3] = 1.0f;
        }
        std::memcpy(cb.lightVP, lightVP.m, sizeof(cb.lightVP));
        cb.shadowP[0] = shadowOn ? 1.0f : 0.0f; cb.shadowP[1] = shadowTexel;
        cb.shadowP[2] = (float)p->shadowSize; cb.shadowP[3] = 0.0f;

        UINT cbOff = p->cbNext;
        std::memcpy(p->cbPtr + cbOff, &cb, sizeof(cb));
        p->cbNext += p->cbStride;
        p->list->SetGraphicsRootConstantBufferView(0, cbBase + cbOff);

        // Bind the 5 SRVs (base, normal, spec, ao, shadow map) into the SRV ring.
        if (p->srvRingNext + 5 > p->srvRingCap) p->srvRingNext = 0;   // wrap (frame-local)
        UINT ringStart = p->srvRingNext; p->srvRingNext += 5;
        D3D12_CPU_DESCRIPTOR_HANDLE src[5] = {
            p->TextureFor(baseTex),
            mr->normalMap.empty()   ? p->whiteSrv : p->TextureFor(mr->normalMap),
            mr->specularMap.empty() ? p->whiteSrv : p->TextureFor(mr->specularMap),
            mr->aoMap.empty()       ? p->whiteSrv : p->TextureFor(mr->aoMap),
            (shadowOn && p->shadowSrvCpu.ptr) ? p->shadowSrvCpu : p->whiteSrv,
        };
        for (int s = 0; s < 5; ++s) {
            D3D12_CPU_DESCRIPTOR_HANDLE d = srvCpu0; d.ptr += (size_t)(ringStart + s) * p->srvInc;
            p->dev->CopyDescriptorsSimple(1, d, src[s], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        D3D12_GPU_DESCRIPTOR_HANDLE tableGpu = srvGpu0; tableGpu.ptr += (UINT64)ringStart * p->srvInc;
        p->list->SetGraphicsRootDescriptorTable(1, tableGpu);

        p->list->SetPipelineState(mr->color.a < 0.999f ? p->psoBlend : p->psoOpaque);
        p->list->DrawInstanced((UINT)vcount, 1, 0, 0);

        // Ground contact shadow: re-draw the SAME geometry flattened onto the ground
        // plane as flat translucent black (the shader returns that when shadowAlpha>=0).
        if (mr->groundShadow && mr->groundShadowStrength > 0.001f && p->cbNext + p->cbStride <= p->cbCap) {
            CB scb = cb;
            Mat4 sModel = Mat4::PlanarShadow(mr->groundShadowY + 0.01f, SceneLight::Direction()) * model;
            Mat4 sMvp = vp * sModel;
            std::memcpy(scb.mvp, sMvp.m, sizeof(scb.mvp));
            scb.shadowAlpha = mr->groundShadowStrength > 1.0f ? 1.0f : mr->groundShadowStrength;
            UINT soff = p->cbNext;
            std::memcpy(p->cbPtr + soff, &scb, sizeof(scb)); p->cbNext += p->cbStride;
            p->list->SetGraphicsRootConstantBufferView(0, cbBase + soff);
            p->list->SetPipelineState(p->psoBlend);
            p->list->DrawInstanced((UINT)vcount, 1, 0, 0);
        }
    }

    // Resolve MSAA (if on) into the single-sample target, then copy to the readback
    // buffer. Without MSAA the multisample-free colour target is copied directly.
    ID3D12Resource* copySrc = p->colorTex;
    if (p->msaa > 1 && p->resolveTex) {
        p->Barrier(p->colorTex, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        p->Barrier(p->resolveTex, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        p->list->ResolveSubresource(p->resolveTex, 0, p->colorTex, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
        p->Barrier(p->resolveTex, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        copySrc = p->resolveTex;
    } else {
        p->Barrier(p->colorTex, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = p->readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = (UINT)w; dst.PlacedFootprint.Footprint.Height = (UINT)h;
    dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = p->readbackPitch;
    D3D12_TEXTURE_COPY_LOCATION srcL{}; srcL.pResource = copySrc;
    srcL.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; srcL.SubresourceIndex = 0;
    p->list->CopyTextureRegion(&dst, 0, 0, 0, &srcL, nullptr);

    p->list->Close();
    ID3D12CommandList* lists[] = {p->list};
    p->queue->ExecuteCommandLists(1, lists);
    p->WaitGPU();

    // Map the readback buffer and unpack rows (stripping the 256-aligned padding).
    p->pixels.assign((std::size_t)w * h, 0u);
    UINT8* mapped = nullptr; D3D12_RANGE rr{0, (SIZE_T)p->readbackPitch * h};
    if (SUCCEEDED(p->readback->Map(0, &rr, (void**)&mapped)) && mapped) {
        for (int y = 0; y < h; ++y)
            std::memcpy(&p->pixels[(std::size_t)y * w], mapped + (size_t)y * p->readbackPitch, (size_t)w * 4);
        D3D12_RANGE wr{0, 0}; p->readback->Unmap(0, &wr);
    }
    return p->pixels.data();
}

void D3D12Renderer::Destroy() {
    if (!m_impl) return;
    Impl* p = m_impl;
    if (p->queue && p->fence) p->WaitGPU();
    for (auto& kv : p->texCache) Release(kv.second.res);
    p->texCache.clear();
    if (p->cbUpload) { D3D12_RANGE wr{0, 0}; p->cbUpload->Unmap(0, &wr); }
    Release(p->vbUpload); Release(p->cbUpload);
    Release(p->shadowTex);
    p->DestroyTargets();
    Release(p->srvHeap); Release(p->srvStage); Release(p->rtvHeap); Release(p->dsvHeap);
    Release(p->psoOpaque); Release(p->psoBlend); Release(p->psoDepth); Release(p->rootSig);
    Release(p->list); Release(p->alloc); Release(p->fence); Release(p->queue); Release(p->dev);
    if (p->fenceEvent) { CloseHandle(p->fenceEvent); p->fenceEvent = nullptr; }
    delete p; m_impl = nullptr;
}

D3D12Renderer::~D3D12Renderer() { Destroy(); }

} // namespace okay

#endif // _WIN32
