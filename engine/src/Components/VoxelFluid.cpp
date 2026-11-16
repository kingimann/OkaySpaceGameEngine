// Water and lava that flow into dug holes (VoxelTerrain, OFF unless `fluidOn`). Written from scratch: a plain cellular rule on the
// voxel lattice (a fluid cell falls, or slides down a diagonal, when the cells it moves through are open air), an active-cell
// queue so only cells that can still move cost anything, and a per-frame budget on how many cells are looked at.
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

int VoxelTerrain::FluidCells() const { return fluidCount_; }
int VoxelTerrain::FluidActiveCells() const { return (int)fluidActive_.size(); }

void VoxelTerrain::ClearFluid() {
    fluid_.clear(); fluidActive_.clear(); fluidCount_ = 0; ++fluidRev_;
}

bool VoxelTerrain::FluidAt(int x, int y, int z) const {
    return InBounds(x, y, z) && !fluid_.empty() && fluid_[(std::size_t)Index(x, y, z)] != 0;
}

void VoxelTerrain::WakeFluidBox(int x0, int y0, int z0, int x1, int y1, int z1) {
    if (fluid_.empty()) return;
    x0 = std::max(0, x0 - 1); y0 = std::max(0, y0 - 1); z0 = std::max(0, z0 - 1);
    x1 = std::min(nx - 1, x1 + 1); y1 = std::min(ny - 1, y1 + 2); z1 = std::min(nz - 1, z1 + 1);
    for (int z = z0; z <= z1; ++z) for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        const int idx = Index(x, y, z);
        if (fluid_[(std::size_t)idx]) fluidActive_.push_back(idx);
    }
}

int VoxelTerrain::PourFluid(const Vec3& local, int cells) {
    if (!fluidOn || cells <= 0 || nx < 3 || ny < 3 || nz < 3) return 0;
    if (fluid_.size() != density.size()) { fluid_.assign(density.size(), 0); fluidCount_ = 0; }
    const int cx = (int)std::floor((local.x + HalfX()) / voxelSize + 0.5f), cy = (int)std::floor(local.y / voxelSize + 0.5f),
              cz = (int)std::floor((local.z + HalfZ()) / voxelSize + 0.5f);
    if (!std::isfinite(local.x) || !std::isfinite(local.y) || !std::isfinite(local.z)) return 0;
    int put = 0;
    // fill outward from the point, lowest layers first inside each ring, so a pour pools instead of floating
    for (int r = 0; r <= 8 && put < cells; ++r)
        for (int y = cy - r; y <= cy + r && put < cells; ++y)
            for (int z = cz - r; z <= cz + r && put < cells; ++z)
                for (int x = cx - r; x <= cx + r && put < cells; ++x) {
                    if (std::max(std::abs(x - cx), std::max(std::abs(y - cy), std::abs(z - cz))) != r) continue;
                    if (!InBounds(x, y, z) || fluidCount_ >= fluidMaxCells) continue;
                    const int idx = Index(x, y, z);
                    if (density[(std::size_t)idx] > iso || fluid_[(std::size_t)idx]) continue;
                    fluid_[(std::size_t)idx] = 1; ++fluidCount_; ++put; fluidActive_.push_back(idx);
                }
    if (put) ++fluidRev_;
    return put;
}

int VoxelTerrain::FluidStep() {
    if (!fluidOn || fluid_.size() != density.size() || fluidActive_.empty()) return 0;
    int looked = 0, moved = 0;
    const std::size_t n = std::min<std::size_t>(fluidActive_.size(), (std::size_t)std::max(1, fluidBudget));
    std::vector<int> work(fluidActive_.begin(), fluidActive_.begin() + (std::ptrdiff_t)n);
    fluidActive_.erase(fluidActive_.begin(), fluidActive_.begin() + (std::ptrdiff_t)n);
    static const int dd[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    auto open = [&](int x, int y, int z) { return InBounds(x, y, z) && density[(std::size_t)Index(x, y, z)] <= iso && !fluid_[(std::size_t)Index(x, y, z)]; };
    for (int idx : work) {
        ++looked;
        if (!fluid_[(std::size_t)idx]) continue;
        const int x = idx % nx, y = (idx / nx) % ny, z = idx / (nx * ny);
        if (density[(std::size_t)idx] > iso) {                      // the ground was built over it: that fluid is gone
            fluid_[(std::size_t)idx] = 0; --fluidCount_; ++fluidRev_; continue;
        }
        int tx = -1, ty = -1, tz = -1;
        if (open(x, y - 1, z)) { tx = x; ty = y - 1; tz = z; }
        else {
            const int start = (int)(fluidTick_++ % 4u);
            for (int k = 0; k < 4 && tx < 0; ++k) {
                const int* d = dd[(start + k) % 4];
                if (open(x + d[0], y, z + d[1]) && open(x + d[0], y - 1, z + d[1])) { tx = x + d[0]; ty = y - 1; tz = z + d[1]; }
            }
        }
        if (tx < 0) continue;                                         // at rest: not looked at again until something changes near it
        const int t = Index(tx, ty, tz);
        fluid_[(std::size_t)idx] = 0; fluid_[(std::size_t)t] = 1;
        fluidActive_.push_back(t);
        if (y + 1 < ny && fluid_[(std::size_t)Index(x, y + 1, z)]) fluidActive_.push_back(Index(x, y + 1, z));
        for (const auto& d : dd) if (InBounds(x + d[0], y, z + d[1]) && fluid_[(std::size_t)Index(x + d[0], y, z + d[1])]) fluidActive_.push_back(Index(x + d[0], y, z + d[1]));
        ++moved; ++fluidRev_;
    }
    (void)looked;
    return moved;
}

void VoxelTerrain::RebuildFluidMesh() {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc) return;
    if (fluidCount_ <= 0) { if (fluidObj_) { sc->Destroy(fluidObj_); fluidObj_ = nullptr; } return; }
    if (!fluidObj_) {
        fluidObj_ = sc->CreateGameObject("Voxel Fluid");
        fluidObj_->transient = true;
        fluidObj_->transform->SetParent(gameObject->transform);
        fluidObj_->AddComponent<MeshRenderer>();
    }
    auto* mr = fluidObj_->GetComponent<MeshRenderer>();
    if (!mr) return;
    Mesh m; m.name = "VoxelFluid";
    const float h = voxelSize * 0.5f;
    auto isFluid = [&](int x, int y, int z) { return FluidAt(x, y, z); };
    auto solid = [&](int x, int y, int z) { return InBounds(x, y, z) && density[(std::size_t)Index(x, y, z)] > iso; };
    auto quad = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
        const int base = (int)m.vertices.size();
        m.vertices.push_back(a); m.vertices.push_back(b); m.vertices.push_back(c); m.vertices.push_back(d);
        for (int i : {0, 1, 2, 0, 2, 3}) m.triangles.push_back(base + i);
    };
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        if (!fluid_[(std::size_t)Index(x, y, z)]) continue;
        const Vec3 c = GridToLocal(x, y, z);
        auto free = [&](int ax, int ay, int az) { return !isFluid(ax, ay, az) && !solid(ax, ay, az); };
        if (free(x, y + 1, z)) quad({c.x - h, c.y + h, c.z - h}, {c.x - h, c.y + h, c.z + h}, {c.x + h, c.y + h, c.z + h}, {c.x + h, c.y + h, c.z - h});
        if (free(x, y - 1, z)) quad({c.x - h, c.y - h, c.z - h}, {c.x + h, c.y - h, c.z - h}, {c.x + h, c.y - h, c.z + h}, {c.x - h, c.y - h, c.z + h});
        if (free(x + 1, y, z)) quad({c.x + h, c.y - h, c.z - h}, {c.x + h, c.y + h, c.z - h}, {c.x + h, c.y + h, c.z + h}, {c.x + h, c.y - h, c.z + h});
        if (free(x - 1, y, z)) quad({c.x - h, c.y - h, c.z - h}, {c.x - h, c.y - h, c.z + h}, {c.x - h, c.y + h, c.z + h}, {c.x - h, c.y + h, c.z - h});
        if (free(x, y, z + 1)) quad({c.x - h, c.y - h, c.z + h}, {c.x + h, c.y - h, c.z + h}, {c.x + h, c.y + h, c.z + h}, {c.x - h, c.y + h, c.z + h});
        if (free(x, y, z - 1)) quad({c.x - h, c.y - h, c.z - h}, {c.x - h, c.y + h, c.z - h}, {c.x + h, c.y + h, c.z - h}, {c.x + h, c.y - h, c.z - h});
    }
    m.ComputeSmoothNormals();
    mr->mesh = std::move(m);
    mr->color = fluidColor;
    mr->unlit = fluidGlow;
}

void VoxelTerrain::UpdateFluid(float dt) {
    if (!fluidOn) { if (fluidObj_) ClearFluid(), RebuildFluidMesh(); return; }
    fluidAcc_ += dt * std::max(0.1f, fluidRate);
    int steps = 0;
    while (fluidAcc_ >= 1.0f && steps < 4) { fluidAcc_ -= 1.0f; FluidStep(); ++steps; }
    if (fluidAcc_ > 4.0f) fluidAcc_ = 0.0f;
    fluidMeshTimer_ += dt;
    if (fluidRev_ != fluidMeshRev_ && fluidMeshTimer_ >= 0.08f) { fluidMeshTimer_ = 0.0f; fluidMeshRev_ = fluidRev_; RebuildFluidMesh(); }
}

void VoxelTerrain::UpdateFootprints() {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc || !gameObject->transform || nx < 3) return;
    Transform* tt = gameObject->transform;
    std::vector<GameObject*> who;
    if (!footprintTag.empty()) who = sc->FindGameObjectsWithTag(footprintTag);
    if (footprintBodies) for (auto* rb : sc->FindObjectsOfType<Rigidbody3D>()) if (rb->gameObject && rb->gameObject != gameObject) who.push_back(rb->gameObject);
    if (who.size() > 64) who.resize(64);                       // a bound on the work per look
    EditOptions o; o.power = footprintPower;
    bool changed = false;
    for (GameObject* g : who) {
        if (!g || !g->transform || g == gameObject) continue;
        const Vec3 l = tt->InverseTransformPoint(g->transform->Position());
        if (!std::isfinite(l.x) || !std::isfinite(l.y) || !std::isfinite(l.z)) continue;
        float sy = 0.0f;
        if (!SurfaceY(l.x, l.z, sy)) continue;
        if (std::fabs(l.y - sy) > 2.5f * voxelSize + 1.5f) continue;   // in the air or far below: not walking on it
        const std::uint64_t id = g->InstanceId();
        Vec3* last = nullptr;
        for (auto& e : footLast_) if (e.first == id) { last = &e.second; break; }
        if (!last) { footLast_.emplace_back(id, l); continue; }
        const float dx = l.x - last->x, dz = l.z - last->z;
        if (dx * dx + dz * dz < footprintStep * footprintStep) continue;
        *last = l;
        // a dent whose centre sits just above the surface so only its lower cap bites
        const float amount = std::max(0.05f, footprintDepth * 4.0f);
        const float rad = std::max(footprintRadius, voxelSize * 1.1f);   // a print is never smaller than about one voxel, or it would miss every lattice point
        if (Edit({l.x, sy + rad * 0.3f, l.z}, rad, amount, o).changed) { changed = true; ++footprintCount; }
    }
    if (footLast_.size() > 256) footLast_.erase(footLast_.begin(), footLast_.begin() + 128);
    if (changed) Apply();
}

} // namespace okay
