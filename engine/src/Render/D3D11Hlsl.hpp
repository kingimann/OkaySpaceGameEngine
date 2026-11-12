#pragma once
// ---------------------------------------------------------------------------
// The Direct3D 11 renderer's HLSL as plain source text (the renderer compiles
// each entry point from it with D3DCompile at run time). It lives in a header
// with no Windows includes so a test can read the same text on any platform:
// tests/test_d3d11_shaders.cpp compiles every entry point with D3DCompile on
// Windows and checks it on other hosts with glslang's HLSL front end when that
// tool is installed.
// ---------------------------------------------------------------------------
namespace okay {
namespace d3d11hlsl {

// Matrix convention matches GLRenderer: the engine's column-major Mat4 is
// uploaded as-is and used as mul(M, v) (== M*v, same as GL's uMVP*v). The engine's
// projection yields GL-style clip z in [-1,1]; D3D wants [0,1], so we remap z in the
// vertex shader: z' = (z + w) / 2. No row flip is needed on read-back (D3D textures
// are top-left, like SDL).
inline constexpr const char* kSource =
    "cbuffer CB : register(b0) {\n"
    "  float4x4 uMVP; float4x4 uModel;\n"
    "  float3 uColor;     float uSpecular;\n"
    "  float3 uEmissive;  float uShininess;\n"
    "  float3 uLightDir;  float uUnlit;\n"
    "  float3 uLightColor; float uUseTex;\n"
    "  float3 uAmbient;   float uHasNormal;\n"      // _p0 repurposed: 1 = sample normal map
    "  float3 uEye;       float uNormalStrength;\n" // _p1 repurposed: bump intensity
    "  float2 uTiling;    float uShadowAlpha; float uAlpha;\n"   // _p2 repurposed: material opacity
    "  float3 uRimColor;  float uShaderMode;\n"
    "  float3 uGradTop;   float uToonBands;\n"
    "  float3 uGradBot;   float uRimStr;\n"
    "  float4 uPbr;\n"                        // x=metallic y=reflectivity z=hasSpecMap w=hasAo
    "  float4 uPbr2;\n"                       // x=aoStrength y=envOn z=texOffset.x w=texOffset.y
    "  float4 uSky[3];\n"                     // [0]=top [1]=horizon [2]=bottom (xyz) for reflections
    "  float4 uLightCount;\n"                 // .x = number of scene lights (0 = use uLightDir)
    "  float4 uLights[64];\n"                 // 16 lights x 4: [dir,type][pos,range][col,cosOut][cosIn,..]
    "  float4 uFog;\n"                        // x=on y=start z=end (distance fog) w=receive shadows
    "  float4 uFogCol;\n"                     // xyz = fog colour, w = filmic pipeline flag
    "  float4 uFog2;\n"                       // x=mode (0 linear 1 exponential 2 height) y=density z=height w=falloff
    "  float4 uPost;\n"                       // x=1: leave the colour linear for the post chain
    "};\n"
    // Shadows + ambient: once per frame (b2). Sun = cascades side by side in t4;
    // spot/point lights = a 4x4 tile atlas in t5 (see ShadowSetup.hpp).
    "cbuffer SH : register(b2) {\n"
    "  float4x4 uCasVP[4];\n"
    "  float4 uCasSphere[4];\n"               // xyz = centre, w = radius
    "  float4 uCasInfo[4];\n"                 // x = world texel size
    "  float4 uShadow;\n"                     // x=on y=cascade count z=cascade size w=softness (texels)
    "  float4 uShadow2;\n"                    // x=sun light index y=sun strength z=sun bias w=local tile size
    "  float4 uAmbSky;\n"                     // ambient for up-facing surfaces (scene Ambient Mode)
    "  float4 uAmbGround;\n"                  // ... and down-facing ones
    "  float4x4 uLocalVP[16];\n"
    "  float4 uLShadow[16];\n"                // per light: x=first tile (-1 none) y=strength z=bias w=texel at 1m
    "};\n"
    "Texture2D uTex : register(t0);\n"
    "Texture2D uNormalTex : register(t1);\n"        // bump/normal map (tangent-space)
    "Texture2D uSpecTex : register(t2);\n"          // gloss/specular map
    "Texture2D uAoTex : register(t3);\n"            // ambient-occlusion map
    "Texture2D uMrTex : register(t6);\n"            // glTF metallic-roughness (G rough, B metal)
    "Texture2D uEmisTex : register(t7);\n"          // glTF emissive
    "Texture2D uShadowTex : register(t4);\n"        // sun cascade atlas (depth)
    "Texture2D uLocalTex : register(t5);\n"         // spot/point light tile atlas (depth)
    "SamplerState uSamp : register(s0);\n"
    "SamplerComparisonState uShadowCmp : register(s1);\n"   // filtered depth compare (hardware PCF)
    "static float2x2 gRot;\n"                       // per-pixel Poisson rotation
    "float sunTap(float2 uv, float2 o, float z, float ci){\n"
    "  float e = 1.0 / uShadow.z;\n"                                  // stay a texel inside the cascade tile
    "  float2 t = clamp(uv + mul(gRot, o), e, 1.0 - e);\n"
    "  return uShadowTex.SampleCmpLevelZero(uShadowCmp, float2((ci + t.x) / uShadow.y, t.y), z);\n"
    "}\n"
    "float sunCascade(int ci, float3 wp, float3 n){\n"
    "  float tx = uCasInfo[ci].x;\n"
    // World-space bias (same as GL): along the normal, more for a wider filter,
    // plus a little toward the sun.
    "  float3 p = wp + n * (tx * (1.0 + 0.6 * uShadow.w) * uShadow2.z) + uLightDir * (tx * 0.75 * uShadow2.z);\n"
    "  float4 sc = mul(uCasVP[ci], float4(p, 1.0));\n"
    "  float2 uv = float2(sc.x * 0.5 + 0.5, -sc.y * 0.5 + 0.5);\n"        // D3D texture v is top-down
    "  float z = sc.z * 0.5 + 0.5;\n"                                   // GL clip z [-1,1] -> [0,1]
    "  if (z >= 1.0) return 1.0;\n"
    "  float r = uShadow.w / uShadow.z; float c = (float)ci; float s = 0.0;\n"
    "  s += sunTap(uv, float2(-0.326,-0.406)*r, z, c); s += sunTap(uv, float2(-0.840,-0.074)*r, z, c);\n"
    "  s += sunTap(uv, float2(-0.696, 0.457)*r, z, c); s += sunTap(uv, float2(-0.203, 0.621)*r, z, c);\n"
    "  s += sunTap(uv, float2( 0.962,-0.195)*r, z, c); s += sunTap(uv, float2( 0.473,-0.480)*r, z, c);\n"
    "  s += sunTap(uv, float2( 0.519, 0.767)*r, z, c); s += sunTap(uv, float2( 0.185,-0.893)*r, z, c);\n"
    "  s += sunTap(uv, float2( 0.507, 0.064)*r, z, c); s += sunTap(uv, float2( 0.896, 0.412)*r, z, c);\n"
    "  s += sunTap(uv, float2(-0.322,-0.933)*r, z, c); s += sunTap(uv, float2(-0.792,-0.598)*r, z, c);\n"
    "  return s / 12.0;\n"
    "}\n"
    "float shadowFactor(float3 wp, float3 n){\n"     // sun: 0 = shadowed .. 1 = lit
    "  if (uShadow.x < 0.5) return 1.0;\n"
    "  int cnt = (int)(uShadow.y + 0.5);\n"
    "  [loop] for (int ci = 0; ci < 4; ci++) { if (ci >= cnt) break;\n"
    "    float d = length(wp - uCasSphere[ci].xyz); float R = uCasSphere[ci].w;\n"
    "    if (d >= R) continue;\n"                                         // tightest cascade holding the point
    "    float s = sunCascade(ci, wp, n);\n"
    "    float edge = R * 0.82;\n"                                        // blend into the next cascade (no seam)
    "    if (d > edge) { float f = (d - edge) / (R - edge);\n"
    "      s = lerp(s, (ci + 1 < cnt) ? sunCascade(ci + 1, wp, n) : 1.0, f); }\n"
    "    return 1.0 - uShadow2.y * (1.0 - s);\n"
    "  }\n"
    "  return 1.0;\n"
    "}\n"
    "float localTap(float2 org, float2 uv, float2 o, float z){\n"
    "  float e = 1.0 / uShadow2.w;\n"
    "  float2 t = clamp(uv + mul(gRot, o), e, 1.0 - e);\n"
    "  return uLocalTex.SampleCmpLevelZero(uShadowCmp, org + t * 0.25, z);\n"
    "}\n"
    "float localShadow(int li, float3 lpos, float ltype, float3 wp, float3 n, float3 l, float dist){\n"
    "  float4 info = uLShadow[li];\n"
    "  float tw = dist * info.w;\n"                                      // world size of a shadow texel here
    "  float3 p = wp + n * (tw * (1.5 + 0.6 * uShadow.w) * info.z) + l * (tw * info.z);\n"
    "  float tile = info.x;\n"
    "  if (ltype < 1.5 || ltype > 2.5) {\n"                              // point / area: pick the cube face
    "    float3 v = p - lpos; float3 a = abs(v);\n"
    "    if (a.x >= a.y && a.x >= a.z) tile += (v.x > 0.0) ? 0.0 : 1.0;\n"
    "    else if (a.y >= a.z) tile += (v.y > 0.0) ? 2.0 : 3.0;\n"
    "    else tile += (v.z > 0.0) ? 4.0 : 5.0;\n"
    "  }\n"
    "  float4 sc = mul(uLocalVP[(int)(tile + 0.5)], float4(p, 1.0));\n"
    "  if (sc.w <= 1e-5) return 1.0;\n"
    "  float3 q = sc.xyz / sc.w;\n"
    "  float2 uv = float2(q.x * 0.5 + 0.5, -q.y * 0.5 + 0.5); float z = q.z * 0.5 + 0.5;\n"
    "  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || z >= 1.0) return 1.0;\n"
    "  float2 org = float2(fmod(tile, 4.0), floor(tile / 4.0 + 0.01)) * 0.25;\n"
    "  float r = uShadow.w / uShadow2.w; float s = 0.0;\n"
    "  s += localTap(org, uv, float2(-0.326,-0.406)*r, z); s += localTap(org, uv, float2(-0.840,-0.074)*r, z);\n"
    "  s += localTap(org, uv, float2(-0.696, 0.457)*r, z); s += localTap(org, uv, float2(-0.203, 0.621)*r, z);\n"
    "  s += localTap(org, uv, float2( 0.962,-0.195)*r, z); s += localTap(org, uv, float2( 0.473,-0.480)*r, z);\n"
    "  s += localTap(org, uv, float2( 0.519, 0.767)*r, z); s += localTap(org, uv, float2( 0.185,-0.893)*r, z);\n"
    "  s += localTap(org, uv, float2( 0.507, 0.064)*r, z); s += localTap(org, uv, float2( 0.896, 0.412)*r, z);\n"
    "  s += localTap(org, uv, float2(-0.322,-0.933)*r, z); s += localTap(org, uv, float2(-0.792,-0.598)*r, z);\n"
    "  return 1.0 - info.y * (1.0 - s / 12.0);\n"
    "}\n"
    // ACES filmic curve (matches the GL renderer) for the linear/filmic pipeline.
    "float3 aces(float3 x){\n"
    "  return saturate((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14));\n"
    "}\n"
    "float4 VSDepth(float3 pos:POSITION):SV_POSITION {\n"          // depth-only shadow pass
    "  float4 cp = mul(uMVP, float4(pos,1.0)); cp.z = (cp.z + cp.w) * 0.5; return cp;\n"
    "}\n"
    "struct VSIn { float3 pos:POSITION; float3 nrm:NORMAL; float2 uv:TEXCOORD; float3 col:COLOR; float3 tan:TANGENT; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float3 nrm:NORMAL; float3 world:TEXCOORD1; float2 uv:TEXCOORD0; float3 col:COLOR; float3 tan:TEXCOORD2; };\n"
    "PSIn VSMain(VSIn i){\n"
    "  PSIn o;\n"
    "  float4 cp = mul(uMVP, float4(i.pos,1.0));\n"
    "  cp.z = (cp.z + cp.w) * 0.5;\n"        // GL [-1,1] -> D3D [0,1]
    "  o.pos = cp;\n"
    "  o.nrm = mul((float3x3)uModel, i.nrm);\n"
    "  o.tan = mul((float3x3)uModel, i.tan);\n"
    "  o.world = mul(uModel, float4(i.pos,1.0)).xyz;\n"
    "  o.uv = i.uv * uTiling + uPbr2.zw;\n"
    "  o.col = i.col;\n"
    "  return o;\n"
    "}\n"
    // ---- GPU skinning: up to 256 joints as 3x4 affine rows (b1) ----
    "cbuffer Bones : register(b1) { float4 uBones[768]; };\n"
    "float3x4 skinMat(float4 j, float4 w){\n"
    "  float sw = w.x + w.y + w.z + w.w;\n"
    "  if (sw < 1e-6) return float3x4(1,0,0,0, 0,1,0,0, 0,0,1,0);\n"   // unweighted: bind pose
    "  int4 b = (int4)(j + 0.5) * 3;\n"
    "  float3x4 m = w.x * float3x4(uBones[b.x], uBones[b.x+1], uBones[b.x+2]);\n"
    "  m += w.y * float3x4(uBones[b.y], uBones[b.y+1], uBones[b.y+2]);\n"
    "  m += w.z * float3x4(uBones[b.z], uBones[b.z+1], uBones[b.z+2]);\n"
    "  m += w.w * float3x4(uBones[b.w], uBones[b.w+1], uBones[b.w+2]);\n"
    "  return m / sw;\n"
    "}\n"
    "struct VSInSkin { float3 pos:POSITION; float3 nrm:NORMAL; float2 uv:TEXCOORD; float3 col:COLOR; float3 tan:TANGENT;\n"
    "                  float4 bj:BLENDINDICES; float4 bw:BLENDWEIGHT; };\n"
    "PSIn VSSkin(VSInSkin i){\n"
    "  float3x4 m = skinMat(i.bj, i.bw);\n"
    "  VSIn v; v.pos = mul(m, float4(i.pos,1.0)); v.nrm = normalize(mul((float3x3)m, i.nrm));\n"
    "  v.tan = mul((float3x3)m, i.tan); v.uv = i.uv; v.col = i.col;\n"
    "  return VSMain(v);\n"
    "}\n"
    "float4 VSDepthSkin(float3 pos:POSITION, float4 bj:BLENDINDICES, float4 bw:BLENDWEIGHT):SV_POSITION {\n"
    "  return VSDepth(mul(skinMat(bj, bw), float4(pos,1.0)));\n"
    "}\n"
    "float4 PSMain(PSIn i):SV_TARGET {\n"
    "  if (uShadowAlpha >= 0.0) return float4(0,0,0,uShadowAlpha);\n"   // flat ground contact shadow
    "  float3 N = normalize(i.nrm);\n"
    "  if (uHasNormal > 0.5) {\n"                                       // perturb N by the tangent-space normal map
    "    float3 tn = uNormalTex.Sample(uSamp, i.uv).rgb * 2.0 - 1.0;\n"
    "    tn.xy *= uNormalStrength;\n"
    "    float3 Tn = i.tan - N * dot(N, i.tan);\n"                      // Gram-Schmidt orthonormalize
    "    if (length(Tn) > 1e-5) {\n"
    "      Tn = normalize(Tn); float3 Bn = cross(N, Tn);\n"
    "      float3 pn = Tn * tn.x + Bn * tn.y + N * tn.z;\n"
    "      if (length(pn) > 1e-5) N = normalize(pn);\n"
    "    }\n"
    "  }\n"
    "  float3 base = uColor * i.col;\n"      // per-vertex/face color (white when unused)
    "  if (uUseTex > 0.5) base *= uTex.Sample(uSamp, i.uv).rgb;\n"
    "  float3 Vv = normalize(uEye - i.world);\n"
    "  float fz = 1.0 - max(dot(N, Vv), 0.0);\n"               // Fresnel term (grazing)
    "  if (uShaderMode > 2.5 && uShaderMode < 3.5)\n"           // Gradient
    "    base = lerp(uGradBot, uGradTop, saturate(N.y * 0.5 + 0.5));\n"
    "  bool fres = uShaderMode > 3.5 && uShaderMode < 4.5;\n"   // Fresnel
    "  if (fres) base *= 0.10;\n"
    "  if (uShaderMode > 4.5 && uShaderMode < 5.5) {\n"         // Iridescent (oil-slick)
    "    float ph = fz * 6.2831853;\n"
    "    base *= float3(0.5+0.5*cos(ph), 0.5+0.5*cos(ph+2.0944), 0.5+0.5*cos(ph+4.1888));\n"
    "  }\n"
    "  bool holo = uShaderMode > 5.5 && uShaderMode < 6.5;\n"   // Hologram
    "  if (holo) base *= 0.18 * (0.55 + 0.45 * sin(i.world.y * 40.0));\n"
    "  if (uShaderMode > 6.5) base = floor(base * 5.0) / 5.0;\n"  // Posterize (retro banding)
    // Filmic mode (uFogCol.w): gamma-correct pipeline — decode sRGB albedo/emissive
    // to linear, light in linear, ACES + sRGB-encode at the end (matches GL).
    "  bool lin = uFogCol.w > 0.5;\n"
    "  if (lin) base = pow(max(base, 0.0), 2.2);\n"
    "  float3 emis = uEmissive;\n"
    "  if (uLightCount.z > 0.5) emis *= uEmisTex.Sample(uSamp, i.uv).rgb;\n"
    "  if (lin) emis = pow(max(emis, 0.0), 2.2);\n"
    "  float shiny = uShininess; float metalK = uPbr.x; float specK = uSpecular;\n"
    "  if (uLightCount.y > 0.5) {\n"
    "    float4 mr = uMrTex.Sample(uSamp, i.uv);\n"
    "    metalK *= mr.b;\n"
    "    float rough0 = saturate(1.0 - (uShininess - 4.0) / 124.0);\n"
    "    float rough = saturate(rough0 * mr.g);\n"
    "    shiny = 4.0 + (1.0 - rough) * 124.0;\n"
    "    specK *= clamp(1.0 - rough, 0.04, 1.0);\n"
    "  }\n"
    "  if (uUnlit > 0.5) { float3 uc = base + emis;\n"            // decode+encode cancel: unlit unchanged
    "    if (lin) uc = pow(max(uc, 0.0), 1.0/2.2);\n"
    "    return float4(uc, uAlpha); }\n"
    "  bool toon = uShaderMode > 1.5 && uShaderMode < 2.5 && uToonBands > 0.5;\n"
    "  float3 amb = lerp(uAmbGround.xyz, uAmbSky.xyz, saturate(N.y * 0.5 + 0.5));\n"  // scene Ambient Mode
    "  float3 lit = amb; float3 spec = float3(0,0,0);\n"
    "  { float a = 52.9829189 * frac(dot(i.pos.xy, float2(0.06711056, 0.00583715))) * 6.2831853;\n"
    "    float ca = cos(a), sa = sin(a); gRot = float2x2(ca, -sa, sa, ca); }\n"   // interleaved-gradient noise rotation
    "  float3 Ns = normalize(i.nrm);\n"                         // geometric normal for shadow bias
    "  bool recv = uFog.w > 0.5;\n"                             // MeshRenderer::receiveShadows
    "  float sh = recv ? shadowFactor(i.world, Ns) : 1.0;\n"    // sun occlusion (real cast shadows)
    "  int lc = (int)uLightCount.x;\n"
    "  if (lc < 1) {\n"                                          // fallback: global directional
    "    float ndl = max(dot(N, uLightDir), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "    lit += uLightColor * ndl * sh;\n"
    "    if (uSpecular > 0.0) { float3 H = normalize(uLightDir + Vv);\n"
    "      spec = pow(max(dot(N,H),0.0), max(shiny,1.0)) * specK * sh; }\n"
    "  } else {\n"
    "    [loop] for (int li = 0; li < 16; li++) { if (li >= lc) break;\n"   // directional + point + spot + area
    "      float4 d0 = uLights[li*4+0]; float4 d1 = uLights[li*4+1];\n"
    "      float4 d2 = uLights[li*4+2]; float4 d3 = uLights[li*4+3];\n"
    "      float3 ld; float at = 1.0; float wrap = 0.0; float dist = 0.0;\n"
    "      if (d0.w < 0.5) { ld = normalize(-d0.xyz); }\n"             // directional
    "      else { float3 toL = d1.xyz - i.world; dist = length(toL); ld = toL / max(dist, 1e-4);\n"
    "        float rng = max(d1.w, 1e-4);\n"
    "        if (d3.y > 0.5) { float dr = dist / rng; float win = max(1.0 - dr*dr*dr*dr, 0.0); win *= win;\n"   // physical inverse-square, windowed
    "          at = win / (1.0 + 8.0 * dist * dist / (rng * rng)); }\n"
    "        else { at = max(1.0 - dist / rng, 0.0); at *= at; }\n"           // linear rolloff
    "        if (d0.w > 2.5) { wrap = 0.5; }\n"                               // area: soft wrap fill
    "        else if (d0.w > 1.5) { float cs = dot(normalize(d0.xyz), -ld);\n"   // spot cone
    "          float dn = d3.x - d2.w; float sp = dn > 1e-4 ? saturate((cs - d2.w) / dn) : (cs >= d2.w ? 1.0 : 0.0);\n"
    "          at *= sp * sp; } }\n"
    "      float ndl = max((dot(N, ld) + wrap) / (1.0 + wrap), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "      if (ndl * at <= 0.0) continue;\n"                     // no light reaches here: skip the shadow lookup
    "      float lsh = 1.0;\n"
    "      if (d0.w < 0.5) { if (abs((float)li - uShadow2.x) < 0.5) lsh = sh; }\n"   // only the main sun has cascades
    "      else if (recv && uLShadow[li].x > -0.5) lsh = localShadow(li, d1.xyz, d0.w, i.world, Ns, ld, dist);\n"
    "      lit += d2.xyz * ndl * at * lsh;\n"
    "      if (uSpecular > 0.0) { float3 H = normalize(ld + Vv);\n"   // highlights take the light's colour + intensity
    "        spec += d2.xyz * (pow(max(dot(N,H),0.0), max(shiny,1.0)) * specK * at * lsh); }\n"
    "    }\n"
    "  }\n"
    "  if (uShaderMode > 7.5) lit += fz * fz * lit * 0.6;\n"      // Velvet: fuzzy grazing sheen
    "  float gloss = 1.0;\n"                                      // gloss/specular map
    "  if (uPbr.z > 0.5) gloss = dot(uSpecTex.Sample(uSamp, i.uv).rgb, float3(0.2126, 0.7152, 0.0722));\n"
    "  if (uPbr.w > 0.5) {\n"                                     // ambient-occlusion map
    "    float ao = dot(uAoTex.Sample(uSamp, i.uv).rgb, float3(0.2126, 0.7152, 0.0722));\n"
    "    lit *= (1.0 - uPbr2.x * (1.0 - ao));\n"
    "  }\n"
    "  float metal = saturate(metalK);\n"                       // metalness
    "  float diffK = 1.0 - 0.9 * metal;\n"
    "  float3 f0 = lerp(float3(1,1,1), base, metal);\n"
    "  float3 diff = base * lit * diffK;\n"
    "  float3 rim = float3(0,0,0);\n"
    "  float rs = uRimStr; if ((fres || holo) && rs < 0.8) rs = 1.6;\n"
    "  if (rs > 0.0) rim = uRimColor * (fz*fz*fz * rs);\n"
    "  float3 col = diff + (spec * gloss) * f0 + rim + emis;\n"
    "  float reflAmt = max(uPbr.y, metal);\n"                     // env reflection of the sky gradient
    "  float reflK = reflAmt * gloss;\n"
    "  if (reflK > 0.0 && uPbr2.y > 0.5) {\n"
    "    float ndv = max(dot(N, Vv), 0.0);\n"
    "    float3 R = reflect(-Vv, N); float ry = clamp(R.y, -1.0, 1.0);\n"
    "    float3 env = lerp(uSky[1].xyz, ry >= 0.0 ? uSky[0].xyz : uSky[2].xyz, abs(ry));\n"
    "    if (lin) env = pow(max(env, 0.0), 2.2);\n"
    "    float fr2 = 1.0 - ndv; fr2 = fr2*fr2*fr2*fr2*fr2;\n"     // Schlick
    "    float kk = reflK + (1.0 - reflK) * fr2;\n"
    "    col = col * (1.0 - kk) + env * f0 * kk;\n"
    "  }\n"
    "  if (uFog.x > 0.5) {\n"                                    // fog (Atmosphere.hpp FogFactor, same maths)
    "    float fd = length(uEye - i.world);\n"
    "    float ff;\n"
    "    if (uFog2.x < 0.5) ff = saturate((fd - uFog.y) / max(uFog.z - uFog.y, 1e-3));\n"   // linear
    "    else if (uFog2.x < 1.5) ff = 1.0 - exp(-uFog2.y * fd);\n"                            // exponential
    "    else { float te = exp(-max(uEye.y - uFog2.z, 0.0) * uFog2.w);\n"                      // height: thins above fogHeight
    "           float tw = exp(-max(i.world.y - uFog2.z, 0.0) * uFog2.w);\n"
    "           ff = 1.0 - exp(-uFog2.y * fd * 0.5 * (te + tw)); }\n"
    "    float3 fc = uFogCol.xyz;\n"
    "    if (lin) fc = pow(max(fc, 0.0), 2.2);\n"                 // fog is authored sRGB too
    "    col = lerp(col, fc, ff);\n"
    "  }\n"
    // Filmic output: ACES roll-off in linear light, then encode back to sRGB. With
    // the post chain running the colour stays linear: the chain exposes, tonemaps
    // and encodes it.
    "  if (lin && uPost.x < 0.5) { col = aces(col); col = pow(max(col, 0.0), 1.0/2.2); }\n"
    "  return float4(col, uAlpha);\n"
    "}\n"
    // ---- Procedural sky + the post chain (mirrors the GLSL in GLRenderer.cpp) ----
    "cbuffer PC : register(b3) {\n"
    "  float4x4 uInvVP;\n"
    "  float4 uEyeSz;\n"                      // xyz eye, w sun disc radius (radians)
    "  float4 uSunDirHalo;\n"                 // xyz toward the sun, w halo
    "  float4 uZenithDisc;\n"                 // xyz zenith colour, w disc visibility
    "  float4 uHorizonHaze;\n"                // xyz horizon colour, w haze
    "  float4 uGroundNight;\n"                // xyz ground colour, w night (0..1)
    "  float4 uSunColorStar;\n"               // xyz sun colour, w star density
    "  float4 uCloud;\n"                      // cover, density, scale, star brightness
    "  float4 uCloudOffTL;\n"                 // xy cloud offset, z time, w linear output
    "  float4 uP0;\n"                         // exposure, linearIn, tonemap, bloomOn
    "  float4 uP1;\n"                         // bloomIntensity, gradeOn, temperature, tint
    "  float4 uP2;\n"                         // saturation, contrast, vignette, vignetteSmooth
    "  float4 uLiftAspect;\n"                 // xyz lift, w aspect
    "  float4 uGammaThresh;\n"                // xyz gamma, w bloom threshold
    "  float4 uGainRadius;\n"                 // xyz gain, w blur / downsample radius
    "  float4 uTexelDir;\n"                   // xy source texel, zw blur direction (uv)
    "  float4 uP7;\n"                         // x SSAO strength, y 1 = multiply the AO in this pass, z SSAO radius (world), w unused
    "  float4 uTopMode;\n"                    // Colour sky: xyz top, w mode (0 colour, 1 procedural)
    "  float4 uHorHp;\n"                      // xyz horizon, w horizon position
    "  float4 uBotStars;\n"                   // xyz bottom, w stars on
    "  float4 uSunPosRad;\n"                  // xy sun position (uv), z radius (of height), w sun on
    "  float4 uViewSize;\n"                   // xy pixels
    "};\n"
    "SamplerState uSampClamp : register(s2);\n"
    "Texture2D uSsaoTex : register(t8);\n"          // the blurred ambient-occlusion factor (SSAO), r = 1 unoccluded
    "struct PSQuad { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
    "PSQuad VSQuad(uint id : SV_VertexID) {\n"                   // one fullscreen triangle, no vertex buffer
    "  PSQuad o; float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0); o.uv = uv; return o;\n"
    "}\n"
    "float hash21(float2 p){ p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y); }\n"
    "float vnoise(float2 p){ float2 i = floor(p), f = frac(p); f = f * f * (3.0 - 2.0 * f);\n"
    "  float a = hash21(i), b = hash21(i + float2(1.0, 0.0)), c = hash21(i + float2(0.0, 1.0)), d = hash21(i + float2(1.0, 1.0));\n"
    "  return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y); }\n"
    "float fbm(float2 p){ float s = 0.0, a = 0.5; [unroll] for (int k = 0; k < 5; k++) { s += a * vnoise(p); p = p * 2.03 + float2(17.1, 9.7); a *= 0.5; } return s; }\n"
    "float4 PSSky(PSQuad i) : SV_TARGET {\n"
    "  if (uTopMode.w < 0.5) {\n"                                     // Colour mode: the host's screen-space gradient, in-frame
    "    float t = i.uv.y; float hp = clamp(uHorHp.w, 0.05, 0.95);\n"
    "    float3 col = t < hp ? lerp(uTopMode.xyz, uHorHp.xyz, t / hp) : lerp(uHorHp.xyz, uBotStars.xyz, (t - hp) / (1.0 - hp));\n"
    "    float hz = 1.0 - abs(t - hp) / 0.10;\n"
    "    if (hz > 0.0) col = lerp(col, min(uHorHp.xyz * float3(1.15, 1.15, 1.10) + float3(0.12, 0.12, 0.10), 1.0), hz * 0.5);\n"
    "    if (uBotStars.w > 0.5 && t < hp) {\n"
    "      float2 g = i.uv * uViewSize.xy / 22.0; float2 cell = floor(g), cf = frac(g);\n"
    "      float h = hash21(cell); float2 sp = float2(hash21(cell + 3.1), hash21(cell + 9.7)) * 0.8 + 0.1;\n"
    "      col += (h > 1.0 - 0.5 * uSunColorStar.w ? 1.0 : 0.0) * smoothstep(0.12, 0.0, length(cf - sp)) * uCloud.w;\n"
    "    }\n"
    "    if (uSunPosRad.w > 0.5) {\n"
    "      float dp = length((i.uv - uSunPosRad.xy) * uViewSize.xy);\n"
    "      float r = max(uSunPosRad.z, 0.005) * uViewSize.y; float glowR = 0.45 * uViewSize.y;\n"
    "      [unroll] for (int k = 12; k >= 1; --k) { float fr = (float)k / 12.0; if (dp < glowR * fr) col = lerp(col, uSunColorStar.xyz, (34.0 / 255.0) * (1.0 - fr) * (1.0 - fr)); }\n"
    "      [unroll] for (int gi = 4; gi >= 1; --gi) { if (dp < r * (1.0 + (float)gi * 0.7)) col = lerp(col, uSunColorStar.xyz, (40.0 - (float)gi * 6.0) / 255.0); }\n"
    "      if (dp < r) col = uSunColorStar.xyz;\n"
    "    }\n"
    "    if (uCloudOffTL.w > 0.5) col = pow(max(col, 0.0), 2.2);\n"
    "    return float4(col, 1.0);\n"
    "  }\n"
    "  float2 ndc = float2(i.uv.x * 2.0 - 1.0, 1.0 - i.uv.y * 2.0);\n"
    "  float4 p0 = mul(uInvVP, float4(ndc, -1.0, 1.0)); float4 p1 = mul(uInvVP, float4(ndc, 1.0, 1.0));\n"
    "  float3 dir = normalize(p1.xyz / p1.w - p0.xyz / p0.w);\n"
    "  float y = dir.y;\n"
    "  float3 zen = uZenithDisc.xyz, hor = uHorizonHaze.xyz, grd = uGroundNight.xyz, sunC = uSunColorStar.xyz, sunD = uSunDirHalo.xyz;\n"
    "  float haze = uHorizonHaze.w, night = uGroundNight.w, disc = uZenithDisc.w;\n"
    "  float3 col = (y >= 0.0) ? lerp(hor, zen, pow(max(y, 0.0), 0.55)) : lerp(hor, grd, smoothstep(0.0, 0.25, -y));\n"
    "  float3 hazeCol = hor * 1.12 + 0.06;\n"
    "  col = lerp(col, hazeCol, haze * 0.6 * exp(-abs(y) * 5.0));\n"
    "  float3 flatD = normalize(float3(dir.x, 0.0, dir.z) + 1e-5); float3 flatS = normalize(float3(sunD.x, 0.0, sunD.z) + 1e-5);\n"
    "  col += sunC * (0.14 * haze * disc * exp(-abs(y) * 4.0) * pow(max(dot(flatD, flatS), 0.0), 3.0));\n"
    "  float ang = acos(clamp(dot(dir, sunD), -1.0, 1.0));\n"
    "  float dsc = (1.0 - smoothstep(uEyeSz.w * 0.85, uEyeSz.w * 1.15, ang)) * disc;\n"
    "  float halo = uSunDirHalo.w * disc * (0.6 * exp(-ang * 4.0) + 0.3 * exp(-ang * 1.2));\n"
    "  col += sunC * (dsc * 1.2 + halo);\n"
    "  if (night > 0.0 && y > 0.0) {\n"
    "    float az = atan2(dir.z, dir.x); float el = asin(clamp(y, -1.0, 1.0));\n"
    "    float2 sc = float2(az, el) * (64.0 * (0.5 + uSunColorStar.w));\n"
    "    float2 ci = floor(sc), cf = frac(sc);\n"
    "    float h = hash21(ci);\n"
    "    float2 sp = float2(hash21(ci + 7.3), hash21(ci + 13.7)) * 0.8 + 0.1;\n"
    "    float d = length(cf - sp);\n"
    "    float star = (h > 1.0 - 0.35 * uSunColorStar.w ? 1.0 : 0.0) * smoothstep(0.09, 0.0, d);\n"
    "    float tw = 0.7 + 0.3 * sin(uCloudOffTL.z * 3.0 + h * 40.0);\n"
    "    col += star * uCloud.w * night * tw * smoothstep(0.0, 0.15, y);\n"
    "  }\n"
    "  if (uCloud.x > 0.0 && y > 0.01) {\n"
    "    float2 uv = dir.xz / (y + 0.12) * (0.6 / max(uCloud.z, 0.05)) + uCloudOffTL.xy;\n"
    "    float n = fbm(uv);\n"
    "    float cov = smoothstep(1.0 - uCloud.x, 1.0 - uCloud.x * 0.6 + 0.05, n);\n"
    "    float lit = saturate(0.5 + (fbm(uv + flatS.xz * 0.06) - n) * 7.0);\n"
    "    float3 bright = lerp(float3(1, 1, 1), sunC, 0.35) * (0.95 - 0.8 * night);\n"
    "    float3 shade = lerp(hor, zen, 0.5) * (0.8 - 0.3 * night);\n"
    "    float3 cc = lerp(shade, bright, lit);\n"
    "    float a = cov * uCloud.y * smoothstep(0.0, 0.2, y);\n"
    "    col = lerp(col, cc, a);\n"
    "  }\n"
    "  if (uCloudOffTL.w > 0.5) col = pow(max(col, 0.0), 2.2);\n"
    "  return float4(col, 1.0);\n"
    "}\n"
    "float aoMix(float2 uv){ return lerp(1.0, uSsaoTex.Sample(uSampClamp, uv).r, saturate(uP7.x)); }\n"
    "float3 brightTap(float2 uv){ float3 c = uTex.Sample(uSampClamp, uv).rgb; if (uP7.y > 0.5) c *= aoMix(uv); float l = dot(c, float3(0.2126, 0.7152, 0.0722));\n"
    "  float k = max(l - uGammaThresh.w, 0.0) / max(l, 1e-4); return c * k; }\n"
    "float4 PSDown(PSQuad i) : SV_TARGET {\n"                      // bright-pass + 2x2 box downsample
    "  float2 o = uTexelDir.xy * uGainRadius.w;\n"
    "  float3 s = brightTap(i.uv + float2(-o.x, -o.y)) + brightTap(i.uv + float2(o.x, -o.y)) + brightTap(i.uv + float2(-o.x, o.y)) + brightTap(i.uv + float2(o.x, o.y));\n"
    "  return float4(s * 0.25, 1.0);\n"
    "}\n"
    "float4 PSBlur(PSQuad i) : SV_TARGET {\n"                      // 9-tap gaussian (linear-sampled 5-tap)
    "  float2 d = uTexelDir.zw;\n"
    "  float3 s = uTex.Sample(uSampClamp, i.uv).rgb * 0.227027;\n"
    "  s += (uTex.Sample(uSampClamp, i.uv + d * 1.384615).rgb + uTex.Sample(uSampClamp, i.uv - d * 1.384615).rgb) * 0.316216;\n"
    "  s += (uTex.Sample(uSampClamp, i.uv + d * 3.230769).rgb + uTex.Sample(uSampClamp, i.uv - d * 3.230769).rgb) * 0.070270;\n"
    "  return float4(s, 1.0);\n"
    "}\n"
    "float3 hable(float3 v){ float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;\n"
    "  return ((v*(A*v+C*B)+D*E)/(v*(A*v+B)+D*F))-E/F; }\n"
    "float3 filmic(float3 x){ return saturate(hable(x * 2.0) / hable(float3(11.2, 11.2, 11.2))); }\n"
    "float4 PSUber(PSQuad i) : SV_TARGET {\n"                      // exposure, bloom, tonemap, grading, vignette
    "  float4 s = uTex.Sample(uSampClamp, i.uv);\n"
    "  float3 c = s.rgb * uP0.x * (uP7.y > 0.5 ? aoMix(i.uv) : 1.0);\n"
    "  float blLum = 0.0;\n"
    "  if (uP0.w > 0.5) {\n"
    "    float3 bl = (uNormalTex.Sample(uSampClamp, i.uv).rgb + uSpecTex.Sample(uSampClamp, i.uv).rgb) * (0.5 * uP1.x * uP0.x);\n"
    "    c += bl; blLum = dot(bl, float3(0.2126, 0.7152, 0.0722));\n"
    "  }\n"
    "  if (uP0.y > 0.5) {\n"
    "    if (uP0.z > 1.5) c = filmic(c); else if (uP0.z > 0.5) c = aces(c); else c = saturate(c);\n"
    "    c = pow(max(c, 0.0), 1.0 / 2.2);\n"
    "  } else c = saturate(c);\n"
    "  if (uP1.y > 0.5) {\n"
    "    c *= float3(1.0 + 0.25 * uP1.z, 1.0 + 0.2 * uP1.w, 1.0 - 0.25 * uP1.z);\n"
    "    float l = dot(c, float3(0.2126, 0.7152, 0.0722));\n"
    "    c = l + (c - l) * uP2.x;\n"
    "    c = (c - 0.5) * uP2.y + 0.5;\n"
    "    c = c * (1.0 + uGainRadius.xyz) + uLiftAspect.xyz * (1.0 - c);\n"
    "    c = pow(max(c, 0.0), 1.0 / max(1.0 + uGammaThresh.xyz * 0.75, 0.1));\n"
    "  }\n"
    "  float cov = saturate(s.a + blLum * 2.0);\n"
    "  float v = 0.0;\n"
    "  if (uP2.z > 0.0) {\n"
    "    float2 d = (i.uv - 0.5) * float2(uLiftAspect.w, 1.0) * 2.0;\n"
    "    float r = length(d) / sqrt(uLiftAspect.w * uLiftAspect.w + 1.0);\n"
    "    v = 0.8 * uP2.z * smoothstep(max(1.0 - uP2.w * 1.3, 0.0), 1.05, r);\n"
    "  }\n"
    "  float3 pc = c * cov * (1.0 - v);\n"
    "  float oa = 1.0 - (1.0 - v) * (1.0 - cov);\n"
    "  return float4(oa > 1e-4 ? pc / oa : float3(0, 0, 0), oa);\n"
    "}\n"
    "float lumaF(float3 c){ return dot(c, float3(0.299, 0.587, 0.114)); }\n"
    "float4 PSFxaa(PSQuad i) : SV_TARGET {\n"
    "  float2 px = uTexelDir.xy;\n"
    "  float4 c = uTex.Sample(uSampClamp, i.uv);\n"
    "  float lM = lumaF(c.rgb);\n"
    "  float lNW = lumaF(uTex.Sample(uSampClamp, i.uv + float2(-1.0, -1.0) * px).rgb);\n"
    "  float lNE = lumaF(uTex.Sample(uSampClamp, i.uv + float2( 1.0, -1.0) * px).rgb);\n"
    "  float lSW = lumaF(uTex.Sample(uSampClamp, i.uv + float2(-1.0,  1.0) * px).rgb);\n"
    "  float lSE = lumaF(uTex.Sample(uSampClamp, i.uv + float2( 1.0,  1.0) * px).rgb);\n"
    "  float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));\n"
    "  float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));\n"
    "  if (lMax - lMin < max(0.0312, lMax * 0.125)) return c;\n"
    "  float2 dir = float2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));\n"
    "  float dirReduce = max((lNW + lNE + lSW + lSE) * (0.25 * 0.125), 1.0 / 128.0);\n"
    "  float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);\n"
    "  dir = clamp(dir * rcpDirMin, -8.0, 8.0) * px;\n"
    "  float4 rgbA = 0.5 * (uTex.Sample(uSampClamp, i.uv + dir * (1.0 / 3.0 - 0.5)) + uTex.Sample(uSampClamp, i.uv + dir * (2.0 / 3.0 - 0.5)));\n"
    "  float4 rgbB = rgbA * 0.5 + 0.25 * (uTex.Sample(uSampClamp, i.uv + dir * -0.5) + uTex.Sample(uSampClamp, i.uv + dir * 0.5));\n"
    "  float lB = lumaF(rgbB.rgb);\n"
    "  return (lB < lMin || lB > lMax) ? rgbA : rgbB;\n"
    "}\n"
    // ---- SSAO (mirrors GLRenderer.cpp's kSsaoDepthFrag / kSsaoFrag) ----
    // Opaque geometry is drawn at half resolution as PACKED LINEAR DEPTH (camera distance
    // / 150, 16 bits across the R and G channels of an RGBA8 target), a golden-angle spiral
    // of 12 depth-difference taps estimates the occlusion, PSBlur smooths it (two passes),
    // and PSDown / PSUber multiply it into the frame (before the bloom bright pass).
    "static const float kAoFar = 150.0;\n"
    "struct AoV { float4 pos : SV_POSITION; float3 world : TEXCOORD0; };\n"
    "AoV VSAoDepth(float3 pos : POSITION){\n"
    "  AoV o; float4 cp = mul(uMVP, float4(pos, 1.0)); cp.z = (cp.z + cp.w) * 0.5;\n"
    "  o.pos = cp; o.world = mul(uModel, float4(pos, 1.0)).xyz; return o;\n"
    "}\n"
    "AoV VSAoDepthSkin(float3 pos : POSITION, float4 bj : BLENDINDICES, float4 bw : BLENDWEIGHT){\n"
    "  return VSAoDepth(mul(skinMat(bj, bw), float4(pos, 1.0)));\n"
    "}\n"
    "float4 PSAoDepth(AoV i) : SV_TARGET {\n"
    "  float d = saturate(length(uEye - i.world) / kAoFar);\n"
    "  float hi = floor(d * 255.0) / 255.0;\n"                       // 16-bit pack in R + G
    "  return float4(hi, frac(d * 255.0), 0.0, 1.0);\n"
    "}\n"
    "float aoDepthAt(float2 uv){\n"                                   // exact texel read: no filtering of a packed value
    "  float2 sz = 1.0 / uTexelDir.xy;\n"
    "  int2 px = (int2)clamp(uv * sz, float2(0.0, 0.0), sz - 1.0);\n"
    "  float4 t = uTex.Load(int3(px, 0));\n"
    "  return t.r + t.g / 255.0;\n"
    "}\n"
    "float4 PSAo(PSQuad i) : SV_TARGET {\n"
    "  float d = aoDepthAt(i.uv);\n"
    "  if (d >= 0.995) return float4(1.0, 1.0, 1.0, 1.0);\n"          // background / sky
    "  float radius = uP7.z;\n"
    "  float dist = max(d * kAoFar, 0.5);\n"
    "  float sr = clamp(radius / dist, 0.004, 0.12);\n"               // world radius -> screen UV (approx)
    "  float range = radius * 1.6;\n"
    "  float occ = 0.0;\n"
    "  [unroll] for (int k = 0; k < 12; k++) {\n"
    "    float a = (float)k * 2.618034;\n"                              // golden-angle spiral
    "    float rr = sr * (0.25 + 0.75 * (float)k / 11.0);\n"
    "    float2 o = float2(cos(a), sin(a)) * rr;\n"
    "    o.x *= uTexelDir.x / uTexelDir.y;\n"                           // keep the disc round
    "    float diffW = (d - aoDepthAt(i.uv + o)) * kAoFar;\n"           // + = sample nearer (occluder)
    "    if (diffW > 0.03 && diffW < range) occ += 1.0 - diffW / range;\n"
    "  }\n"
    "  float ao = 1.0 - occ / 12.0;\n"
    "  return float4(ao, ao, ao, 1.0);\n"
    "}\n";

/// One compiled entry point: its name and the D3DCompile target profile.
struct Entry { const char* name; const char* profile; };
inline constexpr Entry kEntries[] = {
    {"VSMain", "vs_4_0"}, {"VSDepth", "vs_4_0"}, {"VSSkin", "vs_4_0"}, {"VSDepthSkin", "vs_4_0"}, {"VSQuad", "vs_4_0"},
    {"PSMain", "ps_4_0"}, {"PSSky", "ps_4_0"}, {"PSDown", "ps_4_0"}, {"PSBlur", "ps_4_0"}, {"PSUber", "ps_4_0"}, {"PSFxaa", "ps_4_0"},
    {"VSAoDepth", "vs_4_0"}, {"VSAoDepthSkin", "vs_4_0"}, {"PSAoDepth", "ps_4_0"}, {"PSAo", "ps_4_0"},
};
/// The SSAO constants the HLSL hard-codes, mirrored from GLRenderer.cpp (the test keeps them equal).
inline constexpr float kAoFar = 150.0f;   // linear-depth range; AO fades out beyond this
inline constexpr int kAoSamples = 12;

} // namespace d3d11hlsl
} // namespace okay
