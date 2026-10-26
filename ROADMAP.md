# OkaySpace roadmap

What is being worked on right now and what comes next. Kept current by the main
(coordinating) Claude session; each item links its PR once one exists. Every PR
merges once its five CI jobs are green, and a release follows.

_Last updated: 2026-10-26 15:55 UTC_

## Releases

| Version | What's in it |
|---|---|
| 2.830.0 | Step-up only on real steps (#338); grid inventory fixes (#339); even frame timing (#341); controllers before animation (#342) |
| 2.829.0 | Launcher: delete or remove projects from the Create page (#334); inventory: no dupes or item loss (#335); smooth third-person camera (#336); first-person camera without shake (#337) |
| 2.828.0 | Remote players move smoothly (#330); lag-compensated hits (#331) |
| 2.827.0 | 2D controllers driven by inputs (#327); client prediction (#329) |
| 2.826.0 | First- and third-person controllers driven by inputs (#326) |
| 2.825.0 | Opt-in fixed network tick (#323); server-authoritative movement (#324) |
| 2.824.0 | Account token only to hosts that check sign-ins (#320); anti-cheat phase 4 (#321) |
| 2.823.0 | Anti-cheat phase 3: sealed join, pinned host keys, clock heartbeat (#318) |
| 2.822.0 | Imported-character glitch fixes (#316); anti-cheat phase 2, relay (#315) |
| 2.821.0 | Anti-cheat phase 1 (#311) and P3/P4 (#269, #270); animation part 2 (#305, #308, #312); 2.5D crouch and prone (#313) |

## Done

- **Anti-cheat plan, phases 1–5.** All merged (#311 → #331). The database side
  (`docs/sql/anticheat.sql`: reports, bans, appeals) is applied.
- **Default player walk and run** (#301), and every controller drives the gait.
- **Animation part 2**, items 1–18 (#299, #305, #306, #307, #308, #312).

## Now

| Session | Plan | Current |
|---|---|---|
| Main | Coordination, merges, releases | Merging and releasing as PRs go green |
| QA | Camera and animation smoothing, PRs 1–6 | PR 6: online corrections (#344) |
| Humanoid | IK, animation and humanoid rigging, PRs 1–8 | PR 1: a visible Animator on every built-in player, with baked clips |
| Robustness | Inventory, PRs 1–9 | PR 3: scene wiring (nearest chest/bench, who is the player) |

### Camera and animation smoothing (QA)
1. [x] Third-person camera: smooth the pivot, not the orbit; sphere-cast spring arm that ignores triggers and the player's own parts (#336).
2. [x] First-person camera: eye smoothing over steps, continuous head bob, angle-based look smoothing (#337).
3. [x] Step-up only on real steps: no hops on ramps, seams or pickups (#338).
4. [x] Even frame timing in the editor and the player (#341).
5. [x] Controllers update before animation; spike guard in ModelAnimator (#342).
6. [ ] Online: eased corrections and a clean cut on a snap.

### IK, animation and rigging (Humanoid)
1. [ ] Animator component on the built-in players; procedural animation baked into clips. Takes the gait smoothing and per-pair crossfade times from the camera plan.
2. [ ] Smooth stance changes: stand, crouch and prone, moving in any direction.
3. [ ] IK core correctness: LimbIK, ChainIK, FootIK, FullBodyIK.
4. [ ] Controllers: ground probe (no apex double jump), eased step-up, rotating platforms, 2D one-shot clips. After camera PR 3.
5. [ ] One shared humanoid bone mapper that rejects non-humanoids.
6. [ ] Rest-pose normalisation and retargeting by avatar slot.
7. [ ] Animator graph and preview for every character; import a clip into a state.
8. [ ] One-click humanoid import with the default library on any rig; character script API.

### Inventory (Robustness)
1. [x] Correctness: no dupes, no item loss, max stack respected (#335).
2. [x] Grid fixes (#339).
3. [ ] Wiring.
4. [ ] One shared interaction layer.
5. [ ] Item definitions.
6. [ ] Gameplay features.
7. [ ] Scripting.
8. [ ] UI polish.
9. [ ] Networking: server-authoritative, on the anti-cheat RPC checks.

## Waiting on Iman

- **Join tickets (anti-cheat 3e).** The `session-ticket` edge function needs a
  signing key that Iman creates and adds to Supabase.
- **"server_check" report kind.** A one-word addition to `anticheat.sql`.
- **Nightly clean-up of old anti-cheat records.** Needs the pg_cron extension
  turned on in Supabase. Until then, run `select public.ac_purge_old();` by hand.

## Later

- In-game UI plan: P0 item 3 (batching, caching) and P1–P4.
- Script language: classes; `GetComponent<MyScript>()` with a per-scene script library.
- Procedural object holding (hand IK, grips, multiplayer sync).
- Split the largest source files (editor, launcher, OkayScript VM).
