#!/usr/bin/env python3
"""Run the cross-compiled Windows test executables on a real Windows machine (.github/workflows/windows-tests.yml).

The tests are built on Ubuntu with MinGW (the release compiler, win32 thread model). ctest cannot run there, and the
CTestTestfile it writes holds Linux paths, so the build job exports the test list with `ctest --show-only=json-v1`
and this script runs it here: each test's .exe is looked up in the uploaded folder by its path under the build tree,
tests whose command is not a built .exe (python or bash scripts, emulators) are reported "not run" with the reason.
Writes a ctest-style log and a Markdown summary. Written for this repo from the ctest JSON format description.

Usage: run_windows_tests.py --tests tests.json --build-root <linux build dir> --bin <folder of copied files>
       --source <checkout> --log <file> --summary <file> [--timeout 300] [-j N]
       run_windows_tests.py --self-test
Exit code is 0 even when tests fail unless --strict (the job is non-blocking; failures show in the summary).
"""
import argparse, concurrent.futures as cf, json, os, subprocess, sys, tempfile, time


def plan(tests_json, build_root, bin_dir, source_dir):
    """Return a list of (name, argv or None, cwd, env, timeout, reason_not_run, will_fail)."""
    build_root = build_root.rstrip("/") + "/"
    out = []
    for t in tests_json.get("tests", []):
        name = t.get("name", "?")
        cmd = t.get("command") or []
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        if props.get("DISABLED"):
            out.append((name, None, None, None, None, "disabled in CMake", False)); continue
        if not cmd:
            out.append((name, None, None, None, None, "no command", False)); continue
        exe = cmd[0]
        if not exe.startswith(build_root) or not exe.lower().endswith(".exe"):
            out.append((name, None, None, None, None, "command is not a built .exe (%s)" % os.path.basename(exe), False)); continue
        local = os.path.join(bin_dir, exe[len(build_root):])
        if not os.path.isfile(local):
            out.append((name, None, None, None, None, "executable was not built or not copied", False)); continue

        def fix(a):
            if a.startswith(build_root):
                return os.path.join(bin_dir, a[len(build_root):])
            return a
        argv = [local] + [fix(a) for a in cmd[1:]]
        wd = props.get("WORKING_DIRECTORY")
        cwd = fix(wd) if wd and wd.startswith(build_root) else (source_dir if wd else os.path.dirname(local))
        if not os.path.isdir(cwd):
            cwd = os.path.dirname(local)
        env = {}
        for kv in props.get("ENVIRONMENT", []) or []:
            if "=" in kv:
                k, v = kv.split("=", 1); env[k] = fix(v)
        to = props.get("TIMEOUT")
        out.append((name, argv, cwd, env, float(to) if to else None, None, bool(props.get("WILL_FAIL"))))
    return out


def run_one(item, default_timeout, dll_dir):
    name, argv, cwd, env, to, _, will_fail = item
    e = dict(os.environ); e.update(env)
    e["PATH"] = dll_dir + os.pathsep + os.path.dirname(argv[0]) + os.pathsep + e.get("PATH", "")
    start = time.time()
    try:
        p = subprocess.run(argv, cwd=cwd, env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           stdin=subprocess.DEVNULL, timeout=to or default_timeout)
        ok = (p.returncode == 0) != will_fail
        status, text = ("passed" if ok else "failed (exit %d)" % p.returncode), p.stdout
    except subprocess.TimeoutExpired as x:
        status, text = "failed (timeout)", x.stdout or b""
    except OSError as x:
        status, text = "failed (could not start: %s)" % x, b""
    return name, status, time.time() - start, text.decode("utf-8", "replace")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--tests"); ap.add_argument("--build-root"); ap.add_argument("--bin"); ap.add_argument("--source", default=".")
    ap.add_argument("--dll-dir", default=None); ap.add_argument("--log"); ap.add_argument("--summary")
    ap.add_argument("--timeout", type=float, default=300); ap.add_argument("-j", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--build-failures", default=None, help="file listing targets that failed to compile")
    ap.add_argument("--strict", action="store_true"); ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    with open(a.tests, encoding="utf-8") as f:
        items = plan(json.load(f), a.build_root, a.bin, a.source)
    runnable = [i for i in items if i[1]]
    notrun = [(i[0], i[5]) for i in items if not i[1]]
    results = []
    with cf.ThreadPoolExecutor(max_workers=max(1, a.j)) as ex:
        for r in ex.map(lambda i: run_one(i, a.timeout, a.dll_dir or a.bin), runnable):
            results.append(r); print("%-50s %s (%.1fs)" % (r[0], r[1], r[2]), flush=True)
    failed = [r for r in results if r[1] != "passed"]
    os.makedirs(os.path.dirname(os.path.abspath(a.log)), exist_ok=True)
    with open(a.log, "w", encoding="utf-8") as f:
        for n, s, d, t in sorted(results):
            f.write("===== %s: %s (%.1fs)\n%s\n" % (n, s, d, t))
        for n, why in sorted(notrun):
            f.write("===== %s: not run (%s)\n" % (n, why))
    bf = []
    if a.build_failures and os.path.isfile(a.build_failures):
        bf = [l.strip() for l in open(a.build_failures, encoding="utf-8") if l.strip()]
    s = ["## Windows tests (real Windows, MinGW build)", "",
         "| Passed | Failed | Not run | Targets that did not compile |", "|---|---|---|---|",
         "| %d | %d | %d | %d |" % (len(results) - len(failed), len(failed), len(notrun), len(bf)), ""]
    if failed:
        s += ["### Failed", ""] + ["- `%s`: %s" % (n, st) for n, st, _, _ in sorted(failed)] + [""]
    if bf:
        s += ["### Did not compile for Windows", ""] + ["- `%s`" % b for b in bf[:200]] + [""]
    reasons = {}
    for _, why in notrun:
        reasons[why.split(" (")[0]] = reasons.get(why.split(" (")[0], 0) + 1
    if reasons:
        s += ["### Not run, by reason", ""] + ["- %s: %d" % kv for kv in sorted(reasons.items())] + [""]
    s.append("Full output: the `windows-tests-log` artifact (LastTest.log).")
    with open(a.summary, "a", encoding="utf-8") as f:
        f.write("\n".join(s) + "\n")
    print("passed %d, failed %d, not run %d" % (len(results) - len(failed), len(failed), len(notrun)))
    return 1 if (a.strict and failed) else 0


def self_test():
    d = tempfile.mkdtemp()
    br = "/home/runner/b"
    os.makedirs(os.path.join(d, "bin", "tests"))
    py = sys.executable
    # Use the Python interpreter as a stand-in "exe": copy is not needed, we point the plan at a script wrapper.
    exe = os.path.join(d, "bin", "tests", "t_ok.exe")
    open(exe, "w").close()
    j = {"tests": [
        {"name": "ok", "command": [br + "/tests/t_ok.exe", br + "/x"], "properties": [{"name": "TIMEOUT", "value": 5}]},
        {"name": "py", "command": ["/usr/bin/python3", "s.py"]},
        {"name": "missing", "command": [br + "/tests/nope.exe"]},
        {"name": "off", "command": [br + "/tests/t_ok.exe"], "properties": [{"name": "DISABLED", "value": True}]},
    ]}
    p = plan(j, br, os.path.join(d, "bin"), d)
    assert [x[0] for x in p] == ["ok", "py", "missing", "off"]
    assert p[0][1] == [exe, os.path.join(d, "bin", "x")] and p[0][4] == 5.0, p[0]
    assert p[1][1] is None and "not a built .exe" in p[1][5]
    assert p[2][1] is None and "not built" in p[2][5]
    assert p[3][1] is None and "disabled" in p[3][5]
    r = run_one(("echo", [py, "-c", "print('hi')"], d, {}, None, None, False), 10, d)
    assert r[1] == "passed" and "hi" in r[3], r
    r = run_one(("bad", [py, "-c", "import sys; sys.exit(3)"], d, {}, None, None, False), 10, d)
    assert r[1] == "failed (exit 3)", r
    r = run_one(("wf", [py, "-c", "import sys; sys.exit(3)"], d, {}, None, None, True), 10, d)
    assert r[1] == "passed", r
    r = run_one(("slow", [py, "-c", "import time; time.sleep(5)"], d, {}, 0.5, None, False), 10, d)
    assert r[1] == "failed (timeout)", r
    print("run_windows_tests self-test: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
