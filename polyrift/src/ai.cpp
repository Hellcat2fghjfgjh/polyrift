// Bot and dominator behaviour. Bots use exactly the same inputs a player has
// (move direction, aim point, fire) so they obey every rule players do.
#include "room.hpp"

float Room::bulletSpeedOf(const Entity& e) const {
    const TankData& t = *e.tank;
    for (const GunDef& g : TANKS[t.def].guns)
        if (g.type == G_BULLET) return BULLET_SPEED * g.speed * (1 + 0.06f * t.skills[S_BSPEED]);
    return BULLET_SPEED;
}

// Preset skill builds, one per playstyle (BotStyle in defs.hpp). Each row says how many of the 42
// points a finished build has in each stat; a bot spends towards its row a point at a time, keeping
// the stats in step, so a half-levelled bot has half the build.
//                                              body hp  spd  bhp  pen  dmg  rld  move regen shield
static const int BOT_BUILDS[BS_COUNT][S_COUNT] = {
    /* BS_BALANCED all-round gun classes   */ {0, 3, 4, 6, 6, 7, 7, 5, 2, 2},
    /* BS_GLASS    everything into bullets */ {0, 0, 6, 7, 7, 9, 9, 4, 0, 0},
    /* BS_SNIPER   fast, heavy single shots */ {0, 2, 9, 6, 7, 9, 5, 4, 0, 0},
    /* BS_SPRAY    many barrels, max reload */ {0, 3, 3, 6, 5, 7, 9, 6, 1, 2},
    /* BS_DRONES   fast, tough drones      */ {0, 4, 7, 7, 7, 8, 3, 4, 1, 1},
    /* BS_TRAPPER  traps that last         */ {0, 5, 2, 9, 8, 8, 7, 1, 1, 1},
    /* BS_RAMMER   body, health and speed  */ {9, 9, 0, 0, 0, 0, 3, 9, 6, 6},
    /* BS_HEAVY    Pounder and Launcher lines */ {0, 4, 5, 7, 7, 9, 6, 4, 0, 0},
    /* BS_BRAWLER  Tri-Angle line          */ {3, 5, 3, 5, 5, 6, 6, 7, 1, 1},
    /* BS_HEALER                           */ {0, 6, 5, 5, 3, 7, 7, 5, 2, 2},
};
// The Smasher line has five stats that take ten points each (its Reload slot is Engine Acceleration).
static const int SMASHER_BUILD[S_COUNT] = {10, 10, 0, 0, 0, 0, 10, 0, 6, 6};

// Called when a bot spawns: how well it plays, which level-45 class it is heading for, and the
// skill build that suits that class. Easy bots do not plan: they take whatever class comes up and
// spend by loose preference, as every bot did before.
void Room::botPlan(Entity& e) {
    BotBrain& b = e.tank->ai;
    if (botLevel < 3) b.level = uint8_t(std::clamp(botLevel, 0, 2));
    else {  // mixed: three easy, four or five normal, two or three hard in every ten
        float u = rng_.uni();
        b.level = u < 0.3f ? 0 : u < 0.75f ? 1 : 2;
    }
    b.goalDef = b.build = -1;
    if (b.level == 0) return;
    std::vector<int> leaves;
    for (size_t i = 0; i < TANKS.size(); i++)
        if (!TANKS[i].hidden && TANKS[i].tier == 3) leaves.push_back(int(i));
    if (leaves.empty()) return;
    b.goalDef = leaves[size_t(rng_.irange(0, int(leaves.size()) - 1))];
    b.build = TANKS[size_t(b.goalDef)].style;
    if (b.build == BS_BALANCED && rng_.chance(0.35f)) b.build = BS_GLASS;
}

bool Room::reaches(int from, int to) const {
    if (from == to) return true;
    for (int u : TANKS[size_t(from)].upgrades)
        if (reaches(u, to)) return true;
    return false;
}

// Which of the classes on offer a bot takes: one on the way to its goal, otherwise any. Returns
// -1 to wait: the way to the goal opens at a higher level (Twin to Musket at level 45, for example).
int Room::botUpgrade(const TankData& t, const std::vector<int>& opts) {
    if (t.ai.goalDef >= 0) {
        std::vector<int> onPath;
        for (int o : opts)
            if (reaches(o, t.ai.goalDef)) onPath.push_back(o);
        if (!onPath.empty()) return onPath[size_t(rng_.irange(0, int(onPath.size()) - 1))];
        if (t.def != t.ai.goalDef && reaches(t.def, t.ai.goalDef)) return -1;
    }
    return opts[size_t(rng_.irange(0, int(opts.size()) - 1))];
}

// A weaving bullet swings from side to side of the line it was fired along. After `dist` it is
// off that line by a known amount (the swing is a sine wave: see Room::integrate), so a tank that
// aims off by the opposite angle hits. Zero for classes whose bullets fly straight, and for classes
// whose barrels weave to both sides, where no single angle suits both.
float Room::waveAimFix(const Entity& e, float dist) const {
    const TankDef& d = TANKS[e.tank->def];
    if (d.wave == 0 || d.waveSym) return 0;
    const float flight = std::max(0.f, (dist - e.size * 1.9f) / bulletSpeedOf(e));  // ticks in the air
    const float off = d.wave * BULLET_SPEED / WAVE_FREQ * std::sin(WAVE_FREQ * flight);
    return -std::atan2(off, std::max(dist, 60.f));
}

void Room::botSpend(Entity& e) {
    TankData& t = *e.tank;
    const TankDef& d = TANKS[t.def];
    auto open = [&](int i) { return ((d.statMask >> i) & 1) && t.skills[i] < d.statCap; };  // this class's own stats and cap
    int best = -1;
    float bestScore = 0;
    if (t.ai.build >= 0 || d.engine) {
        // A preset build: put the point where the build is furthest behind.
        const int* want = d.engine ? SMASHER_BUILD : BOT_BUILDS[t.ai.build];
        for (int i = 0; i < S_COUNT; i++) {
            if (!open(i) || want[i] <= t.skills[i]) continue;
            float s = float(want[i] - t.skills[i]) / float(want[i]) + 0.02f * float(want[i]) + rng_.uni() * 0.05f;
            if (s > bestScore) { bestScore = s; best = i; }
        }
    } else {
        // Easy bots: loose preferences, no plan.
        static const int ramBuild[S_COUNT] = {5, 5, 0, 0, 0, 0, 4, 0, 3, 3};
        const int* w = d.ai == AI_RAMMER ? ramBuild : t.ai.weights;
        for (int i = 0; i < S_COUNT; i++) {
            if (!open(i) || w[i] <= 0) continue;
            float s = w[i] / (t.skills[i] + 1.f) + rng_.uni() * 0.3f;
            if (s > bestScore) { bestScore = s; best = i; }
        }
    }
    if (best < 0)
        for (int i = 0; i < S_COUNT; i++)
            if (open(i)) { best = i; break; }
    if (best < 0) return;
    t.skills[best]++;
    t.skillPts--;
    refresh(e);
}

// Sidestep a shot that is about to hit. Looks at enemy bullets nearby, works out which would pass
// within a body's width and soonest, and steps to the side it is already on. Easy bots never
// dodge, normal bots look a short way and miss some, hard bots look further and always react.
bool Room::botDodge(Entity& e, Vec2& mv) {
    BotBrain& b = e.tank->ai;
    if (b.level == 0) return false;
    if (tickNo < b.dodgeUntil) {  // still stepping aside
        mv = mv * 0.4f + b.dodge;
        return true;
    }
    if ((tickNo + b.offset) % (b.level == 2 ? 2 : 4) != 0) return false;
    const float look = b.level == 2 ? 300.f : 210.f;
    float soonest = 1e9f;
    Vec2 side;
    query(e.pos, look, [&](Entity& p) {
        if (p.kind != K_BULLET || p.team == e.team || p.heal) return;
        const Vec2 v = p.vel;
        const float v2 = v.len2();
        if (v2 < 4) return;
        const Vec2 r = e.pos - p.pos;
        const float tca = (r.x * v.x + r.y * v.y) / v2;  // ticks until it is closest to us
        if (tca <= 0 || tca > 28) return;
        const Vec2 miss = r - v * tca;                   // where we would be relative to it then
        const float need = e.size + p.size + 8;
        if (miss.len2() > need * need || tca >= soonest) return;
        soonest = tca;
        Vec2 n = Vec2{-v.y, v.x}.norm();
        float sgn = (miss.x * n.x + miss.y * n.y) >= 0 ? 1.f : -1.f;
        if (miss.len2() < 4) sgn = b.strafe;  // dead centre: either side will do
        side = n * sgn;
    });
    if (soonest > 1e8f) return false;
    if (b.level == 1 && !rng_.chance(0.6f)) return false;  // normal bots do not see every shot coming
    b.dodge = side * 1.6f;
    b.dodgeUntil = tickNo + (b.level == 2 ? 7 : 6);
    statDodges++;
    mv = mv * 0.4f + b.dodge;
    return true;
}

Vec2 Room::pickGoal(Entity& e) {
    const bool bot = e.tank->role == R_PLAYER;  // (bosses pick goals here as well)
    auto around = [&](Vec2 at, float lo, float hi) {  // somewhere near a point that is not inside a wall
        for (int tries = 0; tries < 10; tries++) {
            Vec2 p = at + fromAngle(rng_.range(0, TAU), rng_.range(lo, hi));
            if (openSpot(p, 60)) return p;
        }
        return at;
    };
    if (bot && mode == M_BR && brBegun_)  // stay well inside the safe circle
        return around(brCentre_, 0, std::max(60.f, brRadius_ * 0.6f));
    if (bot && mode == M_KOTH && rng_.chance(0.75f)) return around(hill_, 0, KOTH_RADIUS * 0.8f);
    if (bot && mode == M_MOTHER && mothers_.size() == 2) {  // attack theirs, or stay with ours
        float u = rng_.uni();
        Entity* theirs = get(mothers_[e.team == 0 ? 1 : 0].ent);
        Entity* ours = get(mothers_[e.team == 0 ? 0 : 1].ent);
        if (u < 0.55f && theirs) return around(theirs->pos, 420, 600);
        if (u < 0.8f && ours) return around(ours->pos, 250, 450);
    }
    if (bot && mode == M_CTF && flags_.size() == 2 && e.team >= 0 && e.team < 2) {
        const Flag& own = flags_[size_t(e.team)];
        const Flag& theirs = flags_[size_t(1 - e.team)];
        float u = rng_.uni();
        if (!own.atHome && u < 0.45f) return own.pos;                                   // get our flag back
        if (theirs.carrier && u < 0.75f) return around(theirs.pos, 120, 320);          // escort our carrier
        if (!theirs.carrier && u < 0.6f) return theirs.pos;                             // go and take theirs
        if (u < 0.8f) return around(own.home, 150, 420);                                // guard ours
    }
    if (bot && mode == M_SIEGE && e.team == 1 && rng_.chance(0.65f)) {  // defenders stand by a dominator that can still fall
        std::vector<Vec2> opts;
        for (const Dom& d : doms_) {
            Entity* de = get(d.ent);
            if (de && de->team == 1 && !de->tank->invincible) opts.push_back(d.pos);
        }
        if (!opts.empty()) return around(opts[size_t(rng_.irange(0, int(opts.size()) - 1))] - Vec2{220, 0}, 150, 420);
    }
    // The bounty: now and then a bot goes after the leader (hard bots more often, easy bots never).
    if (bot && leaderId_ && leaderId_ != e.id && e.tank->ai.level > 0 && rng_.chance(e.tank->ai.level == 2 ? 0.3f : 0.18f))
        if (Entity* lead = get(leaderId_))
            if (lead->team != e.team) return around(lead->pos, 200, 420);
    if (rushMode() && e.tank->role == R_PLAYER) {  // go and meet the wave; between waves, stay by a sanctuary
        if (!waveBosses_.empty()) {
            if (Entity* boss = get(waveBosses_[size_t(rng_.irange(0, int(waveBosses_.size()) - 1))]))
                return boss->pos + fromAngle(rng_.range(0, TAU), 520);
        }
        std::vector<Vec2> home;
        // a fallen sanctuary comes first: go and win it back
        for (const Dom& d : sancts_)
            if (!sanctuaryHeld(d) && rng_.chance(0.6f)) return d.pos + fromAngle(rng_.range(0, TAU), 420);
        for (const Dom& d : sancts_)
            if (sanctuaryHeld(d)) home.push_back(d.pos);
        if (!home.empty()) return home[size_t(rng_.irange(0, int(home.size()) - 1))] + fromAngle(rng_.range(0, TAU), rng_.range(200, 500));
    }
    if (!doms_.empty() && e.tank->role == R_PLAYER && rng_.chance(0.75f)) {  // go and fight over a dominator we don't hold
        std::vector<Vec2> opts;
        for (const Dom& d : doms_) {
            Entity* de = get(d.ent);
            if (de && de->team != e.team && !de->tank->invincible) opts.push_back(d.pos);
        }
        if (!opts.empty()) {
            Vec2 at = opts[rng_.irange(0, int(opts.size()) - 1)];
            for (int tries = 0; tries < 10; tries++) {  // somewhere near it that is not inside a wall
                Vec2 p = at + fromAngle(rng_.range(0, TAU), 430);
                if (openSpot(p, 60)) return p;
            }
            return at;
        }
    }
    if (!cols_ && rng_.chance(0.35f))  // the middle of the map is where the good polygons are
        return Vec2{W / 2, H / 2} + fromAngle(rng_.range(0, TAU), rng_.range(120, 750));
    for (int tries = 0; tries < 20; tries++) {
        Vec2 p = randomOpenPoint(70);
        bool bad = false;
        for (const Base& b : bases_)
            if (b.team != e.team && p.x > b.x - 520 && p.x < b.x + b.w + 520 && p.y > b.y - 520 && p.y < b.y + b.h + 520)
                bad = true;  // enemy protectors reach well outside their base
        if (!bad) return p;
    }
    return {W / 2, H / 2};
}

// Where should a body at `from` head next to reach `to`? Straight there when the way is clear,
// otherwise the next cell on the shortest path through the maze (breadth-first search on the wall grid).
Vec2 Room::pathStep(Vec2 from, Vec2 to, float radius) {
    if (!cols_) return to;
    Vec2 d = (to - from).norm();
    Vec2 side{-d.y * radius, d.x * radius};
    if (los(from + side, to + side) && los(from - side, to - side)) return to;

    auto cellOf = [&](float v, int n) { return std::clamp(int(v / cell_), 0, n - 1); };
    int fx = cellOf(from.x, cols_), fy = cellOf(from.y, rows_);
    int tx = cellOf(to.x, cols_), ty = cellOf(to.y, rows_);
    if ((fx == tx && fy == ty) || wallAt(tx, ty)) return to;

    const int n = cols_ * rows_, f = fy * cols_ + fx, goal = ty * cols_ + tx;
    bfs_.assign(n, -1);
    std::vector<int> q;
    q.reserve(n);
    q.push_back(goal);
    bfs_[goal] = 0;
    const int ox[4] = {1, -1, 0, 0}, oy[4] = {0, 0, 1, -1};
    for (size_t head = 0; head < q.size() && bfs_[f] < 0; head++) {
        int c = q[head], cx = c % cols_, cy = c / cols_;
        for (int k = 0; k < 4; k++) {
            int nx = cx + ox[k], ny = cy + oy[k];
            if (nx < 0 || ny < 0 || nx >= cols_ || ny >= rows_) continue;
            int o = ny * cols_ + nx;
            if (bfs_[o] >= 0 || (walls_[o] && o != f)) continue;
            bfs_[o] = bfs_[c] + 1;
            q.push_back(o);
        }
    }
    if (bfs_[f] < 0) return to;
    for (int k = 0; k < 4; k++) {
        int nx = fx + ox[k], ny = fy + oy[k];
        if (nx < 0 || ny < 0 || nx >= cols_ || ny >= rows_) continue;
        int o = ny * cols_ + nx;
        if (bfs_[o] == bfs_[f] - 1 && !walls_[o]) return {(nx + 0.5f) * cell_, (ny + 0.5f) * cell_};
    }
    return to;
}

void Room::botThink(Entity& e) {
    TankData& t = *e.tank;
    BotBrain& b = t.ai;
    int phase = tickNo + b.offset;

    if (t.skillPts > 0 && phase % 12 == 0) botSpend(e);
    // Bots use the level-up key too, just more slowly than a player holding it down.
    if (t.level < (mode == M_SANDBOX ? MAX_LEVEL : freeLevel) && phase % 20 == 0) addScore(e, levelScore(t.level + 1) - t.score);
    if (phase % 50 == 0) {
        std::vector<int> opts = upgradeOptions(t);
        const int pick = opts.empty() ? -1 : botUpgrade(t, opts);
        if (pick >= 0) setDef(e, pick);
    }
    const TankDef& d = TANKS[t.def];
    const bool ram = d.ai == AI_RAMMER;
    const bool truce = mode == M_BR && !brBegun_;  // Battle Royale lobby: tanks cannot hurt each other, so farm instead

    // Re-pick a target a few times a second: nearest visible enemy tank, otherwise something to farm.
    if (phase % 8 == 0) {
        Entity* best = nullptr;
        float bestScore = 1e12f;
        bool food = false;
        if (!truce) query(e.pos, 780 * d.fov, [&](Entity& o) {
            if (o.kind != K_TANK || !targetable(o, e.team)) return;
            float dist = (o.pos - e.pos).len();
            // Prefer real tanks over dominators and bosses, except where the thing itself is the objective.
            const bool objective = o.tank->role == R_MOTHER || (mode == M_SIEGE && o.tank->role == R_DOM);
            if (o.tank->role != R_PLAYER && !objective) dist += 220;
            if (carriedBy(o.id)) dist -= 300;                                      // stop the flag carrier
            if (b.level == 2) dist -= 160 * (1 - o.hp / std::max(1.f, o.maxHp));  // hard bots finish off the wounded
            if (dist < bestScore && los(e.pos, o.pos)) { bestScore = dist; best = &o; }
        });
        // The Necromancer line fights with the shapes it has converted. While it has few drones it
        // goes for squares first and only turns on a tank that is close.
        const bool gather = d.necro && t.drones < d.maxDrones * 6 / 10 && b.level > 0;
        if (gather && best && (best->pos - e.pos).len2() > 330 * 330) {
            best = nullptr;
            bestScore = 1e12f;
        }
        if (!best) {
            query(e.pos, gather ? 700.f : 520.f, [&](Entity& o) {
                if (o.kind != K_FOOD) return;
                float dist = (o.pos - e.pos).len() - std::min(o.xp, 300) * 0.5f;  // richer food is worth a detour
                if (gather && (o.crasher || o.tier != T_NORMAL || !((d.necro >> o.shape) & 1))) dist += 2000;  // only what it can convert
                if (dist < bestScore && los(e.pos, o.pos)) { bestScore = dist; best = &o; food = true; }
            });
        }
        b.target = best ? best->id : 0;
        b.targetIsFood = food;
        const float wobble = b.level == 0 ? 0.16f : b.level == 2 ? 0.03f : 0.07f;  // how steady the bot's hand is
        b.aimNoise = rng_.range(-wobble, wobble);
        if (--b.strafeTimer <= 0) {
            b.strafe = -b.strafe;
            b.strafeTimer = rng_.irange(4, 14);
        }
    }

    Entity* tg = get(b.target);
    if (tg && tg->team == e.team) tg = nullptr;  // e.g. a dominator our team just captured
    float hpFrac = (e.hp + e.shield) / std::max(1.f, e.maxHp + e.maxShield);
    Vec2 mv, aim = t.aim;
    bool fire = false, alt = false;

    // Somewhere the bot must go whatever else is happening: home with the flag, to a flag lying
    // close by, back inside the Battle Royale circle.
    bool must = false;
    Vec2 mustGo;
    if (mode == M_CTF && flags_.size() == 2 && e.team >= 0 && e.team < 2) {
        const Flag& own = flags_[size_t(e.team)];
        const Flag& theirs = flags_[size_t(1 - e.team)];
        if (theirs.carrier == e.id) { must = true; mustGo = own.home; }
        else if (!theirs.carrier && (theirs.pos - e.pos).len2() < 650 * 650) { must = true; mustGo = theirs.pos; }
        else if (!own.atHome && !own.carrier && (own.pos - e.pos).len2() < 650 * 650) { must = true; mustGo = own.pos; }
    }
    if (mode == M_BR && brBegun_ && !inZone(e.pos, 170)) { must = true; mustGo = brCentre_; }

    if (tg) {
        Vec2 to = tg->pos - e.pos;
        float dist = to.len();
        Vec2 dir = to.norm(), perp{-dir.y, dir.x};
        const float shotSpeed = bulletSpeedOf(e);
        float lead = std::min(dist / shotSpeed, 45.f) * (b.level == 0 ? 0.5f : 1.f);  // easy bots under-lead a moving target
        Vec2 aimAt = tg->pos + tg->vel * (b.targetIsFood ? 0.f : lead);
        // Classes whose bullets weave (Desmos line): a bullet is back on its line every half swing,
        // so there are distances at which it hits and distances at which it passes either side.
        // Work out how far off the line it will be when it arrives and aim off by that much. A class
        // whose barrels weave to both sides (Helix, Triplex) cannot aim off for both, so it fights
        // from a distance where the two streams cross (see `want` below).
        const float waveFix = b.level > 0 ? waveAimFix(e, dist) : 0;
        const float muzzle = e.size * 1.9f;
        aim = fromAngle((aimAt - e.pos).angle() + b.aimNoise + waveFix, std::max(dist, 60.f));
        fire = true;
        alt = d.altFire;  // classes with a secondary gun fire it together with the main one

        float want;  // preferred fighting distance
        if (ram) want = 0;
        else if (b.targetIsFood) want = 150 + tg->size;
        else if (d.ai == AI_SNIPER) want = 520;
        else if (d.ai == AI_DRONES) want = 460;
        else want = 320;
        if (tg->tank && tg->tank->role != R_PLAYER && !ram)
            want = std::max(want, tg->tank->role == R_MOTHER ? 430.f : 640.f);  // outside a Gunner Dominator's short sight
        if (b.targetIsFood && d.necro && t.drones < d.maxDrones) want = 0;  // necromancers convert shapes by touching them
        float band = 50;  // how far from `want` the bot lets the distance drift
        if (d.wave != 0 && !b.targetIsFood && b.level > 0) {  // weaving bullets: the nearest distance at which they are on their line
            const float node = PI / WAVE_FREQ * shotSpeed;
            want = muzzle + node * std::max(1.f, std::round((want - muzzle) / node));
            band = 20;
        }

        const float fleeAt = ram ? 0.2f : b.level == 0 ? 0.2f : b.level == 2 ? 0.36f : 0.3f;
        bool flee = !b.targetIsFood && hpFrac < fleeAt;
        if (flee) {
            mv = dir * -1.f;
            // Rear thrusters (Tri-Angle line) push the tank the way it faces: turn round and let them
            // carry it away, instead of facing the enemy and being pushed back into the fight.
            if (d.thrust > 3) {
                aim = dir * -200.f;
                alt = false;  // the front barrel of a Falcon or an Eagle would push it back
            }
        }
        else if (dist > want + band) mv = (pathStep(e.pos, tg->pos, e.size) - e.pos).norm();
        else if (dist < want - band) mv = dir * -1.f;
        if (!b.targetIsFood && !ram) mv += perp * (0.7f * b.strafe);
        if (must) mv = (pathStep(e.pos, mustGo, e.size) - e.pos).norm() + perp * (0.25f * b.strafe);
    } else if (must) {
        mv = (pathStep(e.pos, mustGo, e.size) - e.pos).norm();
        if (mv.len2() > 0.1f) aim = mv * 120;
    } else {
        if (--b.goalTimer <= 0 || (b.goal - e.pos).len2() < 160 * 160) {
            b.goal = pickGoal(e);
            b.goalTimer = 20 * TICK_RATE;
        }
        mv = (pathStep(e.pos, b.goal, e.size) - e.pos).norm();
        if (mv.len2() > 0.1f) aim = mv * 120;
    }

    if (!ram) botDodge(e, mv);

    // Never wander into an enemy base.
    for (const Base& bs : bases_) {
        if (bs.team == e.team) continue;
        const float m = 170;
        if (e.pos.x > bs.x - m && e.pos.x < bs.x + bs.w + m && e.pos.y > bs.y - m && e.pos.y < bs.y + bs.h + m)
            mv = (e.pos - Vec2{bs.x + bs.w / 2, bs.y + bs.h / 2}).norm() * 2 + mv * 0.3f;
    }

    t.move = mv.norm();
    t.aim = aim;
    t.fire = fire;
    t.alt = fire && alt;
}

// Dominators are fixed turrets: track the nearest enemy tank in range and shoot.
void Room::domThink(Entity& e) {
    TankData& t = *e.tank;
    BotBrain& b = t.ai;
    const TankDef& d = TANKS[t.def];
    if ((tickNo + t.dom * 3) % 6 == 0) {
        float bestD = 1e12f;
        b.target = 0;
        query(e.pos, d.reach, [&](Entity& o) {  // each kind of dominator sees a different distance
            if (o.kind != K_TANK || !targetable(o, e.team) || o.tank->role == R_DOM) return;
            float dd = (o.pos - e.pos).len2();
            if (dd < bestD && los(e.pos, o.pos)) { bestD = dd; b.target = o.id; }  // not through maze walls
        });
    }
    Entity* tg = get(b.target);
    if (tg && tg->team == e.team) tg = nullptr;
    if (d.ai == AI_TRAPPER) {
        e.angle = wrapAngle(e.angle + 0.01f);
        t.fire = t.role == R_DOM || tg != nullptr;  // trapper dominators fire all the time; sanctuaries only at intruders
    } else if (tg) {
        Vec2 to = tg->pos - e.pos;
        Vec2 aimAt = tg->pos + tg->vel * (to.len() / bulletSpeedOf(e) * 0.9f);
        float diff = wrapAngle((aimAt - e.pos).angle() - e.angle);
        e.angle = wrapAngle(e.angle + clampf(diff, -0.12f, 0.12f));
        t.fire = std::fabs(diff) < 0.25f;
    } else {
        t.fire = false;
        e.angle = wrapAngle(e.angle + 0.008f);
    }
    t.aim = fromAngle(e.angle, 100);
}

// Base protectors never move: they spin and send their swarm at whoever comes too close.
void Room::protectorThink(Entity& e) {
    TankData& t = *e.tank;
    e.angle = wrapAngle(e.angle + 0.02f);
    if ((tickNo + int(e.id)) % 6 == 0) t.ai.target = pickTarget(e.pos, e.team, 760, 0, PI, false);
    Entity* tg = get(t.ai.target);
    if (tg && !targetable(*tg, e.team)) tg = nullptr;
    t.fire = tg != nullptr;
    if (tg) t.aim = tg->pos - e.pos;  // the swarm flies to wherever the protector "aims"
}

// Arena closers hunt down every remaining player and bot, wherever they are.
void Room::closerThink(Entity& e) {
    TankData& t = *e.tank;
    BotBrain& b = t.ai;
    if (mode == M_BR && !closing()) {
        // Battle Royale: the closers keep to the ground outside the safe circle and destroy
        // whoever is still out there. They circle the edge while there is nobody to hunt.
        if ((tickNo + b.offset) % 10 == 0) {
            float bestD = 1e30f;
            b.target = 0;
            for (Entity* o : ents) {
                if (o->dead || o->hp <= 0 || o->kind != K_TANK || o->tank->role != R_PLAYER || inZone(o->pos)) continue;
                float dd = (o->pos - e.pos).len2();
                if (dd < bestD) { bestD = dd; b.target = o->id; }
            }
        }
        Vec2 out = e.pos - brCentre_;
        const float dist = out.len();
        out = dist > 1 ? out * (1 / dist) : Vec2{1, 0};
        Entity* tg = get(b.target);
        if (tg && inZone(tg->pos)) tg = nullptr;
        t.fire = false;
        if (dist < brRadius_ + 150) {  // too close to the circle: back out
            t.move = out;
        } else if (tg) {
            Vec2 to = tg->pos - e.pos;
            Vec2 at = tg->pos + tg->vel * std::min(to.len() / bulletSpeedOf(e), 30.f) - e.pos;
            e.angle = at.angle();
            t.fire = to.len() < 900;
            t.move = to.len() > 260 ? to.norm() : Vec2{};
            if (dist < brRadius_ + 260) t.move = (t.move + out).norm();
        } else {
            Vec2 along{-out.y, out.x};
            t.move = (along * 0.5f + out * clampf((brRadius_ + 300 - dist) / 200, -1, 1)).norm() * 0.5f;
            e.angle = wrapAngle(e.angle + 0.03f);
        }
        t.aim = fromAngle(e.angle, 100);
        return;
    }
    if ((tickNo + b.offset) % 10 == 0) {
        float bestD = 1e30f;
        b.target = 0;
        for (Entity* o : ents) {
            if (o->dead || o->hp <= 0 || o->kind != K_TANK || o->tank->role != R_PLAYER) continue;
            float d = (o->pos - e.pos).len2() * (1 + 0.2f * float((o->id + e.id) % 4));  // spread out over different victims
            if (d < bestD) { bestD = d; b.target = o->id; }
        }
    }
    Entity* tg = get(b.target);
    if (!tg) {
        t.fire = false;
        t.move = (Vec2{W / 2, H / 2} - e.pos).norm() * 0.3f;
        e.angle = wrapAngle(e.angle + 0.03f);
        t.aim = fromAngle(e.angle, 100);
        return;
    }
    Vec2 to = tg->pos - e.pos;
    float dist = to.len();
    Vec2 at = tg->pos + tg->vel * std::min(dist / bulletSpeedOf(e), 30.f) - e.pos;
    e.angle = at.angle();
    t.aim = at;
    t.fire = dist < 1000;
    t.move = dist > 260 ? to.norm() : Vec2{};
}

// Bosses lumber towards the nearest tank and let their weapons do the work.
void Room::bossThink(Entity& e) {
    TankData& t = *e.tank;
    BotBrain& b = t.ai;
    const TankDef& d = TANKS[t.def];
    if ((tickNo + b.offset) % 8 == 0) {
        float bestD = 1e12f;
        b.target = 0;
        query(e.pos, 950, [&](Entity& o) {
            if (o.kind != K_TANK || (o.tank->role != R_PLAYER && o.tank->role != R_SANCT) || !targetable(o, e.team)) return;
            if (o.tank->role == R_SANCT && o.team != 0) return;  // a fallen sanctuary is no longer worth attacking
            float dd = (o.pos - e.pos).len2();
            if (dd < bestD && los(e.pos, o.pos)) { bestD = dd; b.target = o.id; }
        });
    }
    Entity* tg = get(b.target);
    if (tg && !targetable(*tg, e.team)) tg = nullptr;
    Vec2 mv;
    if (tg) {
        Vec2 to = tg->pos - e.pos;
        float dist = to.len();
        float want = d.ai == AI_DRONES ? 520.f : d.ai == AI_RAMMER ? 0.f : 360.f;  // Gersemi charges
        if (dist > want + 60) mv = (pathStep(e.pos, tg->pos, e.size) - e.pos).norm();
        else if (dist < want - 120) mv = to.norm() * -1.f;
        if (rushMode()) {  // Boss Rush: keep marching on the nearest sanctuary while shooting whoever is close
            float bestD = 1e30f;
            Vec2 home = e.pos;
            for (const Dom& s : sancts_)
                if (sanctuaryHeld(s) && (s.pos - e.pos).len2() < bestD) { bestD = (s.pos - e.pos).len2(); home = s.pos; }
            float reach = e.size + 260;
            if (bestD < 1e29f) mv = bestD > reach * reach ? (home - e.pos).norm() : Vec2{};  // none left standing: hunt the tanks instead
        }
        if (d.ai == AI_SPIN) {
            e.angle = wrapAngle(e.angle + d.spin);
            t.fire = dist < 850;
        } else {
            float diff = wrapAngle(to.angle() - e.angle);
            e.angle = wrapAngle(e.angle + clampf(diff, -0.07f, 0.07f));
            t.fire = d.ai == AI_DRONES || std::fabs(diff) < 0.5f;
        }
        t.aim = to;
    } else {
        if (--b.goalTimer <= 0 || (b.goal - e.pos).len2() < 200 * 200) {
            b.goal = pickGoal(e);
            b.goalTimer = 25 * TICK_RATE;
            if (rushMode()) {  // Boss Rush: march on the nearest sanctuary
                float bestD = 1e30f;
                for (const Dom& d : sancts_)
                    if (sanctuaryHeld(d) && (d.pos - e.pos).len2() < bestD) { bestD = (d.pos - e.pos).len2(); b.goal = d.pos; }
                b.goalTimer = 5 * TICK_RATE;
            }
        }
        mv = (pathStep(e.pos, b.goal, e.size) - e.pos).norm();
        e.angle = wrapAngle(e.angle + 0.012f);
        t.fire = false;
        t.aim = fromAngle(e.angle, 200);
    }
    for (const Base& bs : bases_) {  // keep out of the bases
        const float m = 320;
        if (e.pos.x > bs.x - m && e.pos.x < bs.x + bs.w + m && e.pos.y > bs.y - m && e.pos.y < bs.y + bs.h + m)
            mv = (e.pos - Vec2{bs.x + bs.w / 2, bs.y + bs.h / 2}).norm() * 2 + mv * 0.3f;
    }
    t.move = mv.norm();
}

// Crashers are the polygons that fight back: they chase the nearest tank they can see and ram it.
// They leave tanks alone that are inside a team base, freshly spawned or invisible.
void Room::crasherThink(Entity& e) {
    if ((tickNo + int(e.id)) % 10 == 0) {
        float bestD = 1e12f;
        e.tgt = 0;
        query(e.pos, 540, [&](Entity& o) {
            if (o.kind != K_TANK || o.tank->role != R_PLAYER || !targetable(o, e.team)) return;
            for (const Base& b : bases_)
                if (o.pos.x > b.x - 80 && o.pos.x < b.x + b.w + 80 && o.pos.y > b.y - 80 && o.pos.y < b.y + b.h + 80) return;
            float d = (o.pos - e.pos).len2();
            if (d < bestD && los(e.pos, o.pos)) { bestD = d; e.tgt = o.id; }
        });
    }
    Entity* tg = get(e.tgt);
    if (tg && targetable(*tg, e.team)) {
        Vec2 dir = (tg->pos + tg->vel * 4 - e.pos).norm();
        float top = e.tier ? 9.8f : 12.5f;  // after drag: about 4.4 and 5.6 units a tick, faster than most tanks
        e.vel += (dir * top - e.vel) * 0.09f;
        e.angle = e.vel.angle();
        e.spin = 0;
    } else {
        e.tgt = 0;
        e.spin = 0.02f;
    }
}
