// Our own NFF reader (engine/src/Render/NffReader.cpp): classic Neutral File Format and the Sense8 "nff" object files.
// SYNTHETIC fixtures built in memory, checked against hand-computed numbers (BuildNff, BuildNffSense8 in batchd_fixtures.hpp).
// Real .nff files are checked by model_corpus. Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchd_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static double PartArea(const ImportedPart& p) {
    ImportedModel m; m.nodes.push_back(ImportedNode{}); m.parts.push_back(p); return fx::Area(m);
}

int main() {
    RUN_SUITE("nff_reader");
    for (const char* eol : {"\n", "\r\n"}) {
        ImportedModel m; std::string err;
        CHECK(ReadNffBuffer(fx::BuildNff(eol), "dir/", m, &err));
        CHECK(m.backend == "own" && fx::Sane(m));
        CHECK(m.parts.size() == 5 && fx::TriCount(m) == 111 && m.nodes.size() == 1);
        // parts in file order: polygon, patch, cylinder, sphere, cube
        CHECK_NEAR(PartArea(m.parts[0]), 4.0, 1e-5);
        CHECK_NEAR(PartArea(m.parts[1]), 4.5, 1e-5);
        CHECK_NEAR(PartArea(m.parts[2]), 8 * std::sqrt(2.0) + 4, 1e-4);
        CHECK(m.parts[2].mesh.triangles.size() == 48 && m.parts[3].mesh.triangles.size() == 80 * 3 && m.parts[4].mesh.triangles.size() == 36);
        CHECK_NEAR(PartArea(m.parts[4]), 8.0, 1e-4);
        // every sphere vertex lies on the unit sphere, its normal is its position
        for (std::size_t i = 0; i < m.parts[3].mesh.vertices.size(); ++i) {
            const Vec3 v = m.parts[3].mesh.vertices[i], n = m.parts[3].mesh.normals[i];
            CHECK_NEAR(v.Magnitude(), 1.0, 1e-5);
            CHECK(fx::Near(v.x, n.x, 1e-4f) && fx::Near(v.y, n.y, 1e-4f) && fx::Near(v.z, n.z, 1e-4f));
        }
        const double sa = PartArea(m.parts[3]);
        CHECK(sa > 9.5746 && sa < 12.5664 && std::fabs(sa - 11.6659) < 1e-3);   // icosahedron subdivided once, inscribed in the unit sphere
        // the patch keeps its normals
        for (const Vec3& n : m.parts[1].mesh.normals) CHECK(fx::Near(n.z, 1));
        // materials: red with shininess 20, then green (cylinder is red, sphere and cube green)
        CHECK(m.materials.size() == 2);
        const ImportedMaterial& red = m.materials[(std::size_t)m.parts[0].material];
        CHECK(red.hasDiffuse && fx::Near(red.diffuse.r, 1) && fx::Near(red.diffuse.g, 0) && fx::Near(red.shininess, 20));
        CHECK(m.parts[0].material == m.parts[1].material && m.parts[1].material == m.parts[2].material && m.parts[3].material == m.parts[4].material && m.parts[2].material != m.parts[3].material);
        const ImportedMaterial& green = m.materials[(std::size_t)m.parts[3].material];
        CHECK(fx::Near(green.diffuse.g, 1) && fx::Near(green.diffuse.r, 0));
        CHECK(HasNote(m, "viewpoint") && HasNote(m, "light") && HasNote(m, "spheres are subdivided"));
    }
    {   // an ellipsoid with radii 1, 2, 3 and the box solid with three sizes
        ImportedModel m; std::string err;
        CHECK(ReadNffBuffer("tess 2\ns 10 20 30 1 2 3\nhex 0 0 0 3 4 5\ntet 0 0 0 5\noct 0 0 0 5 5 5\ndod 0 0 0 5\nico 0 0 0 5\n", "", m, &err) && fx::Sane(m));
        CHECK(m.parts.size() == 6);
        Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
        for (const Vec3& v : m.parts[0].mesh.vertices) { lo = Vec3{std::fmin(lo.x, v.x), std::fmin(lo.y, v.y), std::fmin(lo.z, v.z)}; hi = Vec3{std::fmax(hi.x, v.x), std::fmax(hi.y, v.y), std::fmax(hi.z, v.z)}; }
        CHECK(fx::Near(lo.x, 9, 0.01f) && fx::Near(hi.x, 11, 0.01f) && fx::Near(lo.y, 18, 0.05f) && fx::Near(hi.y, 22, 0.05f) && fx::Near(lo.z, 27, 0.1f) && fx::Near(hi.z, 33, 0.1f));
        CHECK(m.parts[0].mesh.triangles.size() == 320 * 3);   // tess 2: 20 * 4^2
        // hex: half edges 3/sqrt(3), 4/sqrt(3), 5/sqrt(3)
        float mx = 0, my = 0, mz = 0;
        for (const Vec3& v : m.parts[1].mesh.vertices) { mx = std::fmax(mx, v.x); my = std::fmax(my, v.y); mz = std::fmax(mz, v.z); }
        CHECK(fx::Near(mx, 3 / std::sqrt(3.0f)) && fx::Near(my, 4 / std::sqrt(3.0f)) && fx::Near(mz, 5 / std::sqrt(3.0f)));
        CHECK(fx::TriCount(m) > 0 && m.parts[2].mesh.triangles.size() == 12 && m.parts[3].mesh.triangles.size() == 24 && m.parts[4].mesh.triangles.size() == 108 && m.parts[5].mesh.triangles.size() == 60);
        // the dodecahedron and tetrahedron have the regular solids' areas for circumradius 5: 3*sqrt(25+10*sqrt5) a^2 with a = 5*4/(sqrt3*(1+sqrt5)), and sqrt3 * a^2 with a = 5*sqrt(8/3)
        const double a5 = 5.0 * 4.0 / (std::sqrt(3.0) * (1 + std::sqrt(5.0)));
        CHECK_NEAR(PartArea(m.parts[4]), 3 * std::sqrt(25 + 10 * std::sqrt(5.0)) * a5 * a5, 1e-2);
        CHECK_NEAR(PartArea(m.parts[2]), std::sqrt(3.0) * std::pow(5.0 * std::sqrt(8.0 / 3.0), 2), 1e-2);
    }
    {   // a flat or negative size is skipped; `tess 0` makes the 20-triangle icosahedron
        ImportedModel m; std::string err;
        CHECK(ReadNffBuffer("tess 0\ns 0 0 0 0\ns 0 0 0 -1\ns 0 0 0 2\n", "", m, &err) && m.parts.size() == 1 && m.parts[0].mesh.triangles.size() == 60 && HasNote(m, "zero or negative"));
        CHECK(!ReadNffBuffer("s 0 0 0 0\n", "", m, &err) && err.find("no geometry") != std::string::npos);
    }
    {   // Sense8: the material table is read from the folder of the file
        const fs::path dir = fs::temp_directory_path() / "okay_nff_reader_test";
        fs::create_directories(dir);
        { std::ofstream f(dir / "m.mat", std::ios::binary); const std::string t = fx::BuildNffMat(); f.write(t.data(), (std::streamsize)t.size()); }
        const std::string base = dir.generic_string() + "/";
        for (const char* eol : {"\n", "\r\n"}) {
            ImportedModel m; std::string err;
            CHECK(ReadNffBuffer(fx::BuildNffSense8(eol), base, m, &err));
            CHECK(m.backend == "own" && fx::Sane(m) && m.parts.size() == 2 && fx::TriCount(m) == 3);
            CHECK_NEAR(fx::Area(m), 6.0, 1e-5);
            // the quad (matid 1, texture) and the triangle (matid 0)
            const ImportedPart* quad = m.parts[0].mesh.triangles.size() == 6 ? &m.parts[0] : &m.parts[1];
            const ImportedPart* tri = quad == &m.parts[0] ? &m.parts[1] : &m.parts[0];
            CHECK(quad->mesh.uvs.size() == quad->mesh.vertices.size() && tri->mesh.uvs.size() == tri->mesh.vertices.size());
            const ImportedMaterial& mq = m.materials[(std::size_t)quad->material];
            CHECK(mq.hasDiffuse && fx::Near(mq.diffuse.g, 1) && fx::Near(mq.diffuse.a, 0.6f) && mq.texture == base + "tex.tga");
            const ImportedMaterial& mt = m.materials[(std::size_t)tri->material];
            CHECK(fx::Near(mt.diffuse.r, 0.9f) && fx::Near(mt.diffuse.g, 0.8f) && fx::Near(mt.diffuse.b, 0.7f) && fx::Near(mt.shininess, 128));
            for (std::size_t i = 0; i < quad->mesh.vertices.size(); ++i) {
                const Vec3 p = quad->mesh.vertices[i]; const Vec2 uv = quad->mesh.uvs[i];
                CHECK(fx::Near(uv.x, p.x / 2) && fx::Near(uv.y, p.y / 2));
            }
            CHECK(HasNote(m, "two-sided"));
        }
        // no table next to the file: the polygons still read, with a note
        ImportedModel m; std::string err;
        CHECK(ReadNffBuffer(fx::BuildNffSense8(), "", m, &err) && m.parts.size() == 2 && HasNote(m, "mtable"));
        std::error_code ec; fs::remove_all(dir, ec);
    }
    {   // refusals and every truncation
        ImportedModel m; std::string err;
        CHECK(!ReadNffBuffer("", "", m, &err) && !err.empty());
        CHECK(!ReadNffBuffer("# nothing here\n", "", m, &err));
        CHECK(!ReadNffBuffer("p 3\n0 0 0\n1 0 0\n", "", m, &err) && err.find("ends after 2 of 3") != std::string::npos);
        CHECK(!ReadNffBuffer("p 3\n0 0 0\n1 0 x\n0 1 0\n", "", m, &err));
        CHECK(!ReadNffBuffer("p -4\n", "", m, &err));
        CHECK(!ReadNffBuffer("c\n0 0 0 1\n", "", m, &err));
        CHECK(!ReadNffBuffer("nff\nObj\n3\n0 0 0\n1 0 0\n0 1 0\n1\n3 0 1 7\n", "", m, &err) && err.find("out of range") != std::string::npos);
        CHECK(!ReadNffBuffer("nff\nObj\n2000000000\n", "", m, &err));
        for (const std::string& good : {fx::BuildNff(), fx::BuildNffSense8()})
            for (std::size_t n = 0; n < good.size(); ++n) {
                ImportedModel t; std::string e;
                const bool ok = ReadNffBuffer(good.substr(0, n), "", t, &e);
                CHECK(ok ? fx::Sane(t) : (!e.empty() && t.parts.empty() && t.nodes.empty()));
            }
    }
    TEST_MAIN_RESULT();
}
