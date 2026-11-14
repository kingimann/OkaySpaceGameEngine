#pragma once
// Synthetic fixtures for the batch-D own readers (AC3D, OGRE XML, XGL / ZGL, Irrlicht scene, NFF), built in memory so
// nothing binary is committed. Shared by the per-reader tests and by the hostile-input / mutation-fuzz test. Every number
// is chosen so it can be checked by hand; the comment above each builder says what the numbers are.
#include "model_fixture_util.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace fx {

// Area of the model with every part seen through its node's world matrix (the readers keep node transforms).
inline double WorldArea(const okay::ImportedModel& m) {
    std::vector<okay::Mat4> w(m.nodes.size());
    for (std::size_t i = 0; i < m.nodes.size(); ++i) {
        const okay::ImportedNode& n = m.nodes[i];
        const okay::Mat4 l = okay::Mat4::TRS(n.position, n.rotation, n.scale);
        w[i] = n.parent >= 0 ? w[(std::size_t)n.parent] * l : l;
    }
    double a = 0;
    for (const okay::ImportedPart& p : m.parts) {
        const okay::Mat4 M = p.node >= 0 && (std::size_t)p.node < w.size() ? w[(std::size_t)p.node] : okay::Mat4::Identity();
        for (std::size_t i = 0; i + 2 < p.mesh.triangles.size(); i += 3) {
            const okay::Vec3 x = M.MultiplyPoint(p.mesh.vertices[(std::size_t)p.mesh.triangles[i]]), y = M.MultiplyPoint(p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 1]]), z = M.MultiplyPoint(p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 2]]);
            a += 0.5 * okay::Vec3::Cross(y - x, z - x).Magnitude();
        }
    }
    return a;
}
inline std::string ToUtf16leD(const std::string& u8) {
    std::string s = "\xFF\xFE";
    for (unsigned char c : u8) { s.push_back((char)c); s.push_back('\0'); }
    return s;
}

// ---- AC3D --------------------------------------------------------------------------------------------------------
// Two materials; world > (quad, group > (tri), strip).
//   quad   : textured "tex/a.png" with texrep 2 1 and texoff 0.5 0, loc (10,0,0), 4 vertices (0,0,0) (2,0,0) (2,3,0) (0,3,0), one polygon
//            of 4 refs (material 0, two sided) = two triangles, area 6; corner uv (0,0) (1,0) (1,1) (0,1)
//   group  : rot = Rz(90 degrees) as the matrix rows (0,-1,0) (1,0,0) (0,0,1), loc (0,5,0), child poly "tri": (0,0,0) (1,0,0) (0,1,0),
//            a triangle (material 1, area 0.5) and a line surface (type 2) that is skipped
//   strip  : 4 vertices (0,0,0) (1,0,0) (0,1,0) (1,1,0) with stored normals (0,0,1), one type-4 surface of 4 refs = two triangles, area 1
// Totals: 3 parts, 5 triangles, area 7.5, 3 materials (red with the texture, blue, red without it), 6 nodes.
inline std::string BuildAc3d(const char* eol = "\n") {
    const std::string n = eol;
    std::string s;
    auto L = [&](const std::string& l) { s += l + n; };
    L("AC3Db");
    L("MATERIAL \"red\" rgb 1 0 0  amb 0.2 0.2 0.2  emis 0.1 0.2 0.3  spec 0.5 0.5 0.5  shi 50  trans 0.25");
    L("MATERIAL \"blue\" rgb 0 0 1  amb 0 0 0  emis 0 0 0  spec 0 0 0  shi 0  trans 0");
    L("OBJECT world"); L("name \"w\""); L("kids 3");
    L("OBJECT poly"); L("name \"quad\""); L("texture \"tex/a.png\""); L("texrep 2 1"); L("texoff 0.5 0"); L("loc 10 0 0");
    L("numvert 4"); L("0 0 0"); L("2 0 0"); L("2 3 0"); L("0 3 0");
    L("numsurf 1"); L("SURF 0x20"); L("mat 0"); L("refs 4"); L("0 0 0"); L("1 1 0"); L("2 1 1"); L("3 0 1"); L("kids 0");
    L("OBJECT group"); L("name \"grp\""); L("data 3"); L("xyz"); L("url \"u.txt\""); L("bogus 1"); L("rot 0 -1 0 1 0 0 0 0 1"); L("loc 0 5 0"); L("kids 1");
    L("OBJECT poly"); L("name \"tri\"");
    L("numvert 3"); L("0 0 0"); L("1 0 0"); L("0 1 0");
    L("numsurf 2"); L("SURF 0x00"); L("mat 1"); L("refs 3"); L("0 0 0"); L("1 0 0"); L("2 0 0");
    L("SURF 0x02"); L("mat 1"); L("refs 2"); L("0 0 0"); L("1 0 0"); L("kids 0");
    L("OBJECT poly"); L("name \"strip\"");
    L("numvert 4"); L("0 0 0 0 0 1"); L("1 0 0 0 0 1"); L("0 1 0 0 0 1"); L("1 1 0 0 0 1");
    L("numsurf 1"); L("SURF 0x14"); L("mat 0"); L("refs 4"); L("0 0 0"); L("1 0 0"); L("2 0 0"); L("3 0 0"); L("kids 0");
    return s;
}
// (the group also has a `data` block of 3 characters, a `url` line and one unknown keyword: all skipped, the unknown one is counted)
// A cube of edge 2 centred on the origin: 8 vertices, 6 quads (counter-clockwise from outside), `subdiv` levels, flat shaded
// (`shaded`: SURF 0x10 and `crease` 180, so the corner normals are averaged over the three faces).
// One Catmull-Clark level: 24 quads = 48 triangles; every old corner goes to (5/9, 5/9, 5/9) (up to sign), every face point
// (the +x face centre is (1,0,0)) stays on the surface of the box [-1,1]^3 at its centre, and an edge point is (0.75, 0.75, 0).
inline std::string BuildAc3dCube(int subdiv, bool shaded = false) {
    std::string s = "AC3Db\nMATERIAL \"m\" rgb 1 1 1 amb 0 0 0 emis 0 0 0 spec 0 0 0 shi 0 trans 0\nOBJECT world\nkids 1\nOBJECT poly\nname \"cube\"\n";
    if (subdiv > 0) s += "subdiv " + std::to_string(subdiv) + "\n";
    if (shaded) s += "crease 180\n";
    s += "numvert 8\n-1 -1 -1\n1 -1 -1\n1 1 -1\n-1 1 -1\n-1 -1 1\n1 -1 1\n1 1 1\n-1 1 1\nnumsurf 6\n";
    const int q[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {0, 4, 7, 3}};
    for (auto& f : q) { s += shaded ? "SURF 0x10\nmat 0\nrefs 4\n" : "SURF 0x00\nmat 0\nrefs 4\n"; for (int i = 0; i < 4; ++i) s += std::to_string(f[i]) + " 0 0\n"; }
    s += "kids 0\n";
    return s;
}

// ---- OGRE XML ----------------------------------------------------------------------------------------------------
// Shared geometry of 4 vertices over two vertex buffers (positions and normals in one, texcoords in the other):
// (0,0,0) (2,0,0) (2,2,0) (0,2,0), normals (0,0,1), uv (0,0) (1,0) (1,1) (0,1). Submesh 0 (shared, material "Mat/A", named "left"):
// faces (0,1,2) (0,2,3), area 4. Submesh 1 (own geometry of 3 vertices (0,0,1) (3,0,1) (0,4,1), material "Mat/B", no uv, face (0,1,2)),
// area 6. A skeleton link and bone assignments (named in the notes). Totals: 2 parts, 3 triangles, area 10.
inline std::string BuildOgreXml() {
    return
        "<mesh>\n"
        " <sharedgeometry vertexcount=\"4\">\n"
        "  <vertexbuffer positions=\"true\" normals=\"true\">\n"
        "   <vertex><position x=\"0\" y=\"0\" z=\"0\"/><normal x=\"0\" y=\"0\" z=\"1\"/></vertex>\n"
        "   <vertex><position x=\"2\" y=\"0\" z=\"0\"/><normal x=\"0\" y=\"0\" z=\"1\"/></vertex>\n"
        "   <vertex><position x=\"2\" y=\"2\" z=\"0\"/><normal x=\"0\" y=\"0\" z=\"1\"/></vertex>\n"
        "   <vertex><position x=\"0\" y=\"2\" z=\"0\"/><normal x=\"0\" y=\"0\" z=\"1\"/></vertex>\n"
        "  </vertexbuffer>\n"
        "  <vertexbuffer texture_coords=\"1\" texture_coord_dimensions_0=\"float2\">\n"
        "   <vertex><texcoord u=\"0\" v=\"0\"/></vertex>\n   <vertex><texcoord u=\"1\" v=\"0\"/></vertex>\n"
        "   <vertex><texcoord u=\"1\" v=\"1\"/></vertex>\n   <vertex><texcoord u=\"0\" v=\"1\"/></vertex>\n"
        "  </vertexbuffer>\n"
        " </sharedgeometry>\n"
        " <submeshes>\n"
        "  <submesh material=\"Mat/A\" usesharedvertices=\"true\" use32bitindexes=\"false\" operationtype=\"triangle_list\">\n"
        "   <faces count=\"2\"><face v1=\"0\" v2=\"1\" v3=\"2\"/><face v1=\"0\" v2=\"2\" v3=\"3\"/></faces>\n"
        "   <boneassignments><vertexboneassignment vertexindex=\"0\" boneindex=\"0\" weight=\"1\"/></boneassignments>\n"
        "  </submesh>\n"
        "  <submesh material=\"Mat/B\" usesharedvertices=\"false\" use32bitindexes=\"false\" operationtype=\"triangle_list\">\n"
        "   <faces count=\"1\"><face v1=\"0\" v2=\"1\" v3=\"2\"/></faces>\n"
        "   <geometry vertexcount=\"3\"><vertexbuffer positions=\"true\" normals=\"true\">\n"
        "    <vertex><position x=\"0\" y=\"0\" z=\"1\"/><normal x=\"0\" y=\"0\" z=\"1\"/></vertex>\n"
        "    <vertex><position x=\"3\" y=\"0\" z=\"1\"/><normal x=\"0\" y=\"0\" z=\"1\"/></vertex>\n"
        "    <vertex><position x=\"0\" y=\"4\" z=\"1\"/><normal x=\"0\" y=\"0\" z=\"1\"/></vertex>\n"
        "   </vertexbuffer></geometry>\n"
        "  </submesh>\n"
        " </submeshes>\n"
        " <submeshnames><submeshname name=\"left\" index=\"0\"/></submeshnames>\n"
        " <skeletonlink name=\"x.skeleton\"/>\n"
        "</mesh>\n";
}
// A material script for it: Mat/A has a parent, a first pass with diffuse (0.5 0.25 0.75 0.5), emissive (0.1 0.2 0.3), specular with
// shininess 32, and a texture unit "tex_a.png"; a second pass colour must not be read.
inline std::string BuildOgreMaterial() {
    return
        "// a comment\n"
        "material Mat/A : Parent\n{\n technique t1\n {\n  pass p1\n  {\n   ambient 0.1 0.1 0.1\n   diffuse 0.5 0.25 0.75 0.5\n"
        "   specular 1 1 1 1 32\n   emissive 0.1 0.2 0.3\n   texture_unit tu\n   {\n    texture tex_a.png\n    scale 1 1\n   }\n  }\n"
        "  pass p2\n  {\n   diffuse 0 0 0\n  }\n }\n}\n";
}

// ---- XGL / ZGL ---------------------------------------------------------------------------------------------------
// World material 0 (diffuse (1,0,0), emissive (0.1,0.2,0.3), shine 64, alpha 0.5), mesh 0 with its own material 1 (diffuse (0,0,1)),
// positions (0,0,0) (1,0,0) (1,1,0) (0,1,0) (0,2,0), one normal (0,0,1), texture coordinates (0,0) (1,0) (1,1) (0,1), faces:
// (0,1,2) and (0,2,3) with material 0 and uv (area 1), (0,1,4) with material 1 (area 1). Object "A": forward (1,0,0), up (0,1,0)
// (a turn of 90 degrees about Y), position (5,0,0), scale 2, mesh 0 by reference; its child "B" holds mesh 0 again, no transform.
// Totals: 2 objects, each 2 parts (so 4 parts) and 3 triangles, 6 triangles, area 2 * (2 * 2 * 2) = 16 in the world, 3 nodes.
inline std::string BuildXgl() {
    return
        "<WORLD>\n"
        "<BACKGROUND><BACKCOLOR>0,0,0</BACKCOLOR></BACKGROUND>\n"
        "<LIGHTING><AMBIENT>1,1,1</AMBIENT></LIGHTING>\n"
        "<MAT ID=\"0\"><AMB>1,0,0</AMB><DIFF>1,0,0</DIFF><SPEC>1,1,1</SPEC><EMISS>0.1,0.2,0.3</EMISS><SHINE>64</SHINE><ALPHA>0.5</ALPHA></MAT>\n"
        "<MESH ID=\"0\">\n"
        "<MAT ID=\"1\"><DIFF>0,0,1</DIFF></MAT>\n"
        "<P ID=\"0\">0,0,0</P><P ID=\"1\">1,0,0</P><P ID=\"2\">1,1,0</P><P ID=\"3\">0,1,0</P><P ID=\"4\">0,2,0</P>\n"
        "<N ID=\"0\">0,0,1</N>\n"
        "<TC ID=\"0\">0,0</TC><TC ID=\"1\">1,0</TC><TC ID=\"2\">1,1</TC><TC ID=\"3\">0,1</TC>\n"
        "<F><MATREF>0</MATREF><FV1><PREF>0</PREF><NREF>0</NREF><TCREF>0</TCREF></FV1><FV2><PREF>1</PREF><NREF>0</NREF><TCREF>1</TCREF></FV2><FV3><PREF>2</PREF><NREF>0</NREF><TCREF>2</TCREF></FV3></F>\n"
        "<F><MATREF>0</MATREF><FV1><PREF>0</PREF><NREF>0</NREF><TCREF>0</TCREF></FV1><FV2><PREF>2</PREF><NREF>0</NREF><TCREF>2</TCREF></FV2><FV3><PREF>3</PREF><NREF>0</NREF><TCREF>3</TCREF></FV3></F>\n"
        "<F><MATREF>1</MATREF><FV1><PREF>0</PREF><NREF>0</NREF></FV1><FV2><PREF>1</PREF><NREF>0</NREF></FV2><FV3><PREF>4</PREF><NREF>0</NREF></FV3></F>\n"
        "</MESH>\n"
        "<OBJECT><NAME>A</NAME><TRANSFORM><FORWARD>1,0,0</FORWARD><UP>0,1,0</UP><POSITION>5,0,0</POSITION><SCALE>2</SCALE></TRANSFORM><MESHREF>0</MESHREF>\n"
        " <OBJECT><NAME>B</NAME><MESHREF>0</MESHREF></OBJECT>\n"
        "</OBJECT>\n"
        "</WORLD>\n";
}
// The same text as one zlib stream made of stored blocks (a valid zlib stream; the Adler-32 is written too).
inline std::string ToZlibStored(const std::string& t) {
    std::string s;
    s.push_back((char)0x78); s.push_back((char)0x01);
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, t.size() - pos);
        const bool last = pos + n >= t.size();
        s.push_back(last ? 1 : 0);
        s.push_back((char)(n & 255)); s.push_back((char)(n >> 8));
        s.push_back((char)(~n & 255)); s.push_back((char)((~n >> 8) & 255));
        s.append(t, pos, n);
        pos += n;
    } while (pos < t.size());
    std::uint32_t a = 1, b = 0;
    for (unsigned char c : t) { a = (a + c) % 65521u; b = (b + a) % 65521u; }
    const std::uint32_t ad = (b << 16) | a;
    for (int i = 3; i >= 0; --i) s.push_back((char)((ad >> (8 * i)) & 255));
    return s;
}

// ---- Irrlicht scene ----------------------------------------------------------------------------------------------
// Scene attributes, a camera (kept as an empty node), a cube node "cube" (Size 2, Position (1,2,3), Rotation (0,0,90), diffuse ff00ff00,
// Texture1 "t/a.png", an animator), an empty "group" at (0,10,0) holding a sphere "ball" (Radius 1, PolyCountX 8, PolyCountY 4) and a
// mesh node "m" at (0,0,5) naming "tri.irrmesh" (read through the loader). The cube: 12 triangles, area 24; the sphere: 48 triangles.
inline std::string BuildIrrScene(const char* eol = "\n") {
    const std::string n = eol;
    std::string s;
    auto L = [&](const std::string& l) { s += l + n; };
    L("<?xml version=\"1.0\"?>");
    L("<irr_scene>");
    L(" <attributes><string name=\"Name\" value=\"root\" /><int name=\"Id\" value=\"-1\" /></attributes>");
    L(" <node type=\"camera\"><attributes><string name=\"Name\" value=\"cam\" /><vector3d name=\"Position\" value=\"0, 0, -10\" /></attributes></node>");
    L(" <node type=\"cube\">");
    L("  <attributes><string name=\"Name\" value=\"cube\" /><vector3d name=\"Position\" value=\"1.0, 2.0, 3.0\" /><vector3d name=\"Rotation\" value=\"0.0, 0.0, 90.0\" />");
    L("   <vector3d name=\"Scale\" value=\"1.0, 1.0, 1.0\" /><float name=\"Size\" value=\"2.0\" /></attributes>");
    L("  <materials><attributes><enum name=\"Type\" value=\"solid\" /><color name=\"Diffuse\" value=\"ff00ff00\" /><color name=\"Emissive\" value=\"00000000\" /><float name=\"Shininess\" value=\"5.0\" />");
    L("   <texture name=\"Texture1\" value=\"t/a.png\" /></attributes></materials>");
    L("  <animators><attributes><string name=\"Type\" value=\"rotation\" /></attributes></animators>");
    L(" </node>");
    L(" <node type=\"empty\">");
    L("  <attributes><string name=\"Name\" value=\"group\" /><vector3d name=\"Position\" value=\"0, 10, 0\" /></attributes>");
    L("  <node type=\"sphere\"><attributes><string name=\"Name\" value=\"ball\" /><float name=\"Radius\" value=\"1.0\" /><int name=\"PolyCountX\" value=\"8\" /><int name=\"PolyCountY\" value=\"4\" /></attributes></node>");
    L("  <node type=\"mesh\"><attributes><string name=\"Name\" value=\"m\" /><vector3d name=\"Position\" value=\"0, 0, 5\" /><string name=\"Mesh\" value=\"tri.irrmesh\" /></attributes></node>");
    L(" </node>");
    L("</irr_scene>");
    return s;
}

// ---- NFF ---------------------------------------------------------------------------------------------------------
// Classic NFF. Viewpoint, background and a light (all skipped). Red fill (shine 20); a polygon (0,0,0) (2,0,0) (2,2,0) (0,2,0), area 4;
// a patch of 3 vertices with normals (0,0,1): (0,0,1) (3,0,1) (0,3,1), area 4.5 (the polygon and the patch are two parts); `tess 1`;
// a cylinder of radius 1 from (0,0,0) to (0,0,2): 4 segments, 16 triangles, area 8*sqrt(2) + 4 = 15.3137; green fill; a sphere r=1 at the
// origin (tess 1 = 80 triangles); a hex solid (cube) of circumradius 1: area 8. Totals: 5 parts, 2 + 1 + 16 + 80 + 12 = 111 triangles.
inline std::string BuildNff(const char* eol = "\n") {
    const std::string n = eol;
    std::string s;
    auto L = [&](const std::string& l) { s += l + n; };
    L("# a comment line");
    L("v"); L("from 0 0 5"); L("at 0 0 0"); L("up 0 1 0"); L("angle 45"); L("hither 1"); L("resolution 100 100");
    L("b 0.1 0.2 0.3");
    L("l 1 2 3");
    L("f 1 0 0 0.5 0.5 20 0 1");
    L("p 4"); L("0 0 0"); L("2 0 0"); L("2 2 0"); L("0 2 0");
    L("pp 3"); L("0 0 1  0 0 1"); L("3 0 1  0 0 1"); L("0 3 1  0 0 1");
    L("tess 1");
    L("c"); L("0 0 0 1"); L("0 0 2 1");
    L("f 0 1 0");
    L("s 0 0 0 1");
    L("hex 0 0 0 1");
    return s;
}
// Sense8 "nff" object: a header, the name, an mtable line, 4 vertices with uv, 2 polygons: a quad (matid 1, "both", texture tex.tga) and a
// triangle (matid 0). Area 4 + 2 = 6, 3 triangles, 2 parts, uv on the quad.
inline std::string BuildNffSense8(const char* eol = "\n") {
    const std::string n = eol;
    std::string s;
    auto L = [&](const std::string& l) { s += l + n; };
    L("nff // header"); L("version 3.0"); L("viewpos 1 2 3"); L("");
    L("My_Object"); L("mtable m.mat"); L("");
    L("4 // vertices");
    L("0 0 0 uv 0 0"); L("2 0 0 uv 1 0"); L("2 2 0 uv 1 1"); L("0 2 0 uv 0 1"); L("");
    L("2 // polygons");
    L("4 0 1 2 3 matid 1 both tex.tga"); L("3 0 1 2 matid 0");
    return s;
}
inline std::string BuildNffMat() {
    return "mat // materials\nversion 3.0\nvalid ambientdiffuse specular shininess opacity\n\nmatdef // 0\nambientdiffuse 0.9 0.8 0.7\nspecular 1 1 1\nshininess 128\n\n"
           "matdef // 1\nambientdiffuse 0 1 0\nopacity 0.6\n";
}

} // namespace fx
