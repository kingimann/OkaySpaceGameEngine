// Our own COLLADA (.dae) reader and its place in the import seam: fixtures written by hand
// (tests/data/models/*.dae), unit and up-axis handling, materials and textures, node
// transforms, skin and animation, the "say what was not read" notes, the opt-in backend
// switch, and (with OKAY_USE_ASSIMP) Assimp's reading of the same files compared with ours.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace okay;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";

static bool HasNote(const ImportedModel& m, const char* needle) {
    for (const std::string& n : m.notes) if (n.find(needle) != std::string::npos) return true;
    return false;
}
static bool Near3(const Vec3& a, float x, float y, float z, float eps = 1e-4f) {
    return std::fabs(a.x - x) < eps && std::fabs(a.y - y) < eps && std::fabs(a.z - z) < eps;
}
static Vec3 Apply(const Mat4& m, const Vec3& p) {
    return Vec3{m.at(0, 0) * p.x + m.at(1, 0) * p.y + m.at(2, 0) * p.z + m.at(3, 0),
                m.at(0, 1) * p.x + m.at(1, 1) * p.y + m.at(2, 1) * p.z + m.at(3, 1),
                m.at(0, 2) * p.x + m.at(1, 2) * p.y + m.at(2, 2) * p.z + m.at(3, 2)};
}
// World matrix of a node of an ImportedModel.
static Mat4 WorldOf(const ImportedModel& m, int node) {
    Mat4 w;
    std::vector<int> chain;
    for (int n = node; n >= 0; n = m.nodes[(std::size_t)n].parent) chain.push_back(n);
    for (std::size_t i = chain.size(); i-- > 0;) {
        const ImportedNode& nd = m.nodes[(std::size_t)chain[i]];
        w = w * Mat4::TRS(nd.position, nd.rotation, nd.scale);
    }
    return w;
}

int main() {
    RUN_SUITE("collada_reader");

    // ---------------- two_materials.dae: triangles + polylist, materials, node transforms ----------------
    {
        ImportedModel m; std::string err;
        CHECK(ReadColladaFile(kDir + "two_materials.dae", m, &err));
        CHECK(m.backend == "own");
        CHECK(m.notes.empty());
        CHECK(m.parts.size() == 2);
        CHECK(m.materials.size() == 2);
        if (m.parts.size() == 2 && m.materials.size() == 2) {
            const ImportedPart& tri = m.parts[0];    // <triangles material="matA">
            const ImportedPart& quad = m.parts[1];   // <polylist material="matB">
            CHECK(tri.mesh.vertices.size() == 3 && tri.mesh.TriangleCount() == 1);
            CHECK(quad.mesh.vertices.size() == 4 && quad.mesh.TriangleCount() == 2);   // quad fan-triangulated
            CHECK(tri.mesh.HasNormals() && tri.mesh.uvs.size() == 3);
            CHECK_NEAR(tri.mesh.normals[0].z, 1.0, 1e-5);
            // UV v is flipped (v = 1 - v) like the other bottom-up formats: vertex (1,1,0) has uv (1,1) -> (1,0).
            bool found = false;
            for (std::size_t v = 0; v < quad.mesh.vertices.size(); ++v)
                if (Near3(quad.mesh.vertices[v], 1, 1, 0)) { found = true; CHECK_NEAR(quad.mesh.uvs[v].x, 1.0, 1e-5); CHECK_NEAR(quad.mesh.uvs[v].y, 0.0, 1e-5); }
            CHECK(found);
            // Materials follow instance_material, not document order.
            const ImportedMaterial& red = m.materials[(std::size_t)tri.material];
            const ImportedMaterial& wood = m.materials[(std::size_t)quad.material];
            CHECK(red.name == "Red" && wood.name == "Wood");
            CHECK(red.hasDiffuse && red.diffuse.r == 1.0f && red.diffuse.g == 0.0f);
            CHECK(red.hasEmissive && std::fabs(red.emissive.b - 0.3f) < 1e-5f);
            CHECK_NEAR(red.shininess, 32.0, 1e-5);
            CHECK(red.texture.empty());
            // sampler -> surface -> image chain, %20 decoded, resolved next to the file.
            CHECK(wood.texture == kDir + "tex/wood floor.png");
            CHECK(wood.normalMap == kDir + "tex/bump.png");   // <bump> in <extra>, backslash path
            // Hierarchy: root(Scene) > Parent > Child, parts on Child.
            CHECK(m.nodes.size() == 3);
            CHECK(m.nodes[1].name == "Parent" && m.nodes[1].parent == 0);
            CHECK(m.nodes[2].name == "Child" && m.nodes[2].parent == 1);
            CHECK(tri.node == 2 && quad.node == 2);
            CHECK(Near3(m.nodes[1].position, 1, 2, 3));
            CHECK(Near3(m.nodes[1].scale, 2, 2, 2));
            CHECK(Near3(m.nodes[2].position, 5, 0, 0));
            // Parent: T(1,2,3) * Rz(90) * S(2). Child origin is (5,0,0) in parent space -> (10,0,0) -> Rz90 -> (0,10,0) -> +T.
            CHECK(Near3(Apply(WorldOf(m, 2), Vec3{0, 0, 0}), 1, 12, 3, 1e-3f));
            // Child point (1,0,0): scaled (12,0,0) -> rotated (0,12,0) -> translated (1,14,3).
            CHECK(Near3(Apply(WorldOf(m, 2), Vec3{1, 0, 0}), 1, 14, 3, 1e-3f));
        }
        // Flattened: transforms baked, one root, nothing but meshes.
        ImportedModel f;
        CHECK(ReadColladaFile(kDir + "two_materials.dae", f, &err, true));
        CHECK(f.nodes.size() == 1 && f.parts.size() == 2 && f.clips.empty());
        bool at = false;
        for (const Vec3& v : f.parts[0].mesh.vertices) if (Near3(v, 1, 12, 3, 1e-3f)) at = true;   // vertex (0,0,0) of the Child
        CHECK(at);
        CHECK_NEAR(f.parts[0].mesh.normals[0].z, 1.0, 1e-4);   // a rotation about Z leaves a Z normal alone
    }

    // ---------------- zup_cm.dae: up axis, unit, polygon fan, generated normals ----------------
    {
        ImportedModel m; std::string err;
        CHECK(ReadColladaFile(kDir + "zup_cm.dae", m, &err));
        CHECK(m.parts.size() == 1);
        CHECK(m.parts[0].mesh.vertices.size() == 5 && m.parts[0].mesh.TriangleCount() == 3);   // pentagon -> 3 triangles
        CHECK(m.parts[0].mesh.HasNormals() && m.parts[0].mesh.uvs.empty());
        for (const Vec3& n : m.parts[0].mesh.normals) CHECK_NEAR(n.Magnitude(), 1.0, 1e-4);
        // Z-up point (0,0,100 cm) must land at +Y 1 m: unit 0.01 then z -> y.
        const Mat4 w = WorldOf(m, m.parts[0].node);
        CHECK(Near3(Apply(w, Vec3{0, 0, 100}), 0, 1, 0, 1e-4f));
        CHECK(Near3(Apply(w, Vec3{100, 0, 0}), 1, 0, 0, 1e-4f));
        CHECK(Near3(Apply(w, Vec3{0, 100, 0}), 0, 0, -1, 1e-4f));   // +Y (forward in Z-up) becomes -Z
        // X_UP via text edit: (1,0,0) must come out as +Y.
        std::string x = [&] { std::string t; FILE* fp = std::fopen((kDir + "zup_cm.dae").c_str(), "rb"); char b[4096]; std::size_t n; while ((n = std::fread(b, 1, sizeof b, fp)) > 0) t.append(b, n); std::fclose(fp); return t; }();
        x.replace(x.find("<up_axis>Z_UP") + 9, 4, "X_UP");
        ImportedModel mx;
        CHECK(ReadColladaBuffer(x, "", mx, &err));
        CHECK(Near3(Apply(WorldOf(mx, mx.parts[0].node), Vec3{100, 0, 0}), 0, 1, 0, 1e-4f));
        // Flattened version bakes the same transform into the vertices.
        ImportedModel f;
        CHECK(ReadColladaFile(kDir + "zup_cm.dae", f, &err, true));
        bool up = false; for (const Vec3& v : f.parts[0].mesh.vertices) if (Near3(v, 0.5f, 1.0f, -1.8f, 1e-4f)) up = true;   // (50,180,100) cm
        CHECK(up);
    }

    // ---------------- skinned_strip.dae: skin and matrix animation ----------------
    {
        ImportedModel m; std::string err;
        CHECK(ReadColladaFile(kDir + "skinned_strip.dae", m, &err));
        CHECK(m.parts.size() == 1);
        const ImportedPart& p = m.parts[0];
        CHECK(p.bones.size() == 2 && p.bones[0].name == "Root" && p.bones[1].name == "Tip");
        CHECK(p.jointIdx.size() == 4 && p.jointWt.size() == 4);
        if (p.jointWt.size() == 4) {
            // Vertex 0: Root 1.0. Vertex 1: Root 0.75 + Tip 0.25. Vertex 2: Root 0.5 + Tip 1.0 -> normalised 1/3, 2/3... (see fixture).
            auto sum = [&](int v) { float s = 0; for (int k = 0; k < 4; ++k) s += p.jointWt[(std::size_t)v][(std::size_t)k]; return s; };
            for (int v = 0; v < 4; ++v) CHECK_NEAR(sum(v), 1.0, 1e-5);
            CHECK(p.jointIdx[0][0] == 0 && std::fabs(p.jointWt[0][0] - 1.0f) < 1e-5f);
            // The vertex with weights (Root 0.75, Tip 0.25) in the fixture is vertex 1.
            float root = 0, tip = 0;
            for (int k = 0; k < 4; ++k) { if (p.jointWt[1][(std::size_t)k] > 0) (p.jointIdx[1][(std::size_t)k] == 0 ? root : tip) += p.jointWt[1][(std::size_t)k]; }
            CHECK_NEAR(root, 0.75, 1e-5); CHECK_NEAR(tip, 0.25, 1e-5);
        }
        // Inverse bind of Tip: translate (0,-2,0), stored column-major.
        CHECK_NEAR(p.bones[1].inverseBind.at(3, 1), -2.0, 1e-5);
        // Bone names are node names, so the scene builder can find them.
        bool tipNode = false, rootNode = false;
        for (const ImportedNode& n : m.nodes) { if (n.name == "Tip") tipNode = true; if (n.name == "Root") rootNode = true; }
        CHECK(tipNode && rootNode);
        // Animation: whole-matrix channel read as position/rotation/scale keys; the per-component channel is reported.
        CHECK(m.clips.size() == 1 && m.clips[0].name == "animation");   // two plain top-level animations merge into one clip
        if (m.clips.size() == 1) {
            CHECK(m.clips[0].channels.size() == 1 && m.clips[0].channels[0].node == "Tip");
            const ImportedChannel& c = m.clips[0].channels[0];
            CHECK(c.position.size() == 2 && c.rotation.size() == 2 && c.scale.size() == 2);
            if (c.position.size() == 2) {
                CHECK_NEAR(c.position[0].time, 0.0, 1e-6); CHECK_NEAR(c.position[1].time, 1.0, 1e-6);
                CHECK(Near3(c.position[1].value, 0, 2, 1));
                CHECK(Near3(c.scale[1].value, 1, 1, 1));
            }
        }
        CHECK(HasNote(m, "animation channel(s) not read") && HasNote(m, "Tip/rotateZ.ANGLE"));
        // It builds into a real scene: SkinnedMesh and a ModelAnimator with the clip.
        Scene scene; scene.physicsEnabled = false;
        GameObject* root = BuildSceneFromModel(scene, m, kDir + "skinned_strip.dae");
        CHECK(root != nullptr);
        if (root) { ModelReport rep = AnalyzeModel(scene, root); CHECK(rep.skinnedMeshes == 1 && rep.bones == 2 && rep.clips == 1); }
        // Flattened: no skin, no clips, no nodes.
        ImportedModel f;
        CHECK(ReadColladaFile(kDir + "skinned_strip.dae", f, &err, true));
        CHECK(f.parts.size() == 1 && f.parts[0].bones.empty() && f.clips.empty() && f.nodes.size() == 1);
    }

    // ---------------- component_anim.dae: per-component channels, STEP, BEZIER note, merged per node ----------------
    {
        ImportedModel m; std::string err;
        CHECK(ReadColladaFile(kDir + "component_anim.dae", m, &err));
        CHECK(m.clips.size() == 1 && m.clips[0].name == "animation");
        auto chan = [&](const char* node) -> const ImportedChannel* {
            if (m.clips.empty()) return nullptr;
            for (const ImportedChannel& c : m.clips[0].channels) if (c.node == node) return &c;
            return nullptr;
        };
        auto at = [](const std::vector<ImportedKeyV3>& k, float t) -> const ImportedKeyV3* { for (const auto& e : k) if (std::fabs(e.time - t) < 1e-4f) return &e; return nullptr; };
        CHECK(m.clips.size() == 1 && m.clips[0].channels.size() == 4);   // Box, Mat, Arr, Spin: one merged channel each
        if (const ImportedChannel* b = chan("Box")) {
            // location.X (0,1,2 -> 0,2,4), location.Y (0,2 -> 0,10) and scale merged into one channel.
            const ImportedKeyV3* p1 = at(b->position, 1.0f); const ImportedKeyV3* p2 = at(b->position, 2.0f);
            CHECK(p1 && p2);
            if (p1) CHECK(Near3(p1->value, 2, 5, 0, 1e-3f));    // y is linear: 5 at t = 1
            if (p2) CHECK(Near3(p2->value, 4, 10, 0, 1e-3f));
            // BEZIER scale is read as linear and said so; clamps after its last key.
            const ImportedKeyV3* s1 = at(b->scale, 1.0f); const ImportedKeyV3* s2 = at(b->scale, 2.0f);
            CHECK(s1 && s2);
            if (s1) CHECK(Near3(s1->value, 2, 2, 2, 1e-3f));
            if (s2) CHECK(Near3(s2->value, 2, 2, 2, 1e-3f));
            // STEP rotation: still identity just before t = 1, 90 degrees about Z at t = 1.
            const ImportedKeyQ* before = nullptr; const ImportedKeyQ* atOne = nullptr;
            for (const ImportedKeyQ& k : b->rotation) { if (k.time > 0.99f && k.time < 0.99999f) before = &k; if (std::fabs(k.time - 1.0f) < 1e-4f) atOne = &k; }
            CHECK(before && atOne);
            if (before) CHECK(std::fabs(before->value.w) > 0.999f);
            if (atOne) { CHECK_NEAR(std::fabs(atOne->value.z), 0.70710678, 1e-3); CHECK_NEAR(std::fabs(atOne->value.w), 0.70710678, 1e-3); }
            // Every key of a channel exists in all three tracks (position, rotation, scale sampled together).
            CHECK(b->position.size() == b->rotation.size() && b->rotation.size() == b->scale.size());
        }
        if (const ImportedChannel* mt = chan("Mat")) {
            const ImportedKeyV3* k = at(mt->position, 1.0f);   // matrix element (1)(3) is the y translation
            CHECK(k && Near3(k->value, 0, 6, 0, 1e-3f));
        } else CHECK(false);
        if (const ImportedChannel* ar = chan("Arr")) {
            const ImportedKeyV3* k = at(ar->position, 1.0f);   // the whole translate as a float3 channel
            CHECK(k && Near3(k->value, 3, 0, 0, 1e-3f));
        } else CHECK(false);
        if (const ImportedChannel* sp = chan("Spin")) {
            // 270 degrees in one segment is split so a slerp cannot take the short way round: 4 keys.
            CHECK(sp->rotation.size() == 4);
            if (sp->rotation.size() == 4) {
                // The last key is 270 about +Y; the one before is 180: their relative angle is 90, not 270.
                const Quat& a = sp->rotation[2].value; const Quat& b = sp->rotation[3].value;
                const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
                CHECK_NEAR(d, 0.70710678, 1e-3);
            }
        } else CHECK(false);
        CHECK(HasNote(m, "BEZIER interpolation, read as LINEAR"));
        CHECK(!HasNote(m, "channel(s) not read"));
    }

    // ---------------- unsupported.dae: what is not read is said, not dropped silently ----------------
    {
        ImportedModel m; std::string err;
        CHECK(ReadColladaFile(kDir + "unsupported.dae", m, &err));
        CHECK(m.parts.size() == 2);   // the triangle, and the polygon's outer ring (<ph><p>)
        CHECK(HasNote(m, "1 x lines"));
        CHECK(HasNote(m, "1 x linestrips"));
        CHECK(HasNote(m, "vertex colors"));
        CHECK(HasNote(m, "polygon hole"));
        CHECK(HasNote(m, "camera/light"));
        CHECK(HasNote(m, "<skew>/<lookat>"));
    }

    // ---------------- found by real files: strips and fans, concave polygons, decimal commas, UTF-16 ----------------
    {
        auto doc = [](const std::string& pos, const std::string& prims, const std::string& vcount = std::string()) {
            (void)vcount;
            return std::string("<?xml version=\"1.0\"?><COLLADA version=\"1.4.1\"><asset><up_axis>Y_UP</up_axis></asset><library_geometries><geometry id=\"g\"><mesh>"
                "<source id=\"p\"><float_array id=\"pa\" count=\"0\">") + pos + "</float_array><technique_common><accessor source=\"#pa\" count=\"0\" stride=\"3\"/></technique_common></source>"
                "<vertices id=\"v\"><input semantic=\"POSITION\" source=\"#p\"/></vertices>" + prims + "</mesh></geometry></library_geometries>"
                "<library_visual_scenes><visual_scene id=\"S\"><node id=\"N\"><instance_geometry url=\"#g\"/></node></visual_scene></library_visual_scenes>"
                "<scene><instance_visual_scene url=\"#S\"/></scene></COLLADA>";
        };
        auto area = [](const Mesh& me) {
            double a = 0;
            for (std::size_t t = 0; t + 2 < me.triangles.size(); t += 3) {
                const Vec3 p0 = me.vertices[(std::size_t)me.triangles[t]], p1 = me.vertices[(std::size_t)me.triangles[t + 1]], p2 = me.vertices[(std::size_t)me.triangles[t + 2]];
                a += 0.5 * Vec3::Cross(p1 - p0, p2 - p0).Magnitude();
            }
            return a;
        };
        // a strip of 4 corners (two triangles, alternate winding) and a fan of 4 corners
        const std::string quadPts = "0 0 0  1 0 0  0 1 0  1 1 0";
        ImportedModel m; std::string err;
        CHECK(ReadColladaBuffer(doc(quadPts, "<tristrips count=\"1\"><input semantic=\"VERTEX\" source=\"#v\" offset=\"0\"/><p>0 1 2 3</p></tristrips>"), "", m, &err));
        CHECK(m.parts.size() == 1 && m.parts[0].mesh.TriangleCount() == 2);
        if (!m.parts.empty()) {
            const Mesh& me = m.parts[0].mesh;
            CHECK_NEAR(area(me), 1.0, 1e-6);
            // both triangles face the same way (+z): the second one is flipped by the strip rule
            for (std::size_t t = 0; t + 2 < me.triangles.size(); t += 3) {
                const Vec3 p0 = me.vertices[(std::size_t)me.triangles[t]], p1 = me.vertices[(std::size_t)me.triangles[t + 1]], p2 = me.vertices[(std::size_t)me.triangles[t + 2]];
                CHECK(Vec3::Cross(p1 - p0, p2 - p0).z > 0.0f);
            }
        }
        CHECK(!HasNote(m, "tristrips"));
        CHECK(ReadColladaBuffer(doc("0 0 0  1 0 0  1 1 0  0 1 0", "<trifans count=\"1\"><input semantic=\"VERTEX\" source=\"#v\" offset=\"0\"/><p>0 1 2 3</p></trifans>"), "", m, &err));
        CHECK(m.parts.size() == 1 && m.parts[0].mesh.TriangleCount() == 2 && std::fabs(area(m.parts[0].mesh) - 1.0) < 1e-6);
        // an L shaped (concave) hexagon, area 3: a fan from corner 0 would cover the notch
        const std::string L = "0 0 0  2 0 0  2 1 0  1 1 0  1 2 0  0 2 0";
        CHECK(ReadColladaBuffer(doc(L, "<polygons count=\"1\"><input semantic=\"VERTEX\" source=\"#v\" offset=\"0\"/><p>0 1 2 3 4 5</p></polygons>"), "", m, &err));
        CHECK(m.parts.size() == 1 && m.parts[0].mesh.TriangleCount() == 4);
        if (!m.parts.empty()) {
            CHECK_NEAR(area(m.parts[0].mesh), 3.0, 1e-5);
            for (std::size_t t = 0; t + 2 < m.parts[0].mesh.triangles.size(); t += 3) {
                const Mesh& me = m.parts[0].mesh;
                CHECK(Vec3::Cross(me.vertices[(std::size_t)me.triangles[t + 1]] - me.vertices[(std::size_t)me.triangles[t]], me.vertices[(std::size_t)me.triangles[t + 2]] - me.vertices[(std::size_t)me.triangles[t]]).z > 0.0f);
            }
        }
        // the same shape wound the other way keeps its winding
        const std::string Lcw = "0 0 0  0 2 0  1 2 0  1 1 0  2 1 0  2 0 0";
        CHECK(ReadColladaBuffer(doc(Lcw, "<polygons count=\"1\"><input semantic=\"VERTEX\" source=\"#v\" offset=\"0\"/><p>0 1 2 3 4 5</p></polygons>"), "", m, &err));
        if (!m.parts.empty()) {
            CHECK_NEAR(area(m.parts[0].mesh), 3.0, 1e-5);
            const Mesh& me = m.parts[0].mesh;
            CHECK(Vec3::Cross(me.vertices[(std::size_t)me.triangles[1]] - me.vertices[(std::size_t)me.triangles[0]], me.vertices[(std::size_t)me.triangles[2]] - me.vertices[(std::size_t)me.triangles[0]]).z < 0.0f);
        }
        // a convex quad is still fanned from its first corner
        CHECK(ReadColladaBuffer(doc("0 0 0  1 0 0  1 1 0  0 1 0", "<polygons count=\"1\"><input semantic=\"VERTEX\" source=\"#v\" offset=\"0\"/><p>0 1 2 3</p></polygons>"), "", m, &err));
        CHECK(m.parts.size() == 1 && m.parts[0].mesh.triangles == std::vector<int>({0, 1, 2, 0, 2, 3}));
        // decimal commas (an exporter running in a locale with a decimal comma) are read as points, and the note says so
        const std::string comma = "0,0 0,0 0,0  1,5 0,0 0,0  0,0 2,5 0,0";
        CHECK(ReadColladaBuffer(doc(comma, "<triangles count=\"1\"><input semantic=\"VERTEX\" source=\"#v\" offset=\"0\"/><p>0 1 2</p></triangles>"), "", m, &err));
        CHECK(m.parts.size() == 1 && HasNote(m, "decimal comma"));
        if (!m.parts.empty()) CHECK_NEAR(area(m.parts[0].mesh), 1.5 * 2.5 / 2, 1e-5);
        // ... but "1,2,3" or "1.5,2" is not a number
        CHECK(!ReadColladaBuffer(doc("1,2,3 0 0 0 0 0 0 0", "<triangles count=\"1\"><input semantic=\"VERTEX\" source=\"#v\" offset=\"0\"/><p>0 1 2</p></triangles>"), "", m, &err));
        CHECK(!ReadColladaBuffer(doc("1.5,2 0 0 0 0 0 0 0 0", "<triangles count=\"1\"><input semantic=\"VERTEX\" source=\"#v\" offset=\"0\"/><p>0 1 2</p></triangles>"), "", m, &err));
        // UTF-16 (little and big endian, with and without a byte order mark) reads like UTF-8
        const std::string tri = doc("0 0 0  1 0 0  0 1 0", "<triangles count=\"1\"><input semantic=\"VERTEX\" source=\"#v\" offset=\"0\"/><p>0 1 2</p></triangles>");
        for (int variant = 0; variant < 4; ++variant) {
            const bool little = variant % 2 == 0, bom = variant < 2;
            std::string u16;
            if (bom) { u16.push_back(little ? '\xFF' : '\xFE'); u16.push_back(little ? '\xFE' : '\xFF'); }
            for (char c : tri) { if (little) { u16.push_back(c); u16.push_back('\0'); } else { u16.push_back('\0'); u16.push_back(c); } }
            CHECK(ReadColladaBuffer(u16, "", m, &err) && m.parts.size() == 1 && m.parts[0].mesh.TriangleCount() == 1);
        }
    }

    // The fixture file: strip, fan, bridged polygon, decimal commas (also in <unit meter="0,5">).
    {
        ImportedModel m; std::string err;
        CHECK(ReadColladaFile(kDir + "strips_concave.dae", m, &err, true));
        CHECK(m.parts.size() == 3);
        if (m.parts.size() == 3) {
            auto area = [](const Mesh& me) { double a = 0; for (std::size_t t = 0; t + 2 < me.triangles.size(); t += 3) { const Vec3 p0 = me.vertices[(std::size_t)me.triangles[t]], p1 = me.vertices[(std::size_t)me.triangles[t + 1]], p2 = me.vertices[(std::size_t)me.triangles[t + 2]]; a += 0.5 * Vec3::Cross(p1 - p0, p2 - p0).Magnitude(); } return a; };
            CHECK(m.parts[0].mesh.TriangleCount() == 2 && std::fabs(area(m.parts[0].mesh) - 1.0 * 0.25) < 1e-5);
            CHECK(m.parts[1].mesh.TriangleCount() == 3);
            CHECK(std::fabs(area(m.parts[2].mesh) - 12.0 * 0.25) < 1e-4);   // unit 0,5: every length halves, so the area is a quarter
        }
        CHECK(HasNote(m, "decimal comma"));
    }

    // ---------------- XML details: entities, CDATA, namespaces, comments ----------------
    {
        ImportedModel m; std::string err;
        const std::string doc =
            "<?xml version=\"1.0\"?><!DOCTYPE COLLADA><c:COLLADA xmlns:c=\"x\"><!-- c --><c:asset><c:up_axis>Y_UP</c:up_axis></c:asset>"
            "<c:library_geometries><c:geometry id=\"g\" name=\"A&amp;B &#65;\"><c:mesh>"
            "<c:source id=\"p\"><c:float_array id=\"pa\" count=\"9\"><![CDATA[0 0 0 1 0 0 0 1 0]]></c:float_array>"
            "<c:technique_common><c:accessor source=\"#pa\" count=\"3\" stride=\"3\"/></c:technique_common></c:source>"
            "<c:vertices id=\"v\"><c:input semantic=\"POSITION\" source=\"#p\"/></c:vertices>"
            "<c:triangles count=\"1\"><c:input semantic=\"VERTEX\" source=\"#v\" offset=\"0\"/><c:p>0 1 2</c:p></c:triangles>"
            "</c:mesh></c:geometry></c:library_geometries>"
            "<c:library_visual_scenes><c:visual_scene id=\"s\"><c:node id=\"n\"><c:instance_geometry url=\"#g\"/></c:node></c:visual_scene></c:library_visual_scenes>"
            "</c:COLLADA>";
        CHECK(ReadColladaBuffer(doc, "", m, &err));
        CHECK(m.parts.size() == 1 && m.parts[0].mesh.name == "A&B A");
        CHECK(m.parts[0].mesh.TriangleCount() == 1);
        // A missing material or texture is not an error.
        CHECK(m.materials.empty() && m.parts[0].material == -1);
    }

    // ---------------- the seam: opt-in switch, merged mesh, scene import ----------------
    {
        CHECK(HasOwnReader(".dae") && HasOwnReader(".DAE"));
        // Default for .dae is off: with no Assimp in the build nothing reads it until it is switched on.
        unsetenv("OKAY_OWN_COLLADA");
        SetImportBackend(".dae", ImportBackend::Default);
        ImportedModel m; std::string err;
#ifdef OKAY_HAVE_ASSIMP
        CHECK(EffectiveImportBackend(".dae") == ImportBackend::Assimp || EffectiveImportBackend(".dae") == ImportBackend::Own);   // Own only if built with OKAY_OWN_COLLADA
#else
        if (EffectiveImportBackend(".dae") == ImportBackend::Default) {   // not built with -DOKAY_OWN_COLLADA=ON
            CHECK(!ImportModelData(kDir + "two_materials.dae", m, ImportBackend::Default, false, &err));
            CHECK(err.find("OKAY_OWN_COLLADA") != std::string::npos);   // the message says how to turn it on
            CHECK(ImportableExtensions().find(".dae") == std::string::npos);
            bool ok = true; ImportModel(kDir + "two_materials.dae", &ok); CHECK(!ok);
        }
#endif
        // Environment switch.
        setenv("OKAY_OWN_COLLADA", "1", 1);
        CHECK(EffectiveImportBackend(".dae") == ImportBackend::Own);
        CHECK(ImportableExtensions().find(".dae") != std::string::npos);
        CHECK(ImportModelData(kDir + "two_materials.dae", m, ImportBackend::Default, false, &err));
        CHECK(m.backend == "own" && m.parts.size() == 2);
        setenv("OKAY_OWN_COLLADA", "0", 1);
        CHECK(EffectiveImportBackend(".dae") != ImportBackend::Own);
        // Run-time switch beats the environment.
        CHECK(SetImportBackend(".dae", ImportBackend::Own));
        CHECK(EffectiveImportBackend(".dae") == ImportBackend::Own);
        CHECK(ImportModelData(kDir + "two_materials.dae", m, ImportBackend::Default, false, &err) && m.backend == "own");
        // Merged one-mesh path (ImportModel): transforms baked, scale normalised.
        bool ok = false; std::string tex;
        Mesh merged = ImportModel(kDir + "two_materials.dae", &ok, &tex);
        CHECK(ok && merged.TriangleCount() == 3 && merged.vertices.size() == 7);
        CHECK(tex == kDir + "tex/wood floor.png");
        // Scene import builds the hierarchy with both materials.
        Scene scene; scene.physicsEnabled = false;
        bool sok = false;
        GameObject* root = ImportModelScene(scene, kDir + "two_materials.dae", &sok);
        CHECK(sok && root != nullptr);
        if (root) {
            int meshes = 0; bool tinted = false, textured = false;
            for (MeshRenderer* mr : root->GetComponentsInChildren<MeshRenderer>(true)) {
                ++meshes;
                if (mr->texture.empty() && mr->color.r > 0.9f && mr->color.g < 0.1f) tinted = true;
                if (mr->texture == kDir + "tex/wood floor.png") textured = true;
            }
            CHECK(meshes == 2 && tinted && textured);
        }
        // A bad file reports and fails cleanly.
        CHECK(!ImportModelData(kDir + "does_not_exist.dae", m, ImportBackend::Own, false, &err) && !err.empty());
        SetImportBackend(".dae", ImportBackend::Default);
        unsetenv("OKAY_OWN_COLLADA");
        // "*" resets every own-reader format, .dae included.
        SetImportBackend("*", ImportBackend::Own);
        CHECK(EffectiveImportBackend(".dae") == ImportBackend::Own);
        SetImportBackend("*", ImportBackend::Default);
    }

#ifdef OKAY_HAVE_ASSIMP
    // ---------------- Assimp reads the same files: compare ----------------
    for (const char* f : {"two_materials.dae", "zup_cm.dae", "skinned_strip.dae", "component_anim.dae"}) {
        ImportedModel own, ai; std::string e1, e2;
        const bool a = ImportModelData(kDir + f, own, ImportBackend::Own, false, &e1);
        const bool b = ImportModelData(kDir + f, ai, ImportBackend::Assimp, false, &e2);
        std::printf("  %-20s own: %s | assimp: %s\n", f, a ? "ok" : e1.c_str(), b ? "ok" : e2.c_str());
        CHECK(a && b);
        if (!(a && b)) continue;
        const ModelCounts co = CountModel(own), ca = CountModel(ai);
        std::printf("    own: parts %d verts %d tris %d bones %d clips %d | assimp: parts %d verts %d tris %d bones %d clips %d\n",
                    co.parts, co.vertices, co.triangles, co.bones, co.clips, ca.parts, ca.vertices, ca.triangles, ca.bones, ca.clips);
        CHECK(co.triangles == ca.triangles);
        CHECK(co.vertices == ca.vertices);
        CHECK(co.parts == ca.parts);
        CHECK(co.bones == ca.bones);
        CHECK(co.clips == ca.clips);
        // Same world-space bounds of every part (up axis and unit agree).
        auto bounds = [](const ImportedModel& m, Vec3& lo, Vec3& hi) {
            lo = Vec3{1e30f, 1e30f, 1e30f}; hi = Vec3{-1e30f, -1e30f, -1e30f};
            for (const ImportedPart& p : m.parts) {
                const Mat4 w = WorldOf(m, p.node);
                for (const Vec3& v : p.mesh.vertices) {
                    const Vec3 q = Apply(w, v);
                    lo = Vec3{std::fmin(lo.x, q.x), std::fmin(lo.y, q.y), std::fmin(lo.z, q.z)};
                    hi = Vec3{std::fmax(hi.x, q.x), std::fmax(hi.y, q.y), std::fmax(hi.z, q.z)};
                }
            }
        };
        Vec3 lo1, hi1, lo2, hi2;
        bounds(own, lo1, hi1); bounds(ai, lo2, hi2);
        CHECK(Near3(lo1, lo2.x, lo2.y, lo2.z, 1e-3f) && Near3(hi1, hi2.x, hi2.y, hi2.z, 1e-3f));
    }
    {   // Materials of the two-material file.
        ImportedModel own, ai;
        CHECK(ImportModelData(kDir + "two_materials.dae", own, ImportBackend::Own));
        CHECK(ImportModelData(kDir + "two_materials.dae", ai, ImportBackend::Assimp));
        for (const ImportedPart& po : own.parts) {
            const ImportedMaterial& mo = own.materials[(std::size_t)po.material];
            bool found = false;
            std::printf("    own material '%s': diffuse %d (%.2f) tex '%s' nrm '%s' | assimp:", mo.name.c_str(), (int)mo.hasDiffuse, mo.diffuse.r, mo.texture.c_str(), mo.normalMap.c_str());
            for (const ImportedPart& pa : ai.parts) {
                std::printf(" ['%s' d%d %.2f tex '%s' nrm '%s']", ai.materials[(std::size_t)pa.material].name.c_str(), (int)ai.materials[(std::size_t)pa.material].hasDiffuse, ai.materials[(std::size_t)pa.material].diffuse.r, ai.materials[(std::size_t)pa.material].texture.c_str(), ai.materials[(std::size_t)pa.material].normalMap.c_str());
                const ImportedMaterial& ma = ai.materials[(std::size_t)pa.material];
                // Assimp fills an unset diffuse with white; ours leaves it unset (the scene builder ignores it under a texture).
                if (ma.texture == mo.texture && ma.normalMap == mo.normalMap && (!mo.hasDiffuse || std::fabs(ma.diffuse.r - mo.diffuse.r) < 1e-4f)) found = true;
            }
            std::printf("\n");
            CHECK(found);
        }
    }
#else
    std::printf("  Assimp not built: backend comparison skipped\n");
#endif

    TEST_MAIN_RESULT();
}
