# Developing Polyrift

## 1. Put the project on GitHub

GitHub stores the code, its history, and builds it for you. It does **not** run the game server
(see HOSTING.md for that).

There are two ways to get the files there. Both end with the same result.

### Route A: through the website (no tools to install)

1. **Create the repository.** Sign in to GitHub, then open one of these links. They open GitHub's
   "new repository" form with the name already filled in:

   - Public (anyone can see and download it):
     <https://github.com/new?name=polyrift&visibility=public&description=Multiplayer+tank+arena+with+a+C%2B%2B+server+and+a+browser+client>
   - Private (only you, until you change it in Settings):
     <https://github.com/new?name=polyrift&visibility=private&description=Multiplayer+tank+arena+with+a+C%2B%2B+server+and+a+browser+client>

   Leave "Add a README", ".gitignore" and "license" **off**, and click **Create repository**.
   People can only download the game from a **public** repository.
2. **Unzip `polyrift.zip`** on your computer and open the `polyrift` folder, so that you are looking
   at `README.md`, `src`, `public`, `docs` and `.github`. On a Mac press Cmd+Shift+. in Finder to
   show the names that start with a dot.
3. **Upload.** On the page GitHub shows after creating the repository, click the link
   **uploading an existing file**. Select **everything inside** the `polyrift` folder (Ctrl+A, or
   Cmd+A) and drag it onto that page. Drag the contents, not the `polyrift` folder itself and not
   the zip: `README.md` must end up at the top level of the repository. Wait until the files are
   listed (there are 24), then click **Commit changes**.
4. **Check that `.github` arrived.** The file list should show a `.github` folder. Some browsers
   skip dot-folders when dragging. If it is missing: **Add file → Create new file**, type
   `.github/workflows/build.yml` as the name (typing the slashes creates the folders), paste in the
   contents of that file from your computer, and commit. Without it nothing is built automatically.

To change something later the same way: open a file on GitHub and click the pencil to edit it, or
use **Add file → Upload files** to replace files with newer copies.

### Route B: with git (better once you edit the code regularly)

1. Create the repository as in step 1 of Route A.
2. In the project folder, run:

   ```
   git init -b main
   git add .
   git commit -m "Initial commit"
   git remote add origin https://github.com/YOUR-NAME/polyrift.git
   git push -u origin main
   ```

   "In the project folder" means a terminal opened inside the unzipped `polyrift` folder on your own
   computer (Windows: click the folder window's address bar, type `cmd`, press Enter), with Git
   installed from git-scm.com. GitHub Desktop does the same with buttons: add the folder as a local
   repository, commit, then *Publish repository*.

### After either route

Open the repository's **Actions** tab. The "Build" workflow compiles the server on Linux, macOS
and Windows and runs a simulated match on each. A green tick means that platform works. If one is
red, open it to read the compiler error.

The build is free for public repositories; private ones get a monthly allowance of build minutes
that depends on your GitHub plan.

### Publishing a downloadable version

On the website: **Releases → Create a new release → Choose a tag**, type `v0.1.0`, pick
**Create new tag on publish**, and click **Publish release**. Or with git:

```
git tag v0.1.0
git push origin v0.1.0
```

Either way, a tag starting with `v` makes the same workflow build all three platforms and, a few
minutes later, attach the downloads to that **Release**:
`polyrift-windows-x64.zip`, `polyrift-macos-arm64.tar.gz` and `polyrift-linux-x64.tar.gz`,
each with the program and the `public` folder ready to run. The macOS build is for Apple Silicon;
on an Intel Mac, or if macOS refuses to open the unsigned download, build from source with `make`
(two minutes, and no warnings).

## 2. Seeing your changes in the game

| You changed… | To see it |
| --- | --- |
| Anything in `public/` (drawing, HUD, menu, controls) | Save the file and **refresh the browser**. The server reads these files fresh on every request, so no restart is needed. |
| Anything in `src/` (rules, tanks, bots, modes, networking) | **Stop** the server (Ctrl+C), **rebuild**, **start** it again. Open browser tabs reconnect by themselves within a couple of seconds. |
| The layout of the binary packets (`Room::sendUpdates` in `proto.cpp` and `onUpdate` in `game.js`) | Change both sides together and add 1 to `PROTOCOL_VERSION` in `src/room.hpp` **and** `PROTO` in `public/game.js`. Browsers still holding the old client then reload themselves instead of misreading packets. |

Rebuild commands:

```
make                                   # Linux / macOS
cmake --build build --config Release   # Windows (or anywhere you used CMake)
```

Run the server **from the project folder** (`./polyrift` or `build\Release\polyrift.exe`) so it serves
the `public/` you are editing. The build also copies `public/` next to the executable, but that copy
is only refreshed when you rebuild.

Getting a change to other people is a separate step: your own machine shows it immediately; GitHub
has it after `git push`; players on a public server see it after you update that server
(HOSTING.md, "Updating the live game").

### A normal working session

```
git pull                         # get the latest version
# ...edit, rebuild, test in the browser...
git add .
git commit -m "Add Quad tank"
git push                         # GitHub rebuilds on all three platforms
```

For anything risky, work on a branch (`git switch -c new-mode`), push it, and open a pull request;
the build runs on the pull request before you merge.

### Working on it with Claude

A new chat does not remember this one. To continue, either connect GitHub in Claude's settings
(Connectors) so a session can open the repository, make changes and push them, or attach the files
you want changed. Point it at this document and the README for orientation.

## 3. Tools for testing

| Tool | What it is for |
| --- | --- |
| `./polyrift --start-level 45` | Everyone spawns at max level, so you can try any class immediately |
| The **Sandbox** slot (or `./polyrift --slots sandbox`) | Free levels, any class with T, and a panel that spawns polygons and bosses, sets your level and adds bots |
| `./polyrift --sim ffa 180` | Bots only, no network, at full speed: three simulated hours take a few seconds. Prints each game's result as it happens, then the summary and a table of kills and deaths by class. Any mode name works; `--seed N` gives a different run, and `--bots`, `--bot-level`, `--free-level`, `--boss-minutes` and `--mutations` apply. Run several seeds before trusting a number. |
| `http://localhost:3000/#gallery` | Draws every tank class from `defs.cpp` on one screen |
| `./polyrift --selftest 20000` (or `make test`) | Simulates every mode with bots and a random fake player, no network. First checks the rules on small scripted arenas (shared score, kill + assist, death summary, Smasher stats, Assembler blocks, regeneration and shield numbers, the trap limit, kept and starter drones, the bounty, the bots' builds and dodging, and one check for every newer mode), then prints entity counts, top scores, kills, assists, polygon and boss statistics, dominator captures, milliseconds per tick and bandwidth per player. Use it after balance changes and to catch crashes. |
| `./polyrift --bots 0` | Empty arenas, useful when testing with a friend |
| Open several browser tabs | Each tab is a separate player (up to `--max-per-ip`, default 6) |

To hunt memory bugs on Linux or macOS, build once with the sanitizers and run the self-test:

```
g++ -O1 -g -std=c++17 -fsanitize=address,undefined -o polyrift_check src/*.cpp
./polyrift_check --selftest 20000
```

## 4. Changing the game

**Add a tank class** in `src/defs.cpp`: describe its guns, then link it into the tree.

```cpp
add(make("Quad", 2, {GB(0, 1.9f, 0.8f), GB(90, 1.9f, 0.8f), GB(180, 1.9f, 0.8f), GB(270, 1.9f, 0.8f)}));
...
link("Flank Guard", {"Hexa Tank", "Tri-Angle", "Trap Guard", "Quad"});
```

`GB(angleDegrees, length, width)` makes a barrel; chain `.ox()` (sideways offset), `.asp()` (flare),
`.delay()`, `.reload()`, `.stats(speed, damage, health)`, `.spread()`, `.recoil()`,
`.type(G_TRAP / G_DRONE / G_BLOCK / G_SWARM / G_MINION / G_DECOR)`. The second argument of `make` is the tier
(1, 2, 3 = unlocks at level 15, 30, 45). The client receives the definitions when it connects and
draws the new class automatically; check it in `/#gallery`.

Special abilities are also data:

| You want | How |
| --- | --- |
| An auto-turret that aims and fires by itself | `t.turrets.push_back(autoTurret(angle, distance, size, arcDegrees))`, or `turret(...)` with your own guns. `autoOf("Auto-X", "X")` copies a class and puts a turret on top |
| A missile that fires its own guns | define the guns once with `addSub(...)`, then give a barrel `.sub(SUB_...)` (see Skimmer, Twister, Rocketeer) |
| A block with a turret on it | same, with `addSub(..., aim = true)` (see Engineer) |
| Bullets that weave | `.wave(amount)`; a negative amount starts on the other side (see Desmos, Helix) |
| Drones that hunt on their own | `.indep()` on a drone barrel (see Hybrid) |
| Drones that shoot | `.armed()` (see Overdrive); small tanks that shoot: `.type(G_MINION)` (see Factory) |
| Shapes that become drones when destroyed | `t.necro = 1 << F_SQUARE` and `t.maxDrones` (see Necromancer) |
| A shot that vanishes at a set distance | `.leash(distance)` (see the dominators) |
| A shot that shoves instead of hurting | `.knock(strength)` with a tiny damage value (see Base Protector) |
| Bullets that repair team-mates | `.heal()` on a barrel (see Healer and the Sanctuary's turret) |
| Invisibility while standing still | `t.invis = 0.03f` (see Stalker) |
| Zoom while alt-fire is held | `t.zoom = 600` (distance the view slides; see Hunter, Predator) |
| Cannot be damaged | `t.godmode = true` (see Anti-Tank Machine Gun; only sensible for hidden classes) |
| A polygon body | `t.shape = SH_SQUARE` (or `SH_TRI`, `SH_PENT`; bosses also use `SH_BODY6`, `SH_BODY7`, `SH_BODY8`) |
| Blocks that fuse when they touch | `.merge()` on a block barrel (see Assembler); the rule is in `Room::collidePair` |
| A block whose turret repairs team-mates | a sub built with `addSub(..., aim = true)` whose gun has `.heal()` (see Surgeon) |
| Drones drawn as squares, pentagons or eggs | `.dshape(4)`, `.dshape(5)`, `.dshape(6)` on a drone barrel (see the mysticals) |
| Its own set of stats | `smasherStats(t)`: sets `t.statCap`, `t.statMask` and `t.engine` (Reload becomes Engine Acceleration) |

`./polyrift --selftest 1500` checks that every class can be reached from Basic and prints a line
for each mistyped name in a `link(...)`.

**Developer tank**: start the server, type the developer code (default `1234`, set with
`--dev-code`) into the **Developer code** box on the start screen, click **Unlock**, and press Play. Press **T** and click any class,
including the hidden computer-controlled ones. This is the quickest way to try a class you are
working on. The check is `case 5` in `Room::onMessage`; never put the real code of a public server
in the repository.

**Boss Rush**: the waves are in `Room::rushLogic` and `Room::spawnWave` (`room.cpp`): how many
bosses per wave, which waves get which big boss, how health grows, and the win and lose conditions.
`RUSH_WAVES` in `room.hpp` is the number of waves. Size, health, speed and reward of every boss are in the
`BOSS_KINDS` table (the first `BOSS_REGULAR` entries are the ones that visit every arena);
sanctuaries are set up in `Room::setupSanctuaries`.

**Score, kills and assists**: `Room::hurt` records who damaged a tank or polygon and how much
(`Entity::hits`), `Room::damageShares` turns that into each player's part, and the payouts are in
`Room::reap` (polygons) and `Room::onTankDeath` (tanks and bosses).

**Polygons**: shapes are the `FOODS` table, sizes the `TIERS` table and mutations the `MUTATIONS`
table in `defs.cpp`. What spawns and how often is `Room::spawnFood`; growing is `Room::evolveFood`;
crashers think in `Room::crasherThink` (`ai.cpp`). The client draws them in `drawPolygon` (`game.js`).

**Bosses, protectors, closers**: their classes are the `hidden` entries at the end of `defs.cpp`.
Their size, health and timing are in the `BOSS_KINDS` table, `Room::spawnBoss`, `Room::setupProtectors` and
`Room::closeArena` (`room.cpp`); how they think is in `ai.cpp`. To add a boss, add a hidden class
and a line to `BOSS_KINDS` (among the first `BOSS_REGULAR` lines if it should visit every arena, and
bump that number), or name it in a wave in `Room::spawnWave`.

**Tune numbers**: base stats are constants at the top of `src/room.cpp`; what each skill point does
is in `Room::refresh` (body, health, shield) and `Room::fireGun` (bullets); map sizes, bot counts and
food amounts are in the `Room` constructor; polygons are the `FOODS` table in `defs.cpp`.

**Change bot behaviour**: `src/ai.cpp`. Bots only set the same three inputs a player has
(move direction, aim point, fire), so they cannot cheat by accident. `Room::botPlan` runs when a bot
spawns: it sets the bot's difficulty, picks the level-45 class it will work towards and the skill
build that suits it. The builds are the `BOT_BUILDS` table: one row per playstyle (`BotStyle` in
`defs.hpp`), ten numbers that add up to 42. Which playstyle a class has is worked out from its guns
by `classify()` at the end of `defs.cpp`, together with two more hints for the bots: `thrust` (firing
pushes the tank forwards, so it turns round to flee) and `wave` (its bullets weave, so it aims off;
`Room::waveAimFix`). `Room::botDodge` is the sidestep, `Room::pickGoal` is where a bot goes when it
has nobody to shoot (each mode's objective is a few lines there), and the `must` block in
`Room::botThink` is for places it has to go whatever else is happening (home with the flag, back
inside the Battle Royale circle).

**Add a mode**: add a value to `enum Mode`, a case in `Room::configure` (map size, teams, maze),
anything it places on the map in `Room::populate`, its rules in a function called from
`Room::modeLogic`, its name in `parseMode` and the self-test list in `main.cpp`, and a `MODE_INFO`
line in `public/game.js`. The newer modes are worked examples: each has a few lines of state in
`room.hpp` ("state of the newer modes"), is reset in `configure`, and has one function in
`src/modes.cpp` (`tagLogic`, `brLogic`, `motherLogic`, `kothLogic`, `siegeLogic`, `ctfLogic`). A mode
with a winner calls `win(text, sub, colour)`, which shows the banner and closes the arena. What the
players need to see goes into the `lb` message in `Room::sendBoards` and is drawn by `drawHud`.
The start-screen buttons build themselves from the server's slots, so no button is needed. To
play it, put its name in a slot: `--slots ffa/maze,2tdm/4tdm,yourmode`, or change `DEFAULT_SLOTS`
in `main.cpp`.

**Maze layouts**: the `styles` table in `Room::generateMaze` (`room.cpp`). Each line is one layout:
how many thin wall pieces and how long, how many solid blocks and how big. Add a line to add a
layout. Layouts that would cut off a base or a dominator are thrown away and re-rolled.

**Slots**: a `Room` is created with a list of modes and calls `configure` with a random one every
time `resetArena` starts a new game. A mode that never ends on its own (`Room::openEnded`) only
re-rolls when the `--arena-minutes` timer closes it.

**The bounty, regeneration, the trap limit**: the bounty's numbers are the `BOUNTY_` constants at the
top of `src/modes.cpp`; health and shield regeneration are the `REGEN_` and `SHIELD_` constants at the
top of `src/room.cpp`; the trap limit is `TRAP_CAP` in `defs.cpp` (any class can have one: set
`t.maxTraps`). Starter drones are `t.starter`.

**Change how things look**: `public/game.js` (`drawTank`, `drawEntity`, `drawHud`), colours in the
`COLORS` array at the top, menu styling in `public/style.css`.

## 5. How the pieces talk

- Browser → server: tiny binary messages. First byte is the type: `0` spawn + name, `1` input
  (key flags, aim offset; flag 64 is the level-up key, 128 is override; an optional second flag byte, bit 0 =
  reverse tank), `2` spend skill, `3` choose class, `4` keep-alive, `5` developer code, `6` control / leave a
  dominator or the Mothership, `7` self-destruct, `8` latency probe (answered with `pong`), `9` chat line (UTF-8 text; cleaned by `sanitizeChat`, rate-limited, sent on to the arena as JSON `chat`), `10` Sandbox command (a command byte, two argument bytes and a point; refused outside the Sandbox; see `Room::sandboxCommand`). See `Room::onMessage`.
- Server → browser: JSON text for rare events (`init`, `lb`, `msg`, `chat`, `banner`, `dead`, `full`, `closed`, `nospawn`, `dev`, `pong`) and one binary
  snapshot per tick. See `src/proto.cpp`. The once-a-second `lb` message also carries what each mode shows: `lead` (the bounty),
  `tag`, `br`, `ms`, `koth`, `siege`, `ctf` and `rush`.
- The server never trusts the browser: every message is bounds-checked, movement and shooting are
  simulated server-side, and a class or skill can only be picked if the server agrees it is available.

## 6. Things that are easy to get wrong

- **Don't put game rules in `game.js`.** Anything the browser decides, a cheater can change.
- **Keep `proto.cpp` and `onUpdate` in step**, and bump the protocol version when you change them.
- **Commit small and often**, so a bad change is easy to undo (`git revert`).
- **Never commit secrets** (server passwords, API keys). The repository has none today; keep it so.
- **The name.** "Polyrift" was made up for this project and has not been checked against existing
  games or trademarks. Search before you launch publicly, and rename if needed (the title lives in
  `public/index.html` and a few log lines).
