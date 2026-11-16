---
id: ac-cheat-sim
row: | ac-cheat-sim | Anti-cheat: watch the server catch a cheater (cheat simulator), ban list, audit log | `OkaySpacePlayer.exe --server --cheat-sim --log cheat_sim.txt` (Tools folder), then open the log | New in this change; tested headless on Linux over loopback; Windows and a real network unverified |
---
## ac-cheat-sim. Anti-cheat: watch the server catch a cheater (cheat simulator)

What this is: a self-test of the server's anti-cheat. It starts a server on your own machine (nothing goes on the
network) and a fake "cheating" client, and the cheating client does eight things a modified game would do. You read what
the server did. Details: `docs/anticheat.md` ("The cheat simulator"), the status table: `docs/anticheat_hardening_status.md`.

**How to try it (Windows release).**

1. Open a Command Prompt in the `Tools` folder of the release (next to `OkaySpacePlayer.exe`).
2. Run: `OkaySpacePlayer.exe --server --cheat-sim --log cheat_sim.txt`
   (The Windows player has no console window, so the output goes to the file. It needs no game and no scene.)
3. Wait about a minute. Open `cheat_sim.txt`. You should see eight blocks like
   `[speed] the rogue moves 60 units a second where 8 is the limit` followed by `CAUGHT: ...` and
   `the normal client next to it: untouched, still playing`.
4. Near the end you should see the ban list (a line starting `a:2130706433`), the audit log (lines like
   `2026-11-16T18:31:28Z ban who=3fa9c21b07 ...`; check that no name and no address appears in it) and the last line
   `Result: 8 of 8 cheats caught, normal players left alone: YES. The server's anti-cheat works.`
5. To run only some steps: `OkaySpacePlayer.exe --server --cheat-sim-only speed,flood --log cheat_sim.txt`, or set the
   environment variable `OKAY_CHEAT_SIM=speed,flood` and run `OkaySpacePlayer.exe --server --log cheat_sim.txt`.
6. The files the run wrote are in a new folder named in the first line of the log (`okay_cheatsim_...` in your temp folder).
   `okay_bans.txt` is the ban list: delete a line to lift a ban. `okay_anticheat_audit.log` is the audit log.
7. The same two files are kept by a real dedicated server that has the anti-cheat on
   (`OkaySpacePlayer.exe --server --anticheat my_rules.okayac`): `okay_bans.txt` and `okay_anticheat_audit.log` appear in its
   folder. `--no-ban-file` and `--no-audit-log` turn them off. A real cheater who gets kicked 3 times, or whose risk
   score passes `detect.banAt`, is refused at the join with "banned from this server".

**What is verified.** The test `anticheat_hard` runs the same code (all eight steps, the files, the no-personal-data check),
on Linux over loopback. **Unverified:** nothing was run on Windows or on a real network; the `macro` step hands the button presses to the
detector instead of sending real input packets; the simulator's packets are not encrypted.
