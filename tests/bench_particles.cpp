// Particle benchmark: how many particles one Update moves per millisecond, and what a
// system costs in memory. Prints a table (not a pass/fail speed gate: a loaded machine must
// not fail CI). The same file compiles against the old and the new ParticleSystem, which is
// how the before/after numbers in docs/particles_upgrade.md were taken.
#include "test_framework.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include <chrono>
#include <cstdio>

using namespace okay;

namespace {
struct Row { const char* name; int particles; double msPerFrame; double perMs; };

Row Run(const char* name, ParticleSystem::Preset preset, int cap, int frames) {
    ParticleSystem object;   // no scene needed: the system runs on its own (no emitter motion, no colliders)
    ParticleSystem* ps = &object;
    ps->ApplyPreset(preset);
    ps->maxParticles = cap;
    ps->emissionRate = (float)cap / 2.0f;   // fills the pool in about two seconds
    ps->startLifetime = 4.0f;
    ps->startLifetimeRandom = 0.0f;
    ps->burstCount = 0;
    ps->duration = 0.0f;
    ps->Awake();
    for (int i = 0; i < 300; ++i) ps->Update(1.0f / 60.0f);   // warm up to a steady pool
    const int alive = ps->AliveCount();
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) ps->Update(1.0f / 60.0f);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / frames;
    return {name, alive, ms, ms > 0.0 ? alive / ms : 0.0};
}
}

int main() {
    RUN_SUITE("bench_particles");
    std::printf("\n%-18s %10s %12s %14s\n", "scene", "alive", "ms/frame", "particles/ms");
    const Row rows[] = {
        Run("sparks 500",    ParticleSystem::Preset::Sparks, 500, 600),
        Run("fire 4000",     ParticleSystem::Preset::Fire, 4000, 300),
        Run("smoke 20000",   ParticleSystem::Preset::Smoke, 20000, 100),
        Run("rain 50000",    ParticleSystem::Preset::Rain, 50000, 40),
    };
    for (const Row& r : rows) std::printf("%-18s %10d %12.4f %14.0f\n", r.name, r.particles, r.msPerFrame, r.perMs);
    std::printf("\n");
    for (const Row& r : rows) CHECK(r.particles > 0);
    TEST_MAIN_RESULT();
}
