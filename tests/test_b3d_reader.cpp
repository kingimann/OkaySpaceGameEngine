// Our own B3D (Blitz3D) reader (engine/src/Render/B3dReader.cpp). SYNTHETIC fixture built in memory, checked against
// hand-computed numbers (see BuildB3d in batchc_fixtures.hpp). Real B3D files are checked by model_corpus and against
// Assimp. Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchc_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>

using namespace okay;
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

int main() {
    RUN_SUITE("b3d_reader");
    const float h = 0.70710678f;
    {
        ImportedModel m; std::string err;
        CHECK(ReadB3dBuffer(fx::BuildB3d(), "dir/", m, &err));
        CHECK(m.backend == "own" && fx::Sane(m));
        CHECK(m.nodes.size() == 3 && m.nodes[0].name == "root" && m.nodes[1].name == "bone1" && m.nodes[2].name == "bone2" && m.nodes[1].parent == 0 && m.nodes[2].parent == 0);
        // Z is mirrored: positions (1,2,3) -> (1,2,-3), (0,1,4) -> (0,1,-4); rotation (w x y z) = (h 0 h 0) -> (x y -z w) = (0 h 0 h)
        CHECK(fx::Near(m.nodes[0].position.x, 1) && fx::Near(m.nodes[0].position.y, 2) && fx::Near(m.nodes[0].position.z, -3));
        CHECK(fx::Near(m.nodes[1].position.z, -4) && fx::Near(m.nodes[1].scale.x, 2));
        CHECK(fx::Near(m.nodes[1].rotation.x, 0) && fx::Near(m.nodes[1].rotation.y, h) && fx::Near(m.nodes[1].rotation.z, 0) && fx::Near(m.nodes[1].rotation.w, h));
        // material: brush colour and alpha, shininess 0.5 * 128, the texture joined to the folder with / separators
        CHECK(m.materials.size() == 1 && m.materials[0].name == "mat" && m.materials[0].texture == "dir/tex/a.png");
        CHECK(fx::Near(m.materials[0].diffuse.r, 0.5f) && fx::Near(m.materials[0].diffuse.g, 0.25f) && fx::Near(m.materials[0].diffuse.b, 0.75f) && fx::Near(m.materials[0].diffuse.a, 0.5f));
        CHECK(fx::Near(m.materials[0].shininess, 64.0f));
        // one skinned part on the mesh's node, vertices in the node's own coordinates; area 0.5; the winding is reversed
        CHECK(m.parts.size() == 1 && fx::TriCount(m) == 1 && m.parts[0].material == 0 && m.parts[0].node == 0);
        const ImportedPart& p = m.parts[0];
        CHECK(p.mesh.vertices.size() == 3 && p.mesh.normals.size() == 3 && p.mesh.uvs.size() == 3);
        CHECK_NEAR(fx::Area(m), 0.5, 1e-5);
        bool saw[3] = {false, false, false};
        for (std::size_t i = 0; i < 3; ++i) {
            const Vec3 v = p.mesh.vertices[i];
            CHECK(fx::Near(p.mesh.normals[i].z, -1));   // normal (0,0,1) mirrored, and it agrees with the reversed winding
            if (fx::Near(v.x, 0) && fx::Near(v.y, 0)) { saw[0] = true; CHECK(fx::Near(v.z, 0) && fx::Near(p.mesh.uvs[i].x, 0) && fx::Near(p.mesh.uvs[i].y, 0)); }
            if (fx::Near(v.x, 1) && fx::Near(v.y, 0)) { saw[1] = true; CHECK(fx::Near(p.mesh.uvs[i].x, 1) && fx::Near(p.mesh.uvs[i].y, 0)); }
            if (fx::Near(v.x, 0) && fx::Near(v.y, 1)) { saw[2] = true; CHECK(fx::Near(p.mesh.uvs[i].x, 0) && fx::Near(p.mesh.uvs[i].y, 1)); }
        }
        CHECK(saw[0] && saw[1] && saw[2]);
        const Vec3 a = p.mesh.vertices[(std::size_t)p.mesh.triangles[0]], b = p.mesh.vertices[(std::size_t)p.mesh.triangles[1]], c = p.mesh.vertices[(std::size_t)p.mesh.triangles[2]];
        const float nz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        CHECK(nz < 0);   // z component of the geometric normal: (0,0,-1)
        // skin: bones are the nodes with a BONE chunk: bone1 = 0, bone2 = 1. v0: bone1 1.0; v1: bone1 0.5 + bone2 0.5; v2: bone1 only (0.25 normalised to 1)
        CHECK(p.bones.size() == 2 && p.bones[0].name == "bone1" && p.bones[1].name == "bone2" && p.jointIdx.size() == 3);
        for (std::size_t i = 0; i < 3; ++i) {
            const Vec3 v = p.mesh.vertices[i];
            if (fx::Near(v.x, 0) && fx::Near(v.y, 0)) CHECK(p.jointIdx[i][0] == 0 && fx::Near(p.jointWt[i][0], 1));
            if (fx::Near(v.x, 1)) CHECK(((p.jointIdx[i][0] == 0 && p.jointIdx[i][1] == 1) || (p.jointIdx[i][0] == 1 && p.jointIdx[i][1] == 0)) && fx::Near(p.jointWt[i][0], 0.5f) && fx::Near(p.jointWt[i][1], 0.5f));
            if (fx::Near(v.y, 1)) CHECK(p.jointIdx[i][0] == 0 && fx::Near(p.jointWt[i][0], 1) && fx::Near(p.jointWt[i][1], 0));
        }
        // inverse bind of bone1 maps the mesh's coordinates to the bone's: the mesh node "root" sits at (1,2,-3), so the vertex (0,0,0) is the world point (1,2,-3);
        // world(bone1) = T(1,2,-3) * TRS((0,1,-4), 90 deg about Y, scale 2); local = S^-1 R^-1 (p - t) = (-2, -0.5, 0)
        const Vec3 lp = p.bones[0].inverseBind.MultiplyPoint(Vec3{0, 0, 0});
        CHECK(fx::Near(lp.x, -2) && fx::Near(lp.y, -0.5f) && fx::Near(lp.z, 0));
        // animation: 30 frames at 30 per second, the keys of bone1 at 0 s and 1 s, mirrored
        CHECK(m.clips.size() == 1 && m.clips[0].channels.size() == 1 && m.clips[0].channels[0].node == "bone1");
        const ImportedChannel& ch = m.clips[0].channels[0];
        CHECK(ch.position.size() == 2 && ch.rotation.size() == 2 && ch.scale.empty());
        CHECK(fx::Near(ch.position[0].time, 0) && fx::Near(ch.position[1].time, 1.0f) && fx::Near(ch.position[1].value.y, 2) && fx::Near(ch.position[1].value.z, -4));
        CHECK(fx::Near(ch.rotation[0].value.w, 1) && fx::Near(ch.rotation[1].value.y, h) && fx::Near(ch.rotation[1].value.w, h) && fx::Near(ch.rotation[1].value.z, 0));
        CHECK(HasNote(m, "left-handed"));
    }
    {   // a vertex without a weight in a skinned mesh follows the root node
        fx::B3dOpts o; o.unweighted = true;
        ImportedModel m; std::string err;
        CHECK(ReadB3dBuffer(fx::BuildB3d(o), "", m, &err) && m.parts[0].bones.size() == 3 && m.parts[0].bones[2].name == "root");
        int onRoot = 0; for (const auto& ji : m.parts[0].jointIdx) if (ji[0] == 2) ++onRoot;
        CHECK(onRoot == 1 && HasNote(m, "no weight"));
    }
    {   // no bones: a rigid part on its own node, no skin; two top-level nodes get a new root; no ANIM: keys at 60 per second
        fx::B3dOpts o; o.noBone = true; o.twoTop = true; o.noAnim = true;
        ImportedModel m; std::string err;
        CHECK(ReadB3dBuffer(fx::BuildB3d(o), "", m, &err) && fx::Sane(m));
        CHECK(m.nodes.size() == 5 && m.nodes[0].name == "B3D" && m.nodes[1].name == "root" && m.nodes[1].parent == 0 && m.nodes[4].name == "second" && m.nodes[4].parent == 0);
        CHECK(m.parts.size() == 1 && m.parts[0].bones.empty() && m.parts[0].node == 1);
        CHECK(m.clips.size() == 1 && fx::Near(m.clips[0].channels[0].position[1].time, 0.5f));   // frame 30 at 60 per second
        CHECK(HasNote(m, "top-level") && HasNote(m, "60 frames per second"));
    }
    {   // refusals and truncations
        ImportedModel m; std::string err;
        const std::string good = fx::BuildB3d();
        CHECK(!ReadB3dBuffer("", "", m, &err) && !err.empty());
        std::string bad = good; bad[0] = 'X';
        CHECK(!ReadB3dBuffer(bad, "", m, &err) && err.find("BB3D") != std::string::npos);
        bad = good; bad[4] = (char)0xFF; bad[5] = (char)0xFF; bad[6] = (char)0xFF; bad[7] = (char)0x7F;
        CHECK(!ReadB3dBuffer(bad, "", m, &err) && err.find("larger than the file") != std::string::npos);
        for (std::size_t n = 0; n < good.size(); ++n) {
            ImportedModel t; std::string e;
            const bool ok = ReadB3dBuffer(good.substr(0, n), "", t, &e);
            CHECK(ok ? fx::Sane(t) : (!e.empty() && t.parts.empty() && t.nodes.empty()));
        }
        // a bad texture count in the brush list
        bad = good; bad.replace(bad.find("BRUS") + 8, 4, std::string("\xFF\xFF\xFF\xFF", 4));
        CHECK(!ReadB3dBuffer(bad, "", m, &err) && err.find("texture count") != std::string::npos);
    }
    TEST_MAIN_RESULT();
}
