#pragma once
// ModelingStarters: one-click ready-made models for the Shape tab (tree, house, car, character head, sword, table, rocket).
// Each one is built from the engine's own shapes (Mesh::Cube, Cylinder, Cone, Sphere ...) put together with Mesh::Add /
// AddPosed, every part coloured, so a beginner starts from something and changes it. Parts overlap a little instead of
// touching exactly, so the result is a set of closed solids (no shared, half-open edges). Written from scratch.
#include "okay/Render/Mesh.hpp"
#include <string>
#include <vector>

namespace oked::easymodel {

struct Starter { const char* id; const char* name; const char* tip; };

inline const std::vector<Starter>& Starters() {
    static const std::vector<Starter> s = {
        {"tree",   "Tree",           "A tree with a brown trunk and a green top."},
        {"house",  "House",          "A house with walls and a red roof."},
        {"car",    "Car",            "A little car with four wheels."},
        {"head",   "Character head", "A round head with eyes and a nose."},
        {"sword",  "Sword",          "A sword with a blade, a guard and a handle."},
        {"table",  "Table",          "A table with four legs."},
        {"rocket", "Rocket",         "A rocket with a pointy top and three fins."},
    };
    return s;
}

namespace detail {
using okay::Mesh; using okay::Vec3; using okay::Color;
inline void Part(Mesh& m, const Mesh& src, Vec3 at, Vec3 size, Color c) { m.Add(src, at, size, &c); }
inline void PartTurned(Mesh& m, const Mesh& src, Vec3 at, Vec3 size, Vec3 deg, Color c) { m.AddPosed(src, at, size, deg, at, &c); }
} // namespace detail

/// Build a starter by id. An unknown id gives an empty mesh.
inline okay::Mesh BuildStarter(const std::string& id) {
    using namespace detail;
    const Mesh box = Mesh::Cube(1.0f), cyl = Mesh::Cylinder(0.5f, 1.0f, 16), cone = Mesh::Cone(0.5f, 1.0f, 16), ball = Mesh::Sphere(0.5f, 10, 16);
    const Color brown{0.45f, 0.30f, 0.15f, 1}, green{0.20f, 0.60f, 0.25f, 1}, red{0.75f, 0.20f, 0.18f, 1}, wall{0.90f, 0.85f, 0.70f, 1},
                grey{0.60f, 0.62f, 0.66f, 1}, dark{0.12f, 0.12f, 0.14f, 1}, blue{0.20f, 0.35f, 0.80f, 1}, skin{0.95f, 0.78f, 0.62f, 1},
                white{0.95f, 0.95f, 0.95f, 1}, gold{0.85f, 0.70f, 0.20f, 1};
    Mesh m;
    if (id == "tree") {
        Part(m, cyl, {0, 0.5f, 0}, {0.3f, 1.0f, 0.3f}, brown);
        Part(m, cone, {0, 1.5f, 0}, {1.2f, 1.4f, 1.2f}, green);
        Part(m, cone, {0, 2.2f, 0}, {0.9f, 1.1f, 0.9f}, green);
    } else if (id == "house") {
        Part(m, box, {0, 0.5f, 0}, {2.0f, 1.0f, 1.6f}, wall);
        Part(m, cone, {0, 1.45f, 0}, {2.9f, 0.9f, 2.9f}, red);   // a wide cone for a roof: overhangs the walls
        Part(m, box, {0, 0.35f, 0.81f}, {0.4f, 0.7f, 0.1f}, brown);   // door, sticks out a little
        Part(m, box, {0.6f, 0.6f, 0.81f}, {0.3f, 0.3f, 0.1f}, blue);  // window
    } else if (id == "car") {
        Part(m, box, {0, 0.45f, 0}, {2.0f, 0.5f, 1.0f}, red);
        Part(m, box, {-0.1f, 0.85f, 0}, {1.0f, 0.4f, 0.8f}, blue);
        for (float x : {-0.65f, 0.65f}) for (float z : {-0.5f, 0.5f})
            PartTurned(m, cyl, {x, 0.25f, z}, {0.5f, 0.2f, 0.5f}, {90, 0, 0}, dark);
    } else if (id == "head") {
        Part(m, ball, {0, 0, 0}, {1.0f, 1.1f, 1.0f}, skin);
        Part(m, ball, {-0.18f, 0.12f, 0.42f}, {0.16f, 0.16f, 0.16f}, white);
        Part(m, ball, {0.18f, 0.12f, 0.42f}, {0.16f, 0.16f, 0.16f}, white);
        PartTurned(m, cone, {0, -0.02f, 0.52f}, {0.14f, 0.2f, 0.14f}, {90, 0, 0}, skin);
    } else if (id == "sword") {
        Part(m, box, {0, 1.0f, 0}, {0.18f, 1.6f, 0.04f}, grey);
        Part(m, box, {0, 0.18f, 0}, {0.6f, 0.09f, 0.11f}, gold);
        Part(m, cyl, {0, -0.12f, 0}, {0.09f, 0.55f, 0.09f}, brown);
        Part(m, ball, {0, -0.42f, 0}, {0.16f, 0.16f, 0.16f}, gold);
    } else if (id == "table") {
        Part(m, box, {0, 0.9f, 0}, {1.6f, 0.12f, 1.0f}, brown);
        for (float x : {-0.65f, 0.65f}) for (float z : {-0.35f, 0.35f})
            Part(m, box, {x, 0.45f, z}, {0.12f, 0.95f, 0.12f}, brown);
    } else if (id == "rocket") {
        Part(m, cyl, {0, 1.0f, 0}, {0.8f, 2.0f, 0.8f}, white);
        Part(m, cone, {0, 2.4f, 0}, {0.84f, 1.0f, 0.84f}, red);
        for (int i = 0; i < 3; ++i) PartTurned(m, box, {0, 0.35f, 0}, {0.06f, 0.7f, 1.4f}, {0, 120.0f * i, 0}, blue);
    }
    m.name = id.empty() ? "Starter" : id;
    return m;
}

} // namespace oked::easymodel
