#!/usr/bin/env python3
"""Writes the OFF fixtures for the own OFF reader into tests/data/models/ (synthetic: written by hand, not by an exporter).
Deterministic. Run from the repository root:  python3 scripts/gen_off_fixtures.py
Expected values are checked in tests/test_off_reader.cpp."""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "data", "models")

# unit cube, 8 corners, 6 quads (counter clockwise seen from outside), comments and blank lines in between
CUBE = """OFF
# a unit cube
8 6 12

0 0 0
1 0 0
1 1 0
0 1 0
0 0 1
1 0 1
1 1 1
0 1 1
4 0 3 2 1
4 4 5 6 7
4 0 1 5 4
4 1 2 6 5
4 2 3 7 6
4 3 0 4 7
"""

# a square with per-vertex normals and texture coordinates (per vertex: position, normal, color, texture, in that order)
SQUARE_STNOFF = """STNOFF
4 2 0
0 0 0  0 0 1  0 0
1 0 0  0 0 1  1 0
1 1 0  0 0 1  1 1
0 1 0  0 0 1  0 1
3 0 1 2
3 0 2 3
"""

# colors on vertices and faces, a concave L-shaped hexagon, a 2-corner face and an out of range face
MIXED_COFF = """COFF
6 4 0
0 0 0 255 0 0 255
2 0 0 0 255 0 255
2 1 0 0 0 255 255
1 1 0 255 255 0 255
1 2 0 0 255 255 255
0 2 0 255 0 255 255
6 0 1 2 3 4 5 200 200 200
2 0 1
3 0 1 9
3 0 1 2 10 20 30
"""

def w(name, text):
    with open(os.path.join(OUT, name), "w", newline="\n") as f:
        f.write(text)

w("cube.off", CUBE)
w("square_stnoff.off", SQUARE_STNOFF)
w("mixed_coff.off", MIXED_COFF)
w("cube_crlf.off", CUBE.replace("\n", "\r\n"))
