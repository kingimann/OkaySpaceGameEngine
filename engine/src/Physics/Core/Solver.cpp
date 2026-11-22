// The island solver of okay::phys::World: sequential impulses with warm starting, friction, restitution, rolling
// resistance and a position pass.
//
// Written from:
//  * E. Catto, "Iterative Dynamics with Temporal Coherence" (2005) and "Fast and Simple Physics using Sequential
//    Impulses" (GDC 2006): the velocity constraint, the effective mass 1 / (J M^-1 J^T), clamping the ACCUMULATED
//    impulse (not each increment), warm starting from last step's impulses;
//  * E. Catto, "Continuous Collision" (GDC 2013): a contact that is still apart lets the bodies approach by exactly its
//    gap this step (speculative contact);
//  * K. Erleben, PhD thesis (2004), ch. 6 (projection of positions after the velocity solve) for the position pass;
//  * Coulomb friction bounded by mu times the normal impulse, as a cone (the tangent impulse vector clamped in length) or
//    a pyramid (each tangent direction clamped on its own).
// No other engine's code was read or used.
#include "WorldInternal.hpp"

#include <algorithm>
#include <cmath>

namespace okay {
namespace phys {

namespace {

struct CPoint {
    V3 n, t1, t2;         // normal and friction directions of this point
    V3 rA, rB;            // arms from the centres of mass (world) at the start of the step
    V3 cA, cB;            // contact points relative to the centres of mass, in each body's frame (position pass)
    float nMass = 0, tMass[2] = {0, 0};
    float bias = 0;       // speculative room: separation / dt when apart
    float relVel = 0;     // normal speed before the solve
    ContactPointState* st = nullptr;
};

struct CSolver {
    int a = 0, b = 0;
    V3 n, t1, t2;
    float friction = 0, restitution = 0, rolling = 0;
    int count = 0;
    bool mixed = false;
    CPoint p[4];
    V3 rollImpulse;
    float rollRadius = 0.0f;
};

void Apply(SolverBody& A, SolverBody& B, const V3& P, const V3& rA, const V3& rB) {
    if (A.dynamic) { A.v -= Mul(A.invMassAxis, P); A.w -= A.invI * Cross(rA, P); }
    if (B.dynamic) { B.v += Mul(B.invMassAxis, P); B.w += B.invI * Cross(rB, P); }
}

// All the normal constraints of one manifold at once (a small linear complementarity problem: impulses x >= 0, resulting
// normal speeds w = K x + b >= 0, x_i w_i = 0), solved by trying the active sets from "all points push" down to "none"
// and taking the first consistent one (the direct method for small LCPs, K. Murty, "Linear Complementarity, Linear and
// Nonlinear Programming", 1988, ch. 1). Solving the points of a face together removes the lean that one-at-a-time
// Gauss-Seidel leaves in a resting stack. Four points on a face carry only three independent constraints, so K gets a
// tiny diagonal term: among the equally valid answers it picks the evenly spread one. Returns the summed impulse.
float BlockNormal(CSolver& c, SolverBody& A, SolverBody& B) {
    const int n = c.count;
    float K[4][4], b[4], a[4];
    V3 ra[4], rb[4];
    for (int i = 0; i < n; ++i) { ra[i] = Cross(c.p[i].rA, c.n); rb[i] = Cross(c.p[i].rB, c.n); a[i] = c.p[i].st->normalImpulse; }
    const float lin = (A.dynamic ? Dot(c.n, Mul(A.invMassAxis, c.n)) : 0.0f) + (B.dynamic ? Dot(c.n, Mul(B.invMassAxis, c.n)) : 0.0f);
    float diag = 0.0f;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            float k = lin;
            if (A.dynamic) k += Dot(ra[i], A.invI * ra[j]);
            if (B.dynamic) k += Dot(rb[i], B.invI * rb[j]);
            K[i][j] = k;
            if (i == j) diag = std::max(diag, k);
        }
    const float reg = 1e-4f * diag + 1e-12f;
    for (int i = 0; i < n; ++i) K[i][i] += reg;
    for (int i = 0; i < n; ++i) {
        const float vn = Dot(VelAt(B, c.p[i].rB) - VelAt(A, c.p[i].rA), c.n) + c.p[i].bias;
        float ka = 0.0f;
        for (int j = 0; j < n; ++j) ka += K[i][j] * a[j];
        b[i] = vn - ka;   // the normal speed with the accumulated impulses taken out
    }
    // active sets, biggest first (15, 14, 13, 11, 7, ... for four points)
    static const int order4[16] = {15, 7, 11, 13, 14, 3, 5, 6, 9, 10, 12, 1, 2, 4, 8, 0};
    static const int order3[8] = {7, 3, 5, 6, 1, 2, 4, 0};
    static const int order2[4] = {3, 1, 2, 0};
    const int* order = n == 4 ? order4 : (n == 3 ? order3 : order2);
    const int sets = 1 << n;
    float x[4] = {0, 0, 0, 0};
    bool found = false;
    for (int si = 0; si < sets && !found; ++si) {
        const int mask = order[si];
        int idx[4], m = 0;
        for (int i = 0; i < n; ++i) if (mask & (1 << i)) idx[m++] = i;
        float M[4][5];
        for (int r = 0; r < m; ++r) {
            for (int cc = 0; cc < m; ++cc) M[r][cc] = K[idx[r]][idx[cc]];
            M[r][m] = -b[idx[r]];
        }
        bool ok = true;
        for (int col = 0; col < m && ok; ++col) {   // Gaussian elimination with partial pivoting
            int piv = col;
            for (int r = col + 1; r < m; ++r) if (std::fabs(M[r][col]) > std::fabs(M[piv][col])) piv = r;
            if (std::fabs(M[piv][col]) < 1e-20f) { ok = false; break; }
            if (piv != col) for (int cc = 0; cc <= m; ++cc) std::swap(M[piv][cc], M[col][cc]);
            for (int r = 0; r < m; ++r) {
                if (r == col) continue;
                const float f = M[r][col] / M[col][col];
                for (int cc = col; cc <= m; ++cc) M[r][cc] -= f * M[col][cc];
            }
        }
        if (!ok) continue;
        float xs[4] = {0, 0, 0, 0};
        for (int r = 0; r < m; ++r) { xs[idx[r]] = M[r][m] / M[r][r]; if (xs[idx[r]] < 0.0f) ok = false; }
        if (!ok) continue;
        for (int i = 0; i < n && ok; ++i) {   // the points left out must not be closing
            if (mask & (1 << i)) continue;
            float wv = b[i];
            for (int j = 0; j < n; ++j) wv += K[i][j] * xs[j];
            if (wv < -1e-6f) ok = false;
        }
        if (!ok) continue;
        for (int i = 0; i < n; ++i) x[i] = xs[i];
        found = true;
    }
    float sum = 0.0f;
    if (!found) {   // (degenerate numbers: fall back to one point at a time)
        for (int i = 0; i < n; ++i) {
            CPoint& p = c.p[i];
            const float vn = Dot(VelAt(B, p.rB) - VelAt(A, p.rA), c.n);
            const float o = p.st->normalImpulse;
            const float nw = std::max(o - p.nMass * (vn + p.bias), 0.0f);
            p.st->normalImpulse = nw;
            p.st->maxNormalImpulse = std::max(p.st->maxNormalImpulse, nw);
            Apply(A, B, c.n * (nw - o), p.rA, p.rB);
            sum += nw;
        }
        return sum;
    }
    for (int i = 0; i < n; ++i) {
        CPoint& p = c.p[i];
        const float d = x[i] - a[i];
        p.st->normalImpulse = x[i];
        p.st->maxNormalImpulse = std::max(p.st->maxNormalImpulse, x[i]);
        Apply(A, B, c.n * d, p.rA, p.rB);
        sum += x[i];
    }
    return sum;
}

} // namespace

void SolveIsland(World::Impl& w, const IslandWork& is, const SolveSettings& s) {
    const float dt = s.dt;
    const float invDt = 1.0f / dt;
    std::vector<SolverBody> sb;
    sb.reserve(is.bodies.size() + 8);
    sb.push_back(SolverBody{});   // slot 0: the fixed world
    sb[0].q = Q{};
    std::vector<std::pair<BodyId, int>> fixedSlots;   // static and kinematic bodies this island reads

    for (BodyId id : is.bodies) {
        BodyData& b = w.bodies[id];
        b.slot = (int)sb.size();
        SolverBody x;
        x.id = id;
        x.dynamic = true;
        x.com = b.com;
        x.q = b.xf.q;
        x.localCom = b.localCom;
        x.invMass = b.invMass;
        for (int k = 0; k < 3; ++k) x.invMassAxis[k] = (b.lockLin & (1u << k)) ? 0.0f : b.invMass;
        x.lockAng = b.lockAng;
        x.invI = WorldInvInertia(b.invInertiaLocal, b.xf.q, b.lockAng);
        // integrate forces into velocity (semi-implicit Euler)
        V3 v = b.v + (s.gravity * b.gravityScale + b.force * b.invMass) * dt;
        V3 av = b.w + (x.invI * b.torque) * dt;
        v *= 1.0f / (1.0f + dt * b.linDamp);
        av *= 1.0f / (1.0f + dt * b.angDamp);
        for (int k = 0; k < 3; ++k) {
            if (b.lockLin & (1u << k)) v[k] = 0.0f;
            if (b.lockAng & (1u << k)) av[k] = 0.0f;
        }
        b.force = V3{};
        b.torque = V3{};
        x.v = v;
        x.w = av;
        sb.push_back(x);
    }
    auto slotOf = [&](BodyId id) -> int {
        if (id == kInvalidId) return 0;
        const BodyData& b = w.bodies[id];
        if (b.type == BodyType::Dynamic && b.awake && b.slot >= 0 && b.slot < (int)sb.size() && sb[(std::size_t)b.slot].id == id)
            return b.slot;
        for (const auto& f : fixedSlots) if (f.first == id) return f.second;
        SolverBody x;
        x.id = id;
        x.dynamic = false;
        x.com = b.com;
        x.q = b.xf.q;
        x.localCom = b.localCom;
        if (b.type == BodyType::Kinematic) { x.v = b.v; x.w = b.w; }
        sb.push_back(x);
        fixedSlots.emplace_back(id, (int)sb.size() - 1);
        return (int)sb.size() - 1;
    };

    // ---- contacts ----
    std::vector<CSolver> cs;
    cs.reserve(is.contacts.size());
    for (int ci : is.contacts) {
        ContactPair& cp = w.contacts[(std::size_t)ci];
        const ColliderData& ca = w.colliders[cp.a];
        const ColliderData& cb = w.colliders[cp.b];
        CSolver c;
        c.a = slotOf(ca.body);
        c.b = slotOf(cb.body);
        c.n = cp.normal;
        Basis(c.n, c.t1, c.t2);
        c.friction = cp.friction;
        c.restitution = cp.restitution;
        c.rolling = cp.rolling;
        c.rollRadius = std::max(std::min(ca.def.shape.InnerRadius(), cb.def.shape.type == ShapeType::Plane ? 1e30f : cb.def.shape.InnerRadius()), 0.0f);
        if (cb.def.shape.type != ShapeType::Plane && ca.def.shape.type == ShapeType::Plane) c.rollRadius = cb.def.shape.InnerRadius();
        c.count = cp.count;
        c.mixed = cp.mixed;
        const BodyData& BA = w.bodies[ca.body];
        const BodyData& BB = w.bodies[cb.body];
        SolverBody& A = sb[(std::size_t)c.a];
        SolverBody& B = sb[(std::size_t)c.b];
        for (int k = 0; k < cp.count; ++k) {
            ContactPointState& st = cp.pts[k];
            CPoint& p = c.p[k];
            p.st = &st;
            const V3 pA = BA.xf.Apply(st.localA), pB = BB.xf.Apply(st.localB);
            p.rA = pA - A.com;
            p.rB = pB - B.com;
            p.cA = st.localA - BA.localCom;
            p.cB = st.localB - BB.localCom;
            if (cp.mixed) { p.n = st.normal; Basis(p.n, p.t1, p.t2); }
            else { p.n = c.n; p.t1 = c.t1; p.t2 = c.t2; }
            p.nMass = EffMass(A, B, p.rA, p.rB, p.n);
            p.tMass[0] = EffMass(A, B, p.rA, p.rB, p.t1);
            p.tMass[1] = EffMass(A, B, p.rA, p.rB, p.t2);
            p.relVel = Dot(VelAt(B, p.rB) - VelAt(A, p.rA), p.n);
            p.bias = st.separation > 0.0f ? st.separation * invDt : 0.0f;
            st.maxNormalImpulse = 0.0f;
            if (!s.warmStarting) { st.normalImpulse = 0; st.tangentImpulse[0] = st.tangentImpulse[1] = 0; }
        }
        cs.push_back(c);
    }

    // ---- joints ----
    std::vector<JointSolver> js;
    JointsPrepare(w, is, sb, slotOf, js, s);

    // ---- warm start ----
    if (s.warmStarting) {
        JointsWarmStart(js, sb);
        for (CSolver& c : cs) {
            SolverBody& A = sb[(std::size_t)c.a];
            SolverBody& B = sb[(std::size_t)c.b];
            for (int k = 0; k < c.count; ++k) {
                const CPoint& p = c.p[k];
                const V3 P = p.n * p.st->normalImpulse + p.t1 * p.st->tangentImpulse[0] + p.t2 * p.st->tangentImpulse[1];
                Apply(A, B, P, p.rA, p.rB);
            }
        }
    }

    // ---- velocity iterations ----
    const bool blockNormals = s.blockSolver;
    for (int it = 0; it < s.velocityIterations; ++it) {
        JointsSolveVelocity(js, sb, s);
        for (CSolver& c : cs) {
            SolverBody& A = sb[(std::size_t)c.a];
            SolverBody& B = sb[(std::size_t)c.b];
            // friction first (it is bounded by the normal impulse of the last pass), then the normal, which wins
            for (int kk = 0; kk < c.count; ++kk) {
                const int k = (it & 1) ? c.count - 1 - kk : kk;
                CPoint& p = c.p[k];
                const float maxF = c.friction * p.st->normalImpulse;
                const V3 dv = VelAt(B, p.rB) - VelAt(A, p.rA);
                if (s.frictionCone) {
                    const float l1 = -p.tMass[0] * Dot(dv, p.t1), l2 = -p.tMass[1] * Dot(dv, p.t2);
                    const float o1 = p.st->tangentImpulse[0], o2 = p.st->tangentImpulse[1];
                    float n1 = o1 + l1, n2 = o2 + l2;
                    const float len = std::sqrt(n1 * n1 + n2 * n2);
                    if (len > maxF) { const float sc = len > 0.0f ? maxF / len : 0.0f; n1 *= sc; n2 *= sc; }
                    p.st->tangentImpulse[0] = n1; p.st->tangentImpulse[1] = n2;
                    Apply(A, B, p.t1 * (n1 - o1) + p.t2 * (n2 - o2), p.rA, p.rB);
                } else {
                    for (int t = 0; t < 2; ++t) {
                        const V3& tn = t == 0 ? p.t1 : p.t2;
                        const V3 dvt = VelAt(B, p.rB) - VelAt(A, p.rA);
                        const float l = -p.tMass[t] * Dot(dvt, tn);
                        const float o = p.st->tangentImpulse[t];
                        const float nw = std::min(std::max(o + l, -maxF), maxF);
                        p.st->tangentImpulse[t] = nw;
                        Apply(A, B, tn * (nw - o), p.rA, p.rB);
                    }
                }
            }
            float sumN = 0.0f;
            if (c.count >= 2 && blockNormals && !c.mixed) {
                sumN = BlockNormal(c, A, B);
            } else {
                // (the points are visited forward on even passes and backward on odd ones, so no corner is always first)
                for (int kk = 0; kk < c.count; ++kk) {
                    const int k = (it & 1) ? c.count - 1 - kk : kk;
                    CPoint& p = c.p[k];
                    const float vn = Dot(VelAt(B, p.rB) - VelAt(A, p.rA), p.n);
                    const float l = -p.nMass * (vn + p.bias);
                    const float o = p.st->normalImpulse;
                    const float nw = std::max(o + l, 0.0f);
                    p.st->normalImpulse = nw;
                    p.st->maxNormalImpulse = std::max(p.st->maxNormalImpulse, nw);
                    Apply(A, B, p.n * (nw - o), p.rA, p.rB);
                    sumN += nw;
                }
            }
            // rolling resistance: an angular friction that resists the relative spin, bounded by the normal push
            // times rolling * radius (the round body's lever)
            if (c.rolling > 0.0f && sumN > 0.0f && (A.dynamic || B.dynamic)) {
                const V3 wr = B.w - A.w;
                const float ws = Len(wr);
                if (ws > 1e-9f) {
                    const V3 dir = wr * (1.0f / ws);
                    const float m = AngEffMass(A, B, dir);
                    const V3 old = c.rollImpulse;
                    V3 nw = old - dir * (m * ws);
                    const float maxR = c.rolling * sumN * std::max(c.rollRadius, 0.05f);
                    const float l = Len(nw);
                    if (l > maxR) nw *= maxR / l;
                    const V3 d = nw - old;
                    c.rollImpulse = nw;
                    ApplyAngular(A, -d);
                    ApplyAngular(B, d);
                }
            }
        }
    }

    // ---- restitution: points that were closing fast and got pushed bounce back with e times that speed ----
    for (CSolver& c : cs) {
        if (c.restitution <= 0.0f) continue;
        SolverBody& A = sb[(std::size_t)c.a];
        SolverBody& B = sb[(std::size_t)c.b];
        for (int k = 0; k < c.count; ++k) {
            CPoint& p = c.p[k];
            if (p.relVel > -s.restitutionThreshold || p.st->maxNormalImpulse <= 0.0f) continue;
            const float vn = Dot(VelAt(B, p.rB) - VelAt(A, p.rA), p.n);
            const float l = -p.nMass * (vn + c.restitution * p.relVel);
            const float o = p.st->normalImpulse;
            const float nw = std::max(o + l, 0.0f);
            p.st->normalImpulse = nw;
            Apply(A, B, p.n * (nw - o), p.rA, p.rB);
        }
    }

    // ---- integrate positions ----
    for (SolverBody& b : sb) {
        if (!b.dynamic) continue;
        const float lv = Len(b.v);
        if (lv > s.maxLinearSpeed) b.v *= s.maxLinearSpeed / lv;
        const float la = Len(b.w);
        if (la > s.maxAngularSpeed) b.w *= s.maxAngularSpeed / la;
        b.com += b.v * dt;
        b.q = Integrate(b.q, b.w, dt);
    }

    // ---- position iterations: remove what is left of overlaps and joint drift ----
    for (int it = 0; it < s.positionIterations; ++it) {
        JointsSolvePosition(w, js, sb, s);
        for (CSolver& c : cs) {
            SolverBody& A = sb[(std::size_t)c.a];
            SolverBody& B = sb[(std::size_t)c.b];
            if (!A.dynamic && !B.dynamic) continue;
            for (int k = 0; k < c.count; ++k) {
                const CPoint& p = c.p[k];
                const V3 rA = Rotate(A.q, p.cA), rB = Rotate(B.q, p.cB);
                const float sep = Dot((B.com + rB) - (A.com + rA), p.n);
                const float C = std::max(std::min(s.positionCorrection * (sep + s.linearSlop), 0.0f), -s.maxCorrection);
                if (C >= 0.0f) continue;
                const float m = EffMass(A, B, rA, rB, p.n);
                const V3 P = p.n * (-C * m);
                if (A.dynamic) { A.com -= Mul(A.invMassAxis, P); A.q = Integrate(A.q, A.invI * Cross(rA, P) * -1.0f, 1.0f); }
                if (B.dynamic) { B.com += Mul(B.invMassAxis, P); B.q = Integrate(B.q, B.invI * Cross(rB, P), 1.0f); }
            }
        }
    }

    JointsFinish(w, js, s);

    // ---- write back ----
    const float linSq = s.sleepLinear * s.sleepLinear, angSq = s.sleepAngular * s.sleepAngular;
    for (const SolverBody& x : sb) {
        if (!x.dynamic) continue;
        BodyData& b = w.bodies[x.id];
        b.v = x.v;
        b.w = x.w;
        b.com = x.com;
        b.xf.q = x.q;
        b.xf.p = x.com - Rotate(x.q, b.localCom);
        if (LenSq(b.v) > linSq || LenSq(b.w) > angSq) b.sleepTime = 0.0f;
        else b.sleepTime += dt;
        b.slot = -1;
    }
}

} // namespace phys
} // namespace okay
