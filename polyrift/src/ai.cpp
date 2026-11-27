// Bot and dominator behaviour. Bots use exactly the same inputs a player has
// (move direction, aim point, fire) so they obey every rule players do.
#include "room.hpp"

float Room::bulletSpeedOf(const Entity& e) const {
    const TankData& t = *e.tank;
    for (const GunDef& g : TANKS[t.def].guns)
        if (g.type == G_BULLET) return BULLET_SPEED * g.speed * (1 + 0.06f * t.skills[S_BSPEED]);
    return BULLET_SPEED;
}

void Room::botSpend(Entity& e) {
    TankData& t = *e.tank;
    // Rammers: body, health, shields and their engine (the Smasher line's Reload slot is Engine Acceleration).
    static const int ramBuild[S_COUNT] = {5, 5, 0, 0, 0, 0, 4, 0, 3, 3};
    const TankDef& d = TANKS[t.def];
    const int* w = d.ai == AI_RAMMER ? ramBuild : t.ai.weights;
    auto open = [&](int i) { return ((d.statMask >> i) & 1) && t.skills[i] < d.statCap; };  // this class's own stats and cap
    int best = -1;
    float bestScore = 0;
    for (int i = 0; i < S_COUNT; i++) {
        if (!open(i) || w[i] <= 0) continue;
        float s = w[i] / (t.skills[i] + 1.f) + rng_.uni() * 0.3f;
        if (s > bestScore) { bestScore = s; best = i; }
    }
    if (best < 0)
        for (int i = 0; i < S_COUNT; i++)
            if (open(i)) { best = i; break; }
    if (best < 0) return;
    t.skills[best]++;
    t.skillPts--;
    refresh(e);
}

Vec2 Room::pickGoal(Entity& e) {
    if (mode == M_RUSH && e.tank->role == R_PLAYER) {  // go and meet the wave; between waves, stay by a sanctuary
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
    if (domMode() && e.tank->role == R_PLAYER && rng_.chance(0.75f)) {  // go and fight over a dominator we don't hold
        std::vector<Vec2> opts;
        for (const Dom& d : doms_) {
            Entity* de = get(d.ent);
            if (de && de->team != e.team) opts.push_back(d.pos);
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
    if (t.level < freeLevel && phase % 20 == 0) addScore(e, levelScore(t.level + 1) - t.score);
    if (phase % 50 == 0) {
        std::vector<int> opts = upgradeOptions(t);
        if (!opts.empty()) setDef(e, opts[rng_.irange(0, int(opts.size()) - 1)]);
    }
    const TankDef& d = TANKS[t.def];
    const bool ram = d.ai == AI_RAMMER;

    // Re-pick a target a few times a second: nearest visible enemy tank, otherwise something to farm.
    if (phase % 8 == 0) {
        Entity* best = nullptr;
        float bestScore = 1e12f;
        bool food = false;
        query(e.pos, 780 * d.fov, [&](Entity& o) {
            if (o.kind != K_TANK || !targetable(o, e.team)) return;
            float dist = (o.pos - e.pos).len();
            if (o.tank->role != R_PLAYER) dist += 220;  // prefer real tanks over dominators and bosses
            if (dist < bestScore && los(e.pos, o.pos)) { bestScore = dist; best = &o; }
        });
        if (!best) {
            query(e.pos, 520, [&](Entity& o) {
                if (o.kind != K_FOOD) return;
                float dist = (o.pos - e.pos).len() - std::min(o.xp, 300) * 0.5f;  // richer food is worth a detour
                if (dist < bestScore && los(e.pos, o.pos)) { bestScore = dist; best = &o; food = true; }
            });
        }
        b.target = best ? best->id : 0;
        b.targetIsFood = food;
        b.aimNoise = rng_.range(-0.07f, 0.07f);
        if (--b.strafeTimer <= 0) {
            b.strafe = -b.strafe;
            b.strafeTimer = rng_.irange(4, 14);
        }
    }

    Entity* tg = get(b.target);
    if (tg && tg->team == e.team) tg = nullptr;  // e.g. a dominator our team just captured
    float hpFrac = (e.hp + e.shield) / std::max(1.f, e.maxHp + e.maxShield);
    Vec2 mv, aim = t.aim;
    bool fire = false;

    if (tg) {
        Vec2 to = tg->pos - e.pos;
        float dist = to.len();
        Vec2 dir = to.norm(), perp{-dir.y, dir.x};
        float lead = std::min(dist / bulletSpeedOf(e), 45.f);
        Vec2 aimAt = tg->pos + tg->vel * (b.targetIsFood ? 0.f : lead);
        aim = fromAngle((aimAt - e.pos).angle() + b.aimNoise, std::max(dist, 60.f));
        fire = true;

        float want;  // preferred fighting distance
        if (ram) want = 0;
        else if (b.targetIsFood) want = 150 + tg->size;
        else if (d.ai == AI_SNIPER) want = 520;
        else if (d.ai == AI_DRONES) want = 460;
        else want = 320;
        if (tg->tank && tg->tank->role != R_PLAYER && !ram) want = std::max(want, 640.f);  // outside a Gunner Dominator's short sight
        if (b.targetIsFood && d.necro && t.drones < d.maxDrones) want = 0;  // necromancers convert shapes by touching them

        bool flee = !b.targetIsFood && hpFrac < (ram ? 0.2f : 0.3f);
        if (flee) mv = dir * -1.f;
        else if (dist > want + 50) mv = (pathStep(e.pos, tg->pos, e.size) - e.pos).norm();
        else if (dist < want - 50) mv = dir * -1.f;
        if (!b.targetIsFood && !ram) mv += perp * (0.7f * b.strafe);
    } else {
        if (--b.goalTimer <= 0 || (b.goal - e.pos).len2() < 160 * 160) {
            b.goal = pickGoal(e);
            b.goalTimer = 20 * TICK_RATE;
        }
        mv = (pathStep(e.pos, b.goal, e.size) - e.pos).norm();
        if (mv.len2() > 0.1f) aim = mv * 120;
    }

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
    t.alt = fire && d.altFire;  // classes with a secondary gun fire it together with the main one
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
        if (mode == M_RUSH) {  // Boss Rush: keep marching on the nearest sanctuary while shooting whoever is close
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
            if (mode == M_RUSH) {  // Boss Rush: march on the nearest sanctuary
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
