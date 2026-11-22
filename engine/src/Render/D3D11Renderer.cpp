// Direct3D 11 scene renderer (Windows only). Guarded so the Linux build / headless
// tests never reference Direct3D. Mirrors GLRenderer: offscreen MSAA render, resolve,
// read back RGBA8. See D3D11Renderer.hpp.
#if defined(_WIN32)

#include "okay/Render/D3D11Renderer.hpp"
#include "okay/Render/Culling.hpp"
#include "okay/Render/MeshHash.hpp"
#include "okay/Render/Visibility.hpp"
#include "okay/Components/LODGroup.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // the shared texture library (TextureAsset.hpp), TextureSamplingOf
#include "okay/Debug/MemoryTracker.hpp"       // "Textures (GPU)"
#include "okay/Render/GpuTexture.hpp"         // .dds / compressed import copies: BC blocks uploaded as they are
#include "okay/Render/ShadowSetup.hpp"
#include "okay/Render/D3D11Layout.hpp"
#include "okay/Render/PostQualityMath.hpp"   // shared bloom soft knee + AO power (UNVERIFIED on a GPU)
#include "okay/Render/DofMath.hpp"    // shared depth of field constants + HLSL (UNVERIFIED on a GPU)
#include "okay/Render/D3D11Hlsl.hpp"   // the HLSL text (moved out of this file, same bytes)
#include "okay/Render/D3DPostHLSL.hpp"   // shared post-chain HLSL (UNVERIFIED on a GPU)
#include "okay/Render/SsaoMath.hpp"   // shared SSAO constants + HLSL (UNVERIFIED on a GPU)
#include "okay/Render/Atmosphere.hpp"   // sky palette, fog modes, post settings
#include "okay/Scene/Transform.hpp"
#include "okay/Components/SkinnedMesh.hpp"    // skinned meshes: VB cache keyed on SkinnedMesh::version
#include "okay/Components/Character.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Render/OkayShader.hpp"   // Custom (.okayshader) surfaces

#include <d3d11.h>
#if !defined(OKAY_XBOX_UWP)
#include <d3dcompiler.h>   // run-time HLSL compile: desktop only. A UWP app loads the .cso bundle instead (ShaderBundle.hpp).
#else
#include "okay/Render/ShaderBundle.hpp"
#include "okay/Render/D3D11OverlayHlsl.hpp"   // the text the bundle is built from (renderer HLSL + the UI overlay shaders)
#include <fstream>
#endif
#include <cstdint>
#include <array>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>

namespace okay {
namespace {

// HLSL. Matrix convention matches GLRenderer: the engine's column-major Mat4 is
// uploaded as-is and used as mul(M, v) (== M*v, same as GL's uMVP*v). The engine's
// projection yields GL-style clip z in [-1,1]; D3D wants [0,1], so we remap z in the
// vertex shader: z' = (z + w) / 2. No row flip is needed on read-back (D3D textures
// are top-left, like SDL).
const char* const kHLSL = D3D11HlslSource();   // text lives in D3D11Hlsl.hpp (shared with the offline shader step)

// One compiled shader. Desktop: the D3DCompile blob (compiled when asked, as before). OKAY_XBOX_UWP: bytes from the
// pre-compiled bundle (shaders.manifest + .cso made by scripts/xbox-compile-shaders.ps1); D3DCompile does not exist in a UWP app.
struct ShaderCode {
    ID3DBlob* blob = nullptr;
    const void* data = nullptr; SIZE_T size = 0;
    std::string error;
    ShaderCode() = default;
    ShaderCode(const ShaderCode&) = delete; ShaderCode& operator=(const ShaderCode&) = delete;
    ~ShaderCode() { if (blob) blob->Release(); }
    const void* Ptr() const { return data; }
    SIZE_T Size() const { return size; }
};
#if defined(OKAY_XBOX_UWP)
std::string g_shaderDir;                     // set by D3D11Renderer::SetShaderBundleDir (the package's Shaders folder)
shaderbundle::Bundle g_bundle; bool g_bundleTried = false, g_bundleOk = false; std::string g_bundleErr;
bool EnsureBundle() {
    if (g_bundleTried) return g_bundleOk;
    g_bundleTried = true;
    auto rd = [](const std::string& name, std::vector<unsigned char>& out) {
        std::ifstream in(g_shaderDir + "/" + name, std::ios::binary);
        if (!in) return false;
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return true;
    };
    g_bundleOk = g_bundle.Load(rd, D3D11BundleSourceText().c_str(), &g_bundleErr);
    if (!g_bundleOk) Log::Error("[d3d11] shader bundle: " + g_bundleErr);
    for (const std::string& w : g_bundle.Warnings()) Log::Warning("[d3d11] shader bundle: " + w);
    return g_bundleOk;
}
#endif
bool CompileShader(const char* entry, const char* profile, ShaderCode& out) {
#if defined(OKAY_XBOX_UWP)
    (void)profile;
    if (!EnsureBundle()) { out.error = g_bundleErr; return false; }
    const std::vector<unsigned char>* b = g_bundle.Find(entry);
    if (!b) { out.error = std::string("no pre-compiled shader for ") + entry; return false; }
    out.data = b->data(); out.size = (SIZE_T)b->size();
    return true;
#else
    ID3DBlob* err = nullptr;
    const SIZE_T n = (SIZE_T)std::strlen(kHLSL);
    if (FAILED(D3DCompile(kHLSL, n, "okay3d", nullptr, nullptr, entry, profile, 0, 0, &out.blob, &err))) {
        if (err) { out.error = (const char*)err->GetBufferPointer(); err->Release(); }
        if (out.blob) { out.blob->Release(); out.blob = nullptr; }
        return false;
    }
    if (err) err->Release();
    out.data = out.blob->GetBufferPointer(); out.size = out.blob->GetBufferSize();
    return true;
#endif
}

using okay::d3d11layout::CB;
using okay::d3d11layout::PostCB;
using okay::d3d11layout::ShadowCB;
using okay::d3d11layout::kCookieRows;

// The light-mask atlas (t8): LightCookies' masks, each resampled to 64 x 64, stacked one per row (R8). UNVERIFIED on a GPU.
static bool BuildCookieAtlas(ID3D11Device* dev, ID3D11Texture2D*& tex, ID3D11ShaderResourceView*& srv) {
    if (srv) { srv->Release(); srv = nullptr; }
    if (tex) { tex->Release(); tex = nullptr; }
    std::vector<unsigned char> px((size_t)64 * 64 * kCookieRows, 255);
    const auto& st = LightCookies::Store();
    for (int r = 0; r < kCookieRows && r < (int)st.size(); ++r) {
        const LightCookie& ck = st[(size_t)r];
        if (ck.w <= 0 || ck.h <= 0) continue;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                float v = ck.At(x * ck.w / 64, y * ck.h / 64);
                v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                px[((size_t)r * 64 + y) * 64 + x] = (unsigned char)(v * 255.0f + 0.5f);
            }
    }
    D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof(td));
    td.Width = 64; td.Height = 64 * kCookieRows; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd; std::memset(&sd, 0, sizeof(sd)); sd.pSysMem = px.data(); sd.SysMemPitch = 64;
    if (FAILED(dev->CreateTexture2D(&td, &sd, &tex)) || !tex) { tex = nullptr; return false; }
    if (FAILED(dev->CreateShaderResourceView(tex, nullptr, &srv))) { srv = nullptr; tex->Release(); tex = nullptr; return false; }
    return true;
}

} // namespace

struct D3D11Renderer::Impl {
    ID3D11Device*           dev = nullptr;
    ID3D11DeviceContext*    ctx = nullptr;
    ID3D11VertexShader*     vs = nullptr;
    ID3D11PixelShader*      ps = nullptr;
    ID3D11InputLayout*      layout = nullptr;
    ID3D11Buffer*           cb = nullptr;
    ID3D11Buffer*           shCB = nullptr;    // per-frame shadows + ambient (b2)
    ID3D11DepthStencilState* depthPlanar = nullptr;   // fallback ground shadow: strict LESS + write (no stacking)
    ID3D11Buffer*           vb = nullptr; int vbCap = 0;
    ID3D11RasterizerState*  raster = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11DepthStencilState* depthNoWrite = nullptr;   // ground-shadow pass: test but don't write
    ID3D11BlendState*       blend = nullptr;           // alpha blend for the ground shadow
    ID3D11SamplerState*     samp = nullptr;
    ID3D11SamplerState*     sampPoint = nullptr;   // nearest filtering (crisp pixel-art)
    ID3D11SamplerState*     sampShadow = nullptr;  // clamp sampler for the shadow map
    // Directional shadow map (depth-only, single-sample):
    ID3D11VertexShader*     vsDepth = nullptr;     // position-only depth pass
    // GPU skinning (SkinnedMesh::UsesGpuSkinning): bind-pose VB + a per-vertex
    // joints/weights stream, deformed in the vertex shader by the bone palette.
    ID3D11VertexShader*     vsSkin = nullptr;      ID3D11InputLayout* layoutSkin = nullptr;
    ID3D11VertexShader*     vsDepthSkin = nullptr; ID3D11InputLayout* layoutDepthSkin = nullptr;
    ID3D11Buffer*           bonesCB = nullptr;     // 768 float4 (256 joints x 3 rows)
    struct SkinVB { ID3D11Buffer* buf = nullptr; int count = 0; const void* vp = nullptr; std::size_t vn = 0, tn = 0; const void* jp = nullptr; std::size_t nj = 0; std::uint64_t sampleHash = 0, fullHash = 0; unsigned lastFullHash = 0; unsigned lastUse = 0; };
    std::unordered_map<const void*, SkinVB> skinVB;   // key: &SkinnedMesh::bind
    SkinVB* EnsureSkinVB(const SkinnedMesh& sm);
    bool UploadBones(const SkinnedMesh& sm);
    bool CanGpuSkin(const SkinnedMesh* sm) const {
        return sm && vsSkin && layoutSkin && bonesCB && sm->UsesGpuSkinning() && !sm->palette.empty() &&
               sm->jointIdx.size() == sm->bind.vertices.size() && sm->jointWt.size() == sm->bind.vertices.size();
    }
    ID3D11InputLayout*      layoutDepth = nullptr;
    // Depth atlases: sun cascades side by side, and the 4x4 spot/point tile atlas.
    struct DepthTarget {
        ID3D11Texture2D* tex = nullptr; ID3D11DepthStencilView* dsv = nullptr;
        ID3D11ShaderResourceView* srv = nullptr; int w = 0, h = 0;
        void Release() {
            if (srv) srv->Release(); if (dsv) dsv->Release(); if (tex) tex->Release();
            srv = nullptr; dsv = nullptr; tex = nullptr; w = h = 0;
        }
    };
    DepthTarget sunAtlas, localAtlas;
    ID3D11Texture2D* cookieTex = nullptr; ID3D11ShaderResourceView* cookieSrv = nullptr; size_t cookieCount = 0;   // light masks (t8)
    ID3D11Buffer*           vbDepth = nullptr; int vbDepthCap = 0;
    bool EnsureDepthTarget(DepthTarget& t, int w, int h);
    // Size-dependent targets:
    int w = 0, h = 0, samples = 0;
    ID3D11Texture2D*        colorTex = nullptr;  ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D*        depthTex = nullptr;  ID3D11DepthStencilView* dsv = nullptr;
    ID3D11Texture2D*        resolveTex = nullptr;
    ID3D11ShaderResourceView* resolveSrv = nullptr;   // the resolve as the post chain's input
    ID3D11Texture2D*        staging = nullptr;         // always RGBA8 (the read-back format)
    int reqSamples = 0;   // the MSAA count asked for (`samples` is what the device gave)
    DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;      // colour format of the live set
    DXGI_FORMAT wantFmt = DXGI_FORMAT_R8G8B8A8_UNORM;  // what the next EnsureTargets should build
    DXGI_FORMAT directFmt = DXGI_FORMAT_R8G8B8A8_UNORM;   // RenderToTexture: the host texture's format
    // Post chain targets (Atmosphere.hpp PostActive): an RGBA8 output + ping-pong
    // pair at full res, half- and quarter-res bloom pairs in the frame's format.
    struct RT {
        ID3D11Texture2D* tex = nullptr; ID3D11RenderTargetView* rtv = nullptr; ID3D11ShaderResourceView* srv = nullptr;
        int w = 0, h = 0;
        void Release() { if (srv) srv->Release(); if (rtv) rtv->Release(); if (tex) tex->Release(); srv = nullptr; rtv = nullptr; tex = nullptr; w = h = 0; }
    };
    RT out, ping[2], bloom[4];
    bool MakeRT(RT& t, int W, int H, DXGI_FORMAT f);
    bool EnsurePostTargets();                  // for the live w/h/fmt (parked with the set)
    void ReleasePostTargets();
    // Post chain / sky pipeline objects, compiled on first use.
    ID3D11VertexShader* vsQuad = nullptr;
    ID3D11PixelShader *psSky = nullptr, *psDown = nullptr, *psBlur = nullptr, *psUber = nullptr, *psFxaa = nullptr;
    // Ambient occlusion (the Ambient Occlusion switch; see SsaoMath.hpp). Own
    // shaders and half-resolution targets, built on first use, so a failure here
    // only turns SSAO off. UNVERIFIED on a real GPU.
    ID3D11PixelShader *psAoDepth = nullptr, *psAo = nullptr, *psAoMul = nullptr, *psDof = nullptr;
    ID3D11PixelShader *psDofCopy = nullptr, *psDofBlur = nullptr;   // depth of field: premultiplied blurred copy
    bool ssaoTried = false, ssaoReady = false;
    struct SsaoSet {
        int w = 0, h = 0; DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        RT depth, aoA, aoB, mul;                 // half res: R32F depth, two AO images; full res: lit frame * AO
        ID3D11Texture2D* dsTex = nullptr; ID3D11DepthStencilView* dsv = nullptr;   // half-res depth buffer
        unsigned lastUse = 0;
        void Release() {
            depth.Release(); aoA.Release(); aoB.Release(); mul.Release();
            if (dsv) dsv->Release(); if (dsTex) dsTex->Release(); dsv = nullptr; dsTex = nullptr; w = h = 0;
        }
    };
    std::vector<SsaoSet> ssaoSets;               // a few sizes (editor views differ), oldest evicted
    bool EnsureSsaoShaders();
    SsaoSet* EnsureSsaoSet();                    // for the live w/h/fmt, or nullptr
    ID3D11Buffer* postCB = nullptr;
    ID3D11SamplerState* sampClamp = nullptr;
    ID3D11DepthStencilState* depthOff = nullptr;
    bool postReady = false, postTried = false;
    bool EnsurePost();
    bool hdrUnsupported = false;
    std::chrono::steady_clock::time_point clock0; bool clockSet = false; float skyTime = 0.0f;
    float cloudOff[2] = {0.0f, 0.0f};
    // Target sets for the OTHER sizes in use. The editor renders the Scene view,
    // Game view, Camera Preview and Animation preview at different sizes every
    // frame; one shared set meant destroying + re-creating MSAA, depth, resolve and
    // staging textures several times per frame. Parked sets are swapped back in.
    struct TargetSet {
        int w = 0, h = 0, samples = 0, reqSamples = 0;
        DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        ID3D11Texture2D* colorTex = nullptr; ID3D11RenderTargetView* rtv = nullptr;
        ID3D11Texture2D* depthTex = nullptr; ID3D11DepthStencilView* dsv = nullptr;
        ID3D11Texture2D* resolveTex = nullptr; ID3D11ShaderResourceView* resolveSrv = nullptr;
        ID3D11Texture2D* staging = nullptr;
        RT out, ping[2], bloom[4];
        unsigned lastUse = 0;
    };
    std::vector<TargetSet> parked;
    // Pipelined read-back (SetReadbackStream): two staging textures per view,
    // written on alternate frames; the one from last frame is read.
    struct Pipe {
        ID3D11Texture2D* st[2] = {};
        int w = 0, h = 0, idx = 0;
        bool filled[2] = {};
        std::chrono::steady_clock::time_point last{};
    };
    std::unordered_map<int, Pipe> pipes;
    int stream = -1;
    ID3D11Texture2D* directDst = nullptr;   // RenderToTexture: resolve here, skip read-back
    bool shared = false;                    // built on a host device (InitShared)
    bool stateSaved = false;                // host pipeline state is already backed up
    unsigned targetUse = 0;
    void ReleaseSet(TargetSet& t);
    std::unordered_map<std::string, ID3D11ShaderResourceView*> texCache;   // from the shared TextureAsset (TextureAsset.hpp)
    struct TexUpload { std::uint64_t serial = 0; std::size_t gpuBytes = 0; };
    std::unordered_map<std::string, TexUpload> texUpload;    // the asset serial each texture was made from, its bytes
    unsigned texGen = 0;                                      // TextureCacheGeneration() texCache was checked against
    unsigned texLibGen = 0;                                   // textures::Generation() likewise
    void FreeTexture(const std::string& name);
    std::unordered_map<std::string, TextureSampling> texSampling;   // a picture's import Filter / Wrap
    std::unordered_map<int, ID3D11SamplerState*> sampCombo;   // sampler per (filter, wrap) combination
    ID3D11SamplerState* SamplerFor(const std::string& name, bool pixel);
    std::vector<float> verts;
    std::vector<std::uint32_t> pixels;

    // Per-mesh vertex-buffer cache (mirrors GLRenderer): static geometry (imported
    // models, primitives) is expanded + uploaded ONCE (immutable buffer) and drawn
    // from GPU memory, instead of being rebuilt on the CPU and re-uploaded every
    // frame — which made large OBJ imports crawl. Revalidated per frame via an
    // allocation fingerprint + sampled content hash, exactly about once a second.
    // Skinned meshes are cached too, keyed on SkinnedMesh::version (a DYNAMIC
    // buffer rewritten in place when the pose changes); Character meshes stream.
    struct MeshVB {
        ID3D11Buffer* vbuf = nullptr;
        int vertCount = 0;
        const void* vp = nullptr;  std::size_t vn = 0;
        const void* tp = nullptr;  std::size_t tn = 0;
        const void* np = nullptr;  std::size_t nn = 0;
        const void* uvp = nullptr; std::size_t uvn = 0;
        const void* fcp = nullptr; std::size_t fcn = 0;
        std::uint64_t sampleHash = 0, fullHash = 0;
        unsigned lastUse = 0, lastFullHash = 0;
        std::uint64_t version = 0;   // SkinnedMesh::version when skinned (0 = static)
        int cap = 0;                 // dynamic buffers: capacity in vertices
        std::vector<std::array<int, 3>> slotRanges;   // material slots: {slot, first vertex, vertex count}, empty = one draw
    };
    std::unordered_map<const void*, MeshVB> meshVB;
    unsigned frame = 0;
    MeshVB* EnsureMeshVB(const Mesh& mesh, std::uint64_t version = 0, bool allowSlots = true);   // expand + upload on miss
    void BuildExpandedVerts(const Mesh& mesh, bool groupBySlot = false);
    std::vector<std::array<int, 3>> slotRanges;   // filled by BuildExpandedVerts(.., true) // fill `verts` (14 floats/vertex)

    ID3D11ShaderResourceView* TextureFor(const std::string& name);
    // Custom (.okayshader) surfaces: PSMain compiled again with the generated okaySurface() spliced in
    // (OKAY_SURFACE), one pixel shader per shader file; properties + time in b4, pictures from t9.
    // UNVERIFIED on Windows: written and syntax-checked with the MinGW headers only.
    struct CustomPS { ID3D11PixelShader* ps = nullptr; std::uint64_t generation = 0; bool failed = false, unlit = false, writesAlpha = false; };
    std::unordered_map<std::string, CustomPS> customPs;
    ID3D11Buffer* customCB = nullptr;                      // float4 time + one float4 per property
    ID3D11Texture2D* whiteTex = nullptr; ID3D11ShaderResourceView* whiteSrv = nullptr;   // an unbound custom picture
    CustomPS* EnsureCustomPS(const std::string& path);
    bool EnsureTargets(int W, int H, int S);
    void DestroyTargets();
};

D3D11Renderer::Impl::CustomPS* D3D11Renderer::Impl::EnsureCustomPS(const std::string& path) {
    const oshader::ShaderFile sf = oshader::LoadShaderFile(path);
    CustomPS& cp = customPs[path];
    if (cp.generation == sf.generation && (cp.ps || cp.failed)) return cp.failed ? nullptr : &cp;
    if (cp.ps) { cp.ps->Release(); cp.ps = nullptr; }
    cp = CustomPS{};
    cp.generation = sf.generation;
    const oshader::Compiled* c = sf.compiled.get();
    if (!c || !c->ok) { cp.failed = true; return nullptr; }
#if defined(OKAY_XBOX_UWP)
    Log::Error("[d3d11] custom shaders need the run-time HLSL compiler, which a UWP app does not have; ", path, " draws magenta");
    cp.failed = true;
    return nullptr;
#else
    std::string src = kHLSL;
    const std::size_t at = src.find("float4 PSMain(");
    if (at == std::string::npos) { cp.failed = true; return nullptr; }
    src.insert(at, "#define OKAY_SURFACE 1\n" + c->hlsl);
    ID3DBlob* blob = nullptr; ID3DBlob* err = nullptr;
    if (FAILED(D3DCompile(src.c_str(), (SIZE_T)src.size(), "okaycustom", nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &blob, &err))) {
        Log::Error("[d3d11] custom shader ", path, " did not compile: ", err ? (const char*)err->GetBufferPointer() : "?");
        if (err) err->Release();
        if (blob) blob->Release();
        cp.failed = true;
        return nullptr;
    }
    if (err) err->Release();
    dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &cp.ps);
    blob->Release();
    if (!customCB) {
        D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
        bd.ByteWidth = (UINT)(16 * (1 + oshader::kMaxProperties)); bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &customCB))) customCB = nullptr;
    }
    if (!whiteSrv) {
        const unsigned char white[4] = {255, 255, 255, 255};
        D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof(td));
        td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd; std::memset(&sd, 0, sizeof(sd)); sd.pSysMem = white; sd.SysMemPitch = 4;
        if (SUCCEEDED(dev->CreateTexture2D(&td, &sd, &whiteTex)) && whiteTex)
            if (FAILED(dev->CreateShaderResourceView(whiteTex, nullptr, &whiteSrv))) whiteSrv = nullptr;
    }
    if (!cp.ps || !customCB) { if (cp.ps) cp.ps->Release(); cp.ps = nullptr; cp.failed = true; return nullptr; }
    cp.unlit = c->unlit;
    cp.writesAlpha = c->writesAlpha;
    return &cp;
#endif
}

bool D3D11Renderer::InitShared(void* device) {
    if (m_impl) return m_impl->shared;
    if (!device) return false;
    m_impl = new Impl();
    m_impl->dev = static_cast<ID3D11Device*>(device);
    m_impl->dev->AddRef();
    m_impl->dev->GetImmediateContext(&m_impl->ctx);   // AddRefs
    m_impl->shared = true;
    return Init();
}
bool D3D11Renderer::Shared() const { return m_impl && m_impl->shared && m_impl->vs; }
bool D3D11Renderer::SupportsGpuSkinning() const {
    return m_impl && m_impl->vsSkin && m_impl->layoutSkin && m_impl->vsDepthSkin && m_impl->layoutDepthSkin && m_impl->bonesCB;
}

bool D3D11Renderer::Init() {
    if (m_impl && m_impl->vs) return true;
    if (!m_impl) m_impl = new Impl();
    Impl* p = m_impl;
    if (!p->dev) {
        UINT flags = 0;
        D3D_FEATURE_LEVEL got;
        const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, want, 3,
                                     D3D11_SDK_VERSION, &p->dev, &got, &p->ctx))) {
            Log::Error("[d3d11] device creation failed");
            Destroy(); return false;
        }
    }
    ShaderCode vsb, psb;
    if (!CompileShader("VSMain", "vs_4_0", vsb) || !CompileShader("PSMain", "ps_4_0", psb)) {
        Log::Error("[d3d11] shader compile failed" + (vsb.error.empty() ? std::string() : ": " + vsb.error)); Destroy(); return false;
    }
    p->dev->CreateVertexShader(vsb.Ptr(), vsb.Size(), nullptr, &p->vs);
    p->dev->CreatePixelShader(psb.Ptr(), psb.Size(), nullptr, &p->ps);
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    p->dev->CreateInputLayout(il, 5, vsb.Ptr(), vsb.Size(), &p->layout);

    D3D11_BUFFER_DESC cbd; std::memset(&cbd, 0, sizeof(cbd));
    cbd.ByteWidth = sizeof(CB); cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    p->dev->CreateBuffer(&cbd, nullptr, &p->cb);
    cbd.ByteWidth = sizeof(ShadowCB);
    p->dev->CreateBuffer(&cbd, nullptr, &p->shCB);

    D3D11_RASTERIZER_DESC rd; std::memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;   // match GLRenderer (no cull)
    rd.DepthClipEnable = TRUE; rd.MultisampleEnable = TRUE;
    p->dev->CreateRasterizerState(&rd, &p->raster);

    D3D11_DEPTH_STENCIL_DESC dd; std::memset(&dd, 0, sizeof(dd));
    dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    p->dev->CreateDepthStencilState(&dd, &p->depth);
    // Ground-shadow pass: depth-test but don't write (so it never occludes geometry).
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    p->dev->CreateDepthStencilState(&dd, &p->depthNoWrite);
    // Fallback ground shadow: every flattened triangle lies on one plane, so a
    // strict LESS test + depth writes blends each pixel once (no darker overlaps).
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS;
    p->dev->CreateDepthStencilState(&dd, &p->depthPlanar);
    // Straight alpha blend for the translucent shadow.
    D3D11_BLEND_DESC bd2; std::memset(&bd2, 0, sizeof(bd2));
    bd2.RenderTarget[0].BlendEnable = TRUE;
    bd2.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd2.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd2.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd2.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd2.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd2.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd2.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    p->dev->CreateBlendState(&bd2, &p->blend);

    D3D11_SAMPLER_DESC sd; std::memset(&sd, 0, sizeof(sd));
    sd.Filter = D3D11_FILTER_ANISOTROPIC;   // sharp textures at grazing angles (floors/terrain)
    sd.MaxAnisotropy = 8;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    p->dev->CreateSamplerState(&sd, &p->samp);
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;   // crisp pixel-art (no up-close blur)
    sd.MaxAnisotropy = 1;
    p->dev->CreateSamplerState(&sd, &p->sampPoint);
    // Shadow-map sampler: a filtered depth COMPARISON (hardware 2x2 PCF per tap),
    // clamped at the borders.
    D3D11_SAMPLER_DESC ss; std::memset(&ss, 0, sizeof(ss));
    ss.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    ss.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ss.MaxLOD = D3D11_FLOAT32_MAX;
    p->dev->CreateSamplerState(&ss, &p->sampShadow);

    // Depth-only vertex shader + position-only input layout for the shadow pass.
    ShaderCode vdb;
    if (CompileShader("VSDepth", "vs_4_0", vdb)) {
        p->dev->CreateVertexShader(vdb.Ptr(), vdb.Size(), nullptr, &p->vsDepth);
        const D3D11_INPUT_ELEMENT_DESC ild[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        p->dev->CreateInputLayout(ild, 1, vdb.Ptr(), vdb.Size(), &p->layoutDepth);
    }

    // GPU skinning shaders + layouts (stream 0: the usual 14-float vertex,
    // stream 1: joints.xyzw + weights.xyzw). Optional: without them skinned
    // meshes keep deforming on the CPU.
    {
        ShaderCode sb;
        if (CompileShader("VSSkin", "vs_4_0", sb)) {
            p->dev->CreateVertexShader(sb.Ptr(), sb.Size(), nullptr, &p->vsSkin);
            const D3D11_INPUT_ELEMENT_DESC ils[] = {
                {"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR",        0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            p->dev->CreateInputLayout(ils, 7, sb.Ptr(), sb.Size(), &p->layoutSkin);
        } else if (!sb.error.empty()) Log::Warning("[d3d11] skinning shader: " + sb.error);
        ShaderCode sb2;
        if (CompileShader("VSDepthSkin", "vs_4_0", sb2)) {
            p->dev->CreateVertexShader(sb2.Ptr(), sb2.Size(), nullptr, &p->vsDepthSkin);
            const D3D11_INPUT_ELEMENT_DESC ilds[] = {
                {"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            p->dev->CreateInputLayout(ilds, 3, sb2.Ptr(), sb2.Size(), &p->layoutDepthSkin);
        }
        D3D11_BUFFER_DESC bb; std::memset(&bb, 0, sizeof(bb));
        bb.ByteWidth = 768 * 16; bb.Usage = D3D11_USAGE_DYNAMIC;
        bb.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        p->dev->CreateBuffer(&bb, nullptr, &p->bonesCB);
    }

    return p->dev && p->vs && p->ps && p->layout && p->cb && p->raster && p->depth && p->samp;
}

bool D3D11Renderer::Available() const { return m_impl && m_impl->dev && m_impl->vs; }

void D3D11Renderer::Impl::ReleaseSet(TargetSet& t) {
    if (t.rtv)        t.rtv->Release();
    if (t.dsv)        t.dsv->Release();
    if (t.colorTex)   t.colorTex->Release();
    if (t.depthTex)   t.depthTex->Release();
    if (t.resolveTex) t.resolveTex->Release();
    if (t.resolveSrv) t.resolveSrv->Release();
    if (t.staging)    t.staging->Release();
    t.out.Release(); t.ping[0].Release(); t.ping[1].Release();
    for (RT& b : t.bloom) b.Release();
    t = TargetSet{};
}

void D3D11Renderer::Impl::DestroyTargets() {
    if (rtv)        { rtv->Release(); rtv = nullptr; }
    if (dsv)        { dsv->Release(); dsv = nullptr; }
    if (colorTex)   { colorTex->Release(); colorTex = nullptr; }
    if (depthTex)   { depthTex->Release(); depthTex = nullptr; }
    if (resolveTex) { resolveTex->Release(); resolveTex = nullptr; }
    if (resolveSrv) { resolveSrv->Release(); resolveSrv = nullptr; }
    if (staging)    { staging->Release(); staging = nullptr; }
    ReleasePostTargets();
    w = h = samples = reqSamples = 0;
}

void D3D11Renderer::Impl::ReleasePostTargets() {
    out.Release(); ping[0].Release(); ping[1].Release();
    for (RT& b : bloom) b.Release();
}

bool D3D11Renderer::Impl::MakeRT(RT& t, int W, int H, DXGI_FORMAT f) {
    t.Release();
    D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof(td));
    td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = f; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &t.tex))) { t.Release(); return false; }
    if (FAILED(dev->CreateRenderTargetView(t.tex, nullptr, &t.rtv)) ||
        FAILED(dev->CreateShaderResourceView(t.tex, nullptr, &t.srv))) { t.Release(); return false; }
    t.w = W; t.h = H;
    return true;
}

bool D3D11Renderer::Impl::EnsurePostTargets() {
    if (out.tex && out.w == w && out.h == h) return true;
    const int hw = w / 2 > 0 ? w / 2 : 1, hh = h / 2 > 0 ? h / 2 : 1;
    const int qw = w / 4 > 0 ? w / 4 : 1, qh = h / 4 > 0 ? h / 4 : 1;
    const bool ok = MakeRT(out, w, h, DXGI_FORMAT_R8G8B8A8_UNORM) &&
                    MakeRT(ping[0], w, h, DXGI_FORMAT_R8G8B8A8_UNORM) && MakeRT(ping[1], w, h, DXGI_FORMAT_R8G8B8A8_UNORM) &&
                    MakeRT(bloom[0], hw, hh, fmt) && MakeRT(bloom[1], hw, hh, fmt) &&
                    MakeRT(bloom[2], qw, qh, fmt) && MakeRT(bloom[3], qw, qh, fmt);
    if (!ok) ReleasePostTargets();
    return ok;
}

// The fullscreen-pass shaders, the sky / post constants, a clamp sampler and a
// depth-off state. Compiled once; a failure leaves the renderer on its plain path.
bool D3D11Renderer::Impl::EnsurePost() {
    if (postTried) return postReady;
    postTried = true;
    auto ps = [&](const char* entry, ID3D11PixelShader*& outPs) {
        ShaderCode b;
        const bool ok = CompileShader(entry, "ps_4_0", b) &&
                        SUCCEEDED(dev->CreatePixelShader(b.Ptr(), b.Size(), nullptr, &outPs));
        if (!ok && !b.error.empty()) Log::Warning(std::string("[d3d11] post shader ") + entry + ": " + b.error);
        return ok;
    };
    ShaderCode vb;
    bool ok = CompileShader("VSQuad", "vs_4_0", vb) &&
              SUCCEEDED(dev->CreateVertexShader(vb.Ptr(), vb.Size(), nullptr, &vsQuad));
    if (!ok && !vb.error.empty()) Log::Warning("[d3d11] quad shader: " + vb.error);
    ok = ok && ps("PSSky", psSky) && ps("PSDown", psDown) && ps("PSBlur", psBlur) && ps("PSUber", psUber) && ps("PSFxaa", psFxaa);
    if (ok) {
        D3D11_BUFFER_DESC cbd; std::memset(&cbd, 0, sizeof(cbd));
        cbd.ByteWidth = sizeof(PostCB); cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ok = SUCCEEDED(dev->CreateBuffer(&cbd, nullptr, &postCB));
    }
    if (ok) {
        D3D11_SAMPLER_DESC sd; std::memset(&sd, 0, sizeof(sd));
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        ok = SUCCEEDED(dev->CreateSamplerState(&sd, &sampClamp));
    }
    if (ok) {
        D3D11_DEPTH_STENCIL_DESC dd; std::memset(&dd, 0, sizeof(dd));
        dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
        ok = SUCCEEDED(dev->CreateDepthStencilState(&dd, &depthOff));
    }
    postReady = ok;
    if (!ok) Log::Warning("[d3d11] sky / post-processing unavailable on this device");
    return ok;
}

// Ambient occlusion shaders (SsaoMath.hpp). Separate from EnsurePost so a compile
// failure here costs only the SSAO pass, never the sky or the rest of the chain.
bool D3D11Renderer::Impl::EnsureSsaoShaders() {
    if (ssaoTried) return ssaoReady;
    ssaoTried = true;
    if (!postReady) return false;
    auto ps = [&](const char* entry, ID3D11PixelShader*& outPs) {
        ShaderCode b;
        const bool ok = CompileShader(entry, "ps_4_0", b) &&
                        SUCCEEDED(dev->CreatePixelShader(b.Ptr(), b.Size(), nullptr, &outPs));
        if (!ok && !b.error.empty()) Log::Warning(std::string("[d3d11] ssao shader ") + entry + ": " + b.error);
        return ok;
    };
    ssaoReady = ps("PSAoDepth", psAoDepth) && ps("PSAo", psAo) && ps("PSAoMul", psAoMul) && vsDepth && layoutDepth;
    if (ssaoReady && !(ps("PSDof", psDof) && ps("PSDofCopy", psDofCopy) && ps("PSDofBlur", psDofBlur))) {
        // separate: a depth of field compile failure must not cost ambient occlusion
        for (ID3D11PixelShader** s : {&psDof, &psDofCopy, &psDofBlur}) if (*s) { (*s)->Release(); *s = nullptr; }
    }
    if (!ssaoReady) Log::Warning("[d3d11] ambient occlusion unavailable on this device");
    return ssaoReady;
}

// The AO targets for the live frame size and format: half-res depth (R32F + a depth
// buffer) and two AO images, plus a full-res copy of the lit frame (*AO).
D3D11Renderer::Impl::SsaoSet* D3D11Renderer::Impl::EnsureSsaoSet() {
    for (SsaoSet& s : ssaoSets)
        if (s.w == w && s.h == h && s.fmt == fmt) { s.lastUse = ++targetUse; return &s; }
    while (ssaoSets.size() >= 3) {
        std::size_t oldest = 0;
        for (std::size_t i = 1; i < ssaoSets.size(); ++i) if (ssaoSets[i].lastUse < ssaoSets[oldest].lastUse) oldest = i;
        ssaoSets[oldest].Release();
        ssaoSets.erase(ssaoSets.begin() + (std::ptrdiff_t)oldest);
    }
    const int hw = w / 2 > 0 ? w / 2 : 1, hh = h / 2 > 0 ? h / 2 : 1;
    SsaoSet s; s.w = w; s.h = h; s.fmt = fmt; s.lastUse = ++targetUse;
    bool ok = MakeRT(s.depth, hw, hh, DXGI_FORMAT_R32_FLOAT) &&
              MakeRT(s.aoA, hw, hh, DXGI_FORMAT_R8G8B8A8_UNORM) && MakeRT(s.aoB, hw, hh, DXGI_FORMAT_R8G8B8A8_UNORM) &&
              MakeRT(s.mul, w, h, fmt);
    if (ok) {
        D3D11_TEXTURE2D_DESC dz; std::memset(&dz, 0, sizeof(dz));
        dz.Width = hw; dz.Height = hh; dz.MipLevels = 1; dz.ArraySize = 1;
        dz.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dz.SampleDesc.Count = 1;
        dz.Usage = D3D11_USAGE_DEFAULT; dz.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ok = SUCCEEDED(dev->CreateTexture2D(&dz, nullptr, &s.dsTex)) && s.dsTex &&
             SUCCEEDED(dev->CreateDepthStencilView(s.dsTex, nullptr, &s.dsv));
    }
    if (!ok) { s.Release(); return nullptr; }
    ssaoSets.push_back(s);
    return &ssaoSets.back();
}

bool D3D11Renderer::Impl::EnsureTargets(int W, int H, int S) {
    // A fully-built target set is cached by size; a half-built one (a mid-sequence
    // CreateTexture2D/View failure) must NOT satisfy the cache — check the views we
    // actually bind, not just colorTex. Otherwise the next call returns "ready" with
    // a null rtv/dsv and the render binds a null render target (hard crash).
    ++targetUse;
    if (colorTex && rtv && dsv && resolveTex && staging &&
        w == W && h == H && reqSamples == S && fmt == wantFmt) return true;
    // Park the current (complete) set, then swap in a parked one of this size.
    if (colorTex && rtv && dsv && resolveTex && staging) {
        TargetSet t;
        t.w = w; t.h = h; t.samples = samples; t.reqSamples = reqSamples; t.fmt = fmt;
        t.colorTex = colorTex; t.rtv = rtv; t.depthTex = depthTex; t.dsv = dsv;
        t.resolveTex = resolveTex; t.resolveSrv = resolveSrv; t.staging = staging; t.lastUse = targetUse;
        t.out = out; t.ping[0] = ping[0]; t.ping[1] = ping[1]; for (int k = 0; k < 4; ++k) t.bloom[k] = bloom[k];
        colorTex = depthTex = resolveTex = staging = nullptr; rtv = nullptr; dsv = nullptr; resolveSrv = nullptr;
        out = RT{}; ping[0] = RT{}; ping[1] = RT{}; for (RT& b : bloom) b = RT{};
        parked.push_back(t);
    }
    for (std::size_t i = 0; i < parked.size(); ++i) {
        TargetSet& t = parked[i];
        if (t.w != W || t.h != H || t.reqSamples != S || t.fmt != wantFmt) continue;
        DestroyTargets();
        w = t.w; h = t.h; samples = t.samples; reqSamples = t.reqSamples; fmt = t.fmt;
        colorTex = t.colorTex; rtv = t.rtv; depthTex = t.depthTex; dsv = t.dsv;
        resolveTex = t.resolveTex; resolveSrv = t.resolveSrv; staging = t.staging;
        out = t.out; ping[0] = t.ping[0]; ping[1] = t.ping[1]; for (int k = 0; k < 4; ++k) bloom[k] = t.bloom[k];
        parked.erase(parked.begin() + (std::ptrdiff_t)i);
        return true;
    }
    // Keep a handful (a view being resized leaves a trail of stale sizes).
    while (parked.size() > 4) {
        std::size_t oldest = 0;
        for (std::size_t i = 1; i < parked.size(); ++i) if (parked[i].lastUse < parked[oldest].lastUse) oldest = i;
        ReleaseSet(parked[oldest]);
        parked.erase(parked.begin() + (std::ptrdiff_t)oldest);
    }
    DestroyTargets();
    // Any partial failure below returns false; reset the size cache so the next call
    // starts clean (DestroyTargets nulls the resources, but the size fields linger).
    w = 0; h = 0; samples = 0;
    struct FailReset { Impl* p; bool ok = false; ~FailReset(){ if(!ok){ p->DestroyTargets(); p->w=p->h=p->samples=0; } } } fr{this};
    w = W; h = H; reqSamples = S; fmt = wantFmt;
    // Clamp the requested MSAA to what the device actually supports for BOTH the color
    // and depth formats. Creating a multisampled texture with an unsupported count is a
    // common hard-failure (and on some drivers a crash) on integrated GPUs / RDP / VMs,
    // so drop to the next lower supported count, down to 1 (no MSAA).
    int want = S < 1 ? 1 : S;
    samples = 1;
    for (int s = want; s >= 1; --s) {
        UINT qC = 0, qD = 0;
        if (SUCCEEDED(dev->CheckMultisampleQualityLevels(fmt, (UINT)s, &qC)) && qC > 0 &&
            SUCCEEDED(dev->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, (UINT)s, &qD)) && qD > 0) {
            samples = s; break;
        }
    }

    D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof(td));
    td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = fmt; td.SampleDesc.Count = samples;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &colorTex))) return false;
    if (FAILED(dev->CreateRenderTargetView(colorTex, nullptr, &rtv))) return false;

    D3D11_TEXTURE2D_DESC dz = td;
    dz.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dz.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(dev->CreateTexture2D(&dz, nullptr, &depthTex))) return false;
    if (FAILED(dev->CreateDepthStencilView(depthTex, nullptr, &dsv))) return false;

    D3D11_TEXTURE2D_DESC tr = td;
    tr.SampleDesc.Count = 1; tr.BindFlags = D3D11_BIND_SHADER_RESOURCE;   // the post chain samples the resolve
    if (FAILED(dev->CreateTexture2D(&tr, nullptr, &resolveTex))) return false;
    if (FAILED(dev->CreateShaderResourceView(resolveTex, nullptr, &resolveSrv))) return false;

    // Read-back staging: always RGBA8 (the chain's output format; a float frame
    // never reaches the CPU directly).
    D3D11_TEXTURE2D_DESC ts = tr;
    ts.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ts.Usage = D3D11_USAGE_STAGING; ts.BindFlags = 0; ts.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(dev->CreateTexture2D(&ts, nullptr, &staging))) return false;
    fr.ok = true;   // fully built — keep the cache
    return true;
}

// (Re)create a shadow depth atlas: a single-sample typeless depth texture usable
// BOTH as a depth-stencil target (depth passes) and a shader resource (the main
// pass's filtered compare). Returns false on failure (renderer works, no shadows).
bool D3D11Renderer::Impl::EnsureDepthTarget(DepthTarget& t, int w, int h) {
    if (t.tex && t.w == w && t.h == h) return true;
    t.Release();
    if (!vsDepth || !layoutDepth) return false;

    D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof(td));
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R24G8_TYPELESS; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &t.tex))) { t.tex = nullptr; return false; }

    D3D11_DEPTH_STENCIL_VIEW_DESC dvd; std::memset(&dvd, 0, sizeof(dvd));
    dvd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    if (FAILED(dev->CreateDepthStencilView(t.tex, &dvd, &t.dsv))) { t.dsv = nullptr; t.Release(); return false; }

    D3D11_SHADER_RESOURCE_VIEW_DESC svd; std::memset(&svd, 0, sizeof(svd));
    svd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    svd.Texture2D.MipLevels = 1;
    if (FAILED(dev->CreateShaderResourceView(t.tex, &svd, &t.srv))) { t.srv = nullptr; t.Release(); return false; }
    t.w = w; t.h = h;
    return true;
}

// ---- Per-mesh vertex-buffer cache (mirrors GLRenderer.cpp) -------------------
// (Was a byte-at-a-time FNV-1a loop, ~1.3 ns per byte; now okay::meshhash::Bytes, about 20 times faster. See MeshHash.hpp.)
static std::uint64_t FnvBytes(const void* data, std::size_t nbytes,
                              std::uint64_t seed = 1469598103934665603ull) {
    return meshhash::Bytes(data, nbytes, seed);
}
static std::uint64_t MeshSampleHash(const Mesh& m) {
    std::size_t sizes[7] = { m.vertices.size(), m.triangles.size(), m.normals.size(),
                             m.uvs.size(), m.triColors.size(), m.vertexColors.size(), m.triSlot.size() };
    std::uint64_t h = FnvBytes(sizes, sizeof(sizes));
    { const std::size_t cn = m.vertexColors.size(), cstep = cn / 2048 + 1;     // a stroke of vertex paint shows at once
      for (std::size_t i = 0; i < cn; i += cstep) h = FnvBytes(&m.vertexColors[i], sizeof(Color), h); }
    const std::size_t vn = m.vertices.size(), step = vn / 2048 + 1;
    // A small mesh is sampled at every vertex: hash its array in one go (a chunk of a voxel terrain is such a mesh,
    // and a scene can show hundreds of them every frame).
    if (step == 1) { if (vn) h = FnvBytes(m.vertices.data(), vn * sizeof(Vec3), h); }
    else for (std::size_t i = 0; i < vn; i += step) h = FnvBytes(&m.vertices[i], sizeof(Vec3), h);
    const std::size_t tn = m.triangles.size(), tstep = tn / 2048 + 1;
    if (tstep == 1) { if (tn) h = FnvBytes(m.triangles.data(), tn * sizeof(int), h); }
    else for (std::size_t i = 0; i < tn; i += tstep) h = FnvBytes(&m.triangles[i], sizeof(int), h);
    return h;
}
static std::uint64_t MeshFullHash(const Mesh& m) {
    std::uint64_t h = 1469598103934665603ull;
    if (!m.vertices.empty())  h = FnvBytes(m.vertices.data(),  m.vertices.size()  * sizeof(Vec3),  h);
    if (!m.normals.empty())   h = FnvBytes(m.normals.data(),   m.normals.size()   * sizeof(Vec3),  h);
    if (!m.uvs.empty())       h = FnvBytes(m.uvs.data(),       m.uvs.size()       * sizeof(Vec2),  h);
    if (!m.triangles.empty()) h = FnvBytes(m.triangles.data(), m.triangles.size() * sizeof(int),   h);
    if (!m.triColors.empty()) h = FnvBytes(m.triColors.data(), m.triColors.size() * sizeof(Color), h);
    if (!m.vertexColors.empty()) h = FnvBytes(m.vertexColors.data(), m.vertexColors.size() * sizeof(Color), h);
    if (!m.triSlot.empty()) h = FnvBytes(m.triSlot.data(), m.triSlot.size() * sizeof(int), h);
    return h;
}

// Content hashes of a skin's joint data (indices + weights + palette size, since
// the upload clamps against it), like the plain-mesh cache: a cheap sample each
// frame and an exact pass about once a second, so a SkinnedMesh freed and
// re-allocated at the same addresses can't be served the old stream.
static std::uint64_t SkinSampleHash(const SkinnedMesh& sm) {
    std::size_t sizes[3] = { sm.jointIdx.size(), sm.jointWt.size(), sm.palette.size() };
    std::uint64_t h = FnvBytes(sizes, sizeof(sizes));
    const std::size_t n = sm.jointIdx.size(), step = n / 2048 + 1;
    for (std::size_t i = 0; i < n; i += step) h = FnvBytes(&sm.jointIdx[i], sizeof(sm.jointIdx[i]), h);
    const std::size_t wn = sm.jointWt.size(), wstep = wn / 2048 + 1;
    for (std::size_t i = 0; i < wn; i += wstep) h = FnvBytes(&sm.jointWt[i], sizeof(sm.jointWt[i]), h);
    return h;
}
static std::uint64_t SkinFullHash(const SkinnedMesh& sm) {
    std::uint64_t h = 1469598103934665603ull;
    if (!sm.jointIdx.empty()) h = FnvBytes(sm.jointIdx.data(), sm.jointIdx.size() * sizeof(sm.jointIdx[0]), h);
    if (!sm.jointWt.empty())  h = FnvBytes(sm.jointWt.data(),  sm.jointWt.size()  * sizeof(sm.jointWt[0]),  h);
    return h;
}

// Expand indexed triangles into the interleaved non-indexed layout the shader
// consumes: pos(3)+normal(3)+uv(2)+color(3)+tangent(3) = 14 floats per vertex.
void D3D11Renderer::Impl::BuildExpandedVerts(const Mesh& mesh, bool groupBySlot) {
    const auto& V = mesh.vertices; const auto& T = mesh.triangles;
    const bool hasN = mesh.HasNormals();
    const bool hasUV = !mesh.uvs.empty() && mesh.uvs.size() == V.size();
    const bool faceCols = mesh.HasFaceColors();
    const bool vcols = mesh.HasVertexColors();   // per-vertex colours blend smoothly across each face
    const int nv = (int)V.size();
    verts.resize(T.size() / 3 * 3 * 14);   // written through a raw pointer, trimmed at the end
    float* out = verts.data();
    // Per-slot drawing: with material slots the triangles are written grouped by slot (stable order) and each group's first
    // vertex and count are kept, so each slot can be drawn with its own picture (same idea as the OpenGL renderer).
    slotRanges.clear();
    std::vector<int> order;
    const bool bySlot = groupBySlot && mesh.HasSlots();
    if (bySlot) order = mesh.GroupBySlot().order;
    const std::size_t triCount = T.size() / 3;
    for (std::size_t oi = 0; oi < triCount; ++oi) {
        const std::size_t i = (bySlot ? (std::size_t)order[oi] : oi) * 3;
        if (bySlot) {
            const int slot = mesh.triSlot[i / 3];
            const int firstVert = (int)((out - verts.data()) / 14);
            if (slotRanges.empty() || slotRanges.back()[0] != slot) slotRanges.push_back({slot, firstVert, 0});
        }
        int a = T[i], b = T[i + 1], cc = T[i + 2];
        if (a < 0 || b < 0 || cc < 0 || a >= nv || b >= nv || cc >= nv) continue; // skip bad tris
        Vec3 fn = Vec3::Cross(V[b] - V[a], V[cc] - V[a]);
        { float m = fn.Magnitude(); fn = m > 1e-8f ? fn * (1.0f / m) : Vec3{0, 1, 0}; }
        const float ax = std::fabs(fn.x), ay = std::fabs(fn.y), az = std::fabs(fn.z);
        float cr = 1.0f, cg = 1.0f, cb2 = 1.0f;  // white = no-op when there are no face colors
        if (faceCols) { const Color& fc = mesh.triColors[i / 3]; cr = fc.r; cg = fc.g; cb2 = fc.b; }
        const int idx[3] = {a, b, cc};
        float uu[3], vv[3];
        for (int k = 0; k < 3; ++k) {
            const Vec3& pos = V[idx[k]];
            if (hasUV) { uu[k] = mesh.uvs[idx[k]].x; vv[k] = mesh.uvs[idx[k]].y; }
            else if (ax >= ay && ax >= az) { uu[k] = pos.z + 0.5f; vv[k] = pos.y + 0.5f; }
            else if (ay >= ax && ay >= az) { uu[k] = pos.x + 0.5f; vv[k] = pos.z + 0.5f; }
            else                           { uu[k] = pos.x + 0.5f; vv[k] = pos.y + 0.5f; }
        }
        Vec3 e1 = V[b] - V[a], e2 = V[cc] - V[a];
        float du1 = uu[1] - uu[0], dv1 = vv[1] - vv[0];
        float du2 = uu[2] - uu[0], dv2 = vv[2] - vv[0];
        float det = du1 * dv2 - du2 * dv1;
        Vec3 tan = std::fabs(det) > 1e-8f ? (e1 * dv2 - e2 * dv1) * (1.0f / det) : e1;
        { float m = tan.Magnitude(); tan = m > 1e-6f ? tan * (1.0f / m) : Vec3{1, 0, 0}; }
        for (int k = 0; k < 3; ++k) {
            const Vec3& pos = V[idx[k]];
            Vec3 nrm = hasN ? mesh.normals[idx[k]] : fn;
            out[0] = pos.x; out[1] = pos.y; out[2] = pos.z;
            out[3] = nrm.x; out[4] = nrm.y; out[5] = nrm.z;
            out[6] = uu[k]; out[7] = vv[k];
            out[8] = cr;    out[9] = cg;    out[10] = cb2;
            if (vcols) { const Color& vc = mesh.vertexColors[idx[k]]; out[8] *= vc.r; out[9] *= vc.g; out[10] *= vc.b; }
            out[11] = tan.x; out[12] = tan.y; out[13] = tan.z;
            out += 14;
        }
    }
    verts.resize((std::size_t)(out - verts.data()));
    if (bySlot) {
        const int total = (int)(verts.size() / 14);
        for (std::size_t r = 0; r < slotRanges.size(); ++r) slotRanges[r][2] = (r + 1 < slotRanges.size() ? slotRanges[r + 1][1] : total) - slotRanges[r][1];
    }
}

// Return the up-to-date cached GPU buffer for a mesh, expanding + uploading only
// when the mesh actually changed. nullptr = mesh has no drawable triangles.
// Joint indices + weights per EXPANDED vertex (same triangle-corner order and
// bad-triangle skipping as BuildExpandedVerts), uploaded once per bind mesh.
D3D11Renderer::Impl::SkinVB* D3D11Renderer::Impl::EnsureSkinVB(const SkinnedMesh& sm) {
    const Mesh& m = sm.bind;
    SkinVB& e = skinVB[&m];
    e.lastUse = frame;
    if (e.buf && e.vp == (const void*)m.vertices.data() && e.vn == m.vertices.size() &&
        e.tn == m.triangles.size() && e.jp == (const void*)sm.jointIdx.data() && e.nj == sm.palette.size()) {
        bool same = SkinSampleHash(sm) == e.sampleHash;
        if (same && frame - e.lastFullHash >= 60) {   // exact check ~once a second
            same = SkinFullHash(sm) == e.fullHash;
            e.lastFullHash = frame;
        }
        if (same) return &e;
    }
    if (e.buf) { e.buf->Release(); e.buf = nullptr; }
    const auto& T = m.triangles; const int nv = (int)m.vertices.size();
    std::vector<float> d; d.reserve(T.size() * 8);
    for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
        const int idx[3] = {T[i], T[i + 1], T[i + 2]};
        if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0 || idx[0] >= nv || idx[1] >= nv || idx[2] >= nv) continue;
        for (int k = 0; k < 3; ++k) {
            const auto& ji = sm.jointIdx[idx[k]]; const auto& jw = sm.jointWt[idx[k]];
            const int maxJ = (int)sm.palette.size() - 1;
            for (int q = 0; q < 4; ++q) d.push_back((float)(ji[q] < 0 ? 0 : ji[q] > maxJ ? maxJ : ji[q]));
            for (int q = 0; q < 4; ++q) d.push_back(ji[q] < 0 || ji[q] > maxJ ? 0.0f : jw[q]);
        }
    }
    e.vp = m.vertices.data(); e.vn = m.vertices.size(); e.tn = m.triangles.size(); e.jp = sm.jointIdx.data(); e.nj = sm.palette.size();
    e.sampleHash = SkinSampleHash(sm); e.fullHash = SkinFullHash(sm); e.lastFullHash = frame;
    e.count = (int)(d.size() / 8);
    if (e.count == 0) return nullptr;
    D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = (UINT)(d.size() * sizeof(float)); bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init; std::memset(&init, 0, sizeof(init)); init.pSysMem = d.data();
    if (FAILED(dev->CreateBuffer(&bd, &init, &e.buf))) { e.buf = nullptr; e.count = 0; return nullptr; }
    return &e;
}

bool D3D11Renderer::Impl::UploadBones(const SkinnedMesh& sm) {
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx->Map(bonesCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return false;
    float* o = static_cast<float*>(ms.pData);
    const std::size_t n = sm.palette.size() < (std::size_t)SkinnedMesh::kMaxGpuJoints ? sm.palette.size() : (std::size_t)SkinnedMesh::kMaxGpuJoints;
    for (std::size_t j = 0; j < n; ++j) {
        const Mat4& M = sm.palette[j];
        for (int r = 0; r < 3; ++r)   // row r: M(r,0..3)
            for (int c2 = 0; c2 < 4; ++c2) *o++ = M.at(c2, r);
    }
    ctx->Unmap(bonesCB, 0);
    return true;
}

D3D11Renderer::Impl::MeshVB* D3D11Renderer::Impl::EnsureMeshVB(const Mesh& mesh, std::uint64_t version, bool allowSlots) {
    MeshVB& e = meshVB[&mesh];
    e.lastUse = frame;
    bool same = e.vbuf != nullptr &&
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
    if (same && frame - e.lastFullHash >= 60) {     // exact check ~once a second
        std::uint64_t fh = MeshFullHash(mesh);
        e.lastFullHash = frame;
        if (fh != e.fullHash) same = false; else e.fullHash = fh;
    }
    if (same) return e.vertCount > 0 ? &e : nullptr;
    BuildExpandedVerts(mesh, allowSlots && version == 0);
    e.slotRanges = slotRanges;
    e.vp  = mesh.vertices.data();  e.vn  = mesh.vertices.size();
    e.tp  = mesh.triangles.data(); e.tn  = mesh.triangles.size();
    e.np  = mesh.normals.data();   e.nn  = mesh.normals.size();
    e.uvp = mesh.uvs.data();       e.uvn = mesh.uvs.size();
    e.fcp = mesh.triColors.data(); e.fcn = mesh.triColors.size();
    e.vertCount = (int)(verts.size() / 14);
    if (version) {
        // Skinned: rewrite a DYNAMIC buffer in place (re-created only to grow, or
        // when it was a static one). One upload per pose, shared by every pass.
        const bool wasStatic = e.vbuf && e.version == 0;
        e.version = version;
        if (e.vertCount == 0) return nullptr;
        if (e.vbuf && (wasStatic || e.vertCount > e.cap)) { e.vbuf->Release(); e.vbuf = nullptr; e.cap = 0; }
        if (!e.vbuf) {
            const int cap = e.vertCount + e.vertCount / 8;
            D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
            bd.ByteWidth = (UINT)((std::size_t)cap * 14 * sizeof(float)); bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(dev->CreateBuffer(&bd, nullptr, &e.vbuf))) { e.vbuf = nullptr; e.vertCount = 0; return nullptr; }
            e.cap = cap;
        }
        D3D11_MAPPED_SUBRESOURCE ms;
        if (FAILED(ctx->Map(e.vbuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { e.version = 0; e.vertCount = 0; return nullptr; }
        std::memcpy(ms.pData, verts.data(), verts.size() * sizeof(float));
        ctx->Unmap(e.vbuf, 0);
        return &e;
    }
    e.version = 0; e.cap = 0;
    e.sampleHash = sh ? sh : MeshSampleHash(mesh);
    e.fullHash = MeshFullHash(mesh);
    e.lastFullHash = frame;
    if (e.vbuf) { e.vbuf->Release(); e.vbuf = nullptr; }
    if (e.vertCount == 0) return nullptr;
    D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = (UINT)(verts.size() * sizeof(float));
    bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init; std::memset(&init, 0, sizeof(init));
    init.pSysMem = verts.data();
    if (FAILED(dev->CreateBuffer(&bd, &init, &e.vbuf))) { e.vbuf = nullptr; e.vertCount = 0; return nullptr; }
    return &e;
}

// A planned .dds texture (gputex::PlanUpload) as one Direct3D 11 texture with all its levels. BC1-BC5 are sampled by every
// Direct3D 11 GPU (feature level 10 and up), so the blocks go up as they are; DXGI numbers from the DXGI_FORMAT list.
static ID3D11ShaderResourceView* UploadDdsPlan(ID3D11Device* dev, const gputex::Plan& p) {
    if (p.levels.empty()) return nullptr;
    DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT blockBytes = 0;
    if (p.compressed) {
        switch (p.source) {
            case tex::DdsFormat::BC1: fmt = DXGI_FORMAT_BC1_UNORM; blockBytes = 8; break;
            case tex::DdsFormat::BC2: fmt = DXGI_FORMAT_BC2_UNORM; blockBytes = 16; break;
            case tex::DdsFormat::BC3: fmt = DXGI_FORMAT_BC3_UNORM; blockBytes = 16; break;
            case tex::DdsFormat::BC4: fmt = DXGI_FORMAT_BC4_UNORM; blockBytes = 8; break;
            case tex::DdsFormat::BC5: fmt = DXGI_FORMAT_BC5_UNORM; blockBytes = 16; break;
            default: return nullptr;
        }
    }
    D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof(td));
    td.Width = (UINT)p.levels[0].width; td.Height = (UINT)p.levels[0].height; td.MipLevels = (UINT)p.levels.size(); td.ArraySize = 1;
    td.Format = fmt; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> sr(p.levels.size());
    for (std::size_t l = 0; l < p.levels.size(); ++l) {
        std::memset(&sr[l], 0, sizeof(sr[l]));
        sr[l].pSysMem = p.levels[l].bytes.data();
        sr[l].SysMemPitch = p.compressed ? (UINT)((p.levels[l].width + 3) / 4) * blockBytes : (UINT)p.levels[l].width * 4;
    }
    ID3D11Texture2D* t = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&td, sr.data(), &t)) && t) {
        dev->CreateShaderResourceView(t, nullptr, &srv);
        t->Release();
    }
    return srv;
}

static int D3D11GpuTextureCategory() { static const int id = MemoryTracker::Global().Category("Textures (GPU)"); return id; }

void D3D11Renderer::Impl::FreeTexture(const std::string& name) {
    auto it = texCache.find(name);
    if (it != texCache.end()) { if (it->second) it->second->Release(); texCache.erase(it); }
    auto u = texUpload.find(name);
    if (u != texUpload.end()) {
        if (u->second.gpuBytes) MemoryTracker::Global().Remove(D3D11GpuTextureCategory(), std::int64_t(u->second.gpuBytes), 1);
        texUpload.erase(u);
    }
    texSampling.erase(name);
}

// The texture for a material texture name, made from the shared TextureAsset (one decode for every renderer) and made
// again only when the asset's serial changes (hot reload, reimport, a painted terrain splat).
ID3D11ShaderResourceView* D3D11Renderer::Impl::TextureFor(const std::string& name) {
    if (name.empty()) return nullptr;
    if (texGen != TextureCacheGeneration() || texLibGen != textures::Generation()) {
        std::vector<std::string> gone;   // names the texture library let go of
        for (const auto& kv : texCache) if (!textures::IsHeld(kv.first)) gone.push_back(kv.first);
        for (const std::string& n : gone) FreeTexture(n);
        texGen = TextureCacheGeneration();
        texLibGen = textures::Generation();
    }
    const TextureAsset* asset = textures::ForRender(name);
    const std::uint64_t serial = asset ? asset->serial : 0;
    auto it = texCache.find(name);
    if (it != texCache.end()) {
        auto u = texUpload.find(name);
        if ((u != texUpload.end() ? u->second.serial : 0) == serial) return it->second;
        FreeTexture(name);
    }
    ID3D11ShaderResourceView* srv = nullptr;
    TexUpload up;
    up.serial = serial;
    if (asset) {
        texSampling[name] = TextureSamplingOf(*asset);
        // A .dds file, or the compressed import copy of a picture set to `compress`: its own levels, BC blocks kept
        // compressed (a top level that is not a multiple of 4 is decoded on the CPU). Anything else: the RGBA8 picture.
        tex::DdsImage dds;
        gputex::Plan plan;
        gputex::Caps caps; caps.s3tc = caps.rgtc = true;
        if (gputex::Source(name, &dds, nullptr) && gputex::PlanUpload(std::move(dds), caps, &plan)) {
            srv = UploadDdsPlan(dev, plan);
            if (srv) for (const auto& lv : plan.levels) up.gpuBytes += lv.bytes.size();
        }
        if (srv) Log::Info("[d3d11] texture '", name, "': ", tex::DdsFormatName(plan.source), plan.compressed ? " kept compressed" : " uploaded as RGBA8", " (", plan.levels.size(), " levels)");
    }
    if (!srv && asset && asset->Valid()) {
        // Our own mip chain (gamma-correct box or Kaiser, renormalised normal maps), the same levels every renderer uses.
        std::vector<Image> built;
        const std::vector<Image>* levels = nullptr;
        if (!asset->settings.mipmaps) { built.push_back(asset->image); levels = &built; }
        else if (asset->MipsBuilt()) levels = &asset->Mips();
        else { built = BuildMipChain(asset->image, asset->settings); levels = &built; }
        D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof(td));
        td.Width = asset->Width(); td.Height = asset->Height(); td.MipLevels = (UINT)levels->size(); td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<D3D11_SUBRESOURCE_DATA> sr(levels->size());
        for (std::size_t l = 0; l < levels->size(); ++l) {
            std::memset(&sr[l], 0, sizeof(sr[l]));
            sr[l].pSysMem = (*levels)[l].Data(); sr[l].SysMemPitch = (UINT)(*levels)[l].Width() * 4;
            up.gpuBytes += (*levels)[l].Pixels().size();
        }
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(dev->CreateTexture2D(&td, sr.data(), &tex)) && tex) {
            dev->CreateShaderResourceView(tex, nullptr, &srv);
            tex->Release();
        }
        if (!srv) up.gpuBytes = 0;
    }
    if (!srv) up.gpuBytes = 0;
    if (up.gpuBytes) MemoryTracker::Global().Add(D3D11GpuTextureCategory(), std::int64_t(up.gpuBytes), 1);
    texUpload[name] = up;
    texCache[name] = srv;   // cache failures (nullptr) too
    return srv;
}

// The sampler for a picture: its own import Filter / Wrap (Point, Bilinear, Trilinear; Repeat, Clamp, Mirror),
// or the material's Pixel override. The default (Trilinear, Repeat) is the renderer's long-standing sampler.
ID3D11SamplerState* D3D11Renderer::Impl::SamplerFor(const std::string& name, bool pixel) {
    auto ti = texSampling.find(name);
    if (ti == texSampling.end()) ti = texSampling.emplace(name, TextureImportSampling(name)).first;
    const TextureSampling ts = ti->second;
    const bool point = pixel || ts.filter == TextureSampling::Point;
    const int aniso = (!point && ts.filter == TextureSampling::Trilinear) ? std::max(1, std::min(16, ts.anisotropy)) : 1;
    if (ts.wrap == TextureSampling::Repeat) {
        if (point && sampPoint) return sampPoint;
        if (!point && ts.filter == TextureSampling::Trilinear && aniso == 8) return samp;   // the long-standing default
    }
    const int key = ((point ? 3 : ts.filter) * 3 + ts.wrap) + aniso * 100;
    auto it = sampCombo.find(key);
    if (it != sampCombo.end()) return it->second ? it->second : samp;
    D3D11_SAMPLER_DESC sd; std::memset(&sd, 0, sizeof(sd));
    sd.Filter = point ? D3D11_FILTER_MIN_MAG_MIP_POINT
              : ts.filter == TextureSampling::Bilinear ? D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT
              : aniso > 1 ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.MaxAnisotropy = (UINT)aniso;
    const D3D11_TEXTURE_ADDRESS_MODE am = ts.wrap == TextureSampling::Clamp ? D3D11_TEXTURE_ADDRESS_CLAMP
                                        : ts.wrap == TextureSampling::Mirror ? D3D11_TEXTURE_ADDRESS_MIRROR
                                                                             : D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressU = sd.AddressV = sd.AddressW = am;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11SamplerState* made = nullptr;
    if (FAILED(dev->CreateSamplerState(&sd, &made))) made = nullptr;
    sampCombo[key] = made;
    return made ? made : samp;
}

namespace {
// Everything the host renderer (SDL's D3D11 backend caches its bindings) could
// notice us changing on the shared immediate context. Mirrors the state backup
// in Dear ImGui's DX11 backend, widened to the slots this renderer uses.
struct D3D11StateBackup {
    static constexpr int kSrv = 8, kSamp = 4, kCb = 2;
    UINT scissorN = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, viewportN = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_RECT scissor[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    D3D11_VIEWPORT viewport[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ID3D11RasterizerState* rs = nullptr;
    ID3D11BlendState* blend = nullptr; FLOAT blendFactor[4]; UINT sampleMask = 0;
    ID3D11DepthStencilState* ds = nullptr; UINT stencilRef = 0;
    ID3D11ShaderResourceView* psSrv[kSrv] = {};
    ID3D11SamplerState* psSamp[kSamp] = {};
    ID3D11PixelShader* ps = nullptr; ID3D11VertexShader* vs = nullptr; ID3D11GeometryShader* gs = nullptr;
    ID3D11ClassInstance* psInst[256]; ID3D11ClassInstance* vsInst[256]; ID3D11ClassInstance* gsInst[256];
    UINT psInstN = 256, vsInstN = 256, gsInstN = 256;
    ID3D11Buffer* vsCb[kCb] = {}; ID3D11Buffer* psCb[kCb] = {};
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN; UINT ibOff = 0;
    ID3D11Buffer* vb = nullptr; UINT vbStride = 0, vbOff = 0;
    ID3D11InputLayout* layout = nullptr;
    ID3D11RenderTargetView* rtv = nullptr; ID3D11DepthStencilView* dsv = nullptr;

    void Save(ID3D11DeviceContext* c) {
        c->RSGetScissorRects(&scissorN, scissor);
        c->RSGetViewports(&viewportN, viewport);
        c->RSGetState(&rs);
        c->OMGetBlendState(&blend, blendFactor, &sampleMask);
        c->OMGetDepthStencilState(&ds, &stencilRef);
        c->PSGetShaderResources(0, kSrv, psSrv);
        c->PSGetSamplers(0, kSamp, psSamp);
        c->PSGetShader(&ps, psInst, &psInstN);
        c->VSGetShader(&vs, vsInst, &vsInstN);
        c->GSGetShader(&gs, gsInst, &gsInstN);
        c->VSGetConstantBuffers(0, kCb, vsCb);
        c->PSGetConstantBuffers(0, kCb, psCb);
        c->IAGetPrimitiveTopology(&topo);
        c->IAGetIndexBuffer(&ib, &ibFmt, &ibOff);
        c->IAGetVertexBuffers(0, 1, &vb, &vbStride, &vbOff);
        c->IAGetInputLayout(&layout);
        c->OMGetRenderTargets(1, &rtv, &dsv);
    }
    void Restore(ID3D11DeviceContext* c) {
        c->RSSetScissorRects(scissorN, scissor);
        c->RSSetViewports(viewportN, viewport);
        c->RSSetState(rs); if (rs) rs->Release();
        c->OMSetBlendState(blend, blendFactor, sampleMask); if (blend) blend->Release();
        c->OMSetDepthStencilState(ds, stencilRef); if (ds) ds->Release();
        c->PSSetShaderResources(0, kSrv, psSrv); for (auto* v : psSrv) if (v) v->Release();
        c->PSSetSamplers(0, kSamp, psSamp); for (auto* v : psSamp) if (v) v->Release();
        c->PSSetShader(ps, psInst, psInstN); if (ps) ps->Release();
        for (UINT i = 0; i < psInstN; ++i) if (psInst[i]) psInst[i]->Release();
        c->VSSetShader(vs, vsInst, vsInstN); if (vs) vs->Release();
        for (UINT i = 0; i < vsInstN; ++i) if (vsInst[i]) vsInst[i]->Release();
        c->GSSetShader(gs, gsInst, gsInstN); if (gs) gs->Release();
        for (UINT i = 0; i < gsInstN; ++i) if (gsInst[i]) gsInst[i]->Release();
        c->VSSetConstantBuffers(0, kCb, vsCb); for (auto* v : vsCb) if (v) v->Release();
        c->PSSetConstantBuffers(0, kCb, psCb); for (auto* v : psCb) if (v) v->Release();
        c->IASetPrimitiveTopology(topo);
        c->IASetIndexBuffer(ib, ibFmt, ibOff); if (ib) ib->Release();
        c->IASetVertexBuffers(0, 1, &vb, &vbStride, &vbOff); if (vb) vb->Release();
        c->IASetInputLayout(layout); if (layout) layout->Release();
        c->OMSetRenderTargets(1, &rtv, dsv); if (rtv) rtv->Release(); if (dsv) dsv->Release();
    }
};
} // namespace

const std::uint32_t* D3D11Renderer::RenderToPixels(const Scene& scene, const Mat4& vp, const Vec3& eye,
                                                   int w, int h, int samples,
                                                   float clearR, float clearG, float clearB, float clearA,
                                                   const GameObject* ignore) {
    if (!Available() || w < 1 || h < 1) return nullptr;
    Impl* p = m_impl;
    if (p->shared && !p->stateSaved) {   // on the host's device: leave its pipeline state as we found it
        D3D11StateBackup* backup = new D3D11StateBackup();
        backup->Save(p->ctx);
        p->stateSaved = true;
        const std::uint32_t* r = RenderToPixels(scene, vp, eye, w, h, samples, clearR, clearG, clearB, clearA, ignore);
        p->stateSaved = false;
        backup->Restore(p->ctx);
        delete backup;
        return r;
    }
    // The scene's post chain wants a float target and linear colour out of the mesh
    // shader; with everything off the frame takes the plain RGBA8 path as before.
    const auto& rsTop = scene.renderSettings;
    const bool chain = PostActive(rsTop) && p->EnsurePost();
    const bool hdr = chain && rsTop.tonemap && !p->hdrUnsupported;
    p->wantFmt = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : (p->directDst ? p->directFmt : DXGI_FORMAT_R8G8B8A8_UNORM);
    if (hdr && !p->EnsureTargets(w, h, samples)) {   // no float targets here: stay on RGBA8 for good
        p->hdrUnsupported = true;
        p->wantFmt = p->directDst ? p->directFmt : DXGI_FORMAT_R8G8B8A8_UNORM;
        Log::Warning("[d3d11] float render target unavailable; post-processing runs on RGBA8");
    }
    if (!p->EnsureTargets(w, h, samples)) return nullptr;
    const bool hdrOut = chain && p->fmt == DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (!p->rtv || !p->dsv || !p->ctx) return nullptr;   // never bind a null target
    ID3D11DeviceContext* c = p->ctx;

    // Advance the mesh-VB cache clock and evict buffers unused for ~5s of frames
    // (deleted objects, replaced meshes), so GPU memory doesn't grow unbounded.
    ++p->frame;
    if ((p->frame % 300u) == 0u) {
        for (auto it = p->meshVB.begin(); it != p->meshVB.end();) {
            if (p->frame - it->second.lastUse > 300u) {
                if (it->second.vbuf) it->second.vbuf->Release();
                it = p->meshVB.erase(it);
            } else ++it;
        }
        for (auto it = p->skinVB.begin(); it != p->skinVB.end();) {
            if (p->frame - it->second.lastUse > 300u) {
                if (it->second.buf) it->second.buf->Release();
                it = p->skinVB.erase(it);
            } else ++it;
        }
    }

    visibility::Build(scene, vp, eye, w, h, visibility::RenderViewFovY(), ignore);

    // ---- Shadow-map depth pre-passes -------------------------------------------
    // Sun: one ortho cascade per tile of the sun atlas. Local lights: one
    // perspective tile per spot light, six per point/area light (cube faces).
    // PlanShadows (ShadowSetup.hpp) decides the cascades and tile assignment.
    ShadowPlan plan;
    bool shadowOn = false, localOn = false;
    // `forView`: the ambient-occlusion depth pass reuses this loop with the CAMERA's
    // view-projection; it draws every opaque mesh, not only the shadow casters.
    auto drawCasters = [&](const Mat4& lightVP, const LightSample* local, bool forView = false) {
        const Frustum lightFr = Frustum::FromVP(lightVP);   // only casters inside the light's view
        for (const auto& up : scene.Objects()) {
            GameObject* go = up.get();
            if (!go || !go->ActiveInHierarchy() || go->firstPersonOnly || go->HiddenFromCamera(ignore)) continue;
            auto* mr = go->GetComponent<MeshRenderer>();
            if (!mr || mr->wireframe || !mr->enabled || (!forView && !mr->castShadows)) continue;
            if (mr->color.a < 0.999f) continue;     // transparent meshes don't cast / occlude
            const Mesh* lodMesh = visibility::Current().ShadowMesh(go, mr);
            if (!lodMesh) continue;
            const Mesh& mesh = *lodMesh;
            const auto& V = mesh.vertices; const auto& T = mesh.triangles;
            if (V.empty() || T.size() < 3) continue;
            if (CulledByFrustum(lightFr, go, mr)) continue;
            if (local) {
                Vec3 bc; float br;
                if (ObjectWorldSphere(go, mr, bc, br) && !CasterInLightRange(*local, bc, br)) continue;
            }
            Mat4 dmvp = lightVP * go->transform->LocalToWorldMatrix();
            CB dcb; std::memset(&dcb, 0, sizeof(dcb));
            std::memcpy(dcb.mvp, dmvp.m, sizeof(dcb.mvp));
            D3D11_MAPPED_SUBRESOURCE dms;
            if (SUCCEEDED(c->Map(p->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &dms))) {
                std::memcpy(dms.pData, &dcb, sizeof(dcb)); c->Unmap(p->cb, 0);
            }
            // Static meshes: draw positions straight from the cached interleaved
            // buffer (stride 14). Per-frame-deformed meshes stream position-only.
            const bool deformed = go->GetComponent<Character>() != nullptr;
            const auto* skinD = go->GetComponent<SkinnedMesh>();
            if (!deformed && p->CanGpuSkin(skinD) && p->vsDepthSkin && p->layoutDepthSkin) {
                Impl::MeshVB* mv = p->EnsureMeshVB(skinD->bind, 0, false);
                Impl::SkinVB* sv = mv ? p->EnsureSkinVB(*skinD) : nullptr;
                if (!mv || !sv || sv->count != mv->vertCount || !p->UploadBones(*skinD)) continue;
                c->IASetInputLayout(p->layoutDepthSkin);
                c->VSSetShader(p->vsDepthSkin, nullptr, 0);
                c->VSSetConstantBuffers(1, 1, &p->bonesCB);
                ID3D11Buffer* bufs[2] = {mv->vbuf, sv->buf};
                UINT strides[2] = {14 * sizeof(float), 8 * sizeof(float)}, offs[2] = {0, 0};
                c->IASetVertexBuffers(0, 2, bufs, strides, offs);
                c->Draw((UINT)mv->vertCount, 0);
                c->IASetInputLayout(p->layoutDepth);
                c->VSSetShader(p->vsDepth, nullptr, 0);
                continue;
            }
            if (!deformed) {
                const auto* skin = skinD;
                Impl::MeshVB* mv = p->EnsureMeshVB(mesh, skin ? skin->version : 0);
                if (!mv) continue;
                UINT ds = 14 * sizeof(float), doff = 0;
                c->IASetVertexBuffers(0, 1, &mv->vbuf, &ds, &doff);
                c->Draw((UINT)mv->vertCount, 0);
                continue;
            }
            const int nv = (int)V.size();
            p->verts.clear(); p->verts.reserve(T.size() * 3);
            for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
                int a = T[i], b = T[i + 1], cc = T[i + 2];
                if (a < 0 || b < 0 || cc < 0 || a >= nv || b >= nv || cc >= nv) continue;
                const Vec3& pa = V[a]; const Vec3& pb = V[b]; const Vec3& pc = V[cc];
                p->verts.push_back(pa.x); p->verts.push_back(pa.y); p->verts.push_back(pa.z);
                p->verts.push_back(pb.x); p->verts.push_back(pb.y); p->verts.push_back(pb.z);
                p->verts.push_back(pc.x); p->verts.push_back(pc.y); p->verts.push_back(pc.z);
            }
            const int dvc = (int)(p->verts.size() / 3);
            if (dvc == 0) continue;
            if (dvc > p->vbDepthCap) {
                if (p->vbDepth) { p->vbDepth->Release(); p->vbDepth = nullptr; p->vbDepthCap = 0; }
                const int cap = dvc + 1024;
                D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
                bd.ByteWidth = (UINT)(cap * 3 * sizeof(float)); bd.Usage = D3D11_USAGE_DYNAMIC;
                bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                if (FAILED(p->dev->CreateBuffer(&bd, nullptr, &p->vbDepth))) continue;
                p->vbDepthCap = cap;
            }
            if (!p->vbDepth) continue;
            if (SUCCEEDED(c->Map(p->vbDepth, 0, D3D11_MAP_WRITE_DISCARD, 0, &dms))) {
                std::memcpy(dms.pData, p->verts.data(), p->verts.size() * sizeof(float));
                c->Unmap(p->vbDepth, 0);
            }
            UINT ds = 3 * sizeof(float), doff = 0;
            c->IASetVertexBuffers(0, 1, &p->vbDepth, &ds, &doff);
            c->Draw((UINT)dvc, 0);
        }
    };
    if (ShadowsEnabled() && p->vsDepth && p->layoutDepth) {
        PlanShadows(scene, vp, eye, plan, 8192);   // D3D11 feature level 10+: 8192 texels
        ID3D11ShaderResourceView* nullsrv[2] = {nullptr, nullptr};
        c->PSSetShaderResources(4, 2, nullsrv);    // never bound as SRV + DSV at once
        auto beginPass = [&](ID3D11DepthStencilView* dsv) {
            c->OMSetRenderTargets(0, nullptr, dsv);
            c->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
            c->IASetInputLayout(p->layoutDepth);
            c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            c->VSSetShader(p->vsDepth, nullptr, 0);
            c->PSSetShader(nullptr, nullptr, 0);        // depth only — no pixel shader
            c->VSSetConstantBuffers(0, 1, &p->cb);
            c->RSSetState(p->raster);
            c->OMSetDepthStencilState(p->depth, 0);
            const float bf0[4] = {0, 0, 0, 0};
            c->OMSetBlendState(nullptr, bf0, 0xffffffff);
        };
        auto tileViewport = [&](int x, int y, int size) {
            D3D11_VIEWPORT sv; std::memset(&sv, 0, sizeof(sv));
            sv.TopLeftX = (FLOAT)x; sv.TopLeftY = (FLOAT)y;
            sv.Width = (FLOAT)size; sv.Height = (FLOAT)size; sv.MaxDepth = 1.0f;
            c->RSSetViewports(1, &sv);
        };
        const int S = plan.cascadeSize;
        if (plan.cascades > 0 && p->EnsureDepthTarget(p->sunAtlas, S * plan.cascades, S)) {
            beginPass(p->sunAtlas.dsv);
            for (int ci = 0; ci < plan.cascades; ++ci) {
                tileViewport(ci * S, 0, S);
                drawCasters(plan.cascade[ci].vp, nullptr);
            }
            shadowOn = true;
        }
        const int T = plan.tileSize, G = ShadowPlan::kLocalGrid;
        if (!plan.local.empty() && p->EnsureDepthTarget(p->localAtlas, T * G, T * G)) {
            beginPass(p->localAtlas.dsv);
            const auto& L = SceneLights::List();
            for (const LocalShadowGpu& ls : plan.local)
                for (int f = 0; f < ls.tiles; ++f) {
                    int t = ls.firstTile + f;
                    tileViewport((t % G) * T, (t / G) * T, T);
                    drawCasters(ls.vp[f], &L[ls.light]);
                }
            localOn = true;
        }
    }

    D3D11_VIEWPORT vpd; std::memset(&vpd, 0, sizeof(vpd));
    vpd.Width = (FLOAT)w; vpd.Height = (FLOAT)h; vpd.MaxDepth = 1.0f;
    c->RSSetViewports(1, &vpd);
    c->OMSetRenderTargets(1, &p->rtv, p->dsv);
    const float clear[4] = {clearR, clearG, clearB, clearA};
    c->ClearRenderTargetView(p->rtv, clear);
    c->ClearDepthStencilView(p->dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

    // Sky + post constants, filled per pass from here on.
    PostCB pc; std::memset(&pc, 0, sizeof(pc));
    auto uploadPc = [&]() {
        D3D11_MAPPED_SUBRESOURCE pm;
        if (p->postCB && SUCCEEDED(c->Map(p->postCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &pm))) {
            std::memcpy(pm.pData, &pc, sizeof(pc)); c->Unmap(p->postCB, 0);
        }
    };
    // A fullscreen triangle with the quad vertex shader (no vertex buffer).
    auto beginQuadPasses = [&]() {
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(p->vsQuad, nullptr, 0);
        c->RSSetState(p->raster);
        c->OMSetDepthStencilState(p->depthOff, 0);
        const float bf[4] = {0, 0, 0, 0};
        c->OMSetBlendState(nullptr, bf, 0xffffffff);
        c->PSSetSamplers(2, 1, &p->sampClamp);
        c->PSSetConstantBuffers(3, 1, &p->postCB);
    };
    // Procedural sky: a fullscreen pass behind everything (depth off), coloured
    // by the sun's height, with the disc, haze, stars and clouds.
    const bool procedural = rsTop.skyMode == Scene::RenderSettings::SkyProcedural;
    if (rsTop.skybox && SkyPassEnabled() && (procedural || chain) && p->EnsurePost()) {
        const auto now = std::chrono::steady_clock::now();
        if (!p->clockSet) { p->clock0 = now; p->clockSet = true; }
        const float t = std::chrono::duration<float>(now - p->clock0).count();
        const float sdt = t - p->skyTime; p->skyTime = t;
        const Vec3 wind = WindVector();
        p->cloudOff[0] += (0.012f * rsTop.cloudSpeed + wind.x * 0.004f) * sdt;
        p->cloudOff[1] += (0.005f * rsTop.cloudSpeed + wind.z * 0.004f) * sdt;
        const Vec3 sun = SunDirection();
        const SkyPalette pal = SkyPaletteFor(sun.y);
        const Mat4 inv = vp.Inverse();
        std::memcpy(pc.invVP, inv.m, sizeof(pc.invVP));
        pc.eye[0] = eye.x; pc.eye[1] = eye.y; pc.eye[2] = eye.z; pc.sunSize = rsTop.skySunDisc * 0.0174532925f;
        pc.sunDir[0] = sun.x; pc.sunDir[1] = sun.y; pc.sunDir[2] = sun.z; pc.sunHalo = rsTop.skySunHalo;
        pc.zenith[0] = pal.zenith.x; pc.zenith[1] = pal.zenith.y; pc.zenith[2] = pal.zenith.z; pc.sunDisc = pal.sunDisc;
        pc.horizon[0] = pal.horizon.x; pc.horizon[1] = pal.horizon.y; pc.horizon[2] = pal.horizon.z; pc.haze = rsTop.skyHaze;
        pc.ground[0] = pal.ground.x; pc.ground[1] = pal.ground.y; pc.ground[2] = pal.ground.z; pc.night = pal.night;
        pc.sunColor[0] = pal.sun.x; pc.sunColor[1] = pal.sun.y; pc.sunColor[2] = pal.sun.z; pc.starDensity = rsTop.skyStarDensity;
        pc.cloud[0] = rsTop.cloudCover; pc.cloud[1] = rsTop.cloudDensity; pc.cloud[2] = rsTop.cloudScale; pc.cloud[3] = rsTop.skyStarBright;
        pc.cloudOff[0] = p->cloudOff[0]; pc.cloudOff[1] = p->cloudOff[1]; pc.time = t; pc.lin = hdrOut ? 1.0f : 0.0f;
        pc.mode = procedural ? 1.0f : 0.0f;
        pc.top[0] = rsTop.skyTop.r; pc.top[1] = rsTop.skyTop.g; pc.top[2] = rsTop.skyTop.b;
        pc.hor[0] = rsTop.skyHorizon.r; pc.hor[1] = rsTop.skyHorizon.g; pc.hor[2] = rsTop.skyHorizon.b; pc.horizonPos = rsTop.skyHorizonPos;
        pc.bot[0] = rsTop.skyBottom.r; pc.bot[1] = rsTop.skyBottom.g; pc.bot[2] = rsTop.skyBottom.b; pc.starsOn = rsTop.skyStars ? 1.0f : 0.0f;
        pc.sunPos[0] = rsTop.skySunX; pc.sunPos[1] = rsTop.skySunY; pc.sunRad = rsTop.skySunSize; pc.sunOn = rsTop.skySun ? 1.0f : 0.0f;
        pc.viewSize[0] = (float)w; pc.viewSize[1] = (float)h;
        if (!procedural) { pc.sunColor[0] = rsTop.skySunColor.r; pc.sunColor[1] = rsTop.skySunColor.g; pc.sunColor[2] = rsTop.skySunColor.b; }
        uploadPc();
        beginQuadPasses();
        c->PSSetShader(p->psSky, nullptr, 0);
        c->Draw(3, 0);
    }

    c->IASetInputLayout(p->layout);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(p->vs, nullptr, 0);
    c->PSSetShader(p->ps, nullptr, 0);
    c->VSSetConstantBuffers(0, 1, &p->cb);
    c->PSSetConstantBuffers(0, 1, &p->cb);
    c->PSSetSamplers(0, 1, &p->samp);
    c->RSSetState(p->raster);
    c->OMSetDepthStencilState(p->depth, 0);

    Vec3 toLight = (SceneLight::Direction() * -1.0f).Normalized();
    // Gather all scene lights once per frame (directional + point + spot) into the
    // packed cbuffer array so every light shades on the GPU, not just the sun.
    float frameLights[384]; std::memset(frameLights, 0, sizeof(frameLights));
    int frameLightCount = 0;
    {
        const auto& LS = SceneLights::List();
        int n = (int)LS.size(); if (n > 16) n = 16; frameLightCount = n;
        for (int i = 0; i < n; ++i) {
            const LightSample& s = LS[i]; Vec3 d = s.dir.Normalized(); int b = i * 24;
            frameLights[b+0] = d.x; frameLights[b+1] = d.y; frameLights[b+2] = d.z; frameLights[b+3] = (float)s.type;
            frameLights[b+4] = s.pos.x; frameLights[b+5] = s.pos.y; frameLights[b+6] = s.pos.z; frameLights[b+7] = s.range;
            frameLights[b+8] = s.color.x; frameLights[b+9] = s.color.y; frameLights[b+10] = s.color.z; frameLights[b+11] = s.cosOuter;
            frameLights[b+12] = s.cosInner; frameLights[b+13] = (float)s.falloff;
            frameLights[b+14] = s.halfW; frameLights[b+15] = s.halfH;                                    // Area light panel (0 = soft glow)
            frameLights[b+16] = s.right.x; frameLights[b+17] = s.right.y; frameLights[b+18] = s.right.z;
            frameLights[b+19] = (s.cookie >= 0 && s.cookie < kCookieRows) ? (float)s.cookie : -1.0f;      // mask row, -1 = none
            frameLights[b+20] = s.up.x; frameLights[b+21] = s.up.y; frameLights[b+22] = s.up.z; frameLights[b+23] = s.cookieScale;
        }
    }
    Vec3 d3amb = SceneLights::AmbientColor();
    if (d3amb.x + d3amb.y + d3amb.z < 1e-4f) d3amb = Vec3{0.35f, 0.36f, 0.40f};
    // Per-frame shadow + ambient constants (b2) and the two depth atlases (t4, t5).
    {
        ShadowCB sh; std::memset(&sh, 0, sizeof(sh));
        for (int ci = 0; ci < plan.cascades; ++ci) {
            std::memcpy(&sh.casVP[ci * 16], plan.cascade[ci].vp.m, sizeof(float) * 16);
            sh.casSphere[ci * 4 + 0] = plan.cascade[ci].center.x; sh.casSphere[ci * 4 + 1] = plan.cascade[ci].center.y;
            sh.casSphere[ci * 4 + 2] = plan.cascade[ci].center.z; sh.casSphere[ci * 4 + 3] = plan.cascade[ci].radius;
            sh.casInfo[ci * 4] = plan.cascade[ci].texelWorld;
            sh.casInfo[ci * 4 + 1] = plan.cascade[ci].depthRange;
        }
        const LightSample* sun = SceneLights::Sun();
        sh.shadow[0] = shadowOn ? 1.0f : 0.0f; sh.shadow[1] = (float)plan.cascades;
        sh.shadow[2] = (float)(plan.cascadeSize > 0 ? plan.cascadeSize : 1);
        sh.shadow[3] = ShadowSoftness() < 0.0f ? 0.0f : ShadowSoftness();
        sh.shadow2[0] = (float)plan.sunLight;
        sh.shadow2[1] = sun ? sun->shadowStrength : 1.0f;
        sh.shadow2[2] = sun ? sun->shadowBias : 1.0f;
        sh.shadow2[3] = (float)(plan.tileSize > 0 ? plan.tileSize : 1);
        sh.sunExtra[0] = sun ? sun->shadowSoftness : 0.5f;
        sh.sunExtra[1] = ShadowDistance() > 0.0f ? ShadowDistance() : 0.0f;
        sh.sunExtra[2] = (float)kCookieRows;
        Vec3 aSky, aGround;
        SceneAmbientColors(scene.renderSettings, d3amb, aSky, aGround);   // the scene's Ambient Mode
        sh.ambSky[0] = aSky.x; sh.ambSky[1] = aSky.y; sh.ambSky[2] = aSky.z;
        sh.ambGround[0] = aGround.x; sh.ambGround[1] = aGround.y; sh.ambGround[2] = aGround.z;
        for (int i = 0; i < 16; ++i) sh.lshadow[i * 4] = -1.0f;
        if (localOn) {
            const auto& L = SceneLights::List();
            for (const LocalShadowGpu& ls : plan.local) {
                if (ls.light < 0 || ls.light >= 16) continue;
                sh.lshadow[ls.light * 4 + 0] = (float)ls.firstTile;
                sh.lshadow[ls.light * 4 + 1] = L[ls.light].shadowStrength;
                sh.lshadow[ls.light * 4 + 2] = L[ls.light].shadowBias;
                sh.lshadow[ls.light * 4 + 3] = ls.texelAt1;
                for (int f = 0; f < ls.tiles; ++f)
                    std::memcpy(&sh.localVP[(ls.firstTile + f) * 16], ls.vp[f].m, sizeof(float) * 16);
            }
        }
        D3D11_MAPPED_SUBRESOURCE shm;
        if (p->shCB && SUCCEEDED(c->Map(p->shCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &shm))) {
            std::memcpy(shm.pData, &sh, sizeof(sh)); c->Unmap(p->shCB, 0);
        }
        if (p->shCB) c->PSSetConstantBuffers(2, 1, &p->shCB);
        ID3D11ShaderResourceView* atl[2] = {shadowOn ? p->sunAtlas.srv : nullptr, localOn ? p->localAtlas.srv : nullptr};
        c->PSSetShaderResources(4, 2, atl);   // sun cascades on t4, local tiles on t5
        c->PSSetSamplers(1, 1, &p->sampShadow);
        {   // light masks on t8 (only when a light uses one)
            bool any = false;
            for (const LightSample& ls : SceneLights::List()) if (ls.cookie >= 0 && ls.cookie < kCookieRows) any = true;
            if (any && (!p->cookieSrv || p->cookieCount != LightCookies::Store().size())) {
                BuildCookieAtlas(p->dev, p->cookieTex, p->cookieSrv);
                p->cookieCount = LightCookies::Store().size();
            }
            ID3D11ShaderResourceView* cs = any ? p->cookieSrv : nullptr;
            c->PSSetShaderResources(8, 1, &cs);
        }
    }
    // Flat ground contact shadows stand in for real shadows only when the sun's
    // shadow map isn't drawn (Shadows off / unsupported) but the sun does cast.
    const LightSample* sunL = SceneLights::Sun();
    const bool sunCasts = !sunL || (sunL->castShadows && sunL->shadowStrength > 0.0f);
    const bool mapsWork = ShadowsEnabled() && p->vsDepth && p->layoutDepth && (plan.cascades == 0 || shadowOn);
    const bool planarShadows = sunCasts && !mapsWork;
    // Refresh the env sky (for reflective/metal materials) from the scene, since the
    // software RenderMeshes that normally does this may not run on the GPU path.
    {
        const auto& rs = scene.renderSettings;
        EnvSky().enabled = rs.skybox;
        EffectiveSkyColors(rs, EnvSky().top, EnvSky().horizon, EnvSky().bottom);   // gradient, or the sun's palette
    }

    // Skip what the camera can't see (frustum, occlusion, LOD) and merge safe batches.
    const visibility::Frame& vis = visibility::Current();
    int drawnN = 0;
    for (const visibility::Item& it : vis.items) {
        GameObject* go = it.go;
        auto* mr = it.mr;
        const Mesh& mesh = *it.mesh;
        const auto& V = mesh.vertices; const auto& T = mesh.triangles;
        if (V.empty() || T.size() < 3) continue;
        ++drawnN;
        ID3D11ShaderResourceView* srv = p->TextureFor(mr->texture);
        // Custom (.okayshader) surface: its own pixel shader, or the magenta error material.
        Impl::CustomPS* cust = nullptr;
        bool custBad = false;
        if (mr->shader == MeshRenderer::Shader::Custom) {
            cust = p->EnsureCustomPS(mr->customShader);
            custBad = cust == nullptr;
            if (custBad) srv = nullptr;
        }

        // Geometry source: static meshes draw from the per-mesh cached GPU buffer
        // (expanded + uploaded once — see Impl::EnsureMeshVB); meshes deformed on
        // the CPU every frame by a Character stream through `vb`. SkinnedMesh is
        // cached too, re-uploaded only when its version moves.
        const bool deformed = !it.batched && go->GetComponent<Character>() != nullptr;
        const auto* skin = it.batched ? nullptr : go->GetComponent<SkinnedMesh>();
        Impl::MeshVB* mvb = nullptr;
        Impl::SkinVB* svb = nullptr;
        int vcount = 0;
        D3D11_MAPPED_SUBRESOURCE ms;
        if (!deformed && p->CanGpuSkin(skin)) {
            // GPU skinning: the bind pose is uploaded once; only the bones change.
            mvb = p->EnsureMeshVB(skin->bind, 0, false);
            svb = mvb ? p->EnsureSkinVB(*skin) : nullptr;
            if (!mvb) continue;
            if (!svb || svb->count != mvb->vertCount || !p->UploadBones(*skin)) svb = nullptr;
            vcount = mvb->vertCount;
            if (!svb) mvb = p->EnsureMeshVB(mesh, skin->version);   // fall back: draw whatever mesh holds
            if (!mvb) continue;
        } else if (!deformed) {
            mvb = p->EnsureMeshVB(mesh, skin ? skin->version : 0);
            if (!mvb) continue;                    // no drawable triangles
            vcount = mvb->vertCount;
        } else {
            p->BuildExpandedVerts(mesh);
            vcount = (int)(p->verts.size() / 14);
            if (vcount == 0) continue;
            // Grow / upload the dynamic vertex buffer.
            if (vcount > p->vbCap) {
                if (p->vb) { p->vb->Release(); p->vb = nullptr; p->vbCap = 0; }
                const int cap = vcount + 1024;
                D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
                bd.ByteWidth = (UINT)(cap * 14 * sizeof(float)); bd.Usage = D3D11_USAGE_DYNAMIC;
                bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                if (FAILED(p->dev->CreateBuffer(&bd, nullptr, &p->vb))) continue;
                p->vbCap = cap;
            }
            if (!p->vb) continue;  // never Map a null buffer (a prior CreateBuffer may have failed)
            if (SUCCEEDED(c->Map(p->vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                std::memcpy(ms.pData, p->verts.data(), p->verts.size() * sizeof(float));
                c->Unmap(p->vb, 0);
            }
        }

        Mat4 model = it.model;
        Mat4 mvp = vp * model;
        bool unlit = cust ? cust->unlit : (custBad || mr->unlit || mr->shader == MeshRenderer::Shader::Unlit);
        CB cb; std::memset(&cb, 0, sizeof(cb));
        std::memcpy(cb.mvp, mvp.m, sizeof(cb.mvp));
        std::memcpy(cb.model, model.m, sizeof(cb.model));
        float tintR = 1, tintG = 1, tintB = 1;
        if (it.lod >= 0) if (auto* lg = go->GetComponent<LODGroup>(); lg) lg->DebugTint(it.lod, tintR, tintG, tintB);
        cb.color[0] = mr->color.r * tintR; cb.color[1] = mr->color.g * tintG; cb.color[2] = mr->color.b * tintB;
        cb.specular = unlit ? 0.0f : mr->specular;
        cb.emissive[0] = mr->emissive.r; cb.emissive[1] = mr->emissive.g; cb.emissive[2] = mr->emissive.b;
        cb.shininess = mr->shininess;
        cb.lightDir[0] = toLight.x; cb.lightDir[1] = toLight.y; cb.lightDir[2] = toLight.z;
        cb.unlit = unlit ? 1.0f : 0.0f;
        cb.lightColor[0] = cb.lightColor[1] = cb.lightColor[2] = 0.85f;
        cb.useTex = srv ? 1.0f : 0.0f;
        cb.ambient[0] = d3amb.x; cb.ambient[1] = d3amb.y; cb.ambient[2] = d3amb.z;
        cb.eye[0] = eye.x; cb.eye[1] = eye.y; cb.eye[2] = eye.z;
        ID3D11ShaderResourceView* nsrv = mr->normalMap.empty() ? nullptr : p->TextureFor(mr->normalMap);
        cb.hasNormal = nsrv ? 1.0f : 0.0f;
        cb.normalStrength = mr->normalStrength;
        ID3D11ShaderResourceView* ssrv = mr->specularMap.empty() ? nullptr : p->TextureFor(mr->specularMap);
        ID3D11ShaderResourceView* aosrv = mr->aoMap.empty() ? nullptr : p->TextureFor(mr->aoMap);
        bool d3unlit = unlit;
        cb.pbr[0] = d3unlit ? 0.0f : mr->metallic;       // metallic
        cb.pbr[1] = d3unlit ? 0.0f : mr->reflectivity;   // reflectivity
        cb.pbr[2] = ssrv ? 1.0f : 0.0f;                  // hasSpecMap
        cb.pbr[3] = aosrv ? 1.0f : 0.0f;                 // hasAo
        cb.pbr2[0] = mr->aoStrength;                      // aoStrength
        cb.pbr2[1] = EnvSky().enabled ? 1.0f : 0.0f;     // envOn
        cb.pbr2[2] = mr->texOffset.x; cb.pbr2[3] = mr->texOffset.y;
        { const EnvSkyData& sky = EnvSky();
          cb.sky[0] = sky.top.x;     cb.sky[1] = sky.top.y;     cb.sky[2] = sky.top.z;
          cb.sky[4] = sky.horizon.x; cb.sky[5] = sky.horizon.y; cb.sky[6] = sky.horizon.z;
          cb.sky[8] = sky.bottom.x;  cb.sky[9] = sky.bottom.y;  cb.sky[10] = sky.bottom.z; }
        cb.tiling[0] = srv ? mr->tiling.x : 1.0f; cb.tiling[1] = srv ? mr->tiling.y : 1.0f;
        cb.shaderMode = (cust || custBad) ? 0.0f : (float)(int)mr->shader;
        if (cust) { cb.tiling[0] = mr->tiling.x; cb.tiling[1] = mr->tiling.y; }   // a custom surface's uv is tiled with or without a picture
        if (custBad) {
            cb.color[0] = oshader::kErrorColor[0]; cb.color[1] = oshader::kErrorColor[1]; cb.color[2] = oshader::kErrorColor[2];
            cb.emissive[0] = cb.emissive[1] = cb.emissive[2] = 0.0f;
        }
        cb.toonBands = (float)mr->toonBands;
        cb.rimStr = mr->rimStrength;
        cb.rimColor[0] = mr->rimColor.r; cb.rimColor[1] = mr->rimColor.g; cb.rimColor[2] = mr->rimColor.b;
        cb.gradTop[0] = mr->gradientTop.r; cb.gradTop[1] = mr->gradientTop.g; cb.gradTop[2] = mr->gradientTop.b;
        cb.gradBot[0] = mr->gradientBottom.r; cb.gradBot[1] = mr->gradientBottom.g; cb.gradBot[2] = mr->gradientBottom.b;
        cb.shadowAlpha = -1.0f;   // lit pass (>=0 would draw the flat shadow — must be off here)
        cb.alpha = custBad ? 1.0f : mr->color.a;   // material opacity (1 = opaque); < 1 alpha-blends (water/glass)
        cb.lightCount[0] = (float)frameLightCount;
        cb.lightCount[1] = mr->metallicRoughnessMap.empty() ? 0.0f : 1.0f;
        cb.lightCount[2] = mr->emissiveMap.empty() ? 0.0f : 1.0f;
        std::memcpy(cb.lights, frameLights, sizeof(frameLights));
        {
            const auto& rs = scene.renderSettings;
            const Vec3 fc = EffectiveFogColor(rs);
            cb.fog[0] = FogActive(rs) ? 1.0f : 0.0f; cb.fog[1] = rs.fogStart; cb.fog[2] = rs.fogEnd;
            cb.fog[3] = mr->receiveShadows ? 1.0f : 0.0f;   // receive shadows
            // fogCol.w carries the filmic (linear + ACES + gamma) pipeline flag.
            cb.fogCol[0] = fc.x; cb.fogCol[1] = fc.y; cb.fogCol[2] = fc.z;
            cb.fogCol[3] = rs.tonemap ? 1.0f : 0.0f;
            cb.fog2[0] = (float)rs.fogMode; cb.fog2[1] = rs.fogDensity; cb.fog2[2] = rs.fogHeight; cb.fog2[3] = rs.fogFalloff;
            cb.post[0] = hdrOut ? 1.0f : 0.0f;   // leave the colour linear for the post chain
        }
        if (SUCCEEDED(c->Map(p->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            std::memcpy(ms.pData, &cb, sizeof(cb)); c->Unmap(p->cb, 0);
        }
        if (cust) {   // the custom pixel shader, its properties + time (b4) and its pictures (t9..)
            c->PSSetShader(cust->ps, nullptr, 0);
            const oshader::ShaderFile sf = oshader::LoadShaderFile(mr->customShader);
            float cdata[4 * (1 + oshader::kMaxProperties)];
            std::memset(cdata, 0, sizeof(cdata));
            cdata[0] = Time::ElapsedTime();
            if (sf.compiled && sf.compiled->ok) {
                const std::vector<float> v = oshader::ResolveProperties(*sf.compiled, mr->shaderParams);
                std::memcpy(cdata + 4, v.data(), sizeof(float) * std::min<std::size_t>(v.size(), 4 * oshader::kMaxProperties));
                for (int t = 0; t < (int)sf.compiled->textures.size() && t < oshader::kMaxTextures; ++t) {
                    const std::string pic = oshader::ResolveTexture(*sf.compiled, t, mr->shaderParams);
                    ID3D11ShaderResourceView* ts = pic.empty() ? nullptr : p->TextureFor(pic);
                    if (!ts) ts = p->whiteSrv;
                    c->PSSetShaderResources((UINT)(oshader::kFirstTextureUnit + t), 1, &ts);
                }
            }
            if (SUCCEEDED(c->Map(p->customCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                std::memcpy(ms.pData, cdata, sizeof(cdata)); c->Unmap(p->customCB, 0);
            }
            c->PSSetConstantBuffers((UINT)oshader::kHlslPropRegister, 1, &p->customCB);
        }
        if (srv) c->PSSetShaderResources(0, 1, &srv);
        if (nsrv) c->PSSetShaderResources(1, 1, &nsrv);   // normal/bump map on t1
        if (ssrv) c->PSSetShaderResources(2, 1, &ssrv);   // gloss/specular map on t2
        if (aosrv) c->PSSetShaderResources(3, 1, &aosrv); // ambient-occlusion map on t3
        ID3D11ShaderResourceView* mrsrv = mr->metallicRoughnessMap.empty() ? nullptr : p->TextureFor(mr->metallicRoughnessMap);
        ID3D11ShaderResourceView* emsrv = mr->emissiveMap.empty() ? nullptr : p->TextureFor(mr->emissiveMap);
        if (mrsrv) c->PSSetShaderResources(6, 1, &mrsrv);
        if (emsrv) c->PSSetShaderResources(7, 1, &emsrv);
        // Per-material texture filter (Pixel = nearest, Smooth = aniso/linear).
        const bool pixelMat = mr->texFilter == MeshRenderer::TexFilter::Pixel;
        ID3D11SamplerState* useSamp = mr->texture.empty() ? ((pixelMat && p->sampPoint) ? p->sampPoint : p->samp)
                                                          : p->SamplerFor(mr->texture, pixelMat);
        c->PSSetSamplers(0, 1, &useSamp);

        UINT stride = 14 * sizeof(float), offset = 0;
        ID3D11Buffer* vbUse = mvb ? mvb->vbuf : p->vb;   // cached static buffer or the stream buffer
        c->IASetVertexBuffers(0, 1, &vbUse, &stride, &offset);
        if (svb) {   // skinned on the GPU: second stream + bones, skinning vertex shader
            UINT st1 = 8 * sizeof(float), of1 = 0;
            c->IASetVertexBuffers(1, 1, &svb->buf, &st1, &of1);
            c->VSSetConstantBuffers(1, 1, &p->bonesCB);
            c->IASetInputLayout(p->layoutSkin);
            c->VSSetShader(p->vsSkin, nullptr, 0);
        }
        // Transparency: a material alpha < 1 (water, glass) alpha-blends over the
        // scene and doesn't write depth, so geometry behind shows through. Reuses the
        // ground-shadow blend + no-depth-write states. Draw transparent meshes after
        // opaque ones (objects draw in scene order).
        bool transparent = (cust ? (mr->color.a < 0.999f || cust->writesAlpha) : (!custBad && mr->color.a < 0.999f)) && p->blend && p->depthNoWrite;
        const float blendF[4] = {0, 0, 0, 0};
        if (transparent) {
            c->OMSetBlendState(p->blend, blendF, 0xffffffff);
            c->OMSetDepthStencilState(p->depthNoWrite, 0);
        }
        // Per-slot pictures: with material slots that have their own picture, one draw per slot range with that picture.
        bool slotDraw = false;
        if (mvb && !svb && !mvb->slotRanges.empty())
            for (const std::string& st : mr->slotTextures) if (!st.empty()) slotDraw = true;
        if (slotDraw) {
            ID3D11ShaderResourceView* bound = srv;
            for (const auto& rg : mvb->slotRanges) {
                if (rg[2] <= 0) continue;
                ID3D11ShaderResourceView* want = srv;
                if (rg[0] >= 0 && rg[0] < (int)mr->slotTextures.size() && !mr->slotTextures[rg[0]].empty()) {
                    if (ID3D11ShaderResourceView* st = p->TextureFor(mr->slotTextures[rg[0]])) want = st;
                }
                if (want != bound) {
                    cb.useTex = want ? 1.0f : 0.0f;
                    cb.tiling[0] = want ? mr->tiling.x : 1.0f; cb.tiling[1] = want ? mr->tiling.y : 1.0f;
                    if (SUCCEEDED(c->Map(p->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { std::memcpy(ms.pData, &cb, sizeof(cb)); c->Unmap(p->cb, 0); }
                    if (want) c->PSSetShaderResources(0, 1, &want);
                    bound = want;
                }
                c->Draw((UINT)rg[2], (UINT)rg[1]);
            }
            if (bound != srv) {   // put the object's own picture back for the shadow pass below
                cb.useTex = srv ? 1.0f : 0.0f;
                cb.tiling[0] = srv ? mr->tiling.x : 1.0f; cb.tiling[1] = srv ? mr->tiling.y : 1.0f;
                if (SUCCEEDED(c->Map(p->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { std::memcpy(ms.pData, &cb, sizeof(cb)); c->Unmap(p->cb, 0); }
                if (srv) c->PSSetShaderResources(0, 1, &srv);
            }
        } else
        c->Draw((UINT)vcount, 0);
        if (transparent) {
            c->OMSetBlendState(nullptr, blendF, 0xffffffff);
            c->OMSetDepthStencilState(p->depth, 0);
        }

        // Ground contact shadow: re-draw the SAME geometry flattened onto the ground
        // plane as flat translucent black, so objects sit on the ground (not floating).
        if (planarShadows && !go->firstPersonOnly && mr->groundShadow && mr->castShadows && mr->groundShadowStrength > 0.001f &&
            p->blend && p->depthPlanar) {
            Mat4 sModel = Mat4::PlanarShadow(mr->groundShadowY + 0.01f, SceneLight::Direction()) * model;
            Mat4 sMvp = vp * sModel;
            std::memcpy(cb.mvp, sMvp.m, sizeof(cb.mvp));
            cb.shadowAlpha = mr->groundShadowStrength > 1.0f ? 1.0f : mr->groundShadowStrength;
            if (SUCCEEDED(c->Map(p->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                std::memcpy(ms.pData, &cb, sizeof(cb)); c->Unmap(p->cb, 0);
            }
            const float bf[4] = {0, 0, 0, 0};
            c->OMSetBlendState(p->blend, bf, 0xffffffff);
            c->OMSetDepthStencilState(p->depthPlanar, 0);
            c->Draw((UINT)vcount, 0);
            c->OMSetBlendState(nullptr, bf, 0xffffffff);
            c->OMSetDepthStencilState(p->depth, 0);
        }
        if (svb) { c->IASetInputLayout(p->layout); c->VSSetShader(p->vs, nullptr, 0); }
        if (cust) c->PSSetShader(p->ps, nullptr, 0);
    }
    LastCullCounters().drawn = drawnN;

    const bool useChain = chain && p->EnsurePostTargets();
    PostVignetteHandled() = useChain && rsTop.vignette + rsTop.vignettePulse > 0.0f;

    // ---- Ambient occlusion depth pre-pass (UNVERIFIED on a real GPU) -------------
    // Runs only when the post chain is on AND an Ambient Occlusion switch is on
    // (the scene's post.ssao, or the Build Settings / Rendering SSAOEnabled), as on
    // OpenGL. Opaque geometry is drawn again at half resolution as view depth / far
    // into an R32F target; the chain below estimates, blurs and applies it
    // (SsaoMath.hpp has the algorithm, shared with the OpenGL renderer's numbers).
    // Depth of field (post.dof, DofMath.hpp) reads the same depth image, so the
    // pre-pass also runs when only depth of field is on.
    const bool aoWanted = useChain && (rsTop.post.ssao || SSAOEnabled());
    Impl::SsaoSet* aoSet = nullptr;
    if (useChain && (aoWanted || rsTop.post.dof) && p->EnsureSsaoShaders()) aoSet = p->EnsureSsaoSet();
    const bool aoOn = aoSet && aoWanted;
    const bool dofOn = aoSet && rsTop.post.dof && p->psDof;
    if (aoSet) {
        const int ahw = w / 2 > 0 ? w / 2 : 1, ahh = h / 2 > 0 ? h / 2 : 1;
        pc.p7[0] = rsTop.post.ssao ? rsTop.post.ssaoRadius : SSAORadius();
        pc.p7[1] = ssao::kFar;
        pc.p7[2] = rsTop.post.ssao ? rsTop.post.ssaoIntensity : SSAOStrength();
        pc.p7[3] = rsTop.post.ssao ? rsTop.post.ssaoPower : 1.0f;   // read by PSAoMul (PostQualityMath.hpp)
        uploadPc();
        { ID3D11ShaderResourceView* nullsrv[2] = {nullptr, nullptr}; c->PSSetShaderResources(4, 2, nullsrv); }
        c->OMSetRenderTargets(1, &aoSet->depth.rtv, aoSet->dsv);
        D3D11_VIEWPORT av; std::memset(&av, 0, sizeof(av));
        av.Width = (FLOAT)ahw; av.Height = (FLOAT)ahh; av.MaxDepth = 1.0f;
        c->RSSetViewports(1, &av);
        const float farDepth[4] = {1.0f, 0.0f, 0.0f, 1.0f};       // nothing drawn = far = no occlusion
        c->ClearRenderTargetView(aoSet->depth.rtv, farDepth);
        c->ClearDepthStencilView(aoSet->dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        c->IASetInputLayout(p->layoutDepth);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(p->vsDepth, nullptr, 0);
        c->PSSetShader(p->psAoDepth, nullptr, 0);
        c->VSSetConstantBuffers(0, 1, &p->cb);
        c->PSSetConstantBuffers(3, 1, &p->postCB);
        c->RSSetState(p->raster);
        c->OMSetDepthStencilState(p->depth, 0);
        const float abf[4] = {0, 0, 0, 0};
        c->OMSetBlendState(nullptr, abf, 0xffffffff);
        drawCasters(vp, nullptr, /*forView=*/true);
    }
    // RenderToTexture without the chain: resolve straight into the host's texture and stop here.
    if (p->directDst && !useChain) {
        if (p->samples > 1) c->ResolveSubresource(p->directDst, 0, p->colorTex, 0, p->fmt);
        else                c->CopyResource(p->directDst, p->colorTex);
        { ID3D11ShaderResourceView* nullsrv[2] = {nullptr, nullptr}; c->PSSetShaderResources(4, 2, nullsrv); }
        return reinterpret_cast<const std::uint32_t*>(p->directDst);   // non-null = success (no pixels)
    }
    // Resolve MSAA (or copy) -> [post chain] -> staging -> read back. D3D textures
    // are top-left, so no row flip is needed (unlike GL).
    if (p->samples > 1) c->ResolveSubresource(p->resolveTex, 0, p->colorTex, 0, p->fmt);
    else                c->CopyResource(p->resolveTex, p->colorTex);
    ID3D11Texture2D* readTex = p->resolveTex;   // what the staging copy reads
    if (useChain) {
        const auto& ps = rsTop.post;
        beginQuadPasses();
        ID3D11ShaderResourceView* nulls[3] = {nullptr, nullptr, nullptr};
        auto pass = [&](ID3D11PixelShader* shader, ID3D11RenderTargetView* rtv, int vw, int vh,
                        ID3D11ShaderResourceView* src, ID3D11ShaderResourceView* src1, ID3D11ShaderResourceView* src2) {
            c->PSSetShaderResources(0, 3, nulls);          // never bound as SRV + RT at once
            c->OMSetRenderTargets(1, &rtv, nullptr);
            D3D11_VIEWPORT v; std::memset(&v, 0, sizeof(v));
            v.Width = (FLOAT)vw; v.Height = (FLOAT)vh; v.MaxDepth = 1.0f;
            c->RSSetViewports(1, &v);
            c->PSSetShader(shader, nullptr, 0);
            ID3D11ShaderResourceView* in[3] = {src, src1, src2};
            c->PSSetShaderResources(0, 3, in);
            uploadPc();
            c->Draw(3, 0);
        };
        // 0) Ambient occlusion: estimate at half res, one gaussian round (horizontal
        //    then vertical), then lit frame * mix(1, ao, strength) into the AO target
        //    set. Bloom and the uber pass read that instead of the plain resolve.
        ID3D11ShaderResourceView* frameSrv = p->resolveSrv;
        if (aoOn) {
            const int ahw = aoSet->depth.w, ahh = aoSet->depth.h;
            pc.texel[0] = 1.0f / (float)ahw; pc.texel[1] = 1.0f / (float)ahh;
            pass(p->psAo, aoSet->aoA.rtv, ahw, ahh, aoSet->depth.srv, nullptr, nullptr);
            pc.dir[0] = 1.0f / (float)ahw; pc.dir[1] = 0.0f;
            pass(p->psBlur, aoSet->aoB.rtv, ahw, ahh, aoSet->aoA.srv, nullptr, nullptr);
            pc.dir[0] = 0.0f; pc.dir[1] = 1.0f / (float)ahh;
            pass(p->psBlur, aoSet->aoA.rtv, ahw, ahh, aoSet->aoB.srv, nullptr, nullptr);
            pass(p->psAoMul, aoSet->mul.rtv, w, h, p->resolveSrv, aoSet->aoA.srv, nullptr);
            frameSrv = aoSet->mul.srv;
        }
        // 1) Bloom: bright-pass to half res, blur, down to quarter, blur (the uber pass adds both).
        const bool bloomOn = ps.bloom || BloomEnabled();
        const float bloomThresh = ps.bloom ? ps.bloomThreshold : BloomThreshold();
        const float bloomIntensity = ps.bloom ? ps.bloomIntensity : BloomIntensity();
        const float bloomRadius = ps.bloom ? ps.bloomRadius : 1.0f;
        Impl::RT& bA = p->bloom[0]; Impl::RT& bB = p->bloom[1]; Impl::RT& bC = p->bloom[2]; Impl::RT& bD = p->bloom[3];
        if (bloomOn) {
            pc.p7[3] = ps.bloom ? ps.bloomSoftness : 0.0f;   // read by PSDown's soft knee (PostQualityMath.hpp)
            pc.texel[0] = 1.0f / (float)w; pc.texel[1] = 1.0f / (float)h; pc.thresh = bloomThresh; pc.radius = 1.0f;
            pass(p->psDown, bA.rtv, bA.w, bA.h, frameSrv, nullptr, nullptr);
            for (int it = 0; it < 2; ++it) {
                pc.dir[0] = bloomRadius / (float)bA.w; pc.dir[1] = 0.0f;
                pass(p->psBlur, bB.rtv, bB.w, bB.h, bA.srv, nullptr, nullptr);
                pc.dir[0] = 0.0f; pc.dir[1] = bloomRadius / (float)bA.h;
                pass(p->psBlur, bA.rtv, bA.w, bA.h, bB.srv, nullptr, nullptr);
            }
            pc.texel[0] = 1.0f / (float)bA.w; pc.texel[1] = 1.0f / (float)bA.h; pc.thresh = 0.0f; pc.radius = 1.0f;
            pass(p->psDown, bC.rtv, bC.w, bC.h, bA.srv, nullptr, nullptr);
            for (int it = 0; it < 2; ++it) {
                pc.dir[0] = bloomRadius * 1.5f / (float)bC.w; pc.dir[1] = 0.0f;
                pass(p->psBlur, bD.rtv, bD.w, bD.h, bC.srv, nullptr, nullptr);
                pc.dir[0] = 0.0f; pc.dir[1] = bloomRadius * 1.5f / (float)bC.h;
                pass(p->psBlur, bC.rtv, bC.w, bC.h, bD.srv, nullptr, nullptr);
            }
        }
        // 2) Exposure, bloom composite, tonemap, grading, vignette -> the output
        //    (or the ping texture when FXAA follows).
        ID3D11RenderTargetView* directRtv = nullptr;
        if (p->directDst) p->dev->CreateRenderTargetView(p->directDst, nullptr, &directRtv);   // may fail: copied below instead
        ID3D11RenderTargetView* finalRtv = directRtv ? directRtv : p->out.rtv;
        pc.p0[0] = ExposureScale(rsTop); pc.p0[1] = hdrOut ? 1.0f : 0.0f; pc.p0[2] = (float)ps.tonemap; pc.p0[3] = bloomOn ? 1.0f : 0.0f;
        pc.p1[0] = bloomIntensity; pc.p1[1] = ps.grading ? 1.0f : 0.0f; pc.p1[2] = ps.temperature; pc.p1[3] = ps.tint;
        pc.p2[0] = ps.saturation; pc.p2[1] = ps.contrast; pc.p2[2] = rsTop.vignette + rsTop.vignettePulse > 1.0f ? 1.0f : rsTop.vignette + rsTop.vignettePulse; pc.p2[3] = ps.vignetteSmooth;
        pc.lift[0] = ps.lift.x; pc.lift[1] = ps.lift.y; pc.lift[2] = ps.lift.z; pc.aspect = (float)w / (float)h;
        pc.gamma[0] = ps.gamma.x; pc.gamma[1] = ps.gamma.y; pc.gamma[2] = ps.gamma.z;
        pc.gain[0] = ps.gain.x; pc.gain[1] = ps.gain.y; pc.gain[2] = ps.gain.z;
        const bool toPing = dofOn || ps.fxaa;
        pass(p->psUber, toPing ? p->ping[0].rtv : finalRtv, w, h, frameSrv, bA.srv, bC.srv);
        ID3D11ShaderResourceView* cur = p->ping[0].srv;   // the display image so far (when toPing)
        // 3) Depth of field (DofMath.hpp): a half-res blurred copy of the display image
        //    (box downsample with no threshold, then two gaussian rounds), mixed in by
        //    the distance from the focus plane, read from the AO depth image.
        if (dofOn) {
            pc.texel[0] = 1.0f / (float)w; pc.texel[1] = 1.0f / (float)h; pc.thresh = 0.0f; pc.radius = 1.0f;
            pass(p->psDofCopy, bA.rtv, bA.w, bA.h, cur, nullptr, nullptr);   // premultiplied: the sky's alpha 0 survives
            for (int it = 0; it < 2; ++it) {
                pc.dir[0] = 1.0f / (float)bA.w; pc.dir[1] = 0.0f;
                pass(p->psDofBlur, bB.rtv, bB.w, bB.h, bA.srv, nullptr, nullptr);
                pc.dir[0] = 0.0f; pc.dir[1] = 1.0f / (float)bA.h;
                pass(p->psDofBlur, bA.rtv, bA.w, bA.h, bB.srv, nullptr, nullptr);
            }
            pc.texel[0] = 1.0f / (float)aoSet->depth.w; pc.texel[1] = 1.0f / (float)aoSet->depth.h;
            pc.p7[0] = ps.dofFocus; pc.p7[1] = ps.dofRange; pc.p7[2] = ssao::kFar;
            pass(p->psDof, ps.fxaa ? p->ping[1].rtv : finalRtv, w, h, cur, bA.srv, aoSet->depth.srv);
            cur = p->ping[1].srv;
        }
        // 4) FXAA last, on the display image.
        if (ps.fxaa) {
            pc.texel[0] = 1.0f / (float)w; pc.texel[1] = 1.0f / (float)h;
            pass(p->psFxaa, finalRtv, w, h, cur, nullptr, nullptr);
        }
        c->PSSetShaderResources(0, 3, nulls);
        c->OMSetRenderTargets(0, nullptr, nullptr);
        if (directRtv) directRtv->Release();
        if (p->directDst) {
            if (!directRtv) {   // couldn't target the host texture: copy the output (same format only)
                D3D11_TEXTURE2D_DESC dd; p->directDst->GetDesc(&dd);
                if (dd.Format != DXGI_FORMAT_R8G8B8A8_UNORM) return nullptr;
                c->CopyResource(p->directDst, p->out.tex);
            }
            { ID3D11ShaderResourceView* nullsrv[2] = {nullptr, nullptr}; c->PSSetShaderResources(4, 2, nullsrv); }
            return reinterpret_cast<const std::uint32_t*>(p->directDst);
        }
        readTex = p->out.tex;
    }
    ID3D11Texture2D* readFrom = p->staging;
    if (p->stream >= 0) {
        Impl::Pipe& pp = p->pipes[p->stream];
        const auto now = std::chrono::steady_clock::now();
        const bool fresh = now - pp.last < std::chrono::milliseconds(250);
        pp.last = now;
        if (pp.w != w || pp.h != h || !pp.st[0] || !pp.st[1]) {
            for (auto*& t : pp.st) if (t) { t->Release(); t = nullptr; }
            pp.w = pp.h = 0; pp.filled[0] = pp.filled[1] = false; pp.idx = 0;
            D3D11_TEXTURE2D_DESC ts; std::memset(&ts, 0, sizeof(ts));
            ts.Width = w; ts.Height = h; ts.MipLevels = 1; ts.ArraySize = 1;
            ts.Format = DXGI_FORMAT_R8G8B8A8_UNORM; ts.SampleDesc.Count = 1;
            ts.Usage = D3D11_USAGE_STAGING; ts.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (SUCCEEDED(p->dev->CreateTexture2D(&ts, nullptr, &pp.st[0])) &&
                SUCCEEDED(p->dev->CreateTexture2D(&ts, nullptr, &pp.st[1]))) { pp.w = w; pp.h = h; }
        }
        if (pp.w == w && pp.h == h) {
            const int cur = pp.idx, prev = pp.idx ^ 1;
            c->CopyResource(pp.st[cur], readTex);
            pp.filled[cur] = true;
            readFrom = (fresh && pp.filled[prev]) ? pp.st[prev] : pp.st[cur];
            pp.idx = prev;
        } else c->CopyResource(p->staging, readTex);   // couldn't make the ring: synchronous
    } else {
        c->CopyResource(p->staging, readTex);
    }

    p->pixels.assign((std::size_t)w * h, 0u);
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(c->Map(readFrom, 0, D3D11_MAP_READ, 0, &ms))) return nullptr;
    const std::uint8_t* src = (const std::uint8_t*)ms.pData;
    for (int y = 0; y < h; ++y)
        std::memcpy(p->pixels.data() + (std::size_t)y * w, src + (std::size_t)y * ms.RowPitch, (std::size_t)w * 4);
    c->Unmap(readFrom, 0);
    // Release the shadow map from t4 so next frame's depth pass can bind it as a DSV
    // without a read/write hazard warning.
    { ID3D11ShaderResourceView* nullsrv[2] = {nullptr, nullptr}; c->PSSetShaderResources(4, 2, nullsrv); }
    return p->pixels.data();
}

void D3D11Renderer::SetReadbackStream(int id) { if (m_impl) m_impl->stream = id; }

void* D3D11Renderer::BoundRenderTargetTexture() {
    if (!m_impl || !m_impl->ctx) return nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    m_impl->ctx->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return nullptr;
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    rtv->Release();
    if (!res) return nullptr;
    ID3D11Texture2D* tex = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    res->Release();
    return tex;
}
bool D3D11Renderer::BundleShader(const char* entry, const void** data, std::size_t* size) {
#if defined(OKAY_XBOX_UWP)
    if (!entry || !data || !size || !EnsureBundle()) return false;
    const std::vector<unsigned char>* b = g_bundle.Find(entry);
    if (!b) return false;
    *data = b->data(); *size = b->size(); return true;
#else
    (void)entry; (void)data; (void)size; return false;
#endif
}
void D3D11Renderer::SetShaderBundleDir(const char* dir) {
#if defined(OKAY_XBOX_UWP)
    g_shaderDir = dir ? dir : ""; g_bundleTried = false; g_bundleOk = false;
#else
    (void)dir;
#endif
}
void* D3D11Renderer::DeviceForPresent() const { return m_impl ? static_cast<void*>(m_impl->dev) : nullptr; }
void D3D11Renderer::ReleaseCom(void* unknown) { if (unknown) static_cast<IUnknown*>(unknown)->Release(); }


bool D3D11Renderer::RenderToTexture(const Scene& scene, const Mat4& vp, const Vec3& eye,
                                    int w, int h, int samples,
                                    float clearR, float clearG, float clearB, float clearA,
                                    void* dstTexture, const GameObject* ignore) {
    if (!Available() || !dstTexture) return false;
    Impl* p = m_impl;
    D3D11_TEXTURE2D_DESC dd; static_cast<ID3D11Texture2D*>(dstTexture)->GetDesc(&dd);
    if ((int)dd.Width != w || (int)dd.Height != h || dd.SampleDesc.Count != 1) return false;
    if (dd.Format != DXGI_FORMAT_R8G8B8A8_UNORM && dd.Format != DXGI_FORMAT_B8G8R8A8_UNORM) return false;
    auto* backup = new D3D11StateBackup();   // (large: keep it off the stack)
    backup->Save(p->ctx);
    p->stateSaved = true;
    p->directDst = static_cast<ID3D11Texture2D*>(dstTexture);
    p->directFmt = dd.Format;
    const bool ok = RenderToPixels(scene, vp, eye, w, h, samples, clearR, clearG, clearB, clearA, ignore) != nullptr;
    p->directDst = nullptr;
    p->wantFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    p->stateSaved = false;
    backup->Restore(p->ctx);
    delete backup;
    return ok;
}

void D3D11Renderer::Destroy() {
    if (!m_impl) return;
    Impl* p = m_impl;
    p->DestroyTargets();
    for (auto& t : p->parked) p->ReleaseSet(t);
    p->parked.clear();
    for (auto& kv : p->pipes) for (auto* t : kv.second.st) if (t) t->Release();
    p->pipes.clear();
    for (auto& kv : p->texCache) if (kv.second) kv.second->Release();
    for (auto& kv : p->texUpload) if (kv.second.gpuBytes) MemoryTracker::Global().Remove(D3D11GpuTextureCategory(), std::int64_t(kv.second.gpuBytes), 1);
    p->texCache.clear();
    p->texUpload.clear();
    p->texSampling.clear();
    for (auto& kv : p->customPs) if (kv.second.ps) kv.second.ps->Release();
    p->customPs.clear();
    if (p->customCB) { p->customCB->Release(); p->customCB = nullptr; }
    if (p->whiteSrv) { p->whiteSrv->Release(); p->whiteSrv = nullptr; }
    if (p->whiteTex) { p->whiteTex->Release(); p->whiteTex = nullptr; }
    for (auto& kv : p->sampCombo) if (kv.second) kv.second->Release();
    p->sampCombo.clear();
    for (auto& kv : p->meshVB) if (kv.second.vbuf) kv.second.vbuf->Release();
    p->meshVB.clear();
    p->sunAtlas.Release();
    p->localAtlas.Release();
    if (p->shCB) p->shCB->Release();
    if (p->postCB) p->postCB->Release();
    if (p->sampClamp) p->sampClamp->Release();
    if (p->cookieSrv) p->cookieSrv->Release();
    if (p->cookieTex) p->cookieTex->Release();
    if (p->depthOff) p->depthOff->Release();
    if (p->vsQuad) p->vsQuad->Release();
    for (ID3D11PixelShader* s : {p->psSky, p->psDown, p->psBlur, p->psUber, p->psFxaa, p->psAoDepth, p->psAo, p->psAoMul, p->psDof, p->psDofCopy, p->psDofBlur}) if (s) s->Release();
    for (auto& t : p->ssaoSets) t.Release();
    p->ssaoSets.clear();
    if (p->depthPlanar) p->depthPlanar->Release();
    if (p->vbDepth)   p->vbDepth->Release();
    if (p->layoutDepth) p->layoutDepth->Release();
    if (p->vsDepth)   p->vsDepth->Release();
    for (auto& kv : p->skinVB) if (kv.second.buf) kv.second.buf->Release();
    p->skinVB.clear();
    if (p->vsSkin) p->vsSkin->Release();
    if (p->layoutSkin) p->layoutSkin->Release();
    if (p->vsDepthSkin) p->vsDepthSkin->Release();
    if (p->layoutDepthSkin) p->layoutDepthSkin->Release();
    if (p->bonesCB) p->bonesCB->Release();
    if (p->sampShadow) p->sampShadow->Release();
    if (p->vb)     p->vb->Release();
    if (p->samp)   p->samp->Release();
    if (p->sampPoint) p->sampPoint->Release();
    if (p->blend)  p->blend->Release();
    if (p->depthNoWrite) p->depthNoWrite->Release();
    if (p->depth)  p->depth->Release();
    if (p->raster) p->raster->Release();
    if (p->cb)     p->cb->Release();
    if (p->layout) p->layout->Release();
    if (p->ps)     p->ps->Release();
    if (p->vs)     p->vs->Release();
    if (p->ctx)    p->ctx->Release();
    if (p->dev)    p->dev->Release();
    delete p;
    m_impl = nullptr;
}

D3D11Renderer::~D3D11Renderer() { Destroy(); }

} // namespace okay

#endif // _WIN32
