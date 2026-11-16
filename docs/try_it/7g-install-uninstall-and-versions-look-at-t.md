---
id: 7g
row: | 7g | Install, uninstall and Versions: see today's behaviour (audit, nothing changed) | Setup, Settings > Apps, `%LOCALAPPDATA%\OkaySpace`, the launcher's Versions page | Audit only: no program change in this branch. Windows steps unverified |
---
## 7g. Install, uninstall and Versions: look at today's behaviour yourself (audit)

Why: the install audit (`docs/install_audit.md`) found things it could only read in code or run on a Linux test machine. Nothing
in the program changed in that branch. These steps let you see on Windows whether it is right. They use a **copy** of a project, never
a real one, because two of the checks touch projects.

1. **Where do my projects go?** Install with `OkaySpace-Setup.exe`. Open the launcher, **Create**, make a project. Paste
   `%LOCALAPPDATA%\OkaySpace\Projects` into the Explorer address bar. The audit says your project is **there** (the Setup welcome
   page says Documents; it is wrong).
2. **What does uninstall leave?** In the launcher open **Versions** and install one older version. Then uninstall from
   **Settings > Apps > Installed apps > OkaySpace**. Look again at `%LOCALAPPDATA%\OkaySpace` and `%APPDATA%\OkaySpace`. The
   audit predicts these are left: `Versions\`, `Projects\`, `downloads\`, `community\`, the `.log` and `okay_*.txt` files, `imgui.ini`,
   `achievements.txt`, and in `%APPDATA%\OkaySpace` the `session.txt`, `accounts.db` and `servers.cfg`. Please tell us if the list
   differs (that is how the audit gets checked).
3. **Uninstall with the launcher open.** Install again, leave the launcher open, uninstall. The audit predicts `OkaySpace.exe` stays
   and the Apps list no longer shows the program.
4. **Does an old editor update itself?** (Use a copy of a project.) Install an older version on the Versions page, right-click the
   copy of a project > **Open with** that version. The audit predicts an **Engine Update** window; do **not** press Download & Install
   unless you want to see the problem (the old editor is replaced by the newest one and reopens your project without asking).

**Status.** Windows steps unverified. What was run: the installer under wine on Linux (install, update, uninstall, running programs,
full disk, odd folders) and two small test programs against the real Versions and project-gate code. Details, results and the plan to
fix each item are in `docs/install_audit.md`; the comparison with other launchers is `docs/install_comparison.md`.
