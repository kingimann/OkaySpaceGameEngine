// One physics path (owner, 2026-11-22: "No classic, replace with improved. I don't want duplicates."; "You're improving
// the engine, not making copies."). 3D physics always runs our own engine core; 2D has one step.
//  - Old scene files that saved the physics quality as Classic (0), Improved (1) or Engine (2), or not at all, still
//    load, and save back with every line unchanged (the value is kept only so the file round-trips).
//  - The value changes nothing: the same scene stepped with each value gives the same bits (3D and 2D).
#include "test_framework.hpp"
#include "physics_test_scenes.hpp"
#include <Okay.hpp>
#include "okay/Core/StringId.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace okay;

namespace {

std::string Replace(std::string s, const std::string& from, const std::string& to) {
    const std::size_t at = s.find(from);
    if (at != std::string::npos) s.replace(at, from.size(), to);
    return s;
}

// A small scene with physics records worth keeping (gravity, iterations, bodies, a joint).
std::string BaseFile(int iterations) {
    Scene s("old");
    s.physics3D().quality = PhysicsQuality::Improved;
    s.physics().quality = PhysicsQuality::Improved;
    s.physics3D().solverIterations = iterations;
    s.physics3D().gravity = {0, -9.0f, 0};
    GameObject* floor = s.CreateGameObject("Floor");
    floor->AddComponent<BoxCollider3D>()->size = {10, 1, 10};
    GameObject* c = s.CreateGameObject("Crate");
    c->transform->localPosition = {0, 2, 0};
    c->AddComponent<BoxCollider3D>();
    c->AddComponent<Rigidbody3D>();
    auto* j = c->AddComponent<Joint3D>();
    j->mode = (int)Joint3D::Mode::Hinge;
    j->anchor = {0, 3, 0};
    return SceneSerializer::Serialize(s);
}

std::uint64_t Run3D(PhysicsQuality q) {
    Scene s("q");
    okaytest::BuildMixedBodies(s, 120, q, true);
    s.Start();
    for (int i = 0; i < 90; ++i) s.Update(1.0f / 60.0f);
    std::vector<unsigned char> bytes;
    for (const auto& go : s.Objects()) {
        okaytest::PutBytes(bytes, go->transform->localPosition);
        okaytest::PutBytes(bytes, go->transform->localRotation);
    }
    return StringId::HashOf(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::uint64_t Run2D(PhysicsQuality q) {
    Scene s("q2");
    s.physics().quality = q;
    GameObject* floor = s.CreateGameObject("Floor");
    floor->transform->localPosition = {0, -0.5f, 0};
    floor->AddComponent<BoxCollider2D>()->size = {40, 1};
    for (int i = 0; i < 30; ++i) {
        GameObject* g = s.CreateGameObject("B" + std::to_string(i));
        g->transform->localPosition = {(float)(i % 6) * 1.1f - 3.0f, 1.0f + (float)(i / 6) * 1.1f, 0};
        auto* c = g->AddComponent<BoxCollider2D>();
        c->offset = {0.1f, 0.0f};
        g->transform->localRotation = Quat::Euler(0, 0, (float)(i * 7 % 30));
        g->AddComponent<Rigidbody2D>();
    }
    s.Start();
    for (int i = 0; i < 90; ++i) s.Update(1.0f / 60.0f);
    std::vector<unsigned char> bytes;
    for (const auto& go : s.Objects()) okaytest::PutBytes(bytes, go->transform->localPosition);
    return StringId::HashOf(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

} // namespace

int main() {
    RUN_SUITE("one physics path: old files with any physics quality load and save back unchanged");
    {
        // the record as a file of each kind wrote it: "physicsquality <3D> <iterations> <catch fast> <2D> <iterations>"
        const std::string improved = BaseFile(12);
        const std::string line = "physicsquality 1 12 1 1 10";
        CHECK(improved.find(line) != std::string::npos);
        const std::string files[] = {
            Replace(improved, line, "physicsquality 0 12 1 0 10"),   // saved as Classic
            improved,                                              // saved as Improved
            Replace(improved, line, "physicsquality 2 12 1 1 10"),   // saved as Engine (3D) and Improved (2D)
            BaseFile(10),                                          // no record at all (the old default, Classic)
        };
        int i = 0;
        for (const std::string& old : files) {
            Scene s("load");
            std::string err;
            CHECK(SceneSerializer::Deserialize(s, old, &err));
            CHECK(s.Find("Crate") && s.Find("Crate")->GetComponent<Joint3D>());
            CHECK(s.physics3D().solverIterations == (i == 3 ? 10 : 12));
            const std::string again = SceneSerializer::Serialize(s);
            if (again != old) std::cerr << "  file " << i << " changed on save\n";
            CHECK(again == old);
            ++i;
        }
    }

    RUN_SUITE("one physics path: the quality value changes nothing (3D and 2D)");
    {
        const std::uint64_t c3 = Run3D(PhysicsQuality::Classic), i3 = Run3D(PhysicsQuality::Improved), e3 = Run3D(PhysicsQuality::Engine);
        CHECK(c3 == i3);
        CHECK(i3 == e3);
        const std::uint64_t c2 = Run2D(PhysicsQuality::Classic), i2 = Run2D(PhysicsQuality::Improved);
        CHECK(c2 == i2);
    }
    TEST_MAIN_RESULT();
}
