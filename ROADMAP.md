# OkaySpace roadmap

What is being worked on right now and what comes next. Kept current by the main
(coordinating) Claude session; each item links its PR once one exists. Every PR
merges once its five CI jobs are green, and a release follows.

_Last updated: 2026-11-09 22:15 UTC_

## Releases

| Version | What's in it |
|---|---|
| 2.855.0 | Projects and saving: per-project settings and build files, all-or-nothing create and save, unsaved-changes prompts everywhere, New Project dialog, save status in the status bar, no saving or shipping a running game, every collider and audio source saved (#607); loop and time fifth pass: script access to the clock, thread-safe Random, saved ramps, replay of pauses and random state, TimeBudget (#603); timers and tweens fifth pass: nested updates, pause-menu rule, sequence cleanup, Tween Animation speed/decay/tag, ease_curve, seven visual-script tween nodes (#604); scene graph fifth pass: component order for every object, fast long child lists, pool reset, clone-safe joints, FindAllByPath, look-at helpers (#605) |
| 2.854.0 | Updater restarts the app after install, keeping arguments and paths with spaces (#584); loop and time fourth pass: TogglePause under a menu pause, session clocks, run N frames and slow-motion presets, shared overlay text, random sampling and saved bags, 13 fixes (#594); timers and tweens: O(1) scheduler, double-precision tween timeline, typed and tagged tweens, timer debug view, 10 script builtins (#595); scene graph: cached active state, linear destroy, deferred Clear, saved component order, zero-scale inverse (#596); docs index and properties page (#593); Windows build retries SDL2 and libsodium downloads (#592) |
| 2.853.0 | Editor on the new core: Play on the shared game loop, project tag and layer lists with rename, Duplicate keeps the parent, tween clock and ease-curve fields (#590); feature map for the core 1.1 to 1.3 wave (#589) |
| 2.852.0 | Loop and time third pass: player on the shared game loop, Countdown and Cooldown, time snapshot, frame stats CSV and console (#585); timers and tweens: owner-safe timers, clock channels, springs, ease curves, tween any property (#587); scene graph: deep hierarchies, Transform and spawn API, project tag and layer lists, collision presets (#586) |
| 2.851.0 | Roomier editor layout across the shell, Visual Script editor and Script Editor (#577); frame stepping, frame recorder and player, capture mode, frame stats, server tick pacer (#581); scene text tools learn the gravity3 record (#582); feature map v2 (#576) |
| 2.850.0 | Shared game loop with hitch handling, clocks, hit stop, pause and quit reasons, random helpers (#579); nested prefabs and variants, property registry, scene diff and merge, job system and arenas (#578); scene hierarchy, search and validation, exact float round trip (#575); profiling data, optimization tools, localization tools, accessibility (#574) |
| 2.849.0 | Scenes load all-or-nothing and in the background, with pooling, GUIDs, prefab overrides, layer matrix and camera stack (#553, #570, #558); durable saves (#552); asset manager and asset database (#568); core wave 4: math, CLI, localization, ECS, input (#571); undo/recovery, test framework, save framework (#572); log, profiler, timers and tweens (#559, #560); core bug audit (#557, #565) |
| 2.848.0 | Speed fit for every player controller (#561); strafe blend (#537); IK follows state weight (#535); more script builtins, navmesh calls and music, camera, particle and inventory visual-script nodes (#556, #562, #563, #566); build-cost and refund fixes (#564) |
| 2.847.0 | Imported characters with their own Walk and Run clips walk on W and run on Shift (#541); a new script starts as an empty Start and Update (#543); Windows exes carry their own name, description and version (#545); docs index, a README checked against the code and the feature map (#544, #546) |
| 2.846.0 | Built-in walk: no more hip snap at every footfall, which was the leg jitter and head bob (#540) |
| 2.845.0 | VRoid, Character Creator and Daz characters animate; a Mixamo character with its own clips no longer stays low after a crouch; rig sweep test (#528) |
| 2.844.0 | Animator graph keeps the built-in idle, run, jump and stride matching for walk-only models (#531); blocky character jump crouch, turn steps and lean (#532); Make Playable Character right-click, the Set Character Model crash fix, and the ik_look_at, crouch and stand script calls (#533); walk-only jump test |
| 2.843.0 | In-game UI fixes: one press does one thing, clickable stretch widgets, a working Settings screen, menus and HUD that fit 16:9, 4:3 and 9:16, Quality High keeps the sky (#529) |
| 2.842.0 | One Player Controller for first and third person (#523); stride-matched Mixamo locomotion, per-slot built-in fallback and the Character Model panel (#524); riders turn with rotating platforms (#526) |
| 2.841.0 | Navigation meshes (#514); cutscenes (#515); dialogue and quests (#516); behavior trees (#517, #519); post-processing, sky, fog and weather (#518); localization (#520); map a rig's body bones by hand (#522, IK plan item 8) |
| 2.840.0 | First-person hands, view switch and the third-person head fix (#505); save games (#498); built-in action clips (#506); ragdolls (#501); IK batches 1–2: Look-At shares, clamp, eyes and range, Limb and Full Body stretch, foot toe/heel rays, keep-phase transitions (#507, #508); Animation window key lane (#509); older starter projects get both view controllers on load (#510); Animation tab clip editor: dope sheet, curves, record, onion skin, clip list (#511); Animator window: canvas, parameters, blend trees, inspectors, layers (#512) |
| 2.839.0 | Smoother built-in walk: fast walks ease into a jog, baked cycles on a curve (#500); an Animator graph for imported characters' own clips (#499, IK plan item 7); net_interp fix: no near-second lag after an object stands still (#502) |
| 2.838.0 | Audio: stereo mixer, OGG/MP3, music player, saved volume settings, Audio Mixer (#492); input actions, rebinding and full gamepad (#494); action framework (#435); bone mapper (#493); grounded stops (#480); IK guide (#424); inventory tooltips and host-owned bags (#426, #481) |
| 2.837.0 | IK plan PR 7: one solve order, goals that swing (#417); bind-pose recovery and held props on model swap (#416); Script Editor undo for Format/Rename/Replace All/Reload (#445); Plugins menu with several plugins (#479) |
| 2.836.0 | Networking catch-up, rewind, snapshots and interest (#401, #403, #407, #408, #423, #427, #428); animation missing-clip and jump hold, gait crossfade (#404, #410, #419, #459); FullBodyIK restore (#414); sandbox swim, throw, and fill without zoom (#439, #443, #448); OkayScript classes, events, nullish, named args, play buffer, arrow axes (#454, #455, #456, #458, #460, #462, #465, #474); visual-script drag, pin filter, node docs (#463, #473, #475); UI themes, options, toasts, controls, HUD and color (#452, #457, #466, #470, #472, #476); Brick Breaker (#477) |
| 2.835.0 | IK plan PRs 2d–5: own crouch/prone clips (#385), joint limits (#396), ChainIK (#398), FootIK turns bones (#412) and filtered ground probes (#413); Look At/Aim at any frame rate (#418, #433); sandbox hands (#430); Animator preview (#429); first/third-person toggle (#434); apex and step fixes (#409, #411, #436); voxel and heightmap digging (#440, #437), sandbox lake (#438); inventory 6b–7a (#367, #378, #383, #387); Scene Manager (#415); Script Editor outline (#442); NetworkObject/SyncVar/RPCs (#431), net simulator (#432), authoritative pawn and server flag (#400, #405, #406) |
| 2.834.0 | Directional crawl, get-up/drop (#371) and crouch/prone on every controller (#375); Limb IK turns bones with a steady pole (#388, IK PR 1 #395); holding body adjustment (#389) and the prone-holding fix (#397); inventory pickups/drops, encumbrance, loot (#359); UI P1a/P1c frames, modals, gamepad + touch (#381, #386, #391); typed OkayScript lambdas + LINQ (#379); relay voice on v3 hubs (#393); anti-cheat: bans at ticket issue (#350), bound tickets (#360), SQL hardening (#392), join gating (#365), reconnect strikes (#363), limits (#368), safe defaults (#384), lag-comp cap (#376), pings/rate/press limits (#390), trusted keys (#394) |
| 2.833.0 | Smooth stance changes (#364, IK PR 2a); fixes: walking in place at a wall (#366), one-shot replay in a fade (#380), Euler flip near straight up/down (#370); object holding PRs 1–2 (#362, #377); item definitions (#358); cached, batched UI (#361) and UI P1b layout (#374); typed OkayScript `??`/`?.`, classes, collections (#354, #369); hardening PRs 4 and 11 (#355, #372); release tags on the built commit (#373); CI cancels superseded PR runs (#382) |
| 2.832.0 | Visible Animator on the built-in players (#346) and its inspector (#348), IK plan PR 1 done; one shared inventory interaction layer (#349); anti-cheat hardening PRs 1–3 and 7: safe spawns (#351), pinning (#352), hit claims (#353), relay cookies (#357); CI toolchain cache (#356) |
| 2.831.0 | Online camera corrections (#344, camera plan done); nearest chest/bench, slot count, scene wiring (#343); Animator backend (#340) |
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
| QA | Anti-cheat hardening, PRs 1–4, 6, 8–14 | PRs 8, 9, 10, 12 (#363, #365, #368, #376) merging main back in after #372; PR 6 (#360) waits on Iman's OK; PR 13 next |
| Humanoid | IK, animation and humanoid rigging, PRs 1–8 | Items 1–5 and 7 done; item 6 (rest-pose normalisation) is next |
| Release | Anti-cheat hardening, PRs 5 and 7 | PR 7 merged; relay follow-ups (voice calls on v3 hellos, sessions per address); PR 5 (#350) waits on Iman's OK |
| Robustness | Inventory, PRs 1–9 | PR 6a–6c (#359, #367, #378): pickups and drops, bag between scenes, equipment |
| UI | In-game UI plan and component library | P1a UIFrame (#381), then P1c: modals, context menu, keybind field, 2-D navigation, touch |
| Scripting | Script language to C# level | P1.6 lambdas (#379), then events and inheritance |
| Holding | Procedural object holding | PR 3: body adjustment, then PR 4: multiplayer |

### Camera and animation smoothing (QA)
1. [x] Third-person camera: smooth the pivot, not the orbit; sphere-cast spring arm that ignores triggers and the player's own parts (#336).
2. [x] First-person camera: eye smoothing over steps, continuous head bob, angle-based look smoothing (#337).
3. [x] Step-up only on real steps: no hops on ramps, seams or pickups (#338).
4. [x] Even frame timing in the editor and the player (#341).
5. [x] Controllers update before animation; spike guard in ModelAnimator (#342).
6. [x] Online: eased corrections and a clean cut on a snap (#344).

### IK, animation and rigging (Humanoid)
1. [x] Animator component on the built-in players; procedural animation baked into clips: 1a backend (#340), 1b default graph and baked clips (#346), 1c inspector (#348).
2. [x] Smooth stance changes: stand, crouch and prone, moving in any direction. 2a core (#364); 2b animation (#371); 2c every controller (#375); 2d imported humanoids (#385).
3. [x] IK core correctness: LimbIK, ChainIK, FootIK, FullBodyIK (#417, #507, #508).
4. [x] Controllers: ground probe (no apex double jump), eased step-up, rotating platforms, 2D one-shot clips (#409, #411, #436, #526).
5. [x] One shared humanoid bone mapper that rejects non-humanoids (#493, #522).
6. [ ] Rest-pose normalisation and retargeting by avatar slot.
7. [x] Animator graph and preview for every character; import a clip into a state (#499, #531). A graph still uses the built-in fallback and stride matching for empty slots.
8. [ ] One-click humanoid import with the default library on any rig; character script API.

### Inventory (Robustness)
1. [x] Correctness: no dupes, no item loss, max stack respected (#335).
2. [x] Grid fixes (#339).
3. [x] Wiring (#343).
4. [x] One shared interaction layer (#349).
5. [x] Item definitions (#358).
6. [ ] Gameplay features.
7. [ ] Scripting.
8. [ ] UI polish.
9. [ ] Networking: server-authoritative, on the anti-cheat RPC checks.

### Anti-cheat hardening (QA, Release), from the 2026-10-26 audit
1. [x] Only safe, registered prefabs can be network-spawned (critical) (#351).
2. [x] Host-key pinning can't be skipped by a plaintext Welcome, Reject or Kicked (#352).
3. [x] Hit claims need a known weapon and are rate-limited (#353).
4. [x] The hit origin isn't taken from positions the client can bank (#355).
5. [ ] Bans enforced when a join ticket is issued; no service_role key for game servers (Release).
6. [ ] Tickets bound to one server, one client key and one connection.
7. [x] Relay hellos need a cookie; caps per destination (Release) (#357). The live relay needs redeploying to get it; launcher voice calls still send old hellos, so keep legacy hellos on.
8. [ ] Budgets and strikes survive a reconnect; rejoin cooldown after a kick.
9. [ ] Join checks: encryption first, rate-limited password and sign-in checks.
10. [ ] SyncVar, message and spawn limits.
11. [x] Unowned world objects belong to the server (#372).
12. [ ] Lag-compensation inputs the client can't stretch.
13. [ ] Safer defaults and a config that fails closed.
14. [ ] Small fixes (self-unban, trusted server reports, pings, press intervals, CI key check).

## Waiting on Iman

- **SQL and Edge-function changes in the hardening plan** need Iman's OK before they merge: PR 5 (#350, bans checked in `session-ticket`), PR 6 (#360, tickets bound to one server and client), and PR 14 when it's up.
- **Join tickets (anti-cheat 3e).** The `session-ticket` edge function needs a
  signing key that Iman creates and adds to Supabase.
- **"server_check" report kind.** A one-word addition to `anticheat.sql`.
- **Nightly clean-up of old anti-cheat records.** Needs the pg_cron extension
  turned on in Supabase. Until then, run `select public.ac_purge_old();` by hand.

## Later

- Split the largest source files (editor, launcher, OkayScript VM).
