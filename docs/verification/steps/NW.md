# Hand-written verification rows: networking, multiplayer and anti-cheat

### V-NW-001 Host and join on one PC
- **Map:** 16
- **Where:** two copies of the editor
- **Steps:**
  1. Choose **File > New Project...** and the **Multiplayer** template in two editor windows (or open the same project twice).
  2. In the first editor open the Multiplayer section (try **View > Panels > Services**, then **View > Panels > Net**), set a **Name** and press **Host Game**, then press Play.
  3. In the second editor type 127.0.0.1 into **Address**, press **Join**, then Play.
- **See:** both players appear in each other's game view; the connection status shows online.
- **Partial:** which panel holds the buttons was not confirmed; look in both. The map (16) lists Open notes for replication gaps (e.g. vehicle seats are not replicated).
- **Grounded:** no

### V-NW-002 Join by address from the launcher
- **Map:** 16, 17.15
- **Where:** `OkaySpace.exe`, Servers or Play page
- **Steps:**
  1. With a game hosting on your PC open the launcher's multiplayer (server browser) page.
  2. Press **Connect to server...** and type the address, or use the **LAN** tab.
- **See:** the game starts and joins; the **LAN** tab lists games on your network by itself.
- **Partial:** the Internet list needs the live Supabase server (map 17.15).
- **Link:** [launcher-multiplayer.md](../../launcher-multiplayer.md)

### V-NW-003 Quick Play and invites
- **Map:** 17.15, 16
- **Where:** `OkaySpace.exe`, signed in
- **Steps:**
  1. Host a match in a game, then in the launcher press **Invite friends**.
  2. Press **Quick Play** to join the best open server.
- **See:** an invite card with a Join game button goes to a friend; Quick Play skips full and password servers.
- **Partial:** needs two accounts that are friends; it is only a smoke test without them.
- **Link:** [launcher-multiplayer.md](../../launcher-multiplayer.md)

### V-NW-004 Anti-cheat in one click
- **Map:** 39
- **Where:** editor, multiplayer scene
- **Steps:**
  1. Open a scene with a Network Manager (the **Multiplayer** template).
  2. Select the Network Manager, pick a preset (Casual co-op, Competitive, Economy game) and press **Add Anti-Cheat**. Or **Add Component > Anti-Cheat (one click, safe preset)**.
  3. Open **File > Build Settings...**, find the **Anti-cheat** box and read the checklist.
  4. Set **Anti-cheat** to **Report**.
- **See:** an anti-cheat config is added with the preset numbers; the checklist shows what is on and what is `[missing]`, with fix buttons.
- **Partial:** a detection engine starts in observe mode and never kicks; the libsodium-dependent parts run in a separate CI job; not independently reviewed (CLAUDE.md rule 18).
- **Link:** [anticheat_quickstart.md](../../anticheat_quickstart.md)

### V-NW-005 Dedicated server export
- **Map:** 16, 18
- **Where:** editor, Build Settings
- **Steps:**
  1. Open **File > Build Settings...** (Ctrl+B).
  2. Set the platform to Server, fill the **Server** page and press **Export**.
  3. Look in the output folder for `server.cfg` and `start-server.bat`.
- **See:** an output folder with the server runtime, `server.cfg` and start scripts.
- **Partial:** the Windows server was not run by us; Linux server export fetches a runtime from GitHub with only a size check (docs/updater.md known gaps).
- **Link:** [dedicated-server.md](../../dedicated-server.md)

### V-NW-006 Network simulation panel
- **Map:** 16
- **Where:** editor
- **Steps:**
  1. Open **View > Panels > Net**.
  2. Raise the simulated latency or loss and run two clients as in V-NW-001.
- **See:** the connected game gets laggy as you raise the numbers.
- **Partial:** steps beyond opening the panel are unverified.
- **Grounded:** no

### V-NW-007 Join by link from a visual script (Net Join Link, Net Pin Host)
- **Map:** 14, 16.4
- **Where:** editor, two windows on one PC
- **Steps:**
  1. Choose **File > Open...** and open `examples\visual_script\playground\online.okayscene` (zip: the `examples` folder next to `OkaySpace.exe`; installer: `%LOCALAPPDATA%\OkaySpace\examples`). Press Play (Ctrl+P).
  2. Read the green line. Then Stop, start a second editor, open the same scene in both and press Play in both.
  3. In window 1 press H (it hosts on UDP 47777). In window 2 press J (it joins `okay://join?game=Playground&addr=127.0.0.1:47777`).
  4. In any graph, click **+ Instruction**, open the topic **Online** and look for **Net Join Link** and **Net Pin Host**.
- **See:** the green line says `Pin a made-up key: 0   Clear the pin: 1   Join hello: 0`; after H and J both blue lines say `Players: 1` and window 2 says `Connected: 1`; both nodes are in the Online topic.
- **Partial:** pinning to a real host key needs a host with an identity key (dedicated server); the demo only clears the pin. The Windows firewall may ask about the host window. File > Examples may not find the folder if the editor was started far from the install folder; use File > Open.
- **Link:** [../../try_it.md](../../try_it.md)

