#include "test_framework.hpp"
#include <Okay.hpp>
#include <string>

using namespace okay;

// Helper: spawn a voxel block (matches what BlockBuilder places) at a cell.
static GameObject* MakeBlock(Scene& s, const Vec3& pos, const std::string& tag = "Block") {
    GameObject* b = s.CreateGameObject("Block");
    b->tag = tag;
    b->transform->localPosition = pos;
    b->transform->localScale = {1, 1, 1};
    auto* mr = b->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube();
    mr->color = Color::FromBytes(170, 172, 182);
    b->AddComponent<BoxCollider3D>();
    return b;
}

static int CountTag(Scene& s, const std::string& tag) {
    int n = 0;
    for (const auto& up : s.Objects()) if (up && up->tag == tag) ++n;
    return n;
}

// Chaos destruction: fracture a block into physics debris, and collapse blocks
// that lose their connection to the ground.
int main() {
    RUN_SUITE("destructible");

    // --- Fracture: one block -> a cluster of debris shards with velocity -------
    {
        Scene s("frac");
        GameObject* d = s.CreateGameObject("Destroyer");
        auto* de = d->AddComponent<Destructible>();
        de->fractureChunks = 2;          // 2^3 = 8 shards
        de->collapseEnabled = false;
        GameObject* blk = MakeBlock(s, Vec3{0, 3, 0});

        CHECK(de->BreakBlock(blk, Vec3{0, 0, 0}));
        s.Update(0.016f);                // flush the queued Destroy of the original

        CHECK(CountTag(s, "Block") == 0);    // solid cube gone
        CHECK(CountTag(s, "Debris") == 8);   // shattered into 8
        // Every shard is a dynamic rigidbody that's actually moving.
        int moving = 0;
        for (const auto& up : s.Objects()) {
            if (!up || up->tag != "Debris") continue;
            auto* rb = up->GetComponent<Rigidbody3D>();
            CHECK(rb != nullptr);
            if (rb) {
                Vec3 v = rb->velocity;
                if (v.x*v.x + v.y*v.y + v.z*v.z > 0.0f) ++moving;
            }
            // Shard is 1/2 the size of the parent on each axis.
            CHECK_NEAR(up->transform->localScale.x, 0.5f, 0.001f);
        }
        CHECK(moving == 8);
    }

    // --- Debris despawns after its lifetime (TimedDestroy) --------------------
    {
        Scene s("life");
        GameObject* d = s.CreateGameObject("Destroyer");
        auto* de = d->AddComponent<Destructible>();
        de->fractureChunks = 2;
        de->debrisLifetime = 0.5f;
        de->collapseEnabled = false;
        de->debrisGravityScale = 0.0f;   // keep them put so only the timer matters
        MakeBlock(s, Vec3{0, 5, 0});
        de->BreakAt(Vec3{0, 5, 0}, 1.0f);
        s.Update(0.016f);
        CHECK(CountTag(s, "Debris") == 8);
        // Advance past the lifetime; the shards should clean themselves up.
        for (int i = 0; i < 40; ++i) s.Update(0.016f);   // ~0.64s
        CHECK(CountTag(s, "Debris") == 0);
    }

    // --- Structural collapse: knock out the base -> the top loses support -----
    {
        Scene s("collapse");
        GameObject* d = s.CreateGameObject("Destroyer");
        auto* de = d->AddComponent<Destructible>();
        de->fractureChunks = 1;          // keep debris count low/irrelevant here
        de->anchorY = 0.5f;              // only y<=0.5 blocks are ground-anchored

        // A 1x4 column: base on the ground (y=0), three stacked above.
        GameObject* base = MakeBlock(s, Vec3{0, 0, 0});
        MakeBlock(s, Vec3{0, 1, 0});
        MakeBlock(s, Vec3{0, 2, 0});
        MakeBlock(s, Vec3{0, 3, 0});

        // Nothing floating yet -> a collapse pass converts nothing.
        CHECK(de->CollapseUnsupported() == 0);

        // Blow up the base: now the three above have no path to the ground.
        CHECK(de->BreakBlock(base));     // BreakBlock auto-runs collapse? no (collapse via BreakAt)
        int collapsed = de->CollapseUnsupported();
        CHECK(collapsed == 3);           // the three stacked blocks start to fall

        s.Update(0.016f);                // flush destroy of base + let physics step
        // The three formerly-static blocks are now dynamic rigidbodies.
        int dyn = 0;
        for (const auto& up : s.Objects()) {
            if (!up || up->tag != "Block") continue;
            if (auto* rb = up->GetComponent<Rigidbody3D>())
                if (rb->bodyType == Rigidbody3D::BodyType::Dynamic) ++dyn;
        }
        CHECK(dyn == 3);
    }

    // --- BreakAt radius + auto-collapse end to end ---------------------------
    {
        Scene s("breakat");
        GameObject* d = s.CreateGameObject("Destroyer");
        auto* de = d->AddComponent<Destructible>();
        de->fractureChunks = 1;
        de->anchorY = 0.5f;

        // An "arch": two legs (y=0,1) and a top block bridging at y=2 over the gap.
        MakeBlock(s, Vec3{0, 0, 0});
        MakeBlock(s, Vec3{0, 1, 0});
        MakeBlock(s, Vec3{0, 2, 0});   // connected to the left leg
        MakeBlock(s, Vec3{1, 2, 0});   // floating bridge segment, only held via {0,2,0}

        // Destroy the left leg base; everything above + the bridge should detach.
        int broke = de->BreakAt(Vec3{0, 0, 0}, 0.4f, Vec3{0, 0, 0});
        CHECK(broke == 1);             // only the base in radius
        s.Update(0.016f);

        // {0,1},{0,2},{1,2} all lost their ground path -> dynamic & falling.
        int dyn = 0;
        for (const auto& up : s.Objects()) {
            if (!up || up->tag != "Block") continue;
            if (auto* rb = up->GetComponent<Rigidbody3D>())
                if (rb->bodyType == Rigidbody3D::BodyType::Dynamic) ++dyn;
        }
        CHECK(dyn == 3);
    }

    // --- Serialization round-trip of the config ------------------------------
    {
        Scene s("ser");
        GameObject* d = s.CreateGameObject("Destroyer");
        auto* de = d->AddComponent<Destructible>();
        de->blockTag = "Voxel";
        de->voxelSize = 2.0f;
        de->fractureChunks = 3;
        de->explosionForce = 9.5f;
        de->collapseEnabled = false;
        de->anchorY = 1.25f;
        de->breakButton = 0;
        de->breakRadius = 2.5f;

        std::string text = SceneSerializer::SerializeObject(*d);
        Scene re("re");
        std::string err;
        GameObject* root = SceneSerializer::InstantiateFromText(re, text, &err);
        CHECK(root != nullptr);
        auto* de2 = root ? root->GetComponent<Destructible>() : nullptr;
        CHECK(de2 != nullptr);
        if (de2) {
            CHECK(de2->blockTag == "Voxel");
            CHECK_NEAR(de2->voxelSize, 2.0f, 0.01f);
            CHECK(de2->fractureChunks == 3);
            CHECK_NEAR(de2->explosionForce, 9.5f, 0.01f);
            CHECK(de2->collapseEnabled == false);
            CHECK_NEAR(de2->anchorY, 1.25f, 0.01f);
            CHECK(de2->breakButton == 0);
            CHECK_NEAR(de2->breakRadius, 2.5f, 0.01f);
        }
    }

    TEST_MAIN_RESULT();
}
