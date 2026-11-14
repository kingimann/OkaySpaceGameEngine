#!/usr/bin/env python3
"""Writes the BVH motion capture fixtures for the own BVH reader into tests/data/models/.
Deterministic. Run from the repository root:  python3 scripts/gen_bvh_fixtures.py
Expected values are checked in tests/test_bvh_reader.cpp."""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "data", "models")

WALK = """HIERARCHY
ROOT Hips
{
\tOFFSET 0.0 1.0 0.0
\tCHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation
\tJOINT Spine
\t{
\t\tOFFSET 0.0 0.5 0.0
\t\tCHANNELS 3 Zrotation Xrotation Yrotation
\t\tJOINT Head
\t\t{
\t\t\tOFFSET 0.0 0.5 0.0
\t\t\tCHANNELS 3 Zrotation Xrotation Yrotation
\t\t\tEnd Site
\t\t\t{
\t\t\t\tOFFSET 0.0 0.25 0.0
\t\t\t}
\t\t}
\t}
\tJOINT Left Arm
\t{
\t\tOFFSET 0.4 0.4 0.0
\t\tCHANNELS 3 Zrotation Xrotation Yrotation
\t\tEnd Site
\t\t{
\t\t\tOFFSET 0.3 0.0 0.0
\t\t}
\t}
}
MOTION
Frames: 3
Frame Time: 0.5
0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0
1.0 0.5 -2.0 90.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 90.0
2.0 0.0 0.0 90.0 90.0 0.0 30.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0
"""

TWO_ROOTS = """HIERARCHY
ROOT A
{
  OFFSET 0 0 0
  CHANNELS 3 Xposition Yposition Zposition
  End Site
  {
    OFFSET 0 1 0
  }
}
ROOT B {
  OFFSET 5 0 0
  CHANNELS 3 Zrotation Xrotation Yrotation
}
MOTION
Frames: 2
Frame Time: 0.1
0 0 0 0 0 0
1 2 3 0 0 45
"""


def write(name, text, crlf=False):
    path = os.path.normpath(os.path.join(OUT, name))
    data = text.replace("\n", "\r\n") if crlf else text
    with open(path, "wb") as f:
        f.write(data.encode("ascii"))
    print("wrote", path, len(data), "bytes")


if __name__ == "__main__":
    write("walk.bvh", WALK)
    write("walk_crlf.bvh", WALK, crlf=True)
    write("two_roots.bvh", TWO_ROOTS)
