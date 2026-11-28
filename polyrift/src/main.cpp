// Polyrift server entry point.
//   ./polyrift                      run the game server on http://localhost:3000
//   ./polyrift --help               list every option
//   ./polyrift --selftest 3000      no network: simulate every mode for N ticks and print stats
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    static const std::pair<const char*, Mode> names[] = {
        {"ffa", M_FFA}, {"maze", M_MAZE}, {"2tdm", M_TDM2}, {"4tdm", M_TDM4}, {"dom", M_DOM}, {"mazedom", M_MAZEDOM}, {"rush", M_RUSH},
        {"sandbox", M_SANDBOX}, {"tag", M_TAG}, {"br", M_BR}, {"mother", M_MOTHER}, {"koth", M_KOTH}, {"siege", M_SIEGE}, {"ctf", M_CTF},
        {"endless", M_ENDLESS}};
    for (const auto& n : names)
        if (name == n.first) { out = n.second; return true; }
    return false;
}

// The slots a server runs when --slots is not given.
static const char* const DEFAULT_SLOTS = "ffa/maze,2tdm/4tdm,dom/mazedom/mother/siege/ctf,rush/endless,tag/br/koth,sandbox";

// "ffa/maze,2tdm/4tdm,rush" -> one list of modes per slot. Empty result = bad input.
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
    Room room(std::vector<Mode>{M_FFA, M_MAZE, M_TDM2, M_TDM4, M_DOM, M_MAZEDOM, M_RUSH, M_SANDBOX, M_TAG, M_BR, M_MOTHER, M_KOTH, M_SIEGE,
                                M_CTF, M_ENDLESS}, 4321u);
    room.bossEvery = 300;
    room.send = [](Client&, const std::string&, bool) {};
    Client c;
    room.addClient(&c);
    Rng rng(7);
    std::string seen;
    int rounds = 0;
    for (int t = 0; t < ticks * 12 && rounds < 24; t++) {
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

// Rules added with the newer modes: regeneration and shield numbers, the trap limit, drones kept
// through a class change, starter drones, the bounty, the bots' builds, and one scripted check
// for every new mode.
static int modesTest() {
    int bad = 0;
    auto fail = [&](const char* what) { std::printf("RULE CHECK FAILED: %s\n", what); bad++; };
    std::map<Client*, std::string> inbox;
    auto wire = [&](Room& room) { room.send = [&](Client& c, const std::string& d, bool binary) { if (!binary) inbox[&c] += d + "\n"; }; };
    auto join = [&](Room& room, Client& c, const char* name) {
        room.addClient(&c);
        std::string m = std::string(1, '\0') + name;
        room.onMessage(c, (const uint8_t*)m.data(), m.size());
    };
    auto tankOf = [&](Room& room, Client& c) -> Entity* {
        for (Entity* e : room.ents)
            if (e->id == c.tankId && !e->dead) return e;
        return nullptr;
    };
    auto input = [&](Room& room, Client& c, Vec2 aim, uint32_t flags) {
        ByteWriter w;
        w.u8(1);
        w.u8(flags);
        w.f32(aim.x);
        w.f32(aim.y);
        room.onMessage(c, (const uint8_t*)w.buf.data(), w.buf.size());
    };
    auto become = [&](Room& room, Client& c, std::initializer_list<const char*> path) {
        for (const char* cls : path) {
            ByteWriter u;
            u.u8(3);
            u.u16(uint32_t(tankIndex(cls)));
            room.onMessage(c, (const uint8_t*)u.buf.data(), u.buf.size());
        }
    };
    auto spend = [&](Room& room, Client& c, int stat, int times) {
        const uint8_t m[] = {2, uint8_t(stat)};
        for (int i = 0; i < times; i++) room.onMessage(c, m, 2);
    };
    auto clearFood = [&](Room& room) {  // no polygons in the way (none grow back: the arena still counts them)
        for (Entity* e : room.ents)
            if (e->kind == K_FOOD) e->dead = true;
    };
    auto drones = [&](Room& room, Entity* owner) {
        int n = 0;
        for (Entity* e : room.ents)
            if (!e->dead && e->hp > 0 && e->kind == K_DRONE && e->owner == owner->id && e->counted) n++;
        return n;
    };
    auto count = [&](Room& room, Kind k, Entity* owner) {
        int n = 0;
        for (Entity* e : room.ents)
            if (!e->dead && e->hp > 0 && e->kind == k && e->owner == owner->id) n++;
        return n;
    };

    // --- class data
    {
        const TankDef& quad = TANKS[size_t(tankIndex("Quadruplex"))];
        bool forward = false;
        for (const GunDef& g : quad.guns) forward = forward || std::fabs(g.ang) < 0.01f;
        if (!forward) fail("Quadruplex has a barrel that fires where it aims");
        const TankDef& dual = TANKS[size_t(tankIndex("Dual"))];
        if (dual.guns.size() != 4 || std::fabs(dual.guns[2].delay - dual.guns[0].delay - 0.5f) > 0.01f) fail("Dual's two pairs alternate");
        if (std::fabs(dual.guns[0].reload - 1.6f * 1.3f) > 0.01f) fail("Dual reloads 1.3 times slower than Hunter");
        if (TANKS[size_t(tankIndex("Smasher"))].statCap != 10 || TANKS[size_t(tankIndex("Spike"))].statCap != 10) fail("the Smasher line's stats stop at 10");
        if (TANKS[size_t(tankIndex("Tri-Angle"))].thrust <= 3 || TANKS[size_t(tankIndex("Booster"))].thrust <= 3 ||
            TANKS[size_t(tankIndex("Eagle"))].thrust <= 3 || TANKS[size_t(tankIndex("Basic"))].thrust > 0 || TANKS[size_t(tankIndex("Destroyer"))].thrust > 0)
            fail("rear thrusters are recognised (Tri-Angle line)");
        if (TANKS[size_t(tankIndex("Desmos"))].wave == 0 || TANKS[size_t(tankIndex("Desmos"))].waveSym || !TANKS[size_t(tankIndex("Helix"))].waveSym ||
            !TANKS[size_t(tankIndex("Triplex"))].waveSym || TANKS[size_t(tankIndex("Twin"))].wave != 0)
            fail("weaving barrels are recognised (Desmos line)");
        const std::pair<const char*, int> styles[] = {{"Smasher", BS_RAMMER}, {"Overlord", BS_DRONES}, {"Hexa-Trapper", BS_TRAPPER}, {"Ranger", BS_SNIPER},
                                                      {"Booster", BS_BRAWLER}, {"Annihilator", BS_HEAVY}, {"Medic", BS_HEALER}, {"Streamliner", BS_SPRAY},
                                                      {"Triplet", BS_BALANCED}};
        for (const auto& s : styles)
            if (TANKS[size_t(tankIndex(s.first))].style != s.second) { std::printf("  (%s)\n", s.first); fail("every class gets the bot build that suits it"); }
    }

    // --- regeneration, shield, trap limit, kept drones, starter drones (an empty FFA arena)
    {
        Room::mutationDefault = 0;
        Room room(M_FFA, 31u);
        Room::mutationDefault = 100;
        room.setBots(0);
        room.bossEvery = 0;
        room.startScore = levelScore(MAX_LEVEL);
        wire(room);
        Client reg, sep, ovr, nec;
        join(room, reg, "Reg");
        join(room, sep, "Sep");
        join(room, ovr, "Ovr");
        join(room, nec, "Nec");
        room.tick();
        clearFood(room);
        Entity *er = tankOf(room, reg), *es = tankOf(room, sep), *eo = tankOf(room, ovr), *en = tankOf(room, nec);
        if (!er || !es || !eo || !en) { fail("players spawn"); return bad; }
        er->pos = {1000, 1000}; es->pos = {4000, 1000}; eo->pos = {1000, 4000}; en->pos = {4000, 4000};
        for (Entity* e : {er, es, eo, en}) e->tank->invuln = 0;

        // Health: 0.025% of the maximum a tick, three times that after ten seconds without damage.
        er->hp = er->maxHp * 0.5f;
        er->tank->lastHurt = room.tickNo;
        float h0 = er->hp;
        for (int t = 0; t < 30; t++) room.tick();
        float perSec = (er->hp - h0) / er->maxHp;
        if (std::fabs(perSec - 0.00025f * 30) > 0.0005f) fail("health comes back at 0.75% a second without the Regeneration stat");
        er->tank->lastHurt = room.tickNo - 400;
        h0 = er->hp;
        for (int t = 0; t < 30; t++) room.tick();
        perSec = (er->hp - h0) / er->maxHp;
        if (std::fabs(perSec - 0.00025f * 3 * 30) > 0.001f) fail("health comes back three times faster after ten seconds without damage");
        // Shield with Shield Regeneration at 9: nothing for 54 ticks, full about 72 ticks later.
        spend(room, reg, S_REGEN, 9);
        if (er->tank->skills[S_REGEN] != 9) fail("Shield Regeneration takes nine points");
        er->shield = 0;
        er->tank->lastHurt = room.tickNo;
        for (int t = 0; t < 53; t++) room.tick();
        if (er->shield > 0) fail("the shield waits 1.8 seconds before it refills at Shield Regeneration 9");
        for (int t = 0; t < 40; t++) room.tick();
        if (er->shield <= 0 || er->shield >= er->maxShield) fail("the shield refills gradually");
        for (int t = 0; t < 40; t++) room.tick();
        if (er->shield < er->maxShield) fail("the shield is full about 2.4 seconds after it starts to refill");

        // Septa-Trapper: never more than 45 traps out, however fast it fires.
        become(room, sep, {"Trapper", "Tri-Trapper", "Septa-Trapper"});
        spend(room, sep, S_RELOAD, 9);
        int most = 0;
        for (int t = 0; t < 12 * TICK_RATE; t++) {
            es->pos = {4000, 1000};
            es->vel = {0, 0};
            input(room, sep, {200, 0}, 16);
            room.tick();
            most = std::max(most, count(room, K_TRAP, es));
        }
        if (TANKS[size_t(es->tank->def)].name != "Septa-Trapper") fail("Tri-Trapper upgrades to Septa-Trapper");
        const int cap = TANKS[size_t(es->tank->def)].maxTraps;
        if (cap != 45 || most > cap || most < cap - 5 || room.statTrapCaps == 0) { std::printf("  (most traps out: %d)\n", most); fail("a Septa-Trapper has at most 45 traps out"); }

        // Drones survive a class change, up to the new class's limit.
        become(room, ovr, {"Director", "Overseer"});
        for (int t = 0; t < 30 * TICK_RATE && drones(room, eo) < 8; t++) { eo->pos = {1000, 4000}; room.tick(); }
        if (drones(room, eo) != 8) fail("an Overseer builds eight drones");
        become(room, ovr, {"Overlord"});
        room.tick();
        if (TANKS[size_t(eo->tank->def)].name != "Overlord" || drones(room, eo) != 8 || eo->tank->drones != 8) fail("an Overseer keeps its drones when it becomes an Overlord");
        // ...and the Necromancer line starts with a few.
        become(room, nec, {"Director", "Underseer"});
        room.tick();
        if (drones(room, en) != 0) fail("an Underseer starts without drones");
        become(room, nec, {"Necromancer"});
        room.tick();
        if (TANKS[size_t(en->tank->def)].name != "Necromancer" || drones(room, en) != 4 || en->tank->drones != 4) fail("a Necromancer starts with four drones");
        for (Client* c : {&reg, &sep, &ovr, &nec}) room.removeClient(c);
    }

    // --- weaving bullets: aiming off by waveAimFix lands shots that aiming straight at the target misses
    {
        Room::mutationDefault = 0;
        Room room(M_FFA, 36u);
        Room::mutationDefault = 100;
        room.setBots(0);
        room.bossEvery = 0;
        room.startScore = levelScore(MAX_LEVEL);
        wire(room);
        Client des, tgt;
        join(room, des, "Des");
        join(room, tgt, "Tgt");
        room.tick();
        clearFood(room);
        Entity *ed = tankOf(room, des), *et = tankOf(room, tgt);
        if (!ed || !et) { fail("players spawn"); return bad; }
        become(room, des, {"Desmos"});
        float straight = 0, fixed = 0;
        int landed = 0;
        const float dists[4] = {230, 300, 370, 440};
        for (int pass = 0; pass < 8; pass++) {
            const float dist = dists[pass / 2];
            const bool fix = pass % 2 == 1;
            et->hits.clear();
            for (int t = 0; t < 8 * TICK_RATE; t++) {
                ed->pos = {2000, 2500}; et->pos = {2000 + dist, 2500};
                ed->vel = et->vel = {0, 0};
                et->hp = et->maxHp;
                et->shield = et->maxShield;
                et->size = 10;  // a small target: at full size a tank is wide enough to be grazed by most of the swing
                ed->tank->invuln = et->tank->invuln = 0;
                const float a = fix ? room.waveAimFix(*ed, dist) : 0;
                input(room, des, fromAngle(a, dist), t < 6 * TICK_RATE ? 16 : 0);
                room.tick();
            }
            float dealt = 0;
            for (const Entity::Hit& h : et->hits) dealt += h.dmg;
            (fix ? fixed : straight) += dealt;
            if (fix && dealt > 20) landed++;
        }
        if (landed != 4 || fixed < straight * 1.5f) { std::printf("  (aimed straight %.0f, aimed off %.0f, %d of 4 distances)\n", straight, fixed, landed); fail("bots allow for the weave of Desmos-line bullets"); }
        room.removeClient(&des);
        room.removeClient(&tgt);
    }

    // --- the bounty on the leader
    {
        Room::mutationDefault = 0;
        Room room(M_FFA, 32u);
        Room::mutationDefault = 100;
        room.setBots(0);
        room.bossEvery = 0;
        room.startScore = levelScore(MAX_LEVEL);
        wire(room);
        Client ann, bea, vic;
        join(room, ann, "Ann");
        join(room, bea, "Bea");
        join(room, vic, "Vic");
        room.tick();
        clearFood(room);
        Entity *ea = tankOf(room, ann), *eb = tankOf(room, bea), *ev = tankOf(room, vic);
        if (!ea || !eb || !ev) { fail("players spawn"); return bad; }
        ev->tank->score += 30000;  // Vic is far ahead
        for (int t = 0; t < 2 * TICK_RATE; t++) room.tick();
        inbox.clear();
        const double a0 = ea->tank->score;
        for (int t = 0; t < 3000 && vic.tankId; t++) {
            ea->pos = {2250, 2500}; eb->pos = {600, 600}; ev->pos = {2500, 2500};
            ea->vel = ev->vel = {0, 0};
            input(room, ann, ev->pos - ea->pos, 16);
            room.tick();
        }
        if (vic.tankId) fail("the leader is destroyed");
        const double bounty = std::floor((levelScore(MAX_LEVEL) + 30000) * 0.25);  // a quarter of the leader's score
        const std::string amount = std::to_string((long long)bounty);
        if (room.statBounties != 1 || inbox[&bea].find("Ann claimed the bounty on Vic (+" + amount + ")") == std::string::npos) fail("destroying the leader pays the bounty and everyone is told");
        if (ea->tank->score - a0 < 12000 + bounty - 5) fail("the bounty comes on top of the usual reward");
        if (inbox[&vic].find("there was a bounty of " + amount + " on you") == std::string::npos) fail("the leader's death screen mentions the bounty");
        for (Client* c : {&ann, &bea, &vic}) room.removeClient(c);
    }

    // --- bots: a plan, a preset build, the class they aimed for
    for (int level = 0; level <= 2; level++) {
        Room::botLevelDefault = level;
        Room room(M_FFA, 33u + uint32_t(level));
        Room::botLevelDefault = 1;
        room.send = [](Client&, const std::string&, bool) {};
        room.bossEvery = 0;
        Client watcher;
        room.addClient(&watcher);
        for (int t = 0; t < 70 * TICK_RATE; t++) room.tick();
        int bots = 0, built = 0, onGoal = 0, spent = 0;
        for (Entity* e : room.ents) {
            if (e->dead || e->kind != K_TANK || !e->tank->bot || e->tank->level < MAX_LEVEL) continue;
            if (room.tickNo - e->tank->spawnTick < 45 * TICK_RATE) continue;  // not long enough alive to have finished
            bots++;
            const TankData& t = *e->tank;
            if (int(t.ai.level) != level) fail("bots play at the level the server asks for");
            int sum = 0;
            for (int s : t.skills) sum += s;
            if (sum == 42 && t.skillPts == 0) spent++;
            if (t.ai.build >= 0) built++;
            if (t.ai.goalDef >= 0 && t.def == t.ai.goalDef) onGoal++;
        }
        if (bots < 5 || spent != bots) fail("bots spend all 42 points");
        if (level == 0 ? built != 0 : (built != bots || onGoal != bots)) fail("normal and hard bots follow a preset build to the class they aimed for; easy bots do not plan");
        if (level == 0 ? room.statDodges != 0 : room.statDodges == 0) fail("normal and hard bots dodge bullets; easy bots do not");
        room.removeClient(&watcher);
    }

    // --- Tag: the loser joins the winner's team, and a team that has everyone wins
    {
        Room room(M_TAG, 41u);
        room.setBots(0);
        room.bossEvery = 0;
        room.startScore = levelScore(MAX_LEVEL);
        wire(room);
        Client ann, bea;
        join(room, ann, "Ann");
        join(room, bea, "Bea");
        room.tick();
        clearFood(room);
        Entity *ea = tankOf(room, ann), *eb = tankOf(room, bea);
        if (!ea || !eb || ann.team == bea.team) { fail("Tag deals the players onto different teams"); return bad; }
        const int annTeam = ann.team;
        for (int t = 0; t < 3000 && bea.tankId; t++) {
            ea->pos = {2250, 2500}; eb->pos = {2500, 2500};
            ea->vel = eb->vel = {0, 0};
            input(room, ann, eb->pos - ea->pos, 16);
            room.tick();
        }
        if (bea.tankId || bea.team != annTeam || room.statTagged != 1) fail("a tagged tank joins the team that destroyed it");
        if (inbox[&bea].find("You were tagged") == std::string::npos) fail("the death screen says which team the tank joined");
        const uint8_t again[] = {0, 'B', 'e', 'a'};
        room.onMessage(bea, again, sizeof(again));
        for (int t = 0; t < 25 * TICK_RATE; t++) room.tick();
        Entity* eb2 = tankOf(room, bea);
        if (room.statWins != 1) fail("Tag ends when every tank is on one team");
        if (eb2 && eb2->team != annTeam) fail("a tagged player respawns on the new team");
        room.removeClient(&ann);
        room.removeClient(&bea);
    }

    // --- King of the Hill: a team alone on the hill scores once a second
    {
        Room room(M_KOTH, 42u);
        room.setBots(0);
        room.bossEvery = 0;
        wire(room);
        Client ann;
        join(room, ann, "Ann");
        room.tick();
        Entity* ea = tankOf(room, ann);
        if (!ea) { fail("players spawn"); return bad; }
        ea->tank->invuln = 0;
        for (int t = 0; t < (KOTH_GOAL + 3) * TICK_RATE && room.statWins == 0; t++) {
            ea->pos = {room.W / 2, room.H / 2};
            ea->vel = {0, 0};
            ea->hp = ea->maxHp;
            room.tick();
        }
        if (room.statWins != 1 || room.summary().find("hill " + std::to_string(ann.team == 0 ? KOTH_GOAL : 0) + ":" + std::to_string(ann.team == 1 ? KOTH_GOAL : 0)) == std::string::npos)
            fail("holding the hill alone for 150 seconds wins King of the Hill");
        room.removeClient(&ann);
    }

    // --- Capture the Flag: take it, carry it home, score
    {
        Room room(M_CTF, 43u);
        room.setBots(0);
        room.bossEvery = 0;
        wire(room);
        Client ann;
        join(room, ann, "Ann");
        room.tick();
        Entity* ea = tankOf(room, ann);
        if (!ea) { fail("players spawn"); return bad; }
        ea->tank->invuln = 0;
        const Vec2 own{room.W * (ann.team ? 0.8f : 0.2f), room.H / 2}, theirs{room.W * (ann.team ? 0.2f : 0.8f), room.H / 2};
        for (int cap = 0; cap < CTF_GOAL; cap++) {
            ea->pos = theirs;
            for (int t = 0; t < 3; t++) { ea->hp = ea->maxHp; room.tick(); }
            if (room.statFlagGrabs != cap + 1) fail("touching the enemy flag picks it up");
            ea->pos = own;
            for (int t = 0; t < 3; t++) { ea->hp = ea->maxHp; room.tick(); }
            if (room.statFlagCaps != cap + 1) fail("carrying the flag to your own stand captures it");
        }
        if (room.statWins != 1) fail("three captures win Capture the Flag");
        if (inbox[&ann].find("\"ctf\":{\"score\":[") == std::string::npos) fail("the flag state is sent to the players");
        room.removeClient(&ann);
    }

    // --- Siege: the Citadel is shielded until both outer dominators are taken; taking all three wins
    {
        Room room(M_SIEGE, 44u);
        room.setBots(0);
        room.bossEvery = 0;
        wire(room);
        Client ann;
        join(room, ann, "Ann");
        room.tick();
        std::vector<Entity*> doms;
        for (Entity* e : room.ents)
            if (e->kind == K_TANK && e->tank->role == R_DOM) doms.push_back(e);
        std::sort(doms.begin(), doms.end(), [](Entity* a, Entity* b) { return a->tank->dom < b->tank->dom; });
        if (doms.size() != 3 || TANKS[size_t(doms[2]->tank->def)].name != "Citadel" || !doms[2]->tank->invincible || doms[0]->team != 1)
            { fail("Siege has two outer dominators and a shielded Citadel, all Green's"); return bad; }
        auto blueDestroys = [&](Entity* d) {
            d->lastHitTeam = 0;
            d->lastHitTick = room.tickNo;
            d->hp = 0;
            room.tick();
        };
        blueDestroys(doms[0]);
        if (doms[0]->team != 0 || !doms[0]->tank->invincible || !doms[2]->tank->invincible) fail("a dominator Blue destroys is Blue's for good");
        doms[1]->lastHitTeam = TEAM_FOOD;  // a boss knocks one down: it is repaired, not captured
        doms[1]->lastHitTick = room.tickNo;
        doms[1]->hp = 0;
        room.tick();
        if (doms[1]->team != 1 || doms[1]->hp < doms[1]->maxHp) fail("only the attackers can take a Siege dominator");
        blueDestroys(doms[1]);
        if (doms[2]->tank->invincible) fail("the Citadel can be attacked once both outer dominators are taken");
        blueDestroys(doms[2]);
        if (room.statWins != 1) fail("taking all three dominators wins Siege for the attackers");
        room.removeClient(&ann);
        Room timed(M_SIEGE, 45u);
        timed.setBots(0);
        timed.bossEvery = 0;
        timed.send = [](Client&, const std::string&, bool) {};
        for (int t = 0; t < (SIEGE_TIME + 2) * TICK_RATE; t++) timed.tick();
        if (timed.statWins != 1) fail("the defenders win Siege when the clock runs out");
    }

    // --- Mothership: driving it with the dominator key; losing it loses the game
    {
        Room room(M_MOTHER, 46u);
        room.setBots(0);
        room.bossEvery = 0;
        wire(room);
        Client ann;
        join(room, ann, "Ann");
        room.tick();
        Entity* ea = tankOf(room, ann);
        Entity* ships[2] = {nullptr, nullptr};
        for (Entity* e : room.ents)
            if (e->kind == K_TANK && e->tank->role == R_MOTHER && e->team >= 0 && e->team < 2) ships[e->team] = e;
        if (!ea || !ships[0] || !ships[1]) { fail("Mothership mode has one Mothership a team"); return bad; }
        if ((ea->pos - ships[ann.team]->pos).len() > 600) fail("players spawn beside their Mothership");
        const uint8_t h[] = {6};
        room.onMessage(ann, h, 1);
        room.tick();
        if (!ann.inDom || ann.tankId != ships[ann.team]->id) fail("the dominator key puts a player in their team's Mothership");
        const Vec2 before = ships[ann.team]->pos;
        for (int t = 0; t < 60; t++) { input(room, ann, {300, 0}, 2 | 16); room.tick(); }
        if (ships[ann.team]->pos.y - before.y < 20) fail("a player can steer the Mothership");
        if (ships[ann.team]->tank->drones < 10) fail("the Mothership launches its drones");
        room.onMessage(ann, h, 1);
        room.tick();
        if (ann.inDom || !tankOf(room, ann) || tankOf(room, ann)->tank->role != R_PLAYER) fail("the key gives the player their own tank back");
        ships[1 - ann.team]->hp = 0;
        room.tick();
        if (room.statWins != 1) fail("destroying the enemy Mothership wins the game");
        room.removeClient(&ann);
    }

    // --- Battle Royale: a lobby without damage, then no respawns, a closing circle and one winner
    {
        Room room(M_BR, 47u);
        room.bossEvery = 0;
        wire(room);
        Client ann;
        join(room, ann, "Ann");
        int t = 0;
        for (; t < (BR_LOBBY + 5) * TICK_RATE && !room.battleBegun(); t++) room.tick();
        if (!room.battleBegun() || t < 20 * TICK_RATE) fail("the battle begins after the lobby");
        if (room.statKills != 0) fail("tanks cannot destroy each other in the Battle Royale lobby");
        if (Entity* ea = tankOf(room, ann)) { ea->tank->invuln = 0; ea->hp = 0; }
        room.tick();
        inbox.clear();
        const uint8_t spawn[] = {0, 'A', 'n', 'n'};
        room.onMessage(ann, spawn, sizeof(spawn));
        room.tick();
        if (ann.tankId || inbox[&ann].find("nospawn") == std::string::npos) fail("nobody respawns once the battle has begun");
        for (t = 0; t < (BR_SHRINK + 30) * TICK_RATE && room.statWins == 0; t++) room.tick();
        if (room.statWins != 1) fail("a Battle Royale ends with a winner before the circle has closed");
        int left = 0;
        for (Entity* e : room.ents)
            if (!e->dead && e->hp > 0 && e->kind == K_TANK && e->tank->role == R_PLAYER) left++;
        if (left > 1) fail("at most one tank is left when a Battle Royale ends");
        room.removeClient(&ann);
    }

    // --- Endless Boss Rush: wave 21 follows wave 20, and the server keeps the record
    {
        Room room(M_ENDLESS, 48u);
        room.send = [](Client&, const std::string&, bool) {};
        Client watcher;
        room.addClient(&watcher);
        for (int t = 0; t < 400 * TICK_RATE && room.wave() < 23; t++) {
            if (t % 20 == 0)
                for (Entity* e : room.ents)
                    if (!e->dead && e->kind == K_TANK && e->tank->role == R_BOSS) e->hp = 0;  // every boss falls at once
            room.tick();
        }
        if (room.wave() < 23 || room.bestWave() < 22 || room.statWins != 0) fail("Endless Boss Rush goes on past wave 20 and records the waves cleared");
        room.removeClient(&watcher);
    }

    // --- Sandbox: free levels, any class, and the panel's commands
    {
        Room room(M_SANDBOX, 49u);
        room.freeLevel = 0;  // even with the level-up key switched off on this server
        wire(room);
        Client ann;
        join(room, ann, "Ann");
        room.tick();
        Entity* ea = tankOf(room, ann);
        if (!ea) { fail("players spawn"); return bad; }
        auto cmd = [&](uint32_t c, uint32_t a, uint32_t b, Vec2 at) {
            ByteWriter w;
            w.u8(10); w.u8(c); w.u8(a); w.u8(b); w.f32(at.x); w.f32(at.y);
            room.onMessage(ann, (const uint8_t*)w.buf.data(), w.buf.size());
        };
        become(room, ann, {"Annihilator"});
        if (TANKS[size_t(ea->tank->def)].name != "Annihilator") fail("a Sandbox player can take any class at any level");
        become(room, ann, {"Arena Closer"});
        if (TANKS[size_t(ea->tank->def)].name != "Annihilator") fail("computer-controlled classes stay out of reach in the Sandbox");
        for (int t = 0; t < 4 * TICK_RATE; t++) { input(room, ann, {100, 0}, 64); room.tick(); }
        if (ea->tank->level != MAX_LEVEL) fail("the level-up key always works in the Sandbox");
        cmd(5, 20, 0, {0, 0});
        if (ea->tank->level != 20 || ea->tank->skillPts != 19) fail("the Sandbox can set a level");
        const int food0 = room.countKind(K_FOOD);
        cmd(1, F_PENTAGON, T_ALPHA | (MU_SHINY << 3), {300, 0});
        room.tick();
        bool found = false;
        for (Entity* e : room.ents)
            if (e->kind == K_FOOD && e->shape == F_PENTAGON && e->tier == T_ALPHA && e->mut == MU_SHINY && (e->pos - ea->pos).len() < 400) found = true;
        if (!found || room.countKind(K_FOOD) < food0) fail("the Sandbox can spawn a polygon of any shape, size and mutation");
        cmd(2, 0, 0, {600, 0});
        room.tick();
        if (room.statBosses != 1) fail("the Sandbox can spawn a boss");
        cmd(4, 0, 0, {0, 0});
        cmd(3, 0, 0, {0, 0});
        room.tick();
        room.tick();
        int bosses = 0;
        for (Entity* e : room.ents)
            if (!e->dead && e->kind == K_TANK && e->tank->role == R_BOSS) bosses++;
        if (bosses != 0 || room.countKind(K_FOOD) != 0) fail("the Sandbox can clear bosses and polygons");
        cmd(7, 0, 0, {0, 0});
        if (!ea->tank->invincible) fail("the Sandbox can switch damage off");
        for (int t = 0; t < 2 * TICK_RATE; t++) room.tick();
        cmd(6, 1, 0, {0, 0});
        cmd(6, 1, 0, {0, 0});
        for (int t = 0; t < 2 * TICK_RATE; t++) room.tick();
        int bots = 0;
        for (Entity* e : room.ents)
            if (!e->dead && e->kind == K_TANK && e->tank->bot) bots++;
        if (bots != 2) fail("the Sandbox can add bots");
        for (int i = 0; i < 40; i++) cmd(1, F_SQUARE, 0, {200, 200});  // far more than the allowance
        room.tick();
        if (room.countKind(K_FOOD) > 8) fail("Sandbox commands are rate-limited");
        Room other(M_FFA, 50u);
        other.send = [](Client&, const std::string&, bool) {};
        other.setBots(0);
        Client bob;
        join(other, bob, "Bob");
        other.tick();
        const int before = other.countKind(K_FOOD);
        ByteWriter w;
        w.u8(10); w.u8(1); w.u8(F_SQUARE); w.u8(0); w.f32(100); w.f32(0);
        other.onMessage(bob, (const uint8_t*)w.buf.data(), w.buf.size());
        ByteWriter u;
        u.u8(3);
        u.u16(uint32_t(tankIndex("Annihilator")));
        other.onMessage(bob, (const uint8_t*)u.buf.data(), u.buf.size());
        other.tick();
        Entity* eb = tankOf(other, bob);
        if (other.countKind(K_FOOD) > before + 4 || !eb || TANKS[size_t(eb->tank->def)].name != "Basic") fail("Sandbox commands do nothing outside the Sandbox");
        other.removeClient(&bob);
        room.removeClient(&ann);
    }
    std::printf("new rules: %s (regeneration, shield, trap limit, kept drones, starter drones, bounty, bot builds and dodging, Tag, King of the Hill,\n"
                "           Capture the Flag, Siege, Mothership, Battle Royale, Endless Boss Rush, Sandbox)\n", bad ? "FAILED" : "ok");
    return bad;
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

    // --- Smasher line: bullet points come back, five stats, ten points each, Reload is its engine
    spend(sam, S_BDMG, 5);
    if (es->tank->skills[S_BDMG] != 5) fail("a Basic can spend on Bullet Damage");
    become(sam, "Smasher");
    if (TANKS[size_t(es->tank->def)].name != "Smasher") fail("Basic upgrades to Smasher");
    if (es->tank->skills[S_BDMG] != 0 || es->tank->skillPts != 42) fail("becoming a Smasher hands back the bullet points");
    spend(sam, S_BODY, 14);
    spend(sam, S_BSPEED, 3);
    spend(sam, S_MOVE, 3);
    spend(sam, S_RELOAD, 3);
    if (es->tank->skills[S_BODY] != 10) fail("Smasher stats stop at 10");
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
    bad += modesTest();
    bad += rotationTest(ticks);
    const Mode modes[] = {M_FFA, M_MAZE, M_TDM2, M_TDM4, M_DOM, M_MAZEDOM, M_RUSH, M_SANDBOX, M_TAG, M_BR, M_MOTHER, M_KOTH, M_SIEGE, M_CTF, M_ENDLESS};
    for (int i = 0; i < int(sizeof(modes) / sizeof(modes[0])); i++) {
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

// --sim MODE MINUTES: bots only, no network, as fast as the machine goes. Prints every result as
// it happens, then the usual summary and a kill / death table by class. For balance work.
static int simulate(const std::string& modeName, int minutes, uint32_t seed, int bots, int freeLevel, int bossMinutes) {
    Mode m;
    if (!parseMode(modeName, m)) {
        std::fprintf(stderr, "unknown mode \"%s\"\n", modeName.c_str());
        return 1;
    }
    Room room(m, seed);
    room.freeLevel = freeLevel;
    room.bossEvery = bossMinutes * 60 * TICK_RATE;
    if (bots >= 0) room.setBots(bots);
    int now = 0, games = 0;
    auto field = [](const std::string& d, const char* key) {
        size_t a = d.find(std::string("\"") + key + "\":\"");
        if (a == std::string::npos) return std::string();
        a += std::strlen(key) + 4;
        size_t b = d.find('"', a);
        return d.substr(a, b == std::string::npos ? b : b - a);
    };
    room.send = [&](Client&, const std::string& d, bool binary) {
        if (binary || d.find("\"t\":\"banner\"") == std::string::npos) return;
        std::printf("%3d:%02d  %s | %s\n", now / TICK_RATE / 60, now / TICK_RATE % 60, field(d, "text").c_str(), field(d, "sub").c_str());
        if (d.find("NEW GAME") != std::string::npos) games++;
    };
    Client watcher;  // somebody has to be connected for the arena to run
    room.addClient(&watcher);
    std::vector<int> kills(TANKS.size(), 0), deaths(TANKS.size(), 0);
    auto t0 = std::chrono::steady_clock::now();
    for (now = 0; now < minutes * 60 * TICK_RATE; now++) room.tick();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s\n      %d games finished in %d simulated minutes (%.1f s of computing, %.3f ms a tick)\n", room.summary().c_str(), games, minutes,
                ms / 1000, ms / std::max(1, now));
    if (room.statClassKills.size() == TANKS.size()) { kills = room.statClassKills; deaths = room.statClassDeaths; }
    std::vector<size_t> order;
    for (size_t i = 0; i < TANKS.size(); i++)
        if (kills[i] + deaths[i] > 0) order.push_back(i);
    auto kd = [&](size_t i) { return double(kills[i]) / std::max(1, deaths[i]); };
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return kd(a) > kd(b); });
    std::printf("      class                  kills deaths   K/D\n");
    for (size_t i : order) std::printf("      %-22s %5d %6d %5.2f\n", TANKS[i].name.c_str(), kills[i], deaths[i], kd(i));
    room.removeClient(&watcher);
    return 0;
}

static void usage(const char* exe) {
    std::printf(
        "usage: %s [options]\n"
        "  --port N           port to listen on (default 3000, or the PORT environment variable)\n"
        "  --bind ADDR        address to listen on: 0.0.0.0 = everyone (default), 127.0.0.1 = this machine only\n"
        "  --public DIR       folder with the web client (default ./public)\n"
        "  --bots N           bots per arena when it is empty; they make room as players join (default 12-16)\n"
        "  --bot-level LEVEL  how well the bots play: easy, normal (default), hard, or mixed (every bot rolls its own)\n"
        "  --max-players N    players allowed in one arena (default 40)\n"
        "  --max-per-ip N     connections allowed from one address, 0 = unlimited (default 6)\n"
        "  --trust-proxy      read the player's address from X-Forwarded-For (only behind your own reverse proxy)\n"
        "  --start-level N    sandbox: every tank spawns at this level (1-45)\n"
        "  --free-level N     holding the level-up key (N) raises a tank to this level for free; 0 = off (default 45)\n"
        "  --boss-minutes N   minutes between boss visits, 0 = no bosses (default 6)\n"
        "  --mutations N      how common mutated polygons are (shiny, legendary, shadow, rainbow, trans, polyhedra):\n"
        "                     a multiplier on the base odds of 1 in 50,000 for shiny. Default 100 (1 shiny in 500);\n"
        "                     0 = none, 1 = the base odds, 20000 = see them all within minutes\n"
        "  --arena-minutes N  FFA, Maze, Teams and Tag arenas close and restart after this many minutes, 0 = never\n"
        "                     (default 45 while any slot has more than one mode, otherwise 0)\n"
        "  --no-chat          players cannot chat (by default they can: Enter, one line of up to 80 characters)\n"
        "  --dev-code CODE    code that unlocks the Developer tank (default 1234; \"off\" = nobody can)\n"
        "  --slots LIST       arenas to run. Slots are separated by commas; a slot with several modes separated\n"
        "                     by / picks one at random for every new game. Modes: ffa maze 2tdm 4tdm dom mazedom rush\n"
        "                     endless mother siege ctf tag br koth sandbox\n"
        "                     (default ffa/maze,2tdm/4tdm,dom/mazedom/mother/siege/ctf,rush/endless,tag/br/koth,sandbox)\n"
        "  --selftest TICKS   simulate every mode without networking and print statistics\n"
        "  --sim MODE MINUTES bots only, no network, at full speed: prints each game's result, then kills and\n"
        "                     deaths by class (for balance work). Honours --bots, --bot-level, --free-level, --boss-minutes\n"
        "                     and --mutations; --seed N gives another run (the same seed repeats a run exactly)\n",
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
    std::string devCode = "1234", slotSpec = DEFAULT_SLOTS;
    bool arenaMinutesSet = false;
    double mutations = 100;
    std::string simMode;
    int simMinutes = 0;
    uint32_t simSeed = 1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto num = [&](int lo, int hi) { return std::clamp(std::atoi(argv[++i]), lo, hi); };
        if (a == "--port" && i + 1 < argc) port = num(1, 65535);
        else if (a == "--public" && i + 1 < argc) publicDir = argv[++i];
        else if (a == "--bind" && i + 1 < argc) bindAddr = argv[++i];
        else if (a == "--bots" && i + 1 < argc) bots = num(0, 60);
        else if (a == "--bot-level" && i + 1 < argc) {
            const std::string v = argv[++i];
            static const char* const names[4] = {"easy", "normal", "hard", "mixed"};
            int level = -1;
            for (int k = 0; k < 4; k++)
                if (v == names[k] || v == std::to_string(k)) level = k;
            if (level < 0) {
                std::fprintf(stderr, "bad --bot-level value \"%s\". Use easy, normal, hard or mixed.\n", v.c_str());
                return 1;
            }
            Room::botLevelDefault = level;
        }
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
        else if (a == "--sim" && i + 2 < argc) { simMode = argv[++i]; simMinutes = num(1, 100000); }
        else if (a == "--seed" && i + 1 < argc) simSeed = uint32_t(std::atoi(argv[++i]));
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
    if (!simMode.empty()) {
        Room::mutationDefault = mutations;
        return simulate(simMode, simMinutes, simSeed, bots, freeLevel, bossMinutes);
    }

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
        std::fprintf(stderr, "bad --slots value \"%s\". Example: --slots %s\n", slotSpec.c_str(), DEFAULT_SLOTS);
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
