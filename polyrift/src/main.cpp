// Polyrift server entry point.
//   ./polyrift                      run the game server on http://localhost:3000
//   ./polyrift --help               list every option
//   ./polyrift --selftest 3000      no network: simulate every mode for N ticks and print stats
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "net.hpp"
#include "room.hpp"

static volatile std::sig_atomic_t g_stop = 0;
static void onSignal(int) { g_stop = 1; }

// Every playable class must be reachable from Basic through the upgrade tree.
static int checkTree() {
    std::vector<char> seen(TANKS.size(), 0);
    std::vector<int> todo{0};
    seen[0] = 1;
    while (!todo.empty()) {
        int i = todo.back();
        todo.pop_back();
        for (int u : TANKS[size_t(i)].upgrades)
            if (!seen[size_t(u)]) { seen[size_t(u)] = 1; todo.push_back(u); }
    }
    int playable = 0, lost = 0;
    for (size_t i = 0; i < TANKS.size(); i++) {
        if (TANKS[i].hidden) continue;
        playable++;
        if (!seen[i]) { lost++; std::printf("UNREACHABLE CLASS: %s\n", TANKS[i].name.c_str()); }
    }
    // Every class except the ramming ones must have something that shoots; a class with nothing
    // usually means a block of definitions was pasted into the wrong place.
    for (const TankDef& d : TANKS) {
        bool armed = !d.turrets.empty();
        for (const GunDef& g : d.guns)
            if (g.type != G_DECOR) armed = true;
        if (!armed && d.ai != AI_RAMMER) { lost++; std::printf("CLASS WITHOUT WEAPONS: %s\n", d.name.c_str()); }
    }
    std::printf("classes: %d playable, %zu computer-controlled, %d unreachable\n", playable, TANKS.size() - size_t(playable), lost);
    return lost;
}

// Mode names as typed in --slots.
static bool parseMode(const std::string& name, Mode& out) {
    static const std::pair<const char*, Mode> names[] = {{"ffa", M_FFA}, {"maze", M_MAZE}, {"2tdm", M_TDM2}, {"4tdm", M_TDM4},
                                                         {"dom", M_DOM}, {"mazedom", M_MAZEDOM}, {"rush", M_RUSH}};
    for (const auto& n : names)
        if (name == n.first) { out = n.second; return true; }
    return false;
}

// "ffa/maze,2tdm/4tdm,dom/mazedom,rush" -> one list of modes per slot. Empty result = bad input.
static std::vector<std::vector<Mode>> parseSlots(const std::string& spec) {
    std::vector<std::vector<Mode>> slots(1);
    std::string word;
    auto flush = [&]() {
        Mode m;
        if (!parseMode(word, m)) return false;
        slots.back().push_back(m);
        word.clear();
        return true;
    };
    for (char ch : spec) {
        if (ch == '/' || ch == '|') { if (!flush()) return {}; }
        else if (ch == ',') { if (!flush()) return {}; slots.emplace_back(); }
        else word.push_back(ch);
    }
    if (!flush() || slots.size() > 12) return {};
    return slots;
}

// A slot that keeps switching mode: close the arena again and again and check every restart works.
static int rotationTest(int ticks) {
    Room room(std::vector<Mode>{M_FFA, M_MAZE, M_TDM2, M_TDM4, M_DOM, M_MAZEDOM, M_RUSH}, 4321u);
    room.bossEvery = 300;
    room.send = [](Client&, const std::string&, bool) {};
    Client c;
    room.addClient(&c);
    Rng rng(7);
    std::string seen;
    int rounds = 0;
    for (int t = 0; t < ticks * 6 && rounds < 10; t++) {
        if (c.tankId == 0 && t % 30 == 0) {
            const uint8_t spawn[] = {0, 'R', 'o', 't'};
            room.onMessage(c, spawn, sizeof(spawn));
        }
        if (t % 15 == 0) {
            ByteWriter w;
            w.u8(1);
            w.u8(uint32_t(rng.irange(0, 15)) | 16 | 64);
            w.f32(rng.range(-400, 400));
            w.f32(rng.range(-400, 400));
            room.onMessage(c, (const uint8_t*)w.buf.data(), w.buf.size());
        }
        if (!room.closing() && t % 400 == 399) room.closeArena("");
        room.tick();
        if (room.statRounds != rounds) {
            rounds = room.statRounds;
            seen += (seen.empty() ? "" : " > ") + room.key;
        }
    }
    std::printf("slot rotation: %d restarts: %s\n", rounds, seen.c_str());
    room.removeClient(&c);
    return rounds >= 3 ? 0 : 1;
}

// The scoring and stat rules, checked on a small arena with scripted players: score shared by
// damage, kills and assists, the death summary, the Smasher line's stats, and Assembler blocks.
static int rulesTest() {
    int bad = 0;
    auto fail = [&](const char* what) { std::printf("RULE CHECK FAILED: %s\n", what); bad++; };
    Room::mutationDefault = 0;
    Room room(M_FFA, 2024u);
    Room::mutationDefault = 100;
    room.setBots(0);
    room.bossEvery = 0;
    room.startScore = levelScore(MAX_LEVEL);
    std::map<Client*, std::string> inbox;
    room.send = [&](Client& c, const std::string& d, bool binary) { if (!binary) inbox[&c] += d + "\n"; };
    Client ann, bea, vic, sam, mia;
    const std::pair<Client*, const char*> everyone[] = {{&ann, "Ann"}, {&bea, "Bea"}, {&vic, "Vic"}, {&sam, "Sam"}, {&mia, "Mia"}};
    for (const auto& who : everyone) {
        room.addClient(who.first);
        std::string m = std::string(1, '\0') + who.second;
        room.onMessage(*who.first, (const uint8_t*)m.data(), m.size());
    }
    room.tick();
    auto tank = [&](Client& c) -> Entity* {
        for (Entity* e : room.ents)
            if (e->id == c.tankId && !e->dead) return e;
        return nullptr;
    };
    auto input = [&](Client& c, Vec2 aim, bool fire) {
        ByteWriter w;
        w.u8(1);
        w.u8(fire ? 16 : 0);
        w.f32(aim.x);
        w.f32(aim.y);
        room.onMessage(c, (const uint8_t*)w.buf.data(), w.buf.size());
    };
    auto become = [&](Client& c, const char* cls) {
        ByteWriter u;
        u.u8(3);
        u.u16(uint32_t(tankIndex(cls)));
        room.onMessage(c, (const uint8_t*)u.buf.data(), u.buf.size());
    };
    auto spend = [&](Client& c, int stat, int times) {
        const uint8_t m[] = {2, uint8_t(stat)};
        for (int i = 0; i < times; i++) room.onMessage(c, m, 2);
    };
    Entity *ea = tank(ann), *eb = tank(bea), *ev = tank(vic), *es = tank(sam), *em = tank(mia);
    if (!ea || !eb || !ev || !es || !em) { fail("players spawn"); return bad; }

    // --- Smasher line: bullet points come back, five stats, twelve points each, Reload is its engine
    spend(sam, S_BDMG, 5);
    if (es->tank->skills[S_BDMG] != 5) fail("a Basic can spend on Bullet Damage");
    become(sam, "Smasher");
    if (TANKS[size_t(es->tank->def)].name != "Smasher") fail("Basic upgrades to Smasher");
    if (es->tank->skills[S_BDMG] != 0 || es->tank->skillPts != 42) fail("becoming a Smasher hands back the bullet points");
    spend(sam, S_BODY, 14);
    spend(sam, S_BSPEED, 3);
    spend(sam, S_MOVE, 3);
    spend(sam, S_RELOAD, 3);
    if (es->tank->skills[S_BODY] != 12) fail("Smasher stats stop at 12");
    if (es->tank->skills[S_BSPEED] != 0 || es->tank->skills[S_MOVE] != 0) fail("a Smasher has no bullet or movement stats");
    if (es->tank->skills[S_RELOAD] != 3) fail("a Smasher can raise Engine Acceleration");
    es->pos = {600, 600};

    // --- one pentagon for the damage-share check; every other polygon is cleared out of the way
    Entity* pent = nullptr;
    for (Entity* e : room.ents) {
        if (e->kind != K_FOOD) continue;
        if (!pent && e->shape == F_PENTAGON && e->tier == T_NORMAL && !e->crasher) pent = e;
        else e->dead = true;
    }
    if (!pent) { fail("a pentagon exists"); return bad; }
    auto park = [&]() {  // Ann and Bea stand either side of the middle; Vic and Max wait far away
        ea->pos = {2250, 2500}; eb->pos = {2750, 2500};
        ea->vel = eb->vel = {0, 0};
    };
    ev->pos = {4200, 600};
    em->pos = {600, 4200};
    pent->pos = {2500, 2500};
    pent->vel = {0, 0};
    double a0 = ea->tank->score, b0 = eb->tank->score;
    const int xp = pent->xp;
    const uint32_t pentId = pent->id;
    auto alive = [&](uint32_t id) -> Entity* {  // entities are freed once destroyed, so look them up afresh
        for (Entity* e : room.ents)
            if (e->id == id && !e->dead) return e;
        return nullptr;
    };
    for (int t = 0; t < 900 && (pent = alive(pentId)) != nullptr; t++) {
        park();
        pent->pos = {2500, 2500};
        input(ann, pent->pos - ea->pos, true);
        input(bea, pent->pos - eb->pos, true);
        room.tick();
    }
    double ga = ea->tank->score - a0, gb = eb->tank->score - b0;
    if (ga <= 0 || gb <= 0) fail("both players who damaged a polygon are paid");
    if (std::abs(ga + gb - xp) > 1.0) fail("a polygon's score is shared out exactly");
    if (ea->tank->polys + eb->tank->polys != 1) fail("the polygon counts as destroyed for whoever finished it");

    // --- a kill and an assist: both fire at Vic until he dies
    a0 = ea->tank->score;
    b0 = eb->tank->score;
    const double pool = std::max(20.0, std::min(ev->tank->score, 24000.0) * 0.5);
    for (int t = 0; t < 3000 && vic.tankId; t++) {
        park();
        ev->pos = {2500, 2500};
        ev->vel = {0, 0};
        input(ann, ev->pos - ea->pos, true);
        input(bea, ev->pos - eb->pos, true);
        room.tick();
    }
    if (vic.tankId) { fail("the target is destroyed"); return bad; }
    ga = ea->tank->score - a0;
    gb = eb->tank->score - b0;
    if (ga <= 0 || gb <= 0 || std::abs(ga + gb - pool) > 2.0) fail("a kill's score is shared by damage");
    if (ea->tank->kills + eb->tank->kills != 1 || ea->tank->assists + eb->tank->assists != 1) fail("one kill and one assist are counted");
    Client& killer = ea->tank->kills ? ann : bea;
    Client& helper = ea->tank->kills ? bea : ann;
    if (inbox[&killer].find("You killed Vic's Basic.") == std::string::npos) fail("the killer is told");
    if (inbox[&helper].find("You assisted in killing Vic's Basic.") == std::string::npos) fail("the helper is told");
    const std::string& dead = inbox[&vic];
    if (dead.find("\"t\":\"dead\"") == std::string::npos || dead.find("\"assists\":0") == std::string::npos ||
        dead.find("\"polys\":0") == std::string::npos || dead.find("\"bosses\":0") == std::string::npos ||
        dead.find(std::string("\"helpers\":[\"") + (&helper == &ann ? "Ann" : "Bea") + "\"]") == std::string::npos)
        fail("the death summary lists kills, assists, bosses, polygons and who helped");

    // --- Assembler: blocks fired onto the same spot fuse
    become(mia, "Pounder");
    become(mia, "Builder");
    become(mia, "Assembler");
    if (TANKS[size_t(em->tank->def)].name != "Assembler") fail("Builder upgrades to Assembler");
    for (int t = 0; t < 400; t++) {
        em->pos = {600, 4200};
        em->vel = {0, 0};
        input(mia, {0, -200}, true);
        room.tick();
    }
    if (room.statMerges == 0) fail("Assembler blocks merge");

    // --- self-destruct: refused once spawn protection is over, accepted while it lasts
    const uint8_t boom[] = {7};
    room.onMessage(sam, boom, 1);
    room.tick();
    if (!sam.tankId) fail("self-destruct is refused after spawn protection ends");
    Client zed;
    room.addClient(&zed);
    const uint8_t spawnZed[] = {0, 'Z', 'e', 'd'};
    room.onMessage(zed, spawnZed, sizeof(spawnZed));
    room.tick();
    if (!zed.tankId) fail("a new player spawns");
    room.onMessage(zed, boom, 1);
    room.tick();
    if (zed.tankId || inbox[&zed].find("self-destruct") == std::string::npos) fail("self-destruct works while spawn-protected");
    room.removeClient(&zed);
    // Standing still keeps the protection (and with it self-destruct) for up to twenty seconds;
    // a tank that has moved loses it after three.
    {
        Client still, walker, idler;
        for (Client* c : {&still, &walker, &idler}) {
            room.addClient(c);
            const uint8_t sp[] = {0, 'N', 'e', 'w'};
            room.onMessage(*c, sp, sizeof(sp));
        }
        for (int t = 0; t < 5 * TICK_RATE; t++) {
            ByteWriter w;  // the walker holds "right"
            w.u8(1);
            w.u8(8);
            w.f32(100);
            w.f32(0);
            room.onMessage(walker, (const uint8_t*)w.buf.data(), w.buf.size());
            room.tick();
        }
        room.onMessage(still, boom, 1);
        room.onMessage(walker, boom, 1);
        room.tick();
        if (still.tankId) fail("a tank that stood still can self-destruct five seconds after spawning");
        if (!walker.tankId) fail("a tank that moved cannot self-destruct five seconds after spawning");
        for (int t = 0; t < 17 * TICK_RATE; t++) room.tick();
        room.onMessage(idler, boom, 1);
        room.tick();
        if (!idler.tankId) fail("spawn protection ends after twenty seconds even for a tank standing still");
        for (Client* c : {&still, &walker, &idler}) room.removeClient(c);
    }
    for (const auto& who : everyone) room.removeClient(who.first);

    // --- secondary guns: an Eagle's thrusters follow the fire button, its front barrel follows alt-fire
    {
        Client ed, zoe, kit;
        for (const auto& who : {std::pair<Client*, const char*>{&ed, "Ed"}, {&zoe, "Zoe"}, {&kit, "Kit"}}) {
            room.addClient(who.first);
            std::string m = std::string(1, '\0') + who.second;
            room.onMessage(*who.first, (const uint8_t*)m.data(), m.size());
        }
        room.tick();
        Entity* ee = tank(ed);
        if (!ee) { fail("the secondary-gun tester spawns"); return bad; }
        for (const char* step : {"Flank Guard", "Tri-Angle", "Eagle"}) become(ed, step);
        const TankDef& eagle = TANKS[size_t(ee->tank->def)];
        if (eagle.name != "Eagle" || !eagle.altFire || !eagle.guns[0].alt || eagle.guns[1].alt) fail("the Eagle's front barrel is a secondary gun");
        for (const char* n : {"Falcon", "Vulture", "Phoenix"})
            if (!TANKS[size_t(tankIndex(n))].altFire) fail("Falcon, Vulture and Phoenix have a secondary gun");
        for (const char* n : {"Basic", "Tri-Angle", "Trap Guard", "Hunter", "Desmos", "Overseer"})
            if (TANKS[size_t(tankIndex(n))].altFire) fail("ordinary classes have no secondary gun");
        ee->tank->invuln = 0;
        // Count the bullets an Eagle launches in two seconds: big ones come from the front barrel.
        auto volley = [&](uint32_t flags, int& front, int& rear) {
            front = rear = 0;
            std::set<uint32_t> seen;
            for (Entity* e : room.ents)
                if (e->kind == K_BULLET && e->owner == ee->id) seen.insert(e->id);
            for (int t = 0; t < 2 * TICK_RATE; t++) {
                ee->pos = {1200, 1200};
                ee->vel = {0, 0};
                ByteWriter w;
                w.u8(1);
                w.u8(flags);
                w.f32(300);
                w.f32(0);
                room.onMessage(ed, (const uint8_t*)w.buf.data(), w.buf.size());
                room.tick();
                for (Entity* e : room.ents)
                    if (e->kind == K_BULLET && e->owner == ee->id && seen.insert(e->id).second) (e->vel.x > 0 ? front : rear)++;
            }
            for (int t = 0; t < TICK_RATE; t++) {  // let go of everything
                ByteWriter w;
                w.u8(1); w.u8(0); w.f32(300); w.f32(0);
                room.onMessage(ed, (const uint8_t*)w.buf.data(), w.buf.size());
                room.tick();
            }
        };
        int front = 0, rear = 0;
        volley(16, front, rear);
        if (front != 0 || rear < 4) fail("the fire button fires an Eagle's thrusters and not its front barrel");
        volley(32, front, rear);
        if (front < 1 || rear != 0) fail("alt-fire fires an Eagle's front barrel and not its thrusters");
        volley(16 | 32, front, rear);
        if (front < 1 || rear < 4) fail("both buttons fire everything");

        // --- chat: cleaned, delivered to everyone, rate-limited, only from a tank in the arena
        auto say = [&](Client& c, const std::string& text) {
            std::string m = std::string(1, '\x09') + text;
            room.onMessage(c, (const uint8_t*)m.data(), m.size());
        };
        auto got = [&](Client& c, const char* what) { return inbox[&c].find(what) != std::string::npos; };
        inbox.clear();
        say(zoe, "  hello \x01\x02  \"there\"\n<b>  ");
        if (!got(ed, "\"t\":\"chat\"") || !got(kit, "\"name\":\"Zoe\",\"text\":\"hello \\\"there\\\" <b>\"") || !got(zoe, "\"t\":\"chat\""))
            fail("a chat line is cleaned up and reaches everyone in the arena, the sender included");
        inbox.clear();
        say(zoe, std::string(300, 'x'));
        if (!got(ed, (std::string(CHAT_MAX_CHARS, 'x') + "\"").c_str()) || got(ed, std::string(CHAT_MAX_CHARS + 1, 'x').c_str()))
            fail("a long chat line is cut to the limit");
        inbox.clear();
        say(zoe, std::string(4 * CHAT_MAX_CHARS + 1, 'y'));
        say(zoe, " \x01 \xe2\x80\x8b ");  // nothing but spaces and invisible characters
        if (got(ed, "\"t\":\"chat\"")) fail("oversized and empty chat lines are dropped");
        say(zoe, "three");
        inbox.clear();
        say(zoe, "four");
        if (got(ed, "four") || !got(zoe, "too fast")) fail("the fourth chat line in a row is refused");
        for (int t = 0; t < 2 * TICK_RATE + 2; t++) room.tick();
        inbox.clear();
        say(zoe, "five");
        if (!got(ed, "five")) fail("a chat line is allowed again two seconds later");
        for (int t = 0; t < 3 * TICK_RATE; t++) room.tick();
        inbox.clear();
        say(zoe, "five");
        if (got(ed, "\"t\":\"chat\"") || !got(zoe, "just said")) fail("repeating the same chat line is refused");
        Client menu;  // connected, but still on the start screen
        room.addClient(&menu);
        inbox.clear();
        say(menu, "boo");
        if (got(ed, "boo")) fail("only a tank in the arena can chat");
        for (int t = 0; t < 8 * TICK_RATE; t++) room.tick();
        room.chat = false;
        inbox.clear();
        say(kit, "anyone?");
        if (got(ed, "anyone")) fail("--no-chat turns chat off");
        room.chat = true;
        for (Client* c : {&ed, &zoe, &kit, &menu}) room.removeClient(c);
    }

    // --- Team Polygon: neutral dominators and the arena closers of a closing arena
    {
        Room dom(M_DOM, 99u);
        dom.send = [](Client&, const std::string&, bool) {};
        dom.tick();
        int doms = 0, closers = 0;
        for (Entity* e : dom.ents)
            if (e->kind == K_TANK && e->tank->role == R_DOM) { doms++; if (e->team != TEAM_FOOD) fail("a neutral dominator is on Team Polygon"); }
        dom.closeArena("");
        dom.tick();
        for (Entity* e : dom.ents)
            if (e->kind == K_TANK && e->tank->role == R_CLOSER) { closers++; if (e->team != TEAM_FOOD || !e->ghost) fail("an arena closer is on Team Polygon"); }
        if (doms != 4 || closers < 3) fail("dominators and arena closers exist");
    }
    {   // a fallen sanctuary joins Team Polygon; restoring it gives it back to the players' team
        Room rush(M_RUSH, 77u);
        rush.send = [](Client&, const std::string&, bool) {};
        rush.setBots(0);
        rush.tick();
        Entity* s = nullptr;
        for (Entity* e : rush.ents)
            if (e->kind == K_TANK && e->tank->role == R_SANCT) s = e;
        if (!s || s->team != 0) fail("a standing sanctuary is on the players' team");
        if (s) {
            s->hp = 0;
            rush.tick();
            if (s->dead || s->team != TEAM_FOOD) fail("a fallen sanctuary is on Team Polygon");
            s->hp = 0;
            rush.tick();
            if (s->dead || s->team != 0) fail("a restored sanctuary is back on the players' team");
        }
    }
    std::printf("rules: %s (shared score, kill + assist, death summary, Smasher stats, Assembler merge, self-destruct, Team Polygon, secondary guns, chat)\n", bad ? "FAILED" : "ok");
    return bad;
}

static int selfTest(int ticks, int startLevel) {
    int bad = checkTree();
    bad += rulesTest();
    bad += rotationTest(ticks);
    const Mode modes[7] = {M_FFA, M_MAZE, M_TDM2, M_TDM4, M_DOM, M_MAZEDOM, M_RUSH};
    for (int i = 0; i < 7; i++) {
        Room::mutationDefault = (i % 2) ? 20000 : 100;  // every other mode is full of mutated polygons
        Room room(modes[i], 1234u + uint32_t(i));
        room.startScore = levelScore(startLevel);
        room.bossEvery = std::max(200, ticks / 4);  // make sure bosses show up during the test
        room.devCode = "selftest";
        size_t bytes = 0, packets = 0;
        room.send = [&](Client&, const std::string& d, bool) { bytes += d.size(); packets++; };
        Client c;
        room.addClient(&c);
        Rng rng(99);
        auto t0 = std::chrono::steady_clock::now();
        for (int t = 0; t < ticks; t++) {
            // A fake player that respawns, wanders, shoots and spends upgrades at random.
            if (c.tankId == 0 && t % 60 == 0) {
                const uint8_t spawn[] = {0, 'T', 'e', 's', 't', 'e', 'r'};
                room.onMessage(c, spawn, sizeof(spawn));
            }
            if (t % 15 == 0) {
                ByteWriter w;
                w.u8(1);
                w.u8(uint32_t(rng.irange(0, 15)) | 16 | 64);  // moving, firing, level-up key held
                w.f32(rng.range(-400, 400));
                w.f32(rng.range(-400, 400));
                room.onMessage(c, (const uint8_t*)w.buf.data(), w.buf.size());
                const uint8_t skill[] = {2, uint8_t(rng.irange(0, 9))};
                room.onMessage(c, skill, 2);
                ByteWriter u;
                u.u8(3);
                u.u16(uint32_t(rng.irange(0, int(TANKS.size()) - 1)));
                room.onMessage(c, (const uint8_t*)u.buf.data(), u.buf.size());
            }
            if (t == ticks * 2 / 5) room.closeArena("");  // exercise the arena closers and the restart
            if (t % 240 == 120) {  // the "control a dominator" key (only does something in Domination)
                const uint8_t h[] = {6};
                room.onMessage(c, h, 1);
            }
            room.tick();
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const size_t bytesPlayed = bytes;
        // Developer mode: a wrong code must be refused, the right one must let the player drive every
        // class in the game, the computer-controlled ones included.
        {
            const uint8_t wrong[] = {5, 'n', 'o'};
            room.onMessage(c, wrong, sizeof(wrong));
            if (c.dev) { std::printf("DEVELOPER CODE CHECK FAILED\n"); bad++; }
            const uint8_t right[] = {5, 's', 'e', 'l', 'f', 't', 'e', 's', 't'};
            room.onMessage(c, right, sizeof(right));
            if (!c.dev) { std::printf("DEVELOPER CODE NOT ACCEPTED\n"); bad++; }
            size_t driven = 0;
            for (size_t d = 0; d < TANKS.size() && c.dev; d++) {
                for (int k = 0; k < 12; k++) {
                    if (c.tankId == 0) {
                        const uint8_t spawn[] = {0, 'D', 'e', 'v'};
                        room.onMessage(c, spawn, sizeof(spawn));
                    }
                    if (k == 1) {
                        ByteWriter u;
                        u.u8(3);
                        u.u16(uint32_t(d));
                        room.onMessage(c, (const uint8_t*)u.buf.data(), u.buf.size());
                        ByteWriter w;
                        w.u8(1);
                        w.u8(16 | 8);
                        w.f32(300);
                        w.f32(40);
                        room.onMessage(c, (const uint8_t*)w.buf.data(), w.buf.size());
                    }
                    room.tick();
                }
                driven++;
            }
            std::printf("      developer mode drove %zu classes\n", driven);
        }
        // Nothing may be left on a team of its own: every entity is on a player team or on Team Polygon.
        for (Entity* e : room.ents)
            if (!e->dead && e->team < 0 && e->team != TEAM_FOOD) {
                std::printf("ENTITY ON A STRAY TEAM %d (kind %d)\n", e->team, int(e->kind));
                bad++;
                break;
            }
        std::printf("%s\n      %.3f ms/tick, %.1f KB/s to one client\n", room.summary().c_str(), ms / ticks,
                    double(bytesPlayed) / 1024.0 / (double(ticks) / TICK_RATE));
        room.removeClient(&c);
    }
    return bad ? 1 : 0;
}

static void usage(const char* exe) {
    std::printf(
        "usage: %s [options]\n"
        "  --port N           port to listen on (default 3000, or the PORT environment variable)\n"
        "  --bind ADDR        address to listen on: 0.0.0.0 = everyone (default), 127.0.0.1 = this machine only\n"
        "  --public DIR       folder with the web client (default ./public)\n"
        "  --bots N           bots per arena when it is empty; they make room as players join (default 14-16)\n"
        "  --max-players N    players allowed in one arena (default 40)\n"
        "  --max-per-ip N     connections allowed from one address, 0 = unlimited (default 6)\n"
        "  --trust-proxy      read the player's address from X-Forwarded-For (only behind your own reverse proxy)\n"
        "  --start-level N    sandbox: every tank spawns at this level (1-45)\n"
        "  --free-level N     holding the level-up key (N) raises a tank to this level for free; 0 = off (default 45)\n"
        "  --boss-minutes N   minutes between boss visits, 0 = no bosses (default 6)\n"
        "  --mutations N      how common mutated polygons are (shiny, legendary, shadow, rainbow, trans, polyhedra):\n"
        "                     a multiplier on the base odds of 1 in 50,000 for shiny. Default 100 (1 shiny in 500);\n"
        "                     0 = none, 1 = the base odds, 20000 = see them all within minutes\n"
        "  --arena-minutes N  FFA, Maze and Teams arenas close and restart after this many minutes, 0 = never\n"
        "                     (default 45 while any slot has more than one mode, otherwise 0)\n"
        "  --no-chat          players cannot chat (by default they can: Enter, one line of up to 80 characters)\n"
        "  --dev-code CODE    code that unlocks the Developer tank (default 1234; \"off\" = nobody can)\n"
        "  --slots LIST       arenas to run. Slots are separated by commas; a slot with several modes separated\n"
        "                     by / picks one at random for every new game. Modes: ffa maze 2tdm 4tdm dom mazedom rush\n"
        "                     (default ffa/maze,2tdm/4tdm,dom/mazedom,rush)\n"
        "  --selftest TICKS   simulate every mode without networking and print statistics\n",
        exe);
}

static bool fileExists(const std::string& p) { return bool(std::ifstream(p)); }

int main(int argc, char** argv) {
    int port = 3000;
    if (const char* env = std::getenv("PORT")) port = std::atoi(env);
    std::string publicDir = "public", bindAddr = "0.0.0.0";
    int selftest = 0, startLevel = 1, bots = -1, maxPlayers = 40, maxPerIp = 6;
    int freeLevel = MAX_LEVEL, bossMinutes = 6, arenaMinutes = 0;
    bool trustProxy = false;
    std::string devCode = "1234", slotSpec = "ffa/maze,2tdm/4tdm,dom/mazedom,rush";
    bool arenaMinutesSet = false;
    double mutations = 100;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto num = [&](int lo, int hi) { return std::clamp(std::atoi(argv[++i]), lo, hi); };
        if (a == "--port" && i + 1 < argc) port = num(1, 65535);
        else if (a == "--public" && i + 1 < argc) publicDir = argv[++i];
        else if (a == "--bind" && i + 1 < argc) bindAddr = argv[++i];
        else if (a == "--bots" && i + 1 < argc) bots = num(0, 60);
        else if (a == "--max-players" && i + 1 < argc) maxPlayers = num(1, 500);
        else if (a == "--max-per-ip" && i + 1 < argc) maxPerIp = num(0, 100000);
        else if (a == "--trust-proxy") trustProxy = true;
        else if (a == "--dev-code" && i + 1 < argc) devCode = argv[++i];
        else if (a == "--no-chat") Room::chatDefault = false;
        else if (a == "--start-level" && i + 1 < argc) startLevel = num(1, MAX_LEVEL);
        else if (a == "--free-level" && i + 1 < argc) freeLevel = num(0, MAX_LEVEL);
        else if (a == "--boss-minutes" && i + 1 < argc) bossMinutes = num(0, 100000);
        else if (a == "--mutations" && i + 1 < argc) mutations = std::clamp(std::atof(argv[++i]), 0.0, 1e9);
        else if (a == "--arena-minutes" && i + 1 < argc) { arenaMinutes = num(0, 100000); arenaMinutesSet = true; }
        else if (a == "--slots" && i + 1 < argc) slotSpec = argv[++i];
        else if (a == "--selftest" && i + 1 < argc) selftest = num(1, 100000000);
        else {
            usage(argv[0]);
            return a == "--help" ? 0 : 1;
        }
    }
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    buildTankDefs();
    if (selftest > 0) return selfTest(selftest, startLevel);

    // If the client folder isn't in the working directory, look next to the executable
    // (so double-clicking the program or running it from elsewhere still works).
    if (!fileExists(publicDir + "/index.html")) {
        std::string exe = argv[0];
        size_t slash = exe.find_last_of("/\\");
        std::string beside = (slash == std::string::npos ? std::string(".") : exe.substr(0, slash)) + "/public";
        if (fileExists(beside + "/index.html")) publicDir = beside;
        else std::fprintf(stderr, "warning: %s/index.html not found; the web client will not load (use --public DIR)\n", publicDir.c_str());
    }

    std::vector<std::vector<Mode>> slots = parseSlots(slotSpec);
    if (slots.empty()) {
        std::fprintf(stderr, "bad --slots value \"%s\". Example: --slots ffa/maze,2tdm/4tdm,dom/mazedom,rush\n", slotSpec.c_str());
        return 1;
    }
    // Slots that switch between modes need their games to end now and then, so unless told otherwise
    // the modes with no ending of their own (FFA, Maze, Teams) restart every 45 minutes.
    bool rotating = false;
    for (const auto& sl : slots)
        if (sl.size() > 1) rotating = true;
    if (rotating && !arenaMinutesSet) arenaMinutes = 45;

    uint32_t seed = uint32_t(std::chrono::system_clock::now().time_since_epoch().count());
    Room::mutationDefault = mutations;  // before the arenas are built, so their first polygons follow it too
    std::map<std::string, std::unique_ptr<Room>> rooms;
    for (size_t i = 0; i < slots.size(); i++) {
        auto r = std::make_unique<Room>(slots[i], seed + uint32_t(i) * 7919u);
        r->slot = std::to_string(i + 1);
        r->startScore = levelScore(startLevel);
        r->maxPlayers = maxPlayers;
        r->freeLevel = freeLevel;
        r->devCode = devCode == "off" ? "" : devCode;
        r->bossEvery = bossMinutes * 60 * TICK_RATE;
        r->closeEvery = arenaMinutes * 60 * TICK_RATE;
        if (bots >= 0) r->setBots(bots);
        rooms[r->slot] = std::move(r);
    }

    net::Server server;
    server.maxPerIp = maxPerIp;
    server.trustProxy = trustProxy;
    for (auto& kv : rooms)
        kv.second->send = [&server](Client& c, const std::string& data, bool binary) {
            if (c.conn) server.send(*static_cast<net::Conn*>(c.conn), data, binary);
        };

    server.onOpen = [&](net::Conn& conn) {
        // WebSocket path selects the slot: /ws/1, /ws/2, ...
        std::string key = conn.path.rfind("/ws/", 0) == 0 ? conn.path.substr(4) : "1";
        auto it = rooms.find(key);
        if (it == rooms.end()) it = rooms.find("1");
        Client* c = new Client();
        c->conn = &conn;
        conn.user = c;
        it->second->addClient(c);
        std::printf("[+] %s joined slot %s, %s (%zu connections)\n", conn.ip.c_str(), it->first.c_str(), it->second->title.c_str(), server.connections());
    };
    server.onMessage = [&](net::Conn& conn, const uint8_t* data, size_t n) {
        Client* c = static_cast<Client*>(conn.user);
        if (c && c->room) c->room->onMessage(*c, data, n);
    };
    server.onClose = [&](net::Conn& conn) {
        Client* c = static_cast<Client*>(conn.user);
        if (!c) return;
        c->conn = nullptr;
        if (c->room) c->room->removeClient(c);
        delete c;
        conn.user = nullptr;
        std::printf("[-] %s left\n", conn.ip.c_str());
    };
    // GET /status: a small JSON summary for uptime monitors and server lists.
    server.onStatus = [&]() {
        int total = 0;
        std::string arenas;
        for (auto& kv : rooms) {
            int p = kv.second->players();
            total += p;
            arenas += std::string(arenas.empty() ? "" : ",") + "\"" + kv.first + "\":{\"mode\":\"" + kv.second->key + "\",\"title\":\"" +
                      kv.second->title + "\",\"players\":" + std::to_string(p) +
                      ",\"watching\":" + std::to_string(int(kv.second->clients.size()) - p) + "}";
        }
        return "{\"players\":" + std::to_string(total) + ",\"connections\":" + std::to_string(server.connections()) +
               ",\"maxPlayersPerArena\":" + std::to_string(maxPlayers) + ",\"arenas\":{" + arenas + "}}";
    };

    if (!server.listen(port, publicDir, bindAddr)) {
        std::fprintf(stderr, "could not listen on %s port %d (port already in use, or bad --bind address?)\n", bindAddr.c_str(), port);
        return 1;
    }
    std::printf("Polyrift server running.  Open http://localhost:%d in a browser.  Ctrl+C stops it.\n", port);
    if (devCode == "1234")
        std::printf("note: the developer code is still the default one. Before strangers can reach this server,\n"
                    "      start it with  --dev-code SOMETHING-LONG-AND-SECRET  (or  --dev-code off).\n");
    std::fflush(stdout);

    using clock = std::chrono::steady_clock;
    const auto step = std::chrono::microseconds(1000000 / TICK_RATE);
    auto next = clock::now() + step;
    while (!g_stop) {
        auto now = clock::now();
        int waitMs = int(std::chrono::duration_cast<std::chrono::milliseconds>(next - now).count());
        server.poll(std::max(0, waitMs));
        now = clock::now();
        if (now >= next) {
            // Arenas with nobody connected are frozen, so an idle server uses no CPU.
            for (auto& kv : rooms)
                if (!kv.second->clients.empty()) kv.second->tick();
            next += step;
            if (now > next + step * 5) next = now + step;  // fell behind: don't try to catch up
            std::fflush(stdout);
        }
    }

    // Clean shutdown: tell everyone, then close. Browsers reconnect on their own once the server is back.
    for (auto& kv : rooms) kv.second->announce("Server is restarting. You will reconnect automatically.");
    server.shutdown();
    std::printf("stopped.\n");
    return 0;
}
