#!/usr/bin/env python3
"""Offline checks of .github/workflows/dev-build.yml, the test-build workflow. Nothing here can run GitHub Actions, so this does what can be done
without it (a workflow nobody has seen run is a risk, and this is how that risk is kept small):

  * the shape: only workflow_dispatch with the inputs ref and note, read-only permissions, two jobs, a timeout on each, the publish job serialized;
  * the promises in the workflow's header, as checks: no GitHub release, tag or push; no signing, installer, zip or Linux server; the real release
    publisher (scripts/publish-supabase.sh) is never called, only scripts/publish-supabase-dev.sh; no `set -x`;
  * secrets: the build job (which runs the branch's own scripts) never receives a secret's value, only whether the publish key exists; the key and the
    signing seed are given to one step of the publish job; the publish job runs the scripts of the workflow's own commit, not the built branch;
  * nothing a person types (the ref, the note) is ever pasted into a shell script: `${{ ... }}` never appears inside a `run:`;
  * only actions the repository already uses, at the versions it already uses;
  * the build steps are a COPY of release.yml's first job: every copied step is compared field by field, so a change to one reminds you of the other;
  * the shell of the new steps is RUN here (bash, with a fake curl and temp folders): numbering a test build (and that it changes exactly one line of
    CMakeLists.txt), refusing a bad branch name, checking the version is inside the programs, the start-from-main guard, the missing-key stop.

Exit 0 = fine, 1 = a check failed, 77 = PyYAML is not installed (ctest shows that as skipped, not passed).
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

try:
    import yaml
except ImportError:
    print("SKIP: PyYAML is not installed, so the workflow cannot be read")
    sys.exit(77)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEV = os.path.join(ROOT, ".github", "workflows", "dev-build.yml")
REL = os.path.join(ROOT, ".github", "workflows", "release.yml")
fails = []


def check(cond, what):
    if not cond:
        fails.append(what)


class StrictLoader(yaml.SafeLoader):
    """SafeLoader that refuses a key written twice in one mapping (PyYAML silently keeps the last one; GitHub would reject the file)."""


def _no_duplicate_keys(loader, node, deep=False):
    seen = set()
    for key_node, _ in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in seen:
            raise yaml.constructor.ConstructorError(None, None, "found a duplicate key %r" % (key,), key_node.start_mark)
        seen.add(key)
    return yaml.SafeLoader.construct_mapping(loader, node, deep)


StrictLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _no_duplicate_keys)


def load(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    return yaml.load(text, Loader=StrictLoader), text


dev, dev_text = load(DEV)
rel, _ = load(REL)
ci, ci_text = load(os.path.join(ROOT, ".github", "workflows", "ci.yml"))
check("\t" not in dev_text, "the workflow has no tab characters (YAML does not allow them)")
check(dev_text.endswith("\n") and "\r" not in dev_text, "the workflow ends with a newline and uses LF line endings")
on = dev.get("on", dev.get(True))   # (YAML 1.1 reads a bare `on` as the boolean true)
jobs = dev["jobs"]
build, publish = jobs["windows-exe"], jobs["publish"]


def steps_by_name(job):
    return {s.get("name", s.get("uses")): s for s in job["steps"]}


bsteps, psteps = steps_by_name(build), steps_by_name(publish)

# ---- the shape --------------------------------------------------------------------------------------------------------------------
check(isinstance(on, dict) and set(on) == {"workflow_dispatch"}, "the only trigger is workflow_dispatch (no push, no pull request, no schedule)")
inputs = (on.get("workflow_dispatch") or {}).get("inputs", {})
check(set(inputs) == {"ref", "note"}, "the inputs are exactly ref and note")
for name in ("ref", "note"):
    check(inputs.get(name, {}).get("type") == "string" and inputs.get(name, {}).get("required") is False, "input %s is an optional string" % name)
check(dev.get("permissions") == {"contents": "read"}, "workflow permissions are contents: read and nothing else")
for jn, j in jobs.items():
    check(j.get("permissions") == {"contents": "read"}, "job %s has read-only permissions" % jn)
    check(isinstance(j.get("timeout-minutes"), int) and 0 < j["timeout-minutes"] <= 60, "job %s has a timeout" % jn)
check(set(jobs) == {"windows-exe", "publish"}, "two jobs: the build and the publish")
check(publish.get("needs") in ("windows-exe", ["windows-exe"]), "publish needs the build")
check(publish.get("concurrency", {}).get("group") == "dev-build-publish" and publish["concurrency"].get("cancel-in-progress") is False,
      "publishes are one at a time and a waiting publish is never cancelled by a newer one running")
check("concurrency" not in build or build["concurrency"].get("cancel-in-progress") is not True, "a running build is never cancelled by a newer one")

# ---- the promises in the header ---------------------------------------------------------------------------------------------------
code_only = "\n".join(l for l in dev_text.split("\n") if not l.lstrip().startswith("#"))   # (the header talks ABOUT these things)
for pat, why in (
    (r"action-gh-release|gh release|--latest|make_latest|releases/latest", "no GitHub Release and no 'latest'"),
    (r"git tag|git push|git commit|git config", "no tag, push or commit"),
    (r"publish-supabase\.sh", "the real release publisher is never called (only publish-supabase-dev.sh)"),
    (r"makensis|okayspace-installer|OkaySpace-Setup|\.nsi\b", "no installer"),
    (r"sign-windows|WINDOWS_SIGN|PFX", "no code signing, so the signing secrets stay out"),
    (r"linux-server|okay-server-linux|okay-player\b", "no Linux server"),
    (r"\bzip -r|package zip", "no zip"),
    (r"set -x|bash -x|sh -x|--trace|curl[^\n]* -v\b", "nothing is traced (a trace could print a key)"),
    (r"pull_request|\bpush:|schedule:|workflow_run", "no other trigger"),
):
    check(re.search(pat, code_only, re.I) is None, "the workflow must not contain /%s/ (%s)" % (pat, why))
check("scripts/publish-supabase-dev.sh" in code_only, "the workflow publishes with scripts/publish-supabase-dev.sh")
publish_run = psteps["Publish to the dev folder"]["run"].strip()
check(publish_run == "bash scripts/publish-supabase-dev.sh", "the publish step runs exactly the dev publisher")

# ---- secrets ------------------------------------------------------------------------------------------------------------------------
build_json = json.dumps(build)
refs = re.findall(r"secrets\.[A-Za-z0-9_]+(?:\s*!=\s*'')?", build_json)
check(all(r.replace(" ", "") == "secrets.SUPABASE_SERVICE_KEY!=''" for r in refs), "the build job only asks whether the publish key exists, never for its value: %r" % refs)
check("secrets" not in json.dumps({k: v for k, v in build.items() if k != "steps"}), "no secret at job level of the build")
for name, st in bsteps.items():
    check("secrets." not in json.dumps(st) or name == "Check the publish key exists", "build step %r does not touch secrets" % name)
secret_steps = [n for n, st in psteps.items() if "secrets." in json.dumps(st)]
check(secret_steps == ["Publish to the dev folder"], "only the publish step gets secrets, got %r" % secret_steps)
penv = psteps["Publish to the dev folder"].get("env", {})
check(set(penv) == {"SUPABASE_SERVICE_KEY", "OKAY_UPDATE_SIGNING_SEED"}, "the publish step's secrets are the key and the signing seed: %r" % set(penv))
check(penv.get("SUPABASE_SERVICE_KEY") == "${{ secrets.SUPABASE_SERVICE_KEY }}" and penv.get("OKAY_UPDATE_SIGNING_SEED") == "${{ secrets.OKAY_UPDATE_SIGNING_SEED }}", "secrets are passed whole, by name")
for name, st in {**bsteps, **psteps}.items():
    for line in str(st.get("run", "")).split("\n"):   # (naming the secret in a sentence is fine; expanding its value in a script is not)
        check(not re.search(r"\$\{?(SUPABASE_SERVICE_KEY|OKAY_UPDATE_SIGNING_SEED|SUPABASE_ACCESS_TOKEN)", line), "step %r expands a key in its script: %s" % (name, line.strip()))
check(not any("inputs.ref" in json.dumps(st) for st in publish["steps"]), "the publish job never checks out or builds the ref that was typed")
pcheckout = [s for s in publish["steps"] if s.get("uses", "").startswith("actions/checkout@")]
check(len(pcheckout) == 1 and "with" not in pcheckout[0], "the publish job checks out the workflow's own commit (no ref), so a branch cannot change what publishes")
bcheckout = [s for s in build["steps"] if s.get("uses", "").startswith("actions/checkout@")]
check(len(bcheckout) == 1 and "inputs.ref" in bcheckout[0].get("with", {}).get("ref", ""), "the build job checks out the ref that was typed")
check(penv == penv and publish.get("env", {}).get("DIST_DIR") == "dev-dist", "the publisher reads the downloaded build, not a checkout's")

# ---- nothing typed reaches a shell ---------------------------------------------------------------------------------------------------
for name, st in {**bsteps, **psteps}.items():
    check("${{" not in str(st.get("run", "")), "step %r pastes an expression into its script (use env: and a shell variable)" % name)

# ---- only actions the repository already uses --------------------------------------------------------------------------------------
def uses_of(doc):
    return {s["uses"] for j in doc["jobs"].values() for s in j.get("steps", []) if "uses" in s}


known = uses_of(rel) | uses_of(ci)
used = uses_of(dev)
check(used <= known, "new actions or versions: %r" % sorted(used - known))
check(all(re.match(r"^[A-Za-z0-9._-]+/[A-Za-z0-9._/-]+@v\d+$", u) for u in used), "every action is pinned to a version tag")

# ---- the order of the build job -----------------------------------------------------------------------------------------------------
names = [s.get("name", s.get("uses")) for s in build["steps"]]


def idx(n):
    return names.index(n) if n in names else -1


order = ["Check this run was started from the default branch", "Check the publish key exists", "actions/checkout@v6", "Record the branch and commit",
         "Number this test build", "Build Windows distribution", "Check the programs carry the test version", "Collect the files for the publish job",
         "Keep the files for the publish job"]
check(all(idx(n) >= 0 for n in order), "build steps missing: %r" % [n for n in order if idx(n) < 0])
check([idx(n) for n in order] == sorted(idx(n) for n in order), "the build steps are in the wrong order")
check(idx("Refuse own crypto and the own-stack switch") < idx("Build Windows distribution"), "the own-crypto refusal runs before the build")
check(idx("Restore compiler cache") < idx("Build Windows distribution"), "the compiler cache is restored before the build")

# ---- the copy of release.yml's first job ---------------------------------------------------------------------------------------------
relsteps = steps_by_name(rel["jobs"]["windows-exe"])
copied = ["Check the launcher's trusted anti-cheat keys", "Note the runner image", "Restore downloaded apt packages", "Install toolchain",
          "Check for newly downloaded apt packages", "Save downloaded apt packages", "Fetch SDL2 (MinGW dev)", "Fetch libsodium (MinGW prebuilt)",
          "Refuse own crypto and the own-stack switch", "Build Windows distribution"]
for name in copied:
    check(name in relsteps and name in bsteps, "copied step %r is missing on one side" % name)
    if name in relsteps and name in bsteps:
        check(relsteps[name] == bsteps[name], "copied step %r has drifted from release.yml: change both" % name)
rc, dc = relsteps.get("Restore compiler cache", {}), bsteps.get("Restore compiler cache", {})
check(rc.get("uses") == "actions/cache@v5" and dc.get("uses") == "actions/cache/restore@v5" and rc.get("with") == dc.get("with"),
      "the compiler cache is restored with release.yml's key, never saved")
check(not any(s.get("uses", "").startswith("actions/cache@") or s.get("uses", "").startswith("actions/cache/save@") and s.get("name") != "Save downloaded apt packages" for s in build["steps"]),
      "no step saves the compiler cache")
renv, denv = rel["jobs"]["windows-exe"]["env"], build["env"]
for k in ("SDL2_VERSION", "CCACHE_DIR", "CCACHE_BASEDIR", "CCACHE_COMPRESS", "CCACHE_MAXSIZE", "OKAY_AC_TRUSTED_KEYS", "OKAY_UPDATE_TRUSTED_KEYS"):
    check(renv.get(k) == denv.get(k), "job env %s differs from release.yml" % k)
check(set(denv) <= set(renv) - {"SUPABASE_PUBLISH"}, "the build job's env has something release.yml's does not: %r" % (set(denv) - set(renv)))

# ---- run the new shell steps ---------------------------------------------------------------------------------------------------------
def run_step(name, step_env, cwd, path_dir=None, use_pipefail=True):
    """Run a step's script with bash the way the runner does (bash -e, and also with pipefail); returns (exit code, output text, outputs file text)."""
    step = (bsteps.get(name) or psteps.get(name))
    out_file = os.path.join(cwd, "_github_output")
    sum_file = os.path.join(cwd, "_github_summary")
    for f in (out_file, sum_file):
        open(f, "w").close()
    env = {"PATH": (path_dir + ":" if path_dir else "") + "/usr/bin:/bin", "HOME": cwd, "GITHUB_OUTPUT": out_file, "GITHUB_STEP_SUMMARY": sum_file}
    env.update(step_env)
    script = os.path.join(cwd, "_step.sh")
    with open(script, "w") as f:
        f.write(step["run"])
    p = subprocess.run(["bash", "-e"] + (["-o", "pipefail"] if use_pipefail else []) + [script], cwd=cwd, env=env, capture_output=True, text=True)
    with open(out_file) as f:
        outputs = f.read()
    with open(sum_file) as f:
        summary = f.read()
    return p.returncode, p.stdout + p.stderr, outputs, summary


with tempfile.TemporaryDirectory() as tmp:
    # a fake curl that answers the one request the numbering step makes, and records what it was asked
    fake = os.path.join(tmp, "bin")
    os.makedirs(fake)
    curl = os.path.join(fake, "curl")
    with open(curl, "w") as f:
        f.write('#!/bin/sh\necho "$@" >> "$FAKE_CURL_LOG"\nprintf \'%s\' "$FAKE_CURL_BODY"\n[ "$FAKE_CURL_FAIL" = 1 ] && exit 22\nexit 0\n')
    os.chmod(curl, 0o755)

    for pipefail in (True, False):
        # ---- numbering
        work = os.path.join(tmp, "num%d" % pipefail)
        os.makedirs(work)
        shutil.copy(os.path.join(ROOT, "CMakeLists.txt"), os.path.join(work, "CMakeLists.txt"))
        original = open(os.path.join(work, "CMakeLists.txt")).read()
        env = {"RUN_NUMBER": "17", "FAKE_CURL_BODY": "2.876.0\n", "FAKE_CURL_FAIL": "0", "FAKE_CURL_LOG": os.path.join(work, "curl.log")}
        rc_, out_, outputs, _ = run_step("Number this test build", env, work, fake, pipefail)
        check(rc_ == 0, "numbering exits 0 (pipefail=%s): %s" % (pipefail, out_[-300:]))
        check("ver=2.876.0.17" in outputs and "base=2.876.0" in outputs, "numbering outputs ver and base: %r" % outputs)
        now = open(os.path.join(work, "CMakeLists.txt")).read()
        a, b = original.split("\n"), now.split("\n")
        changed = [(x, y) for x, y in zip(a, b) if x != y]
        check(len(a) == len(b) and len(changed) == 1, "numbering changes exactly one line of CMakeLists.txt: %r" % changed)
        m = re.search(r"^    VERSION (\d+\.\d+\.\d+)$", original, re.M)
        check(bool(m) and changed == [("    VERSION " + m.group(1), "    VERSION 2.876.0.17")], "the changed line is the project VERSION: %r" % changed)
        asked = open(os.path.join(work, "curl.log")).read()
        check("storage/v1/object/public/releases/VERSION.txt" in asked and "supabase.co" in asked, "numbering reads VERSION.txt from the update server (read only): %r" % asked)
        check(not re.search(r"-X|--data|-d |-T |--upload|POST|PUT|DELETE", asked), "numbering only reads: %r" % asked)
        # CMake accepts the result (a four-number project version) and bakes it in
        cm = shutil.which("cmake")
        if cm:
            probe = os.path.join(work, "probe")
            os.makedirs(probe)
            with open(os.path.join(probe, "CMakeLists.txt"), "w") as f:
                f.write("cmake_minimum_required(VERSION 3.16)\nproject(OkaySpaceGameEngine\n    VERSION 2.876.0.17\n    LANGUAGES NONE)\n"
                        'set(OKAY_VERSION_DEFINE OKAY_ENGINE_VERSION="${PROJECT_VERSION}")\nmessage(STATUS "DEFINE=${OKAY_VERSION_DEFINE}")\n')
            q = subprocess.run([cm, "-S", probe, "-B", os.path.join(probe, "b")], capture_output=True, text=True)
            check('DEFINE=OKAY_ENGINE_VERSION="2.876.0.17"' in q.stdout, "CMake bakes the four-number version into OKAY_ENGINE_VERSION: %s" % q.stdout[-200:])
        # the failures
        for label, extra in (("garbage from the server", {"FAKE_CURL_BODY": "<html>oops</html>"}), ("an unreachable server", {"FAKE_CURL_FAIL": "1", "FAKE_CURL_BODY": ""}),
                             ("a four-number release", {"FAKE_CURL_BODY": "2.876.0.5\n"}), ("an empty answer", {"FAKE_CURL_BODY": ""}),
                             ("a run number of 0", {"RUN_NUMBER": "0"}), ("a run number with letters", {"RUN_NUMBER": "17; rm -rf /"})):
            w2 = os.path.join(tmp, "numfail%d%s" % (pipefail, abs(hash(label))))
            os.makedirs(w2)
            shutil.copy(os.path.join(ROOT, "CMakeLists.txt"), os.path.join(w2, "CMakeLists.txt"))
            e2 = dict(env, FAKE_CURL_LOG=os.path.join(w2, "curl.log"), **extra)
            rc2, out2, outputs2, summary2 = run_step("Number this test build", e2, w2, fake, pipefail)
            check(rc2 != 0 and "ver=" not in outputs2, "numbering refuses %s" % label)
            check(open(os.path.join(w2, "CMakeLists.txt")).read() == original, "numbering leaves CMakeLists.txt alone for %s" % label)
        # a CMakeLists with no project VERSION line is refused, not published as an unnumbered build
        w3 = os.path.join(tmp, "numnover%d" % pipefail)
        os.makedirs(w3)
        with open(os.path.join(w3, "CMakeLists.txt"), "w") as f:
            f.write("project(X)\nset(OKAY_DEFAULT_ACCOUNT_URL \"https://abcdefghij.supabase.co\")\n")
        rc3, out3, outputs3, _ = run_step("Number this test build", dict(env, FAKE_CURL_LOG=os.path.join(w3, "c.log")), w3, fake, pipefail)
        check(rc3 != 0 and "ver=" not in outputs3, "numbering refuses a CMakeLists without a project VERSION")

    # ---- the branch and commit
    repo = os.path.join(tmp, "repo")
    os.makedirs(repo)
    subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
    subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@example.invalid", "commit", "-q", "--allow-empty", "-m", "x"], cwd=repo, check=True)
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo, capture_output=True, text=True).stdout.strip()
    for want, good in (("claude/walk-vs-run", True), ("refs/heads/main", True), ("v2.876.0", True), (head, True), ("a b", False), ("x\ny=1", False), ("a;b", False),
                       ("../x", False), ("", False), ("$(id)", False), ("a" * 81, False), ("-rf", True)):
        rc4, out4, outputs4, _ = run_step("Record the branch and commit", {"WANT_REF": want}, repo, None, True)
        if good:
            expect = want[len("refs/heads/"):] if want.startswith("refs/heads/") else want
            check(rc4 == 0 and ("branch=%s\n" % expect) in outputs4 and ("sha=%s\n" % head) in outputs4, "branch %r is recorded: %r %s" % (want, outputs4, out4[-200:]))
            check(outputs4.count("\n") == 2, "branch %r writes exactly two output lines (no injected output): %r" % (want, outputs4))
        else:
            check(rc4 != 0 and "branch=" not in outputs4 and "y=1" not in outputs4, "branch %r is refused and writes no output: %r" % (want, outputs4))

    # ---- the version inside the programs
    for ok_version in (True, False):
        w = os.path.join(tmp, "prog%d" % ok_version)
        os.makedirs(os.path.join(w, "build-win-dist", "Tools"))
        ver = "2.876.0.17"
        mark = ver if ok_version else "2.876.0"
        with open(os.path.join(w, "build-win-dist", "OkaySpace.exe"), "wb") as f:
            f.write(b"MZ\x90\x00 OkaySpace Launcher  v" + mark.encode() + b"\x00")
        with open(os.path.join(w, "build-win-dist", "Tools", "OkayEngine.exe"), "wb") as f:
            f.write(b"MZ\x90\x00 [v" + mark.encode() + b"]\x00")
        with open(os.path.join(w, "build-win-dist", "Tools", "OkaySpacePlayer.exe"), "wb") as f:
            f.write(b"MZ\x90\x00 player")
        for rel_ in ("build-win-dist/VERSION.txt", "build-win-dist/Tools/VERSION.txt"):
            with open(os.path.join(w, rel_), "w") as f:
                f.write("2.876.0\n")
        rc5, out5, _, _ = run_step("Check the programs carry the test version", {"VER": ver}, w, None, True)
        if ok_version:
            check(rc5 == 0, "a build that carries the version passes: %s" % out5[-200:])
            check(open(os.path.join(w, "build-win-dist", "VERSION.txt")).read() == ver + "\n" and open(os.path.join(w, "build-win-dist", "Tools", "VERSION.txt")).read() == ver + "\n",
                  "both VERSION.txt files say the test version")
        else:
            check(rc5 != 0 and "does not contain" in out5, "a launcher built without the version stops the run: %s" % out5[-200:])
    w = os.path.join(tmp, "notexe")
    os.makedirs(os.path.join(w, "build-win-dist", "Tools"))
    for rel_, data in (("OkaySpace.exe", b"MZ 2.876.0.17"), ("Tools/OkayEngine.exe", b"MZ 2.876.0.17"), ("Tools/OkaySpacePlayer.exe", b"ELF not windows")):
        with open(os.path.join(w, "build-win-dist", rel_), "wb") as f:
            f.write(data)
    rc6, out6, _, _ = run_step("Check the programs carry the test version", {"VER": "2.876.0.17"}, w, None, True)
    check(rc6 != 0 and "not a Windows program" in out6, "a program that is not a Windows exe stops the run")

    # ---- the guard and the key check
    w = os.path.join(tmp, "guard")
    os.makedirs(w)
    for ref, default, good in (("refs/heads/main", "main", True), ("refs/heads/claude/dev-channel", "main", False), ("refs/tags/v1", "main", False), ("refs/heads/main", "master", False)):
        rc7, out7, _, summary7 = run_step("Check this run was started from the default branch", {"RUN_REF": ref, "DEFAULT_BRANCH": default}, w, None, True)
        check((rc7 == 0) == good, "start-from-the-default-branch guard for %s on %s" % (ref, default))
        if not good:
            check("Start the test build from" in out7 and "Start the test build from" in summary7, "the guard says what to do, in the log and the summary")
    for have, good in (("true", True), ("false", False), ("", False)):
        rc8, out8, _, _ = run_step("Check the publish key exists", {"HAVE_KEY": have}, w, None, True)
        check((rc8 == 0) == good, "key check for HAVE_KEY=%r" % have)
        if not good:
            check("SUPABASE_SERVICE_KEY" in out8 and "cannot be published" in out8, "the missing-key stop says why")

    # ---- the YAML run scripts are valid bash (the copied ones too)
    for name, st in {**bsteps, **psteps}.items():
        if "run" in st:
            p = subprocess.run(["bash", "-n"], input=st["run"], capture_output=True, text=True)
            check(p.returncode == 0, "step %r is not valid bash: %s" % (name, p.stderr.strip()))

for f in fails:
    sys.stderr.write("FAIL: %s\n" % f)
print("dev_workflow: %s (%d failures)" % ("FAILED" if fails else "ok", len(fails)))
sys.exit(1 if fails else 0)
