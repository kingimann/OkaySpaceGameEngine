# Work board

**Live version (rewritten every 10 minutes from GitHub, open PRs and CI state, branches, the #422 thread):** [WORK_BOARD.md on the work-board branch](https://github.com/kingimann/OkaySpaceGameEngine/blob/work-board/WORK_BOARD.md) (written by `scripts/work_board.py` and the workflow `.github/workflows/work-board.yml`). This page is the hand-written part: agents, owner decisions.

A snapshot of what the main session, its agents and the second Claude (`claude2/*`) are doing. It does not update by
itself: the main session rewrites it as work moves (the live copy is the "Engine Work Board" artifact). Snapshot time:
**2026-11-15, about 02:10 UTC**. Anything here can be out of date by the time you read it; the pull request list and
issue #422 are the source of truth.

Rules that govern it (`CLAUDE.md`): always be working on a feature, a bug, a third-party replacement, or checking on the
second Claude (rule 2); a PR merges only when every CI job is finished and green, Ubuntu included (rule 6).

## Working on right now

| Work | What | Branch |
|---|---|---|
| Streamed WAV and RF64 | Own audio decoder, next step; off by default | `claude/own-wav-stream` |
| Readers e2: Q3D, RAW | Fixing review findings (huge-line memory, slow polygons, test-name clashes) | `claude/readers-e2` |
| Readers e3: IQM, COB | Fixing review findings (slow loop, memory limit, feature map, provenance wording) | `claude/readers-e3` |
| Multiplayer gaps | Smallest unfinished items, starting with deaths not sent to other players | `claude/mp-*` |
| Flaky timing tests | Make speed-dependent tests (such as `ui_render_bench`) reliable | `claude/flaky-timing-tests` |
| Readers e4 fixes | OpenGEX and MDL: review said fix first (two denial-of-service paths: unbudgeted corner and animation loops, per-placement bone copies; duplicate node names; skin layout to check against the spec; provenance wording). Starts when a build slot is free | `claude/readers-e4` |

## Open pull requests (none red at snapshot time)

| PR | What | Next |
|---|---|---|
| #866 | Own Terragen reader (off by default) | Merge first of the readers when all jobs are green |
| #861 | Turn interpolation with a real-controller test | Read the Ubuntu result (player test) before merging |
| #870 | Release 2.872.0 (Admin page fix) | Merge, dispatch the release once, check the three checksums |
| #868 | CLAUDE.md rules 2 and 6 | Merge when green |
| #864 | Launcher startup update (draft) | Ready for review when green |
| #867 | LWO UV rewrite (draft) | Ready for review when green |

## Finished branches waiting for a PR

| Branch | What | State |
|---|---|---|
| `claude/readers-e1` | MDC, HMP, CSM readers | Review fixes done; goes after #866 |
| `claude/readers-e5` | PMX, LWS readers | Review fixes done; docs need the new limits |
| `claude/readers-e4` | OpenGEX, MDL readers | Under review |
| `claude/launcher-login-gate` | Launcher requires sign-in | Fixes done; PR opens after #864. Owner decision: on for all users in one release? |

## Second Claude (`claude2/*`)

| Branch | State |
|---|---|
| `claude2/obj-texture-roots` | Security fix for OBJ texture paths. Review said fix first (network paths touched before the check, unvalidated project folder); waiting for its fixes |
| `claude2/own-reader-gltf1` | Review fixes verified; mergeable once CI is green; PR goes after the other readers |
| `claude2/reduce-motion-bob`, `claude2/navmesh-mesh-collider` | Building and testing |
| `claude2/own-reader-unreal3d` | Parked draft; needs a provenance note before any PR |

Claude 2 writes its remaining usage at the top of each report and posts `HANDOFF SOON` at about 20 percent left.

## Merged this session

Release 2.871.0 (checked on GitHub and Supabase, checksums match), #852, #858, #860, #862, #863 (Admin page fix),
#865 (own WAV decoder), #869 (CI apt retry).

## Waiting on the owner

- Branch protection: the required-checks list for `main` is empty, so GitHub still allows red merges.
- Whether the login gate ships to every user in one release (it only restricts the launcher, not the engine programs).
- The Silo (SIB) reader needs a published spec or the sample file `heffalump.sib`.
- Whether the MDC constants recalled from memory are acceptable.

## Machine limits

About 13 GB free disk (stop at 5 GB), about 13 GB memory available. At most four compile jobs at once, builds run with
one job each, build folders are deleted after use.
