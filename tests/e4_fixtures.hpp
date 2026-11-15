#pragma once
// Synthetic fixtures for the own OpenGEX and MDL readers (batch E4). Everything is built in memory from the structure of the
// two formats as documented, so no binary file is committed; the expected values in the tests are worked out by hand from the
// numbers written here. Real .ogex / .mdl files are not in this repository: the readers are unverified against real files.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace e4 {

// ---- OpenGEX (text) ------------------------------------------------------------------------------------------------------
// One quad (two triangles) with normals and texture coordinates, a material with a texture, a unit scale of 2 and a Z-up scene.
//   positions (0,0,0) (1,0,0) (0,1,0) (1,1,0), triangles (0,1,2) (1,3,2)
//   root node "root" at (1,2,3), its child GeometryNode "quad" at (4,5,6)
inline std::string OgexQuad() {
    return R"(// a quad
Metric (key = "distance") {float {2.0}}
Metric (key = "up") {string {"z"}}
Material $mat1
{
	Name {string {"red"}}
	Color (attrib = "diffuse") {float[3] {{1, 0, 0}}}
	Param (attrib = "specular_power") {float {32}}
	Texture (attrib = "diffuse") {string {"tex/red.png"}}
}
GeometryObject $geo1
{
	Mesh (primitive = "triangles")
	{
		VertexArray (attrib = "position") {float[3] {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}}}
		VertexArray (attrib = "normal") {float[3] {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}}}
		VertexArray (attrib = "texcoord") {float[2] {{0, 0}, {1, 0}, {0, 1}, {1, 1}}}
		IndexArray (material = 0) {unsigned_int32[3] {{0, 1, 2}, {1, 3, 2}}}
	}
}
Node $root
{
	Name {string {"root"}}
	Translation (kind = "xyz") {float[3] {{1, 2, 3}}}
	GeometryNode $n1
	{
		Name {string {"quad"}}
		Translation (kind = "xyz") {float[3] {{4, 5, 6}}}
		ObjectRef {ref {$geo1}}
		MaterialRef (index = 0) {ref {$mat1}}
	}
}
)";
}

// A mesh with `prim` as the primitive and the given index array text (the part after the opening brace of the IndexArray data).
inline std::string OgexMesh(const std::string& primitive, const std::string& indexArrayProps, const std::string& indexData, int verts = 4) {
    std::string pos = "float[3] {";
    for (int i = 0; i < verts; ++i) pos += std::string(i ? ", " : "") + "{" + std::to_string(i) + ", " + std::to_string(i * i) + ", 0}";
    pos += "}";
    return "GeometryObject $g {Mesh (primitive = \"" + primitive + "\") {VertexArray (attrib = \"position\") {" + pos + "} IndexArray " + indexArrayProps + " {" + indexData + "}}}\n"
           "GeometryNode $n {Name {string {\"n\"}} ObjectRef {ref {$g}}}\n";
}

// A two-bone skin on one triangle (0,0,0) (1,0,0) (0,1,0): bone b0 at the origin, bone b1 its child at (0,1,0); counts 1, 2, 1;
// the second vertex is 25 percent bone 0 and 75 percent bone 1. `skinTransform` is the text of the skin's Transform values.
inline std::string OgexSkin(const std::string& skinTransform = "1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1") {
    return "GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0}}} IndexArray {unsigned_int32[3] {{0,1,2}}}\n"
           "  Skin {Transform {float[16] {{" + skinTransform + "}}} BoneRefArray {ref {$b0, $b1}} BoneCountArray {unsigned_int8 {1, 2, 1}}\n"
           "    BoneIndexArray {unsigned_int8 {0, 0, 1, 1}} BoneWeightArray {float {1.0, 0.25, 0.75, 1.0}}}}}\n"
           "BoneNode $b0 {Name {string {\"b0\"}} BoneNode $b1 {Name {string {\"b1\"}} Translation {float[3] {{0, 1, 0}}}}}\n"
           "GeometryNode $n {Name {string {\"skin\"}} ObjectRef {ref {$g}}}\n";
}

// An animated node: its translation goes (0,0,0) to (2,4,6) over keys 0 and 1, its rotation about Z is 0, 1.0 and 2.0 radians at
// 0, 0.5 and 1 (so the key times are {0, 0.5, 1}); a second clip (index 1) moves a node in Y. Clip 0 is named "walk".
inline std::string OgexAnim() {
    return R"(Clip (index = 0) {Name {string {"walk"}}}
Node $a
{
	Name {string {"anim"}}
	Translation %t (kind = "xyz") {float[3] {{0, 0, 0}}}
	Rotation %r (kind = "z") {float {0}}
	Animation (clip = 0)
	{
		Track (target = %t)
		{
			Time (curve = "linear") {Key {float {0, 1}}}
			Value (curve = "linear") {Key {float[3] {{0, 0, 0}, {2, 4, 6}}}}
		}
		Track (target = %r)
		{
			Time (curve = "linear") {Key {float {0, 0.5, 1}}}
			Value (curve = "linear") {Key {float {0, 1.0, 2.0}}}
		}
	}
	Animation (clip = 1)
	{
		Track (target = %t)
		{
			Time {Key {float {0, 2}}}
			Value {Key {float[3] {{0, 0, 0}, {0, 8, 0}}}}
		}
	}
}
GeometryObject $g {Mesh {VertexArray (attrib = "position") {float[3] {{0,0,0},{1,0,0},{0,1,0}}} IndexArray {unsigned_int8[3] {{0,1,2}}}}}
GeometryNode $n {Name {string {"m"}} ObjectRef {ref {$g}}}
)";
}

// ---- helpers for binary fixtures -----------------------------------------------------------------------------------------
struct Buf {
    std::string s;
    void Pad(std::size_t n) { if (s.size() < n) s.resize(n, '\0'); }
    void U8At(std::size_t o, unsigned v) { Pad(o + 1); s[o] = (char)v; }
    void U16At(std::size_t o, unsigned v) { Pad(o + 2); for (int i = 0; i < 2; ++i) s[o + (std::size_t)i] = (char)((v >> (8 * i)) & 255); }
    void I32At(std::size_t o, std::int32_t v) { Pad(o + 4); const std::uint32_t u = (std::uint32_t)v; for (int i = 0; i < 4; ++i) s[o + (std::size_t)i] = (char)((u >> (8 * i)) & 255); }
    void F32At(std::size_t o, float f) { std::uint32_t u; std::memcpy(&u, &f, 4); I32At(o, (std::int32_t)u); }
    void StrAt(std::size_t o, const std::string& t, std::size_t len) { Pad(o + len); for (std::size_t i = 0; i < len && i < t.size(); ++i) s[o + i] = t[i]; }
};

// ---- Quake 1 MDL ---------------------------------------------------------------------------------------------------------
// scale (1,2,3), origin (10,20,30); one 4 x 2 skin; 4 vertices; 2 triangles; first frame simple, second a group of two.
//   vertices (bytes) (0,0,0) (1,0,0) (0,1,0) (1,1,1)  ->  (10,20,30) (11,20,30) (10,22,30) (11,22,33)
//   texture coordinates (onseam, s, t): v0 (0,0,0), v1 (0,3,0), v2 (1,0,1), v3 (0,3,1)
//   triangle 0: facing front, vertices 0 1 2;   triangle 1: facing back, vertices 1 3 2
struct QuakeOffsets { std::size_t skins, st, tris, frames, end; };
inline std::string QuakeMdl(QuakeOffsets* off = nullptr, bool groupFirst = false) {
    Buf b;
    b.StrAt(0, "IDPO", 4);
    b.I32At(4, 6);
    const float scale[3] = {1, 2, 3}, trans[3] = {10, 20, 30};
    for (int k = 0; k < 3; ++k) { b.F32At(8 + 4 * (std::size_t)k, scale[k]); b.F32At(20 + 4 * (std::size_t)k, trans[k]); }
    b.F32At(32, 5.0f);
    b.I32At(48, 1);    // numskins
    b.I32At(52, 4);    // skinwidth
    b.I32At(56, 2);    // skinheight
    b.I32At(60, 4);    // numverts
    b.I32At(64, 2);    // numtris
    b.I32At(68, 2);    // numframes
    std::size_t p = 84;
    const std::size_t skins = p;
    b.I32At(p, 0); p += 4;                  // single skin
    b.Pad(p + 8); for (int i = 0; i < 8; ++i) b.U8At(p + (std::size_t)i, 10 + (unsigned)i); p += 8;   // 4 x 2 palette indices
    const std::size_t st = p;
    const int sts[4][3] = {{0, 0, 0}, {0, 3, 0}, {1, 0, 1}, {0, 3, 1}};
    for (const auto& v : sts) { for (int k = 0; k < 3; ++k) { b.I32At(p, v[k]); p += 4; } }
    const std::size_t tris = p;
    const int tr[2][4] = {{1, 0, 1, 2}, {0, 1, 3, 2}};
    for (const auto& t : tr) { for (int k = 0; k < 4; ++k) { b.I32At(p, t[k]); p += 4; } }
    const std::size_t frames = p;
    const int vb[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 1}};
    auto simple = [&](const char* name, bool real) {
        b.Pad(p + 8); p += 8;               // bounding box
        b.StrAt(p, name, 16); p += 16;
        for (int v = 0; v < 4; ++v) { for (int k = 0; k < 3; ++k) b.U8At(p + (std::size_t)k, real ? (unsigned)vb[v][k] : 1u); b.U8At(p + 3, 0); p += 4; }
    };
    auto group = [&]() {
        b.I32At(p, 1); p += 4;
        b.I32At(p, 2); p += 4;
        b.Pad(p + 8); p += 8;
        b.F32At(p, 0.1f); b.F32At(p + 4, 0.2f); p += 8;
        simple("run", true);
        simple("run2", false);
    };
    if (groupFirst) { group(); b.I32At(p, 0); p += 4; simple("stand2", false); }
    else { b.I32At(p, 0); p += 4; simple("stand1", true); group(); }
    b.Pad(p);
    if (off) *off = {skins, st, tris, frames, p};
    return b.s;
}

// ---- Half-Life 1 MDL -----------------------------------------------------------------------------------------------------
// Two bones: "root" at (1,2,3), "child" under it at (0,1,0) turned 90 degrees about Z (value[5] = pi/2); one 64 x 32 texture
// "tex.bmp"; one body part with one model of one mesh holding one triangle strip of three vertices:
//   v0 (1,0,0) in bone 1, v1 (0,1,0) in bone 1, v2 (0,0,0) in bone 0; texture coordinates (0,0) (64,0) (0,32)
//   one stored normal, `normalZ`, in bone 0
// and one sequence "walk": 4 frames at 10 per second, bone 1's Z rotation animated: raw values 100, 100, 300, 400 (two runs),
// scale 0.01 per raw value. Layout offsets are returned in `o`.
struct HlOffsets { std::size_t bones, tex, skin, body, model, vtx, vinfo, nrm, ninfo, mesh, cmds, seq, anim, rle, end; };
inline std::string HalfLifeMdl(float normalZ = 1.0f, HlOffsets* o = nullptr, bool withSequence = true, const float* rootEuler = nullptr, bool fourthVertex = false) {
    Buf b;
    const double kPi = 3.14159265358979323846;
    b.StrAt(0, "IDST", 4);
    b.I32At(4, 10);
    b.StrAt(8, "test", 64);
    const std::size_t nv = fourthVertex ? 4 : 3;   // the optional fourth vertex is (-1,0,0) in the root bone, i.e. (0,2,3) at the bind pose
    const std::size_t bones = 244, tex = bones + 224, skin = tex + 80, body = skin + 4, model = body + 76, vtx = model + 112, vinfo = vtx + 12 * nv, nrm = vinfo + 4,
                      ninfo = nrm + 12, mesh = ninfo + 4, cmds = mesh + 20, seq = cmds + 4 + 8 * nv, anim = seq + 176, rle = anim + 24, end = rle + 10;
    // header (offsets of the studio header: numbones 140, boneindex 144, numseq 164, seqindex 168, numseqgroups 172, numtextures 180,
    // textureindex 184, numskinref 192, numskinfamilies 196, skinindex 200, numbodyparts 204, bodypartindex 208)
    b.I32At(140, 2); b.I32At(144, (std::int32_t)bones);
    b.I32At(164, withSequence ? 1 : 0); b.I32At(168, (std::int32_t)seq); b.I32At(172, 1);
    b.I32At(180, 1); b.I32At(184, (std::int32_t)tex);
    b.I32At(192, 1); b.I32At(196, 1); b.I32At(200, (std::int32_t)skin);
    b.I32At(204, 1); b.I32At(208, (std::int32_t)body);
    b.Pad(244);
    // bones: name 0, parent 32, flags 36, controllers 40, value[6] 64, scale[6] 88
    b.StrAt(bones, "root", 32); b.I32At(bones + 32, -1);
    b.F32At(bones + 64, 1); b.F32At(bones + 68, 2); b.F32At(bones + 72, 3);
    for (int k = 0; k < 6; ++k) b.F32At(bones + 88 + 4 * (std::size_t)k, 1.0f);
    if (rootEuler) for (int k = 0; k < 3; ++k) b.F32At(bones + 76 + 4 * (std::size_t)k, rootEuler[k]);   // value[3..5] of the root bone
    const std::size_t b1 = bones + 112;
    b.StrAt(b1, "child", 32); b.I32At(b1 + 32, 0);
    b.F32At(b1 + 68, 1.0f);                              // value[1]
    b.F32At(b1 + 84, (float)(kPi / 2));                  // value[5]: a turn of 90 degrees about Z
    for (int k = 0; k < 5; ++k) b.F32At(b1 + 88 + 4 * (std::size_t)k, 1.0f);
    b.F32At(b1 + 108, 0.01f);                            // scale[5]
    // texture
    b.StrAt(tex, "tex.bmp", 64); b.I32At(tex + 68, 64); b.I32At(tex + 72, 32);
    // skin table
    b.U16At(skin, 0);
    // body part: name, nummodels, base, modelindex
    b.StrAt(body, "body", 64); b.I32At(body + 64, 1); b.I32At(body + 68, 0); b.I32At(body + 72, (std::int32_t)model);
    // model
    b.StrAt(model, "m", 64);
    b.I32At(model + 72, 1); b.I32At(model + 76, (std::int32_t)mesh);       // nummesh, meshindex
    b.I32At(model + 80, (std::int32_t)nv); b.I32At(model + 84, (std::int32_t)vinfo); b.I32At(model + 88, (std::int32_t)vtx);     // numverts, vertinfoindex, vertindex
    b.I32At(model + 92, 1); b.I32At(model + 96, (std::int32_t)ninfo); b.I32At(model + 100, (std::int32_t)nrm);   // numnorms, norminfoindex, normindex
    // vertices
    const float v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 0}};
    for (int i = 0; i < 3; ++i) for (int k = 0; k < 3; ++k) b.F32At(vtx + 12 * (std::size_t)i + 4 * (std::size_t)k, v[i][k]);
    b.U8At(vinfo, 1); b.U8At(vinfo + 1, 1); b.U8At(vinfo + 2, 0);
    if (fourthVertex) { b.F32At(vtx + 36, -1.0f); b.U8At(vinfo + 3, 0); }
    b.F32At(nrm + 8, normalZ);
    b.U8At(ninfo, 0);
    // mesh: numtris, triindex, skinref
    b.I32At(mesh, 1); b.I32At(mesh + 4, (std::int32_t)cmds); b.I32At(mesh + 8, 0);
    // triangle commands: a strip of 3
    b.U16At(cmds, (unsigned)nv);
    const int tc[4][2] = {{0, 0}, {64, 0}, {0, 32}, {64, 32}};
    for (std::size_t i = 0; i < nv; ++i) { const std::size_t q = cmds + 2 + 8 * i; b.U16At(q, (unsigned)i); b.U16At(q + 2, 0); b.U16At(q + 4, (unsigned)tc[i][0]); b.U16At(q + 6, (unsigned)tc[i][1]); }
    b.U16At(cmds + 2 + 8 * nv, 0);
    // sequence
    b.StrAt(seq, "walk", 32); b.F32At(seq + 32, 10.0f);
    b.I32At(seq + 56, 4);                              // numframes
    b.I32At(seq + 120, 1);                             // numblends
    b.I32At(seq + 124, (std::int32_t)anim);            // animindex
    b.I32At(seq + 156, 0);                             // seqgroup
    // animation: bone 0 has none, bone 1 has its Z rotation (channel 5) at +12 from its own record
    b.U16At(anim + 12 + 10, (unsigned)(rle - (anim + 12)));
    // runs: valid 1 total 2 [100]; valid 2 total 2 [300, 400]
    b.U8At(rle, 1); b.U8At(rle + 1, 2); b.U16At(rle + 2, 100);
    b.U8At(rle + 4, 2); b.U8At(rle + 5, 2); b.U16At(rle + 6, 300); b.U16At(rle + 8, 400);
    b.Pad(end);
    if (o) *o = {bones, tex, skin, body, model, vtx, vinfo, nrm, ninfo, mesh, cmds, seq, anim, rle, end};
    return b.s;
}

} // namespace e4
