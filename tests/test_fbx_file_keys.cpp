// The animation keys our FBX reader makes at the file's own key times (okay::fbx::BuildOptions::fileKeys, the default;
// docs/content/model_readers.md (FBX part), "Animation keys"), checked against the curves themselves. Standalone (no engine library), so
// it can run under sanitizers. For every animated node and blend shape weight of every fixture in tests/assets/fbx, of
// every real file under $OKAY_MODEL_CORPUS when it is set (docs/content/model_corpus.md), and of any file or directory given on
// the command line:
//   - the truth is the same reader's fixed-rate bake at 240 Hz (every sample is the curves evaluated at that time, with
//     the same code), the old clip is the fixed-rate bake at 30 Hz (the behaviour before 2026-11-16);
//   - the new clip is played the way the engine plays it (translation and scale linear, rotation the normalised linear
//     blend of quaternions with matched signs) at every 240 Hz time and compared with the truth: within the stated
//     tolerances wherever the reader was free to add keys; between keys at most 2 x minGap apart (the reader adds
//     nothing there) no worse than the old 30 Hz clip on that node;
//   - every key time of the node's curves inside the take is a key of the clip (found independently: the test walks the
//     file's own objects and connections, stack -> layer -> curve node -> model);
//   - the key counts against the 30 Hz clip are printed per file and must not grow in total.
// Written for this engine; no third-party code.
#include "test_framework.hpp"
#include "okay/Render/FbxReader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace okay;

namespace {

const double kPi = 3.14159265358979323846;

std::vector<std::uint8_t> ReadAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// ---- playback, as the engine does it ----------------------------------------------------------------------------
struct Played { double t[3], q[4], s[3]; double segLen; };
template <class K> std::size_t SegmentAt(const std::vector<K>& keys, double t) {   // last key at or before t
    std::size_t lo = 0, hi = keys.size();
    while (hi - lo > 1) { const std::size_t mid = (lo + hi) / 2; if (keys[mid].time <= t) lo = mid; else hi = mid; }
    return lo;
}
Played Play(const fbx::NodeAnim& na, double t) {
    Played p{};
    const std::size_t i = SegmentAt(na.translation, t);
    const std::size_t j = i + 1 < na.translation.size() ? i + 1 : i;
    const double t0 = na.translation[i].time, t1 = na.translation[j].time;
    const double u = (j == i || t <= t0) ? 0.0 : std::min(1.0, (t - t0) / (t1 - t0));
    p.segLen = j == i ? 0.0 : t1 - t0;
    for (int k = 0; k < 3; ++k) {
        p.t[k] = na.translation[i].v[k] + (na.translation[j].v[k] - na.translation[i].v[k]) * u;
        p.s[k] = na.scale[i].v[k] + (na.scale[j].v[k] - na.scale[i].v[k]) * u;
    }
    const double* a = na.rotation[i].v; const double* b = na.rotation[j].v;
    const double sg = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0 ? -1.0 : 1.0;
    double n = 0;
    for (int k = 0; k < 4; ++k) { p.q[k] = a[k] + (sg * b[k] - a[k]) * u; n += p.q[k] * p.q[k]; }
    n = std::sqrt(n);
    if (n > 0) for (double& c : p.q) c /= n;
    return p;
}
double PlayBlend(const std::vector<fbx::Key3>& keys, double t, double* segLen) {
    const std::size_t i = SegmentAt(keys, t);
    const std::size_t j = i + 1 < keys.size() ? i + 1 : i;
    const double t0 = keys[i].time, t1 = keys[j].time;
    const double u = (j == i || t <= t0) ? 0.0 : std::min(1.0, (t - t0) / (t1 - t0));
    *segLen = j == i ? 0.0 : t1 - t0;
    return keys[i].v[0] + (keys[j].v[0] - keys[i].v[0]) * u;
}
// The gap the reader leaves between the key just before a jump and the jump (its BeforeJump): inside it the truth has
// already jumped while playback ramps, so those samples are not compared.
double JumpGap(double clipTime) { return std::max(2e-4, std::fabs(clipTime) * 7.62939453125e-6); }
double AngleDeg(const double a[4], const double b[4]) {
    double d = 0, na = 0, nb = 0;
    for (int k = 0; k < 4; ++k) { d += a[k] * b[k]; na += a[k] * a[k]; nb += b[k] * b[k]; }
    if (!(na > 0 && nb > 0)) return 0;
    return 2.0 * std::acos(std::min(1.0, std::fabs(d) / std::sqrt(na * nb))) * 180.0 / kPi;
}

// ---- the file's own key times, from the element tree --------------------------------------------------------------
std::int64_t IdOf(const fbx::Prop& p) { return (p.type == 'F' || p.type == 'D') ? (std::int64_t)p.d : p.i; }
std::string Clean(std::string_view raw) {   // "Name\0\1Class" (binary) or "Class::Name" (ASCII) -> "Name"
    const std::size_t z = raw.find(std::string_view("\0\1", 2));
    if (z != std::string_view::npos) return std::string(raw.substr(0, z));
    const std::size_t c = raw.find("::");
    if (c != std::string_view::npos) return std::string(raw.substr(c + 2));
    return std::string(raw);
}
struct FileTimes {
    struct Stack { std::string name; std::map<std::string, std::vector<double>> timesOfModel; };
    std::vector<Stack> stacks;   // in file order
};
bool AuthoredTimes(const fbx::Document& doc, FileTimes& out) {
    const fbx::Elem* objs = doc.root.Find("Objects");
    const fbx::Elem* conns = doc.root.Find("Connections");
    if (!objs || !conns) return false;
    struct Obj { std::string cls, name; const fbx::Elem* e; };
    std::map<std::int64_t, Obj> byId;
    std::vector<std::int64_t> stackIds;
    for (const fbx::Elem& e : objs->children) {
        if (e.props.size() < 2) continue;
        const std::int64_t id = IdOf(e.props[0]);
        byId[id] = {std::string(e.name), Clean(e.props[1].s()), &e};
        if (e.name == "AnimationStack") stackIds.push_back(id);
    }
    struct Conn { std::int64_t child, parent; std::string prop; bool op; };
    std::vector<Conn> cs;
    for (const fbx::Elem& c : conns->children) {
        if (c.name != "C" || c.props.size() < 3) continue;
        Conn k{IdOf(c.props[1]), IdOf(c.props[2]), c.props.size() > 3 ? std::string(c.props[3].s()) : std::string(), c.props[0].s() == "OP"};
        cs.push_back(k);
    }
    auto cls = [&](std::int64_t id) -> const Obj* { auto it = byId.find(id); return it == byId.end() ? nullptr : &it->second; };
    for (std::int64_t sid : stackIds) {
        FileTimes::Stack st; st.name = byId[sid].name;
        for (const Conn& lc : cs) {
            const Obj* layer = cls(lc.child);
            if (lc.parent != sid || lc.op || !layer || layer->cls != "AnimationLayer") continue;
            for (const Conn& nc : cs) {
                const Obj* cn = cls(nc.child);
                if (nc.parent != lc.child || nc.op || !cn || cn->cls != "AnimationCurveNode") continue;
                const Conn* drives = nullptr;   // the first property connection from the curve node (as the reader takes it)
                for (const Conn& d : cs) if (d.child == nc.child && d.op) { drives = &d; break; }
                if (!drives || (drives->prop != "Lcl Translation" && drives->prop != "Lcl Rotation" && drives->prop != "Lcl Scaling")) continue;
                const Obj* model = cls(drives->parent);
                if (!model || model->cls != "Model") continue;
                std::vector<double>& out_ = st.timesOfModel[model->name];
                for (const Conn& cc : cs) {
                    const Obj* curve = cls(cc.child);
                    if (cc.parent != nc.child || !cc.op || !curve || curve->cls != "AnimationCurve") continue;
                    if (cc.prop != "d|X" && cc.prop != "d|Y" && cc.prop != "d|Z") continue;
                    const fbx::Elem* kt = curve->e->Find("KeyTime");
                    const fbx::Elem* kv = curve->e->Find("KeyValueFloat");
                    if (!kv) kv = curve->e->Find("KeyValueDouble");
                    if (!kt || !kv || kt->props.empty() || kv->props.empty() || kt->props[0].type != 'a' || kv->props[0].type != 'a') continue;
                    const fbx::Array& ta = kt->props[0].arr; const fbx::Array& va = kv->props[0].arr;
                    double prev = -1e300;
                    for (std::size_t k = 0; k < std::min(ta.size(), va.size()); ++k) {
                        const double t = ta[k] / 46186158000.0;
                        if (!std::isfinite(t) || !std::isfinite(va[k]) || t < prev) continue;   // the keys the reader keeps
                        prev = t; out_.push_back(t);
                    }
                }
            }
        }
        out.stacks.push_back(std::move(st));
    }
    return true;
}

// ---- totals ---------------------------------------------------------------------------------------------------------
struct Totals {
    std::size_t files = 0, animated = 0, nodes = 0, keysNew = 0, keysOld = 0, timesChecked = 0, timesMissing = 0, samples = 0, denseSamples = 0;
    double worstRatio = 0;         // largest error / tolerance where the tolerance applies
    double worstT = 0, worstR = 0, worstS = 0, worstP = 0;   // largest errors there (m, deg, scale, percent)
    double worstOldR = 0;          // the 30 Hz clip's largest rotation error, for comparison
    double worstDenseExcess = 0;   // dense parts: largest (new error / max(tolerance, old clip's error on the node))
    std::size_t filesMoreKeys = 0;
};

void CheckFile(const std::string& path, Totals& tot, bool verbose) {
    std::string err;
    const std::vector<std::uint8_t> bytes = ReadAll(path);
    fbx::Scene now, truth, old;
    fbx::BuildOptions on; on.fileKeys = true;
    // (the reader, then the import step that makes the clips' keys: fbx::BuildClips)
    if (!fbx::LoadMemory(bytes.data(), bytes.size(), now, &err, on) || !fbx::BuildClips(now, &err)) return;   // files our reader rejects are another test's
    ++tot.files;
    if (now.clips.empty()) return;
    fbx::BuildOptions ref; ref.fileKeys = false; ref.bakeRate = 240.0;
    ref.limits.maxBakedSamples = std::size_t(1) << 24; ref.limits.maxTotalBakedSamples = std::size_t(1) << 28;
    fbx::BuildOptions o30; o30.fileKeys = false;
    CHECK(fbx::LoadMemory(bytes.data(), bytes.size(), truth, &err, ref) && fbx::BuildClips(truth, &err));
    CHECK(fbx::LoadMemory(bytes.data(), bytes.size(), old, &err, o30) && fbx::BuildClips(old, &err));
    CHECK(truth.clips.size() == now.clips.size() && old.clips.size() == now.clips.size());
    if (truth.clips.size() != now.clips.size() || old.clips.size() != now.clips.size()) return;
    ++tot.animated;
    const fbx::KeyTolerance tol;
    // the file's own key times
    fbx::Document doc;
    FileTimes ft;
    const bool haveTimes = fbx::Parse(bytes.data(), bytes.size(), doc, &err) && AuthoredTimes(doc, ft);
    CHECK(haveTimes);
    std::map<std::string, int> nameCount;
    for (const fbx::SceneNode& n : now.nodes) ++nameCount[n.name];
    std::vector<char> stackUsed(ft.stacks.size(), 0);

    std::size_t keysNew = 0, keysOld = 0;
    double fileRatio = 0, fileOldR = 0, fileR = 0;
    for (std::size_t c = 0; c < now.clips.size(); ++c) {
        const fbx::Clip& A = now.clips[c]; const fbx::Clip& R = truth.clips[c]; const fbx::Clip& O = old.clips[c];
        CHECK(A.name == R.name && A.duration == R.duration && A.start == R.start);
        CHECK(A.nodes.size() == R.nodes.size() && A.nodes.size() == O.nodes.size() && A.blends.size() == R.blends.size());
        if (A.nodes.size() != R.nodes.size() || A.nodes.size() != O.nodes.size() || A.blends.size() != R.blends.size()) continue;
        const FileTimes::Stack* stack = nullptr;
        for (std::size_t s = 0; s < ft.stacks.size(); ++s) if (!stackUsed[s] && ft.stacks[s].name == A.name) { stackUsed[s] = 1; stack = &ft.stacks[s]; break; }
        for (std::size_t ni = 0; ni < A.nodes.size(); ++ni) {
            const fbx::NodeAnim& a = A.nodes[ni]; const fbx::NodeAnim& r = R.nodes[ni]; const fbx::NodeAnim& o = O.nodes[ni];
            CHECK(a.node == r.node && a.node == o.node);
            CHECK(!a.translation.empty() && a.translation.size() == a.rotation.size() && a.translation.size() == a.scale.size());
            if (a.translation.empty()) continue;
            ++tot.nodes;
            keysNew += a.translation.size(); keysOld += o.translation.size();
            for (std::size_t k = 1; k < a.translation.size(); ++k) CHECK(a.translation[k].time > a.translation[k - 1].time);
            CHECK(a.translation.front().time == 0.0 && (A.duration <= 0 || a.translation.back().time == A.duration));
            // tolerances as the reader states them (from the node's keys)
            double maxT = 0, maxS = 1;
            for (std::size_t k = 0; k < a.translation.size(); ++k) {
                const double* t = a.translation[k].v;
                maxT = std::max(maxT, std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]));
                for (int m = 0; m < 3; ++m) maxS = std::max(maxS, std::fabs(a.scale[k].v[m]));
            }
            const double tolT = tol.positionAbs + tol.positionRel * maxT, tolS = tol.scaleRel * maxS, tolR = tol.rotationDeg;
            // the 30 Hz clip's error on this node, and the new clip's in the parts where it only has the file's keys
            double oldWorst = 0, denseWorst = 0;
            for (std::size_t k = 0; k < r.translation.size(); ++k) {
                const double t = r.translation[k].time;
                const Played pn = Play(a, t), po = Play(o, t);
                double eT = 0, oT = 0, eS = 0, oS = 0;
                for (int m = 0; m < 3; ++m) {
                    eT += (pn.t[m] - r.translation[k].v[m]) * (pn.t[m] - r.translation[k].v[m]);
                    oT += (po.t[m] - r.translation[k].v[m]) * (po.t[m] - r.translation[k].v[m]);
                    eS = std::max(eS, std::fabs(pn.s[m] - r.scale[k].v[m]));
                    oS = std::max(oS, std::fabs(po.s[m] - r.scale[k].v[m]));
                }
                eT = std::sqrt(eT); oT = std::sqrt(oT);
                const double eR = AngleDeg(pn.q, r.rotation[k].v), oR = AngleDeg(po.q, r.rotation[k].v);
                const double ratio = std::max(eT / tolT, std::max(eR / tolR, eS / tolS));
                const double oldRatio = std::max(oT / tolT, std::max(oR / tolR, oS / tolS));
                // inside the short gap before a jump the truth may already have jumped: not compared
                const std::size_t si = SegmentAt(a.translation, t);
                const bool atJump = pn.segLen > 0 && pn.segLen <= JumpGap(t) * 1.01 && t > a.translation[si].time;
                if (atJump) continue;
                oldWorst = std::max(oldWorst, oldRatio);
                fileOldR = std::max(fileOldR, oR); tot.worstOldR = std::max(tot.worstOldR, oR);
                ++tot.samples;
                if (pn.segLen > 0 && pn.segLen <= 2 * tol.minGap * (1 + 1e-6)) { ++tot.denseSamples; denseWorst = std::max(denseWorst, ratio); continue; }
                if (!(ratio <= 1.0 + 1e-9))
                    std::fprintf(stderr, "  %s clip %zu node %d t=%.6f: error T %.3g (tol %.3g) R %.4g deg S %.3g (tol %.3g)\n",
                                 fs::path(path).filename().string().c_str(), c, a.node, t, eT, tolT, eR, eS, tolS);
                CHECK(ratio <= 1.0 + 1e-9);
                fileRatio = std::max(fileRatio, ratio); fileR = std::max(fileR, eR);
                tot.worstT = std::max(tot.worstT, eT); tot.worstR = std::max(tot.worstR, eR); tot.worstS = std::max(tot.worstS, eS);
            }
            if (denseWorst > 0) {
                const double excess = denseWorst / std::max(1.0, oldWorst);
                tot.worstDenseExcess = std::max(tot.worstDenseExcess, excess);
                if (!(excess <= 1.05)) std::fprintf(stderr, "  %s node %d: dense parts %.3g x tolerance, the 30 Hz clip %.3g x\n", fs::path(path).filename().string().c_str(), a.node, denseWorst, oldWorst);
                CHECK(excess <= 1.05);
            }
            // every key time of the node's curves in the take is a key
            if (stack && a.node >= 0 && (std::size_t)a.node < now.nodes.size() && nameCount[now.nodes[(std::size_t)a.node].name] == 1) {
                auto it = stack->timesOfModel.find(now.nodes[(std::size_t)a.node].name);
                if (it != stack->timesOfModel.end()) for (double ft_ : it->second) {
                    if (ft_ < A.start || ft_ > A.start + A.duration) continue;
                    const double rel = ft_ - A.start;
                    bool found = false;
                    const std::size_t si = SegmentAt(a.translation, rel);
                    for (std::size_t k = si > 0 ? si - 1 : 0; k < std::min(a.translation.size(), si + 2); ++k) if (a.translation[k].time == rel) found = true;
                    ++tot.timesChecked;
                    if (!found) { ++tot.timesMissing; std::fprintf(stderr, "  %s node %d: file key time %.9f not a key\n", fs::path(path).filename().string().c_str(), a.node, rel); }
                }
            }
        }
        for (std::size_t bi = 0; bi < A.blends.size(); ++bi) {
            const fbx::BlendAnim& a = A.blends[bi]; const fbx::BlendAnim& r = R.blends[bi];
            CHECK(!a.percent.empty());
            if (a.percent.empty()) continue;
            keysNew += a.percent.size(); keysOld += O.blends[bi].percent.size();
            for (const fbx::Key3& k : r.percent) {
                double seg = 0;
                const double v = PlayBlend(a.percent, k.time, &seg);
                if (seg > 0 && seg <= JumpGap(k.time) * 1.01) continue;
                ++tot.samples;
                if (seg > 0 && seg <= 2 * tol.minGap * (1 + 1e-6)) { ++tot.denseSamples; continue; }
                const double e = std::fabs(v - k.v[0]);
                tot.worstP = std::max(tot.worstP, e);
                CHECK(e <= tol.percent * (1 + 1e-9));
            }
        }
    }
    tot.keysNew += keysNew; tot.keysOld += keysOld;
    tot.worstRatio = std::max(tot.worstRatio, fileRatio);
    if (keysNew > keysOld) ++tot.filesMoreKeys;
    if (verbose)
        std::printf("  %-48s keys %7zu (30 Hz: %7zu)  worst error/tolerance %.3f  worst rotation %.4f deg (30 Hz clip: %.4f deg)\n",
                    fs::path(path).filename().string().c_str(), keysNew, keysOld, fileRatio, fileR, fileOldR);
}

void Collect(const fs::path& p, std::vector<std::string>& out) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        for (auto it = fs::recursive_directory_iterator(p, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            std::string ext = it->path().extension().string();
            for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
            if (it->is_regular_file(ec) && ext == ".fbx") out.push_back(it->path().string());
        }
    } else if (fs::is_regular_file(p, ec)) out.push_back(p.string());
}

} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("fbx file keys: every animated node against its curves, the 30 Hz clip and the file's key times");
    std::vector<std::string> files;
    Collect(fs::path(OKAY_SOURCE_DIR) / "tests" / "assets" / "fbx", files);
    if (const char* c = std::getenv("OKAY_MODEL_CORPUS")) Collect(c, files);
    for (int i = 1; i < argc; ++i) Collect(argv[i], files);
    std::sort(files.begin(), files.end());
    Totals tot;
    for (const std::string& f : files) CheckFile(f, tot, true);
    const fbx::KeyTolerance tol;
    std::printf("%zu files read, %zu animated, %zu animated nodes; keys %zu (30 Hz clips: %zu); %zu files with more keys than at 30 Hz\n",
                tot.files, tot.animated, tot.nodes, tot.keysNew, tot.keysOld, tot.filesMoreKeys);
    std::printf("%zu samples at 240 Hz (%zu between keys at most %.4f s apart); worst error/tolerance %.3f: translation %.3g m, rotation %.4f deg (30 Hz clip: %.4f deg), scale %.3g, blend %.3g %%\n",
                tot.samples, tot.denseSamples, 2 * tol.minGap, tot.worstRatio, tot.worstT, tot.worstR, tot.worstOldR, tot.worstS, tot.worstP);
    std::printf("between close file keys: worst error %.3f x max(tolerance, the 30 Hz clip's error); file key times checked %zu, missing %zu\n",
                tot.worstDenseExcess, tot.timesChecked, tot.timesMissing);
    CHECK(tot.animated > 0);
    CHECK(tot.timesMissing == 0 && tot.timesChecked > 0);
    CHECK(tot.keysNew <= tot.keysOld);
    TEST_MAIN_RESULT();
}
