# How the two Claudes work together

> **What this is:** Two Claudes work on this repo at the same time: **Claude 2** (branches `claude2/*`, coordination issue #422; bug fixes and third-party replacement) and the **coordinator** with its agents (branches `claude/*`; unfinished features, reviews, releases).
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `process/`, paths and links checked; the text itself was not re-checked against the code).

Two Claudes work on this repo at the same time: **Claude 2** (branches `claude2/*`, coordination issue #422; bug fixes and third-party replacement) and the
**coordinator** with its agents (branches `claude/*`; unfinished features, reviews, releases). One day went wrong in ways that cost real work. This page is the
agreement that stops it, and every rule here has a script that checks it, so nobody has to remember.

The problems of 2026-11-15 and what now prevents each one:

| What went wrong | What stops it now |
|---|---|
| Both built the vehicle seat at the same time | Lanes, the claim protocol, `find_existing.py`, the collision check on every push and on the work board |
| Two tween nodes, two SHA-256, two inflate: nobody searched first | `python3 scripts/find_existing.py <keywords>` before every claim; the weekly dedupe audit |
| Claude 2 branches had no "How to try it", sample scene or screenshots | Readiness checklist; the PR hygiene check fails a PR with no "How to try it" |
| `<mutex>` in a header broke the Windows MinGW build (twice) | `branch-lint` fails the push (rule 21a), plus a MinGW syntax check |
| Stale tests and doc claims | Bug-class checklist in every brief; `bug_patterns.py` warnings; docs-changed warning |
| Two branches editing the same lines; three branches rewriting one generated file | Collision check (file and line ranges); generated files regenerated, never hand-edited; shared-file handshake |
| Claims were free text, nobody could see the whole picture | One claim format; the claims table on the live work board |
| Bugs found late by reviews that cheap checks could have caught | `bug_patterns.py` and `branch-lint` on every push |
| Agents started without the current rules (CLAUDE.md changes only when a PR merges) | `scripts/make_brief.py` pastes the rules from main plus unmerged rules branches into every brief |

## 1. Lanes

Who owns what is in one table, [lanes.json](../lanes.json), read by the scripts. Rules are checked top to bottom, the first
match wins. Short version:

| Owner | Areas |
|---|---|
| **Claude 2** | Bug fixes (any file, see below) and our own replacements for third-party code: model and image readers, WAV and audio decoders, HTTP, crypto (rule 18), the platform layer, the UI toolkit `okayui/`, the text editor, the ufbx and Assimp replacements, and their tests and docs |
| **Coordinator** | Unfinished features: gameplay components, in-game UI, editor features, rendering features, visual scripting features, inventory, vehicles, the launcher, examples, tests and docs of those; plus releases, CI and packaging (`.github/`, `scripts/`, `cmake/`, `dist/`), `shared/` (except the own-code headers), reviews |
| **Shared (handshake)** | `engine/src/Scene/SceneSerializer.cpp`, `tests/CMakeLists.txt`, `engine/CMakeLists.txt`, the feature map and every generated file, `CLAUDE.md`, `engine/src/Scripting/OkayScriptVM.cpp`, `examples/visual_script/playground/coverage.okayvs`, `docs/status/UNFINISHED.md`, roadmap and registry-style docs |
| **Open** | Everything else (for example `engine/include/okay/Core`): either lane may claim it |

How the lanes work in practice:

- Work in your lane. If you need a change in the other lane's files, ask on issue #422 (`Claim:` naming those files). The
  owner answers `Agree: <your branch>` or says no. Until then the branch-lint check fails the push.
- **Shared files need a handshake.** Only one open claim at a time may name a shared file. Keep your edit small and at the
  end of the file or in its own block, so a second branch can merge. Never rewrite a whole generated file by hand.
- **Generated files are regenerated, never edited.** `docs/FEATURE_MAP.md` and `docs/feature_map/_index_*.md` come from
  `python3 scripts/gen_feature_map.py` (check: `--check`); `editor/VisualScriptNodeDocs.inc` from
  `scripts/gen_vs_node_docs.py`. Edit the source pages, then regenerate. If the file conflicts when merging main,
  regenerate instead of resolving by hand.
- **Bugs go to Claude 2, and a bug fix may touch any file** (owner, 2026-11-15). Post the claim with an extra line
  `Kind: bug` and a `Test-first:` line (the failing test that proves the bug); the lane check then does not need an `Agree:`
  from the other lane. Claims that overlap another open claim still need the handshake. The coordinator's agents fix a bug
  only inside a feature branch they are finishing (rule 24).
- **No duplicate features** (owner, 2026-11-15): both lanes run `find_existing.py` first, extend what exists, and say
  `Existing:` / `Gap:` in the claim.

## 2. Before you start: find what exists (2 minutes)

```
python3 scripts/find_existing.py vehicle seat
python3 scripts/find_existing.py "tween position"
```

It searches the feature map, the open notes in `docs/status/UNFINISHED.md` and `docs/status/unfinished_engine.md`, doc titles, class,
component, node and function names, tests, example scenes, **and every open branch and PR** (names and the files they
change), plus the claims on #422. It ends with a ready-made `Existing: ... / Gap: ...` line. Add `--offline` when GitHub is
not reachable (then it skips PR titles and claims). If it finds something, extend it. If it finds a branch, talk to that
branch's owner first.

## 3. The claim protocol

Claims live on issue #422, one comment per claim. The format is fixed so a script can read it:

```
Claim: claude2/tween-easing
Existing: TweenPosition node (editor/VisualScriptCategories.hpp), Tween.cpp; no easing choice in the node
Gap: add an easing pin (linear, ease-in, ease-out, ease-in-out) to the tween nodes
Files: engine/src/Core/Tween.cpp, engine/include/okay/VisualScript/**, tests/test_tween_ease.cpp
Test-first: tween_ease
Try-it: examples/visual_script/tween_ease.okayvs, press Play, the box eases in and out
```

(The five fields may also sit on one line separated by ` / `.)

1. **A claim is one comment** starting `Claim: <branch>`, with `Existing`, `Gap`, `Files` (globs), `Test-first` (the
   failing test you will write first) and `Try-it` (the scene or steps the owner will use).
2. **Refused** until the other side releases or agrees: a claim whose `Files` are in the other lane, or that overlap an open
   claim. The work board shows the refusal reason in the claims table. A refused claim is not started.
3. **Claims expire after 24 hours without a push** to the branch. A stale claim stops blocking others and shows as stale on
   the board.
4. **Release** with a comment `Release: <branch>` when the work is merged, handed over or dropped. A result report is not a
   release.
5. **Agree** with a comment `Agree: <branch>` from the lane that owns the files (or the owner of the overlapping claim).
6. Work posted without a claim (a branch with no claim and no PR) shows up on the board as unclaimed. Claim it.

## 4. Readiness checklist (before the coordinator reads a branch)

A branch that does not meet this is sent back, not reviewed. The coordinator's time is for real review.

- [ ] A claim exists for the branch with all five lines.
- [ ] The failing test was shown failing first, then passing; an option-ON test where the feature is off by default.
- [ ] Rule 4a material: a way to try it in the Windows release (menu, template, shipped example scene, setting), exact steps
      and what to expect in `docs/try_it.md`; screenshots for visual work (headless renders are fine, say they are).
- [ ] Docs updated in the same change; generated files regenerated (`python3 scripts/gen_feature_map.py --check`).
- [ ] No forbidden includes (`<mutex>`, `<thread>`, `<condition_variable>`, `std::call_once` in Windows code; rule 21a):
      `python3 scripts/branch_lint.py all`.
- [ ] `python3 scripts/bug_patterns.py` is clean or each finding is explained in the report.
- [ ] `python3 scripts/collision_check.py --offline --branch <branch>` shows no overlap that was not discussed.
- [ ] The report says what was run and what was not (unverified is a fine word), and the PR text has "How to try it".

## 5. Agent briefs

Every agent starts from [agent_brief_template.md](agent_brief_template.md): goal, files to read, off limits, constraints, the
**rules pasted in full** (not "see CLAUDE.md"), the recurring bug-class checklist from [review_log.md](review_log.md), and
how to report. Build it with `python3 scripts/make_brief.py --goal ... --branch ... --with-branch origin/claude/owner-rule2`:
it takes `CLAUDE.md` from `origin/main` and adds the rules of any rules branch that is not merged yet, so no agent works from
stale rules. The coordinator uses this for its own agents and posts the same brief for Claude 2's new items.

## 6. The automatic checks

| Check | Where | Fatal? |
|---|---|---|
| Forbidden thread includes in Windows code (rule 21a) | `branch-lint` on every push to `claude/**`, `claude2/**` | yes |
| Secrets (private keys, `sk-`, `ghp_`, `AKIA`, long key strings) | `branch-lint` | yes |
| Push into the other lane's open claim | `branch-lint` (`collision_check.py --branch`) | yes |
| Code changed with no docs changed | `branch-lint` | warning |
| Feature map not regenerated | `branch-lint` | warning |
| MinGW `-fsyntax-only` of changed engine files | `branch-lint` (installs MinGW only when engine files changed) | warning |
| Bug-class patterns | `branch-lint` (`scripts/bug_patterns.py`) | warning |
| PR body has "How to try it" with content | `pr-hygiene` workflow | yes (PR check) |
| Collisions between branches, claims table | live work board (`WORK_BOARD.md` on branch `work-board`), every 10 minutes | report |

Try them locally, from the repo root with the branch checked out:

```
python3 scripts/find_existing.py tween
python3 scripts/collision_check.py --branch $(git branch --show-current)
python3 scripts/branch_lint.py all
python3 scripts/bug_patterns.py
python3 scripts/tests/test_collab.py      # the scripts' own tests (also ctest: collab_scripts)
```

`collision_check.py` knows only what the remote refs hold: run `git fetch origin` first. Its report means: *file overlaps*
(two branches change the same file; docs and shared files are listed apart because they merge by hand), *same lines* (the same
line ranges in one file; "approx" when the two branches started from different commits of main, so line numbers may have
moved), *cross-lane*, *no claim and no PR*, *stale claims*, *out of lane*, *generated files edited by hand*, *same test or
CMake target name in two branches*, and *far behind main*. A branch that was only built on top of another (one is an ancestor
of the other) is a stack, not a collision. Branches whose PR merged, and branches whose files already match main (squash
merged), are left out.

What the checks cannot see: a field written by the serializer but missing from the property registry, docs that claim more
than the evidence, a test that passes for the wrong reason, a design that duplicates a feature under another name. Those stay
with the reviewer and with the dedupe audit below.

### False positives to expect

- `find_existing.py`: common words match many things (the feature map is matched by section text, the code search by names
  that contain every keyword). Read the top of each list; the branch list is ranked by name match first.
- `collision_check.py`: line ranges are compared in the coordinates of each branch's merge base, so two branches that started
  from different commits of main can show "approx" overlaps that moved. A squash-merged branch whose PR is not found (offline
  run) can still look live if main changed its files afterwards: run it online. Squash merges mean git itself cannot tell
  merged from unmerged, so the script uses the PR list and "main already holds these files".
- `branch_lint.py includes`: a thread header inside an `#if` that mentions a platform macro is only a warning; add
  `lint-allow: win32-thread` on a line that is really never built for Windows. The secret scan skips `tests/`, `docs/` and the
  crypto folders for long hex strings; add `lint-allow: secret` for a public constant.
- `branch_lint.py mingw`: our MinGW here accepts `<mutex>` (this machine's GCC 13 win32 compiler compiled it), so this check
  finds other Windows-only breakage, not the thread headers; the text check above is the guard for those.
- `bug_patterns.py`: all heuristics. `substr-loop` and `loop-copy` flag by shape; `alloc-before-check` looks 10 lines back for
  a read; a limit checked in a helper function is invisible to it. Fix it, or explain it in the report.

## 7. Weekly dedupe audit

Once a week the coordinator asks Grok (or a read-only agent) to list duplicate features: two classes, nodes, helpers or
scenes that do the same job, two copies of a hash, decoder or math routine, two docs describing one thing. The brief is:
"read-only; search `engine/ editor/ launcher/ shared/ okayui/` and `docs/`; list each pair with file:line, which one is
used, which looks like the copy; no fixes." Findings go as comments on issue #888 with the usual rules (rule 24): each gets
a test or a clear file:line, a guess is labelled "unconfirmed". Consolidations then follow the normal claim protocol and
lanes. (Known today: `Sha256` in `Crypto/Crypto.hpp` and in `Platform/Account/AccountService.hpp`; see `docs/status/UNFINISHED.md`.)

## 8. What is still by hand

- The coordinator posts `Agree:` and `Release:` answers and settles a refused claim.
- Someone runs the weekly dedupe audit and files the findings on #888.
- Reviews of design and of "does this really match the claim". The scripts only catch the cheap, mechanical cases.
- Keeping `docs/lanes.json` true when new directories appear (it falls back to "open", which is safe).
- Claim comments written before this protocol have no `Existing`/`Gap`/`Files` lines; the board marks them and they should be
  reposted in the new format when next touched.
