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
        # third-party replacement: Claude 2 from 2026-11-15, handed over to the coordinator on 2026-11-22
        self.assertEqual(L.owner_of("engine/src/Crypto/Sha512.cpp", lanes), "coordinator")
        self.assertEqual(L.owner_of("engine/src/Render/ObjReader.cpp", lanes), "coordinator")
        self.assertEqual(L.owner_of("okayui/src/OkayUI.cpp", lanes), "coordinator")
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


    def test_registry_first_by_old_name(self):
        r = Repo()
        try:
            r.write("docs/feature_map/04-characters-cameras.md",
                    "# Page\n\n## 4. Characters\n\n### 4.5 Vehicles\n- **System:** Vehicles - Drivable cars.\n"
                    "  Home: `engine/include/okay/Components/VehicleSeat.hpp`. Entry: `engine/include/okay/Components/VehicleSeat.hpp`. "
                    "Also called: Classic Car, CarKit.\n- **What:** Arcade car with a seat.\n- **Code:** `Components/VehicleSeat`\n"
                    "- **Status:** Works\n- **Words:** car\n")
            hits = FE.registry_hits(r.d, FE.tokens_of(["classic car"]))
            self.assertEqual([(h["name"], h["matched"]) for h in hits], [("Vehicles", "Classic Car")])
            self.assertEqual(hits[0]["entry"], "engine/include/okay/Components/VehicleSeat.hpp")
            self.assertEqual(FE.registry_hits(r.d, FE.tokens_of(["CarKit"]))[0]["name"], "Vehicles")
            self.assertEqual(FE.registry_hits(r.d, FE.tokens_of(["zzzunknown"])), [])
            text = FE.render(FE.build(r.d, ["classic", "car"], offline=True))
            self.assertIn("## Systems (docs/SYSTEMS.md", text)
            self.assertIn("**Vehicles** (also called Classic Car)", text)
            self.assertIn("system Vehicles (engine/include/okay/Components/VehicleSeat.hpp)", text)
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

    def test_archived_branch_is_not_live(self):
        r = self.r
        body = "\n".join(f"int v{i} = compute_something_long({i}, alpha, beta, gamma, delta);" for i in range(30)) + "\n"
        r.branch("claude2/old", {"engine/src/Components/A.cpp": body})
        r.branch("claude/new", {"engine/src/Components/B.cpp": body})
        self.assertTrue(CC.fails_for_branch(self.rep(only_branch="claude/new"), "claude/new"))
        tip = subprocess.run(["git", "-C", r.d, "rev-parse", "origin/claude2/old"], stdout=subprocess.PIPE, text=True).stdout.strip()
        tree = subprocess.run(["git", "-C", r.d, "rev-parse", "origin/main^{tree}"], stdout=subprocess.PIPE, text=True).stdout.strip()
        arch = subprocess.run(["git", "-C", r.d, "-c", "user.name=t", "-c", "user.email=t@t", "commit-tree", tree, "-p", "origin/main", "-p", tip, "-m", "archive"],
                              stdout=subprocess.PIPE, text=True).stdout.strip()
        sh(r.d, "update-ref", "refs/remotes/origin/claude2/archive", arch)
        rep = self.rep(only_branch="claude/new")
        self.assertEqual(CC.fails_for_branch(rep, "claude/new"), [])
        self.assertNotIn("claude2/old", [o["a"] for o in rep["overlaps"]] + [o["b"] for o in rep["overlaps"]])

    def test_stacked_after_the_lower_branch_moved_on(self):
        """Bug found by CI on 2026-11-22 (#1131/#1134): b was built on an earlier commit of a; then a got more commits
        (main merged in). Neither tip is an ancestor of the other any more, but they still share unmerged commits:
        a stack, not a collision."""
        r = self.r
        r.branch("claude/a", {"engine/src/Components/Foo.cpp": r.edited(10, "from a")})
        sh(r.d, "checkout", "-q", "-b", "tmp-b", "origin/claude/a")
        r.write("engine/src/Components/Foo.cpp", r.edited(10, "from a") .replace("line 20", "b line 20")); r.commit("b on a")
        sh(r.d, "update-ref", "refs/remotes/origin/claude/b", "HEAD")
        sh(r.d, "checkout", "-q", "-b", "tmp-a2", "origin/claude/a"); r.write("docs/x.md", "more\n"); r.commit("a moves on")
        sh(r.d, "update-ref", "refs/remotes/origin/claude/a", "HEAD"); sh(r.d, "checkout", "-q", "main")
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

    def test_markdown_and_json(self):
        self.r.branch("claude2/a", {"engine/src/Components/Foo.cpp": self.r.edited(10, "a")})
        rep = self.rep()
        self.assertIn("Collision report", CC.to_markdown(rep)); self.assertIn("Claims", CC.claims_markdown(rep))
        json.dumps(rep, default=str)


class ThreeLanes(unittest.TestCase):
    """Three Claudes (owner 2026-11-22): coordinator claude/*, Claude 2 claude2/*, Claude 3 claude3/*. Lanes come from docs/lanes.json."""
    def setUp(self):
        self.lanes = L.load_lanes(ROOT)
        self.now = datetime.datetime.now(datetime.timezone.utc)

    def claim(self, branch, files, **kw):
        return dict({"branch": branch, "has_fields": True, "existing": "e", "gap": "g", "files": files, "test_first": "t", "try_it": "s", "agreed": False}, **kw)

    def test_lanes_come_from_the_file(self):
        self.assertEqual(L.lane_prefixes(self.lanes), {"coordinator": "claude/", "claude2": "claude2/", "claude3": "claude3/", "grok": "grok/"})   # grok: #1190
        self.assertEqual(L.lane_of_branch("claude3/world-streaming", self.lanes), "claude3")
        self.assertEqual(L.lane_of_branch("origin/claude3/x"), "claude3")
        self.assertEqual(L.lane_of_branch("claude2/x"), "claude2"); self.assertEqual(L.lane_of_branch("claude/x"), "coordinator")
        self.assertIsNone(L.lane_of_branch("claude4/x")); self.assertIsNone(L.lane_of_branch("john/x"))
        # a fourth lane added to the data is picked up with no code change
        more = json.loads(json.dumps(self.lanes)); more["lanes"]["claude4"] = {"prefix": "claude4/", "name": "Claude 4", "description": "test"}
        self.assertEqual(L.lane_of_branch("claude4/x", more), "claude4")

    def test_lane_split(self):
        o = lambda p: L.owner_of(p, self.lanes)
        self.assertEqual(o("engine/src/Physics/Physics3D.cpp"), "claude2")
        self.assertEqual(o("engine/include/okay/Components/Ragdoll.hpp"), "claude2")
        self.assertEqual(o("engine/include/okay/Components/Joint3D.hpp"), "claude2")
        self.assertEqual(o("engine/src/Core/BuildPipeline.cpp"), "claude3")
        self.assertEqual(o("editor/BuildSettingsWindow.cpp"), "claude3")
        self.assertEqual(o("engine/include/okay/Components/WorldStreamer.hpp"), "claude3")
        self.assertEqual(o("engine/src/Optimize/StreamingGrid.cpp"), "claude3")
        self.assertEqual(o("engine/src/Platform/Steam/Steam.cpp"), "claude3")
        self.assertEqual(o("engine/src/Lua/LuaState.cpp"), "claude3")
        # third-party replacement moved from Claude 2 to the coordinator on 2026-11-22
        self.assertEqual(o("engine/src/Crypto/Sha512.cpp"), "coordinator")
        self.assertEqual(o("engine/src/Platform/Window.cpp"), "coordinator")
        self.assertEqual(o("okayui/src/OkayUI.cpp"), "coordinator")
        self.assertEqual(o("platform/xbox_uwp/XboxExport.hpp"), "coordinator")

    def test_claude3_touching_claude2_file_needs_agree(self):
        c = self.claim("claude3/stream-ragdolls", ["engine/src/Physics/Physics3D.cpp"])
        probs = L.check_claim_rules(c, self.lanes, [])
        self.assertTrue(any("claude2 lane" in p for p in probs), probs)
        self.assertEqual(L.check_claim_rules(dict(c, agreed=True), self.lanes, []), [])
        # the same through the parser: Agree from Claude 2 clears it, Agree from another lane does not
        body = "Claim: claude3/stream-ragdolls\nExisting: e / Gap: g / Files: engine/src/Physics/Physics3D.cpp / Test-first: t / Try-it: s"
        claims, _ = L.parse_claims([{"created_at": "2026-11-22T10:00:00Z", "body": body}])
        self.assertTrue(L.check_claim_rules(claims["claude3/stream-ragdolls"], self.lanes, []))
        claims, _ = L.parse_claims([{"created_at": "2026-11-22T10:00:00Z", "body": body},
                                    {"created_at": "2026-11-22T11:00:00Z", "body": "Agree (Claude 2): claude3/stream-ragdolls"}])
        self.assertEqual(L.check_claim_rules(claims["claude3/stream-ragdolls"], self.lanes, []), [])
        claims, _ = L.parse_claims([{"created_at": "2026-11-22T10:00:00Z", "body": body},
                                    {"created_at": "2026-11-22T11:00:00Z", "body": "Agree: claude3/stream-ragdolls\nLane: coordinator"}])
        self.assertTrue(any("claude2 lane" in p for p in L.check_claim_rules(claims["claude3/stream-ragdolls"], self.lanes, [])))
        # a bug claim with a Test-first line may still touch any file
        self.assertEqual(L.check_claim_rules(dict(c, kind="bug"), self.lanes, []), [])

    def test_claim_names_and_explicit_lane(self):
        claims, _ = L.parse_claims([{"created_at": "2026-11-22T10:00:00Z", "body": "Claim (Claude 3): claude3/lod\nLane: claude3\nExisting: e / Gap: g / Files: engine/src/Core/BuildPipeline.cpp / Test-first: t / Try-it: s"}])
        c = claims["claude3/lod"]
        self.assertEqual(c["lane"], "claude3"); self.assertEqual(c["files"], ["engine/src/Core/BuildPipeline.cpp"])
        self.assertEqual(L.check_claim_rules(c, self.lanes, []), [])
        self.assertEqual(L.lane_key("Claude 3", self.lanes), "claude3"); self.assertEqual(L.lane_key("claude3/", self.lanes), "claude3")
        self.assertEqual(L.lane_key("Coordinator", self.lanes), "coordinator"); self.assertIsNone(L.lane_key("Claude 9", self.lanes))
        bad, _ = L.parse_claims([{"created_at": "2026-11-22T10:00:00Z", "body": "Claim: claude2/x\nLane: claude3\nExisting: e / Gap: g / Files: docs/a.md / Test-first: t / Try-it: s"}])
        self.assertTrue(any("Lane" in p for p in L.check_claim_rules(bad["claude2/x"], self.lanes, [])))

    def test_three_way_shared_file(self):
        r = Repo()
        try:
            r.branch("claude/a", {"tests/CMakeLists.txt": "a\n"})
            r.branch("claude2/b", {"tests/CMakeLists.txt": "b\n"})
            r.branch("claude3/c", {"tests/CMakeLists.txt": "c\n"})
            rep = CC.build_report(r.d, [], [], use_disk_cache=False, now=self.now)
            s = next(x for x in rep["shared_files"] if x["file"] == "tests/CMakeLists.txt")
            self.assertEqual(s["branches"], ["claude/a", "claude2/b", "claude3/c"])
            self.assertEqual(s["lanes"], ["claude2", "claude3", "coordinator"])
            self.assertIn("claude3/c", CC.to_markdown(rep))
            # claims: three claims on one shared file each see the other two
            cl = [self.claim(b, ["tests/CMakeLists.txt"]) for b in ("claude/a", "claude2/b", "claude3/c")]
            probs = L.check_claim_rules(cl[2], self.lanes, cl[:2])
            self.assertTrue(any("claude/a" in p for p in probs) and any("claude2/b" in p for p in probs), probs)
        finally:
            r.close()

    def test_claude3_branch_out_of_lane_and_cross_lane(self):
        r = Repo()
        try:
            r.write("engine/src/Physics/Physics3D.cpp", "x\n"); r.commit("phys"); sh(r.d, "update-ref", "refs/remotes/origin/main", "HEAD")
            r.branch("claude3/a", {"engine/src/Physics/Physics3D.cpp": "y\n"})
            r.branch("claude2/b", {"engine/src/Physics/Physics3D.cpp": "z\n"})
            rep = CC.build_report(r.d, [], [], use_disk_cache=False, now=self.now)
            self.assertEqual([(o["branch"], o["lane"]) for o in rep["out_of_lane"]], [("claude3/a", "claude3")])
            pair = next(o for o in rep["overlaps"] if {o["a"], o["b"]} == {"claude3/a", "claude2/b"})
            self.assertTrue(pair["cross_lane"])
            old = (self.now - datetime.timedelta(hours=1)).strftime("%Y-%m-%dT%H:%M:%SZ")
            body = "Claim: claude2/b\nExisting: e / Gap: g / Files: engine/src/Physics/** / Test-first: t / Try-it: s"
            rep = CC.build_report(r.d, [{"body": body, "created_at": old}], [], use_disk_cache=False, now=self.now)
            self.assertTrue(CC.fails_for_branch(rep, "claude3/a"))
        finally:
            r.close()

    def test_make_brief_accepts_claude3(self):
        p = subprocess.run([sys.executable, os.path.join(SCRIPTS, "make_brief.py"), "--lane", "claude3", "--goal", "g", "--root", ROOT],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("Claude 3", p.stdout); self.assertIn("claude3/", p.stdout)
        self.assertIn("engine/src/Physics/**", p.stdout)   # Claude 2's files are off limits for Claude 3

    def test_every_glob_matches_a_file_or_is_planned(self):
        files = subprocess.run(["git", "-C", ROOT, "ls-files"], stdout=subprocess.PIPE, text=True).stdout.splitlines()
        if not files:
            self.skipTest("not a git checkout")
        dead = [(r["owner"], g) for r in self.lanes["rules"] for g in r["globs"] if not any(L.glob_match(f, g) for f in files)]
        self.assertEqual(dead, [], "globs that match no file: match a real file or move them to the rule's 'planned' list")
        for r in self.lanes["rules"]:
            self.assertIn(r["owner"], set(L.lane_names(self.lanes)) | {"shared", "open"})

    def test_workflows_name_every_lane(self):
        wf = open(os.path.join(ROOT, ".github", "workflows", "branch-lint.yml"), encoding="utf-8").read()
        every_branch = "branches: ['**']" in wf   # pushes to every branch (the duplicate guard's trigger) cover every lane
        for lane, prefix in L.lane_prefixes(self.lanes).items():
            if not every_branch:
                self.assertIn(f"'{prefix}**'", wf, f"branch-lint.yml must run on pushes to {prefix}** ({lane})")
            # the claims step (claims on #422 are for lane branches) must run for every lane
            self.assertIn(f"startsWith(github.ref, 'refs/heads/{prefix}')", wf, f"branch-lint.yml claims step must cover {prefix} branches")
        self.assertRegex(wf, r"(?m)^  pull_request:")   # forks are checked on their pull request

    def test_display_branch_drops_every_lane_prefix(self):
        import dev_index as DI
        hpp = open(os.path.join(ROOT, "shared", "TestBuild.hpp"), encoding="utf-8").read()
        for lane, prefix in L.lane_prefixes(self.lanes).items():
            self.assertEqual(DI.display_branch(prefix + "walk"), "walk", lane)
            self.assertIn(f'std::string_view("{prefix}")', hpp, f"shared/TestBuild.hpp DisplayBranch must drop {prefix} like dev_index.py")


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

    def test_comment_mentions_are_not_uses(self):
        f = self.lint({"launcher/main.cpp": "// MinGW has no std::thread here\nint x; // std::mutex is not used\n/* std::thread */\n * std::condition_variable\n",
                       "launcher/other.cpp": "std::thread t;  // a real use\n"}, BL.check_includes)
        self.assertEqual([(x["file"], x["line"]) for x in f], [("launcher/other.cpp", 1)])

    def test_guarded_is_warning_and_allow_marker(self):
        f = self.lint({"engine/src/Core/X.cpp": "#if !defined(_WIN32)\n#include <thread>\n#endif\n#include <mutex>  // lint-allow: win32-thread\n"}, BL.check_includes)
        self.assertEqual([x["sev"] for x in f], ["warning"])

    def test_binaries(self):
        big = "\0" + "x" * (210 * 1024)
        # a built program or library is refused at any size, an archive too
        f = self.lint({"dist/OkaySpace.exe": "MZ\0\0", "dist/SDL2.dll": "MZ\0", "lib/libfoo.so": "\0ELF", "out/game.zip": "PK\0\0"}, BL.check_binaries)
        self.assertEqual(sorted(x["file"] for x in f), ["dist/OkaySpace.exe", "dist/SDL2.dll", "lib/libfoo.so", "out/game.zip"])
        self.assertTrue(all(x["sev"] == "error" for x in f))
        # a big binary with an unknown extension is refused; a big png, font or mesh is fine outside docs/
        self.r.close(); self.r = Repo()
        f = self.lint({"tools/blob.dat": big, "assets/a.png": big, "assets/f.ttf": big, "examples/m.okaymesh": big, "tests/golden/shot.png": big}, BL.check_binaries)
        self.assertEqual([x["file"] for x in f], ["tools/blob.dat"])
        # a screenshot under docs/ is refused (owner rule 2026-11-17)
        self.r.close(); self.r = Repo()
        f = self.lint({"docs/images/shot.png": "x", "docs/pic.jpg": "x", "shot.png": "x", "tests/golden/ok.png": "x"}, BL.check_binaries)
        self.assertEqual(sorted(x["file"] for x in f), ["docs/images/shot.png", "docs/pic.jpg", "shot.png"])
        # small binaries and big TEXT files (source, scene json, docs) are fine
        self.r.close(); self.r = Repo()
        f = self.lint({"tools/small.dat": "\0" * 100, "docs/big.md": "word " * 100000, "dist/CHANGELOG.txt": "text\n" * 50000}, BL.check_binaries)
        self.assertEqual(f, [])
        # the real tree has none to start with
        tracked = subprocess.run(["git", "-C", ROOT, "ls-files"], capture_output=True, text=True).stdout.split("\n")
        self.assertEqual([p for p in tracked if p.lower().endswith(BL.PROGRAM_EXT) and os.path.exists(os.path.join(ROOT, p))], [])

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
