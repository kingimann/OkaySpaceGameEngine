// Subnormal floats in a scene file (Grok, issue #888 finding 4). libc++ (clang on macOS, Emscripten for the WebGL build)
// sets failbit when a float underflows (strtof's ERANGE) although it stores the value, so a scene holding one number
// between about 1.4e-45 and 1.17e-38 failed to load there; libstdc++ (GCC, this CI) does not. The scene reader's number
// facet now keeps an underflow, on the main stream and on every per-line stream. Under libstdc++ these checks pass
// before and after the fix; they are the ones that fail under libc++ without it. Written for this engine.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <cstring>
#include <string>

using namespace okay;

static std::uint32_t Bits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }

int main() {
    RUN_SUITE("scene subnormal floats load (main stream and per-line streams)");
    const float tiny[] = {1e-45f, -1e-45f, 1e-40f, 1.1754942e-38f};
    for (float t : tiny) {
        Scene a("a");
        GameObject* go = a.CreateGameObject("T");
        go->AddComponent<Mover>()->velocity = {t, 1.0f, -t};               // read from the main stream
        auto* look = go->AddComponent<LookAtIK>();
        look->smoothing = t;                                              // read from a per-line stream (the rest of the line)
        look->clampAngle = -t;                                            // a key=value on that line stream
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("b");
        std::string err;
        const bool ok = SceneSerializer::Deserialize(b, text, &err);
        if (!ok) std::fprintf(stderr, "  %g: %s\n", (double)t, err.c_str());
        CHECK(ok);
        const auto roots = b.Roots();
        CHECK(roots.size() == 1);
        if (roots.empty()) continue;
        auto* mv = roots[0]->GetComponent<Mover>();
        auto* lk = roots[0]->GetComponent<LookAtIK>();
        CHECK(mv && Bits(mv->velocity.x) == Bits(t) && Bits(mv->velocity.z) == Bits(-t) && mv->velocity.y == 1.0f);
        CHECK(lk && Bits(lk->smoothing) == Bits(t) && Bits(lk->clampAngle) == Bits(-t));
    }
    {   // a malformed number still fails the load (the facet only forgives an underflow)
        Scene b("b");
        std::string err;
        CHECK(!SceneSerializer::Deserialize(b, "okayscene 3\ngameobject 0 \"T\"\n  active 1\n  parent -1\n  transform x 0 0 0 0 0 1 1 1 1\nend\n", &err) ||
              !err.empty());
    }
    TEST_MAIN_RESULT();
}
