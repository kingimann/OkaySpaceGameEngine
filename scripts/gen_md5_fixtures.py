#!/usr/bin/env python3
"""Writes the MD5 fixtures for the own MD5 reader into tests/data/models/ (synthetic: written by hand, not by an exporter).
Deterministic. Run from the repository root:  python3 scripts/gen_md5_fixtures.py
Expected values are checked in tests/test_md5_reader.cpp."""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "data", "models")

# Three joints: base at the origin; arm 10 units up, turned -90 degrees about Z (orientation x y z = 0 0 0.70710678, w = -0.70710678);
# tip 5 units further along +Z in model space. Three meshes: a skinned triangle, a one-joint triangle with a shader that is a
# material name (not an image), and an empty one.
SKEL_MESH = """MD5Version 10
commandline "synthetic fixture for the own MD5 reader"

numJoints 3
numMeshes 3

joints {
	"base"	-1 ( 0 0 0 ) ( 0 0 0 )		// origin
	"arm"	0 ( 0 10 0 ) ( 0 0 0.70710678 )		// base
	"tip"	1 ( 0 10 5 ) ( 0 0 0 )		// arm
}

mesh {
	// meshes: arm
	shader "arm.tga"

	numverts 3
	vert 0 ( 0 0 ) 0 1
	vert 1 ( 1 0 ) 1 2
	vert 2 ( 0 1 ) 3 3

	numtris 1
	tri 0 0 1 2

	numweights 6
	weight 0 1 1 ( 1 0 0 )
	weight 1 0 0.5 ( 2 0 0 )
	weight 2 1 0.5 ( 0 2 0 )
	weight 3 0 0.25 ( 0 0 0 )
	weight 4 1 0.25 ( 0 0 0 )
	weight 5 2 0.5 ( 0 0 0 )
}

mesh {
	shader "models/zombie/body"

	numverts 3
	vert 0 ( 0 0 ) 0 1
	vert 1 ( 1 0 ) 1 1
	vert 2 ( 0 1 ) 2 1

	numtris 1
	tri 0 0 2 1

	numweights 3
	weight 0 0 1 ( 0 0 0 )
	weight 1 0 1 ( 4 0 0 )
	weight 2 0 1 ( 0 4 0 )
}

mesh {
	shader ""

	numverts 0

	numtris 0

	numweights 0
}
"""

# three frames at 10 per second: base moves in x and y, arm turns about Z (flag 32 = qz), tip has no animated component
SKEL_ANIM = """MD5Version 10
commandline "synthetic fixture for the own MD5 reader"

numFrames 3
numJoints 3
frameRate 10
numAnimatedComponents 3

hierarchy {
	"base"	-1 3 0	// origin
	"arm"	0 32 2	// base
	"tip"	1 0 0	// arm
}

bounds {
	( -1 -1 -1 ) ( 1 1 1 )
	( -1 -1 -1 ) ( 3 5 1 )
	( -1 -1 -1 ) ( 3 5 1 )
}

baseframe {
	( 0 0 0 ) ( 0 0 0 )
	( 0 10 0 ) ( 0 0 0 )
	( 0 0 5 ) ( 0 0 0 )
}

frame 0 {
	0 0 0
}

frame 1 {
	1 2 0.3826834
}

frame 2 {
	2 4 0.7071068
}
"""

def w(name, text):
    with open(os.path.join(OUT, name), "w", newline="\n") as f:
        f.write(text)

w("skel.md5mesh", SKEL_MESH)
w("skel.md5anim", SKEL_ANIM)
w("skel_crlf.md5mesh", SKEL_MESH.replace("\n", "\r\n"))
w("skel_crlf.md5anim", SKEL_ANIM.replace("\n", "\r\n"))
