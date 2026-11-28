# What changed in this version

This version applies the "Updates" list: three changes to the core rules, five nerfs, eight buffs,
two design fixes, four bot improvements and eight new game modes in six slots. The protocol version
went from 8 to 9, so the server and `public/game.js` must be updated together (a tab left open on
the old version reloads itself once).

Numbers marked "tuned" were chosen or adjusted from bot-only simulations (`--sim`, new in this
version); the last section says what those showed.

## 1. Core rules

- **Health regeneration.** The base rate went from 0.0004 to 0.00025 of maximum health a tick, and
  the bonus after ten seconds without damage from ×6 to ×3. With Regeneration at 9 a tank now heals
  about 4% a second under fire (was 6.6%) and about 12% a second when left alone (was 40%), so a
  full heal takes about 8 seconds instead of 2.5. The numbers are the `REGEN_` constants at the top
  of `src/room.cpp`.
- **Shield.** The delay before the shield refills is `90 − 4 × regen` ticks (was `90 − 7 × regen`),
  and it refills at `0.005 + 0.001 × regen` of its capacity a tick (was `0.008 + 0.003 × regen`).
  At Shield Regeneration 9: starts after 1.8 seconds (was 0.9) and is full about 2.4 seconds later
  (was about 1). With no points: starts after 3 seconds as before and takes 6.7 seconds (was 4.2).
  The `SHIELD_` constants, same place.
- **Bounty on the leader.** The living tank with the highest score, if it has at least 10,000 and
  the arena has at least three tanks, is the leader (`Room::bountyLogic`, once a second). Everyone
  sees it: a gold star on the minimap, a crown on its name and on the leaderboard, a line above the
  minimap with the amount, and an arrow at the edge of the screen while it is out of view.
  Destroying it pays a quarter of its score, between 2,500 and 15,000, on top of the usual reward,
  shared by damage; everyone is told who claimed it and the leader's death screen says so. Bots go
  after the leader now and then. No bounty in Boss Rush, the Sandbox or the Battle Royale lobby.

## 2. Nerfs

| Class | Change |
| --- | --- |
| Dual | The two barrel pairs alternate (the second pair fires half a cycle after the first), and reload is 1.6 → 2.08 (×1.3) |
| Predator | Reload 1.7 → 1.9 (zoom stays 900) |
| Smasher, Mega Smasher, Spike, Auto-Smasher, Landmine | Stat cap 12 → 10 |
| X-Hunter | Reload 1.45 → 1.6 |
| Poacher | Reload of its two front barrels 1.6 → 1.75 |
| Hexa-Trapper, Septa-Trapper | At most **45** traps out at once; laying another removes the oldest (`TankDef::maxTraps`). The list suggested 30 as an example. In simulation 30 turned them from the classes that almost never die into two of the weakest (kill/death ratio about 0.2); 45 puts them near 1; 60 leaves them near 2 to 3. Tuned |

## 3. Buffs

| Class | Change |
| --- | --- |
| Necromancer, Maleficitor, Infestor | Start with 4, 4 and 3 square drones the moment a tank becomes one (`TankDef::starter`), and keep the Underseer's drones (see 4) |
| Penta Shot | Middle barrel damage 0.75 → 0.9, the two beside it 0.75 → 0.85 |
| Bent Double | Middle barrel of each fan 0.75 → 0.9 |
| Spreadshot | Its middle barrel was already 0.9, so the same step up was applied: middle barrel 0.9 → 1.05, the ten small ones 0.32 → 0.36 |
| Nailgun | Reload 0.85 → 0.7 |
| Auto-5 | Turret arc 95° → 150° (all five can bear on one target), reload 1.25 → 1.0. Tuned |
| Auto-4 | Turret arc 100° → 140°, reload 1.1 → 0.9. Tuned |
| Mega-3 | Turret arc 105° → 125°, reload 2.1 → 1.85 |
| Cyclone | Damage 0.5 → 0.65 |
| Sidewinder | Reload 2.6 → 2.0 |
| Skimmer, Twister, Rocketeer | Their missiles carry stronger guns: Skimmer and Twister 0.35 → 0.5 damage, 0.5 → 0.7 health and a slightly faster reload; Rocketeer's exhaust 0.2 → 0.32 damage. The bosses that fire the same kinds of missile keep the old ones |
| Swarmer | The hive's swarm drones 0.4 → 0.55 damage, 0.5 → 0.7 health, reload 0.9 → 0.8 |
| Gunner Trapper | Its two front barrels reload 1 → 0.65 and do 0.5 → 0.55 damage: about 85% of a Gunner's forward fire instead of half |
| Architect | Turret traps: reload 2.2 → 1.8, speed 1.1 → 1.3, damage 0.8 → 0.9, health 4 → 5.5 |
| Overtrapper | Trap: reload 1.5 → 1.35, speed 1 → 1.15, damage 0.8 → 0.85, health 4 → 5. Tuned down from a first attempt that made it one of the strongest classes |

## 4. Design fixes

- **Quadruplex** has its barrels at 0° / 90° / 180° / 270° instead of 45° / 135° / 225° / 315°, so
  one of them fires where the tank aims.
- **Drones are kept on a class change**, up to the new class's limit (`Room::setDef`). Kept drones
  take on the habits of the new class's own drones (whether they follow the cursor, whether they
  carry a gun). A different kind of drone altogether, such as a Spawner's small tanks, is let go.

## 5. Bots

- **Tri-Angle line.** A bot that flees in a class with rear thrusters turns its back on the enemy
  and lets the thrusters carry it away. Which classes those are is worked out from their guns
  (`TankDef::thrust`).
- **Wave-firing classes.** A bot works out how far off its line a weaving bullet will be when it
  arrives and aims off by that much (`Room::waveAimFix`). Classes whose barrels weave to both sides
  (Helix, Triplex) fight from a distance at which the two streams cross.
- **Dodging and difficulty.** Bots sidestep bullets that are about to hit them (`Room::botDodge`).
  `--bot-level easy | normal | hard | mixed`: easy bots do not dodge or plan and aim loosely;
  normal bots (the default) dodge most shots; hard bots see further, always react, aim more
  steadily, flee earlier and prefer wounded targets. `mixed` gives every bot its own level.
- **Builds.** A normal or hard bot chooses a level-45 class when it spawns, takes the upgrades that
  lead to it, and spends its 42 points on the preset build for that class's playstyle: ten builds
  (`BOT_BUILDS` in `src/ai.cpp`), chosen by `classify()` in `src/defs.cpp`.
- Bots also play the new modes' objectives, and Necromancer-line bots gather squares before they
  pick a fight.

## 6. New game modes

| Mode | Slot | Rules in one line |
| --- | --- | --- |
| Sandbox | 6, alone | Free levels, any class (T), a panel to spawn polygons and bosses, clear the map, set a level, add bots, switch damage off |
| Tag | 5 | Four teams, no bases; a tank that is destroyed joins the team that destroyed it; the team with everyone wins |
| Battle Royale | 5 | A lobby without damage, then no respawns; the safe circle closes in four minutes, arena closers work outside it; the last tank alive wins |
| King of the Hill | 5 | Two teams; a team alone on the hill scores a point a second; first to 150 |
| Mothership | 3 | Each team protects one big drone tank; a player can drive it with the dominator key (H); destroy the other one |
| Siege | 3 | Blue must take Green's two outer dominators and then the Citadel (a new, tougher kind) before the clock runs out |
| Capture the Flag | 3 | Two teams; carry the enemy flag to your own stand; first to 3 |
| Endless Boss Rush | 4 | Boss Rush with no last wave: every 20 waves the bosses double in health and bring three more escorts; the server keeps the record |

Slots are now `ffa/maze`, `2tdm/4tdm`, `dom/mazedom/mother/siege/ctf`, `rush/endless`,
`tag/br/koth` and `sandbox`, as the list asked. A slot still picks one of its modes at random for
every new game.

## 7. Also new

- `--bot-level`, and `--sim MODE MINUTES` (with `--seed N`): bots only, no network, at full speed,
  printing each game's result and kills and deaths by class.
- The death screen has one more line where there is something to say: the bounty, the team a
  tagged tank joined, a Battle Royale placing.
- Key bindings for the two Sandbox keys (F and G by default) in the Controls panel.

## 8. Follow-up: a slower Destroyer Dominator shell

Asked for after the list, to make fighting it fairer. The shell of the **Destroyer Dominator** flies
at 0.8 of the base bullet speed instead of 1.3: 8 units a tick, the speed of a Destroyer's own shell,
down from 13. It still destroys any tank it touches, reloads as slowly as before and reaches as far:
its lifetime went from 2.2 to 2.4 so that it still gets to the edge of the dominator's view. One line
in `src/defs.cpp`, the block named `"Destroyer Dominator"`. The Gunner and Trapper Dominators and
Siege's Citadel are unchanged.

In bot-only Domination (12 simulated hours, the same three seeds each time) the Destroyer Dominator
destroyed 1,030 tanks at speed 1.3, 869 at 1.0, 680 at 0.8 and 678 at 0.65, so 0.8 is where slowing
it stops helping the bots. It is still the deadliest of the three kinds. Siege, where it is often
one of the outer dominators, moved with it: over 15 simulated hours the attackers won 44 games of
86, against 27 of 84 before.

## Files

| File | Change |
| --- | --- |
| `src/defs.hpp`, `src/defs.cpp` | The class changes above; `maxTraps`, `starter`, `thrust`, `wave`, `waveSym`, `style` on `TankDef`; `classify()`; the Launcher line's own missiles; new hidden classes Mothership and Citadel |
| `src/room.hpp`, `src/room.cpp` | Eight modes in `enum Mode` and `configure`; regeneration and shield constants; trap limit; kept and starter drones; bounty payout; Tag conversion; death-screen note; the dominator key drives the Mothership; Endless waves; Sandbox commands (message 10) |
| `src/modes.cpp` | New: the bounty and the rules of Tag, Battle Royale, Mothership, King of the Hill, Siege and Capture the Flag |
| `src/ai.cpp` | Bot plans, preset builds, dodging, difficulty, thruster fleeing, wave aiming, mode objectives; Mothership and Battle Royale closer behaviour |
| `src/proto.cpp` | Protocol 9: mode data in `init` and `lb`, a flag bit for a dominator that cannot be hurt |
| `src/main.cpp` | New mode names and default slots, `--bot-level`, `--sim`, `--seed`, the new rule checks in `--selftest` |
| `public/game.js`, `index.html`, `style.css` | Leader marker, crown and arrow; drawing and status lines for every new mode; the Sandbox panel and class picker; six slot buttons; the death-screen note |
| `README.md`, `docs/PLAYING.md`, `docs/DEVELOPING.md` | Updated to match |

Unchanged: `src/net.cpp`, `src/net.hpp`, `src/util.hpp`, `Makefile`, `CMakeLists.txt`, `Dockerfile`,
`docs/HOSTING.md`, the GitHub workflow.

## What was checked, and what was not

- Builds with g++ 13 (`-Wall -Wextra`, no warnings), with clang, with Make and with CMake.
- `--selftest` passes, with new scripted checks for: the regeneration and shield numbers; the trap
  limit; drones kept through Overseer → Overlord; the Necromancer's starter drones; the bounty paid
  and announced; bots at each level (all 42 points spent, the planned class reached, dodging on or
  off); the wave-aim correction landing shots on a small target that straight aim misses; and one
  check for each new mode (Tag conversion and win, the hill scoring to 150, three flag captures,
  the Citadel's shield and both Siege endings, driving the Mothership and losing it, a whole Battle
  Royale from lobby to winner, Endless past wave 20, every Sandbox command and its rate limit).
  It also ran clean under AddressSanitizer and UndefinedBehaviorSanitizer, as did 25 simulated
  minutes of every new mode.
- The client was driven in headless Chrome through every new mode with no script errors: the
  Sandbox panel, picker and F / G keys; driving and leaving the Mothership; a Battle Royale from
  the lobby to the death screen ("You placed 6 of 15") and the refused respawn; the Sandbox panel on
  a phone-sized touch screen.
- Not tested: real human play, real phones, Windows and macOS builds (the first GitHub Actions run
  will show), other browsers.

## What the simulations showed

Bot-only free-for-all, 24 simulated hours over eight seeds, normal bots. Kill/death ratio by class;
1 is average. The bots themselves changed in this version, so compare these with the list's old
figures with care.

| Class | Before (from the list) | Now |
| --- | --- | --- |
| Dual | 10 to 11 | 3.9 |
| Predator | 3.8 to 5.8 | 2.5 |
| X-Hunter, Poacher | | 1.8, 1.3 |
| Hexa-Trapper, Septa-Trapper | "almost never die" | 1.0, 0.8 |
| Gunner Trapper | 0.26 to 0.36 | 0.7 |
| Architect, Overtrapper | 0.26 to 0.36 | 1.5, 1.5 |
| Mega-3, Auto-4, Auto-5 | | 1.3, 0.9, 0.8 |
| Twister, Swarmer, Skimmer, Rocketeer | | 1.5, 2.2, 0.9, 0.3 |
| Nailgun, Sidewinder | | 0.7, 0.4 |
| Spreadshot, Penta Shot, Bent Double | | 0.4, 0.3, 0.2 |
| Cyclone, Quadruplex | | 0.14, 0.07 |
| Necromancer, Infestor, Maleficitor | | 0.03, 0.03, 0.00 |
| Spike, Mega Smasher, Auto-Smasher, Landmine | | 3.0, 3.0, 2.9, 2.1 |

What stands out:

- **The Necromancer line is still far behind.** Starter drones and kept drones are in, and the bots
  now gather squares first, but in these runs they destroy 20 to 50 polygons a life and still die
  with a handful of drones: the drones are used up as fast as they are made. Making the converted
  drones sturdier (their health is 1.6 against an Overseer drone's 2.5) was tried and raised the
  Infestor to about 0.2; it is not in this version because the list did not ask for it.
- **The Smasher line is still strong at a cap of 10** (2 to 3). The list's other option, making
  each point give less, is the next thing to try.
- **Classes that fire in every direction** (Quadruplex, Cyclone, Bent Double) lose to bots that
  fight one target at a time, whatever the damage numbers. Expect them to do better with people.
- **New top of the table:** Engineer 4.6, Big Cheese 4.3, Dual 3.9, Commander 3.0. The first two
  were not touched; the preset builds suit them.
- **Boss Rush is easier for bots.** A bot-only team now wins every game (45 of 45, about 33 minutes
  each; 26 of 30 before). Endless Boss Rush with bots lasts 33 to 49 waves.
- **Mode lengths with bots only:** Tag about 6 minutes, Battle Royale about 3 after the lobby, King
  of the Hill about 8, Capture the Flag about 11 (3 of 42 games went to overtime). Siege: the
  attackers won 20 of 65 (about half since the follow-up in section 8). Mothership: 18 of 51 games
  ended with a Mothership destroyed, the rest on time.

---

# Earlier: what changed in the version before

This version adds the Arras-style features listed below. The protocol version went from 6 to 7,
so the server and `public/game.js` must be updated together (an old tab reloads itself).

## 1. Score, kills and assists

- **Damage-based score.** Every tank and polygon remembers who damaged it and how much
  (`Entity::hits`, filled in `Room::hurt`). When it dies its score is shared between those players
  in proportion to their damage (`Room::damageShares`), instead of all going to the last hit.
  This applies to polygons, player kills and boss rewards.
- **Kill assists.** The last hit earns the kill. Anyone else who did at least 10% of the damage to a
  tank (5% to a boss) earns an assist and is told "You assisted in killing …".
- **Kill bar.** A bar beside the score bar counts kills, assists and boss kills for the current life.
  `P` hides it; the choice is remembered.
- **Death summary.** The death screen now shows score, level and time alive, then tanks killed,
  assists, bosses defeated and polygons destroyed, and names up to three players who helped.
- Healers earn score for the health they restore to other players.

## 2. Smasher stats

Smasher, Mega Smasher, Spike, Auto-Smasher and Landmine have five stats (Body Damage, Max Health,
Engine Acceleration, Shield Regeneration, Shield Capacity) capped at 12. Engine Acceleration takes
the Reload slot. Points in stats the new class does not have are refunded on upgrade. Bots follow
the same rules. The Auto-Smasher's turret got a fixed, slightly stronger gun because it no longer
has bullet stats.

## 3. Tanks (121 → 128 playable classes)

| Class | From | What it does |
| --- | --- | --- |
| Assembler | Builder | Its blocks fuse when they touch: bigger, tougher, harder-hitting, up to 2.2 tank widths |
| Manager | Director (level 45) | Eight drones; fades from sight while it holds still |
| Big Cheese | Director (level 45) | One huge, strong drone |
| Medic | Healer | Long barrel: faster repair shots, wider view |
| Ambulance | Healer | Healer with Tri-Angle thrusters |
| Surgeon | Healer | Lays blocks whose turret repairs the most hurt team-mate nearby |
| Paramedic | Healer | Three repair barrels in a fan |

- **Desmos alt-fire:** right-click or Shift mirrors the wave for Desmos, Helix, Volute, Triplex,
  Quadruplex and Sidewinder.
- **Extra upgrade paths:** Twin can go straight to Musket or Bulwark at level 45.

## 4. Polygons

- **Hexagons:** about 1 in 48 polygons; more in the nest.
- **Sizes for every shape:** Normal, Beta, Alpha and Omega (Beta and Alpha Pentagons keep their old
  numbers). Mazes stop at what fits in a corridor.
- **Crashers:** about 1 in 20 polygons (more in the nest), small or large. They chase and ram the
  nearest visible tank, and leave bosses and tanks inside a base alone.
- **Mutations:** Shiny, Legendary, Shadow, Rainbow, Trans and Platonic solids, each tougher and worth
  100 to 25,000 times the score (capped at 5,000,000). Shadow polygons fade in and out; solids are
  drawn as turning 3D wireframes. New option `--mutations N` scales the odds: default 100
  (1 shiny in 500), 1 = the base odds of 1 in 50,000, 0 = off.
- **Evolution:** polygons left alone slowly grow into the next shape, then the next size. Mutated
  ones grow about twenty times faster, so a shiny egg rarely stays an egg.

## 5. Bosses (8 → 34)

- **Every arena:** Elite Sprayer, Elite Battleship, Elite Spawner, Elite Trap Guard, Elite Spinner,
  Elite Skimmer and Rogue Palisade join the Elite Destroyer, Elite Gunner, Summoner and Nest Keeper.
  Each kind visits once before any returns.
- **Boss Rush only:** Rogue Armada, Nest Warden, Nest Guardian, Sorcerer, Enchantress, Exorcistor,
  Ares, Gersemi, Ezekiel, Selene, Eris, Theia, Nyx, Julius, Genghis, Napoleon, Ragnarok, Legionary
  Crasher and Bob. Waves: celestials at 5, 10, 15; terrestrials at 8 and 14; Legionary Crasher at 12;
  a rogue celestial at 17; Kronos (weakened) at 19; Ragnarok at 20.
- **Group arrivals:** in a busy arena two or three bosses can arrive together.
- **Names:** elites, nesters, mysticals and rogues get a name of their own ("Vexa the Elite Gunner"),
  shown above them and in every message. Each boss shows on the minimap in its own colour.
- Rewards differ per boss (30,000 for an elite up to 350,000 for Ragnarok) and are shared by damage.

## 6. Controls

`V` reverse tank · `B` reverse mouse · hold `O` to self-destruct (see below) · `Y U I H J K` pick
upgrades 1 to 6 · hold `M` + a number to max a stat · `L` latency · `Q` screenshot · `Z` record
video · `P` kill bar. All action keys can be rebound from **Controls** on the start screen.
`H` picks the fourth upgrade while four or more boxes are showing; otherwise it is still the
dominator key.

## 7. Follow-up: self-destruct and Team Polygon

- **Self-destruct only at spawn.** `O` now works only while the tank is still spawn-protected (the
  first three seconds, or until it fires), and the hold is one second. The server enforces this, so
  it can undo a bad spawn but cannot pull a tank out of a fight, deny a kill or act as a free trip
  home. The HUD shows a hint while it is available.
- **Team Polygon.** Neutral (uncaptured) dominators and the arena closers that spawn when an arena
  closes are now on the polygons' team, with the bosses. They no longer shoot or break shapes, and
  bosses, closers and neutral dominators no longer damage each other; all of them still fight the
  players. A captured dominator belongs to its team as before and returns to Team Polygon when it is
  knocked back to neutral. Closers still pass through maze walls and ignore bases (now a flag on the
  closer and its shots instead of a team of its own).
- **One team for everything unowned.** Fallen (yellow) sanctuaries in Boss Rush join Team Polygon as
  well, and the separate "neutral" team is gone from the code: every entity is either on a player
  team or on Team Polygon. Boss shots, drones and traps now pass through a fallen sanctuary instead
  of breaking on it, and it no longer hurts a boss that touches it. Only the players can still
  damage it, which is how they win it back; restored, it returns to the players' team.
- `--selftest` checks both.
- **Longer protection for a tank that stands still.** Self-destruct was nearly unusable at first,
  because protection lasted a flat three seconds: the key had to go down within two seconds of
  spawning. Now a tank that has neither moved nor fired stays protected for up to twenty seconds
  (three seconds as before once it moves; firing still ends it at once), and the hold is 0.7 seconds.
  The HUD says while it is available, and says why when it is not.
- **Controls panel stays on screen.** Opening it used to leave its lower rows below the edge of the
  screen (the menu had to be scrolled). It now scrolls itself into view and hides the key summary
  above it while open.

## 8. Follow-up: GitHub workflow and packaging

- **Workflow in the right place.** The build workflow is now at `.github/workflows/build.yml`, beside
  the `polyrift` folder, which is the layout it expects (it runs inside `polyrift`). In the original
  upload it sat in a folder named `.githubworkflowsbuild.yml`, where GitHub does not look.
- **Linux runner.** `ubuntu-22.04` became `ubuntu-24.04`, because GitHub is retiring the 22.04
  image. The Linux download is therefore built on 24.04 and may not start on older distributions
  (build from source there); `docs/PLAYING.md` says so. Nothing else in the workflow changed.
- **Self-test portability.** In `src/main.cpp` a scripted test player was renamed and a few standard
  headers are now included explicitly, so the Windows compiler is less likely to object. No game
  rule changed.

## 9. Follow-up: chat, secondary guns, start screen

- **Chat.** Press **Enter** while playing, type one line (up to 80 characters) and press Enter
  again; Esc or a click on the game cancels. The line appears in a bubble above your tank for six
  seconds and in a short list above the skill panel for everyone in the arena. Your tank does not
  move or shoot while the box is open, and the letters you type do not trigger game keys.
  - Server side (`Room::onMessage`, message `9`): only a tank that is in the arena can chat; the
    text is cleaned by `sanitizeChat` (valid UTF-8, no control, invisible or text-direction
    characters, runs of spaces squeezed, cut at 80 characters); three lines in a row are allowed,
    then one every two seconds; the same line again within ten seconds is refused.
  - **X** (rebindable) hides chat for you: nobody's lines are shown and you cannot send any.
  - `--no-chat` turns chat off for the whole server.
  - Touch screens get a **Chat** button beside the map; the box opens at the top of the screen,
    clear of the on-screen keyboard.
  - There is no word filter, mute, report or log. See `docs/HOSTING.md` before running it in public.
- **Secondary guns.** Falcon, Eagle, Vulture and Phoenix now work as in arras: click / Space fires
  only the thrusters, and the front barrel fires on **right-click or Shift** ("Hold right-click or
  Shift to fire your main barrel" is shown while you drive one). Autofire does not fire a secondary
  gun, as in arras. Holding both fires everything.
  - Any gun can be made secondary with `.alt()` in `src/defs.cpp` (`GunDef::alt`); `bird()` does it
    for the four classes above. `Room::updateGuns` runs the two triggers separately.
  - Bots hold both triggers when they attack, so they play these classes as before.
  - Firing a secondary gun ends spawn protection and reveals an invisible class, like firing a main gun.
  - Touch screens get a button beside the map for the class's second action: **Barrel** for these
    four, and also **Zoom** (Hunter line), **Mirror** (Desmos line) and **Repel** (drone classes),
    which touch players could not use before. Tap to switch on, tap again to switch off.
  - Rear trap launchers (Trap Guard, Bushwhacker, Conqueror and the like) still fire with the main
    gun: the arras pages I could reach name only the four classes above as having a right-click barrel.
- **Start screen: the part below Play.** On a window 560 pixels high or less (a small laptop
  window, a browser zoomed in, a phone on its side) a style rule hid the key list and the Controls
  button completely. That rule is gone: both are always shown now.
  - Short and wide windows (at least 700 wide) put the key list and Controls beside the form, so
    everything fits without scrolling.
  - Short and narrow windows keep one column and scroll; the scrolling area now has room at the
    bottom and scrolls by finger.
  - The key list mentions the second weapon, chat and the chat on / off key.
- **Protocol 8.** Tabs left open from an older version reload themselves once.

## Files

| File | Change |
| --- | --- |
| `src/defs.hpp`, `src/defs.cpp` | Polygon size and mutation tables, 7 new classes, 26 new boss classes, per-class stat sets, new gun flags (`merge`, `dshape`, `alt`), new links |
| `src/room.hpp`, `src/room.cpp` | Damage ledger and shared payouts, assists, polygon spawning / evolution, block merging, boss table, group arrivals, names, new Boss Rush waves, self-destruct, latency and chat messages, separate trigger for secondary guns |
| `src/ai.cpp` | Crasher AI, bots respect per-class stats, charging and fast-spinning bosses |
| `src/util.hpp` | `sanitizeChat` |
| `src/proto.cpp` | Protocol 8: chat broadcast, kill-bar counters, polygon size / crasher bits, per-class stat data, boss colours |
| `src/main.cpp` | `--mutations`, `--no-chat`, rule checks added to `--selftest`, extra statistics |
| `public/game.js`, `index.html`, `style.css` | Kill bar, death summary, new polygon and boss drawing, per-class stat panel, new keys, Controls panel, chat box / list / bubbles, touch buttons, start-screen layout |
| `README.md`, `docs/PLAYING.md`, `docs/DEVELOPING.md`, `docs/HOSTING.md` | Updated to match |
| `.github/workflows/build.yml` (outside `polyrift`) | Moved to where GitHub runs it; Linux runner 22.04 → 24.04 |
| `CHANGES.md` | New: this file |

Unchanged from the original: `src/net.cpp`, `src/net.hpp`, `Makefile`, `CMakeLists.txt`, `Dockerfile`.

## What was checked, and what was not

- Builds with g++ 13 (`-Wall -Wextra`, no warnings) and clang.
- `--selftest` passes, including new scripted checks for shared score, kill + assist, the death
  summary, Smasher stats and Assembler merging. It also ran clean under AddressSanitizer and
  UndefinedBehaviorSanitizer.
- The client was exercised in headless Chrome: menu, Controls panel and rebinding a key, HUD, kill
  bar, new classes, mutated polygons, bosses driven through developer mode, self-destruct, the death
  screen, and saving a screenshot and a recording.
- The workflow's Linux steps (configure, build, package, self-test, archive) were run by hand and pass.
- Follow-up 9: the self-test has new scripted checks for secondary guns (an Eagle's fire button
  launches only thruster shots, alt-fire only the front barrel) and chat (cleaning, delivery, length
  cut, flood and repeat limits, menu spectators refused, `--no-chat`). In headless Chrome: two
  players chatting, typing not reaching the game keys, Esc, the flood limit, hiding chat, the Eagle
  hint; the start screen measured at nine window sizes from 330×480 to 1280×760 (key list and
  Controls button shown at every size, all 21 key rows reachable); with touch emulation, dragging
  the start screen by finger and the Barrel and Chat buttons.
- Not tested: real human play, real phones (touch was only emulated), Windows and macOS builds (the first GitHub Actions
  run will show), other browsers.
- Balance is a first pass. The new bosses' weapons and numbers were designed here, not copied from
  Arras, and were tuned only against bots: in 30 simulated bot-only Boss Rush games (run after the
  Team Polygon follow-up) the bots won 26, in 33 to 90 minutes, 55 on average. Before that follow-up
  they won 8 of 12; the original version won 3 of 3 in 44 to 55 minutes.
