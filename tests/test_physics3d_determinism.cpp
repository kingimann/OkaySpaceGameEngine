// Classic 3D physics must give the same result every time the same scene is built and stepped the same way, in the
// same process, wherever the heap happens to put its objects. A rollback, a replay or a network lockstep check rebuilds
// a scene and expects bit-identical poses.
//
// The scene is the one tests/test_jobs_adoption.cpp uses (400 mixed bodies on a floor, 40 frames). It is built several
// times; before each build the heap is stirred differently (blocks of many sizes allocated and freed in a shuffled order),
// so the objects of each copy land at different addresses and in a different address order. Every copy must match the
// first one bit for bit after every frame. When one does not, the first frame and object that differ are printed.
#include "test_framework.hpp"
#include "physics_test_scenes.hpp"
#include <Okay.hpp>
#include "okay/Physics/ColliderShapes3D.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace okay;

namespace {

template <class T> void Put(std::vector<unsigned char>& out, const T& v) { okaytest::PutBytes(out, v); }

void Build3D(Scene& s, int bodies, bool improved) {
    okaytest::BuildMixedBodies(s, bodies, improved ? PhysicsQuality::Improved : PhysicsQuality::Classic);
}

// One state record per frame per object: pose and velocities, raw bytes.
using Frames = std::vector<std::vector<std::vector<unsigned char>>>;

Frames Run(int bodies, bool improved, int frames) {
    Frames out;
    Scene s("det");
    Build3D(s, bodies, improved);
    s.Start();
    for (int f = 0; f < frames; ++f) {
        s.Update(1.0f / 60.0f);
        std::vector<std::vector<unsigned char>> frame;
        for (const auto& go : s.Objects()) {
            std::vector<unsigned char> rec;
            Put(rec, go->transform->localPosition);
            Put(rec, go->transform->localRotation);
            if (auto* rb = go->GetComponent<Rigidbody3D>()) {
                Put(rec, rb->velocity);
                Put(rec, rb->angularVelocity);
                Put(rec, rb->sleeping);
            }
            frame.push_back(std::move(rec));
        }
        out.push_back(std::move(frame));
    }
    return out;
}

// Allocate blocks of many sizes, free them in a shuffled order and keep some, so the next scene's objects land at
// addresses (and in an address order) unlike the previous one's. Returns the kept blocks; the caller frees them later.
std::vector<void*> StirHeap(unsigned seed) {
    std::vector<void*> blocks;
    unsigned rng = 12345u + seed * 7919u;
    auto next = [&]() { rng = rng * 1103515245u + 12345u; return rng >> 8; };
    const int count = 4000 + (int)(next() % 20000);
    for (int i = 0; i < count; ++i) blocks.push_back(std::malloc(16 + next() % 700));
    for (std::size_t i = blocks.size(); i > 1; --i) std::swap(blocks[i - 1], blocks[next() % i]);
    std::vector<void*> keep;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (next() % 3 == 0) keep.push_back(blocks[i]);
        else std::free(blocks[i]);
    }
    return keep;
}

// Compare b with the reference a; print the first frame and object that differ.
bool Same(const Frames& a, const Frames& b, const char* what, int copy) {
    if (a.size() != b.size()) { std::printf("  %s copy %d: frame count differs\n", what, copy); return false; }
    for (std::size_t f = 0; f < a.size(); ++f) {
        if (a[f] == b[f]) continue;
        for (std::size_t o = 0; o < a[f].size() && o < b[f].size(); ++o)
            if (a[f][o] != b[f][o]) {
                std::printf("  %s copy %d: first difference at frame %zu, object %zu\n", what, copy, f, o);
                return false;
            }
        std::printf("  %s copy %d: object count differs at frame %zu\n", what, copy, f);
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    // `test_physics3d_determinism repeat N` runs the Classic check N times in one process and prints how many copies
    // differed (for measuring a rare failure); it is not part of ctest.
    if (argc > 2 && std::strcmp(argv[1], "repeat") == 0) {
        const int n = std::atoi(argv[2]);
        const Frames ref = Run(400, false, 40);
        int bad = 0;
        for (int k = 0; k < n; ++k) {
            std::vector<void*> keep = StirHeap((unsigned)k);
            bad += !Same(ref, Run(400, false, 40), "Classic", k + 1);
            for (void* p : keep) std::free(p);
        }
        std::printf("Classic: %d of %d copies differ from the first\n", bad, n);
        return bad ? 1 : 0;
    }

    RUN_SUITE("physics3d_determinism");

    // The cause found for the rare mismatch: the "where are its feet" test that puts a body back on a thin floor it fell
    // through (CatchTunneledFloor) asked a Compound collider for its lowest point as if it were a box, reading the
    // compound's part text (a heap pointer) as the box size. A compound whose only part hangs 1 m below its origin falls
    // fast through a thin floor in one step: it must be put back standing on the floor by that part.
    for (int q = 0; q < 2; ++q) {
        Scene s("feet");
        s.physics3D().quality = q ? PhysicsQuality::Improved : PhysicsQuality::Classic;
        s.physics3D().autoContinuous = false;   // (Improved would sweep the body instead; the rescue is what is tested)
        GameObject* floor = s.CreateGameObject("Floor");
        floor->AddComponent<BoxCollider3D>()->size = {20, 0.05f, 20};   // top at y = 0.025
        floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        GameObject* g = s.CreateGameObject("Hanging");
        g->transform->localPosition = {0, 2.6f, 0};                       // the ball's bottom starts at 1.3
        auto* cc = g->AddComponent<CompoundCollider3D>();
        cc->AddSphere({0, -1.0f, 0}, 0.3f);
        auto* rb = g->AddComponent<Rigidbody3D>();
        rb->velocity = {0, -60.0f, 0};                                    // about 1 m per step: the ball skips the floor
        s.fixedTimeStep = 0.0f;
        s.Start();
        for (int f = 0; f < 30; ++f) s.Update(1.0f / 60.0f);
        Vec3 mn, mx;
        cc->WorldAABB(mn, mx);
        std::printf("  %s: compound origin y %.3f, lowest point %.3f (floor top 0.025)\n", q ? "Improved" : "Classic",
                    g->transform->localPosition.y, mn.y);
        CHECK(mn.y > -0.03f && mn.y < 0.2f);   // standing on the floor, not under it
    }

    for (int q = 0; q < 2; ++q) {
        const bool improved = q != 0;
        const char* what = improved ? "Improved" : "Classic";
        const Frames ref = Run(400, improved, 40);
        CHECK(ref.size() == 40 && !ref[0].empty());
        for (int k = 0; k < 6; ++k) {
            std::vector<void*> keep = StirHeap((unsigned)(k + 100 * q));
            const Frames again = Run(400, improved, 40);
            CHECK(Same(ref, again, what, k + 1));
            for (void* p : keep) std::free(p);
        }
    }

    TEST_MAIN_RESULT();
}
