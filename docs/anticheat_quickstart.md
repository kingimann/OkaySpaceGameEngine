# Add anti-cheat to your game in 5 minutes

This is the short way. You do not write any file by hand. For every setting and what it does, see
[anticheat.md](anticheat.md).

Anti-cheat is **opt-in**. A project that never clicks any of this behaves exactly as before.

## What it does

The **server** checks what players send. It refuses what a player may not do:

- move or delete another player's object;
- spawn something that is not on your list;
- write a variable only the server may write (score, money);
- move faster than you allow;
- flood the server with messages.

A player who keeps doing it is kicked. You can also turn on a **detection engine**
that watches for cheats that look like normal play and keeps evidence for you to read.

## The steps

1. **Open your multiplayer scene.** It needs a **Network Manager** component.
   If you have none, add one (Add Component > Network Manager).
2. **Click Add Anti-Cheat.** Two ways, same result:
   - On the Network Manager in the Inspector, pick a preset and press **Add Anti-Cheat**.
   - Or Add Component > **Anti-Cheat (one click, safe preset)**.
3. **Pick the preset that fits your game.** You can change it later in the Anti-Cheat Config.

   | Preset | Use it for | Main numbers |
   |---|---|---|
   | Casual co-op | Friends playing together | max speed 20, kick after 10 refusals a minute |
   | Competitive | Players race or fight | max speed 12, kick after 5, clock check 5% |
   | Economy game | Money, items, a shop | server owns money, gold, coins, gems, inventory |

   Every preset also: requires encryption, fills **Prefabs clients may spawn** from your
   networked prefabs, and turns on the detection engine in **observe mode**.
4. **Trim the spawn list.** The preset lists every prefab with a Network Sync.
   Remove any that a player should never spawn (a boss, a pickup the server places).
5. **Open the checklist.** It is in the Anti-Cheat Config, and in File > Build Settings
   under the **Anti-cheat** box. It shows what is on and what is missing.
   Where a safe fix exists, there is a button. Click through the list until nothing says
   `[missing]`.
6. **Set Build Settings > Anti-cheat to Report.** The built game then checks its own files
   against a signed list and reports changes. **Report** never blocks a player.
   Move to **Enforce** later, when you trust it.
7. **Play with friends.** Host a game and join it. Watch the log for lines like
   "forged syncvar" or "rpc rejected". If an honest move is refused, raise the number in
   the Anti-Cheat Config.
8. **Export.** Build Game, or a dedicated server. The rules travel inside the scene file,
   so the exported server has them. A rules file (`.okayac`) you pointed at is shipped too.

## Observe mode: read before you kick

The detection engine starts in **observe mode**. It scores players and keeps evidence, but it
never kicks, restricts or reports anyone for a ban. Run real matches this way first. Read the
evidence. If an honest player scored high, raise that detector's limit. Only then untick
**Observe mode** in the Detection engine section.

## Things the checklist asks about

- **Server authority.** Variables are owner-only, and movement has a speed limit.
- **Encryption required.** Only encrypted clients join. Needs a build with libsodium.
- **Session tickets.** Optional. Players show a signed pass when they join. You need a ticket
  service (see below) and its public key in your `.okayac` rules.
- **Client file check.** The Build Settings box: Off, Report, Enforce.
- **Detection engine.** Off, or on in observe mode, or on with kicks.
- **Objects nobody owns.** A Network Sync object that a player might drive (its authority is not
  Host) but that nobody owns and nobody may claim belongs to the server. Players cannot move it or
  write its variables. That is right for a door. A ball or a pickup that players should push or grab
  needs **Players can claim it** ticked on its Network Sync. The checklist lists these objects with a
  **Select** button. It never ticks the box for you, because it cannot know which is a door and which
  is a ball. Players made by the Network Player Spawner are fine.

## What anti-cheat does NOT do

Be honest with your players and yourself.

- **No kernel driver.** Nothing runs at the deepest level of the player's computer.
- **No memory scanning.** It does not look inside the game's memory for cheat tools.
- **Client checks run only on Windows.** The debugger, loaded-module and clock probes exist
  for Windows players. Linux and macOS players are protected only by the server's checks.
- **The server is the real defence.** A client check can be removed by a determined cheater.
  The server checks cannot, because the server is yours.
- **Tickets need a service you deploy.** The ticket function and its key pair are the
  owner's deployment. Until you have one, anyone can join.
- **Not a promise.** It makes cheating harder and visible. It does not make it impossible.
  Cheats that only change what one player sees (a wallhack) cannot be seen by the server.
- **Encryption deters casual tampering.** The game's data encryption in Build Settings is the
  same: it keeps players out of your scene files, it does not stop a determined attacker.

## If something goes wrong

- **Every join is refused, "server misconfigured".** The rules file did not load. The
  checklist line **Rules file loads** says why.
- **Honest players are kicked.** Raise *Max speed*, or raise *Kick after*. Watch the
  log to see which refusal fired.
- **Players cannot move the ball.** Tick **Players can claim it** on the ball's Network Sync.
- **The preset did nothing.** The scene has no Network Manager. Add one.
