// An endless voxel world streamed in chunks. See VoxelWorld.hpp and docs/engine/world_streaming.md.
//
// The ground function is our own: value noise on an integer lattice (a hash of the lattice point gives a
// value in [0, 1), blended with the 6t^5 - 15t^4 + 10t^3 fade), summed over octaves that halve in size
// (fractal Brownian motion). Caves carve where a 3D noise field sits close to its middle value, the same rule
// VoxelTerrain::Generate uses on its own grid.
#include "okay/Components/VoxelWorld.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/StreamingSource.hpp"
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Optimize/ChunkStreamer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace okay {

using optimize::CellCoord;

namespace {

/// Everything the ground function needs, copied so a worker can use it while the component changes.
struct GroundParams {
    float voxelSize = 1.0f, groundHeight = 12.0f, hills = 8.0f, featureSize = 48.0f, caves = 0.0f;
    std::uint32_t seed = 1;
    int chunkVoxels = 16, heightVoxels = 32;
};

std::uint32_t Hash3(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed) {
    std::uint32_t h = (std::uint32_t)x * 0x3A8F05C5u ^ (std::uint32_t)y * 0x6F4D2B19u ^ (std::uint32_t)z * 0x2C1B3C6Du ^ seed * 0x297A2D39u;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}
float Unit(std::uint32_t h) { return (float)(h >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
float Lerp(float a, float b, float t) { return a + (b - a) * t; }

float Value2(float x, float z, std::uint32_t seed) {
    const float fx = std::floor(x), fz = std::floor(z);
    const std::int32_t ix = (std::int32_t)fx, iz = (std::int32_t)fz;
    const float tx = Fade(x - fx), tz = Fade(z - fz);
    const float a = Unit(Hash3(ix, 0, iz, seed)), b = Unit(Hash3(ix + 1, 0, iz, seed));
    const float c = Unit(Hash3(ix, 0, iz + 1, seed)), d = Unit(Hash3(ix + 1, 0, iz + 1, seed));
    return Lerp(Lerp(a, b, tx), Lerp(c, d, tx), tz);
}

float Value3(float x, float y, float z, std::uint32_t seed) {
    const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const std::int32_t ix = (std::int32_t)fx, iy = (std::int32_t)fy, iz = (std::int32_t)fz;
    const float tx = Fade(x - fx), ty = Fade(y - fy), tz = Fade(z - fz);
    float v[2][2];
    for (int dz = 0; dz < 2; ++dz)
        for (int dy = 0; dy < 2; ++dy)
            v[dz][dy] = Lerp(Unit(Hash3(ix, iy + dy, iz + dz, seed)), Unit(Hash3(ix + 1, iy + dy, iz + dz, seed)), tx);
    return Lerp(Lerp(v[0][0], v[0][1], ty), Lerp(v[1][0], v[1][1], ty), tz);
}

/// Octaves of value noise, each half the size and half the weight of the one before; result in [0, 1).
float Fbm2(float x, float z, int octaves, std::uint32_t seed) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * Value2(x, z, seed + (std::uint32_t)o * 101u);
        norm += amp;
        amp *= 0.5f; x *= 2.0f; z *= 2.0f;
    }
    return sum / norm;
}
float Fbm3(float x, float y, float z, int octaves, std::uint32_t seed) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * Value3(x, y, z, seed + (std::uint32_t)o * 131u);
        norm += amp;
        amp *= 0.5f; x *= 2.0f; y *= 2.0f; z *= 2.0f;
    }
    return sum / norm;
}

float Ground(const GroundParams& p, float x, float z) {
    const float f = 1.0f / std::max(1.0f, p.featureSize);
    return p.groundHeight + (Fbm2(x * f, z * f, 4, p.seed) - 0.5f) * 2.0f * p.hills;
}

float Density(const GroundParams& p, float x, float y, float z) {
    const float surf = Ground(p, x, z);
    float d = surf - y;
    if (p.caves > 0.0f && y < surf - p.voxelSize * 1.5f) {
        const float f = 1.0f / std::max(1.0f, p.featureSize * 0.4f);
        const float worm = std::fabs(Fbm3(x * f, y * f, z * f, 3, p.seed ^ 0x5AB3u) - 0.5f);
        if (worm < 0.07f * (0.4f + std::min(1.0f, p.caves))) d = -p.voxelSize;
    }
    const float c = p.voxelSize * 3.0f;           // VoxelTerrain::Clamp: the band the field is kept in
    return d < -c ? -c : (d > c ? c : d);
}

struct ChunkData {
    int nx = 0, ny = 0, nz = 0;
    std::vector<float> density;
    std::vector<unsigned char> material;
};

}  // namespace

struct VoxelWorld::Impl {
    optimize::ChunkStreamer streamer;
    std::map<CellCoord, std::uint64_t> objects;   // chunk -> its object
    GroundParams params;
    bool configured = false;
    std::uint64_t sig = 0;
};

VoxelWorld::VoxelWorld() : m(std::make_unique<Impl>()) {}
VoxelWorld::~VoxelWorld() = default;

float VoxelWorld::GroundAt(float x, float z) const {
    GroundParams p = m->params;
    if (!m->configured) {
        p.voxelSize = voxelSize; p.groundHeight = groundHeight; p.hills = hills; p.featureSize = featureSize;
        p.caves = caves; p.seed = (std::uint32_t)seed;
    }
    return Ground(p, x, z);
}

float VoxelWorld::DensityAt(const Vec3& local) const {
    GroundParams p = m->params;
    if (!m->configured) {
        p.voxelSize = voxelSize; p.groundHeight = groundHeight; p.hills = hills; p.featureSize = featureSize;
        p.caves = caves; p.seed = (std::uint32_t)seed;
    }
    return Density(p, local.x, local.y, local.z);
}

void VoxelWorld::Configure() {
    GroundParams p;
    p.voxelSize = voxelSize > 0.05f ? voxelSize : 0.05f;
    p.groundHeight = groundHeight; p.hills = hills < 0.0f ? 0.0f : hills;
    p.featureSize = featureSize; p.caves = caves < 0.0f ? 0.0f : caves;
    p.seed = (std::uint32_t)seed;
    p.chunkVoxels = std::max(2, std::min(64, chunkVoxels));
    p.heightVoxels = std::max(2, std::min(256, heightVoxels));
    // A change to the shape of the world starts it again (the chunks and the remembered edits no longer fit).
    std::uint64_t sig = 1469598103934665603ull;
    auto mix = [&sig](double v) { sig = (sig ^ (std::uint64_t)std::llround(v * 1000.0)) * 1099511628211ull; };
    mix(p.voxelSize); mix(p.groundHeight); mix(p.hills); mix(p.featureSize); mix(p.caves); mix(p.seed);
    mix(p.chunkVoxels); mix(p.heightVoxels);
    if (m->configured && sig != m->sig) {
        m->streamer.Clear();
        m->streamer.ForgetSaved();
    }
    m->sig = sig;
    m->params = p;
    m->configured = true;

    optimize::ChunkStreamerConfig cfg;
    cfg.chunkSize = p.chunkVoxels * p.voxelSize;
    cfg.plane = optimize::GridPlane::XZ;
    cfg.loadRadius = viewDistance < 0.0f ? 0.0f : viewDistance;
    cfg.unloadRadius = std::max(cfg.loadRadius, unloadDistance);
    cfg.maxRequestsPerFrame = 4;
    cfg.maxInFlight = 8;
    cfg.maxAttachPerFrame = std::max(1, maxChunksPerFrame);
    cfg.attachBudgetMs = meshBudgetMs < 0.0f ? 0.0f : meshBudgetMs;
    cfg.viewDirectionWeight = 0.5f;
    m->streamer.SetConfig(cfg);

    optimize::ChunkHooks h;
    const GroundParams gp = p;
    h.generate = [gp](const CellCoord& c) -> optimize::ChunkPayload {
        auto d = std::make_shared<ChunkData>();
        d->nx = gp.chunkVoxels + 1; d->ny = gp.heightVoxels + 1; d->nz = gp.chunkVoxels + 1;
        d->density.resize((std::size_t)d->nx * d->ny * d->nz);
        const float w = gp.chunkVoxels * gp.voxelSize;
        const float x0 = c.x * w, z0 = c.z * w;
        std::size_t i = 0;
        for (int z = 0; z < d->nz; ++z)
            for (int y = 0; y < d->ny; ++y)
                for (int x = 0; x < d->nx; ++x)
                    d->density[i++] = Density(gp, x0 + x * gp.voxelSize, y * gp.voxelSize, z0 + z * gp.voxelSize);
        // (index = x + y * nx + z * nx * ny, VoxelTerrain's order)
        return d;
    };
    h.attach = [this](const CellCoord& c, optimize::ChunkPayload& payload) {
        Scene* s = GetScene();
        if (!s || !gameObject) return false;
        auto data = std::static_pointer_cast<ChunkData>(payload);
        const GroundParams& gp = m->params;
        if (data->nx != gp.chunkVoxels + 1 || data->ny != gp.heightVoxels + 1) return true;   // from an older shape: skip
        GameObject* g = s->CreateGameObject("Voxel Chunk " + std::to_string(c.x) + "," + std::to_string(c.z));
        g->transient = true;
        g->transform->SetParent(gameObject->transform, false);
        const float w = gp.chunkVoxels * gp.voxelSize;
        g->transform->localPosition = Vec3{(c.x + 0.5f) * w, 0.0f, (c.z + 0.5f) * w};
        auto* vt = g->AddComponent<VoxelTerrain>();
        vt->voxelSize = gp.voxelSize;
        vt->snowLevel = gp.groundHeight + gp.hills * 0.8f;
        vt->Resize(data->nx, data->ny, data->nz);
        vt->density = std::move(data->density);
        if (!data->material.empty()) { vt->EnableMaterials(); vt->material = std::move(data->material); }
        vt->Apply();
        m->objects[c] = g->InstanceId();
        return true;
    };
    h.detach = [this](const CellCoord& c) -> optimize::ChunkPayload {
        auto f = m->objects.find(c);
        if (f == m->objects.end()) return nullptr;
        GameObject* g = GameObject::FromInstanceId(f->second);
        m->objects.erase(f);
        if (!g) return nullptr;
        optimize::ChunkPayload keep;
        VoxelTerrain* vt = g->GetComponent<VoxelTerrain>();
        if (vt && rememberEdits && vt->editCount > 0) {
            auto d = std::make_shared<ChunkData>();
            d->nx = vt->nx; d->ny = vt->ny; d->nz = vt->nz;
            d->density = vt->density;
            if (vt->UseMaterials()) d->material = vt->material;
            keep = d;
        }
        Scene* s = GetScene();
        if (s && !g->IsDestroyed() && g->scene() == s) s->Destroy(g);
        return keep;
    };
    m->streamer.SetHooks(std::move(h));
}

namespace {
std::vector<optimize::StreamingViewer> LocalSources(const VoxelWorld& w, const std::string& target, bool follow) {
    std::vector<optimize::StreamingViewer> out;
    Scene* s = w.GetScene();
    if (!s || !w.gameObject || !w.gameObject->transform) return out;
    const Vec3 origin = w.gameObject->transform->Position();
    Transform* t = nullptr;
    if (!target.empty()) { if (GameObject* go = s->Find(target)) t = go->transform; }
    else if (s->mainCamera && s->mainCamera->gameObject) t = s->mainCamera->gameObject->transform;
    if (t) out.push_back({t->Position() - origin, t->Forward()});
    if (follow) for (const auto& p : StreamingSource::Collect(s)) out.push_back({p.position - origin, p.direction});
    return out;
}
}  // namespace

void VoxelWorld::Start() { SyncNow(); }

void VoxelWorld::SyncNow() {
    Configure();
    const auto views = LocalSources(*this, target, followSources);
    if (!views.empty()) m->streamer.Flush(views);
}

void VoxelWorld::Update(float) {
    Configure();
    const auto views = LocalSources(*this, target, followSources);
    if (views.empty()) return;
    m->streamer.Update(views);
}

void VoxelWorld::OnDestroy() {
    m->streamer.Clear();
    m->objects.clear();
}

int VoxelWorld::LoadedChunkCount() const { return m->streamer.LoadedCount(); }
bool VoxelWorld::IsChunkLoaded(int cx, int cz) const { return m->streamer.IsLoaded(CellCoord{cx, 0, cz}); }
int VoxelWorld::RememberedChunkCount() const { return (int)m->streamer.SavedChunks().size(); }

VoxelTerrain* VoxelWorld::ChunkAt(const Vec3& world) const {
    if (!gameObject || !gameObject->transform) return nullptr;
    const CellCoord c = m->streamer.ChunkOf(world - gameObject->transform->Position());
    auto f = m->objects.find(CellCoord{c.x, 0, c.z});
    if (f == m->objects.end()) return nullptr;
    GameObject* g = GameObject::FromInstanceId(f->second);
    return g ? g->GetComponent<VoxelTerrain>() : nullptr;
}

VoxelWorld::Stats VoxelWorld::GetStats() const {
    const optimize::ChunkStats c = m->streamer.Stats();
    Stats s;
    s.loaded = c.loaded; s.generating = c.generating; s.waiting = c.ready; s.remembered = c.saved;
    s.generated = c.generated; s.attached = c.attached; s.detached = c.detached;
    s.generateMs = c.generateMs; s.lastMeshMs = c.lastAttachMs; s.maxMeshMs = c.maxAttachMs;
    return s;
}

std::string VoxelWorld::DebugText() const {
    const Stats s = GetStats();
    std::ostringstream o;
    o << "voxel world: " << s.loaded << " chunks, " << s.generating << " generating, " << s.waiting << " waiting, "
      << s.remembered << " dug and remembered";
    return o.str();
}

} // namespace okay
