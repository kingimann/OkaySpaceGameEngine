#!/usr/bin/env python3
"""Writes the Blender (.blend) fixtures for the own Blender reader into tests/data/models/.

IMPORTANT, read this first: these files were NOT written by Blender. They are SYNTHETIC. The script
builds a small SDNA (the struct description that sits at the end of every .blend) from a hand written
subset of Blender's real struct and field names, and then writes blocks in the documented container
layout (BLENDER header, 20/24 byte block headers, DNA1, ENDB). What they prove is that the reader's
code paths (the 3.x / 4.x attribute mesh storage, face offsets, node-tree materials, collections,
armatures with vertex-group weights, F-Curves with bezier handles, big endian + 32 bit pointers, gzip)
agree with this script's reading of the format. They do NOT prove agreement with a real Blender 3.x /
4.x file: no such file was available when they were written. The real-file evidence for the reader is
in docs/own_blend_reader.md (Assimp's test directory: Blender 2.41 to 2.93 files).

Deterministic: running it twice gives the same bytes. Run from the repository root:
    python3 scripts/gen_blend_fixtures.py
The expected values are checked in tests/test_blend_reader.cpp.
"""
import gzip
import math
import os
import struct

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "data", "models")

# ---------------------------------------------------------------------------------------------
# DNA description: struct name -> list of (type, field name as it appears in SDNA)
# ---------------------------------------------------------------------------------------------
PRIM = {"char": 1, "uchar": 1, "short": 2, "ushort": 2, "int": 4, "uint": 4, "float": 4, "double": 8, "int64_t": 8, "void": 0}


def S(*fields):
    return [tuple(f.split(" ", 1)) for f in fields]


def dna_defs(naming):
    """naming: '36' (totvert / vdata / poly_offset_indices), '41' (verts_num / vert_data / face_offset_indices),
    'legacy' (2.7x: mvert / mpoly / mloop / dvert pointers)."""
    d = {}
    d["ListBase"] = S("void *first", "void *last")
    d["ID"] = S("void *next", "void *prev", "ID *newid", "Library *lib", "char name[66]", "short flag", "int tag", "int us", "int icon_id", "IDProperty *properties")
    d["Library"] = S("int dummy")
    d["IDProperty"] = S("int dummy")
    d["AnimData"] = S("bAction *action", "ListBase nla_tracks", "ListBase drivers")
    d["Object"] = S("ID id", "AnimData *adt", "short type", "short partype", "int par1", "int par2", "int par3", "char parsubstr[64]",
                    "Object *parent", "void *data", "ListBase modifiers", "Material **mat", "char *matbits", "int totcol",
                    "float loc[3]", "float dloc[3]", "float size[3]", "float dsize[3]", "float dscale[3]", "float rot[3]", "float drot[3]",
                    "float quat[4]", "float dquat[4]", "float rotAxis[3]", "float drotAxis[3]", "float rotAngle", "float drotAngle",
                    "float obmat[4][4]", "float parentinv[4][4]", "short rotmode", "bPose *pose", "ListBase defbase", "ListBase constraints")
    d["CustomDataLayer"] = S("int type", "int offset", "int flag", "int active", "int active_rnd", "int active_clone", "int active_mask", "int uid", "char name[64]", "void *data")
    d["CustomData"] = S("CustomDataLayer *layers", "int typemap[51]", "int totlayer", "int maxlayer", "int totsize", "void *pool", "void *external")
    if naming == "legacy":
        d["Mesh"] = S("ID id", "AnimData *adt", "Key *key", "Material **mat", "MPoly *mpoly", "MLoop *mloop", "MLoopUV *mloopuv", "MVert *mvert",
                      "MDeformVert *dvert", "CustomData vdata", "CustomData edata", "CustomData fdata", "CustomData pdata", "CustomData ldata",
                      "int totvert", "int totedge", "int totface", "int totpoly", "int totloop", "short totcol", "short flag")
    elif naming == "36":
        d["Mesh"] = S("ID id", "AnimData *adt", "Key *key", "Material **mat", "int totvert", "int totedge", "int totpoly", "int totloop",
                      "CustomData vdata", "CustomData edata", "CustomData fdata", "CustomData pdata", "CustomData ldata", "int *poly_offset_indices",
                      "short totcol", "short flag")
    else:
        d["Mesh"] = S("ID id", "AnimData *adt", "Key *key", "Material **mat", "int verts_num", "int edges_num", "int faces_num", "int corners_num",
                      "CustomData vert_data", "CustomData edge_data", "CustomData fdata_legacy", "CustomData face_data", "CustomData corner_data",
                      "int *face_offset_indices", "short totcol", "short flag")
    d["Key"] = S("ID id")
    d["MVert"] = S("float co[3]", "short no[3]", "char flag", "char bweight")
    d["MPoly"] = S("int loopstart", "int totloop", "short mat_nr", "char flag", "char _pad")
    d["MLoop"] = S("int v", "int e")
    d["MLoopUV"] = S("float uv[2]", "int flag")
    d["MDeformVert"] = S("MDeformWeight *dw", "int totweight", "int flag")
    d["MDeformWeight"] = S("int def_nr", "float weight")
    d["bDeformGroup"] = S("bDeformGroup *next", "bDeformGroup *prev", "char name[64]", "char flag", "char _pad0[7]")
    d["Material"] = S("ID id", "AnimData *adt", "short flag", "float r", "float g", "float b", "float a", "float metallic", "float roughness",
                      "char use_nodes", "bNodeTree *nodetree", "char blend_method")
    d["bNodeTree"] = S("ID id", "ListBase nodes", "ListBase links")
    d["bNode"] = S("bNode *next", "bNode *prev", "char idname[64]", "char name[64]", "ListBase inputs", "ListBase outputs", "ID *id")
    d["bNodeSocket"] = S("bNodeSocket *next", "bNodeSocket *prev", "char name[64]", "void *default_value")
    d["bNodeSocketValueRGBA"] = S("float value[4]")
    d["bNodeSocketValueFloat"] = S("float value", "float min", "float max")
    d["bNodeLink"] = S("bNodeLink *next", "bNodeLink *prev", "bNode *fromnode", "bNode *tonode", "bNodeSocket *fromsock", "bNodeSocket *tosock")
    d["Image"] = S("ID id", "char name[1024]", "PackedFile *packedfile")
    d["PackedFile"] = S("int dummy")
    d["RenderData"] = S("short frs_sec", "float frs_sec_base", "int cfra", "int sfra", "int efra")
    d["Scene"] = S("ID id", "ListBase base", "Collection *master_collection", "RenderData r")
    d["Base"] = S("Base *next", "Base *prev", "Object *object")
    d["FileGlobal"] = S("Scene *curscene")
    d["Collection"] = S("ID id", "ListBase gobject", "ListBase children")
    d["CollectionObject"] = S("CollectionObject *next", "CollectionObject *prev", "Object *ob")
    d["CollectionChild"] = S("CollectionChild *next", "CollectionChild *prev", "Collection *collection")
    d["bArmature"] = S("ID id", "ListBase bonebase")
    d["Bone"] = S("Bone *next", "Bone *prev", "Bone *parent", "ListBase childbase", "char name[64]", "float head[3]", "float tail[3]",
                  "float arm_head[3]", "float arm_tail[3]", "float arm_mat[4][4]", "float length")
    d["bPose"] = S("ListBase chanbase")
    d["bPoseChannel"] = S("bPoseChannel *next", "bPoseChannel *prev", "char name[64]", "float loc[3]", "float size[3]", "float eul[3]", "float quat[4]",
                          "float rotAxis[3]", "float rotAngle", "short rotmode", "float pose_mat[4][4]")
    d["bAction"] = S("ID id", "ListBase curves")
    d["FCurve"] = S("FCurve *next", "FCurve *prev", "bActionGroup *grp", "ChannelDriver *driver", "ListBase modifiers", "BezTriple *bezt", "FPoint *fpt",
                    "int totvert", "float curval", "short flag", "short extend", "int array_index", "char *rna_path")
    d["bActionGroup"] = S("int dummy")
    d["ChannelDriver"] = S("int dummy")
    d["FPoint"] = S("float vec[2]", "int flag", "int pad")
    d["BezTriple"] = S("float vec[3][3]", "float alfa", "float weight", "float radius", "char ipo", "char h1", "char h2", "char f1", "char f2", "char f3",
                       "char hide", "char easing", "float back", "float amplitude", "float period", "char pad[4]")
    return d


class Dna:
    def __init__(self, naming, ps, le):
        self.ps, self.le = ps, le
        self.e = "<" if le else ">"
        self.defs = dna_defs(naming)
        self.order = list(self.defs.keys())
        self.sizes = dict(PRIM)
        self.layouts = {}
        for name in self.order:           # definition order: dependencies come first except pointers
            self.layout(name)

    def layout(self, name):
        if name in self.layouts:
            return self.layouts[name]
        off, out = 0, []
        for ftype, fname in self.defs[name]:
            ptr = fname.startswith("*") or fname.startswith("(*")
            base = fname.lstrip("*")
            count = 1
            while "[" in base:
                i = base.index("[")
                j = base.index("]")
                count *= int(base[i + 1:j])
                base = base[:i] + base[j + 1:]
            esz = self.ps if ptr else (PRIM[ftype] if ftype in PRIM else self.layout(ftype)[1])
            out.append((ftype, fname, base, ptr, count, esz, off))
            off += esz * count
        self.layouts[name] = (out, off)
        self.sizes[name] = off
        return self.layouts[name]

    def size(self, name):
        return self.layouts[name][1]

    def pack(self, name, vals):
        """vals: field base name -> number / list / bytes / dict (for embedded structs); pointers are ints."""
        fields, total = self.layouts[name]
        buf = bytearray(total)
        for ftype, fname, base, ptr, count, esz, off in fields:
            v = vals.get(base)
            if v is None:
                continue
            if ptr:
                items = v if isinstance(v, list) else [v]
                for k, p in enumerate(items):
                    buf[off + k * esz: off + (k + 1) * esz] = (p & ((1 << (8 * esz)) - 1)).to_bytes(esz, "little" if self.le else "big")
            elif ftype in PRIM:
                if ftype == "char" and isinstance(v, (bytes, str)):
                    b = v.encode("ascii") if isinstance(v, str) else v
                    buf[off: off + min(len(b), esz * count)] = b[: esz * count]
                    continue
                flat = []
                def walk(x):
                    if isinstance(x, (list, tuple)):
                        for y in x:
                            walk(y)
                    else:
                        flat.append(x)
                walk(v)
                code = {"char": "b", "uchar": "B", "short": "h", "ushort": "H", "int": "i", "uint": "I", "float": "f", "double": "d", "int64_t": "q"}[ftype]
                for k, x in enumerate(flat[:count]):
                    struct.pack_into(self.e + code, buf, off + k * esz, x)
            else:
                items = v if isinstance(v, list) else [v]
                for k, it in enumerate(items[:count]):
                    buf[off + k * esz: off + (k + 1) * esz] = self.pack(ftype, it)
        return bytes(buf)

    def sdna_block(self):
        names, types = [], []
        def nm(n):
            if n not in names:
                names.append(n)
            return names.index(n)
        def tp(t):
            if t not in types:
                types.append(t)
            return types.index(t)
        for p in PRIM:
            tp(p)
        strc = []
        for s in self.order:
            st = tp(s)
            fl = []
            for ftype, fname in self.defs[s]:
                fl.append((tp(ftype), nm(fname)))
            strc.append((st, fl))
        e = self.e
        def pad4(b):
            return b + b"\0" * ((-len(b)) % 4)
        out = b"SDNA" + b"NAME" + struct.pack(e + "i", len(names)) + pad4(b"".join(n.encode() + b"\0" for n in names))
        out += b"TYPE" + struct.pack(e + "i", len(types)) + pad4(b"".join(t.encode() + b"\0" for t in types))
        tlen = [self.sizes.get(t, 0) for t in types]
        out += b"TLEN" + pad4(struct.pack(e + "%dh" % len(types), *tlen))
        out += b"STRC" + struct.pack(e + "i", len(strc))
        for st, fl in strc:
            out += struct.pack(e + "hh", st, len(fl)) + b"".join(struct.pack(e + "hh", a, b) for a, b in fl)
        return out

    def sdna_index(self, name):
        return self.order.index(name)


class Blend:
    def __init__(self, naming="41", ps=8, le=True, version=410):
        self.dna = Dna(naming, ps, le)
        self.ps, self.le, self.version = ps, le, version
        self.blocks = []
        self.next_addr = (0x55d000000000 if ps == 8 else 0x08040000)
        self.addrs = {}

    def addr(self, key):
        if key not in self.addrs:
            self.addrs[key] = self.next_addr
            self.next_addr += 0x1000
        return self.addrs[key]

    def add(self, code, struct_name, nr, data, key=None):
        """key: name for the block's address (None = anonymous). Returns the address."""
        a = self.addr(key if key is not None else ("anon", len(self.blocks)))
        self.blocks.append((code, self.dna.sdna_index(struct_name) if struct_name else 0, nr, a, data))
        return a

    def struct_block(self, code, struct_name, vals, key=None):
        return self.add(code, struct_name, 1, self.dna.pack(struct_name, vals), key)

    def array_block(self, struct_name, items, key=None):
        return self.add("DATA", struct_name, len(items), b"".join(self.dna.pack(struct_name, v) for v in items), key)

    def raw_block(self, data, key=None):
        return self.add("DATA", None, 1, data, key)

    def bytes(self):
        e = self.dna.e
        hdr = b"BLENDER" + (b"-" if self.ps == 8 else b"_") + (b"v" if self.le else b"V") + str(self.version).encode()
        out = bytearray(hdr)
        def code4(c):
            b = c.encode().ljust(4, b"\0") if isinstance(c, str) else c
            if not self.le and len(c) == 2:
                b = b"\0\0" + c.encode()          # big-endian two letter ids are stored reversed in a 32 bit int
            return b
        allb = list(self.blocks) + [("DNA1", 0, 1, 0x1000, self.dna.sdna_block())]
        for code, sdna, nr, old, data in allb:
            out += code4(code) + struct.pack(e + "i", len(data)) + old.to_bytes(self.ps, "little" if self.le else "big") + struct.pack(e + "ii", sdna, nr)
            out += data
        out += code4("ENDB") + struct.pack(e + "i", 0) + (0).to_bytes(self.ps, "little" if self.le else "big") + struct.pack(e + "ii", 0, 0)
        return bytes(out)


# ---------------------------------------------------------------------------------------------
# small matrix helpers (memory order: m[col*4 + row], translation in 12..14, like Blender)
# ---------------------------------------------------------------------------------------------
def mident():
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def mmul(a, b):
    r = [0.0] * 16
    for c in range(4):
        for row in range(4):
            r[c * 4 + row] = sum(a[k * 4 + row] * b[c * 4 + k] for k in range(4))
    return r


def mtrans(x, y, z):
    m = mident(); m[12], m[13], m[14] = x, y, z; return m


def mscale(x, y, z):
    m = mident(); m[0], m[5], m[10] = x, y, z; return m


def mrot(axis, a):
    c, s = math.cos(a), math.sin(a)
    m = mident()
    if axis == 0: m[5], m[6], m[9], m[10] = c, s, -s, c
    elif axis == 1: m[0], m[2], m[8], m[10] = c, -s, s, c
    else: m[0], m[1], m[4], m[5] = c, s, -s, c
    return m


def meuler_xyz(x, y, z):
    return mmul(mrot(2, z), mmul(mrot(1, y), mrot(0, x)))


def minv_affine(m):
    # inverse of a rigid-plus-scale matrix is not needed generally; only translations / rotations are inverted here
    r = [m[0], m[4], m[8], 0, m[1], m[5], m[9], 0, m[2], m[6], m[10], 0, 0, 0, 0, 1]
    t = [m[12], m[13], m[14]]
    r[12] = -(r[0] * t[0] + r[4] * t[1] + r[8] * t[2]); r[13] = -(r[1] * t[0] + r[5] * t[1] + r[9] * t[2]); r[14] = -(r[2] * t[0] + r[6] * t[1] + r[10] * t[2])
    return r


def ob_vals(name, typ, data, loc=(0, 0, 0), rot=(0, 0, 0), size=(1, 1, 1), parent=0, parentinv=None, partype=0, obmat=None, extra=None):
    v = {"id": {"name": "OB" + name}, "type": typ, "data": data, "loc": list(loc), "rot": list(rot), "size": list(size), "dscale": [1, 1, 1],
         "quat": [1, 0, 0, 0], "rotAxis": [0, 1, 0], "rotmode": 1, "parent": parent, "partype": partype,
         "parentinv": parentinv or mident(), "obmat": obmat or mident()}
    if extra:
        v.update(extra)
    return v


def id_vals(code, name):
    return {"id": {"name": code + name, "us": 1}}


def layer(typ, name, data, active_rnd=0):
    return {"type": typ, "name": name, "data": data, "active_rnd": active_rnd}


def customdata_vals(layers_addr, n):
    return {"layers": layers_addr, "totlayer": n, "maxlayer": n}


# ---------------------------------------------------------------------------------------------
# Fixture 1: attribute based mesh storage (Blender 3.6 / 4.x), node material, collection, parenting
# ---------------------------------------------------------------------------------------------
POS = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (2, 0, 0),                           # quad (0..3) and a triangle (1, 4, 2)
       (0, 0, 1), (2, 0, 1), (2, 1, 1), (1, 1, 1), (1, 2, 1), (0, 2, 1)]              # concave L shaped hexagon at z = 1
FACES = [[0, 1, 2, 3], [1, 4, 2], [5, 6, 7, 8, 9, 10]]
FACE_MAT = [0, 1, 0]
FACE_SMOOTH = [True, False, False]


def build_attr(naming="41", ps=8, le=True):
    b = Blend(naming, ps, le, 410 if naming == "41" else 306)
    d = b.dna
    # mesh arrays
    nv, nf = len(POS), len(FACES)
    corners = [v for f in FACES for v in f]
    offs = [0]
    for f in FACES:
        offs.append(offs[-1] + len(f))
    pos_a = b.raw_block(struct.pack(d.e + "%df" % (3 * nv), *[c for p in POS for c in p]))
    cv_a = b.raw_block(struct.pack(d.e + "%di" % len(corners), *corners))
    uv = []
    for v in corners:
        uv += [POS[v][0] / 2.0, POS[v][1] / 2.0]
    uv_a = b.raw_block(struct.pack(d.e + "%df" % len(uv), *uv))
    mi_a = b.raw_block(struct.pack(d.e + "%di" % nf, *FACE_MAT))
    sf_a = b.raw_block(bytes(0 if s else 1 for s in FACE_SMOOTH))
    off_a = b.raw_block(struct.pack(d.e + "%di" % len(offs), *offs))
    # a vertex colour layer (named, 4 floats per corner): must be reported, not imported
    col_a = b.raw_block(struct.pack(d.e + "%df" % (4 * len(corners)), *([0.5, 0.25, 0.125, 1.0] * len(corners))))
    vl = b.array_block("CustomDataLayer", [layer(48, "position", pos_a)])
    fl = b.array_block("CustomDataLayer", [layer(11, "material_index", mi_a), layer(50, "sharp_face", sf_a)])
    ll = b.array_block("CustomDataLayer", [layer(45, ".corner_vert", cv_a), layer(49, "UVMap", uv_a), layer(47, "Col", col_a)])
    mat_a = b.raw_block((b.addr("ma0")).to_bytes(ps, "little" if le else "big") + (b.addr("ma1")).to_bytes(ps, "little" if le else "big"))
    if naming == "41":
        mv = {"verts_num": nv, "faces_num": nf, "corners_num": len(corners), "vert_data": customdata_vals(vl, 1), "face_data": customdata_vals(fl, 2),
              "corner_data": customdata_vals(ll, 3), "face_offset_indices": off_a}
    else:
        mv = {"totvert": nv, "totpoly": nf, "totloop": len(corners), "vdata": customdata_vals(vl, 1), "pdata": customdata_vals(fl, 2),
              "ldata": customdata_vals(ll, 3), "poly_offset_indices": off_a}
    mv.update({"id": {"name": "MEFaceMesh"}, "mat": mat_a, "totcol": 2})
    b.struct_block("ME", "Mesh", mv, "me")
    # materials
    img_a = b.struct_block("IM", "Image", {"id": {"name": "IMdiffuse.png"}, "name": "//tex/diffuse.png"})
    sock_base_v = b.struct_block("DATA", "bNodeSocketValueRGBA", {"value": [0.1, 0.2, 0.3, 1.0]})
    sock_metal_v = b.struct_block("DATA", "bNodeSocketValueFloat", {"value": 0.5})
    sock_rough_v = b.struct_block("DATA", "bNodeSocketValueFloat", {"value": 0.25})
    sock_base = b.struct_block("DATA", "bNodeSocket", {"name": "Base Color", "default_value": sock_base_v, "next": b.addr("sm")}, "sb")
    sock_metal = b.struct_block("DATA", "bNodeSocket", {"name": "Metallic", "default_value": sock_metal_v, "prev": sock_base, "next": b.addr("sr")}, "sm")
    sock_rough = b.struct_block("DATA", "bNodeSocket", {"name": "Roughness", "default_value": sock_rough_v, "prev": sock_metal}, "sr")
    sock_out = b.struct_block("DATA", "bNodeSocket", {"name": "Color"}, "so")
    node_p = b.struct_block("DATA", "bNode", {"idname": "ShaderNodeBsdfPrincipled", "name": "Principled BSDF", "inputs": {"first": sock_base, "last": sock_rough},
                                              "next": b.addr("nt")}, "np")
    node_t = b.struct_block("DATA", "bNode", {"idname": "ShaderNodeTexImage", "name": "Image Texture", "outputs": {"first": sock_out, "last": sock_out},
                                              "id": img_a, "prev": node_p}, "nt")
    link = b.struct_block("DATA", "bNodeLink", {"fromnode": node_t, "tonode": node_p, "fromsock": sock_out, "tosock": sock_base})
    tree = b.struct_block("DATA", "bNodeTree", {"id": {"name": "NTShader Nodetree"}, "nodes": {"first": node_p, "last": node_t}, "links": {"first": link, "last": link}})
    b.struct_block("MA", "Material", {"id": {"name": "MAMat0"}, "r": 0.8, "g": 0.8, "b": 0.8, "a": 1.0, "use_nodes": 1, "nodetree": tree}, "ma0")
    b.struct_block("MA", "Material", {"id": {"name": "MAMat1"}, "r": 1.0, "g": 0.0, "b": 0.0, "a": 1.0, "metallic": 0.0, "roughness": 0.5}, "ma1")
    # objects: Parent (rotated 90 degrees about Z, scaled 2 in x), Child (parented, with a parent inverse)
    parent_m = mmul(mtrans(1, 2, 3), mmul(meuler_xyz(0, 0, math.pi / 2), mscale(2, 1, 1)))
    pinv = mtrans(-1, -2, -3)
    child_basis = mtrans(0, 0, 1)
    child_m = mmul(parent_m, mmul(pinv, child_basis))
    b.struct_block("OB", "Object", ob_vals("Parent", 1, b.addr("me"), loc=(1, 2, 3), rot=(0, 0, math.pi / 2), size=(2, 1, 1), obmat=parent_m,
                                           extra={"mat": 0, "totcol": 0}), "obP")
    b.struct_block("OB", "Object", ob_vals("Child", 0, 0, loc=(0, 0, 1), parent=b.addr("obP"), parentinv=pinv, obmat=child_m), "obC")
    # a hidden-from-the-scene object: must not be imported
    b.struct_block("OB", "Object", ob_vals("Orphan", 0, 0, loc=(9, 9, 9), obmat=mtrans(9, 9, 9)), "obO")
    # collections: the master collection holds Parent; a child collection holds Child
    co1 = b.struct_block("DATA", "CollectionObject", {"ob": b.addr("obP")})
    co2 = b.struct_block("DATA", "CollectionObject", {"ob": b.addr("obC")})
    sub = b.struct_block("GR", "Collection", {"id": {"name": "GRSub"}, "gobject": {"first": co2, "last": co2}}, "sub")
    ch = b.struct_block("DATA", "CollectionChild", {"collection": sub})
    master = b.struct_block("DATA", "Collection", {"id": {"name": "GRMaster"}, "gobject": {"first": co1, "last": co1}, "children": {"first": ch, "last": ch}}, "master")
    b.struct_block("SC", "Scene", {"id": {"name": "SCScene"}, "master_collection": master, "r": {"frs_sec": 24, "frs_sec_base": 1.0, "cfra": 1, "sfra": 1, "efra": 250}}, "sc")
    b.struct_block("GLOB", "FileGlobal", {"curscene": b.addr("sc")})
    return b


# ---------------------------------------------------------------------------------------------
# Fixture 2: legacy storage (2.7x style pointers), armature, vertex groups, action with bezier and linear curves
# ---------------------------------------------------------------------------------------------
STRIP_Z = [0.0, 1.0, 2.0]


def bone_mat(head):          # a bone along +Z: its Y axis points up, so X stays, Y -> Z, Z -> -Y
    return [1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, head[0], head[1], head[2], 1]


def bez(x, y, hl, hr, ipo):
    return {"vec": [[hl[0], hl[1], 0], [x, y, 0], [hr[0], hr[1], 0]], "ipo": ipo}


def build_skin(ps=8, le=True):
    b = Blend("legacy", ps, le, 279)
    d = b.dna
    verts = []
    for z in STRIP_Z:
        verts += [(-0.5, 0.0, z), (0.5, 0.0, z)]
    nv = len(verts)
    mv_a = b.array_block("MVert", [{"co": list(v)} for v in verts])
    loops = []
    polys = []
    for q in range(2):
        a, bb, c, dd = 2 * q, 2 * q + 1, 2 * q + 3, 2 * q + 2
        polys.append({"loopstart": len(loops), "totloop": 4, "mat_nr": 0, "flag": 1})
        loops += [a, bb, c, dd]
    ml_a = b.array_block("MLoop", [{"v": v, "e": 0} for v in loops])
    mp_a = b.array_block("MPoly", polys)
    uvs = [{"uv": [(verts[v][0] + 0.5), verts[v][2] / 2.0]} for v in loops]
    uv_a = b.array_block("MLoopUV", uvs)
    # weights: z=0 Root 1; z=1 Root .5 Child .5; z=2 Child 1   (group 0 = Root, group 1 = Child)
    wts = [[(0, 1.0)], [(0, 1.0)], [(0, 0.5), (1, 0.5)], [(0, 0.5), (1, 0.5)], [(1, 1.0)], [(1, 1.0)]]
    dv = []
    for w in wts:
        dw = b.array_block("MDeformWeight", [{"def_nr": g, "weight": x} for g, x in w])
        dv.append({"dw": dw, "totweight": len(w)})
    dv_a = b.array_block("MDeformVert", dv)
    mat_a = b.raw_block(b.addr("ma").to_bytes(ps, "little" if le else "big"))
    b.struct_block("ME", "Mesh", {"id": {"name": "MEStrip"}, "mat": mat_a, "mpoly": mp_a, "mloop": ml_a, "mloopuv": uv_a, "mvert": mv_a, "dvert": dv_a,
                                  "totvert": nv, "totpoly": 2, "totloop": 8, "totcol": 1}, "me")
    b.struct_block("MA", "Material", {"id": {"name": "MAStripMat"}, "r": 0.2, "g": 0.4, "b": 0.6, "a": 1.0}, "ma")
    # armature: Root (0,0,0)-(0,0,1), Child (0,0,1)-(0,0,2)
    child = b.struct_block("DATA", "Bone", {"parent": b.addr("bRoot"), "name": "Child", "head": [0, 0, 0], "tail": [0, 0, 1], "arm_head": [0, 0, 1], "arm_tail": [0, 0, 2],
                                            "arm_mat": bone_mat((0, 0, 1)), "length": 1.0}, "bChild")
    root = b.struct_block("DATA", "Bone", {"name": "Root", "head": [0, 0, 0], "tail": [0, 0, 1], "arm_head": [0, 0, 0], "arm_tail": [0, 0, 1],
                                           "arm_mat": bone_mat((0, 0, 0)), "childbase": {"first": child, "last": child}, "length": 1.0}, "bRoot")
    b.struct_block("AR", "bArmature", {"id": {"name": "ARArmature"}, "bonebase": {"first": root, "last": root}}, "ar")
    pc_child = b.struct_block("DATA", "bPoseChannel", {"name": "Child", "size": [1, 1, 1], "quat": [1, 0, 0, 0], "rotmode": 0, "pose_mat": bone_mat((0, 0, 1)),
                                                      "prev": b.addr("pcRoot")}, "pcChild")
    pc_root = b.struct_block("DATA", "bPoseChannel", {"name": "Root", "size": [1, 1, 1], "quat": [1, 0, 0, 0], "rotmode": 0, "pose_mat": bone_mat((0, 0, 0)),
                                                      "next": pc_child}, "pcRoot")
    pose = b.struct_block("DATA", "bPose", {"chanbase": {"first": pc_root, "last": pc_child}}, "pose")
    # action "Walk"
    def rna(path, key):
        return b.raw_block(path.encode() + b"\0", key)
    def fcurve(path, idx, keys, key, nxt=0, prv=0):
        bz = b.array_block("BezTriple", keys)
        return b.struct_block("DATA", "FCurve", {"rna_path": rna(path, key + "path"), "array_index": idx, "totvert": len(keys), "bezt": bz, "next": nxt, "prev": prv}, key)
    s = 10.0 / 3.0
    # Armature object location z: bezier, flat y handles: y = 10 (3t^2 - 2t^3), x = 1 + 10 t
    f_loc = fcurve("location", 2, [bez(1, 0, (1 - s, 0), (1 + s, 0), 2), bez(11, 10, (11 - s, 10), (11 + s, 10), 2)], "fcLoc", b.addr("fcQ0"))
    # Child bone quaternion: 90 degrees about X (local) between frames 1 and 11, linear
    r2 = math.sqrt(0.5)
    f_q0 = fcurve("pose.bones[\"Child\"].rotation_quaternion", 0, [bez(1, 1.0, (0, 1.0), (2, 1.0), 1), bez(11, r2, (10, r2), (12, r2), 1)], "fcQ0", b.addr("fcQ1"), f_loc)
    f_q1 = fcurve("pose.bones[\"Child\"].rotation_quaternion", 1, [bez(1, 0.0, (0, 0.0), (2, 0.0), 1), bez(11, r2, (10, r2), (12, r2), 1)], "fcQ1", b.addr("fcLens"), f_q0)
    # an F-Curve on a property we do not read: must be named in the notes
    f_other = fcurve("data.lens", 0, [bez(1, 50, (0, 50), (2, 50), 1), bez(2, 60, (1, 60), (3, 60), 1)], "fcLens", 0, f_q1)
    b.struct_block("AC", "bAction", {"id": {"name": "ACWalk"}, "curves": {"first": f_loc, "last": f_other}}, "ac")
    adt = b.struct_block("DATA", "AnimData", {"action": b.addr("ac")}, "adt")
    # objects
    arm_m = mident()
    b.struct_block("OB", "Object", ob_vals("Rig", 25, b.addr("ar"), obmat=arm_m, extra={"pose": pose, "adt": adt}), "obA")
    g1 = b.struct_block("DATA", "bDeformGroup", {"name": "Root", "next": b.addr("g2")}, "g1")
    g2 = b.struct_block("DATA", "bDeformGroup", {"name": "Child", "prev": g1}, "g2")
    b.struct_block("OB", "Object", ob_vals("Strip", 1, b.addr("me"), parent=b.addr("obA"), partype=4, obmat=mident(),
                                           extra={"defbase": {"first": g1, "last": g2}}), "obM")
    base2 = b.struct_block("DATA", "Base", {"object": b.addr("obM"), "next": 0, "prev": b.addr("base1")}, "base2")
    base1 = b.struct_block("DATA", "Base", {"object": b.addr("obA"), "next": base2}, "base1")
    b.struct_block("SC", "Scene", {"id": {"name": "SCScene"}, "base": {"first": base1, "last": base2}, "r": {"frs_sec": 24, "frs_sec_base": 1.0, "cfra": 1, "sfra": 1, "efra": 250}}, "sc")
    b.struct_block("GLOB", "FileGlobal", {"curscene": b.addr("sc")})
    return b


def main():
    os.makedirs(OUT, exist_ok=True)
    files = {
        "blend_attr_41.blend": build_attr("41", 8, True).bytes(),
        "blend_attr_36.blend": build_attr("36", 8, True).bytes(),
        "blend_attr_be32.blend": build_attr("36", 4, False).bytes(),
        "blend_skin_anim.blend": build_skin(8, True).bytes(),
    }
    files["blend_attr_41_gz.blend"] = gzip.compress(files["blend_attr_41.blend"], 9, mtime=0)
    for name, data in files.items():
        with open(os.path.join(OUT, name), "wb") as fh:
            fh.write(data)
        print("wrote", name, len(data), "bytes")


if __name__ == "__main__":
    main()
