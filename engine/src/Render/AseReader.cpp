// Our own ASE (3ds Max ASCII Scene Export) reader: meshes with materials, the node hierarchy and keyframed node
// animation. Written from the public description of the format and from files that 3ds Max wrote. Bounds-checked, capped,
// no recursion (blocks are tracked on a stack). See okay/Render/ModelData.hpp and docs/own_ase_reader.md.
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace ase {

using namespace detail::txt;

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxDepth     = 64;
constexpr std::size_t kMaxNodes     = 100000;
constexpr std::size_t kMaxMaterials = 20000;
constexpr std::size_t kMaxVerts     = 10000000;   // mesh vertices + texture vertices over all objects
constexpr std::size_t kMaxFaces     = 10000000;
constexpr std::size_t kMaxKeys      = 10000000;
constexpr std::size_t kMaxIncident  = 256;        // faces per vertex that take part in smoothing
constexpr double      kMaxCoord     = 1e15;

struct KeyV { double tick; Vec3 v; };
struct KeyQ { double tick; Vec3 axis; float angle; };

struct Material {
    std::string name, bitmap;
    bool hasDiffuse = false; float diffuse[3] = {0.5f, 0.5f, 0.5f};
    float shine = -1, selfIllum = 0, transparency = 0;
    std::vector<Material> subs;
    int declaredSubs = 0;
};

struct Face { int v[3] = {0, 0, 0}; int tv[3] = {-1, -1, -1}; std::uint32_t sg = 0; int mtl = 0; bool hasTv = false; };

struct Obj {
    enum Kind { Geom, Helper, Camera, Light, Shape } kind = Geom;
    std::string name, parent;
    bool hasTm = false;
    double row[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
    int matRef = -1;
    // mesh
    long long numVertex = -1, numFaces = -1, numTVertex = -1, numTFace = -1;
    std::vector<Vec3> pos;
    std::vector<Vec2> tvert;
    std::vector<Face> faces;
    std::vector<std::array<Vec3, 3>> fnormals;   // per face corner (world space), when the file has MESH_NORMALS
    std::vector<char> fnHas;
    std::vector<Vec3> fnormalFace;
    int curNormalFace = -1;
    // animation
    std::vector<KeyV> posKeys, sclKeys;
    std::vector<KeyQ> rotKeys;
    bool hasMesh = false;
    int extraChannels = 0, vertexColorBlocks = 0;
};

struct Reader {
    std::string err;
    std::string baseDir;
    bool Fail(const std::string& m, std::size_t line = 0) {
        if (err.empty()) err = "ASE: " + m + (line ? " (line " + std::to_string(line) + ")" : "");
        return false;
    }

    int version = 0;
    int frameSpeed = 30, ticksPerFrame = 160;
    long long declaredMaterials = -1;
    std::vector<Material> mats;
    std::vector<Obj> objs;
    std::size_t totalVerts = 0, totalFaces = 0, totalKeys = 0;
    int groups = 0, secondTm = 0, ignoredMaps = 0, uvTransforms = 0, otherKeyKinds = 0, unknownObjects = 0, sgOverflow = 0, shortSamples = 0, singleKeyTracks = 0;
    bool sawHeader = false;

    static bool NumAt(const std::vector<std::string>& tk, std::size_t i, double& v) { return i < tk.size() && ToDouble(tk[i], v); }
    static bool FloatAt(const std::vector<std::string>& tk, std::size_t i, float& v) { return i < tk.size() && ToFloat(tk[i], v); }
    static bool IntAt(const std::vector<std::string>& tk, std::size_t i, long long& v) {
        if (i >= tk.size()) return false;
        std::string t = tk[i];
        if (!t.empty() && t.back() == ':') t.pop_back();
        return ToInt(t, v);
    }
    // "A:" style label followed by a number
    static bool Labeled(const std::vector<std::string>& tk, const char* label, long long& v) {
        for (std::size_t i = 1; i + 1 < tk.size(); ++i) if (tk[i] == label) return ToInt(tk[i + 1], v);
        return false;
    }

    Material* CurMaterial(const std::vector<std::string>& stack) {
        if (mats.empty()) return nullptr;
        Material* m = &mats.back();
        bool sub = false;
        for (const std::string& s : stack) if (s == "SUBMATERIAL") sub = true;
        if (sub) { if (m->subs.empty()) return nullptr; return &m->subs.back(); }
        return m;
    }

    bool Parse(const std::string& text) {
        Lines lines(text);
        std::string raw;
        std::vector<std::string> stack;
        std::string pending;   // a block name whose "{" is on the next line
        Obj* cur = nullptr;
        int curDepth = 0;      // stack size when the current object's block was opened
        std::size_t lineCount = 0;
        while (lines.Next(raw)) {
            ++lineCount;
            const std::size_t ln = lines.number;
            std::vector<std::string> tk = Split(raw);
            if (tk.empty()) continue;
            if (tk.size() == 1 && tk[0] == "{") { if (stack.size() >= kMaxDepth) return Fail("blocks nested too deeply", ln); stack.push_back(pending); pending.clear(); continue; }
            if (tk[0] == "}") {
                if (stack.empty()) return Fail("'}' without an open block", ln);
                if (cur && (int)stack.size() == curDepth) cur = nullptr;
                stack.pop_back();
                // a block closing may share its line with the next keyword: ignore the rest
                continue;
            }
            if (tk[0].empty() || tk[0][0] != '*') { continue; }   // stray text
            const std::string key = tk[0].substr(1);
            const bool opens = tk.back() == "{";
            const std::string top = stack.empty() ? std::string() : stack.back();
            bool handled = true;

            // ---- file header and scene ----
            if (stack.empty()) {
                if (key == "3DSMAX_ASCIIEXPORT") { long long v = 0; IntAt(tk, 1, v); version = (int)v; sawHeader = true; }
                else if (key == "COMMENT") {}
                else if (opens) {}   // top-level blocks handled below
                else handled = false;
            } else if (top == "SCENE") {
                long long v;
                if (key == "SCENE_FRAMESPEED" && IntAt(tk, 1, v) && v > 0 && v < 100000) frameSpeed = (int)v;
                else if (key == "SCENE_TICKSPERFRAME" && IntAt(tk, 1, v) && v > 0 && v < 1000000) ticksPerFrame = (int)v;
                else handled = false;
            } else if (top == "MATERIAL_LIST") {
                long long v;
                if (key == "MATERIAL_COUNT" && IntAt(tk, 1, v) && v >= 0) {
                    if ((std::size_t)v > kMaxMaterials) return Fail("too many materials", ln);
                    declaredMaterials = v;
                }
            } else if (top == "MATERIAL" || top == "SUBMATERIAL") {
                Material* m = CurMaterial(stack);
                if (m) {
                    long long v; float f;
                    if (key == "MATERIAL_NAME") { if (tk.size() > 1) m->name = tk[1]; }
                    else if (key == "MATERIAL_DIFFUSE") { float c[3]; if (FloatAt(tk, 1, c[0]) && FloatAt(tk, 2, c[1]) && FloatAt(tk, 3, c[2])) { m->hasDiffuse = true; for (int i = 0; i < 3; ++i) m->diffuse[i] = std::min(1.0f, std::max(0.0f, c[i])); } }
                    else if (key == "MATERIAL_SHINE") { if (FloatAt(tk, 1, f)) m->shine = f; }
                    else if (key == "MATERIAL_SELFILLUM") { if (FloatAt(tk, 1, f)) m->selfIllum = f; }
                    else if (key == "MATERIAL_TRANSPARENCY") { if (FloatAt(tk, 1, f)) m->transparency = f; }
                    else if (key == "NUMSUBMTLS") { if (IntAt(tk, 1, v) && v >= 0 && (std::size_t)v <= kMaxMaterials) m->declaredSubs = (int)v; }
                    else if (key == "SUBMATERIAL") {}
                    else if (key == "MATERIAL_AMBIENT" || key == "MATERIAL_SPECULAR" || key == "MATERIAL_CLASS" || key == "MATERIAL_SHADING" || key == "MATERIAL_SHINESTRENGTH" || key == "MATERIAL_WIRESIZE" ||
                             key == "MATERIAL_XP_FALLOFF" || key == "MATERIAL_FALLOFF" || key == "MATERIAL_XP_TYPE" || key == "MATERIAL_TWOSIDED" || key == "MATERIAL_FACEMAP" || key == "MATERIAL_SELFILLUM") {}
                    else handled = false;
                }
            } else if (top.compare(0, 4, "MAP_") == 0 && stack.size() >= 2) {
                Material* m = CurMaterial(stack);
                if (m) {
                    if (key == "BITMAP" && tk.size() > 1) { if (top == "MAP_DIFFUSE") m->bitmap = tk[1]; else ++ignoredMaps; }
                    else if (key == "UVW_U_OFFSET" || key == "UVW_V_OFFSET" || key == "UVW_U_TILING" || key == "UVW_V_TILING" || key == "UVW_ANGLE") {
                        float f;
                        if (top == "MAP_DIFFUSE" && FloatAt(tk, 1, f) && ((key == "UVW_U_TILING" || key == "UVW_V_TILING") ? std::fabs(f - 1.0f) > 1e-4f : std::fabs(f) > 1e-4f)) ++uvTransforms;
                    }
                    else handled = false;
                }
            } else if (stack.size() >= 1 && (top == "GEOMOBJECT" || top == "HELPEROBJECT" || top == "CAMERAOBJECT" || top == "LIGHTOBJECT" || top == "SHAPEOBJECT")) {
                if (cur) {
                    long long v;
                    if (key == "NODE_NAME" && tk.size() > 1) cur->name = tk[1];
                    else if (key == "NODE_PARENT" && tk.size() > 1) cur->parent = tk[1];
                    else if (key == "MATERIAL_REF" && IntAt(tk, 1, v)) cur->matRef = (int)v;
                    else handled = false;
                }
            } else if (top == "NODE_TM" && cur) {
                if (key == "NODE_NAME") {
                    if (tk.size() > 1 && !cur->name.empty() && tk[1] != cur->name) { ++secondTm; tmSkip = true; } else tmSkip = false;
                } else if (!tmSkip) {
                    int row = -1;
                    if (key == "TM_ROW0") row = 0; else if (key == "TM_ROW1") row = 1; else if (key == "TM_ROW2") row = 2; else if (key == "TM_ROW3") row = 3;
                    if (row >= 0) {
                        double a, b, c;
                        if (!NumAt(tk, 1, a) || !NumAt(tk, 2, b) || !NumAt(tk, 3, c)) return Fail("bad number in a node transform", ln);
                        cur->row[row][0] = a; cur->row[row][1] = b; cur->row[row][2] = c;
                        cur->hasTm = true;
                    }
                }
            } else if (top == "MESH" && cur) {
                long long v;
                if (key == "MESH_NUMVERTEX") {
                    if (!IntAt(tk, 1, v) || v < 0) return Fail("bad MESH_NUMVERTEX", ln);
                    if (totalVerts + (std::size_t)v > kMaxVerts) return Fail("too many vertices", ln);
                    cur->numVertex = v; cur->pos.assign((std::size_t)v, Vec3{0, 0, 0}); totalVerts += (std::size_t)v; cur->hasMesh = true;
                } else if (key == "MESH_NUMFACES") {
                    if (!IntAt(tk, 1, v) || v < 0) return Fail("bad MESH_NUMFACES", ln);
                    if (totalFaces + (std::size_t)v > kMaxFaces) return Fail("too many faces", ln);
                    cur->numFaces = v; cur->faces.assign((std::size_t)v, Face{}); totalFaces += (std::size_t)v;
                } else if (key == "MESH_NUMTVERTEX") {
                    if (!IntAt(tk, 1, v) || v < 0) return Fail("bad MESH_NUMTVERTEX", ln);
                    if (totalVerts + (std::size_t)v > kMaxVerts) return Fail("too many texture vertices", ln);
                    cur->numTVertex = v; cur->tvert.assign((std::size_t)v, Vec2{0, 0}); totalVerts += (std::size_t)v;
                } else if (key == "MESH_NUMTVFACES") {
                    if (!IntAt(tk, 1, v) || v < 0) return Fail("bad MESH_NUMTVFACES", ln);
                    cur->numTFace = v;
                } else if (key == "MESH_NUMCVERTEX" || key == "MESH_NUMCVFACES") { /* vertex colors: counted at their lists */ }
                else handled = false;
                // the lists are blocks: the line opens one
            } else if (top == "MESH_VERTEX_LIST" && cur) {
                long long i; double x, y, z;
                if (key != "MESH_VERTEX") handled = false;
                else {
                    if (!IntAt(tk, 1, i) || !NumAt(tk, 2, x) || !NumAt(tk, 3, y) || !NumAt(tk, 4, z)) return Fail("bad vertex", ln);
                    if (i < 0 || i >= cur->numVertex) return Fail("vertex index out of range", ln);
                    cur->pos[(std::size_t)i] = Vec3{(float)x, (float)y, (float)z};
                }
            } else if (top == "MESH_FACE_LIST" && cur) {
                if (key != "MESH_FACE") handled = false;
                else {
                    long long i, a, b, c;
                    if (!IntAt(tk, 1, i) || !Labeled(tk, "A:", a) || !Labeled(tk, "B:", b) || !Labeled(tk, "C:", c)) return Fail("bad face", ln);
                    if (i < 0 || i >= cur->numFaces) return Fail("face index out of range", ln);
                    Face& f = cur->faces[(std::size_t)i];
                    f.v[0] = (int)std::max<long long>(-1, std::min<long long>(a, 2147483647)); f.v[1] = (int)std::max<long long>(-1, std::min<long long>(b, 2147483647)); f.v[2] = (int)std::max<long long>(-1, std::min<long long>(c, 2147483647));
                    f.sg = 0;
                    for (std::size_t k = 1; k + 1 < tk.size(); ++k) {
                        if (tk[k] == "*MESH_SMOOTHING") {
                            // "1" or "1,2,3": a list of group numbers (0 = none)
                            const std::string& s = tk[k + 1];
                            std::size_t p = 0;
                            while (p < s.size()) {
                                std::size_t q = s.find(',', p);
                                if (q == std::string::npos) q = s.size();
                                long long g;
                                if (ToInt(s.substr(p, q - p), g) && g > 0) { if (g > 32) ++sgOverflow; f.sg |= 1u << ((g - 1) % 32); }
                                p = q + 1;
                            }
                        } else if (tk[k] == "*MESH_MTLID") { long long m; if (ToInt(tk[k + 1], m) && m >= 0) f.mtl = (int)std::min<long long>(m, 1000000); }
                    }
                }
            } else if (top == "MESH_TVERTLIST" && cur) {
                long long i; float u, v;
                if (key != "MESH_TVERT") handled = false;
                else {
                    if (!IntAt(tk, 1, i) || !FloatAt(tk, 2, u) || !FloatAt(tk, 3, v)) return Fail("bad texture vertex", ln);
                    if (i < 0 || i >= cur->numTVertex) return Fail("texture vertex index out of range", ln);
                    cur->tvert[(std::size_t)i] = Vec2{u, v};
                }
            } else if (top == "MESH_TFACELIST" && cur) {
                long long i, a, b, c;
                if (key != "MESH_TFACE") handled = false;
                else {
                    if (!IntAt(tk, 1, i) || !IntAt(tk, 2, a) || !IntAt(tk, 3, b) || !IntAt(tk, 4, c)) return Fail("bad texture face", ln);
                    if (i < 0 || i >= cur->numFaces) { continue; }
                    Face& f = cur->faces[(std::size_t)i];
                    f.tv[0] = (int)a; f.tv[1] = (int)b; f.tv[2] = (int)c; f.hasTv = true;
                }
            } else if (top == "MESH_NORMALS" && cur) {
                if (cur->fnormals.empty() && cur->numFaces > 0) { cur->fnormals.assign((std::size_t)cur->numFaces, std::array<Vec3, 3>{Vec3{0, 0, 0}, Vec3{0, 0, 0}, Vec3{0, 0, 0}}); cur->fnHas.assign((std::size_t)cur->numFaces, 0); }
                long long i; double x, y, z;
                if (key == "MESH_FACENORMAL") {
                    if (!IntAt(tk, 1, i) || !NumAt(tk, 2, x) || !NumAt(tk, 3, y) || !NumAt(tk, 4, z)) return Fail("bad face normal", ln);
                    cur->curNormalFace = (i >= 0 && i < cur->numFaces) ? (int)i : -1;
                } else if (key == "MESH_VERTEXNORMAL") {
                    if (!IntAt(tk, 1, i) || !NumAt(tk, 2, x) || !NumAt(tk, 3, y) || !NumAt(tk, 4, z)) return Fail("bad vertex normal", ln);
                    if (cur->curNormalFace >= 0) {
                        const Face& f = cur->faces[(std::size_t)cur->curNormalFace];
                        for (int k = 0; k < 3; ++k) if (f.v[k] == (int)i) { cur->fnormals[(std::size_t)cur->curNormalFace][(std::size_t)k] = Vec3{(float)x, (float)y, (float)z}; cur->fnHas[(std::size_t)cur->curNormalFace] |= (char)(1 << k); }
                    }
                } else handled = false;
            } else if (top == "TM_ANIMATION" && cur) {
                // a target camera / light has a second TM_ANIMATION block named "<name>.Target": its keys are not for this node
                if (key == "NODE_NAME") { animSkip = tk.size() > 1 && !cur->name.empty() && tk[1] != cur->name; if (animSkip) ++targetAnims; }
            } else if (!animSkip && (top == "CONTROL_POS_TRACK" || top == "CONTROL_POS_TCB" || top == "CONTROL_POS_BEZIER") && cur) {
                double t, x, y, z;
                if (key == "CONTROL_POS_SAMPLE" || key == "CONTROL_TCB_POS_KEY" || key == "CONTROL_BEZIER_POS_KEY") {
                    if (!NumAt(tk, 1, t) || !NumAt(tk, 2, x) || !NumAt(tk, 3, y) || !NumAt(tk, 4, z)) { ++shortSamples; }
                    else { if (++totalKeys > kMaxKeys) return Fail("too many animation keys", ln); cur->posKeys.push_back({t, Vec3{(float)x, (float)y, (float)z}}); if (key != "CONTROL_POS_SAMPLE") ++otherKeyKinds; }
                } else handled = false;
            } else if (!animSkip && (top == "CONTROL_SCALE_TRACK" || top == "CONTROL_SCALE_TCB" || top == "CONTROL_SCALE_BEZIER") && cur) {
                double t, x, y, z;
                if (key == "CONTROL_SCALE_SAMPLE" || key == "CONTROL_TCB_SCALE_KEY" || key == "CONTROL_BEZIER_SCALE_KEY") {
                    if (!NumAt(tk, 1, t) || !NumAt(tk, 2, x) || !NumAt(tk, 3, y) || !NumAt(tk, 4, z)) { ++shortSamples; }
                    else { if (++totalKeys > kMaxKeys) return Fail("too many animation keys", ln); cur->sclKeys.push_back({t, Vec3{(float)x, (float)y, (float)z}}); if (key != "CONTROL_SCALE_SAMPLE") ++otherKeyKinds; }
                } else handled = false;
            } else if (!animSkip && (top == "CONTROL_ROT_TRACK" || top == "CONTROL_ROT_TCB" || top == "CONTROL_ROT_BEZIER") && cur) {
                double t, x, y, z, a;
                if (key == "CONTROL_ROT_SAMPLE" || key == "CONTROL_TCB_ROT_KEY" || key == "CONTROL_BEZIER_ROT_KEY") {
                    if (!NumAt(tk, 1, t) || !NumAt(tk, 2, x) || !NumAt(tk, 3, y) || !NumAt(tk, 4, z) || !NumAt(tk, 5, a)) { ++shortSamples; }
                    else { if (++totalKeys > kMaxKeys) return Fail("too many animation keys", ln); cur->rotKeys.push_back({t, Vec3{(float)x, (float)y, (float)z}, (float)a}); if (key != "CONTROL_ROT_SAMPLE") ++otherKeyKinds; }
                } else handled = false;
            } else handled = false;

            if (opens) {
                if (stack.size() >= kMaxDepth) return Fail("blocks nested too deeply", ln);
                // an object block starts a new current object
                if (stack.empty() || (stack.size() == 1 && stack[0] == "GROUP")) {
                    const bool isObj = key == "GEOMOBJECT" || key == "HELPEROBJECT" || key == "CAMERAOBJECT" || key == "LIGHTOBJECT" || key == "SHAPEOBJECT";
                    if (isObj) {
                        if (objs.size() >= kMaxNodes) return Fail("too many objects", ln);
                        objs.emplace_back();
                        cur = &objs.back();
                        cur->kind = key == "GEOMOBJECT" ? Obj::Geom : key == "HELPEROBJECT" ? Obj::Helper : key == "CAMERAOBJECT" ? Obj::Camera : key == "LIGHTOBJECT" ? Obj::Light : Obj::Shape;
                        curDepth = (int)stack.size() + 1;
                        tmSkip = false;
                    } else if (key == "GROUP") ++groups;
                    else if (key != "SCENE" && key != "MATERIAL_LIST") ++unknownObjects;
                }
                if (key == "TM_ANIMATION") animSkip = false;
                if (cur && key == "MESH_MAPPINGCHANNEL") ++cur->extraChannels;
                if (cur && (key == "MESH_CVERTLIST" || key == "MESH_CFACELIST")) ++cur->vertexColorBlocks;
                if (key == "MATERIAL" && stack.size() == 1 && stack[0] == "MATERIAL_LIST") { if (mats.size() >= kMaxMaterials) return Fail("too many materials", ln); mats.emplace_back(); }
                if (key == "SUBMATERIAL" && !mats.empty()) { Material* m = &mats.back(); if (m->subs.size() >= kMaxMaterials) return Fail("too many sub-materials", ln); m->subs.emplace_back(); }
                stack.push_back(key);
            } else if (!handled && tk.size() >= 1 && false) {}
        }
        if (!sawHeader) return Fail("not an ASE file (no *3DSMAX_ASCIIEXPORT line)");
        if (!stack.empty()) return Fail("the file ends inside an open block");
        (void)lineCount;
        return true;
    }
    bool tmSkip = false, animSkip = false;
    int targetAnims = 0;

    // ---- output ----
    struct Mat3d { double m[3][3]; double t[3]; };   // column vectors: world = R * local + t (columns are TM_ROW0..2)

    static bool Invert(const Mat3d& a, Mat3d& r) {
        const double (*m)[3] = a.m;
        const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        if (!std::isfinite(det) || std::fabs(det) < 1e-20) return false;
        r.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det; r.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det; r.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
        r.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det; r.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det; r.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
        r.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det; r.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det; r.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
        for (int i = 0; i < 3; ++i) r.t[i] = -(r.m[i][0] * a.t[0] + r.m[i][1] * a.t[1] + r.m[i][2] * a.t[2]);
        return true;
    }
    static Mat3d FromObj(const Obj& o) {
        Mat3d a;
        for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) a.m[r][c] = o.row[c][r];   // column c is TM_ROWc
        for (int i = 0; i < 3; ++i) a.t[i] = o.row[3][i];
        return a;
    }
    static Mat4 ToMat4(const Mat3d& a) {
        Mat4 m;
        for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) m.at(c, r) = (float)a.m[r][c];
        m.at(3, 0) = (float)a.t[0]; m.at(3, 1) = (float)a.t[1]; m.at(3, 2) = (float)a.t[2];
        return m;
    }

    bool Build(ImportedModel& out, const std::string& baseDirIn) {
        const std::size_t n = objs.size();
        if (n == 0) return Fail("the file has no objects");
        ImportedNode root;
        root.name = "ASE";
        root.rotation = Quat::AngleAxis(-90.0f, Vec3{1, 0, 0});   // Z up to Y up
        out.nodes.push_back(root);

        // ---- materials: one ImportedMaterial per top-level material, or per sub-material when it has them; made when first used ----
        std::vector<std::vector<int>> matIndex(mats.size());   // [material][sub] -> ImportedMaterial index, -1 = not made yet
        for (std::size_t mi = 0; mi < mats.size(); ++mi) matIndex[mi].assign(std::max<std::size_t>(1, mats[mi].subs.size()), -1);
        auto materialFor = [&](std::size_t mi, std::size_t sub) -> int {
            int& slot = matIndex[mi][sub];
            if (slot >= 0) return slot;
            const Material& top = mats[mi];
            const Material& s = top.subs.empty() ? top : top.subs[sub];
            ImportedMaterial im;
            im.name = (s.name.empty() ? top.name : s.name);
            if (!s.bitmap.empty()) {
                std::string tex = s.bitmap;
                for (char& c : tex) if (c == '\\') c = '/';
                // 3ds Max writes absolute paths of the author's disk: keep only the file name when the path is a Windows drive path
                im.texture = tex;
                const std::size_t sl = tex.find_last_of('/');
                const bool abs = tex.size() > 1 && tex[1] == ':';
                if (abs && sl != std::string::npos) { im.texture = baseDir.empty() ? tex.substr(sl + 1) : baseDir + tex.substr(sl + 1); ++absolutePaths; }
                else if (!baseDir.empty() && !tex.empty() && tex[0] != '/') im.texture = baseDir + tex;
            }
            if (s.hasDiffuse) { im.hasDiffuse = true; im.diffuse = Color{s.diffuse[0], s.diffuse[1], s.diffuse[2], std::min(1.0f, std::max(0.0f, 1.0f - s.transparency))}; }
            if (s.shine > 0) im.shininess = s.shine * 128.0f;   // 3ds Max stores the glossiness as 0..1 of 0..128
            if (s.selfIllum > 0 && s.hasDiffuse) { im.hasEmissive = true; im.emissive = Color{s.diffuse[0] * s.selfIllum, s.diffuse[1] * s.selfIllum, s.diffuse[2] * s.selfIllum, 1.0f}; }
            if (s.transparency > 0.001f) ++transparent;
            out.materials.push_back(std::move(im));
            return slot = (int)out.materials.size() - 1;
        };

        // ---- nodes in parent-first order ----
        std::unordered_map<std::string, int> byName;
        for (std::size_t i = 0; i < n; ++i) byName.emplace(objs[i].name, (int)i);
        std::vector<int> par(n, -1);
        int missingParent = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (objs[i].parent.empty()) continue;
            auto it = byName.find(objs[i].parent);
            if (it != byName.end() && it->second != (int)i) par[i] = it->second; else ++missingParent;
        }
        std::vector<int> order;
        const int loops = OrderParentsFirst(par, order);
        std::vector<Mat3d> world(n);
        std::vector<char> usable(n, 0);
        std::vector<int> nodeOf(n, 0);
        std::vector<std::string> nodeName(n);
        std::unordered_map<std::string, int> used;
        used["ASE"] = 1;
        int noTm = 0, singular = 0, dupNames = 0, mirrored = 0, shear = 0;
        for (int oi : order) {
            const Obj& o = objs[(std::size_t)oi];
            std::string nm = o.name.empty() ? "node" + std::to_string(oi) : o.name;
            int& u = used[nm];
            if (u++) { nm += "_" + std::to_string(u - 1); used[nm] = 1; ++dupNames; }
            nodeName[(std::size_t)oi] = nm;
            world[(std::size_t)oi] = FromObj(o);
            if (!o.hasTm) ++noTm;
            Mat3d inv;
            usable[(std::size_t)oi] = o.hasTm && Invert(world[(std::size_t)oi], inv) ? 1 : 0;
            if (o.hasTm && !usable[(std::size_t)oi]) ++singular;
            ImportedNode nd;
            nd.name = nm;
            nd.parent = par[(std::size_t)oi] < 0 ? 0 : nodeOf[(std::size_t)par[(std::size_t)oi]];
            // local = parent^-1 * world
            Mat4 local = usable[(std::size_t)oi] ? ToMat4(world[(std::size_t)oi]) : Mat4::Identity();
            if (usable[(std::size_t)oi] && par[(std::size_t)oi] >= 0) {
                if (usable[(std::size_t)par[(std::size_t)oi]]) local = ToMat4(world[(std::size_t)par[(std::size_t)oi]]).Inverse() * local;
            }
            if (!usable[(std::size_t)oi] || (par[(std::size_t)oi] >= 0 && !usable[(std::size_t)par[(std::size_t)oi]])) { /* kept as is */ }
            Vec3 t, s; Quat r;
            local.Decompose(t, r, s);
            if (s.x < 0) ++mirrored;
            // shear check: the decomposition must give the matrix back
            const Mat4 back = Mat4::TRS(t, r, s);
            float diff = 0, mag = 1;
            for (int k = 0; k < 16; ++k) { diff = std::max(diff, std::fabs(back.m[k] - local.m[k])); mag = std::max(mag, std::fabs(local.m[k])); }
            if (diff > 1e-3f * mag) ++shear;
            nd.position = t; nd.rotation = r; nd.scale = s;
            nodeOf[(std::size_t)oi] = (int)out.nodes.size();
            out.nodes.push_back(std::move(nd));
        }
        for (const ImportedNode& nd : out.nodes)
            if (!std::isfinite(nd.position.x + nd.position.y + nd.position.z + nd.scale.x + nd.scale.y + nd.scale.z) || std::fabs(nd.position.x) > 1e30f || std::fabs(nd.position.y) > 1e30f || std::fabs(nd.position.z) > 1e30f || std::fabs(nd.scale.x) > 1e30f || std::fabs(nd.scale.y) > 1e30f || std::fabs(nd.scale.z) > 1e30f)
                return Fail("node transforms overflow");

        // ---- meshes ----
        int badFaces = 0, degenerate = 0, noFaceGeom = 0, noUv = 0, generatedNormals = 0, fileNormals = 0, mtlWrap = 0, badMatRef = 0, badTv = 0, incidentCapped = 0, extraChannels = 0, colorBlocks = 0, kinds[5] = {0, 0, 0, 0, 0};
        for (std::size_t oi = 0; oi < n; ++oi) {
            const Obj& o = objs[oi];
            ++kinds[o.kind];
            extraChannels += o.extraChannels; colorBlocks += o.vertexColorBlocks;
            if (o.kind != Obj::Geom || !o.hasMesh) continue;
            if (o.faces.empty() || o.pos.empty()) { ++noFaceGeom; continue; }
            // world -> local
            Mat3d inv{};
            const bool haveInv = usable[oi] && Invert(world[oi], inv);
            std::vector<Vec3> lp(o.pos.size());
            for (std::size_t v = 0; v < o.pos.size(); ++v) {
                if (haveInv) {
                    const double x = o.pos[v].x, y = o.pos[v].y, z = o.pos[v].z;
                    lp[v] = Vec3{(float)(inv.m[0][0] * x + inv.m[0][1] * y + inv.m[0][2] * z + inv.t[0]), (float)(inv.m[1][0] * x + inv.m[1][1] * y + inv.m[1][2] * z + inv.t[1]), (float)(inv.m[2][0] * x + inv.m[2][1] * y + inv.m[2][2] * z + inv.t[2])};
                } else lp[v] = o.pos[v];
                if (!std::isfinite(lp[v].x + lp[v].y + lp[v].z) || std::fabs(lp[v].x) > 1e30f || std::fabs(lp[v].y) > 1e30f || std::fabs(lp[v].z) > 1e30f) return Fail("coordinates overflow");
            }
            // valid faces
            std::vector<int> goodFaces;
            std::vector<Vec3> faceN(o.faces.size());
            const int nv = (int)o.pos.size();
            for (std::size_t fi = 0; fi < o.faces.size(); ++fi) {
                const Face& f = o.faces[fi];
                if (f.v[0] < 0 || f.v[1] < 0 || f.v[2] < 0 || f.v[0] >= nv || f.v[1] >= nv || f.v[2] >= nv) { ++badFaces; continue; }
                if (f.v[0] == f.v[1] || f.v[1] == f.v[2] || f.v[0] == f.v[2]) { ++degenerate; continue; }
                faceN[fi] = Vec3::Cross(lp[(std::size_t)f.v[1]] - lp[(std::size_t)f.v[0]], lp[(std::size_t)f.v[2]] - lp[(std::size_t)f.v[0]]);
                goodFaces.push_back((int)fi);
            }
            if (goodFaces.empty()) { ++noFaceGeom; continue; }
            // per vertex incident faces for smoothing groups (normals the file did not give)
            const bool haveFileNormals = !o.fnormals.empty();
            std::vector<std::vector<int>> incident;
            if (!haveFileNormals) {
                incident.resize(o.pos.size());
                for (int fi : goodFaces) for (int k = 0; k < 3; ++k) { auto& l = incident[(std::size_t)o.faces[(std::size_t)fi].v[k]]; if (l.size() < kMaxIncident) l.push_back(fi); else ++incidentCapped; }
            }
            struct PartAcc { ImportedPart part; std::unordered_map<std::string, int> weld; };
            std::unordered_map<int, PartAcc> parts;   // ImportedMaterial index (or -1) -> part
            std::vector<int> partOrder;
            const bool matOk = o.matRef >= 0 && (std::size_t)o.matRef < mats.size();
            if (o.matRef >= 0 && !matOk) ++badMatRef;
            for (int fi : goodFaces) {
                const Face& f = o.faces[(std::size_t)fi];
                int mat = -1;
                if (matOk) {
                    const Material& mt = mats[(std::size_t)o.matRef];
                    if (mt.subs.empty()) mat = materialFor((std::size_t)o.matRef, 0);
                    else { if ((std::size_t)f.mtl >= mt.subs.size()) ++mtlWrap; mat = materialFor((std::size_t)o.matRef, (std::size_t)f.mtl % mt.subs.size()); }
                }
                auto pit = parts.find(mat);
                if (pit == parts.end()) { pit = parts.emplace(mat, PartAcc{}).first; partOrder.push_back(mat); }
                PartAcc& pa = pit->second;
                for (int k = 0; k < 3; ++k) {
                    // normal
                    Vec3 nrm;
                    if (haveFileNormals && (o.fnHas[(std::size_t)fi] & (1 << k))) {
                        const Vec3& w = o.fnormals[(std::size_t)fi][(std::size_t)k];
                        nrm = Vec3{(float)(o.row[0][0] * w.x + o.row[0][1] * w.y + o.row[0][2] * w.z), (float)(o.row[1][0] * w.x + o.row[1][1] * w.y + o.row[1][2] * w.z), (float)(o.row[2][0] * w.x + o.row[2][1] * w.y + o.row[2][2] * w.z)};
                        // the vertex lives in local space: the normal goes through the transpose of the world matrix columns (dot with TM_ROWi)
                        ++fileNormals;
                    } else {
                        Vec3 sum = faceN[(std::size_t)fi];
                        if (f.sg != 0 && !incident.empty()) {
                            sum = Vec3{0, 0, 0};
                            for (int g : incident[(std::size_t)f.v[k]]) if (o.faces[(std::size_t)g].sg & f.sg) sum += faceN[(std::size_t)g];
                        }
                        nrm = sum; ++generatedNormals;
                    }
                    const float nm = nrm.Magnitude();
                    nrm = (nm > 1e-12f && std::isfinite(nm)) ? nrm * (1.0f / nm) : Vec3{0, 1, 0};
                    // uv
                    Vec2 uv{0, 0};
                    bool hasUv = false;
                    if (f.hasTv && f.tv[k] >= 0 && (std::size_t)f.tv[k] < o.tvert.size()) { uv = Vec2{o.tvert[(std::size_t)f.tv[k]].x, 1.0f - o.tvert[(std::size_t)f.tv[k]].y}; hasUv = true; }
                    else if (f.hasTv) ++badTv;
                    std::uint32_t key[9];
                    key[0] = (std::uint32_t)f.v[k];
                    const float fl[6] = {nrm.x, nrm.y, nrm.z, uv.x, uv.y, hasUv ? 1.0f : 0.0f};
                    for (int q = 0; q < 6; ++q) std::memcpy(&key[1 + q], &fl[q], 4);
                    key[7] = key[8] = 0;
                    const std::string ks((const char*)key, sizeof key);
                    auto it = pa.weld.find(ks);
                    int vi;
                    if (it == pa.weld.end()) {
                        vi = (int)pa.part.mesh.vertices.size();
                        pa.part.mesh.vertices.push_back(lp[(std::size_t)f.v[k]]);
                        pa.part.mesh.normals.push_back(nrm);
                        pa.part.mesh.uvs.push_back(uv);
                        pa.weld.emplace(ks, vi);
                    } else vi = it->second;
                    pa.part.mesh.triangles.push_back(vi);
                }
            }
            if (o.tvert.empty()) ++noUv;
            for (int mat : partOrder) {
                PartAcc& pa = parts[mat];
                if (o.tvert.empty()) pa.part.mesh.uvs.clear();
                pa.part.mesh.name = o.name;
                pa.part.node = nodeOf[oi];
                pa.part.material = mat;
                out.parts.push_back(std::move(pa.part));
            }
        }
        if (out.parts.empty() && out.nodes.size() <= 1) return Fail("the file has no objects");
        if (out.parts.empty()) return Fail("the file has no mesh");

        // ---- animation ----
        const double tps = (double)frameSpeed * (double)ticksPerFrame;
        ImportedClip clip;
        clip.name = "animation";
        for (std::size_t oi = 0; oi < n; ++oi) {
            const Obj& o = objs[oi];
            ImportedChannel ch;
            ch.node = nodeName[oi];
            auto sorted = [&](auto& keys) {
                bool ok = true;
                for (std::size_t q = 0; q < keys.size(); ++q) if (!(keys[q].tick >= 0) || keys[q].tick > 1e12 || (q && keys[q].tick < keys[q - 1].tick)) ok = false;
                return ok;
            };
            if (o.posKeys.size() > 1 && sorted(o.posKeys)) for (const KeyV& k : o.posKeys) { if (std::fabs(k.v.x) > 1e30f || std::fabs(k.v.y) > 1e30f || std::fabs(k.v.z) > 1e30f) return Fail("animation values overflow"); ch.position.push_back({(float)(k.tick / tps), k.v}); }
            else if (!o.posKeys.empty() && o.posKeys.size() > 1) ++unsortedTracks; else if (!o.posKeys.empty()) ++singleKeyTracks;
            if (o.sclKeys.size() > 1 && sorted(o.sclKeys)) for (const KeyV& k : o.sclKeys) { if (std::fabs(k.v.x) > 1e30f || std::fabs(k.v.y) > 1e30f || std::fabs(k.v.z) > 1e30f) return Fail("animation values overflow"); ch.scale.push_back({(float)(k.tick / tps), k.v}); }
            else if (!o.sclKeys.empty() && o.sclKeys.size() > 1) ++unsortedTracks; else if (!o.sclKeys.empty()) ++singleKeyTracks;
            if (o.rotKeys.size() > 1 && sorted(o.rotKeys)) {
                // axis-angle samples (3ds Max sign convention: the rotation is the inverse of a right-handed one); for files newer than
                // version 110 every sample is relative to the one before, so they are concatenated
                Quat cur;
                for (std::size_t q = 0; q < o.rotKeys.size(); ++q) {
                    const KeyQ& k = o.rotKeys[q];
                    const float len = k.axis.Magnitude();
                    Quat qq = (len > 1e-12f && std::isfinite(len) && std::isfinite(k.angle)) ? Quat::AngleAxis(k.angle * 57.29577951308232f, k.axis * (1.0f / len)) : Quat{0, 0, 0, 1};
                    if (version > 110) cur = (q ? cur * qq : qq).Normalized(); else cur = qq;
                    ch.rotation.push_back({(float)(k.tick / tps), Quat{cur.x, cur.y, cur.z, -cur.w}.Normalized()});
                }
            } else if (!o.rotKeys.empty() && o.rotKeys.size() > 1) ++unsortedTracks; else if (!o.rotKeys.empty()) ++singleKeyTracks;
            if (!ch.position.empty() || !ch.rotation.empty() || !ch.scale.empty()) clip.channels.push_back(std::move(ch));
        }
        if (!clip.channels.empty()) out.clips.push_back(std::move(clip));
        (void)baseDirIn;

        auto note = [&](const std::string& s) { out.notes.push_back("ASE: " + s); };
        note("the root node turns Z-up into Y-up; units and winding are kept; v coordinates are flipped (v = 1 - v)");
        if (kinds[Obj::Camera]) note(std::to_string(kinds[Obj::Camera]) + " camera(s) are plain empty nodes (their settings and targets are not imported)");
        if (kinds[Obj::Light]) note(std::to_string(kinds[Obj::Light]) + " light(s) are plain empty nodes (their settings are not imported)");
        if (kinds[Obj::Shape]) note(std::to_string(kinds[Obj::Shape]) + " shape / spline object(s) are plain empty nodes (the splines are not imported)");
        if (kinds[Obj::Helper]) note(std::to_string(kinds[Obj::Helper]) + " helper object(s) kept as empty nodes");
        if (groups) note(std::to_string(groups) + " group(s): their members are imported, the grouping is not kept");
        if (unknownObjects) note(std::to_string(unknownObjects) + " unknown top-level block(s) skipped");
        if (secondTm) note(std::to_string(secondTm) + " second node transform(s) (camera / light targets) ignored");
        if (targetAnims) note(std::to_string(targetAnims) + " target animation block(s) (camera / light targets) ignored");
        if (colorBlocks) note(std::to_string(colorBlocks) + " vertex color block(s) not imported");
        if (extraChannels) note(std::to_string(extraChannels) + " extra texture mapping channel(s) not imported");
        if (ignoredMaps) note(std::to_string(ignoredMaps) + " material map(s) other than the diffuse map not imported (opacity, bump, specular, reflection ...)");
        if (uvTransforms) note(std::to_string(uvTransforms) + " texture tiling/offset/rotation value(s) not applied");
        if (absolutePaths) note(std::to_string(absolutePaths) + " texture path(s) were absolute Windows paths: only the file name is kept");
        if (transparent) note(std::to_string(transparent) + " material(s) with transparency: it is stored in the diffuse alpha");
        if (!mats.empty()) note("ambient and specular colors and the shading mode are not imported");
        if (generatedNormals && !fileNormals) note("no MESH_NORMALS in the file: normals were built from the smoothing groups");
        else if (generatedNormals) note("some corners had no file normal: built from the smoothing groups");
        if (sgOverflow) note(std::to_string(sgOverflow) + " smoothing group number(s) above 32 were folded into 1..32");
        if (incidentCapped) note("a vertex shared by more than " + std::to_string(kMaxIncident) + " faces smooths over the first " + std::to_string(kMaxIncident) + " only");
        if (noUv) note(std::to_string(noUv) + " mesh(es) without texture coordinates");
        if (badTv) note(std::to_string(badTv) + " face corner(s) with a missing texture vertex got (0, 0)");
        if (badFaces) note(std::to_string(badFaces) + " face(s) with an out-of-range vertex index skipped");
        if (degenerate) note(std::to_string(degenerate) + " degenerate face(s) skipped");
        if (noFaceGeom) note(std::to_string(noFaceGeom) + " mesh object(s) without usable faces skipped");
        if (mtlWrap) note(std::to_string(mtlWrap) + " face(s) with a material id beyond the sub-material count wrapped around");
        if (badMatRef) note(std::to_string(badMatRef) + " object(s) name a material that does not exist");
        if (missingParent) note(std::to_string(missingParent) + " object(s) name a parent that does not exist and were attached to the root");
        if (loops) note(std::to_string(loops) + " parent loop(s) were cut");
        if (dupNames) note(std::to_string(dupNames) + " object name(s) were repeated and got a numeric suffix");
        if (noTm) note(std::to_string(noTm) + " object(s) without a node transform sit at the origin");
        if (singular) note(std::to_string(singular) + " node transform(s) not invertible: the object is placed at the origin with its vertices as stored");
        if (mirrored) note(std::to_string(mirrored) + " node(s) with a mirroring transform (negative scale)");
        if (shear) note(std::to_string(shear) + " node transform(s) with shear: the shear was dropped");
        if (otherKeyKinds) note(std::to_string(otherKeyKinds) + " TCB / Bezier key(s) read as plain keys (tangents and tension are not used)");
        if (shortSamples) note(std::to_string(shortSamples) + " animation key line(s) with too few values skipped");
        if (unsortedTracks) note(std::to_string(unsortedTracks) + " animation track(s) with keys out of time order or at negative times skipped");
        if (singleKeyTracks) note(std::to_string(singleKeyTracks) + " animation track(s) with a single key are not animation and were skipped");
        if (!out.clips.empty()) note("key times are ticks / (SCENE_FRAMESPEED x SCENE_TICKSPERFRAME); rotation samples follow 3ds Max's axis-angle sign (inverse of right-handed)");
        return true;
    }
    int absolutePaths = 0, transparent = 0, unsortedTracks = 0;
};

} // namespace ase
} // namespace

bool ReadAseBuffer(const std::string& textIn, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    ase::Reader r;
    r.baseDir = baseDir;
    std::string text = textIn, why;
    bool conv = false;
    bool ok = detail::txt::NormalizeEncoding(text, conv, why, ase::kMaxFileBytes);
    if (!ok) r.err = "ASE: " + why;
    ok = ok && r.Parse(text) && r.Build(out, baseDir);
    if (!ok) {
        const std::string m = r.err.empty() ? "ASE: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    if (conv) out.notes.push_back("ASE: the file is UTF-16 text and was converted");
    out.backend = "own";
    return true;
}

bool ReadAseFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, ase::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadAseBuffer(bytes, dir, out, err);
}

} // namespace okay
