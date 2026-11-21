---
id: 7v
row: | 7v | Test builds: try a branch in the launcher in about 30 minutes, before it is released | Ask the coordinator for a test build; then Launcher > Settings > Behavior > Startup & updates > **Get test builds** (off by default) | New in this change; the rules and the publisher are tested, the workflow, the real server and the launcher screens were never run (see section 7v) |
---
## 7v. Test builds: try a branch before it is released

**What it is.** Today a change waits about an hour for the pull-request checks, is merged, and then a release builds for 15 to 30 minutes before you
can see it. A **test build** skips all of that: the coordinator builds one branch for you, it is published to its own folder on the update server,
and your launcher offers it **only if you switched "Get test builds" on**. It never touches the normal "latest" update, and a normal player never
sees it. It may be broken: that is what a test is. Rules and files: [release_channels.md](release/release_channels.md#test-builds-for-the-owner-to-try-a-branch-not-a-channel-for-players).

**How to try it.**

1. **Ask the coordinator** to start a test build of a branch ("start a test build of walk-vs-run"). They start the workflow **Test build (dev
   channel)** from `main`; it takes **about 15 to 30 minutes**. When it is done its page says "Test build published", the version (for example
   `2.876.0.17`) and the branch. You cannot start it yourself: it needs GitHub, and it only runs from `main`.
2. In the launcher open **Settings** (the sidebar), the **Behavior** tab, the card **STARTUP & UPDATES**, and switch **Get test builds** on. A
   message says "Test builds are on. They can be broken. Switch this off here to stop getting them."
3. **What you should see** within a minute or so (or click the version number at the top of the sidebar to look right now):
   * a message "Test build of walk-vs-run (abc1234) is ready (v2.876.0.17)";
   * in the sidebar a card **Test build available** with the text "Test build of walk-vs-run (abc1234) is ready" and an **Install now** button.
4. Press **Install now**. The **Software Update** window shows `v2.876.0 » v2.876.0.17`, the line **Test build of walk-vs-run (abc1234)** in amber,
   and the coordinator's note under "What's new". It downloads and checks the files like any update (a file that does not match its checksum is
   refused and nothing is installed). When it says "Update installed" press **Restart now**.
5. After the restart: **Settings > About** says "Version 2.876.0.17 (test build)" and, under it in amber, "Test build of walk-vs-run (abc1234)";
   the sidebar version says `v2.876.0.17 test`; the editor's **Help > About** says `v2.876.0.17 (test build)` and its window title ends in
   `[v2.876.0.17 (test build)]`; the **Versions** page says "You're running v2.876.0.17 (test build)".
6. **To stop getting test builds:** Settings > Behavior > Startup & updates > switch **Get test builds** off. A message says "Test builds are off.
   Nothing new will be offered." (**Reset preferences** on the same tab also turns it off.)
7. **To go back to the stable version** (while a test build is installed): close the editor, then in the same card press **Go back to the stable
   version** (it shows when Get test builds is off; the row above it names the test build you run). The update window says "Going back to the
   newest normal release (a test build is replaced)" and `v2.876.0.17 » v2.876.0`; press **Restart now**. After the restart About says "Version
   2.876.0" with no test line. (Otherwise the next normal release replaces the test build by itself.)
8. If the launcher does not start at all after a test build: run `OkaySpace-Setup.exe` from the normal release page over the top; it is a normal
   installer and replaces everything.

**Limits.** A test build replaces only the launcher, the editor and the player, like any in-app update. If a branch adds an example scene or another
file that ships in the zip or installer (the `examples\` folder), that file is **not** in the test build: ask for it separately or wait for a release.

**How to tell it is on or off.** With the switch off the launcher never asks the update server for the test folder, so nothing about test builds
can appear. With it on, the launcher reads `dev/versions.json` on the update server; the file `dev/DEV.txt` there names the newest test build.

**Status.** Checked on Linux, not on Windows:
`dev_channel` (16,516 checks: the version order, the normal reader ignoring test versions, the test reader, the signature rule, the setting, the
same install rules), `self_update` (the way back is judged strictly), `dev_publish_script` (the publisher against a fake storage server: only `dev/`
is written, in order, the real release files are byte for byte unchanged, and every refusal writes nothing), `dev_index` (self-test) and
`dev_workflow_lint` (the workflow read and its shell steps run offline). The launcher and editor files were compiled with `g++ -fsyntax-only` and
the MinGW win32 compiler; they were not built or run. **Never run:** the workflow on GitHub, a write to the real Supabase storage, anything on Windows,
and every screen above (no screenshot: the launcher was not built here). The wording in step 3 to 7 is what the code says; tell us what you actually see.
