# Owner rules

These are the owner's standing instructions. Every session and every agent follows them, so the owner does not
have to repeat them. If a rule here and a request in chat disagree, the newest thing the owner said wins, and
then this file gets updated. Keep this file short and true.

## How to work

1. **Keep working.** Do not stop to ask or to wait. When a decision is the owner's, pick the safe default, say what
   you picked, and carry on. Use several agents at once for independent work.
2. **Always be working on something real, and keep the three Claudes apart.** (Owner rule, 2026-11-15: "have him work on
   bug fixes and third party and you work on unfinished features", and "no duplicate features". This replaces the
   earlier split of the same day, which was the other way round.)
   - **Three Claudes since 2026-11-22** (owner: "I have 3 Claudes. I want 3 Claudes to be able to work together."). The
     lanes and the paths each owns are in `docs/lanes.json`; the plain rules are in `docs/process/collaboration.md`.
   - **The coordinator session and its agents (`claude/*`)** work on unfinished features (`docs/status/UNFINISHED.md` and its
     audits), the Xbox UWP port and our own replacements for third-party libraries (`docs/status/own_code_roadmap.md`;
     handed over from Claude 2 on 2026-11-22), and on coordinating: reviews, releases, CI, and checking on the other Claudes.
   - **Claude 2 (`claude2/*`, issue #422)** works on bug fixes, physics (own physics engine, ragdolls), removing duplicate
     code and the duplicate guard, and splitting `editor/main.cpp`.
   - **Claude 3 (`claude3/*`, issue #422)** works on the Build window and game export, streaming and LOD, the Steam/Epic
     Store Kit, and C# and Lua scripting. It starts from `docs/process/claude3_start.md`.
   - **No duplicate features, in either lane.** Before building anything, search the feature map and the code
     (`python3 scripts/find_existing.py <keywords>`), then extend and finish what already exists instead of writing a
     second copy, and say in every claim what exists and what the gap is. (Owner, 2026-11-15: "make sure Claude 2 is
     developing features we already have, but improving them where we already have them", now for both lanes.) A
     duplicate found later is compared first, then merged or deleted.
   - Waiting for CI is not a reason to stop: start or continue one of those things while it runs, and never sit
     polling GitHub. If an agent or the main session has nothing running, start more agents on the next items.
2a. **Do not give the other Claudes tasks.** (Owner rule, 2026-11-17: "stop giving claude 2 tasks".) The coordinator and its
    agents do not assign, request or queue work for Claude 2 or Claude 3, and do not post task requests for them on issue #422.
    Each Claude chooses and claims its own work in its lane (rule 2). If another Claude's branch or PR needs fixing, fix it
    yourself (rules 8 and 25) and do not ask that Claude to.
3. **Explain plainly.** Short, simple sentences. Say what you did, what it means and what is left. No jargon
   without a word of explanation.
4. **Be honest.** Say exactly what was run and what was not. Say "unverified" when it is. Never call something
   done, ready or green when it is not. A failing test is reported with its output.
4a. **No "how to try it" write-ups.** (Owner rule, 2026-11-17: "stop doing how try it. remove that too".) PRs, release
    notes and agents no longer write "How to try it" sections or `docs/try_it/` entries, and CI no longer checks them.
    Still say honestly what was run and what was not (rule 4). The old `docs/try_it/` files and `scripts/gen_try_it.py`
    stay for now; they are not required.
4b. **No screenshots in the repository.** (Owner rule, 2026-11-17: "Remove any screenshots on GitHub, clean up the GitHub,
    organize files better".) Pictures are never committed: `docs/images/` was deleted and is git-ignored, and `branch_lint.py`
    refuses a picture under `docs/` or at the repo root. Make pictures outside the repo (`OKAY_SHOT_DIR` or a temp folder), show
    them to the owner in chat, and describe them in words in docs. Only pictures that a test compares against stay
    (`tests/`, `okayui/tests/`, `assets/`, `examples/`). Keep `docs/README.md` (the index) in step when a guide is added.
5. **Docs are for our own use only.** (Owner rule, 2026-11-17: "No more guides, tutorials or anything, documents are for
   our use only".) Do not write guides, tutorials, how-tos, quick starts or player/user-facing pages, and do not add new
   ones. Keep only the internal records, short and in the same change as the code: the feature map
   (`python3 scripts/gen_feature_map.py`, then `--check`), the roadmap, `docs/status/UNFINISHED.md` rows, and design or audit
   notes when a decision needs one. Existing guides stay until the owner says to delete them; do not extend them.

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
10. Agents push only their own branch (plain push, never force) and do not open PRs unless told. Each Claude's main
    session opens and merges its own lane's PRs (rule 6). Only the coordinator merges the release PR and runs `release.yml`.
26. **Three Claudes, one set of lanes.** The coordinator (`claude/*`), Claude 2 (`claude2/*`) and Claude 3 (`claude3/*`) follow
    `docs/process/collaboration.md` and its table `docs/lanes.json`: run `python3 scripts/find_existing.py <keywords>` before
    you start anything; claim it with one comment `Claim: <branch>` on issue #422 (`Existing:`, `Gap:`, `Files:`,
    `Test-first:`, `Try-it:`; `Release: <branch>` when done; a claim expires after 24 hours with no push); never
    touch files another open claim or another lane owns without an `Agree:` reply from that lane; meet the readiness checklist
    before asking for a review. Every agent brief starts from `docs/process/agent_brief_template.md` with the current
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

17. **The goal is to write every part ourselves, even if it takes time, and nothing is off by default.** (Owner rule,
    2026-11-22: "I don't want anything off by default anymore", and "you're improving the engine, not making copies".)
    Follow `docs/status/own_code_roadmap.md`, but a replacement is not parked behind an off switch: build it, test it
    against the old one until it gives the same or better results, then make it THE path in the same change and delete
    the old code (rule 19c: no second copy kept "just in case"). New features ship on. The switches that are off today
    are being turned on one by one, each with its tests; one that cannot be turned on safely yet is finished first, not
    left off. Do not drop a format or feature just because it is hard: write the replacement. Cryptography is the one
    exception (rule 18).
18. **Cryptography cannot be replaced on our word.** It needs an outside review first. The owner has decided not to hire a reviewer (2026-11-14), so libsodium stays the default in every release and `okay::crypto` stays off by default. AI reviews (an internal agent, a second AI such as Grok through the `claude/crypto-review-grok` branch) are extra assurance and are reported as "not independently reviewed"; they do not unlock the switch. Releases refuse own-crypto builds.
19. Do not switch to an own version until its tests show it is finished; finish it rather than leave it off. Say honestly how close each one is.

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
      `docs/status/THIRD_PARTY.md`, keep the licence file next to the asset, and ship the required notices with releases.
      Never use an asset whose licence is unknown or that forbids our use, and never scrape or download assets without
      recording where they came from.
    - **No third-party SDKs or libraries.** Do not add or link any (for example Steamworks, Photon, PlayFab or Supabase
      client SDKs, analytics, crash-report or ad SDKs, middleware). Talk to outside services over their plain web APIs
      with our own code. Optional hooks that only work when someone installs an SDK stay off by default and are not
      extended.
    - Do not spend time on licence texts, notice files or licence audits for third-party libraries that we are replacing
      (SDL2, Dear ImGui, Assimp, ufbx, libsodium, curl and similar). Spend that time on the replacement. Licence and
      notice work is only for assets that stay (fonts, images and data).
    - Third-party code that is already here (listed in `docs/status/THIRD_PARTY.md` and `docs/status/provenance_audit.md`) stays only
      until our own replacement is ready; nothing new may be added.

19b. **GitHub is only for the owner and Claude.** (Owner rule, 2026-11-20: "I don't want any one to depend on GitHub. GitHub
    should be only for me and you.") Nothing public may need GitHub: not the launcher, editor, player or server, not their
    updates, downloads, version lists, news, help or report links, not the installer or server downloads, and not web pages
    such as the password-reset page. Those come from our own Supabase project. GitHub stays our workspace: code, CI, pull
    requests, issues and the internal record of releases. The repository may become private at any time, so never add a
    public-facing link to github.com, and a repo check fails if one appears in shipped code or shipped files.

19c. **No duplicate code, across the whole repository, every language.** (Owner rule, 2026-11-22: "create a system so
    duplicates can't happen again".) Before writing a helper or a system, look in `docs/engine/shared_code.md` and use or
    extend the one listed there; never paste code from another file. Search `docs/engine/code_index.md` first; a PR lists
    the existing code it reuses ("## Existing code"). Every claim on #422 names the existing code it builds on
    ("Existing:"). `python3 scripts/dup_lint.py check` runs in CI and must pass: a pasted block or a new copy of a known
    helper fails. A real need for a copy is marked `dup-allow: <reason>` on the line and explained in the PR. When a dedup
    lands, shrink the baseline with `--update-baseline` in the same PR; the baseline may only go down.

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
24a. **Exception: Grok may write our own build tool and our own compiler (owner 2026-11-17: "Yes please", then "start
    building our own MSVC and Clang too ... we are not replacing them any time soon, its so i have something to do").**
    Only in `tools/okaybuild/` (build tool, `docs/platform/own_build_plan.md`), `tools/okaycc/` (our own Clang-style compiler,
    `docs/platform/own_compiler_plan.md`) and `tools/okayms/` (our own MSVC-style compiler, linker and Windows toolchain,
    `docs/platform/own_msvc_plan.md`), on `grok/*` branches it pushes itself, never on main. Everything else in rule 24 still
    holds for the rest of the repository, and rule 18 still holds (no cryptography). The code must be its own, written
    from language standards, object-file and CPU manuals and general compiler ideas, with no code taken from CMake,
    Ninja, GCC, Clang/LLVM, MSVC, tcc or any other project (rule 19a). Both stay OFF: not in CMake, CI or releases, and
    nothing in the engine depends on them, until the owner says so. The coordinator's agents run the tests, read the
    result, and open and merge the PR.
24b. **Exception: experimental Xbox One export through Microsoft's public Windows SDK and UWP tools (owner 2026-11-18:
    "For Xbox I'll allow it").** The Windows SDK, Visual Studio UWP tools, MakeAppx and SignTool are build tools the owner
    installs on their own PC; nothing from them is copied into the repository or shipped. Only the public SDK: no GDKX
    or other restricted Xbox code. Code lives in its own isolated folder behind a CMake option that is OFF by default,
    and existing Windows export must keep working. Say "unverified" for anything not built and packaged on Windows, and
    never claim console hardware support without a test on a real console. Rule 19a still holds for everything else.
24c. **Exception: C# scripting through the Microsoft .NET runtime (owner 2026-11-19: "Yes go ahead with c#"; the owner also
    decided to replace our own OkayScript languages with C# and Lua, and Visual Script stays the main way to script).**
    The .NET runtime (CoreCLR, MIT licence) may be loaded at run time through its documented hosting API (hostfxr) to run C#
    scripts, and the Roslyn compiler of a .NET SDK the developer installs may be run as a build tool, like MSVC (rule 24b).
    Nothing from the runtime, the SDK or NuGet is copied or linked into the repository; our hosting code and our managed API
    assembly (`OkayScripting.dll`) are written by us. It is opt-in per project behind a CMake option that is OFF by default;
    a game with no C# scripts ships no runtime. Exported C# games ship the runtime files with their MIT notice in
    `docs/status/THIRD_PARTY.md`. Windows only (rule 14). Say "unverified" for anything not built and run on Windows. This allows no
    other SDK or library, and rule 19a still holds for everything else. Lua is written by us from the Lua manual (no Lua
    code). Our own OkayScript is deleted only after the replacements work and the owner confirms (plan:
    `docs/scripting_migration.md`).
24d. **Exception: Microsoft's public Windows SDK and Visual Studio build tools for Windows work (owner 2026-11-20: "Yes",
    after "I'm gonna have other guy start working on windows sdk"; widens rule 24b beyond Xbox).** They may be used as build,
    test and debugging tools on Windows machines and CI runners: building and testing Windows programs (including an MSVC
    build job), the Direct3D debug layer, PIX and other SDK debugging tools, and SignTool for code signing (the certificate
    goes in GitHub secrets, never in chat or the repo, rule 15). Nothing from the SDK or Visual Studio is copied into the
    repository or shipped, apart from files Microsoft marks as redistributable and only with the owner's approval. The
    release build stays the MinGW cross-compile until the owner approves a switch. A feature that only works with the SDK
    sits behind a CMake option that is OFF by default, and code must keep compiling with the MinGW release compiler (rule
    21a). Only the public SDK: no GDK, GDKX or other restricted or separately licensed Microsoft SDKs without asking. Say
    "unverified" for anything not built and run on Windows. Rule 19a still holds for everything else.
25. **Claude 2 and Claude 3 report their usage.** With each result each of them posts `Usage: about N% left` on issue #422, and
    `HANDOFF SOON` at about 20% and `HANDOFF NOW` at about 5%, naming its branches and what is unfinished, so the
    coordinator can take its branches over. Each pushes only its own lane's branches (`claude2/*` or `claude3/*`), and starts a
    piece of work only after posting "Claim: <branch>" with "Existing: ... / Gap: ..." (rule 2).
