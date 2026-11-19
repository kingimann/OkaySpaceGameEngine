// OGRE skeleton (.skeleton.xml) and bone assignments in our own OGRE XML reader (engine/src/Render/OgreXmlReader.cpp).
// SYNTHETIC fixtures built in memory, checked against numbers worked out by hand (see the comments). Builds on its own with
// -DOKAY_STANDALONE_TEST (reader source only). Written from the skeleton format's public description, no code from OGRE.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

// A quad (0,0,0) (2,0,0) (2,2,0) (0,2,0) on shared vertices; faces (0,2,3) then (0,1,2), so the output vertices come in the order
// 0, 2, 3, 1 and the weights must follow them. Mesh-level bone assignments:
//   v0: bone id 0 (weight 1)            v1: bone id 1 (0.75) and id 2 (0.25)
//   v2: bone id 1 (1) and id 0 (0, ignored)      v3: nothing (tied to the root bone)
static std::string MeshXml(const std::string& extraAssign = "") {
    return
        "<mesh>\n <sharedgeometry vertexcount=\"4\"><vertexbuffer positions=\"true\">\n"
        "  <vertex><position x=\"0\" y=\"0\" z=\"0\"/></vertex><vertex><position x=\"2\" y=\"0\" z=\"0\"/></vertex>\n"
        "  <vertex><position x=\"2\" y=\"2\" z=\"0\"/></vertex><vertex><position x=\"0\" y=\"2\" z=\"0\"/></vertex>\n"
        " </vertexbuffer></sharedgeometry>\n"
        " <submeshes><submesh material=\"M\" usesharedvertices=\"true\" operationtype=\"triangle_list\">\n"
        "  <faces count=\"2\"><face v1=\"0\" v2=\"2\" v3=\"3\"/><face v1=\"0\" v2=\"1\" v3=\"2\"/></faces></submesh></submeshes>\n"
        " <skeletonlink name=\"x.skeleton\"/>\n"
        " <boneassignments>\n"
        "  <vertexboneassignment vertexindex=\"0\" boneindex=\"0\" weight=\"1\"/>\n"
        "  <vertexboneassignment vertexindex=\"1\" boneindex=\"1\" weight=\"0.75\"/>\n"
        "  <vertexboneassignment vertexindex=\"1\" boneindex=\"2\" weight=\"0.25\"/>\n"
        "  <vertexboneassignment vertexindex=\"2\" boneindex=\"1\" weight=\"1\"/>\n"
        "  <vertexboneassignment vertexindex=\"2\" boneindex=\"0\" weight=\"0\"/>\n" + extraAssign +
        " </boneassignments>\n</mesh>\n";
}

// Bones (ids are not in file order on purpose): "root" id 0 at the origin; "arm" id 2 at (0,1,0) under root; "hand" id 1 at (0,1,0) with
// a rotation of 90 degrees (1.5707963 rad) about Z under arm. Global bind of arm = T(0,1,0), of hand = T(0,2,0) * Rz(90).
// Animation "wave", bone arm: key 0 has only a rotate of 0; key 1 (time 1) has translate (1,0,0) and a rotate of pi about Z.
// The keys are written out of order (time 1 first) to check the sort.
static std::string Skeleton() {
    return
        "<skeleton>\n <bones>\n"
        "  <bone id=\"0\" name=\"root\"><position x=\"0\" y=\"0\" z=\"0\"/><rotation angle=\"0\"><axis x=\"1\" y=\"0\" z=\"0\"/></rotation></bone>\n"
        "  <bone id=\"2\" name=\"arm\"><position x=\"0\" y=\"1\" z=\"0\"/><rotation angle=\"0\"><axis x=\"1\" y=\"0\" z=\"0\"/></rotation></bone>\n"
        "  <bone id=\"1\" name=\"hand\"><position x=\"0\" y=\"1\" z=\"0\"/><rotation angle=\"1.5707963267948966\"><axis x=\"0\" y=\"0\" z=\"1\"/></rotation></bone>\n"
        " </bones>\n <bonehierarchy><boneparent bone=\"arm\" parent=\"root\"/><boneparent bone=\"hand\" parent=\"arm\"/></bonehierarchy>\n"
        " <animations><animation name=\"wave\" length=\"1\"><tracks><track bone=\"arm\"><keyframes>\n"
        "  <keyframe time=\"1\"><translate x=\"1\" y=\"0\" z=\"0\"/><rotate angle=\"3.141592653589793\"><axis x=\"0\" y=\"0\" z=\"1\"/></rotate></keyframe>\n"
        "  <keyframe time=\"0\"><rotate angle=\"0\"><axis x=\"0\" y=\"0\" z=\"1\"/></rotate></keyframe>\n"
        " </keyframes></track><track bone=\"nobody\"><keyframes><keyframe time=\"0\"/></keyframes></track></tracks></animation></animations>\n</skeleton>\n";
}

static Vec3 Xf(const Mat4& m, Vec3 p) { return Vec3{m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12], m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13], m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]}; }

int main() {
    RUN_SUITE("ogre_skeleton");
    {
        ImportedModel m; std::string err;
        const std::string sk = Skeleton();
        CHECK(ReadOgreXmlBuffer(MeshXml(), "", m, &err, nullptr, &sk));
        CHECK(m.backend == "own" && fx::Sane(m));
        // nodes: the root, then the bones parent first (file order is root, arm id 2, hand id 1)
        CHECK(m.nodes.size() == 4 && m.nodes[1].name == "root" && m.nodes[2].name == "arm" && m.nodes[3].name == "hand");
        CHECK(m.nodes[1].parent == 0 && m.nodes[2].parent == 1 && m.nodes[3].parent == 2);
        CHECK(fx::Near(m.nodes[2].position.y, 1.0f) && fx::Near(m.nodes[3].rotation.z, std::sqrt(0.5f)) && fx::Near(m.nodes[3].rotation.w, std::sqrt(0.5f)));
        CHECK(m.parts.size() == 1 && m.parts[0].mesh.vertices.size() == 4 && m.parts[0].bones.size() == 4);   // 3 bones plus the root bone for v3
        const ImportedPart& p = m.parts[0];
        // inverse bind: the bind position of each bone goes to the origin
        CHECK(p.bones[0].name == "root" && p.bones[1].name == "arm" && p.bones[2].name == "hand" && p.bones[3].name == "OGRE");
        { const Vec3 a = Xf(p.bones[1].inverseBind, Vec3{0, 1, 0}); CHECK(fx::Near(a.x, 0) && fx::Near(a.y, 0) && fx::Near(a.z, 0)); }
        { const Vec3 h = Xf(p.bones[2].inverseBind, Vec3{0, 2, 0}); CHECK(fx::Near(h.x, 0) && fx::Near(h.y, 0) && fx::Near(h.z, 0));
          // the hand's x axis (1,0,0) points along +y at bind (90 degrees about z), so the point (0,3,0) is 1 along its x axis
          const Vec3 q = Xf(p.bones[2].inverseBind, Vec3{0, 3, 0}); CHECK(fx::Near(q.x, 1) && fx::Near(q.y, 0)); }
        // weights follow the vertices: output order is source vertices 0, 2, 3, 1
        const float e = 1e-5f;
        CHECK(p.jointIdx.size() == 4 && p.jointWt.size() == 4);
        CHECK(std::fabs(p.mesh.vertices[0].x - 0) < e && p.jointIdx[0][0] == 0 && fx::Near(p.jointWt[0][0], 1));                       // v0: root bone
        CHECK(std::fabs(p.mesh.vertices[1].x - 2) < e && std::fabs(p.mesh.vertices[1].y - 2) < e && p.jointIdx[1][0] == 2 && fx::Near(p.jointWt[1][0], 1) && fx::Near(p.jointWt[1][1], 0));   // v2: bone id 1 = hand, index 2 (the zero weight is ignored)
        CHECK(std::fabs(p.mesh.vertices[2].y - 2) < e && p.jointIdx[2][0] == 3 && fx::Near(p.jointWt[2][0], 1));                         // v3: no assignment, root bone
        CHECK(std::fabs(p.mesh.vertices[3].x - 2) < e && std::fabs(p.mesh.vertices[3].y) < e);                                            // v1
        CHECK(p.jointIdx[3][0] == 2 && fx::Near(p.jointWt[3][0], 0.75f) && p.jointIdx[3][1] == 1 && fx::Near(p.jointWt[3][1], 0.25f));    // ids: 1 is the hand (.75), 2 is the arm (.25)
        CHECK(HasNote(m, "tied to a root bone"));
        // the clip
        CHECK(m.clips.size() == 1 && m.clips[0].name == "wave" && m.clips[0].channels.size() == 1);
        const ImportedChannel& c = m.clips[0].channels[0];
        CHECK(c.node == "arm" && c.position.size() == 2 && c.rotation.size() == 2 && c.scale.empty());
        CHECK(fx::Near(c.position[0].time, 0) && fx::Near(c.position[0].value.x, 0) && fx::Near(c.position[0].value.y, 1));           // key 0 has no translate: the bind position
        CHECK(fx::Near(c.position[1].time, 1) && fx::Near(c.position[1].value.x, 1) && fx::Near(c.position[1].value.y, 1));           // bind (0,1,0) + (1,0,0)
        CHECK(fx::Near(std::fabs(c.rotation[1].value.z), 1) && fx::Near(c.rotation[1].value.w, 0, 1e-4f) && fx::Near(c.rotation[0].value.w, 1));   // pi about z
        CHECK(HasNote(m, "unknown bone skipped") && HasNote(m, "skeleton read: 3 bone(s), 1 animation(s)"));
    }
    {   // without the skeleton the same mesh stays rigid with the old note
        ImportedModel m; std::string err;
        CHECK(ReadOgreXmlBuffer(MeshXml(), "", m, &err) && m.nodes.size() == 1 && m.parts[0].bones.empty() && m.clips.empty() && HasNote(m, "x.skeleton"));
        // a skeleton that cannot be read: the mesh is still imported, rigid, and the note says why
        const std::string bad = "<notaskeleton/>";
        CHECK(ReadOgreXmlBuffer(MeshXml(), "", m, &err, nullptr, &bad) && m.nodes.size() == 1 && m.parts[0].bones.empty() && HasNote(m, "not an OGRE skeleton"));
        const std::string nan = "<skeleton><bones><bone id=\"0\" name=\"a\"><position x=\"nan\" y=\"0\" z=\"0\"/></bone></bones></skeleton>";
        CHECK(ReadOgreXmlBuffer(MeshXml(), "", m, &err, nullptr, &nan) && m.nodes.size() == 1 && m.parts[0].bones.empty() && fx::Sane(m));
    }
    {   // hostile skeleton text: every truncation, and odd structure
        const std::string sk = Skeleton(), mesh = MeshXml();
        for (std::size_t n = 0; n < sk.size(); n += 3) {
            ImportedModel m; std::string e;
            const std::string cut = sk.substr(0, n);
            CHECK(ReadOgreXmlBuffer(mesh, "", m, &e, nullptr, &cut) && fx::Sane(m));
        }
        // a parent loop (a under b under a), a repeated name, a bone with no name, an assignment to a missing bone and a vertex out of range
        const std::string odd =
            "<skeleton><bones><bone id=\"0\" name=\"a\"/><bone id=\"1\" name=\"b\"/><bone id=\"2\" name=\"a\"/><bone id=\"3\"/></bones>"
            "<bonehierarchy><boneparent bone=\"a\" parent=\"b\"/><boneparent bone=\"b\" parent=\"a\"/><boneparent bone=\"zzz\" parent=\"a\"/></bonehierarchy></skeleton>";
        ImportedModel m; std::string e;
        CHECK(ReadOgreXmlBuffer(MeshXml("<vertexboneassignment vertexindex=\"99\" boneindex=\"0\" weight=\"1\"/><vertexboneassignment vertexindex=\"0\" boneindex=\"77\" weight=\"1\"/>"), "", m, &e, nullptr, &odd));
        CHECK(fx::Sane(m) && m.nodes.size() == 5 && HasNote(m, "loop") && HasNote(m, "repeated") && HasNote(m, "unknown bone skipped") && HasNote(m, "unknown bone"));
        for (const ImportedNode& nd : m.nodes) for (const ImportedNode& o : m.nodes) CHECK(&nd == &o || nd.name != o.name);
        // five influences on one vertex: the four strongest stay
        const std::string five = "<skeleton><bones><bone id=\"0\" name=\"a\"/><bone id=\"1\" name=\"b\"/><bone id=\"2\" name=\"c\"/><bone id=\"3\" name=\"d\"/><bone id=\"4\" name=\"e\"/></bones></skeleton>";
        std::string asg;
        for (int k = 0; k < 5; ++k) asg += "<vertexboneassignment vertexindex=\"3\" boneindex=\"" + std::to_string(k) + "\" weight=\"" + std::to_string(k == 0 ? 0.05 : 0.2) + "\"/>";
        CHECK(ReadOgreXmlBuffer(MeshXml(asg), "", m, &e, nullptr, &five) && fx::Sane(m) && HasNote(m, "beyond the four strongest"));
        // output vertex 2 is source vertex 3: bones 1..4 at 0.2 each, bone 0 (0.05) dropped, so each weight is 0.25
        CHECK(m.parts[0].jointIdx[2][0] != 0 && fx::Near(m.parts[0].jointWt[2][0] + m.parts[0].jointWt[2][1] + m.parts[0].jointWt[2][2] + m.parts[0].jointWt[2][3], 1.0f) && fx::Near(m.parts[0].jointWt[2][0], 0.25f));
    }
    {   // from disk: the skeleton named by the link is read from "<link>.xml" next to the mesh, and only from there
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "okay_ogre_skel_test";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir / "sub", ec);
        auto put = [](const fs::path& p, const std::string& t) { std::ofstream(p, std::ios::binary) << t; };
        put(dir / "x.mesh.xml", MeshXml()); put(dir / "x.skeleton.xml", Skeleton());
        ImportedModel m; std::string err;
        CHECK(ReadOgreXmlFile((dir / "x.mesh.xml").string(), m, &err) && m.nodes.size() == 4 && m.clips.size() == 1 && m.parts[0].bones.size() == 4);
        std::string evil = MeshXml(); evil.replace(evil.find("x.skeleton"), 10, "../x.skeleton");
        put(dir / "sub" / "y.mesh.xml", evil); put(dir / "x.skeleton.xml", Skeleton());
        CHECK(ReadOgreXmlFile((dir / "sub" / "y.mesh.xml").string(), m, &err) && m.nodes.size() == 1 && m.parts[0].bones.empty() && HasNote(m, "not read"));
        std::string missing = MeshXml(); missing.replace(missing.find("x.skeleton"), 10, "gone.skeleton");
        put(dir / "z.mesh.xml", missing);
        CHECK(ReadOgreXmlFile((dir / "z.mesh.xml").string(), m, &err) && m.nodes.size() == 1 && HasNote(m, "gone.skeleton"));
        fs::remove_all(dir, ec);
    }
    TEST_MAIN_RESULT();
}
