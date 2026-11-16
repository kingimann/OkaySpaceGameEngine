# Agent brief template

Every agent (a coordinator agent or Claude 2 starting a piece of work) starts from this brief. It exists because
`CLAUDE.md` only changes when a rules PR merges, so an agent started today can miss a rule that is agreed but not merged
yet. The brief therefore carries the rules **pasted in full**, never "see CLAUDE.md".

Build it with one command (it pastes `CLAUDE.md` from `origin/main` and adds rules from branches that are not merged yet,
such as `claude/owner-rule2`):

```
python3 scripts/make_brief.py --goal "Add easing to tween position" --lane claude2 \
    --branch claude2/tween-easing --files "engine/src/UI/**" --test-first tween_ease \
    --read docs/animation.md --with-branch origin/claude/owner-rule2 > /tmp/brief.md
```

Or fill the template below by hand and paste the output of `python3 scripts/make_brief.py --rules-only` where it says
`{{RULES}}`. Plain rules for claims and lanes: [collaboration.md](collaboration.md).

---

<!-- BRIEF START: everything from here down is sent to the agent -->

# Brief: {{GOAL}}

You are {{WHO}}. Branch: `{{BRANCH}}` (plain push to that branch only, never force, no PR unless told).

## 1. Goal

{{GOAL}}

Done means: what a person can do or see that they could not before, the test that proves it, and how to try it in the
Windows release.

## 2. Before you write any code (5 minutes, not optional)

1. Run `python3 scripts/find_existing.py {{KEYWORDS}}`. Read what it lists: the feature map entry, the code names, the
   tests, the example scenes, and **every open branch that touches it**. If something already does this, extend it. Do not
   build a second copy (past duplicates: two tween nodes, two SHA-256, two inflate, two vehicle seats).
2. Post the claim on issue #422, one comment:
   `Claim: {{BRANCH}}` then `Existing: ... / Gap: ... / Files: {{FILES}} / Test-first: {{TESTFIRST}} / Try-it: <scene or steps>`.
   Files owned by the other lane, or overlapping an open claim, are refused until the other side releases or says
   `Agree: {{BRANCH}}`. Claims expire after 24 hours without a push. Say `Release: {{BRANCH}}` when you stop.
3. Write the failing test first and show its failure (rule 4a material comes with the change, see section 6).

## 3. Files to read

{{READ}}

## 4. Off limits

- Files and folders of the other lane (see `docs/lanes.json`): {{OFFLIMITS}}
- Shared files need a handshake (one claim at a time): `engine/src/Scene/SceneSerializer.cpp`, `tests/CMakeLists.txt`,
  `docs/FEATURE_MAP.md` and `docs/feature_map/*` (generated: edit the pages, run `python3 scripts/gen_feature_map.py`),
  `CLAUDE.md`, `engine/src/Scripting/OkayScriptVM.cpp`, `examples/visual_script/playground/coverage.okayvs`.
- Generated files are never edited by hand: regenerate them.
- No third-party code, assets or SDKs (rule 19a). No `<mutex>`, `<thread>`, `<condition_variable>` or `std::call_once`
  in code the Windows build compiles (rule 21a): use `debugutil::SpinLock` or atomics.

## 5. Constraints

`df -h /` first; `ninja -j2`; only the targets you need; delete the build trees you made. Stay in your lane. Pick the safe
default for a decision that is the owner's, say what you picked, and carry on.

## 6. Readiness: what must be true before anyone reads your branch

- [ ] A claim comment exists for this branch, with all five lines.
- [ ] The failing test was shown failing first (paste the output), then passing. Option-ON cases are tested too.
- [ ] Rule 4a material: a way to try it in the Windows release (menu, template, shipped example scene, setting), exact
      steps and what to expect in `docs/try_it.md`; screenshots for visual work (headless renders are fine, say so).
- [ ] Docs updated in the same change; generated files regenerated (`python3 scripts/gen_feature_map.py --check`).
- [ ] No forbidden includes; `python3 scripts/branch_lint.py all` and `python3 scripts/bug_patterns.py` are clean or each
      finding is explained.
- [ ] `python3 scripts/collision_check.py --offline --branch {{BRANCH}}` shows no overlap you did not discuss.
- [ ] The report (section 8) is posted.

## 7. Bug classes reviews keep finding (check each one on your own diff before you report)

1. **Work before the size check.** Read a count, then allocate (`resize`, `reserve`, `vector(n)`, `new T[n]`) before
   comparing it with a budget. Check first, allocate after.
2. **Per-item copies.** `for (auto x : meshes)` or a `std::vector` copied per bone, per mesh, per vertex. Use references.
3. **Quadratic loops.** `s = s.substr(i)` in a loop, erase-from-front in a loop, a linear search inside a loop over the same
   data. Use an index, a `string_view`, a map.
4. **Unchecked narrowing.** `(int)` of `strtol`, `strtoll`, a float, a 64-bit size or a file field with no range check.
   Hostile files hit it.
5. **Option-ON not tested.** A feature that is off by default (own reader, own backend, a new table slot such as
   `kOwnSlots`) needs a test that turns it ON and checks the real result, not only that the default is unchanged.
6. **Saved but not registered.** A new field written by `SceneSerializer`, read back, but missing from the property
   registry, the inspector, undo, clone or the coverage list. Save, load, save again and compare the text.
7. **Tests that cannot fail.** `CHECK(true)`, `ms < 2000` timing limits, a loop that never runs, an assertion on the input.
   Break the code on purpose once and see the test go red.
8. **Docs stronger than evidence.** "Works", "fixed", "matches X" with nothing run. Say what was run and what was not.
   "Unverified" is a fine word.
9. **Stale claims.** Test counts, file lists and "not done" notes in docs that the change just made wrong. Grep for them.
10. **Windows-only breakage seen late.** Thread headers (see section 4), `strtok_r`, POSIX-only calls, `%zu`, paths with
    backslashes, network-share paths. Run `python3 scripts/branch_lint.py mingw` if MinGW is installed.
11. **Hostile input.** Every reader and parser: truncated, huge counts, recursion, cycles, NaN, overflow in size math.
12. **Same lines as another branch.** Two branches editing one function or one generated file. Run the collision check.

## 8. How to report

Post one comment on issue #422 (Claude 2) or reply to the coordinator, in plain short sentences:

- **What changed** (files, behaviour).
- **Tests and results** (names, counts, the exact command, pass/fail). The failing-first output.
- **How to try it** (steps, scene, what you should see; screenshots).
- **What is unverified** and **what is not done**. Never write "done", "ready" or "green" for something that is not.
- `Usage: about N% left` (Claude 2, rule 25) and `Release: {{BRANCH}}` if you are finished.

## 9. The rules (pasted in full, current as of {{DATE}})

{{RULES}}
