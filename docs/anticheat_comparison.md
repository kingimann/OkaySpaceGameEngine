# Anti-cheat compared with the commercial ones

An honest, approximate comparison of what OkaySpace's anti-cheat does and what the
well-known commercial anti-cheats are generally understood to do. It exists so you can decide
where to spend effort, and so nobody (including the people who ship games on the engine)
thinks this is something it isn't.

**How much to trust the right-hand column.** Everything said about Easy Anti-Cheat, BattlEye,
Vanguard, VAC and Ricochet comes from general public knowledge: their own public statements,
developer talks, press coverage and what security researchers have published. It was **not
verified against vendor documentation while writing this**, vendors don't publish
their detection methods (on purpose), and they change what they do. Where a claim rests on memory
it says *not verified*. Treat every cell about a commercial product as approximate and check the
vendor's own pages before quoting it. Everything said about OkaySpace cites a file in this
repository.

## The short version

- The commercial anti-cheats are **large, continuously updated services**: a kernel driver (most
  of them), a user-mode component, a cloud backend with teams who study cheats and ship
  signatures within hours or days, and ban operations. OkaySpace is a **small, opt-in, user-mode
  library plus a server that doesn't trust the client**. That is a different category, not a
  weaker version of the same one.
- What OkaySpace can do well is the **server side**: the server owns positions, hits and
  inventory and refuses what isn't possible ([anticheat.md](anticheat.md), P1 and Gameplay
  validation). That is also where the commercial products put their strongest, hardest-to-evade
  checks (not verified in detail).
- A client check can always be removed by someone who controls the client. In user mode, without
  a driver, it can be removed by someone who controls the machine. Everything client-side here
  raises the cost of off-the-shelf tools; none of it proves a player is innocent, and none of it may
  be the only layer.
- **Kernel drivers are out of scope, deliberately.** A driver is a security liability (a signed
  driver with a bug is a way into the kernel for any malware; see the privacy and legal
  section), needs a code-signing certificate and a signing relationship with the platform vendor,
  needs a team to keep it working across Windows updates, and costs players' trust. A hobby-scale
  engine should not ship one.

## Category by category

"Today" cites the code. "This PR" is the work on the branch `claude/anticheat-client`.
Effort: S = days, M = weeks, L = months or a team.

### 1. Kernel-level protection

- **What they do:** Most of the big ones (Easy Anti-Cheat, BattlEye, Vanguard, Ricochet) are
  generally understood to load a kernel driver that can watch handle access to the game,
  loaded drivers, and some memory operations that a user-mode program can't see, and to
  refuse to run next to known-bad drivers. Vanguard is reported to load at boot. VAC is generally
  described as user mode only. (All: not verified in detail.)
- **Today:** none. No kernel code anywhere in the repo.
- **Should we add it?** **No.** Reasons above: security liability, signing certificate, user trust,
  and a maintenance load that needs a dedicated team.
- **Effort if we ever did:** L plus an ongoing cost.
- **This PR:** nothing. The honest consequence: a cheat that runs in the kernel, or a hypervisor,
  can hide from every user-mode check here, including the new ones.

### 2. Memory scanning

- **What they do:** Scan the game's memory (and in some cases other processes) for cheat code,
  injected regions and known byte patterns; look at memory that isn't backed by a file. How deep
  this goes in each product is not public (not verified).
- **Today:** none of the scanning kind. The debugger and module checks
  ([shared/AntiCheatChecks.hpp](../shared/AntiCheatChecks.hpp)) look at the process's module list,
  not its memory.
- **Should we add it?** A narrow version, yes, report-only: hashing the game's **own** code
  sections is cheap and has few false positives. Scanning for executable memory that no module
  owns has more false positives (graphics drivers, overlays and some input methods allocate it)
  and was **not** done. Scanning *other* processes' memory is a privacy cost this project
  shouldn't take on.
- **Effort:** S for own-code hashing (done), M for an unbacked-executable-memory report.
- **This PR:** [shared/AntiCheatPe.hpp](../shared/AntiCheatPe.hpp) hashes the player's own
  executable sections against a build-time baseline;
  [shared/AntiCheatProtected.hpp](../shared/AntiCheatProtected.hpp) keeps health, ammo and
  currency-style values out of the reach of a naive memory editor. Neither is memory scanning of
  the cheat's side.

### 3. Signature and blocklists

- **What they do:** Keep large, frequently updated lists of known cheat files, drivers,
  and code patterns, pushed from the vendor's backend. VAC is generally described as working
  mainly this way, with delayed bans so the cheat's authors can't tell which build was caught
  (not verified).
- **Today:** a hard-coded allowlist of module locations and two known overlay names
  ([shared/AntiCheatChecks.hpp](../shared/AntiCheatChecks.hpp), `DefaultModuleAllowlist`,
  `KnownOverlayModules`). No blocklist, no update channel.
- **Should we add it?** A small, owner-maintained version, yes: it costs little and lets a game
  owner react to a cheat they've seen without shipping a build. It will always lag a vendor's
  research team.
- **Effort:** S.
- **This PR:** a signed module rules file (`okayspace-modules/1`: block and allow by file-name
  pattern and by code-signing subject), signed with the same ed25519 key as the manifest, read from
  `.anticheat/modules.json` so it changes without a rebuild; a build tool to sign it
  ([tools/okay_ac_tool.cpp](../tools/okay_ac_tool.cpp)). Matches are weighted signals, not bans.
  There are no hash-based rules and no automatic distribution of the file (that is the update
  channel, which belongs with the backend work).

### 4. Hardware, TPM and Secure Boot attestation

- **What they do:** Some require Secure Boot and a TPM on Windows 11 (Vanguard is reported to
  for Windows 11; Call of Duty's PC titles were reported to move to similar requirements; not
  verified). The aim is to make a driver-level cheat harder to load and a banned machine harder to
  disguise. Remote attestation through a TPM quote is the strong form; a program simply asking
  Windows "is Secure Boot on?" is self-reported and can be lied to by the same cheat.
- **Today:** a salted hash of machine id, volume serial and CPU brand
  ([shared/AntiCheatHardware.hpp](../shared/AntiCheatHardware.hpp)), sent only with a report. No TPM,
  no Secure Boot.
- **Should we add it?** Self-reported Secure Boot / TPM state as context in a report: possible but
  weak, and the privacy cost is real. True TPM remote attestation needs a verification service and
  vendor certificate chains; that is not worth building here. Not done.
- **Effort:** S for the weak form, L for the real one.
- **This PR:** nothing for this category. (The heartbeat attestation below is bound to the session
  key, not to hardware, and says so.)

### 5. Device bans

- **What they do:** Ban the machine as well as the account, using identifiers they don't
  publish, so a new account on the same PC doesn't escape. Cheat sellers sell "HWID spoofers", so
  this is a cat-and-mouse game for them too (not verified in detail).
- **Today:** `ac_bans` can name an account or a hardware hash;
  `ac_is_banned` is checked when a ticket is issued
  ([docs/anticheat.md](anticheat.md) "The backend", [shared/AntiCheatBackend.hpp](../shared/AntiCheatBackend.hpp),
  [docs/sql/anticheat.sql](sql/anticheat.sql)). A player with no machine id is banned by
  account only.
- **Should we add it?** Already there; it is as strong as the identifiers, and a spoofer defeats
  it. The ticket and backend work is another branch's lane.
- **Effort:** done; hardening it is M.
- **This PR:** nothing new.

### 6. Code protection and anti-tamper

- **What they do:** Obfuscate and often virtualise their own modules so they are hard to
  analyse, verify the game's own code and imports at run time, and check themselves. Games on
  these platforms commonly add a commercial packer too (not verified for any given title).
- **Today:** a signed manifest of every shipped file, checked at launch
  ([shared/AntiCheatIntegrity.hpp](../shared/AntiCheatIntegrity.hpp)); a light XOR obfuscation of
  data files that is explicitly **not** protection ([engine/include/okay/Core/DataPack.hpp](../engine/include/okay/Core/DataPack.hpp)).
  Nothing checks the code once it is loaded.
- **Should we add it?** Run-time checks of the game's own code, yes (cheap, open, honest).
  Packing and virtualisation, no: a commercial packer is the right tool and it is a build-pipeline
  decision, not engine code. (Packers also trigger antivirus false positives.)
- **Effort:** S to M.
- **This PR:** hash of the player's own code sections against a baseline made after the link,
  relocation-aware so it is the same at any load address
  ([shared/AntiCheatPe.hpp](../shared/AntiCheatPe.hpp)); an inline-hook check on a short list of
  system functions and a check of the import table
  ([shared/AntiCheatHardening.hpp](../shared/AntiCheatHardening.hpp)); extra debugger hints, including
  "the debugger API disagrees with the process block". All of it can be patched out by whoever
  patches the player, and all of it only produces weighted signals.

### 7. Server-side behavioural and ML analysis

- **What they do:** This is where the strongest, least evadable detection is generally said to
  live: statistical models over aim, movement, reaction and economy, often with machine learning
  and often applied after the fact so the cheat's authors don't learn what tripped it. Valve's
  public talks describe deep-learning on CS:GO demos; Activision describes server-side
  analytics in Ricochet (all: not verified in detail).
- **Today:** rule-based checks on the server: ownership, spawn allowlists, speed, SyncVar and RPC
  rate limits, hit validation with lag compensation, server-verified clock heartbeat
  ([docs/anticheat.md](anticheat.md) P1, Network hardening, Gameplay validation;
  [engine/src/Net/AntiCheat.cpp](../engine/src/Net/AntiCheat.cpp)). These are deterministic limits, not
  statistics, and there is no machine learning.
- **Should we add it?** Statistical behaviour analysis: yes, it is the highest-value place to
  invest and it needs no driver. Machine learning: only with real data volumes and labelled
  cheaters; a rules-and-thresholds approach is the honest start. Another branch owns this lane.
- **Effort:** M (statistical), L (ML).
- **This PR:** pure-logic **input analysis helpers** a server can call
  ([shared/AntiCheatInput.hpp](../shared/AntiCheatInput.hpp)): inhuman timing regularity
  (turbo, repeated delay patterns), impossible mouse deltas, straight-line cursor motion, and
  camera movement the input doesn't explain, each with synthetic human-like and macro-like tests
  and a measured false-positive rate. Not wired into the server (that is the detection branch's
  call).

### 8. Replay and review (Overwatch-style)

- **What they do:** Record matches or suspicious segments and have staff, or in one case
  trusted community members, review them (CS:GO's Overwatch; not verified whether and how it
  continues today). Review turns "a weight" into a decision someone can defend.
- **Today:** reports and appeals with an admin review screen
  ([docs/sql/anticheat.sql](sql/anticheat.sql), the launcher's Admin > Anti-cheat). No demo or
  replay recording.
- **Should we add it?** A server-side recording of the inputs and state around a flagged event
  would make weights reviewable; worth doing before any automatic punishment.
- **Effort:** L (recording, storage, a viewer, retention).
- **This PR:** nothing for recording. The input helpers return readable reasons so a reviewer can
  see why a trace was flagged.

### 9. Anti-DMA

- **What they do:** DMA cheats read and write game memory over a second computer and a PCIe
  card, so nothing on the player's PC sees them. Vendors reportedly look for the hardware,
  require IOMMU / kernel DMA protection, or rely on behavioural detection (not verified for any
  product).
- **Today:** nothing, and nothing user-mode could do reliably.
- **Should we add it?** No client check. The realistic defence is the server not trusting the
  client (a DMA user still has to send inputs the server can judge) plus behavioural analysis.
- **Effort:** n/a.
- **This PR:** the input analysis helpers are the relevant part: a DMA aimbot that drives the
  game through hardware input still produces input and camera behaviour the server can examine.
  The [protected values](anticheat.md#protected-values) are useless against DMA (the
  reader can find the key); said plainly.

### 10. Input-device analysis

- **What they do:** Detect macro and "hardware" cheats by the input they produce
  (suspiciously regular timing, impossible precision), and in some cases the devices attached
  (not verified).
- **Today:** none.
- **Should we add it?** Behavioural analysis of the input stream, yes. Enumerating the player's
  USB devices, no: a privacy cost with little payoff.
- **Effort:** S (helpers), M (tuning against real play).
- **This PR:** [shared/AntiCheatInput.hpp](../shared/AntiCheatInput.hpp), as above, with
  parameters the game sets for its own normal (quantum of the server tick, mouse dpi, assist).

### 11. Update integrity

- **What they do:** Ship their own modules signed, and keep them updated continuously from their
  servers; some refuse to run an out-of-date module.
- **Today:** the build manifest is signed and checked
  ([shared/AntiCheatIntegrity.hpp](../shared/AntiCheatIntegrity.hpp)); the official-game updater verifies
  size and SHA-256 ([shared/OfficialGame.hpp](../shared/OfficialGame.hpp)). Signed launcher and
  game updates are another branch's work.
- **Should we add it?** Yes, and it is being done elsewhere.
- **Effort:** M.
- **This PR:** the new data files (code baseline, module rules) use the same signing and the
  same trusted-key rules as the manifest, so a player with trusted keys refuses unsigned ones.

### 12. Privacy and legal costs

- **What they do, and what it costs:** A kernel driver sees a great deal of the machine. That has
  produced public controversy and some concrete incidents (a signed game driver was reportedly
  abused by malware in 2022; not verified in detail) and regulatory questions about hardware
  identifiers and data retention. The commercial vendors have legal and privacy teams. A small
  project doesn't, so it should collect less.
- **Today:** the checks are opt-in per game and per launcher setting. The only identifier sent is
  a salted hash of hardware ids, with a report ([shared/AntiCheatHardware.hpp](../shared/AntiCheatHardware.hpp));
  paths in a report are cut down to file names; reports are purged after 90 days
  (`ac_purge_old`, [docs/sql/anticheat.sql](sql/anticheat.sql)).
- **Should we add it?** The opposite: keep collecting little. A game that turns anti-cheat on should
  say so in its privacy notice, name what is sent and for how long it is kept, and let players ask
  for deletion. This is not legal advice; a hardware hash can count as personal data in some places.
- **Effort:** S (a notice), ongoing (process).
- **This PR:** adds no new identifiers. The hardening findings are text about the game's own
  process and the module *file names* in it (module paths are redacted to file names before they are
  sent). The heartbeat attestation carries a build id and a hash of eight booleans, no hardware data.

## What this PR adds, in one table

| Piece | File | Runs where | What it is for | What it can't do |
|---|---|---|---|---|
| Protected values | `shared/AntiCheatProtected.hpp` | client, opt-in per value | health/ammo/currency not findable or freezable by a naive memory editor | stop anyone who reads the code; stop DMA |
| Code baseline + hash scan | `shared/AntiCheatPe.hpp`, `tools/okay_ac_tool.cpp` | player (Windows probes), tool anywhere | notice a patched instruction in the player's own code | survive a cheater who patches the check or restores bytes before it looks |
| Inline-hook and IAT check | `shared/AntiCheatHardening.hpp` | player (Windows) | notice a hooked clock, debugger or loader API | see a hook below user mode, or a hook placed deeper than the first bytes |
| Signed module rules | `shared/AntiCheatHardening.hpp` | player, tool signs | block or allow modules by name or signer, without a rebuild | keep up with a vendor's research team |
| Debugger and hypervisor hints | same | player (Windows) | cheap extra context, API-vs-process-block mismatch | tell a debugging developer from a cheater |
| Clock cross-check | same | player, server time optional | a bent performance counter or tick clock | catch a cheat that bends every clock consistently |
| Heartbeat attestation | `shared/AntiCheatAttest.hpp` | server verifies, client answers | prove a live, keyed, current-build client answered | prove the answer is true |
| Input analysis | `shared/AntiCheatInput.hpp` | server (or a reviewer) | turbo, macros, impossible or robotic input, unexplained camera | prove intent; see a well-randomised tool |

## Where the gaps are, plainly

1. **No kernel, no hypervisor-level view.** A cheat below user mode is invisible here.
2. **No cloud-fed signatures.** A commercial vendor learns of a new cheat and ships a detection the
   same week; this project's owner can update a rules file, which is far smaller.
3. **No hardware root of trust.** Nothing here can tell a real machine from a spoofed one.
4. **Weights, not verdicts.** Nothing client-side in this PR ends a game or bans anyone by
   itself, in any mode. The server decides.
5. **No replay recording**, so a flagged player can't yet be reviewed the way the commercial ones do.

## Sources and confidence

No sources were fetched for this document. It is written from general knowledge of the vendors'
public positioning and common security-research coverage, as of the writing date. Specific numbers
and requirements (for example operating-system requirements) change, which is why the cells that
mention them say *not verified*. If a decision depends on one of them, read the vendor's current
documentation first.
