// OkayUI Direct3D 11 backend — Windows only. The whole file is guarded so the
// cross-platform (Linux/SDL) build never touches Direct3D.
#if defined(_WIN32)

#include "okay/UI/OkayUI_D3D11.hpp"
#include "okay/UI/OkayUI.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>   // memcpy / strlen (libc; the no-STL rule is for the widget core)

namespace OkayUI {
namespace {

// One shader file: an orthographic transform (top-left origin) and pass-through of
// the per-vertex color. OkayUI v1 geometry is solid-colored, so no texture sampling.
const char* kHLSL =
    "cbuffer CB : register(b0) { float2 uScale; float2 uPad; };\n"
    "struct VSIn  { float2 pos : POSITION; float4 col : COLOR; float2 uv : TEXCOORD; };\n"
    "struct PSIn  { float4 pos : SV_POSITION; float4 col : COLOR; };\n"
    "PSIn VSMain(VSIn i) {\n"
    "  PSIn o;\n"
    "  o.pos = float4(i.pos.x * uScale.x - 1.0, 1.0 - i.pos.y * uScale.y, 0.0, 1.0);\n"
    "  o.col = i.col;\n"
    "  return o;\n"
    "}\n"
    "float4 PSMain(PSIn i) : SV_TARGET { return i.col; }\n";

} // namespace

struct D3D11Backend::Impl {
    ID3D11Device*           dev    = nullptr;
    ID3D11VertexShader*     vs     = nullptr;
    ID3D11PixelShader*      ps     = nullptr;
    ID3D11InputLayout*      layout = nullptr;
    ID3D11Buffer*           vb     = nullptr; int vbCap = 0;   // capacity in vertices
    ID3D11Buffer*           ib     = nullptr; int ibCap = 0;   // capacity in indices
    ID3D11Buffer*           cb     = nullptr;
    ID3D11BlendState*       blend  = nullptr;
    ID3D11RasterizerState*  raster = nullptr;
};

bool D3D11Backend::Init(ID3D11Device* device) {
    if (!device) return false;
    Shutdown();
    m_impl = new Impl();
    m_impl->dev = device;
    Impl* p = m_impl;

    ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* err = nullptr;
    const SIZE_T len = (SIZE_T)std::strlen(kHLSL);
    if (FAILED(D3DCompile(kHLSL, len, "okayui", nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vsb, &err))) {
        if (err) err->Release();
        Shutdown(); return false;
    }
    if (FAILED(D3DCompile(kHLSL, len, "okayui", nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &psb, &err))) {
        if (err) err->Release(); if (vsb) vsb->Release();
        Shutdown(); return false;
    }
    device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &p->vs);
    device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &p->ps);

    // Input layout matches OkayUI's vertex (pos float2 @0, RGBA8 @8, uv float2 @12).
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,  0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,  0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    device->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &p->layout);
    vsb->Release(); psb->Release();

    D3D11_BUFFER_DESC cbd; std::memset(&cbd, 0, sizeof(cbd));
    cbd.ByteWidth = 16; cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&cbd, nullptr, &p->cb);

    // Straight-alpha blending so anti-aliased / translucent UI composites correctly.
    D3D11_BLEND_DESC bd; std::memset(&bd, 0, sizeof(bd));
    bd.RenderTarget[0].BlendEnable    = TRUE;
    bd.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&bd, &p->blend);

    D3D11_RASTERIZER_DESC rd; std::memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&rd, &p->raster);

    return p->vs && p->ps && p->layout && p->cb && p->blend && p->raster;
}

void D3D11Backend::Render(ID3D11DeviceContext* ctx, int displayW, int displayH) {
    if (!m_impl || !ctx || displayW <= 0 || displayH <= 0) return;
    Impl* p = m_impl;
    const DrawData d = GetDrawData();
    if (d.vertexCount <= 0 || d.indexCount <= 0 || !p->vs) return;

    // Grow the dynamic vertex/index buffers when the batch outgrows them.
    if (d.vertexCount > p->vbCap) {
        if (p->vb) { p->vb->Release(); p->vb = nullptr; }
        const int cap = d.vertexCount + 512;
        D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
        bd.ByteWidth = (UINT)(d.vertexStride * cap); bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(p->dev->CreateBuffer(&bd, nullptr, &p->vb))) return;
        p->vbCap = cap;
    }
    if (d.indexCount > p->ibCap) {
        if (p->ib) { p->ib->Release(); p->ib = nullptr; }
        const int cap = d.indexCount + 1024;
        D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
        bd.ByteWidth = (UINT)(sizeof(int) * cap); bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(p->dev->CreateBuffer(&bd, nullptr, &p->ib))) return;
        p->ibCap = cap;
    }

    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(ctx->Map(p->vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        std::memcpy(ms.pData, d.vertices, (size_t)d.vertexStride * d.vertexCount);
        ctx->Unmap(p->vb, 0);
    }
    if (SUCCEEDED(ctx->Map(p->ib, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        std::memcpy(ms.pData, d.indices, sizeof(int) * (size_t)d.indexCount);
        ctx->Unmap(p->ib, 0);
    }
    if (SUCCEEDED(ctx->Map(p->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        const float cb[4] = {2.0f / (float)displayW, 2.0f / (float)displayH, 0.0f, 0.0f};
        std::memcpy(ms.pData, cb, sizeof(cb));
        ctx->Unmap(p->cb, 0);
    }

    UINT stride = (UINT)d.vertexStride, offset = 0;
    ctx->IASetInputLayout(p->layout);
    ctx->IASetVertexBuffers(0, 1, &p->vb, &stride, &offset);
    ctx->IASetIndexBuffer(p->ib, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(p->vs, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &p->cb);
    ctx->PSSetShader(p->ps, nullptr, 0);
    const float blendFactor[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(p->blend, blendFactor, 0xffffffffu);
    ctx->RSSetState(p->raster);
    ctx->DrawIndexed((UINT)d.indexCount, 0, 0);
}

void D3D11Backend::Shutdown() {
    if (!m_impl) return;
    Impl* p = m_impl;
    if (p->raster) p->raster->Release();
    if (p->blend)  p->blend->Release();
    if (p->cb)     p->cb->Release();
    if (p->ib)     p->ib->Release();
    if (p->vb)     p->vb->Release();
    if (p->layout) p->layout->Release();
    if (p->ps)     p->ps->Release();
    if (p->vs)     p->vs->Release();
    delete p;
    m_impl = nullptr;
}

} // namespace OkayUI

#endif // _WIN32
