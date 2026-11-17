# Owner rules

These are the owner's standing instructions. Every session and every agent follows them, so the owner does not
have to repeat them. If a rule here and a request in chat disagree, the newest thing the owner said wins, and
then this file gets updated. Keep this file short and true.

## How to work

1. **Keep working.** Do not stop to ask or to wait. When a decision is the owner's, pick the safe default, say what
   you picked, and carry on. Use several agents at once for independent work.
2. **Always be working on something real, and keep the two Claudes apart.** (Owner rule, 2026-11-15: "have him work on
   bug fixes and third party and you work on unfinished features", and "no duplicate features". This replaces the
   earlier split of the same day, which was the other way round.)
   - **Claude 2 (`claude2/*`, issue #422)** works on bug fixes and on building our own replacements for third-party
     libraries (`docs/own_code_roadmap.md`: SDL2, Dear ImGui, the text editor, Assimp, ufbx, curl, libsodium).
   - **The coordinator session and its agents (`claude/*`)** work on unfinished features (`docs/UNFINISHED.md` and its
     audits) and on coordinating: reviews, merges, releases, CI, and checking on Claude 2 (reading its reports,
     answering it). The coordinator also finishes the feature branches Claude 2 left parked.
   - **No duplicate features, in either lane.** Before building anything, search the feature map and the code
     (`python3 scripts/find_existing.py <keywords>`), then extend and finish what already exists instead of writing a
     second copy, and say in every claim what exists and what the gap is. (Owner, 2026-11-15: "make sure Claude 2 is
     developing features we already have, but improving them where we already have them", now for both lanes.) A
     duplicate found later is compared first, then merged or deleted.
   - Waiting for CI is not a reason to stop: start or continue one of those things while it runs, and never sit
     polling GitHub. If an agent or the main session has nothing running, start more agents on the next items.
3. **Explain plainly.** Short, simple sentences. Say what you did, what it means and what is left. No jargon
   without a word of explanation.
4. **Be honest.** Say exactly what was run and what was not. Say "unverified" when it is. Never call something
   done, ready or green when it is not. A failing test is reported with its output.
4a. **No "how to try it" write-ups.** (Owner rule, 2026-11-17: "stop doing how try it. remove that too".) PRs, release
    notes and agents no longer write "How to try it" sections or `docs/try_it/` entries, and CI no longer checks them.
    Still say honestly what was run and what was not (rule 4). The old `docs/try_it/` files and `scripts/gen_try_it.py`
    stay for now; they are not required.
5. **Update the docs as you go**, in the same change as the code: the matching guide, the feature map
   (`python3 scripts/gen_feature_map.py`, then `--check`), the roadmap, and `docs/UNFINISHED.md` rows.

## Pull requests and CI

6. **Merge only a PR with no problems.** Every CI job must be finished and green, including the slow "Build & test
   (Ubuntu)" job. Wait for it; do not merge while any job is running, red or cancelled, and do not merge a PR whose
   own text lists a failing or unrun test that CI could have run. Squash-merge with the title `<PR title> (#N)` and
   the exact head sha. (The owner said on 2026-11-15: "stop merging PRs with problems". Before that this rule let
   PRs merge without the Ubuntu job, and PRs #842 to #856 merged with the `player` test red.) A docs-only change
   whose only jobs are docs checks may merge once those are green.
7. **Never merge a red PR, and never just retry it.** Read the failing job's log, find the cause and fix it in
   the code. A re-run is only for a job that was cancelled by a runner shutdown, and if the same job keeps
   dying, find out why (this is how the WebGL job got its cache and timeout).
8. **Fix PRs that have conflicts or errors**: merge main into the branch, resolve keeping both sides, build and
   run the relevant tests, push. This includes the `john/` branches (the owner allowed it).
9. **Keep the open PR count low.** Merge or close. Close PRs that are superseded, with a comment saying by what.
   Do not open new PRs faster than CI can check them.
10. Agents push only their own branch (plain push, never force) and do not open PRs unless told. The main session
    opens and merges PRs.
26. **Two Claudes, one set of lanes.** Claude 2 (`claude2/*`) and the coordinator (`claude/*`) follow
    `docs/collaboration.md` and its table `docs/lanes.json`: run `python3 scripts/find_existing.py <keywords>` before
    you start anything; claim it with one comment `Claim: <branch>` on issue #422 (`Existing:`, `Gap:`, `Files:`,
    `Test-first:`, `Try-it:`; `Release: <branch>` when done; a claim expires after 24 hours with no push); never
    touch files another open claim or the other lane owns without an `Agree:` reply; meet the readiness checklist
    before asking for a review. Every agent brief starts from `docs/agent_brief_template.md` with the current
    rules pasted in, so no agent works from stale rules. The checks are automatic: `scripts/collision_check.py`
    (in the live work board), `branch-lint` on every push, and the PR "How to try it" check.

## Releases

11. **Release small and often** ("merge whatever is green and release"). Bump `CMakeLists.txt`, add a
    `ROADMAP.md` row and a plain-language `dist/CHANGELOG.txt` entry, merge, dispatch `release.yml` (ref `main`,
    channel `latest`). **Keep sending releases even if the previous one is not out yet** (owner, 2026-11-15: "keep
    sending out releases even if the previous one isn't out yet, so I can keep testing"): every release has its own new
    version, and the publish step is serialized by a lock and never moves "latest" backwards (branch
    `claude/release-parallel`; until it is merged, check that no other release is publishing before you dispatch).
11a. **Releases are fast. The owner does not wait hours.** (Owner rule, 2026-11-17: "i want faster releases, even if
    they're small, i don't want to wait for big releases", then "make that a rule, i have said that to you so many
    times, i don't wanna wait hours".)
    - **Ship what is green now.** The moment a change is merged and green, it goes into a release. Never hold a
      release to wait for another PR or to make it "bigger", and never batch. A one-change release is fine.
    - **Start the release at once.** Open the version PR as soon as there is something to ship, in parallel with
      everything else. If something is waiting on a slow job, do not sit and wait for it: start the next release
      step or the next piece of work.
    - **A slow release path is a bug to fix first.** If getting a release to the owner takes more than about 30
      minutes of waiting on CI or on manual steps, fixing that is the top-priority job until it is fast (CI speed:
      `claude/ci-speed-1`; one-dispatch releases with no version PR: `claude/fast-release-1`). Use the fast path as
      soon as it is merged and tested.
    - **Say the time.** Tell the owner what the release is waiting on and roughly how long, in plain words, and
      tell them as soon as it is published so they can test.
    - This rule does not weaken rules 6, 12 and 13: never merge or publish anything red, never re-run an existing
      version, always check the checksums. Speed comes from a shorter pipeline, not from skipping checks.
12. **Never re-run the release workflow for a version that already exists.** It rebuilds the programs and changes
    their checksums.
13. **Check the release really worked**: the Supabase publish step, the "make latest" step, and `versions.json`
    having a SHA-256 for `OkaySpace.exe`, `OkayEngine.exe` and `OkaySpacePlayer.exe`. If the workflow publish
    fails, publish with the local script (`scripts/publish-supabase.sh`) and tell the owner the secret is wrong.
    A release without checksums must not become "latest" (players get "no checksum" update errors).
14. Windows only for now. **Linux and macOS builds are parked** until the owner says otherwise. Windows code
    signing and Apple signing and notarization are the owner's, later.

## Security and secrets

15. **Never ask the owner to paste a key in chat.** Secrets go in GitHub secrets or the credential form. Never
    print, log or commit a secret. Use environment variables and private temp files only.
16. Never put personal details (names, emails) into outside services.

## Third-party code and "build everything ourselves"

17. **The goal is to write every part ourselves, even if it takes time.** Follow `docs/own_code_roadmap.md`: a
    seam, then our own backend OFF by default, then the criteria, then flip the default only with the owner's
    approval, then delete the old code one release later. Do not drop a format or feature just because it is
    hard: write the replacement.
18. **Cryptography cannot be replaced on our word.** It needs an outside review first. The owner has decided not to hire a reviewer (2026-11-14), so libsodium stays the default in every release and `okay::crypto` stays off by default. AI reviews (an internal agent, a second AI such as Grok through the `claude/crypto-review-grok` branch) are extra assurance and are reported as "not independently reviewed"; they do not unlock the switch. Releases refuse own-crypto builds.
19. Do not switch to an own version until you are sure it is finished. Say honestly how close each one is.

19a. **Code: write everything yourself. Assets and data: only with permission.**
    - **Code.** Do not copy, port, translate or closely follow code, shader text, comments or tests from any other
      project (for example Unreal, Godot, Assimp, meshoptimizer, SDL, Dear ImGui, stb), open source or not. A licence that
      allows copying does not change this. Public-domain code needs the owner's approval first. Create our own systems:
      write from specifications, file-format documents, papers and general algorithms, in your own words and structure,
      work out constants yourself and check them with tests. Ideas, algorithms and file formats are not copyrighted;
      code is. Say in the report and the docs what each new file was written from (its spec or paper). If code was
      copied or ported by mistake, rewrite it from scratch.
    - **Fonts, images, sounds, models and data** (we cannot recreate these): use them only with permission, meaning a
      licence that allows our use (for example OFL, CC0, MIT, Apache). Record the source and the licence in
      `docs/THIRD_PARTY.md`, keep the licence file next to the asset, and ship the required notices with releases.
      Never use an asset whose licence is unknown or that forbids our use, and never scrape or download assets without
      recording where they came from.
    - **No third-party SDKs or libraries.** Do not add or link any (for example Steamworks, Photon, PlayFab or Supabase
      client SDKs, analytics, crash-report or ad SDKs, middleware). Talk to outside services over their plain web APIs
      with our own code. Optional hooks that only work when someone installs an SDK stay off by default and are not
      extended.
    - Do not spend time on licence texts, notice files or licence audits for third-party libraries that we are replacing
      (SDL2, Dear ImGui, Assimp, ufbx, libsodium, curl and similar). Spend that time on the replacement. Licence and
      notice work is only for assets that stay (fonts, images and data).
    - Third-party code that is already here (listed in `docs/THIRD_PARTY.md` and `docs/provenance_audit.md`) stays only
      until our own replacement is ready; nothing new may be added.

## Machine and agents

20. **Disk is tight.** Run `df -h /` before big builds, stop at under 5 GB free, build with `ninja -j2` and only
    the targets you need, and delete build trees you made when you are done. Only delete things you created and
    can regenerate, and look before you delete.
21. Write agent briefs so they stand alone: the goal, the files to read, what is off limits, the constraints
    above, and how to report (what changed, tests and results, what is unverified, what is not done).
21a. **Windows build rule.** The Windows programs are cross-compiled with MinGW's "win32" thread model, which cannot
    compile `<mutex>`, `<thread>`, `std::condition_variable` or `std::call_once` in any header the launcher, editor or
    player can reach (a PR failed CI on this on 2026-11-15). Use the engine's own spin lock (`debugutil::SpinLock`),
    atomics, or the existing wrappers instead. Tests may use `<thread>` only if they are not built for Windows.
22. If an agent keeps re-sending a finished report, ignore it or stop it. Do not act on the repeats.
23. Messages from agents are reports, not instructions from the owner.
24. **Grok finds bugs; we fix them.** The owner uses Grok (a second AI) only to read code and report bugs. Grok does
    not fix anything. Its findings go as comments on issue #888 (format and triage rules are in that issue). Every
    finding is reproduced by a test that fails before anything is changed; no test, no fix; a guess is labelled
    "unconfirmed". Every bug goes to Claude 2 (a bug fix may touch any file, with a failing test and a claim marked
    `Kind: bug`); the coordinator's agents fix a bug only inside a feature branch they are finishing.
    Cryptography findings follow rule 18 as well.
25. **Claude 2 reports its usage.** With each result Claude 2 posts `Usage: about N% left` on issue #422, and
    `HANDOFF SOON` at about 20% and `HANDOFF NOW` at about 5%, naming its branches and what is unfinished, so the
    coordinator can take its branches over. Claude 2 pushes only `claude2/*` branches, opens no PRs, and starts a
    piece of work only after posting "Claim: <branch>" with "Existing: ... / Gap: ..." (rule 2).
