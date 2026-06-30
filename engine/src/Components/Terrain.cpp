#include "okay/Components/Terrain.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Graphics/Image.hpp"

#include <cmath>
#include <algorithm>

namespace okay {

void Terrain::Resize(int res) {
    if (res < 1) res = 1;
    resolution = res;
    heights.assign((std::size_t)Dim() * Dim(), 0.0f);
}

float Terrain::GetHeight(int x, int z) const {
    if (x < 0 || z < 0 || x >= Dim() || z >= Dim()) return 0.0f;
    return heights[(std::size_t)z * Dim() + x];
}

void Terrain::SetHeight(int x, int z, float h) {
    if (x < 0 || z < 0 || x >= Dim() || z >= Dim()) return;
    heights[(std::size_t)z * Dim() + x] = h;
}

void Terrain::Flatten(float h) {
    for (auto& v : heights) v = h;
}

void Terrain::Smooth() {
    std::vector<float> out = heights;
    for (int z = 0; z < Dim(); ++z)
        for (int x = 0; x < Dim(); ++x) {
            float sum = 0.0f; int n = 0;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = x + dx, nz = z + dz;
                    if (nx < 0 || nz < 0 || nx >= Dim() || nz >= Dim()) continue;
                    sum += GetHeight(nx, nz); ++n;
                }
            out[(std::size_t)z * Dim() + x] = n ? sum / n : GetHeight(x, z);
        }
    heights.swap(out);
}

void Terrain::Randomize(float amount, unsigned seed) {
    Random rng(seed);
    for (auto& v : heights) v += rng.Range(-amount, amount);
}

void Terrain::Hills(int count, float maxHeight, unsigned seed) {
    Random rng(seed);
    for (int i = 0; i < count; ++i) {
        float cx = rng.Range(0.0f, (float)resolution);
        float cz = rng.Range(0.0f, (float)resolution);
        float r  = rng.Range(resolution * 0.08f, resolution * 0.3f);
        float h  = rng.Range(maxHeight * 0.3f, maxHeight);
        for (int z = 0; z < Dim(); ++z)
            for (int x = 0; x < Dim(); ++x) {
                float dx = x - cx, dz = z - cz;
                float d2 = dx * dx + dz * dz;
                float falloff = std::exp(-d2 / (2.0f * r * r));   // gaussian bump
                heights[(std::size_t)z * Dim() + x] += h * falloff;
            }
    }
}

// --- Fractal value noise (Perlin-like): smooth, tileable-ish hills in [0,1]. ---
static float ValueNoise(float x, float z, unsigned seed) {
    int xi = (int)std::floor(x), zi = (int)std::floor(z);
    float xf = x - xi, zf = z - zi;
    auto r = [&](int a, int b) {
        unsigned h = seed + 0x9E3779B9u;
        h ^= (unsigned)a * 0x85EBCA6Bu; h = (h ^ (h >> 13)) * 0xC2B2AE35u;
        h ^= (unsigned)b * 0x27D4EB2Fu; h = (h ^ (h >> 16));
        return (h & 0xFFFFu) / 65535.0f;
    };
    float u = xf * xf * (3.0f - 2.0f * xf), v = zf * zf * (3.0f - 2.0f * zf);
    float a = r(xi, zi), b = r(xi + 1, zi), c = r(xi, zi + 1), d = r(xi + 1, zi + 1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
static float Fractal(float x, float z, int octaves, unsigned seed) {
    float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += ValueNoise(x * freq, z * freq, seed + (unsigned)o * 1013u) * amp;
        norm += amp; amp *= 0.5f; freq *= 2.0f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;   // [0,1]
}

// Ridged multifractal: folds each octave into a sharp crease (1-|2n-1|) and
// squares it, so peaks become knife-edge ridgelines instead of rounded blobs —
// the look of real eroded mountain ranges and canyon walls.
static float RidgedFractal(float x, float z, int octaves, unsigned seed) {
    float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        float n = ValueNoise(x * freq, z * freq, seed + (unsigned)o * 1013u);
        float r = 1.0f - std::fabs(2.0f * n - 1.0f);   // crease at the midline
        r *= r;                                         // sharpen the ridge
        sum += r * amp;
        norm += amp; amp *= 0.5f; freq *= 2.0f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;   // [0,1]
}

// Domain warp: perturb the sample coordinates by a low-frequency noise field so
// straight features bend and meander, killing the grid-aligned regularity of raw
// fBm and giving organic, weather-beaten shapes.
static void DomainWarp(float& x, float& z, unsigned seed, float amount) {
    float wx = Fractal(x * 0.5f + 11.3f, z * 0.5f + 7.1f, 2, seed ^ 0xA53Cu) - 0.5f;
    float wz = Fractal(x * 0.5f + 3.7f,  z * 0.5f + 19.9f, 2, seed ^ 0x91E7u) - 0.5f;
    x += wx * amount;
    z += wz * amount;
}

void Terrain::Generate(int type, float amplitude, float frequency, int octaves, unsigned seed) {
    if (octaves < 1) octaves = 1;
    const int dim = Dim();
    const float cx = (dim - 1) * 0.5f, cz = (dim - 1) * 0.5f;
    const float maxr = std::sqrt(cx * cx + cz * cz) + 1e-3f;
    for (int z = 0; z < dim; ++z)
        for (int x = 0; x < dim; ++x) {
            float fx = (float)x / resolution * frequency;
            float fz = (float)z / resolution * frequency;
            float n = Fractal(fx, fz, octaves, seed);     // [0,1]
            float h = 0.0f;
            switch (type) {
                case 0:  h = std::pow(n, 1.6f) * amplitude; break;            // Mountains (sharp peaks)
                case 1:  h = n * amplitude * 0.6f; break;                     // Hills (rolling)
                case 2:  h = (n - 0.5f) * amplitude * 0.4f; break;            // Plains (gentle)
                case 3: {                                                     // Plateau (mesas)
                    float t = (n - 0.45f) / 0.25f; t = t < 0 ? 0 : (t > 1 ? 1 : t);
                    h = t * t * (3.0f - 2.0f * t) * amplitude; break;
                }
                case 4: {                                                     // Islands (radial falloff)
                    float dx = x - cx, dz = z - cz;
                    float d = std::sqrt(dx * dx + dz * dz) / maxr;            // 0 center .. 1 edge
                    float island = n - d * 1.15f;
                    h = (island > 0.0f ? island : 0.0f) * amplitude; break;
                }
                case 5: {                                                     // Ridged Mountains
                    float wx = fx, wz = fz;
                    DomainWarp(wx, wz, seed, 0.6f);                           // meandering ridges
                    float r = RidgedFractal(wx, wz, octaves, seed);
                    h = std::pow(r, 1.3f) * amplitude; break;
                }
                default: {                                                    // Canyons (carved channels)
                    float wx = fx, wz = fz;
                    DomainWarp(wx, wz, seed, 0.8f);                           // winding canyon walls
                    float r = RidgedFractal(wx, wz, octaves, seed);
                    // High plateau gouged by deep, narrow ridged channels.
                    float carved = 1.0f - std::pow(r, 0.7f);
                    h = carved * amplitude; break;
                }
            }
            heights[(std::size_t)z * dim + x] = h;
        }
}

float Terrain::SampleHeight(float localX, float localZ) const {
    float cell = CellSize(), half = size * 0.5f;
    float gx = (localX + half) / cell, gz = (localZ + half) / cell;
    int x0 = (int)std::floor(gx), z0 = (int)std::floor(gz);
    float fx = gx - x0, fz = gz - z0;
    float h00 = GetHeight(x0, z0), h10 = GetHeight(x0 + 1, z0);
    float h01 = GetHeight(x0, z0 + 1), h11 = GetHeight(x0 + 1, z0 + 1);
    return (h00 * (1 - fx) + h10 * fx) * (1 - fz) + (h01 * (1 - fx) + h11 * fx) * fz;
}

float Terrain::BrushWeight(float dist, float radius, float hardness) {
    if (radius <= 0.0f) return dist <= 0.0f ? 1.0f : 0.0f;
    if (dist >= radius) return 0.0f;
    if (hardness < 0.0f) hardness = 0.0f; if (hardness > 1.0f) hardness = 1.0f;
    float core = radius * hardness;            // full-strength inner disc
    if (dist <= core) return 1.0f;
    float t = 1.0f - (dist - core) / (radius - core);   // 1 at core edge .. 0 at rim
    return t * t * (3.0f - 2.0f * t);          // smoothstep skirt (soft, natural)
}

void Terrain::RaiseAt(float localX, float localZ, float radius, float delta, float hardness) {
    float cell = CellSize();
    float half = size * 0.5f;
    for (int z = 0; z < Dim(); ++z)
        for (int x = 0; x < Dim(); ++x) {
            float vx = x * cell - half, vz = z * cell - half;
            float dx = vx - localX, dz = vz - localZ;
            float d = std::sqrt(dx * dx + dz * dz);
            if (d > radius) continue;
            heights[(std::size_t)z * Dim() + x] += delta * BrushWeight(d, radius, hardness);
        }
}

void Terrain::SmoothAt(float localX, float localZ, float radius, float amount, float hardness) {
    float cell = CellSize(), half = size * 0.5f;
    std::vector<float> src = heights;   // read from a snapshot so it relaxes evenly
    auto at = [&](int x, int z) {
        if (x < 0) x = 0; if (z < 0) z = 0;
        if (x >= Dim()) x = Dim() - 1; if (z >= Dim()) z = Dim() - 1;
        return src[(std::size_t)z * Dim() + x];
    };
    for (int z = 0; z < Dim(); ++z)
        for (int x = 0; x < Dim(); ++x) {
            float vx = x * cell - half, vz = z * cell - half;
            float dx = vx - localX, dz = vz - localZ;
            float d = std::sqrt(dx * dx + dz * dz);
            if (d > radius) continue;
            // Wider 3x3 average for a stronger relax than the old 5-tap cross.
            float avg = (at(x, z) + at(x - 1, z) + at(x + 1, z) + at(x, z - 1) + at(x, z + 1)
                       + at(x - 1, z - 1) + at(x + 1, z - 1) + at(x - 1, z + 1) + at(x + 1, z + 1)) * (1.0f / 9.0f);
            float falloff = BrushWeight(d, radius, hardness) * amount;
            float& h = heights[(std::size_t)z * Dim() + x];
            h += (avg - h) * falloff;
        }
}

void Terrain::FlattenAt(float localX, float localZ, float radius, float target, float amount, float hardness) {
    float cell = CellSize(), half = size * 0.5f;
    for (int z = 0; z < Dim(); ++z)
        for (int x = 0; x < Dim(); ++x) {
            float vx = x * cell - half, vz = z * cell - half;
            float dx = vx - localX, dz = vz - localZ;
            float d = std::sqrt(dx * dx + dz * dz);
            if (d > radius) continue;
            float falloff = BrushWeight(d, radius, hardness) * amount;
            float& h = heights[(std::size_t)z * Dim() + x];
            h += (target - h) * falloff;
        }
}

void Terrain::NoiseAt(float localX, float localZ, float radius, float amount, unsigned seed, float hardness) {
    float cell = CellSize(), half = size * 0.5f;
    for (int z = 0; z < Dim(); ++z)
        for (int x = 0; x < Dim(); ++x) {
            float vx = x * cell - half, vz = z * cell - half;
            float dx = vx - localX, dz = vz - localZ;
            float d = std::sqrt(dx * dx + dz * dz);
            if (d > radius) continue;
            // Fractal bump centred on zero so it adds texture without a net rise.
            float n = Fractal((float)x * 0.35f, (float)z * 0.35f, 3, seed) - 0.5f;
            heights[(std::size_t)z * Dim() + x] += n * 2.0f * amount * BrushWeight(d, radius, hardness);
        }
}

void Terrain::ErodeAt(float localX, float localZ, float radius, float amount) {
    // A localized hydraulic pass: scatter droplets that start inside the brush so
    // only the patch under the cursor gets weathered.
    const int dim = Dim();
    if (dim < 3) return;
    float cell = CellSize(), half = size * 0.5f;
    int droplets = (int)(radius / std::fmax(cell, 1e-3f)) * 12 + 20;
    Random rng((unsigned)(localX * 131.0f + localZ * 977.0f) ^ 0xBEEF);
    auto idx = [&](int x, int z) { return (std::size_t)z * dim + x; };
    auto heightGrad = [&](float px, float pz, float& gx, float& gz) {
        int x = (int)px, z = (int)pz;
        if (x < 0) x = 0; if (z < 0) z = 0;
        if (x > dim - 2) x = dim - 2; if (z > dim - 2) z = dim - 2;
        float u = px - x, v = pz - z;
        float h00 = heights[idx(x, z)],     h10 = heights[idx(x + 1, z)];
        float h01 = heights[idx(x, z + 1)], h11 = heights[idx(x + 1, z + 1)];
        gx = (h10 - h00) * (1 - v) + (h11 - h01) * v;
        gz = (h01 - h00) * (1 - u) + (h11 - h10) * u;
        return (h00 * (1 - u) + h10 * u) * (1 - v) + (h01 * (1 - u) + h11 * u) * v;
    };
    auto deposit = [&](float px, float pz, float a) {
        int x = (int)px, z = (int)pz;
        if (x < 0 || z < 0 || x > dim - 2 || z > dim - 2) return;
        float u = px - x, v = pz - z;
        heights[idx(x, z)]     += a * (1 - u) * (1 - v);
        heights[idx(x + 1, z)] += a * u * (1 - v);
        heights[idx(x, z + 1)] += a * (1 - u) * v;
        heights[idx(x + 1, z + 1)] += a * u * v;
    };
    float erodeRate = 0.3f * amount;
    for (int d = 0; d < droplets; ++d) {
        // Start somewhere in the brush disc (local XZ -> grid coords).
        float ang = rng.Range(0.0f, 6.2831853f), rr = rng.Range(0.0f, radius);
        float lx = localX + std::cos(ang) * rr, lz = localZ + std::sin(ang) * rr;
        float px = (lx + half) / cell, pz = (lz + half) / cell;
        if (px < 1 || pz < 1 || px > dim - 2 || pz > dim - 2) continue;
        float dx = 0, dz = 0, speed = 1, water = 1, sediment = 0;
        for (int life = 0; life < dim; ++life) {
            float gx, gz; float hOld = heightGrad(px, pz, gx, gz);
            dx = dx * 0.05f - gx * 0.95f; dz = dz * 0.05f - gz * 0.95f;
            float len = std::sqrt(dx * dx + dz * dz); if (len < 1e-6f) break;
            dx /= len; dz /= len;
            float nx = px + dx, nz = pz + dz;
            if (nx < 1 || nz < 1 || nx > dim - 2 || nz > dim - 2) break;
            float ngx, ngz; float dh = heightGrad(nx, nz, ngx, ngz) - hOld;
            float cap = std::fmax(-dh, 0.01f) * speed * water * 4.0f;
            if (sediment > cap || dh > 0.0f) {
                float a = (dh > 0.0f) ? std::fmin(dh, sediment) : (sediment - cap) * 0.3f;
                deposit(px, pz, a); sediment -= a;
            } else {
                float a = std::fmin((cap - sediment) * erodeRate, -dh);
                deposit(px, pz, -a); sediment += a;
            }
            speed = std::sqrt(std::fmax(0.0f, speed * speed + (-dh) * 4.0f));
            water *= 0.98f; px = nx; pz = nz;
        }
    }
}

Vec3 Terrain::NormalAt(float localX, float localZ) const {
    float e = CellSize();
    float hl = SampleHeight(localX - e, localZ), hr = SampleHeight(localX + e, localZ);
    float hd = SampleHeight(localX, localZ - e), hu = SampleHeight(localX, localZ + e);
    // Central differences -> surface normal (Y up).
    Vec3 n{(hl - hr), 2.0f * e, (hd - hu)};
    float m = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    return m > 1e-6f ? Vec3{n.x / m, n.y / m, n.z / m} : Vec3{0, 1, 0};
}

void Terrain::Erode(int droplets, float strength, unsigned seed) {
    const int dim = Dim();
    if (dim < 3 || droplets <= 0) return;
    if (strength < 0.0f) strength = 0.0f;

    // Droplet ("particle") hydraulic erosion. Each drop flows downhill along the
    // height gradient, eroding where the slope is steep and the drop is moving,
    // depositing where it slows or climbs — carving channels and building fans.
    const float inertia      = 0.05f;   // 0 = follow gradient exactly, 1 = keep momentum
    const float capacityK    = 4.0f;    // how much sediment a fast drop can hold
    const float minSlope     = 0.01f;   // floor so flat ground still carries a little
    const float erodeRate    = 0.3f * strength;
    const float depositRate   = 0.3f;
    const float evaporate    = 0.02f;
    const float gravity      = 4.0f;
    const int   maxLifetime  = dim * 2;

    Random rng(seed);
    auto idx = [&](int x, int z) { return (std::size_t)z * dim + x; };

    // Height + gradient at a fractional grid position (bilinear over the cell).
    auto heightGrad = [&](float px, float pz, float& gx, float& gz) {
        int x = (int)px, z = (int)pz;
        if (x < 0) x = 0; if (z < 0) z = 0;
        if (x > dim - 2) x = dim - 2; if (z > dim - 2) z = dim - 2;
        float u = px - x, v = pz - z;
        float h00 = heights[idx(x, z)],     h10 = heights[idx(x + 1, z)];
        float h01 = heights[idx(x, z + 1)], h11 = heights[idx(x + 1, z + 1)];
        gx = (h10 - h00) * (1 - v) + (h11 - h01) * v;
        gz = (h01 - h00) * (1 - u) + (h11 - h10) * u;
        return (h00 * (1 - u) + h10 * u) * (1 - v) + (h01 * (1 - u) + h11 * u) * v;
    };
    // Distribute a height change to the 4 corners of the cell containing (px,pz).
    auto deposit = [&](float px, float pz, float amount) {
        int x = (int)px, z = (int)pz;
        if (x < 0 || z < 0 || x > dim - 2 || z > dim - 2) return;
        float u = px - x, v = pz - z;
        heights[idx(x, z)]     += amount * (1 - u) * (1 - v);
        heights[idx(x + 1, z)] += amount * u * (1 - v);
        heights[idx(x, z + 1)] += amount * (1 - u) * v;
        heights[idx(x + 1, z + 1)] += amount * u * v;
    };

    for (int d = 0; d < droplets; ++d) {
        float px = rng.Range(0.0f, (float)(dim - 1));
        float pz = rng.Range(0.0f, (float)(dim - 1));
        float dx = 0.0f, dz = 0.0f;     // direction
        float speed = 1.0f, water = 1.0f, sediment = 0.0f;

        for (int life = 0; life < maxLifetime; ++life) {
            float gx, gz;
            float hOld = heightGrad(px, pz, gx, gz);
            // Blend momentum with the downhill (negative gradient) direction.
            dx = dx * inertia - gx * (1.0f - inertia);
            dz = dz * inertia - gz * (1.0f - inertia);
            float len = std::sqrt(dx * dx + dz * dz);
            if (len < 1e-6f) break;                  // settled in a pit
            dx /= len; dz /= len;
            float nx = px + dx, nz = pz + dz;
            if (nx < 0 || nz < 0 || nx > dim - 1 || nz > dim - 1) break;   // ran off the edge

            float ngx, ngz;
            float hNew = heightGrad(nx, nz, ngx, ngz);
            float dh = hNew - hOld;                  // >0 = climbing

            float capacity = std::fmax(-dh, minSlope) * speed * water * capacityK;
            if (sediment > capacity || dh > 0.0f) {
                // Drop carries too much (or hit an upslope): deposit.
                float amount = (dh > 0.0f) ? std::fmin(dh, sediment)
                                           : (sediment - capacity) * depositRate;
                deposit(px, pz, amount);
                sediment -= amount;
            } else {
                // Erode, but never dig deeper than the step down (avoids spikes).
                float amount = std::fmin((capacity - sediment) * erodeRate, -dh);
                deposit(px, pz, -amount);
                sediment += amount;
            }
            speed = std::sqrt(std::fmax(0.0f, speed * speed + (-dh) * gravity));
            water *= (1.0f - evaporate);
            if (water < 0.01f) break;
            px = nx; pz = nz;
        }
    }
}

void Terrain::ThermalErode(int iterations, float talus, float strength) {
    const int dim = Dim();
    if (dim < 3 || iterations <= 0) return;
    if (strength < 0.0f) strength = 0.0f; if (strength > 1.0f) strength = 1.0f;
    if (talus <= 0.0f) talus = 0.01f;
    auto idx = [&](int x, int z) { return (std::size_t)z * dim + x; };
    static const int NX[4] = {1, -1, 0, 0};
    static const int NZ[4] = {0, 0, 1, -1};
    std::vector<float> delta(heights.size());

    for (int it = 0; it < iterations; ++it) {
        std::fill(delta.begin(), delta.end(), 0.0f);
        for (int z = 0; z < dim; ++z)
            for (int x = 0; x < dim; ++x) {
                float h = heights[idx(x, z)];
                // Total material above talus relative to lower neighbours.
                float over = 0.0f;
                float diff[4] = {0, 0, 0, 0};
                for (int k = 0; k < 4; ++k) {
                    int nx = x + NX[k], nz = z + NZ[k];
                    if (nx < 0 || nz < 0 || nx >= dim || nz >= dim) continue;
                    float d = h - heights[idx(nx, nz)];
                    if (d > talus) { diff[k] = d - talus; over += diff[k]; }
                }
                if (over <= 0.0f) continue;
                // Move a fraction of the excess, split toward the steeper drops.
                float move = over * 0.5f * strength;
                delta[idx(x, z)] -= move;
                for (int k = 0; k < 4; ++k) {
                    if (diff[k] <= 0.0f) continue;
                    int nx = x + NX[k], nz = z + NZ[k];
                    delta[idx(nx, nz)] += move * (diff[k] / over);
                }
            }
        for (std::size_t i = 0; i < heights.size(); ++i) heights[i] += delta[i];
    }
}

void Terrain::HeightRange(float& lo, float& hi) const {
    lo = 0.0f; hi = 0.0f;
    if (heights.empty()) return;
    lo = hi = heights[0];
    for (float h : heights) { if (h < lo) lo = h; if (h > hi) hi = h; }
}

void Terrain::Terrace(int steps) {
    if (steps < 2 || heights.empty()) return;
    float lo, hi; HeightRange(lo, hi);
    float range = hi - lo;
    if (range < 1e-5f) return;
    float n = (float)(steps - 1);
    for (float& h : heights) {
        float t = (h - lo) / range;                  // 0..1
        h = lo + (std::round(t * n) / n) * range;     // snap to the nearest band
    }
}

float Terrain::SlopeAt(float localX, float localZ) const {
    Vec3 nrm = NormalAt(localX, localZ);
    float c = nrm.y;                                  // cos(angle from straight up)
    if (c > 1.0f) c = 1.0f; if (c < -1.0f) c = -1.0f;
    return std::acos(c) * 57.29577951308232f;         // radians -> degrees
}

void Terrain::Normalize(float lowY, float highY) {
    if (heights.empty()) return;
    float lo, hi; HeightRange(lo, hi);
    float range = hi - lo;
    if (range < 1e-6f) { for (float& h : heights) h = lowY; return; }
    float scale = (highY - lowY) / range;
    for (float& h : heights) h = lowY + (h - lo) * scale;
}

void Terrain::Invert() {
    if (heights.empty()) return;
    float lo, hi; HeightRange(lo, hi);
    float mid = (lo + hi) * 0.5f;
    for (float& h : heights) h = 2.0f * mid - h;       // reflect about the mid elevation
}

bool Terrain::ExportHeightmap(const std::string& path) const {
    const int dim = Dim();
    if (dim < 1) return false;
    float lo, hi; HeightRange(lo, hi);
    float range = hi - lo;
    Image img(dim, dim);
    for (int z = 0; z < dim; ++z)
        for (int x = 0; x < dim; ++x) {
            float g = range > 1e-6f ? (GetHeight(x, z) - lo) / range : 0.0f;
            img.SetPixel(x, z, Color(g, g, g, 1.0f));
        }
    return img.SavePNG(path);
}

bool Terrain::ImportHeightmap(const std::string& path, float lowY, float highY) {
    Image img;
    if (!img.Load(path) || !img.Valid()) return false;
    const int dim = Dim();
    for (int z = 0; z < dim; ++z)
        for (int x = 0; x < dim; ++x) {
            float u = (dim > 1) ? (float)x / (dim - 1) : 0.0f;
            float v = (dim > 1) ? (float)z / (dim - 1) : 0.0f;
            Color c = img.Sample(u, v);                 // resample to our resolution
            float g = (c.r + c.g + c.b) * (1.0f / 3.0f); // luminance (grayscale heightmap)
            heights[(std::size_t)z * dim + x] = lowY + g * (highY - lowY);
        }
    return true;
}

Mesh Terrain::BuildMesh() const {
    Mesh m;
    m.name = "Terrain";
    const int dim = Dim();
    const float cell = CellSize();
    const float half = size * 0.5f;
    m.vertices.reserve((std::size_t)dim * dim);
    m.uvs.reserve((std::size_t)dim * dim);
    for (int z = 0; z < dim; ++z)
        for (int x = 0; x < dim; ++x) {
            m.vertices.push_back({x * cell - half, GetHeight(x, z), z * cell - half});
            m.uvs.push_back({(float)x / (dim - 1), (float)z / (dim - 1)});
        }
    m.triangles.reserve((std::size_t)resolution * resolution * 6);
    if (autoColor) m.triColors.reserve((std::size_t)resolution * resolution * 2);
    // Pick a layer color for a face from its elevation + steepness.
    auto faceColor = [&](int a, int b, int c) {
        const Vec3& va = m.vertices[a]; const Vec3& vb = m.vertices[b]; const Vec3& vc = m.vertices[c];
        float y = (va.y + vb.y + vc.y) * (1.0f / 3.0f);
        Vec3 n = Vec3::Cross(vb - va, vc - va).Normalized();
        float slope = 1.0f - std::fabs(n.y);                 // 0 flat .. 1 vertical
        if (y <= waterLevel) return waterColor;
        if (y <= waterLevel + std::fmax(0.4f, snowLevel * 0.06f)) return sandColor;
        if (slope >= rockSlope) return rockColor;
        if (y >= snowLevel) return snowColor;
        // Blend grass->rock as the slope approaches the rock threshold for a
        // softer transition.
        float t = slope / rockSlope; t = t < 0 ? 0 : (t > 1 ? 1 : t);
        return Color{grassColor.r + (rockColor.r - grassColor.r) * t * 0.5f,
                     grassColor.g + (rockColor.g - grassColor.g) * t * 0.5f,
                     grassColor.b + (rockColor.b - grassColor.b) * t * 0.5f, 1.0f};
    };
    for (int z = 0; z < resolution; ++z)
        for (int x = 0; x < resolution; ++x) {
            int i00 = z * dim + x, i10 = z * dim + x + 1;
            int i01 = (z + 1) * dim + x, i11 = (z + 1) * dim + x + 1;
            // Wound so the face normal points up (+Y).
            m.triangles.push_back(i00); m.triangles.push_back(i01); m.triangles.push_back(i10);
            m.triangles.push_back(i10); m.triangles.push_back(i01); m.triangles.push_back(i11);
            if (autoColor) {
                m.triColors.push_back(faceColor(i00, i01, i10));
                m.triColors.push_back(faceColor(i10, i01, i11));
            }
        }
    m.ComputeSmoothNormals();   // smooth (Gouraud) shading for rolling hills
    return m;
}

void Terrain::Apply() {
    if (!gameObject) return;
    auto* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) mr = gameObject->AddComponent<MeshRenderer>();
    mr->mesh = BuildMesh();
    mr->color = color;
    mr->doubleSided = true;   // visible from above and below regardless of winding
    // Ground texture via the material pipeline. Triplanar keeps cliffs from smearing;
    // tiling repeats the texture across the surface. Auto-colours (per-face) tint it.
    mr->texture = texture;
    if (!texture.empty()) {
        mr->tiling = {textureTiling, textureTiling};
        mr->triplanar = triplanarTex;
    } else {
        mr->triplanar = false;
    }
    mr->normalMap = normalMap;
    mr->normalStrength = normalStrength;
}

} // namespace okay
