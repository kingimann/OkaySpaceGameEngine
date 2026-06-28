#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"

#include <cmath>
#include <algorithm>
#include <array>

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
void VoxelTerrain::Resize(int X, int Y, int Z) {
    nx = X < 2 ? 2 : X; ny = Y < 2 ? 2 : Y; nz = Z < 2 ? 2 : Z;
    density.assign((std::size_t)nx * ny * nz, -1.0f);   // all air
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
}

void VoxelTerrain::Dig(const Vec3& local, float radius, float amount) {
    float gx = (local.x + HalfX()) / voxelSize;
    float gy = local.y / voxelSize;
    float gz = (local.z + HalfZ()) / voxelSize;
    float gr = radius / voxelSize;
    int x0 = std::max(0, (int)std::floor(gx - gr)), x1 = std::min(nx - 1, (int)std::ceil(gx + gr));
    int y0 = std::max(0, (int)std::floor(gy - gr)), y1 = std::min(ny - 1, (int)std::ceil(gy + gr));
    int z0 = std::max(0, (int)std::floor(gz - gr)), z1 = std::min(nz - 1, (int)std::ceil(gz + gr));
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                float dx = x - gx, dy = y - gy, dz = z - gz;
                float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d > gr) continue;
                float fall = 1.0f - d / gr;               // soft sphere
                float& cell = density[Index(x, y, z)];
                cell = ClampTo(cell - amount * fall, Clamp());   // subtract = carve air
            }
}

void VoxelTerrain::Add(const Vec3& local, float radius, float amount) {
    Dig(local, radius, -amount);   // adding is digging with negative amount
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

Mesh VoxelTerrain::BuildMesh() const {
    Mesh m;
    m.name = "VoxelTerrain";
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

    for (int z = 0; z < nz - 1; ++z)
        for (int y = 0; y < ny - 1; ++y)
            for (int x = 0; x < nx - 1; ++x) {
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
                    int base = (int)m.vertices.size();
                    m.vertices.push_back(a); m.vertices.push_back(b); m.vertices.push_back(c);
                    Vec3 na = normalAt(a), nb = normalAt(b), nc = normalAt(c);
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
    return m;
}

void VoxelTerrain::Apply() {
    if (!gameObject) return;
    auto* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) mr = gameObject->AddComponent<MeshRenderer>();
    mr->mesh = BuildMesh();
    mr->color = color;
    mr->doubleSided = true;
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
    int acc = 0, bits = 0;
    for (char c : b64) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = B64Val(c); if (v < 0) continue;
        acc = (acc << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; bytes.push_back((unsigned char)((acc >> bits) & 0xFF)); }
    }
    if (bytes.size() != density.size()) return false;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        signed char q = (signed char)bytes[i];
        density[i] = (q / 127.0f) * clamp;
    }
    return true;
}

} // namespace okay
