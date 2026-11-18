#pragma once
// SYNTHETIC fixtures for the own readers of the "Assimp gap" batch: the OGRE binary mesh (.mesh), ZAE (.zae) and the .xml
// dispatch. Every byte is written here from the format descriptions (docs/own_ogre_reader.md, docs/own_zae_reader.md); no
// file of any other project is copied.
#include "model_fixture_util.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace fx {

// ---- OGRE binary mesh ----
// The binary twin of BuildOgreXml (batchd_fixtures.hpp): the same shared geometry (4 vertices; position + normal in buffer 0,
// texture coordinates in buffer 1), submesh 0 "Mat/A" on it (two triangles, a bone assignment), submesh 1 "Mat/B" with its own
// 3 vertices, the name "left" for submesh 0 and the skeleton link "x.skeleton".
struct OgreBinOpts {
    std::string version = "[MeshSerializer_v1.8]";
    bool index32 = false;
    int op0 = 4;                  // submesh 0's operation (4 list, 5 strip, 6 fan, 2 lines)
    bool stripIndices = false;    // submesh 0 written as the strip / fan sequence 0 1 3 2 (strip) or 0 1 2 3 (fan)
    bool edgeList = true;         // a chunk the reader skips by its length
    std::uint32_t badTailLength = 0;   // > 0: a trailing unknown chunk claims this length
    bool extraUvSet = false;      // a second texture coordinate element (index 1) in buffer 1
};

inline void OChunk(std::string& s, unsigned id, std::uint32_t len) { P16(s, id); P32(s, len); }
inline void OLine(std::string& s, const std::string& t) { s += t; s += '\n'; }

inline std::string BuildOgreBin(const OgreBinOpts& o = OgreBinOpts()) {
    std::string s;
    P16(s, 0x1000); OLine(s, o.version);
    OChunk(s, 0x3000, 0); s.push_back(1);   // the mesh (its length is not used), skeletally animated
    auto geometry = [&](std::uint32_t vc, const std::vector<float>& pn, const std::vector<float>& uv, bool extra) {
        OChunk(s, 0x5000, 0); P32(s, vc);
        OChunk(s, 0x5100, 6);
        OChunk(s, 0x5110, 16); P16(s, 0); P16(s, 2); P16(s, 1); P16(s, 0); P16(s, 0);    // buffer 0: float3 position at 0
        OChunk(s, 0x5110, 16); P16(s, 0); P16(s, 2); P16(s, 4); P16(s, 12); P16(s, 0);   // float3 normal at 12
        if (!uv.empty()) {
            OChunk(s, 0x5110, 16); P16(s, 1); P16(s, 1); P16(s, 7); P16(s, 0); P16(s, 0);   // buffer 1: float2 uv set 0
            if (extra) { OChunk(s, 0x5110, 16); P16(s, 1); P16(s, 1); P16(s, 7); P16(s, 8); P16(s, 1); }   // uv set 1
        }
        OChunk(s, 0x5200, 0); P16(s, 0); P16(s, 24);
        OChunk(s, 0x5210, 0); for (float f : pn) PF(s, f);
        if (!uv.empty()) {
            const unsigned stride = extra ? 16 : 8;
            OChunk(s, 0x5200, 0); P16(s, 1); P16(s, stride);
            OChunk(s, 0x5210, 0);
            for (std::size_t i = 0; i + 1 < uv.size(); i += 2) { PF(s, uv[i]); PF(s, uv[i + 1]); if (extra) { PF(s, 9); PF(s, 9); } }
        }
    };
    // shared geometry
    geometry(4, {0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 1, 2, 2, 0, 0, 0, 1, 0, 2, 0, 0, 0, 1}, {0, 0, 1, 0, 1, 1, 0, 1}, o.extraUvSet);
    auto submesh = [&](const std::string& mat, bool shared, const std::vector<std::uint32_t>& idx) {
        OChunk(s, 0x4000, 0); OLine(s, mat); s.push_back(shared ? 1 : 0);
        P32(s, (std::uint32_t)idx.size()); s.push_back(o.index32 ? 1 : 0);
        for (std::uint32_t i : idx) { if (o.index32) P32(s, i); else P16(s, i); }
    };
    submesh("Mat/A", true, o.stripIndices ? (o.op0 == 5 ? std::vector<std::uint32_t>{0, 1, 3, 2} : std::vector<std::uint32_t>{0, 1, 2, 3}) : std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
    OChunk(s, 0x4010, 8); P16(s, (unsigned)o.op0);
    OChunk(s, 0x4100, 16); P32(s, 0); P16(s, 0); PF(s, 1.0f);   // vertex 0 -> bone 0, weight 1
    submesh("Mat/B", false, {0, 1, 2});
    geometry(3, {0, 0, 1, 0, 0, 1, 3, 0, 1, 0, 0, 1, 0, 4, 1, 0, 0, 1}, {}, false);
    OChunk(s, 0x4010, 8); P16(s, 4);
    OChunk(s, 0x6000, 0); OLine(s, "x.skeleton");
    OChunk(s, 0x9000, 34); for (float f : {0.f, 0.f, 0.f, 3.f, 4.f, 1.f, 5.f}) PF(s, f);
    OChunk(s, 0xA000, 0);
    OChunk(s, 0xA100, 0); P16(s, 0); OLine(s, "left");
    if (o.edgeList) { OChunk(s, 0xB000, 6 + 10); s += std::string(10, '\x07'); }
    if (o.badTailLength) { OChunk(s, 0xD000, o.badTailLength); s += "xx"; }
    return s;
}

// ---- COLLADA and ZAE ----
// A small COLLADA document: one triangle (0,0,0) (2,0,0) (0,2,0) with the material "m" whose diffuse texture is `image`
// (a relative path as written in <init_from>; empty: no texture).
inline std::string BuildMiniDae(const std::string& image = "./tex%20a.png") {
    std::string img, eff;
    if (!image.empty()) {
        img = "<library_images><image id=\"img\"><init_from>" + image + "</init_from></image></library_images>";
        eff = "<newparam sid=\"surf\"><surface type=\"2D\"><init_from>img</init_from></surface></newparam>"
              "<newparam sid=\"samp\"><sampler2D><source>surf</source></sampler2D></newparam>"
              "<technique sid=\"t\"><phong><diffuse><texture texture=\"samp\" texcoord=\"uv\"/></diffuse></phong></technique>";
    } else eff = "<technique sid=\"t\"><phong><diffuse><color>1 0 0 1</color></diffuse></phong></technique>";
    return "<?xml version=\"1.0\"?>\n<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">\n"
           "<asset><up_axis>Y_UP</up_axis></asset>" + img +
           "<library_effects><effect id=\"e\"><profile_COMMON>" + eff + "</profile_COMMON></effect></library_effects>"
           "<library_materials><material id=\"m\" name=\"m\"><instance_effect url=\"#e\"/></material></library_materials>"
           "<library_geometries><geometry id=\"g\"><mesh>"
           "<source id=\"p\"><float_array id=\"pa\" count=\"9\">0 0 0 2 0 0 0 2 0</float_array>"
           "<technique_common><accessor source=\"#pa\" count=\"3\" stride=\"3\"><param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/><param name=\"Z\" type=\"float\"/></accessor></technique_common></source>"
           "<vertices id=\"v\"><input semantic=\"POSITION\" source=\"#p\"/></vertices>"
           "<triangles material=\"mat\" count=\"1\"><input semantic=\"VERTEX\" source=\"#v\" offset=\"0\"/><p>0 1 2</p></triangles>"
           "</mesh></geometry></library_geometries>"
           "<library_visual_scenes><visual_scene id=\"s\"><node id=\"n\" name=\"n\"><instance_geometry url=\"#g\"><bind_material><technique_common>"
           "<instance_material symbol=\"mat\" target=\"#m\"/></technique_common></bind_material></instance_geometry></node></visual_scene></library_visual_scenes>"
           "<scene><instance_visual_scene url=\"#s\"/></scene></COLLADA>\n";
}

inline std::string BuildManifest(const std::string& root) { return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<dae_root>" + root + "</dae_root>"; }

// A ZAE: manifest.xml naming "sub/doc%20one.dae", the document there (deflated), its image "sub/tex a.png" and a decoy .dae at
// the top that must not be read when the manifest names another.
inline std::string BuildZae(bool manifest = true, bool decoy = true) {
    std::vector<ZipEntry> es;
    if (manifest) es.push_back(ZipEntry{"manifest.xml", BuildManifest("./sub/doc%20one.dae")});
    ZipEntry d{"sub/doc one.dae", BuildMiniDae()}; d.deflate = true;
    es.push_back(d);
    es.push_back(ZipEntry{"sub/tex a.png", std::string("\x89PNG\r\n\x1a\n-not-a-real-image-", 26)});
    if (decoy) es.push_back(ZipEntry{"zz_decoy.dae", "<COLLADA><library_geometries/></COLLADA>"});
    return MakeZip(es);
}

} // namespace fx
