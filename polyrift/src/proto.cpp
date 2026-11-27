// Wire protocol.
//   server -> client : JSON text frames for rare events (init, leaderboard, messages, death)
//                      and one compact binary frame per tick with everything the player can see.
//   client -> server : tiny binary frames (see Room::onMessage).
#include <cstdio>

#include "room.hpp"

uint8_t Room::teamColor(int team) {
    if (team >= 0 && team < 4) return uint8_t(team);
    if (team == TEAM_FOOD) return C_NEUTRAL;  // uncaptured dominators and fallen sanctuaries are drawn yellow
    return C_RED;
}

const char* Room::teamName(int team) {
    static const char* names[4] = {"BLUE", "GREEN", "RED", "PURPLE"};
    return (team >= 0 && team < 4) ? names[team] : "NOBODY";
}

// In free-for-all modes every tank is its own team: show yourself blue and everyone else red.
uint8_t Room::colorFor(const Entity& e, int viewerTeam) const {
    if (teamCount_ == 0 && e.kind != K_FOOD && e.team >= 0) return e.team == viewerTeam ? C_BLUE : C_RED;
    return e.color;
}

static std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.4g", v);
    return b;
}

void Room::sendMsg(Client* c, const std::string& text) {
    if (!send) return;
    std::string m = "{\"t\":\"msg\",\"text\":\"" + jsonEscape(text) + "\"}";
    if (c) send(*c, m, false);
    else
        for (Client* x : clients) send(*x, m, false);
}

void Room::sendChat(const Entity& from, const std::string& name, const std::string& text) {
    if (!send) return;
    const std::string head = "{\"t\":\"chat\",\"id\":" + std::to_string(from.id) + ",\"name\":\"" + jsonEscape(name) + "\",\"text\":\"" +
                             jsonEscape(text) + "\",\"color\":";
    for (Client* x : clients) {  // the sender's colour depends on who is looking (team colours, or blue for yourself in FFA)
        const Entity* me = get(x->tankId);
        send(*x, head + std::to_string(colorFor(from, me ? me->team : x->team)) + "}", false);
    }
}

void Room::sendBanner(const std::string& text, const std::string& sub, int color) {
    if (!send) return;
    std::string m = "{\"t\":\"banner\",\"text\":\"" + jsonEscape(text) + "\",\"sub\":\"" + jsonEscape(sub) + "\",\"color\":" +
                    std::to_string(color) + "}";
    for (Client* x : clients) send(*x, m, false);
}

static std::string gunsJson(const std::vector<GunDef>& guns) {
    std::string s = "[";
    for (size_t g = 0; g < guns.size(); g++) {
        const GunDef& gd = guns[g];
        if (g) s += ",";
        s += "[" + num(gd.ang) + "," + num(gd.len) + "," + num(gd.w) + "," + num(gd.asp) + "," + num(gd.ox) + "," +
             std::to_string(int(gd.type)) + "," + (gd.heal ? "1" : "0") + "]";
    }
    return s + "]";
}

void Room::sendInit(Client& c) {
    if (!send) return;
    std::string s = "{\"t\":\"init\",\"proto\":" + std::to_string(PROTOCOL_VERSION) + ",\"slot\":\"" + jsonEscape(slot) + "\",\"mode\":\"" + key +
                    "\",\"title\":\"" + title + "\"";
    s += ",\"w\":" + num(W) + ",\"h\":" + num(H) + ",\"tick\":" + std::to_string(TICK_RATE);
    s += ",\"teams\":" + std::to_string(teamCount_) + ",\"team\":" + std::to_string(c.team);
    s += ",\"maxLevel\":" + std::to_string(MAX_LEVEL) + ",\"maxSkill\":" + std::to_string(MAX_SKILL);
    s += ",\"freeLevel\":" + std::to_string(freeLevel) + ",\"chat\":" + (chat ? "1" : "0") + ",\"chatMax\":" + std::to_string(CHAT_MAX_CHARS);
    s += ",\"cell\":" + num(cell_) + ",\"cols\":" + std::to_string(cols_) + ",\"rows\":" + std::to_string(rows_);
    s += ",\"walls\":\"";
    for (uint8_t w : walls_) s.push_back(w ? '1' : '0');
    s += "\",\"bases\":[";
    for (size_t i = 0; i < bases_.size(); i++) {
        const Base& b = bases_[i];
        if (i) s += ",";
        s += "[" + num(b.x) + "," + num(b.y) + "," + num(b.w) + "," + num(b.h) + "," + std::to_string(teamColor(b.team)) + "]";
    }
    s += "],\"doms\":[";
    for (size_t i = 0; i < doms_.size(); i++) {
        if (i) s += ",";
        s += "[" + num(doms_[i].pos.x) + "," + num(doms_[i].pos.y) + "]";
    }
    s += "],\"sancts\":[";
    for (size_t i = 0; i < sancts_.size(); i++) {
        if (i) s += ",";
        s += "[" + num(sancts_[i].pos.x) + "," + num(sancts_[i].pos.y) + "]";
    }
    s += "],\"skills\":[";
    for (int i = 0; i < S_COUNT; i++) s += std::string(i ? "," : "") + "\"" + SKILL_NAMES[i] + "\"";
    s += "],\"foods\":[";
    for (int i = 0; i < F_COUNT; i++)
        s += std::string(i ? "," : "") + "{\"n\":\"" + FOODS[i].name + "\",\"s\":" + std::to_string(FOODS[i].sides) + "}";
    s += "],\"tiers\":[";
    for (int i = 0; i < T_COUNT; i++) s += std::string(i ? "," : "") + "\"" + TIERS[i].prefix + "\"";
    s += "],\"defs\":[";
    for (size_t i = 0; i < TANKS.size(); i++) {
        const TankDef& d = TANKS[i];
        if (i) s += ",";
        s += "{\"n\":\"" + jsonEscape(d.name) + "\",\"tier\":" + std::to_string(d.tier) + ",\"shape\":" +
             std::to_string(d.shape) + ",\"hid\":" + (d.hidden ? "1" : "0") + ",\"z\":" + (d.zoom > 0 ? "1" : "0") + ",\"guns\":" + gunsJson(d.guns) + ",\"tur\":[";
        bool waves = false;  // alt-fire mirrors the weave (Desmos family)
        for (const GunDef& g : d.guns) waves = waves || g.wave != 0;
        for (size_t k = 0; k < d.turrets.size(); k++) {
            const TurretDef& td = d.turrets[k];
            if (k) s += ",";
            s += "[" + num(td.ang) + "," + num(td.dist) + "," + num(td.size) + "," + gunsJson(td.guns) + "]";
        }
        s += "],\"up\":[";
        for (size_t k = 0; k < d.upgrades.size(); k++) s += std::string(k ? "," : "") + std::to_string(d.upgrades[k]);
        // which stats the class has, how many points each takes, and whether Reload is its Engine Acceleration
        s += "],\"cap\":" + std::to_string(d.statCap) + ",\"sm\":" + std::to_string(d.statMask) + ",\"eng\":" + (d.engine ? "1" : "0") +
             ",\"wv\":" + (waves ? "1" : "0") + ",\"af\":" + (d.altFire ? "1" : "0") + "}";
    }
    s += "],\"subs\":[";
    for (size_t i = 0; i < SUBS.size(); i++) s += std::string(i ? "," : "") + gunsJson(SUBS[i].guns);
    s += "]}";
    send(c, s, false);
}

void Room::sendUpdates() {
    if (!send) return;
    for (Client* c : clients) {
        Entity* me = get(c->tankId);
        int viewTeam = c->team;
        if (me) {
            const TankData& t = *me->tank;
            c->fov = 1700 * TANKS[t.def].fov * (1 + 0.006f * (t.level - 1));
            c->cam = me->pos;
            if (t.zoomAmt > 0) {  // zoom: slide the view, but keep the tank itself on screen
                Vec2 off = t.zoomDir * (TANKS[t.def].zoom * t.zoomAmt);
                off.x = clampf(off.x, -0.36f * c->fov, 0.36f * c->fov);
                off.y = clampf(off.y, -0.36f * c->fov * 0.5625f, 0.36f * c->fov * 0.5625f);
                c->cam += off;
            }
            viewTeam = me->team;
        } else {
            Entity* s = get(c->spectate);
            if (!s && !c->played && tickNo % 15 == 0) {  // on the menu: watch the current leader
                double top = -1;
                for (Entity* e : ents)
                    if (!e->dead && e->kind == K_TANK && e->tank->role == R_PLAYER && e->tank->score > top) {
                        top = e->tank->score;
                        c->spectate = e->id;
                    }
            }
            if (s) c->cam += (s->pos - c->cam) * 0.12f;
            c->fov += (2100 - c->fov) * 0.1f;
        }

        ByteWriter w;
        w.buf.reserve(4096);
        w.u8(1);
        w.u32(uint32_t(tickNo));
        w.u8(me ? 1 : 0);
        w.f32(c->cam.x);
        w.f32(c->cam.y);
        w.f32(c->fov);
        w.u32(me ? me->id : 0);
        if (me) {
            const TankData& t = *me->tank;
            double lo = levelScore(t.level), hi = levelScore(t.level + 1);
            w.u32(uint32_t(c->inDom ? c->domScore : t.score));
            w.u8(uint32_t(t.level));
            w.f32(t.level >= MAX_LEVEL ? 1.f : float((t.score - lo) / std::max(1.0, hi - lo)));
            w.u8(uint32_t(t.skillPts));
            for (int i = 0; i < S_COUNT; i++) w.u8(uint32_t(t.skills[i]));
            w.u16(uint32_t(t.def));
            std::vector<int> opts = upgradeOptions(t);
            w.u8(uint32_t(opts.size()));
            for (int o : opts) w.u16(uint32_t(o));
            // the kill bar: kills, assists, boss kills and polygons of this life (kept while driving a dominator)
            w.u16(uint32_t(std::min(65535, c->inDom ? c->domKills : t.kills)));
            w.u16(uint32_t(std::min(65535, c->inDom ? c->domAssists : t.assists)));
            w.u16(uint32_t(std::min(65535, c->inDom ? c->domBossKills : t.bossKills)));
            w.u16(uint32_t(std::min(65535, c->inDom ? c->domPolys : t.polys)));
        } else {
            w.u32(0); w.u8(0); w.f32(0); w.u8(0);
            for (int i = 0; i < S_COUNT; i++) w.u8(0);
            w.u16(0); w.u8(0);
            w.u16(0); w.u16(0); w.u16(0); w.u16(0);
        }

        size_t countAt = w.buf.size();
        w.u16(0);
        float hw = c->fov * 0.5f + 60, hh = c->fov * 0.5f * 0.5625f + 60;
        uint32_t n = 0;
        for (Entity* e : ents) {
            if (e->dead || e->hp <= 0) continue;
            if (std::fabs(e->pos.x - c->cam.x) > hw + e->size || std::fabs(e->pos.y - c->cam.y) > hh + e->size) continue;
            // Fully invisible enemies are not sent at all, so a modified client cannot reveal them.
            if (e->tank && e->tank->alpha < 0.02f && e != me && e->team != viewTeam) continue;
            uint32_t flags = 0;
            if (e->tank && e->tank->invuln > 0) flags |= 1;
            if (e->tank && e->tank->role != R_PLAYER) flags |= 2;  // computer-controlled: no shield bar, no score
            if (tickNo - e->hitTick <= 1) flags |= 4;
            if (e->tank && (e->tank->role == R_DOM || e->tank->role == R_PROTECTOR)) flags |= 8;  // no name tag
            uint32_t shape = e->shape;
            // polygons: shape (3 bits), size (2 bits), crasher (1 bit); the mutation travels as the colour
            if (e->kind == K_FOOD) shape = uint32_t(e->shape & 7) | uint32_t(e->tier & 3) << 3 | (e->crasher ? 32u : 0u);
            if (e->heal) shape = 255;  // healing bullet
            if (e->sub >= 0) shape = e->kind == K_BULLET ? uint32_t(e->sub + 1) : (e->shape | uint32_t(e->sub + 1) << 4);
            w.u32(e->id);
            w.u8(e->kind);
            w.u8(shape);
            w.u8(colorFor(*e, viewTeam));
            w.u8(flags);
            w.f32(e->pos.x);
            w.f32(e->pos.y);
            w.f32(e->angle);
            w.f32(e->size);
            w.u8(uint32_t(clampf(e->hp / e->maxHp, 0, 1) * 255 + 0.5f));
            w.u8(e->maxShield > 0 ? uint32_t(clampf(e->shield / e->maxShield, 0, 1) * 255 + 0.5f) : 0);
            if (e->kind == K_TANK) {
                const TankData& t = *e->tank;
                w.u16(uint32_t(t.def));
                w.u32(uint32_t(t.score));
                w.u8(uint32_t(t.level));
                w.u8(uint32_t(clampf(t.alpha, 0, 1) * 255 + 0.5f));
                const size_t nt = TANKS[t.def].turrets.size();  // the client knows the count from the class
                for (size_t k = 0; k < nt; k++) {
                    float a = k < t.tur.size() ? t.tur[k].ang : e->angle + TANKS[t.def].turrets[k].ang;
                    w.u8(uint32_t(int(std::floor(wrapAngle(a) / TAU * 256.f + 256.5f)) & 255));
                }
                w.str8(t.name);
            }
            if (++n >= 6000) break;
        }
        w.setU16(countAt, n);
        send(*c, w.buf, true);
    }
}

// Once a second: leaderboard, minimap blips and dominator ownership.
void Room::sendBoards() {
    if (!send || clients.empty()) return;
    std::vector<Entity*> tanks;
    for (Entity* e : ents)
        if (!e->dead && e->hp > 0 && e->kind == K_TANK && e->tank->role == R_PLAYER) tanks.push_back(e);
    std::sort(tanks.begin(), tanks.end(), [](Entity* a, Entity* b) { return a->tank->score > b->tank->score; });

    std::string doms = "[";
    for (size_t i = 0; i < doms_.size(); i++) {
        Entity* e = get(doms_[i].ent);
        if (i) doms += ",";
        doms += std::to_string(e ? int(e->color) : int(C_NEUTRAL));
    }
    doms += "]";
    {   // every boss in the arena, for the minimap
        std::vector<uint32_t> ids = waveBosses_;
        ids.insert(ids.end(), bossEnts_.begin(), bossEnts_.end());
        doms += ",\"bosses\":[";
        bool first = true;
        for (uint32_t id : ids)
            if (Entity* boss = get(id)) {
                doms += std::string(first ? "" : ",") + "[" + std::to_string(int(boss->pos.x)) + "," + std::to_string(int(boss->pos.y)) + "," +
                        std::to_string(int(boss->color)) + "]";
                first = false;
            }
        doms += "]";
    }
    if (mode == M_RUSH) {
        doms += ",\"rush\":{\"wave\":" + std::to_string(wave_) + ",\"of\":" + std::to_string(RUSH_WAVES) + ",\"bosses\":" +
                std::to_string(waveBosses_.size()) + ",\"next\":" +
                std::to_string(waveNextTick_ >= 0 ? std::max(0, (waveNextTick_ - tickNo) / TICK_RATE + 1) : 0) + ",\"sancts\":[";
        for (size_t i = 0; i < sancts_.size(); i++) doms += std::string(i ? "," : "") + (sanctuaryHeld(sancts_[i]) ? "1" : "0");
        doms += "],\"lose\":" + std::to_string(allLostTick_ >= 0 && closeAt_ < 0 ? std::max(0, 30 - (tickNo - allLostTick_) / TICK_RATE) : -1) + "}";
    }
    if (closing()) doms += ",\"closing\":1";

    for (Client* c : clients) {
        Entity* me = get(c->tankId);
        int viewTeam = me ? me->team : c->team;
        std::string s = "{\"t\":\"lb\",\"n\":" + std::to_string(tanks.size()) + ",\"lb\":[";
        for (size_t i = 0; i < tanks.size() && i < 10; i++) {
            const TankData& t = *tanks[i]->tank;
            if (i) s += ",";
            s += "[\"" + jsonEscape(t.name) + "\"," + std::to_string((long long)t.score) + "," +
                 std::to_string(colorFor(*tanks[i], viewTeam)) + ",\"" + jsonEscape(TANKS[t.def].name) + "\"]";
        }
        s += "],\"mm\":[";
        if (teamCount_ && me) {
            bool first = true;
            for (Entity* e : tanks) {
                if (e == me || e->team != me->team) continue;
                if (!first) s += ",";
                first = false;
                s += "[" + std::to_string(int(e->pos.x)) + "," + std::to_string(int(e->pos.y)) + "]";
            }
        }
        s += "],\"doms\":" + doms + "}";
        send(*c, s, false);
    }
}
