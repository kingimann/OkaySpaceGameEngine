#include "test_framework.hpp"
#include <Okay.hpp>
#include <fstream>
#include <cmath>

using namespace okay;

// Mesh::LoadOBJ imports geometry, UVs and (new) authored vertex normals (vn), and
// still handles the plain position-only form.
int main() {
    RUN_SUITE("obj_import");

    // A quad with authored normals + UVs (two triangles via a 4-corner face).
    {
        const char* path = "/tmp/okay_test_quad.obj";
        std::ofstream o(path);
        o << "v -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\n"
          << "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
          << "vn 0 1 0\n"
          << "f 1/1/1 2/2/1 3/3/1 4/4/1\n";
        o.close();

        bool ok = false; std::string tex;
        Mesh m = Mesh::LoadOBJ(path, &ok, &tex);
        CHECK(ok);
        CHECK(!m.vertices.empty());
        CHECK(m.triangles.size() == 6);              // quad -> 2 tris
        CHECK(m.normals.size() == m.vertices.size()); // authored normals kept
        CHECK(m.uvs.size() == m.vertices.size());
        // The authored normal points +Y.
        CHECK(std::fabs(m.normals[0].y - 1.0f) < 1e-4f);
    }

    // Position-only OBJ still loads (1:1 vertices, no normals/uvs).
    {
        const char* path = "/tmp/okay_test_tri.obj";
        std::ofstream o(path);
        o << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
        o.close();

        bool ok = false;
        Mesh m = Mesh::LoadOBJ(path, &ok);
        CHECK(ok);
        CHECK(m.vertices.size() == 3);
        CHECK(m.triangles.size() == 3);
        CHECK(m.normals.empty());                    // none authored -> left for recompute
    }

    TEST_MAIN_RESULT();
}
