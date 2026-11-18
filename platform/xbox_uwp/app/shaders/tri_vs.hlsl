// One triangle from the vertex id: no vertex buffer. EXPERIMENTAL, UNVERIFIED (never compiled with fxc).
cbuffer Frame : register(b0) { float2 offset; float aspect; float pad; };
struct VSOut { float4 pos : SV_POSITION; float3 col : COLOR0; };
VSOut main(uint id : SV_VertexID) {
    const float2 p[3] = { float2(0.0f, 0.5f), float2(0.5f, -0.5f), float2(-0.5f, -0.5f) };
    const float3 c[3] = { float3(1, 0.2f, 0.2f), float3(0.2f, 1, 0.2f), float3(0.2f, 0.4f, 1) };
    VSOut o;
    o.pos = float4(p[id].x / aspect + offset.x, p[id].y + offset.y, 0.0f, 1.0f);
    o.col = c[id];
    return o;
}
