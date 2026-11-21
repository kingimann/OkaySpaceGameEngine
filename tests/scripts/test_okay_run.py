#!/usr/bin/env python3
"""Self-test for scripts/okay_run.py.

Part 1 (always): the input-file conversion and every check kind, on a hand-made trace (no engine needed).
Part 2 (when the okay_run engine tool is built: --bin, $OKAY_RUN_BIN or build/bin/okay_run): plays
examples/player/player_obstacle_course.okayscene for 5 s, walks forward 2 s and jumps, and checks that the player moved,
left the ground, landed, that the camera followed without jumps or going into a wall, and that the log has no errors.
Then a check that must fail, to prove a failing run exits 1. Exit 0 = all good, 77 = part 2 skipped (no tool).
"""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import okay_run  # noqa: E402

fails = 0


def check(cond, words):
    global fails
    print(("ok    " if cond else "FAIL  ") + words)
    fails += not cond


# ---- part 1: no engine ----
lines = okay_run.events_to_lines([{"time": 0.5, "hold": "w", "seconds": 1.0}, {"frame": 3, "tap": "space"},
                                  {"frame": 4, "click": "Start Button"}, {"frame": 5, "stick": [0, 1]}, {"frame": 6, "shot": True}], 0.1)
check(lines == ["5 key w down", "15 key w up", "3 key space down", "5 key space up", "4 click Start Button",
                "5 stick 0 1 0 0", "6 shot"], "events to lines: %r" % lines)
trace = [{"frame": i, "objects": {"P": {"pos": [0, 0.1 * (i == 2), -i], "grounded": i != 2, "anim": [{"state": "Walk", "params": {"speed": i}}]}},
          "camera": {"pos": [0, 1.6, -i], "fov": 60, "inside": "Wall" if i == 3 else None}} for i in range(5)]
summ = {"frames_run": 5, "frames_asked": 5, "exit": 0, "frame_ms_mean": 1.0}
log = ["[0] info: hi", "[2] warning: careful"]
E = lambda c, l=log: okay_run.evaluate(c, trace, l, summ)[0]  # noqa: E731
check(E({"expect": "ran"}), "ran")
check(E({"expect": "no_errors"}) and not E({"expect": "no_errors"}, log + ["[3] error: boom"]), "no_errors")
check(not E({"expect": "no_errors", "warnings": True}), "no_errors with warnings counts the warning")
check(E({"expect": "value", "object": "P", "field": "pos.z", "at": 4, "op": "<", "value": -3.5}), "value at frame")
check(E({"expect": "value", "object": "P", "field": "anim.0.params.speed", "at": "end", "op": "==", "value": 4}), "value of an animator param")
check(E({"expect": "ever", "object": "P", "field": "grounded", "value": False}), "ever")
check(not E({"expect": "always", "object": "P", "field": "grounded", "value": True}), "always fails on one frame")
check(E({"expect": "moved", "object": "P", "min": 3.9, "max": 4.1}), "moved")
check(E({"expect": "max_step", "object": "camera", "max": 1.0}) and not E({"expect": "max_step", "object": "camera", "max": 0.5}), "max_step")
check(not E({"expect": "camera_never_inside"}) and E({"expect": "camera_never_inside", "to": 2}), "camera_never_inside")
check(not E({"expect": "nonsense"}), "an unknown check fails")

# ---- part 2: the real engine ----
exe = okay_run.find_bin(sys.argv[1] if len(sys.argv) > 1 else None)
if not exe:
    print("skip  part 2: the okay_run tool is not built (cmake --build build --target okay_run)")
    sys.exit(1 if fails else 77)
scene = os.path.join(ROOT, "examples", "player", "player_obstacle_course.okayscene")
spec = os.path.join(HERE, "okay_run", "obstacle_walk_jump.json")
chk = os.path.join(HERE, "okay_run", "obstacle_walk_jump.check.json")
with tempfile.TemporaryDirectory(prefix="test_okay_run_") as out:
    r = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "scripts", "okay_run.py"), scene, "--input", spec, "--check", chk,
                        "--out", os.path.join(out, "a"), "--bin", exe, "--size", "320x180"], capture_output=True, text=True)
    print(r.stdout + r.stderr)
    check(r.returncode == 0, "obstacle course walk + jump: every check passed (exit %d)" % r.returncode)
    rows = [json.loads(l) for l in open(os.path.join(out, "a", "trace.jsonl"))]
    check(len(rows) == 300 and rows[0]["objects"]["Player"] is not None, "trace has 300 frames with the player")
    check(all(os.path.isfile(os.path.join(out, "a", "frame_%05d.png" % f)) for f in (0, 150, 299)), "pictures at frames 0, 150, 299")
    bad = os.path.join(out, "bad.json")
    json.dump({"checks": [{"expect": "value", "object": "Player", "field": "pos.y", "at": "end", "op": ">", "value": 50}]}, open(bad, "w"))
    r = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "scripts", "okay_run.py"), scene, "--input", spec, "--check", bad,
                        "--out", os.path.join(out, "b"), "--bin", exe, "--frames", "30", "--shots", ""], capture_output=True, text=True)
    check(r.returncode == 1 and "FAIL" in r.stdout, "an impossible check fails the run (exit %d)" % r.returncode)
    r = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "scripts", "okay_run.py"), os.path.join(out, "missing.okayscene"),
                        "--out", os.path.join(out, "c"), "--bin", exe], capture_output=True, text=True)
    check(r.returncode == 2, "a missing scene is exit 2 (got %d)" % r.returncode)
sys.exit(1 if fails else 0)
