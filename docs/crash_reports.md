# Crash reports: what is saved, what is sent, how an issue is made

If the editor crashes, it writes a detailed report. The launcher then (unless you turn it off) sends a cleaned copy to our server, and a
scheduled GitHub job turns it into an issue that says what happened, with the backtrace in a code block. This page explains each step in
plain words, how to turn it off, and what the owner has to set up once.

**Status, honestly:** the saved report and the launcher page are in the code. The server half (the table and the GitHub job) **cannot work
until the one-time setup below is done**. Nothing here has been run on a real Windows machine or against the real server yet; the
"Tested" section says exactly what was run.

## The path of one crash

1. **The editor crashes.** A handler inside the editor writes the report first and does nothing else: no network, no GitHub, no
   asking. It writes `okay_crash.txt` (the latest) and appends to `okay_crashlog.txt` (every crash), next to `OkayEngine.exe`
   (or in the folder named by `OKAY_CRASH_DIR`). On Windows you also see the "OkaySpace crashed" box.
2. **The launcher sees it** (within about two seconds if it is open, or the next time it starts): a notification, and a card in
   **Settings > Crash reports** with the time, the version and what the editor was doing.
3. **The launcher sends it**, if sending is on, once the files are 15 seconds old (so the editor has finished writing). It removes personal
   details first (see below), sends one row to the `crash_reports` table of our Supabase project using the **public** key that is already
   in every launcher, and writes the report's id to `okay_crash_sent.txt` so it is never sent twice. Offline, it keeps the report and
   tries again later (5 minutes, then longer, up to 6 hours). At most 5 reports go out in 24 hours.
4. **A GitHub job makes the issue** (every 30 minutes, `.github/workflows/crash-issues.yml`): one issue per kind of crash, labelled
   `crash`. The same crash again adds a comment (how many, which versions, which systems) instead of a second issue.
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
| Mode, Scene, Objects, Assets loaded | Edit or play (and prefab open), the scene's **file name only**, how many objects, how many assets are loaded |
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
* A stack overflow is reported too: on Windows the work (and the message box) runs on a thread with its own stack, on Linux on an alternate
  signal stack (set up for the thread that starts the handlers).
* If two threads fault at the same moment, the first writes the report and the second waits (up to 20 seconds) instead of ending the
  program under it; a fault inside the handler itself ends the program at once.
* On Windows the second pass (function names) runs on its own thread and the crash waits for it at most 8 seconds, so a slow or stuck
  dbghelp cannot hold the "OkaySpace crashed" box up; the first report is already saved.
* It also catches `std::terminate` and `abort()`: an uncaught C++ exception (for example from a worker thread) never used to leave a report.

## What is removed before anything is sent

`shared/CrashScrub.hpp`, applied to the whole report and to every column, on the launcher's side:

* Your user folder: `C:\Users\<name>\...` becomes `%USERPROFILE%\...`, `/home/<name>/` and `/Users/<name>/` become `~/`, and a name inside a
  longer path becomes `<user>`. Network paths `\\SERVER\share` become `\\<host>\share`.
* Your login name and your computer name wherever they appear as a word (not for very common words such as `main` or `admin`).
* Email addresses (`<email>`) and IP addresses (`<ip>`, except `127.x` and `0.0.0.0`).
* Secrets: GitHub, Slack, Stripe, AWS, Google and Supabase style keys, JSON web tokens, `Bearer ...`, `Authorization: ...`, the value of
  `password=`, `token=`, `secret=`, `api_key=`, names and passwords inside web addresses, and long hex or letter-and-digit strings (`<token>`).
* Control characters; text that is not valid UTF-8 becomes `?`; very long lines are cut; the whole row is at most 100 KB (the oldest
  Console lines are left out first).

The scrubber is a set of byte-level rules written for this project and is **not perfect**: a name that the rules cannot know (for example a
project called after a person, in a Console line) is not found. That is why you can **read exactly what would be sent** first.

The GitHub job scrubs again before it writes an issue, treats every byte of a row as hostile (anyone with the public key can add a row),
puts all of it in code spans or code blocks whose fence is longer than any backtick run inside, makes `@mentions` and `#links` harmless,
cuts long lines and removes invisible and right-to-left characters.

## Turning it off, and looking first

* **Settings > Crash reports > Send crash reports automatically** is **on by default** (the owner's choice, 2026-11-15). Turn it off there.
  It is saved as `crash_upload=0` in `launcher.cfg`.
* **`OKAY_CRASH_UPLOAD=0`** in the environment turns it off whatever the switch says (`false`, `off` and `no` work too). The variable can
  only turn sending off, never on.
* **Preview what would be sent** (the button in that card), or **What would be sent** on a report's card, shows the exact text and the
  columns, after the removal. Looking sends nothing. With sending off it says so at the top.
* A report whose card says "sent", "waiting to be sent" or "not sent (sending is off)" tells you where it stands.
* Reports stay on your computer until you clear them (**Clear all**). Sending a report never deletes it.

The one line next to the switch: *Sends: the editor version, your Windows version, CPU, memory and graphics card, which scene and mode it
was in, the last 200 Console lines, the steps it took and the crash location. Your name, folders, email, computer name and IP address are
removed first.*

## One-time setup (the owner; until then no report reaches GitHub)

Nothing in chat, and no key in a file. Two steps, both in the browser:

1. **Run the table's SQL once.** Open the Supabase project (the one in `CMakeLists.txt`, `OKAY_DEFAULT_ACCOUNT_URL`), **SQL editor**, paste
   the whole of [`docs/sql/crash_reports.sql`](sql/crash_reports.sql) and run it. It is safe to run again. Until it has run, the launcher keeps
   every report and the Crash reports page says "The server has no crash_reports table yet".
2. **The GitHub secret.** The job needs `SUPABASE_SERVICE_KEY`, the project's `service_role` key. **It is the same secret `release.yml`
   already uses**, so if releases publish to Supabase it is already there and you do nothing. Otherwise: GitHub repository > Settings >
   Secrets and variables > Actions > New repository secret, name `SUPABASE_SERVICE_KEY`, value = the `service_role` key from Supabase
   (Project settings > API Keys). Without it the job ends green with a notice and does nothing.

Then, to check: Actions > **Crash reports to issues** > Run workflow, with **dry run** ticked first (it prints the issues it would make, makes
none). Scheduled runs only start once the workflow file is on the default branch.

Nothing else is needed: the job's GitHub token is the one every workflow already gets (`issues: write`), the `crash` label is created by the
job, and the project URL is read from `CMakeLists.txt` as `scripts/publish-supabase.sh` does. Old rows that were turned into issues are deleted
after 90 days.

## How issues are made (`scripts/crash_to_issue.py`)

* Reads the rows that have no `processed_at`, oldest first (at most 200).
* A signature that already has an issue (label `crash`, or any issue whose text carries the hidden line
  `<!-- okay-crash-signature: ... -->`, which the **Report on GitHub** button also writes) gets **one comment** with the number of new
  reports, the versions, the systems and graphics cards, and the total so far. A closed issue still gets a comment, not a new issue: reopen it if
  the crash is back on the latest version.
* A new signature gets **one issue** (at most 5 new issues per run; the rest wait 30 minutes): title like
  `Crash: Access violation in OkayEngine.exe!DrawFields (v2.874.0)`, a table of the facts, the backtrace, the last steps, the Console in a
  collapsed block, and a **How to reproduce** stub for a person to fill in.
* A row is marked processed (with the issue number) only after GitHub accepted the issue or comment.
* Windows names in the backtrace come out mangled (`_ZN4okay5Scene6UpdateEf`); where binutils' `c++filt` is on the machine (it is on GitHub's
  Ubuntu runners) they are shown readable in the issue and its title. The signature is made from the original names by the launcher and is
  not affected; with no `c++filt` the names stay as they are.
* **Try it with no network and no secret:** `python3 scripts/crash_to_issue.py --rows-file tests/data/crash_rows_sample.json --dry-run`
  prints the issues the three sample rows would make (the third sample row was "sent by an old client" with a name, an email, an address and a
  token left in; the output shows them removed).

## The table (`docs/sql/crash_reports.sql`)

* `anon` and `authenticated` may **insert** nine columns and nothing else: they cannot read (not even their own row), update, delete,
  truncate, or choose the id, the time or the `processed_at` / `issue_number` columns. The launcher asks for the answer without the row
  (`Prefer: return=minimal`), because returning it would need read access.
* The shape is checked by the database: ids and signatures are 16 hex digits, the report starts with the report title and is at most 100 KB,
  text columns have length limits, the same report id is stored once (a retry that arrives twice is harmless).
* A flood is capped at 300 rows in any hour for the whole table; beyond that an insert fails with `crash_rate_limit` and the launcher keeps
  the report and tries later.
* `service_role` (the GitHub job) bypasses row-level security: it reads, marks rows processed and deletes old ones.

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

[`docs/try_it.md`](try_it.md), section 14, has the exact steps for the Windows release: a real crash on purpose with `OKAY_TEST_CRASH_LATE`,
where `okay_crash.txt` appears, what the launcher shows, the preview, the GitHub button, and how to see the row and the issue.

**Why an environment variable and not a menu item.** `OKAY_TEST_CRASH=<step>` (it existed before) crashes the editor right after it starts;
`OKAY_TEST_CRASH_LATE=<step>` crashes it on the 120th frame of the running editor, so the report has the graphics card, the scene and the
Console. Neither can be triggered by accident, they need no new UI in the 50,000-line `editor/main.cpp`, and a release build ships them
unchanged (the crash is a real null write, not a simulated one). A hidden "Simulate crash" menu item would be one more way to lose work by
mistake. The editor's and the launcher's pages see the same folder if you set `OKAY_CRASH_DIR` for both.

## Tested (and not)

Run on Linux for this change, all passing:

* `test_crash_context` (84 checks): the context, the bounded text writer, the report layout, truncation, the `OKAY_*` switch list.
* `test_crash_reports` (55): parsing both formats and the new sections, the signature (same crash same name, different crash different).
* `test_crash_handler` (46): a child process **really faults** (segmentation fault, divide by zero, `abort()`, an uncaught C++ exception, a stack
  overflow, two threads at once) and the parent reads back the files the handler wrote, with the launcher's parser; the real report is also run
  through the scrubber and the row builder. POSIX only.
* `test_crash_scrub` (72): hostile text for every rule above, idempotence, 200 KB runs.
* `test_crash_upload` (95): the switch, the row, the 100 KB limit, the ledger, every server answer, the 5-a-day limit, offline and retry,
  the prefilled GitHub address.
* `tests/scripts/test_crash_reports_sql.sh` (43 checks) against a throwaway PostgreSQL 16: insert-only for `anon`, no reads, the row checks,
  the flood cap, loading the SQL twice. CI's `sql-tests` job runs it.
* `tests/scripts/test_crash_to_issue.py` (47 tests): issue text, hostile input, dedupe, failures, the green no-op without the secret.
* `editor/main.cpp`, `editor/ConsolePanel.cpp` and `launcher/main.cpp` with `g++ -fsyntax-only`; every new header with the MinGW win32
  compiler (x86-64 and i686); a small Windows program that installs the handler was cross-linked.

**Not run, so unverified:** a real crash on **Windows** (the Windows handler, the unwind from the exception record, dbghelp names, the
message box, the stack-overflow thread), the editor and launcher as programs (only syntax-checked here), an upload to the **real Supabase**
project, the workflow on GitHub, the **Report on GitHub** page in a browser, and the `OKAY_TEST_CRASH_LATE` path in a running editor window.
Tell us when one of them is wrong.

## Written from

Every file is our own code, written from specifications and general algorithms (CLAUDE.md rule 19a); nothing was copied from another
project's code, and no third-party crash SDK is used.

| File | Written from |
|---|---|
| `shared/CrashContext.hpp`, `shared/CrashHandler.hpp` | the Windows unhandled-exception-filter and x64 unwind documentation (`RtlLookupFunctionEntry`, `RtlVirtualUnwind`), the dbghelp `Sym*` calls the old handler already used, `signal-safety(7)`, `sigaction(2)`, `sigaltstack(2)`, `backtrace(3)`; the layout of the old `okay_crash.txt` |
| `shared/CrashReports.hpp` (signature) | FNV-1a (a public-domain hash described in the IETF draft) over the exception kind and normalised frames |
| `shared/CrashScrub.hpp` | the plain shapes of paths, emails, IP addresses and the key formats of the services named; no other project's rules |
| `shared/CrashUpload.hpp` | the PostgREST insert API, HTTP status meanings, the GitHub new-issue address format |
| `docs/sql/crash_reports.sql` | the PostgreSQL row-level-security and column-grant documentation; the pattern of the project's other `docs/sql` files |
| `scripts/crash_to_issue.py` | the GitHub REST API and PostgREST documentation; standard library only |
