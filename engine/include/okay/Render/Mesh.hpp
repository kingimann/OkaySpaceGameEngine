#pragma once
#include "okay/Math/Vec3.hpp"
#include <string>
#include <vector>

namespace okay {

/// A simple indexed triangle mesh (positions + triangle indices). Enough to
/// describe 3D geometry; the editor renders these as wireframes and a future
/// GPU backend can upload them directly. `name` tags built-in primitives so they
/// can be serialized compactly.
struct Mesh {
    std::string       name;       // "Cube"/"Pyramid"/"Quad" for primitives
    std::vector<Vec3> vertices;
    std::vector<int>  triangles;  // 3 indices per triangle

    int TriangleCount() const { return static_cast<int>(triangles.size() / 3); }

    // ---- Primitive generators -----------------------------------------
    static Mesh Quad(float size = 1.0f) {
        float h = size * 0.5f;
        Mesh m;
        m.name = "Quad";
        m.vertices = {{-h, -h, 0}, {h, -h, 0}, {h, h, 0}, {-h, h, 0}};
        m.triangles = {0, 1, 2, 0, 2, 3};
        return m;
    }

    static Mesh Cube(float size = 1.0f) {
        float h = size * 0.5f;
        Mesh m;
        m.name = "Cube";
        m.vertices = {
            {-h, -h, -h}, { h, -h, -h}, { h, h, -h}, {-h, h, -h}, // back
            {-h, -h,  h}, { h, -h,  h}, { h, h,  h}, {-h, h,  h}, // front
        };
        m.triangles = {
            0,1,2, 0,2,3,   // back
            4,6,5, 4,7,6,   // front
            4,5,1, 4,1,0,   // bottom
            3,2,6, 3,6,7,   // top
            4,0,3, 4,3,7,   // left
            1,5,6, 1,6,2,   // right
        };
        return m;
    }

    static Mesh Pyramid(float size = 1.0f) {
        float h = size * 0.5f;
        Mesh m;
        m.name = "Pyramid";
        m.vertices = {
            {-h, -h, -h}, { h, -h, -h}, { h, -h, h}, {-h, -h, h}, // base
            { 0,  h,  0},                                         // apex
        };
        m.triangles = {
            0,2,1, 0,3,2,        // base
            0,1,4, 1,2,4,        // sides
            2,3,4, 3,0,4,
        };
        return m;
    }

    static Mesh Plane(float size = 10.0f) { Mesh m = Quad(size); m.name = "Quad"; return m; }

    /// Recreate a primitive mesh from its name ("Cube"/"Pyramid"/"Quad").
    static Mesh FromName(const std::string& n) {
        if (n == "Pyramid") return Pyramid();
        if (n == "Quad")    return Quad();
        return Cube();
    }
};

} // namespace okay
