# Hand-written verification rows: launcher and accounts

Format: see the top of `scripts/gen_verify_checklist.py`. Bold names must exist in the source (`--check-labels`).

### V-LA-001 Start the launcher and switch pages
- **Map:** 17.1.1
- **Where:** `OkaySpace.exe`
- **Steps:**
  1. Double-click `OkaySpace.exe`. A splash shows, then the launcher window with a sidebar.
  2. Click the sidebar entries **Create**, **Play**, **Community**, **Account** and **Settings** one after the other.
  3. Click in an empty part of the page (not in a text box) and press the number keys 1 to 5: they jump to the same five pages in that order.
- **See:** each page opens without a black frame or a crash; the number keys change the page.
- **Partial:** `launcher/main.cpp` is one 1.5 MB file with no automated UI test (map 17.1); the tray icon code is not used.

### V-LA-002 Create an account
- **Map:** 17.2.1, 17.2.5
- **Where:** `OkaySpace.exe`, Account page
- **Steps:**
  1. Open the **Account** page (not signed in).
  2. Switch to **Create account**. Fill **Username**, **Email** and **Password** (6 or more characters).
  3. Press **Create account**.
- **See:** the page changes to a signed-in profile and a "Signed in" notice; a taken username says "That username is taken".
- **Partial:** the Supabase path needs the live server and is not run in CI (map 17.2); use a throwaway account.

### V-LA-003 Sign out and sign in again
- **Map:** 17.2.5
- **Where:** `OkaySpace.exe`, Account page
- **Steps:**
  1. On the **Account** page press **Sign out**.
  2. Press **Sign in** and enter your **Email or username** and **Password**.
  3. Close and reopen the launcher.
- **See:** you are signed in again, and still signed in after the restart (the session is kept).
- **Partial:** "signed out after a restart" is a listed symptom in the map (17.2); report it if you see it.

### V-LA-004 Password reset email
- **Map:** 17.2.1
- **Where:** `OkaySpace.exe`, Account page, signed out
- **Steps:**
  1. On the **Account** page press **Forgot password?**.
  2. Enter your email and send.
  3. Open the link in the email.
- **See:** "Password reset email sent. Open the link in it to choose a new password." and a page where you pick a new password.
- **Partial:** the hosted reset page (`OKAY_PASSWORD_RESET_URL`) must be set up by the owner; without it the launcher says no reset page is set up.
- **Link:** [password-reset.md](../../online/password-reset.md)

### V-LA-005 Check for updates by hand
- **Map:** 17.1.2
- **Where:** `OkaySpace.exe`, Settings page
- **Steps:**
  1. Open **Settings** and find the About section (the version is shown there).
  2. Press **Check for updates**.
  3. Wait for the button to stop saying "Checking".
- **See:** a toast "You're up to date" when you are on the newest release, or "Update available" with an **Install now** button when not.
- **Partial:** the update index (`versions.json`) is not signed yet (map 17.1, 17.11).
- **Link:** [updater.md](../../release/updater.md)

### V-LA-006 Update check when the launcher starts
- **Map:** 17.1.2, 17.1.3
- **Where:** `OkaySpace.exe`, Settings page
- **Steps:**
  1. In **Settings** find **Check for updates when the launcher starts** and turn it on.
  2. Close the launcher and start it again; wait about 10 seconds (the first look is shortly after start, then every 5 minutes).
  3. Turn it off, restart, and wait again.
- **See:** with it on, a newer release raises the toast "Update available: OkaySpace v..." and a notification; with it off, nothing is checked until you press the button. If you are already current, nothing appears either way, so you need a release older than the newest one to see this.
- **Partial:** needs an older installed version to see a real result. Updates are opt-in by button (launcher comment near the periodic check).

### V-LA-007 Install an update and restart
- **Map:** 17.1.2
- **Where:** `OkaySpace.exe`
- **Needs:** an installed release older than the newest published one
- **Steps:**
  1. When the sidebar shows **Install now** (or Settings shows it after a check), press it.
  2. Watch the progress; the launcher closes and starts again by itself.
  3. Open **Settings** and read the version.
- **See:** the version is the new one, the launcher started on its own, and `Tools\OkayEngine.exe` and `Tools\OkaySpacePlayer.exe` are updated too. A refusal (no checksum, antivirus lock) shows the reason in words and leaves your version unchanged; the reason is also in `okay_update.log` next to the exe.
- **Partial:** the Windows restart helper has only run in CI, never by us on real Windows (docs/release/updater.md audit). Never re-run a release to fix this (CLAUDE.md rule 12).
- **Link:** [updater.md](../../release/updater.md)

### V-LA-008 Update channel and other editor versions
- **Map:** 17.1.2, 18.5
- **Where:** `OkaySpace.exe`, Settings page
- **Steps:**
  1. In **Settings** open the **Update channel** combo and look at its choices (Latest and long-term support).
  2. Press **Manage versions** (next to "Other versions of the editor").
  3. On the **Versions** page install or open one other editor version.
- **See:** the combo offers the channels, and the Versions page lists installable editor versions; a version without a checksum is refused.
- **Partial:** the launcher itself is never downgraded by the channel choice (the setting text says so).
- **Link:** [launcher_versions.md](../../release/launcher_versions.md)

### V-LA-009 Settings are remembered
- **Map:** 17.1.5
- **Where:** `OkaySpace.exe`, Settings page
- **Steps:**
  1. In **Settings** change **UI scale** and toggle **Show progress bar**.
  2. Close and reopen the launcher.
  3. Press **Reset preferences** and confirm.
- **See:** your choices are still set after the restart (they live in `launcher.cfg` next to the exe); Reset puts theme, accent, UI scale, sorting and update settings back.
- **Partial:** some splash changes apply "the next time OkaySpace starts" (the page says so).

### V-LA-010 Create a project from the launcher
- **Map:** 17.1.1, 25
- **Where:** `OkaySpace.exe`, Create page
- **Steps:**
  1. Open **Create** and press **+  New Project**.
  2. Pick a template, type a **Project name** and check the **Location**.
  3. Create it, then press **Open** on the project card.
- **See:** the project appears in the list and the editor (`Tools\OkayEngine.exe`) opens on its scene.
- **Partial:** the editor is started by the launcher from `Tools`; a missing editor is a download step.

### V-LA-011 What's New and Tips
- **Map:** 17.1.4
- **Where:** `OkaySpace.exe`
- **Steps:**
  1. Open the **What's New** page.
  2. Open the **Tips** page and type a word in its search box.
- **See:** release history cards, and a tips list that filters as you type, with a tip of the day.
- **Partial:** none known.

### V-LA-012 Play the official game
- **Map:** 17.1.1, 18.5
- **Where:** `OkaySpace.exe`, Play page
- **Steps:**
  1. Open the **Play** page.
  2. Press **Install** (the button shows the download size), wait, then press **Play**.
  3. Find **Keep the official game up to date** in **Settings**.
- **See:** the game installs, starts in its own window, and the button reads "Running" while it is open.
- **Partial:** background updates install only after the game is closed (the setting text says so).

### V-LA-013 Download my data
- **Map:** 17.2.7
- **Where:** `OkaySpace.exe`, Account page, signed in
- **Steps:**
  1. On the **Account** page find the "Your data" card (under Sign-in and security).
  2. Press the download button and wait for the progress to finish.
  3. Open the `exports` folder next to the launcher.
- **See:** one JSON file with your data, and a list of any sections that failed or are "not set up".
- **Partial:** the server function and tables are not set up everywhere; the owner steps are in `docs/online/account_data.md`.
- **Link:** [account_data.md](../../online/account_data.md)

### V-LA-014 Delete my account (use a throwaway account only)
- **Map:** 17.2.8
- **Where:** `OkaySpace.exe`, Account page, signed in as a throwaway account
- **Steps:**
  1. On the **Account** page press **Delete my account...**.
  2. Type the username and the password and confirm.
- **See:** the launcher shows each server step, then signs you out; or it says the function is not deployed. Do not run this on an account you want to keep.
- **Partial:** the Edge Function is not deployed or run against a live project yet (map 17.2). Expect "function not deployed".
- **Link:** [account_data.md](../../online/account_data.md)

### V-LA-015 Crash reports page
- **Map:** 17.1.5, 48
- **Where:** `OkaySpace.exe`, Settings page
- **Steps:**
  1. Open **Settings** and find the Crash reports section.
  2. Read what it lists (it shows editor crashes found on disk).
- **See:** an empty list when nothing crashed, or a card per crash. A toast "The editor crashed" appears after a real crash.
- **Partial:** we cannot make the editor crash on purpose here, so only the empty state is likely to be seen.

### V-LA-016 Friends page
- **Map:** 17.3
- **Where:** `OkaySpace.exe`, signed in
- **Steps:**
  1. Open the **Friends** page from the sidebar.
  2. Look at the friend list and any requests; try **Add friend** on a profile.
- **See:** your friends with online status, or an empty state ("Add friends first"); signed out it asks you to sign in.
- **Partial:** needs a signed-in account (and a second one to see a real friend); smoke level only.
- **Grounded:** no

### V-LA-017 Forums page
- **Map:** 17.5
- **Where:** `OkaySpace.exe`, signed in
- **Steps:**
  1. Open the **Forums** page from the sidebar.
  2. Open a category or thread, if any exist.
- **See:** forum categories and threads, or an empty list.
- **Partial:** depends on the live server; smoke level only.
- **Grounded:** no

### V-LA-018 Marketplace page
- **Map:** 17.6
- **Where:** `OkaySpace.exe`
- **Steps:**
  1. Open the **Market** page from the sidebar.
  2. Search and open an item.
- **See:** a list of published games or assets with a search box.
- **Partial:** depends on the live server; smoke level only.
- **Grounded:** no

### V-LA-019 Admin page
- **Map:** 17.9
- **Where:** `OkaySpace.exe`, signed in as moderator, staff or admin
- **Steps:**
  1. Open the **Admin** page (only those roles get it).
  2. Look through its tabs (Overview, Players, Marketplace, Forums, Bans, Audit log, Reports, Official game, Anti-cheat).
- **See:** the tabs open; ordinary accounts do not see the page at all.
- **Partial:** needs an admin account; smoke level only.
- **Grounded:** no
