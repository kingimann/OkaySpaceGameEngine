// FBX animation: Builder::Animation (reads the curves, plans the clips) and the Animator that bakes the clip keys (BuildClips),
// a pure move out of FbxScene.cpp.
#include "FbxSceneInternal.hpp"
#include "okay/Core/SafeCast.hpp"
#include "okay/Core/Base64.hpp"
#include "okay/Render/FbxReader.hpp"
#include "okay/Core/ThirdParty.hpp"
#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace okay {
namespace fbx {
namespace scene_build {
// ---------------------------------------------------------------------------
// clip keys (the import step: BuildClips). The reader (Builder::Animation) reads the curves and plans the clips; the
// keys are made here, from what the Animator kept, when the import asks for them.
// ---------------------------------------------------------------------------
struct Animator : ClipSource {
    using XProps = Builder::XProps; using FixedRot = Builder::FixedRot;
    static FixedRot FixedRotations(const XProps& x) { return Builder::FixedRotations(x); }
    static Mat LocalMatrix(const XProps& x, const FixedRot& f, const Mat& R) { return Builder::LocalMatrix(x, f, R); }
    static Mat LocalSimple(const XProps& x, const FixedRot& f, const Mat& R) { return Builder::LocalSimple(x, f, R); }
    BuildOptions opt; Conv conv; std::string err;
    std::vector<XProps> xp;          // per node: its transform properties
    std::vector<char> isRoot;        // per node: hangs off the scene root
    std::vector<CurveNode> cns;      // (curve pointers into curveStore: neither vector changes after the reader)
    bool Fail(const std::string& m) { if (err.empty()) err = m; return false; }
    struct Layr { Id id; std::string_view name; double weight = 100; int blendMode = 0; };
    std::vector<Curve> curveStore;
    // The curves' arrays (Curve's runs point into these; they do not change after the reader)
    std::vector<double> poolT, poolV, poolSlope; std::vector<std::uint32_t> poolSet; std::vector<int> poolFlags;
    /// A curve node driving one node's T, R or S in one layer; `hint` and `hintP` remember the key segments of the
    /// last evaluation (two, so the probes between keys do not move the one the keys use).
    struct NodeCN { int layer; int which; const CurveNode* cn; std::size_t hint[3]; std::size_t hintP[3]; };   // which: 0 T, 1 R, 2 S

    // ---- keys at the file's own key times ---------------------------------------------------------------------   // lint-allow: secret
    std::size_t fileKeyEvals = 0;   // evaluations so far (each counted once per curve node it reads), whole file

    /// One channel (0 T, 1 R, 2 S) at absolute file time `at`, the layers combined as the fixed-rate bake does.
    static void EvalChannel(std::vector<NodeCN>& list, const std::vector<Layr>& layers, int which, const double base[3], double at, bool probe, double out[3]) {
        for (int k = 0; k < 3; ++k) out[k] = base[k];
        bool first = true;
        for (NodeCN& n : list) {
            if (n.which != which) continue;
            double lv[3];
            for (int k = 0; k < 3; ++k) lv[k] = n.cn->curve[k] ? n.cn->curve[k]->Eval(at, probe ? n.hintP[k] : n.hint[k]) : n.cn->def[k];
            const Layr& ly = layers[(std::size_t)n.layer];
            const double w = std::max(0.0, ly.weight) * 0.01;
            if (first) { for (int k = 0; k < 3; ++k) out[k] = lv[k]; first = false; }
            else if (ly.blendMode == 0) for (int k = 0; k < 3; ++k) out[k] += lv[k] * w;
            else for (int k = 0; k < 3; ++k) out[k] += (lv[k] - out[k]) * std::min(1.0, w);
        }
    }
    /// A node's pose at one time: the curves' raw values (rt, rr, rs: file units and Euler degrees) and the local
    /// translation, rotation and scale they give (what the clip stores).
    struct KeyPose {
        double rt[3], rr[3], rs[3]; double t[3], q[4], s[3];
        double qr[4]; bool hasQr = false;   // EulerQuat(rr), made when first needed
        const double* RawQuat(int order) { if (!hasQr) { EulerQuat(rr, order, qr); hasQr = true; } return qr; }
    };
    struct NodeBake {
        std::vector<NodeCN>* list; const std::vector<Layr>* layers;
        XProps x; FixedRot fixed; bool isRoot;
        double t0, tolT, tolS, tolR;
        std::size_t cost;   // curve nodes read per evaluation
        // When no channel has more than one curve node (one layer, the usual case), the one per channel (or none):
        // EvalChannel then returns that curve node's values, or the base values without one, which is what the direct
        // reading in RawPose does without walking the list three times.
        bool direct = false; NodeCN* only[3] = {nullptr, nullptr, nullptr};
    };
    void RawPose(NodeBake& nb, double at, bool probe, KeyPose& p) {
        if (nb.direct) {
            const double* base[3] = {nb.x.t, nb.x.r, nb.x.s};
            double* out[3] = {p.rt, p.rr, p.rs};
            for (int w = 0; w < 3; ++w) {
                NodeCN* n = nb.only[w];
                if (!n) { for (int k = 0; k < 3; ++k) out[w][k] = base[w][k]; continue; }
                for (int k = 0; k < 3; ++k) out[w][k] = n->cn->curve[k] ? n->cn->curve[k]->Eval(at, probe ? n->hintP[k] : n->hint[k]) : n->cn->def[k];
            }
        } else {
            EvalChannel(*nb.list, *nb.layers, 0, nb.x.t, at, probe, p.rt);
            EvalChannel(*nb.list, *nb.layers, 1, nb.x.r, at, probe, p.rr);
            EvalChannel(*nb.list, *nb.layers, 2, nb.x.s, at, probe, p.rs);
        }
        fileKeyEvals += nb.cost;
    }
    /// The same matrix chain and decomposition as the fixed-rate bake, so equal curve values give equal keys.
    void LocalPose(NodeBake& nb, KeyPose& p) {
        XProps x = nb.x;
        std::memcpy(x.t, p.rt, sizeof(x.t)); std::memcpy(x.r, p.rr, sizeof(x.r)); std::memcpy(x.s, p.rs, sizeof(x.s));
        const Mat R = Euler(x.r, x.order);
        const bool simple = nb.fixed.simple && Small3(x.t) && Small3(x.r) && Small3(x.s);
        const Mat local = conv.Local(simple ? LocalSimple(x, nb.fixed, R) : LocalMatrix(x, nb.fixed, R), nb.isRoot);
        Decompose(local, p.t, p.q, p.s);
    }
    // The keys of the node being baked go here first (kept from node to node, so they do not grow again for each one),
    // then into the node's arrays at their final size.
    std::vector<Key3> emitT, emitS; std::vector<Key4> emitQ;
    void Emit(NodeBake& nb, double at, const KeyPose& p) {
        const double rel = at - nb.t0;
        Key3 kt; kt.time = rel; for (int k = 0; k < 3; ++k) kt.v[k] = p.t[k];
        Key4 kq; kq.time = rel; for (int k = 0; k < 4; ++k) kq.v[k] = p.q[k];
        Key3 ks; ks.time = rel; for (int k = 0; k < 3; ++k) ks.v[k] = p.s[k];
        emitT.push_back(kt); emitQ.push_back(kq); emitS.push_back(ks);
    }
    /// How far linear playback from a to b is from the tolerances, judged from the poses at 1/3 and 2/3: `euler` is the
    /// largest move of an Euler angle over maxEulerStep, `err` the largest error over what is allowed. The segment is
    /// good when both are at most 1 (NaN counts as good: a broken curve is not chased down to minGap).
    void SegmentNeed(NodeBake& nb, KeyPose& a, KeyPose& p1, KeyPose& p2, KeyPose& b, bool& haveLocal, double& euler, double& err) {
        const KeyTolerance& tol = opt.keyTolerance;
        euler = 0; err = 0;
        // Euler angles: no angle moves more than maxEulerStep inside the segment (exact: each is one cubic piece here).
        // This also keeps every step under half a turn, so the quaternion blend cannot take the other way round.
        bool rotMoves = false;
        for (int k = 0; k < 3; ++k) {
            double lo, hi; CubicMinMax(a.rr[k], p1.rr[k], p2.rr[k], b.rr[k], lo, hi);
            euler = std::max(euler, (hi - lo) / tol.maxEulerStep);
            rotMoves |= hi != lo;
        }
        // When an angle turns too far, Subdivide looks at nothing else (it splits by `euler` alone and never reads
        // `err`), so the error is not worked out. (The poses' quaternions and local poses it would have made are made
        // when they are needed, with the same values.)
        if (euler > 1.0) return;
        // Without offsets and pivots the local translation is the curves' translation times the unit scale (turned by
        // the axis matrix at a root) and the local scale is the curves' scale up to fixed signs, so the error of a
        // straight line is the exact deviation of a cubic. The scale needs the same sign at both ends and no zero in
        // between. Otherwise (and for the rotation) the poses at 1/3 and 2/3 are compared with half the tolerance.
        bool raw = nb.fixed.simple;
        for (int k = 0; k < 3 && raw; ++k) {
            double lo, hi; CubicMinMax(a.rs[k], p1.rs[k], p2.rs[k], b.rs[k], lo, hi);
            raw = lo > 1e-9 || hi < -1e-9;
        }
        if (raw) {
            double e2 = 0, es = 0;
            for (int k = 0; k < 3; ++k) {
                const double dt = CubicDeviation(p1.rt[k] - (a.rt[k] + (b.rt[k] - a.rt[k]) / 3.0), p2.rt[k] - (a.rt[k] + (b.rt[k] - a.rt[k]) * 2.0 / 3.0));
                e2 += dt * dt;
                es = std::max(es, CubicDeviation(p1.rs[k] - (a.rs[k] + (b.rs[k] - a.rs[k]) / 3.0), p2.rs[k] - (a.rs[k] + (b.rs[k] - a.rs[k]) * 2.0 / 3.0)));
            }
            err = std::max(std::sqrt(e2) * std::fabs(conv.s) / nb.tolT, es / nb.tolS);
            if (!rotMoves) return;
            const double* qa = a.RawQuat(nb.x.order); const double* qb = b.RawQuat(nb.x.order);
            const double* q1 = p1.RawQuat(nb.x.order); const double* q2 = p2.RawQuat(nb.x.order);
            // the fixed pre/post rotations and the axis matrix turn every quaternion the same way: angles do not change
            err = std::max(err, std::max(NlerpErrorDeg(qa, qb, 1.0 / 3.0, q1), NlerpErrorDeg(qa, qb, 2.0 / 3.0, q2)) / (0.5 * nb.tolR));
            return;
        }
        LocalPose(nb, p1); LocalPose(nb, p2); haveLocal = true;
        for (int i = 0; i < 2; ++i) {
            const KeyPose& p = i ? p2 : p1;
            const double u = i ? 2.0 / 3.0 : 1.0 / 3.0;
            double e2 = 0, es = 0;
            for (int k = 0; k < 3; ++k) {
                const double d = p.t[k] - (a.t[k] + (b.t[k] - a.t[k]) * u); e2 += d * d;
                es = std::max(es, std::fabs(p.s[k] - (a.s[k] + (b.s[k] - a.s[k]) * u)));
            }
            err = std::max(err, std::max(std::sqrt(e2) / (0.5 * nb.tolT), es / (0.5 * nb.tolS)));
            err = std::max(err, NlerpErrorDeg(a.q, b.q, u, p.q) / (0.5 * nb.tolR));
        }
    }
    /// Keys strictly between a (at ta) and b (at tb), emitted in time order. None if the segment is within the
    /// tolerances. Else it is cut into n equal parts, n from how far it is off (an Euler move shrinks in proportion to
    /// the distance between keys, an error at least with its square), never into parts of minGap or less, and each
    /// part is checked the same way. Three parts reuse the poses at 1/3 and 2/3.
    bool Subdivide(NodeBake& nb, double ta, KeyPose& a, double tb, KeyPose& b, int depth) {
        const double len = tb - ta, g = opt.keyTolerance.minGap * (1.0 + 1e-6);   // (the margin: keys a frame apart at
        if (!(len / 2.0 > g) || depth > 40) return true;                          //  30 or 60 Hz are never split by rounding)
        if (fileKeyEvals > 4 * opt.limits.maxTotalBakedSamples) return Fail("animation bake budget exceeded");
        const double t1 = ta + len / 3.0, t2 = ta + len * (2.0 / 3.0);
        KeyPose p1, p2;
        RawPose(nb, t1, true, p1); RawPose(nb, t2, true, p2);
        bool haveLocal = false;
        double euler, err;
        SegmentNeed(nb, a, p1, p2, b, haveLocal, euler, err);
        if (!(euler > 1.0 || err > 1.0)) return true;
        // (while an angle still turns too far, the error at 1/3 and 2/3 says little: the turn alone decides then)
        double want = std::max(2.0, euler > 1.0 ? std::ceil(euler * 1.05) : std::ceil(std::sqrt(err) * 1.1));
        want = std::min(want, std::min(64.0, std::floor(len / g)));   // parts longer than minGap (at least 2 here)
        const int n = (int)std::max(2.0, want);
        if (n == 3) {
            if (!haveLocal) { LocalPose(nb, p1); LocalPose(nb, p2); }
            if (!Subdivide(nb, ta, a, t1, p1, depth + 1)) return false;
            Emit(nb, t1, p1);
            if (!Subdivide(nb, t1, p1, t2, p2, depth + 1)) return false;
            Emit(nb, t2, p2);
            return Subdivide(nb, t2, p2, tb, b, depth + 1);
        }
        KeyPose prev = a; double tp = ta;
        for (int i = 1; i < n; ++i) {
            const double ti = ta + len * ((double)i / (double)n);
            KeyPose pi; RawPose(nb, ti, true, pi); LocalPose(nb, pi);
            if (!Subdivide(nb, tp, prev, ti, pi, depth + 1)) return false;
            Emit(nb, ti, pi);
            prev = pi; tp = ti;
        }
        return Subdivide(nb, tp, prev, tb, b, depth + 1);
    }
    /// The key times of `cs` in (t0, t1], merged without repeats, each marked when a curve jumps there (a step key
    /// before it, or two keys at the same time with different values); t0 first and t1 last.
    static void KeyTimes(const std::vector<const Curve*>& cs, double t0, double t1, std::vector<double>& times, std::vector<char>& jump) {
        times.assign(1, t0); jump.assign(1, 0);
        if (!(t1 > t0)) return;
        std::vector<std::size_t> pos(cs.size());
        for (std::size_t i = 0; i < cs.size(); ++i) pos[i] = (std::size_t)(std::upper_bound(cs[i]->t.begin(), cs[i]->t.end(), t0) - cs[i]->t.begin());
        for (;;) {
            double m = std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < cs.size(); ++i) if (pos[i] < cs[i]->t.size() && cs[i]->t[pos[i]] < m) m = cs[i]->t[pos[i]];
            if (!(m <= t1)) break;
            char j = 0;
            for (std::size_t i = 0; i < cs.size(); ++i) {
                const Curve& c = *cs[i];
                while (pos[i] < c.t.size() && c.t[pos[i]] == m) {
                    const std::size_t k = pos[i]++;
                    if (k >= 1 && c.v[k - 1] != c.v[k] && (c.t[k - 1] == c.t[k] || (c.FlagsOf(k - 1) & 0x2))) j = 1;
                }
            }
            times.push_back(m); jump.push_back(j);
        }
        if (times.back() != t1) { times.push_back(t1); jump.push_back(0); }
    }
    /// The time of the key just before a jump at `at`: 2e-4 s earlier, or 2^-17 of the clip time when that is more (64
    /// float steps). The engine's clips merge keys less than 1e-4 s apart (AnimationClip::AddKeys), so the pair must be
    /// further apart than that, also after the times are rounded to float.
    static double BeforeJump(double at, double t0) { return at - JumpGap(at - t0); }
    static double JumpGap(double clipTime) { return std::max(2e-4, std::fabs(clipTime) * 7.62939453125e-6); }

    bool BakeNodeFileKeys(std::vector<NodeCN>& list, const std::vector<Layr>& layers, int node, double t0, double t1, NodeAnim& na) {
        std::vector<const Curve*> cs;
        for (const NodeCN& n : list) for (int k = 0; k < 3; ++k) {
            const Curve* c = n.cn->curve[k];
            if (c && std::find(cs.begin(), cs.end(), c) == cs.end()) cs.push_back(c);
        }
        std::vector<double> times; std::vector<char> jump;
        KeyTimes(cs, t0, t1, times, jump);
        NodeBake nb;
        nb.list = &list; nb.layers = &layers; nb.x = xp[(std::size_t)node]; nb.fixed = FixedRotations(nb.x);
        nb.isRoot = isRoot[(std::size_t)node] != 0; nb.t0 = t0; nb.cost = std::max<std::size_t>(1, list.size());
        nb.direct = true;
        for (NodeCN& n : list) {
            if (n.which < 0 || n.which > 2 || nb.only[n.which]) { nb.direct = false; break; }
            nb.only[n.which] = &n;
        }
        if ((double)times.size() * (double)nb.cost > (double)(4 * opt.limits.maxTotalBakedSamples - std::min(fileKeyEvals, 4 * opt.limits.maxTotalBakedSamples)))
            return Fail("animation bake budget exceeded");
        std::vector<KeyPose> poses(times.size());
        double maxT = 0, maxS = 1;
        for (std::size_t i = 0; i < times.size(); ++i) {
            RawPose(nb, times[i], false, poses[i]); LocalPose(nb, poses[i]);
            const KeyPose& p = poses[i];
            const double n = std::sqrt(p.t[0] * p.t[0] + p.t[1] * p.t[1] + p.t[2] * p.t[2]);
            if (n > maxT) maxT = n;   // (NaN never wins)
            for (int k = 0; k < 3; ++k) if (std::fabs(p.s[k]) > maxS) maxS = std::fabs(p.s[k]);
        }
        const KeyTolerance& tol = opt.keyTolerance;
        nb.tolT = tol.positionAbs + tol.positionRel * maxT; nb.tolS = tol.scaleRel * maxS; nb.tolR = tol.rotationDeg;
        emitT.clear(); emitQ.clear(); emitS.clear();
        Emit(nb, times[0], poses[0]);
        double ta = times[0]; KeyPose a = poses[0];
        for (std::size_t i = 1; i < times.size(); ++i) {
            if (jump[i]) {   // a key just before the jump with the value before it; nothing is added across the jump
                const double pre = BeforeJump(times[i], t0);
                if (pre > ta) {
                    KeyPose p; RawPose(nb, pre, true, p); LocalPose(nb, p);
                    if (!Subdivide(nb, ta, a, pre, p, 0)) return false;
                    Emit(nb, pre, p);
                }
            } else if (!Subdivide(nb, ta, a, times[i], poses[i], 0)) return false;
            Emit(nb, times[i], poses[i]);
            ta = times[i]; a = poses[i];
        }
        na.translation.assign(emitT.begin(), emitT.end()); na.rotation.assign(emitQ.begin(), emitQ.end()); na.scale.assign(emitS.begin(), emitS.end());
        return true;
    }
    /// A blend shape weight curve the same way (its own value is the cubic, so the check is exact).
    bool BakeBlendFileKeys(const CurveNode& cn, double t0, double t1, BlendAnim& ba) {
        const Curve* c = cn.curve[0];
        std::vector<const Curve*> cs; if (c) cs.push_back(c);
        std::vector<double> times; std::vector<char> jump;
        KeyTimes(cs, t0, t1, times, jump);
        std::size_t hint = (std::size_t)-1, hintP = (std::size_t)-1;
        auto val = [&](double at, std::size_t& h) { ++fileKeyEvals; return c ? c->Eval(at, h) : cn.def[0]; };
        auto emit = [&](double at, double v) { Key3 k; k.time = at - t0; k.v[0] = v; k.v[1] = k.v[2] = 0; ba.percent.push_back(k); };
        const KeyTolerance& tol = opt.keyTolerance;
        bool failed = false;
        std::function<void(double, double, double, double, int)> sub = [&](double ta, double va, double tb, double vb, int depth) {
            const double len = tb - ta, g = tol.minGap * (1.0 + 1e-6);
            if (failed || !(len / 2.0 > g) || depth > 40) return;
            if (fileKeyEvals > 4 * opt.limits.maxTotalBakedSamples) { failed = true; return; }
            const double t1_ = ta + len / 3.0, t2_ = ta + len * (2.0 / 3.0);
            const double v1 = val(t1_, hintP), v2 = val(t2_, hintP);
            const double err = CubicDeviation(v1 - (va + (vb - va) / 3.0), v2 - (va + (vb - va) * 2.0 / 3.0)) / tol.percent;
            if (!(err > 1.0)) return;
            const int n = (int)std::max(2.0, std::min(std::ceil(std::sqrt(err) * 1.1), std::min(64.0, std::floor(len / g))));
            if (n == 3) {
                sub(ta, va, t1_, v1, depth + 1); emit(t1_, v1);
                sub(t1_, v1, t2_, v2, depth + 1); emit(t2_, v2);
                sub(t2_, v2, tb, vb, depth + 1);
                return;
            }
            double tp = ta, vp = va;
            for (int i = 1; i < n; ++i) {
                const double ti = ta + len * ((double)i / (double)n), vi = val(ti, hintP);
                sub(tp, vp, ti, vi, depth + 1); emit(ti, vi);
                tp = ti; vp = vi;
            }
            sub(tp, vp, tb, vb, depth + 1);
        };
        double ta = times[0], va = val(ta, hint);
        emit(ta, va);
        for (std::size_t i = 1; i < times.size() && !failed; ++i) {
            const double vb = val(times[i], hint);
            if (jump[i]) {
                const double pre = BeforeJump(times[i], t0);
                if (pre > ta) { const double vp = val(pre, hintP); sub(ta, va, pre, vp, 0); emit(pre, vp); }
            } else sub(ta, va, times[i], vb, 0);
            emit(times[i], vb);
            ta = times[i]; va = vb;
        }
        if (failed) return Fail("animation bake budget exceeded");
        return true;
    }

    /// One take: what its keys are made from.
    struct Plan {
        std::size_t clip = 0;        // index in Scene::clips
        std::vector<Layr> layers;
        std::map<int, std::vector<NodeCN>> perNode;   // node index -> curve nodes (ordered)
        struct Blend { int mesh, channel; const CurveNode* cn; };
        std::vector<Blend> blends;
        double t0 = 0, t1 = 0, dur = 0; std::size_t samples = 0;
    };
    std::vector<Plan> plans;
    /// Makes the keys of every planned clip into `sc.clips` (the import step; BuildClips).
    bool Bake(Scene& sc) {
        fileKeyEvals = 0; err.clear();
        for (Plan& pl : plans) {
            if (pl.clip >= sc.clips.size()) return Fail("clip missing");
            Clip& clip = sc.clips[pl.clip];
            clip.nodes.clear(); clip.blends.clear();
            const double t0 = pl.t0, t1 = pl.t1, dur = pl.dur;
            const std::size_t samples = pl.samples;
            std::vector<Layr>& layers = pl.layers;
            std::map<int, std::vector<NodeCN>>& perNode = pl.perNode;
            if (opt.fileKeys) {
                for (auto& kv : perNode) {
                    NodeAnim na; na.node = kv.first;
                    if (!BakeNodeFileKeys(kv.second, layers, kv.first, t0, t1, na)) return false;
                    clip.nodes.push_back(std::move(na));
                }
                for (const Plan::Blend& b : pl.blends) {
                    BlendAnim ba; ba.mesh = b.mesh; ba.channel = b.channel;
                    if (!BakeBlendFileKeys(*b.cn, t0, t1, ba)) return false;
                    clip.blends.push_back(std::move(ba));
                }
                continue;
            }
            std::vector<double> times;
            for (std::size_t k = 0; k <= samples; ++k) times.push_back(dur > 0 ? dur * (double)k / (double)samples : 0.0);
            if (dur <= 0) times.assign(1, 0.0);
            auto evalVec = [&](std::vector<NodeCN>& list, int which, const double base[3], double time, double out[3]) {
                for (int k = 0; k < 3; ++k) out[k] = base[k];
                bool first = true;
                for (NodeCN& n : list) {
                    if (n.which != which) continue;
                    double lv[3];
                    for (int k = 0; k < 3; ++k) lv[k] = n.cn->curve[k] ? n.cn->curve[k]->Eval(time + t0, n.hint[k]) : n.cn->def[k];
                    const Layr& ly = layers[(std::size_t)n.layer];
                    const double w = std::max(0.0, ly.weight) * 0.01;
                    if (first) { for (int k = 0; k < 3; ++k) out[k] = lv[k]; first = false; }
                    else if (ly.blendMode == 0) for (int k = 0; k < 3; ++k) out[k] += lv[k] * w;
                    else for (int k = 0; k < 3; ++k) out[k] += (lv[k] - out[k]) * std::min(1.0, w);
                }
            };
            for (auto& kv : perNode) {
                NodeAnim na; na.node = kv.first;
                const XProps base = xp[(std::size_t)kv.first];
                const FixedRot fixed = FixedRotations(base);   // samples change only t, r and s
                na.translation.reserve(times.size()); na.rotation.reserve(times.size()); na.scale.reserve(times.size());
                // A sample whose translation, rotation and scaling are the same (bit for bit) as the previous sample's has
                // the same result: it is copied. The rotation matrix alone is reused the same way.
                XProps x = base;
                double prevT[3], prevR[3], prevS[3], tr[3], q[4], s[3];
                bool havePrev = false, haveR = false;
                Mat R;
                const bool isRootNode = isRoot[(std::size_t)kv.first] != 0;
                for (double t : times) {
                    evalVec(kv.second, 0, base.t, t, x.t); evalVec(kv.second, 1, base.r, t, x.r); evalVec(kv.second, 2, base.s, t, x.s);
                    const bool sameR = haveR && std::memcmp(prevR, x.r, sizeof(prevR)) == 0;
                    if (!(havePrev && sameR && std::memcmp(prevT, x.t, sizeof(prevT)) == 0 && std::memcmp(prevS, x.s, sizeof(prevS)) == 0)) {
                        if (!sameR) { R = Euler(x.r, x.order); std::memcpy(prevR, x.r, sizeof(prevR)); haveR = true; }
                        const bool simple = fixed.simple && Small3(x.t) && Small3(x.r) && Small3(x.s);
                        const Mat local = conv.Local(simple ? LocalSimple(x, fixed, R) : LocalMatrix(x, fixed, R), isRootNode);
                        Decompose(local, tr, q, s);
                        std::memcpy(prevT, x.t, sizeof(prevT)); std::memcpy(prevS, x.s, sizeof(prevS)); havePrev = true;
                    }
                    Key3 kt; kt.time = t; for (int k = 0; k < 3; ++k) kt.v[k] = tr[k];
                    Key4 kq; kq.time = t; for (int k = 0; k < 4; ++k) kq.v[k] = q[k];
                    Key3 ks; ks.time = t; for (int k = 0; k < 3; ++k) ks.v[k] = s[k];
                    na.translation.push_back(kt); na.rotation.push_back(kq); na.scale.push_back(ks);
                }
                clip.nodes.push_back(std::move(na));
            }
            for (const Plan::Blend& b : pl.blends) {
                BlendAnim ba; ba.mesh = b.mesh; ba.channel = b.channel;
                std::size_t blendHint = (std::size_t)-1;
                for (double t : times) {
                    Key3 k; k.time = t; k.v[1] = k.v[2] = 0;
                    k.v[0] = b.cn->curve[0] ? b.cn->curve[0]->Eval(t + t0, blendHint) : b.cn->def[0];
                    ba.percent.push_back(k);
                }
                clip.blends.push_back(std::move(ba));
            }
        }
        return true;
    }
};

bool Builder::Animation() {
    using Layr = Animator::Layr; using NodeCN = Animator::NodeCN;
    // Budgets first: every count below is checked before anything proportional to it is allocated.
    {
        std::size_t stacks = 0, layers = 0, curves = 0, curveNodes = 0;
        for (const Obj& o : objs) {
            if (o.cls == "AnimationStack") ++stacks;
            else if (o.cls == "AnimationLayer") ++layers;
            else if (o.cls == "AnimationCurve") ++curves;
            else if (o.cls == "AnimationCurveNode") ++curveNodes;
        }
        if (stacks > opt.limits.maxAnimStacks) return Fail("too many animation stacks");
        if (layers > opt.limits.maxAnimLayers) return Fail("too many animation layers");
        if (curves > opt.limits.maxAnimCurves || curveNodes > opt.limits.maxAnimCurves) return Fail("too many animation curves");
    }
    std::size_t totalKeys = 0, totalBaked = 0;
    // The clips' keys are made later, by BuildClips (the import step): what they need is kept in `job`.
    auto job = std::make_shared<Animator>();
    std::vector<Curve>& curveStore = job->curveStore;
    // curves: their arrays go into the Animator's pools; each curve records where its runs start, and the runs get
    // their pointers once every curve is in (the pools may move while they grow)
    IdMap<int> curveOf;
    std::vector<double>& poolT = job->poolT; std::vector<double>& poolV = job->poolV; std::vector<double>& poolSlope = job->poolSlope;
    std::vector<std::uint32_t>& poolSet = job->poolSet; std::vector<int>& poolFlags = job->poolFlags;
    struct At { std::size_t t, set, flags, slope; };
    std::vector<At> runAt;
    {
        std::size_t keys = 0;   // an upper bound of the keys, for one allocation of each pool
        for (const Obj& o : objs) {
            if (o.cls != "AnimationCurve") continue;
            const Array* kt = ArrayOf(*o.e, "KeyTime");
            const Array* kv = ArrayOf(*o.e, "KeyValueFloat");
            if (!kv) kv = ArrayOf(*o.e, "KeyValueDouble");
            if (kt && kv) keys += std::min(kt->size(), kv->size());
        }
        keys = std::min<std::size_t>(keys, opt.limits.maxAnimKeys);
        poolT.reserve(keys); poolV.reserve(keys);
    }
    for (const Obj& o : objs) {
        if (o.cls != "AnimationCurve") continue;
        Curve c; At at{poolT.size(), poolSet.size(), poolFlags.size(), poolSlope.size()};
        const Array* kt = ArrayOf(*o.e, "KeyTime");
        const Array* kv = ArrayOf(*o.e, "KeyValueFloat");
        if (!kv) kv = ArrayOf(*o.e, "KeyValueDouble");
        if (!kt || !kv) continue;
        const std::size_t n = std::min(kt->size(), kv->size());
        if (n > opt.limits.maxAnimKeys - totalKeys) return Fail("too many animation keys");
        totalKeys += n;
        const Array* fl = ArrayOf(*o.e, "KeyAttrFlags");
        const Array* da = ArrayOf(*o.e, "KeyAttrDataFloat");
        std::vector<double> daBits; Array daBitsArr;
        if (da && !doc.binary && doc.version >= 7200) {
            // ASCII files from 7.2 on print this array as integers: the 32-bit patterns of the floats (slopes,
            // and packed weights/velocities). Binary files carry real floats; see the same rule in ufbx.
            daBits.reserve(da->size());
            for (std::size_t k = 0; k < da->size(); ++k) { const double d = (*da)[k]; const std::uint32_t u = (d > -4.3e9 && d < 4.3e9) ? (std::uint32_t)(std::int64_t)d : 0u; float f; std::memcpy(&f, &u, 4); daBits.push_back((double)f); }
            daBitsArr.type = 'd'; daBitsArr.count = (std::uint32_t)daBits.size(); daBitsArr.data = (const std::uint8_t*)daBits.data();
            da = &daBitsArr;
        }
        const Array* rc = ArrayOf(*o.e, "KeyAttrRefCount");
        // Attribute set of each key: KeyAttrRefCount says how many keys in a row use each set (all use set 0
        // without it). Walked along with the keys below, the same rule as one index per key computed first.
        const std::size_t nsets = (rc && !rc->empty()) ? rc->size() : 1;
        std::size_t sCur = 0, left = (rc && !rc->empty()) ? (std::size_t)std::max(0.0, (*rc)[0]) : n;
        auto nextSet = [&]() -> std::size_t {
            if (!rc) return 0;
            while (left == 0 && sCur + 1 < rc->size()) { ++sCur; left = (std::size_t)std::max(0.0, (*rc)[sCur]); }
            const std::size_t sg = sCur;
            if (left) --left;
            return sg;
        };
        double prev = -1e300;
        bool otherSet = false;
        std::size_t maxSet = 0;   // the tables below cover the sets the kept keys use (never more than the keys)
        // The usual types (KeyTime int64, KeyValueFloat float32) read directly: the same (double)value as Array's [].
        const bool ktLong = kt->type == 'l', kvFloat = kv->type == 'f';
        auto timeAt = [&](std::size_t i) -> double { if (ktLong) { std::int64_t x; std::memcpy(&x, kt->data + i * 8, 8); return (double)x; } return (*kt)[i]; };
        auto valueAt = [&](std::size_t i) -> double { if (kvFloat) { float x; std::memcpy(&x, kv->data + i * 4, 4); return (double)x; } return (*kv)[i]; };
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t sg = nextSet();
            const double t = timeAt(i) / kKTimePerSec;
            const double val = valueAt(i);
            if (!std::isfinite(t) || !std::isfinite(val) || t < prev) continue;   // keep keys sorted and finite
            prev = t;
            poolT.push_back(t); poolV.push_back(val);
            if (nsets > 1) { poolSet.push_back((std::uint32_t)sg); otherSet |= sg != 0; maxSet = std::max(maxSet, sg); }
            if (da && (sg * 4 + 1) < da->size()) c.hasSlopes = true;   // without the array every slope is 0
        }
        c.t.n = c.v.n = poolT.size() - at.t;
        if (!otherSet) poolSet.resize(at.set);   // every key uses set 0
        c.set.n = poolSet.size() - at.set;
        // per set: the flags (0x8, cubic, without KeyAttrFlags) and the slopes (0 where the file has none)
        const std::size_t usedSets = c.set.empty() ? 1 : maxSet + 1;
        for (std::size_t sg = 0; sg < usedSets; ++sg)
            poolFlags.push_back((fl && !fl->empty()) ? SafeInt((*fl)[std::min(sg, fl->size() - 1)]) : 0x8);
        c.setFlags.n = usedSets;
        if (c.hasSlopes) {   // right slopes, then next-left slopes, one run each
            poolSlope.resize(at.slope + 2 * usedSets, 0.0);
            for (std::size_t sg = 0; sg < usedSets; ++sg)
                if ((sg * 4 + 1) < da->size()) { poolSlope[at.slope + sg] = (*da)[sg * 4]; poolSlope[at.slope + usedSets + sg] = (*da)[sg * 4 + 1]; }
            c.setR.n = c.setL.n = usedSets;
        }
        curveOf[ToId(o.e->props[0].Num())] = (int)curveStore.size();
        curveStore.push_back(c);
        runAt.push_back(at);
    }
    for (std::size_t i = 0; i < curveStore.size(); ++i) {   // the pools are complete: point the runs into them
        Curve& c = curveStore[i]; const At& at = runAt[i];
        c.t.p = poolT.data() + at.t; c.v.p = poolV.data() + at.t;
        c.set.p = poolSet.data() + at.set; c.setFlags.p = poolFlags.data() + at.flags;
        c.setR.p = poolSlope.data() + at.slope; c.setL.p = poolSlope.data() + at.slope + c.setR.n;
    }
    // curve nodes
    std::vector<CurveNode>& cns = job->cns; IdMap<std::size_t> cnOf;
    for (const Obj& o : objs) {
        if (o.cls != "AnimationCurveNode") continue;
        CurveNode cn;
        const Id id = ToId(o.e->props[0].Num());
        o.props.Vec3("d|X", cn.def, 0);
        if (o.props.Has("d|DeformPercent")) cn.def[0] = o.props.Get("d|DeformPercent", 0);
        for (const Conn* c : ChildrenOf(id)) {   // curves feeding it
            if (!c->propLink) continue;
            const Obj* cv = Get(c->child);
            if (!cv || cv->cls != "AnimationCurve") continue;
            const int* it = curveOf.find(c->child);
            if (!it || curveStore[(std::size_t)*it].Empty()) continue;
            const int comp = (c->prop == "d|X" || c->prop == "d|DeformPercent") ? 0 : c->prop == "d|Y" ? 1 : c->prop == "d|Z" ? 2 : -1;
            if (comp >= 0) cn.curve[comp] = &curveStore[(std::size_t)*it];
        }
        for (int k = 1; k < 3; ++k) { const char* nm = k == 1 ? "d|Y" : "d|Z"; cn.def[k] = o.props.Get(nm, 0); }
        for (const Conn* c : ParentsOf(id)) {    // what it drives
            if (!c->propLink) continue;
            cn.target = c->parent; cn.prop = c->prop; break;
        }
        cnOf[id] = cns.size();
        cns.push_back(cn);
    }
    // stacks -> layers -> curve nodes
    for (const Obj& st : objs) {
        if (st.cls != "AnimationStack") continue;
        const Id sid = ToId(st.e->props[0].Num());
        std::vector<Layr> layers;
        for (const Conn* c : ChildrenOf(sid)) {
            const Obj* l = Get(c->child);
            if (!l || l->cls != "AnimationLayer" || c->propLink) continue;
            Layr ly; ly.id = c->child; ly.name = l->name; ly.weight = l->props.Get("Weight", 100.0); ly.blendMode = SafeInt(l->props.Get("BlendMode", 0.0));
            layers.push_back(ly);
        }
        std::map<int, std::vector<NodeCN>> perNode;                       // node index -> curve nodes (ordered)
        struct BlendCN { int layer; Id target; const CurveNode* cn; };
        std::vector<BlendCN> blendCns;
        double t0 = 1e300, t1 = -1e300;
        auto span = [&](const CurveNode& cn) { for (int k = 0; k < 3; ++k) if (cn.curve[k] && !cn.curve[k]->Empty()) { t0 = std::min(t0, cn.curve[k]->t.front()); t1 = std::max(t1, cn.curve[k]->t.back()); } };
        for (std::size_t li = 0; li < layers.size(); ++li)
            for (const Conn* c : ChildrenOf(layers[li].id)) {
                const std::size_t* it = cnOf.find(c->child);
                if (!it || c->propLink) continue;
                const CurveNode& cn = cns[*it];
                const int* nm = nodeOfModel.find(cn.target);
                if (nm) {
                    int which = cn.prop == "Lcl Translation" ? 0 : cn.prop == "Lcl Rotation" ? 1 : cn.prop == "Lcl Scaling" ? 2 : -1;
                    if (which < 0) continue;
                    perNode[*nm].push_back({(int)li, which, &cn, {(std::size_t)-1, (std::size_t)-1, (std::size_t)-1}, {(std::size_t)-1, (std::size_t)-1, (std::size_t)-1}}); span(cn);
                } else if (cn.prop == "DeformPercent" && chanOfObj.count(cn.target)) { blendCns.push_back({(int)li, cn.target, &cn}); span(cn); }
            }
        if (perNode.empty() && blendCns.empty()) continue;
        if (t1 < t0) continue;
        // A take has its own time span (LocalStart / LocalStop): several takes can share one timeline
        // and their curves, so the span, not the keys, says which part of the curves a take plays.
        if (st.props.Has("LocalStop")) {
            const double a = st.props.Get("LocalStart", 0.0) / kKTimePerSec, b = st.props.Get("LocalStop", 0.0) / kKTimePerSec;
            if (std::isfinite(a) && std::isfinite(b) && b > a) { t0 = a; t1 = b; }
        }
        Clip clip; clip.name = std::string(st.name);
        const double dur = t1 - t0;
        std::size_t samples = SafeSize(std::max(1.0, std::ceil(dur * opt.bakeRate)));   // a hostile LocalStop makes dur huge: saturate, then the limits below apply
        if (samples >= opt.limits.maxBakedSamples) samples = opt.limits.maxBakedSamples - 1;   // (not samples + 1 > max: that wraps for a saturated count)
        {   // whole-file budget: this clip costs (sample count) x (curve nodes it evaluates)
            std::size_t entries = blendCns.size();
            for (const auto& kv : perNode) entries += kv.second.size();
            const std::size_t ts = dur > 0 ? samples + 1 : 1;
            if (entries && ts > (opt.limits.maxTotalBakedSamples - totalBaked) / entries) return Fail("animation bake budget exceeded");
            totalBaked += ts * entries;
        }
        clip.duration = dur;
        clip.start = t0;
        Animator::Plan plan;
        plan.clip = sc.clips.size(); plan.t0 = t0; plan.t1 = t1; plan.dur = dur; plan.samples = samples;
        for (Layr& ly : layers) ly.name = {};   // (a view into the file: not kept)
        plan.layers = std::move(layers); plan.perNode = std::move(perNode);
        for (const BlendCN& b : blendCns) plan.blends.push_back({chanOfObj[b.target].first, chanOfObj[b.target].second, b.cn});
        job->plans.push_back(std::move(plan));
        sc.clips.push_back(std::move(clip));
    }
    if (!job->plans.empty()) {
        for (CurveNode& cn : cns) cn.prop = {};   // (views into the file: not kept)
        job->opt = opt; job->conv = conv;
        job->isRoot.resize(sc.nodes.size());
        for (std::size_t i = 0; i < sc.nodes.size(); ++i) job->isRoot[i] = sc.nodes[i].parent < 0 ? 1 : 0;
        job->xp = std::move(xp);   // (the last step of the build: nothing reads xp after this)
        sc.clipSource = std::move(job);
    }
    return true;
}

} // namespace scene_build

using namespace scene_build;

bool BuildClips(Scene& s, std::string* err) {
    if (!s.clipSource) return true;
    Animator& job = static_cast<Animator&>(*s.clipSource);
    const bool ok = job.Bake(s);
    if (!ok && err) *err = job.err.empty() ? "FBX clip build failed" : job.err;
    s.clipSource.reset();
    return ok;
}

} // namespace fbx
} // namespace okay
