# Hosting Polyrift

The game is already multiplayer: one server program, any number of browsers. What changes between
"me and a friend" and "a public game" is only where that program runs and what sits in front of it.

## 1. Playing with friends

**Same Wi-Fi / LAN.** Start the server and give people `http://YOUR-LOCAL-IP:3000`
(find the IP with `ipconfig` on Windows, `ipconfig getifaddr en0` on macOS, `hostname -I` on Linux).
The first time, Windows asks whether to allow the program through the firewall: allow it on private networks.

**Over the internet from your own PC.** Possible (forward port 3000 on your router to your PC and
share your public IP), but it exposes your home address and stops when your PC sleeps. A rented
server is the better route for anything beyond a quick test.

## 2. A public server

GitHub cannot run the game: GitHub Pages and similar static hosts only serve files, and this game
needs a program that stays running and holds WebSocket connections open. You need either

- a small **Linux virtual server (VPS)** from any provider, or
- a **container host** that runs a Dockerfile and allows long-lived WebSocket connections.

The steps below are the VPS route on Ubuntu or Debian. Replace `yourgame.example` with a domain you own.

### Install and build

```
sudo apt update && sudo apt install -y build-essential git
sudo useradd --system --create-home --home-dir /opt/polyrift polyrift
sudo -u polyrift git clone https://github.com/YOUR-NAME/polyrift.git /opt/polyrift/app
cd /opt/polyrift/app && sudo -u polyrift make
```

(A private repository needs a deploy key or token for `git clone`; a public one does not.)

### Keep it running

Create `/etc/systemd/system/polyrift.service`:

```
[Unit]
Description=Polyrift game server
After=network.target

[Service]
User=polyrift
WorkingDirectory=/opt/polyrift/app
ExecStart=/opt/polyrift/app/polyrift --port 3000 --bind 127.0.0.1 --trust-proxy
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
```

```
sudo systemctl enable --now polyrift
journalctl -u polyrift -f        # live log
```

`Restart=always` brings the game back after a crash or reboot. `--bind 127.0.0.1` means only the
proxy below can reach the game directly.

### HTTPS and your domain

Point the domain's DNS A record at the server, then put a reverse proxy in front. Caddy is the least
work because it obtains and renews the HTTPS certificate by itself and forwards WebSockets without
extra settings. Install Caddy, then `/etc/caddy/Caddyfile` is just:

```
yourgame.example {
    reverse_proxy 127.0.0.1:3000
}
```

`sudo systemctl reload caddy`, open ports 80 and 443 in the firewall, and the game is live at
`https://yourgame.example`. The client switches to secure WebSockets (`wss://`) automatically on an
HTTPS page.

If you use nginx instead, the proxy block must pass the upgrade headers:

```
location / {
    proxy_pass http://127.0.0.1:3000;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_read_timeout 300s;
}
```

**About `--trust-proxy`.** Behind a proxy every connection appears to come from 127.0.0.1, so the
per-address limit would lump all players together. `--trust-proxy` makes the server read the real
address from the `X-Forwarded-For` header the proxy adds. Only use it together with
`--bind 127.0.0.1` (or a firewall that blocks port 3000), otherwise anyone connecting directly could
fake that header. If you add a CDN in front of your proxy as well, the address the server sees is
the CDN's unless you configure the proxy to pass the visitor's address through; until then run with
`--max-per-ip 0`.

### Updating the live game

```
cd /opt/polyrift/app
sudo -u polyrift git pull
sudo -u polyrift make
sudo systemctl restart polyrift
```

On restart the server tells everyone it is restarting, players' browsers reconnect by themselves
within a couple of seconds, and if the protocol version changed they reload the new client.
Everyone's tank and score are lost on restart (nothing is saved to disk), so update at a quiet time.

### Docker instead

```
docker build -t polyrift .
docker run -d --restart=always -p 3000:3000 polyrift
```

The Dockerfile is included but has not been built or run yet; treat the first build as its test.
The server also honours a `PORT` environment variable, which many container hosts set.

## 3. What it costs to run

Measured with bots on the development machine; treat as estimates.

- **CPU:** about 0.03 to 0.08 ms per tick per arena with ~15 tanks, i.e. well under 1% of one core
  for all six arenas. Cost grows with players and projectiles. The server is single-threaded.
- **Memory:** a few megabytes.
- **Bandwidth is the real cost:** roughly 25 to 50 KB/s sent to each connected player now that
  everyone can reach level 45 quickly (more bullets on screen and a wider view). At 35 KB/s,
  one player connected around the clock is about 3 GB a day; 50 players at once is on the order of
  4.5 TB a month. Check your host's traffic allowance and overage price before you grow.
  Ways to cut it later: send positions as 16-bit integers instead of floats, send snapshots 20 times
  a second instead of 30, or send only what changed since the last snapshot.
- **Connections:** 600 per process by default (`maxConns` in `net.hpp`), with a hard ceiling near
  1000 because the network loop uses `select()`. Beyond that, either replace the loop with
  epoll/kqueue or run several server processes (next section).

## 4. Growing beyond one server

Each server process is independent and shares nothing. To add capacity or regions, run more copies
(different machines, or different ports behind different subdomains such as `eu.yourgame.example`
and `us.yourgame.example`) and add a server picker to the menu in `public/`. `GET /status` on each
server returns its player counts as JSON, which is what a server list would read.

Players far from the server will feel lag because there is no client-side prediction yet; a server
per region helps more than anything else.

## 5. Abuse protection

Built in:

| Protection | Default | Where |
| --- | --- | --- |
| Connections per address | 6 (`--max-per-ip`) | `net.cpp` |
| Total connections | 600 | `net.hpp` |
| Players per arena | 40 (`--max-players`) | `room.cpp` |
| Message flood | connection dropped above 200 messages/second | `net.cpp` |
| Silent connections | dropped after 45 s without a message (the client pings every 10 s) | `net.cpp` |
| Slow HTTP requests | dropped after 10 s | `net.cpp` |
| Oversized messages | connection dropped above 64 KB | `net.cpp` |
| Input validation | every message bounds-checked; names cleaned to valid text, 18 characters | `room.cpp`, `util.hpp` |
| Path traversal | only plain file names inside `public/` are served | `net.cpp` |
| Server authority | movement, shooting, score, upgrades all decided by the server | everywhere |

Not built, and you will want them once strangers arrive:

- **Aim bots and scripts.** The client only gets what is inside its field of view, which prevents
  map hacks, but nothing detects automated aiming or movement.
- **Offensive names and chat.** There is no word filter. Players can chat (one line of up to 80
  characters, Enter in the game). The server strips control and invisible characters, allows three
  lines in a row and then one every two seconds, and refuses the same line twice within ten
  seconds; a player can hide chat for themselves with X. That stops flooding, not abuse: there is
  no word filter, no mute or report button and no log. On a public server, start with `--no-chat`
  unless you are ready to moderate.
- **Bans.** No way yet to block an address or kick a player; you would add that to `main.cpp`.
- **Multi-tab teaming** beyond the per-address limit.
- **Large floods (DDoS).** The limits above stop casual abuse, not a real attack. That is handled
  by your hosting provider's protection or a proxy service in front of the server.

## 6. Before you open it to the public

- **Change the developer code.** The default `1234` is written in the public source code, so anyone
  could become an invulnerable Arena Closer on your server. Start the public server with
  `--dev-code` followed by a long private code (in the systemd file or Docker command, not in the
  repository), or with `--dev-code off`. Anyone who can read that start command can read the code.
- **Decide about chat.** It is on by default. `--no-chat` turns it off; see the note on offensive
  names and chat above.
- **Watch the first GitHub build** and fix anything red on Windows or macOS.
- **Play it with real people** on a real connection before announcing it. Balance so far comes from
  bots only, and lag has only been felt on localhost.
- **Name and identity.** Launch under your own name, artwork and domain. Check that the name is not
  already used by another game or registered as a trademark ("Polyrift" has not been checked).
  Do not use another game's name, logo or assets.
- **Licence.** There is no LICENSE file, which means nobody else is allowed to copy or reuse the code
  even if the repository is public. That is a fine default. If you want contributors or want others
  to host their own servers, pick a licence deliberately; it is hard to take back.
- **Privacy.** The server prints player IP addresses to its log. A public site should say what it
  logs and why, and rules get stricter if you add analytics, ads or accounts, or if much of your
  audience is children. Laws differ by country and this is not legal advice; check what applies to you.
- **Backups.** Nothing is stored on the server, so there is nothing to back up except the code,
  which GitHub already holds.
- **Monitoring.** Point any uptime checker at `https://yourgame.example/status`.
- **Unsigned downloads.** The Windows and macOS builds from the Releases page are not code-signed,
  so Windows SmartScreen and macOS Gatekeeper will warn people who download them. That only matters
  for people running their own server; players in a browser never see it.

## 7. What a mature game of this kind has that this one does not yet

Roughly in the order players would notice: client-side prediction for your own tank, smaller
packets, more classes and polygons, sound, a server/region picker, polished touch controls, bans and
a name filter, saved accounts or progress, and anti-cheat. None of these needs a rewrite; the
structure in DEVELOPING.md is where each would go.
