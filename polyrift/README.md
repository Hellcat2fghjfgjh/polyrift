# Polyrift

A multiplayer tank arena in the style of the classic ".io" polygon shooters: farm shapes, level up,
spend skill points, pick one of 128 classes at levels 15 / 30 / 45, and fight. Bosses drop in,
base protectors guard the team bases, and arena closers end a round. Fifteen modes take turns in
six slots, from free-for-all to Capture the Flag, Battle Royale and a Sandbox.

The whole game server is **C++17 with no third-party libraries**. It runs the simulation, the bots and
all game rules, and also serves the browser client (plain JavaScript + canvas) over its own built-in
HTTP/WebSocket server. Written from scratch; it does not contain code from any other game.

**Just want to play?** Follow **[docs/PLAYING.md](docs/PLAYING.md)**: download from this page, start
the program, open `http://localhost:3000`. Every step for Windows, macOS and Linux is there.

- **[docs/PLAYING.md](docs/PLAYING.md)** — complete player guide: download, run, play alone or with friends, settings, troubleshooting
- **[docs/DEVELOPING.md](docs/DEVELOPING.md)** — putting it on GitHub, the edit → rebuild → see-it loop, where things live
- **[docs/HOSTING.md](docs/HOSTING.md)** — playing with friends, running a public server, launch checklist

## Modes

| Mode | What happens |
| --- | --- |
| **FFA** | Every tank for itself on an open map with a pentagon nest in the middle. |
| **Maze** | Free-for-all inside a randomly generated maze. Walls block tanks and bullets. |
| **2 Teams** | Blue vs Green. Each team has a base that damages intruders. |
| **4 Teams** | Blue, Green, Red and Purple, one base per corner. |
| **Domination** | Two teams fight over four dominator turrets. Destroy a neutral one to capture it. The team that holds all four **wins the game**: the arena closes, arena closers clear it, and a new game starts. |
| **Maze Domination** | Domination inside a maze. Bases and dominators are kept clear of walls. |
| **Mothership** | Two teams, no bases. Each team has one **Mothership**: a big tank with twenty drones. You spawn beside yours. The computer drives it until a player takes it over with **H** (the dominator key); H again gives it back. It never repairs itself: only Healers can mend it. Destroy the enemy's to win. If both are alive after 15 minutes, the one in better shape wins. |
| **Siege** | Blue attacks, Green defends three dominators: two outer ones and the **Citadel** behind them, which cannot be hurt while an outer one stands. A dominator Blue destroys is Blue's for the rest of the game and adds 90 seconds to the clock. Blue wins by taking all three; Green wins when the clock (9 minutes at the start) runs out. |
| **Capture the Flag** | Two teams. Each flag stands just outside its team's base. Touch the enemy flag to pick it up and carry it to your own stand to score (a carrier is 15% slower and shows on everyone's minimap). A dropped flag returns when a team-mate touches it, or by itself after 20 seconds. First to 3 captures wins; after 15 minutes the higher score wins, and a tie plays on until the next capture. |
| **Tag** | Four teams, no bases. A tank that is destroyed by another team's tank **joins that team**. The team that ends up with everyone wins; when the arena timer runs out first, the biggest team wins. |
| **Battle Royale** | Free-for-all with **no respawns**. First a lobby of about 40 seconds in which tanks level up and cannot hurt each other. Then the battle begins: a safe circle closes over four minutes, tanks outside it burn, and arena closers hunt them. The last tank alive wins. Anyone who joins late watches and plays the next battle. |
| **King of the Hill** | Two teams. The hill is the circle in the middle. A team that is alone on it scores a point a second; if both teams are on it, nobody scores. First to 150 points wins. |
| **Sandbox** | Nothing is at stake. The level-up key always works, **T** opens a picker with every playable class at any level, and a panel under the leaderboard spawns polygons (any shape, size and mutation) and bosses, clears the map, sets your level, adds bots (up to 12) and changes how well they play, or switches damage to your tank off. **F** and **G** spawn the chosen polygon or boss under the cursor. |
| **Endless Boss Rush** | Boss Rush without a last wave. The same twenty waves come round again and again; every lap the bosses have twice the health and three more lesser bosses come along. There is no victory: the game ends when the sanctuaries fall, and the server remembers the most waves cleared. |
| **Boss Rush** | Everyone is on one team. Bosses attack in **20 waves** and march on your four **sanctuaries**. Lesser bosses come first (elite crashers, then the Elite Skimmer, Rogue Palisade, Summoner and Nest Keeper, later the other mysticals, the Nest Warden and Guardian and the Rogue Armada). Waves 5, 10 and 15 bring a celestial, wave 8 a terrestrial, wave 12 the Legionary Crasher, wave 14 two terrestrials, wave 17 a rogue celestial, wave 19 Kronos and wave 20 the final boss, Ragnarok. Clear wave 20 to win. A destroyed sanctuary turns yellow and can be won back by destroying the yellow one; if all four are yellow for 30 seconds in a row, the game is lost. |

A **sanctuary** is a trapper dominator with a healing turret: you spawn beside one and it repairs hurt team-mates near it. A yellow (fallen) sanctuary does nothing, and nobody can respawn while all four are yellow. Each game deals three of the five celestials (**Paladin**, **Freyja**, **Zaphkiel**, **Theia**, **Nyx**) and three of the five terrestrials (**Ares**, **Gersemi**, **Ezekiel**, **Selene**, **Eris**) in a random order, and one of the rogue celestials (**Julius**, **Genghis**, **Napoleon**). Very rarely **Bob** tags along. The **Healer** class (from Basic at level 30) fires bullets that repair team-mates, sanctuaries included, and barely scratch enemies; at level 45 it becomes a **Medic**, **Ambulance**, **Surgeon** or **Paramedic**.

### Slots: which modes are running

The server runs six **slots**, and each slot picks its mode at random every time a new game starts there:

| Slot | Plays |
| --- | --- |
| 1 | FFA or Maze |
| 2 | 2 Teams or 4 Teams |
| 3 | Domination, Maze Domination, Mothership, Siege or Capture the Flag |
| 4 | Boss Rush or Endless Boss Rush |
| 5 | Tag, Battle Royale or King of the Hill |
| 6 | Sandbox |

The start screen shows one button per slot, labelled with the mode it is playing right now. A slot
re-rolls when its game ends. Most modes end when someone wins or loses; FFA, Maze, Teams and Tag
have no ending of their own (Tag can go on a long time), so they close and restart every 45 minutes
(`--arena-minutes`). The Sandbox never closes.
Change the layout with `--slots`, for example `--slots ffa,rush` for two fixed arenas or
`--slots ffa/maze/2tdm` for one slot that rotates between three modes.

In every mode:

- **Level-up key.** Hold **N** to level up for free, up to level 45 (`--free-level` changes or disables this). Level 45 gives 42 skill points.
- **Score is shared by damage.** A polygon, a tank or a boss pays everyone who damaged it, in proportion to the damage they did, not just whoever landed the last hit. The last hit earns the **kill**; anyone else who did at least a tenth of the damage to a tank (a twentieth to a boss) earns an **assist** and is told so. The **kill bar** beside the score bar counts kills, assists and boss kills (P hides it), and the death screen adds the polygons you destroyed and who helped destroy you.
- **Bounty on the leader.** The living tank with the highest score (10,000 or more, in an arena of at least three tanks) is the **leader**. A gold star marks it on everyone's minimap, a crown sits on its name and on the leaderboard, and an arrow at the edge of the screen points to it while it is out of sight. Destroying the leader pays a bounty on top of the usual reward: a quarter of its score, between 2,500 and 15,000, shared by damage like the kill itself. There is no bounty in Boss Rush, the Sandbox or the Battle Royale lobby.
- **Health and shield.** Health comes back at 0.75% of the maximum a second, three times that after ten seconds without damage; every point of Shield Regeneration adds half as much again (at 9 points: about 4% and 12% a second). The shield starts to refill 3 seconds after the last damage and takes about 6.7 seconds; at 9 points of Shield Regeneration it starts after 1.8 seconds and takes about 2.4.
- **Smasher stats.** Smasher, Mega Smasher, Spike, Auto-Smasher and Landmine have five stats instead of ten (Body Damage, Max Health, Engine Acceleration, Shield Regeneration, Shield Capacity) and each takes 10 points instead of 9. Points spent on bullets or movement come back when you become a Smasher.
- **Drones stay.** A tank keeps its drones when it changes class, up to the new class's limit. Necromancer, Maleficitor and Infestor, which cannot launch drones, are given four, four and three square drones the moment a tank becomes one.
- **Trap limit.** A Hexa-Trapper or Septa-Trapper can have 45 traps out at once; laying another removes the oldest.
- **Polygons.** Eggs, squares, triangles, pentagons and hexagons (about 1 in 48), each in four sizes: normal, **Beta**, **Alpha** and **Omega**. About 1 in 20 is a pink **crasher** that chases and rams the nearest tank. Polygons left alone slowly **evolve** into the next shape or size. Very rarely one is a **mutation** worth far more: Shiny, Legendary, Shadow (fades from sight), Rainbow, Trans or a 3D Platonic solid. `--mutations` sets how rare they are.
- **Bosses.** Every few minutes bosses visit: usually one, sometimes two or three together in a busy arena. Eleven kinds take turns: the elite crashers (Destroyer, Gunner, Sprayer, Battleship, Spawner, Trap Guard, Spinner), the Elite Skimmer, the Rogue Palisade, the Summoner and the Nest Keeper. Each has a name of its own ("Vexa the Elite Gunner"), is marked on the minimap in its colour, and pays its reward (30,000 to 45,000) to everyone who damaged it, by damage. Bosses are on the same team as the polygons (Team Polygon), so they and their shots leave shapes alone, and crashers leave bosses alone. Everything else that belongs to no player team is on Team Polygon too: neutral dominators, fallen sanctuaries in Boss Rush and the arena closers that end a round. None of these fight each other or break shapes; they only fight the players.
- **Base protectors** (team modes). Each base has protectors that cannot be damaged. Their drones do almost no damage but throw intruders far back from the base.
- **Dominators** come in three kinds. The **Gunner** is short-sighted and fires a storm of hard-hitting bullets that vanish at the edge of its view. The **Destroyer** sees twice as far, reloads slowly, and fires a slow shell that destroys any tank it hits; the shell also vanishes at the edge of its view. The **Trapper** sees as far as the Destroyer and never stops laying traps. A player driving one (H) sees exactly as far as it does.
- **Mazes** are different every game: the wall randomizer rolls one of seven layouts (Classic, Open field, Tight, Corridors, Blocks, Pillars, Fortresses). Team mazes are mirrored left to right so both sides are equal.
- **Zoom.** Hunter, Predator, X-Hunter, Poacher, Ordnance and Dual slide their view towards where they are aiming while right-click or Shift is held. Predator reaches farthest.
- **Secondary guns.** Falcon, Eagle, Vulture and Phoenix fire their thrusters with click / Space and their front barrel with right-click or Shift, as in arras. Autofire leaves secondary guns alone.
- **Chat.** Enter opens a one-line chat box; the line shows above your tank and in a short list for everyone in the arena. Rate-limited, can be hidden with X, and `--no-chat` turns it off.
- **Control a dominator** (Domination, Siege). Press **H** to sit in the nearest dominator your team holds: you aim and fire it, and your own tank waits for you. Press H again to get your tank back at the base with its class, skills and score. You are thrown out if the dominator is destroyed. In Mothership mode the same key puts you in your team's Mothership, which you can also steer.
- **Bots.** Bots head for a level-45 class chosen when they spawn and spend their points on one of ten preset builds that suits it (rammer, sniper, drone commander, trapper and so on). They sidestep incoming bullets, turn round to flee when they have rear thrusters, allow for the weave of Desmos-line bullets, and play each mode's objective. `--bot-level easy`, `normal` (the default), `hard` or `mixed` sets how well they do it: easy bots do not dodge or plan, hard bots aim better, react to every shot and pick on wounded tanks.
- **Arena closers.** When a round ends they sweep the map; they cannot be damaged and pass through walls. Afterwards the arena restarts with scores at zero.

It is multiplayer: everyone who opens the server's address in a browser plays in the same arenas.
Bots fill each arena when it is quiet and step aside one-for-one as real players join.

## Run it

You run **one program** (the server) on one computer. Players need nothing but a web browser.

### Windows

*No compiler:* once the repository has a release (see DEVELOPING.md), download
`polyrift-windows-x64.zip` from the repository's **Releases** page, unzip it, double-click
`polyrift.exe`, and open **http://localhost:3000**. Windows may warn that the program is from an
unknown publisher (it is not code-signed): choose *More info → Run anyway*.

*Build it yourself:* install [Visual Studio Community](https://visualstudio.microsoft.com/) with the
"Desktop development with C++" workload and [Git](https://git-scm.com/). Then, in the
"Developer PowerShell for VS" window:

```
git clone https://github.com/YOUR-NAME/polyrift.git
cd polyrift
cmake -S . -B build
cmake --build build --config Release
build\Release\polyrift.exe
```

### macOS

```
xcode-select --install        # one time: installs the compiler and git
git clone https://github.com/YOUR-NAME/polyrift.git
cd polyrift
make
./polyrift
```

### Linux (including a rented server or a Raspberry Pi)

```
sudo apt install build-essential git     # Debian / Ubuntu; use your distro's equivalent
git clone https://github.com/YOUR-NAME/polyrift.git
cd polyrift
make
./polyrift
```

Then open **http://localhost:3000**. Other people on your network can join at `http://YOUR-IP:3000`.

### Phones, tablets, Chromebooks

These are for *playing*, not hosting: open the address of a running server in the browser.
Touch controls are built in (left thumb moves, right thumb aims and shoots).

### What has actually been tested

| Platform | Status |
| --- | --- |
| Linux x64, g++ 13, Make and CMake | Built and tested, including long simulated matches under memory-error checking |
| Linux, clang | Compiles cleanly; not run |
| Linux on ARM (Raspberry Pi and similar) | Nothing platform-specific in the code, so it should build the same way; not tested |
| Desktop Chrome | Tested (headless) |
| Phone layout + touch | Tested in Chrome's phone emulation only, not on a real device |
| Windows, macOS | The first version built and passed its self-test on GitHub's Windows, macOS and Linux machines. Every later change is only proven there once it is pushed and the Actions run turns green |
| Docker image | Dockerfile written; not built |

## Options

```
--port N           port to listen on (default 3000, or the PORT environment variable)
--bind ADDR        0.0.0.0 = reachable by everyone (default), 127.0.0.1 = this machine only
--public DIR       folder with the web client (default ./public)
--bots N           bots per arena when it is empty; they make room as players join
--bot-level LEVEL  how well the bots play: easy, normal (default), hard, or mixed (every bot rolls its own)
--max-players N    players allowed in one arena (default 40)
--max-per-ip N     connections allowed from one address, 0 = unlimited (default 6)
--trust-proxy      read player addresses from X-Forwarded-For (only behind your own reverse proxy)
--start-level N    sandbox: every tank spawns at this level (1-45)
--free-level N     holding N raises a tank to this level for free; 0 = off (default 45)
--boss-minutes N   minutes between boss visits; 0 = no bosses (default 6)
--mutations N      how common mutated polygons are: a multiplier on the base odds (1 in 50,000 for shiny).
                   Default 100 = 1 shiny in 500; 0 = none; 20000 = see them all within minutes
--arena-minutes N  FFA, Maze, Teams and Tag arenas close and restart after this many minutes; 0 = never
                   (default 45 while any slot has more than one mode)
--slots LIST       arenas to run: slots separated by commas, a slot's modes separated by /
                   modes: ffa maze 2tdm 4tdm dom mazedom rush endless mother siege ctf tag br koth sandbox
                   (default ffa/maze,2tdm/4tdm,dom/mazedom/mother/siege/ctf,rush/endless,tag/br/koth,sandbox)
--no-chat          players cannot chat (by default they can: Enter, one line of up to 80 characters)
--dev-code CODE    code that unlocks the Developer tank (default 1234; "off" = nobody can)
--selftest TICKS   simulate every mode without networking and print statistics
--sim MODE MINUTES bots only, no network, at full speed: prints each game's result as it happens, then
                   kills and deaths by class. For balance work. --seed N gives a different run
```

Useful URLs on a running server: `/#gallery` shows every tank class, `/status` returns player counts as JSON.

## Developer tank

On the start screen, type the server's developer code into the **Developer code** box and click
**Unlock** (the box tells you whether the code was accepted), then press Play. You spawn
at level 45 as the **Developer** tank; press **T** and click any class to become it, including the
dominators, the Base Protector, the Arena Closer, every boss and the developer-only
**Anti-Tank Machine Gun**. Protector, closer and Anti-Tank Machine Gun cannot be damaged while you
drive them.

The code is set when the server starts: `--dev-code YOURCODE`. The default is `1234`, and because
this repository is public, **everybody can read that default**. Before strangers can reach your
server, start it with a long private code, or with `--dev-code off`. Five wrong codes end the
attempts for a connection, and twenty wrong codes within ten minutes lock developer login for ten
minutes for everyone.

## Controls

| Input | Action |
| --- | --- |
| WASD / arrows | Move |
| Mouse | Aim |
| Left click / Space | Shoot (drone classes: drones fly to the cursor) |
| Right click / Shift | Drone classes: push drones away from the cursor |
| Right click / Shift | Falcon, Eagle, Vulture and Phoenix: fire the front barrel (a secondary gun). Click / Space fires only their thrusters, and autofire never fires a secondary gun |
| E | Toggle autofire |
| C | Toggle autospin |
| R | Toggle override: auto-turrets and drones stop acting on their own and only follow your aim and fire |
| V | Reverse tank: the body faces away from the cursor (drones and turrets still follow it) |
| B | Reverse mouse: the left and right buttons swap jobs |
| 1 – 9, 0 | Spend a skill point (or click the + next to a skill) |
| Hold M + a number | Put every point that fits into that stat |
| Y U I H J K | Pick upgrade 1 to 6 (or click a class box) |
| Hold N | Level up for free (to level 45 by default) |
| Hold T | Show the class tree |
| Right click / Shift | Hunter classes: zoom towards where you aim. Desmos classes: mirror the wave |
| H | Domination and Siege: control one of your team's dominators, or leave it. Mothership: drive your team's Mothership, or leave it. (While four or more upgrade boxes are showing, H picks the fourth) |
| T | Sandbox: open or close the picker and click any class |
| F / G | Sandbox: spawn the polygon / the boss chosen on the panel, under the cursor |
| Hold O | Self-destruct, only while still spawn-protected. Protection lasts three seconds, or up to twenty while you neither move nor fire; firing ends it at once |
| P | Show or hide the kill bar |
| L | Show latency |
| Q / Z | Save a screenshot / start and stop a video recording |
| Enter | Play / respawn. While playing: open the chat box, Enter again sends the line, Esc cancels |
| X | Hide or show chat (hidden: you see nobody's lines and cannot send any) |
| Touch | Left half of the screen: move. Right half: aim and shoot. Tap +, class boxes and the Level up button. Beside the map: a button for the second action (right-click) of the class, and **Chat** |

Every key above except moving, shooting and the numbers can be changed: open **Controls** on the start
screen, click a key and press the new one. The choice is remembered by the browser.

## Project layout

```
src/
  main.cpp    entry point, options, tick loop, wiring between network and arenas
  net.*       HTTP + WebSocket server on raw sockets, with connection limits and timeouts
  defs.*      DATA: tank classes, guns, upgrade tree, food polygons, colours
  room.*      one arena: entities, movement, guns, turrets, missiles, collisions, damage, score, maze,
              teams, dominators, bosses, arena closing and restart, the Sandbox commands
  modes.cpp   the bounty on the leader and the rules of Tag, Battle Royale, Mothership, King of the Hill,
              Siege and Capture the Flag
  ai.cpp      bots (plans and builds, targeting, kiting, dodging, maze pathfinding, mode objectives),
              dominators, base protectors, arena closers, bosses
  proto.cpp   wire protocol: JSON for rare events, a binary snapshot every tick
  util.hpp    Vec2, RNG, byte reader/writer
public/
  index.html, style.css, game.js    renderer, HUD, input (no game rules)
.github/workflows/build.yml         builds and self-tests on Linux, macOS, Windows; publishes releases
Dockerfile                          container image for hosting
```

The server ticks each arena 30 times a second: input and AI, collisions on a spatial grid, movement,
deaths, then a binary snapshot to each player of what is inside their field of view. The client only
interpolates and draws. Arenas with nobody connected are frozen, so an idle server uses no CPU.

## Known limits

- No client-side prediction: fine on localhost and LAN, noticeable on a slow connection.
- No accounts or anti-cheat beyond the server validating every input. Chat has a rate limit but no word filter and no moderation tools; `--no-chat` turns it off.
- Touch controls are basic.
- Balance was tuned from bot-versus-bot simulations, not from human play. The 77 classes added in the
  second version have had far less tuning than the first 43. `./polyrift --sim ffa 180` prints kills and
  deaths by class; in such runs the Necromancer line is still far behind every other class, and the
  classes that fire in all directions (Quadruplex, Cyclone, Bent Double) lose most of their fights.
- The numbers of the newer modes (Mothership health, the Siege clock and Citadel, the hill's points,
  the Battle Royale timings) were set from bot-only games. Among bots the Siege defenders win about
  two games in three, and about two Mothership games in three are decided on time rather than by a kill.
- The class list follows the Arras-style tree as I know it. It is not a guaranteed
  match for the live game, which changes often. Some mechanics are simplified:
  classes with a second weapon fire everything together instead of using a separate alt-fire key.
- The bosses added in the third version (the five extra elites, Elite Skimmer, the rogues, the nesters,
  the mysticals, the terrestrials, Theia, Nyx, the rogue celestials, Ragnarok, the Legionary Crasher
  and Bob) share their names and general idea with Arras, but their weapons, health and rewards were
  designed here and tuned only against bots. Since the bots learned to build and dodge, a team of
  bots alone wins Boss Rush every time (45 of 45 simulated, in about 33 minutes; before, 26 of 30),
  and lasts 33 to 49 waves of Endless Boss Rush.
- Engine Acceleration raises a Smasher's acceleration and, because of how movement works here, its
  top speed with it.
- One process handles roughly 600 connections (see HOSTING.md for what to do beyond that).
