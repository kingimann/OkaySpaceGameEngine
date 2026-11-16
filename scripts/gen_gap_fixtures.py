#!/usr/bin/env python3
"""Writes the sample files of the "Assimp gap" readers into examples/own_readers (shipped in the Windows download):

  cube.mesh        an OGRE binary mesh (serializer v1.8): a unit cube, 24 vertices with normals and texture coordinates,
                   12 triangles, material "Cube/White"; read by OKAY_OWN_OGREBIN=1
  cube.zae         a ZAE archive: manifest.xml naming cube.dae (COLLADA 1.4.1, the same cube, red); read by OKAY_OWN_ZAE=1
  cube_irrmesh.xml an Irrlicht mesh saved under .xml (the same cube); read by OKAY_OWN_XML=1

Every byte is written here from the format descriptions (docs/own_ogre_reader.md, docs/own_zae_reader.md,
docs/own_xml_reader.md); nothing is copied from any other project. Run from the repository root:
    python3 scripts/gen_gap_fixtures.py
The output is deterministic (fixed zip timestamps), so running it again gives the same bytes.
"""
import os
import struct
import zipfile

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "examples", "own_readers")

# A unit cube centred on the origin: 6 faces x 4 corners, each face with its own normal and uv 0..1.
FACES = [  # normal, then the four corners counter-clockwise seen from outside
    ((0, 0, 1), [(-.5, -.5, .5), (.5, -.5, .5), (.5, .5, .5), (-.5, .5, .5)]),
    ((0, 0, -1), [(.5, -.5, -.5), (-.5, -.5, -.5), (-.5, .5, -.5), (.5, .5, -.5)]),
    ((1, 0, 0), [(.5, -.5, .5), (.5, -.5, -.5), (.5, .5, -.5), (.5, .5, .5)]),
    ((-1, 0, 0), [(-.5, -.5, -.5), (-.5, -.5, .5), (-.5, .5, .5), (-.5, .5, -.5)]),
    ((0, 1, 0), [(-.5, .5, .5), (.5, .5, .5), (.5, .5, -.5), (-.5, .5, -.5)]),
    ((0, -1, 0), [(-.5, -.5, -.5), (.5, -.5, -.5), (.5, -.5, .5), (-.5, -.5, .5)]),
]
UV = [(0, 1), (1, 1), (1, 0), (0, 0)]
VERTS = []   # (pos, normal, uv)
TRIS = []
for n, corners in FACES:
    b = len(VERTS)
    for c, uv in zip(corners, UV):
        VERTS.append((c, n, uv))
    TRIS += [(b, b + 1, b + 2), (b, b + 2, b + 3)]


def ogre_mesh():
    s = bytearray()
    def u16(v): s.extend(struct.pack("<H", v))
    def u32(v): s.extend(struct.pack("<I", v))
    def f32(v): s.extend(struct.pack("<f", v))
    def chunk(i, length=0): u16(i); u32(length)
    def line(t): s.extend(t.encode() + b"\n")
    u16(0x1000); line("[MeshSerializer_v1.8]")
    chunk(0x3000); s.append(0)                       # the mesh, not skeletally animated
    chunk(0x5000); u32(len(VERTS))                   # shared geometry
    chunk(0x5100)
    for src, typ, sem, off in ((0, 2, 1, 0), (0, 2, 4, 12), (0, 1, 7, 24)):   # float3 position, float3 normal, float2 uv
        chunk(0x5110, 16); u16(src); u16(typ); u16(sem); u16(off); u16(0)
    chunk(0x5200); u16(0); u16(32)
    chunk(0x5210)
    for p, n, uv in VERTS:
        for v in p + n + uv:
            f32(v)
    chunk(0x4000); line("Cube/White"); s.append(1); u32(3 * len(TRIS)); s.append(0)   # shared vertices, 16-bit indices
    for t in TRIS:
        for i in t:
            u16(i)
    chunk(0x4010, 8); u16(4)                         # triangle list
    chunk(0x9000, 34)
    for v in (-.5, -.5, -.5, .5, .5, .5, .8660254):
        f32(v)
    return bytes(s)


def collada():
    pos = " ".join("%g %g %g" % v[0] for v in VERTS)
    nrm = " ".join("%g %g %g" % v[1] for v in VERTS)
    idx = " ".join("%d" % i for t in TRIS for i in t)
    n = len(VERTS)
    return ("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">\n"
            "<asset><unit name=\"meter\" meter=\"1\"/><up_axis>Y_UP</up_axis></asset>\n"
            "<library_effects><effect id=\"red-fx\"><profile_COMMON><technique sid=\"common\"><phong>"
            "<diffuse><color>0.8 0.1 0.1 1</color></diffuse></phong></technique></profile_COMMON></effect></library_effects>\n"
            "<library_materials><material id=\"red\" name=\"red\"><instance_effect url=\"#red-fx\"/></material></library_materials>\n"
            "<library_geometries><geometry id=\"cube\" name=\"cube\"><mesh>\n"
            "<source id=\"cube-p\"><float_array id=\"cube-pa\" count=\"%d\">%s</float_array><technique_common>"
            "<accessor source=\"#cube-pa\" count=\"%d\" stride=\"3\"><param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/>"
            "<param name=\"Z\" type=\"float\"/></accessor></technique_common></source>\n"
            "<source id=\"cube-n\"><float_array id=\"cube-na\" count=\"%d\">%s</float_array><technique_common>"
            "<accessor source=\"#cube-na\" count=\"%d\" stride=\"3\"><param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/>"
            "<param name=\"Z\" type=\"float\"/></accessor></technique_common></source>\n"
            "<vertices id=\"cube-v\"><input semantic=\"POSITION\" source=\"#cube-p\"/><input semantic=\"NORMAL\" source=\"#cube-n\"/></vertices>\n"
            "<triangles material=\"mat\" count=\"%d\"><input semantic=\"VERTEX\" source=\"#cube-v\" offset=\"0\"/><p>%s</p></triangles>\n"
            "</mesh></geometry></library_geometries>\n"
            "<library_visual_scenes><visual_scene id=\"scene\"><node id=\"cube-node\" name=\"cube\"><instance_geometry url=\"#cube\">"
            "<bind_material><technique_common><instance_material symbol=\"mat\" target=\"#red\"/></technique_common></bind_material>"
            "</instance_geometry></node></visual_scene></library_visual_scenes>\n"
            "<scene><instance_visual_scene url=\"#scene\"/></scene>\n</COLLADA>\n") % (3 * n, pos, n, 3 * n, nrm, n, len(TRIS), idx)


def zae():
    path = os.path.join(OUT, "cube.zae")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in (("manifest.xml", "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<dae_root>./cube.dae</dae_root>\n"),
                           ("cube.dae", collada())):
            info = zipfile.ZipInfo(name, date_time=(2026, 11, 16, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)


def irrmesh():
    out = ["<?xml version=\"1.0\"?>",
           "<mesh xmlns=\"http://irrlicht.sourceforge.net/IRRMESH_09_2007\" version=\"1.0\">",
           "<buffer>",
           "<material><enum name=\"Type\" value=\"solid\" /><color name=\"Diffuse\" value=\"ff20a020\" /></material>",
           "<vertices type=\"standard\" vertexCount=\"%d\">" % len(VERTS)]
    for p, n, uv in VERTS:
        out.append("%g %g %g %g %g %g ffffffff %g %g" % (p + n + uv))
    out.append("</vertices>")
    out.append("<indices indexCount=\"%d\">" % (3 * len(TRIS)))
    out.append(" ".join("%d" % i for t in TRIS for i in t))
    out += ["</indices>", "</buffer>", "</mesh>", ""]
    return "\n".join(out)


def main():
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "cube.mesh"), "wb") as f:
        f.write(ogre_mesh())
    zae()
    with open(os.path.join(OUT, "cube_irrmesh.xml"), "w", newline="\n") as f:
        f.write(irrmesh())
    for n in ("cube.mesh", "cube.zae", "cube_irrmesh.xml"):
        print(n, os.path.getsize(os.path.join(OUT, n)), "bytes")


if __name__ == "__main__":
    main()
