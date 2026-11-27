# Polyrift: complete guide to downloading and playing

This guide takes you from the GitHub page to playing, step by step. It is written for people who
have never used GitHub or built a program before.

Wherever you see `YOUR-NAME/polyrift`, use the address of the GitHub repository you are on.

**Contents**

1. [What you are getting](#1-what-you-are-getting)
2. [Open the GitHub page and choose a download](#2-open-the-github-page-and-choose-a-download)
3. [Get it running: Windows](#3-get-it-running-windows) · [macOS](#3-get-it-running-macos) · [Linux](#3-get-it-running-linux)
4. [Open the game in your browser](#4-open-the-game-in-your-browser)
5. [How to play](#5-how-to-play)
6. [Play with other people](#6-play-with-other-people)
7. [Change the settings](#7-change-the-settings)
8. [Stop, update, remove](#8-stop-update-remove)
9. [If something goes wrong](#9-if-something-goes-wrong)
10. [Common questions](#10-common-questions)

---

## 1. What you are getting

Polyrift has two parts:

- **The server**: one small program (`polyrift.exe` on Windows, `polyrift` elsewhere). It runs the whole game.
- **The game screen**: an ordinary web page that the server hands to your browser.

So playing always means the same two actions: **start the server, then open a web address in your browser.**
On your own computer that address is **http://localhost:3000**. "localhost" just means "this computer".

Nothing is installed into your system, you do not need a GitHub account, and bots fill the arenas so
you can play alone. You only need the internet to download it.

If someone else is already running a server and gave you an address, you do not need any of this:
type their address into your browser and skip to [step 5](#5-how-to-play).

---

## 2. Open the GitHub page and choose a download

Open the repository page (`https://github.com/YOUR-NAME/polyrift`). You will see a list of files and
folders (`src`, `public`, `docs`, `README.md` …). There are two things on that page you can download:

| | **Way A: the ready-made program** | **Way B: the source code** |
| --- | --- | --- |
| Where | **Releases**, in the right-hand column (on a phone: below the file list) | The green **Code** button above the file list → **Download ZIP** |
| What you get | The program already built, plus the `public` folder | The code only; you build the program yourself |
| Effort | Unzip and run | Install a free compiler once, then two or three commands |
| Choose it when | You just want to play | Releases is empty or says "No releases published", your computer is not listed there, or you want to change the game |

Files under Releases are named by system:

| Your computer | File to download |
| --- | --- |
| Windows (64-bit) | `polyrift-windows-x64.zip` |
| Mac with Apple Silicon (M1 or newer) | `polyrift-macos-arm64.tar.gz` |
| Linux, 64-bit Intel/AMD | `polyrift-linux-x64.tar.gz` |
| Intel Mac, Raspberry Pi, anything else | No ready-made file: use Way B |

Now follow the section for your system.

---

## 3. Get it running: Windows

### Way A: ready-made program

1. On the repository page click **Releases**, open the newest one, and under **Assets** click **`polyrift-windows-x64.zip`**.
2. Open your Downloads folder, right-click the zip → **Extract All** → **Extract**.
3. Open the extracted `polyrift` folder. It contains `polyrift.exe` and a `public` folder. **Keep these two together.**
4. Double-click **`polyrift.exe`**.
   - A blue box "Windows protected your PC" may appear, because the program is not signed by a company. Click **More info**, then **Run anyway**.
   - Windows Firewall may ask about network access. Tick **Private networks** and click **Allow** if friends will join; if you play alone you can click Cancel.
5. A black window opens and prints `Polyrift server running`. **Leave this window open while you play.**

Continue at [step 4](#4-open-the-game-in-your-browser).

### Way B: build from the source code

Do this once:

1. Install **Visual Studio Community** (free) from visualstudio.microsoft.com. When the installer shows a list of "workloads", tick **Desktop development with C++** and install. It is a large download.
2. On the repository page click the green **Code** button → **Download ZIP**.
3. Right-click the downloaded zip → **Extract All**. You get a folder called `polyrift-main`.

Build and start:

1. Open the Start menu, type **Developer PowerShell for VS**, and open it.
2. Go into the folder. Adjust the path if you extracted it somewhere else:

   ```
   cd $HOME\Downloads\polyrift-main\polyrift-main
   ```

   (Extract All usually makes a folder inside a folder with the same name. If the command fails, try with `polyrift-main` only once. You are in the right place when `dir` lists `README.md`, `src` and `public`.)
3. Build the program. The first line prepares, the second compiles; together they take a minute or two:

   ```
   cmake -S . -B build
   cmake --build build --config Release
   ```

4. Start the server:

   ```
   build\Release\polyrift.exe
   ```

It prints `Polyrift server running`. Leave the window open and continue at [step 4](#4-open-the-game-in-your-browser).
Next time you only need to start `build\Release\polyrift.exe` (double-clicking it works too).

---

## 3. Get it running: macOS

### Way A: ready-made program (Apple Silicon only)

1. On the repository page click **Releases**, open the newest one, and download **`polyrift-macos-arm64.tar.gz`**.
2. In Downloads, double-click the file. A `polyrift` folder appears.
3. Open **Terminal** (press Cmd+Space, type `Terminal`, press Return) and run these three lines:

   ```
   cd ~/Downloads/polyrift
   xattr -dr com.apple.quarantine .
   ./polyrift
   ```

   The second line tells macOS to stop blocking a program that came from the internet and is not
   signed by a registered developer. If macOS still refuses, use Way B.

It prints `Polyrift server running`. Leave Terminal open and continue at [step 4](#4-open-the-game-in-your-browser).

### Way B: build from the source code (any Mac)

Open **Terminal** and run these lines one at a time:

```
xcode-select --install
```

A box appears offering to install the command line developer tools. Click **Install** and wait for
it to finish (only needed once). Then:

```
cd ~/Downloads
git clone https://github.com/YOUR-NAME/polyrift.git
cd polyrift
make
./polyrift
```

(If you used **Code → Download ZIP** instead of `git clone`, double-click the zip and use
`cd ~/Downloads/polyrift-main` before `make`.)

It prints `Polyrift server running`. Leave Terminal open and continue at [step 4](#4-open-the-game-in-your-browser).

---

## 3. Get it running: Linux

### Way A: ready-made program (64-bit Intel/AMD)

Download **`polyrift-linux-x64.tar.gz`** from **Releases**, then in a terminal:

```
cd ~/Downloads
tar xzf polyrift-linux-x64.tar.gz
cd polyrift
./polyrift
```

The file is built on Ubuntu 24.04. On an older distribution it can fail with a `GLIBC` version
error; if so, use Way B.

### Way B: build from the source code (any Linux, including Raspberry Pi)

Install the compiler once:

| Distribution | Command |
| --- | --- |
| Ubuntu, Debian, Mint, Raspberry Pi OS | `sudo apt install build-essential git` |
| Fedora | `sudo dnf install gcc-c++ make git` |
| Arch, Manjaro | `sudo pacman -S base-devel git` |

Then:

```
git clone https://github.com/YOUR-NAME/polyrift.git
cd polyrift
make
./polyrift
```

It prints `Polyrift server running`. Leave the terminal open and continue below.

---

## 4. Open the game in your browser

1. With the server window still open, start your web browser.
2. In the address bar type **`http://localhost:3000`** and press Enter.
3. The menu appears with a live game running behind it. Type a name, pick a mode, press **Play**.

If the page does not load, see [step 9](#9-if-something-goes-wrong).

---

## 5. How to play

- **Goal:** shoot polygons and other tanks to earn score. Score raises your level, up to 45.
- **Level up fast:** hold **N** and your tank levels up for free, to level 45. On a touch screen, tap **Level up**.
- **Skill points:** levelling up gives points to spend on ten skills (bottom-left), 42 in total at level 45. Press the number shown next to a skill, or click its **+**. Hold **M** and press the number to fill a skill in one go. The Smasher classes have only five skills, but each takes 12 points instead of 9.
- **Classes:** at levels 15, 30 and 45, boxes appear top-left. Click one to turn your tank into that class. There are 128 classes; hold **T** to see them all and which ones you can still reach.
- **Bosses:** every few minutes big computer-controlled bosses arrive, one at a time or two or three together. A diamond in the boss's colour on the minimap shows where each one is. Its reward (30,000 or more) is shared between everyone who damaged it.
- **Score and assists:** a polygon, a tank or a boss pays everyone who damaged it, by how much damage they did. The last hit gets the kill; others who did a real part get an assist. The red bar beside your score counts kills, assists and boss kills (**P** hides it).
- **Polygons:** besides squares, triangles and pentagons there are hexagons, and every shape also comes in Beta, Alpha and Omega sizes. Pink **crashers** chase and ram you. A glowing polygon is a rare mutation worth a great deal: shoot it.
- **Base protectors:** in team modes, each base has protectors that cannot be hurt. Their drones barely hurt but knock enemies far away from the base.
- **Dominators:** the Gunner cannot see far but shreds anything close; the Destroyer sees twice as far and kills with one slow shot; the Trapper lays traps all the time. Attack a Gunner from long range and keep moving against a Destroyer.
- **Mazes:** every maze game rolls a new wall layout, named on the "NEW GAME" banner.
- **Arena closers:** when a round ends, yellow arena closers hunt down everyone left. They cannot be hurt. After that a new round starts and all scores go back to zero.
- **Dying:** you respawn with part of your score. Press Enter.

The start screen has four mode buttons. Each one is a **slot** that switches between modes: slot 1 is FFA or Maze, slot 2 is 2 Teams or 4 Teams, slot 3 is Domination or Maze Domination, and slot 4 is Boss Rush. The button shows what the slot is playing now. A slot picks again at random when its game ends (FFA, Maze and Teams restart every 45 minutes).

| Mode | Rules |
| --- | --- |
| FFA | Everyone against everyone. The centre has the most valuable polygons. |
| Maze | FFA inside a random maze. Walls stop bullets. |
| 2 Teams / 4 Teams | Shoot the other colours. Enemy bases hurt you, so stay out. |
| Maze Domination | Domination, but inside a maze. |
| Boss Rush | Everyone is on the same team. Bosses come in 20 waves and walk towards your four sanctuaries (the blue squares on the minimap). Destroy every boss to start the next wave. Waves 5, 10 and 15 bring a giant celestial, 8 and 14 terrestrials, 12 the Legionary Crasher, 17 a rogue celestial, 19 Kronos and 20 the final boss, Ragnarok. You win by clearing wave 20. A destroyed sanctuary turns yellow and stops working; shoot the yellow sanctuary until it breaks to get it back. If all four are yellow, nobody can respawn and a 30-second countdown starts: restore one in time or the game is lost. You respawn beside a blue sanctuary, and standing near one heals you. The Healer classes can repair team-mates and sanctuaries. |
| Domination | Two teams. Destroy a yellow dominator turret to capture it; a captured one must be knocked back to yellow before the other team can take it. The team that holds all four wins the game; the arena then closes and a new game starts. |

| Input | Action |
| --- | --- |
| WASD or arrow keys | Move |
| Mouse | Aim |
| Left click or Space | Shoot |
| Right click or Shift | Drone classes: push drones away from the cursor |
| Right click or Shift | Falcon, Eagle, Vulture and Phoenix: fire the front barrel. For these four, left click or Space fires only the thrusters at the back, and autofire (E) does not fire the front barrel |
| E | Autofire on/off |
| C | Autospin on/off |
| R | Override on/off: with it on, auto-turrets and drones do nothing by themselves and only shoot where you aim while you fire |
| V | Reverse tank: your tank faces away from the cursor |
| B | Reverse mouse: the right button shoots, the left one is the second action |
| 1 to 9, 0 | Spend a skill point |
| Hold M and press a number | Fill that skill with every point that fits |
| Y, U, I, H, J, K | Pick the first to sixth upgrade box |
| Hold N | Level up for free |
| Hold T | Show the class tree |
| Right click or Shift | Hunter, Predator and the other Hunter classes: zoom towards where you aim. Desmos, Helix and their upgrades: mirror the wave |
| H | Domination: take control of one of your team's dominators; press again to leave. (While four or more upgrade boxes are showing, H picks the fourth box instead.) |
| Hold O | Self-destruct and spawn again. Only works while you are still spawn-protected: your tank blinks for three seconds after spawning, or for up to twenty if you do not move or fire. Firing ends it at once |
| P | Kill bar on/off |
| L | Show latency |
| Q, Z | Save a screenshot; start or stop recording a video |
| Enter | Play / respawn. In the game: open the chat box, type one line (up to 80 characters), Enter sends it, Esc cancels. Your tank does not move while the box is open |
| X | Hide or show chat |
| Touch screen | Left thumb moves, right thumb aims and shoots, tap **+** and class boxes. Two buttons beside the map: the class's second action (Barrel, Zoom, Mirror or Repel; tap to switch it on, tap again to switch it off) and **Chat** |

To change a key, click **Controls** on the start screen, click the key and press the new one.

---

## 6. Play with other people

Only **one** person runs the server (the "host"). Everyone else just opens the host's address in a
browser, on a computer, phone or tablet, and lands in the same arenas.

### On the same Wi-Fi or network

1. The host starts the server as above.
2. The host finds the computer's local address:

   | Host system | How |
   | --- | --- |
   | Windows | Open Command Prompt, run `ipconfig`, read **IPv4 Address** (looks like `192.168.1.23`) |
   | macOS | In Terminal: `ipconfig getifaddr en0` |
   | Linux | In a terminal: `hostname -I` (use the first address) |

3. Everyone else types `http://THAT-ADDRESS:3000`, for example `http://192.168.1.23:3000`.

`localhost` only works on the host's own computer. Everyone else must use the host's address.

If it does not load for the others:

- **Windows host:** the firewall must allow the program on Private networks. To fix it afterwards: Windows Security → Firewall & network protection → Allow an app through firewall → tick `polyrift`.
- **Linux host with a firewall:** `sudo ufw allow 3000/tcp` (Ubuntu) or `sudo firewall-cmd --add-port=3000/tcp` (Fedora).
- **macOS host:** if asked whether to accept incoming connections, choose **Allow**.
- Some school, hotel and guest networks stop devices from reaching each other. Nothing in the game can change that.

### With friends somewhere else

They cannot reach a home computer unless the host sets up port forwarding on the router, which
exposes the home internet address. To play across the internet, run the server on a rented machine
instead; **[HOSTING.md](HOSTING.md)** explains how.

### On phones and tablets

Open the host's address in the phone's browser and turn the phone sideways.

---

## 7. Change the settings

Settings are words typed after the program name when you start the server.

- **Windows:** open the folder that contains `polyrift.exe`, click the address bar at the top of the window, type `cmd` and press Enter. Type the command in the black window that opens.
- **macOS / Linux:** type the command in the terminal, in the project folder.

| You want | Windows | macOS / Linux |
| --- | --- | --- |
| Everyone starts at max level, to try any class at once | `polyrift.exe --start-level 45` | `./polyrift --start-level 45` |
| No bots, just you and friends | `polyrift.exe --bots 0` | `./polyrift --bots 0` |
| More bots | `polyrift.exe --bots 25` | `./polyrift --bots 25` |
| Turn off the free level-up key | `polyrift.exe --free-level 0` | `./polyrift --free-level 0` |
| No bosses (or more often: use a small number of minutes) | `polyrift.exe --boss-minutes 0` | `./polyrift --boss-minutes 0` |
| See the rare mutated polygons often (0 turns them off) | `polyrift.exe --mutations 20000` | `./polyrift --mutations 20000` |
| FFA, Maze and Teams restart (and re-roll their mode) every hour instead of every 45 minutes | `polyrift.exe --arena-minutes 60` | `./polyrift --arena-minutes 60` |
| Choose the modes yourself, with no random switching | `polyrift.exe --slots ffa,maze,2tdm,4tdm,dom,mazedom,rush` | `./polyrift --slots ffa,maze,2tdm,4tdm,dom,mazedom,rush` |
| Only one arena, always Boss Rush | `polyrift.exe --slots rush` | `./polyrift --slots rush` |
| A different port (then browse to `http://localhost:8080`) | `polyrift.exe --port 8080` | `./polyrift --port 8080` |
| No chat between players | `polyrift.exe --no-chat` | `./polyrift --no-chat` |
| More than 6 devices from one network | `polyrift.exe --max-per-ip 0` | `./polyrift --max-per-ip 0` |
| Only this computer may connect | `polyrift.exe --bind 127.0.0.1` | `./polyrift --bind 127.0.0.1` |
| List every setting | `polyrift.exe --help` | `./polyrift --help` |

Settings can be combined: `polyrift.exe --bots 0 --start-level 45`.

**Windows shortcut:** in the folder with `polyrift.exe`, create a text file, put one line in it such
as `polyrift.exe --bots 25`, and save it as `start.bat`. Double-click `start.bat` to start with those settings.

While the server runs, `http://localhost:3000/#gallery` shows every tank class on one page.

---

## 8. Stop, update, remove

**Stop:** close the server window, or click it and press Ctrl+C. The browser tab shows
"Connection lost" until you start the server again.

**Start again later:** repeat only the last action of step 3 (double-click `polyrift.exe`, or run
`./polyrift` in the folder), then open `http://localhost:3000`.

**Update to a newer version:**

| How you got it | How to update |
| --- | --- |
| Ready-made program | Download the newest release and replace the old folder |
| `git clone` | In the project folder run `git pull`, then build again (`make`, or the two `cmake` lines on Windows) |
| Download ZIP | Download the ZIP again and build it again |

Stop the old server first: two servers cannot use the same port.

**Remove:** delete the folder. The game writes nothing outside it. Your browser remembers the name
you typed; clearing that site's data removes it. (Visual Studio, if you installed it only for this,
is removed from Windows Settings → Apps.)

---

## 9. If something goes wrong

| What you see | What it means and what to do |
| --- | --- |
| Browser: "This site can't be reached" at `localhost:3000` | The server is not running. Start it and read what it prints. |
| `could not listen on ... port 3000` | Another program, or a second copy of Polyrift, is using the port. Close the other copy, or start with `--port 8080`. |
| `warning: public/index.html not found`, and the browser shows "404 Not Found" | The `public` folder is not next to the program. Keep the program and `public` in the same folder. |
| The black window flashes and closes (Windows) | Open `cmd` in that folder (see step 7) and run `polyrift.exe` there so the message stays on screen. |
| "Windows protected your PC" | Click **More info → Run anyway**. |
| Antivirus warns about `polyrift.exe` | Unsigned programs are sometimes flagged. If you prefer not to trust a ready-made file, build it yourself (Way B). |
| macOS: "cannot be opened because the developer cannot be verified" | Run `xattr -dr com.apple.quarantine .` in the folder, or use Way B. |
| Linux: `Permission denied` | Run `chmod +x polyrift`, then `./polyrift`. |
| Linux: `GLIBC_... not found` | The ready-made file is too new for your system. Use Way B. |
| `cmake` "is not recognized" (Windows) | You are in an ordinary PowerShell. Use **Developer PowerShell for VS**, and check that the C++ workload was installed. |
| `make: command not found` / `git: command not found` | The tools from the "install once" line are missing. Run that line. |
| `cmake` says it cannot find `CMakeLists.txt` | You are in the wrong folder. Go into the folder where `dir` or `ls` shows `README.md`, `src` and `public`. |
| Menu shows "Connection lost. Reconnecting…" | The server stopped or restarted. The page reconnects by itself when it is back. |
| "This arena is full" | That mode has reached its player limit (40 by default). Pick another mode. |
| A seventh tab or device from the same network cannot join | One address may hold 6 connections by default. Start the server with `--max-per-ip 0`. |
| Friends on the same Wi-Fi cannot open the address | See the firewall notes in step 6. Check they typed `http://` (not `https://`) and the `:3000`. |
| Movement feels delayed for someone far away | Expected: the server confirms every move, so distance adds delay. |
| "This page is out of date. Please refresh." | The server was updated while the tab was open. Refresh the page. |

Still stuck? On the repository page open **Issues → New issue** and include your operating system
and the exact text the server window printed.

---

## 10. Common questions

**Do I need a GitHub account?** Not to download from a public repository. You need one only to report an issue.

**Does it cost anything?** No.

**Do I need the internet to play?** Only to download (and to install the compiler for Way B). After that it runs on your computer with no connection.

**What is the "Developer code" field on the start screen?** Leave it empty. It is for the person running the server, who can use it to try any tank.

**Is my progress saved?** No. Score and level last until you leave or the server stops.

**Which browser?** Any current browser should work. It has been tested in Chrome.

**Can a phone or tablet run the server?** No. They can only join a server running on a computer.

**Can I play without running a server at all?** Only if someone else runs one and gives you its address.

**Which systems has this actually been tried on?** See the table in the [README](../README.md#what-has-actually-been-tested).

**Can I change the game?** Yes, with Way B. See [DEVELOPING.md](DEVELOPING.md).
