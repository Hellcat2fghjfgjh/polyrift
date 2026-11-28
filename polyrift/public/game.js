// Polyrift browser client: connects to the C++ server over a WebSocket, draws what the
// server says is visible, and sends back keyboard/mouse input. No game rules live here.
(() => {
'use strict';

const $ = id => document.getElementById(id);
const canvas = $('game');
const ctx = canvas.getContext('2d');
const TAU = Math.PI * 2;

// Colour indices match enum Color in src/defs.hpp.
const COLORS = ['#4aa3df', '#5fc66a', '#e5534b', '#b678dd', '#f2c94c', '#f28f5c', '#7b8cf0', '#8d949e', '#f4f1e8', '#d9a441',
  '#e86fb0', '#f5d33c', '#3fbfae',  // 10 = elite bosses and crashers, 11 = arena closers, 12 = Kronos
  '#4bb5c4', '#8ee66b', '#6fe8ee', '#3d4149',  // 13 = hexagons, 14 = shiny, 15 = legendary, 16 = shadow
  '#ff6b6b', '#f5a9b8', '#b9c4ea', '#5b616b'];  // 17 = rainbow, 18 = trans, 19 = polyhedra (all drawn specially), 20 = rogue bosses
const C_SHINY = 14, C_LEGENDARY = 15, C_SHADOW = 16, C_RAINBOW = 17, C_TRANS = 18, C_POLY = 19;
const SKILL_COLORS = ['#b678dd', '#e5749b', '#f2c94c', '#f28f5c', '#e5534b', '#e0764e', '#5fc66a', '#4fc3c0', '#4aa3df', '#7b8cf0'];
const GUN = '#a7adb8', FLOOR = '#dde1e6', GRID = '#c9ced6', VOID = '#b4bac3', DARK = '#4a4f57', INK = '#2f343b';
const OUT = 3.4;  // outline width in world units
const PROTO = 9;  // must equal PROTOCOL_VERSION in src/room.hpp
const MODE_INFO = {
  ffa: 'Every tank for itself on an open map.',
  maze: 'Free-for-all inside a random maze. Walls stop bullets.',
  '2tdm': 'Blue against Green. Your base burns intruders.',
  '4tdm': 'Four teams, one base in each corner.',
  dom: 'Two teams fight over four dominators. Hold all four to win the game.',
  rush: 'Everyone on one team. Defend the sanctuaries through 20 waves of bosses.',
  mazedom: 'Domination inside a maze: two teams, four dominators, walls everywhere.',
  endless: 'Boss Rush without a last wave. The bosses grow stronger every 20 waves: how far can you get?',
  mother: 'Two teams, one Mothership each. Protect yours (H lets one player drive it) and destroy theirs.',
  siege: 'Blue attacks, Green defends. Take both outer dominators, then the Citadel, before time runs out.',
  ctf: 'Two teams. Take the enemy flag and carry it to your own stand. First to 3 captures wins.',
  tag: 'Four teams. A tank that is destroyed joins the team that destroyed it. Tag everyone to win.',
  br: 'No respawns. The safe circle keeps shrinking. The last tank alive wins.',
  koth: 'Two teams. Hold the hill in the middle alone to score. First to 150 points wins.',
  sandbox: 'Free levels, any class (T), and a panel to spawn shapes and bosses. Nothing is at stake.',
};

// ------------------------------------------------------------------ state
let ws = null, mode = '1', world = null, defs = [], miniCanvas = null;  // mode = the slot we are connected to
let state = 'menu';            // menu | play | dead
let pendingSpawn = false;
const ents = new Map();
let updSeq = 0, lastUpd = 0, updGap = 34;
const cam = { x: 0, y: 0, px: 0, py: 0, rx: 0, ry: 0, t0: 0, fov: 2000, rfov: 2000, snap: true };
const me = { alive: false, id: 0, score: 0, level: 1, prog: 0, pts: 0, skills: new Array(10).fill(0), def: 0, ups: [], name: '',
  kills: 0, assists: 0, bossKills: 0, polys: 0 };
let board = { lb: [], mm: [], doms: [], n: 0 };
const messages = [];
const keys = Object.create(null);
const mouse = { x: 0, y: 0, down: false, alt: false };
const touch = { used: false, move: null, aim: null, angle: 0 };  // on-screen thumb sticks
let myColor = 0;
let autofire = false, autospin = false, spinAngle = 0;
let override = false;          // auto-turrets and drones only act on the player's command
let autoLevel = false;         // touch stand-in for holding the level-up key
let banner = null;             // big centred announcement: { text, sub, color, t }
let isDev = false;             // the server accepted our developer code on this connection
let devAsked = false;          // the code has been sent on this connection
let devTree = false;           // developers toggle the class picker with T instead of holding it
let uiHits = [];               // clickable HUD regions rebuilt every frame
let reverseTank = false;       // the body faces away from the cursor
let reverseMouse = false;      // left and right mouse buttons swap jobs
let killBar = true;            // kills / assists / boss kills beside the score bar
let showLatency = false, ping = -1, pingSent = 0, pingSeq = 0;
let destructAt = 0;            // when the self-destruct key went down (0 = not held)
let chatOn = true;             // show other players' chat lines (and allow sending)
let chatOpen = false;          // the chat box is open: typing goes there, not to the tank
let touchAlt = false;          // touch stand-in for holding right-click / Shift
const chatLog = [];            // recent lines: { name, text, color, t }
const bubbles = new Map();     // entity id -> { text, t }: the line shown above a tank
let recorder = null;           // video recording in progress
let zoneR = -1;                // Battle Royale: the safe circle as drawn (eased towards what the server last sent)
// Sandbox: what the panel is set to spawn, and whether the panel is open on a small screen
const sand = { shape: 3, size: 0, mut: 0, boss: 0, shapes: true, open: false };

// ------------------------------------------------------------------ key bindings
// Every action key can be changed on the start screen ("Controls"); the choice is kept in the browser.
const DEFAULT_BINDS = {
  autofire: 'KeyE', autospin: 'KeyC', override: 'KeyR', levelup: 'KeyN', tree: 'KeyT', dominator: 'KeyH',
  reverseTank: 'KeyV', reverseMouse: 'KeyB', selfDestruct: 'KeyO', maxStat: 'KeyM', latency: 'KeyL', killBar: 'KeyP',
  screenshot: 'KeyQ', record: 'KeyZ', chatToggle: 'KeyX', sandShape: 'KeyF', sandBoss: 'KeyG', up1: 'KeyY', up2: 'KeyU', up3: 'KeyI', up4: 'KeyH', up5: 'KeyJ', up6: 'KeyK',
};
const BIND_NAMES = {
  autofire: 'Autofire', autospin: 'Autospin', override: 'Override', levelup: 'Level up (hold)', tree: 'Class tree (hold)',
  dominator: 'Dominator / Mothership', reverseTank: 'Reverse tank', reverseMouse: 'Reverse mouse', selfDestruct: 'Self-destruct (hold, at spawn)',
  maxStat: 'Max a stat (hold + number)', latency: 'Show latency', killBar: 'Kill bar', screenshot: 'Screenshot', record: 'Record video', chatToggle: 'Chat on / off',
  sandShape: 'Sandbox: spawn shape', sandBoss: 'Sandbox: spawn boss',
  up1: 'Upgrade 1', up2: 'Upgrade 2', up3: 'Upgrade 3', up4: 'Upgrade 4', up5: 'Upgrade 5', up6: 'Upgrade 6',
};
const UP_ACTIONS = ['up1', 'up2', 'up3', 'up4', 'up5', 'up6'];
const binds = Object.assign({}, DEFAULT_BINDS);
try {
  const saved = JSON.parse(localStorage.getItem('polyrift-keys') || '{}');
  for (const k in DEFAULT_BINDS) if (typeof saved[k] === 'string') binds[k] = saved[k];
  killBar = localStorage.getItem('polyrift-killbar') !== '0';
  chatOn = localStorage.getItem('polyrift-chat') !== '0';
} catch (e) { /* storage unavailable or damaged: defaults */ }
const held = action => !!keys[binds[action]];
const KEY_LABELS = { ShiftLeft: 'L Shift', ShiftRight: 'R Shift', ControlLeft: 'L Ctrl', ControlRight: 'R Ctrl', AltLeft: 'L Alt',
  AltRight: 'R Alt', Backquote: '`', Minus: '-', Equal: '=', BracketLeft: '[', BracketRight: ']', Semicolon: ';', Quote: "'",
  Comma: ',', Period: '.', Slash: '/', Backslash: '\\' };
function keyLabel(code) {
  if (!code) return '—';
  return KEY_LABELS[code] || code.replace(/^Key/, '').replace(/^Digit/, '').replace(/^Numpad/, 'Num ').replace(/^Arrow/, '');
}
let cw = 0, ch = 0, dpr = 1, viewScale = 1;
let fps = 60, lastFrame = 0;
const decoder = new TextDecoder();

// ------------------------------------------------------------------ colour helpers
const shadeCache = new Map();
function hexToRgb(h) { return [parseInt(h.slice(1, 3), 16), parseInt(h.slice(3, 5), 16), parseInt(h.slice(5, 7), 16)]; }
function shade(hex, f = 0.72) {
  const key = hex + f;
  let v = shadeCache.get(key);
  if (!v) {
    const [r, g, b] = hexToRgb(hex);
    v = `rgb(${Math.round(r * f)},${Math.round(g * f)},${Math.round(b * f)})`;
    shadeCache.set(key, v);
  }
  return v;
}
function lighten(hex, t) {
  const key = hex + 'L' + t;
  let v = shadeCache.get(key);
  if (!v) {
    const [r, g, b] = hexToRgb(hex);
    v = `rgb(${Math.round(r + (255 - r) * t)},${Math.round(g + (255 - g) * t)},${Math.round(b + (255 - b) * t)})`;
    shadeCache.set(key, v);
  }
  return v;
}
function hueHex(h) {
  h = ((Math.round(h / 10) * 10) % 360 + 360) % 360;
  const f = n => {
    const k = (n + h / 30) % 12, c = 0.62 - 0.36 * Math.max(-1, Math.min(k - 3, 9 - k, 1));
    return Math.round(c * 255).toString(16).padStart(2, '0');
  };
  return `#${f(0)}${f(8)}${f(4)}`;
}
function fmt(n) {
  if (n >= 1e6) return (n / 1e6).toFixed(2) + 'm';
  if (n >= 1e4) return (n / 1e3).toFixed(1) + 'k';
  return Math.round(n).toLocaleString('en-US');
}
function angDiff(a, b) {
  let d = (b - a) % TAU;
  if (d > Math.PI) d -= TAU;
  if (d < -Math.PI) d += TAU;
  return d;
}

// ------------------------------------------------------------------ networking
function connect(m) {
  mode = m;
  if (ws) { ws.onclose = null; ws.onmessage = null; ws.close(); }
  ents.clear();
  world = null;
  me.alive = false;
  isDev = devAsked = devTree = false;
  setDevStatus('', '');
  cam.snap = true;
  board = { lb: [], mm: [], doms: [], n: 0 };
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  const sock = new WebSocket(`${proto}://${location.host}/ws/${m}`);
  sock.binaryType = 'arraybuffer';
  sock.onopen = () => setStatus('');
  sock.onmessage = ev => {
    if (typeof ev.data === 'string') onJson(JSON.parse(ev.data));
    else onUpdate(ev.data);
  };
  sock.onclose = () => {
    if (ws !== sock) return;
    ws = null;
    world = null;
    showMenu();
    setStatus('Connection lost. Reconnecting…');
    setTimeout(() => { if (!ws) connect(mode); }, 1500);
  };
  ws = sock;
}

function isOpen() { return ws && ws.readyState === 1; }

function sendSpawn() {
  if (!isOpen() || !world) { pendingSpawn = true; return; }
  pendingSpawn = false;
  sendDevCode();  // ask for developer mode first, so the spawn that follows already uses it
  const bytes = new TextEncoder().encode(me.name);
  const b = new Uint8Array(1 + bytes.length);
  b[0] = 0;
  b.set(bytes, 1);
  ws.send(b);
}
// Send the developer code typed on the start screen (once per connection, or again after it was edited).
function sendDevCode() {
  const code = $('code').value;
  if (!code || devAsked || isDev || !isOpen()) return false;
  devAsked = true;
  const cb = new TextEncoder().encode(code);
  const c = new Uint8Array(1 + cb.length);
  c[0] = 5;
  c.set(cb, 1);
  ws.send(c);
  return true;
}
function setDevStatus(text, cls) {
  $('devStatus').textContent = text;
  $('devStatus').className = cls;
}
function sendSkill(i) { if (isOpen() && me.alive && me.pts > 0) ws.send(new Uint8Array([2, i])); }
// Sandbox command (message 10): cmd, two small arguments and a point relative to our tank.
// atCursor: use where the mouse points; otherwise somewhere just beside the tank.
function sendSand(cmd, a = 0, b = 0, atCursor = false) {
  if (!isOpen() || !me.alive || !world || world.mode !== 'sandbox') return;
  let dx, dy;
  if (atCursor && !touch.used) {
    const own = ents.get(me.id);
    dx = (mouse.x - cw / 2) / viewScale + (own ? cam.rx - own.rx : 0);
    dy = (mouse.y - ch / 2) / viewScale + (own ? cam.ry - own.ry : 0);
  } else {
    const ang = Math.random() * TAU;
    dx = Math.cos(ang) * 280;
    dy = Math.sin(ang) * 280;
  }
  const dv = new DataView(new ArrayBuffer(12));
  dv.setUint8(0, 10);
  dv.setUint8(1, cmd);
  dv.setUint8(2, a);
  dv.setUint8(3, b);
  dv.setFloat32(4, dx, true);
  dv.setFloat32(8, dy, true);
  ws.send(dv.buffer);
}
// The polygon the Sandbox panel is set to: size in bits 0-1, crasher in bit 2, mutation in bits 3-5.
// (The last entry of the "kind" row is the crasher; the others are the mutations.)
function sandShape(atCursor) {
  const muts = (world.sandbox && world.sandbox.muts) || ['Plain'];
  const crasher = sand.mut >= muts.length;
  sendSand(1, sand.shape, crasher ? 4 | (sand.size & 1) : sand.size | (sand.mut << 3), atCursor);
}
function sandBoss(atCursor) { sendSand(2, sand.boss, 0, atCursor); }
const freePick = () => isDev || (world && world.mode === 'sandbox');  // may click any class in the tree
function sendUpgrade(def) { if (isOpen() && me.alive) ws.send(new Uint8Array([3, def & 255, def >> 8])); }

function sendInput() {
  if (!isOpen() || state !== 'play' || !me.alive) return;
  const typing = document.activeElement === $('name');
  let flags = 0;
  if (!typing) {
    if (keys.KeyW || keys.ArrowUp) flags |= 1;
    if (keys.KeyS || keys.ArrowDown) flags |= 2;
    if (keys.KeyA || keys.ArrowLeft) flags |= 4;
    if (keys.KeyD || keys.ArrowRight) flags |= 8;
    if (mouse.down || keys.Space || autofire) flags |= 16;
    if (mouse.alt || keys.ShiftLeft || keys.ShiftRight || touchAlt) flags |= 32;
    if (held('levelup')) flags |= 64;
  }
  if (override) flags |= 128;
  if (autoLevel) {
    if (world && me.level < world.freeLevel) flags |= 64;
    else autoLevel = false;
  }
  // Aim is measured from our own tank, which is not always the middle of the screen (zoom moves the view).
  const own = ents.get(me.id);
  const ox = own ? cam.rx - own.rx : 0, oy = own ? cam.ry - own.ry : 0;
  let dx = (mouse.x - cw / 2) / viewScale + ox, dy = (mouse.y - ch / 2) / viewScale + oy;
  if (touch.used) {
    const m = touch.move;
    if (m) {  // left stick: eight-way movement
      const mx = m.x - m.sx, my = m.y - m.sy, len = Math.hypot(mx, my);
      if (len > 12) {
        if (my < -0.38 * len) flags |= 1;
        if (my > 0.38 * len) flags |= 2;
        if (mx < -0.38 * len) flags |= 4;
        if (mx > 0.38 * len) flags |= 8;
      }
    }
    const a = touch.aim;
    if (a && Math.hypot(a.x - a.sx, a.y - a.sy) > 8) {  // right stick: aim, and fire while held
      touch.angle = Math.atan2(a.y - a.sy, a.x - a.sx);
      flags |= 16;
    }
    dx = Math.cos(touch.angle) * 300;
    dy = Math.sin(touch.angle) * 300;
  }
  if (autospin) {
    spinAngle += 0.11;
    dx = Math.cos(spinAngle) * 300;
    dy = Math.sin(spinAngle) * 300;
  }
  const dv = new DataView(new ArrayBuffer(11));
  dv.setUint8(0, 1);
  dv.setUint8(1, flags);
  dv.setFloat32(2, dx, true);
  dv.setFloat32(6, dy, true);
  dv.setUint8(10, reverseTank ? 1 : 0);  // second flag byte
  ws.send(dv.buffer);
}
setInterval(sendInput, 33);
setInterval(() => { if (isOpen()) ws.send(new Uint8Array([4])); }, 10000);  // keep-alive while idle on the menu
setInterval(() => {  // latency probe: the server sends the number straight back
  if (!showLatency || !isOpen()) return;
  const dv = new DataView(new ArrayBuffer(5));
  dv.setUint8(0, 8);
  dv.setUint32(1, ++pingSeq, true);
  pingSent = performance.now();
  ws.send(dv.buffer);
}, 2000);

function onJson(m) {
  if (m.t === 'init') {
    if (m.proto !== PROTO) {  // the server was updated while this tab was open: fetch the matching client
      let last = 0;
      try { last = Number(sessionStorage.getItem('polyrift-reload')) || 0; } catch (e) { /* ignore */ }
      if (Date.now() - last > 15000) {
        try { sessionStorage.setItem('polyrift-reload', String(Date.now())); } catch (e) { /* ignore */ }
        location.reload();
        return;
      }
      setStatus('This page is out of date. Please refresh.');
    }
    world = m;
    defs = m.defs;
    board = { lb: [], mm: [], doms: [], n: 0 };
    zoneR = -1;
    sand.shapes = true;
    buildMinimap();
    refreshSlots();  // the slot may have switched mode
    if (state !== 'play') setStatus('');
    $('modeInfo').textContent = MODE_INFO[m.mode] || '';
    if (pendingSpawn) sendSpawn();
  } else if (m.t === 'lb') {
    board = m;
  } else if (m.t === 'msg') {
    pushMessage(m.text);
  } else if (m.t === 'chat') {
    if (!chatOn) return;
    const text = String(m.text).slice(0, 200), now = performance.now();
    chatLog.push({ name: String(m.name), text, color: m.color, t: now });
    if (chatLog.length > 6) chatLog.shift();
    bubbles.set(m.id, { text, t: now });
  } else if (m.t === 'banner') {
    banner = { text: m.text, sub: m.sub, color: m.color, t: performance.now() };
  } else if (m.t === 'pong') {
    if (m.v === pingSeq) ping = Math.round(performance.now() - pingSent);
  } else if (m.t === 'dev') {
    isDev = !!m.ok;
    pushMessage(isDev ? 'Developer mode on. Press T and click any class.' : 'Developer code not accepted. Playing normally.');
    setDevStatus(isDev ? 'Unlocked. Press Play, then press T in the game and click any class.'
      : 'Code not accepted. (After several wrong tries the server stops accepting codes for a while.)', isDev ? 'ok' : 'bad');
  } else if (m.t === 'closed') {
    showMenu();
    setStatus('The arena is closed. A new game starts in a few seconds.');
  } else if (m.t === 'nospawn') {
    showMenu();
    setStatus(m.why || 'Your team has no sanctuary to spawn at. You can join again when one is restored.');
  } else if (m.t === 'full') {
    showMenu();
    setStatus('This arena is full. Pick another mode or try again in a moment.');
  } else if (m.t === 'dead') {
    state = 'dead';
    mouse.down = mouse.alt = touchAlt = false;
    closeChat();
    let by = m.killer ? `Destroyed by ${m.killer}${m.killerTank ? ' (' + m.killerTank + ')' : ''}` : 'Destroyed';
    if (m.helpers && m.helpers.length) by += `, with help from ${m.helpers.join(', ')}`;
    $('deathBy').textContent = by;
    $('deathNote').textContent = m.note || '';
    $('deathStats').innerHTML = '';
    destructAt = 0;
    const mins = Math.floor(m.time / 60), secs = String(m.time % 60).padStart(2, '0');
    // The run in numbers: three wide tiles, then what was destroyed along the way.
    const tiles = [['Score', fmt(m.score), 'wide'], ['Level', m.level, 'wide'], ['Alive', `${mins}:${secs}`, 'wide'],
      ['Kills', m.kills || 0], ['Assists', m.assists || 0], ['Bosses', m.bosses || 0], ['Polygons', fmt(m.polys || 0)]];
    for (const [label, value, cls] of tiles) {
      const d = document.createElement('div');
      if (cls) d.className = cls;
      d.textContent = value;
      const s = document.createElement('span');
      s.textContent = label;
      d.appendChild(s);
      $('deathStats').appendChild(d);
    }
    $('deathTitle').textContent = `Your ${m.tank} was destroyed`;
    setTimeout(() => { if (state === 'dead') $('death').hidden = false; }, 700);
  }
}

// Binary world update. Layout mirrors Room::sendUpdates in src/proto.cpp.
function onUpdate(buf) {
  const dv = new DataView(buf);
  let o = 0;
  const u8 = () => dv.getUint8(o++);
  const u16 = () => { const v = dv.getUint16(o, true); o += 2; return v; };
  const u32 = () => { const v = dv.getUint32(o, true); o += 4; return v; };
  const f32 = () => { const v = dv.getFloat32(o, true); o += 4; return v; };
  if (u8() !== 1) return;
  u32();  // server tick
  const alive = u8() === 1;
  const cx = f32(), cy = f32(), fov = f32();
  me.id = u32();
  me.score = u32();
  me.level = u8();
  me.prog = f32();
  me.pts = u8();
  for (let i = 0; i < 10; i++) me.skills[i] = u8();
  me.def = u16();
  const nUp = u8();
  me.ups.length = 0;
  for (let i = 0; i < nUp; i++) me.ups.push(u16());
  me.kills = u16(); me.assists = u16(); me.bossKills = u16(); me.polys = u16();

  const now = performance.now();
  updSeq++;
  const n = u16();
  for (let i = 0; i < n; i++) {
    const id = u32(), kind = u8(), shape = u8(), color = u8(), flags = u8();
    const x = f32(), y = f32(), a = f32(), size = f32();
    const hp = u8() / 255, sh = u8() / 255;
    let e = ents.get(id);
    if (!e) {
      e = { id, kind, x, y, a, size, px: x, py: y, pa: a, ps: size, rx: x, ry: y, ra: a, rs: size, born: now, dying: 0 };
      ents.set(id, e);
    } else {
      e.px = e.rx; e.py = e.ry; e.pa = e.ra; e.ps = e.rs;
      e.x = x; e.y = y; e.a = a; e.size = size;
      e.dying = 0;
    }
    e.t0 = now;
    e.shape = shape; e.color = color; e.flags = flags; e.hp = hp; e.sh = sh; e.seq = updSeq;
    if (kind === 0) {
      e.def = u16();
      e.score = u32();
      e.level = u8();
      e.alpha = u8() / 255;
      const nt = defs[e.def] ? defs[e.def].tur.length : 0;
      if (!e.tur || e.tur.length !== nt) e.tur = new Array(nt);
      for (let k = 0; k < nt; k++) e.tur[k] = (u8() / 256) * TAU;
      const len = u8();
      e.name = decoder.decode(new Uint8Array(buf, o, len));
      o += len;
    }
  }
  const hidden = document.hidden;
  for (const e of ents.values()) {
    if (e.seq === updSeq) continue;
    if (hidden) ents.delete(e.id);
    else if (!e.dying) e.dying = now;
  }

  if (cam.snap) { cam.rx = cam.px = cx; cam.ry = cam.py = cy; cam.rfov = fov; cam.snap = false; }
  cam.px = cam.rx; cam.py = cam.ry;
  cam.x = cx; cam.y = cy; cam.fov = fov; cam.t0 = now;
  if (lastUpd) updGap += (Math.min(100, Math.max(10, now - lastUpd)) - updGap) * 0.1;
  lastUpd = now;

  if (alive && !me.alive) {  // (re)spawned
    state = 'play';
    $('menu').hidden = true;
    $('death').hidden = true;
  }
  me.alive = alive;
}

function pushMessage(text) {
  messages.push({ text, t: performance.now() });
  if (messages.length > 5) messages.shift();
}

// ------------------------------------------------------------------ drawing primitives
function poly(x, y, r, sides, a) {
  ctx.beginPath();
  for (let i = 0; i < sides; i++) {
    const t = a + (i * TAU) / sides;
    const px = x + Math.cos(t) * r, py = y + Math.sin(t) * r;
    if (i) ctx.lineTo(px, py); else ctx.moveTo(px, py);
  }
  ctx.closePath();
}
function star(x, y, r1, r2, points, a) {
  ctx.beginPath();
  for (let i = 0; i < points * 2; i++) {
    const t = a + (i * Math.PI) / points, r = i % 2 ? r2 : r1;
    const px = x + Math.cos(t) * r, py = y + Math.sin(t) * r;
    if (i) ctx.lineTo(px, py); else ctx.moveTo(px, py);
  }
  ctx.closePath();
}
function rrect(x, y, w, h, r) {
  ctx.beginPath();
  if (ctx.roundRect) ctx.roundRect(x, y, w, h, r);
  else ctx.rect(x, y, w, h);
}
function fillStroke(fill, lw = OUT) {
  ctx.fillStyle = fill;
  ctx.fill();
  ctx.lineWidth = lw;
  ctx.strokeStyle = shade(fill);
  ctx.stroke();
}
function label(text, x, y, size, align = 'center', color = '#fff') {
  ctx.font = `800 ${size}px Nunito, Ubuntu, "Segoe UI", system-ui, sans-serif`;
  ctx.textAlign = align;
  ctx.textBaseline = 'middle';
  ctx.lineJoin = 'round';
  ctx.lineWidth = Math.max(2, size / 4.5);
  ctx.strokeStyle = INK;
  ctx.strokeText(text, x, y);
  ctx.fillStyle = color;
  ctx.fillText(text, x, y);
}

function drawGun(g, r, lw) {
  const L = g[1] * r, h0 = (g[2] * r) / 2, h1 = h0 * g[3], oy = g[4] * r, type = g[5];
  ctx.save();
  ctx.rotate(g[0]);
  ctx.beginPath();
  if (type === 1 || type === 3 || type === 6) {  // trap / block / minion launcher: straight barrel with a flared mouth
    const m = L * 0.74, f = h0 * 1.7;
    ctx.moveTo(0, oy - h0); ctx.lineTo(m, oy - h0); ctx.lineTo(L, oy - f);
    ctx.lineTo(L, oy + f); ctx.lineTo(m, oy + h0); ctx.lineTo(0, oy + h0);
  } else {
    ctx.moveTo(0, oy - h0); ctx.lineTo(L, oy - h1); ctx.lineTo(L, oy + h1); ctx.lineTo(0, oy + h0);
  }
  ctx.closePath();
  fillStroke(GUN, lw);
  ctx.restore();
}

// Draws a tank of class `def` centred on the current origin. Used for the world and the upgrade icons.
// turA holds the world angle of each auto-turret; without it they point straight out from their mounts.
// Polygon bodies: class shape code -> [sides, radius factor]. 26-28 are the bosses' hexagon, heptagon and octagon.
const BODY = { 3: [3, 1.5], 4: [4, 1.24], 5: [5, 1.14], 9: [9, 1.05], 11: [11, 1.035], 26: [6, 1.1], 27: [7, 1.075], 28: [8, 1.06] };
function drawCross(x, y, s, lw) {  // the mark of anything that heals
  ctx.beginPath();
  ctx.rect(x - s, y - s * 0.3, s * 2, s * 0.6);
  ctx.rect(x - s * 0.3, y - s, s * 0.6, s * 2);
  ctx.fillStyle = '#e5534b';
  ctx.fill();
  ctx.lineWidth = lw * 0.5;
  ctx.strokeStyle = '#8f2f2a';
  ctx.stroke();
}
const heals = guns => guns.some(g => g[6]);
function drawTank(def, r, angle, fill, now, lw = OUT, turA = null) {
  ctx.lineJoin = 'round';
  const spin = now * 0.0012;
  if (def.shape === 20) { poly(0, 0, r * 1.3, 6, 0); fillStroke(DARK, lw); }
  else if (def.shape === 6) { poly(0, 0, r * 1.22, 6, spin); fillStroke(DARK, lw); }
  else if (def.shape === 7) { poly(0, 0, r * 1.4, 6, spin); fillStroke(DARK, lw); }
  else if (def.shape === 8) { poly(0, 0, r * 1.26, 6, spin); fillStroke(DARK, lw); poly(0, 0, r * 1.26, 6, -spin * 1.6); fillStroke(DARK, lw); }
  else if (def.shape === 12) { star(0, 0, r * 1.36, r * 1.02, 12, spin * 2); fillStroke(DARK, lw); }
  ctx.save();
  ctx.rotate(angle);
  for (const g of def.guns) drawGun(g, r, lw);
  ctx.restore();
  const body = BODY[def.shape];
  if (body) poly(0, 0, r * body[1], body[0], angle + (body[0] === 4 ? Math.PI / 4 : 0));
  else { ctx.beginPath(); ctx.arc(0, 0, r, 0, TAU); }
  fillStroke(fill, lw);
  if (heals(def.guns)) drawCross(0, 0, r * 0.5, lw);
  for (let k = 0; k < def.tur.length; k++) {
    const tu = def.tur[k], ma = angle + tu[0];
    const tx = Math.cos(ma) * tu[1] * r, ty = Math.sin(ma) * tu[1] * r, tr = r * tu[2];
    ctx.save();
    ctx.translate(tx, ty);
    ctx.rotate(turA && turA[k] !== undefined ? turA[k] : ma);
    for (const g of tu[3]) drawGun(g, tr, lw * 0.8);
    ctx.restore();
    ctx.beginPath();
    ctx.arc(tx, ty, tr, 0, TAU);
    fillStroke(GUN, lw * 0.8);
    if (heals(tu[3])) drawCross(tx, ty, tr * 0.55, lw);
  }
}

const LAYER = [4, 2, 1, 3, 0];  // tank, bullet, trap, drone, food -> draw order
const FOOD_R = { 0: 1, 3: 1.42, 4: 1.28, 5: 1.14, 6: 1.1 };

function drawEntity(e, now) {
  let alpha = 1, grow = 1;
  if (e.dying) {
    const k = (now - e.dying) / 140;
    alpha = 1 - k;
    grow = 1 + k * 0.35;
  } else if (now - e.born < 90) {
    alpha = (now - e.born) / 90;
  }
  if (e.flags & 1) alpha *= 0.55 + 0.3 * Math.sin(now * 0.02);
  if (e.kind === 0) alpha *= shownAlpha(e);
  if (alpha <= 0.01) return;
  ctx.globalAlpha = alpha;
  const r = e.rs * grow;
  let fill = COLORS[e.color] || '#999';
  if (e.flags & 4) fill = lighten(fill, 0.45);
  ctx.lineJoin = 'round';
  if (e.kind === 0) {
    const def = defs[e.def];
    if (def) {
      ctx.save();
      ctx.translate(e.rx, e.ry);
      drawTank(def, r, e.ra, fill, now, OUT, e.tur);
      ctx.restore();
      if (e.flags & 16) {  // Siege: a dominator that cannot be hurt wears a ring
        ctx.beginPath();
        ctx.arc(e.rx, e.ry, r * 1.62, 0, TAU);
        ctx.setLineDash([14, 10]);
        ctx.lineDashOffset = -now * 0.02;
        ctx.lineWidth = OUT * 1.4;
        ctx.strokeStyle = lighten(COLORS[e.color] || '#999', 0.55);
        ctx.stroke();
        ctx.setLineDash([]);
      }
    }
  } else if (e.kind === 1) {
    const sub = e.shape && e.shape !== 255 ? world.subs[e.shape - 1] : null;
    if (sub) {  // a missile: draw the guns it carries
      ctx.save();
      ctx.translate(e.rx, e.ry);
      ctx.rotate(e.ra);
      for (const g of sub) drawGun(g, r, OUT * 0.8);
      ctx.restore();
    }
    ctx.beginPath();
    ctx.arc(e.rx, e.ry, r, 0, TAU);
    fillStroke(fill);
    if (e.shape === 255) {  // healing bullet: white cross
      ctx.beginPath();
      ctx.rect(e.rx - r * 0.6, e.ry - r * 0.2, r * 1.2, r * 0.4);
      ctx.rect(e.rx - r * 0.2, e.ry - r * 0.6, r * 0.4, r * 1.2);
      ctx.fillStyle = '#fff';
      ctx.fill();
    }
  } else if (e.kind === 2) {
    const sides = e.shape & 15, sub = e.shape >> 4 ? world.subs[(e.shape >> 4) - 1] : null;
    if (sides === 4) poly(e.rx, e.ry, r * 1.25, 4, (sub ? 0 : e.ra) + Math.PI / 4);
    else star(e.rx, e.ry, r * 1.35, r * 0.6, 3, e.ra);
    fillStroke(fill);
    if (sub) {  // a pillbox: a block with a little turret on top
      ctx.save();
      ctx.translate(e.rx, e.ry);
      ctx.rotate(e.ra);
      for (const g of sub) drawGun(g, r * 0.55, OUT * 0.7);
      ctx.restore();
      ctx.beginPath();
      ctx.arc(e.rx, e.ry, r * 0.55, 0, TAU);
      fillStroke(GUN, OUT * 0.7);
    }
  } else if (e.kind === 3) {
    if (e.shape === 1) {  // minion: a small tank
      ctx.save();
      ctx.translate(e.rx, e.ry);
      ctx.rotate(e.ra);
      drawGun(MINION_GUN, r, OUT * 0.8);
      ctx.restore();
      ctx.beginPath();
      ctx.arc(e.rx, e.ry, r, 0, TAU);
      fillStroke(fill);
    } else {
      if (e.shape === 4) poly(e.rx, e.ry, r * 1.25, 4, e.ra + Math.PI / 4);
      else if (e.shape === 5) poly(e.rx, e.ry, r * 1.18, 5, e.ra);
      else if (e.shape === 6) { ctx.beginPath(); ctx.arc(e.rx, e.ry, r, 0, TAU); }
      else poly(e.rx, e.ry, r * 1.4, 3, e.ra);
      fillStroke(fill);
      if (e.shape === 2) {  // armed drone
        ctx.beginPath();
        ctx.arc(e.rx, e.ry, r * 0.42, 0, TAU);
        fillStroke(GUN, OUT * 0.7);
      }
    }
  } else {
    drawPolygon(e, r, now, fill, alpha);
  }
  ctx.globalAlpha = 1;
}

// A polygon. The shape byte carries its kind (bits 0-2), its size (bits 3-4; the server already
// sent the matching radius) and whether it is a crasher (bit 5); a mutation arrives as its colour.
function drawPolygon(e, r, now, fill, alpha) {
  const type = e.shape & 7, crasher = (e.shape & 32) !== 0, mut = e.color;
  const sides = crasher ? 3 : world.foods[type] ? world.foods[type].s : 4;
  if (mut === C_SHADOW && e.hp > 0.995 && !(e.flags & 4)) {  // shadow polygons drift in and out of sight until something hits them
    const k = Math.sin(now * 0.0011 + e.id * 1.7);
    ctx.globalAlpha = alpha * Math.max(0.04, Math.min(1, 0.5 + k * 0.9));
  }
  if (mut === C_RAINBOW) {
    fill = hueHex(now * 0.09 + e.id * 47);
    if (e.flags & 4) fill = lighten(fill, 0.45);
  }
  if (mut === C_SHINY || mut === C_LEGENDARY || mut === C_RAINBOW || mut === C_TRANS || mut === C_POLY) {  // a soft glow marks a rare find
    const a = ctx.globalAlpha;
    ctx.globalAlpha = a * (0.22 + 0.1 * Math.sin(now * 0.005 + e.id));
    ctx.beginPath();
    ctx.arc(e.rx, e.ry, r * 1.75 + 5, 0, TAU);
    ctx.fillStyle = mut === C_TRANS ? '#ffffff' : lighten(fill, 0.35);
    ctx.fill();
    ctx.globalAlpha = a;
  }
  if (mut === C_POLY) { drawSolid(SOLIDS[SOLID_OF[type] || 'cube'], e.rx, e.ry, r * 1.3, now * 0.0007 + e.id, e.flags & 4); return; }
  const path = () => {
    if (sides === 0) { ctx.beginPath(); ctx.arc(e.rx, e.ry, r, 0, TAU); }
    else poly(e.rx, e.ry, r * (FOOD_R[sides] || 1.15), sides, e.ra);
  };
  path();
  if (mut === C_TRANS) {  // five stripes clipped to the polygon
    const stripes = ['#5bcefa', '#f5a9b8', '#ffffff', '#f5a9b8', '#5bcefa'], R = r * (sides === 3 ? 1.1 : FOOD_R[sides] || 1);
    ctx.save();
    ctx.clip();
    for (let i = 0; i < 5; i++) {
      ctx.fillStyle = e.flags & 4 ? lighten(stripes[i], 0.45) : stripes[i];
      ctx.fillRect(e.rx - R, e.ry - R + (i * 2 * R) / 5, 2 * R, (2 * R) / 5 + 1);
    }
    ctx.restore();
    path();
    ctx.lineWidth = OUT;
    ctx.strokeStyle = shade('#f5a9b8');
    ctx.stroke();
  } else {
    fillStroke(fill);
  }
}

// Platonic solids, the rarest mutation: a real 3D wireframe, turning slowly. Vertices are scaled
// to a unit sphere and the edges are found by joining each vertex to its nearest neighbours.
function makeSolid(verts) {
  const n = Math.hypot(...verts[0]);
  verts = verts.map(v => v.map(c => c / n));
  let min = Infinity;
  const d = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
  for (let i = 0; i < verts.length; i++) for (let j = i + 1; j < verts.length; j++) min = Math.min(min, d(verts[i], verts[j]));
  const edges = [];
  for (let i = 0; i < verts.length; i++) for (let j = i + 1; j < verts.length; j++) if (d(verts[i], verts[j]) < min * 1.01) edges.push([i, j]);
  return { verts, edges };
}
const signs3 = f => { const o = []; for (const a of [-1, 1]) for (const b of [-1, 1]) for (const c of [-1, 1]) o.push(f(a, b, c)); return o; };
const cyc = (a, b, c) => [[a, b, c], [c, a, b], [b, c, a]];
const PHI = (1 + Math.sqrt(5)) / 2;
const uniq = list => [...new Map(list.map(v => [v.map(c => c.toFixed(4)).join(), v])).values()];
const SOLIDS = {
  tetra: makeSolid([[1, 1, 1], [1, -1, -1], [-1, 1, -1], [-1, -1, 1]]),
  cube: makeSolid(signs3((a, b, c) => [a, b, c])),
  octa: makeSolid(uniq(signs3((a) => cyc(a, 0, 0)).flat())),
  dodeca: makeSolid(uniq([...signs3((a, b, c) => [a, b, c]), ...signs3((a, b) => cyc(0, a / PHI, b * PHI)).flat()])),
  icosa: makeSolid(uniq(signs3((a, b) => cyc(0, a, b * PHI)).flat())),
};
const SOLID_OF = ['octa', 'cube', 'tetra', 'dodeca', 'icosa'];  // egg, square, triangle, pentagon, hexagon
function drawSolid(solid, x, y, r, t, flash) {
  const ca = Math.cos(t), sa = Math.sin(t), cb = Math.cos(t * 0.71 + 1), sb = Math.sin(t * 0.71 + 1);
  const pts = solid.verts.map(([vx, vy, vz]) => {
    const x1 = vx * ca + vz * sa, z1 = vz * ca - vx * sa;  // turn about the vertical axis...
    const y2 = vy * cb - z1 * sb, z2 = z1 * cb + vy * sb;  // ...then tilt
    return [x + x1 * r, y + y2 * r, z2];
  });
  // silhouette: the convex hull of the projected corners
  const sorted = pts.map(q => q).sort((p, q) => p[0] - q[0] || p[1] - q[1]);
  const cross = (o, a, b) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
  const hull = [];
  for (const pass of [sorted, sorted.slice().reverse()]) {
    const start = hull.length;
    for (const q of pass) {
      while (hull.length - start >= 2 && cross(hull[hull.length - 2], hull[hull.length - 1], q) <= 0) hull.pop();
      hull.push(q);
    }
    hull.pop();
  }
  ctx.beginPath();
  hull.forEach((q, i) => (i ? ctx.lineTo(q[0], q[1]) : ctx.moveTo(q[0], q[1])));
  ctx.closePath();
  ctx.lineJoin = 'round';
  fillStroke(flash ? '#ffffff' : COLORS[C_POLY]);
  const base = ctx.globalAlpha;
  for (const [i, j] of solid.edges) {  // edges at the back are fainter than those facing us
    const front = pts[i][2] + pts[j][2] > 0;
    ctx.globalAlpha = base * (front ? 0.95 : 0.3);
    ctx.beginPath();
    ctx.moveTo(pts[i][0], pts[i][1]);
    ctx.lineTo(pts[j][0], pts[j][1]);
    ctx.lineWidth = front ? OUT * 0.75 : OUT * 0.5;
    ctx.strokeStyle = shade(COLORS[C_POLY], 0.55);
    ctx.stroke();
  }
  ctx.globalAlpha = base;
}

const MINION_GUN = [0, 1.7, 0.8, 1, 0, 0];
// Invisible classes stay faintly visible to themselves and their team.
function shownAlpha(e) {
  const a = e.alpha === undefined ? 1 : e.alpha;
  return e.id === me.id || (world.teams && e.color === myColor) ? Math.max(a, 0.35) : a;
}

function drawOverhead(e) {
  if (e.dying) return;
  const isTank = e.kind === 0;
  if (isTank && shownAlpha(e) < 0.5) return;
  if (e.hp < 0.995 || (isTank && e.sh < 0.995 && !(e.flags & 2))) {
    const w = Math.max(e.rs * 1.9, 26), h = 5.5, x = e.rx - w / 2, y = e.ry + e.rs + 10;
    ctx.fillStyle = DARK;
    rrect(x - 2, y - 2, w + 4, h + 4, 4);
    ctx.fill();
    ctx.fillStyle = '#7fd97a';
    ctx.fillRect(x, y, w * e.hp, h);
    if (isTank && e.sh > 0 && !(e.flags & 2)) {
      ctx.fillStyle = '#9ad8ff';
      ctx.fillRect(x, y, w * e.sh, 2.2);
    }
  }
  if (isTank && e.id !== me.id && !(e.flags & 8)) {
    const fs = Math.max(11 / viewScale, e.rs * 0.62);
    const cls = defs[e.def] ? defs[e.def].n : '';
    const reach = e.rs * ((BODY[defs[e.def] && defs[e.def].shape] || [0, 1])[1]);  // polygon bodies stick out past their radius
    const crown = board.lead && board.lead[2] === e.id ? '♛ ' : '';  // the leader: there is a bounty on this tank
    label(crown + e.name, e.rx, e.ry - reach - fs * 1.55, fs, 'center', crown ? '#f2c94c' : '#fff');
    if (e.score > 0) label(fmt(e.score), e.rx, e.ry - reach - fs * 0.6, fs * 0.6);
    else if ((e.flags & 2) && cls && cls !== e.name) label(cls, e.rx, e.ry - reach - fs * 0.6, fs * 0.6, 'center', '#f2c94c');  // "Vexa" / "Elite Gunner"
  }
  const say = isTank && chatOn ? bubbles.get(e.id) : null;
  if (say) {  // what the player has just said, in a bubble above the name
    const age = performance.now() - say.t;
    if (age > BUBBLE_MS) { bubbles.delete(e.id); return; }
    const fs = 12.5 / viewScale, pad = fs * 0.7;
    const reach = e.rs * ((BODY[defs[e.def] && defs[e.def].shape] || [0, 1])[1]);
    const nameFs = Math.max(11 / viewScale, e.rs * 0.62);
    ctx.font = `800 ${fs}px Nunito, Ubuntu, "Segoe UI", system-ui, sans-serif`;
    const text = fitText(say.text, 330 / viewScale);
    const w = ctx.measureText(text).width + pad * 2, h = fs * 1.9;
    const x = e.rx - w / 2, y = e.ry - reach - (e.id === me.id ? fs * 0.9 : nameFs * 2.3) - h;
    ctx.globalAlpha = Math.min(1, (BUBBLE_MS - age) / 500) * Math.min(1, shownAlpha(e));
    ctx.fillStyle = '#fff';
    ctx.strokeStyle = INK;
    ctx.lineWidth = 2.2 / viewScale;
    rrect(x, y, w, h, h / 2);
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = INK;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(text, e.rx, y + h / 2 + fs * 0.05);
    ctx.globalAlpha = 1;
  }
}
const BUBBLE_MS = 6000, CHAT_MS = 16000;
// Shorten a line with an ellipsis until it fits (the font must already be set on ctx).
function fitText(text, maxW) {
  if (ctx.measureText(text).width <= maxW) return text;
  let lo = 0, hi = text.length;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (ctx.measureText(text.slice(0, mid) + '…').width <= maxW) lo = mid;
    else hi = mid - 1;
  }
  return text.slice(0, lo) + '…';
}

function buildMinimap() {
  const S = 150;
  miniCanvas = document.createElement('canvas');
  miniCanvas.width = miniCanvas.height = S * 2;
  const m = miniCanvas.getContext('2d');
  m.scale(2, 2);
  m.fillStyle = FLOOR;
  m.fillRect(0, 0, S, S);
  const k = S / world.w;
  m.globalAlpha = 0.45;
  for (const b of world.bases) {
    m.fillStyle = COLORS[b[4]];
    m.fillRect(b[0] * k, b[1] * k, b[2] * k, b[3] * k);
  }
  m.globalAlpha = 1;
  if (world.cols) {
    m.fillStyle = COLORS[7];
    const c = world.cell * k;
    for (let y = 0; y < world.rows; y++)
      for (let x = 0; x < world.cols; x++)
        if (world.walls[y * world.cols + x] === '1') m.fillRect(x * c, y * c, c + 0.3, c + 0.3);
  }
}

// ------------------------------------------------------------------ frame
function resize() {
  dpr = Math.min(window.devicePixelRatio || 1, 2);
  cw = window.innerWidth;
  ch = window.innerHeight;
  canvas.width = Math.round(cw * dpr);
  canvas.height = Math.round(ch * dpr);
}
window.addEventListener('resize', resize);
resize();

function frame(now) {
  requestAnimationFrame(frame);
  if (lastFrame) fps += (1000 / Math.max(1, now - lastFrame) - fps) * 0.05;
  lastFrame = now;
  uiHits = [];

  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.globalAlpha = 1;
  ctx.fillStyle = VOID;
  ctx.fillRect(0, 0, cw, ch);
  if (!world) {
    label('Connecting…', cw / 2, ch / 2, 22);
    return;
  }

  // Ease everything from its previous drawn position to the latest server position.
  const span = updGap * 1.15;
  // Clamp at 0: a frame can carry a timestamp slightly older than the update it follows, and
  // extrapolating backwards from that would make positions and sizes drift.
  const ck = Math.max(0, Math.min(1, (now - cam.t0) / span));
  cam.rx = cam.px + (cam.x - cam.px) * ck;
  cam.ry = cam.py + (cam.y - cam.py) * ck;
  cam.rfov += (cam.fov - cam.rfov) * 0.06;
  viewScale = Math.max(cw / cam.rfov, ch / (cam.rfov * 0.5625));
  const s = viewScale;
  ctx.setTransform(s * dpr, 0, 0, s * dpr, (cw / 2 - cam.rx * s) * dpr, (ch / 2 - cam.ry * s) * dpr);
  const vx0 = cam.rx - cw / 2 / s, vx1 = cam.rx + cw / 2 / s, vy0 = cam.ry - ch / 2 / s, vy1 = cam.ry + ch / 2 / s;

  ctx.fillStyle = FLOOR;
  ctx.fillRect(0, 0, world.w, world.h);
  ctx.globalAlpha = 0.3;
  for (const b of world.bases) {
    ctx.fillStyle = COLORS[b[4]];
    ctx.fillRect(b[0], b[1], b[2], b[3]);
  }
  for (let i = 0; i < world.doms.length; i++) {
    const c = board.doms[i] === undefined ? 9 : board.doms[i];
    ctx.fillStyle = COLORS[c];
    ctx.fillRect(world.doms[i][0] - 170, world.doms[i][1] - 170, 340, 340);
  }
  for (let i = 0; i < (world.sancts || []).length; i++) {  // Boss Rush: the ground around a living sanctuary
    ctx.fillStyle = COLORS[board.rush && board.rush.sancts[i] === 0 ? 9 : 0];  // yellow once it has fallen
    ctx.fillRect(world.sancts[i][0] - 200, world.sancts[i][1] - 200, 400, 400);
  }
  if (world.hill) {  // King of the Hill: the hill takes the colour of the team that holds it
    const o = board.koth ? board.koth.owner : 9;
    ctx.fillStyle = o === 255 ? '#ffffff' : COLORS[o] || COLORS[9];
    ctx.beginPath();
    ctx.arc(world.hill[0], world.hill[1], world.hill[2], 0, TAU);
    ctx.fill();
  }
  for (const st of world.stands || []) {  // Capture the Flag: where each flag stands, and where captures are made
    ctx.fillStyle = COLORS[st[2]];
    ctx.beginPath();
    ctx.arc(st[0], st[1], 70, 0, TAU);
    ctx.fill();
  }
  ctx.globalAlpha = 1;
  if (world.hill) {
    ctx.beginPath();
    ctx.arc(world.hill[0], world.hill[1], world.hill[2], 0, TAU);
    ctx.lineWidth = 5;
    ctx.strokeStyle = shade(COLORS[board.koth && board.koth.owner !== 255 ? board.koth.owner : 7] || COLORS[9]);
    ctx.stroke();
  }

  ctx.beginPath();
  for (let x = Math.floor(vx0 / 40) * 40; x <= vx1; x += 40) { ctx.moveTo(x, vy0); ctx.lineTo(x, vy1); }
  for (let y = Math.floor(vy0 / 40) * 40; y <= vy1; y += 40) { ctx.moveTo(vx0, y); ctx.lineTo(vx1, y); }
  ctx.lineWidth = 1.4;
  ctx.strokeStyle = GRID;
  ctx.stroke();

  if (world.cols) {  // maze walls: stroke everything, then fill, so touching cells merge into one block
    const c = world.cell;
    const x0 = Math.max(0, Math.floor(vx0 / c)), x1 = Math.min(world.cols - 1, Math.floor(vx1 / c));
    const y0 = Math.max(0, Math.floor(vy0 / c)), y1 = Math.min(world.rows - 1, Math.floor(vy1 / c));
    ctx.beginPath();
    for (let y = y0; y <= y1; y++)
      for (let x = x0; x <= x1; x++)
        if (world.walls[y * world.cols + x] === '1') ctx.rect(x * c - 0.5, y * c - 0.5, c + 1, c + 1);
    ctx.lineJoin = 'round';
    ctx.lineWidth = OUT * 2;
    ctx.strokeStyle = shade(COLORS[7]);
    ctx.stroke();
    ctx.fillStyle = COLORS[7];
    ctx.fill();
  }

  if (location.hash === '#gallery') { drawGallery(now); return; }

  // Battle Royale: everything outside the safe circle is tinted red. The server sends the circle
  // once a second, so the radius drawn eases towards it.
  const br = board.br;
  if (br && br.begun && br.r >= 0) {
    zoneR = zoneR < 0 ? br.r : zoneR + (br.r - zoneR) * 0.08;
    ctx.beginPath();
    ctx.rect(vx0 - 10, vy0 - 10, vx1 - vx0 + 20, vy1 - vy0 + 20);
    ctx.arc(br.x, br.y, Math.max(0, zoneR), 0, TAU, true);
    ctx.fillStyle = 'rgba(229, 83, 75, 0.2)';
    ctx.fill('evenodd');
    ctx.beginPath();
    ctx.arc(br.x, br.y, Math.max(0, zoneR), 0, TAU);
    ctx.lineWidth = 7;
    ctx.strokeStyle = COLORS[11];
    ctx.stroke();
  } else {
    zoneR = -1;
  }

  const list = [];
  for (const e of ents.values()) {
    if (e.dying && now - e.dying > 140) { ents.delete(e.id); continue; }
    const k = Math.max(0, Math.min(1, (now - e.t0) / span));
    e.rx = e.px + (e.x - e.px) * k;
    e.ry = e.py + (e.y - e.py) * k;
    e.rs = e.ps + (e.size - e.ps) * k;
    e.ra = e.pa + angDiff(e.pa, e.a) * k;
    if (e.id === me.id && me.alive && state === 'play') {  // turn our own barrel instantly, without waiting for the server
      e.ra = autospin ? spinAngle : touch.used ? touch.angle
        : Math.atan2(mouse.y - ch / 2 - (e.ry - cam.ry) * s, mouse.x - cw / 2 - (e.rx - cam.rx) * s);
      if (reverseTank) e.ra += Math.PI;
      myColor = e.color;
    }
    list.push(e);
  }
  list.sort((a, b) => LAYER[a.kind] - LAYER[b.kind] || a.id - b.id);
  for (const e of list) drawEntity(e, now);
  if (board.ctf) {  // Capture the Flag: a flag rides on its carrier, lies where it was dropped or stands at home
    for (const f of board.ctf.flags) {
      const carrier = f[2] === 1 ? ents.get(f[3]) : null;
      if (carrier) drawFlag(carrier.rx + carrier.rs * 0.5, carrier.ry - carrier.rs * 0.6, 34, COLORS[f[4]]);
      else drawFlag(f[0], f[1], 44, COLORS[f[4]], f[2] === 2 ? 0.55 + 0.35 * Math.sin(now * 0.008) : 1);
    }
  }
  for (const e of list) if (e.kind === 0 || e.kind === 4) drawOverhead(e);

  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  drawHud(now);
  if ((freePick() ? devTree : held('tree')) && state === 'play' && me.alive) drawTree(now);
}

// A flag on a pole, the foot of the pole at (x, y).
function drawFlag(x, y, s, color, alpha = 1) {
  ctx.globalAlpha = alpha;
  ctx.lineJoin = 'round';
  ctx.beginPath();
  ctx.moveTo(x, y - s);
  ctx.lineTo(x + s * 0.75, y - s * 0.72);
  ctx.lineTo(x, y - s * 0.44);
  ctx.closePath();
  fillStroke(color, Math.max(1.5, s * 0.07));
  ctx.beginPath();
  ctx.moveTo(x, y);
  ctx.lineTo(x, y - s);
  ctx.lineWidth = Math.max(2, s * 0.1);
  ctx.lineCap = 'round';
  ctx.strokeStyle = INK;
  ctx.stroke();
  ctx.lineCap = 'butt';
  ctx.globalAlpha = 1;
}

// Developer aid: open /#gallery to preview every tank class defined in src/defs.cpp.
function drawGallery(now) {
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.fillStyle = FLOOR;
  ctx.fillRect(0, 0, cw, ch);
  let cell = 104, cols = 1;
  for (; cell > 36; cell -= 2) {  // shrink the cells until every class fits on screen
    cols = Math.max(1, Math.floor(cw / (cell * 1.2)));
    if (Math.ceil(defs.length / cols) * cell <= ch) break;
  }
  const cellW = cw / cols;
  defs.forEach((d, i) => {
    const x = (i % cols + 0.5) * cellW, y = Math.floor(i / cols) * cell + cell * 0.45;
    ctx.save();
    ctx.translate(x, y);
    drawTank(d, cell * (d.shape === 20 ? 0.2 : 0.155), -Math.PI / 2, COLORS[d.hid ? 10 : d.tier % 4], now, cell * 0.023);
    ctx.restore();
    label(d.n, x, y + cell * 0.43, Math.max(7, cell * 0.105));
  });
}

// Hold T while playing: every class by level, with the ones you can still reach lit up.
function drawTree(now) {
  const tiers = [[], [], [], []];
  const names = ['Level 1', 'Level 15', 'Level 30', 'Level 45'];
  defs.forEach((d, i) => { if (!d.hid && tiers[d.tier]) tiers[d.tier].push(i); });
  const pick = freePick();  // a developer, or anyone in the Sandbox: every class can be clicked
  if (isDev) {  // developers also get every computer-controlled class
    tiers.push(defs.map((d, i) => (d.hid ? i : -1)).filter(i => i >= 0));
    names.push('Developer only');
  }
  if (pick) uiHits = [];
  const reach = new Set([me.def]), todo = [me.def];
  while (todo.length) {
    const d = defs[todo.pop()];
    for (const u of (d ? d.up : [])) if (!reach.has(u)) { reach.add(u); todo.push(u); }
  }
  ctx.fillStyle = 'rgba(47,52,59,0.94)';
  ctx.fillRect(0, 0, cw, ch);
  let cell = 86, cols = 1;
  for (; cell > 30; cell -= 2) {
    cols = Math.max(1, Math.floor((cw - 40) / cell));
    const rows = tiers.reduce((a, t) => a + Math.ceil(t.length / cols), 0);
    if (rows * cell + tiers.length * 22 + 44 <= ch) break;
  }
  label(isDev ? 'Developer: click any class to become it (T closes this)' : pick ? 'Sandbox: click any class to become it (T closes this)'
    : 'Class tree: the classes you can still reach are lit', cw / 2, 18, 14);
  let y = 34;
  tiers.forEach((t, ti) => {
    label(names[ti], 20, y + 10, 12, 'left', '#f2c94c');
    y += 22;
    t.forEach((di, k) => {
      const d = defs[di], x = 20 + (k % cols + 0.5) * cell, yy = y + Math.floor(k / cols) * cell + cell * 0.4;
      ctx.globalAlpha = pick || reach.has(di) ? 1 : 0.28;
      if (pick) uiHits.push({ x: x - cell / 2, y: yy - cell * 0.4, w: cell, h: cell, fn: () => { sendUpgrade(di); devTree = false; } });
      if (di === me.def) {
        ctx.fillStyle = '#f2c94c';
        rrect(x - cell / 2 + 2, yy - cell * 0.4 + 2, cell - 4, cell - 4, 8);
        ctx.fill();
      }
      ctx.save();
      ctx.translate(x, yy);
      drawTank(d, cell * (d.shape === 20 ? 0.19 : 0.16), -Math.PI / 4, COLORS[myColor] || COLORS[0], now, cell * 0.025);
      ctx.restore();
      label(d.n, x, yy + cell * 0.43, Math.max(5.5, Math.min(cell * 0.112, (cell * 1.7) / d.n.length)));
      ctx.globalAlpha = 1;
    });
    y += Math.ceil(t.length / cols) * cell;
  });
}

// ------------------------------------------------------------------ HUD
function bar(x, y, w, h, frac, color, text, size) {
  ctx.fillStyle = INK;
  ctx.globalAlpha = 0.82;
  rrect(x, y, w, h, h / 2);
  ctx.fill();
  ctx.globalAlpha = 1;
  if (frac > 0.005) {
    ctx.fillStyle = color;
    rrect(x + 2, y + 2, Math.max(h - 4, (w - 4) * Math.min(1, frac)), h - 4, (h - 4) / 2);
    ctx.fill();
  }
  if (text) label(text, x + w / 2, y + h / 2 + 0.5, size || h * 0.62);
}

function drawHud(now) {
  // big announcement (victory, arena closed, new game)
  if (banner) {
    const age = now - banner.t;
    if (age > 8000) banner = null;
    else {
      ctx.globalAlpha = Math.min(1, age / 300, (8000 - age) / 800);
      const size = Math.min(54, cw / 14);
      label(banner.text, cw / 2, ch * 0.27, size, 'center', COLORS[banner.color] || '#fff');
      if (banner.sub) label(banner.sub, cw / 2, ch * 0.27 + size * 0.85, Math.max(12, size * 0.34));
      ctx.globalAlpha = 1;
    }
  }

  // messages
  let my = 16;
  for (let i = messages.length - 1; i >= 0; i--) {
    const m = messages[i], age = now - m.t;
    if (age > 6000) { messages.splice(i, 1); continue; }
    ctx.font = '800 14px Nunito, Ubuntu, "Segoe UI", system-ui, sans-serif';
    const w = ctx.measureText(m.text).width + 28;
    ctx.globalAlpha = Math.min(1, (6000 - age) / 600) * 0.85;
    ctx.fillStyle = INK;
    rrect(cw / 2 - w / 2, my, w, 26, 13);
    ctx.fill();
    ctx.globalAlpha = Math.min(1, (6000 - age) / 600);
    ctx.fillStyle = '#fff';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(m.text, cw / 2, my + 13.5);
    ctx.globalAlpha = 1;
    my += 31;
  }

  // Small screens (phones) get a slimmer HUD.
  const compact = ch < 520 || cw < 720;

  // leaderboard
  const lbW = compact ? 150 : 196, lbX = cw - lbW - 14;
  const sandbox = world.mode === 'sandbox' && !!world.sandbox;
  const lbRows = Math.min(board.lb.length, compact || sandbox ? 5 : 10);  // the Sandbox panel needs the room below
  let lbBottom = compact ? 28 : 38;
  if (board.lb.length && !(sandbox && compact && sand.open)) {
    label('Leaderboard', lbX + lbW / 2, compact ? 16 : 22, compact ? 13 : 17);
    const top = Math.max(1, board.lb[0][1]);
    board.lb.slice(0, lbRows).forEach((row, i) => {
      const crown = i === 0 && board.lead ? '♛ ' : '';  // there is a bounty on the leader
      if (compact) bar(lbX, 28 + i * 17, lbW, 15, row[1] / top, COLORS[row[2]], `${crown}${row[0]} · ${fmt(row[1])}`, 9.5);
      else bar(lbX, 38 + i * 21, lbW, 18, row[1] / top, COLORS[row[2]], `${crown}${row[0]} · ${fmt(row[1])}`, 11);
    });
    lbBottom += lbRows * (compact ? 17 : 21);
  }

  // minimap
  const S = compact ? 96 : 150, mx = cw - S - 14, mY = ch - S - 14;
  if (miniCanvas) {
    ctx.globalAlpha = 0.9;
    ctx.drawImage(miniCanvas, mx, mY, S, S);
    ctx.globalAlpha = 1;
    const k = S / world.w;
    world.doms.forEach((d, i) => {
      ctx.fillStyle = COLORS[board.doms[i] === undefined ? 9 : board.doms[i]];
      ctx.fillRect(mx + d[0] * k - 5, mY + d[1] * k - 5, 10, 10);
      ctx.lineWidth = 1.5;
      ctx.strokeStyle = INK;
      ctx.strokeRect(mx + d[0] * k - 5, mY + d[1] * k - 5, 10, 10);
    });
    (world.sancts || []).forEach((d, i) => {  // Boss Rush sanctuaries: blue while standing, yellow once fallen
      ctx.fillStyle = board.rush && board.rush.sancts[i] === 0 ? COLORS[9] : COLORS[0];
      ctx.fillRect(mx + d[0] * k - 5, mY + d[1] * k - 5, 10, 10);
      ctx.lineWidth = 1.5;
      ctx.strokeStyle = INK;
      ctx.strokeRect(mx + d[0] * k - 5, mY + d[1] * k - 5, 10, 10);
    });
    for (const bp of board.bosses || []) {  // bosses in the arena
      poly(mx + bp[0] * k, mY + bp[1] * k, 6, 4, 0);
      ctx.fillStyle = COLORS[bp[2] === undefined ? 10 : bp[2]] || COLORS[10];
      ctx.fill();
      ctx.lineWidth = 1.5;
      ctx.strokeStyle = INK;
      ctx.stroke();
    }
    if (world.hill) {  // King of the Hill
      const o = board.koth ? board.koth.owner : 9;
      ctx.beginPath();
      ctx.arc(mx + world.hill[0] * k, mY + world.hill[1] * k, world.hill[2] * k, 0, TAU);
      ctx.globalAlpha = 0.6;
      ctx.fillStyle = o === 255 ? '#ffffff' : COLORS[o] || COLORS[9];
      ctx.fill();
      ctx.globalAlpha = 1;
      ctx.lineWidth = 1.5;
      ctx.strokeStyle = INK;
      ctx.stroke();
    }
    for (const st of world.stands || []) {  // Capture the Flag: the stands...
      ctx.beginPath();
      ctx.arc(mx + st[0] * k, mY + st[1] * k, 4.5, 0, TAU);
      ctx.lineWidth = 2;
      ctx.strokeStyle = COLORS[st[2]];
      ctx.stroke();
    }
    if (board.ctf) for (const f of board.ctf.flags) drawFlag(mx + f[0] * k, mY + f[1] * k + 5, 12, COLORS[f[4]]);  // ...and the flags
    for (const m of board.ms || []) {  // Motherships
      ctx.beginPath();
      ctx.arc(mx + m[0] * k, mY + m[1] * k, 6, 0, TAU);
      ctx.fillStyle = COLORS[m[2]];
      ctx.fill();
      ctx.lineWidth = 2;
      ctx.strokeStyle = INK;
      ctx.stroke();
    }
    if (board.br && board.br.begun && zoneR >= 0) {  // Battle Royale: the safe circle
      ctx.save();
      ctx.beginPath();
      ctx.rect(mx, mY, S, S);
      ctx.clip();
      ctx.beginPath();
      ctx.arc(mx + board.br.x * k, mY + board.br.y * k, zoneR * k, 0, TAU);
      ctx.lineWidth = 2;
      ctx.strokeStyle = COLORS[2];
      ctx.stroke();
      ctx.restore();
    }
    if (board.lead) {  // the leader, for everyone to see: there is a bounty on that tank
      star(mx + board.lead[0] * k, mY + board.lead[1] * k, 6.5, 2.8, 5, -Math.PI / 2);
      ctx.fillStyle = '#f2c94c';
      ctx.fill();
      ctx.lineWidth = 1.5;
      ctx.strokeStyle = INK;
      ctx.stroke();
    }
    if (me.alive && world.teams) {
      ctx.fillStyle = COLORS[myColor] || INK;
      for (const p of board.mm) {
        ctx.beginPath();
        ctx.arc(mx + p[0] * k, mY + p[1] * k, 2.5, 0, TAU);
        ctx.fill();
      }
    }
    const self = me.alive ? ents.get(me.id) : null;  // our tank, not the view centre (zoom moves the view)
    const dotX = Math.max(0, Math.min(world.w, self ? self.rx : cam.rx)), dotY = Math.max(0, Math.min(world.h, self ? self.ry : cam.ry));
    ctx.fillStyle = INK;
    ctx.beginPath();
    ctx.arc(mx + dotX * k, mY + dotY * k, 3.5, 0, TAU);
    ctx.fill();
    ctx.lineWidth = 3;
    ctx.strokeStyle = DARK;
    ctx.strokeRect(mx, mY, S, S);
    const lat = showLatency ? ` · ${ping < 0 ? '…' : ping} ms` : '';
    label(compact ? world.title + lat : `${world.title} · ${board.n} tanks · ${Math.round(fps)} fps${lat}`, mx + S, mY - 12, 11, 'right');
    let infoY = mY - 28;
    if (board.rush) {  // Boss Rush progress
      const ru = board.rush, alive = ru.sancts.filter(v => v).length;
      const what = ru.wave === 0 ? `First wave in ${ru.next}s` : ru.bosses ? `${ru.bosses} boss${ru.bosses === 1 ? '' : 'es'} left`
        : ru.next ? `next wave in ${ru.next}s` : 'cleared';
      // Endless Boss Rush has no last wave (of = 0): it shows the server's record instead.
      label(`Wave ${Math.max(1, ru.wave)}${ru.of ? '/' + ru.of : ` (best ${ru.best})`} · ${what}`, mx + S, infoY, 13, 'right', '#f2c94c');
      label(`Sanctuaries ${alive}/${ru.sancts.length}`, mx + S, infoY - 17, 12, 'right', alive > 1 ? '#fff' : COLORS[2]);
      infoY -= 34;
      if (ru.lose >= 0) {  // every sanctuary is yellow: the countdown to defeat
        label(`RESTORE A SANCTUARY: ${ru.lose}s`, cw / 2, ch * 0.18, Math.min(30, cw / 22), 'center', COLORS[2]);
      }
    }
    const clock = s => `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
    const line = (text, color = '#fff', size = 12) => { label(text, mx + S, infoY, size, 'right', color); infoY -= size + 5; };
    if (board.ctf) {
      const c = board.ctf;
      line(c.ot ? 'Overtime: the next capture wins' : `First to ${c.goal} · ${clock(c.left)} left`);
      line(`Flags: Blue ${c.score[0]} · Green ${c.score[1]}`, '#f2c94c', 13);
    }
    if (board.siege) {
      const sg = board.siege;
      if (me.alive && state === 'play') line(myColor === 0 ? 'You attack: take the dominators' : 'You defend: hold the dominators');
      line(`Siege · ${clock(sg.left)} left · ${sg.taken}/${sg.of} taken`, sg.left <= 60 ? COLORS[2] : '#f2c94c', 13);
    }
    if (board.koth) {
      const kh = board.koth;
      line(kh.owner === 255 ? 'The hill is contested' : kh.owner === 9 ? 'Nobody holds the hill' : `${kh.owner === 0 ? 'Blue' : 'Green'} holds the hill`,
        kh.owner === 255 || kh.owner === 9 ? '#fff' : lighten(COLORS[kh.owner], 0.3));
      line(`Hill: Blue ${kh.score[0]} · Green ${kh.score[1]} / ${kh.goal}`, '#f2c94c', 13);
    }
    for (const m of board.ms || [])
      line(`${m[2] === 0 ? 'Blue' : 'Green'} Mothership ${m[3]}%${m[4] ? ' (player)' : ''}`, m[3] <= 30 ? COLORS[2] : lighten(COLORS[m[2]], 0.3), 13);
    if (board.br) {
      const b = board.br;
      line(b.begun ? `${b.alive} alive · circle closes in ${clock(b.left)}` : b.start < 0 ? 'The countdown starts when a player joins'
        : `Battle begins in ${b.start}s · no damage yet`, '#f2c94c', 13);
    }
    if (board.tag) {  // Tag: how many tanks each team has
      const names = ['Blue', 'Green', 'Red', 'Purple'];
      let x = mx + S;
      for (let i = board.tag.length - 1; i >= 0; i--) {
        const text = `${names[i]} ${board.tag[i]}`;
        label(text, x, infoY, 12, 'right', board.tag[i] ? lighten(COLORS[i], 0.3) : '#8d949e');
        x -= ctx.measureText(text).width + 12;
      }
      infoY -= 17;
    }
    if (board.lead) {
      const who = board.lead[2] === me.id && me.alive ? 'you' : board.lb[0] ? board.lb[0][0] : 'the leader';
      line(`♛ Bounty on ${who}: ${fmt(board.lead[3])}`, '#f2c94c');
    }
    if (board.closing) label('ARENA CLOSED', mx + S, infoY, 12, 'right', COLORS[11]);
  }

  if (!me.alive || state !== 'play') return;
  const def = defs[me.def] || { n: '' };
  const own = ents.get(me.id);

  // The bounty: an arrow at the edge of the view points to the leader while they are off screen
  // (not at a team-mate, and not at ourselves).
  if (board.lead && own && board.lead[2] !== me.id && !(world.teams && board.lead[4] === myColor)) {
    const dx = board.lead[0] - own.rx, dy = board.lead[1] - own.ry;
    const sx = (board.lead[0] - cam.rx) * viewScale, sy = (board.lead[1] - cam.ry) * viewScale;
    if (Math.abs(sx) > cw / 2 - 30 || Math.abs(sy) > ch / 2 - 30) {
      const a = Math.atan2(dy, dx), rad = Math.min(cw, ch) * 0.36;
      const ax = cw / 2 + Math.cos(a) * rad, ay = ch / 2 + Math.sin(a) * rad;
      ctx.save();
      ctx.translate(ax, ay);
      ctx.rotate(a);
      ctx.beginPath();
      ctx.moveTo(15, 0); ctx.lineTo(-9, -10); ctx.lineTo(-4, 0); ctx.lineTo(-9, 10);
      ctx.closePath();
      ctx.globalAlpha = 0.9;
      ctx.lineJoin = 'round';
      fillStroke('#f2c94c', 2.5);
      ctx.restore();
      ctx.globalAlpha = 1;
      label(`♛ ${fmt(board.lead[3])}`, ax - Math.cos(a) * 30, ay - Math.sin(a) * 30, 11.5, 'center', '#f2c94c');
    }
  }
  // Battle Royale: say so loudly while our tank is outside the safe circle.
  if (board.br && board.br.begun && own && Math.hypot(own.rx - board.br.x, own.ry - board.br.y) > Math.max(0, zoneR))
    label('OUTSIDE THE CIRCLE: GET BACK IN', cw / 2, ch * 0.18, Math.min(28, cw / 24), 'center', COLORS[2]);
  if (sandbox) drawSandbox(compact, lbBottom, lbX, lbW);

  // thumb sticks
  for (const st of [touch.move, touch.aim]) {
    if (!st) continue;
    const dx = st.x - st.sx, dy = st.y - st.sy, len = Math.hypot(dx, dy) || 1, k = Math.min(1, 44 / len);
    ctx.globalAlpha = 0.22;
    ctx.fillStyle = INK;
    ctx.beginPath(); ctx.arc(st.sx, st.sy, 50, 0, TAU); ctx.fill();
    ctx.globalAlpha = 0.5;
    ctx.beginPath(); ctx.arc(st.sx + dx * k, st.sy + dy * k, 22, 0, TAU); ctx.fill();
    ctx.globalAlpha = 1;
  }

  // name, score and level bars
  const bw = Math.min(340, cw * 0.4), bx = cw / 2 - bw / 2;
  if (!compact) {
    label(me.name, cw / 2, ch - 86, 26);
    if (killBar) {  // the kill bar sits beside a shorter score bar: kills, assists and boss kills of this life
      const sw = bw * 0.38;
      bar(bx, ch - 62, sw, 18, 1, '#5fc66a', `Score: ${fmt(me.score)}`, 11.5);
      bar(bx + sw + 6, ch - 62, bw - sw - 6, 18, 1, '#e5534b',
        `${me.kills} kill${me.kills === 1 ? '' : 's'} · ${me.assists} assist${me.assists === 1 ? '' : 's'} · ${me.bossKills} boss${me.bossKills === 1 ? '' : 'es'}`, 10.5);
    } else {
      bar(bx + bw * 0.14, ch - 62, bw * 0.72, 18, 1, '#5fc66a', `Score: ${fmt(me.score)}`, 11.5);
    }
    bar(bx, ch - 40, bw, 24, me.prog, '#f2c94c', `Level ${me.level} ${def.n}`, 13.5);
  } else {
    bar(bx, ch - 26, bw, 20, me.prog, '#f2c94c', `Lv ${me.level} ${def.n} · ${fmt(me.score)}`, 11);
  }
  if (isDev && !compact) label('Developer mode: press T to choose any class', cw / 2, ch - 112, 12, 'center', COLORS[10]);
  else if (sandbox && !compact && me.level >= (world.freeLevel || 0)) label(`Sandbox: press ${keyLabel(binds.tree)} to choose any class`, cw / 2, ch - 112, 12, 'center', COLORS[10]);
  // key hints for abilities that are easy to miss
  if (!compact && !touch.used) {
    const hints = [];
    const mine = ents.get(me.id);
    if (mine && (mine.flags & 1)) hints.push(`Spawn-protected while you stay still and hold fire (20 s at most) · hold ${keyLabel(binds.selfDestruct)} to self-destruct and spawn again`);
    if (def.z) hints.push('Hold right-click or Shift to zoom');
    if (def.wv) hints.push('Hold right-click or Shift to mirror the wave');
    if (def.af) hints.push('Hold right-click or Shift to fire your main barrel');
    if (def.tur && def.tur.length) hints.push(`R: turrets ${override ? 'manual' : 'automatic'}`);
    const hk = keyLabel(binds.dominator);
    if (world.doms.length) {  // Domination, Maze Domination and Siege
      if (def.hid && (def.n.endsWith('Dominator') || def.n === 'Citadel') && me.level === 1) hints.push(`Press ${hk} to leave the dominator`);
      else if (board.doms.includes(myColor)) hints.push(`Press ${hk} to control one of your dominators`);
    }
    if (board.ms) {  // Mothership
      if (def.n === 'Mothership') hints.push(`You drive the Mothership: click sends its drones · press ${hk} to leave it`);
      else if (board.ms.some(m => m[2] === myColor && !m[4])) hints.push(`Press ${hk} to drive your Mothership`);
    }
    if (board.ctf && board.ctf.flags.some(f => f[2] === 1 && f[3] === me.id)) hints.push('You carry the flag: take it to your own stand');
    if (board.lead && board.lead[2] === me.id) hints.push(`♛ You are the leader: a bounty of ${fmt(board.lead[3])} is on you`);
    if (hints.length) label(hints.join(' · '), cw / 2, ch - 148, 12, 'center', '#f4f1e8');
  }
  // free levels: hold N on a keyboard, tap the button on a touch screen
  if (world.freeLevel && me.level < world.freeLevel && !def.hid) {
    const ly = compact ? ch - 40 : ch - 112;
    if (touch.used) {
      const w = 118, h = 26, x = cw / 2 - w / 2, y = ly - h / 2 - 4;
      bar(x, y, w, h, 1, autoLevel ? '#f2c94c' : '#5fc66a', autoLevel ? 'Levelling…' : 'Level up', 12);
      uiHits.push({ x, y, w, h, fn: () => { autoLevel = !autoLevel; } });
    } else {
      label(`Hold ${keyLabel(binds.levelup)} to level up (free until level ${world.freeLevel})`, cw / 2, ly, 12, 'center', '#f2c94c');
    }
  }

  // self-destruct: the key has to stay down for two seconds
  if (destructAt) {
    const left = 700 - (now - destructAt);
    const mine = ents.get(me.id);
    if (!held('selfDestruct')) destructAt = 0;
    else if (!mine || !(mine.flags & 1)) { destructAt = 0; pushMessage('Too late: your spawn protection has ended'); }
    else if (left <= 0) { destructAt = 0; if (isOpen()) ws.send(new Uint8Array([7])); }
    else label(`Self-destruct in ${(left / 1000).toFixed(1)}s (let go to cancel)`, cw / 2, ch * 0.36, 18, 'center', COLORS[2]);
  }
  if (recorder) label('● REC', 14, ch * 0.5, 13, 'left', COLORS[2]);

  // Each class has its own stats: the Smasher line shows five rows that take 12 points each,
  // and its Reload row is called Engine Acceleration.
  const maxS = def.cap || world.maxSkill || 9, mask = def.sm === undefined ? 1023 : def.sm;
  const rows = [];
  for (let i = 0; i < 10; i++) if ((mask >> i) & 1) rows.push(i);
  const rowH = compact ? 17 : 20, sw = compact ? 176 : 214, sx = 14, sy = ch - 14 - rowH * rows.length;
  const panelShown = !(compact && me.pts === 0);

  // touch buttons beside the minimap: the second action (what right-click / Shift does for this class) and chat
  if (touch.used) {
    const what = def.af ? 'Barrel' : def.z ? 'Zoom' : def.wv ? 'Mirror' : (def.guns || []).some(g => g[5] === 2 || g[5] === 4) ? 'Repel' : '';
    if (!what) touchAlt = false;
    const w = 64, h = 30, x = mx - w - 10;
    let y = ch - 14 - h;
    if (what) {
      bar(x, y, w, h, 1, touchAlt ? '#f2c94c' : '#8f98a3', what, 12);
      uiHits.push({ x, y, w, h, fn: () => { touchAlt = !touchAlt; } });
      y -= h + 8;
    }
    if (world.chat && chatOn) {
      bar(x, y, w, h, 1, '#4aa3df', 'Chat', 12);
      uiHits.push({ x, y, w, h, fn: openChat });
    }
  }

  // chat: the last few lines, stacked above the skill panel
  if (chatOn && chatLog.length) {
    const fs = compact ? 11 : 13, lh = fs + 9, maxW = Math.min(compact ? 250 : 380, cw * 0.42);
    let y = (panelShown ? sy - (me.pts > 0 ? 34 : 12) : ch - 14) - lh;
    ctx.font = `800 ${fs}px Nunito, Ubuntu, "Segoe UI", system-ui, sans-serif`;
    ctx.textAlign = 'left';
    ctx.textBaseline = 'middle';
    const from = Math.max(0, chatLog.length - (compact ? 3 : 6));
    for (let i = chatLog.length - 1; i >= from; i--) {
      const c = chatLog[i], age = now - c.t;
      if (age > CHAT_MS) continue;
      const name = fitText(c.name, maxW * 0.4) + ': ', nw = ctx.measureText(name).width;
      const text = fitText(c.text, maxW - nw);
      const fade = Math.min(1, (CHAT_MS - age) / 800);
      ctx.globalAlpha = 0.62 * fade;
      ctx.fillStyle = INK;
      rrect(sx, y, nw + ctx.measureText(text).width + 18, lh - 3, 8);
      ctx.fill();
      ctx.globalAlpha = fade;
      ctx.fillStyle = lighten(COLORS[c.color] || '#ffffff', 0.35);
      ctx.fillText(name, sx + 9, y + (lh - 3) / 2 + 0.5);
      ctx.fillStyle = '#fff';
      ctx.fillText(text, sx + 9 + nw, y + (lh - 3) / 2 + 0.5);
      y -= lh;
    }
    ctx.globalAlpha = 1;
    while (chatLog.length && now - chatLog[0].t > CHAT_MS) chatLog.shift();
  }

  // skill panel (on small screens it only appears while there are points to spend)
  if (compact && me.pts === 0 && !me.ups.length) return;
  if (me.pts > 0) label(`x${me.pts}`, sx + sw + 22, sy - 14, 20, 'center', '#f2c94c');
  for (let row = 0; row < rows.length && !(compact && me.pts === 0); row++) {
    const i = rows[row], y = sy + row * rowH, lv = me.skills[i];
    ctx.globalAlpha = me.pts > 0 ? 0.9 : 0.6;
    ctx.fillStyle = INK;
    rrect(sx, y, sw, rowH - 3, 8);
    ctx.fill();
    ctx.globalAlpha = me.pts > 0 ? 1 : 0.75;
    const segW = (sw - 6) / maxS;
    ctx.fillStyle = SKILL_COLORS[i];
    for (let k = 0; k < lv; k++) {
      rrect(sx + 3 + k * segW + 0.75, y + 3, segW - 1.5, rowH - 9, 3);
      ctx.fill();
    }
    label(def.eng && i === 6 ? 'Engine Acceleration' : world.skills[i], sx + sw / 2, y + (rowH - 3) / 2 + 0.5, 10.5);
    label(`[${(i + 1) % 10}]`, sx + sw - 8, y + (rowH - 3) / 2 + 0.5, 9.5, 'right', '#cfd4da');
    ctx.globalAlpha = 1;
    if (me.pts > 0 && lv < maxS) {
      ctx.fillStyle = SKILL_COLORS[i];
      ctx.beginPath();
      ctx.arc(sx + sw + 13, y + (rowH - 3) / 2, 7.5, 0, TAU);
      ctx.fill();
      ctx.lineWidth = 2;
      ctx.strokeStyle = INK;
      ctx.stroke();
      label('+', sx + sw + 13, y + (rowH - 3) / 2 + 0.5, 12);
      // With a mouse the whole row is clickable; on touch only the + is, so the left thumb can steer over the panel.
      if (touch.used) uiHits.push({ x: sx + sw, y, w: 30, h: rowH, fn: () => sendSkill(i) });
      else uiHits.push({ x: sx, y, w: sw + 24, h: rowH, fn: () => sendSkill(i) });
    }
  }

  // class upgrades
  if (me.ups.length) {
    const B = compact ? 62 : 86, gap = compact ? 6 : 8;
    // Small screens fit more boxes per row so nine choices don't run down into the skill panel.
    const perRow = compact ? Math.max(3, Math.min(6, Math.floor((cw - 42 - lbW) / (B + gap)))) : 4;
    const cols = Math.min(perRow, me.ups.length);
    const upY = compact ? 26 : 42;
    label('Choose an upgrade', 14 + (cols * (B + gap) - gap) / 2, compact ? 14 : 24, compact ? 12 : 16);
    me.ups.forEach((di, i) => {
      const d = defs[di];
      if (!d) return;
      const x = 14 + (i % perRow) * (B + gap), y = upY + Math.floor(i / perRow) * (B + gap);
      const hover = mouse.x >= x && mouse.x <= x + B && mouse.y >= y && mouse.y <= y + B;
      ctx.globalAlpha = hover ? 1 : 0.88;
      ctx.fillStyle = lighten(SKILL_COLORS[(i * 3) % 10], hover ? 0.5 : 0.3);
      rrect(x, y, B, B, 10);
      ctx.fill();
      ctx.lineWidth = 3;
      ctx.strokeStyle = INK;
      ctx.stroke();
      ctx.globalAlpha = 1;
      ctx.save();
      ctx.translate(x + B / 2, y + B / 2 - 7);
      drawTank(d, compact ? 9 : 13, now * 0.0008 + i, COLORS[myColor] || COLORS[0], now, compact ? 1.8 : 2.2);
      ctx.restore();
      label(d.n, x + B / 2, y + B - (compact ? 9 : 12), compact ? 8 : d.n.length > 11 ? 9.5 : 11);
      if (i < UP_ACTIONS.length && !touch.used) label(keyLabel(binds[UP_ACTIONS[i]]), x + 8, y + 11, compact ? 8 : 10, 'left', '#f2c94c');
      uiHits.push({ x, y, w: B, h: B, fn: () => sendUpgrade(di) });
    });
  }

  // toggles
  const tags = [];
  if (autofire) tags.push('Autofire');
  if (autospin) tags.push('Autospin');
  if (override) tags.push('Override');
  if (reverseTank) tags.push('Reverse tank');
  if (reverseMouse) tags.push('Reverse mouse');
  if (tags.length) label(tags.join(' · '), cw / 2, compact ? ch - 58 : ch - 130, 12, 'center', '#f2c94c');
}

// The Sandbox panel, under the leaderboard: choose a polygon or a boss and spawn it, clear the
// map, set a level, add bots. Every button sends one command (message 10) to the server. The two
// Spawn buttons place the thing beside the tank; the F and G keys place it under the cursor.
function drawSandbox(compact, lbBottom, lbX, lbW) {
  const sb = world.sandbox, rowH = 22, w = lbW, x = lbX;
  let y = lbBottom + 10;
  const btn = (bx, by, bw, text, fn, color = '#8f98a3') => {
    bar(bx, by, bw, rowH - 4, 1, color, text, 10.5);
    uiHits.push({ x: bx, y: by, w: bw, h: rowH - 4, fn });
  };
  if (compact) {  // small screens: the panel opens and closes with a button
    if (!sand.open) { btn(x, y, w, 'Sandbox panel', () => { sand.open = true; }, '#4aa3df'); return; }
    y = 8;
    btn(x, y, w, 'Close panel', () => { sand.open = false; }, '#4aa3df');
    y += rowH;
  } else {
    label('Sandbox', x + w / 2, y + 8, 13);
    y += rowH - 2;
  }
  const cycle = (key, n) => d => { sand[key] = (sand[key] + d + n) % n; };
  const chooser = (text, step) => {  // ◀ value ▶
    btn(x, y, 24, '◀', () => step(-1));
    bar(x + 27, y, w - 54, rowH - 4, 1, '#4a4f57', text, 10.5);
    btn(x + w - 24, y, 24, '▶', () => step(1));
    y += rowH;
  };
  const pair = (a, b) => {
    const hw = (w - 4) / 2;
    btn(x, y, hw, a[0], a[1]);
    btn(x + hw + 4, y, hw, b[0], b[1]);
    y += rowH;
  };
  const kinds = sb.muts.concat(['Crasher']);
  const crasher = sand.mut === kinds.length - 1;
  const sizes = crasher ? ['Small', 'Large'] : world.tiers.map(t => t.trim() || 'Normal');
  if (sand.size >= sizes.length) sand.size = 0;
  const boss = defs[sb.bosses[sand.boss]];
  const key = a => (touch.used ? '' : ` · ${keyLabel(binds[a])}`);
  if (!crasher) chooser(world.foods[sand.shape] ? world.foods[sand.shape].n : '?', cycle('shape', world.foods.length));
  chooser(sizes[sand.size], cycle('size', sizes.length));
  chooser(kinds[sand.mut], cycle('mut', kinds.length));
  btn(x, y, w, `Spawn shape${key('sandShape')}`, () => sandShape(false), '#5fc66a');
  y += rowH;
  chooser(boss ? boss.n : '?', cycle('boss', sb.bosses.length));
  btn(x, y, w, `Spawn boss${key('sandBoss')}`, () => sandBoss(false), '#e86fb0');
  y += rowH;
  pair([sand.shapes ? 'Clear shapes' : 'Shapes back', () => { sand.shapes = !sand.shapes; sendSand(3, sand.shapes ? 1 : 0); }], ['Bosses away', () => sendSand(4)]);
  pair(['Level 1', () => sendSand(5, 1)], [`Level ${world.maxLevel}`, () => sendSand(5, world.maxLevel)]);
  pair(['Bot +', () => sendSand(6, 1)], ['Bot −', () => sendSand(6, 0)]);
  pair(['Bot skill', () => sendSand(8)], ['No damage', () => sendSand(7)]);
  btn(x, y, w, 'Repair my tank', () => sendSand(9));
}

// ------------------------------------------------------------------ menu + input
function setStatus(t) { $('status').textContent = t; }
function showMenu() {
  state = 'menu';
  closeChat();
  $('menu').hidden = false;
  $('death').hidden = true;
}
// Chat: Enter opens a one-line box, Enter again sends it, Esc (or a click on the game) closes it.
function openChat() {
  if (chatOpen || state !== 'play' || !me.alive) return;
  if (!world || !world.chat) { pushMessage('Chat is turned off on this server'); return; }
  if (!chatOn) { pushMessage(`Chat is hidden: press ${keyLabel(binds.chatToggle)} to show it`); return; }
  chatOpen = true;
  for (const k in keys) keys[k] = false;  // the tank stops while you type
  mouse.down = mouse.alt = false;
  touch.move = touch.aim = null;
  $('chatInput').maxLength = world.chatMax || 80;
  $('chatInput').value = '';
  $('chatBox').classList.toggle('top', touch.used);  // clear of a phone's on-screen keyboard
  $('chatBox').hidden = false;
  $('chatInput').focus();
}
function closeChat() {
  if (!chatOpen) return;
  chatOpen = false;
  $('chatBox').hidden = true;
  $('chatInput').blur();
}
$('chatBox').addEventListener('submit', ev => {
  ev.preventDefault();
  const text = $('chatInput').value.trim();
  if (text && isOpen() && me.alive) {
    const bytes = new TextEncoder().encode(text.slice(0, world.chatMax || 80));
    const pkt = new Uint8Array(bytes.length + 1);
    pkt[0] = 9;
    pkt.set(bytes, 1);
    ws.send(pkt);
  }
  closeChat();
});
$('chatInput').addEventListener('keydown', ev => {
  ev.stopPropagation();  // letters typed here are not game keys
  if (ev.key === 'Escape') { ev.preventDefault(); closeChat(); }
});
$('chatInput').addEventListener('keyup', ev => ev.stopPropagation());
$('chatInput').addEventListener('blur', () => closeChat());

function play() {
  me.name = $('name').value.trim().slice(0, 18) || 'Unnamed';
  try { localStorage.setItem('polyrift-name', $('name').value); } catch (e) { /* storage unavailable */ }
  $('name').blur();
  $('code').blur();
  if (!me.alive) sendSpawn();
  if (!isOpen()) setStatus('Connecting…');
}

try { $('name').value = localStorage.getItem('polyrift-name') || ''; } catch (e) { /* storage unavailable */ }
$('play').addEventListener('click', play);
$('unlock').addEventListener('click', () => {
  if (isDev) return;
  if (!$('code').value) setDevStatus('Type the code first.', 'bad');
  else if (!isOpen()) setDevStatus('Still connecting. Try again in a moment.', 'bad');
  else if (sendDevCode()) setDevStatus('Checking…', '');
});
$('code').addEventListener('input', () => { if (!isDev) { devAsked = false; setDevStatus('', ''); } });
$('respawn').addEventListener('click', play);
$('toMenu').addEventListener('click', () => { showMenu(); connect(mode); });
// The mode buttons are the server's slots. Each slot shows the mode it is playing right now;
// a slot can switch to another mode when its game ends.
$('modes').addEventListener('click', ev => {
  const b = ev.target.closest('button');
  if (!b) return;
  for (const o of document.querySelectorAll('#modes button')) o.classList.toggle('on', o === b);
  $('modeInfo').textContent = '';
  connect(b.dataset.mode);
});
function renderSlots(arenas) {
  const ids = Object.keys(arenas).sort((a, b) => a - b);
  if (!ids.length) return;
  const box = $('modes');
  const have = [...box.querySelectorAll('button')].map(b => b.dataset.mode);
  if (have.join() !== ids.join()) {  // the server runs a different set of slots than the page shows
    box.textContent = '';
    for (const id of ids) {
      const b = document.createElement('button');
      b.dataset.mode = id;
      box.appendChild(b);
    }
    if (!ids.includes(mode)) connect(ids[0]);
  }
  for (const b of box.querySelectorAll('button')) {
    const a = arenas[b.dataset.mode];
    b.textContent = a.players ? `${a.title} · ${a.players}` : a.title;
    b.classList.toggle('on', b.dataset.mode === mode);
  }
}
function refreshSlots() {
  fetch('/status').then(r => r.json()).then(j => renderSlots(j.arenas || {})).catch(() => { /* server busy or restarting */ });
}
setInterval(() => { if (state !== 'play') refreshSlots(); }, 4000);
refreshSlots();

// ------------------------------------------------------------------ extra controls
function maxStat(i) {  // hold the max-stat key and press a stat's number: every point that fits goes into it
  const d = defs[me.def] || {}, cap = d.cap || 9, mask = d.sm === undefined ? 1023 : d.sm;
  if (!((mask >> i) & 1)) return;
  for (let n = Math.min(me.pts, cap - me.skills[i]); n > 0; n--) ws.send(new Uint8Array([2, i]));
}
function saveBlob(blob, name) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 5000);
}
const stamp = () => new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-');
function screenshot() {
  canvas.toBlob(b => { if (b) saveBlob(b, `polyrift-${stamp()}.png`); });
  pushMessage('Screenshot saved');
}
function toggleRecord() {
  if (recorder) { recorder.stop(); return; }
  if (!canvas.captureStream || !window.MediaRecorder) { pushMessage('This browser cannot record video'); return; }
  const chunks = [];
  let rec;
  try { rec = new MediaRecorder(canvas.captureStream(30)); } catch (e) { pushMessage('This browser cannot record video'); return; }
  rec.ondataavailable = ev => { if (ev.data.size) chunks.push(ev.data); };
  rec.onstop = () => {
    saveBlob(new Blob(chunks, { type: rec.mimeType || 'video/webm' }), `polyrift-${stamp()}.webm`);
    recorder = null;
    pushMessage('Recording saved');
  };
  rec.start();
  recorder = rec;
  pushMessage(`Recording… press ${keyLabel(binds.record)} again to stop`);
}

// ------------------------------------------------------------------ the Controls panel (key bindings)
let rebinding = null;  // the action waiting for its new key
const FIXED_KEYS = /^(Key[WASD]|Arrow.*|Space|Enter|Escape|Shift.*|Digit\d|Numpad\d|Meta.*|Tab)$/;
const esc = t => String(t).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
function renderKeys() {
  const grid = $('keysGrid');
  grid.textContent = '';
  const count = {};
  for (const a in binds) count[binds[a]] = (count[binds[a]] || 0) + 1;
  for (const a in DEFAULT_BINDS) {
    const name = document.createElement('span');
    name.textContent = BIND_NAMES[a];
    const b = document.createElement('button');
    b.type = 'button';
    b.dataset.action = a;
    b.textContent = rebinding === a ? 'press a key' : keyLabel(binds[a]);
    if (rebinding === a) b.className = 'wait';
    else if (count[binds[a]] > 1) { b.className = 'dup'; b.title = 'Shared with another action'; }
    grid.append(name, b);
  }
  const k = a => `<b>${esc(keyLabel(binds[a]))}</b>`;
  $('help').innerHTML =
    `<b>WASD</b> move · <b>Mouse</b> aim · <b>Click / Space</b> shoot · <b>Right-click / Shift</b> second weapon, repel drones, zoom, mirror the wave<br>` +
    `${k('autofire')} autofire · ${k('autospin')} autospin · ${k('override')} override · ${k('reverseTank')} reverse tank · ${k('reverseMouse')} reverse mouse<br>` +
    `<b>1–0</b> spend skill points · hold ${k('maxStat')} + number to max a stat · ${UP_ACTIONS.map(k).join(' ')} pick an upgrade<br>` +
    `Hold ${k('levelup')} level up · hold ${k('tree')} class tree · ${k('dominator')} control a dominator or your Mothership · hold ${k('selfDestruct')} self-destruct (only while spawn-protected)<br>` +
    `<b>Sandbox:</b> ${k('tree')} any class · ${k('sandShape')} spawn the chosen shape · ${k('sandBoss')} spawn the chosen boss<br>` +
    `<b>Enter</b> chat · ${k('chatToggle')} hide or show chat · ${k('killBar')} kill bar · ${k('latency')} latency · ${k('screenshot')} screenshot · ${k('record')} record video<br>` +
    `<b>Touch:</b> left thumb moves, right thumb aims and shoots; buttons beside the map for the second weapon and chat`;
}
function saveKeys() {
  try { localStorage.setItem('polyrift-keys', JSON.stringify(binds)); } catch (e) { /* storage unavailable */ }
}
$('keysBtn').addEventListener('click', () => {
  const open = $('keysPanel').hidden;
  $('keysPanel').hidden = !open;
  $('help').hidden = open;  // the panel lists the same keys, so the summary above it makes way
  $('keysBtn').textContent = open ? 'Hide controls' : 'Controls';
  rebinding = null;
  renderKeys();
  // The menu is taller than most screens once the panel is open: bring the panel into view
  // instead of leaving its lower rows below the edge of the screen.
  if (open) $('keysBtn').scrollIntoView({ block: 'start', behavior: 'smooth' });
});
$('keysGrid').addEventListener('click', ev => {
  const b = ev.target.closest('button');
  if (!b) return;
  rebinding = rebinding === b.dataset.action ? null : b.dataset.action;
  renderKeys();
});
$('keysReset').addEventListener('click', () => { Object.assign(binds, DEFAULT_BINDS); rebinding = null; saveKeys(); renderKeys(); });
renderKeys();

window.addEventListener('keydown', ev => {
  if (rebinding) {  // the Controls panel is waiting for a key
    ev.preventDefault();
    if (ev.code !== 'Escape' && !FIXED_KEYS.test(ev.code)) { binds[rebinding] = ev.code; saveKeys(); }
    rebinding = null;
    renderKeys();
    return;
  }
  const enter = ev.code === 'Enter' || ev.code === 'NumpadEnter';
  if (enter && state !== 'play') { play(); ev.preventDefault(); return; }
  if (state !== 'play' || chatOpen || document.activeElement === $('name')) return;
  if (enter) { if (!ev.repeat) openChat(); ev.preventDefault(); return; }
  if (ev.repeat) { if (ev.code === 'Space' || ev.code.startsWith('Arrow')) ev.preventDefault(); return; }
  keys[ev.code] = true;
  const is = action => ev.code === binds[action];
  // Upgrade hotkeys come first: while that many choices are on screen, the key picks one
  // (so by default H is "upgrade 4" when there are four or more, and the dominator key otherwise).
  const up = UP_ACTIONS.findIndex(is);
  if (up >= 0 && up < me.ups.length && me.alive && !(freePick() && devTree)) { sendUpgrade(me.ups[up]); return; }
  if (is('tree') && freePick()) devTree = !devTree;
  if (world && world.mode === 'sandbox' && me.alive && !devTree) {  // Sandbox: spawn what the panel is set to, under the cursor
    if (is('sandShape')) sandShape(true);
    if (is('sandBoss')) sandBoss(true);
  }
  if (is('dominator') && isOpen() && me.alive) ws.send(new Uint8Array([6]));  // control / leave a dominator
  if (is('override')) {
    override = !override;
    pushMessage(override ? 'Override on: turrets and drones wait for your command' : 'Override off: turrets and drones act on their own');
  }
  if (is('autofire')) { autofire = !autofire; pushMessage(`Autofire ${autofire ? 'on' : 'off'}`); }
  else if (is('autospin')) {
    autospin = !autospin;
    spinAngle = Math.atan2(mouse.y - ch / 2, mouse.x - cw / 2);
    pushMessage(`Autospin ${autospin ? 'on' : 'off'}`);
  } else if (/^(Digit|Numpad)\d$/.test(ev.code)) {
    const i = (Number(ev.code.slice(-1)) + 9) % 10;
    if (held('maxStat') && isOpen() && me.alive) maxStat(i);
    else sendSkill(i);
  }
  if (is('reverseTank')) { reverseTank = !reverseTank; pushMessage(`Reverse tank ${reverseTank ? 'on: your tank faces away from the cursor' : 'off'}`); }
  if (is('reverseMouse')) {
    reverseMouse = !reverseMouse;
    mouse.down = mouse.alt = false;
    pushMessage(`Reverse mouse ${reverseMouse ? 'on: right button shoots, left button is the second action' : 'off'}`);
  }
  if (is('selfDestruct') && me.alive && !destructAt) {  // only while spawn-protected (the server checks too)
    const mine = ents.get(me.id);
    if (mine && (mine.flags & 1)) destructAt = performance.now();
    else pushMessage('Self-destruct only works while spawn-protected: right after spawning, before you fire');
  }
  if (is('latency')) { showLatency = !showLatency; ping = -1; }
  if (is('killBar')) {
    killBar = !killBar;
    try { localStorage.setItem('polyrift-killbar', killBar ? '1' : '0'); } catch (e) { /* storage unavailable */ }
  }
  if (is('chatToggle')) {
    chatOn = !chatOn;
    if (!chatOn) { chatLog.length = 0; bubbles.clear(); }
    try { localStorage.setItem('polyrift-chat', chatOn ? '1' : '0'); } catch (e) { /* storage unavailable */ }
    pushMessage(chatOn ? 'Chat shown: press Enter to say something' : 'Chat hidden: you no longer see what other players say');
  }
  if (is('screenshot')) screenshot();
  if (is('record')) toggleRecord();
  if (ev.code === 'Space' || ev.code.startsWith('Arrow')) ev.preventDefault();
});
window.addEventListener('keyup', ev => { keys[ev.code] = false; });
window.addEventListener('blur', () => {
  for (const k in keys) keys[k] = false;
  mouse.down = mouse.alt = false;
});
canvas.addEventListener('mousemove', ev => { mouse.x = ev.clientX; mouse.y = ev.clientY; touch.used = false; });
// "Reverse mouse" swaps the jobs of the left and right buttons (the HUD is always clicked with the left one).
const mouseJob = button => (reverseMouse ? (button === 0 ? 2 : button === 2 ? 0 : button) : button);
canvas.addEventListener('mousedown', ev => {
  mouse.x = ev.clientX; mouse.y = ev.clientY;
  if (state !== 'play') return;
  if (ev.button === 0)
    for (const h of uiHits)
      if (mouse.x >= h.x && mouse.x <= h.x + h.w && mouse.y >= h.y && mouse.y <= h.y + h.h) { h.fn(); return; }
  const job = mouseJob(ev.button);
  if (job === 0) mouse.down = true;
  else if (job === 2) mouse.alt = true;
});
window.addEventListener('mouseup', ev => {
  const job = mouseJob(ev.button);
  if (job === 0) mouse.down = false;
  else if (job === 2) mouse.alt = false;
});
canvas.addEventListener('contextmenu', ev => ev.preventDefault());

// Touch: the left half of the screen is a movement stick, the right half an aim-and-fire stick.
canvas.addEventListener('touchstart', ev => {
  ev.preventDefault();
  touch.used = true;
  if (state !== 'play') return;
  for (const t of ev.changedTouches) {
    const x = t.clientX, y = t.clientY;
    const hit = uiHits.find(h => x >= h.x && x <= h.x + h.w && y >= h.y && y <= h.y + h.h);
    if (hit) { hit.fn(); continue; }
    const stick = { id: t.identifier, sx: x, sy: y, x, y };
    if (x < cw / 2 && !touch.move) touch.move = stick;
    else if (x >= cw / 2 && !touch.aim) touch.aim = stick;
  }
}, { passive: false });
canvas.addEventListener('touchmove', ev => {
  ev.preventDefault();
  for (const t of ev.changedTouches)
    for (const st of [touch.move, touch.aim])
      if (st && st.id === t.identifier) { st.x = t.clientX; st.y = t.clientY; }
}, { passive: false });
const touchEnd = ev => {
  for (const t of ev.changedTouches) {
    if (touch.move && touch.move.id === t.identifier) touch.move = null;
    if (touch.aim && touch.aim.id === t.identifier) touch.aim = null;
  }
};
canvas.addEventListener('touchend', touchEnd);
canvas.addEventListener('touchcancel', touchEnd);

connect(mode);
requestAnimationFrame(frame);
})();
