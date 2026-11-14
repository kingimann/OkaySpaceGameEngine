// Our own COLLADA (.dae) reader. A small bounds-checked XML parser plus a reader for
// the parts of COLLADA 1.4 / 1.5 that models use: geometry (triangles, polylist,
// polygons), materials and textures, the node hierarchy, skins and matrix animations.
// Anything it does not read is listed in ImportedModel::notes. See
// okay/Render/ModelData.hpp and docs/model_import.md.
#include "FileOpen.hpp"
#include "NumParse.hpp"
#include "okay/Render/ModelData.hpp"
#include "PolygonTriangulate.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace okay {

namespace {
namespace dae {

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxXmlNodes  = 4000000;
constexpr int         kMaxXmlDepth  = 512;
constexpr int         kMaxSceneDepth = 256;   // real exporters nest bone / box chains 60+ deep
constexpr std::size_t kMaxSceneNodes = 200000;
constexpr std::size_t kMaxParts      = 100000;
constexpr std::size_t kMaxOutVerts   = 40000000;

inline bool IsWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

// ---------------------------------------------------------------------------
// XML
// ---------------------------------------------------------------------------

struct XNode {
    std::string name;                                        // without a namespace prefix
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<int> kids;
    std::string text;
};

struct Xml {
    std::vector<XNode> n;

    std::string Attr(int i, const char* key) const {
        if (i < 0) return std::string();
        for (const auto& a : n[(std::size_t)i].attrs) if (a.first == key) return a.second;
        return std::string();
    }
    bool HasAttr(int i, const char* key) const {
        if (i < 0) return false;
        for (const auto& a : n[(std::size_t)i].attrs) if (a.first == key) return true;
        return false;
    }
    int First(int i, const char* name) const {
        if (i < 0) return -1;
        for (int k : n[(std::size_t)i].kids) if (n[(std::size_t)k].name == name) return k;
        return -1;
    }
    std::vector<int> All(int i, const char* name) const {
        std::vector<int> r;
        if (i < 0) return r;
        for (int k : n[(std::size_t)i].kids) if (n[(std::size_t)k].name == name) r.push_back(k);
        return r;
    }
    // First descendant (any depth, document order) with this name.
    int Find(int i, const char* name) const {
        if (i < 0) return -1;
        for (int k : n[(std::size_t)i].kids) {
            if (n[(std::size_t)k].name == name) return k;
            int d = Find(k, name);
            if (d >= 0) return d;
        }
        return -1;
    }
    const std::string& Text(int i) const { return n[(std::size_t)i].text; }
};

void AppendDecoded(std::string& dst, const std::string& s, std::size_t a, std::size_t b) {
    if (s.find('&', a) >= b) { dst.append(s, a, b - a); return; }
    for (std::size_t i = a; i < b; ++i) {
        if (s[i] != '&') { dst += s[i]; continue; }
        std::size_t e = s.find(';', i);
        if (e == std::string::npos || e >= b || e - i > 12) { dst += '&'; continue; }
        const std::string ent = s.substr(i + 1, e - i - 1);
        if (ent == "amp") dst += '&';
        else if (ent == "lt") dst += '<';
        else if (ent == "gt") dst += '>';
        else if (ent == "quot") dst += '"';
        else if (ent == "apos") dst += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            long cp = 0;
            bool okc = ent.size() > 1;
            if (okc && (ent[1] == 'x' || ent[1] == 'X')) {
                okc = ent.size() > 2;
                for (std::size_t k = 2; k < ent.size() && okc; ++k) {
                    char c = ent[k];
                    int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
                    if (d < 0) okc = false; else cp = cp * 16 + d;
                }
            } else {
                for (std::size_t k = 1; k < ent.size() && okc; ++k) {
                    if (ent[k] < '0' || ent[k] > '9') okc = false; else cp = cp * 10 + (ent[k] - '0');
                }
            }
            if (!okc || cp <= 0 || cp > 0x10FFFF) { dst += '?'; }
            else if (cp < 0x80) dst += (char)cp;
            else if (cp < 0x800) { dst += (char)(0xC0 | (cp >> 6)); dst += (char)(0x80 | (cp & 63)); }
            else if (cp < 0x10000) { dst += (char)(0xE0 | (cp >> 12)); dst += (char)(0x80 | ((cp >> 6) & 63)); dst += (char)(0x80 | (cp & 63)); }
            else { dst += (char)(0xF0 | (cp >> 18)); dst += (char)(0x80 | ((cp >> 12) & 63)); dst += (char)(0x80 | ((cp >> 6) & 63)); dst += (char)(0x80 | (cp & 63)); }
        } else { dst += '&'; continue; }
        i = e;
    }
}

std::string StripPrefix(const std::string& s) {
    std::size_t c = s.find(':');
    return c == std::string::npos ? s : s.substr(c + 1);
}

bool ParseXml(const std::string& s, Xml& x, std::string& err) {
    std::size_t p = 0;
    const std::size_t n = s.size();
    std::vector<int> stack;
    bool haveRoot = false;
    if (n >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) p = 3;
    auto fail = [&](const char* m) { err = m; return false; };
    while (p < n) {
        if (s[p] != '<') {
            std::size_t e = s.find('<', p);
            if (e == std::string::npos) e = n;
            if (!stack.empty()) AppendDecoded(x.n[(std::size_t)stack.back()].text, s, p, e);
            p = e;
            continue;
        }
        if (s.compare(p, 4, "<!--") == 0) {
            std::size_t e = s.find("-->", p + 4);
            if (e == std::string::npos) return fail("XML: unterminated comment");
            p = e + 3; continue;
        }
        if (s.compare(p, 9, "<![CDATA[") == 0) {
            std::size_t e = s.find("]]>", p + 9);
            if (e == std::string::npos) return fail("XML: unterminated CDATA");
            if (!stack.empty()) x.n[(std::size_t)stack.back()].text.append(s, p + 9, e - p - 9);
            p = e + 3; continue;
        }
        if (s.compare(p, 2, "<?") == 0) {
            std::size_t e = s.find("?>", p + 2);
            if (e == std::string::npos) return fail("XML: unterminated declaration");
            p = e + 2; continue;
        }
        if (s.compare(p, 2, "<!") == 0) {
            int depth = 0;
            std::size_t q = p + 2;
            for (; q < n; ++q) {
                if (s[q] == '[') ++depth;
                else if (s[q] == ']') --depth;
                else if (s[q] == '>' && depth <= 0) break;
            }
            if (q >= n) return fail("XML: unterminated DOCTYPE");
            p = q + 1; continue;
        }
        if (s.compare(p, 2, "</") == 0) {
            std::size_t q = p + 2, b = q;
            while (q < n && !IsWs(s[q]) && s[q] != '>') ++q;
            std::string nm = StripPrefix(s.substr(b, q - b));
            while (q < n && IsWs(s[q])) ++q;
            if (q >= n || s[q] != '>') return fail("XML: malformed end tag");
            if (stack.empty() || x.n[(std::size_t)stack.back()].name != nm) return fail("XML: mismatched end tag");
            stack.pop_back();
            p = q + 1; continue;
        }
        // Start tag.
        std::size_t q = p + 1, b = q;
        while (q < n && !IsWs(s[q]) && s[q] != '/' && s[q] != '>') ++q;
        if (q == b) return fail("XML: empty tag name");
        if (x.n.size() >= kMaxXmlNodes) return fail("XML: too many elements");
        if ((int)stack.size() >= kMaxXmlDepth) return fail("XML: nesting too deep");
        if (stack.empty() && haveRoot) return fail("XML: more than one root element");
        XNode node;
        node.name = StripPrefix(s.substr(b, q - b));
        bool selfClose = false;
        for (;;) {
            while (q < n && IsWs(s[q])) ++q;
            if (q >= n) return fail("XML: unterminated tag");
            if (s[q] == '>') { ++q; break; }
            if (s[q] == '/') {
                if (q + 1 >= n || s[q + 1] != '>') return fail("XML: malformed tag");
                selfClose = true; q += 2; break;
            }
            std::size_t ab = q;
            while (q < n && !IsWs(s[q]) && s[q] != '=' && s[q] != '>' && s[q] != '/') ++q;
            if (q == ab) return fail("XML: malformed attribute");
            std::string an = s.substr(ab, q - ab);
            while (q < n && IsWs(s[q])) ++q;
            if (q >= n || s[q] != '=') return fail("XML: attribute without a value");
            ++q;
            while (q < n && IsWs(s[q])) ++q;
            if (q >= n || (s[q] != '"' && s[q] != '\'')) return fail("XML: attribute value not quoted");
            const char quote = s[q++];
            std::size_t ve = s.find(quote, q);
            if (ve == std::string::npos) return fail("XML: unterminated attribute value");
            std::string av;
            AppendDecoded(av, s, q, ve);
            node.attrs.emplace_back(StripPrefix(an), std::move(av));
            q = ve + 1;
        }
        const int idx = (int)x.n.size();
        x.n.push_back(std::move(node));
        if (!stack.empty()) x.n[(std::size_t)stack.back()].kids.push_back(idx);
        else haveRoot = true;
        if (!selfClose) stack.push_back(idx);
        p = q;
    }
    if (!stack.empty()) return fail("XML: unclosed element");
    if (!haveRoot) return fail("XML: no root element");
    return true;
}

// ---------------------------------------------------------------------------
// Number lists
// ---------------------------------------------------------------------------

// Some exporters run in a locale with a decimal comma ("21,920870") and write it into the arrays
// unchanged. A token with a comma and no point is read with the comma as the decimal point; `commas`
// counts such tokens so the reader can say so.
bool ParseFloats(const std::string& t, std::vector<float>& out, int* commas = nullptr) {
    out.clear();
    out.reserve(t.size() / 4 + 1);
    const char* p = t.c_str();
    const char* end = p + t.size();
    for (;;) {
        while (p < end && IsWs(*p)) ++p;
        if (p >= end) break;
        const char* q = p;
        bool comma = false, point = false;
        while (q < end && !IsWs(*q)) { if (*q == ',') comma = true; else if (*q == '.') point = true; ++q; }
        char* e = nullptr;
        float v;
        if (comma && !point && q - p < 64) {
            char buf[72];
            for (const char* c = p; c < q; ++c) buf[c - p] = *c == ',' ? '.' : *c;
            buf[q - p] = 0;
            v = detail::StrToF(buf, &e);
            if (e != buf + (q - p)) return false;
            if (commas) ++*commas;
        } else {
            v = detail::StrToF(p, &e);
            if (e != q) return false;   // not a whole token (or no number at all)
        }
        if (!std::isfinite(v)) return false;
        out.push_back(v);
        p = q;
    }
    return true;
}

bool ParseInts(const std::string& t, std::vector<int>& out) {
    out.clear();
    out.reserve(t.size() / 2 + 1);
    const char* p = t.c_str();
    const char* end = p + t.size();
    for (;;) {
        while (p < end && IsWs(*p)) ++p;
        if (p >= end) break;
        char* e = nullptr;
        long v = std::strtol(p, &e, 10);
        if (e == p || v < -2147483647L || v > 2147483647L) return false;
        out.push_back((int)v);
        p = e;
    }
    return true;
}

std::vector<std::string> Tokens(const std::string& t) {
    std::vector<std::string> r;
    std::size_t i = 0;
    while (i < t.size()) {
        while (i < t.size() && IsWs(t[i])) ++i;
        std::size_t b = i;
        while (i < t.size() && !IsWs(t[i])) ++i;
        if (i > b) r.push_back(t.substr(b, i - b));
    }
    return r;
}

std::string UrlId(const std::string& u) {   // "#id" -> "id"
    return (!u.empty() && u[0] == '#') ? u.substr(1) : u;
}

std::string PercentDecode(const std::string& s) {
    std::string r;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            r += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else r += s[i];
    }
    return r;
}

// An image reference: "file:///C:/a.png", "a%20b.png", "..\\tex\\a.png" -> a path the renderer can open.
std::string ResolveImagePath(std::string tp, const std::string& baseDir) {
    tp = PercentDecode(tp);
    for (char& c : tp) if (c == '\\') c = '/';
    if (tp.compare(0, 8, "file:///") == 0) {
        tp = tp.substr(8);
        if (!(tp.size() > 1 && tp[1] == ':')) tp = "/" + tp;   // "/home/..." keeps its slash, "C:/..." has none
    } else if (tp.compare(0, 7, "file://") == 0) {
        tp = tp.substr(7);
    }
    const bool abs = !tp.empty() && (tp[0] == '/' || (tp.size() > 1 && tp[1] == ':'));
    return abs ? tp : baseDir + tp;
}

// Column-major Mat4 from COLLADA's row-major, column-vector matrix.
Mat4 MatFromRowMajor(const float* a) {
    Mat4 r;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) r.at(col, row) = a[row * 4 + col];
    return r;
}

// ---------------------------------------------------------------------------
// The reader
// ---------------------------------------------------------------------------

struct Src {                       // a float source read through its accessor
    std::vector<float> f;
    int stride = 1, offset = 0;
    bool Has(long i, int comps) const {
        if (i < 0 || stride < 1 || comps > stride) return false;
        const unsigned long long last = (unsigned long long)offset + (unsigned long long)i * (unsigned long long)stride + (unsigned long long)(comps - 1);
        return last < f.size();
    }
    float At(long i, int c) const { return f[(std::size_t)offset + (std::size_t)i * (std::size_t)stride + (std::size_t)c]; }
    long Count() const { return stride < 1 || (std::size_t)offset > f.size() ? 0 : (long)((f.size() - (std::size_t)offset) / (std::size_t)stride); }
};

struct PrimSet {
    Mesh mesh;
    std::vector<int> posIdx;       // per output vertex: the POSITION index it came from (for skin weights)
    std::string materialSymbol;
};

struct Skin {
    std::string geometry;
    Mat4 bindShape;
    std::vector<std::string> joints;
    std::vector<Mat4> invBind;
    std::vector<std::vector<std::pair<int, float>>> weights;   // per POSITION index, strongest 4, normalised
};

struct TElem { char kind = 't'; std::string sid; float v[16] = {0}; int n = 3; };   // 'm' matrix, 't' translate, 'r' rotate, 's' scale

struct PendingSkin { std::size_t part; std::vector<std::string> joints; std::vector<std::string> skeletonRoots; };


struct Reader {
    Xml x;
    std::string baseDir;
    bool flatten = false;
    ImportedModel* out = nullptr;
    std::string err;

    std::unordered_map<std::string, int> ids;                       // element id -> xml node (first wins)
    std::unordered_map<std::string, std::vector<PrimSet>> geoCache;
    std::unordered_map<std::string, bool> geoFailed;
    std::unordered_map<std::string, std::shared_ptr<Skin>> skinCache;
    std::unordered_map<std::string, int> matIndex;                  // material id -> out->materials index
    std::vector<std::string> nodeId, nodeSid;                       // parallel to nodes (kept even when flattening)
    std::vector<ImportedNode> nodes;
    std::vector<std::vector<TElem>> nodeElems;                       // transform elements per node, for animation
    std::map<std::string, int> interpApprox;                         // non-LINEAR/STEP interpolation names, read as linear
    std::vector<PendingSkin> pending;
    std::size_t totalVerts = 0;
    int decimalCommas = 0;

    // Things skipped, reported once each through ImportedModel::notes.
    std::map<std::string, int> skippedPrims;
    int holes = 0, vcolors = 0, extraUv = 0, morphs = 0, skews = 0, camLights = 0, badDecompose = 0;
    int animSkipped = 0, weightsTrunc = 0, skinsDropped = 0, emptyClips = 0;
    std::string animExample;
    std::vector<std::string> missingJoints;

    bool Fail(const std::string& m) { if (err.empty()) err = m; return false; }

    int Lookup(const std::string& id) const {
        auto it = ids.find(id);
        return it == ids.end() ? -1 : it->second;
    }

    // ---- sources ----------------------------------------------------------
    bool ReadSrc(const std::string& id, Src& s) {
        const int sn = Lookup(id);
        if (sn < 0 || x.n[(std::size_t)sn].name != "source") return false;
        const int fa = x.First(sn, "float_array");
        if (fa < 0 || !ParseFloats(x.Text(fa), s.f, &decimalCommas)) return false;
        const int acc = x.Find(sn, "accessor");
        s.stride = 1; s.offset = 0;
        if (acc >= 0) {
            s.stride = std::atoi(x.Attr(acc, "stride").c_str());
            if (!x.HasAttr(acc, "stride")) s.stride = 1;
            s.offset = std::atoi(x.Attr(acc, "offset").c_str());
        }
        return s.stride >= 1 && s.stride <= 64 && s.offset >= 0;
    }

    // ---- geometry ---------------------------------------------------------
    struct Chan { Src src; int offset = 0; bool has = false; };
    struct CornerKey {
        int p, n, t;
        bool operator==(const CornerKey& o) const { return p == o.p && n == o.n && t == o.t; }
    };
    struct CornerHash {
        std::size_t operator()(const CornerKey& k) const {
            std::uint64_t h = (std::uint64_t)(std::uint32_t)k.p * 0x9E3779B97F4A7C15ull;
            h ^= (std::uint64_t)(std::uint32_t)k.n + 0x7F4A7C15ull + (h << 6) + (h >> 2);
            h ^= (std::uint64_t)(std::uint32_t)k.t + 0x9E3779B9ull + (h << 6) + (h >> 2);
            return (std::size_t)h;
        }
    };

    bool BuildPrim(int meshNode, int prim, PrimSet& ps) {
        const std::string kind = x.n[(std::size_t)prim].name;
        ps.materialSymbol = x.Attr(prim, "material");
        Chan pos, nrm, uv;
        int uvSet = 1 << 30, stride = 1;
        bool sawColor = false;
        for (int in : x.All(prim, "input")) {
            const std::string sem = x.Attr(in, "semantic");
            const int off = std::atoi(x.Attr(in, "offset").c_str());
            if (off < 0 || off > 64) return Fail("COLLADA: bad input offset");
            stride = std::max(stride, off + 1);
            const std::string src = UrlId(x.Attr(in, "source"));
            const int set = x.HasAttr(in, "set") ? std::atoi(x.Attr(in, "set").c_str()) : 0;
            auto take = [&](const std::string& semantic, const std::string& sid, int offset, int st) -> bool {
                if (semantic == "POSITION" && !pos.has) { if (!ReadSrc(sid, pos.src)) return false; pos.offset = offset; pos.has = true; }
                else if (semantic == "NORMAL" && !nrm.has) { if (!ReadSrc(sid, nrm.src)) return false; nrm.offset = offset; nrm.has = true; }
                else if (semantic == "TEXCOORD") {
                    if (st < uvSet) {
                        if (uv.has) ++extraUv;
                        if (!ReadSrc(sid, uv.src)) return false;
                        uv.offset = offset; uv.has = true; uvSet = st;
                    } else ++extraUv;
                } else if (semantic == "COLOR") sawColor = true;
                return true;
            };
            if (sem == "VERTEX") {
                const int vn = Lookup(src);
                if (vn < 0 || x.n[(std::size_t)vn].name != "vertices") return Fail("COLLADA: VERTEX input does not name a <vertices> element");
                for (int vi : x.All(vn, "input"))
                    if (!take(x.Attr(vi, "semantic"), UrlId(x.Attr(vi, "source")), off, 0))
                        return Fail("COLLADA: a <vertices> source is missing or malformed");
            } else if (sem == "NORMAL" || sem == "TEXCOORD" || sem == "POSITION" || sem == "COLOR") {
                if (!take(sem, src, off, set)) return Fail("COLLADA: a primitive source is missing or malformed");
            }
        }
        (void)meshNode;
        if (sawColor) ++vcolors;
        if (!pos.has) return Fail("COLLADA: primitive has no POSITION input");
        if (pos.src.stride < 3) return Fail("COLLADA: POSITION source has fewer than 3 components");
        if (nrm.has && nrm.src.stride < 3) nrm.has = false;
        if (uv.has && uv.src.stride < 2) uv.has = false;

        // Corner stream -> polygons.
        std::vector<int> all;
        std::vector<std::pair<std::size_t, std::size_t>> polys;   // (first corner, corner count)
        auto addP = [&](int pn) -> bool {
            std::vector<int> v;
            if (!ParseInts(x.Text(pn), v)) return false;
            all.insert(all.end(), v.begin(), v.end());
            return true;
        };
        if (kind == "triangles") {
            const int pn = x.First(prim, "p");
            if (pn < 0) return true;   // empty set
            if (!addP(pn)) return Fail("COLLADA: bad <p> index list");
            const std::size_t nc = all.size() / (std::size_t)stride;
            for (std::size_t c = 0; c + 3 <= nc; c += 3) polys.emplace_back(c, 3);
        } else if (kind == "polylist") {
            const int pn = x.First(prim, "p"), vn = x.First(prim, "vcount");
            if (pn < 0 || vn < 0) return true;
            std::vector<int> vc;
            if (!addP(pn) || !ParseInts(x.Text(vn), vc)) return Fail("COLLADA: bad <p> or <vcount> list");
            const std::size_t nc = all.size() / (std::size_t)stride;
            std::size_t at = 0;
            for (int c : vc) {
                if (c < 0 || (std::size_t)c > nc - std::min(nc, at)) return Fail("COLLADA: polylist <vcount> runs past <p>");
                polys.emplace_back(at, (std::size_t)c);
                at += (std::size_t)c;
            }
        } else if (kind == "tristrips" || kind == "trifans") {
            // every <p> is one strip (or fan); each triangle becomes its own 3 corners
            const bool strip = kind == "tristrips";
            for (int pn : x.All(prim, "p")) {
                std::vector<int> v;
                if (!ParseInts(x.Text(pn), v)) return Fail("COLLADA: bad <p> index list");
                const std::size_t n = v.size() / (std::size_t)stride;
                auto push = [&](std::size_t k) { all.insert(all.end(), v.begin() + (std::ptrdiff_t)(k * (std::size_t)stride), v.begin() + (std::ptrdiff_t)((k + 1) * (std::size_t)stride)); };
                for (std::size_t i = 0; i + 2 < n; ++i) {
                    const std::size_t at = all.size() / (std::size_t)stride;
                    if (strip) { if (i & 1) { push(i + 1); push(i); push(i + 2); } else { push(i); push(i + 1); push(i + 2); } }
                    else { push(0); push(i + 1); push(i + 2); }
                    polys.emplace_back(at, 3);
                }
            }
        } else {   // polygons
            for (int k : x.n[(std::size_t)prim].kids) {
                int pn = -1;
                if (x.n[(std::size_t)k].name == "p") pn = k;
                else if (x.n[(std::size_t)k].name == "ph") { pn = x.First(k, "p"); ++holes; }   // outer ring only
                if (pn < 0) continue;
                const std::size_t before = all.size();
                if (!addP(pn)) return Fail("COLLADA: bad <p> index list");
                polys.emplace_back(before / (std::size_t)stride, (all.size() - before) / (std::size_t)stride);
            }
        }

        std::unordered_map<CornerKey, int, CornerHash> weld;
        Mesh& m = ps.mesh;
        auto corner = [&](std::size_t k, int& vi) -> bool {
            const std::size_t b = k * (std::size_t)stride;
            const int pi = all[b + (std::size_t)pos.offset];
            const int ni = nrm.has ? all[b + (std::size_t)nrm.offset] : -1;
            const int ti = uv.has ? all[b + (std::size_t)uv.offset] : -1;
            if (!pos.src.Has(pi, 3)) return false;
            if (nrm.has && !nrm.src.Has(ni, 3)) return false;
            if (uv.has && !uv.src.Has(ti, 2)) return false;
            const CornerKey key{pi, ni, ti};
            auto it = weld.find(key);
            if (it != weld.end()) { vi = it->second; return true; }
            vi = (int)m.vertices.size();
            weld.emplace(key, vi);
            m.vertices.push_back(Vec3{pos.src.At(pi, 0), pos.src.At(pi, 1), pos.src.At(pi, 2)});
            ps.posIdx.push_back(pi);
            if (nrm.has) {
                Vec3 nv{nrm.src.At(ni, 0), nrm.src.At(ni, 1), nrm.src.At(ni, 2)};
                const float l = std::sqrt(nv.x * nv.x + nv.y * nv.y + nv.z * nv.z);
                if (l > 0.0f) nv = Vec3{nv.x / l, nv.y / l, nv.z / l};
                m.normals.push_back(nv);
            }
            if (uv.has) m.uvs.push_back(Vec2{uv.src.At(ti, 0), 1.0f - uv.src.At(ti, 1)});
            return true;
        };
        std::vector<Vec3> ring;
        std::vector<std::array<int, 3>> tri;
        std::vector<int> cv;   // output vertex of each corner of the polygon
        for (const auto& pl : polys) {
            if (pl.second < 3) continue;
            if (pl.first + pl.second > all.size() / (std::size_t)stride) return Fail("COLLADA: polygon runs past <p>");
            cv.assign(pl.second, 0);
            for (std::size_t i = 0; i < pl.second; ++i) if (!corner(pl.first + i, cv[i])) return Fail("COLLADA: index out of range");
            if (pl.second == 3) { m.triangles.push_back(cv[0]); m.triangles.push_back(cv[1]); m.triangles.push_back(cv[2]); continue; }
            ring.resize(pl.second);
            for (std::size_t i = 0; i < pl.second; ++i) ring[i] = m.vertices[(std::size_t)cv[i]];
            detail::TriangulatePolygon(ring, tri);
            for (const auto& t : tri) { m.triangles.push_back(cv[(std::size_t)t[0]]); m.triangles.push_back(cv[(std::size_t)t[1]]); m.triangles.push_back(cv[(std::size_t)t[2]]); }
        }
        if (totalVerts + m.vertices.size() > kMaxOutVerts) return Fail("COLLADA: model too large");
        totalVerts += m.vertices.size();
        if (!nrm.has && !m.vertices.empty()) {   // smooth normals, welded per source position
            std::vector<Vec3> acc((std::size_t)pos.src.Count(), Vec3{0, 0, 0});
            for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) {
                const Vec3& p0 = m.vertices[(std::size_t)m.triangles[t]];
                const Vec3& p1 = m.vertices[(std::size_t)m.triangles[t + 1]];
                const Vec3& p2 = m.vertices[(std::size_t)m.triangles[t + 2]];
                const Vec3 e1{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z}, e2{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
                const Vec3 cr{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
                for (int k = 0; k < 3; ++k) {
                    Vec3& a = acc[(std::size_t)ps.posIdx[(std::size_t)m.triangles[t + (std::size_t)k]]];
                    a = Vec3{a.x + cr.x, a.y + cr.y, a.z + cr.z};
                }
            }
            for (std::size_t v = 0; v < m.vertices.size(); ++v) {
                const Vec3 a = acc[(std::size_t)ps.posIdx[v]];
                const float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
                m.normals.push_back(l > 1e-20f ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0, 1, 0});
            }
        }
        return true;
    }

    const std::vector<PrimSet>* Geometry(const std::string& gid) {
        auto it = geoCache.find(gid);
        if (it != geoCache.end()) return &it->second;
        if (geoFailed.count(gid)) return nullptr;
        const int gn = Lookup(gid);
        const int mn = gn >= 0 && x.n[(std::size_t)gn].name == "geometry" ? x.First(gn, "mesh") : -1;
        if (mn < 0) { geoFailed[gid] = true; skippedPrims["geometry that is not a <mesh> (spline, brep)"]++; return nullptr; }
        std::vector<PrimSet> sets;
        for (int k : x.n[(std::size_t)mn].kids) {
            const std::string& nm = x.n[(std::size_t)k].name;
            if (nm == "triangles" || nm == "polylist" || nm == "polygons" || nm == "tristrips" || nm == "trifans") {
                PrimSet ps;
                ps.mesh.name = x.Attr(gn, "name").empty() ? gid : x.Attr(gn, "name");
                if (!BuildPrim(mn, k, ps)) { geoFailed[gid] = true; return nullptr; }
                if (!ps.mesh.triangles.empty()) sets.push_back(std::move(ps));
            } else if (nm == "lines" || nm == "linestrips") {
                skippedPrims[nm]++;
            }
        }
        auto ins = geoCache.emplace(gid, std::move(sets));
        return &ins.first->second;
    }

    // ---- materials --------------------------------------------------------
    std::string ImageFile(const std::string& imageId) {
        const int im = Lookup(imageId);
        if (im < 0 || x.n[(std::size_t)im].name != "image") return std::string();
        int f = x.First(im, "init_from");
        if (f < 0) return std::string();
        std::string t = x.Text(f);
        const int r = x.First(f, "ref");
        if (r >= 0) t = x.Text(r);
        std::string trimmed;
        { std::size_t a = 0, b = t.size(); while (a < b && IsWs(t[a])) ++a; while (b > a && IsWs(t[b - 1])) --b; trimmed = t.substr(a, b - a); }
        return trimmed.empty() ? std::string() : ResolveImagePath(trimmed, baseDir);
    }

    // texture="..." names a sampler sid in the effect, or (older exporters) an image id.
    std::string TextureFile(int effect, const std::string& ref) {
        std::string cur = ref;
        for (int hop = 0; hop < 4; ++hop) {
            int np = -1;
            // newparams live under the effect or its profile_COMMON
            std::vector<int> holders{effect};
            const int prof = x.First(effect, "profile_COMMON");
            if (prof >= 0) holders.push_back(prof);
            for (int h : holders)
                for (int c : x.All(h, "newparam")) if (x.Attr(c, "sid") == cur) { np = c; break; }
            if (np < 0) break;
            const int sm = x.First(np, "sampler2D");
            if (sm >= 0) { const int sr = x.First(sm, "source"); if (sr < 0) return std::string(); cur.clear(); for (char c : x.Text(sr)) if (!IsWs(c)) cur += c; continue; }
            const int sf = x.First(np, "surface");
            if (sf >= 0) {
                int f = x.First(sf, "init_from");
                if (f < 0) return std::string();
                std::string id; for (char c : x.Text(f)) if (!IsWs(c)) id += c;
                return ImageFile(id);
            }
            break;
        }
        return ImageFile(cur);
    }

    ImportedMaterial ReadMaterial(int matNode) {
        ImportedMaterial m;
        m.name = x.Attr(matNode, "name").empty() ? x.Attr(matNode, "id") : x.Attr(matNode, "name");
        const int ie = x.First(matNode, "instance_effect");
        const int fx = ie >= 0 ? Lookup(UrlId(x.Attr(ie, "url"))) : -1;
        if (fx < 0 || x.n[(std::size_t)fx].name != "effect") return m;
        const int prof = x.First(fx, "profile_COMMON");
        const int tech = prof >= 0 ? x.First(prof, "technique") : -1;
        int shader = -1;
        for (const char* s : {"phong", "blinn", "lambert", "constant"}) { shader = x.First(tech, s); if (shader >= 0) break; }
        auto colorOf = [&](int prop, Color& c) -> bool {
            const int cn = x.First(prop, "color");
            if (cn < 0) return false;
            std::vector<float> v;
            if (!ParseFloats(x.Text(cn), v) || v.size() < 3) return false;
            c = Color(v[0], v[1], v[2], 1.0f);
            return true;
        };
        auto texOf = [&](int prop) -> std::string {
            const int tn = x.First(prop, "texture");
            return tn < 0 ? std::string() : TextureFile(fx, x.Attr(tn, "texture"));
        };
        if (shader >= 0) {
            const int d = x.First(shader, "diffuse");
            if (d >= 0) { m.texture = texOf(d); if (colorOf(d, m.diffuse)) m.hasDiffuse = true; }
            const int e = x.First(shader, "emission");
            if (e >= 0 && colorOf(e, m.emissive)) m.hasEmissive = true;
            const int sp = x.First(shader, "specular");
            if (sp >= 0) m.specularMap = texOf(sp);
            const int sh = x.First(shader, "shininess");
            if (sh >= 0) {
                const int fl = x.First(sh, "float");
                std::vector<float> v;
                if (fl >= 0 && ParseFloats(x.Text(fl), v) && v.size() == 1) m.shininess = v[0];
            }
        }
        const int bump = x.Find(fx, "bump");   // <extra><technique><bump><texture/> (a normal map in practice)
        if (bump >= 0) m.normalMap = texOf(bump);
        return m;
    }

    int MaterialIndex(const std::string& matId) {
        auto it = matIndex.find(matId);
        if (it != matIndex.end()) return it->second;
        const int mn = Lookup(matId);
        if (mn < 0 || x.n[(std::size_t)mn].name != "material") return -1;
        out->materials.push_back(ReadMaterial(mn));
        const int idx = (int)out->materials.size() - 1;
        matIndex[matId] = idx;
        return idx;
    }

    // ---- skins ------------------------------------------------------------
    std::shared_ptr<Skin> GetSkin(const std::string& cid) {
        auto it = skinCache.find(cid);
        if (it != skinCache.end()) return it->second;
        std::shared_ptr<Skin> sk;
        const int cn = Lookup(cid);
        const int sn = cn >= 0 ? x.First(cn, "skin") : -1;
        if (sn >= 0) sk = ReadSkin(sn);
        skinCache[cid] = sk;
        return sk;
    }

    std::shared_ptr<Skin> ReadSkin(int sn) {
        auto sk = std::make_shared<Skin>();
        sk->geometry = UrlId(x.Attr(sn, "source"));
        const int bs = x.First(sn, "bind_shape_matrix");
        if (bs >= 0) {
            std::vector<float> v;
            if (!ParseFloats(x.Text(bs), v) || v.size() != 16) return nullptr;
            sk->bindShape = MatFromRowMajor(v.data());
        }
        const int jn = x.First(sn, "joints"), vw = x.First(sn, "vertex_weights");
        if (jn < 0 || vw < 0) return nullptr;
        std::string jointSrc, invSrc, wSrc;
        for (int in : x.All(jn, "input")) {
            if (x.Attr(in, "semantic") == "JOINT") jointSrc = UrlId(x.Attr(in, "source"));
            else if (x.Attr(in, "semantic") == "INV_BIND_MATRIX") invSrc = UrlId(x.Attr(in, "source"));
        }
        int jOff = -1, wOff = -1, stride = 1;
        for (int in : x.All(vw, "input")) {
            const std::string sem = x.Attr(in, "semantic");
            const int off = std::atoi(x.Attr(in, "offset").c_str());
            if (off < 0 || off > 64) return nullptr;
            stride = std::max(stride, off + 1);
            if (sem == "JOINT") { jOff = off; if (jointSrc.empty()) jointSrc = UrlId(x.Attr(in, "source")); }
            else if (sem == "WEIGHT") { wOff = off; wSrc = UrlId(x.Attr(in, "source")); }
        }
        if (jOff < 0 || wOff < 0 || jointSrc.empty() || invSrc.empty()) return nullptr;
        // Joint names: Name_array or IDREF_array.
        const int js = Lookup(jointSrc);
        if (js < 0) return nullptr;
        int arr = x.First(js, "Name_array");
        if (arr < 0) arr = x.First(js, "IDREF_array");
        if (arr < 0) return nullptr;
        sk->joints = Tokens(x.Text(arr));
        std::vector<float> ib;
        {
            const int is = Lookup(invSrc);
            const int fa = is >= 0 ? x.First(is, "float_array") : -1;
            if (fa < 0 || !ParseFloats(x.Text(fa), ib)) return nullptr;
        }
        if (ib.size() != sk->joints.size() * 16) return nullptr;
        for (std::size_t j = 0; j < sk->joints.size(); ++j) sk->invBind.push_back(MatFromRowMajor(&ib[j * 16]) * sk->bindShape);
        Src ws;
        if (!ReadSrc(wSrc, ws)) return nullptr;
        const int vcn = x.First(vw, "vcount"), vn = x.First(vw, "v");
        std::vector<int> vc, v;
        if (vcn >= 0 && !ParseInts(x.Text(vcn), vc)) return nullptr;
        if (vn >= 0 && !ParseInts(x.Text(vn), v)) return nullptr;
        std::size_t at = 0;
        const std::size_t pairs = v.size() / (std::size_t)stride;
        sk->weights.reserve(vc.size());
        for (int c : vc) {
            if (c < 0 || (std::size_t)c > pairs - std::min(pairs, at)) return nullptr;
            std::vector<std::pair<int, float>> w;
            for (int k = 0; k < c; ++k, ++at) {
                const int ji = v[at * (std::size_t)stride + (std::size_t)jOff];
                const int wi = v[at * (std::size_t)stride + (std::size_t)wOff];
                if (ji < 0) continue;   // -1 = the bind shape itself
                if ((std::size_t)ji >= sk->joints.size() || !ws.Has(wi, 1)) return nullptr;
                const float wt = ws.At(wi, 0);
                if (wt > 0.0f) w.emplace_back(ji, wt);
            }
            if (w.size() > 4) {
                ++weightsTrunc;
                std::stable_sort(w.begin(), w.end(), [](const std::pair<int, float>& a, const std::pair<int, float>& b) { return a.second > b.second; });
                w.resize(4);
            }
            float sum = 0.0f;
            for (const auto& e : w) sum += e.second;
            if (sum > 0.0f) for (auto& e : w) e.second /= sum;
            sk->weights.push_back(std::move(w));
        }
        return sk;
    }

    // ---- scene ------------------------------------------------------------
    Mat4 LocalTransform(int nn, std::vector<TElem>& elems) {
        for (int k : x.n[(std::size_t)nn].kids) {
            const std::string& nm = x.n[(std::size_t)k].name;
            std::vector<float> v;
            TElem e;
            if (nm == "matrix") { e.kind = 'm'; e.n = 16; }
            else if (nm == "translate") { e.kind = 't'; e.n = 3; }
            else if (nm == "scale") { e.kind = 's'; e.n = 3; }
            else if (nm == "rotate") { e.kind = 'r'; e.n = 4; }
            else { if (nm == "skew" || nm == "lookat") ++skews; continue; }
            if (!ParseFloats(x.Text(k), v) || (int)v.size() != e.n) continue;
            for (int i = 0; i < e.n; ++i) e.v[i] = v[(std::size_t)i];
            e.sid = x.Attr(k, "sid");
            elems.push_back(e);
        }
        return ComposeElems(elems);
    }

    static void Bake(Mesh& m, const Mat4& w, bool skinned) {
        (void)skinned;
        for (Vec3& p : m.vertices) {
            const float a = w.at(0, 0) * p.x + w.at(1, 0) * p.y + w.at(2, 0) * p.z + w.at(3, 0);
            const float b = w.at(0, 1) * p.x + w.at(1, 1) * p.y + w.at(2, 1) * p.z + w.at(3, 1);
            const float c = w.at(0, 2) * p.x + w.at(1, 2) * p.y + w.at(2, 2) * p.z + w.at(3, 2);
            p = Vec3{a, b, c};
        }
        for (Vec3& p : m.normals) {
            const float a = w.at(0, 0) * p.x + w.at(1, 0) * p.y + w.at(2, 0) * p.z;
            const float b = w.at(0, 1) * p.x + w.at(1, 1) * p.y + w.at(2, 1) * p.z;
            const float c = w.at(0, 2) * p.x + w.at(1, 2) * p.y + w.at(2, 2) * p.z;
            const float l = std::sqrt(a * a + b * b + c * c);
            p = l > 0.0f ? Vec3{a / l, b / l, c / l} : Vec3{0, 1, 0};
        }
        const float det = w.at(0, 0) * (w.at(1, 1) * w.at(2, 2) - w.at(2, 1) * w.at(1, 2)) -
                          w.at(1, 0) * (w.at(0, 1) * w.at(2, 2) - w.at(2, 1) * w.at(0, 2)) +
                          w.at(2, 0) * (w.at(0, 1) * w.at(1, 2) - w.at(1, 1) * w.at(0, 2));
        if (det < 0.0f)   // mirrored: keep the faces facing out
            for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) std::swap(m.triangles[t + 1], m.triangles[t + 2]);
    }

    bool AddInstance(int nodeIdx, const Mat4& world, int inst, bool controller) {
        std::string gid;
        std::shared_ptr<Skin> skin;
        const std::string url = UrlId(x.Attr(inst, "url"));
        if (controller) {
            const int cn = Lookup(url);
            if (cn < 0) return true;
            if (x.First(cn, "morph") >= 0) {
                ++morphs;
                gid = UrlId(x.Attr(x.First(cn, "morph"), "source"));
            } else if (x.First(cn, "skin") >= 0) {
                skin = GetSkin(url);
                if (!skin) { ++skinsDropped; gid = UrlId(x.Attr(x.First(cn, "skin"), "source")); }
                else gid = skin->geometry;
            } else return true;
        } else gid = url;
        const std::vector<PrimSet>* sets = Geometry(gid);
        if (!sets) return err.empty();   // a skipped geometry is noted; a real failure carries err
        // Material bindings of this instance.
        std::map<std::string, std::string> bind;
        const int bm = x.First(inst, "bind_material");
        if (bm >= 0) {
            const int tc = x.First(bm, "technique_common");
            for (int im : x.All(tc, "instance_material")) bind[x.Attr(im, "symbol")] = UrlId(x.Attr(im, "target"));
        }
        std::vector<std::string> skelRoots;
        for (int sn : x.All(inst, "skeleton")) {
            std::string id; for (char c : x.Text(sn)) if (!IsWs(c)) id += c;
            skelRoots.push_back(UrlId(id));
        }
        for (const PrimSet& ps : *sets) {
            if (out->parts.size() >= kMaxParts) return Fail("COLLADA: too many mesh instances");
            if (totalVerts + ps.mesh.vertices.size() > kMaxOutVerts) return Fail("COLLADA: model too large");
            totalVerts += ps.mesh.vertices.size();
            ImportedPart part;
            part.mesh = ps.mesh;
            part.node = nodeIdx;
            auto b = bind.find(ps.materialSymbol);
            part.material = MaterialIndex(b != bind.end() ? b->second : ps.materialSymbol);
            if (flatten) Bake(part.mesh, world, false);
            else if (skin) {
                const std::size_t nv = part.mesh.vertices.size();
                part.jointIdx.assign(nv, std::array<int, 4>{0, 0, 0, 0});
                part.jointWt.assign(nv, std::array<float, 4>{0, 0, 0, 0});
                for (std::size_t j = 0; j < skin->joints.size(); ++j) {
                    ImportedBone ib; ib.name = skin->joints[j]; ib.inverseBind = skin->invBind[j];
                    part.bones.push_back(std::move(ib));
                }
                for (std::size_t v = 0; v < nv; ++v) {
                    const std::size_t pi = (std::size_t)ps.posIdx[v];
                    if (pi >= skin->weights.size()) continue;
                    const auto& w = skin->weights[pi];
                    for (std::size_t k = 0; k < w.size() && k < 4; ++k) { part.jointIdx[v][k] = w[k].first; part.jointWt[v][k] = w[k].second; }
                }
                pending.push_back(PendingSkin{out->parts.size(), skin->joints, skelRoots});
            }
            out->parts.push_back(std::move(part));
        }
        return true;
    }

    bool BuildNode(int xn, int parent, const Mat4& parentWorld, int depth) {
        if (depth > kMaxSceneDepth) return Fail("COLLADA: node hierarchy too deep (or an instance_node loop)");
        if (nodes.size() >= kMaxSceneNodes) return Fail("COLLADA: too many nodes");
        ImportedNode in;
        const std::string id = x.Attr(xn, "id"), nm = x.Attr(xn, "name"), sid = x.Attr(xn, "sid");
        in.name = !id.empty() ? id : !nm.empty() ? nm : sid;   // ids are what skins and animations name
        in.parent = parent;
        std::vector<TElem> elems;
        const Mat4 local = LocalTransform(xn, elems);
        nodeElems.push_back(std::move(elems));
        if (!local.Decompose(in.position, in.rotation, in.scale)) ++badDecompose;
        const Mat4 world = parentWorld * local;
        const int me = (int)nodes.size();
        nodes.push_back(std::move(in));
        nodeId.push_back(id);
        nodeSid.push_back(sid);
        for (int k : x.n[(std::size_t)xn].kids) {
            const std::string& nm2 = x.n[(std::size_t)k].name;
            if (nm2 == "node") { if (!BuildNode(k, me, world, depth + 1)) return false; }
            else if (nm2 == "instance_node") {
                const int target = Lookup(UrlId(x.Attr(k, "url")));
                if (target >= 0 && x.n[(std::size_t)target].name == "node")
                    if (!BuildNode(target, me, world, depth + 1)) return false;
            }
            else if (nm2 == "instance_geometry") { if (!AddInstance(me, world, k, false)) return false; }
            else if (nm2 == "instance_controller") { if (!AddInstance(me, world, k, true)) return false; }
            else if (nm2 == "instance_camera" || nm2 == "instance_light") ++camLights;
        }
        return true;
    }

    int NodeByRef(const std::string& ref) const {   // an id, else a sid
        for (std::size_t i = 0; i < nodeId.size(); ++i) if (!ref.empty() && nodeId[i] == ref) return (int)i;
        for (std::size_t i = 0; i < nodeSid.size(); ++i) if (!ref.empty() && nodeSid[i] == ref) return (int)i;
        return -1;
    }
    bool Under(int node, int root) const {
        for (int n = node; n >= 0; n = nodes[(std::size_t)n].parent) if (n == root) return true;
        return false;
    }

    void ResolveBoneNames() {
        for (const PendingSkin& ps : pending) {
            ImportedPart& part = out->parts[ps.part];
            std::vector<int> roots;
            for (const std::string& r : ps.skeletonRoots) { int n = NodeByRef(r); if (n >= 0) roots.push_back(n); }
            for (std::size_t j = 0; j < part.bones.size(); ++j) {
                const std::string& jn = ps.joints[j];
                int found = -1;
                for (int pass = 0; pass < 2 && found < 0; ++pass)
                    for (std::size_t i = 0; i < nodes.size() && found < 0; ++i) {
                        if (nodeSid[i] != jn && nodeId[i] != jn) continue;
                        if (pass == 0 && !roots.empty()) {
                            bool in = false;
                            for (int r : roots) if (Under((int)i, r)) in = true;
                            if (!in) continue;
                        }
                        found = (int)i;
                    }
                if (found >= 0) part.bones[j].name = nodes[(std::size_t)found].name;
                else if (std::find(missingJoints.begin(), missingJoints.end(), jn) == missingJoints.end()) missingJoints.push_back(jn);
            }
        }
    }

    // ---- animation --------------------------------------------------------
    // Every channel animates one value of one transform element of a node (a whole matrix,
    // a translate/scale/rotate, or one component of them). For each animated node the
    // element values are sampled at the union of its channels' key times, recomposed
    // into the node's local matrix, and decomposed into position / rotation / scale keys.
    struct Track {
        int node = -1, elem = -1, comp = -1;   // elem -1: replaces the node's whole local matrix
        int stride = 1;
        std::vector<float> times, vals;
        std::vector<char> step;                // per key: the segment after it holds the key's value
    };

    static float SampleTrack(const Track& t, double at, int k) {
        const std::size_t n = t.times.size();
        auto v = [&](std::size_t i) { return t.vals[i * (std::size_t)t.stride + (std::size_t)k]; };
        if (at <= t.times[0]) return v(0);
        if (at >= t.times[n - 1]) return v(n - 1);
        std::size_t i = (std::size_t)(std::upper_bound(t.times.begin(), t.times.end(), (float)at) - t.times.begin());
        if (i == 0) return v(0);
        --i;
        if (i + 1 >= n) return v(n - 1);
        if (t.step[i]) return v(i);
        const double d = (double)t.times[i + 1] - (double)t.times[i];
        const double u = d > 0.0 ? (at - (double)t.times[i]) / d : 1.0;
        return (float)((double)v(i) + ((double)v(i + 1) - (double)v(i)) * u);
    }

    static Mat4 ComposeElems(const std::vector<TElem>& es) {
        Mat4 m;
        for (const TElem& e : es) {
            if (e.kind == 'm') m = m * MatFromRowMajor(e.v);
            else if (e.kind == 't') m = m * Mat4::Translate(Vec3{e.v[0], e.v[1], e.v[2]});
            else if (e.kind == 's') m = m * Mat4::Scale(Vec3{e.v[0], e.v[1], e.v[2]});
            else if (e.v[0] != 0.0f || e.v[1] != 0.0f || e.v[2] != 0.0f) m = m * Mat4::Rotate(Quat::AngleAxis(e.v[3], Vec3{e.v[0], e.v[1], e.v[2]}));
        }
        return m;
    }

    // "location.X" / "rotationZ.ANGLE" / "translate(1)" / "mat(1)(3)" -> component index, or -1 for the whole element.
    static bool ParseComponent(const std::string& s, char kind, int& comp) {
        comp = -1;
        if (s.empty()) return true;
        if (s[0] == '.') {
            const std::string c = s.substr(1);
            if (c == "X") comp = 0; else if (c == "Y") comp = 1; else if (c == "Z") comp = 2; else if (c == "W") comp = 3;
            else if (c == "ANGLE" && kind == 'r') comp = 3;
            else return false;
            return true;
        }
        int a = -1, b = -1, used = 0;
        if (std::sscanf(s.c_str(), "(%d)(%d)%n", &a, &b, &used) == 2 && (std::size_t)used == s.size()) {
            if (kind != 'm' || a < 0 || a > 3 || b < 0 || b > 3) return false;
            comp = a * 4 + b; return true;
        }
        if (std::sscanf(s.c_str(), "(%d)%n", &a, &used) == 1 && (std::size_t)used == s.size()) {
            if (kind == 'm' || a < 0 || a > 3) return false;
            comp = a; return true;
        }
        return false;
    }

    void SkipChannel(const std::string& target) { if (animSkipped++ == 0) animExample = target; }

    bool MakeTrack(int ch, Track& tr) {
        const std::string target = x.Attr(ch, "target");
        const std::size_t slash = target.find('/');
        if (slash == std::string::npos) return false;
        const int ni = NodeByRef(target.substr(0, slash));
        if (ni < 0) return false;
        std::string what = target.substr(slash + 1);
        std::size_t cut = what.find_first_of(".(");
        const std::string sidName = what.substr(0, cut), compText = cut == std::string::npos ? std::string() : what.substr(cut);
        tr.node = ni;
        const std::vector<TElem>& es = nodeElems[(std::size_t)ni];
        int found = -1;
        for (std::size_t i = 0; i < es.size() && found < 0; ++i) if (!sidName.empty() && es[i].sid == sidName) found = (int)i;
        int expect;
        if (found >= 0) {
            tr.elem = found;
            int comp;
            if (!ParseComponent(compText, es[(std::size_t)found].kind, comp)) return false;
            tr.comp = comp;
            expect = comp >= 0 ? 1 : es[(std::size_t)found].n;
        } else if ((sidName == "transform" || sidName == "matrix") && compText.empty()) {
            tr.elem = -1; tr.comp = -1; expect = 16;   // whole local matrix
        } else return false;
        const int smp = [&] { const int s = Lookup(UrlId(x.Attr(ch, "source"))); return (s >= 0 && x.n[(std::size_t)s].name == "sampler") ? s : -1; }();
        if (smp < 0) return false;
        std::string inSrc, outSrc, intSrc;
        for (int in : x.All(smp, "input")) {
            const std::string sem = x.Attr(in, "semantic");
            if (sem == "INPUT") inSrc = UrlId(x.Attr(in, "source"));
            else if (sem == "OUTPUT") outSrc = UrlId(x.Attr(in, "source"));
            else if (sem == "INTERPOLATION") intSrc = UrlId(x.Attr(in, "source"));
        }
        Src times, vals;
        if (!ReadSrc(inSrc, times) || !ReadSrc(outSrc, vals) || times.stride != 1 || vals.stride != expect) return false;
        const long nk = std::min(times.Count(), vals.Count());
        if (nk < 1) return false;
        std::vector<std::string> interp;
        if (!intSrc.empty()) {
            const int is = Lookup(intSrc);
            const int na = is >= 0 ? x.First(is, "Name_array") : -1;
            if (na >= 0) interp = Tokens(x.Text(na));
        }
        tr.stride = expect;
        for (long i = 0; i < nk; ++i) {
            const float t = times.At(i, 0);
            if (!vals.Has(i, expect) || (i > 0 && t < tr.times.back())) return false;   // keys must be in time order
            tr.times.push_back(t);
            for (int c = 0; c < expect; ++c) tr.vals.push_back(vals.At(i, c));
            char st = 0;
            if (i < (long)interp.size()) {
                const std::string& m = interp[(std::size_t)i];
                if (m == "STEP") st = 1;
                else if (m != "LINEAR" && !m.empty()) interpApprox[m]++;
            }
            tr.step.push_back(st);
        }
        return true;
    }

    void BuildClip(const std::vector<Track>& tracks, ImportedClip& clip) {
        std::map<int, std::vector<std::size_t>> byNode;
        for (std::size_t i = 0; i < tracks.size(); ++i) byNode[tracks[i].node].push_back(i);
        for (const auto& kv : byNode) {
            const int ni = kv.first;
            std::vector<double> ts;
            for (std::size_t ti : kv.second) {
                const Track& t = tracks[ti];
                const bool rot = t.elem >= 0 && nodeElems[(std::size_t)ni][(std::size_t)t.elem].kind == 'r';
                for (std::size_t i = 0; i < t.times.size(); ++i) {
                    ts.push_back(t.times[i]);
                    if (i + 1 >= t.times.size()) continue;
                    const double a = t.times[i], b = t.times[i + 1], d = b - a;
                    if (d <= 0.0) continue;
                    if (t.step[i]) { ts.push_back(b - std::min(1e-4, d * 0.5)); continue; }   // hold, then jump at the next key
                    if (rot) {   // a slerp between keys takes the short way: keep each piece under 90 degrees
                        const int k = t.comp >= 0 ? 0 : 3;
                        const double da = std::fabs((double)t.vals[(i + 1) * (std::size_t)t.stride + (std::size_t)k] - (double)t.vals[i * (std::size_t)t.stride + (std::size_t)k]);
                        const int parts = (int)std::min(64.0, std::ceil(da / 90.0));
                        for (int p = 1; p < parts; ++p) ts.push_back(a + d * p / parts);
                    }
                }
            }
            std::sort(ts.begin(), ts.end());
            ts.erase(std::unique(ts.begin(), ts.end(), [](double a, double b) { return b - a < 1e-7; }), ts.end());
            ImportedChannel ic; ic.node = nodes[(std::size_t)ni].name;
            Quat prev{0, 0, 0, 1};
            bool first = true;
            for (double at : ts) {
                std::vector<TElem> es = nodeElems[(std::size_t)ni];
                bool whole = false; Mat4 wm;
                for (std::size_t ti : kv.second) {
                    const Track& t = tracks[ti];
                    if (t.elem < 0) {
                        float a[16]; for (int c = 0; c < 16; ++c) a[c] = SampleTrack(t, at, c);
                        wm = MatFromRowMajor(a); whole = true;
                    } else if (t.comp >= 0) es[(std::size_t)t.elem].v[t.comp] = SampleTrack(t, at, 0);
                    else for (int c = 0; c < t.stride; ++c) es[(std::size_t)t.elem].v[c] = SampleTrack(t, at, c);
                }
                const Mat4 m = whole ? wm : ComposeElems(es);
                Vec3 p, s; Quat q;
                if (!m.Decompose(p, q, s)) continue;
                if (!first && (q.x * prev.x + q.y * prev.y + q.z * prev.z + q.w * prev.w) < 0.0f) q = Quat{-q.x, -q.y, -q.z, -q.w};
                prev = q; first = false;
                ic.position.push_back({(float)at, p}); ic.rotation.push_back({(float)at, q}); ic.scale.push_back({(float)at, s});
            }
            if (!ic.rotation.empty()) clip.channels.push_back(std::move(ic));
        }
    }

    void CollectTracks(int top, std::vector<Track>& tracks) {
        std::vector<int> stack{top};
        while (!stack.empty()) {
            const int an = stack.back(); stack.pop_back();
            for (int k : x.n[(std::size_t)an].kids) if (x.n[(std::size_t)k].name == "animation") stack.push_back(k);
            for (int ch : x.All(an, "channel")) {
                Track tr;
                if (MakeTrack(ch, tr)) tracks.push_back(std::move(tr)); else SkipChannel(x.Attr(ch, "target"));
            }
        }
    }

    // Keys of `ch` inside [t0, t1], times shifted by -t0, with a key interpolated at each edge that has none.
    static void TrimChannel(ImportedChannel& ch, float t0, float t1) {
        auto trimV = [&](std::vector<ImportedKeyV3>& k) {
            if (k.empty()) return;
            auto at = [&](float t) {
                if (t <= k.front().time) return k.front().value;
                if (t >= k.back().time) return k.back().value;
                for (std::size_t i = 1; i < k.size(); ++i) if (t <= k[i].time) {
                    const float d = k[i].time - k[i - 1].time, a = d > 0 ? (t - k[i - 1].time) / d : 0.0f;
                    return k[i - 1].value + (k[i].value - k[i - 1].value) * a;
                }
                return k.back().value;
            };
            std::vector<ImportedKeyV3> r;
            r.push_back({0.0f, at(t0)});
            for (const auto& e : k) if (e.time > t0 + 1e-6f && e.time < t1 - 1e-6f) r.push_back({e.time - t0, e.value});
            if (t1 > t0 + 1e-6f) r.push_back({t1 - t0, at(t1)});
            k = std::move(r);
        };
        trimV(ch.position); trimV(ch.scale);
        auto& k = ch.rotation;
        if (k.empty()) return;
        auto at = [&](float t) {
            if (t <= k.front().time) return k.front().value;
            if (t >= k.back().time) return k.back().value;
            for (std::size_t i = 1; i < k.size(); ++i) if (t <= k[i].time) {
                const float d = k[i].time - k[i - 1].time, a = d > 0 ? (t - k[i - 1].time) / d : 0.0f;
                Quat q0 = k[i - 1].value, q1 = k[i].value;
                if (q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w < 0) q1 = Quat{-q1.x, -q1.y, -q1.z, -q1.w};
                return Quat{q0.x + (q1.x - q0.x) * a, q0.y + (q1.y - q0.y) * a, q0.z + (q1.z - q0.z) * a, q0.w + (q1.w - q0.w) * a}.Normalized();
            }
            return k.back().value;
        };
        std::vector<ImportedKeyQ> r;
        r.push_back({0.0f, at(t0)});
        for (const auto& e : k) if (e.time > t0 + 1e-6f && e.time < t1 - 1e-6f) r.push_back({e.time - t0, e.value});
        if (t1 > t0 + 1e-6f) r.push_back({t1 - t0, at(t1)});
        k = std::move(r);
    }

    void ReadAnimations(int lib) {
        // <library_animation_clips>: named takes (start / end, the animations they play). When there
        // are any, they are the clips: each one holds its animations' channels, cut to [start, end] and
        // moved to start at 0.
        if (const int lc = x.First(0, "library_animation_clips"); lc >= 0 && x.First(lc, "animation_clip") >= 0) {
            int n = 0;
            for (int ac : x.All(lc, "animation_clip")) {
                std::vector<Track> tracks;
                for (int ia : x.All(ac, "instance_animation")) {
                    const int an = Lookup(UrlId(x.Attr(ia, "url")));
                    if (an >= 0 && x.n[(std::size_t)an].name == "animation") CollectTracks(an, tracks);
                }
                ImportedClip clip;
                clip.name = !x.Attr(ac, "name").empty() ? x.Attr(ac, "name") : !x.Attr(ac, "id").empty() ? x.Attr(ac, "id") : "clip" + std::to_string(n);
                ++n;
                BuildClip(tracks, clip);
                std::vector<float> sv, ev;
                if (x.HasAttr(ac, "start") && x.HasAttr(ac, "end") && ParseFloats(x.Attr(ac, "start"), sv) && ParseFloats(x.Attr(ac, "end"), ev) && sv.size() == 1 && ev.size() == 1 && ev[0] > sv[0])
                    for (ImportedChannel& ch : clip.channels) TrimChannel(ch, sv[0], ev[0]);
                if (!clip.channels.empty()) out->clips.push_back(std::move(clip)); else ++emptyClips;
            }
            if (!out->clips.empty() || emptyClips) return;
        }
        // A top-level <animation> that holds other animations is a clip of its own. Plain
        // top-level animations (one channel group per bone or property) are merged into one clip.
        std::vector<Track> flat;
        std::vector<int> leaves;
        int unnamed = 0;
        auto nameOf = [&](int a) { return !x.Attr(a, "name").empty() ? x.Attr(a, "name") : !x.Attr(a, "id").empty() ? x.Attr(a, "id") : "clip" + std::to_string(unnamed); };
        for (int top : x.All(lib, "animation")) {
            if (x.First(top, "animation") < 0) { leaves.push_back(top); CollectTracks(top, flat); ++unnamed; continue; }
            std::vector<Track> tracks;
            CollectTracks(top, tracks);
            ImportedClip clip; clip.name = nameOf(top); ++unnamed;
            BuildClip(tracks, clip);
            if (!clip.channels.empty()) out->clips.push_back(std::move(clip)); else ++emptyClips;
        }
        if (!leaves.empty()) {
            ImportedClip clip; clip.name = leaves.size() == 1 ? nameOf(leaves[0]) : "animation";
            BuildClip(flat, clip);
            if (!clip.channels.empty()) out->clips.push_back(std::move(clip)); else ++emptyClips;
        }
    }

    // ---- driver -----------------------------------------------------------
    bool Run(const std::string& raw) {
        out->backend = "own";
        std::string conv;
        if (const bool le = raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE,
                       be = raw.size() >= 2 && (unsigned char)raw[0] == 0xFE && (unsigned char)raw[1] == 0xFF,
                       leNoBom = raw.size() >= 4 && raw[0] == '<' && raw[1] == 0 && raw[3] == 0,
                       beNoBom = raw.size() >= 4 && raw[0] == 0 && raw[1] == '<' && raw[2] == 0;
            le || be || leNoBom || beNoBom) {
            // UTF-16: convert to UTF-8 first (the XML reader and every number in it are UTF-8 / ASCII)
            const bool little = le || leNoBom;
            std::size_t i = (le || be) ? 2 : 0;
            conv.reserve(raw.size() / 2 + 8);
            auto unit = [&](std::size_t at) { return little ? (unsigned)((unsigned char)raw[at] | ((unsigned char)raw[at + 1] << 8)) : (unsigned)(((unsigned char)raw[at] << 8) | (unsigned char)raw[at + 1]); };
            for (; i + 1 < raw.size(); i += 2) {
                unsigned c = unit(i);
                if (c >= 0xD800 && c < 0xDC00 && i + 3 < raw.size() && unit(i + 2) >= 0xDC00 && unit(i + 2) < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (unit(i + 2) - 0xDC00); i += 2; }
                else if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;   // lone surrogate
                if (c < 0x80) conv.push_back((char)c);
                else if (c < 0x800) { conv.push_back((char)(0xC0 | (c >> 6))); conv.push_back((char)(0x80 | (c & 0x3F))); }
                else if (c < 0x10000) { conv.push_back((char)(0xE0 | (c >> 12))); conv.push_back((char)(0x80 | ((c >> 6) & 0x3F))); conv.push_back((char)(0x80 | (c & 0x3F))); }
                else { conv.push_back((char)(0xF0 | (c >> 18))); conv.push_back((char)(0x80 | ((c >> 12) & 0x3F))); conv.push_back((char)(0x80 | ((c >> 6) & 0x3F))); conv.push_back((char)(0x80 | (c & 0x3F))); }
            }
        }
        const std::string& text = conv.empty() ? raw : conv;
        if (!ParseXml(text, x, err)) return false;
        if (x.n.empty() || x.n[0].name != "COLLADA") return Fail("not a COLLADA file (root element is not COLLADA)");
        for (std::size_t i = 0; i < x.n.size(); ++i) {
            const std::string id = x.Attr((int)i, "id");
            if (!id.empty() && !ids.count(id)) ids[id] = (int)i;
        }
        float unit = 1.0f;
        std::string up = "Y_UP";
        if (const int as = x.First(0, "asset"); as >= 0) {
            if (const int u = x.First(as, "unit"); u >= 0) {
                std::vector<float> uv;
                if (ParseFloats(x.Attr(u, "meter"), uv, &decimalCommas) && uv.size() == 1 && std::isfinite(uv[0]) && uv[0] > 0.0f) unit = uv[0];
            }
            if (const int a = x.First(as, "up_axis"); a >= 0) {
                std::string t; for (char c : x.Text(a)) if (!IsWs(c)) t += c;
                if (t == "X_UP" || t == "Y_UP" || t == "Z_UP") up = t;
            }
        }
        // Root: unit scale, then the rotation that makes +Y up (the engine is Y-up).
        Mat4 root = Mat4::Scale(Vec3{unit, unit, unit});
        if (up == "Z_UP") { const float a[16] = {1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1}; root = MatFromRowMajor(a) * root; }
        else if (up == "X_UP") { const float a[16] = {0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; root = MatFromRowMajor(a) * root; }

        int vs = -1;
        if (const int sc = x.First(0, "scene"); sc >= 0)
            if (const int iv = x.First(sc, "instance_visual_scene"); iv >= 0) vs = Lookup(UrlId(x.Attr(iv, "url")));
        if (vs < 0 || x.n[(std::size_t)vs].name != "visual_scene") {
            const int lib = x.First(0, "library_visual_scenes");
            vs = lib >= 0 ? x.First(lib, "visual_scene") : -1;
        }
        if (vs < 0) return Fail("COLLADA: no visual_scene");

        ImportedNode rootNode;
        rootNode.name = x.Attr(vs, "id").empty() ? "Scene" : x.Attr(vs, "id");
        if (!root.Decompose(rootNode.position, rootNode.rotation, rootNode.scale)) ++badDecompose;
        nodes.push_back(rootNode); nodeId.push_back(""); nodeSid.push_back(""); nodeElems.emplace_back();
        for (int k : x.n[(std::size_t)vs].kids)
            if (x.n[(std::size_t)k].name == "node" && !BuildNode(k, 0, root, 1)) return false;
        if (!err.empty()) return false;
        if (out->parts.empty()) {
            // Cameras, lights and empty nodes only: an empty mesh set, not an error (Assimp reads such a file too). A scene
            // with no node at all is still refused.
            if (nodes.size() <= 1) return Fail("COLLADA: the scene has no mesh");
            out->notes.push_back("COLLADA: the scene has no mesh (cameras, lights or empty nodes only); only its node hierarchy was read");
        }

        if (!flatten) {
            ResolveBoneNames();
            for (const std::string& j : missingJoints) out->notes.push_back("COLLADA: skin joint '" + j + "' is not a node of the scene; its bone will not move");
            if (const int la = x.First(0, "library_animations"); la >= 0) ReadAnimations(la);
            out->nodes = nodes;
        } else {
            out->nodes.assign(1, ImportedNode{});
            out->nodes[0].name = rootNode.name;
        }
        EmitNotes();
        return true;
    }

    void EmitNotes() {
        auto note = [&](const std::string& s) { out->notes.push_back("COLLADA: " + s); };
        for (const auto& kv : skippedPrims) note("not read: " + std::to_string(kv.second) + " x " + kv.first);
        if (holes) note(std::to_string(holes) + " polygon hole(s) (<ph>) ignored; the outer polygon is kept");
        if (vcolors) note("vertex colors (COLOR input) are not read");
        if (extraUv) note("only the first UV set is read; " + std::to_string(extraUv) + " extra UV set(s) ignored");
        if (morphs) note(std::to_string(morphs) + " morph controller(s) (blend shapes) not read; the base mesh is imported");
        if (skews) note(std::to_string(skews) + " <skew>/<lookat> transform(s) ignored");
        if (camLights) note(std::to_string(camLights) + " camera/light instance(s) not imported");
        if (skinsDropped) note(std::to_string(skinsDropped) + " skin controller(s) were malformed; the mesh is imported unskinned");
        if (decimalCommas) note(std::to_string(decimalCommas) + " number(s) in the geometry were written with a decimal comma and were read as decimal points");
        if (weightsTrunc) note(std::to_string(weightsTrunc) + " vertices had more than 4 bone weights; the 4 strongest are kept");
        if (badDecompose) note(std::to_string(badDecompose) + " non-finite node transform(s) replaced by identity");
        if (animSkipped) note(std::to_string(animSkipped) + " animation channel(s) not read (target element not found, wrong value count, keys out of order or unreadable sampler; first: '" + animExample + "')");
        for (const auto& kv : interpApprox) note(std::to_string(kv.second) + " animation key(s) use " + kv.first + " interpolation, read as LINEAR (only LINEAR and STEP are honoured)");
        if (emptyClips) note(std::to_string(emptyClips) + " animation(s) had no readable channel and were dropped");
    }
};

} // namespace dae
} // namespace

bool ReadColladaBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    dae::Reader r;
    r.baseDir = baseDir;
    r.flatten = flatten;
    r.out = &out;
    if (!r.Run(text)) {
        const std::string m = r.err.empty() ? "COLLADA: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    return true;
}

bool ReadColladaFile(const std::string& path, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    std::FILE* fp = detail::OpenRead(path);
    if (!fp) { if (err) *err = "cannot open file"; return false; }
    std::string bytes;
    char buf[65536];
    for (;;) {
        const std::size_t n = std::fread(buf, 1, sizeof(buf), fp);
        if (n == 0) break;
        if (bytes.size() + n > dae::kMaxFileBytes) { std::fclose(fp); if (err) *err = "file too large"; return false; }
        bytes.append(buf, n);
    }
    std::fclose(fp);
    const std::size_t sl = path.find_last_of("/\\");
    return ReadColladaBuffer(bytes, sl == std::string::npos ? std::string() : path.substr(0, sl + 1), out, err, flatten);
}

} // namespace okay
