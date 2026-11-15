#!/usr/bin/env python3
"""Unit tests for the collaboration scripts (collab_lib, find_existing, collision_check, branch_lint, bug_patterns,
check_pr_body). They build tiny throw-away git repositories, so they do not depend on the real repo's branches or on
the feature map. Run: python3 scripts/tests/test_collab.py"""
import json, os, shutil, subprocess, sys, tempfile, unittest, datetime

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.dirname(HERE)
ROOT = os.path.dirname(SCRIPTS)
sys.path.insert(0, SCRIPTS)
import collab_lib as L, collision_check as CC, find_existing as FE, branch_lint as BL, bug_patterns as BP, check_pr_body as PB


def sh(cwd, *a):
    subprocess.run(["git", "-C", cwd, "-c", "user.name=t", "-c", "user.email=t@t", *a], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


class Repo:
    """A temp repo whose refs/remotes/origin/* stand in for GitHub branches."""
    def __init__(self):
        self.d = tempfile.mkdtemp(prefix="collabtest")
        sh(self.d, "init", "-q", "-b", "main")
        os.makedirs(os.path.join(self.d, "docs"))
        shutil.copy(os.path.join(ROOT, "docs", "lanes.json"), os.path.join(self.d, "docs", "lanes.json"))
        self.write("engine/src/Components/Foo.cpp", "\n".join(f"line {i}" for i in range(1, 60)) + "\n")
        self.write("tests/CMakeLists.txt", "add_executable(test_foo test_foo.cpp)\nadd_test(NAME foo COMMAND test_foo)\n")
        self.write("docs/feature_map/04-characters-cameras.md", "# Page\n\n## 4. Characters\n\n### 4.5 Vehicles\n- **What:** Arcade car with a seat.\n- **Code:** `Components/VehicleSeat`\n- **Status:** Works\n- **Words:** car\n")
        self.write("engine/include/okay/Components/VehicleSeat.hpp", "class VehicleSeat { };\n")
        self.commit("init")
        sh(self.d, "update-ref", "refs/remotes/origin/main", "HEAD")

    def write(self, rel, text):
        p = os.path.join(self.d, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w") as fh: fh.write(text)

    def commit(self, msg):
        sh(self.d, "add", "-A"); sh(self.d, "commit", "-q", "-m", msg)

    def branch(self, name, files, msg="work"):
        sh(self.d, "checkout", "-q", "-b", "tmp-" + name.replace("/", "-"), "origin/main")
        for rel, text in files.items(): self.write(rel, text)
        self.commit(msg)
        sh(self.d, "update-ref", "refs/remotes/origin/" + name, "HEAD")
        sh(self.d, "checkout", "-q", "main")

    def edited(self, line_no, text):
        with open(os.path.join(self.d, "engine/src/Components/Foo.cpp")) as fh: lines = fh.read().splitlines()
        lines[line_no - 1] = text
        return "\n".join(lines) + "\n"

    def close(self): shutil.rmtree(self.d, ignore_errors=True)


class Lib(unittest.TestCase):
    def test_globs(self):
        self.assertTrue(L.glob_match("engine/src/UI/a/b.cpp", "engine/src/UI/**"))
        self.assertTrue(L.glob_match("a/b.hpp", "**/*.hpp"))
        self.assertFalse(L.glob_match("editor/a/b.cpp", "editor/*.cpp"))
        self.assertTrue(L.globs_overlap("engine/**", "engine/src/X.cpp"))
        self.assertFalse(L.globs_overlap("editor/*.cpp", "editor/Foo.hpp", ["editor/Foo.hpp", "editor/a.cpp"]))

    def test_lanes(self):
        lanes = L.load_lanes(ROOT)
        self.assertEqual(L.owner_of("engine/src/Scene/SceneSerializer.cpp", lanes), "shared")
        self.assertEqual(L.owner_of("engine/include/okay/Components/VehicleSeat.hpp", lanes), "coordinator")   # unfinished features (owner 2026-11-15)
        self.assertEqual(L.owner_of("engine/src/Crypto/Sha512.cpp", lanes), "claude2")   # third-party replacement (owner 2026-11-15)
        self.assertEqual(L.owner_of("engine/src/Render/ObjReader.cpp", lanes), "claude2")
        self.assertEqual(L.owner_of("okayui/src/OkayUI.cpp", lanes), "claude2")
        self.assertEqual(L.owner_of("editor/main.cpp", lanes), "coordinator")
        self.assertEqual(L.owner_of("scripts/work_board.py", lanes), "coordinator")
        self.assertEqual(L.owner_of("engine/include/okay/Core/Time.hpp", lanes), "open")
        self.assertEqual(L.lane_of_branch("claude2/x"), "claude2"); self.assertEqual(L.lane_of_branch("claude/x"), "coordinator")
        self.assertIsNone(L.lane_of_branch("main"))

    def test_claims_one_line_multi_line_release_agree(self):
        c = [{"created_at": "2026-11-15T10:00:00Z", "body": "Claim: `claude2/a`\nExisting: tween nodes / Gap: no easing / Files: engine/src/UI/**, editor/Foo.cpp / Test-first: tween_ease / Try-it: open scene"},
             {"created_at": "2026-11-15T11:00:00Z", "body": "**CLAIM: claude2/b**\nExisting: x\nGap: y\nFiles: docs/a.md\nTest-first: t\nTry-it: s"},
             {"created_at": "2026-11-15T12:00:00Z", "body": "Release: claude2/b"},
             {"created_at": "2026-11-15T13:00:00Z", "body": "Agree: claude2/a"}]
        claims, _ = L.parse_claims(c)
        self.assertEqual(sorted(claims), ["claude2/a"])
        kc, _ = L.parse_claims([{"created_at": "2026-11-15T10:00:00Z", "body": "Claim: claude2/fix-x\nKind: bug\nExisting: e / Gap: g / Files: engine/src/Scene/Foo.cpp / Test-first: t / Try-it: s"}])
        self.assertEqual(kc["claude2/fix-x"]["kind"], "bug")
        a = claims["claude2/a"]
        self.assertEqual(a["files"], ["engine/src/UI/**", "editor/Foo.cpp"])
        self.assertEqual(a["test_first"], "tween_ease"); self.assertTrue(a["agreed"])

    def test_claim_expiry(self):
        c = {"created_at": "2026-11-15T10:00:00Z"}
        now = datetime.datetime(2026, 11, 15, 20, 0, tzinfo=datetime.timezone.utc)
        self.assertEqual(L.claim_status(c, None, now), "active")
        self.assertEqual(L.claim_status(c, None, now + datetime.timedelta(hours=20)), "stale")
        push = now + datetime.timedelta(hours=18)
        self.assertEqual(L.claim_status(c, push, now + datetime.timedelta(hours=20)), "active")

    def test_claim_rules(self):
        lanes = L.load_lanes(ROOT)
        c = {"branch": "claude2/x", "has_fields": True, "existing": "e", "gap": "g", "files": ["scripts/foo.py"], "test_first": "t", "try_it": "s", "agreed": False}
        probs = L.check_claim_rules(c, lanes, [])
        self.assertTrue(any("coordinator lane" in p for p in probs))
        c["agreed"] = True
        self.assertEqual(L.check_claim_rules(c, lanes, []), [])
        # a bug-fix claim with a Test-first line may touch the other lane's files (owner 2026-11-15)
        bug = dict(c, agreed=False, kind="bug", files=["scripts/foo.py"])
        self.assertEqual(L.check_claim_rules(bug, lanes, []), [])
        self.assertTrue(any("coordinator lane" in p for p in L.check_claim_rules(dict(bug, kind="feature"), lanes, [])))
        self.assertTrue(any("coordinator lane" in p or "Test-first" in p for p in L.check_claim_rules(dict(bug, test_first=""), lanes, [])))
        other = {"branch": "claude2/y", "files": ["engine/src/Components/**"]}
        c2 = dict(c, files=["engine/src/Components/Foo.cpp"], agreed=False)
        self.assertTrue(any("overlaps claude2/y" in p for p in L.check_claim_rules(c2, lanes, [other])))

    # ---- append-only shared files ("append_only" in docs/lanes.json) ----

    def test_append_only_list(self):
        lanes = L.load_lanes(ROOT)
        for p in ("docs/try_it.md", "docs/UNFINISHED.md", "docs/unfinished_engine.md", "docs/FEATURE_MAP.md", "docs/README.md",
                  "docs/feature_map/04-characters-cameras.md", "docs/feature_map/_index_az.md", "tests/CMakeLists.txt", "ROADMAP.md",
                  "dist/CHANGELOG.txt"):
            self.assertTrue(L.is_append_only(p, lanes), p)
        for p in ("engine/src/Scene/SceneSerializer.cpp", "engine/CMakeLists.txt", "CMakeLists.txt", "CLAUDE.md", "docs/collaboration.md",
                  "docs/lanes.json", "tests/test_foo.cpp", "scripts/collab_lib.py", "dist/versions.json", "docs/scripting.md"):
            self.assertFalse(L.is_append_only(p, lanes), p)
        # a claim glob counts when it lies inside the list, not when it is wider than the list
        self.assertTrue(L.is_append_only("docs/feature_map/**", lanes))
        self.assertFalse(L.is_append_only("docs/**", lanes)); self.assertFalse(L.is_append_only("tests/*", lanes))

    def claim_dict(self, branch, files, **kw):
        return dict({"branch": branch, "has_fields": True, "existing": "e", "gap": "g", "files": files, "test_first": "t", "try_it": "s", "agreed": False}, **kw)

    def test_claim_rules_ignore_append_only_files(self):
        lanes = L.load_lanes(ROOT)
        names = ["docs/try_it.md", "tests/CMakeLists.txt", "dist/CHANGELOG.txt", "docs/feature_map/04-characters-cameras.md"]
        mine = self.claim_dict("claude2/x", names)          # dist/ is the coordinator lane, but the file is append-only
        other = self.claim_dict("claude/y", names + ["docs/feature_map/**"])
        self.assertEqual(L.check_claim_rules(mine, lanes, [other]), [])
        self.assertEqual(L.check_claim_rules(other, lanes, [mine]), [])
        # a real code file next to them is still checked, and it is the one that gets reported (an "open" file, so no lane rule applies)
        both_a = self.claim_dict("claude2/x", ["docs/try_it.md", "engine/include/okay/Core/Time.hpp"])
        both_b = self.claim_dict("claude2/y", ["docs/try_it.md", "engine/include/okay/Core/Time.hpp"])
        probs = L.check_claim_rules(both_a, lanes, [both_b])
        self.assertEqual(len(probs), 1); self.assertIn("engine/include/okay/Core/Time.hpp", probs[0]); self.assertNotIn("try_it", probs[0])
        # a glob wider than the list is not exempt
        wide = self.claim_dict("claude2/z", ["docs/**"])
        self.assertTrue(any("overlaps" in p for p in L.check_claim_rules(wide, lanes, [self.claim_dict("claude2/w", ["docs/**"])])))

    def test_claim_rules_keep_real_shared_files(self):
        lanes = L.load_lanes(ROOT)
        a = self.claim_dict("claude2/a", ["engine/src/Scene/SceneSerializer.cpp"])
        b = self.claim_dict("claude/b", ["engine/src/Scene/SceneSerializer.cpp"])
        self.assertTrue(any("overlaps claude/b" in p for p in L.check_claim_rules(a, lanes, [b])))
        self.assertTrue(any("overlaps claude2/a" in p for p in L.check_claim_rules(b, lanes, [a])))
        # a claim of the other lane's code is still refused (append-only files next to it do not help)
        c = self.claim_dict("claude2/c", ["docs/try_it.md", "scripts/foo.py"])
        self.assertTrue(any("coordinator lane" in p for p in L.check_claim_rules(c, lanes, [])))

    # ---- a bare "Agree: <branch>" ----

    @staticmethod
    def thread(*texts):
        return [{"created_at": f"2026-11-15T{10 + i:02d}:00:00Z", "body": t} for i, t in enumerate(texts)]

    CLAIM_X = "Claim: claude/x\nExisting: e\nGap: g\nFiles: scripts/a.py\nTest-first: t\nTry-it: s"

    def test_bare_agree_counts_without_a_claim(self):
        claims, events = L.parse_claims(self.thread("Agree: claude/some-branch"))
        self.assertEqual(claims, {})
        self.assertEqual(L.agreed_branches(events), {"claude/some-branch"})
        for text in ("**Agree:** claude/x", "agree: `claude/x`.", "Agree: origin/claude/x", "> Agree: claude/x"):
            self.assertEqual(L.agreed_branches(L.parse_claims(self.thread(text))[1]), {"claude/x"}, text)
        self.assertEqual(L.agreed_branches(L.parse_claims(self.thread("Agree: claude/a\nAgree: claude/b"))[1]), {"claude/a", "claude/b"})

    def test_agree_before_or_after_the_claim_and_list_order(self):
        for texts in ((self.CLAIM_X, "Agree: claude/x"), ("Agree: claude/x", self.CLAIM_X)):
            for comments in (self.thread(*texts), self.thread(*texts)[::-1]):   # the comment times decide, not the list order
                claims, events = L.parse_claims(comments)
                self.assertTrue(claims["claude/x"]["agreed"], texts)
                self.assertEqual(L.agreed_branches(events), {"claude/x"})
        claims, _ = L.parse_claims(self.thread(self.CLAIM_X, "Agree: claude/other"))
        self.assertFalse(claims["claude/x"]["agreed"])

    def test_release_clears_the_agreement_too(self):
        claims, events = L.parse_claims(self.thread(self.CLAIM_X, "Agree: claude/x", "Release: claude/x"))
        self.assertEqual(claims, {}); self.assertEqual(L.agreed_branches(events), set())
        # the same without a Claim, and with the Agree first
        self.assertEqual(L.agreed_branches(L.parse_claims(self.thread("Agree: claude/x", "Release: claude/x"))[1]), set())
        # a new Claim after the Release starts without the old agreement
        claims, _ = L.parse_claims(self.thread("Agree: claude/x", "Release: claude/x", self.CLAIM_X))
        self.assertFalse(claims["claude/x"]["agreed"])
        claims, _ = L.parse_claims(self.thread(self.CLAIM_X, "Agree: claude/x", "Release: claude/x", self.CLAIM_X))
        self.assertFalse(claims["claude/x"]["agreed"])
        # an Agree after the Release counts again, with the Claim before or after it
        claims, _ = L.parse_claims(self.thread("Release: claude/x", "Agree: claude/x", self.CLAIM_X))
        self.assertTrue(claims["claude/x"]["agreed"])

    def test_agree_is_honoured_by_check_claim_rules(self):
        lanes = L.load_lanes(ROOT)
        claim = "Claim: claude2/x\nExisting: e\nGap: g\nFiles: scripts/foo.py\nTest-first: t\nTry-it: s"   # scripts/ is the coordinator lane
        claims, _ = L.parse_claims(self.thread(claim))
        self.assertTrue(any("coordinator lane" in p for p in L.check_claim_rules(claims["claude2/x"], lanes, [])))
        claims, _ = L.parse_claims(self.thread("Agree: claude2/x", claim))      # a bare Agree posted before the Claim
        self.assertEqual(L.check_claim_rules(claims["claude2/x"], lanes, []), [])
        claims, _ = L.parse_claims(self.thread("Agree: claude2/x", "Release: claude2/x", claim))
        self.assertTrue(any("coordinator lane" in p for p in L.check_claim_rules(claims["claude2/x"], lanes, [])))


class Find(unittest.TestCase):
    def test_tokens(self):
        self.assertEqual(FE.tokens_of(["VehicleSeat"]), ["vehicle", "seat"])
        self.assertEqual(FE.tokens_of(["vehicle seat", "the"]), ["vehicle", "seat"])
        self.assertTrue(FE.all_in("uses SHA-256 here", ["sha256"]))

    def test_finds_map_code_and_branch(self):
        r = Repo()
        try:
            r.branch("claude2/vehicle-seat-2", {"engine/include/okay/Components/Seat2.hpp": "class Seat2 {};\n"})
            rep = FE.build(r.d, ["vehicle", "seat"], offline=True)
            self.assertTrue(any(f["id"] == "4.5" for f in rep["feature_map"]))
            self.assertTrue(any(e["name"] == "VehicleSeat" for e in rep["identifiers"]))
            self.assertTrue(any(b["branch"] == "claude2/vehicle-seat-2" for b in rep["branches"]))
            text = FE.render(rep)
            self.assertIn("Existing:", text); self.assertIn("VehicleSeat", text)
            self.assertEqual(FE.build(r.d, ["zzzunknown"], offline=True)["feature_map"], [])
        finally:
            r.close()


class Collisions(unittest.TestCase):
    def setUp(self):
        self.r = Repo()
        self.now = datetime.datetime.now(datetime.timezone.utc)

    def tearDown(self): self.r.close()

    def rep(self, comments=None, **kw):
        return CC.build_report(self.r.d, comments or [], [], use_disk_cache=False, now=self.now, **kw)

    def test_same_lines_same_lane_and_cross_lane(self):
        r = self.r
        r.branch("claude2/a", {"engine/src/Components/Foo.cpp": r.edited(10, "from a")})
        r.branch("claude2/b", {"engine/src/Components/Foo.cpp": r.edited(10, "from b")})
        r.branch("claude/c", {"engine/src/Components/Foo.cpp": r.edited(40, "from c")})
        rep = self.rep()
        pair = next(o for o in rep["overlaps"] if {o["a"], o["b"]} == {"claude2/a", "claude2/b"})
        self.assertEqual(pair["severity"], "high"); self.assertTrue(pair["line_overlaps"]); self.assertFalse(pair["cross_lane"])
        cross = next(o for o in rep["overlaps"] if {o["a"], o["b"]} == {"claude2/a", "claude/c"})
        self.assertTrue(cross["cross_lane"]); self.assertEqual(cross["line_overlaps"], [])

    def test_shared_generated_and_duplicate_test(self):
        r = self.r
        r.branch("claude2/a", {"docs/FEATURE_MAP.md": "x\n", "tests/CMakeLists.txt": "add_executable(test_foo2 t.cpp)\nadd_test(NAME foo2 COMMAND test_foo2)\n", "tests/test_foo2.cpp": "int main(){}\n"})
        r.branch("claude2/b", {"docs/FEATURE_MAP.md": "y\n", "tests/CMakeLists.txt": "add_executable(test_foo2 t.cpp)\nadd_test(NAME foo2 COMMAND test_foo2)\n", "tests/test_foo2.cpp": "int main(){return 1;}\n"})
        rep = self.rep()
        self.assertFalse([f for o in rep["overlaps"] for f in o["files"] if f == "docs/FEATURE_MAP.md"])  # shared files are not pair overlaps
        self.assertTrue(any(s["file"] == "docs/FEATURE_MAP.md" and s["generated"] for s in rep["shared_files"]))
        kinds = {(d["kind"], d["name"]) for d in rep["duplicate_tests"]}
        self.assertIn(("test", "foo2"), kinds); self.assertIn(("target", "test_foo2"), kinds); self.assertIn(("file", "tests/test_foo2.cpp"), kinds)
        self.assertEqual({g["file"] for g in rep["generated_by_hand"]}, {"docs/FEATURE_MAP.md"})

    def test_out_of_lane_unclaimed_stacked(self):
        r = self.r
        r.branch("claude2/a", {"scripts/x.py": "print(1)\n"})
        rep = self.rep()
        self.assertEqual(rep["out_of_lane"][0]["branch"], "claude2/a")
        self.assertEqual([u["branch"] for u in rep["unclaimed"]], ["claude2/a"])
        # stacked: b is built on a, so no overlap
        sh(r.d, "checkout", "-q", "-b", "tmp-stack", "origin/claude2/a")
        r.write("scripts/x.py", "print(2)\n"); r.commit("more"); sh(r.d, "update-ref", "refs/remotes/origin/claude2/b", "HEAD"); sh(r.d, "checkout", "-q", "main")
        self.assertEqual(self.rep()["overlaps"], [])

    def test_claims_stale_and_conflict(self):
        r = self.r
        r.branch("claude/mine", {"engine/src/Components/Foo.cpp": r.edited(5, "x")})
        old = (self.now - datetime.timedelta(hours=1)).strftime("%Y-%m-%dT%H:%M:%SZ")
        ancient = (self.now - datetime.timedelta(hours=60)).strftime("%Y-%m-%dT%H:%M:%SZ")
        body = "Claim: claude2/theirs\nExisting: a / Gap: b / Files: engine/src/Components/** / Test-first: t / Try-it: s"
        rep = self.rep([{"body": body, "created_at": old}])
        self.assertEqual(rep["claims"][0]["status"], "active")
        self.assertTrue(rep["claim_conflicts"][0]["cross_lane"])
        self.assertTrue(CC.fails_for_branch(rep, "claude/mine"))
        rep2 = self.rep([{"body": body, "created_at": old}, {"body": "Agree: claude2/theirs", "created_at": old}])
        self.assertTrue(rep2["claims"][0]["agreed"])
        rep3 = self.rep([{"body": body.replace("claude2/theirs", "claude2/old"), "created_at": ancient}])
        self.assertEqual(rep3["claims"][0]["status"], "stale"); self.assertEqual(rep3["claim_conflicts"], [])

    # ---- append-only shared files and a bare Agree (docs/collaboration.md; PRs #913 and #917 were refused for these) ----

    APPEND_ONLY = {"docs/try_it.md": "# try\nrow\n", "docs/UNFINISHED.md": "| row |\n", "ROADMAP.md": "| row |\n", "dist/CHANGELOG.txt": "1.0\n",
                   "tests/CMakeLists.txt": "add_executable(test_foo test_foo.cpp)\nadd_test(NAME foo COMMAND test_foo)\nadd_test(NAME mine COMMAND test_foo)\n",
                   "docs/feature_map/04-characters-cameras.md": "# Page\nmore\n"}
    CLAIM_FILES = "engine/src/Components/Foo.cpp, docs/try_it.md, docs/UNFINISHED.md, ROADMAP.md, dist/CHANGELOG.txt, tests/CMakeLists.txt, docs/feature_map/**"

    def ago(self, minutes): return (self.now - datetime.timedelta(minutes=minutes)).strftime("%Y-%m-%dT%H:%M:%SZ")

    def claim(self, branch, files, minutes=60):
        return {"body": f"Claim: {branch}\nExisting: a / Gap: b / Files: {files} / Test-first: t / Try-it: s", "created_at": self.ago(minutes)}

    def say(self, text, minutes): return {"body": text, "created_at": self.ago(minutes)}

    def mine(self, with_code=False, **more):
        """claude/mine (the coordinator lane) adds its own script and a row to every append-only file; with_code also edits Foo.cpp."""
        files = dict(self.APPEND_ONLY, **{"scripts/own_tool.py": "print(1)\n"}, **more)
        if with_code: files["engine/src/Components/Foo.cpp"] = self.r.edited(5, "mine")
        self.r.branch("claude/mine", files)

    def test_append_only_files_never_fail_a_branch(self):
        self.mine()
        for focus in (None, "claude/mine"):                    # the full report and the quick one branch-lint runs
            rep = self.rep([self.claim("claude2/own-step0-visible", self.CLAIM_FILES)], only_branch=focus)
            self.assertEqual(rep["claim_conflicts"], [], focus)
            self.assertEqual(CC.fails_for_branch(rep, "claude/mine"), [], focus)

    def test_real_code_file_in_the_claim_still_fails_and_is_the_only_one_named(self):
        self.mine(with_code=True)
        for focus in (None, "claude/mine"):
            rep = self.rep([self.claim("claude2/theirs", self.CLAIM_FILES)], only_branch=focus)
            self.assertEqual([(c["files"], c["count"], c["cross_lane"]) for c in rep["claim_conflicts"]], [(["engine/src/Components/Foo.cpp"], 1, True)], focus)
            msgs = CC.fails_for_branch(rep, "claude/mine")
            self.assertEqual(len(msgs), 1, focus)
            self.assertIn("engine/src/Components/Foo.cpp", msgs[0]); self.assertNotIn("try_it", msgs[0]); self.assertNotIn("ROADMAP", msgs[0])

    def test_real_shared_file_is_still_protected(self):
        self.r.branch("claude/mine", {"engine/src/Scene/SceneSerializer.cpp": "x\n", "docs/try_it.md": "y\n"})
        rep = self.rep([self.claim("claude2/theirs", "engine/src/Scene/SceneSerializer.cpp, docs/try_it.md")])
        self.assertEqual([c["files"] for c in rep["claim_conflicts"]], [["engine/src/Scene/SceneSerializer.cpp"]])
        self.assertEqual(len(CC.fails_for_branch(rep, "claude/mine")), 1)

    def test_claim_naming_only_append_only_files_is_not_refused_and_blocks_nobody(self):
        self.mine()
        rep = self.rep([self.claim("claude2/docs-only", "docs/try_it.md, tests/CMakeLists.txt, dist/CHANGELOG.txt", 90),
                        self.claim("claude/also", "docs/try_it.md, tests/CMakeLists.txt, ROADMAP.md", 30)])
        self.assertEqual({c["branch"]: c["problems"] for c in rep["claims"]}, {"claude2/docs-only": [], "claude/also": []})
        self.assertEqual(rep["claim_overlaps"], [])
        self.assertEqual(rep["claim_conflicts"], [])
        self.assertEqual(CC.fails_for_branch(rep, "claude/mine"), [])

    def test_append_only_files_are_still_shown_as_information(self):
        self.mine()
        self.r.branch("claude2/b", {"docs/try_it.md": "# try\nother row\n", "dist/CHANGELOG.txt": "2.0\n"})
        rep = self.rep([self.claim("claude2/theirs", self.CLAIM_FILES)])
        note = next(n for n in rep["append_only_notes"] if n["branch"] == "claude/mine")
        self.assertEqual(note["claim"], "claude2/theirs"); self.assertIn("docs/try_it.md", note["files"])
        shared = {s["file"]: s for s in rep["shared_files"]}
        self.assertTrue(shared["docs/try_it.md"]["append_only"]); self.assertTrue(shared["dist/CHANGELOG.txt"]["append_only"])
        self.assertEqual(rep["out_of_lane"], [])                # claude2/b's CHANGELOG line is not "outside its lane"
        md = CC.to_markdown(rep)
        self.assertIn("append-only", md); self.assertIn("`docs/try_it.md`", md); self.assertIn("claude2/theirs", md)
        self.assertNotIn("handshake needed", md)                # only append-only files are shared here, so no handshake table
        json.dumps(rep, default=str)

    def test_bare_agree_counts_with_no_claim_and_before_or_after_the_claim(self):
        self.r.branch("claude/mine", {"engine/src/Components/Foo.cpp": self.r.edited(5, "mine")})
        theirs, mine_claim = self.claim("claude2/theirs", "engine/src/Components/**", 120), self.claim("claude/mine", "engine/src/Components/Foo.cpp", 30)
        cases = {"no Claim at all": [theirs, self.say("Agree: claude/mine", 20)],
                 "Agree after its Claim": [theirs, mine_claim, self.say("Agree: claude/mine", 20)],
                 "Agree before its Claim": [theirs, self.say("Agree: claude/mine", 50), mine_claim],
                 "Agree before everything": [self.say("**Agree:** `claude/mine`", 200), theirs, mine_claim]}
        for name, comments in cases.items():
            for focus in (None, "claude/mine"):
                with self.subTest(name, focus=focus):
                    rep = self.rep(comments, only_branch=focus)
                    self.assertEqual(CC.fails_for_branch(rep, "claude/mine"), [])
                    self.assertEqual([(c["cross_lane"], c["agreed"]) for c in rep["claim_conflicts"]], [(False, True)])
                    self.assertEqual(rep["agreed"], ["claude/mine"])
                    self.assertTrue(all(c["problems"] == [] for c in rep["claims"] if c["branch"] == "claude/mine"))
        # an Agree for another branch does not help this one, and the board line names who is agreed
        rep = self.rep([theirs, self.say("Agree: claude/other", 20)])
        self.assertEqual(len(CC.fails_for_branch(rep, "claude/mine")), 1)
        rep = self.rep(cases["no Claim at all"])
        self.assertIn("`claude/mine`", CC.claims_markdown(rep)); self.assertIn("agreed", CC.to_markdown(rep))

    def test_release_clears_the_agreement_in_the_report(self):
        self.r.branch("claude/mine", {"engine/src/Components/Foo.cpp": self.r.edited(5, "mine")})
        theirs = self.claim("claude2/theirs", "engine/src/Components/**", 120)
        agree, release = self.say("Agree: claude/mine", 50), self.say("Release: claude/mine", 40)
        rep = self.rep([theirs, agree, release])
        self.assertEqual(len(CC.fails_for_branch(rep, "claude/mine")), 1); self.assertEqual(rep["agreed"], [])
        rep = self.rep([theirs, agree, release, self.say("Agree: claude/mine", 10)])        # a newer Agree counts again
        self.assertEqual(CC.fails_for_branch(rep, "claude/mine"), [])

    def test_markdown_and_json(self):
        self.r.branch("claude2/a", {"engine/src/Components/Foo.cpp": self.r.edited(10, "a")})
        rep = self.rep()
        self.assertIn("Collision report", CC.to_markdown(rep)); self.assertIn("Claims", CC.claims_markdown(rep))
        json.dumps(rep, default=str)


class Lint(unittest.TestCase):
    def setUp(self): self.r = Repo()
    def tearDown(self): self.r.close()

    def lint(self, files, fn):
        self.r.branch("claude2/l", files)
        return fn(self.r.d, "origin/main", "origin/claude2/l")

    def test_mutex_in_header_is_error(self):
        f = self.lint({"engine/include/okay/Core/X.hpp": "#include <mutex>\ninline std::mutex& M(){ static std::mutex m; return m; }\n"}, BL.check_includes)
        self.assertTrue(f and all(x["sev"] == "error" for x in f))
        self.assertEqual({x["line"] for x in f}, {1, 2})

    def test_spinlock_atomics_and_tests_are_fine(self):
        f = self.lint({"engine/include/okay/Core/X.hpp": "#include <atomic>\n#include \"okay/Debug/DebugUtil.hpp\"\ndebugutil::SpinLock l;\nstd::atomic<int> a;\n",
                       "tests/test_x.cpp": "#include <thread>\nstd::thread t;\n"}, BL.check_includes)
        self.assertEqual(f, [])

    def test_guarded_is_warning_and_allow_marker(self):
        f = self.lint({"engine/src/Core/X.cpp": "#if !defined(_WIN32)\n#include <thread>\n#endif\n#include <mutex>  // lint-allow: win32-thread\n"}, BL.check_includes)
        self.assertEqual([x["sev"] for x in f], ["warning"])

    def test_secrets(self):
        f = self.lint({"editor/a.cpp": 'const char* k = "ghp_' + "a" * 36 + '";\n// -----BEGIN RSA ' + 'PRIVATE KEY-----\nconst char* apiKey = "' + "Ab1" * 14 + '";\n',
                       "tests/t.cpp": 'const char* key = "' + "ab" * 32 + '";\n'}, BL.check_secrets)
        self.assertEqual(sorted(x["line"] for x in f if x["file"] == "editor/a.cpp"), [1, 2, 3])
        self.assertFalse([x for x in f if x["file"] == "tests/t.cpp"])

    def test_docs_warning(self):
        self.assertEqual(len(self.lint({"engine/src/Components/Y.cpp": "int y;\n"}, BL.check_docs)), 1)

    def test_docs_present_ok(self):
        self.assertEqual(self.lint({"engine/src/Components/Y.cpp": "int y;\n", "docs/y.md": "# y\n"}, BL.check_docs), [])


class Patterns(unittest.TestCase):
    def scan(self, code, path="engine/src/X.cpp"):
        return [(rid, ln) for _, ln, rid, _ in BP.scan_file(path, code.splitlines(), None)]

    def test_int_cast(self):
        self.assertEqual(self.scan("int a = (int)strtol(s, 0, 10);"), [("int-cast", 1)])
        self.assertEqual(self.scan("int a = (int)std::min<long>(strtol(s,0,10), INT_MAX);"), [])
        self.assertEqual(self.scan("long n = strtol(s,0,10);\nint k = (int)n;"), [("int-cast", 2)])
        self.assertEqual(self.scan("long n = strtol(s,0,10);\nif (n > INT_MAX) return;\nint k = (int)n;"), [])

    def test_substr_loop(self):
        self.assertEqual(self.scan("while (x) {\n  t = t.substr(1);\n}"), [("substr-loop", 2)])
        self.assertEqual(self.scan("t = t.substr(1);"), [])
        self.assertEqual(self.scan("for (;;) {\n  a = s.substr(i, 3);\n}"), [])

    def test_loop_copy(self):
        self.assertEqual(self.scan("for (auto m : meshes) {}"), [("loop-copy", 1)])
        self.assertEqual(self.scan("for (const auto& m : meshes) {}"), [])
        self.assertEqual(self.scan("for (int i=0;i<n;i++) {\n  std::vector<int> v = items[i];\n}"), [("loop-copy", 2)])

    def test_alloc_before_check(self):
        self.assertEqual(self.scan("uint32_t n = r.ReadU32();\nv.resize(n);"), [("alloc-before-check", 2)])
        self.assertEqual(self.scan("uint32_t n = r.ReadU32();\nif (n > kMax) return;\nv.resize(n);"), [])

    def test_tests(self):
        self.assertEqual(self.scan("CHECK(true);\nCHECK(ms < 2000);\nCHECK(ms < 50);", "tests/t.cpp"), [("test-cannot-fail", 1), ("test-cannot-fail", 2)])

    def test_allow_marker(self):
        self.assertEqual(self.scan("int a = (int)strtol(s,0,10); // lint-allow: int-cast"), [])


class PrBody(unittest.TestCase):
    def test_cases(self):
        self.assertFalse(PB.check("## Summary\nx")[0])
        self.assertFalse(PB.check("## How to try it\n\n<!-- hint -->\n\n-\n\n## Verification\n- x")[0])
        self.assertTrue(PB.check("## How to try it\n1. Open the editor.\n2. Add a Seat; press E.\n")[0])
        self.assertTrue(PB.check("### How to try it\nNothing to run: read docs/x.md\n", ["docs/x.md"])[0])
        self.assertFalse(PB.check("## How to try it\nn/a\n", ["a.cpp"])[0])
        self.assertTrue(PB.check("## How to try it\nRead the guide.\n", ["docs/a.md"])[0])


if __name__ == "__main__":
    unittest.main(verbosity=1)
