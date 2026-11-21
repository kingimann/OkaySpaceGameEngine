#!/usr/bin/env python3
"""Run a scene for real and look at what happened, instead of guessing from the code.

    python3 scripts/okay_run.py SCENE [--input IN.json] [--check CHECK.json] [--out DIR] [--frames N] [--dt S]
                                [--trace NAME,NAME] [--shots F,F] [--size WxH] [--bin PATH]

It runs the `okay_run` engine tool (tests/okay_run.cpp: the editor's Play path, the same Scene::Update the player runs,
fixed time step, no window, software-rendered pictures) with the scripted input, then evaluates the checks.
The output folder (default: $OKAY_SHOT_DIR/okay_run/<scene> or a new temp folder; never inside the repository) gets
trace.jsonl, log.txt, frame_NNNNN.png, summary.json, events.txt and checks.json.
Exit: 0 every check passed, 1 a check failed, 2 bad arguments or files, 3 the engine run failed (no scene, Play blocked,
stopped early, crashed). Reference: docs/process/okay_run.md. Written from scratch for this repository.
"""
import argparse
import json
import operator
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OPS = {">": operator.gt, ">=": operator.ge, "<": operator.lt, "<=": operator.le, "==": operator.eq, "!=": operator.ne}


def find_bin(given):
    cands = [given] if given else []
    if os.environ.get("OKAY_RUN_BIN"):
        cands.append(os.environ["OKAY_RUN_BIN"])
    for b in ("build", "build-release", "build-debug"):
        cands += [os.path.join(ROOT, b, "bin", "okay_run"), os.path.join(ROOT, b, "tests", "okay_run")]
    for c in cands:
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return None


def frame_of(ev, dt):
    if "frame" in ev:
        return int(ev["frame"])
    if "time" in ev:
        return int(round(float(ev["time"]) / dt))
    raise ValueError("event without 'frame' or 'time': %r" % (ev,))


def events_to_lines(events, dt):
    """The JSON input events to the engine tool's plain event lines."""
    lines = []
    for ev in events:
        f = frame_of(ev, dt)
        if "press" in ev:
            lines.append("%d key %s down" % (f, ev["press"]))
        elif "release" in ev:
            lines.append("%d key %s up" % (f, ev["release"]))
        elif "tap" in ev:   # press, hold for `frames` (default 2), release
            lines.append("%d key %s down" % (f, ev["tap"]))
            lines.append("%d key %s up" % (f + int(ev.get("frames", 2)), ev["tap"]))
        elif "hold" in ev:  # press now, release after `seconds`
            lines.append("%d key %s down" % (f, ev["hold"]))
            lines.append("%d key %s up" % (f + max(1, int(round(float(ev.get("seconds", 1.0)) / dt))), ev["hold"]))
        elif "mouse" in ev:
            lines.append("%d mouse %g %g" % (f, ev["mouse"][0], ev["mouse"][1]))
        elif "look" in ev:
            lines.append("%d look %g %g" % (f, ev["look"][0], ev["look"][1]))
        elif "button" in ev:
            lines.append("%d button %d %s" % (f, int(ev["button"]), "down" if ev.get("down", True) else "up"))
        elif "stick" in ev:
            r = ev.get("right", [0, 0])
            lines.append("%d stick %g %g %g %g" % (f, ev["stick"][0], ev["stick"][1], r[0], r[1]))
        elif "click" in ev:
            lines.append("%d click %s" % (f, ev["click"]))
        elif ev.get("shot"):
            lines.append("%d shot" % f)
        else:
            raise ValueError("unknown event %r" % (ev,))
    return lines


def get_path(obj, path):
    """'pos.y' / 'anim.0.params.speed' / 'grounded' inside one traced object; None when missing."""
    cur = obj
    for part in path.split(".") if path else []:
        if cur is None:
            return None
        if isinstance(cur, list):
            if part in ("x", "y", "z") and len(cur) == 3 and not isinstance(cur[0], (dict, list)):
                cur = cur["xyz".index(part)]
            elif part.isdigit() and int(part) < len(cur):
                cur = cur[int(part)]
            else:
                return None
        elif isinstance(cur, dict):
            cur = cur.get(part)
        else:
            return None
    return cur


def subject(frame, name):
    return frame.get("camera") if name == "camera" else (frame.get("objects") or {}).get(name)


def dist(a, b):
    if isinstance(a, list) and isinstance(b, list):
        return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5
    return abs(a - b)


def window(trace, c):
    last = len(trace) - 1
    def idx(v, default):
        if v is None:
            return default
        if v == "end":
            return last
        return int(v)
    lo, hi = idx(c.get("from"), 0), idx(c.get("to"), last)
    return [f for f in trace if lo <= f["frame"] <= hi]


def evaluate(c, trace, log_lines, summary):
    """One check -> (passed, words)."""
    kind = c.get("expect")
    name, field = c.get("object", ""), c.get("field", "")
    if kind == "no_errors":
        ignore = c.get("ignore", [])
        bad = [l for l in log_lines if "] error: " in l and not any(s in l for s in ignore)]
        if c.get("warnings"):
            bad += [l for l in log_lines if "] warning: " in l and not any(s in l for s in ignore)]
        return (not bad, "no errors in the log" if not bad else "%d error line(s), first: %s" % (len(bad), bad[0].strip()))
    if kind == "ran":
        want = c.get("frames", summary.get("frames_asked"))
        ok = summary.get("frames_run") == want and summary.get("exit") == 0
        return ok, "ran %s of %s frames (exit %s)" % (summary.get("frames_run"), want, summary.get("exit"))
    if kind == "camera_never_inside":
        for f in window(trace, c):
            cam = f.get("camera")
            if cam and cam.get("inside"):
                return False, "camera inside collider '%s' at frame %d" % (cam["inside"], f["frame"])
        return True, "camera never inside a collider"
    if kind == "value":
        at = c.get("at", "end")
        f = trace[-1] if at == "end" else next((x for x in trace if x["frame"] == int(at)), None)
        if f is None:
            return False, "no frame %s in the trace" % at
        v = get_path(subject(f, name), field)
        if v is None:
            return False, "%s.%s missing at frame %d" % (name, field, f["frame"])
        ok = OPS[c.get("op", "==")](v, c["value"])
        return ok, "%s.%s = %r at frame %d (want %s %r)" % (name, field, v, f["frame"], c.get("op", "=="), c["value"])
    if kind in ("ever", "always"):
        op = OPS[c.get("op", "==")]
        frames = window(trace, c)
        hits = [f["frame"] for f in frames if get_path(subject(f, name), field) is not None and op(get_path(subject(f, name), field), c["value"])]
        if kind == "ever":
            return (bool(hits), "%s.%s %s %r first at frame %s" % (name, field, c.get("op", "=="), c["value"], hits[0]) if hits
                    else "%s.%s never %s %r" % (name, field, c.get("op", "=="), c["value"]))
        miss = [f["frame"] for f in frames if f["frame"] not in set(hits)]
        return (not miss, "%s.%s %s %r on every frame" % (name, field, c.get("op", "=="), c["value"]) if not miss
                else "%s.%s not %s %r at frame %d (value %r)" % (name, field, c.get("op", "=="), c["value"], miss[0],
                                                              get_path(subject(next(f for f in frames if f["frame"] == miss[0]), name), field)))
    if kind == "moved":
        frames = window(trace, c)
        field = field or "pos"
        a, b = get_path(subject(frames[0], name), field), get_path(subject(frames[-1], name), field)
        if a is None or b is None:
            return False, "%s.%s missing" % (name, field)
        d = dist(a, b)
        ok = d >= float(c.get("min", 0)) and d <= float(c.get("max", float("inf")))
        return ok, "%s.%s moved %.3f from frame %d to %d (want %s..%s)" % (name, field, d, frames[0]["frame"], frames[-1]["frame"], c.get("min", 0), c.get("max", "inf"))
    if kind == "max_step":
        frames = window(trace, c)
        field = field or "pos"
        worst, at = 0.0, None
        for p, q in zip(frames, frames[1:]):
            a, b = get_path(subject(p, name), field), get_path(subject(q, name), field)
            if a is None or b is None:
                continue
            d = dist(a, b)
            if d > worst:
                worst, at = d, q["frame"]
        ok = worst <= float(c["max"])
        return ok, "largest frame-to-frame change of %s.%s is %.4f%s (max %s)" % (name, field, worst, " at frame %d" % at if at is not None else "", c["max"])
    if kind == "frame_ms":
        v = summary.get("frame_ms_mean", 0)
        return v <= float(c["max"]), "mean frame time %.3f ms (max %s)" % (v, c["max"])
    return False, "unknown check %r" % (kind,)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scene")
    ap.add_argument("--input", help="JSON input file: events, and optionally frames, dt, trace, shots, checks")
    ap.add_argument("--check", help="JSON check file: {\"checks\": [...]} (or a list)")
    ap.add_argument("--out", help="output folder (default $OKAY_SHOT_DIR/okay_run/<scene> or a temp folder)")
    ap.add_argument("--frames", type=int)
    ap.add_argument("--dt", type=float)
    ap.add_argument("--trace", help="object names to trace, comma separated")
    ap.add_argument("--shots", help="frames to picture, comma separated ('end' = the last)")
    ap.add_argument("--size", default="640x360")
    ap.add_argument("--bin", help="the okay_run engine tool (default: $OKAY_RUN_BIN, build/bin/okay_run)")
    a = ap.parse_args(argv)

    try:
        spec = json.load(open(a.input)) if a.input else {}
        checks = spec.get("checks", [])
        if a.check:
            cj = json.load(open(a.check))
            checks = checks + (cj if isinstance(cj, list) else cj.get("checks", []))
    except (OSError, ValueError) as e:
        print("okay_run: %s" % e, file=sys.stderr)
        return 2
    dt = a.dt or float(spec.get("dt", 1.0 / 60.0))
    frames = a.frames or int(spec.get("frames", 120))
    trace = a.trace or ",".join(spec.get("trace", []))
    shots = [str(s) for s in (a.shots.split(",") if a.shots else spec.get("shots", []))]
    shots = ",".join(str(frames - 1) if s == "end" else s for s in shots)
    scene = os.path.abspath(a.scene)
    if not os.path.isfile(scene):
        print("okay_run: no such scene: %s" % a.scene, file=sys.stderr)
        return 2
    out = a.out
    if not out:
        base = os.environ.get("OKAY_SHOT_DIR")
        stem = os.path.splitext(os.path.basename(scene))[0]
        out = os.path.join(base, "okay_run", stem) if base else tempfile.mkdtemp(prefix="okay_run_%s_" % stem)
    out = os.path.abspath(out)
    if out == ROOT or out.startswith(ROOT + os.sep):
        print("okay_run: the output folder must be outside the repository (pictures are never committed)", file=sys.stderr)
        return 2
    os.makedirs(out, exist_ok=True)
    try:
        lines = events_to_lines(spec.get("events", []), dt)
    except (ValueError, KeyError, IndexError, TypeError) as e:
        print("okay_run: bad input file: %s" % e, file=sys.stderr)
        return 2
    with open(os.path.join(out, "events.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    exe = find_bin(a.bin)
    if not exe:
        print("okay_run: no okay_run tool; build it: cmake --build build --target okay_run (or pass --bin / OKAY_RUN_BIN)", file=sys.stderr)
        return 2
    cmd = [exe, "--scene", scene, "--out", out, "--frames", str(frames), "--dt", repr(dt), "--events", os.path.join(out, "events.txt"),
           "--size", a.size]
    if trace:
        cmd += ["--trace", trace]
    if shots:
        cmd += ["--shots", shots]
    # the run happens in the output folder, with a private HOME, so nothing it saves lands in the repository or your settings
    env = dict(os.environ, HOME=os.path.join(out, "home"), SDL_AUDIODRIVER="dummy", OKAY_UPDATE_ON_LAUNCH="0")
    os.makedirs(env["HOME"], exist_ok=True)
    try:
        code = subprocess.run(cmd, cwd=out, env=env, timeout=float(os.environ.get("OKAY_RUN_TIMEOUT", "600"))).returncode
    except subprocess.TimeoutExpired:
        code = -9
    try:
        summary = json.load(open(os.path.join(out, "summary.json")))
    except (OSError, ValueError):
        summary = {"exit": code, "why": "the engine tool left no summary (crashed?)"}
    summary["frames_asked"] = frames
    trace_rows = []
    try:
        with open(os.path.join(out, "trace.jsonl")) as f:
            trace_rows = [json.loads(l) for l in f if l.strip()]
    except (OSError, ValueError):
        pass
    try:
        log_lines = open(os.path.join(out, "log.txt")).read().splitlines()
    except OSError:
        log_lines = []

    print("okay_run: %s: %s frames, %s errors, %s warnings, %.3f ms/frame mean (%.3f max), exit %s -> %s"
          % (os.path.relpath(scene, ROOT) if scene.startswith(ROOT) else scene, summary.get("frames_run"), summary.get("errors"),
             summary.get("warnings"), summary.get("frame_ms_mean") or 0, summary.get("frame_ms_max") or 0, summary.get("exit"), out))
    if code != 0 or summary.get("exit") != 0:
        print("okay_run: ENGINE RUN FAILED (exit %s): %s" % (code, summary.get("why", "")))
        for l in [l for l in log_lines if "] error: " in l][:5]:
            print("  " + l)
        json.dump({"engine_exit": code, "results": []}, open(os.path.join(out, "checks.json"), "w"), indent=1)
        return 3
    results, failed = [], 0
    for c in checks:
        try:
            ok, words = evaluate(c, trace_rows, log_lines, summary)
        except (KeyError, ValueError, TypeError, IndexError, StopIteration) as e:
            ok, words = False, "bad check %r: %s" % (c, e)
        failed += not ok
        results.append({"check": c, "pass": ok, "result": words})
        print("  %s  %s" % ("PASS" if ok else "FAIL", words))
    json.dump({"engine_exit": 0, "failed": failed, "results": results}, open(os.path.join(out, "checks.json"), "w"), indent=1)
    if checks:
        print("okay_run: %d of %d checks passed" % (len(checks) - failed, len(checks)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
