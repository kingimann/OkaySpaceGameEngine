---
id: 7f
row: | 7f | Crash reports: a detailed report, sent for you, turned into a GitHub issue | `OKAY_TEST_CRASH_LATE=Test.Step` crashes the editor on purpose; launcher Settings > Crash reports | Local half new; the server half needs the owner's one-time setup (see 7f) |
---
## 7f. Crash reports: detailed report, sending, GitHub issues

**What it is.** When the editor crashes it now writes a much fuller report (version and build, Windows version, CPU, memory, graphics card, the
scene's name, the last 64 steps, the last 200 Console lines, a backtrace). The launcher shows it, lets you read exactly what would be sent
(names, folders, emails and addresses removed), tells you once that a sent report becomes a **public GitHub issue**, sends it for you (switch **on by
default**), and a GitHub job turns it into an issue. Full story: [crash_reports.md](crash_reports.md).

> **The server half (steps 9 and 10) cannot work until the owner has done two one-time steps, in the browser, in this order:**
> - **Owner step A:** run `docs/sql/crash_reports.sql` once in the Supabase SQL editor (open the project, **SQL editor**, paste the whole file,
>   run it; it is safe to run again).
> - **Owner step B:** set the repository variable `CRASH_ISSUES_ENABLED` to `true` (GitHub repository > **Settings > Secrets and variables >
>   Actions > Variables > New repository variable**). Until it is `true`, the 30-minute schedule skips the job (it shows as skipped, never red).
>
> The GitHub secret `SUPABASE_SERVICE_KEY` must exist too (releases already use it, so it normally does). Never paste a key in chat.
> Steps 1 to 8 and 11 need none of this.

**Steps (Windows release).**
1. **Quit the launcher first.** It is single-instance: a launcher that is already running (also in the tray) never passes `OKAY_TEST_CRASH_LATE` on
   to the editor, so the test crash would not happen. Close the editor. Open **Command Prompt** in the folder with `OkaySpace.exe` (zip: the unzipped
   folder; installer: `%LOCALAPPDATA%\OkaySpace`). To keep the files easy to find, first run: `set OKAY_CRASH_DIR=%TEMP%\crashtest` after
   `mkdir %TEMP%\crashtest`.
2. Run `set OKAY_TEST_CRASH_LATE=Test.Step` and then `OkaySpace.exe` (the launcher). The editor the launcher starts inherits both settings.
3. In the launcher open the editor the way you normally do (it must be started from the launcher). After a couple of seconds, at the 120th frame, the editor crashes on purpose and the box
   **OkaySpace crashed** appears. Click OK.
   (`OKAY_TEST_CRASH=Test.Step` crashes before the window opens instead: the report then has no scene or graphics card yet.)
4. **Where the report is:** `okay_crash.txt` (latest) and `okay_crashlog.txt` (all) in `%TEMP%\crashtest`, or, without `OKAY_CRASH_DIR`, next to
   `OkayEngine.exe` (`Tools\`). Open it in Notepad: you should see `Version`, `Build`, `Last step: Test.Step`, `OS`, `CPU`, `GPU`, `Backtrace`, then
   `--- Breadcrumbs`, `--- Console` (the last line says the crash was on purpose).
5. In the launcher a notification "The editor crashed" appears. Open **Settings > Crash reports**. You should see a card "Access violation"
   with the time and version and, after a dot, **waiting to be sent**. (Crash reports that were already on your computer before the launcher
   first ran this version say "not sent (from before sending was switched on)" and are never sent; only crashes from now on are.) The switch card
   above it shows the same words you will see in the next step: what is sent and that it becomes a **public** GitHub issue.
6. **The one-time notice.** About 15 to 40 seconds after the crash, before anything is sent, a window **Crash reports are sent automatically**
   opens in the launcher. It says what is sent, that it is published as a public issue on GitHub, that the server sees your connection address, and how
   to turn it off. Nothing is sent until you answer. Click **OK, keep sending** to carry on (or **Turn it off**: the card then says "not sent (sending
   is off)"). It appears once. To see it again: quit the launcher, open `launcher.cfg` (next to `OkaySpace.exe`) in Notepad, delete the line
   `crash_notice=1`, save, start the launcher.
7. Click **What would be sent** on the card (or **Preview** in the SENDING CRASH REPORTS card). You should see the report with your folders
   shown as `%USERPROFILE%` where the editor was installed under your user folder, and the columns (report id, signature, version, ...). The test
   report does not contain your computer name, so you will not see `<host>` in it; your login name or computer name would only show as `<user>` or
   `<host>` if it appeared in the text (for example in a Console line). Nothing has been sent by looking.
8. **Turn it off:** switch **Send crash reports automatically** off: the card says "not sent (sending is off)". (Or start the launcher with
   `set OKAY_CRASH_UPLOAD=0`.) Crashes that happen while it is off are never sent later, even after you switch it on again or unset the variable.
   Switch it on again to continue.
9. **Sending** (needs owner step A): after you answered the notice the card changes to **sent** within about 20 seconds and a toast says "Crash report
   sent. Thank you." If the table is missing, the page says "The server has no crash_reports table yet ... run docs/sql/crash_reports.sql" and the
   report is kept. To see the row: Supabase > Table editor > `crash_reports` (or SQL editor:
   `select created_at, signature, version, exception, top_frame, processed_at, issue_number from crash_reports order by created_at desc;`).
10. **The issue** (needs owner steps A and B): GitHub > Actions > **Crash reports to issues** > Run workflow. The **dry run** box is ticked by default:
    the log shows the issue text and changes nothing. Untick it to make the issue for real, or wait up to 30 minutes for the schedule once the variable is
    `true`. An issue `Crash: Access violation in ...` appears with label `crash`; it says the text is untrusted data. Crash again the same way and send: the
    issue gets a **comment**, not a second issue. (The schedule only runs once this change is on `main`.)
11. **Report on GitHub** (no server needed): on the card click **Report on GitHub**. Your browser opens GitHub's new-issue page with the title, a table
    and the backtrace filled in, and the whole cleaned report is on your clipboard: paste it into the issue. Read it, then Submit or close the tab.

**Try the issue text without Windows or any secret** (any machine with Python): `python3 scripts/crash_to_issue.py --rows-file
tests/data/crash_rows_sample.json --dry-run` prints the two issues and one comment the three sample rows would make (the third row had a name, an email,
an address and a token left in; the output shows them removed).

**How you know it worked.** The report has the lines in step 4; the card says what its sending state is; the notice appears once, before the first
send; the preview has no folder with your name in it; with sending off nothing is ever sent, also later; one crash makes one issue, the same crash
again a comment.

**Status.** Verified here (Linux): the report builder, the parser and signature, the handler (a child process really faults: segmentation fault,
divide by zero, abort, uncaught exception, stack overflow; the files are `0600` and a planted symlink is not followed), the scrubber, the upload logic
against a fake server (the notice rule, the ledger that follows the switch), the SQL against a local PostgreSQL (insert-only, no reads, the caps,
30 senders at once, the self-pruning, an upgrade from the first version), the issue script (72 unit tests, 16 over HTTP) and the workflow file's
safety properties. **Unverified:** everything that needs Windows (the Windows handler, the message box, function names from dbghelp), the editor
and launcher as running programs (they were only syntax-checked, so the notice window has not been seen on a screen), the upload to the real
Supabase, the GitHub workflow on GitHub, the browser page of step 11, and `OKAY_TEST_CRASH_LATE` in a running editor.
