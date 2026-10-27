#!/usr/bin/env python3
"""Generate the small skinned test rigs in tests/assets/rigs/.

Deterministic, pure Python, no network. Every rig is one 25-joint humanoid
topology (hips, 3 spine links, neck, head, shoulders, arms, legs, toes and end
bones) under a different naming convention, rest pose, joint-frame convention,
proportion and unit scale, skinned with one box per bone. fox.glb is a
quadruped with a humanoid-looking bone set, for the "not a humanoid" check.

    python3 tools/make_test_rigs.py [out_dir]
"""
import json
import math
import os
import struct
import sys

# ---- quaternion / vector math (x, y, z, w) ---------------------------------

def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)

def qconj(q):
    return (-q[0], -q[1], -q[2], q[3])

def qaxis(axis, deg):
    h = math.radians(deg) * 0.5
    s = math.sin(h)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h))

def qrot(q, v):
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), qconj(q))
    return (p[0], p[1], p[2])

def qfrom_basis(x, y, z):
    """Quaternion of the rotation whose columns are the axes x, y, z."""
    m00, m01, m02 = x[0], y[0], z[0]
    m10, m11, m12 = x[1], y[1], z[1]
    m20, m21, m22 = x[2], y[2], z[2]
    tr = m00 + m11 + m22
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        q = ((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s)
    elif m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2
        q = (0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s)
    elif m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2
        q = ((m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s)
    else:
        s = math.sqrt(1.0 + m22 - m00 - m11) * 2
        q = ((m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s)
    return qnorm(q)

def qnorm(q):
    n = math.sqrt(sum(c * c for c in q))
    return tuple(c / n for c in q)

def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])

def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])

def mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)

def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]

def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])

def norm(v):
    n = math.sqrt(dot(v, v))
    return mul(v, 1.0 / n)

def r6(v):
    """Round so the files don't depend on the last bits of the float math."""
    return [round(c, 6) + 0.0 for c in v]

# ---- the humanoid -----------------------------------------------------------
# (key, parent key, primary child key, rest position in metres for a 1.8 m
# T-pose facing +Z — the character's left is +X).
HUMAN = [
    ("Hips",          None,           "Spine",        (0.0, 1.00, 0.0)),
    ("Spine",         "Hips",         "Spine1",       (0.0, 1.10, 0.0)),
    ("Spine1",        "Spine",        "Spine2",       (0.0, 1.25, 0.0)),
    ("Spine2",        "Spine1",       "Neck",         (0.0, 1.40, 0.0)),
    ("Neck",          "Spine2",       "Head",         (0.0, 1.55, 0.0)),
    ("Head",          "Neck",         "HeadTop_End",  (0.0, 1.65, 0.0)),
    ("HeadTop_End",   "Head",         None,           (0.0, 1.80, 0.0)),
    ("LeftShoulder",  "Spine2",       "LeftArm",      (0.05, 1.48, 0.0)),
    ("LeftArm",       "LeftShoulder", "LeftForeArm",  (0.18, 1.48, 0.0)),
    ("LeftForeArm",   "LeftArm",      "LeftHand",     (0.46, 1.48, 0.0)),
    ("LeftHand",      "LeftForeArm",  None,           (0.72, 1.48, 0.0)),
    ("RightShoulder", "Spine2",       "RightArm",     (-0.05, 1.48, 0.0)),
    ("RightArm",      "RightShoulder", "RightForeArm", (-0.18, 1.48, 0.0)),
    ("RightForeArm",  "RightArm",     "RightHand",    (-0.46, 1.48, 0.0)),
    ("RightHand",     "RightForeArm", None,           (-0.72, 1.48, 0.0)),
    ("LeftUpLeg",     "Hips",         "LeftLeg",      (0.10, 0.95, 0.0)),
    ("LeftLeg",       "LeftUpLeg",    "LeftFoot",     (0.10, 0.52, 0.0)),
    ("LeftFoot",      "LeftLeg",      "LeftToeBase",  (0.10, 0.08, 0.0)),
    ("LeftToeBase",   "LeftFoot",     "LeftToe_End",  (0.10, 0.02, 0.12)),
    ("LeftToe_End",   "LeftToeBase",  None,           (0.10, 0.02, 0.20)),
    ("RightUpLeg",    "Hips",         "RightLeg",     (-0.10, 0.95, 0.0)),
    ("RightLeg",      "RightUpLeg",   "RightFoot",    (-0.10, 0.52, 0.0)),
    ("RightFoot",     "RightLeg",     "RightToeBase", (-0.10, 0.08, 0.0)),
    ("RightToeBase",  "RightFoot",    "RightToe_End", (-0.10, 0.02, 0.12)),
    ("RightToe_End",  "RightToeBase", None,           (-0.10, 0.02, 0.20)),
]
KEYS = [j[0] for j in HUMAN]
ARM_KEYS = {"LeftArm", "LeftForeArm", "LeftHand", "RightArm", "RightForeArm", "RightHand"}
LEG_KEYS = {"LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase", "LeftToe_End",
            "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase", "RightToe_End"}

MIXAMO = {k: "mixamorig:" + k for k in KEYS}
UE = dict(zip(KEYS, [
    "pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "head", "head_end",
    "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l",
    "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r",
    "thigh_l", "calf_l", "foot_l", "ball_l", "ball_l_end",
    "thigh_r", "calf_r", "foot_r", "ball_r", "ball_r_end"]))
VROID = dict(zip(KEYS, [
    "J_Bip_C_Hips", "J_Bip_C_Spine", "J_Bip_C_Chest", "J_Bip_C_UpperChest", "J_Bip_C_Neck",
    "J_Bip_C_Head", "J_Bip_C_HeadTop_End",
    "J_Bip_L_Shoulder", "J_Bip_L_UpperArm", "J_Bip_L_LowerArm", "J_Bip_L_Hand",
    "J_Bip_R_Shoulder", "J_Bip_R_UpperArm", "J_Bip_R_LowerArm", "J_Bip_R_Hand",
    "J_Bip_L_UpperLeg", "J_Bip_L_LowerLeg", "J_Bip_L_Foot", "J_Bip_L_ToeBase", "J_Bip_L_ToeBase_End",
    "J_Bip_R_UpperLeg", "J_Bip_R_LowerLeg", "J_Bip_R_Foot", "J_Bip_R_ToeBase", "J_Bip_R_ToeBase_End"]))
CC = dict(zip(KEYS, [
    "CC_Base_Hip", "CC_Base_Waist", "CC_Base_Spine01", "CC_Base_Spine02", "CC_Base_NeckTwist01",
    "CC_Base_Head", "CC_Base_HeadTop_End",
    "CC_Base_L_Clavicle", "CC_Base_L_Upperarm", "CC_Base_L_Forearm", "CC_Base_L_Hand",
    "CC_Base_R_Clavicle", "CC_Base_R_Upperarm", "CC_Base_R_Forearm", "CC_Base_R_Hand",
    "CC_Base_L_Thigh", "CC_Base_L_Calf", "CC_Base_L_Foot", "CC_Base_L_ToeBase", "CC_Base_L_ToeBase_End",
    "CC_Base_R_Thigh", "CC_Base_R_Calf", "CC_Base_R_Foot", "CC_Base_R_ToeBase", "CC_Base_R_ToeBase_End"]))
DAZ = dict(zip(KEYS, [
    "hip", "abdomenLower", "chestLower", "chestUpper", "neckLower", "head", "headEnd",
    "lCollar", "lShldrBend", "lForearmBend", "lHand",
    "rCollar", "rShldrBend", "rForearmBend", "rHand",
    "lThighBend", "lShin", "lFoot", "lToe", "lToeEnd",
    "rThighBend", "rShin", "rFoot", "rToe", "rToeEnd"]))
NONSENSE = {k: "bone_%03d" % i for i, k in enumerate(KEYS)}


def human_positions(pose="T", legs=1.0, arms=1.0, scale=1.0):
    """Rest joint positions (metres) for a pose and limb proportions; feet on y=0."""
    info = {j[0]: j for j in HUMAN}
    pos = {}
    for key, parent, _child, p in HUMAN:
        if parent is None:
            pos[key] = p
            continue
        off = sub(p, info[parent][3])
        if key in LEG_KEYS and key not in ("LeftUpLeg", "RightUpLeg"):
            off = mul(off, legs)
        if key in ARM_KEYS and key not in ("LeftArm", "RightArm"):
            off = mul(off, arms)
        pos[key] = add(pos[parent], off)
    # Longer legs lift the body: put the hips back over the feet.
    lift = (info["LeftFoot"][3][1] - pos["LeftFoot"][1])
    pos = {k: add(v, (0.0, lift, 0.0)) for k, v in pos.items()}
    if pose == "A":   # arms 45 degrees down, about each shoulder
        for side, sign in (("Left", 1.0), ("Right", -1.0)):
            pivot = pos[side + "Arm"]
            q = qaxis((0.0, 0.0, 1.0), -45.0 * sign)
            for k in (side + "ForeArm", side + "Hand"):
                pos[k] = add(pivot, qrot(q, sub(pos[k], pivot)))
    return {k: mul(v, scale) for k, v in pos.items()}


def frames(skel, pos, style):
    """World rest rotation per joint for a joint-frame convention.
    identity: every joint axis-aligned with the model (Mixamo-style T rigs).
    y_bone:  local +Y runs down the bone to its child (Blender export), roll
             picked from `ref` so different exporters get different rolls.
    y_bone_ue: as y_bone with the opposite roll.
    x_bone:  local +X runs down the bone (CC-style)."""
    out = {}
    if style == "identity":
        return {k: (0.0, 0.0, 0.0, 1.0) for k, _p, _c in skel}
    for key, parent, child in skel:
        if child is not None:
            d = norm(sub(pos[child], pos[key]))
        else:   # leaves continue their parent's direction
            d = norm(sub(pos[key], pos[parent]))
        ref = (0.0, 0.0, 1.0) if style in ("y_bone", "x_bone") else (0.0, 0.0, -1.0)
        if abs(dot(d, ref)) > 0.9:
            ref = (0.0, 1.0, 0.0)
        a = norm(cross(d, ref))
        if style == "x_bone":     # x = bone, y = a, z = x cross y
            out[key] = qfrom_basis(d, a, cross(d, a))
        else:                     # y = bone, x = a, z = a cross d
            out[key] = qfrom_basis(a, d, cross(a, d))
    return out


# ---- clips (authored for the identity-frame Mixamo T rig) -------------------

def clip_pose(name, t):
    """Local rotations (and the hips' offset in metres) of one clip at time t."""
    X, Z = (1.0, 0.0, 0.0), (0.0, 0.0, 1.0)
    rot, hips = {}, (0.0, 0.0, 0.0)

    def arms_down(swing_l, swing_r, bend):
        rot["LeftArm"] = qmul(qaxis(Z, -75.0), qaxis(X, swing_l))
        rot["RightArm"] = qmul(qaxis(Z, 75.0), qaxis(X, swing_r))
        rot["LeftForeArm"] = qaxis((0.0, 1.0, 0.0), -bend)
        rot["RightForeArm"] = qaxis((0.0, 1.0, 0.0), bend)

    if name == "Idle":                     # 2 s breathing loop
        w = math.sin(2 * math.pi * t / 2.0)
        arms_down(2.0 * w, -2.0 * w, 10.0)
        rot["Spine1"] = qaxis(X, 1.5 * w)
        hips = (0.0, -0.005 + 0.005 * w, 0.0)
    elif name in ("Walk", "Run"):
        period, amp, knee, bob = (1.0, 30.0, 35.0, 0.02) if name == "Walk" else (0.7, 45.0, 70.0, 0.04)
        ph = 2 * math.pi * t / period
        s = math.sin(ph)
        rot["LeftUpLeg"] = qaxis(X, -amp * s)
        rot["RightUpLeg"] = qaxis(X, amp * s)
        rot["LeftLeg"] = qaxis(X, 5.0 + knee * max(0.0, math.sin(ph + 1.2)))
        rot["RightLeg"] = qaxis(X, 5.0 + knee * max(0.0, math.sin(ph + 1.2 + math.pi)))
        rot["LeftFoot"] = qaxis(X, -8.0 * s)
        rot["RightFoot"] = qaxis(X, 8.0 * s)
        arms_down(amp * 0.6 * s, -amp * 0.6 * s, 20.0 if name == "Walk" else 70.0)
        rot["Spine"] = qaxis(X, 3.0 if name == "Walk" else 10.0)
        hips = (0.0, -bob * 0.5 + bob * 0.5 * math.cos(2 * ph), 0.0)
    elif name == "Jump":                   # 1 s: crouch, push off, tuck, land
        u = t / 1.0
        crouch = math.sin(math.pi * min(1.0, u / 0.25)) if u < 0.25 else 0.0
        tuck = math.sin(math.pi * (u - 0.3) / 0.5) if 0.3 <= u <= 0.8 else 0.0
        bend = 40.0 * crouch + 50.0 * tuck
        rot["LeftUpLeg"] = qaxis(X, -bend)
        rot["RightUpLeg"] = qaxis(X, -bend)
        rot["LeftLeg"] = qaxis(X, 2.0 * bend)
        rot["RightLeg"] = qaxis(X, 2.0 * bend)
        rot["LeftFoot"] = qaxis(X, -bend * 0.5)
        rot["RightFoot"] = qaxis(X, -bend * 0.5)
        arms_down(-60.0 * tuck, -60.0 * tuck, 30.0)
        hips = (0.0, -0.25 * crouch, 0.0)
    elif name in ("Crouch Idle", "Crouched Walking", "Standing To Crouched"):
        k = min(1.0, t / 0.5) if name == "Standing To Crouched" else 1.0
        s = math.sin(2 * math.pi * t / 1.2) if name == "Crouched Walking" else 0.0
        rot["LeftUpLeg"] = qaxis(X, -70.0 * k - 20.0 * s)
        rot["RightUpLeg"] = qaxis(X, -70.0 * k + 20.0 * s)
        rot["LeftLeg"] = rot["RightLeg"] = qaxis(X, 120.0 * k)
        rot["LeftFoot"] = rot["RightFoot"] = qaxis(X, -50.0 * k)
        arms_down(10.0 * s, -10.0 * s, 30.0)
        hips = (0.0, -0.35 * k, 0.0)
    elif name in ("Prone Idle", "Crawling", "Getting Up"):
        k = 1.0 - min(1.0, t / 1.0) if name == "Getting Up" else 1.0
        s = math.sin(2 * math.pi * t / 1.5) if name == "Crawling" else 0.0
        arms_down(-150.0 * k + 25.0 * s, -150.0 * k - 25.0 * s, 20.0)
        rot["LeftUpLeg"] = qaxis(X, 15.0 * s)
        rot["RightUpLeg"] = qaxis(X, -15.0 * s)
        hips = (0.0, -0.75 * k, 0.0)
    return rot, hips

CLIPS = [("Idle", 2.0, 17), ("Walk", 1.0, 17), ("Run", 0.7, 15), ("Jump", 1.0, 17)]
# Crouch / prone clips under the names a Mixamo download gives them (plus a
# stance change that maps to no state).
STANCE_CLIPS = CLIPS[:3] + [("Crouch Idle", 2.0, 9), ("Crouched Walking", 1.2, 13), ("Prone Idle", 2.0, 9),
                            ("Crawling", 1.5, 13), ("Getting Up", 1.0, 11), ("Standing To Crouched", 0.5, 7)]
CLIP_BONES = ["Hips", "Spine", "Spine1", "LeftArm", "LeftForeArm", "RightArm", "RightForeArm",
              "LeftUpLeg", "LeftLeg", "LeftFoot", "RightUpLeg", "RightLeg", "RightFoot"]


# ---- glTF writer ------------------------------------------------------------

class Gltf:
    def __init__(self):
        self.bin = bytearray()
        self.views, self.accessors = [], []

    def add(self, fmt, rows, gltf_type, comp, minmax=False, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        off = len(self.bin)
        for r in rows:
            self.bin += struct.pack("<" + fmt * len(r), *r)
        view = {"buffer": 0, "byteOffset": off, "byteLength": len(self.bin) - off}
        if target:
            view["target"] = target
        self.views.append(view)
        acc = {"bufferView": len(self.views) - 1, "componentType": comp,
               "count": len(rows), "type": gltf_type}
        if minmax:
            acc["min"] = [min(r[i] for r in rows) for i in range(len(rows[0]))]
            acc["max"] = [max(r[i] for r in rows) for i in range(len(rows[0]))]
        self.accessors.append(acc)
        return len(self.accessors) - 1

    def write(self, path, doc):
        while len(self.bin) % 4:
            self.bin.append(0)
        doc["asset"] = {"version": "2.0", "generator": "okay make_test_rigs.py"}
        doc["buffers"] = [{"byteLength": len(self.bin)}]
        doc["bufferViews"] = self.views
        doc["accessors"] = self.accessors
        js = json.dumps(doc, separators=(",", ":"), sort_keys=True).encode()
        while len(js) % 4:
            js += b" "
        body = struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(self.bin), 0x004E4942) + bytes(self.bin)
        with open(path, "wb") as f:
            f.write(struct.pack("<III", 0x46546C67, 2, 12 + len(body)) + body)


def box_mesh(g, skel, pos, widths):
    """One box per bone (joint -> its primary child, or a stub for leaves),
    rigidly skinned to that bone. Vertices are in model (world) metres."""
    verts, joints, weights, idx = [], [], [], []
    ji = {k: i for i, (k, _p, _c) in enumerate(skel)}
    for key, parent, child in skel:
        w = widths(key)
        if w <= 0.0:
            continue
        a = pos[key]
        b = pos[child] if child else add(a, mul(norm(sub(a, pos[parent])), 0.08))
        d = sub(b, a)
        ref = (0.0, 0.0, 1.0) if abs(norm(d)[2]) < 0.9 else (0.0, 1.0, 0.0)
        u = mul(norm(cross(d, ref)), w)
        v = mul(norm(cross(d, u)), w)
        base = len(verts)
        for end in (a, b):
            for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                verts.append(r6(add(end, add(mul(u, su), mul(v, sv)))))
                joints.append((ji[key], 0, 0, 0))
                weights.append((1.0, 0.0, 0.0, 0.0))
        for q in ((0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
            idx += [base + q[0], base + q[1], base + q[2], base + q[0], base + q[2], base + q[3]]
    pa = g.add("f", verts, "VEC3", 5126, minmax=True, target=34962)
    ja = g.add("H", joints, "VEC4", 5123, target=34962)
    wa = g.add("f", weights, "VEC4", 5126, target=34962)
    ia = g.add("H", [(i,) for i in idx], "SCALAR", 5123, target=34963)
    return {"primitives": [{"attributes": {"POSITION": pa, "JOINTS_0": ja, "WEIGHTS_0": wa}, "indices": ia}]}


def write_rig(path, skel, names, pos, style, unit=1.0, clips=False, widths=None):
    """skel: [(key, parent, child)], pos: rest positions in metres.
    unit: the armature's scale (0.01 = joints stored in centimetres)."""
    g = Gltf()
    rot = frames(skel, pos, style)
    nodes = [{"name": "Armature", "children": [], "scale": [unit, unit, unit]} if unit != 1.0
             else {"name": "Armature", "children": []}]
    idx = {}
    for key, parent, _child in skel:
        idx[key] = len(nodes)
        if parent is None:
            lp, lq = mul(pos[key], 1.0 / unit), rot[key]
            nodes[0]["children"].append(idx[key])
        else:
            pinv = qconj(rot[parent])
            lp = mul(qrot(pinv, sub(pos[key], pos[parent])), 1.0 / unit)
            lq = qnorm(qmul(pinv, rot[key]))
            nodes[idx[parent]].setdefault("children", []).append(idx[key])
        n = {"name": names[key], "translation": r6(lp)}
        if any(abs(c) > 1e-9 for c in lq[:3]):
            n["rotation"] = r6(lq)
        nodes.append(n)
    # Inverse bind: inverse of each joint's world matrix (rotation, unit scale, position).
    ib = []
    for key, _p, _c in skel:
        q, p = rot[key], pos[key]
        cols = [mul(qrot(q, e), unit) for e in ((1, 0, 0), (0, 1, 0), (0, 0, 1))]
        # inverse of [R*s | p] = [R^T / s | -R^T p / s]
        rows = [mul(c, 1.0 / (unit * unit)) for c in cols]   # rows of R^T / s
        t = [-dot(r, p) for r in rows]
        m = [rows[0][0], rows[1][0], rows[2][0], 0.0,
             rows[0][1], rows[1][1], rows[2][1], 0.0,
             rows[0][2], rows[1][2], rows[2][2], 0.0,
             t[0], t[1], t[2], 1.0]
        ib.append(tuple(round(c, 6) + 0.0 for c in m))
    widths = widths or (lambda k: 0.0 if k.endswith("_End") else 0.05)
    mesh = box_mesh(g, skel, pos, widths)
    skin = {"joints": [idx[k] for k, _p, _c in skel],
            "inverseBindMatrices": g.add("f", ib, "MAT4", 5126), "skeleton": idx[skel[0][0]]}
    nodes.append({"name": "Body", "mesh": 0, "skin": 0})
    doc = {"scene": 0, "scenes": [{"nodes": [0, len(nodes) - 1]}], "nodes": nodes,
           "meshes": [mesh], "skins": [skin]}
    if clips:
        anims = []
        for cname, length, count in (CLIPS if clips is True else clips):
            times = [length * i / (count - 1) for i in range(count)]
            ta = g.add("f", [(round(t, 6),) for t in times], "SCALAR", 5126, minmax=True)
            samplers, channels = [], []
            for bone in CLIP_BONES:
                vals = []
                for t in times:
                    r, hips = clip_pose(cname, t)
                    if bone == "Hips":
                        vals.append(r6(mul(add(pos["Hips"], hips), 1.0 / unit)))
                    else:
                        vals.append(r6(r.get(bone, (0.0, 0.0, 0.0, 1.0))))
                path_ = "translation" if bone == "Hips" else "rotation"
                va = g.add("f", vals, "VEC3" if bone == "Hips" else "VEC4", 5126)
                samplers.append({"input": ta, "output": va, "interpolation": "LINEAR"})
                channels.append({"sampler": len(samplers) - 1, "target": {"node": idx[bone], "path": path_}})
            anims.append({"name": cname, "samplers": samplers, "channels": channels})
        doc["animations"] = anims
    g.write(path, doc)


def human_widths(k):
    if k.endswith("_End"):
        return 0.0
    if k == "Head":
        return 0.1
    if k in ("Hips", "Spine", "Spine1", "Spine2"):
        return 0.14
    if k in ("LeftFoot", "RightFoot"):
        return 0.06
    if k in ("LeftToeBase", "RightToeBase"):
        return 0.02
    return 0.05


# ---- the quadruped ----------------------------------------------------------
# Named like the Khronos fox sample's bones: legs and arms by name, a spine and
# a head, so a name count alone takes it for a humanoid. Spine along +Z.
FOX = [
    ("b_Root_00",         None,                 "b_Hip_01",          (0.0, 0.40, -0.30)),
    ("b_Hip_01",          "b_Root_00",          "b_Spine01_02",      (0.0, 0.42, -0.25)),
    ("b_Spine01_02",      "b_Hip_01",           "b_Spine02_03",      (0.0, 0.43, -0.05)),
    ("b_Spine02_03",      "b_Spine01_02",       "b_Neck_04",         (0.0, 0.44, 0.15)),
    ("b_Neck_04",         "b_Spine02_03",       "b_Head_05",         (0.0, 0.50, 0.25)),
    ("b_Head_05",         "b_Neck_04",          None,                (0.0, 0.55, 0.35)),
    ("b_RightUpperArm_06", "b_Spine02_03",      "b_RightForeArm_07", (-0.08, 0.38, 0.15)),
    ("b_RightForeArm_07", "b_RightUpperArm_06", "b_RightHand_08",    (-0.08, 0.20, 0.15)),
    ("b_RightHand_08",    "b_RightForeArm_07",  None,                (-0.08, 0.04, 0.15)),
    ("b_LeftUpperArm_09", "b_Spine02_03",       "b_LeftForeArm_010", (0.08, 0.38, 0.15)),
    ("b_LeftForeArm_010", "b_LeftUpperArm_09",  "b_LeftHand_011",    (0.08, 0.20, 0.15)),
    ("b_LeftHand_011",    "b_LeftForeArm_010",  None,                (0.08, 0.04, 0.15)),
    ("b_Tail01_012",      "b_Hip_01",           "b_Tail02_013",      (0.0, 0.42, -0.35)),
    ("b_Tail02_013",      "b_Tail01_012",       None,                (0.0, 0.40, -0.55)),
    ("b_LeftLeg01_015",   "b_Hip_01",           "b_LeftLeg02_016",   (0.08, 0.38, -0.25)),
    ("b_LeftLeg02_016",   "b_LeftLeg01_015",    "b_LeftFoot01_017",  (0.08, 0.20, -0.28)),
    ("b_LeftFoot01_017",  "b_LeftLeg02_016",    None,                (0.08, 0.04, -0.25)),
    ("b_RightLeg01_019",  "b_Hip_01",           "b_RightLeg02_020",  (-0.08, 0.38, -0.25)),
    ("b_RightLeg02_020",  "b_RightLeg01_019",   "b_RightFoot01_021", (-0.08, 0.20, -0.28)),
    ("b_RightFoot01_021", "b_RightLeg02_020",   None,                (-0.08, 0.04, -0.25)),
]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "tests", "assets", "rigs")
    os.makedirs(out, exist_ok=True)
    skel = [(k, p, c) for k, p, c, _pos in HUMAN]
    rigs = [
        # file,                        names,    pose, legs, arms, scale, frames,     unit, clips
        ("mixamo_t_cm_clips.glb",      MIXAMO,   "T", 1.0,  1.0, 1.0,  "identity", 0.01, True),
        ("mixamo_t_stance_clips.glb",  MIXAMO,   "T", 1.0,  1.0, 1.0,  "identity", 1.0,  STANCE_CLIPS),
        ("mixamo_t_noclips.glb",       MIXAMO,   "T", 1.0,  1.0, 1.0,  "identity", 1.0,  False),
        ("ue_a_blender.glb",           UE,       "A", 1.15, 0.9, 1.0,  "y_bone_ue", 1.0, False),
        ("mixamo_a_blender_short.glb", MIXAMO,   "A", 1.0,  1.0, 0.8,  "y_bone",   1.0,  False),
        ("vroid_t.glb",                VROID,    "T", 1.0,  1.0, 1.0,  "identity", 1.0,  False),
        ("cc_t.glb",                   CC,       "T", 1.0,  1.0, 1.0,  "x_bone",   1.0,  False),
        ("daz_t.glb",                  DAZ,      "T", 1.0,  1.0, 1.0,  "identity", 1.0,  False),
        ("nonsense_t.glb",             NONSENSE, "T", 1.0,  1.0, 1.0,  "identity", 1.0,  False),
    ]
    for fname, names, pose, legs, arms, scale, style, unit, clips in rigs:
        pos = human_positions(pose, legs, arms, scale)
        write_rig(os.path.join(out, fname), skel, names, pos, style, unit, clips, human_widths)
    fox = [(k, p, c) for k, p, c, _pos in FOX]
    write_rig(os.path.join(out, "fox.glb"), fox, {k: k for k, _p, _c in fox},
              {k: p for k, _pa, _c, p in FOX}, "identity", 1.0, False,
              lambda k: 0.0 if k.startswith("b_Root") else 0.04)


if __name__ == "__main__":
    main()
