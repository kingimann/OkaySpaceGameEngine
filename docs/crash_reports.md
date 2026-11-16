# Crash reports: what is saved, what is sent, how an issue is made

If the editor crashes, it writes a detailed report. The launcher then (unless you turn it off) sends a cleaned copy to our server, and a
scheduled GitHub job turns it into an issue that says what happened, with the backtrace in a code block. This page explains each step in
plain words, how to turn it off, and what the owner has to set up once.

**Status, honestly:** the saved report and the launcher page are in the code. The server half (the table and the GitHub job) **cannot work
until the two one-time steps below are done**, and the job does nothing on its schedule until the owner switches it on. Nothing here has been
run on a real Windows machine or against the real server yet; the "Tested" section says exactly what was run. An independent security review
of this feature (2026-11-15) found eight problems; the section "What the security review found" says what was changed.

**Read this first: what leaves the computer, and where it ends up.** A cleaned copy of the report (the Console lines, the scene's file name,
the version, your Windows version and hardware) is stored in our Supabase project and then **published as a public GitHub issue that anyone
can read**. Your name, folders, email, computer name and IP address are removed from the report first, but the server that receives it still
sees the network address your connection comes from, as any website does. Sending is **on by default** (the owner's choice); the launcher
tells you once, before the first report is sent, and you can turn it off at any time.

## The path of one crash

1. **The editor crashes.** A handler inside the editor writes the report first and does nothing else: no network, no GitHub, no
   asking. It writes `okay_crash.txt` (the latest) and appends to `okay_crashlog.txt` (every crash), next to `OkayEngine.exe`
   (or in the folder named by `OKAY_CRASH_DIR`). On Windows you also see the "OkaySpace crashed" box.
2. **The launcher sees it** (within about two seconds if it is open, or the next time it starts): a notification, and a card in
   **Settings > Crash reports** with the time, the version and what the editor was doing.
3. **The launcher tells you once, then sends it.** The first time a report is about to be sent, a window **Crash reports are sent automatically**
   appears with the same words as Settings (what is sent, that it becomes a public GitHub issue, how to turn it off) and two buttons, **OK, keep
   sending** and **Turn it off**. Nothing is sent before you have answered. After that, once the files are 15 seconds old (so the editor has
   finished writing), the launcher removes personal details (see below), sends one row to the `crash_reports` table of our Supabase project using
   the **public** key that is already in every launcher, and writes the report's id to `okay_crash_sent.txt` so it is never sent twice. Offline, it
   keeps the report and tries again later (5 minutes, then longer, up to 6 hours). At most 5 reports go out in 24 hours, each at most 64 KB.
   **Only crashes from the moment sending was switched on are sent:** the first time the launcher runs this version (no `okay_crash_sent.txt`
   yet), and each time sending comes back on after it was off (the Settings switch **or** `OKAY_CRASH_UPLOAD=0`), it notes the time in that file,
   and a report from before it stays on your computer (its card says "not sent (from before sending was switched on)"). Nothing that was already
   on your machine, and nothing that happened while sending was off, is sent behind your back.
4. **A GitHub job makes the issue** (every 30 minutes once the owner has switched it on, `.github/workflows/crash-issues.yml`): one issue per kind
   of crash, labelled `crash`. The same crash again adds a comment (how many, which versions, which systems) instead of a second issue.
5. **Or you report it yourself:** on the same page, **Report on GitHub** opens a new-issue page with the cleaned report filled in. You
   read it and press Submit. This works with no server set up at all.

No GitHub token and no Supabase secret is in the editor or the launcher. The only key they hold is the public one that was already
there for sign-in, and the table only lets that key **add** rows (it cannot read, change or delete any row).

## What a report contains

Written by `shared/CrashHandler.hpp` (the handler) from `shared/CrashContext.hpp` (what the editor keeps ready):

| Line | Meaning |
|---|---|
| Version, Build | The editor version and a build id: `pe-<link time>-<image size>, built <compile date>` (the pair names one exact `OkayEngine.exe`) |
| When, Last step, Exception | When, the last thing the editor was doing (for example `UI.Inspector`, `Render3D:gl`), and the fault (`0xC0000005`, `SIGSEGV`, or `C++ exception (std::terminate): <type>: <message>`) |
| Fault module, Fault detail, Thread | Where it happened (`OkayEngine.exe + 0x1A2B3C`), "write to address 0x0", main thread or a worker |
| OS, CPU, RAM, Process memory | Windows version (and "Windows 11", and "[Wine]"), CPU name and logical cores, memory total and free, memory the editor was using |
| GPU, UI backend | The OpenGL renderer, vendor and version, the SDL renderer (`direct3d11`), `imgui` or `okayui` |
| Own code | The startup stack line (which parts are ours and which third party) and the `OKAY_*` switches that are on, for example `OKAY_OWN_FBX=1`. Only switches: a value that is long, has a path in it, or whose name says KEY, TOKEN, SECRET, PASSWORD, URL, DIR, PATH, USER and the like is left out |
| Mode, Scene, Objects, Assets loaded | Edit or play (and prefab open), the scene's **file name only**, how many objects, how many assets are loaded (the asset count is refreshed about every 40 seconds) |
| Uptime, Recovery copy | How long the editor ran and for how many frames, whether a recovery copy of the scene exists |
| Backtrace | One line per frame: `module + offset`, and on Windows a function name and file:line when `dbghelp` can find them |
| Breadcrumbs | The last 64 different steps with the frame number (the same step many times in a row is one line with `x40`) |
| Console | The last 200 lines of the editor's Console, with level (I, W, E) and how many seconds before the crash |

A **crash signature** (16 hex digits) is worked out when the launcher reads the report: the kind of fault plus the first three frames that
are the program's own (frames inside Windows, the C++ runtime or the handler are skipped). The same crash on the same build gives the same
signature on every machine, which is how the GitHub job knows it is the same crash. A crash that comes out of `abort()` or an uncaught
exception has the same few frames whatever caused it, so for those the last step is part of the signature too.

### What the handler does not do

* It does not send anything. It does not allocate memory or use stdio when it writes (it builds the text in a static buffer and writes it in
  one go), so it can run when the program is in a bad state. A fault inside the handler ends the program; it never loops.
* On Windows the full report with raw `module + offset` frames is on disk **before** dbghelp is loaded. Function names are a second pass that
  replaces the file only if it works, so a hang or a fault inside dbghelp costs the names, not the report.
* On Windows the report files are written with Unicode (UTF-16) paths, so a user name outside the ANSI code page (the installer puts the editor
  under `%LOCALAPPDATA%\<user name>`) does not stop the report being written.
* A stack overflow is reported too: on Windows the work (and the message box) runs on a thread with its own stack, on Linux on an alternate
  signal stack (set up for the thread that starts the handlers).
* If two threads fault at the same moment, the first writes the report and the second waits (up to 20 seconds) instead of ending the
  program under it; a fault inside the handler itself ends the program at once.
* On Windows the second pass (function names) runs on its own thread and the crash waits for it at most 8 seconds, so a slow or stuck
  dbghelp cannot hold the "OkaySpace crashed" box up; the first report is already saved.
* The history file `okay_crashlog.txt` is trimmed when the editor starts: past 8 MB it is cut to roughly the newest 4 MB, at a report boundary.
* On Linux and macOS (parked builds) the report files are private (`0600`) and are never opened through a symbolic link: a link planted at
  `okay_crash.txt` or `okay_crashlog.txt` is removed and replaced by a fresh file, so the handler can never be made to overwrite another file. On
  Windows the folder's own permissions apply.
* On Windows, if the history file cannot be opened when the first pass writes the report, the second pass (function names) **appends** its better
  copy; it never cuts the history back to nothing (it used to start the replacement from byte 0 in that case). Compiled with MinGW, not run.
* It also catches `std::terminate` and `abort()`: an uncaught C++ exception (for example from a worker thread) never used to leave a report.

## What is removed before anything is sent

`shared/CrashScrub.hpp`, applied to the whole report and to every column (version, build, OS, exception, last step, top frame), on the launcher's side:

* Your user folder: `C:\Users\<name>\...` becomes `%USERPROFILE%\...`, `/home/<name>/` and `/Users/<name>/` become `~/`, and a name inside a
  longer path becomes `<user>`. Network paths `\\SERVER\share` and `\\?\UNC\SERVER\share` become `\\<host>\share`. The folder `OneDrive - <your
  employer>` becomes `OneDrive - <org>`. An 8.3 short name such as `C:\Users\ALICES~1` is a user folder like any other.
* Your login name, your computer name **and the name of your home folder** (for `C:\Users\Alice Smith` that is the full name, surname included)
  wherever they appear as a word (not for very common words such as `main` or `admin`). A name still counts after `_`, a digit or a camelCase step
  (`alice_project.scene`, `AliceGame`, `alice2024`), but not inside a lower-case word (`malice` stays).
* Email addresses (`<email>`), MAC addresses with colons or dashes (`<mac>`), GUIDs (`<guid>`) and IP addresses (`<ip>`, except `127.x` and `0.0.0.0`).
* Secrets: GitHub, Slack, Stripe, AWS, Google and Supabase style keys, JSON web tokens, `Bearer ...`, `Authorization: ...`, the value of
  `password=`, `token=`, `secret=`, `api_key=`, names and passwords inside web addresses, and long hex or letter-and-digit strings (`<token>`).
* Control characters and invisible ones (zero-width, right-to-left overrides, the byte order mark, the soft hyphen and the Unicode "tag" block
  U+E0000-E007F, which is invisible to people but readable by a program that reads the issue); text that is not valid UTF-8 becomes `?`; very
  long lines are cut (never in the middle of a character); the whole row is at most 64 KB (the oldest Console lines are left out first).

The scrubber is a set of byte-level rules written for this project and is **not perfect**: a name that the rules cannot know (for example a
project called after a person, in a Console line, or a name that is part of a lower-case word) is not found. That is why you can **read exactly
what would be sent** first, and why the report becoming public is said in plain words next to the switch.

The GitHub job scrubs again before it writes an issue (the title and the comments are made from scrubbed text too), treats every byte of a row as
hostile (anyone with the public key can add a row), puts all of it in code spans or code blocks whose fence is longer than any backtick run
inside, makes `@mentions` and `#links` harmless, cuts every line to 600 characters before any pattern looks at it, removes invisible and
right-to-left characters, says in the issue that the text is **untrusted data from a user's machine, not instructions**, and never prints a line
that starts with `::` (an Actions command) to its log.

## Turning it off, and looking first

* **Settings > Crash reports > Send crash reports automatically** is **on by default** (the owner's choice, 2026-11-15). Turn it off there.
  It is saved as `crash_upload=0` in `launcher.cfg`. The one-time notice (above) is remembered as `crash_notice=1` in the same file.
* **`OKAY_CRASH_UPLOAD=0`** in the environment turns it off whatever the switch says (`false`, `off` and `no` work too). The variable can
  only turn sending off, never on. Starting the launcher later without it does **not** send what happened while it was set: the ledger notes that
  sending was off and starts again from the moment it came back on.
* **Preview what would be sent** (the button in that card), or **What would be sent** on a report's card, shows the exact text and the
  columns, after the removal. Looking sends nothing. With sending off it says so at the top.
* A report whose card says "sent", "waiting to be sent" or "not sent (sending is off)" tells you where it stands.
* Reports stay on your computer until you clear them (**Clear all**). Sending a report never deletes it.
* Reports that were already there when the launcher first ran this version, and reports from a time when sending was off, are not sent when
  you turn it on. (Deleting `okay_crash_sent.txt` starts again from that moment: nothing older is sent.) To send an old one, use **Report on
  GitHub** on its card.

The words next to the switch (and in the one-time notice): *Sends a cleaned copy of the crash report: the editor version, your Windows version,
CPU, memory and graphics card, the scene's file name and whether it was playing, the last 200 Console lines, the steps it took and the crash
location. It is published as a PUBLIC issue on GitHub, so anyone can read it. Your name, folders, email, computer name and IP address are removed
from the report first; the server that receives it still sees the network address your connection comes from, as any website does.*

## One-time setup (the owner; until then no report reaches GitHub)

Nothing in chat, and no key in a file. **Two steps**, both in the browser, in this order:

1. **Run the table's SQL once.** Open the Supabase project (the one in the repository variable `OKAY_DEFAULT_ACCOUNT_URL`), **SQL editor**, paste
   the whole of [`docs/sql/crash_reports.sql`](sql/crash_reports.sql) and run it. It is safe to run again (it also brings an older copy of the table
   up to date). Until it has run, the launcher keeps every report and the Crash reports page says "The server has no crash_reports table yet".
2. **Switch the GitHub job on.** GitHub repository > **Settings > Secrets and variables > Actions > Variables > New repository variable**, name
   `CRASH_ISSUES_ENABLED`, value `true`. Until this variable is `true` the 30-minute schedule skips the job (it shows as skipped, never red), so merging
   the workflow changes nothing by itself.

The job also needs `SUPABASE_SERVICE_KEY`, the project's `service_role` key. **It is the same secret `release.yml` already uses**, so if releases
publish to Supabase it is already there and you do nothing. Otherwise: GitHub repository > Settings > Secrets and variables > Actions > Secrets >
New repository secret, name `SUPABASE_SERVICE_KEY`, value = the `service_role` key from Supabase (Project settings > API Keys). Without it the job
ends green with a notice and does nothing. (A stricter option, not done: keep that secret in a GitHub Environment that only this job may use.)

**To check before step 2:** Actions > **Crash reports to issues** > Run workflow. A manual run is a **dry run by default** (the box is ticked): it
prints the issues it would make and changes nothing. Untick the box to do it for real. If the table is not there yet the run says so, in words.

Nothing else is needed: the job's GitHub token is the one every workflow already gets (`issues: write`), the `crash` label is created by the
job, and the project URL is read from `CMakeLists.txt` as `scripts/publish-supabase.sh` does.

## How issues are made (`scripts/crash_to_issue.py`)

* **First, before anything else,** it deletes old rows: a processed row 7 days after it was processed, a row nobody turned into an issue 14 days
  after it arrived. A run that fails later has still emptied the table.
* Reads the rows that have no `processed_at`, **newest first** (at most 200), so a pile of old junk can never keep a fresh report out of the window.
* A signature that already has an issue (label `crash`, or any issue whose text carries the hidden line
  `<!-- okay-crash-signature: ... -->`, which the **Report on GitHub** button also writes) gets **one comment** with the number of new
  reports, the versions, the systems and graphics cards, and how many reports of that signature are in the table (the last 7 days). A closed issue
  still gets a comment, not a new issue: reopen it if the crash is back on the latest version. At most 20 comments per run.
* A new signature gets **one issue**: at most 5 per run, **at most 20 per UTC day** (counted from the issues already made today), the rest wait. Title like
  `Crash: Access violation in OkayEngine.exe!DrawFields (v2.874.0)`, a table of the facts, the backtrace, the last steps, the Console in a
  collapsed block, and a **How to reproduce** stub for a person to fill in. The newest crashes get the day's slots first.
* A row is marked processed (with the issue number) only after GitHub accepted the issue or comment.
* **A bad row cannot stall the job.** Every row is cleaned within 3 seconds; one that takes longer is **quarantined**: marked processed with issue
  number 0 so it is never read again, and the log says its id and why (never its text). A row that makes a run fail because of its own content
  (GitHub refuses the text, or the cleaning breaks) is counted in the `attempts` column and quarantined after 3 runs. An outage of GitHub or Supabase
  is not the row's fault and counts against nothing. A run starts no new issue after 5 minutes (the job's limit is 10).
* Windows names in the backtrace come out mangled (`_ZN4okay5Scene6UpdateEf`); where binutils' `c++filt` is on the machine (it is on GitHub's
  Ubuntu runners) they are shown readable in the issue and its title. The signature is made from the original names by the launcher and is
  not affected; with no `c++filt` the names stay as they are. `c++filt` is run with an empty environment (none of the job's secrets).
* The script never follows a redirect to another host (it would carry the keys along), and it prints nothing from a row that could start an
  Actions command (in a dry run the lines are shown with a space inside the leading `::`).
* **Try it with no network and no secret:** `python3 scripts/crash_to_issue.py --rows-file tests/data/crash_rows_sample.json --dry-run`
  prints the issues the three sample rows would make (the third sample row was "sent by an old client" with a name, an email, an address and a
  token left in; the output shows them removed).

## The table (`docs/sql/crash_reports.sql`)

* `anon` and `authenticated` may **insert** nine columns and nothing else: they cannot read (not even their own row), update, delete,
  truncate, or choose the id, the time, `processed_at`, `issue_number` or `attempts`. The launcher asks for the answer without the row
  (`Prefer: return=minimal`), because returning it would need read access.
* The shape is checked by the database: ids and signatures are 16 hex digits, the report starts with the report title and is at most **64 KB**,
  text columns have length limits, the same report id is stored once (a retry that arrives twice is harmless).
* A flood is capped at **60 rows in any hour** for the whole table and at **32 MB of report text** in all; beyond that an insert fails with
  `crash_rate_limit` or `crash_table_full` and the launcher keeps the report and tries later. The cap holds when many senders write at the same
  moment (the guard takes an advisory lock first; without it 30 senders at once got 61 to 63 rows past a cap of 60) and a bulk insert over the cap
  is refused whole.
* **The table empties itself.** Every insert first deletes what is old (processed rows after 7 days, rows nobody processed after 14), so the
  table cannot grow without limit even if the GitHub job never runs; the job deletes the same rows too. The text of a report lives on in the
  GitHub issue, and the original stays on the sender's computer.
* `service_role` (the GitHub job) bypasses row-level security: it reads, marks rows processed (issue number 0 = quarantined), counts failed
  attempts and deletes old rows.
* Loading the file again on a table made by the first version of it brings it up to date (the `attempts` column, the 64 KB limit, the new guard).

## What the security review found (2026-11-15) and what was changed

An independent reviewer read the branch and ran probes. Eight findings, all addressed (the tests named are in the repository):

| # | Finding | What changed |
|---|---|---|
| 1 | One anonymous row (`a-a-a-...`, 100 KB) took minutes to scrub and stalled the job for good | Lines are cut to 600 characters before any pattern runs; the secret-value and email patterns are linear (80 hostile 100 KB shapes: under 0.05 s each, with the cut off too); a row over 3 seconds, or failing 3 runs, is quarantined and never read again |
| 2 | Unbounded storage and issue spam | 64 KB rows, 60 an hour, 32 MB in all; advisory lock (the cap raced: 303 rows through 300); old rows deleted (7 and 14 days) on every insert and first thing in every run; newest rows first; 5 issues a run, 20 a day, 20 comments a run |
| 3 | The disclosure did not say "public GitHub issue"; no first-run notice; `OKAY_CRASH_UPLOAD=0` then unset sent the backlog | New wording (also next to the switch), a one-time notice before the first send, the ledger is paused while sending is off and starts again from when it is back on |
| 4 | Scrubber leaks | Names inside longer words, the home folder's (full) name, `OneDrive - <employer>`, dash MACs, GUIDs, `\\?\UNC\server`, `C:\Users\ALICES~1`; the issue title and comments are made from scrubbed text; every column is scrubbed (and a test now fails if that is dropped) |
| 5 | Invisible Unicode tag characters; dry-run output could start an Actions command | Removed in C++ and Python; the issue says the text is untrusted data; `::` lines are broken up in the log |
| 6 | `c++filt` inherited the secrets; auth headers followed redirects; the schedule would go red on merge | Empty environment; redirects to another host refused; the schedule runs only when `CRASH_ISSUES_ENABLED` is `true`, a manual run is a dry run by default |
| 7 | Handler robustness | History start on Windows; no half UTF-8 character at a cut (it was a permanent 400: 90 of 90 oversized test rows were invalid before); `0600` and no symlinks on Linux and macOS |
| 8 | try_it section 7f (was 14) | Says to quit the launcher first; no longer claims `<host>` shows in the test report |

Accepted and not changed: a forged signature (they are public in the issues) can still add a comment to an existing issue, which is only noise
and is capped at 20 a run; the service_role key is still a repository secret (an Environment-scoped secret would be stricter); the scrubber cannot
know a name it is not told (see above); the Windows handler has not been run on Windows.

## What cannot be read from a stripped release (open question)

The shipped `OkayEngine.exe` has its debug information split off (`scripts/build-windows-dist.sh`), so a Windows crash is saved as
`OkayEngine.exe + 0x1A2B3C` with, where `dbghelp` can find a name in the exe's symbol table, a function name (a C++ one comes out in its
mangled `_ZN...` form). It cannot give file and line numbers from a stripped exe. **What exists:** `release.yml` already attaches the split-off
`OkayEngine.debug` to each GitHub release, and a report names its build (`pe-<link time>-<image size>`) and version. **What is missing,
proposed here and not done** (this task does not touch `release.yml`):

1. Record the build id of each release next to its `OkayEngine.debug` (a line in the release notes or a small `symbols.json` asset), so a
   report's `Build:` finds its symbols file without guessing.
2. A step in `crash_to_issue.py` (off by default) that, for a new signature, downloads that release's `OkayEngine.debug` and runs the MinGW
   `addr2line` on the top frames' offsets, then adds `file:line` to the issue. The report's offset is the address minus the module's load
   address (an RVA); `addr2line` on a PE file wants the image base added, which has to be checked against a real build before it is trusted.
   Not verified.
3. Until then: anyone can do the same by hand with `x86_64-w64-mingw32-addr2line -e OkayEngine.debug -f -C <address>`.

Linux and macOS reports (parked builds) get function names only for exported symbols (`backtrace_symbols`), and the test binary here is linked
with `-rdynamic` so the tests can name the faulting functions.

## Try it

[`docs/try_it.md`](try_it.md), section 7f, has the exact steps for the Windows release: quit the launcher first, a real crash on purpose with
`OKAY_TEST_CRASH_LATE`, where `okay_crash.txt` appears, what the launcher shows, the one-time notice, the preview, the GitHub button, the owner's two
one-time steps, and how to see the row and the issue.

`OKAY_CRASH_NO_DIALOG=1` skips the "OkaySpace crashed" box on Windows (for automated runs).

**Why an environment variable and not a menu item.** `OKAY_TEST_CRASH=<step>` (it existed before) crashes the editor right after it starts;
`OKAY_TEST_CRASH_LATE=<step>` crashes it on the 120th frame of the running editor, so the report has the graphics card, the scene and the
Console. Neither can be triggered by accident, they need no new UI in the 50,000-line `editor/main.cpp`, and a release build ships them
unchanged (the crash is a real null write, not a simulated one). A hidden "Simulate crash" menu item would be one more way to lose work by
mistake. The editor's and the launcher's pages see the same folder if you set `OKAY_CRASH_DIR` for both.

## Tested (and not)

Run on Linux for this change, all passing (the counts are the ones printed by the final run):

* `test_crash_context` (84 checks): the context, the bounded text writer, the report layout, truncation, the `OKAY_*` switch list.
* `test_crash_reports` (55): parsing both formats and the new sections, the signature (same crash same name, different crash different).
* `test_crash_handler` (67): a child process **really faults** (segmentation fault, divide by zero, `abort()`, an uncaught C++ exception, a stack
  overflow, two threads at once) and the parent reads back the files the handler wrote, with the launcher's parser; the real report is also run
  through the scrubber and the row builder; the files are `0600`, a symlink planted at either file is not written through, an older world-readable
  file becomes private. POSIX only.
* `test_crash_scrub` (103): hostile text for every rule above, names inside longer words, the surname from the home folder, OneDrive, MAC, GUID,
  `\\?\UNC`, 8.3 names, invisible characters, idempotence, 200 KB runs of every new shape.
* `test_crash_upload` (145): the switch and the words (public GitHub issue, the connection address), the one-time notice rule, the row and every
  column scrubbed, the 64 KB limit, a cut inside a multi-byte character (90 sizes, three character widths), the ledger, every server answer, the
  5-a-day limit, offline and retry, "only crashes from the moment sending was switched on", the ledger that follows the switch
  (`OKAY_CRASH_UPLOAD=0` then unset), the prefilled GitHub address.
* `tests/scripts/test_crash_reports_sql.sh` (55 checks) against a throwaway PostgreSQL 16: insert-only for `anon`, no reads, the row checks, the
  60-an-hour cap and a refused bulk insert, the self-pruning at 7 and 14 days, the 32 MB limit, the `attempts` column, then **30 senders at once**
  (exactly 60 rows get in; with the advisory lock removed the same test stored 61 to 63), and the upgrade of a table made by the first version of
  the file. CI's `sql-tests` job runs it.
* `tests/scripts/test_crash_to_issue.py` (72 tests): issue text, hostile input (every pattern on 80 hostile 100 KB shapes, a whole row of it, the
  time limit), the leaks above, caps, newest first, quarantine and attempts, dry-run output that cannot start an Actions command, `c++filt` with an
  empty environment; `tests/scripts/test_crash_to_issue_http.py` (16): the same script end to end over real HTTP against two fake servers on
  127.0.0.1 (the order of the calls, the prune before the work, a junk backlog that cannot starve a real report, a poison row, a redirect that would
  carry the keys to another host, a missing table, a GitHub outage, an answer that is not JSON); `tests/scripts/test_crash_workflow.py` (7): the workflow file's triggers, permissions and opt-in.
* `editor/main.cpp`, `editor/ConsolePanel.cpp` and `launcher/main.cpp` with `g++ -fsyntax-only` (the launcher again after this change); every
  new header with the MinGW win32 compiler (x86-64 and i686, `-Wall -Wextra`); a small Windows program that installs the handler was cross-linked.

**Mutation checks** (a deliberate break that the new tests must catch): without the advisory lock the 30-sender test fails (61 to 63 rows); without
`Scrub` in `Column()` nine checks fail; with the old oldest-first read, the old regex or the old prune order, the new Python tests fail (the first
version of the script fails 28 of the 72 unit tests and 11 of the 16 HTTP tests).

**Not run, so unverified:** a real crash on **Windows** (the Windows handler, the unwind from the exception record, dbghelp names, the message box,
the stack-overflow thread, the history-start change), the editor and launcher as programs (only syntax-checked here, so the one-time notice window has
not been seen on a screen), an upload to the **real Supabase** project, the workflow on GitHub (only its text is tested), the **Report on GitHub**
page in a browser, and the `OKAY_TEST_CRASH_LATE` path in a running editor. Tell us when one of them is wrong.

## Written from

Every file is our own code, written from specifications and general algorithms (CLAUDE.md rule 19a); nothing was copied from another
project's code, and no third-party crash SDK is used.

| File | Written from |
|---|---|
| `shared/CrashContext.hpp`, `shared/CrashHandler.hpp` | the Windows unhandled-exception-filter and x64 unwind documentation (`RtlLookupFunctionEntry`, `RtlVirtualUnwind`), the dbghelp `Sym*` calls the old handler already used, `signal-safety(7)`, `sigaction(2)`, `sigaltstack(2)`, `backtrace(3)`; the layout of the old `okay_crash.txt` |
| `shared/CrashReports.hpp` (signature) | FNV-1a (a public-domain hash described in the IETF draft) over the exception kind and normalised frames |
| `shared/CrashScrub.hpp` | the plain shapes of paths, emails, MAC addresses, GUIDs, IP addresses and the key formats of the services named, and the Unicode character database's list of invisible code points; no other project's rules |
| `shared/CrashUpload.hpp` | the PostgREST insert API, HTTP status meanings, the GitHub new-issue address format |
| `docs/sql/crash_reports.sql` | the PostgreSQL row-level-security, column-grant, trigger and advisory-lock documentation; the pattern of the project's other `docs/sql` files |
| `scripts/crash_to_issue.py` | the GitHub REST API and PostgREST documentation, the Python `re` and `signal` documentation, the GitHub Actions workflow-command documentation; standard library only |
| `.github/workflows/crash-issues.yml` | the GitHub Actions workflow syntax documentation (`vars` context, `workflow_dispatch` inputs) |
