// Loader robustness: an OBJ face past the vertex list (item 5), malformed glTF
// accessors and node cycles (item 6), and an image header claiming gigapixels
// (item 9) are rejected with ok=false instead of overflowing, hanging or
// allocating gigabytes. Valid files still load.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/Gltf.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Graphics/Image.hpp"
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/resource.h>

using namespace okay;

static void Write(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary); f << text;
}

// A .gltf with one embedded buffer (base64) and the given accessors / meshes / nodes JSON.
static std::string Gltf(const std::string& b64, int byteLength, const std::string& rest) {
    return std::string("{\"asset\":{\"version\":\"2.0\"},") +
           "\"buffers\":[{\"byteLength\":" + std::to_string(byteLength) +
           ",\"uri\":\"data:application/octet-stream;base64," + b64 + "\"}]," + rest + "}";
}
// 36 bytes: three float VEC3 positions (0,0,0) (1,0,0) (0,1,0).
static const char* kTri = "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA";
static const std::string kView = "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],";

static long PeakRssMB() { rusage u{}; getrusage(RUSAGE_SELF, &u); return u.ru_maxrss / 1024; }

int main() {
    RUN_SUITE("loaders_robust");

    // ---------------- OBJ ----------------
    {
        Write("lr_bad.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n");
        bool ok = true; Mesh m = Mesh::LoadOBJ("lr_bad.obj", &ok);
        CHECK(!ok);
        CHECK(m.triangles.empty());
        Write("lr_bad2.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nf 1/1 2/1 -7/1\n");
        ok = true; Mesh::LoadOBJ("lr_bad2.obj", &ok);
        CHECK(!ok);
        // Valid: relative indices, and a face listed before its vertices.
        Write("lr_ok.obj", "f 1 2 3\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n");
        ok = false; Mesh g = Mesh::LoadOBJ("lr_ok.obj", &ok);
        CHECK(ok);
        CHECK(g.triangles.size() == 6);
        for (int t : g.triangles) CHECK(t >= 0 && t < (int)g.vertices.size());
        std::remove("lr_bad.obj"); std::remove("lr_bad2.obj"); std::remove("lr_ok.obj");
    }

    // ---------------- glTF accessors ----------------
    const std::string mesh = "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}]";
    {   // valid triangle
        Write("lr_ok.gltf", Gltf(kTri, 36, kView +
              "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}]," + mesh));
        bool ok = false; Mesh m = LoadGLTF("lr_ok.gltf", &ok);
        CHECK(ok);
        CHECK(m.vertices.size() == 3 && m.triangles.size() == 3);
    }
    {   // SCALAR POSITION: 3 floats read as 3 VEC3s -> overflow before
        Write("lr_scalar.gltf", Gltf(kTri, 36, kView +
              "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"SCALAR\"}]," + mesh));
        bool ok = true; Mesh m = LoadGLTF("lr_scalar.gltf", &ok);
        CHECK(!ok);
        CHECK(m.vertices.empty());
    }
    {   // count far beyond the buffer -> gigabytes of zero-fill before
        const long before = PeakRssMB();
        Write("lr_count.gltf", Gltf(kTri, 36, kView +
              "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":200000000,\"type\":\"VEC3\"}]," + mesh));
        bool ok = true; LoadGLTF("lr_count.gltf", &ok);
        CHECK(!ok);
        CHECK(PeakRssMB() - before < 256);
    }
    {   // bad componentType, missing bufferView, negative offset
        for (const char* acc : {"{\"bufferView\":0,\"componentType\":9999,\"count\":3,\"type\":\"VEC3\"}",
                                "{\"bufferView\":7,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}",
                                "{\"bufferView\":0,\"byteOffset\":-24,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}"}) {
            Write("lr_acc.gltf", Gltf(kTri, 36, kView + "\"accessors\":[" + acc + "]," + mesh));
            bool ok = true; LoadGLTF("lr_acc.gltf", &ok);
            CHECK(!ok);
        }
    }
    {   // index past the primitive's vertices (indices: u8 0,1,7)
        Write("lr_idx.gltf", Gltf(std::string(kTri) + "AAEH", 39,
              "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":3}],"
              "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
              "{\"bufferView\":1,\"componentType\":5121,\"count\":3,\"type\":\"SCALAR\"}],"
              "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}]"));
        bool ok = true; Mesh m = LoadGLTF("lr_idx.gltf", &ok);
        CHECK(!ok);
        for (int t : m.triangles) CHECK(t >= 0 && t < (int)m.vertices.size());
    }

    // ---------------- glTF node cycle ----------------
    {
        Write("lr_cycle.gltf", Gltf(kTri, 36, kView +
              "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}]," + mesh +
              ",\"nodes\":[{\"name\":\"A\",\"mesh\":0,\"children\":[1]},{\"name\":\"B\",\"children\":[0]}]"));
        Scene s("c");
        bool ok = true;
        GameObject* r = ImportModelScene(s, "lr_cycle.gltf", &ok);
        CHECK(!ok);
        CHECK(r == nullptr);
        // A well-formed tree still imports.
        Write("lr_tree.gltf", Gltf(kTri, 36, kView +
              "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}]," + mesh +
              ",\"nodes\":[{\"name\":\"A\",\"children\":[1]},{\"name\":\"B\",\"mesh\":0}]"));
        ok = false;
        GameObject* t = ImportModelScene(s, "lr_tree.gltf", &ok);
        CHECK(ok && t != nullptr);
        GameObject* b = s.Find("B");
        CHECK(b && b->transform->Parent() && b->transform->Parent()->gameObject->name == "A");
    }
    for (const char* f : {"lr_ok.gltf", "lr_scalar.gltf", "lr_count.gltf", "lr_acc.gltf", "lr_idx.gltf",
                          "lr_cycle.gltf", "lr_tree.gltf"}) std::remove(f);

    // ---------------- Image: a 40-byte TGA claiming 52032 x 8192 ----------------
    {
        std::string tga(40, '\0');
        tga[2] = 2;                                         // uncompressed true-colour
        tga[12] = (char)(52032 & 0xFF); tga[13] = (char)(52032 >> 8);
        tga[14] = (char)(8192 & 0xFF);  tga[15] = (char)(8192 >> 8);
        tga[16] = 32;                                       // bpp
        Write("lr_huge.tga", tga);
        const long before = PeakRssMB();
        Image img; std::string err;
        CHECK(!img.Load("lr_huge.tga", &err));
        CHECK(!err.empty());
        CHECK(img.Width() == 0 && img.Height() == 0);
        CHECK(PeakRssMB() - before < 256);
        std::remove("lr_huge.tga");
        // A small valid image still loads.
        std::string ok(18 + 4 * 4, '\0');
        ok[2] = 2; ok[12] = 2; ok[14] = 2; ok[16] = 32;
        Write("lr_small.tga", ok);
        Image s;
        CHECK(s.Load("lr_small.tga", &err));
        CHECK(s.Width() == 2 && s.Height() == 2);
        std::remove("lr_small.tga");
    }

    TEST_MAIN_RESULT();
}
