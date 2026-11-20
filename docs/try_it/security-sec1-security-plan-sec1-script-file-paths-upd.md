---
id: security-sec1
row: | security-sec1 | Security plan SEC1: script file paths, update signature status, relay caps, reactions SQL | examples\security_paths\script_paths.okayscene (Play, read Console); Settings > About and Help > About line; SQL and relay are owner steps | Tests on Linux only; editor/launcher lines not run; nothing on Windows or real Supabase |
---
## Security plan SEC1: script file paths, update signature status, relay caps, reactions SQL

Details and the reasons: [security.md](online/security.md). Four protections; two you can see in the Windows release, two are owner steps.

**Script file paths (try it in the editor).**

1. Open the editor from the Windows release, **File > Open...**, open `examples\security_paths\script_paths.okayscene` (in the `examples`
   folder next to the program) and press Play.
2. Open the Console. You should see seven lines: the first six file names (`NUL`, `con.txt`, `C:/x`, `slot.okaysave:hidden`,
   `../escape.okaysave`, `/etc/evil.okaysave`) end with `false`, and `slot_ok.okaysave` ends with `true`. Above them the Console
   shows `[script] save file: refusing file 'NUL': a reserved device name (CON, NUL, COM1 ...)` and one like it per refused name.

**Update signature status (visible now, OFF until you add a key).**

1. Open the launcher, **Settings > About**: under the version you should see `update signature check: OFF (no key configured)` in amber.
   In the editor, **Help > About** shows the same line. (Start the launcher from a terminal, or open `okay_update.log` beside
   `OkaySpace.exe`, to see it printed at start-up too.)
2. To turn it ON: add the GitHub secret `OKAY_UPDATE_SIGNING_SEED` and the variable `OKAY_UPDATE_TRUSTED_KEYS` (steps in security.md section 3),
   release, install that release: the line turns green and says `ON (1 key built in)`.

**Relay caps and reactions SQL (owner steps, nothing to click in the Windows release).** The relay is a Linux server program; its new
caps are checked by `ctest -R relay_security`. The reactions fix is `docs/sql/reactions_privacy.sql`: Supabase dashboard > SQL Editor >
paste it > Run. Check it with `select * from public.reaction_counts('dm', array[1]);` (no user column in the answer).

**Unverified.** Checked on Linux only: the tests `path_safety`, `script_paths`, `update_security`, `update_signature`, `relay_security`,
`sign_release_script`, and `reactions_sql` against a throwaway local Postgres. The editor and launcher lines are code that compiles
in the build but was not seen on screen; nothing ran on Windows or against your real Supabase project.
