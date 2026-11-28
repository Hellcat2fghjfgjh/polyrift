// Rules of the newer modes and of the bounty on the leader. Each mode is a small block of state in
// Room (see "state of the newer modes" in room.hpp) and one function here that Room::modeLogic
// calls every tick. A mode that has a winner calls win(), which closes the arena a few seconds later.
#include "room.hpp"

#include <cstdio>
#include <initializer_list>

namespace {
const double BOUNTY_MIN = 10000;    // nobody is worth hunting below this score
const double BOUNTY_PART = 0.25;    // the bounty is this part of the leader's score...
const double BOUNTY_LOW = 2500, BOUNTY_HIGH = 15000;  // ...kept between these
const float MOTHER_SIZE = 78, MOTHER_HP = 4500;
const float SIEGE_OUTER_HP = 2400, SIEGE_CITADEL_HP = 2400;
}  // namespace

// Announce the result of a game. Only the first call counts; the arena closes six seconds later.
void Room::win(const std::string& text, const std::string& sub, int color) {
    if (closeAt_ >= 0 || closeTick_ >= 0) return;
    sendBanner(text, sub, color);
    sendMsg(nullptr, text);
    closeAt_ = tickNo + 6 * TICK_RATE;
    statWins++;
}

// ============================================================ bounty on the leader
// Once a second: the living tank with the highest score is the leader. Everyone sees where it is
// (sendBoards), and destroying it pays a bounty on top of the usual reward (onTankDeath).
void Room::bountyLogic() {
    leaderId_ = 0;
    leaderBounty_ = 0;
    if (!bountyMode() || closeAt_ >= 0) return;
    Entity* top = nullptr;
    int tanks = 0;
    for (Entity* e : ents) {
        if (e->dead || e->hp <= 0 || e->kind != K_TANK || e->tank->role != R_PLAYER) continue;
        tanks++;
        if (!top || e->tank->score > top->tank->score) top = e;
    }
    if (!top || tanks < 3 || top->tank->score < BOUNTY_MIN) return;
    leaderId_ = top->id;
    leaderBounty_ = std::floor(std::clamp(top->tank->score * BOUNTY_PART, BOUNTY_LOW, BOUNTY_HIGH));
}

// ============================================================ Tag
// How many members each team has: living tanks, bots waiting to respawn and players on the death screen.
std::vector<int> Room::teamMembers() const {
    std::vector<int> n(size_t(std::max(teamCount_, 1)), 0);
    auto add = [&](int team) { if (team >= 0 && team < teamCount_) n[size_t(team)]++; };
    int playing = 0, counted = 0;
    for (const Client* c : clients)
        if (c->played) { playing++; add(c->team); }
    const int allowed = std::max(0, int(bots_.size()) - playing);  // bots step aside for players (manageBots)
    for (const BotSlot& s : bots_)
        if (s.ent) { add(s.team); counted++; }
    for (const BotSlot& s : bots_)
        if (!s.ent && s.team >= 0 && counted < allowed) { add(s.team); counted++; }
    return n;
}

void Room::tagConvert(Entity& victim, Entity& killer) {
    TankData& t = *victim.tank;
    const int team = killer.team;
    if (t.client) t.client->team = team;
    if (t.slot >= 0 && t.slot < int(bots_.size())) bots_[size_t(t.slot)].team = team;
    statTagged++;
    sendMsg(nullptr, t.name + " was tagged by " + killer.tank->name + " and joins " + teamName(team));
}

void Room::tagLogic() {
    if (closeAt_ >= 0 || tickNo % TICK_RATE != 0 || tickNo - openedTick_ < 20 * TICK_RATE) return;
    std::vector<int> n = teamMembers();
    int alive = 0, total = 0, seen = 0, last = 0;
    for (int i = 0; i < teamCount_; i++) {
        if (tagSeen_[i]) seen++;
        total += n[size_t(i)];
        if (n[size_t(i)] > 0) { alive++; last = i; }
    }
    if (seen >= 2 && total >= 2 && alive == 1)
        win(std::string(teamName(last)) + " HAS WON THE GAME!", "Every tank in the arena is on their team. The arena is closing.", teamColor(last));
}

// ============================================================ Battle Royale
bool Room::inZone(Vec2 p, float margin) const {
    if (mode != M_BR || !brBegun_) return true;
    float r = brRadius_ - margin;
    return r > 0 && (p - brCentre_).len2() <= r * r;
}

void Room::brLogic() {
    if (closeAt_ >= 0) return;
    auto living = [&](Entity** one) {
        int n = 0;
        for (Entity* e : ents)
            if (!e->dead && e->hp > 0 && e->kind == K_TANK && e->tank->role == R_PLAYER) { n++; if (one) *one = e; }
        return n;
    };
    if (!brBegun_) {
        // The lobby: tanks level up and cannot hurt each other. The countdown starts with the first
        // player; if nobody comes for a minute and a half the bots have their battle anyway.
        if (brStartTick_ < 0) {
            bool anyone = false;
            for (const Client* c : clients) anyone = anyone || c->played;
            if (anyone) brStartTick_ = std::max(tickNo + 20 * TICK_RATE, openedTick_ + BR_LOBBY * TICK_RATE);
            else if (tickNo - openedTick_ > 90 * TICK_RATE) brStartTick_ = tickNo + 10 * TICK_RATE;
            return;
        }
        if (brStartTick_ - tickNo == 10 * TICK_RATE) sendMsg(nullptr, "The battle begins in 10 seconds. After that nobody can join or respawn.");
        if (tickNo < brStartTick_) return;
        brStarters_ = living(nullptr);
        if (brStarters_ < 2) {  // nobody to fight yet
            brStartTick_ = tickNo + 10 * TICK_RATE;
            return;
        }
        brBegun_ = true;
        brBeganTick_ = tickNo;
        brCentre_ = {rng_.range(W * 0.3f, W * 0.7f), rng_.range(H * 0.3f, H * 0.7f)};
        brRadius0_ = 0;
        for (Vec2 corner : {Vec2{0, 0}, Vec2{W, 0}, Vec2{0, H}, Vec2{W, H}}) brRadius0_ = std::max(brRadius0_, (corner - brCentre_).len() + 60);
        brRadius_ = brRadius0_;
        sendBanner("THE BATTLE BEGINS", "No respawns. Stay inside the circle. The last tank alive wins.", C_CLOSER);
        return;
    }
    // The safe circle closes steadily, and completely: the battle always ends.
    brRadius_ = brRadius0_ * std::max(0.f, 1 - float(tickNo - brBeganTick_) / float(BR_SHRINK * TICK_RATE));
    for (Entity* e : ents)
        if (!e->dead && e->hp > 0 && e->kind == K_TANK && e->tank->role == R_PLAYER && !inZone(e->pos))
            hurt(*e, e->maxHp * 0.004f + 0.4f, nullptr, "the closing arena");
    // Arena closers work the ground the circle has given up: one arrives in each corner as the edge passes it.
    if (tickNo % 15 == 0) {
        const Vec2 corners[4] = {{80, 80}, {W - 80, 80}, {80, H - 80}, {W - 80, H - 80}};
        for (int i = 0; i < 4; i++) {
            if (get(brClosers_[i]) || (corners[i] - brCentre_).len() < brRadius_ + 320) continue;
            Entity* e = spawnNpc("Arena Closer", R_CLOSER, TEAM_FOOD, C_CLOSER, corners[i], 44, 1);
            e->ghost = true;
            e->tank->invincible = true;
            e->tank->accel = 1.05f;
            e->tank->ai.offset = i * 3;
            e->damage = 45;
            e->projDamage = 45;
            e->damp = 0.9f;
            brClosers_[i] = e->id;
        }
    }
    Entity* last = nullptr;
    const int left = living(&last);
    if (left == 1)
        win(last->tank->name + " WINS THE BATTLE ROYALE!", "The last tank alive out of " + std::to_string(brStarters_) + ". The arena is closing.", C_LEGENDARY);
    else if (left == 0)
        win("NOBODY SURVIVED", "The last tanks fell together. The arena is closing.", C_CLOSER);
}

// ============================================================ Mothership
void Room::setupMotherships() {
    mothers_.clear();
    for (int team = 0; team < 2; team++) {
        Vec2 p{W * (team ? 0.84f : 0.16f), H / 2};
        Entity* e = spawnNpc("Mothership", R_MOTHER, team, teamColor(team), p, MOTHER_SIZE, MOTHER_HP);
        e->tank->name = std::string(team ? "Green" : "Blue") + " Mothership";
        e->damage = 6;
        e->projDamage = 5;
        e->mass = MOTHER_SIZE * MOTHER_SIZE * 5;
        e->pushf = 0.15f;
        e->damp = 0.9f;
        e->xp = 40000;  // shared between the players who destroy it
        e->angle = team ? PI : 0;
        e->tank->accel = 0.2f;
        e->tank->ai.offset = team * 4;
        e->tank->ai.goal = p;
        mothers_.push_back({p, e->id});  // pos = where it started: the computer keeps it near there
    }
}

// The computer's Mothership stays near where it started, sends its drones at whoever comes close
// and backs away when it is hurt. A player who takes it over (the dominator key) can do better.
void Room::motherThink(Entity& e) {
    TankData& t = *e.tank;
    BotBrain& b = t.ai;
    const Vec2 home = (e.team >= 0 && e.team < int(mothers_.size())) ? mothers_[size_t(e.team)].pos : e.pos;
    if ((tickNo + b.offset) % 8 == 0) {
        float bestD = 1e12f;
        b.target = 0;
        query(e.pos, 950, [&](Entity& o) {
            if (o.kind != K_TANK || !targetable(o, e.team)) return;
            float d = (o.pos - e.pos).len2();
            if (d < bestD && los(e.pos, o.pos)) { bestD = d; b.target = o.id; }
        });
    }
    Entity* tg = get(b.target);
    if (tg && !targetable(*tg, e.team)) tg = nullptr;
    Vec2 mv;
    if (tg) {
        Vec2 to = tg->pos - e.pos;
        float dist = to.len();
        t.aim = to;
        t.fire = true;  // the drones fly to where it aims
        e.angle = wrapAngle(e.angle + clampf(wrapAngle(to.angle() - e.angle), -0.05f, 0.05f));
        if (e.hp < e.maxHp * 0.4f || dist < 380) mv = to.norm() * -1.f;
        else if (dist > 640) mv = to.norm() * 0.6f;
    } else {
        t.fire = false;
        e.angle = wrapAngle(e.angle + 0.008f);
        t.aim = fromAngle(e.angle, 200);
        if (--b.goalTimer <= 0 || (b.goal - e.pos).len2() < 120 * 120) {
            b.goal = home + fromAngle(rng_.range(0, TAU), rng_.range(0, 420));
            b.goalTimer = 12 * TICK_RATE;
        }
        mv = (b.goal - e.pos).norm() * 0.5f;
    }
    if ((e.pos - home).len2() > 850 * 850) mv = (home - e.pos).norm();
    t.move = mv;
}

void Room::motherLogic() {
    if (closeAt_ >= 0 || tickNo % TICK_RATE != 0) return;
    const int age = (tickNo - openedTick_) / TICK_RATE;
    if (age == MOTHER_TIME - 60) sendMsg(nullptr, "One minute left: the Mothership in better shape will win.");
    if (age < MOTHER_TIME || mothers_.size() < 2) return;
    Entity* a = get(mothers_[0].ent);
    Entity* b = get(mothers_[1].ent);
    if (!a || !b) return;
    const float fa = a->hp / a->maxHp, fb = b->hp / b->maxHp;
    if (fa == fb) win("DRAW", "Time is up and the Motherships are level. The arena is closing.", C_NEUTRAL);
    else {
        const int winner = fa > fb ? 0 : 1;
        win(std::string(teamName(winner)) + " HAS WON THE GAME!", "Time is up: their Mothership is in better shape. The arena is closing.", teamColor(winner));
    }
}

// ============================================================ King of the Hill
void Room::kothLogic() {
    if (closeAt_ >= 0 || tickNo % TICK_RATE != 0) return;
    int in[4] = {0, 0, 0, 0};
    std::vector<Entity*> inside;
    query(hill_, KOTH_RADIUS, [&](Entity& x) {
        if (x.kind != K_TANK || x.tank->role != R_PLAYER || x.tank->invuln > 0 || x.team < 0 || x.team >= teamCount_) return;
        if ((x.pos - hill_).len2() > KOTH_RADIUS * KOTH_RADIUS) return;
        in[x.team]++;
        inside.push_back(&x);
    });
    int owner = -1, present = 0;
    for (int i = 0; i < teamCount_; i++)
        if (in[i] > 0) { present++; owner = i; }
    if (present > 1) owner = -2;  // contested: nobody scores
    if (owner != hillOwner_) {
        if (owner >= 0) sendMsg(nullptr, std::string(teamName(owner)) + " holds the hill!");
        else if (owner == -2) sendMsg(nullptr, "The hill is contested!");
        hillOwner_ = owner;
    }
    if (owner < 0) return;
    hillScore_[owner]++;
    for (Entity* x : inside) addScore(*x, 40);  // holding the hill is worth score as well
    const int s = hillScore_[owner];
    if (s == KOTH_GOAL / 2 || s == KOTH_GOAL - 20)
        sendMsg(nullptr, std::string(teamName(owner)) + " has " + std::to_string(s) + " of " + std::to_string(KOTH_GOAL) + " points.");
    if (s >= KOTH_GOAL)
        win(std::string(teamName(owner)) + " HAS WON THE GAME!", "They held the hill for " + std::to_string(KOTH_GOAL) + " seconds. The arena is closing.", teamColor(owner));
}

// ============================================================ Siege
// Green defends three dominators: two outer ones and, behind them, the Citadel, which cannot be
// hurt while an outer one stands. Blue has to take all three before the clock runs out. A dominator
// Blue destroys is Blue's for the rest of the game and adds time to the clock.
void Room::setupSiege() {
    doms_.clear();
    const char* const outer[3] = {"Destroyer Dominator", "Gunner Dominator", "Trapper Dominator"};
    const int a = rng_.irange(0, 2), b = (a + rng_.irange(1, 2)) % 3;  // two different kinds
    const Vec2 spots[3] = {{W * 0.52f, H * 0.27f}, {W * 0.52f, H * 0.73f}, {W * 0.7f, H * 0.5f}};
    for (int i = 0; i < 3; i++) {
        const bool citadel = i == 2;
        Entity* e = spawnNpc(citadel ? "Citadel" : outer[i ? b : a], R_DOM, 1, teamColor(1), spots[i], citadel ? 76.f : 62.f,
                             citadel ? SIEGE_CITADEL_HP : SIEGE_OUTER_HP);
        e->tank->dom = i;
        e->tank->name = citadel ? "Citadel" : "Dominator";
        e->tank->invincible = citadel;
        doms_.push_back({e->pos, e->id});
    }
    siegeTaken_ = 0;
    siegeEnd_ = tickNo + SIEGE_TIME * TICK_RATE;
}

// A Siege dominator ran out of health.
void Room::siegeCapture(Entity& dom, Entity* killer) {
    TankData& t = *dom.tank;
    const bool taken = dom.team == 1 && tickNo - dom.lastHitTick < 300 && dom.lastHitTeam == 0 && closeAt_ < 0;
    dom.hp = dom.maxHp;
    dom.lastHitBy = 0;
    dom.lastHitTick = -100000;
    dom.hits.clear();
    t.ai.target = 0;
    if (!taken) return;  // a boss did it, or the game is already decided: it is simply repaired
    dom.team = 0;
    dom.color = teamColor(0);
    t.invincible = true;  // Blue keeps what it takes
    siegeTaken_++;
    siegeEnd_ += SIEGE_BONUS * TICK_RATE;
    statCaptures++;
    if (killer && killer->tank) addScore(*killer, 1500);
    const int total = int(doms_.size());
    if (siegeTaken_ >= total) {
        win("BLUE HAS WON THE GAME!", "The attackers took the Citadel. The arena is closing.", teamColor(0));
    } else if (siegeTaken_ == total - 1) {
        if (Entity* citadel = get(doms_.back().ent)) citadel->tank->invincible = false;
        sendBanner("THE CITADEL IS EXPOSED", "Both outer dominators have fallen. " + std::to_string(SIEGE_BONUS) + " seconds were added to the clock.", teamColor(0));
    } else {
        sendMsg(nullptr, "BLUE took a dominator (" + std::to_string(siegeTaken_) + " of " + std::to_string(total) + "). " +
                             std::to_string(SIEGE_BONUS) + " seconds were added to the clock.");
    }
}

void Room::siegeLogic() {
    if (closeAt_ >= 0 || tickNo % TICK_RATE != 0) return;
    const int left = (siegeEnd_ - tickNo) / TICK_RATE;
    if (left == 60 || left == 10) sendMsg(nullptr, "The attackers have " + std::to_string(left) + " seconds left.");
    if (left <= 0) win("GREEN HAS WON THE GAME!", "The defenders held out until the clock ran down. The arena is closing.", teamColor(1));
}

// ============================================================ Capture the Flag
void Room::setupFlags() {
    flags_.clear();
    for (int team = 0; team < 2; team++) {
        Flag f;
        f.team = team;
        f.home = f.pos = {W * (team ? 0.8f : 0.2f), H / 2};  // just outside the team's base
        flags_.push_back(f);
    }
    ctfScore_[0] = ctfScore_[1] = 0;
    ctfOvertime_ = false;
    ctfEnd_ = tickNo + CTF_TIME * TICK_RATE;
}

const Room::Flag* Room::carriedBy(uint32_t tank) const {
    if (tank)
        for (const Flag& f : flags_)
            if (f.carrier == tank) return &f;
    return nullptr;
}

void Room::sendHome(Flag& f) {
    f.pos = f.home;
    f.carrier = 0;
    f.atHome = true;
}

void Room::ctfLogic() {
    if (closeAt_ >= 0) return;
    bool changed = false;
    for (Flag& f : flags_) {
        const std::string flagName = std::string("the ") + teamName(f.team) + " flag";
        if (f.carrier) {
            Entity* c = get(f.carrier);
            if (!c || c->hp <= 0 || c->tank->role != R_PLAYER || c->team < 0 || c->team > 1) {  // the carrier is gone: the flag lies where it fell
                f.carrier = 0;
                f.dropTick = tickNo;
                changed = true;
                sendMsg(nullptr, "The " + std::string(teamName(f.team)) + " flag was dropped! It returns home in " + std::to_string(CTF_RETURN) + " seconds.");
            } else {
                f.pos = c->pos;
                if ((c->pos - flags_[size_t(c->team)].home).len2() < 70 * 70) {  // carried to the carrier's own stand: a capture
                    const int team = c->team;
                    ctfScore_[team]++;
                    statFlagCaps++;
                    addScore(*c, 3000);
                    sendHome(f);
                    changed = true;
                    sendMsg(nullptr, c->tank->name + " captured " + flagName + "! BLUE " + std::to_string(ctfScore_[0]) + " : " +
                                         std::to_string(ctfScore_[1]) + " GREEN");
                    if (ctfScore_[team] >= CTF_GOAL || ctfOvertime_)
                        win(std::string(teamName(team)) + " HAS WON THE GAME!",
                            "They captured the flag " + std::to_string(ctfScore_[team]) + (ctfScore_[team] == 1 ? " time" : " times") + ". The arena is closing.",
                            teamColor(team));
                }
                continue;
            }
        }
        if (!f.atHome && tickNo - f.dropTick > CTF_RETURN * TICK_RATE) {
            sendHome(f);
            changed = true;
            sendMsg(nullptr, "The " + std::string(teamName(f.team)) + " flag went back to its stand.");
        }
        // Whoever touches it: an enemy picks it up, a team-mate sends a dropped one home.
        Entity* taker = nullptr;
        float bestD = 1e12f;
        query(f.pos, 34, [&](Entity& x) {
            if (x.kind != K_TANK || x.tank->role != R_PLAYER || x.tank->invuln > 0 || x.team < 0 || x.team > 1) return;
            if (x.team == f.team && f.atHome) return;  // nothing to do with their own flag while it is home
            float d = (x.pos - f.pos).len2();
            if (d < bestD) { bestD = d; taker = &x; }
        });
        if (!taker) continue;
        changed = true;
        if (taker->team != f.team) {
            f.carrier = taker->id;
            f.atHome = false;
            statFlagGrabs++;
            sendMsg(nullptr, taker->tank->name + " has taken " + flagName + "!");
        } else {
            sendHome(f);
            addScore(*taker, 400);
            sendMsg(nullptr, taker->tank->name + " returned " + flagName + ".");
        }
    }
    if (tickNo % TICK_RATE == 0 && tickNo >= ctfEnd_ && closeAt_ < 0) {
        if (ctfScore_[0] != ctfScore_[1]) {
            const int winner = ctfScore_[0] > ctfScore_[1] ? 0 : 1;
            win(std::string(teamName(winner)) + " HAS WON THE GAME!", "Time is up and they have more captures. The arena is closing.", teamColor(winner));
        } else if (!ctfOvertime_) {
            ctfOvertime_ = true;
            sendBanner("OVERTIME", "The score is level: the next capture wins.", C_NEUTRAL);
        }
    }
    if (changed) sendBoards();  // flags move on the minimap at once, not at the next second
}
