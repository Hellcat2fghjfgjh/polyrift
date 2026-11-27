# What changed in this version

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
