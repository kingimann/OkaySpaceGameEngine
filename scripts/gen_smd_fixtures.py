#!/usr/bin/env python3
"""Writes the SMD fixtures for the own SMD reader into tests/data/models/ (synthetic: written by hand, not by an exporter).
Deterministic. Run from the repository root:  python3 scripts/gen_smd_fixtures.py
Expected values are checked in tests/test_smd_reader.cpp."""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "data", "models")

# Three bones: root, upper (1 unit above), lower (1 more unit above, turned 90 degrees about Z) and a tip with
# rx = ry... = (90 deg, 0, 90 deg) to pin the Euler order. Two materials, link lists of 2, 1 (omitted: parent only) and 6 entries.
ARM = """version 1
nodes
  0 "root" -1
  1 "upper arm" 0
  2 "lower" 1
  3 "tip" 2
end
skeleton
time 0
  0 0.0 0.0 0.0 0.0 0.0 0.0
  1 0.0 1.0 0.0 0.0 0.0 0.0
  2 0.0 1.0 0.0 0.0 0.0 1.5707963
  3 1.0 0.0 0.0 1.5707963 0.0 1.5707963
end
triangles
arm_a.tga
  1 0.0 1.0 0.0 0.0 0.0 1.0 0.0 0.0 2 1 0.75 2 0.25
  1 1.0 1.0 0.0 0.0 0.0 1.0 1.0 0.0
  2 0.0 2.0 0.0 0.0 0.0 1.0 0.0 1.0 1 2 1.0
arm_b.tga
  0 0.0 0.0 0.0 0.0 0.0 1.0 0.0 0.0
  0 1.0 0.0 0.0 0.0 0.0 1.0 1.0 0.0
  0 0.0 0.0 1.0 0.0 0.0 1.0 0.0 0.25
arm_a.tga
  2 1.0 2.0 0.0 0.0 0.0 1.0 1.0 1.0 6 0 0.1 1 0.2 2 0.3 3 0.4 9 0.001 8 0.002
  2 0.0 2.0 0.0 0.0 0.0 1.0 0.0 1.0 1 2 1.0
  1 0.0 1.0 0.0 0.0 0.0 1.0 0.0 0.0 2 1 0.75 2 0.25
end
"""

# an animation: no triangles, frames 0, 5 and 10 (30 frames per second -> 0, 1/6, 1/3 s)
ARM_ANIM = """version 1
nodes
  0 "root" -1
  1 "upper arm" 0
  2 "lower" 1
end
skeleton
time 0
  0 0.0 0.0 0.0 0.0 0.0 0.0
  1 0.0 1.0 0.0 0.0 0.0 0.0
  2 0.0 1.0 0.0 0.0 0.0 0.0
time 5
  0 3.0 0.0 0.0 0.0 0.0 0.0
  1 0.0 1.0 0.0 0.0 0.0 1.5707963
  2 0.0 1.0 0.0 0.0 0.0 0.0
time 10
  0 6.0 0.0 0.0 0.0 0.0 0.0
  1 0.0 1.0 0.0 0.0 0.0 3.1415926
  2 0.0 2.0 0.0 0.0 0.0 0.0
end
"""

def w(name, text):
    with open(os.path.join(OUT, name), "w", newline="\n") as f:
        f.write(text)

w("arm.smd", ARM)
w("arm_anim.smd", ARM_ANIM)
w("arm_crlf.smd", ARM.replace("\n", "\r\n"))
