// Our own X3D reader (XML encoding). Written from the public X3D abstract and XML encoding specifications
// (ISO/IEC 19775 / 19776), no code from any other importer. See docs/own_x3d_reader.md.
#include "FileOpen.hpp"
#include "PolygonTriangulate.hpp"
#include "ReaderCommon.hpp"
#include "XmlLite.hpp"
#include "okay/Render/X3dReader.hpp"
#include <map>
#include <set>
#include <unordered_map>

namespace okay {

namespace {
namespace x3d {

using namespace okay::detail;
using rc::M4;

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxVisits    = 1000000;     // scene graph nodes visited, DEF/USE expansions included
constexpr int         kMaxDepth     = 128;
constexpr std::size_t kMaxPoints    = 20000000;
constexpr std::size_t kMaxIndices   = 60000000;
constexpr std::size_t kMaxTris      = 20000000;
constexpr std::size_t kMaxParts     = 100000;
constexpr int         kSegments     = 24;
constexpr double      kPi           = 3.14159265358979323846;

bool InvTranspose3(const M4& a, double n[9]) {   // inverse transpose of the upper 3x3, row-major
    const double m00 = a.at(0, 0), m01 = a.at(1, 0), m02 = a.at(2, 0), m10 = a.at(0, 1), m11 = a.at(1, 1), m12 = a.at(2, 1), m20 = a.at(0, 2), m21 = a.at(1, 2), m22 = a.at(2, 2);
    const double c00 = m11 * m22 - m12 * m21, c01 = m12 * m20 - m10 * m22, c02 = m10 * m21 - m11 * m20;
    const double det = m00 * c00 + m01 * c01 + m02 * c02;
    if (!(std::fabs(det) > 1e-300) || !std::isfinite(det)) return false;
    const double c10 = m02 * m21 - m01 * m22, c11 = m00 * m22 - m02 * m20, c12 = m01 * m20 - m00 * m21;
    const double c20 = m01 * m12 - m02 * m11, c21 = m02 * m10 - m00 * m12, c22 = m00 * m11 - m01 * m10;
    n[0] = c00 / det; n[1] = c01 / det; n[2] = c02 / det; n[3] = c10 / det; n[4] = c11 / det; n[5] = c12 / det; n[6] = c20 / det; n[7] = c21 / det; n[8] = c22 / det;
    return true;
}

struct Geo {   // geometry in object space, before the transform
    std::vector<double> pos;                 // x y z
    std::vector<std::uint32_t> tri;          // 3 per triangle: position index
    std::vector<std::uint32_t> triCorner;    // 3 per triangle: index of the corner in the file's index arrays (or the position index)
    std::vector<std::uint32_t> triFace;      // per triangle: face number
    std::vector<Vec2> cuv;                   // per triangle corner, or empty
    std::vector<Vec3> cn;                    // per triangle corner, or empty
    double creaseDeg = 0;
    bool ccw = true;
};

struct Reader {
    std::string err, baseDir;
    ImportedModel* out = nullptr;
    xml::Doc d;
    std::map<std::string, int> defs;
    std::set<int> active;
    std::map<int, int> matOf;              // Appearance node -> ImportedMaterial
    std::map<std::string, int> skipped;    // unsupported node name -> count
    int visits = 0, cyclic = 0, badFaces = 0, badNormals = 0, badUv = 0, colors = 0, switchNone = 0, texTransform = 0, otherTex = 0, degenerate = 0, billboards = 0, badRefs = 0, shapes = 0, emptyShapes = 0;
    std::size_t totalPoints = 0, totalTris = 0;
    bool flatten = false;

    bool Fail(const std::string& m) { if (err.empty()) err = "X3D: " + m; return false; }

    int Resolve(int i) {
        const std::string* u = d.Attr(i, "USE");
        if (!u) return i;
        auto it = defs.find(*u);
        if (it == defs.end()) { ++badRefs; return -1; }
        return it->second;
    }
    int Child(int i, std::initializer_list<const char*> names) {
        for (int k : d.Kids(i)) {
            const int r = Resolve(k);
            if (r < 0) continue;
            for (const char* n : names) if (d.Name(r) == n) return r;
        }
        return -1;
    }
    bool Floats(int node, const char* attr, std::vector<double>& v, std::size_t maxCount, std::size_t mult) {
        v.clear();
        const std::string* s = d.Attr(node, attr);
        if (!s) return true;
        if (!rc::ParseList(*s, v, maxCount, true) || v.size() % mult) return Fail(std::string("a ") + d.Name(node) + " has an invalid " + attr + " list");
        return true;
    }
    bool Vec(int i, const char* attr, double* v, int n) {   // an optional fixed-size field; a malformed one is an error
        const std::string* s = d.Attr(i, attr);
        if (!s) return true;
        std::vector<double> l;
        if (!rc::ParseList(*s, l, (std::size_t)n, true) || (int)l.size() != n) return Fail(std::string("an invalid ") + attr + " value");
        for (int k = 0; k < n; ++k) v[k] = l[(std::size_t)k];
        return true;
    }
    bool Num1(int i, const char* attr, double& v) {
        const std::string* s = d.Attr(i, attr);
        if (!s) return true;
        if (!rc::ParseNumber(*s, v)) return Fail(std::string("an invalid ") + attr + " value");
        return true;
    }

    // ---- appearance ----
    static std::string FirstUrl(const std::string& s) {
        // MFString: "a.png" "b.png" (quoted), or a bare name
        const std::size_t q = s.find('"');
        if (q == std::string::npos) { std::size_t a = 0, b = s.size(); while (a < b && rc::Ws(s[a])) ++a; while (b > a && rc::Ws(s[b - 1])) --b; return s.substr(a, b - a); }
        const std::size_t e = s.find('"', q + 1);
        return e == std::string::npos ? std::string() : s.substr(q + 1, e - q - 1);
    }
    static std::string ResolvePath(std::string t, const std::string& baseDir) {
        std::string r;
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '%' && i + 2 < t.size() + 0 && std::isxdigit((unsigned char)t[i + 1]) && std::isxdigit((unsigned char)t[i + 2])) { r += (char)std::strtol(t.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
            else r += t[i];
        }
        for (char& c : r) if (c == '\\') c = '/';
        if (r.compare(0, 8, "file:///") == 0) { r = r.substr(8); if (!(r.size() > 1 && r[1] == ':')) r = "/" + r; }
        else if (r.compare(0, 7, "file://") == 0) r = r.substr(7);
        const bool abs = !r.empty() && (r[0] == '/' || (r.size() > 1 && r[1] == ':'));
        return abs ? r : baseDir + r;
    }
    int MaterialOf(int app) {
        if (app < 0) return -1;
        auto it = matOf.find(app);
        if (it != matOf.end()) return it->second;
        ImportedMaterial m;
        m.name = d.AttrOr(app, "DEF");
        bool any = false;
        const int mat = Child(app, {"Material"});
        if (mat >= 0) {
            double dc[3] = {0.8, 0.8, 0.8}, ec[3] = {0, 0, 0}, sh = 0.2, tr = 0;
            if (!Vec(mat, "diffuseColor", dc, 3) || !Vec(mat, "emissiveColor", ec, 3) || !Num1(mat, "shininess", sh) || !Num1(mat, "transparency", tr)) return -2;
            auto cl = [](double v) { return (float)std::max(0.0, std::min(1.0, v)); };
            m.hasDiffuse = true; m.diffuse = Color(cl(dc[0]), cl(dc[1]), cl(dc[2]), cl(1.0 - tr));
            if (ec[0] + ec[1] + ec[2] > 0) { m.hasEmissive = true; m.emissive = Color(cl(ec[0]), cl(ec[1]), cl(ec[2]), 1.0f); }
            m.shininess = (float)(std::max(0.0, std::min(1.0, sh)) * 128.0);
            if (m.name.empty()) m.name = d.AttrOr(mat, "DEF");
            any = true;
        }
        if (Child(app, {"TwoSidedMaterial", "PhysicalMaterial", "UnlitMaterial"}) >= 0) ++skipped["TwoSidedMaterial/PhysicalMaterial/UnlitMaterial"];
        const int tex = Child(app, {"ImageTexture"});
        if (tex >= 0) {
            const std::string url = FirstUrl(d.AttrOr(tex, "url"));
            if (!url.empty() && url.compare(0, 4, "http") != 0 && url.compare(0, 5, "data:") != 0) { m.texture = ResolvePath(url, baseDir); any = true; }
            else if (!url.empty()) ++skipped["ImageTexture(remote url)"];
        }
        if (Child(app, {"TextureTransform"}) >= 0) ++texTransform;
        if (Child(app, {"PixelTexture", "MovieTexture", "MultiTexture", "ComposedCubeMapTexture", "ImageCubeMapTexture"}) >= 0) ++otherTex;
        if (m.name.empty()) m.name = "material" + std::to_string(out->materials.size());
        if (!any) { matOf[app] = -1; return -1; }
        out->materials.push_back(m);
        const int idx = (int)out->materials.size() - 1;
        matOf[app] = idx;
        return idx;
    }

    // ---- geometry ----
    bool Primitive(int g, const std::string& nm, Geo& geo, std::vector<Vec3>& nrm) {
        // explicit normals / uv per corner, positions per triangle corner (no sharing: every corner is its own position)
        std::map<std::array<double, 3>, std::uint32_t> posIndex;   // equal positions share an index, so corners with equal normal and UV weld
        auto addTri = [&](const double* a, const double* b, const double* c, const double* na, const double* nb, const double* nc, Vec2 ua, Vec2 ub, Vec2 uc) {
            const double* p[3] = {a, b, c}; const double* n[3] = {na, nb, nc}; const Vec2 u[3] = {ua, ub, uc};
            for (int k = 0; k < 3; ++k) {
                const std::array<double, 3> key{p[k][0], p[k][1], p[k][2]};
                auto it = posIndex.find(key);
                std::uint32_t pi;
                if (it == posIndex.end()) { pi = (std::uint32_t)(geo.pos.size() / 3); geo.pos.push_back(p[k][0]); geo.pos.push_back(p[k][1]); geo.pos.push_back(p[k][2]); posIndex.emplace(key, pi); }
                else pi = it->second;
                geo.tri.push_back(pi); geo.triCorner.push_back((std::uint32_t)geo.triCorner.size());
                geo.cuv.push_back(u[k]);
                nrm.push_back(Vec3{(float)n[k][0], (float)n[k][1], (float)n[k][2]});
            }
            geo.triFace.push_back((std::uint32_t)geo.triFace.size());
        };
        auto addQuad = [&](const double* a, const double* b, const double* c, const double* e, const double* n) {   // counter-clockwise a b c e
            addTri(a, b, c, n, n, n, Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1});
            addTri(a, c, e, n, n, n, Vec2{0, 0}, Vec2{1, 1}, Vec2{0, 1});
        };
        if (nm == "Box") {
            double s[3] = {2, 2, 2};
            if (!Vec(g, "size", s, 3)) return false;
            const double x = s[0] / 2, y = s[1] / 2, z = s[2] / 2;
            const double P[8][3] = {{-x, -y, -z}, {x, -y, -z}, {x, y, -z}, {-x, y, -z}, {-x, -y, z}, {x, -y, z}, {x, y, z}, {-x, y, z}};
            const double N[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
            const int F[6][4] = {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6}, {0, 4, 7, 3}, {7, 6, 2, 3}, {0, 1, 5, 4}};
            for (int f = 0; f < 6; ++f) addQuad(P[F[f][0]], P[F[f][1]], P[F[f][2]], P[F[f][3]], N[f]);
        } else if (nm == "Sphere") {
            double r = 1;
            if (!Num1(g, "radius", r)) return false;
            const int ns = kSegments, nt = kSegments / 2;
            auto pt = [&](int i, int j, double* p, double* n, Vec2& uv) {
                const double th = kPi * j / nt, ph = 2 * kPi * i / ns;
                n[0] = std::sin(th) * std::sin(ph); n[1] = std::cos(th); n[2] = std::sin(th) * std::cos(ph);
                p[0] = r * n[0]; p[1] = r * n[1]; p[2] = r * n[2];
                uv = Vec2{(float)i / ns, 1.0f - (float)j / nt};
            };
            for (int j = 0; j < nt; ++j) for (int i = 0; i < ns; ++i) {
                double p[4][3], n[4][3]; Vec2 u[4];
                pt(i, j, p[0], n[0], u[0]); pt(i + 1, j, p[1], n[1], u[1]); pt(i + 1, j + 1, p[2], n[2], u[2]); pt(i, j + 1, p[3], n[3], u[3]);
                // viewed from outside, counter-clockwise: (i, j) -> (i, j+1) -> (i+1, j+1) -> (i+1, j)
                if (j < nt - 1) addTri(p[0], p[3], p[2], n[0], n[3], n[2], u[0], u[3], u[2]);   // at the north pole p0 == p1: only the first one is a triangle
                if (j > 0) addTri(p[0], p[2], p[1], n[0], n[2], n[1], u[0], u[2], u[1]);    // at the south pole p3 == p2: only the second one
            }
        } else if (nm == "Cylinder" || nm == "Cone") {
            const bool cone = nm == "Cone";
            double r = 1, h = 2;
            if (!Num1(g, cone ? "bottomRadius" : "radius", r) || !Num1(g, "height", h)) return false;
            const bool side = d.AttrOr(g, "side", "true") != "false", bottom = d.AttrOr(g, "bottom", "true") != "false", top = !cone && d.AttrOr(g, "top", "true") != "false";
            const double hy = h / 2, rt = cone ? 0.0 : r;
            const int ns = kSegments;
            const double slope = (r - rt) / std::max(h, 1e-12);
            for (int i = 0; i < ns; ++i) {
                const double a0 = 2 * kPi * i / ns, a1 = 2 * kPi * (i + 1) / ns;
                const double c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
                if (side) {
                    const double pb0[3] = {r * s0, -hy, r * c0}, pb1[3] = {r * s1, -hy, r * c1}, pt0[3] = {rt * s0, hy, rt * c0}, pt1[3] = {rt * s1, hy, rt * c1};
                    double n0[3] = {s0, slope, c0}, n1[3] = {s1, slope, c1};
                    for (double* n : {n0, n1}) { const double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); n[0] /= l; n[1] /= l; n[2] /= l; }
                    const Vec2 ub0{(float)i / ns, 0}, ub1{(float)(i + 1) / ns, 0}, ut0{(float)i / ns, 1}, ut1{(float)(i + 1) / ns, 1};
                    if (!cone) {
                        addTri(pb0, pb1, pt1, n0, n1, n1, ub0, ub1, ut1);
                        addTri(pb0, pt1, pt0, n0, n1, n0, ub0, ut1, ut0);
                    } else {   // the apex: one triangle, its normal is the one halfway between the two edges
                        const double am = (a0 + a1) / 2;
                        double nm2[3] = {std::sin(am), slope, std::cos(am)};
                        const double l = std::sqrt(nm2[0] * nm2[0] + nm2[1] * nm2[1] + nm2[2] * nm2[2]); nm2[0] /= l; nm2[1] /= l; nm2[2] /= l;
                        addTri(pb0, pb1, pt0, n0, n1, nm2, ub0, ub1, Vec2{((float)i + 0.5f) / ns, 1});
                    }
                }
                if (bottom) { const double nb[3] = {0, -1, 0}; const double pa[3] = {0, -hy, 0}, pb[3] = {r * s1, -hy, r * c1}, pc[3] = {r * s0, -hy, r * c0}; addTri(pa, pb, pc, nb, nb, nb, Vec2{0.5f, 0.5f}, Vec2{0.5f + 0.5f * (float)s1, 0.5f + 0.5f * (float)c1}, Vec2{0.5f + 0.5f * (float)s0, 0.5f + 0.5f * (float)c0}); }
                if (top) { const double nt2[3] = {0, 1, 0}; const double pa[3] = {0, hy, 0}, pb[3] = {r * s0, hy, r * c0}, pc[3] = {r * s1, hy, r * c1}; addTri(pa, pb, pc, nt2, nt2, nt2, Vec2{0.5f, 0.5f}, Vec2{0.5f + 0.5f * (float)s0, 0.5f - 0.5f * (float)c0}, Vec2{0.5f + 0.5f * (float)s1, 0.5f - 0.5f * (float)c1}); }
            }
        }
        geo.creaseDeg = 0;
        return true;
    }


    // Extrusion: a cross-section swept along a spine. The spine-aligned cross-section plane (SCP) of every spine point is
    // built as the X3D specification describes: Y along the spine, Z from the turn of the spine (taken from a neighbour
    // where the spine is straight, flipped when it would jump), X = Y x Z; a straight spine turns the Y axis to the spine.
    bool Extrusion(int g, Geo& geo) {
        std::vector<double> cs = {1, 1, 1, -1, -1, -1, -1, 1, 1, 1}, sp = {0, 0, 0, 0, 1, 0}, sc = {1, 1}, ori = {0, 0, 1, 0};
        auto list = [&](const char* name, std::vector<double>& v, std::size_t mult) {
            const std::string* t = d.Attr(g, name);
            if (!t) return true;
            std::vector<double> l;
            if (!rc::ParseList(*t, l, 20000000, true) || l.empty() || l.size() % mult) return Fail(std::string("an Extrusion with an invalid ") + name + " list");
            v = std::move(l);
            return true;
        };
        if (!list("crossSection", cs, 2) || !list("spine", sp, 3) || !list("scale", sc, 2) || !list("orientation", ori, 4)) return false;
        const std::size_t nc = cs.size() / 2, ns = sp.size() / 3;
        if (nc < 2 || ns < 2) return true;   // nothing to sweep
        if (nc * ns > 4000000) return Fail("an Extrusion is too large");
        double crease = 0;
        if (!Num1(g, "creaseAngle", crease)) return false;
        geo.creaseDeg = std::max(0.0, std::min(180.0, crease * 180.0 / kPi));
        geo.ccw = d.AttrOr(g, "ccw", "true") != "false";
        const bool beginCap = d.AttrOr(g, "beginCap", "true") != "false", endCap = d.AttrOr(g, "endCap", "true") != "false";
        auto near = [](const double* a, const double* b) { return std::fabs(a[0] - b[0]) < 1e-9 && std::fabs(a[1] - b[1]) < 1e-9 && std::fabs(a[2] - b[2]) < 1e-9; };
        const bool closedSpine = ns > 2 && near(&sp[0], &sp[(ns - 1) * 3]);
        const bool closedCs = nc > 2 && std::fabs(cs[0] - cs[(nc - 1) * 2]) < 1e-9 && std::fabs(cs[1] - cs[(nc - 1) * 2 + 1]) < 1e-9;
        using V = std::array<double, 3>;
        auto sub = [](const V& a, const V& b) { return V{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
        auto cross = [](const V& a, const V& b) { return V{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; };
        auto dot = [](const V& a, const V& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
        auto len = [&](const V& a) { return std::sqrt(dot(a, a)); };
        auto norm = [&](const V& a) { const double l = len(a); return l > 1e-300 ? V{a[0] / l, a[1] / l, a[2] / l} : V{0, 0, 0}; };
        auto P = [&](std::size_t i) { return V{sp[i * 3], sp[i * 3 + 1], sp[i * 3 + 2]}; };
        std::vector<V> Y(ns), Z(ns);
        std::vector<char> hasZ(ns, 0);
        for (std::size_t i = 0; i < ns; ++i) {
            if (i == 0 || i == ns - 1) Y[i] = closedSpine ? sub(P(1), P(ns - 2)) : (i == 0 ? sub(P(1), P(0)) : sub(P(ns - 1), P(ns - 2)));
            else Y[i] = sub(P(i + 1), P(i - 1));
            V z{0, 0, 0};
            if (i > 0 && i < ns - 1) z = cross(sub(P(i + 1), P(i)), sub(P(i - 1), P(i)));
            else if (closedSpine) z = cross(sub(P(1), P(0)), sub(P(ns - 2), P(0)));
            Z[i] = z; hasZ[i] = len(z) > 1e-12;
        }
        bool anyZ = false;
        for (std::size_t i = 0; i < ns; ++i) anyZ = anyZ || hasZ[i];
        if (!closedSpine) { if (hasZ[1]) { Z[0] = Z[1]; hasZ[0] = 1; } if (hasZ[ns - 2]) { Z[ns - 1] = Z[ns - 2]; hasZ[ns - 1] = 1; } }
        std::vector<V> X(ns);
        if (anyZ) {
            std::size_t first = 0; while (!hasZ[first]) ++first;
            for (std::size_t i = 0; i < ns; ++i) { if (!hasZ[i]) { Z[i] = i == 0 ? Z[first] : Z[i - 1]; } }
            for (std::size_t i = 1; i < ns; ++i) if (dot(Z[i], Z[i - 1]) < 0) Z[i] = V{-Z[i][0], -Z[i][1], -Z[i][2]};
            for (std::size_t i = 0; i < ns; ++i) {
                Y[i] = norm(Y[i]); Z[i] = norm(Z[i]);
                X[i] = norm(cross(Y[i], Z[i]));
                Z[i] = norm(cross(X[i], Y[i]));
            }
        } else {   // a straight spine: turn the Y axis onto it
            for (std::size_t i = 0; i < ns; ++i) {
                const V y = norm(Y[i]);
                const V ax = cross(V{0, 1, 0}, y);
                const double c = y[1];
                M4 r;
                if (len(ax) > 1e-12) r = rc::AxisAngle(ax[0], ax[1], ax[2], std::acos(std::max(-1.0, std::min(1.0, c))));
                else if (c < 0) r = rc::AxisAngle(1, 0, 0, kPi);
                X[i] = V{r.at(0, 0), r.at(0, 1), r.at(0, 2)}; Z[i] = V{r.at(2, 0), r.at(2, 1), r.at(2, 2)}; Y[i] = y;
            }
        }
        // the rings of points; a closed cross-section drops its repeated last point, a closed spine reuses ring 0 for the last
        const std::size_t rc_ = closedCs ? nc - 1 : nc;
        const std::size_t rings = closedSpine ? ns - 1 : ns;
        std::vector<std::uint32_t> idx(rings * rc_);
        for (std::size_t i = 0; i < rings; ++i) {
            const double s0 = sc.size() >= 2 * (i + 1) ? sc[i * 2] : sc[(sc.size() / 2 - 1) * 2], s1 = sc.size() >= 2 * (i + 1) ? sc[i * 2 + 1] : sc[(sc.size() / 2 - 1) * 2 + 1];
            const std::size_t oi = std::min(i, ori.size() / 4 - 1);
            const M4 R = rc::AxisAngle(ori[oi * 4], ori[oi * 4 + 1], ori[oi * 4 + 2], ori[oi * 4 + 3]);
            for (std::size_t j = 0; j < rc_; ++j) {
                double vx, vy, vz;
                rc::Apply(R, s0 * cs[j * 2], 0, s1 * cs[j * 2 + 1], vx, vy, vz);
                const V p = P(i);
                idx[i * rc_ + j] = (std::uint32_t)(geo.pos.size() / 3);
                geo.pos.push_back(p[0] + X[i][0] * vx + Y[i][0] * vy + Z[i][0] * vz);
                geo.pos.push_back(p[1] + X[i][1] * vx + Y[i][1] * vy + Z[i][1] * vz);
                geo.pos.push_back(p[2] + X[i][2] * vx + Y[i][2] * vy + Z[i][2] * vz);
            }
        }
        auto tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
            for (std::uint32_t v : {a, b, c}) { geo.tri.push_back(v); geo.triCorner.push_back((std::uint32_t)geo.triCorner.size()); }
            geo.triFace.push_back((std::uint32_t)geo.triFace.size());
        };
        const std::size_t segs = ns - 1;
        const std::size_t cseg = closedCs ? rc_ : nc - 1;
        for (std::size_t i = 0; i < segs; ++i) {
            const std::size_t i1 = closedSpine ? (i + 1) % rings : i + 1;
            for (std::size_t j = 0; j < cseg; ++j) {
                const std::size_t j1 = (j + 1) % rc_;
                const std::uint32_t a = idx[i * rc_ + j], b = idx[i * rc_ + j1], c = idx[i1 * rc_ + j1], e = idx[i1 * rc_ + j];
                tri(a, b, c); tri(a, c, e);
            }
        }
        if (closedCs && !closedSpine) {
            for (int cap = 0; cap < 2; ++cap) {
                if (!(cap == 0 ? beginCap : endCap)) continue;
                const std::size_t ring = cap == 0 ? 0 : rings - 1;
                std::vector<Vec3> poly;
                std::vector<std::uint32_t> ids;
                for (std::size_t k = 0; k < rc_; ++k) {
                    const std::size_t j = cap == 0 ? rc_ - 1 - k : k;   // the begin cap faces against the spine
                    const std::uint32_t pi = idx[ring * rc_ + j];
                    poly.push_back(Vec3{(float)geo.pos[pi * 3], (float)geo.pos[pi * 3 + 1], (float)geo.pos[pi * 3 + 2]});
                    ids.push_back((std::uint32_t)(geo.pos.size() / 3));
                    geo.pos.push_back(geo.pos[pi * 3]); geo.pos.push_back(geo.pos[pi * 3 + 1]); geo.pos.push_back(geo.pos[pi * 3 + 2]);
                }
                std::vector<std::array<int, 3>> tris;
                detail::TriangulatePolygon(poly, tris);
                for (const auto& t : tris) tri(ids[(std::size_t)t[0]], ids[(std::size_t)t[1]], ids[(std::size_t)t[2]]);
            }
        }
        return true;
    }

    bool IndexedFaceSet(int g, Geo& geo, std::vector<Vec3>& nrm, bool& hasNrm, bool& hasUv, const std::string& nm) {
        const int co = Child(g, {"Coordinate", "CoordinateDouble"});
        if (co < 0) return true;   // nothing to draw
        if (!Floats(co, "point", geo.pos, kMaxPoints * 3, 3)) return false;
        totalPoints += geo.pos.size() / 3;
        if (totalPoints > kMaxPoints) return Fail("too many points");
        const std::size_t np = geo.pos.size() / 3;
        std::vector<long long> ci;
        const bool indexed = nm != "TriangleSet";
        if (indexed) {
            const char* attr = nm == "IndexedFaceSet" ? "coordIndex" : "index";
            const std::string* s = d.Attr(g, attr);
            if (s && !rc::ParseIntList(*s, ci, kMaxIndices, true)) return Fail(std::string("an invalid ") + attr + " list");
        } else for (std::size_t i = 0; i < np; ++i) ci.push_back((long long)i);
        d.AttrOr(g, "ccw", "true") == "false" ? geo.ccw = false : geo.ccw = true;
        double crease = 0;
        if (nm == "IndexedFaceSet" && !Num1(g, "creaseAngle", crease)) return false;
        geo.creaseDeg = std::max(0.0, std::min(180.0, crease * 180.0 / kPi));
        const bool polys = nm == "IndexedFaceSet";
        // optional normals / texture coordinates
        std::vector<double> nv, tv;
        const int nn = Child(g, {"Normal"});
        if (nn >= 0 && !Floats(nn, "vector", nv, kMaxPoints * 3, 3)) return false;
        const int tc = Child(g, {"TextureCoordinate"});
        if (tc >= 0 && !Floats(tc, "point", tv, kMaxPoints * 2, 2)) return false;
        std::vector<long long> nidx, tidx;
        if (polys) {
            if (const std::string* s = d.Attr(g, "normalIndex")) if (!rc::ParseIntList(*s, nidx, kMaxIndices, true)) return Fail("an invalid normalIndex list");
            if (const std::string* s = d.Attr(g, "texCoordIndex")) if (!rc::ParseIntList(*s, tidx, kMaxIndices, true)) return Fail("an invalid texCoordIndex list");
        }
        const bool perVertexN = d.AttrOr(g, "normalPerVertex", "true") != "false";
        if (Child(g, {"Color", "ColorRGBA"}) >= 0) ++colors;
        // faces
        std::vector<std::vector<std::uint32_t>> faces;   // corner positions in ci
        if (polys) {
            std::vector<std::uint32_t> cur;
            for (std::size_t i = 0; i < ci.size(); ++i) {
                if (ci[i] < 0) { if (!cur.empty()) faces.push_back(std::move(cur)); cur.clear(); }
                else cur.push_back((std::uint32_t)i);
            }
            if (!cur.empty()) faces.push_back(std::move(cur));
        } else {
            for (std::size_t i = 0; i + 2 < ci.size(); i += 3) faces.push_back({(std::uint32_t)i, (std::uint32_t)i + 1, (std::uint32_t)i + 2});
        }
        const bool wantN = nn >= 0, wantT = tc >= 0;
        bool nOk = wantN, tOk = wantT;
        std::vector<std::array<int, 3>> tris;
        std::vector<Vec3> poly;
        std::uint32_t faceNo = 0;
        for (const auto& f : faces) {
            bool okf = f.size() >= 3;
            for (std::uint32_t c : f) if (ci[c] < 0 || (std::size_t)ci[c] >= np) okf = false;
            if (!okf) { ++badFaces; ++faceNo; continue; }
            poly.clear();
            for (std::uint32_t c : f) { const std::size_t p = (std::size_t)ci[c] * 3; poly.push_back(Vec3{(float)geo.pos[p], (float)geo.pos[p + 1], (float)geo.pos[p + 2]}); }
            tris.clear();
            detail::TriangulatePolygon(poly, tris);
            for (const auto& t : tris) {
                if (++totalTris > kMaxTris) return Fail("too many triangles");
                for (int k = 0; k < 3; ++k) {
                    const std::uint32_t c = f[(std::size_t)t[(std::size_t)k]];
                    geo.tri.push_back((std::uint32_t)ci[c]);
                    geo.triCorner.push_back(c);
                }
                geo.triFace.push_back(faceNo);
            }
            ++faceNo;
        }
        // per corner uv / normal
        if (wantT) {
            geo.cuv.assign(geo.tri.size(), Vec2{0, 0});
            for (std::size_t k = 0; k < geo.tri.size() && tOk; ++k) {
                long long ti = polys ? (tidx.empty() ? ci[geo.triCorner[k]] : (geo.triCorner[k] < tidx.size() ? tidx[geo.triCorner[k]] : -1)) : (long long)geo.tri[k];
                if (ti < 0 || (std::size_t)ti >= tv.size() / 2) { tOk = false; break; }
                geo.cuv[k] = Vec2{(float)tv[(std::size_t)ti * 2], 1.0f - (float)tv[(std::size_t)ti * 2 + 1]};
            }
            if (!tOk) { geo.cuv.clear(); ++badUv; }
        }
        if (wantN) {
            nrm.assign(geo.tri.size(), Vec3{0, 0, 0});
            for (std::size_t k = 0; k < geo.tri.size() && nOk; ++k) {
                long long ni;
                if (perVertexN) ni = polys ? (nidx.empty() ? ci[geo.triCorner[k]] : (geo.triCorner[k] < nidx.size() ? nidx[geo.triCorner[k]] : -1)) : (long long)geo.tri[k];
                else { const std::size_t face = geo.triFace[k / 3]; ni = polys ? (nidx.empty() ? (long long)face : (face < nidx.size() ? nidx[face] : -1)) : (long long)face; }
                if (ni < 0 || (std::size_t)ni >= nv.size() / 3) { nOk = false; break; }
                nrm[k] = Vec3{(float)nv[(std::size_t)ni * 3], (float)nv[(std::size_t)ni * 3 + 1], (float)nv[(std::size_t)ni * 3 + 2]};
            }
            if (!nOk) { nrm.clear(); ++badNormals; }
        }
        hasNrm = wantN && nOk; hasUv = wantT && tOk;
        return true;
    }

    // ---- scene walk ----
    bool Emit(int shape, const M4& xf) {
        ++shapes;
        int app = Child(shape, {"Appearance"});
        int geoNode = -1;
        for (int k : d.Kids(shape)) { const int r = Resolve(k); if (r < 0) continue; const std::string& n = d.Name(r); if (n != "Appearance" && n != "Metadata" && n.compare(0, 8, "Metadata") != 0) { geoNode = r; break; } }
        if (geoNode < 0) { ++emptyShapes; return true; }
        const std::string& gn = d.Name(geoNode);
        Geo geo; std::vector<Vec3> prim;
        bool hasN = false, hasU = false;
        if (gn == "IndexedFaceSet" || gn == "IndexedTriangleSet" || gn == "TriangleSet") {
            if (!IndexedFaceSet(geoNode, geo, prim, hasN, hasU, gn)) return false;
            geo.creaseDeg = gn == "IndexedFaceSet" ? geo.creaseDeg : 0.0;
        } else if (gn == "Box" || gn == "Sphere" || gn == "Cylinder" || gn == "Cone") {
            if (!Primitive(geoNode, gn, geo, prim)) return false;
            hasN = true; hasU = true;
            if (geo.tri.size() / 3 + totalTris > kMaxTris) return Fail("too many triangles");
            totalTris += geo.tri.size() / 3;
        } else if (gn == "Extrusion") {
            if (!Extrusion(geoNode, geo)) return false;
            if (geo.tri.size() / 3 + totalTris > kMaxTris) return Fail("too many triangles");
            totalTris += geo.tri.size() / 3;
        } else { ++skipped[gn]; return true; }
        if (geo.tri.empty()) { ++emptyShapes; return true; }
        // transform to world
        double nm[9]; const bool nmOk = InvTranspose3(xf, nm);
        const bool mirror = rc::Det3(xf) < 0;
        rc::Soup soup;
        const std::size_t np = geo.pos.size() / 3;
        soup.pos.resize(np);
        for (std::size_t i = 0; i < np; ++i) {
            double x, y, z;
            rc::Apply(xf, geo.pos[i * 3], geo.pos[i * 3 + 1], geo.pos[i * 3 + 2], x, y, z);
            if (!std::isfinite(x + y + z) || std::fabs(x) > rc::kMaxCoord || std::fabs(y) > rc::kMaxCoord || std::fabs(z) > rc::kMaxCoord) return Fail("coordinates overflow");
            soup.pos[i] = Vec3{(float)x, (float)y, (float)z};
        }
        const bool flip = (!geo.ccw) != mirror;
        const std::size_t nc = geo.tri.size();
        soup.ci.resize(nc);
        if (hasU) soup.cuv.resize(nc);
        if (hasN) soup.cnrm.resize(nc);
        for (std::size_t t = 0; t < nc / 3; ++t) {
            const std::size_t o[3] = {0, flip ? (std::size_t)2 : (std::size_t)1, flip ? (std::size_t)1 : (std::size_t)2};
            for (int k = 0; k < 3; ++k) {
                const std::size_t src = t * 3 + o[k], dst = t * 3 + (std::size_t)k;
                soup.ci[dst] = geo.tri[src];
                if (hasU) soup.cuv[dst] = geo.cuv[src];
                if (hasN) {
                    const Vec3 n = prim[src];
                    double x = n.x, y = n.y, z = n.z;
                    if (nmOk) { x = nm[0] * n.x + nm[1] * n.y + nm[2] * n.z; y = nm[3] * n.x + nm[4] * n.y + nm[5] * n.z; z = nm[6] * n.x + nm[7] * n.y + nm[8] * n.z; }
                    const double l = std::sqrt(x * x + y * y + z * z);
                    soup.cnrm[dst] = l > 1e-30 ? Vec3{(float)(x / l), (float)(y / l), (float)(z / l)} : Vec3{0, 1, 0};
                }
            }
        }
        ImportedPart p;
        rc::BuildStats bs;
        if (!rc::BuildMesh(soup, geo.creaseDeg, p.mesh, &bs)) return Fail("too many output vertices");
        degenerate += bs.degenerate;
        if (p.mesh.triangles.empty()) { ++emptyShapes; return true; }
        if (out->parts.size() >= kMaxParts) return Fail("too many mesh parts");
        const int mi = MaterialOf(app);
        if (mi == -2) return false;
        p.material = mi;
        ImportedNode nd;
        nd.name = d.AttrOr(shape, "DEF");
        if (nd.name.empty()) nd.name = d.AttrOr(geoNode, "DEF");
        if (nd.name.empty()) nd.name = "Shape" + std::to_string(shapes);
        nd.parent = 0;
        p.node = (int)out->nodes.size();
        out->nodes.push_back(nd);
        if (!rc::PartInRange(p)) return Fail("coordinates overflow");
        out->parts.push_back(std::move(p));
        return true;
    }

    bool LocalMatrix(int i, M4& m) {
        m = M4{};
        double t[3] = {0, 0, 0}, s[3] = {1, 1, 1}, c[3] = {0, 0, 0}, r[4] = {0, 0, 1, 0}, so[4] = {0, 0, 1, 0};
        if (!Vec(i, "translation", t, 3) || !Vec(i, "scale", s, 3) || !Vec(i, "center", c, 3) || !Vec(i, "rotation", r, 4) || !Vec(i, "scaleOrientation", so, 4)) return false;
        // T * C * R * SR * S * SR^-1 * C^-1
        M4 a = rc::Translate(t[0], t[1], t[2]);
        a = rc::Mul(a, rc::Translate(c[0], c[1], c[2]));
        a = rc::Mul(a, rc::AxisAngle(r[0], r[1], r[2], r[3]));
        a = rc::Mul(a, rc::AxisAngle(so[0], so[1], so[2], so[3]));
        a = rc::Mul(a, rc::ScaleM(s[0], s[1], s[2]));
        a = rc::Mul(a, rc::AxisAngle(so[0], so[1], so[2], -so[3]));
        a = rc::Mul(a, rc::Translate(-c[0], -c[1], -c[2]));
        if (!rc::Finite(a)) return Fail("a transform overflows");
        m = a;
        return true;
    }

    bool Walk(int raw, const M4& xf, int depth) {
        const int i = Resolve(raw);
        if (i < 0) return true;
        if (depth > kMaxDepth) return Fail("scene graph nested too deep");
        if (++visits > (int)kMaxVisits) return Fail("scene graph too large (too many nodes, DEF/USE expansions included)");
        if (active.count(i)) { ++cyclic; return true; }
        const std::string& n = d.Name(i);
        struct Guard { std::set<int>& s; int k; ~Guard() { s.erase(k); } } guard{active, i};
        active.insert(i);
        auto kids = [&](const M4& m) { for (int k : d.Kids(i)) if (!Walk(k, m, depth + 1)) return false; return true; };
        if (n == "Shape") return Emit(i, xf);
        if (n == "Transform") { M4 l; if (!LocalMatrix(i, l)) return false; return kids(rc::Mul(xf, l)); }
        if (n == "Group" || n == "Collision" || n == "Anchor" || n == "StaticGroup" || n == "Scene" || n == "X3D" || n == "CADLayer" || n == "CADAssembly" || n == "CADPart") return kids(xf);
        if (n == "Billboard") { ++billboards; return kids(xf); }
        if (n == "Switch") {
            double w = -1;
            if (!Num1(i, "whichChoice", w)) return false;
            const auto& ks = d.Kids(i);
            if (w >= 0 && w < (double)ks.size()) return Walk(ks[(std::size_t)w], xf, depth + 1);
            ++switchNone;
            return true;
        }
        if (n == "LOD") { const auto& ks = d.Kids(i); if (ks.size() > 1) ++skipped["LOD(lower levels)"]; return ks.empty() ? true : Walk(ks[0], xf, depth + 1); }
        if (n == "Inline") { ++skipped["Inline"]; return true; }
        if (n == "ProtoDeclare" || n == "ExternProtoDeclare") { ++skipped["ProtoDeclare"]; return true; }
        if (n == "head" || n == "meta" || n == "component" || n == "unit" || n == "field" || n == "fieldValue" || n == "IS" || n == "connect" || n == "Metadata" || n.compare(0, 8, "Metadata") == 0 || n == "ROUTE" || n == "Appearance" || n == "Material") return true;
        ++skipped[n];
        return true;
    }

    void CollectDefs() {
        for (std::size_t i = 0; i < d.n.size(); ++i) {
            const std::string* def = d.Attr((int)i, "DEF");
            if (def && !defs.count(*def)) defs[*def] = (int)i;
        }
    }

    bool Run(const std::string& text, ImportedModel& model, bool flat) {
        out = &model; flatten = flat;
        if (text.size() > kMaxFileBytes) return Fail("file too large");
        std::string e;
        xml::Limits lim; lim.maxElements = 3000000;
        if (!xml::Parse(text, d, e, lim)) return Fail(e);
        if (d.Name(0) != "X3D") return Fail(d.Name(0) == "Scene" ? "the root element is not <X3D>" : "this is not an X3D XML file (the root element is not <X3D>)");
        CollectDefs();
        ImportedNode root; root.name = "RootNode";
        model.nodes.push_back(root);
        if (!Walk(0, M4{}, 0)) return false;
        if (model.parts.empty()) {
            std::string why = "the file has no triangle geometry";
            if (!skipped.empty()) { why += " (it has only:"; int c = 0; for (auto& kv : skipped) { if (c++ >= 6) break; why += " " + kv.first; } why += ")"; }
            return Fail(why);
        }
        auto note = [&](const std::string& s) { model.notes.push_back("X3D: " + s); };
        if (!skipped.empty()) {
            std::string l; int c = 0;
            for (auto& kv : skipped) { if (c++ >= 12) { l += ", ..."; break; } if (!l.empty()) l += ", "; l += kv.first + " x" + std::to_string(kv.second); }
            note("nodes not read: " + l);
        }
        if (colors) note(std::to_string(colors) + " geometry node(s) with per-vertex or per-face colours: colours not read");
        if (texTransform) note(std::to_string(texTransform) + " TextureTransform node(s) not applied");
        if (otherTex) note(std::to_string(otherTex) + " texture node(s) that are not ImageTexture not read");
        if (switchNone) note(std::to_string(switchNone) + " Switch node(s) chose no child (whichChoice outside the list, default -1)");
        if (billboards) note(std::to_string(billboards) + " Billboard node(s) are static groups here (they do not turn to face the camera)");
        if (cyclic) note(std::to_string(cyclic) + " USE reference(s) back into their own parent were skipped");
        if (badRefs) note(std::to_string(badRefs) + " USE reference(s) to a name that is not defined were skipped");
        if (badFaces) note(std::to_string(badFaces) + " face(s) with fewer than 3 corners or an out-of-range coordinate index skipped");
        if (badNormals) note(std::to_string(badNormals) + " geometry node(s) whose normal indices do not fit: normals generated instead");
        if (badUv) note(std::to_string(badUv) + " geometry node(s) whose texture coordinate indices do not fit: no UVs");
        if (emptyShapes) note(std::to_string(emptyShapes) + " shape(s) without geometry or triangles skipped");
        if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
        return true;
    }
};

} // namespace x3d
} // namespace

bool ReadX3dBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    x3d::Reader r;
    r.baseDir = baseDir;
    if (!r.Run(text, out, flatten)) {
        const std::string m = r.err.empty() ? "X3D: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    out.backend = "own";
    return true;
}

bool ReadX3dFile(const std::string& path, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    std::FILE* fp = detail::OpenRead(path);
    if (!fp) { if (err) *err = "cannot open file"; return false; }
    bool big = false;
    std::string bytes = detail::rc::ReadAllBounded(fp, x3d::kMaxFileBytes, &big);
    std::fclose(fp);
    if (big) { if (err) *err = "file too large"; return false; }
    const std::size_t sl = path.find_last_of("/\\");
    return ReadX3dBuffer(bytes, sl == std::string::npos ? std::string() : path.substr(0, sl + 1), out, err, flatten);
}

} // namespace okay
