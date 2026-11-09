#include "okay/Scene/Transform.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace okay {

namespace {
std::uint64_t g_worldStamp = 0;   // every recompute gets a new stamp (children compare it)
thread_local std::uint64_t t_walkSteps = 0;    // Transform::WorldWalkSteps
thread_local std::uint64_t t_rebuilds = 0;     // Transform::WorldRebuilds
thread_local std::uint64_t t_childWork = 0;    // Transform::ChildListWork
std::mutex g_settleMutex;                      // readers that sweep the holes out of a child list take turns
}

std::uint64_t Transform::WorldWalkSteps() { return t_walkSteps; }
std::uint64_t Transform::WorldRebuilds() { return t_rebuilds; }
std::uint64_t Transform::ChildListWork() { return t_childWork; }

// World matrices are cached per transform. The local fields are public and get
// written directly all over the engine (about 2400 places, the editor's drag boxes and the
// scripting bindings among them), so no dirty flag could see a plain `t->localPosition.x = 3`.
// Instead the cache keeps a copy of the inputs it was built from: same local TRS, same
// parent, and the parent's own cache unchanged (its stamp) -> the cached matrix is still right.
// Checking an unchanged chain is one read-only loop up the parents (a few compares per level, no
// allocation, no matrix math); only a level that really changed is rebuilt, and only from the
// first changed level down. The world scale (LossyScale) and rotation are cached with the matrix, so
// asking for any of them costs the same one check.
//
// The walk up the parent chain is a loop, not a recursion: a hierarchy 100000 deep
// (a generated chain, a corrupt file) would overflow the stack otherwise.
namespace {
struct ChainBuffer {   // the chain from a transform up to its root; no allocation for a normal depth
    const Transform* small[48];
    std::vector<const Transform*> big;
    std::size_t n = 0;
    void push(const Transform* t) {
        if (big.empty() && n < 48) { small[n++] = t; return; }
        if (big.empty()) big.assign(small, small + n);
        big.push_back(t);
        n = big.size();
    }
    const Transform* at(std::size_t i) const { return big.empty() ? small[i] : big[i]; }
};

// The inverse of an affine matrix; false when it has none (a collapsed axis, or not finite).
// Mat4::Inverse answers the identity then, which is not an inverse. Same relative test as Mat4::Inverse.
bool TryInvert(const Mat4& m, Mat4& out) {
    const float det = m.Determinant();
    float bound = 1.0f;
    for (int c = 0; c < 4; ++c) {
        const float* a = &m.m[4 * c];
        bound *= std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2] + a[3] * a[3]);
    }
    if (!(std::fabs(det) > 1e-9f * bound)) return false;
    out = m.Inverse();
    return true;
}

// Undo a scale axis by axis; an axis that is collapsed (0) has no way back and reads `whenCollapsed`.
float Unscale(float v, float scale, float whenCollapsed) { return std::fabs(scale) > 1e-12f ? v / scale : whenCollapsed; }
} // namespace

// Bring this one transform's own cache up to date; its parent's cache must be current.
const Mat4& Transform::RefreshWorld() const {
    std::uint64_t ps = 0;
    const Mat4* pw = nullptr;
    if (m_parent) { pw = &m_parent->m_world; ps = m_parent->m_wStamp; }
    if (m_wValid && m_wParent == m_parent && m_wParentStamp == ps &&
        std::memcmp(&m_wP, &localPosition, sizeof(Vec3)) == 0 &&
        std::memcmp(&m_wR, &localRotation, sizeof(Quat)) == 0 &&
        std::memcmp(&m_wS, &localScale, sizeof(Vec3)) == 0)
        return m_world;
    ++t_rebuilds;
    Mat4 local = Mat4::TRS(localPosition, localRotation, localScale);
    m_world = pw ? (*pw) * local : local;
    m_wRot = m_parent ? (m_parent->m_wRot * localRotation).Normalized() : localRotation;
    // Root first, as the chain walk multiplied it before the scale was cached: the same products in the same order.
    m_wScale = m_parent ? Vec3{m_parent->m_wScale.x * localScale.x, m_parent->m_wScale.y * localScale.y,
                               m_parent->m_wScale.z * localScale.z}
                        : localScale;
    m_wP = localPosition; m_wR = localRotation; m_wS = localScale;
    m_wParent = m_parent; m_wParentStamp = ps;
    m_wStamp = ++g_worldStamp;
    m_wValid = true;
    return m_world;
}

// True when every transform from this one to the root still matches the inputs its cache was built from and
// the stamp of the parent it was built under. Read-only, one loop, stops at the first level that differs.
bool Transform::ChainIsCurrent() const {
    std::uint64_t steps = 0;
    bool current = true;
    for (const Transform* t = this; t; t = t->m_parent) {
        ++steps;
        const Transform* p = t->m_parent;
        if (!t->m_wValid || t->m_wParent != p || (p && t->m_wParentStamp != p->m_wStamp) ||
            std::memcmp(&t->m_wP, &t->localPosition, sizeof(Vec3)) != 0 ||
            std::memcmp(&t->m_wR, &t->localRotation, sizeof(Quat)) != 0 ||
            std::memcmp(&t->m_wS, &t->localScale, sizeof(Vec3)) != 0) {
            current = false;
            break;
        }
    }
    t_walkSteps += steps;
    return current;
}

const Mat4& Transform::WorldCache() const {
    if (ChainIsCurrent()) return m_world;
    ChainBuffer chain;
    for (const Transform* t = this; t; t = t->m_parent) chain.push(t);
    t_walkSteps += chain.n;
    for (std::size_t i = chain.n; i-- > 1;) chain.at(i)->RefreshWorld();   // root first, down to this one's parent
    return RefreshWorld();
}

Mat4 Transform::LocalToWorldMatrix() const {
    return WorldCache();
}

Vec3 Transform::Position() const {
    if (!m_parent) return localPosition;
    return m_parent->WorldCache().MultiplyPoint(localPosition);
}

Quat Transform::Rotation() const {
    if (!m_parent) return localRotation;
    WorldCache();
    return m_wRot;
}

Vec3 Transform::LossyScale() const {
    if (!m_parent) return localScale;
    WorldCache();
    return m_wScale;
}

// Position, rotation and scale in the world with a single check of the parent chain (Position()
// + Rotation() + LossyScale() would check it three times).
void Transform::ReadWorldPose(Vec3& pos, Quat& rot, Vec3& scale) const {
    if (!m_parent) { pos = localPosition; rot = localRotation; scale = localScale; return; }
    pos = m_parent->WorldCache().MultiplyPoint(localPosition);
    RefreshWorld();   // the parent's cache is current now: this is one level
    rot = m_wRot;
    scale = m_wScale;
}

Vec3 Transform::WorldToParentLocal(const Vec3& worldPos) const {
    // The whole parent matrix is inverted, so a stretched grandparent above a turned parent (a shear,
    // which "rotation then scale" cannot undo) still lands the position exactly.
    Mat4 inv;
    if (TryInvert(m_parent->WorldCache(), inv)) return inv.MultiplyPoint(worldPos);
    // The parent has a collapsed axis: undo what can be undone, keep the local value on that axis.
    const Vec3 parentPos = m_parent->Position();
    const Quat invRotation = m_parent->Rotation().Conjugate();
    const Vec3 parentScale = m_parent->LossyScale();
    const Vec3 rel = invRotation * (worldPos - parentPos);
    return {Unscale(rel.x, parentScale.x, localPosition.x),
            Unscale(rel.y, parentScale.y, localPosition.y),
            Unscale(rel.z, parentScale.z, localPosition.z)};
}

Vec3 Transform::InverseTransformPoint(const Vec3& world) const {
    const Mat4& m = WorldCache();
    Mat4 inv;
    if (TryInvert(m, inv)) return inv.MultiplyPoint(world);
    // No inverse: the pseudo-inverse, in position, rotation and scale. A collapsed axis reads 0.
    const Vec3 rel = Rotation().Conjugate() * (world - Vec3{m.m[12], m.m[13], m.m[14]});
    const Vec3 sc = LossyScale();
    return {Unscale(rel.x, sc.x, 0.0f), Unscale(rel.y, sc.y, 0.0f), Unscale(rel.z, sc.z, 0.0f)};
}

Vec3 Transform::InverseTransformVector(const Vec3& world) const {
    Mat4 inv;
    if (TryInvert(WorldCache(), inv)) return inv.MultiplyVector(world);
    const Vec3 rel = Rotation().Conjugate() * world;
    const Vec3 sc = LossyScale();
    return {Unscale(rel.x, sc.x, 0.0f), Unscale(rel.y, sc.y, 0.0f), Unscale(rel.z, sc.z, 0.0f)};
}

void Transform::SetPosition(const Vec3& worldPos) {
    if (!m_parent) { localPosition = worldPos; return; }
    localPosition = WorldToParentLocal(worldPos);
}

void Transform::SetRotation(const Quat& worldRot) {
    if (!m_parent) { localRotation = worldRot.Normalized(); return; }
    // Convert world rotation into the parent's local space.
    localRotation = (m_parent->Rotation().Conjugate() * worldRot).Normalized();
}

void Transform::SetLossyScale(const Vec3& worldScale) {
    const Vec3 ps = m_parent ? m_parent->LossyScale() : Vec3::One;
    if (ps.x != 0) localScale.x = worldScale.x / ps.x;
    if (ps.y != 0) localScale.y = worldScale.y / ps.y;
    if (ps.z != 0) localScale.z = worldScale.z / ps.z;
}

void Transform::LookAt(const Vec3& worldTarget, const Vec3& worldUp) {
    const Vec3 dir = worldTarget - Position();
    if (dir.SqrMagnitude() < 1e-12f) return;
    SetRotation(Quat::LookRotation(dir, worldUp));
}

namespace {
bool FiniteVec(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// An up hint that is not parallel to `dir`: the hint itself, or a world axis when the direction runs along it.
Vec3 UsableUp(const Vec3& dir, const Vec3& up) {
    const Vec3 d = dir.Normalized();
    Vec3 u = FiniteVec(up) && up.SqrMagnitude() > 1e-12f ? up.Normalized() : Vec3::Up;
    if (Vec3::Cross(u, d).SqrMagnitude() > 1e-8f) return u;
    // Along the hint (or no hint): any axis that is not parallel to the direction will do; this one is fixed so
    // the answer does not change from frame to frame.
    const Vec3 alt = std::fabs(d.z) < 0.9f ? Vec3::Forward : Vec3::Right;
    return alt;
}
} // namespace

void Transform::LookDirection(const Vec3& worldDirection, const Vec3& worldUp) {
    if (!FiniteVec(worldDirection) || worldDirection.SqrMagnitude() < 1e-12f) return;
    SetRotation(Quat::LookRotation(worldDirection, UsableUp(worldDirection, worldUp)));
}

void Transform::LookAtYaw(const Vec3& worldTarget, const Vec3& worldUp) {
    const Vec3 dir = worldTarget - Position();
    if (!FiniteVec(dir) || !FiniteVec(worldUp) || worldUp.SqrMagnitude() < 1e-12f) return;
    const Vec3 up = worldUp.Normalized();
    const Vec3 level = dir - up * Vec3::Dot(dir, up);   // drop what runs along the up axis
    if (level.SqrMagnitude() < 1e-12f) return;           // straight above or below: no side to face
    SetRotation(Quat::LookRotation(level, up));
}

void Transform::LookAt2D(const Vec3& worldTarget, Facing2D facing) {
    const Vec3 pos = Position();
    const float dx = worldTarget.x - pos.x, dy = worldTarget.y - pos.y;
    if (!std::isfinite(dx) || !std::isfinite(dy) || dx * dx + dy * dy < 1e-12f) return;
    float degrees = std::atan2(dy, dx) * Mathf::Rad2Deg;
    if (facing == Facing2D::Up) degrees -= 90.0f;   // Up() is a quarter turn ahead of Right()
    SetRotation(Quat::AngleAxis(degrees, Vec3::Forward));
}

bool Transform::RotateTowards(const Vec3& worldTarget, float maxDegrees, const Vec3& worldUp) {
    const Vec3 dir = worldTarget - Position();
    if (!FiniteVec(dir) || dir.SqrMagnitude() < 1e-12f) return true;   // nothing to face: it already does
    const Quat want = Quat::LookRotation(dir, UsableUp(dir, worldUp)).Normalized();
    const Quat now = Rotation();
    constexpr float kFacing = 0.1f;   // degrees: float rounding in an angle between two nearly equal rotations is about 0.04
    if (Quat::Angle(now, want) < kFacing) return true;
    if (!(maxDegrees > 0.0f)) return false;                              // negative, zero or NaN turns nothing
    SetRotation(Quat::RotateTowards(now, want, maxDegrees));
    return Quat::Angle(Rotation(), want) < kFacing;
}

int Transform::GetSiblingIndex() const {
    if (m_parent) {
        const auto& kids = m_parent->Kids();   // settled: every child's slot is its position
        if (m_slot < kids.size() && kids[m_slot] == this) return static_cast<int>(m_slot);
        for (std::size_t i = 0; i < kids.size(); ++i)   // the slot is not trusted (it should always be): look it up
            if (kids[i] == this) return static_cast<int>(i);
        return -1;
    }
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc) return -1;
    int n = 0;
    for (const auto& up : sc->Objects()) {
        if (up.get() == gameObject) return n;
        if (!up->transform || !up->transform->Parent()) ++n;
    }
    return -1;
}

void Transform::SetSiblingIndex(int index) {
    const int cur = GetSiblingIndex();
    if (cur < 0) return;
    if (m_parent) {
        auto& kids = m_parent->m_children;   // settled by GetSiblingIndex
        index = std::clamp(index, 0, static_cast<int>(kids.size()) - 1);
        if (index == cur) return;
        kids.erase(kids.begin() + cur);
        kids.insert(kids.begin() + index, this);
        const std::size_t lo = static_cast<std::size_t>(std::min(cur, index)), hi = static_cast<std::size_t>(std::max(cur, index));
        for (std::size_t i = lo; i <= hi; ++i) kids[i]->m_slot = i;   // only the slots between the two positions moved
        t_childWork += hi - lo + 1;
        NoteOrderChanged();
        return;
    }
    Scene* sc = gameObject->scene();
    std::vector<GameObject*> roots = sc->Roots();
    index = std::clamp(index, 0, static_cast<int>(roots.size()) - 1);
    if (index == cur) return;
    sc->ReorderSibling(gameObject, roots[index], /*after=*/index > cur);
}

// ---- The child list ---------------------------------------------------------------

void Transform::ReindexChildren(std::size_t from) {
    for (std::size_t i = from; i < m_children.size(); ++i)
        if (m_children[i]) m_children[i]->m_slot = i;
    if (from < m_children.size()) t_childWork += m_children.size() - from;
}

void Transform::SettleChildren() const {
    std::lock_guard<std::mutex> lock(g_settleMutex);
    if (m_tombs.load(std::memory_order_acquire) == 0) return;   // another reader did it while this one waited
    auto& kids = const_cast<std::vector<Transform*>&>(m_children);
    std::size_t w = 0;
    for (std::size_t r = 0; r < kids.size(); ++r) {
        Transform* c = kids[r];
        if (!c) continue;
        kids[w] = c;
        c->m_slot = w;
        ++w;
    }
    t_childWork += kids.size();
    kids.resize(w);
    m_tombs.store(0, std::memory_order_release);
}

void Transform::DropChild(Transform* child) {
    auto& kids = m_children;
    std::size_t i = child->m_slot;
    if (i >= kids.size() || kids[i] != child) {   // the slot should always be right; if it is not, find it the slow way
        const auto it = std::find(kids.begin(), kids.end(), child);
        t_childWork += kids.size();
        if (it == kids.end()) return;
        i = static_cast<std::size_t>(it - kids.begin());
    }
    ++t_childWork;
    const std::uint32_t holes = m_tombs.load(std::memory_order_relaxed);
    if (i + 1 == kids.size()) {   // the last one: nothing to move (and the holes just before it go with it)
        kids.pop_back();
        std::uint32_t left = holes;
        while (!kids.empty() && !kids.back()) { kids.pop_back(); --left; ++t_childWork; }
        if (left != holes) m_tombs.store(left, std::memory_order_release);
        return;
    }
    if (holes == 0 && kids.size() < kHoleMin) {   // a short list: erase, as always
        kids.erase(kids.begin() + static_cast<std::ptrdiff_t>(i));
        t_childWork += kids.size() - i;
        ReindexChildren(i);
        return;
    }
    kids[i] = nullptr;   // a long list: leave a hole, sweep the holes out in bulk
    m_tombs.store(holes + 1, std::memory_order_release);
    if (static_cast<std::size_t>(holes + 1) * 2 > kids.size()) SettleChildren();
}

void Transform::MoveChild(Transform* child, int dir) {
    if (!child) return;
    SettleChildren();
    auto& kids = m_children;
    std::size_t i = child->m_slot;
    if (i >= kids.size() || kids[i] != child) {
        const auto it = std::find(kids.begin(), kids.end(), child);
        if (it == kids.end()) return;
        i = static_cast<std::size_t>(it - kids.begin());
    }
    const std::size_t j = (dir < 0) ? (i == 0 ? i : i - 1) : (i + 1 >= kids.size() ? i : i + 1);
    if (i == j) return;
    std::swap(kids[i], kids[j]);
    kids[i]->m_slot = i;
    kids[j]->m_slot = j;
    NoteOrderChanged();
}

void Transform::MoveChildToEdge(Transform* child, bool toFront) {
    SettleChildren();
    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it == m_children.end()) return;
    const std::size_t from = static_cast<std::size_t>(it - m_children.begin());
    m_children.erase(it);
    if (toFront) m_children.insert(m_children.begin(), child);
    else         m_children.push_back(child);
    ReindexChildren(toFront ? 0 : from);
    NoteOrderChanged();
}

void Transform::ReorderChild(Transform* child, Transform* anchor, bool after) {
    if (child == anchor) return;
    SettleChildren();
    auto rm = std::find(m_children.begin(), m_children.end(), child);
    if (rm == m_children.end()) return;
    m_children.erase(rm);
    auto at = std::find(m_children.begin(), m_children.end(), anchor);
    if (at == m_children.end()) m_children.push_back(child);
    else {
        if (after) ++at;
        m_children.insert(at, child);
    }
    ReindexChildren(0);
    NoteOrderChanged();
}

void Transform::NoteOrderChanged() {
    if (gameObject)   // UI draws in hierarchy order, so caches keyed on it re-sort
        if (Scene* sc = gameObject->scene()) sc->MarkStructureChanged();
}

bool Transform::SetParent(Transform* parent, bool worldPositionStays, int siblingIndex) {
    if (!SetParent(parent, worldPositionStays)) return false;
    SetSiblingIndex(siblingIndex);
    return true;
}

bool Transform::SetParent(Transform* parent, bool worldPositionStays) {
    if (parent == m_parent) return true;
    if (parent) {
        if (parent == this) return false;
        // A destroyed object is only a frozen shell waiting to be freed; a transform of
        // another scene would dangle when either scene goes first.
        if (parent->gameObject && parent->gameObject->IsDestroyed()) return false;
        if (gameObject && gameObject->IsDestroyed()) return false;
        if (gameObject && parent->gameObject && gameObject->scene() != parent->gameObject->scene()) return false;
        // Would the parent end up below this transform? Only a transform that has children can
        // be above anything, so a leaf (the usual case while a hierarchy is being built) skips the walk.
        if (ChildCount() > 0)
            for (const Transform* t = parent; t; t = t->m_parent)
                if (t == this) return false;   // a descendant: would form a cycle
    }
    // The world pose is read before the move and put back after it, only when it is wanted:
    // each read walks the whole parent chain (once, not once per number).
    Vec3 world, worldScale; Quat worldRot;
    if (worldPositionStays) ReadWorldPose(world, worldRot, worldScale);
    if (m_parent) m_parent->DropChild(this);   // no search, and no shifting of a long list (see DropChild)
    m_parent = parent;
    if (m_parent) { m_slot = m_parent->m_children.size(); m_parent->m_children.push_back(this); }
    detail::g_activeEpoch.fetch_add(1, std::memory_order_relaxed);   // what is above this object changed: its active state is worked out again
    if (gameObject)   // a reparent changes which bones / names a model's caches see
        if (Scene* sc = gameObject->scene()) sc->MarkStructureChanged();
    // Preserve the world pose (position, rotation and scale) only when asked.
    if (worldPositionStays) {
        // The same numbers SetPosition(world), SetRotation(worldRot) and the scale division give, with one check
        // of the new parent's chain instead of one for each of them.
        Vec3 ps = Vec3::One;
        if (m_parent) {
            const Mat4& parentWorld = m_parent->WorldCache();
            Mat4 inverse;
            localPosition = TryInvert(parentWorld, inverse) ? inverse.MultiplyPoint(world) : WorldToParentLocal(world);
            localRotation = (m_parent->m_wRot.Conjugate() * worldRot).Normalized();
            ps = m_parent->m_wScale;
        } else {
            localPosition = world;
            localRotation = worldRot.Normalized();
        }
        // Scale follows LossyScale(): the world scale divided by the new parent's. Under a
        // rotated parent with non-uniform scale a child turned relative to it would need a
        // shear, which a TRS transform can't hold: position and rotation stay exact and
        // the scale is the per-axis lossy one (an axis with zero parent scale is kept).
        if (ps.x != 0) localScale.x = worldScale.x / ps.x;
        if (ps.y != 0) localScale.y = worldScale.y / ps.y;
        if (ps.z != 0) localScale.z = worldScale.z / ps.z;
    }
    return true;
}

// ---- Lookups ----------------------------------------------------------------

Transform* Transform::Find(const std::string& path) const {
    const Transform* cur = this;
    std::size_t start = 0;
    bool any = false;
    while (start <= path.size() && cur) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string::npos ? path.size() : slash;
        if (end > start) {   // empty parts ("a//b", a leading or trailing '/') are skipped
            any = true;
            const std::string part = path.substr(start, end - start);
            const Transform* next = nullptr;
            for (const Transform* c : cur->Kids())
                if (c && c->gameObject && c->gameObject->name == part) { next = c; break; }
            cur = next;
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return any ? const_cast<Transform*>(cur) : nullptr;
}

Transform* Transform::FindDeep(const std::string& name) const {
    std::vector<const Transform*> stack;   // explicit stack: depth first, siblings in order, no recursion
    {
        const auto& kids = Kids();
        for (std::size_t i = kids.size(); i-- > 0;) stack.push_back(kids[i]);
    }
    while (!stack.empty()) {
        const Transform* t = stack.back(); stack.pop_back();
        if (!t) continue;
        if (t->gameObject && t->gameObject->name == name) return const_cast<Transform*>(t);
        const auto& kids = t->Kids();
        for (std::size_t i = kids.size(); i-- > 0;) stack.push_back(kids[i]);
    }
    return nullptr;
}

bool Transform::IsChildOf(const Transform* ancestor) const {
    if (!ancestor) return false;
    for (const Transform* t = this; t; t = t->m_parent)
        if (t == ancestor) return true;
    return false;
}

const Transform* Transform::Root() const {
    const Transform* t = this;
    while (t->m_parent) t = t->m_parent;
    return t;
}
Transform* Transform::Root() { return const_cast<Transform*>(static_cast<const Transform*>(this)->Root()); }

int Transform::Depth() const {
    int n = 0;
    for (const Transform* t = m_parent; t; t = t->m_parent) ++n;
    return n;
}

void Transform::DetachChildren() {
    SettleChildren();
    if (m_children.empty()) return;
    struct Pose { Vec3 position, scale; Quat rotation; };
    std::vector<Pose> poses;   // read while they are still attached, written once they are free: linear, not one list edit per child
    poses.reserve(m_children.size());
    for (const Transform* k : m_children) poses.push_back({k->Position(), k->LossyScale(), k->Rotation()});
    std::vector<Transform*> kids;
    kids.swap(m_children);
    for (std::size_t i = 0; i < kids.size(); ++i) {
        Transform* k = kids[i];
        k->m_parent = nullptr;
        k->localPosition = poses[i].position;
        k->localRotation = poses[i].rotation.Normalized();
        k->localScale = poses[i].scale;
    }
    t_childWork += kids.size();
    detail::g_activeEpoch.fetch_add(1, std::memory_order_relaxed);
    if (gameObject)
        if (Scene* sc = gameObject->scene()) sc->MarkStructureChanged();
}

bool Transform::PlaceNextTo(Transform* anchor, bool after) {
    if (!anchor || anchor == this || !gameObject || !anchor->gameObject) return false;
    Scene* sc = gameObject->scene();
    if (!sc || anchor->gameObject->scene() != sc) return false;
    if (anchor->IsChildOf(this)) return false;   // under this transform: it cannot become its sibling
    sc->ReorderSibling(gameObject, anchor->gameObject, after);
    return m_parent == anchor->m_parent;
}

// ---- Moving and turning in a chosen space -------------------------------------

void Transform::Translate(const Vec3& delta, Space space) {
    switch (space) {
        case Space::Local: localPosition += delta; break;
        case Space::World: SetPosition(Position() + delta); break;
        case Space::Self:  SetPosition(Position() + Rotation() * delta); break;
    }
}

void Transform::Rotate(const Vec3& eulerDegrees, Space space) {
    const Quat q = Quat::Euler(eulerDegrees);
    switch (space) {
        case Space::Self:  localRotation = (localRotation * q).Normalized(); break;
        case Space::Local: localRotation = (q * localRotation).Normalized(); break;
        case Space::World: SetRotation(q * Rotation()); break;
    }
}

void Transform::Rotate(const Vec3& axis, float degrees, Space space) {
    if (axis.SqrMagnitude() < 1e-12f) return;
    const Quat q = Quat::AngleAxis(degrees, axis.Normalized());
    switch (space) {
        case Space::Self:  localRotation = (localRotation * q).Normalized(); break;
        case Space::Local: localRotation = (q * localRotation).Normalized(); break;
        case Space::World: SetRotation(q * Rotation()); break;
    }
}

void Transform::RotateAround(const Vec3& point, const Vec3& axis, float degrees) {
    if (axis.SqrMagnitude() < 1e-12f) return;
    const Quat q = Quat::AngleAxis(degrees, axis.Normalized());
    const Vec3 pos = Position();
    const Quat rot = Rotation();
    SetPosition(point + q * (pos - point));
    SetRotation(q * rot);
}

} // namespace okay
