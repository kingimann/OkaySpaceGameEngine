#include "okay/Scene/Transform.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace okay {

namespace {
std::uint64_t g_worldStamp = 0;   // every recompute gets a new stamp (children compare it)
}

// World matrices are cached per transform. The local fields are public and get
// written directly all over the engine, so instead of dirty flags the cache keeps
// a copy of the inputs it was built from: same local TRS, same parent, and the
// parent's own cache unchanged (its stamp) -> the cached matrix is still right.
// Checking is a few compares per level; rebuilding was a TRS + matrix multiply
// per level, for every Position()/Rotation()/LocalToWorldMatrix() call.
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
    Mat4 local = Mat4::TRS(localPosition, localRotation, localScale);
    m_world = pw ? (*pw) * local : local;
    m_wRot = m_parent ? (m_parent->m_wRot * localRotation).Normalized() : localRotation;
    m_wP = localPosition; m_wR = localRotation; m_wS = localScale;
    m_wParent = m_parent; m_wParentStamp = ps;
    m_wStamp = ++g_worldStamp;
    m_wValid = true;
    return m_world;
}

const Mat4& Transform::WorldCache() const {
    if (!m_parent) return RefreshWorld();
    ChainBuffer chain;
    for (const Transform* t = this; t; t = t->m_parent) chain.push(t);
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
    ChainBuffer chain;   // a loop, not a recursion: the chain can be very deep. Multiplied root first, as it always was.
    for (const Transform* t = this; t; t = t->m_parent) chain.push(t);
    Vec3 out = chain.at(chain.n - 1)->localScale;
    for (std::size_t i = chain.n - 1; i-- > 0;) {
        const Vec3& sc = chain.at(i)->localScale;
        out = {out.x * sc.x, out.y * sc.y, out.z * sc.z};
    }
    return out;
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

int Transform::GetSiblingIndex() const {
    if (m_parent) {
        for (std::size_t i = 0; i < m_parent->m_children.size(); ++i)
            if (m_parent->m_children[i] == this) return static_cast<int>(i);
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
        auto& kids = m_parent->m_children;
        index = std::clamp(index, 0, static_cast<int>(kids.size()) - 1);
        if (index == cur) return;
        kids.erase(kids.begin() + cur);
        kids.insert(kids.begin() + index, this);
        NoteOrderChanged();
        return;
    }
    Scene* sc = gameObject->scene();
    std::vector<GameObject*> roots = sc->Roots();
    index = std::clamp(index, 0, static_cast<int>(roots.size()) - 1);
    if (index == cur) return;
    sc->ReorderSibling(gameObject, roots[index], /*after=*/index > cur);
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
        if (!m_children.empty())
            for (const Transform* t = parent; t; t = t->m_parent)
                if (t == this) return false;   // a descendant: would form a cycle
    }
    // The world pose is read before the move and put back after it, only when it is wanted:
    // each of these walks the whole parent chain.
    Vec3 world, worldScale; Quat worldRot;
    if (worldPositionStays) { world = Position(); worldRot = Rotation(); worldScale = LossyScale(); }
    if (m_parent) {
        auto& sib = m_parent->m_children;
        sib.erase(std::remove(sib.begin(), sib.end(), this), sib.end());
    }
    m_parent = parent;
    if (m_parent) m_parent->m_children.push_back(this);
    detail::g_activeEpoch.fetch_add(1, std::memory_order_relaxed);   // what is above this object changed: its active state is worked out again
    if (gameObject)   // a reparent changes which bones / names a model's caches see
        if (Scene* sc = gameObject->scene()) sc->MarkStructureChanged();
    // Preserve the world pose (position, rotation and scale) only when asked.
    if (worldPositionStays) {
        SetPosition(world);
        SetRotation(worldRot);
        // Scale follows LossyScale(): the world scale divided by the new parent's. Under a
        // rotated parent with non-uniform scale a child turned relative to it would need a
        // shear, which a TRS transform can't hold: position and rotation stay exact and
        // the scale is the per-axis lossy one (an axis with zero parent scale is kept).
        const Vec3 ps = m_parent ? m_parent->LossyScale() : Vec3::One;
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
            for (const Transform* c : cur->m_children)
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
    for (std::size_t i = m_children.size(); i-- > 0;) stack.push_back(m_children[i]);
    while (!stack.empty()) {
        const Transform* t = stack.back(); stack.pop_back();
        if (!t) continue;
        if (t->gameObject && t->gameObject->name == name) return const_cast<Transform*>(t);
        for (std::size_t i = t->m_children.size(); i-- > 0;) stack.push_back(t->m_children[i]);
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
