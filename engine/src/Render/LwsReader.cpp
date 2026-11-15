// Our own LWS (LightWave scene, "LWSC" text) and MOT (LightWave motion, "LWMO" text) reader. Written from the public description
// of the LightWave scene file format (the header and version line, LoadObject / LoadObjectLayer / AddNullObject / AddCamera /
// AddLight / AddBone items, ParentItem, the NumChannels / Channel / { Envelope / Key / Behaviors blocks with channels 0 to 8 as
// position, heading, pitch, bank and scale) and from reasoning about that structure; no code, table or snippet of any other
// importer was read. Each item is a node; the objects it names are read through a loader the caller supplies, and only from
// inside the scene's own folder. No real LWS file was available to check it against (docs/own_lws_reader.md). Bounds-checked,
// capped, no recursion. See okay/Render/ModelData.hpp.
#include "ReaderCommon.hpp"
#include "SceneRefPath.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace lws {

constexpr std::size_t kMaxFileBytes = 64u * 1024u * 1024u;
constexpr std::size_t kMaxLine = 65536;                    // a longer line is skipped, never split into tokens
constexpr std::size_t kMaxItems = 100000;
constexpr std::size_t kMaxKeys = 4000000;                  // envelope keys in the whole file (read or not)
constexpr std::size_t kMaxNodes = 1000000, kMaxLoadedFiles = 256;
constexpr std::size_t kMaxOutVerts = 4000000, kMaxOutTris = 4000000, kMaxLoadedVerts = 4000000, kMaxLoadedTris = 4000000;
constexpr double kMaxTime = 1e7;
constexpr std::size_t kMaxNameBytes = 256, kMaxPathBytes = 4096;   // item names and paths are kept this long at most (they are copied several times)
constexpr std::size_t kMaxPlacedBones = 1000000, kMaxPlacedBoneBytes = 128u * 1024u * 1024u;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "LWS: " + m; return false; }

struct Tk { std::size_t b, e; };
std::size_t Tokenize(const std::string& s, Tk* t, std::size_t maxT) {
    std::size_t n = 0, i = 0;
    while (i < s.size() && n < maxT) {
        while (i < s.size() && detail::txt::IsWs(s[i])) ++i;
        if (i >= s.size()) break;
        const std::size_t a = i;
        while (i < s.size() && !detail::txt::IsWs(s[i])) ++i;
        t[n++] = Tk{a, i};
    }
    return n;
}
bool Is(const std::string& s, const Tk& t, const char* w) { return s.compare(t.b, t.e - t.b, w) == 0 && std::strlen(w) == t.e - t.b; }
bool Num(const std::string& s, const Tk& t, double& v) { return detail::rc::ParseNumber(s.data() + t.b, s.data() + t.e, v); }
// The rest of the line from token `from` on (a file name may hold spaces), trailing white space dropped.
std::string Rest(const std::string& s, const Tk* t, std::size_t n, std::size_t from) {
    if (from >= n) return std::string();
    std::size_t e = s.size();
    while (e > t[from].b && detail::txt::IsWs(s[e - 1])) --e;
    return s.substr(t[from].b, e - t[from].b);
}
// Exactly 8 hexadecimal digits: an item id.
bool HexId(const std::string& s, const Tk& t, std::int64_t& v) {
    if (t.e - t.b != 8) return false;
    std::int64_t r = 0;
    for (std::size_t i = t.b; i < t.e; ++i) {
        const char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0'; else if (c >= 'a' && c <= 'f') d = 10 + c - 'a'; else if (c >= 'A' && c <= 'F') d = 10 + c - 'A'; else return false;
        r = r * 16 + d;
    }
    v = r;
    return true;
}

struct Key { double t, v; };
enum Kind { kObject, kNull, kLight, kCamera, kBone };
struct Item {
    Kind kind = kNull;
    std::string name, path;
    std::int64_t id = -1, parent = -1;
    std::vector<Key> ch[9];
};

double Eval(const std::vector<Key>& k, double t, double def) {
    if (k.empty()) return def;
    if (t <= k.front().t) return k.front().v;
    if (t >= k.back().t) return k.back().v;
    std::size_t lo = 0, hi = k.size() - 1;   // k[lo].t <= t < k[hi].t
    while (hi - lo > 1) { const std::size_t m = lo + (hi - lo) / 2; if (k[m].t <= t) lo = m; else hi = m; }
    const double d = k[hi].t - k[lo].t;
    if (!(d > 0)) return k[hi].v;
    return k[lo].v + (k[hi].v - k[lo].v) * ((t - k[lo].t) / d);
}

// LightWave's heading (about Y), pitch (about X) and bank (about Z), in radians, bank applied first; then the z mirror that takes the
// left-handed file to the right-handed engine (the vector part of a rotation turns with the mirror like a pseudo vector).
Quat RotOf(double h, double p, double b) {
    auto q = [](double a, float x, float y, float z) { const float s = (float)std::sin(a * 0.5), c = (float)std::cos(a * 0.5); return Quat{x * s, y * s, z * s, c}; };
    const Quat r = (q(h, 0, 1, 0) * q(p, 1, 0, 0) * q(b, 0, 0, 1)).Normalized();
    return Quat{-r.x, -r.y, r.z, r.w};
}

struct Loaded { bool ok = false; ImportedModel m; std::vector<int> matMap; };

} // namespace lws
} // namespace

static bool ReadLwsImpl(const std::string& input, const std::string& baseDir, ImportedModel& out, std::string* err, const IrrMeshLoader* loader) {
    using namespace lws;
    std::string text = input;
    bool converted = false;
    std::string why;
    if (!detail::txt::NormalizeEncoding(text, converted, why, kMaxFileBytes)) return Fail(err, why);

    detail::txt::Lines lines(text);
    std::string line;
    Tk tk[16];
    // ---- header: "LWSC" (scene) or "LWMO" (motion), then the version ----
    bool isMotion = false;
    int version = 0;
    {
        bool gotMagic = false;
        while (lines.Next(line)) {
            const std::size_t n = Tokenize(line, tk, 16);
            if (n == 0) continue;
            if (!gotMagic) {
                if (Is(line, tk[0], "LWSC")) isMotion = false;
                else if (Is(line, tk[0], "LWMO")) isMotion = true;
                else return Fail(err, "not a LightWave scene or motion file (no \"LWSC\" or \"LWMO\" first line)");
                gotMagic = true;
                if (n >= 2) { double v; if (!Num(line, tk[1], v) || v != std::floor(v)) return Fail(err, "bad version number"); version = v < 0 || v > 1000 ? -1 : (int)v; break; }
                continue;
            }
            double v;
            if (!Num(line, tk[0], v) || v != std::floor(v)) return Fail(err, "bad version line");
            version = v < 0 || v > 1000 ? -1 : (int)v;   // not cast first: a huge value is undefined behaviour
            break;
        }
        if (!gotMagic) return Fail(err, "the file is empty");
        if (version < 1 || version > 6) return Fail(err, "scene version " + std::to_string(version) + " (versions 1 to 6 are read)");
    }

    std::vector<Item> items;
    if (isMotion) { Item m; m.kind = kNull; m.name = "motion"; items.push_back(std::move(m)); }
    int cur = isMotion ? 0 : -1, curChan = -1;
    bool inEnv = false, braceOnly = false;
    Item* sinkItem = nullptr;
    std::vector<Key>* sink = nullptr;
    std::size_t totalKeys = 0, envDeclared = 0;
    int objCount = 0, lightCount = 0, camCount = 0, boneCount = 0;
    int longPaths = 0, badKeys = 0, nonLinear = 0, longLines = 0, ignoredLines = 0, strayChannels = 0, strayParents = 0, badParentLines = 0, behaviors = 0, unclosedEnv = 0, strayKeys = 0;
    double fps = 30;
    auto isItemCmd = [&](const Tk& t) { return Is(line, t, "LoadObject") || Is(line, t, "LoadObjectLayer") || Is(line, t, "AddNullObject") || Is(line, t, "AddCamera") || Is(line, t, "AddLight") || Is(line, t, "AddBone") || Is(line, t, "Channel") || Is(line, t, "ParentItem"); };

    while (lines.Next(line)) {
        if (line.size() > kMaxLine) { ++longLines; continue; }
        const std::size_t n = Tokenize(line, tk, 16);
        if (n == 0) continue;
        if (inEnv) {
            if (isItemCmd(tk[0])) { ++unclosedEnv; inEnv = false; }   // the "}" is missing: go on with the command below
            else if (Is(line, tk[0], "}")) { inEnv = false; continue; }
            else if (Is(line, tk[0], "Key")) {
                if (++totalKeys > kMaxKeys) return Fail(err, "more than " + std::to_string(kMaxKeys) + " envelope keys (budget)");
                double v, t, span;
                if (n < 4 || !Num(line, tk[1], v) || !Num(line, tk[2], t) || !Num(line, tk[3], span) || std::fabs(t) > kMaxTime) { ++badKeys; continue; }
                if (span != 3.0) ++nonLinear;
                if (sink) sink->push_back(Key{t, v});
                continue;
            } else if (Is(line, tk[0], "Behaviors")) { ++behaviors; continue; }
            else {   // the key count line (or a field of a newer version): the count is only checked against the budget
                double c;
                if (n == 1 && Num(line, tk[0], c) && c >= 0) { envDeclared = (std::size_t)std::min(c, 1e18); if (envDeclared > kMaxKeys) return Fail(err, "an envelope declares " + std::to_string(envDeclared) + " keys (the limit is " + std::to_string(kMaxKeys) + ") (budget)"); }
                continue;
            }
        }
        if (Is(line, tk[0], "{")) {
            if (n >= 2 && Is(line, tk[1], "Envelope")) { inEnv = true; sinkItem = cur >= 0 ? &items[(std::size_t)cur] : nullptr; sink = (sinkItem && curChan >= 0 && curChan < 9) ? &sinkItem->ch[curChan] : nullptr; if (!sink) ++strayKeys; }
            else braceOnly = n == 1;
            continue;
        }
        if (Is(line, tk[0], "{Envelope")) { inEnv = true; sinkItem = cur >= 0 ? &items[(std::size_t)cur] : nullptr; sink = (sinkItem && curChan >= 0 && curChan < 9) ? &sinkItem->ch[curChan] : nullptr; if (!sink) ++strayKeys; continue; }
        if (braceOnly && Is(line, tk[0], "Envelope")) { braceOnly = false; inEnv = true; sinkItem = cur >= 0 ? &items[(std::size_t)cur] : nullptr; sink = (sinkItem && curChan >= 0 && curChan < 9) ? &sinkItem->ch[curChan] : nullptr; if (!sink) ++strayKeys; continue; }
        braceOnly = false;

        const bool newItem = Is(line, tk[0], "LoadObject") || Is(line, tk[0], "LoadObjectLayer") || Is(line, tk[0], "AddNullObject") || Is(line, tk[0], "AddCamera") || Is(line, tk[0], "AddLight") || Is(line, tk[0], "AddBone");
        if (newItem && !isMotion) {
            if (items.size() >= kMaxItems) return Fail(err, "more than " + std::to_string(kMaxItems) + " items (budget)");
            Item it;
            std::size_t pathFrom = 1;
            if (Is(line, tk[0], "LoadObject")) { it.kind = kObject; }
            else if (Is(line, tk[0], "LoadObjectLayer")) {
                it.kind = kObject;
                pathFrom = 2;   // the layer number
                std::int64_t id;
                if (n >= 4 && HexId(line, tk[2], id)) { it.id = id; pathFrom = 3; }   // an item id between the layer and the path
            } else if (Is(line, tk[0], "AddNullObject")) it.kind = kNull;
            else if (Is(line, tk[0], "AddCamera")) it.kind = kCamera;
            else if (Is(line, tk[0], "AddLight")) it.kind = kLight;
            else it.kind = kBone;
            if (it.kind == kObject) {
                it.path = Rest(line, tk, n, pathFrom);
                if (it.path.size() > kMaxPathBytes) { it.path.clear(); ++longPaths; }
                std::string stem = it.path;
                const std::size_t sl = stem.find_last_of("/\\");
                if (sl != std::string::npos) stem.erase(0, sl + 1);
                const std::size_t dot = stem.find_last_of('.');
                if (dot != std::string::npos && dot > 0) stem.erase(dot);
                it.name = stem.empty() ? std::string("object") : stem;
                if (it.id < 0) it.id = 0x10000000 + objCount;
                ++objCount;
            } else if (it.kind == kNull) {
                it.name = Rest(line, tk, n, 1);
                if (it.name.empty()) it.name = "null";
                it.id = 0x10000000 + objCount++;
            } else {
                std::int64_t id;
                if (n >= 2 && HexId(line, tk[1], id)) it.id = id;
                if (it.kind == kLight) { it.name = "light" + std::to_string(lightCount); if (it.id < 0) it.id = 0x20000000 + lightCount; ++lightCount; }
                else if (it.kind == kCamera) { it.name = "camera" + std::to_string(camCount); if (it.id < 0) it.id = 0x30000000 + camCount; ++camCount; }
                else { it.name = "bone" + std::to_string(boneCount); if (it.id < 0) it.id = 0x40000000 + boneCount; ++boneCount; }
            }
            if (it.name.size() > kMaxNameBytes) it.name.resize(kMaxNameBytes);
            items.push_back(std::move(it));
            cur = (int)items.size() - 1;
            curChan = -1;
            continue;
        }
        if (Is(line, tk[0], "Channel")) {
            double c;
            if (cur < 0 || n < 2 || !Num(line, tk[1], c) || c < 0 || c != std::floor(c)) { ++strayChannels; curChan = -1; }
            else curChan = c < 100 ? (int)c : -1;
            continue;
        }
        if (Is(line, tk[0], "ParentItem")) {
            std::int64_t id;
            if (cur < 0) ++strayParents;
            else if (n >= 2 && HexId(line, tk[1], id)) items[(std::size_t)cur].parent = id;
            else ++badParentLines;
            continue;
        }
        if (Is(line, tk[0], "FramesPerSecond") && n >= 2) { double v; if (Num(line, tk[1], v) && v > 0 && v <= 100000) fps = v; continue; }
        if (cur >= 0 && n >= 2 && (Is(line, tk[0], "CameraName") || Is(line, tk[0], "LightName") || Is(line, tk[0], "BoneName") || Is(line, tk[0], "ObjectName"))) {
            Item& it = items[(std::size_t)cur];
            const bool match = (Is(line, tk[0], "CameraName") && it.kind == kCamera) || (Is(line, tk[0], "LightName") && it.kind == kLight) || (Is(line, tk[0], "BoneName") && it.kind == kBone) || (Is(line, tk[0], "ObjectName") && (it.kind == kObject || it.kind == kNull));
            const std::string nm = Rest(line, tk, n, 1);
            if (match && !nm.empty()) it.name = nm.substr(0, kMaxNameBytes);
            continue;
        }
        ++ignoredLines;
    }
    if (inEnv) return Fail(err, "the file ends inside an envelope (truncated)");
    if (items.empty()) return Fail(err, "the scene has no object, camera, light or bone");

    // ---- parents first ----
    std::unordered_map<std::int64_t, int> byId;
    int dupIds = 0;
    for (std::size_t i = 0; i < items.size(); ++i) if (items[i].id >= 0 && !byId.emplace(items[i].id, (int)i).second) ++dupIds;
    std::vector<int> par(items.size(), -1);
    int missingParent = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].parent < 0) continue;
        auto f = byId.find(items[i].parent);
        if (f == byId.end()) ++missingParent;
        else par[i] = f->second;   // a self reference or a loop is cut below
    }
    std::vector<int> order;
    const int loops = detail::txt::OrderParentsFirst(par, order);

    // ---- nodes, rest pose, clip ----
    out.nodes.clear();
    {
        ImportedNode root;
        root.name = isMotion ? "LWMO" : "LWS";
        out.nodes.push_back(root);
    }
    std::unordered_map<std::string, int> used;
    used[out.nodes[0].name] = 1;
    std::vector<int> nodeOf(items.size(), 0);
    ImportedClip clip;
    clip.name = "scene";
    std::size_t outKeys = 0;
    for (Item& it : items) for (auto& c : it.ch) {
        std::stable_sort(c.begin(), c.end(), [](const Key& a, const Key& b) { return a.t < b.t; });
        std::vector<Key> d;   // two keys at one time: the later one wins
        for (const Key& k : c) { if (!d.empty() && d.back().t == k.t) d.back() = k; else d.push_back(k); }
        c.swap(d);
    }
    std::vector<std::string> nodeName(items.size());
    for (int ii : order) {
        const Item& it = items[(std::size_t)ii];
        std::string nm = it.name;
        int& u = used[nm];
        if (u++) { std::string cand; do { cand = nm + "_" + std::to_string(u - 1); ++u; } while (used.count(cand)); nm = cand; used[nm] = 1; }   // the new name must be unused too ("a", "a_1", "a")
        nodeName[(std::size_t)ii] = nm;
        ImportedNode nd;
        nd.name = nm;
        nd.parent = par[(std::size_t)ii] < 0 ? 0 : nodeOf[(std::size_t)par[(std::size_t)ii]];
        nd.position = Vec3{(float)Eval(it.ch[0], 0, 0), (float)Eval(it.ch[1], 0, 0), (float)-Eval(it.ch[2], 0, 0)};
        nd.rotation = RotOf(Eval(it.ch[3], 0, 0), Eval(it.ch[4], 0, 0), Eval(it.ch[5], 0, 0));
        nd.scale = Vec3{(float)Eval(it.ch[6], 0, 1), (float)Eval(it.ch[7], 0, 1), (float)Eval(it.ch[8], 0, 1)};
        if (!std::isfinite(nd.position.x + nd.position.y + nd.position.z + nd.scale.x + nd.scale.y + nd.scale.z)) return Fail(err, "item '" + it.name + "' has a transform that is not finite");
        nodeOf[(std::size_t)ii] = (int)out.nodes.size();
        out.nodes.push_back(std::move(nd));
        if (out.nodes.size() > kMaxNodes) return Fail(err, "too many nodes");
    }
    for (std::size_t ii = 0; ii < items.size(); ++ii) {
        const Item& it = items[ii];
        ImportedChannel chn;
        chn.node = nodeName[ii];
        auto times = [&](int a) {
            std::vector<double> t;
            for (int c = a; c < a + 3; ++c) for (const Key& k : it.ch[c]) t.push_back(k.t);
            std::sort(t.begin(), t.end());
            t.erase(std::unique(t.begin(), t.end()), t.end());
            return t;
        };
        if (!it.ch[0].empty() || !it.ch[1].empty() || !it.ch[2].empty())
            for (double t : times(0)) chn.position.push_back({(float)t, Vec3{(float)Eval(it.ch[0], t, 0), (float)Eval(it.ch[1], t, 0), (float)-Eval(it.ch[2], t, 0)}});
        if (!it.ch[3].empty() || !it.ch[4].empty() || !it.ch[5].empty()) {
            for (double t : times(3)) {
                Quat q = RotOf(Eval(it.ch[3], t, 0), Eval(it.ch[4], t, 0), Eval(it.ch[5], t, 0));
                if (!chn.rotation.empty() && Quat::Dot(chn.rotation.back().value, q) < 0) q = Quat{-q.x, -q.y, -q.z, -q.w};   // the short way round between keys
                chn.rotation.push_back({(float)t, q});
            }
        }
        if (!it.ch[6].empty() || !it.ch[7].empty() || !it.ch[8].empty())
            for (double t : times(6)) chn.scale.push_back({(float)t, Vec3{(float)Eval(it.ch[6], t, 1), (float)Eval(it.ch[7], t, 1), (float)Eval(it.ch[8], t, 1)}});
        outKeys += chn.position.size() + chn.rotation.size() + chn.scale.size();
        if (outKeys > 3 * kMaxKeys) return Fail(err, "more than " + std::to_string(3 * kMaxKeys) + " output keys (budget)");
        if (!chn.position.empty() || !chn.rotation.empty() || !chn.scale.empty()) clip.channels.push_back(std::move(chn));
    }
    if (!clip.channels.empty()) out.clips.push_back(std::move(clip));

    // ---- the object files ----
    int refused = 0, unreadable = 0, noLoader = 0, droppedClips = 0, sceneRefs = 0;
    std::string firstMissing;
    std::map<std::string, std::unique_ptr<Loaded>> loadedFiles;
    std::size_t loadedVerts = 0, loadedTris = 0, outVerts = 0, outTris = 0, placedBones = 0, placedBoneBytes = 0;
    auto triCount = [](const Mesh& m) { return m.triangles.size() / 3; };
    for (std::size_t ii = 0; ii < items.size(); ++ii) {
        const Item& it = items[ii];
        if (it.kind != kObject || it.path.empty()) continue;
        if (!loader || !*loader) { ++noLoader; continue; }
        const std::string lp = detail::txt::Lower(it.path);
        auto ends = [&](const char* e) { const std::size_t k = std::strlen(e); return lp.size() >= k && lp.compare(lp.size() - k, k, e) == 0; };
        if (ends(".lws") || ends(".mot") || ends(".irr")) { ++sceneRefs; ++unreadable; if (firstMissing.empty()) firstMissing = it.path + " (a scene may not load another scene)"; continue; }
        std::string path, whyNot;
        if (!detail::ResolveInsideDir(baseDir, it.path, path, whyNot)) { ++refused; ++unreadable; if (firstMissing.empty()) firstMissing = it.path + " (refused: " + whyNot + ")"; continue; }
        auto f = loadedFiles.find(path);
        if (f == loadedFiles.end()) {
            if (loadedFiles.size() >= kMaxLoadedFiles) return Fail(err, "more than " + std::to_string(kMaxLoadedFiles) + " different object files (budget)");
            auto l = std::make_unique<Loaded>();
            std::string e;
            l->ok = (*loader)(path, l->m, &e) && !l->m.parts.empty();
            if (l->ok && l->m.nodes.empty()) l->m.nodes.push_back(ImportedNode{});
            if (l->ok) {
                for (const ImportedPart& lp2 : l->m.parts) { loadedVerts += lp2.mesh.vertices.size(); loadedTris += triCount(lp2.mesh); }
                if (loadedVerts > kMaxLoadedVerts || loadedTris > kMaxLoadedTris) return Fail(err, "the object files this scene names hold more than " + std::to_string(kMaxLoadedTris) + " triangles or " + std::to_string(kMaxLoadedVerts) + " vertices in all (budget)");
            } else { ++unreadable; if (firstMissing.empty()) firstMissing = it.path + (e.empty() ? std::string() : " (" + e + ")"); l->m = ImportedModel{}; }
            l->matMap.assign(l->m.materials.size(), -1);
            f = loadedFiles.emplace(path, std::move(l)).first;
        } else if (!f->second->ok) { ++unreadable; if (firstMissing.empty()) firstMissing = it.path; }
        Loaded& L = *f->second;
        if (!L.ok) continue;
        droppedClips += (int)L.m.clips.size();
        if (out.nodes.size() + L.m.nodes.size() > kMaxNodes) return Fail(err, "too many nodes (budget)");
        const int base = (int)out.nodes.size();
        const int me = nodeOf[ii];
        for (std::size_t i = 0; i < L.m.nodes.size(); ++i) {
            ImportedNode c = L.m.nodes[i];
            c.parent = c.parent >= 0 ? base + c.parent : me;
            out.nodes.push_back(c);
        }
        for (const ImportedPart& sp : L.m.parts) {
            outVerts += sp.mesh.vertices.size();
            outTris += triCount(sp.mesh);
            if (outVerts > kMaxOutVerts || outTris > kMaxOutTris) return Fail(err, "over the budget of " + std::to_string(kMaxOutTris) + " triangles or " + std::to_string(kMaxOutVerts) + " vertices for the whole scene (an object file placed many times counts once per placement) (budget)");
            if (out.parts.size() >= kMaxNodes) return Fail(err, "too many parts (budget)");
            placedBones += sp.bones.size();
            for (const ImportedBone& pb : sp.bones) placedBoneBytes += pb.name.size() + sizeof(ImportedBone);
            if (placedBones > kMaxPlacedBones || placedBoneBytes > kMaxPlacedBoneBytes) return Fail(err, "the skins of the placed objects hold more than " + std::to_string(kMaxPlacedBones) + " bone entries (or " + std::to_string(kMaxPlacedBoneBytes >> 20) + " MiB) in all (budget)");
            ImportedPart q = sp;
            q.node = base + std::max(0, std::min(sp.node, (int)L.m.nodes.size() - 1));
            if (sp.material >= 0 && (std::size_t)sp.material < L.matMap.size()) {
                int& mm = L.matMap[(std::size_t)sp.material];
                if (mm < 0) { out.materials.push_back(L.m.materials[(std::size_t)sp.material]); mm = (int)out.materials.size() - 1; }
                q.material = mm;
            } else q.material = -1;
            out.parts.push_back(std::move(q));
        }
    }
    if (out.parts.empty() && loader && !isMotion) return Fail(err, "the scene has no geometry (no object file could be read)" + (firstMissing.empty() ? std::string() : "; first that could not be read: " + firstMissing));
    out.backend = "own";

    auto note = [&](const std::string& s) { out.notes.push_back("LWS: " + s); };
    note(std::string(isMotion ? "motion file" : "scene") + " version " + std::to_string(version) + ": positions are metres as written, z mirrored (left-handed to right-handed, like the LWO reader); angles in the envelopes are read as radians and applied as heading (Y), pitch (X), bank (Z) with the bank first; key times are seconds as written (the scene's rate is " + std::to_string((int)fps) + " frames per second). No real LightWave scene was available to check these conventions");
    if (!isMotion) note("object files are looked up inside the scene's own folder only (not in LightWave's content directory), so a path like \"Objects/Cube.lwo\" works when that folder is next to the scene");
    if (version < 3) note("version " + std::to_string(version) + " scene: read with the version 3 layout (unchecked)");
    if (refused) note(std::to_string(refused) + " object file(s) refused: a scene may only name files inside its own folder (no absolute path, no .. out of it, no link out of it)");
    if (sceneRefs) note(std::to_string(sceneRefs) + " object reference(s) to a scene file refused: a scene may not load another scene");
    if (unreadable) note(std::to_string(unreadable) + " object(s) whose file could not be read, first: " + firstMissing);
    if (noLoader) note(std::to_string(noLoader) + " object(s) not loaded: this call had no object loader (they are empty nodes)");
    if (droppedClips) note(std::to_string(droppedClips) + " animation clip(s) inside the object files dropped");
    if (loops) note(std::to_string(loops) + " ParentItem loop(s) (or items that name themselves as parent) cut");
    if (missingParent) note(std::to_string(missingParent) + " item(s) with a ParentItem that names no item were made roots");
    if (badParentLines || strayParents) note(std::to_string(badParentLines + strayParents) + " bad or misplaced ParentItem line(s) ignored");
    if (dupIds) note(std::to_string(dupIds) + " item id(s) used twice: a ParentItem names the first");
    if (nonLinear) note(std::to_string(nonLinear) + " key(s) with a TCB, Hermite, Bezier or stepped span were read as linear between keys");
    if (behaviors) note(std::to_string(behaviors) + " envelope(s) with pre / post behaviours (repeat, oscillate, ...): only the keys are imported");
    if (badKeys) note(std::to_string(badKeys) + " envelope key(s) that are not numbers or have a time out of range skipped");
    if (strayKeys || strayChannels) note(std::to_string(strayKeys) + " envelope(s) outside a motion channel (lighting, camera and other values) and " + std::to_string(strayChannels) + " misplaced Channel line(s) not imported");
    if (unclosedEnv) note(std::to_string(unclosedEnv) + " envelope(s) without a closing brace");
    if (longPaths) note(std::to_string(longPaths) + " object path(s) longer than " + std::to_string(kMaxPathBytes) + " bytes ignored (the item is an empty node)");
    if (longLines) note(std::to_string(longLines) + " line(s) longer than " + std::to_string(kMaxLine) + " bytes skipped");
    note(std::to_string(ignoredLines) + " other line(s) not read (render settings, lighting values, plugins, bones' details, ...); cameras and lights are empty nodes");
    return true;
}

bool ReadLwsBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err, const IrrMeshLoader* loader) {
    out = ImportedModel{};
    const bool ok = ReadLwsImpl(text, baseDir, out, err, loader);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadLwsFile(const std::string& path, ImportedModel& out, std::string* err, const IrrMeshLoader* loader) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, lws::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadLwsBuffer(bytes, dir, out, err, loader);
}

} // namespace okay
