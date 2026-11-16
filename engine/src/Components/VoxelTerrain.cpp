#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/LODGroup.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Occluder.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <type_traits>

namespace okay {

// ===================== Noise (matches Terrain's value-noise feel) ===========
static float VN(int a, int b, int c, unsigned seed) {
    unsigned h = seed + 0x9E3779B9u;
    h ^= (unsigned)a * 0x85EBCA6Bu; h = (h ^ (h >> 13)) * 0xC2B2AE35u;
    h ^= (unsigned)b * 0x27D4EB2Fu; h = (h ^ (h >> 16)) * 0x165667B1u;
    h ^= (unsigned)c * 0x9E3779B1u; h = (h ^ (h >> 15));
    return (h & 0xFFFFu) / 65535.0f;
}
static float Smooth3(float x, float y, float z, unsigned seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
    float xf = x - xi, yf = y - yi, zf = z - zi;
    auto f = [](float t) { return t * t * (3.0f - 2.0f * t); };
    float u = f(xf), v = f(yf), w = f(zf);
    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
    float c000 = VN(xi, yi, zi, seed),       c100 = VN(xi + 1, yi, zi, seed);
    float c010 = VN(xi, yi + 1, zi, seed),   c110 = VN(xi + 1, yi + 1, zi, seed);
    float c001 = VN(xi, yi, zi + 1, seed),   c101 = VN(xi + 1, yi, zi + 1, seed);
    float c011 = VN(xi, yi + 1, zi + 1, seed), c111 = VN(xi + 1, yi + 1, zi + 1, seed);
    return L(L(L(c000, c100, u), L(c010, c110, u), v),
             L(L(c001, c101, u), L(c011, c111, u), v), w);
}
static float Fractal3(float x, float y, float z, int oct, unsigned seed) {
    float sum = 0, amp = 1, freq = 1, norm = 0;
    for (int o = 0; o < oct; ++o) {
        sum += Smooth3(x * freq, y * freq, z * freq, seed + (unsigned)o * 1013u) * amp;
        norm += amp; amp *= 0.5f; freq *= 2.0f;
    }
    return norm > 0 ? sum / norm : 0;
}
static float Fractal2(float x, float z, int oct, unsigned seed) {
    return Fractal3(x, 0.0f, z, oct, seed);   // y=0 slice reads as 2D noise
}

// ===================== Marching cubes tables (Paul Bourke) ==================
static const int kEdge[256] = {
0x0,0x109,0x203,0x30a,0x406,0x50f,0x605,0x70c,0x80c,0x905,0xa0f,0xb06,0xc0a,0xd03,0xe09,0xf00,
0x190,0x99,0x393,0x29a,0x596,0x49f,0x795,0x69c,0x99c,0x895,0xb9f,0xa96,0xd9a,0xc93,0xf99,0xe90,
0x230,0x339,0x33,0x13a,0x636,0x73f,0x435,0x53c,0xa3c,0xb35,0x83f,0x936,0xe3a,0xf33,0xc39,0xd30,
0x3a0,0x2a9,0x1a3,0xaa,0x7a6,0x6af,0x5a5,0x4ac,0xbac,0xaa5,0x9af,0x8a6,0xfaa,0xea3,0xda9,0xca0,
0x460,0x569,0x663,0x76a,0x66,0x16f,0x265,0x36c,0xc6c,0xd65,0xe6f,0xf66,0x86a,0x963,0xa69,0xb60,
0x5f0,0x4f9,0x7f3,0x6fa,0x1f6,0xff,0x3f5,0x2fc,0xdfc,0xcf5,0xfff,0xef6,0x9fa,0x8f3,0xbf9,0xaf0,
0x650,0x759,0x453,0x55a,0x256,0x35f,0x55,0x15c,0xe5c,0xf55,0xc5f,0xd56,0xa5a,0xb53,0x859,0x950,
0x7c0,0x6c9,0x5c3,0x4ca,0x3c6,0x2cf,0x1c5,0xcc,0xfcc,0xec5,0xdcf,0xcc6,0xbca,0xac3,0x9c9,0x8c0,
0x8c0,0x9c9,0xac3,0xbca,0xcc6,0xdcf,0xec5,0xfcc,0xcc,0x1c5,0x2cf,0x3c6,0x4ca,0x5c3,0x6c9,0x7c0,
0x950,0x859,0xb53,0xa5a,0xd56,0xc5f,0xf55,0xe5c,0x15c,0x55,0x35f,0x256,0x55a,0x453,0x759,0x650,
0xaf0,0xbf9,0x8f3,0x9fa,0xef6,0xfff,0xcf5,0xdfc,0x2fc,0x3f5,0xff,0x1f6,0x6fa,0x7f3,0x4f9,0x5f0,
0xb60,0xa69,0x963,0x86a,0xf66,0xe6f,0xd65,0xc6c,0x36c,0x265,0x16f,0x66,0x76a,0x663,0x569,0x460,
0xca0,0xda9,0xea3,0xfaa,0x8a6,0x9af,0xaa5,0xbac,0x4ac,0x5a5,0x6af,0x7a6,0xaa,0x1a3,0x2a9,0x3a0,
0xd30,0xc39,0xf33,0xe3a,0x936,0x83f,0xb35,0xa3c,0x53c,0x435,0x73f,0x636,0x13a,0x33,0x339,0x230,
0xe90,0xf99,0xc93,0xd9a,0xa96,0xb9f,0x895,0x99c,0x69c,0x795,0x49f,0x596,0x29a,0x393,0x99,0x190,
0xf00,0xe09,0xd03,0xc0a,0xb06,0xa0f,0x905,0x80c,0x70c,0x605,0x50f,0x406,0x30a,0x203,0x109,0x0 };

static const int kTri[256][16] = {
{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,8,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,1,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{1,8,3,9,8,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{1,2,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,8,3,1,2,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{9,2,10,0,2,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{2,8,3,2,10,8,10,9,8,-1,-1,-1,-1,-1,-1,-1},
{3,11,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,11,2,8,11,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{1,9,0,2,3,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{1,11,2,1,9,11,9,8,11,-1,-1,-1,-1,-1,-1,-1},
{3,10,1,11,10,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,10,1,0,8,10,8,11,10,-1,-1,-1,-1,-1,-1,-1},
{3,9,0,3,11,9,11,10,9,-1,-1,-1,-1,-1,-1,-1},
{9,8,10,10,8,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{4,7,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{4,3,0,7,3,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,1,9,8,4,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{4,1,9,4,7,1,7,3,1,-1,-1,-1,-1,-1,-1,-1},
{1,2,10,8,4,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{3,4,7,3,0,4,1,2,10,-1,-1,-1,-1,-1,-1,-1},
{9,2,10,9,0,2,8,4,7,-1,-1,-1,-1,-1,-1,-1},
{2,10,9,2,9,7,2,7,3,7,9,4,-1,-1,-1,-1},
{8,4,7,3,11,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{11,4,7,11,2,4,2,0,4,-1,-1,-1,-1,-1,-1,-1},
{9,0,1,8,4,7,2,3,11,-1,-1,-1,-1,-1,-1,-1},
{4,7,11,9,4,11,9,11,2,9,2,1,-1,-1,-1,-1},
{3,10,1,3,11,10,7,8,4,-1,-1,-1,-1,-1,-1,-1},
{1,11,10,1,4,11,1,0,4,7,11,4,-1,-1,-1,-1},
{4,7,8,9,0,11,9,11,10,11,0,3,-1,-1,-1,-1},
{4,7,11,4,11,9,9,11,10,-1,-1,-1,-1,-1,-1,-1},
{9,5,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{9,5,4,0,8,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,5,4,1,5,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{8,5,4,8,3,5,3,1,5,-1,-1,-1,-1,-1,-1,-1},
{1,2,10,9,5,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{3,0,8,1,2,10,4,9,5,-1,-1,-1,-1,-1,-1,-1},
{5,2,10,5,4,2,4,0,2,-1,-1,-1,-1,-1,-1,-1},
{2,10,5,3,2,5,3,5,4,3,4,8,-1,-1,-1,-1},
{9,5,4,2,3,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,11,2,0,8,11,4,9,5,-1,-1,-1,-1,-1,-1,-1},
{0,5,4,0,1,5,2,3,11,-1,-1,-1,-1,-1,-1,-1},
{2,1,5,2,5,8,2,8,11,4,8,5,-1,-1,-1,-1},
{10,3,11,10,1,3,9,5,4,-1,-1,-1,-1,-1,-1,-1},
{4,9,5,0,8,1,8,10,1,8,11,10,-1,-1,-1,-1},
{5,4,0,5,0,11,5,11,10,11,0,3,-1,-1,-1,-1},
{5,4,8,5,8,10,10,8,11,-1,-1,-1,-1,-1,-1,-1},
{9,7,8,5,7,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{9,3,0,9,5,3,5,7,3,-1,-1,-1,-1,-1,-1,-1},
{0,7,8,0,1,7,1,5,7,-1,-1,-1,-1,-1,-1,-1},
{1,5,3,3,5,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{9,7,8,9,5,7,10,1,2,-1,-1,-1,-1,-1,-1,-1},
{10,1,2,9,5,0,5,3,0,5,7,3,-1,-1,-1,-1},
{8,0,2,8,2,5,8,5,7,10,5,2,-1,-1,-1,-1},
{2,10,5,2,5,3,3,5,7,-1,-1,-1,-1,-1,-1,-1},
{7,9,5,7,8,9,3,11,2,-1,-1,-1,-1,-1,-1,-1},
{9,5,7,9,7,2,9,2,0,2,7,11,-1,-1,-1,-1},
{2,3,11,0,1,8,1,7,8,1,5,7,-1,-1,-1,-1},
{11,2,1,11,1,7,7,1,5,-1,-1,-1,-1,-1,-1,-1},
{9,5,8,8,5,7,10,1,3,10,3,11,-1,-1,-1,-1},
{5,7,0,5,0,9,7,11,0,1,0,10,11,10,0,-1},
{11,10,0,11,0,3,10,5,0,8,0,7,5,7,0,-1},
{11,10,5,7,11,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{10,6,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,8,3,5,10,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{9,0,1,5,10,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{1,8,3,1,9,8,5,10,6,-1,-1,-1,-1,-1,-1,-1},
{1,6,5,2,6,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{1,6,5,1,2,6,3,0,8,-1,-1,-1,-1,-1,-1,-1},
{9,6,5,9,0,6,0,2,6,-1,-1,-1,-1,-1,-1,-1},
{5,9,8,5,8,2,5,2,6,3,2,8,-1,-1,-1,-1},
{2,3,11,10,6,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{11,0,8,11,2,0,10,6,5,-1,-1,-1,-1,-1,-1,-1},
{0,1,9,2,3,11,5,10,6,-1,-1,-1,-1,-1,-1,-1},
{5,10,6,1,9,2,9,11,2,9,8,11,-1,-1,-1,-1},
{6,3,11,6,5,3,5,1,3,-1,-1,-1,-1,-1,-1,-1},
{0,8,11,0,11,5,0,5,1,5,11,6,-1,-1,-1,-1},
{3,11,6,0,3,6,0,6,5,0,5,9,-1,-1,-1,-1},
{6,5,9,6,9,11,11,9,8,-1,-1,-1,-1,-1,-1,-1},
{5,10,6,4,7,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{4,3,0,4,7,3,6,5,10,-1,-1,-1,-1,-1,-1,-1},
{1,9,0,5,10,6,8,4,7,-1,-1,-1,-1,-1,-1,-1},
{10,6,5,1,9,7,1,7,3,7,9,4,-1,-1,-1,-1},
{6,1,2,6,5,1,4,7,8,-1,-1,-1,-1,-1,-1,-1},
{1,2,5,5,2,6,3,0,4,3,4,7,-1,-1,-1,-1},
{8,4,7,9,0,5,0,6,5,0,2,6,-1,-1,-1,-1},
{7,3,9,7,9,4,3,2,9,5,9,6,2,6,9,-1},
{3,11,2,7,8,4,10,6,5,-1,-1,-1,-1,-1,-1,-1},
{5,10,6,4,7,2,4,2,0,2,7,11,-1,-1,-1,-1},
{0,1,9,4,7,8,2,3,11,5,10,6,-1,-1,-1,-1},
{9,2,1,9,11,2,9,4,11,7,11,4,5,10,6,-1},
{8,4,7,3,11,5,3,5,1,5,11,6,-1,-1,-1,-1},
{5,1,11,5,11,6,1,0,11,7,11,4,0,4,11,-1},
{0,5,9,0,6,5,0,3,6,11,6,3,8,4,7,-1},
{6,5,9,6,9,11,4,7,9,7,11,9,-1,-1,-1,-1},
{10,4,9,6,4,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{4,10,6,4,9,10,0,8,3,-1,-1,-1,-1,-1,-1,-1},
{10,0,1,10,6,0,6,4,0,-1,-1,-1,-1,-1,-1,-1},
{8,3,1,8,1,6,8,6,4,6,1,10,-1,-1,-1,-1},
{1,4,9,1,2,4,2,6,4,-1,-1,-1,-1,-1,-1,-1},
{3,0,8,1,2,9,2,4,9,2,6,4,-1,-1,-1,-1},
{0,2,4,4,2,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{8,3,2,8,2,4,4,2,6,-1,-1,-1,-1,-1,-1,-1},
{10,4,9,10,6,4,11,2,3,-1,-1,-1,-1,-1,-1,-1},
{0,8,2,2,8,11,4,9,10,4,10,6,-1,-1,-1,-1},
{3,11,2,0,1,6,0,6,4,6,1,10,-1,-1,-1,-1},
{6,4,1,6,1,10,4,8,1,2,1,11,8,11,1,-1},
{9,6,4,9,3,6,9,1,3,11,6,3,-1,-1,-1,-1},
{8,11,1,8,1,0,11,6,1,9,1,4,6,4,1,-1},
{3,11,6,3,6,0,0,6,4,-1,-1,-1,-1,-1,-1,-1},
{6,4,8,11,6,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{7,10,6,7,8,10,8,9,10,-1,-1,-1,-1,-1,-1,-1},
{0,7,3,0,10,7,0,9,10,6,7,10,-1,-1,-1,-1},
{10,6,7,1,10,7,1,7,8,1,8,0,-1,-1,-1,-1},
{10,6,7,10,7,1,1,7,3,-1,-1,-1,-1,-1,-1,-1},
{1,2,6,1,6,8,1,8,9,8,6,7,-1,-1,-1,-1},
{2,6,9,2,9,1,6,7,9,0,9,3,7,3,9,-1},
{7,8,0,7,0,6,6,0,2,-1,-1,-1,-1,-1,-1,-1},
{7,3,2,6,7,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{2,3,11,10,6,8,10,8,9,8,6,7,-1,-1,-1,-1},
{2,0,7,2,7,11,0,9,7,6,7,10,9,10,7,-1},
{1,8,0,1,7,8,1,10,7,6,7,10,2,3,11,-1},
{11,2,1,11,1,7,10,6,1,6,7,1,-1,-1,-1,-1},
{8,9,6,8,6,7,9,1,6,11,6,3,1,3,6,-1},
{0,9,1,11,6,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{7,8,0,7,0,6,3,11,0,11,6,0,-1,-1,-1,-1},
{7,11,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{7,6,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{3,0,8,11,7,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,1,9,11,7,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{8,1,9,8,3,1,11,7,6,-1,-1,-1,-1,-1,-1,-1},
{10,1,2,6,11,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{1,2,10,3,0,8,6,11,7,-1,-1,-1,-1,-1,-1,-1},
{2,9,0,2,10,9,6,11,7,-1,-1,-1,-1,-1,-1,-1},
{6,11,7,2,10,3,10,8,3,10,9,8,-1,-1,-1,-1},
{7,2,3,6,2,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{7,0,8,7,6,0,6,2,0,-1,-1,-1,-1,-1,-1,-1},
{2,7,6,2,3,7,0,1,9,-1,-1,-1,-1,-1,-1,-1},
{1,6,2,1,8,6,1,9,8,8,7,6,-1,-1,-1,-1},
{10,7,6,10,1,7,1,3,7,-1,-1,-1,-1,-1,-1,-1},
{10,7,6,1,7,10,1,8,7,1,0,8,-1,-1,-1,-1},
{0,3,7,0,7,10,0,10,9,6,10,7,-1,-1,-1,-1},
{7,6,10,7,10,8,8,10,9,-1,-1,-1,-1,-1,-1,-1},
{6,8,4,11,8,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{3,6,11,3,0,6,0,4,6,-1,-1,-1,-1,-1,-1,-1},
{8,6,11,8,4,6,9,0,1,-1,-1,-1,-1,-1,-1,-1},
{9,4,6,9,6,3,9,3,1,11,3,6,-1,-1,-1,-1},
{6,8,4,6,11,8,2,10,1,-1,-1,-1,-1,-1,-1,-1},
{1,2,10,3,0,11,0,6,11,0,4,6,-1,-1,-1,-1},
{4,11,8,4,6,11,0,2,9,2,10,9,-1,-1,-1,-1},
{10,9,3,10,3,2,9,4,3,11,3,6,4,6,3,-1},
{8,2,3,8,4,2,4,6,2,-1,-1,-1,-1,-1,-1,-1},
{0,4,2,4,6,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{1,9,0,2,3,4,2,4,6,4,3,8,-1,-1,-1,-1},
{1,9,4,1,4,2,2,4,6,-1,-1,-1,-1,-1,-1,-1},
{8,1,3,8,6,1,8,4,6,6,10,1,-1,-1,-1,-1},
{10,1,0,10,0,6,6,0,4,-1,-1,-1,-1,-1,-1,-1},
{4,6,3,4,3,8,6,10,3,0,3,9,10,9,3,-1},
{10,9,4,6,10,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{4,9,5,7,6,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,8,3,4,9,5,11,7,6,-1,-1,-1,-1,-1,-1,-1},
{5,0,1,5,4,0,7,6,11,-1,-1,-1,-1,-1,-1,-1},
{11,7,6,8,3,4,3,5,4,3,1,5,-1,-1,-1,-1},
{9,5,4,10,1,2,7,6,11,-1,-1,-1,-1,-1,-1,-1},
{6,11,7,1,2,10,0,8,3,4,9,5,-1,-1,-1,-1},
{7,6,11,5,4,10,4,2,10,4,0,2,-1,-1,-1,-1},
{3,4,8,3,5,4,3,2,5,10,5,2,11,7,6,-1},
{7,2,3,7,6,2,5,4,9,-1,-1,-1,-1,-1,-1,-1},
{9,5,4,0,8,6,0,6,2,6,8,7,-1,-1,-1,-1},
{3,6,2,3,7,6,1,5,0,5,4,0,-1,-1,-1,-1},
{6,2,8,6,8,7,2,1,8,4,8,5,1,5,8,-1},
{9,5,4,10,1,6,1,7,6,1,3,7,-1,-1,-1,-1},
{1,6,10,1,7,6,1,0,7,8,7,0,9,5,4,-1},
{4,0,10,4,10,5,0,3,10,6,10,7,3,7,10,-1},
{7,6,10,7,10,8,5,4,10,4,8,10,-1,-1,-1,-1},
{6,9,5,6,11,9,11,8,9,-1,-1,-1,-1,-1,-1,-1},
{3,6,11,0,6,3,0,5,6,0,9,5,-1,-1,-1,-1},
{0,11,8,0,5,11,0,1,5,5,6,11,-1,-1,-1,-1},
{6,11,3,6,3,5,5,3,1,-1,-1,-1,-1,-1,-1,-1},
{1,2,10,9,5,11,9,11,8,11,5,6,-1,-1,-1,-1},
{0,11,3,0,6,11,0,9,6,5,6,9,1,2,10,-1},
{11,8,5,11,5,6,8,0,5,10,5,2,0,2,5,-1},
{6,11,3,6,3,5,2,10,3,10,5,3,-1,-1,-1,-1},
{5,8,9,5,2,8,5,6,2,3,8,2,-1,-1,-1,-1},
{9,5,6,9,6,0,0,6,2,-1,-1,-1,-1,-1,-1,-1},
{1,5,8,1,8,0,5,6,8,3,8,2,6,2,8,-1},
{1,5,6,2,1,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{1,3,6,1,6,10,3,8,6,5,6,9,8,9,6,-1},
{10,1,0,10,0,6,9,5,0,5,6,0,-1,-1,-1,-1},
{0,3,8,5,6,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{10,5,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{11,5,10,7,5,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{11,5,10,11,7,5,8,3,0,-1,-1,-1,-1,-1,-1,-1},
{5,11,7,5,10,11,1,9,0,-1,-1,-1,-1,-1,-1,-1},
{10,7,5,10,11,7,9,8,1,8,3,1,-1,-1,-1,-1},
{11,1,2,11,7,1,7,5,1,-1,-1,-1,-1,-1,-1,-1},
{0,8,3,1,2,7,1,7,5,7,2,11,-1,-1,-1,-1},
{9,7,5,9,2,7,9,0,2,2,11,7,-1,-1,-1,-1},
{7,5,2,7,2,11,5,9,2,3,2,8,9,8,2,-1},
{2,5,10,2,3,5,3,7,5,-1,-1,-1,-1,-1,-1,-1},
{8,2,0,8,5,2,8,7,5,10,2,5,-1,-1,-1,-1},
{9,0,1,5,10,3,5,3,7,3,10,2,-1,-1,-1,-1},
{9,8,2,9,2,1,8,7,2,10,2,5,7,5,2,-1},
{1,3,5,3,7,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,8,7,0,7,1,1,7,5,-1,-1,-1,-1,-1,-1,-1},
{9,0,3,9,3,5,5,3,7,-1,-1,-1,-1,-1,-1,-1},
{9,8,7,5,9,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{5,8,4,5,10,8,10,11,8,-1,-1,-1,-1,-1,-1,-1},
{5,0,4,5,11,0,5,10,11,11,3,0,-1,-1,-1,-1},
{0,1,9,8,4,10,8,10,11,10,4,5,-1,-1,-1,-1},
{10,11,4,10,4,5,11,3,4,9,4,1,3,1,4,-1},
{2,5,1,2,8,5,2,11,8,4,5,8,-1,-1,-1,-1},
{0,4,11,0,11,3,4,5,11,2,11,1,5,1,11,-1},
{0,2,5,0,5,9,2,11,5,4,5,8,11,8,5,-1},
{9,4,5,2,11,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{2,5,10,3,5,2,3,4,5,3,8,4,-1,-1,-1,-1},
{5,10,2,5,2,4,4,2,0,-1,-1,-1,-1,-1,-1,-1},
{3,10,2,3,5,10,3,8,5,4,5,8,0,1,9,-1},
{5,10,2,5,2,4,1,9,2,9,4,2,-1,-1,-1,-1},
{8,4,5,8,5,3,3,5,1,-1,-1,-1,-1,-1,-1,-1},
{0,4,5,1,0,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{8,4,5,8,5,3,9,0,5,0,3,5,-1,-1,-1,-1},
{9,4,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{4,11,7,4,9,11,9,10,11,-1,-1,-1,-1,-1,-1,-1},
{0,8,3,4,9,7,9,11,7,9,10,11,-1,-1,-1,-1},
{1,10,11,1,11,4,1,4,0,7,4,11,-1,-1,-1,-1},
{3,1,4,3,4,8,1,10,4,7,4,11,10,11,4,-1},
{4,11,7,9,11,4,9,2,11,9,1,2,-1,-1,-1,-1},
{9,7,4,9,11,7,9,1,11,2,11,1,0,8,3,-1},
{11,7,4,11,4,2,2,4,0,-1,-1,-1,-1,-1,-1,-1},
{11,7,4,11,4,2,8,3,4,3,2,4,-1,-1,-1,-1},
{2,9,10,2,7,9,2,3,7,7,4,9,-1,-1,-1,-1},
{9,10,7,9,7,4,10,2,7,8,7,0,2,0,7,-1},
{3,7,10,3,10,2,7,4,10,1,10,0,4,0,10,-1},
{1,10,2,8,7,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{4,9,1,4,1,7,7,1,3,-1,-1,-1,-1,-1,-1,-1},
{4,9,1,4,1,7,0,8,1,8,7,1,-1,-1,-1,-1},
{4,0,3,7,4,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{4,8,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{9,10,8,10,11,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{3,0,9,3,9,11,11,9,10,-1,-1,-1,-1,-1,-1,-1},
{0,1,10,0,10,8,8,10,11,-1,-1,-1,-1,-1,-1,-1},
{3,1,10,11,3,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{1,2,11,1,11,9,9,11,8,-1,-1,-1,-1,-1,-1,-1},
{3,0,9,3,9,11,1,2,9,2,11,9,-1,-1,-1,-1},
{0,2,11,8,0,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{3,2,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{2,3,8,2,8,10,10,8,9,-1,-1,-1,-1,-1,-1,-1},
{9,10,2,0,9,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{2,3,8,2,8,10,0,1,8,1,10,8,-1,-1,-1,-1},
{1,10,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{1,3,8,9,1,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,9,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{0,3,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1} };

// Cube-corner offsets and the 12 edges (pairs of corner indices), Bourke order.
static const int kCorner[8][3] = {{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
static const int kEdgeC[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};


// ===================== VoxelTerrain =========================================
namespace {

using Clock = std::chrono::steady_clock;
inline double MsSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

// Grid coordinates from a float that may be far outside the grid (or not a number): clamped so the
// cast to int can never overflow. The same answer as (int)std::floor(v) for every ordinary value.
inline int FloorI(float v) {
    if (!(v > -1.0e9f)) return -1000000000;      // also catches NaN
    if (v > 1.0e9f) return 1000000000;
    const int i = (int)v;
    return v < (float)i ? i - 1 : i;
}
inline int CeilI(float v) {
    if (!(v > -1.0e9f)) return -1000000000;
    if (v > 1.0e9f) return 1000000000;
    const int i = (int)v;
    return v > (float)i ? i + 1 : i;
}
inline bool Finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

std::size_t MeshBytes(const Mesh& m) {
    return m.vertices.size() * sizeof(Vec3) + m.normals.size() * sizeof(Vec3) + m.uvs.size() * sizeof(Vec2) +
           m.triangles.size() * sizeof(int) + m.triColors.size() * sizeof(Color);
}

// ---- the MeshRenderer settings a chunk object copies from the terrain's own renderer ------------------
// One list drives both the copy and the change check, so a field cannot be in one and forgotten in the other.
// (When MeshRenderer gets a new look-changing field, add it here: tests/test_voxel_chunks.cpp checks the list.)
#define OKAY_VOXEL_MATERIAL_FIELDS(X)                                                                         \
    X(enabled) X(color) X(shader) X(toonBands) X(gradientTop) X(gradientBottom)                              \
    X(rimStrength) X(rimPower) X(rimColor) X(castShadows) X(receiveShadows)                                  \
    X(groundShadow) X(groundShadowY) X(groundShadowStrength)                                                 \
    X(outline) X(outlineColor) X(outlineWidth) X(uvScroll) X(triplanar) X(wireframe) X(doubleSided)          \
    X(emissive) X(specular) X(shininess) X(unlit) X(texture) X(tiling) X(texOffset) X(texFilter)             \
    X(normalMap) X(normalStrength) X(reflectivity) X(specularMap) X(aoMap) X(aoStrength)                     \
    X(metallicRoughnessMap) X(emissiveMap) X(metallic) X(matcap)

inline std::uint64_t MixU(std::uint64_t h, std::uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); return h; }
inline std::uint64_t MixField(std::uint64_t h, float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return MixU(h, u); }
inline std::uint64_t MixField(std::uint64_t h, bool b) { return MixU(h, b ? 1u : 0u); }
inline std::uint64_t MixField(std::uint64_t h, int i) { return MixU(h, (std::uint32_t)i); }
inline std::uint64_t MixField(std::uint64_t h, const Color& c) { return MixField(MixField(MixField(MixField(h, c.r), c.g), c.b), c.a); }
inline std::uint64_t MixField(std::uint64_t h, const Vec2& v) { return MixField(MixField(h, v.x), v.y); }
inline std::uint64_t MixField(std::uint64_t h, const std::string& s) { return MixU(h, std::hash<std::string>{}(s)); }
template <class E, class = typename std::enable_if<std::is_enum<E>::value>::type>
inline std::uint64_t MixField(std::uint64_t h, E e) { return MixU(h, (std::uint64_t)(int)e); }

std::uint64_t MaterialSig(const MeshRenderer& r) {
    std::uint64_t h = 1469598103934665603ull;
#define X(f) h = MixField(h, r.f);
    OKAY_VOXEL_MATERIAL_FIELDS(X)
#undef X
    return h;
}
void MirrorMaterial(const MeshRenderer& src, MeshRenderer& dst) {
#define X(f) dst.f = src.f;
    OKAY_VOXEL_MATERIAL_FIELDS(X)
#undef X
}

int g_forceOldRemesh = -1;

} // namespace

void VoxelTerrain::ForceOldRemesh(int mode) { g_forceOldRemesh = mode; }
bool VoxelTerrain::OldRemeshActive() {
    if (g_forceOldRemesh >= 0) return g_forceOldRemesh != 0;
    static const bool env = [] {
        const char* e = std::getenv("OKAY_VOXEL_OLD_REMESH");
        return e && *e && *e != '0';
    }();
    return env;
}

void VoxelTerrain::Resize(int X, int Y, int Z) {
    nx = X < 2 ? 2 : X; ny = Y < 2 ? 2 : Y; nz = Z < 2 ? 2 : Z;
    density.assign((std::size_t)nx * ny * nz, -1.0f);   // all air
    mcAllDirty_ = true;
    mcNx_ = -1;
    floatersSwept_ = false;
    editAny_ = false;
}

// Material values are kept within a bounded band (±~5 voxels) so the field reads
// as a clamped signed distance: only the sign and the near-surface gradient drive
// the mesh, and digging/adding with a finite strength reliably flips a voxel
// rather than fighting an ever-larger interior value deep in the rock.
float VoxelTerrain::Clamp() const { return voxelSize * 3.0f; }
static float ClampTo(float v, float c) { return v < -c ? -c : (v > c ? c : v); }

void VoxelTerrain::FillSlab(float frac) {
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    float topY = SizeY() * frac, c = Clamp();
    for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x)
                density[Index(x, y, z)] = ClampTo(topY - y * voxelSize, c);   // solid below topY
    SealFloor();
    mcAllDirty_ = true;
    floatersSwept_ = false;
    editAny_ = false;
}

void VoxelTerrain::Generate(float surfaceFrac, float amplitude, float caveAmount, unsigned seed) {
    if (caveAmount < 0) caveAmount = 0; if (caveAmount > 1) caveAmount = 1;
    const float baseY = SizeY() * surfaceFrac;
    const float fxz = 2.5f / std::max(1.0f, (float)(nx - 1));   // surface feature scale
    const float fc  = 3.5f / std::max(1.0f, (float)(nx - 1));   // cave feature scale
    for (int z = 0; z < nz; ++z)
        for (int x = 0; x < nx; ++x) {
            float n = Fractal2((float)x * fxz, (float)z * fxz, 5, seed);   // [0,1]
            float surf = baseY + (n - 0.5f) * 2.0f * amplitude;
            for (int y = 0; y < ny; ++y) {
                float wy = y * voxelSize;
                float d = surf - wy;                  // solid below the surface
                if (caveAmount > 0.0f && wy < surf - voxelSize * 1.5f) {
                    // Winding caverns: carve where a 3D field sits near a midline.
                    float cf = Fractal3((float)x * fc, (float)y * fc, (float)z * fc, 4, seed ^ 0x5AB3u);
                    float worm = std::fabs(cf - 0.5f);
                    if (worm < 0.07f * (0.4f + caveAmount)) d = -voxelSize;   // open air
                }
                density[Index(x, y, z)] = ClampTo(d, Clamp());
            }
        }
    SealFloor();
    mcAllDirty_ = true;
    floatersSwept_ = false;
    editAny_ = false;
}

// ---- dirty tracking ---------------------------------------------------------
// A density write at cell k changes the corners of cubes k-1 and k, and ALSO the normals of cubes k-2 .. k+1
// (a normal reads half a cell either side of its vertex, so a cube's vertices read cells c-1 .. c+2). The old
// code only marked k-1 .. k, so the cubes just outside an edit kept normals from before it.
void VoxelTerrain::TouchBox(int x0, int y0, int z0, int x1, int y1, int z1) {
    if (x0 > x1 || y0 > y1 || z0 > z1) return;
    ++editCount;
    if (!editAny_) {
        editMin_[0] = x0; editMin_[1] = y0; editMin_[2] = z0;
        editMax_[0] = x1; editMax_[1] = y1; editMax_[2] = z1;
        editAny_ = true;
    } else {
        if (x0 < editMin_[0]) editMin_[0] = x0;
        if (y0 < editMin_[1]) editMin_[1] = y0;
        if (z0 < editMin_[2]) editMin_[2] = z0;
        if (x1 > editMax_[0]) editMax_[0] = x1;
        if (y1 > editMax_[1]) editMax_[1] = y1;
        if (z1 > editMax_[2]) editMax_[2] = z1;
    }
    if (y0 < floorLayers) floorTouched_ = true;
    MarkCubes(x0 - 2, y0 - 2, z0 - 2, x1 + 1, y1 + 1, z1 + 1);
}

void VoxelTerrain::MarkCubes(int a0, int b0, int c0, int a1, int b1, int c1) {
    if (mcAllDirty_) return;
    const int cubesX = nx - 1, cubesY = ny - 1, cubesZ = nz - 1;
    const int ncx = std::max(1, (cubesX + kChunk - 1) / kChunk);
    const int ncy = std::max(1, (cubesY + kChunk - 1) / kChunk);
    const int ncz = std::max(1, (cubesZ + kChunk - 1) / kChunk);
    if (mcNx_ != nx || mcNy_ != ny || mcNz_ != nz || (int)mcDirty_.size() != ncx * ncy * ncz) {
        mcAllDirty_ = true;      // the chunk table does not describe this grid (yet): everything is meshed
        return;
    }
    if (a0 < 0) a0 = 0; if (b0 < 0) b0 = 0; if (c0 < 0) c0 = 0;
    if (a1 > cubesX - 1) a1 = cubesX - 1;
    if (b1 > cubesY - 1) b1 = cubesY - 1;
    if (c1 > cubesZ - 1) c1 = cubesZ - 1;
    if (a0 > a1 || b0 > b1 || c0 > c1) return;
    for (int cz = c0 / kChunk; cz <= c1 / kChunk; ++cz)
        for (int cy = b0 / kChunk; cy <= b1 / kChunk; ++cy)
            for (int cx = a0 / kChunk; cx <= a1 / kChunk; ++cx) {
                unsigned char& d = mcDirty_[(std::size_t)(cx + ncx * (cy + ncy * cz))];
                if (!d) { d = 1; ++mcDirtyCount_; }
            }
}

bool VoxelTerrain::Dig(const Vec3& local, float radius, float amount) {
    // A brush that cannot make sense must not touch the field. (Radius 0 divided by zero and wrote NaN into
    // the cell the brush sat on; a NaN strength or position poisoned the whole box.)
    if (!(radius > 0.0f) || !std::isfinite(radius) || !std::isfinite(amount) || !Finite3(local)) return false;
    float gx = (local.x + HalfX()) / voxelSize;
    float gy = local.y / voxelSize;
    float gz = (local.z + HalfZ()) / voxelSize;
    float gr = radius / voxelSize;
    int x0 = std::max(0, FloorI(gx - gr)), x1 = std::min(nx - 1, CeilI(gx + gr));
    int y0 = std::max(0, FloorI(gy - gr)), y1 = std::min(ny - 1, CeilI(gy + gr));
    int z0 = std::max(0, FloorI(gz - gr)), z1 = std::min(nz - 1, CeilI(gz + gr));
    int cx0 = nx, cy0 = ny, cz0 = nz, cx1 = -1, cy1 = -1, cz1 = -1;    // the box of cells that really changed
    bool changed = false;
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                // The bottom rows are the floor of the volume. Carving them opens a
                // hole out of the world; filling (negative amount) is still allowed.
                if (floorLayers > 0 && amount > 0.0f && y < floorLayers) continue;
                float dx = x - gx, dy = y - gy, dz = z - gz;
                float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d > gr) continue;
                float t = 1.0f - d / gr;
                float fall = t * t * (3.0f - 2.0f * t);    // smoothstep sphere (softer edge)
                float& cell = density[Index(x, y, z)];
                float nv = ClampTo(cell - amount * fall, Clamp());   // subtract = carve air
                if (nv != cell) {
                    cell = nv; changed = true;
                    if (x < cx0) cx0 = x; if (x > cx1) cx1 = x;
                    if (y < cy0) cy0 = y; if (y > cy1) cy1 = y;
                    if (z < cz0) cz0 = z; if (z > cz1) cz1 = z;
                }
            }
    if (changed) TouchBox(cx0, cy0, cz0, cx1, cy1, cz1);
    return changed;
}

bool VoxelTerrain::Add(const Vec3& local, float radius, float amount) {
    return Dig(local, radius, -amount);   // adding is digging with negative amount
}

bool VoxelTerrain::SmoothAt(const Vec3& local, float radius, float amount) {
    if (!(amount > 0.0f) || !(radius > 0.0f) || !std::isfinite(radius) || !std::isfinite(amount) || !Finite3(local)) return false;
    if (amount > 1.0f) amount = 1.0f;
    float gx = (local.x + HalfX()) / voxelSize;
    float gy = local.y / voxelSize;
    float gz = (local.z + HalfZ()) / voxelSize;
    float gr = radius / voxelSize;
    int x0 = std::max(1, FloorI(gx - gr)), x1 = std::min(nx - 2, CeilI(gx + gr));
    int y0 = std::max(std::max(1, floorLayers), FloorI(gy - gr));
    int y1 = std::min(ny - 2, CeilI(gy + gr));
    int z0 = std::max(1, FloorI(gz - gr)), z1 = std::min(nz - 2, CeilI(gz + gr));
    if (x0 > x1 || y0 > y1 || z0 > z1) return false;
    // Read from a snapshot of the box (and one cell around it) so the relaxation is even. The old code
    // copied the whole field for every call: megabytes per frame on a big world.
    const int sx = x1 - x0 + 3, sy = y1 - y0 + 3, sz = z1 - z0 + 3;
    std::vector<float> snap((std::size_t)sx * sy * sz);
    for (int z = 0; z < sz; ++z)
        for (int y = 0; y < sy; ++y) {
            const float* row = &density[Index(x0 - 1, y0 - 1 + y, z0 - 1 + z)];
            std::copy(row, row + sx, snap.begin() + ((std::size_t)z * sy + y) * sx);
        }
    auto S = [&](int x, int y, int z) { return snap[((std::size_t)(z - z0 + 1) * sy + (y - y0 + 1)) * sx + (x - x0 + 1)]; };
    int cx0 = nx, cy0 = ny, cz0 = nz, cx1 = -1, cy1 = -1, cz1 = -1;
    bool changed = false;
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                float dx = x - gx, dy = y - gy, dz = z - gz;
                float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d > gr) continue;
                // 6-neighbour average (a light Laplacian relax).
                float avg = (S(x - 1, y, z) + S(x + 1, y, z) +
                             S(x, y - 1, z) + S(x, y + 1, z) +
                             S(x, y, z - 1) + S(x, y, z + 1)) * (1.0f / 6.0f);
                float w = (1.0f - d / gr) * amount;
                float& cell = density[Index(x, y, z)];
                float nv = ClampTo(cell + (avg - cell) * w, Clamp());
                if (nv != cell) {
                    cell = nv; changed = true;
                    if (x < cx0) cx0 = x; if (x > cx1) cx1 = x;
                    if (y < cy0) cy0 = y; if (y > cy1) cy1 = y;
                    if (z < cz0) cz0 = z; if (z > cz1) cz1 = z;
                }
            }
    if (changed) TouchBox(cx0, cy0, cz0, cx1, cy1, cz1);
    return changed;
}

bool VoxelTerrain::DigBox(const Vec3& a, const Vec3& b, float amount) {
    if (!Finite3(a) || !Finite3(b) || !std::isfinite(amount)) return false;
    Vec3 lo{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
    Vec3 hi{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
    int x0 = std::max(0, FloorI((lo.x + HalfX()) / voxelSize));
    int x1 = std::min(nx - 1, CeilI((hi.x + HalfX()) / voxelSize));
    int y0 = std::max(0, FloorI(lo.y / voxelSize));
    int y1 = std::min(ny - 1, CeilI(hi.y / voxelSize));
    int z0 = std::max(0, FloorI((lo.z + HalfZ()) / voxelSize));
    int z1 = std::min(nz - 1, CeilI((hi.z + HalfZ()) / voxelSize));
    int cx0 = nx, cy0 = ny, cz0 = nz, cx1 = -1, cy1 = -1, cz1 = -1;
    bool changed = false;
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                if (floorLayers > 0 && amount > 0.0f && y < floorLayers) continue;
                float& cell = density[Index(x, y, z)];
                float nv = ClampTo(cell - amount, Clamp());
                if (nv != cell) {
                    cell = nv; changed = true;
                    if (x < cx0) cx0 = x; if (x > cx1) cx1 = x;
                    if (y < cy0) cy0 = y; if (y > cy1) cy1 = y;
                    if (z < cz0) cz0 = z; if (z > cz1) cz1 = z;
                }
            }
    if (changed) TouchBox(cx0, cy0, cz0, cx1, cy1, cz1);
    return changed;
}

// ---- floating pieces ------------------------------------------------------------------------------
namespace {
// Labels the 6-connected solid components (density > iso) that lie inside the region [rlo, rhi] and
// collects the cells of every one with fewer than `minVoxels` voxels. Seeds come from [slo, shi]. With
// `stopWhenBig` a walk ends as soon as its piece reaches minVoxels (it is attached, so it is not a crumb):
// the walk then never goes further than minVoxels cells from its seed, which is why a region that is
// minVoxels wider than the seed box can never cut a big piece short.
//   A cell is 0 (not seen), 1 (seen by a walk that found a crumb) or 2 (known to belong to a big piece:
//   the cells of a walk that stopped early, including the ones it had queued but not looked at yet). A walk
//   that runs into a 2 is part of that big piece too, however few cells it saw itself.
void FindFloaters(const std::vector<float>& dens, int nx, int ny, float iso,
                  const int rlo[3], const int rhi[3], const int slo[3], const int shi[3],
                  int minVoxels, bool stopWhenBig, std::vector<int>& outCells) {
    const std::size_t rx = (std::size_t)(rhi[0] - rlo[0] + 1), ry = (std::size_t)(rhi[1] - rlo[1] + 1),
                      rz = (std::size_t)(rhi[2] - rlo[2] + 1);
    std::vector<unsigned char> seen(rx * ry * rz, 0);
    auto vidx = [&](int x, int y, int z) {
        return (std::size_t)(x - rlo[0]) + rx * ((std::size_t)(y - rlo[1]) + ry * (std::size_t)(z - rlo[2]));
    };
    const std::size_t gsy = (std::size_t)nx, gsz = (std::size_t)nx * ny;
    auto gidx = [&](int x, int y, int z) { return (std::size_t)x + gsy * (std::size_t)y + gsz * (std::size_t)z; };
    auto pack = [](int x, int y, int z) { return (std::uint64_t)x | ((std::uint64_t)y << 21) | ((std::uint64_t)z << 42); };
    std::vector<std::uint64_t> stack;
    std::vector<int> comp;                 // global indices of the cells this walk has taken
    std::vector<std::size_t> compV;        // the same cells in the region's own numbering
    for (int sz = slo[2]; sz <= shi[2]; ++sz)
        for (int sy = slo[1]; sy <= shi[1]; ++sy)
            for (int sx = slo[0]; sx <= shi[0]; ++sx) {
                if (dens[gidx(sx, sy, sz)] <= iso) continue;
                const std::size_t v0 = vidx(sx, sy, sz);
                if (seen[v0]) continue;
                comp.clear(); compV.clear(); stack.clear();
                stack.push_back(pack(sx, sy, sz));
                seen[v0] = 1;
                bool attached = false;
                while (!stack.empty() && !attached) {
                    const std::uint64_t pk = stack.back(); stack.pop_back();
                    const int x = (int)(pk & 0x1FFFFF), y = (int)((pk >> 21) & 0x1FFFFF), z = (int)(pk >> 42);
                    comp.push_back((int)gidx(x, y, z));
                    if (stopWhenBig) compV.push_back(vidx(x, y, z));   // only the local walk needs to mark attached pieces
                    if (stopWhenBig && (int)comp.size() >= minVoxels) { attached = true; break; }
                    auto visit = [&](int xx, int yy, int zz) {
                        if (xx < rlo[0] || xx > rhi[0] || yy < rlo[1] || yy > rhi[1] || zz < rlo[2] || zz > rhi[2]) return;
                        if (dens[gidx(xx, yy, zz)] <= iso) return;
                        const std::size_t vi = vidx(xx, yy, zz);
                        if (seen[vi] == 2) { attached = true; return; }
                        if (seen[vi]) return;
                        seen[vi] = 1;
                        stack.push_back(pack(xx, yy, zz));
                    };
                    visit(x + 1, y, z); visit(x - 1, y, z); visit(x, y + 1, z);
                    visit(x, y - 1, z); visit(x, y, z + 1); visit(x, y, z - 1);
                }
                if (attached) {
                    for (std::size_t vi : compV) seen[vi] = 2;
                    for (std::uint64_t pk : stack)
                        seen[vidx((int)(pk & 0x1FFFFF), (int)((pk >> 21) & 0x1FFFFF), (int)(pk >> 42))] = 2;
                } else if ((int)comp.size() < minVoxels) {
                    outCells.insert(outCells.end(), comp.begin(), comp.end());
                }
            }
}
} // namespace

int VoxelTerrain::RemoveFloaters(int minVoxels) {
    if (minVoxels <= 1) return 0;
    const int rlo[3] = {0, 0, 0}, rhi[3] = {nx - 1, ny - 1, nz - 1};
    std::vector<int> cells;
    FindFloaters(density, nx, ny, iso, rlo, rhi, rlo, rhi, minVoxels, false, cells);
    const float air = iso - Clamp();          // a firmly-air density value
    int cleared = 0;
    for (int idx : cells) {
        const int x = idx % nx, y = (idx / nx) % ny, z = idx / (nx * ny);
        if (floorLayers > 0 && y < floorLayers) continue;   // leave the floor
        density[(std::size_t)idx] = air;
        ++cleared;
        TouchCell(x, y, z);                   // only the chunks around this voxel are meshed again
    }
    floatersSwept_ = true;
    editAny_ = false;
    return cleared;
}

int VoxelTerrain::RemoveFloatersEdited(int minVoxels) {
    if (minVoxels <= 1) return 0;
    if (!floatersSwept_) return RemoveFloaters(minVoxels);   // first time: the whole field, as it always was
    if (!editAny_) return 0;
    const int lim[3] = {nx - 1, ny - 1, nz - 1};
    int slo[3], shi[3], rlo[3], rhi[3];
    std::size_t volume = 1;
    for (int a = 0; a < 3; ++a) {
        slo[a] = std::max(0, editMin_[a] - 1);
        shi[a] = std::min(lim[a], editMax_[a] + 1);
        rlo[a] = std::max(0, slo[a] - minVoxels);
        rhi[a] = std::min(lim[a], shi[a] + minVoxels);
        volume *= (std::size_t)(rhi[a] - rlo[a] + 1);
    }
    if (volume > (std::size_t)Count() / 2 || volume > ((std::size_t)1 << 24)) return RemoveFloaters(minVoxels);
    std::vector<int> cells;
    FindFloaters(density, nx, ny, iso, rlo, rhi, slo, shi, minVoxels, true, cells);
    const float air = iso - Clamp();
    int cleared = 0;
    for (int idx : cells) {
        const int x = idx % nx, y = (idx / nx) % ny, z = idx / (nx * ny);
        if (floorLayers > 0 && y < floorLayers) continue;
        density[(std::size_t)idx] = air;
        ++cleared;
        TouchCell(x, y, z);
    }
    editAny_ = false;
    return cleared;
}

float VoxelTerrain::SampleDensity(const Vec3& local) const {
    float gx = (local.x + HalfX()) / voxelSize;
    float gy = local.y / voxelSize;
    float gz = (local.z + HalfZ()) / voxelSize;
    int x0 = (int)std::floor(gx), y0 = (int)std::floor(gy), z0 = (int)std::floor(gz);
    float fx = gx - x0, fy = gy - y0, fz = gz - z0;
    auto at = [&](int x, int y, int z) {
        if (x < 0) x = 0; if (y < 0) y = 0; if (z < 0) z = 0;
        if (x >= nx) x = nx - 1; if (y >= ny) y = ny - 1; if (z >= nz) z = nz - 1;
        return density[Index(x, y, z)];
    };
    float c000 = at(x0, y0, z0),     c100 = at(x0 + 1, y0, z0);
    float c010 = at(x0, y0 + 1, z0), c110 = at(x0 + 1, y0 + 1, z0);
    float c001 = at(x0, y0, z0 + 1), c101 = at(x0 + 1, y0, z0 + 1);
    float c011 = at(x0, y0 + 1, z0 + 1), c111 = at(x0 + 1, y0 + 1, z0 + 1);
    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
    return L(L(L(c000, c100, fx), L(c010, c110, fx), fy),
             L(L(c001, c101, fx), L(c011, c111, fx), fy), fz);
}

Vec3 VoxelTerrain::SurfaceNormal(const Vec3& local) const {
    float e = voxelSize * 0.5f;
    float dx = SampleDensity({local.x + e, local.y, local.z}) - SampleDensity({local.x - e, local.y, local.z});
    float dy = SampleDensity({local.x, local.y + e, local.z}) - SampleDensity({local.x, local.y - e, local.z});
    float dz = SampleDensity({local.x, local.y, local.z + e}) - SampleDensity({local.x, local.y, local.z - e});
    // Density increases INTO solid, so the outward normal is -gradient.
    Vec3 n{-dx, -dy, -dz};
    float L = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    return L > 1e-6f ? Vec3{n.x / L, n.y / L, n.z / L} : Vec3{0, 1, 0};
}

bool VoxelTerrain::SurfaceY(float lx, float lz, float& outY) const {
    // March from the top down; the first solid sample's top face is the surface.
    for (int y = ny - 1; y >= 0; --y) {
        float wy = y * voxelSize;
        if (SampleDensity(Vec3{lx, wy, lz}) > iso) {
            // Refine between this solid sample and the air one above it.
            float above = (y + 1 < ny) ? (y + 1) * voxelSize : wy + voxelSize;
            float dSolid = SampleDensity(Vec3{lx, wy, lz});
            float dAir   = SampleDensity(Vec3{lx, above, lz});
            float t = (dAir != dSolid) ? (iso - dSolid) / (dAir - dSolid) : 0.0f;
            if (t < 0) t = 0; if (t > 1) t = 1;
            outY = wy + (above - wy) * t;
            return true;
        }
    }
    return false;
}

// ---- marching cubes -----------------------------------------------------------------------------------
// The loop below does the same arithmetic as AppendCubesReference (the original, further down), operation
// for operation, so every vertex, normal and colour is bit for bit the same; it only skips work:
//  * a cube whose corners are all on one side of the surface is dropped before any position is computed,
//  * the density is read straight from the array when the sample cube lies inside the grid (no clamping),
//  * a vertex sits on one of 12 cube edges and up to five triangles share it: its normal (six trilinear
//    samples) is worked out once per edge instead of once per triangle corner.
void VoxelTerrain::AppendCubes(Mesh& m, int x0, int y0, int z0, int x1, int y1, int z1) const {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 > nx - 1) x1 = nx - 1;
    if (y1 > ny - 1) y1 = ny - 1;
    if (z1 > nz - 1) z1 = nz - 1;
    if (x0 >= x1 || y0 >= y1 || z0 >= z1) return;

    const float vs = voxelSize;
    const float halfX = HalfX(), halfZ = HalfZ();
    const float e = vs * 0.5f;
    const float* dens = density.data();
    const std::ptrdiff_t sy = nx, sz = (std::ptrdiff_t)nx * ny;
    const int gnx = nx, gny = ny, gnz = nz;
    const bool useMat = UseMaterials();

    // Trilinear density at a local-space point: SampleDensity's arithmetic exactly.
    auto sample = [&](float lx, float ly, float lz) -> float {
        const float gx = (lx + halfX) / vs;
        const float gy = ly / vs;
        const float gz = (lz + halfZ) / vs;
        const int ix = FloorI(gx), iy = FloorI(gy), iz = FloorI(gz);
        const float fx = gx - ix, fy = gy - iy, fz = gz - iz;
        float c000, c100, c010, c110, c001, c101, c011, c111;
        if (ix >= 0 && iy >= 0 && iz >= 0 && ix + 1 < gnx && iy + 1 < gny && iz + 1 < gnz) {
            const float* p = dens + (ix + (std::ptrdiff_t)iy * sy + (std::ptrdiff_t)iz * sz);
            c000 = p[0];  c100 = p[1];  c010 = p[sy];  c110 = p[sy + 1];
            c001 = p[sz]; c101 = p[sz + 1]; c011 = p[sz + sy]; c111 = p[sz + sy + 1];
        } else {
            auto at = [&](int x, int y, int z) {
                if (x < 0) x = 0; if (y < 0) y = 0; if (z < 0) z = 0;
                if (x >= gnx) x = gnx - 1; if (y >= gny) y = gny - 1; if (z >= gnz) z = gnz - 1;
                return dens[x + (std::ptrdiff_t)y * sy + (std::ptrdiff_t)z * sz];
            };
            c000 = at(ix, iy, iz);         c100 = at(ix + 1, iy, iz);
            c010 = at(ix, iy + 1, iz);     c110 = at(ix + 1, iy + 1, iz);
            c001 = at(ix, iy, iz + 1);     c101 = at(ix + 1, iy, iz + 1);
            c011 = at(ix, iy + 1, iz + 1); c111 = at(ix + 1, iy + 1, iz + 1);
        }
        auto L = [](float a, float b, float t) { return a + (b - a) * t; };
        return L(L(L(c000, c100, fx), L(c010, c110, fx), fy),
                 L(L(c001, c101, fx), L(c011, c111, fx), fy), fz);
    };
    // Gradient of the density field (central differences) -> outward normal.
    auto normalAt = [&](const Vec3& p) -> Vec3 {
        float dx = sample(p.x + e, p.y, p.z) - sample(p.x - e, p.y, p.z);
        float dy = sample(p.x, p.y + e, p.z) - sample(p.x, p.y - e, p.z);
        float dz = sample(p.x, p.y, p.z + e) - sample(p.x, p.y, p.z - e);
        Vec3 n{-dx, -dy, -dz};   // points toward decreasing density (out of solid)
        float L = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        return L > 1e-6f ? Vec3{n.x / L, n.y / L, n.z / L} : Vec3{0, 1, 0};
    };
    auto faceColor = [&](const Vec3& p, const Vec3& n) {
        float slope = 1.0f - std::fabs(n.y);
        if (p.y >= snowLevel) return snowColor;
        if (slope >= rockSlope) return rockColor;
        float t = slope / rockSlope; t = t < 0 ? 0 : (t > 1 ? 1 : t);
        return Color{grassColor.r + (soilColor.r - grassColor.r) * t,
                     grassColor.g + (soilColor.g - grassColor.g) * t,
                     grassColor.b + (soilColor.b - grassColor.b) * t, 1.0f};
    };

    for (int z = z0; z < z1; ++z)
        for (int y = y0; y < y1; ++y) {
            const float* row = dens + ((std::ptrdiff_t)y * sy + (std::ptrdiff_t)z * sz);
            for (int x = x0; x < x1; ++x) {
                const float* p = row + x;
                float val[8];
                val[0] = p[0];      val[1] = p[1];          val[2] = p[sy + 1];       val[3] = p[sy];
                val[4] = p[sz];     val[5] = p[sz + 1];     val[6] = p[sz + sy + 1];  val[7] = p[sz + sy];
                int cubeindex = 0;
                for (int i = 0; i < 8; ++i) if (val[i] < iso) cubeindex |= (1 << i);
                if (cubeindex == 0 || cubeindex == 255) continue;     // all corners on one side: no surface here
                const int edges = kEdge[cubeindex];
                if (edges == 0) continue;
                Vec3 pos[8];
                for (int i = 0; i < 8; ++i)
                    pos[i] = Vec3{(x + kCorner[i][0]) * vs - halfX, (y + kCorner[i][1]) * vs, (z + kCorner[i][2]) * vs - halfZ};
                Vec3 vert[12], nrm[12];
                unsigned nmask = 0;
                for (int ed = 0; ed < 12; ++ed) {
                    if (!(edges & (1 << ed))) continue;
                    const int a = kEdgeC[ed][0], b = kEdgeC[ed][1];
                    const float v1 = val[a], v2 = val[b];
                    const float t = (std::fabs(v2 - v1) > 1e-6f) ? (iso - v1) / (v2 - v1) : 0.5f;
                    vert[ed] = pos[a] + (pos[b] - pos[a]) * t;
                }
                const int* tri = kTri[cubeindex];
                for (int t = 0; tri[t] != -1; t += 3) {
                    const int ea = tri[t], eb = tri[t + 1], ec = tri[t + 2];
                    Vec3 a = vert[ea], b = vert[eb], c = vert[ec];
                    auto nrmOf = [&](int ed) -> Vec3 {
                        if (!(nmask & (1u << ed))) { nrm[ed] = normalAt(vert[ed]); nmask |= 1u << ed; }
                        return nrm[ed];
                    };
                    Vec3 na = nrmOf(ea), nb = nrmOf(eb), nc = nrmOf(ec);
                    // Force a consistent OUTWARD winding (geometric normal agrees with
                    // the gradient normal). This lets the mesh render single-sided —
                    // backface culling then skips ~half the triangles, a big speedup —
                    // without the surface ever culling its visible side.
                    Vec3 g = Vec3::Cross(b - a, c - a);
                    Vec3 navg{na.x + nb.x + nc.x, na.y + nb.y + nc.y, na.z + nb.z + nc.z};
                    if (g.x * navg.x + g.y * navg.y + g.z * navg.z < 0.0f) {
                        std::swap(b, c); std::swap(nb, nc);
                    }
                    int base = (int)m.vertices.size();
                    m.vertices.push_back(a); m.vertices.push_back(b); m.vertices.push_back(c);
                    m.normals.push_back(na); m.normals.push_back(nb); m.normals.push_back(nc);
                    m.uvs.push_back({a.x * 0.25f, a.z * 0.25f});
                    m.uvs.push_back({b.x * 0.25f, b.z * 0.25f});
                    m.uvs.push_back({c.x * 0.25f, c.z * 0.25f});
                    m.triangles.push_back(base); m.triangles.push_back(base + 1); m.triangles.push_back(base + 2);
                    if (useMat) {
                        // the face takes the colour of the most solid corner of its cube
                        const std::ptrdiff_t offs[8] = {0, 1, sy + 1, sy, sz, sz + 1, sz + sy + 1, sz + sy};
                        int bi = 0; for (int i = 1; i < 8; ++i) if (val[i] > val[bi]) bi = i;
                        const std::size_t mi = (std::size_t)((p - dens) + offs[bi]);
                        const unsigned char id = material[mi];
                        m.triColors.push_back(id < materials.size() ? materials[id].color : color);
                    } else if (autoColor) {
                        Vec3 ctr{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
                        Vec3 fn{(na.x + nb.x + nc.x) / 3, (na.y + nb.y + nc.y) / 3, (na.z + nb.z + nc.z) / 3};
                        m.triColors.push_back(faceColor(ctr, fn));
                    }
                }
            }
        }
}

// The original marching-cubes loop, unchanged apart from its name. It is the reference the tests hold
// AppendCubes to, and what OKAY_VOXEL_OLD_REMESH=1 runs.
void VoxelTerrain::AppendCubesReference(Mesh& m, int x0, int y0, int z0, int x1, int y1, int z1) const {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 > nx - 1) x1 = nx - 1;
    if (y1 > ny - 1) y1 = ny - 1;
    if (z1 > nz - 1) z1 = nz - 1;
    if (x0 >= x1 || y0 >= y1 || z0 >= z1) return;
    auto interp = [&](const Vec3& p1, const Vec3& p2, float v1, float v2) {
        float t = (std::fabs(v2 - v1) > 1e-6f) ? (iso - v1) / (v2 - v1) : 0.5f;
        return p1 + (p2 - p1) * t;
    };
    // Gradient of the density field (central differences) -> outward normal.
    auto normalAt = [&](const Vec3& local) {
        float e = voxelSize * 0.5f;
        float dx = SampleDensity({local.x + e, local.y, local.z}) - SampleDensity({local.x - e, local.y, local.z});
        float dy = SampleDensity({local.x, local.y + e, local.z}) - SampleDensity({local.x, local.y - e, local.z});
        float dz = SampleDensity({local.x, local.y, local.z + e}) - SampleDensity({local.x, local.y, local.z - e});
        Vec3 n{-dx, -dy, -dz};   // points toward decreasing density (out of solid)
        float L = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        return L > 1e-6f ? Vec3{n.x / L, n.y / L, n.z / L} : Vec3{0, 1, 0};
    };
    auto faceColor = [&](const Vec3& p, const Vec3& n) {
        float slope = 1.0f - std::fabs(n.y);
        if (p.y >= snowLevel) return snowColor;
        if (slope >= rockSlope) return rockColor;
        float t = slope / rockSlope; t = t < 0 ? 0 : (t > 1 ? 1 : t);
        return Color{grassColor.r + (soilColor.r - grassColor.r) * t,
                     grassColor.g + (soilColor.g - grassColor.g) * t,
                     grassColor.b + (soilColor.b - grassColor.b) * t, 1.0f};
    };

    for (int z = z0; z < z1; ++z)
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                float val[8]; Vec3 pos[8];
                int cubeindex = 0;
                for (int i = 0; i < 8; ++i) {
                    int cx = x + kCorner[i][0], cy = y + kCorner[i][1], cz = z + kCorner[i][2];
                    val[i] = density[Index(cx, cy, cz)];
                    pos[i] = GridToLocal(cx, cy, cz);
                    if (val[i] < iso) cubeindex |= (1 << i);
                }
                int edges = kEdge[cubeindex];
                if (edges == 0) continue;
                Vec3 vert[12];
                for (int e = 0; e < 12; ++e) {
                    if (!(edges & (1 << e))) continue;
                    int a = kEdgeC[e][0], b = kEdgeC[e][1];
                    vert[e] = interp(pos[a], pos[b], val[a], val[b]);
                }
                for (int t = 0; kTri[cubeindex][t] != -1; t += 3) {
                    Vec3 a = vert[kTri[cubeindex][t]];
                    Vec3 b = vert[kTri[cubeindex][t + 1]];
                    Vec3 c = vert[kTri[cubeindex][t + 2]];
                    Vec3 na = normalAt(a), nb = normalAt(b), nc = normalAt(c);
                    Vec3 g = Vec3::Cross(b - a, c - a);
                    Vec3 navg{na.x + nb.x + nc.x, na.y + nb.y + nc.y, na.z + nb.z + nc.z};
                    if (g.x * navg.x + g.y * navg.y + g.z * navg.z < 0.0f) {
                        std::swap(b, c); std::swap(nb, nc);
                    }
                    int base = (int)m.vertices.size();
                    m.vertices.push_back(a); m.vertices.push_back(b); m.vertices.push_back(c);
                    m.normals.push_back(na); m.normals.push_back(nb); m.normals.push_back(nc);
                    m.uvs.push_back({a.x * 0.25f, a.z * 0.25f});
                    m.uvs.push_back({b.x * 0.25f, b.z * 0.25f});
                    m.uvs.push_back({c.x * 0.25f, c.z * 0.25f});
                    m.triangles.push_back(base); m.triangles.push_back(base + 1); m.triangles.push_back(base + 2);
                    if (autoColor) {
                        Vec3 ctr{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
                        Vec3 fn{(na.x + nb.x + nc.x) / 3, (na.y + nb.y + nc.y) / 3, (na.z + nb.z + nc.z) / 3};
                        m.triColors.push_back(faceColor(ctr, fn));
                    }
                }
            }
}

Mesh VoxelTerrain::BuildMesh() const {
    Mesh m;
    m.name = "VoxelTerrain";
    AppendCubes(m, 0, 0, 0, nx - 1, ny - 1, nz - 1);
    return m;
}

Mesh VoxelTerrain::BuildMeshReference() const {
    Mesh m;
    m.name = "VoxelTerrain";
    AppendCubesReference(m, 0, 0, 0, nx - 1, ny - 1, nz - 1);
    return m;
}

bool VoxelTerrain::SealFloor() {
    if (floorLayers <= 0 || nx <= 0 || ny <= 0 || nz <= 0) return false;
    const int yMax = floorLayers < ny ? floorLayers : ny;
    const float solid = Clamp();
    bool changed = false;
    for (int y = 0; y < yMax; ++y)
        for (int z = 0; z < nz; ++z)
            for (int x = 0; x < nx; ++x) {
                float& c = density[Index(x, y, z)];
                if (c <= iso) { c = solid; changed = true; }
            }
    return changed;
}

unsigned VoxelTerrain::MeshStamp() const {
    unsigned h = 2166136261u;
    auto mix = [&](float f) {
        unsigned u = 0;
        std::memcpy(&u, &f, sizeof(u));
        h = h * 16777619u ^ u;
    };
    mix(iso); mix(snowLevel); mix(rockSlope);
    mix(grassColor.r); mix(grassColor.g); mix(grassColor.b);
    mix(soilColor.r); mix(soilColor.g); mix(soilColor.b);
    mix(rockColor.r); mix(rockColor.g); mix(rockColor.b);
    mix(snowColor.r); mix(snowColor.g); mix(snowColor.b);
    h ^= autoColor ? 1u : 0u;
    h = h * 16777619u ^ matRev_;
    h ^= UseMaterials() ? 2u : 0u;
    return h;
}

// ---- chunk table and the hand-off to the renderer ------------------------------------------------
bool VoxelTerrain::RunningGame() const {
    const Scene* s = gameObject ? gameObject->scene() : nullptr;
    return s && s->runScripts;
}

// Components that read the terrain object's own mesh (a collider is fitted to it or cuts triangles from it, an
// occluder or LOD group draws from it). With chunk objects that mesh is empty, so Auto leaves such a terrain on the
// one combined mesh. (Chunked, asked for by name, does not look: the caller knows.)
bool VoxelTerrain::MeshReadByOthers() const {
    return gameObject && (gameObject->GetComponent<Collider3D>() || gameObject->GetComponent<LODGroup>() ||
                          gameObject->GetComponent<Occluder>());
}

void VoxelTerrain::CubeRange(int cx, int cy, int cz, int& x1, int& y1, int& z1) const {
    x1 = std::min(cx * kChunk + kChunk, std::max(0, nx - 1));
    y1 = std::min(cy * kChunk + kChunk, std::max(0, ny - 1));
    z1 = std::min(cz * kChunk + kChunk, std::max(0, nz - 1));
}

// (Re)build the chunk table when the grid, the voxel size or anything that changes the mesh itself
// (iso, colours) changed. Returns true when it did: every chunk is then meshed again.
bool VoxelTerrain::EnsureChunkTable() {
    const int cubesX = std::max(0, nx - 1), cubesY = std::max(0, ny - 1), cubesZ = std::max(0, nz - 1);
    const int ncx = std::max(1, (cubesX + kChunk - 1) / kChunk);
    const int ncy = std::max(1, (cubesY + kChunk - 1) / kChunk);
    const int ncz = std::max(1, (cubesZ + kChunk - 1) / kChunk);
    const unsigned stamp = MeshStamp();
    const int nChunks = ncx * ncy * ncz;
    if (mcNx_ == nx && mcNy_ == ny && mcNz_ == nz && mcVox_ == voxelSize && mcStamp_ == stamp &&
        (int)mcChunks_.size() == nChunks && (int)mcDirty_.size() == nChunks)
        return false;
    DestroyChunkObjects();
    mcChunks_.assign((std::size_t)nChunks, McChunk{});
    mcDirty_.assign((std::size_t)nChunks, 0);
    mcDirtyCount_ = 0;
    mcNx_ = nx; mcNy_ = ny; mcNz_ = nz; mcVox_ = voxelSize; mcStamp_ = stamp;
    ncx_ = ncx; ncy_ = ncy; ncz_ = ncz;
    mcAllDirty_ = true;
    combinedPtr_ = nullptr; combinedVerts_ = 0;
    return true;
}

void VoxelTerrain::CombineInto(MeshRenderer& mr) {
    Mesh combined;
    combined.name = "VoxelTerrain";
    std::size_t nv = 0, nt = 0, nc = 0;
    for (const McChunk& c : mcChunks_) {
        nv += c.mesh.vertices.size();
        nt += c.mesh.triangles.size();
        nc += c.mesh.triColors.size();
    }
    combined.vertices.reserve(nv);
    combined.normals.reserve(nv);
    combined.uvs.reserve(nv);
    combined.triangles.reserve(nt);
    if (nc) combined.triColors.reserve(nc);
    for (const McChunk& c : mcChunks_) {
        const int base = (int)combined.vertices.size();
        combined.vertices.insert(combined.vertices.end(), c.mesh.vertices.begin(), c.mesh.vertices.end());
        combined.normals.insert(combined.normals.end(), c.mesh.normals.begin(), c.mesh.normals.end());
        combined.uvs.insert(combined.uvs.end(), c.mesh.uvs.begin(), c.mesh.uvs.end());
        for (int idx : c.mesh.triangles) combined.triangles.push_back(base + idx);
        if (!c.mesh.triColors.empty())
            combined.triColors.insert(combined.triColors.end(), c.mesh.triColors.begin(), c.mesh.triColors.end());
    }
    lastStats.bytesHandedOff += MeshBytes(combined);
    // A fresh Mesh (new buffers) so the renderer uploads this frame. An in-place
    // edit of the old vertex vector can sit unseen until a sampled hash notices.
    mr.mesh = std::move(combined);
    combinedPtr_ = mr.mesh.vertices.data();
    combinedVerts_ = mr.mesh.vertices.size();
}

GameObject* VoxelTerrain::CreateChunkObject(int cx, int cy, int cz) {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc) return nullptr;
    GameObject* g = sc->CreateGameObject("Voxel Chunk " + std::to_string(cx) + "," + std::to_string(cy) + "," + std::to_string(cz));
    if (!g) return nullptr;
    g->transient = true;                      // runtime only: the scene file never gets these
    g->layer = gameObject->layer;
    if (transform && g->transform) g->transform->SetParent(transform, /*worldPositionStays=*/false);
    g->AddComponent<MeshRenderer>();
    return g;
}

void VoxelTerrain::HandOffChunk(int ci, int cx, int cy, int cz, Mesh&& fresh, const MeshRenderer& parent) {
    McChunk& c = mcChunks_[(std::size_t)ci];
    GameObject* g = c.obj ? GameObject::FromInstanceId(c.obj) : nullptr;
    const bool has = !fresh.vertices.empty();
    if (!g) {
        c.obj = 0;
        if (!has) { c.lastVerts = 0; return; }       // a chunk that never had a surface gets no object
        g = CreateChunkObject(cx, cy, cz);
        if (!g) return;
        c.obj = g->InstanceId();
        MirrorMaterial(parent, *g->GetComponent<MeshRenderer>());
    }
    MeshRenderer* cm = g->GetComponent<MeshRenderer>();
    if (!cm) { cm = g->AddComponent<MeshRenderer>(); MirrorMaterial(parent, *cm); }
    c.lastVerts = (int)fresh.vertices.size();
    ++lastStats.chunksHandedOff;
    lastStats.bytesHandedOff += MeshBytes(fresh);
    // Brand-new buffers (the old ones are still alive while these are allocated), so the renderer's
    // pointer check sees a different mesh and re-uploads THIS chunk, and only this chunk.
    cm->mesh = std::move(fresh);
}

void VoxelTerrain::EnterChunked(MeshRenderer& parent) {
    chunked_ = true;
    if (!mcAllDirty_) {
        int ci = 0;
        for (int cz = 0; cz < ncz_; ++cz)
            for (int cy = 0; cy < ncy_; ++cy)
                for (int cx = 0; cx < ncx_; ++cx, ++ci) {
                    McChunk& c = mcChunks_[(std::size_t)ci];
                    if (c.mesh.vertices.empty()) continue;
                    if (mcDirty_[(std::size_t)ci]) { c.mesh = Mesh{}; continue; }   // replaced right after this
                    Mesh keep = std::move(c.mesh);
                    c.mesh = Mesh{};
                    HandOffChunk(ci, cx, cy, cz, std::move(keep), parent);
                }
    } else {
        for (McChunk& c : mcChunks_) c.mesh = Mesh{};
    }
    parent.mesh = Mesh{};          // the chunk objects draw the surface now; no second copy
    parent.mesh.name = "VoxelTerrain";   // (a saved scene still names it; the terrain rebuilds the surface on load)
    combinedPtr_ = nullptr; combinedVerts_ = 0;
    matSig_ = 0;                   // copy the material to every chunk on the next sync
}

void VoxelTerrain::LeaveChunked() {
    DestroyChunkObjects();
    chunked_ = false;
    mcAllDirty_ = true;            // the combined mesh is built again from the field
    combinedPtr_ = nullptr; combinedVerts_ = 0;
}

void VoxelTerrain::SyncChunkMaterials(const MeshRenderer& parent) {
    for (McChunk& c : mcChunks_) {
        if (!c.obj) continue;
        GameObject* g = GameObject::FromInstanceId(c.obj);
        if (!g) continue;
        if (MeshRenderer* cm = g->GetComponent<MeshRenderer>()) MirrorMaterial(parent, *cm);
    }
    matSig_ = MaterialSig(parent);
}

void VoxelTerrain::DestroyChunkObjects() {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    for (McChunk& c : mcChunks_) {
        if (!c.obj) continue;
        GameObject* g = GameObject::FromInstanceId(c.obj);
        if (g) {
            g->active = false;           // gone from this frame's picture; the scene frees it at the end of the frame
            if (sc) sc->Destroy(g);
        }
        c.obj = 0;
    }
}

int VoxelTerrain::ChunkObjectCount() const {
    int n = 0;
    for (const McChunk& c : mcChunks_)
        if (c.obj && GameObject::FromInstanceId(c.obj)) ++n;
    return n;
}

int VoxelTerrain::CatchFallers() {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc || !gameObject->transform) return 0;
    Transform* tt = gameObject->transform;
    int n = 0;
    for (Rigidbody3D* rb : sc->FindObjectsOfType<Rigidbody3D>()) {
        if (!rb->gameObject || !rb->gameObject->transform || rb->gameObject == gameObject) continue;
        const Vec3 l = tt->InverseTransformPoint(rb->gameObject->transform->Position());
        if (!std::isfinite(l.x) || !std::isfinite(l.y) || !std::isfinite(l.z) || l.y >= -catchMargin * voxelSize) continue;
        ++n; ++caughtCount;
        if (onFall) { onFall(*rb->gameObject); continue; }
        Vec3 in{std::max(-HalfX(), std::min(HalfX(), l.x)), 0.0f, std::max(-HalfZ(), std::min(HalfZ(), l.z))};
        float sy = 0.0f;
        in.y = (SurfaceY(in.x, in.z, sy) ? sy : SizeY() * 0.5f) + 2.0f * voxelSize;
        rb->gameObject->transform->SetPosition(tt->TransformPoint(in));
        rb->velocity = Vec3::Zero;
    }
    return n;
}

void VoxelTerrain::Update(float) {
    if (catchFalling && ((++catchTick_) % 6) == 0) CatchFallers();
    if (!chunked_ || !gameObject) return;
    MeshRenderer* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) return;
    if (MaterialSig(*mr) != matSig_) SyncChunkMaterials(*mr);   // e.g. the Inspector changed the terrain's shader
}

void VoxelTerrain::OnDestroy() { DestroyChunkObjects(); }
void VoxelTerrain::ReleaseUnstarted() { DestroyChunkObjects(); }   // a terrain removed before it started (edit mode, tests)

void VoxelTerrain::Apply() {
    if (!gameObject) return;
    const auto tStart = Clock::now();
    lastStats = MeshStats{};
    MeshRenderer* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) mr = gameObject->AddComponent<MeshRenderer>();
    const bool oldPath = OldRemeshActive();

    // The protected floor is only looked at when something could have changed it.
    if (oldPath || mcAllDirty_ || floorTouched_ || floorLayers != sealedLayers_) {
        if (SealFloor()) mcAllDirty_ = true;
        sealedLayers_ = floorLayers;
        floorTouched_ = false;
    }

    // Marching cubes is the expensive part of a dig. The volume is split into chunks and only the chunks
    // the edits reached are meshed again. Each cube belongs to one chunk, and a normal reads the full
    // field, so the surface matches a full rebuild along the borders.
    EnsureChunkTable();
    lastStats.chunksTotal = (int)mcChunks_.size();

    // The terrain's own renderer keeps what Apply always set: colour, triplanar texture, single sided.
    mr->color = color;
    // Single-sided: the winding is forced outward in AppendCubes, so backface culling
    // safely skips the hidden ~half of the triangles (a real frame-rate win on big
    // terrains) while caves still render correctly from the inside.
    mr->doubleSided = false;
    // Triplanar ground texture (tinted by the per-face auto-colours). Triplanar
    // keeps cave walls / overhangs / floors from smearing.
    mr->texture = texture;
    if (!texture.empty()) {
        mr->tiling = {textureTiling, textureTiling};
        mr->triplanar = true;
    } else {
        mr->triplanar = false;
    }

    // How the surface goes to the renderer. See RenderMode.
    bool wantChunked = false;
    if (!oldPath) {
        if (renderMode == RenderMode::Chunked) wantChunked = true;
        else if (renderMode == RenderMode::Auto)
            wantChunked = chunked_ || (builtOnce_ && !mcAllDirty_ && mcDirtyCount_ > 0 && RunningGame() && !MeshReadByOthers());
    }
    if (wantChunked && !gameObject->scene()) wantChunked = false;   // chunk objects need a scene to live in
    if (wantChunked && !chunked_) EnterChunked(*mr);
    else if (!wantChunked && chunked_) LeaveChunked();

    double remeshMs = 0.0, handoffMs = 0.0;
    bool anyRemeshed = false;
    int ci = 0;
    for (int cz = 0; cz < ncz_; ++cz)
        for (int cy = 0; cy < ncy_; ++cy)
            for (int cx = 0; cx < ncx_; ++cx, ++ci) {
                if (!(mcAllDirty_ || mcDirty_[(std::size_t)ci])) continue;
                int x1, y1, z1;
                CubeRange(cx, cy, cz, x1, y1, z1);
                Mesh fresh;
                fresh.name = chunked_ ? "VoxelChunk" : "VoxelTerrain";
                McChunk& ch = mcChunks_[(std::size_t)ci];
                {
                    OKAY_PROFILE("Voxel.Mesh");
                    const auto t0 = Clock::now();
                    const std::size_t hint = chunked_ ? (std::size_t)ch.lastVerts : ch.mesh.vertices.size();
                    if (hint) {
                        fresh.vertices.reserve(hint + hint / 4); fresh.normals.reserve(hint + hint / 4);
                        fresh.uvs.reserve(hint + hint / 4); fresh.triangles.reserve(hint + hint / 4);
                        if (autoColor || UseMaterials()) fresh.triColors.reserve((hint + hint / 4) / 3);
                    }
                    if (oldPath) AppendCubesReference(fresh, cx * kChunk, cy * kChunk, cz * kChunk, x1, y1, z1);
                    else         AppendCubes(fresh, cx * kChunk, cy * kChunk, cz * kChunk, x1, y1, z1);
                    remeshMs += MsSince(t0);
                }
                ++lastStats.chunksRemeshed;
                lastStats.vertices += (int)fresh.vertices.size();
                lastStats.triangles += fresh.TriangleCount();
                anyRemeshed = true;
                {
                    OKAY_PROFILE("Voxel.Upload");
                    const auto t1 = Clock::now();
                    if (chunked_) HandOffChunk(ci, cx, cy, cz, std::move(fresh), *mr);
                    else { ch.lastVerts = (int)fresh.vertices.size(); ch.mesh = std::move(fresh); }
                    handoffMs += MsSince(t1);
                }
            }

    if (!chunked_) {
        // Single mesh: copy the chunks together again when anything changed (or the mesh was replaced).
        const bool stale = oldPath || anyRemeshed || combinedPtr_ != (const void*)mr->mesh.vertices.data() ||
                           combinedVerts_ != mr->mesh.vertices.size();
        if (stale) {
            OKAY_PROFILE("Voxel.Upload");
            const auto t1 = Clock::now();
            CombineInto(*mr);
            handoffMs += MsSince(t1);
        }
    } else {
        if (MaterialSig(*mr) != matSig_) SyncChunkMaterials(*mr);
    }

    std::fill(mcDirty_.begin(), mcDirty_.end(), (unsigned char)0);
    mcDirtyCount_ = 0;
    mcAllDirty_ = false;
    builtOnce_ = true;
    lastStats.chunked = chunked_;
    lastStats.remeshMs = remeshMs;
    lastStats.handoffMs = handoffMs;
    lastStats.totalMs = MsSince(tStart);
}

// ===================== Compact density (de)serialization ====================
namespace {
const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int B64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62; if (c == '/') return 63;
    return -1;
}
} // namespace

std::string VoxelTerrain::EncodeDensity() const {
    // Quantize each density to a signed byte over [-clamp, clamp] (only values
    // near the iso surface drive the mesh, so clamping the far field is lossless
    // for the surface). Then base64 the bytes.
    const float clamp = Clamp() + 1e-3f;
    std::vector<unsigned char> bytes; bytes.reserve(density.size());
    for (float d : density) {
        float v = d / clamp; if (v < -1) v = -1; if (v > 1) v = 1;
        int q = (int)std::lround(v * 127.0f);
        bytes.push_back((unsigned char)(q & 0xFF));
    }
    std::string out; out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        unsigned b0 = bytes[i];
        unsigned b1 = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
        unsigned b2 = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
        unsigned n = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kB64[(n >> 18) & 63]);
        out.push_back(kB64[(n >> 12) & 63]);
        out.push_back((i + 1 < bytes.size()) ? kB64[(n >> 6) & 63] : '=');
        out.push_back((i + 2 < bytes.size()) ? kB64[n & 63] : '=');
    }
    return out;
}

bool VoxelTerrain::DecodeDensity(const std::string& b64) {
    const float clamp = Clamp() + 1e-3f;
    std::vector<unsigned char> bytes; bytes.reserve(b64.size() / 4 * 3);
    unsigned acc = 0; int bits = 0;
    for (char c : b64) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = B64Val(c); if (v < 0) continue;
        acc = ((acc << 6) | (unsigned)v) & 0xFFFFFFu; bits += 6;
        if (bits >= 8) { bits -= 8; bytes.push_back((unsigned char)((acc >> bits) & 0xFF)); }
    }
    if (bytes.size() != density.size()) return false;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        signed char q = (signed char)bytes[i];
        density[i] = (q / 127.0f) * clamp;
    }
    mcAllDirty_ = true;
    floatersSwept_ = false;
    editAny_ = false;
    return true;
}

} // namespace okay
