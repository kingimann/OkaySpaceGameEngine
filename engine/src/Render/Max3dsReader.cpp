// Our own 3D Studio (.3ds) reader. The format is a tree of little-endian chunks
// (u16 id, u32 length including the 6 byte header). Written from the public
// description of the chunk layout, with no code from any other importer.
//
// Reads: meshes (points, faces, UVs, smoothing groups, per-face material groups,
// the mesh matrix), materials (diffuse / specular / bump texture, colours,
// shininess, self illumination), the keyframe section (node hierarchy, pivots,
// position / rotation / scale tracks) and the master scale. Everything it did not
// read is listed in ImportedModel::notes. See docs/own_3ds_reader.md.
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace okay {

namespace {
namespace tds {

constexpr std::size_t kMaxFileBytes   = 512u * 1024u * 1024u;
constexpr int         kMaxDepth       = 32;
constexpr std::size_t kMaxChunks      = 10000000;
constexpr std::size_t kMaxObjects     = 100000;
constexpr std::size_t kMaxMaterials   = 100000;
constexpr std::size_t kMaxKfNodes     = 100000;
constexpr std::size_t kMaxTotalPoints = 20000000;
constexpr std::size_t kMaxTotalFaces  = 20000000;
constexpr std::size_t kMaxTotalKeys   = 20000000;
constexpr std::size_t kMaxOutVerts    = 40000000;
constexpr std::size_t kMaxParts       = 200000;
constexpr std::size_t kMaxSgPerVertex = 128;
constexpr std::size_t kMaxNameBytes   = 1024;
constexpr float       kFps            = 30.0f;
constexpr float       kMaxCoord       = 1e15f;   // larger coordinates, keys or scales would overflow later arithmetic
inline bool InRange(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::fabs(v.x) <= kMaxCoord && std::fabs(v.y) <= kMaxCoord && std::fabs(v.z) <= kMaxCoord; }

enum : std::uint16_t {
    C_MAIN = 0x4D4D, C_VERSION = 0x0002, C_EDIT = 0x3D3D, C_MASTER_SCALE = 0x0100,
    C_MATERIAL = 0xAFFF, C_MAT_NAME = 0xA000, C_MAT_AMBIENT = 0xA010, C_MAT_DIFFUSE = 0xA020, C_MAT_SPECULAR = 0xA030,
    C_MAT_SHININESS = 0xA040, C_MAT_SHIN_STR = 0xA041, C_MAT_TRANSP = 0xA050, C_MAT_SELFILLUM = 0xA084,
    C_MAT_TEX1 = 0xA200, C_MAT_SPECMAP = 0xA204, C_MAT_OPACMAP = 0xA210, C_MAT_REFLMAP = 0xA220, C_MAT_BUMPMAP = 0xA230,
    C_MAT_TEX2 = 0xA33A, C_MAT_SHINMAP = 0xA33C, C_MAT_SELFMAP = 0xA33D,
    C_MAP_FILE = 0xA300, C_MAP_USCALE = 0xA354, C_MAP_VSCALE = 0xA356, C_MAP_UOFF = 0xA358, C_MAP_VOFF = 0xA35A, C_MAP_ROT = 0xA35C,
    C_RGB_F = 0x0010, C_RGB_B = 0x0011, C_RGB_LB = 0x0012, C_RGB_LF = 0x0013, C_PCT_I = 0x0030, C_PCT_F = 0x0031,
    C_NAMED_OBJECT = 0x4000, C_TRIMESH = 0x4100, C_POINTS = 0x4110, C_FACES = 0x4120, C_FACE_MAT = 0x4130,
    C_TEXVERTS = 0x4140, C_SMOOTH = 0x4150, C_MESH_MATRIX = 0x4160, C_LIGHT = 0x4600, C_CAMERA = 0x4700,
    C_KFDATA = 0xB000, C_KFHDR = 0xB00A, C_KFSEG = 0x7001,
    C_KF_AMBIENT = 0xB001, C_KF_OBJECT = 0xB002, C_KF_CAMERA = 0xB003, C_KF_CAMTARGET = 0xB004,
    C_KF_LIGHT = 0xB005, C_KF_LIGHTTARGET = 0xB006, C_KF_SPOT = 0xB007,
    C_NODE_HDR = 0xB010, C_NODE_INSTANCE = 0xB011, C_NODE_PIVOT = 0xB013,
    C_TRACK_POS = 0xB020, C_TRACK_ROT = 0xB021, C_TRACK_SCL = 0xB022, C_NODE_ID = 0xB030
};

struct Rgb { bool has = false; float c[3] = {0, 0, 0}; };
struct MatRec {
    std::string name, texDiffuse, texSpecular, texBump;
    Rgb diffuse, specular;
    float shininess = -1.0f;     // percent, < 0 unset
    float transparency = 0.0f;   // percent
    float selfIllum = 0.0f;      // percent
};
struct MatGroup { std::string mat; std::vector<std::uint16_t> faces; };
struct ObjRec {
    std::string name;
    bool tri = false;
    std::vector<Vec3> pts;
    std::vector<Vec2> uv;
    std::vector<std::array<std::uint16_t, 3>> faces;
    std::vector<std::uint32_t> sg;
    bool hasSg = false;
    double mm[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};   // mesh matrix: 3 rows of the 3x3, then the translation
    bool hasMM = false;
    std::vector<MatGroup> groups;
};
struct KeyV { std::uint32_t frame; Vec3 v; };
struct KeyR { std::uint32_t frame; Vec3 axis; float angle; };
struct KfRec {
    std::string name, instance;
    std::uint16_t parent = 0xFFFF;
    int id = -1;
    Vec3 pivot{0, 0, 0};
    std::vector<KeyV> pos, scl;
    std::vector<KeyR> rot;
};

struct Reader {
    const unsigned char* d = nullptr;
    std::size_t n = 0;
    std::string baseDir;
    bool flatten = false;
    std::string err;

    std::vector<MatRec> mats;
    std::vector<ObjRec> objs;
    std::vector<KfRec> kfs;
    float masterScale = 1.0f;
    bool hasSeg = false;
    std::uint32_t segStart = 0, segEnd = 0;
    std::size_t chunks = 0, totalPts = 0, totalFaces = 0, totalKeys = 0;
    int lights = 0, cameras = 0, kfOther = 0, ignoredMaps = 0, texTransforms = 0, clamped = 0, badFaces = 0, shortSg = 0;
    int badMatrix = 0, truncatedTracks = 0, sgOverflow = 0, extraMeshChunks = 0;
    int curMat = -1, curObj = -1, curKf = -1;

    bool Fail(const char* m) { if (err.empty()) err = m; return false; }

    // --- bounds-checked little-endian reads (callers check Has first) ---
    bool Has(std::size_t pos, std::size_t k) const { return pos <= n && k <= n - pos; }
    std::uint16_t U16(std::size_t p) const { return (std::uint16_t)(d[p] | (d[p + 1] << 8)); }
    std::uint32_t U32(std::size_t p) const { return (std::uint32_t)d[p] | ((std::uint32_t)d[p + 1] << 8) | ((std::uint32_t)d[p + 2] << 16) | ((std::uint32_t)d[p + 3] << 24); }
    float F32(std::size_t p) const { std::uint32_t u = U32(p); float f; std::memcpy(&f, &u, 4); return f; }
    // A NUL-terminated string inside [pos, end). On success `pos` is moved past the NUL.
    bool CStr(std::size_t& pos, std::size_t end, std::string& out) const {
        std::size_t i = pos;
        while (i < end && d[i] != 0 && i - pos < kMaxNameBytes) ++i;
        if (i >= end || d[i] != 0) return false;
        out.assign((const char*)d + pos, i - pos);
        pos = i + 1;
        return true;
    }

    // --- chunk walk ---
    bool Walk(std::size_t begin, std::size_t end, int depth, std::uint16_t parent) {
        if (depth > kMaxDepth) return Fail("3DS: chunks nested too deeply");
        std::size_t pos = begin;
        while (end > pos && end - pos >= 6) {
            if (++chunks > kMaxChunks) return Fail("3DS: too many chunks");
            const std::uint16_t id = U16(pos);
            const std::uint32_t len = U32(pos + 2);
            if (len < 6) return Fail("3DS: chunk shorter than its header");
            std::size_t cend;
            if (len > end - pos) { cend = end; ++clamped; } else cend = pos + len;
            if (!Chunk(id, pos + 6, cend, depth, parent)) return false;
            pos = cend;
        }
        return true;
    }

    bool Chunk(std::uint16_t id, std::size_t b, std::size_t e, int depth, std::uint16_t parent) {
        switch (id) {
        case C_MAIN: case C_EDIT: case C_KFDATA: case C_TRIMESH:
            return Walk(b, e, depth + 1, id);
        case C_MASTER_SCALE:
            if (Has(b, 4) && e - b >= 4) { const float f = F32(b); if (std::isfinite(f) && f > 0.0f && f < 1e6f) masterScale = f; }
            return true;
        case C_MATERIAL:
            if (mats.size() >= kMaxMaterials) return Fail("3DS: too many materials");
            mats.emplace_back(); curMat = (int)mats.size() - 1;
            return Walk(b, e, depth + 1, id);
        case C_MAT_NAME:
            if (curMat >= 0) { std::size_t p = b; std::string s; if (CStr(p, e, s)) mats[(std::size_t)curMat].name = s; }
            return true;
        case C_MAT_AMBIENT: case C_MAT_DIFFUSE: case C_MAT_SPECULAR:
        case C_MAT_SHININESS: case C_MAT_SHIN_STR: case C_MAT_TRANSP: case C_MAT_SELFILLUM:
            return Walk(b, e, depth + 1, id);
        case C_MAT_TEX1: case C_MAT_SPECMAP: case C_MAT_BUMPMAP:
            return Walk(b, e, depth + 1, id);
        case C_MAT_OPACMAP: case C_MAT_REFLMAP: case C_MAT_TEX2: case C_MAT_SHINMAP: case C_MAT_SELFMAP:
            ++ignoredMaps;
            return Walk(b, e, depth + 1, id);
        case C_MAP_FILE: {
            if (curMat < 0) return true;
            std::size_t p = b; std::string s;
            if (!CStr(p, e, s)) return true;
            MatRec& m = mats[(std::size_t)curMat];
            if (parent == C_MAT_TEX1) m.texDiffuse = s;
            else if (parent == C_MAT_SPECMAP) m.texSpecular = s;
            else if (parent == C_MAT_BUMPMAP) m.texBump = s;
            return true;
        }
        case C_MAP_USCALE: case C_MAP_VSCALE:
            if (e - b >= 4 && Has(b, 4)) { const float f = F32(b); if (std::isfinite(f) && std::fabs(f - 1.0f) > 1e-4f) ++texTransforms; }
            return true;
        case C_MAP_UOFF: case C_MAP_VOFF: case C_MAP_ROT:
            if (e - b >= 4 && Has(b, 4)) { const float f = F32(b); if (std::isfinite(f) && std::fabs(f) > 1e-4f) ++texTransforms; }
            return true;
        case C_RGB_F: case C_RGB_LF: case C_RGB_B: case C_RGB_LB: {
            if (curMat < 0) return true;
            MatRec& m = mats[(std::size_t)curMat];
            Rgb* slot = parent == C_MAT_DIFFUSE ? &m.diffuse : parent == C_MAT_SPECULAR ? &m.specular : nullptr;
            if (!slot) return true;
            const bool isFloat = id == C_RGB_F || id == C_RGB_LF;
            const std::size_t need = isFloat ? 12 : 3;
            if (e - b < need || !Has(b, need)) return true;
            for (int i = 0; i < 3; ++i) {
                float c = isFloat ? F32(b + 4 * (std::size_t)i) : d[b + (std::size_t)i] / 255.0f;
                if (!std::isfinite(c)) c = 0.0f;
                slot->c[i] = std::min(1.0f, std::max(0.0f, c));
            }
            slot->has = true;
            return true;
        }
        case C_PCT_I: case C_PCT_F: {
            if (curMat < 0) return true;
            float v;
            if (id == C_PCT_I) { if (e - b < 2 || !Has(b, 2)) return true; v = (float)U16(b); }
            else { if (e - b < 4 || !Has(b, 4)) return true; v = F32(b); }
            if (!std::isfinite(v)) return true;
            MatRec& m = mats[(std::size_t)curMat];
            if (parent == C_MAT_SHININESS) m.shininess = v;
            else if (parent == C_MAT_TRANSP) m.transparency = v;
            else if (parent == C_MAT_SELFILLUM) m.selfIllum = v;
            return true;
        }
        case C_NAMED_OBJECT: {
            std::size_t p = b; std::string s;
            if (!CStr(p, e, s)) return true;
            if (objs.size() >= kMaxObjects) return Fail("3DS: too many objects");
            objs.emplace_back(); curObj = (int)objs.size() - 1;
            objs.back().name = s;
            return Walk(p, e, depth + 1, id);
        }
        case C_LIGHT: ++lights; return true;
        case C_CAMERA: ++cameras; return true;
        case C_POINTS: {
            if (curObj < 0 || e - b < 2) return true;
            const std::size_t cnt = U16(b);
            if (cnt * 12 > e - b - 2) return Fail("3DS: point array longer than its chunk");
            ObjRec& o = objs[(std::size_t)curObj];
            if (!o.pts.empty()) ++extraMeshChunks;
            totalPts += cnt - std::min(cnt, o.pts.size());
            if (totalPts > kMaxTotalPoints) return Fail("3DS: too many points");
            o.pts.resize(cnt);
            for (std::size_t i = 0; i < cnt; ++i) {
                const std::size_t q = b + 2 + i * 12;
                const Vec3 v{F32(q), F32(q + 4), F32(q + 8)};
                if (!InRange(v)) return Fail("3DS: non-finite or out-of-range point");
                o.pts[i] = v;
            }
            o.tri = true;
            return true;
        }
        case C_FACES: {
            if (curObj < 0 || e - b < 2) return true;
            const std::size_t cnt = U16(b);
            if (cnt * 8 > e - b - 2) return Fail("3DS: face array longer than its chunk");
            ObjRec& o = objs[(std::size_t)curObj];
            totalFaces += cnt - std::min(cnt, o.faces.size());
            if (totalFaces > kMaxTotalFaces) return Fail("3DS: too many faces");
            o.faces.resize(cnt);
            for (std::size_t i = 0; i < cnt; ++i) {
                const std::size_t q = b + 2 + i * 8;
                o.faces[i] = {U16(q), U16(q + 2), U16(q + 4)};
            }
            o.tri = true;
            return Walk(b + 2 + cnt * 8, e, depth + 1, id);
        }
        case C_FACE_MAT: {
            if (curObj < 0 || parent != C_FACES) return true;
            std::size_t p = b; std::string s;
            if (!CStr(p, e, s) || e - p < 2) return true;
            const std::size_t cnt = U16(p);
            if (cnt * 2 > e - p - 2) return Fail("3DS: material group longer than its chunk");
            MatGroup g; g.mat = s; g.faces.resize(cnt);
            for (std::size_t i = 0; i < cnt; ++i) g.faces[i] = U16(p + 2 + i * 2);
            objs[(std::size_t)curObj].groups.push_back(std::move(g));
            return true;
        }
        case C_SMOOTH: {
            if (curObj < 0 || parent != C_FACES) return true;
            ObjRec& o = objs[(std::size_t)curObj];
            const std::size_t cnt = std::min<std::size_t>(o.faces.size(), (e - b) / 4);
            if (cnt < o.faces.size()) ++shortSg;
            o.sg.assign(o.faces.size(), 0);
            for (std::size_t i = 0; i < cnt; ++i) o.sg[i] = U32(b + i * 4);
            o.hasSg = true;
            return true;
        }
        case C_TEXVERTS: {
            if (curObj < 0 || e - b < 2) return true;
            const std::size_t cnt = U16(b);
            if (cnt * 8 > e - b - 2) return Fail("3DS: UV array longer than its chunk");
            ObjRec& o = objs[(std::size_t)curObj];
            o.uv.resize(cnt);
            for (std::size_t i = 0; i < cnt; ++i) {
                float u = F32(b + 2 + i * 8), v = F32(b + 6 + i * 8);
                if (!std::isfinite(u)) u = 0.0f;
                if (!std::isfinite(v)) v = 0.0f;
                o.uv[i] = Vec2{u, v};
            }
            return true;
        }
        case C_MESH_MATRIX:
            if (curObj >= 0 && e - b >= 48 && Has(b, 48)) {
                ObjRec& o = objs[(std::size_t)curObj];
                bool fin = true;
                for (int i = 0; i < 12; ++i) {
                    const float f = F32(b + 4 * (std::size_t)i);
                    if (!std::isfinite(f) || std::fabs(f) > kMaxCoord) fin = false;
                    o.mm[i] = f;
                }
                if (!fin) { ++badMatrix; for (int i = 0; i < 12; ++i) o.mm[i] = i % 4 == 0 && i < 9 ? 1.0 : 0.0; o.mm[0] = o.mm[4] = o.mm[8] = 1.0; }
                else o.hasMM = true;
            }
            return true;
        case C_KFHDR: return true;
        case C_KFSEG:
            if (e - b >= 8 && Has(b, 8)) { segStart = U32(b); segEnd = U32(b + 4); hasSeg = true; }
            return true;
        case C_KF_OBJECT: case C_KF_AMBIENT: case C_KF_CAMERA: case C_KF_CAMTARGET: case C_KF_LIGHT: case C_KF_LIGHTTARGET: case C_KF_SPOT:
            // Cameras, lights and their targets share the node numbering with the objects, so they are
            // nodes here too (position tracks read; lens, roll, colour and cone tracks are not).
            if (kfs.size() >= kMaxKfNodes) return Fail("3DS: too many keyframe nodes");
            if (id != C_KF_OBJECT) ++kfOther;
            kfs.emplace_back(); curKf = (int)kfs.size() - 1;
            return Walk(b, e, depth + 1, id);
        case C_NODE_HDR: {
            if (curKf < 0) return true;
            std::size_t p = b; std::string s;
            if (!CStr(p, e, s)) return true;
            KfRec& k = kfs[(std::size_t)curKf];
            k.name = s;
            if (e - p >= 6) k.parent = U16(p + 4);
            return true;
        }
        case C_NODE_INSTANCE:
            if (curKf >= 0) { std::size_t p = b; std::string s; if (CStr(p, e, s)) kfs[(std::size_t)curKf].instance = s; }
            return true;
        case C_NODE_PIVOT:
            if (curKf >= 0 && e - b >= 12 && Has(b, 12)) {
                const Vec3 v{F32(b), F32(b + 4), F32(b + 8)};
                if (InRange(v)) kfs[(std::size_t)curKf].pivot = v;
            }
            return true;
        case C_NODE_ID:
            if (curKf >= 0 && e - b >= 2) kfs[(std::size_t)curKf].id = U16(b);
            return true;
        case C_TRACK_POS: case C_TRACK_ROT: case C_TRACK_SCL:
            return Track(id, b, e);
        default:
            return true;   // unknown chunks are skipped by length
        }
    }

    bool Track(std::uint16_t id, std::size_t b, std::size_t e) {
        if (curKf < 0 || e - b < 14) return true;
        KfRec& k = kfs[(std::size_t)curKf];
        const std::size_t cnt = U32(b + 10);
        const std::size_t valBytes = id == C_TRACK_ROT ? 16 : 12;
        std::size_t p = b + 14;
        for (std::size_t i = 0; i < cnt; ++i) {
            if (e - p < 6) { ++truncatedTracks; break; }
            const std::uint32_t frame = U32(p);
            const std::uint16_t flags = U16(p + 4);
            p += 6;
            int spl = 0;
            for (int bit = 0; bit < 5; ++bit) if (flags & (1u << bit)) ++spl;
            const std::size_t need = (std::size_t)spl * 4 + valBytes;
            if (e - p < need) { ++truncatedTracks; break; }
            p += (std::size_t)spl * 4;
            if (++totalKeys > kMaxTotalKeys) return Fail("3DS: too many keys");
            if (id == C_TRACK_ROT) {
                KeyR kr{frame, Vec3{F32(p + 4), F32(p + 8), F32(p + 12)}, F32(p)};
                if (!std::isfinite(kr.angle) || !InRange(kr.axis)) { kr.angle = 0.0f; kr.axis = Vec3{0, 0, 1}; }
                k.rot.push_back(kr);
            } else {
                Vec3 v{F32(p), F32(p + 4), F32(p + 8)};
                if (!InRange(v)) v = id == C_TRACK_POS ? Vec3{0, 0, 0} : Vec3{1, 1, 1};
                (id == C_TRACK_POS ? k.pos : k.scl).push_back(KeyV{frame, v});
            }
            p += valBytes;
        }
        return true;
    }

    // ----------------------------------------------------------------------
    // Building the model
    // ----------------------------------------------------------------------
    ImportedModel* out = nullptr;
    std::vector<Vec3> nodePivot;                 // per ImportedModel node
    std::vector<std::string> nodeMeshName;       // per node: the mesh object it carries ("" = none)
    std::vector<int> kfNode;                     // kfs index -> node index (-1 = none)
    int badCycles = 0;

    static Mat4 UpMatrix(float s) {
        // 3DS is Z up, the engine Y up: (x, y, z) -> (x, z, -y), times the master scale.
        // Columns are the images of the x, y and z axes.
        Mat4 m;
        m.at(0, 0) = s; m.at(1, 1) = 0; m.at(2, 2) = 0;
        m.at(1, 2) = -s;
        m.at(2, 1) = s;
        return m;
    }

    static float AngleAt(const Vec3& a, const Vec3& b, const Vec3& c) {
        const Vec3 u = b - a, v = c - a;
        const float lu = u.Magnitude(), lv = v.Magnitude();
        if (!(lu > 1e-20f) || !(lv > 1e-20f)) return 0.0f;
        const float cs = Vec3::Dot(u, v) / (lu * lv);
        return std::acos(std::min(1.0f, std::max(-1.0f, cs)));
    }

    static std::string ResolveTexture(const std::string& name, const std::string& baseDir) {
        std::string t = name;
        for (char& c : t) if (c == '\\') c = '/';
        if (t.empty()) return t;
        if (t.size() > 1 && (t[0] == '/' || t[1] == ':')) return t;
        const std::string direct = baseDir + t;
        namespace fs = std::filesystem;
        std::error_code ec;
        if (fs::exists(fs::path(direct), ec)) return direct;
        // 3DS names are 8.3 upper case; the file on disk may be lower case.
        if (t.find('/') == std::string::npos) {
            std::string want = t;
            for (char& c : want) c = (char)std::tolower((unsigned char)c);
            fs::directory_iterator it(baseDir.empty() ? fs::path(".") : fs::path(baseDir), ec), endIt;
            for (std::size_t seen = 0; !ec && it != endIt && seen < 20000; it.increment(ec), ++seen) {
                std::string f = it->path().filename().string();
                for (char& c : f) c = (char)std::tolower((unsigned char)c);
                if (f == want) return baseDir + it->path().filename().string();
            }
        }
        return direct;
    }

    // Per-corner smooth normals from the smoothing groups (3 entries per face). Corners of
    // faces whose groups share a bit share their vertex's accumulated normal; group 0 is flat;
    // a file without a smoothing group chunk is smoothed as one group.
    void CornerNormals(const ObjRec& o, const std::vector<char>& faceOk, std::vector<Vec3>& cornerN) {
        const std::size_t nf = o.faces.size(), np = o.pts.size();
        cornerN.assign(nf * 3, Vec3{0, 0, 1});
        std::vector<Vec3> fn(nf, Vec3{0, 0, 1});
        std::vector<std::array<float, 3>> ang(nf, std::array<float, 3>{0, 0, 0});
        std::vector<std::uint32_t> off(np + 1, 0);
        for (std::size_t f = 0; f < nf; ++f) {
            if (!faceOk[f]) continue;
            const auto& F = o.faces[f];
            const Vec3 &a = o.pts[F[0]], &b = o.pts[F[1]], &c = o.pts[F[2]];
            const Vec3 nn = Vec3::Cross(b - a, c - a);
            const float l = nn.Magnitude();
            fn[f] = l > 1e-30f ? nn / l : Vec3{0, 0, 1};
            ang[f] = {AngleAt(a, b, c), AngleAt(b, c, a), AngleAt(c, a, b)};
            for (std::size_t k = 0; k < 3; ++k) ++off[F[k] + 1];
        }
        for (std::size_t i = 0; i < np; ++i) off[i + 1] += off[i];
        std::vector<std::uint32_t> fill(off.begin(), off.end() - 1), inc(off[np]);   // incident corners: f * 3 + k
        for (std::size_t f = 0; f < nf; ++f) {
            if (!faceOk[f]) continue;
            for (std::size_t k = 0; k < 3; ++k) inc[fill[o.faces[f][k]]++] = (std::uint32_t)(f * 3 + k);
        }
        std::vector<std::pair<std::uint32_t, Vec3>> sums;
        for (std::size_t p = 0; p < np; ++p) {
            sums.clear();
            for (std::uint32_t q = off[p]; q < off[p + 1]; ++q) {
                const std::size_t f = inc[q] / 3, k = inc[q] % 3;
                const std::uint32_t sg = o.hasSg ? o.sg[f] : 1u;
                if (sg == 0) continue;
                const Vec3 w = fn[f] * ang[f][k];
                bool found = false;
                for (auto& s : sums) if (s.first == sg) { s.second = s.second + w; found = true; break; }
                if (found) continue;
                if (sums.size() >= kMaxSgPerVertex) { ++sgOverflow; sums[0].second = sums[0].second + w; }
                else sums.emplace_back(sg, w);
            }
            for (std::uint32_t q = off[p]; q < off[p + 1]; ++q) {
                const std::size_t f = inc[q] / 3, k = inc[q] % 3;
                const std::uint32_t sg = o.hasSg ? o.sg[f] : 1u;
                Vec3 nn = fn[f];
                if (sg != 0) {
                    Vec3 acc{0, 0, 0};
                    for (const auto& s : sums) if (s.first & sg) acc = acc + s.second;
                    const float l = acc.Magnitude();
                    if (l > 1e-20f && std::isfinite(l)) nn = acc / l;
                }
                cornerN[f * 3 + k] = nn;
            }
        }
    }

    struct VKey {
        std::uint32_t p, nx, ny, nz;
        bool operator==(const VKey& o) const { return p == o.p && nx == o.nx && ny == o.ny && nz == o.nz; }
    };
    struct VKeyHash {
        std::size_t operator()(const VKey& k) const {
            std::uint64_t h = 1469598103934665603ull;
            for (std::uint32_t v : {k.p, k.nx, k.ny, k.nz}) { h ^= v; h *= 1099511628211ull; }
            return (std::size_t)h;
        }
    };
    static std::uint32_t Bits(float f) { if (f == 0.0f) f = 0.0f; std::uint32_t u; std::memcpy(&u, &f, 4); return u; }

    static Quat AxisAngle(const Vec3& axis, float angle) {
        const float l = axis.Magnitude();
        if (!(l > 1e-12f)) return Quat{};
        // 3DS turns clockwise for a positive angle (checked against a real animated file), so the angle is negated
        const float h = -angle * 0.5f, s = std::sin(h) / l;
        return Quat{axis.x * s, axis.y * s, axis.z * s, std::cos(h)}.Normalized();
    }

    // First key of each track = the node's rest placement (parent space, the file's axes).
    void RestTRS(const KfRec& k, Vec3& p, Quat& q, Vec3& s) const {
        p = k.pos.empty() ? Vec3{0, 0, 0} : k.pos.front().v;
        s = k.scl.empty() ? Vec3{1, 1, 1} : k.scl.front().v;
        q = k.rot.empty() ? Quat{} : AxisAngle(k.rot.front().axis, k.rot.front().angle);
    }

    // Keyframe nodes -> nodes of the model: root first, parents before children.
    void BuildNodes() {
        ImportedModel& m = *out;
        const std::size_t nk = kfs.size();
        kfNode.assign(nk, -1);
        std::unordered_map<int, int> idToKf;
        for (std::size_t i = 0; i < nk; ++i) {
            const int id = kfs[i].id >= 0 ? kfs[i].id : (int)i;
            idToKf.emplace(id, (int)i);
        }
        std::vector<int> parentKf(nk, -1);
        for (std::size_t i = 0; i < nk; ++i) {
            if (kfs[i].parent == 0xFFFF) continue;
            auto it = idToKf.find(kfs[i].parent);
            if (it != idToKf.end() && it->second != (int)i) parentKf[i] = it->second;
        }
        // break cycles: walk up from every node, at most nk steps
        for (std::size_t i = 0; i < nk; ++i) {
            int j = (int)i; std::size_t steps = 0;
            while (j >= 0 && steps <= nk) { j = parentKf[(std::size_t)j]; ++steps; }
            if (j >= 0) { parentKf[i] = -1; ++badCycles; }
        }
        // order: parents first (depth-first, iterative)
        std::vector<int> order; order.reserve(nk);
        std::vector<char> done(nk, 0);
        for (std::size_t i = 0; i < nk; ++i) {
            std::vector<int> chain;
            int j = (int)i;
            while (j >= 0 && !done[(std::size_t)j]) { chain.push_back(j); j = parentKf[(std::size_t)j]; }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) { done[(std::size_t)*it] = 1; order.push_back(*it); }
        }
        std::unordered_map<std::string, int> nameUse;
        std::unordered_map<std::string, int> meshByName;
        for (std::size_t i = 0; i < objs.size(); ++i) if (objs[i].tri && !objs[i].name.empty()) meshByName.emplace(objs[i].name, (int)i);
        nodeMeshName.assign(1, std::string());
        nodePivot.assign(1, Vec3{0, 0, 0});
        for (int ki : order) {
            const KfRec& k = kfs[(std::size_t)ki];
            ImportedNode nd;
            std::string nm = k.name.empty() ? "node" : k.name;
            if (!k.instance.empty()) nm += "." + k.instance;
            if (int c = nameUse[nm]++) nm += "_" + std::to_string(c);
            nd.name = nm;
            const int pk = parentKf[(std::size_t)ki];
            nd.parent = pk >= 0 ? kfNode[(std::size_t)pk] : 0;
            RestTRS(k, nd.position, nd.rotation, nd.scale);
            const int ni = (int)m.nodes.size();
            m.nodes.push_back(nd);
            kfNode[(std::size_t)ki] = ni;
            nodeMeshName.push_back(meshByName.count(k.name) ? k.name : std::string());
            nodePivot.push_back(k.pivot);
        }
    }

    void BuildClip() {
        ImportedModel& m = *out;
        std::uint32_t base = hasSeg ? segStart : 0;
        for (const KfRec& k : kfs) {
            for (const KeyV& x : k.pos) base = std::min(base, x.frame);
            for (const KeyV& x : k.scl) base = std::min(base, x.frame);
            for (const KeyR& x : k.rot) base = std::min(base, x.frame);
        }
        ImportedClip clip;
        clip.name = "animation";
        auto T = [&](std::uint32_t f) { return (float)(f - base) / kFps; };
        for (std::size_t i = 0; i < kfs.size(); ++i) {
            const KfRec& k = kfs[i];
            if (kfNode[i] < 0) continue;
            // a node with a single key per track is just its placement (already the node's rest transform)
            if (k.pos.size() < 2 && k.rot.size() < 2 && k.scl.size() < 2) continue;
            ImportedChannel ch;
            ch.node = m.nodes[(std::size_t)kfNode[i]].name;
            for (const KeyV& x : k.pos) ch.position.push_back({T(x.frame), x.v});
            for (const KeyV& x : k.scl) ch.scale.push_back({T(x.frame), x.v});
            // 3DS rotation keys are relative to the previous key: accumulate them.
            Quat acc;
            bool first = true;
            for (const KeyR& x : k.rot) {
                const Quat dq = AxisAngle(x.axis, x.angle);
                acc = first ? dq : (acc * dq).Normalized();
                first = false;
                ch.rotation.push_back({T(x.frame), acc});
            }
            clip.channels.push_back(std::move(ch));
        }
        if (!clip.channels.empty()) m.clips.push_back(std::move(clip));
    }

    bool Run(const std::string& bytes, ImportedModel& model) {
        out = &model;
        d = (const unsigned char*)bytes.data();
        n = bytes.size();
        if (n > kMaxFileBytes) return Fail("file too large");
        if (n < 6 || U16(0) != C_MAIN) return Fail("3DS: not a 3D Studio file (no main chunk)");
        if (U32(2) < 6) return Fail("3DS: bad main chunk length");
        if (!Walk(0, n, 0, 0)) return false;

        std::unordered_map<std::string, int> matIndex;
        for (const MatRec& mr : mats) {
            ImportedMaterial im;
            im.name = mr.name;
            if (mr.diffuse.has) { im.hasDiffuse = true; im.diffuse = Color{mr.diffuse.c[0], mr.diffuse.c[1], mr.diffuse.c[2], 1.0f}; }
            if (mr.selfIllum > 0.0f && mr.diffuse.has) {
                const float s = std::min(1.0f, mr.selfIllum / 100.0f);
                im.hasEmissive = true; im.emissive = Color{mr.diffuse.c[0] * s, mr.diffuse.c[1] * s, mr.diffuse.c[2] * s, 1.0f};
            }
            if (mr.shininess >= 0.0f) im.shininess = std::min(1000.0f, mr.shininess);
            if (!mr.texDiffuse.empty()) im.texture = ResolveTexture(mr.texDiffuse, baseDir);
            if (!mr.texBump.empty()) im.normalMap = ResolveTexture(mr.texBump, baseDir);
            if (!mr.texSpecular.empty()) im.specularMap = ResolveTexture(mr.texSpecular, baseDir);
            matIndex.emplace(mr.name, (int)model.materials.size());   // the first of a name wins
            model.materials.push_back(std::move(im));
        }

        // Geometry in the file's own space, one part per (object, material).
        struct PartGeo { ImportedPart part; std::vector<Vec3> nrm; int obj; };
        std::vector<PartGeo> geo;
        std::size_t outVerts = 0;
        int degenerate = 0, noFaces = 0, uvMismatch = 0;
        for (std::size_t oi = 0; oi < objs.size(); ++oi) {
            const ObjRec& o = objs[oi];
            if (!o.tri) continue;
            if (o.faces.empty() || o.pts.empty()) { ++noFaces; continue; }
            std::vector<char> ok(o.faces.size(), 1);
            for (std::size_t f = 0; f < o.faces.size(); ++f) {
                const auto& F = o.faces[f];
                if (F[0] >= o.pts.size() || F[1] >= o.pts.size() || F[2] >= o.pts.size()) { ok[f] = 0; ++badFaces; }
                else if (F[0] == F[1] || F[1] == F[2] || F[0] == F[2]) { ok[f] = 0; ++degenerate; }
            }
            const bool hasUv = o.uv.size() == o.pts.size();
            if (!o.uv.empty() && !hasUv) ++uvMismatch;
            std::vector<Vec3> cn;
            CornerNormals(o, ok, cn);
            std::vector<int> faceMat(o.faces.size(), -1);   // first material group that lists the face
            for (const MatGroup& g : o.groups) {
                auto it = matIndex.find(g.mat);
                const int mi = it == matIndex.end() ? -1 : it->second;
                for (std::uint16_t f : g.faces) if (f < faceMat.size() && faceMat[f] == -1) faceMat[f] = mi;
            }
            std::vector<int> mats_;   // distinct material ids, first seen first
            for (std::size_t f = 0; f < o.faces.size(); ++f)
                if (ok[f] && std::find(mats_.begin(), mats_.end(), faceMat[f]) == mats_.end()) mats_.push_back(faceMat[f]);
            for (int mi : mats_) {
                if (geo.size() >= kMaxParts) return Fail("3DS: too many mesh parts");
                PartGeo pg; pg.obj = (int)oi;
                pg.part.material = mi;
                pg.part.mesh.name = o.name;
                std::unordered_map<VKey, int, VKeyHash> weld;
                for (std::size_t f = 0; f < o.faces.size(); ++f) {
                    if (!ok[f] || faceMat[f] != mi) continue;
                    for (std::size_t k = 0; k < 3; ++k) {
                        const std::uint16_t pi = o.faces[f][k];
                        const Vec3 nn = cn[f * 3 + k];
                        const VKey key{pi, Bits(nn.x), Bits(nn.y), Bits(nn.z)};
                        auto it = weld.find(key);
                        int vi;
                        if (it == weld.end()) {
                            if (++outVerts > kMaxOutVerts) return Fail("3DS: too many output vertices");
                            vi = (int)pg.part.mesh.vertices.size();
                            pg.part.mesh.vertices.push_back(o.pts[pi]);
                            pg.nrm.push_back(nn);
                            if (hasUv) pg.part.mesh.uvs.push_back(Vec2{o.uv[pi].x, 1.0f - o.uv[pi].y});
                            weld.emplace(key, vi);
                        } else vi = it->second;
                        pg.part.mesh.triangles.push_back(vi);
                    }
                }
                geo.push_back(std::move(pg));
            }
        }
        if (geo.empty()) return Fail("3DS: the file has no mesh");

        const Mat4 up = UpMatrix(masterScale);
        ImportedNode root;
        root.name = "RootNode";
        up.Decompose(root.position, root.rotation, root.scale);

        if (flatten) {
            model.nodes.assign(1, ImportedNode{});
            model.nodes[0].name = root.name;
            for (PartGeo& pg : geo) {
                for (Vec3& v : pg.part.mesh.vertices) { const Vec4 r = up * Vec4{v.x, v.y, v.z, 1.0f}; v = Vec3{r.x, r.y, r.z}; }
                pg.part.mesh.normals = pg.nrm;
                for (Vec3& v : pg.part.mesh.normals) { const Vec4 r = up * Vec4{v.x, v.y, v.z, 0.0f}; v = Vec3{r.x, r.y, r.z}.Normalized(); }
                pg.part.node = 0;
                model.parts.push_back(std::move(pg.part));
            }
        } else {
            model.nodes.assign(1, root);
            BuildNodes();
            std::unordered_map<std::string, std::vector<int>> nodesOfMesh;
            for (std::size_t i = 0; i < nodeMeshName.size(); ++i) if (!nodeMeshName[i].empty()) nodesOfMesh[nodeMeshName[i]].push_back((int)i);
            std::unordered_map<int, int> looseNode;   // object index -> node for meshes without a keyframe node
            for (PartGeo& pg : geo) {
                const ObjRec& o = objs[(std::size_t)pg.obj];
                auto it = nodesOfMesh.find(o.name);
                if (it == nodesOfMesh.end()) {
                    int& ni = looseNode[pg.obj];
                    if (ni == 0) {
                        ni = (int)model.nodes.size();
                        ImportedNode nd; nd.name = o.name.empty() ? "mesh" : o.name; nd.parent = 0;
                        model.nodes.push_back(nd);
                    }
                    pg.part.mesh.normals = pg.nrm;
                    pg.part.node = ni;
                    model.parts.push_back(std::move(pg.part));
                    continue;
                }
                // The vertices are stored in scene space; the mesh matrix maps the object's own space to it
                // (row vectors: world = local * R + t), so local = (stored - t) * R^-1, and the node then
                // places that around its pivot: world = node * (local - pivot).
                double R[3][3], Ri[3][3];
                for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) R[r][c] = o.mm[r * 3 + c];
                const double det = R[0][0] * (R[1][1] * R[2][2] - R[1][2] * R[2][1]) - R[0][1] * (R[1][0] * R[2][2] - R[1][2] * R[2][0]) + R[0][2] * (R[1][0] * R[2][1] - R[1][1] * R[2][0]);
                bool inv = std::isfinite(det) && std::fabs(det) > 1e-18;
                if (inv) {
                    Ri[0][0] = (R[1][1] * R[2][2] - R[1][2] * R[2][1]) / det; Ri[0][1] = (R[0][2] * R[2][1] - R[0][1] * R[2][2]) / det; Ri[0][2] = (R[0][1] * R[1][2] - R[0][2] * R[1][1]) / det;
                    Ri[1][0] = (R[1][2] * R[2][0] - R[1][0] * R[2][2]) / det; Ri[1][1] = (R[0][0] * R[2][2] - R[0][2] * R[2][0]) / det; Ri[1][2] = (R[0][2] * R[1][0] - R[0][0] * R[1][2]) / det;
                    Ri[2][0] = (R[1][0] * R[2][1] - R[1][1] * R[2][0]) / det; Ri[2][1] = (R[0][1] * R[2][0] - R[0][0] * R[2][1]) / det; Ri[2][2] = (R[0][0] * R[1][1] - R[0][1] * R[1][0]) / det;
                } else { ++badMatrix; for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { R[r][c] = Ri[r][c] = r == c ? 1.0 : 0.0; } }
                const double t[3] = {inv ? o.mm[9] : 0.0, inv ? o.mm[10] : 0.0, inv ? o.mm[11] : 0.0};
                for (int ni : it->second) {
                    if (model.parts.size() >= kMaxParts) return Fail("3DS: too many mesh parts");
                    ImportedPart p = pg.part;
                    const Vec3 pv = nodePivot[(std::size_t)ni];
                    for (Vec3& v : p.mesh.vertices) {
                        const double x = v.x - t[0], y = v.y - t[1], z = v.z - t[2];
                        v = Vec3{(float)(x * Ri[0][0] + y * Ri[1][0] + z * Ri[2][0] - pv.x),
                                 (float)(x * Ri[0][1] + y * Ri[1][1] + z * Ri[2][1] - pv.y),
                                 (float)(x * Ri[0][2] + y * Ri[1][2] + z * Ri[2][2] - pv.z)};
                    }
                    p.mesh.normals.resize(pg.nrm.size());
                    for (std::size_t i = 0; i < pg.nrm.size(); ++i) {
                        const Vec3& a = pg.nrm[i];   // inverse transpose of the vertex map is the stored matrix itself
                        p.mesh.normals[i] = Vec3{(float)(R[0][0] * a.x + R[0][1] * a.y + R[0][2] * a.z),
                                                 (float)(R[1][0] * a.x + R[1][1] * a.y + R[1][2] * a.z),
                                                 (float)(R[2][0] * a.x + R[2][1] * a.y + R[2][2] * a.z)}.Normalized();
                    }
                    p.node = ni;
                    model.parts.push_back(std::move(p));
                }
            }
            BuildClip();
        }
        // Extreme (but individually valid) values can still overflow in the matrix arithmetic above.
        for (const ImportedPart& p : model.parts) {
            for (const Vec3& v : p.mesh.vertices) if (!InRange(v)) return Fail("3DS: coordinates overflow");
            for (const Vec3& v : p.mesh.normals) if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return Fail("3DS: coordinates overflow");
        }
        for (const ImportedNode& nd : model.nodes)
            if (!InRange(nd.position) || !InRange(nd.scale) || !std::isfinite(nd.rotation.x + nd.rotation.y + nd.rotation.z + nd.rotation.w)) return Fail("3DS: transform overflow");
        Notes(degenerate, noFaces, uvMismatch);
        return true;
    }

    void Notes(int degenerate, int noFaces, int uvMismatch) {
        auto note = [&](const std::string& s) { out->notes.push_back("3DS: " + s); };
        if (lights) note(std::to_string(lights) + " light(s) not imported");
        if (cameras) note(std::to_string(cameras) + " camera(s) not imported");
        if (kfOther) note(std::to_string(kfOther) + " camera/light/ambient/target keyframe node(s) are plain nodes: only their position tracks are read");
        if (ignoredMaps) note(std::to_string(ignoredMaps) + " material map(s) not read (opacity, reflection, second texture, shininess, self illumination)");
        if (texTransforms) note(std::to_string(texTransforms) + " texture tiling/offset/rotation value(s) not applied");
        for (const MatRec& m : mats) if (m.transparency > 0.0f) { note("material transparency is not read (opacity is left at 1)"); break; }
        if (clamped) note(std::to_string(clamped) + " chunk(s) were longer than their parent and were cut short");
        if (badFaces) note(std::to_string(badFaces) + " face(s) with an out-of-range point index skipped");
        if (degenerate) note(std::to_string(degenerate) + " degenerate face(s) skipped");
        if (noFaces) note(std::to_string(noFaces) + " object(s) without points or faces skipped");
        if (uvMismatch) note(std::to_string(uvMismatch) + " object(s) whose UV count differs from the point count: UVs dropped");
        if (shortSg) note(std::to_string(shortSg) + " smoothing group array(s) shorter than the face list (missing faces are flat)");
        if (sgOverflow) note(std::to_string(sgOverflow) + " vertex(es) with more than " + std::to_string(kMaxSgPerVertex) + " distinct smoothing groups: the extra groups were merged");
        if (badMatrix) note(std::to_string(badMatrix) + " mesh matrix(es) not usable (non-finite or singular); the object is placed as stored");
        if (extraMeshChunks) note(std::to_string(extraMeshChunks) + " object(s) with more than one point array: the last one is used");
        if (truncatedTracks) note(std::to_string(truncatedTracks) + " keyframe track(s) shorter than their key count; the keys that fit were read");
        if (badCycles) note(std::to_string(badCycles) + " keyframe node(s) with a parent cycle were moved to the root");

    }
};

} // namespace tds
} // namespace

bool Read3dsBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    tds::Reader r;
    r.baseDir = baseDir;
    r.flatten = flatten;
    const bool ok = r.Run(bytes, out);
    if (!ok) {
        const std::string m = r.err.empty() ? "3DS: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    out.backend = "own";
    return true;
}

bool Read3dsFile(const std::string& path, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) { if (err) *err = "cannot open file"; return false; }
    std::string bytes;
    char buf[65536];
    for (;;) {
        const std::size_t k = std::fread(buf, 1, sizeof(buf), fp);
        if (k == 0) break;
        if (bytes.size() + k > tds::kMaxFileBytes) { std::fclose(fp); if (err) *err = "file too large"; return false; }
        bytes.append(buf, k);
    }
    std::fclose(fp);
    const std::size_t sl = path.find_last_of("/\\");
    return Read3dsBuffer(bytes, sl == std::string::npos ? std::string() : path.substr(0, sl + 1), out, err, flatten);
}

} // namespace okay
