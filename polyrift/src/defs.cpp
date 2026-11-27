#include "defs.hpp"

#include <cstdio>
#include <initializer_list>

#include "util.hpp"

const char* const SKILL_NAMES[S_COUNT] = {
    "Body Damage", "Max Health", "Bullet Speed", "Bullet Health", "Bullet Penetration",
    "Bullet Damage", "Reload", "Movement Speed", "Shield Regeneration", "Shield Capacity"};

//                                  name            sides size   hp   dmg    xp  colour
const FoodDef FOODS[F_COUNT] = {
    {"Egg", 0, 7, 3, 0.5f, 3, C_EGG},
    {"Square", 4, 15, 12, 1.0f, 10, C_SQUARE},
    {"Triangle", 3, 17, 36, 1.3f, 30, C_TRIANGLE},
    {"Pentagon", 5, 26, 130, 1.6f, 130, C_PENTAGON},
    {"Hexagon", 6, 34, 320, 1.9f, 420, C_HEXAGON},
};

// Sizes. A Beta Pentagon is 48 across with 700 health for 800 XP, an Alpha 90 / 4000 / 4500,
// and the same multipliers give every other shape its Beta, Alpha and Omega.
//                               prefix   size    hp    dmg    xp
const TierDef TIERS[T_COUNT] = {
    {"", 1, 1, 1, 1},
    {"Beta ", 1.85f, 5.4f, 1.35f, 6.2f},
    {"Alpha ", 3.45f, 31, 1.85f, 35},
    {"Omega ", 5.4f, 150, 2.4f, 190},
};

// Mutations, rarest last. The odds are for one freshly spawned polygon at --mutations 1.
//                                        prefix        colour      hp   xp     1 in
const MutationDef MUTATIONS[MU_COUNT] = {
    {"", 255, 1, 1, 0},
    {"Shiny ", C_SHINY, 3, 100, 50000.0},
    {"Legendary ", C_LEGENDARY, 6, 500, 1000000.0},
    {"Shadow ", C_SHADOW, 9, 1500, 3000000.0},
    {"Rainbow ", C_RAINBOW, 14, 5000, 76000000.0},
    {"Trans ", C_TRANS, 20, 10000, 152000000.0},
    {"", C_POLY, 30, 25000, 570000000.0},  // a platonic solid: named after the solid, not the polygon
};

const char* foodName(int type, int tier, int mut, bool crasher) {
    // Built once: [crasher][mutation][tier][type]
    static std::string names[2][MU_COUNT][T_COUNT][F_COUNT];
    static bool built = false;
    if (!built) {
        built = true;
        static const char* const solids[F_COUNT] = {"Octahedron", "Cube", "Tetrahedron", "Dodecahedron", "Icosahedron"};
        for (int m = 0; m < MU_COUNT; m++)
            for (int t = 0; t < T_COUNT; t++)
                for (int f = 0; f < F_COUNT; f++) {
                    names[0][m][t][f] = std::string(MUTATIONS[m].prefix) + TIERS[t].prefix + (m == MU_POLY ? solids[f] : FOODS[f].name);
                    names[1][m][t][f] = std::string(MUTATIONS[m].prefix) + (m == MU_POLY ? "Polyhedral " : "") + (t ? "Large Crasher" : "Crasher");
                }
    }
    type = std::clamp(type, 0, int(F_COUNT) - 1);
    tier = std::clamp(tier, 0, int(T_COUNT) - 1);
    mut = std::clamp(mut, 0, int(MU_COUNT) - 1);
    return names[crasher ? 1 : 0][mut][tier][type].c_str();
}

std::vector<TankDef> TANKS;
std::vector<SubDef> SUBS;

int tankIndex(const std::string& name) {
    for (size_t i = 0; i < TANKS.size(); i++)
        if (TANKS[i].name == name) return int(i);
    return -1;
}

namespace {

// Fluent gun builder so the class list below reads like a table.
struct GB {
    GunDef g;
    GB(float deg, float len, float w) {
        g.ang = deg * PI / 180.0f;
        g.len = len;
        g.w = w;
    }
    GB& ox(float v) { g.ox = v; return *this; }
    GB& asp(float v) { g.asp = v; return *this; }
    GB& delay(float v) { g.delay = v; return *this; }
    GB& reload(float v) { g.reload = v; return *this; }
    GB& stats(float speed, float dmg, float hp) { g.speed = speed; g.dmg = dmg; g.hp = hp; return *this; }
    GB& spread(float v) { g.spread = v; return *this; }
    GB& recoil(float v) { g.recoil = v; return *this; }
    GB& type(GunType t) { g.type = t; return *this; }
    GB& bsize(float v) { g.bsize = v; return *this; }
    GB& life(float v) { g.life = v; return *this; }
    GB& wave(float v) { g.wave = v; return *this; }
    GB& sub(int v) { g.sub = v; return *this; }
    GB& indep() { g.indep = true; return *this; }
    GB& armed() { g.armed = true; return *this; }
    GB& boom() { g.boom = true; return *this; }
    GB& heal() { g.heal = true; return *this; }
    GB& leash(float v) { g.leash = v; return *this; }
    GB& knock(float v) { g.knock = v; return *this; }
    GB& merge() { g.merge = true; return *this; }
    GB& dshape(int v) { g.dshape = uint8_t(v); return *this; }
    GB& alt() { g.alt = true; return *this; }
};

using Guns = std::vector<GB>;
Guns operator+(Guns a, const Guns& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
TankDef make(const std::string& name, int tier, const Guns& guns) {
    TankDef t;
    t.name = name;
    t.tier = tier;
    t.minLevel = tier * 15;
    for (const GB& b : guns) t.guns.push_back(b.g);
    return t;
}

TurretDef turret(float deg, float dist, float size, float arcDeg, const Guns& guns) {
    TurretDef t;
    t.ang = deg * PI / 180.0f;
    t.dist = dist;
    t.size = size;
    t.arc = arcDeg * PI / 180.0f;
    for (const GB& b : guns) t.guns.push_back(b.g);
    return t;
}

int addSub(const Guns& guns, float spin, float accel, bool aim, int delay) {
    SubDef s;
    for (const GB& b : guns) s.guns.push_back(b.g);
    s.spin = spin;
    s.accel = accel;
    s.aim = aim;
    s.delay = delay;
    SUBS.push_back(s);
    return int(SUBS.size()) - 1;
}

void link(const std::string& from, std::initializer_list<const char*> to) {
    int f = tankIndex(from);
    if (f < 0) std::fprintf(stderr, "defs: unknown class '%s'\n", from.c_str());
    for (const char* n : to) {
        int i = tankIndex(n);
        if (i < 0) std::fprintf(stderr, "defs: unknown upgrade '%s' for '%s'\n", n, from.c_str());
        if (f >= 0 && i >= 0) TANKS[f].upgrades.push_back(i);
    }
}

// Reusable gun templates -------------------------------------------------
GB basicGun(float deg = 0) { return GB(deg, 1.9f, 0.84f); }
GB twinGun(float deg, float ox, float delay) {
    return GB(deg, 1.9f, 0.8f).ox(ox).delay(delay).stats(1, 0.75f, 0.9f).recoil(0.5f);
}
Guns twin(float deg = 0) { return {twinGun(deg, 0.5f, 0), twinGun(deg, -0.5f, 0.5f)}; }
Guns tripleShot(float deg = 0) {
    return {GB(deg + 26, 1.75f, 0.8f).delay(0.5f).stats(1, 0.75f, 0.9f).recoil(0.4f),
            GB(deg - 26, 1.75f, 0.8f).delay(0.5f).stats(1, 0.75f, 0.9f).recoil(0.4f),
            GB(deg, 1.9f, 0.8f).stats(1, 0.75f, 0.9f).recoil(0.4f)};
}
GB thinGun(float ox, float len, float delay) {
    return GB(0, len, 0.38f).ox(ox).delay(delay).stats(1.15f, 0.5f, 0.8f).recoil(0.2f).spread(0.08f);
}
Guns gunner() {
    return {thinGun(0.76f, 1.4f, 0.5f), thinGun(-0.76f, 1.4f, 0.75f), thinGun(0.36f, 1.8f, 0), thinGun(-0.36f, 1.8f, 0.25f)};
}
GB mgGun(float deg = 0) {
    return GB(deg, 1.8f, 0.84f).asp(1.7f).reload(0.5f).stats(1, 0.7f, 0.9f).spread(0.45f).recoil(0.7f);
}
GB sniperGun() { return GB(0, 2.4f, 0.84f).reload(1.5f).stats(1.5f, 1.1f, 1.5f).spread(0.01f).life(1.3f); }
GB assassinGun() { return GB(0, 2.8f, 0.84f).reload(2).stats(1.9f, 1.5f, 2.0f).spread(0.005f).life(1.4f).recoil(1.5f); }
Guns hunter(float ox = 0) {
    return {GB(0, 2.4f, 0.62f).ox(ox).reload(1.6f).stats(1.6f, 0.9f, 1.2f).spread(0.01f).life(1.2f).recoil(0.6f),
            GB(0, 2.1f, 0.95f).ox(ox).delay(0.2f).reload(1.6f).stats(1.6f, 1.0f, 1.3f).spread(0.01f).life(1.2f).recoil(0.6f)};
}
Guns minigun() {
    return {GB(0, 2.2f, 0.62f).reload(0.75f).stats(1.3f, 0.45f, 0.9f).recoil(0.3f),
            GB(0, 2.0f, 0.62f).delay(0.33f).reload(0.75f).stats(1.3f, 0.45f, 0.9f).recoil(0.3f),
            GB(0, 1.8f, 0.62f).delay(0.66f).reload(0.75f).stats(1.3f, 0.45f, 0.9f).recoil(0.3f)};
}
Guns rifle() {  // a wide sleeve with a long thin barrel through it
    return {GB(0, 2.0f, 1.05f).type(G_DECOR),
            GB(0, 2.5f, 0.66f).reload(1.3f).stats(1.6f, 1.0f, 1.4f).spread(0.01f).life(1.25f).recoil(0.8f)};
}
Guns sprayer() {
    return {GB(0, 2.15f, 0.6f).reload(0.5f).stats(1.1f, 0.4f, 0.7f).spread(0.2f).recoil(0.3f),
            GB(0, 1.8f, 0.84f).asp(1.7f).delay(0.5f).reload(0.5f).stats(1, 0.7f, 0.9f).spread(0.5f).recoil(0.6f)};
}
GB pounder(float deg = 0) { return GB(deg, 2.0f, 1.1f).reload(2).stats(0.9f, 1.5f, 1.8f).recoil(2.5f).spread(0.03f); }
GB destroyer(float deg = 0) { return GB(deg, 2.0f, 1.4f).reload(3.2f).stats(0.8f, 2.0f, 3.0f).recoil(6).spread(0.02f); }
Guns artillerySides() {
    return {GB(-7, 1.6f, 0.36f).ox(-0.55f).delay(0.5f).stats(1.1f, 0.35f, 0.5f).recoil(0.1f),
            GB(7, 1.6f, 0.36f).ox(0.55f).delay(0.75f).stats(1.1f, 0.35f, 0.5f).recoil(0.1f)};
}
GB trapGun(float deg, float delay = 0) {
    return GB(deg, 1.6f, 0.8f).type(G_TRAP).delay(delay).reload(1.5f).stats(1, 0.8f, 4).bsize(1.3f).recoil(0.6f);
}
GB blockGun(float deg = 0) {
    return GB(deg, 1.7f, 1.1f).type(G_BLOCK).reload(3).stats(1.1f, 1.0f, 9).bsize(1.25f).life(1.5f).recoil(1.5f);
}
GB droneGun(float deg, float reload) {
    return GB(deg, 1.5f, 0.9f).asp(1.5f).type(G_DRONE).reload(reload).stats(0.75f, 0.7f, 2.5f).bsize(1.2f).recoil(0);
}
GB sideDrone(float deg) {  // small launcher for drones that think for themselves (Hybrid, Overtrapper...)
    return GB(deg, 1.35f, 0.8f).asp(1.5f).type(G_DRONE).reload(4).stats(0.75f, 0.6f, 2.0f).bsize(1.2f).recoil(0).indep();
}
GB swarmGun(float deg, float ox, float delay) {
    return GB(deg, 1.55f, 0.55f).asp(1.3f).ox(ox).type(G_SWARM).delay(delay).reload(1.4f)
        .stats(0.85f, 0.7f, 1.0f).bsize(1.3f).life(1.2f).recoil(0.2f);
}
GB necroGun(float deg) {  // never fires: squares it destroys become its drones
    return GB(deg, 1.45f, 0.95f).asp(1.45f).type(G_DRONE).stats(0.8f, 0.55f, 1.6f).recoil(0);
}
GB minionGun(float reload) {
    return GB(0, 1.75f, 1.2f).type(G_MINION).reload(reload).stats(0.7f, 0.5f, 5.0f).bsize(1.0f).recoil(0);
}
GB thruster(float deg, float delay = 0) {  // rear guns that mostly exist for the recoil
    return GB(deg, 1.6f, 0.84f).delay(delay).reload(0.5f).stats(1, 0.25f, 0.5f).life(0.45f).recoil(2.6f).spread(0.2f);
}
Guns thrusters() { return {thruster(150), thruster(210, 0.5f)}; }
// The "bird" classes (Falcon, Eagle, Vulture, Phoenix): the thrusters are the main weapon, and the
// front barrel is a secondary gun that only fires on alt-fire, as in arras.
Guns bird(Guns front, const Guns& rear = thrusters()) {
    for (GB& b : front)
        if (b.g.type != G_DECOR) b.g.alt = true;
    return front + rear;
}
GB launcher(int sub) {
    return GB(0, 1.85f, 1.15f).reload(2.6f).stats(0.72f, 1.4f, 2.4f).recoil(2.2f).spread(0.02f).life(1.35f).sub(sub);
}
GB waveGun(float deg, float amp) { return GB(deg, 1.9f, 0.8f).stats(1, 0.9f, 1.0f).wave(amp).spread(0.02f); }
GB autoGun() { return GB(0, 2.1f, 0.9f).reload(1.25f).stats(1.15f, 0.5f, 0.85f).recoil(0); }
TurretDef autoTurret(float deg = 0, float dist = 0, float size = 0.46f, float arcDeg = 180) {
    return turret(deg, dist, size, arcDeg, {autoGun()});
}

}  // namespace

void buildTankDefs() {
    TANKS.clear();
    SUBS.clear();
    auto add = [](TankDef t) -> TankDef& {
        for (const GunDef& g : t.guns) t.altFire = t.altFire || g.alt;
        TANKS.push_back(std::move(t));
        return TANKS.back();
    };
    auto sniperLike = [](TankDef& t, float fov) -> TankDef& { t.fov = fov; t.ai = AI_SNIPER; return t; };
    // Body of a computer-controlled class for when a developer drives it (the computer's own copies
    // get their size and health where they are spawned, in room.cpp).
    auto npcBody = [](TankDef& t, float size, float hp, float speed) { t.size = size; t.hp = hp; t.speed = speed; t.fov = 1.3f; };
    auto droneLike = [](TankDef& t, int cap, float fov) -> TankDef& { t.maxDrones = cap; t.fov = fov; t.ai = AI_DRONES; return t; };
    // "Auto-" classes: an existing class with an auto-turret on top.
    auto autoOf = [&](const std::string& name, const std::string& base) -> TankDef& {
        TankDef t = TANKS[size_t(tankIndex(base))];
        t.name = name;
        t.tier = 3;
        t.minLevel = 45;
        t.upgrades.clear();
        t.turrets.push_back(autoTurret());
        return add(t);
    };

    // The Smasher line has no bullets to improve: five stats, each of which takes 12 points, and
    // the Reload stat is its Engine Acceleration.
    auto smasherStats = [](TankDef& t) {
        t.statCap = 12;
        t.statMask = (1 << S_BODY) | (1 << S_HEALTH) | (1 << S_RELOAD) | (1 << S_REGEN) | (1 << S_SHIELD);
        t.engine = true;
    };

    // ------------------------------------------------------------ guns carried by projectiles
    const int SUB_MISSILE = addSub({GB(180, 1.5f, 0.7f).reload(0.35f).stats(0.9f, 0.3f, 0.4f).life(0.4f).spread(0.3f)}, 0, 0, false, 6);
    const int SUB_SKIMMER = addSub({GB(150, 1.5f, 0.6f).reload(0.4f).stats(1, 0.35f, 0.5f).life(0.5f).spread(0.15f),
                                    GB(210, 1.5f, 0.6f).delay(0.5f).reload(0.4f).stats(1, 0.35f, 0.5f).life(0.5f).spread(0.15f)}, 0, 0, false, 6);
    const int SUB_TWISTER = addSub({GB(0, 1.5f, 0.6f).reload(0.3f).stats(1, 0.35f, 0.5f).life(0.5f),
                                    GB(180, 1.5f, 0.6f).reload(0.3f).stats(1, 0.35f, 0.5f).life(0.5f)}, 0.26f, 0, false, 6);
    const int SUB_HIVE = addSub({GB(0, 1.35f, 0.7f).asp(1.3f).type(G_SWARM).reload(0.9f).stats(0.8f, 0.4f, 0.5f).life(0.9f).bsize(1.3f),
                                 GB(120, 1.35f, 0.7f).asp(1.3f).type(G_SWARM).delay(0.33f).reload(0.9f).stats(0.8f, 0.4f, 0.5f).life(0.9f).bsize(1.3f),
                                 GB(240, 1.35f, 0.7f).asp(1.3f).type(G_SWARM).delay(0.66f).reload(0.9f).stats(0.8f, 0.4f, 0.5f).life(0.9f).bsize(1.3f)}, 0.05f, 0, false, 8);
    const int SUB_ROCKET = addSub({GB(180, 1.55f, 0.9f).asp(1.5f).reload(0.25f).stats(0.7f, 0.2f, 0.3f).life(0.3f).spread(0.5f)}, 0, 0.16f, false, 8);
    const int SUB_SNAKE = addSub({GB(180, 1.5f, 0.6f).reload(0.2f).stats(0.8f, 0.25f, 0.3f).life(0.35f).spread(0.6f)}, 0, 0.07f, false, 4);
    const int SUB_PILLBOX = addSub({GB(0, 1.5f, 0.6f).reload(1.1f).stats(1.1f, 0.45f, 0.7f)}, 0, 0, true, 25);
    // Surgeon: a block whose little turret repairs the most badly hurt team-mate in reach
    const int SUB_MEDBOX = addSub({GB(0, 1.5f, 0.6f).reload(0.9f).stats(1.2f, 0.5f, 1.0f).heal()}, 0, 0, true, 25);

    // ------------------------------------------------------------ tier 0
    add(make("Basic", 0, {basicGun()}));

    // ------------------------------------------------------------ tier 1 (level 15)
    add(make("Twin", 1, twin()));
    sniperLike(add(make("Sniper", 1, {sniperGun()})), 1.2f);
    add(make("Machine Gun", 1, {mgGun()}));
    add(make("Flank Guard", 1, {basicGun(), GB(180, 1.6f, 0.84f)}));
    droneLike(add(make("Director", 1, {droneGun(0, 3)})), 5, 1.1f);
    add(make("Pounder", 1, {pounder()}));
    add(make("Trapper", 1, {trapGun(0)})).ai = AI_TRAPPER;
    add(make("Desmos", 1,
        {GB(38, 1.15f, 0.5f).type(G_DECOR), GB(-38, 1.15f, 0.5f).type(G_DECOR), waveGun(0, 0.8f)}));

    // ------------------------------------------------------------ tier 2 (level 30)
    add(make("Double Twin", 2, twin() + twin(180)));
    add(make("Triple Shot", 2, tripleShot()));
    add(make("Gunner", 2, gunner()));
    add(make("Hexa Tank", 2,
        {GB(0, 1.9f, 0.8f).stats(1, 0.9f, 1), GB(120, 1.9f, 0.8f).stats(1, 0.9f, 1), GB(240, 1.9f, 0.8f).stats(1, 0.9f, 1),
         GB(60, 1.9f, 0.8f).delay(0.5f).stats(1, 0.9f, 1), GB(180, 1.9f, 0.8f).delay(0.5f).stats(1, 0.9f, 1),
         GB(300, 1.9f, 0.8f).delay(0.5f).stats(1, 0.9f, 1)}));
    add(make("Helix", 2,
        {GB(0, 1.9f, 0.6f).ox(0.34f).stats(1, 0.7f, 0.9f).wave(-0.85f).spread(0.02f).recoil(0.5f),
         GB(0, 1.9f, 0.6f).ox(-0.34f).stats(1, 0.7f, 0.9f).wave(0.85f).spread(0.02f).recoil(0.5f)}));
    add(make("Volute", 2,
        {GB(38, 1.2f, 0.6f).type(G_DECOR), GB(-38, 1.2f, 0.6f).type(G_DECOR),
         GB(0, 2.0f, 1.1f).reload(1.8f).stats(0.9f, 1.4f, 1.7f).wave(0.7f).recoil(2.2f).spread(0.02f)}));
    sniperLike(add(make("Assassin", 2, {assassinGun()})), 1.35f);
    sniperLike(add(make("Hunter", 2, hunter())), 1.25f).zoom = 600;
    add(make("Minigun", 2, minigun()));
    sniperLike(add(make("Rifle", 2, rifle())), 1.22f);
    add(make("Artillery", 2, artillerySides() + Guns{pounder()}));
    add(make("Sprayer", 2, sprayer()));
    add(make("Tri-Angle", 2, Guns{basicGun().recoil(0.3f)} + thrusters()));
    {
        TankDef& t = add(make("Auto-3", 2, {}));
        for (int i = 0; i < 3; i++) t.turrets.push_back(autoTurret(i * 120.0f, 0.86f, 0.44f, 105));
    }
    add(make("Trap Guard", 2, {basicGun(), trapGun(180)}));
    add(make("Tri-Trapper", 2, {trapGun(0).life(0.8f), trapGun(120).life(0.8f), trapGun(240).life(0.8f)})).ai = AI_TRAPPER;
    droneLike(add(make("Overseer", 2, {droneGun(90, 3), droneGun(270, 3)})), 8, 1.1f);
    {
        TankDef& t = add(make("Cruiser", 2, {swarmGun(0, 0.42f, 0), swarmGun(0, -0.42f, 0.5f)}));
        t.fov = 1.15f;
        t.ai = AI_DRONES;
    }
    {
        TankDef& t = add(make("Underseer", 2, {necroGun(90), necroGun(270)}));
        droneLike(t, 8, 1.1f);
        t.shape = SH_SQUARE;
        t.necro = 1 << F_SQUARE;
    }
    droneLike(add(make("Spawner", 2, {GB(0, 1.45f, 1.45f).type(G_DECOR), minionGun(5)})), 3, 1.1f);
    add(make("Destroyer", 2, {destroyer()}));
    add(make("Builder", 2, {blockGun()})).ai = AI_TRAPPER;
    add(make("Launcher", 2, {GB(0, 1.5f, 1.4f).type(G_DECOR), launcher(SUB_MISSILE)}));
    {
        TankDef& t = add(make("Smasher", 2, {}));
        t.shape = SH_HEX;
        t.body = 1.8f;
        t.hp = 1.4f;
        t.speed = 1.2f;
        t.fov = 1.05f;
        t.ai = AI_RAMMER;
        smasherStats(t);
    }

    {   // its bullets repair team-mates instead of hurting them (they still sting enemies a little)
        TankDef& t = add(make("Healer", 2,
            {GB(0, 1.25f, 1.25f).asp(0.8f).type(G_DECOR),
             GB(0, 1.8f, 0.9f).reload(0.8f).stats(1.2f, 0.55f, 1.3f).spread(0.04f).recoil(0.5f).heal()}));
        t.fov = 1.1f;
        t.hp = 1.15f;
    }

    // ------------------------------------------------------------ tier 3 (level 45)
    // --- twin branch
    add(make("Triple Twin", 3, twin() + twin(120) + twin(240)));
    add(make("Hewn Double", 3,
        twin() + twin(180) + Guns{GB(28, 1.7f, 0.8f).delay(0.25f).stats(1, 0.7f, 0.9f).recoil(0.4f),
                                  GB(-28, 1.7f, 0.8f).delay(0.75f).stats(1, 0.7f, 0.9f).recoil(0.4f)}));
    autoOf("Auto-Double", "Double Twin");
    add(make("Bent Double", 3, tripleShot() + tripleShot(180)));
    add(make("Penta Shot", 3,
        {GB(46, 1.6f, 0.8f).delay(0.66f).stats(1, 0.75f, 0.9f).recoil(0.3f),
         GB(-46, 1.6f, 0.8f).delay(0.66f).stats(1, 0.75f, 0.9f).recoil(0.3f),
         GB(23, 1.8f, 0.8f).delay(0.33f).stats(1, 0.75f, 0.9f).recoil(0.3f),
         GB(-23, 1.8f, 0.8f).delay(0.33f).stats(1, 0.75f, 0.9f).recoil(0.3f),
         GB(0, 2.0f, 0.8f).stats(1, 0.75f, 0.9f).recoil(0.3f)}));
    {
        Guns g;
        for (int i = 5; i >= 1; i--)
            for (int s = -1; s <= 1; s += 2)
                g.push_back(GB(s * i * 15.0f, 1.85f - i * 0.11f, 0.36f).delay(i * 0.16f).stats(1.05f, 0.32f, 0.5f).recoil(0.1f).spread(0.04f));
        g.push_back(GB(0, 1.95f, 0.86f).stats(1, 0.9f, 1.0f).recoil(0.6f));
        add(make("Spreadshot", 3, g));
    }
    add(make("Bent Hybrid", 3, tripleShot() + Guns{sideDrone(180)})).maxDrones = 3;
    add(make("Triplet", 3,
        {GB(0, 1.7f, 0.74f).ox(0.52f).delay(0.5f).stats(1.05f, 0.7f, 0.9f).recoil(0.35f),
         GB(0, 1.7f, 0.74f).ox(-0.52f).delay(0.5f).stats(1.05f, 0.7f, 0.9f).recoil(0.35f),
         GB(0, 2.0f, 0.74f).stats(1.05f, 0.7f, 0.9f).recoil(0.35f)}));
    // --- gunner branch
    autoOf("Auto-Gunner", "Gunner");
    add(make("Nailgun", 3,
        {thinGun(0.42f, 1.75f, 0.33f).reload(0.85f), thinGun(-0.42f, 1.75f, 0.66f).reload(0.85f), thinGun(0, 2.0f, 0).reload(0.85f),
         GB(0, 1.25f, 1.2f).type(G_DECOR)}));
    add(make("Machine Gunner", 3,
        {GB(0, 1.5f, 1.0f).asp(1.6f).type(G_DECOR),
         thinGun(0.6f, 1.55f, 0.6f).spread(0.3f).reload(0.7f), thinGun(-0.6f, 1.55f, 0.8f).spread(0.3f).reload(0.7f),
         thinGun(0.3f, 1.8f, 0.2f).spread(0.3f).reload(0.7f), thinGun(-0.3f, 1.8f, 0.4f).spread(0.3f).reload(0.7f),
         thinGun(0, 2.0f, 0).spread(0.3f).reload(0.7f)}));
    add(make("Overgunner", 3, gunner() + Guns{sideDrone(125), sideDrone(235)})).maxDrones = 4;
    add(make("Gunner Trapper", 3,
        {thinGun(0.3f, 1.85f, 0), thinGun(-0.3f, 1.85f, 0.5f), GB(180, 1.5f, 1.2f).type(G_TRAP).reload(2).stats(1, 1.0f, 6).bsize(1.15f).recoil(0.8f)}));
    {
        Guns g;
        for (int i = 0; i < 12; i++)
            g.push_back(GB(i * 30.0f, i % 2 ? 1.45f : 1.7f, 0.38f).delay((i % 4) * 0.25f).stats(1.1f, 0.5f, 0.8f).recoil(0).spread(0.06f));
        add(make("Cyclone", 3, g));
    }
    add(make("Battery", 3,
        {thinGun(0.82f, 1.3f, 0.66f), thinGun(-0.82f, 1.3f, 0.83f), thinGun(0.5f, 1.6f, 0.33f),
         thinGun(-0.5f, 1.6f, 0.5f), thinGun(0.18f, 1.9f, 0), thinGun(-0.18f, 1.9f, 0.16f)}));
    // --- hexa / desmos branches
    {
        Guns g;
        for (int i = 0; i < 8; i++) g.push_back(GB(i * 45.0f, 1.9f, 0.8f).delay((i % 2) * 0.5f).stats(1, 0.9f, 1));
        add(make("Octo Tank", 3, g));
    }
    add(make("Triplex", 3,
        {GB(0, 1.75f, 0.6f).ox(0.4f).delay(0.5f).stats(1, 0.65f, 0.9f).wave(-0.95f).spread(0.02f).recoil(0.4f),
         GB(0, 1.75f, 0.6f).ox(-0.4f).delay(0.5f).stats(1, 0.65f, 0.9f).wave(0.95f).spread(0.02f).recoil(0.4f),
         GB(0, 2.0f, 0.66f).stats(1.05f, 0.75f, 0.9f).spread(0.02f).recoil(0.4f)}));
    add(make("Quadruplex", 3,
        {waveGun(45, 0.8f).stats(1, 0.75f, 0.9f), waveGun(135, -0.8f).stats(1, 0.75f, 0.9f).delay(0.5f),
         waveGun(225, 0.8f).stats(1, 0.75f, 0.9f), waveGun(315, -0.8f).stats(1, 0.75f, 0.9f).delay(0.5f)}));
    add(make("Sidewinder", 3,
        {GB(0, 1.3f, 1.45f).asp(0.7f).type(G_DECOR),
         GB(0, 2.0f, 1.0f).reload(2.6f).stats(0.5f, 1.5f, 2.6f).recoil(2.2f).spread(0.02f).life(1.5f).wave(0.35f).sub(SUB_SNAKE)}));
    // --- sniper branch
    sniperLike(add(make("Ranger", 3,
        {GB(0, 1.3f, 1.3f).asp(0.6f).type(G_DECOR),
         GB(0, 3.0f, 0.84f).reload(2).stats(2.1f, 1.7f, 2.3f).spread(0.004f).life(1.5f).recoil(1.5f)})), 1.5f);
    sniperLike(add(make("Falcon", 3,
        bird({assassinGun().recoil(0.8f)}, Guns{thruster(180, 0.5f)} + thrusters()))), 1.3f);
    {
        TankDef& t = add(make("Stalker", 3,
            {GB(0, 2.8f, 0.95f).asp(0.6f).reload(2).stats(1.9f, 1.5f, 2.0f).spread(0.005f).life(1.4f).recoil(1.5f)}));
        sniperLike(t, 1.35f);
        t.invis = 0.03f;
    }
    sniperLike(autoOf("Auto-Assassin", "Assassin"), 1.35f);
    add(make("Single", 3,
        {GB(0, 1.25f, 1.25f).type(G_DECOR), GB(0, 2.0f, 0.86f).reload(1.1f).stats(1.15f, 1.5f, 1.6f).spread(0.02f).recoil(1.2f)}));
    sniperLike(add(make("Predator", 3,
        {GB(0, 2.5f, 0.56f).reload(1.7f).stats(1.6f, 0.9f, 1.2f).spread(0.01f).life(1.2f).recoil(0.5f),
         GB(0, 2.25f, 0.82f).delay(0.15f).reload(1.7f).stats(1.6f, 1.0f, 1.3f).spread(0.01f).life(1.2f).recoil(0.5f),
         GB(0, 2.0f, 1.08f).delay(0.3f).reload(1.7f).stats(1.6f, 1.1f, 1.4f).spread(0.01f).life(1.2f).recoil(0.5f)})), 1.3f).zoom = 900;
    sniperLike(add(make("X-Hunter", 3,
        {GB(0, 1.2f, 1.3f).type(G_DECOR),
         GB(0, 2.5f, 0.62f).reload(1.45f).stats(1.75f, 0.95f, 1.3f).spread(0.008f).life(1.25f).recoil(0.6f),
         GB(0, 2.2f, 0.95f).delay(0.2f).reload(1.45f).stats(1.75f, 1.05f, 1.4f).spread(0.008f).life(1.25f).recoil(0.6f)})), 1.35f).zoom = 750;
    sniperLike(add(make("Poacher", 3, hunter() + Guns{trapGun(180)})), 1.25f).zoom = 600;
    sniperLike(add(make("Ordnance", 3, artillerySides() + hunter())), 1.25f).zoom = 600;
    sniperLike(add(make("Dual", 3, hunter(0.36f) + hunter(-0.36f))), 1.25f).zoom = 600;
    // --- minigun branch
    {
        Guns g;
        for (int i = 0; i < 5; i++)
            g.push_back(GB(0, 2.5f - i * 0.2f, 0.62f).delay(i * 0.2f).reload(0.8f).stats(1.4f, 0.4f, 0.9f).recoil(0.25f));
        add(make("Streamliner", 3, g));
    }
    add(make("Crop Duster", 3,
        minigun() + Guns{GB(180, 1.6f, 0.7f).asp(1.5f).reload(0.4f).stats(0.9f, 0.4f, 0.7f).spread(0.5f).recoil(0.5f)}));
    {
        Guns g;
        for (int i = 0; i < 3; i++)
            g.push_back(GB(0, 2.1f - i * 0.25f, 0.6f).type(G_TRAP).delay(i * 0.33f).reload(1.0f).stats(1.25f, 0.5f, 2.2f).bsize(1.1f).life(0.5f).recoil(0.3f));
        add(make("Barricade", 3, g)).ai = AI_TRAPPER;
    }
    add(make("Vulture", 3, bird(minigun())));
    // --- rifle branch
    sniperLike(add(make("Musket", 3,
        {GB(0, 1.9f, 1.2f).type(G_DECOR),
         GB(0, 2.4f, 0.5f).ox(0.27f).reload(1.3f).stats(1.6f, 0.75f, 1.2f).spread(0.01f).life(1.25f).recoil(0.5f),
         GB(0, 2.4f, 0.5f).ox(-0.27f).delay(0.5f).reload(1.3f).stats(1.6f, 0.75f, 1.2f).spread(0.01f).life(1.25f).recoil(0.5f)})), 1.22f);
    sniperLike(add(make("Crossbow", 3,
        Guns{GB(32, 1.5f, 0.3f).delay(0.5f).reload(1.3f).stats(1.3f, 0.3f, 0.6f).recoil(0.1f),
             GB(-32, 1.5f, 0.3f).delay(0.5f).reload(1.3f).stats(1.3f, 0.3f, 0.6f).recoil(0.1f),
             GB(16, 1.8f, 0.3f).delay(0.25f).reload(1.3f).stats(1.3f, 0.3f, 0.6f).recoil(0.1f),
             GB(-16, 1.8f, 0.3f).delay(0.25f).reload(1.3f).stats(1.3f, 0.3f, 0.6f).recoil(0.1f)} + rifle())), 1.22f);
    sniperLike(add(make("Armsman", 3, rifle() + Guns{trapGun(180)})), 1.22f);
    // --- artillery / sprayer branches
    add(make("Mortar", 3,
        {GB(-10, 1.35f, 0.36f).ox(-0.8f).delay(0.5f).stats(1.1f, 0.35f, 0.5f).recoil(0.1f),
         GB(10, 1.35f, 0.36f).ox(0.8f).delay(0.625f).stats(1.1f, 0.35f, 0.5f).recoil(0.1f),
         GB(-7, 1.6f, 0.36f).ox(-0.55f).delay(0.75f).stats(1.1f, 0.35f, 0.5f).recoil(0.1f),
         GB(7, 1.6f, 0.36f).ox(0.55f).delay(0.875f).stats(1.1f, 0.35f, 0.5f).recoil(0.1f),
         GB(0, 2.0f, 1.1f).reload(2).stats(0.9f, 1.6f, 1.9f).recoil(2.5f).spread(0.03f)}));
    add(make("Beekeeper", 3,
        {GB(-7, 1.55f, 0.45f).asp(1.3f).ox(-0.6f).type(G_SWARM).delay(0.5f).reload(1.3f).stats(0.85f, 0.5f, 0.8f).bsize(1.3f).life(1.1f).recoil(0.1f),
         GB(7, 1.55f, 0.45f).asp(1.3f).ox(0.6f).type(G_SWARM).delay(0.75f).reload(1.3f).stats(0.85f, 0.5f, 0.8f).bsize(1.3f).life(1.1f).recoil(0.1f),
         pounder()}));
    add(make("Field Gun", 3, artillerySides() + Guns{GB(0, 1.5f, 1.4f).type(G_DECOR), launcher(SUB_MISSILE)}));
    add(make("Redistributor", 3,
        {GB(0, 2.4f, 0.5f).reload(0.5f).stats(1.2f, 0.35f, 0.7f).spread(0.12f).recoil(0.25f),
         GB(0, 2.1f, 0.66f).delay(0.33f).reload(0.5f).stats(1.1f, 0.4f, 0.7f).spread(0.25f).recoil(0.3f),
         GB(0, 1.8f, 0.84f).asp(1.7f).delay(0.66f).reload(0.5f).stats(1, 0.65f, 0.9f).spread(0.5f).recoil(0.6f)}));
    add(make("Phoenix", 3, bird(sprayer())));
    add(make("Atomizer", 3,
        sprayer() + Guns{GB(0, 1.35f, 1.0f).asp(1.9f).delay(0.25f).reload(0.4f).stats(0.9f, 0.3f, 0.5f).spread(0.8f).life(0.6f).recoil(0.3f)}));
    add(make("Focal", 3,
        {GB(0, 2.2f, 0.7f).asp(0.7f).reload(0.55f).stats(1.3f, 0.6f, 0.9f).spread(0.06f).recoil(0.4f),
         mgGun().delay(0.5f)}));
    // --- tri-angle branch
    add(make("Booster", 3,
        {basicGun().recoil(0.3f), thruster(135, 0.5f), thruster(225, 0.5f), thruster(150), thruster(210)}));
    add(make("Fighter", 3,
        Guns{basicGun().recoil(0.3f), GB(90, 1.7f, 0.84f).stats(1, 0.8f, 0.9f).recoil(0.2f),
             GB(-90, 1.7f, 0.84f).delay(0.5f).stats(1, 0.8f, 0.9f).recoil(0.2f)} + thrusters()));
    add(make("Bomber", 3, Guns{basicGun().recoil(0.3f), trapGun(180)} + thrusters()));
    autoOf("Auto-Tri-Angle", "Tri-Angle");
    {
        TankDef& t = add(make("Surfer", 3,
            Guns{basicGun().recoil(0.3f), swarmGun(90, 0, 0), swarmGun(270, 0, 0.5f)} + thrusters()));
        t.fov = 1.1f;
    }
    add(make("Eagle", 3, bird({pounder().recoil(1.2f)})));
    // --- auto branch
    {
        TankDef& t = add(make("Auto-5", 3, {}));
        for (int i = 0; i < 5; i++) t.turrets.push_back(autoTurret(i * 72.0f, 0.88f, 0.4f, 95));
    }
    {
        TankDef& t = add(make("Mega-3", 3, {}));
        for (int i = 0; i < 3; i++)
            t.turrets.push_back(turret(i * 120.0f, 0.82f, 0.52f, 105, {GB(0, 1.9f, 1.05f).reload(2.1f).stats(0.95f, 1.2f, 1.7f).recoil(0)}));
    }
    {
        TankDef& t = add(make("Auto-4", 3, {}));
        for (int i = 0; i < 4; i++)
            t.turrets.push_back(turret(45 + i * 90.0f, 0.86f, 0.44f, 100,
                {GB(0, 2.1f, 0.55f).ox(0.36f).reload(1.1f).stats(1.15f, 0.36f, 0.7f).recoil(0),
                 GB(0, 2.1f, 0.55f).ox(-0.36f).delay(0.5f).reload(1.1f).stats(1.15f, 0.36f, 0.7f).recoil(0)}));
    }
    {
        TankDef& t = add(make("Banshee", 3, {droneGun(60, 3.4f), droneGun(180, 3.4f), droneGun(300, 3.4f)}));
        for (int i = 0; i < 3; i++) t.turrets.push_back(autoTurret(i * 120.0f, 0.86f, 0.4f, 85));
        droneLike(t, 6, 1.1f);
    }
    // --- trapper branches
    sniperLike(add(make("Bushwhacker", 3, {sniperGun(), trapGun(180)})), 1.2f);
    add(make("Bulwark", 3, twin() + Guns{trapGun(180).ox(0.5f), trapGun(180, 0.5f).ox(-0.5f)}));
    add(make("Conqueror", 3, {destroyer(), blockGun(180)}));
    {
        TankDef& t = add(make("Fortress", 3,
            {swarmGun(0, 0, 0), swarmGun(120, 0, 0.33f), swarmGun(240, 0, 0.66f),
             trapGun(60).life(0.7f), trapGun(180).life(0.7f), trapGun(300).life(0.7f)}));
        t.fov = 1.1f;
        t.ai = AI_TRAPPER;
    }
    {
        Guns g;
        for (int i = 0; i < 6; i++) g.push_back(trapGun(i * 60.0f, (i % 2) * 0.5f).life(0.6f));
        add(make("Hexa-Trapper", 3, g)).ai = AI_TRAPPER;
    }
    {
        Guns g;
        for (int i = 0; i < 7; i++) g.push_back(trapGun(i * 360.0f / 7, (i * 4 % 7) / 7.0f).life(0.55f));
        add(make("Septa-Trapper", 3, g)).ai = AI_TRAPPER;
    }
    {
        TankDef& t = add(make("Architect", 3, {}));
        for (int i = 0; i < 3; i++)
            t.turrets.push_back(turret(i * 120.0f, 0.8f, 0.52f, 110,
                {GB(0, 1.8f, 1.0f).type(G_TRAP).reload(2.2f).stats(1.1f, 0.8f, 4).bsize(1.25f).life(0.8f).recoil(0)}));
        t.ai = AI_TRAPPER;
    }
    // --- director branches
    droneLike(add(make("Overlord", 3, {droneGun(0, 2.6f), droneGun(90, 2.6f), droneGun(180, 2.6f), droneGun(270, 2.6f)})), 8, 1.15f);
    droneLike(add(make("Overtrapper", 3, {trapGun(0), sideDrone(125), sideDrone(235)})), 4, 1.1f).ai = AI_TRAPPER;
    droneLike(autoOf("Auto-Overseer", "Overseer"), 8, 1.1f);
    droneLike(add(make("Overdrive", 3,
        {GB(90, 0.7f, 0.5f).type(G_DECOR), droneGun(90, 3.2f).armed(), droneGun(270, 3.2f).armed()})), 6, 1.1f);
    droneLike(add(make("Commander", 3,
        {droneGun(0, 3.2f), droneGun(120, 3.2f), droneGun(240, 3.2f),
         swarmGun(60, 0, 0), swarmGun(180, 0, 0.33f), swarmGun(300, 0, 0.66f)})), 6, 1.15f);
    {
        TankDef& t = add(make("Carrier", 3, {swarmGun(22, 0.3f, 0.33f), swarmGun(-22, -0.3f, 0.66f), swarmGun(0, 0, 0)}));
        t.fov = 1.2f;
        t.ai = AI_DRONES;
    }
    {
        TankDef& t = add(make("Battleship", 3,
            {swarmGun(90, 0.36f, 0), swarmGun(90, -0.36f, 0.5f).indep(), swarmGun(270, 0.36f, 0.25f).indep(), swarmGun(270, -0.36f, 0.75f)}));
        t.fov = 1.15f;
        t.ai = AI_DRONES;
    }
    {
        TankDef& t = autoOf("Auto-Cruiser", "Cruiser");
        t.fov = 1.15f;
        t.ai = AI_DRONES;
    }
    {
        TankDef& t = add(make("Necromancer", 3, {necroGun(0), necroGun(90), necroGun(180), necroGun(270)}));
        droneLike(t, 14, 1.15f);
        t.shape = SH_SQUARE;
        t.necro = 1 << F_SQUARE;
    }
    {
        TankDef& t = add(make("Maleficitor", 3, {necroGun(0).stats(1.0f, 0.45f, 1.1f)}));
        droneLike(t, 16, 1.15f);
        t.shape = SH_SQUARE;
        t.necro = 1 << F_SQUARE;
        t.invis = 0.02f;
    }
    {
        TankDef& t = add(make("Infestor", 3, {necroGun(45), necroGun(135), necroGun(225), necroGun(315)}));
        droneLike(t, 10, 1.15f);
        t.shape = SH_SQUARE;
        t.necro = (1 << F_SQUARE) | (1 << F_TRIANGLE);
    }
    droneLike(add(make("Factory", 3, {GB(0, 1.3f, 1.7f).type(G_DECOR), minionGun(4).stats(0.7f, 0.65f, 6.5f).bsize(1.05f)})), 6, 1.15f);
    droneLike(autoOf("Auto-Spawner", "Spawner"), 3, 1.1f);
    {   // Director at level 45: the same eight drones, and it fades from sight when it holds still
        TankDef& t = add(make("Manager", 3, {droneGun(0, 2.4f)}));
        droneLike(t, 8, 1.15f);
        t.invis = 0.03f;
    }
    // Director at level 45: one huge drone that hits like a truck
    droneLike(add(make("Big Cheese", 3,
        {GB(0, 1.65f, 1.5f).asp(1.25f).type(G_DRONE).reload(9).stats(0.7f, 2.6f, 16).bsize(1.55f).recoil(0)})), 1, 1.15f);
    // --- pounder branches
    add(make("Annihilator", 3, {GB(0, 2.0f, 1.9f).reload(3.4f).stats(0.8f, 2.4f, 4.0f).recoil(9).spread(0.02f)}));
    add(make("Hybrid", 3, {destroyer(), sideDrone(180)})).maxDrones = 2;
    add(make("Constructor", 3,
        {GB(0, 1.7f, 1.5f).type(G_BLOCK).reload(4).stats(1.1f, 1.3f, 16).bsize(1.25f).life(1.5f).recoil(2.5f)})).ai = AI_TRAPPER;
    autoOf("Auto-Builder", "Builder").ai = AI_TRAPPER;
    add(make("Engineer", 3,
        {GB(0, 1.25f, 1.45f).type(G_DECOR),
         GB(0, 1.7f, 1.1f).type(G_BLOCK).reload(4.2f).stats(1.1f, 0.9f, 8).bsize(1.3f).life(1.3f).recoil(1.5f).sub(SUB_PILLBOX)})).ai = AI_TRAPPER;
    add(make("Assembler", 3,  // blocks that touch each other fuse into one bigger, tougher block
        {GB(0, 1.3f, 1.4f).asp(0.8f).type(G_DECOR),
         GB(0, 1.7f, 1.0f).type(G_BLOCK).reload(2.4f).stats(1.1f, 0.85f, 7).bsize(1.15f).life(1.5f).recoil(1.2f).merge()})).ai = AI_TRAPPER;
    add(make("Boomer", 3,
        {GB(0, 1.25f, 0.8f).asp(1.6f).type(G_DECOR),
         GB(0, 1.8f, 1.0f).type(G_BLOCK).reload(2.2f).stats(1.5f, 1.1f, 7).bsize(1.2f).life(0.7f).recoil(1.2f).boom()})).ai = AI_TRAPPER;
    add(make("Skimmer", 3, {GB(0, 1.45f, 1.6f).asp(0.8f).type(G_DECOR), launcher(SUB_SKIMMER)}));
    add(make("Twister", 3, {GB(0, 1.45f, 1.5f).asp(1.25f).type(G_DECOR), launcher(SUB_TWISTER).stats(0.78f, 1.3f, 2.2f)}));
    add(make("Swarmer", 3, {GB(0, 1.4f, 1.5f).type(G_DECOR), GB(0, 2.05f, 0.8f).type(G_DECOR), launcher(SUB_HIVE).stats(0.6f, 1.2f, 2.6f).reload(3.2f)}));
    add(make("Rocketeer", 3, {GB(0, 1.4f, 1.5f).asp(0.75f).type(G_DECOR), launcher(SUB_ROCKET).stats(0.35f, 1.7f, 2.6f).life(1.5f)}));
    // --- healer branch
    {   // long barrel: faster, farther-flying repair shots and a wider view
        TankDef& t = add(make("Medic", 3,
            {GB(0, 1.25f, 1.25f).asp(0.8f).type(G_DECOR),
             GB(0, 2.3f, 0.9f).reload(1.1f).stats(1.6f, 0.8f, 1.6f).spread(0.02f).life(1.3f).recoil(0.6f).heal()}));
        t.fov = 1.3f;
        t.hp = 1.15f;
        t.ai = AI_SNIPER;
    }
    {   // a Healer with Tri-Angle thrusters, to reach the wounded quickly
        TankDef& t = add(make("Ambulance", 3,
            Guns{GB(0, 1.25f, 1.25f).asp(0.8f).type(G_DECOR),
                 GB(0, 1.8f, 0.9f).reload(0.8f).stats(1.2f, 0.55f, 1.3f).spread(0.04f).recoil(0.3f).heal()} + thrusters()));
        t.fov = 1.1f;
        t.hp = 1.15f;
    }
    {   // lays blocks that carry a repair turret
        TankDef& t = add(make("Surgeon", 3,
            {GB(0, 1.25f, 1.45f).type(G_DECOR),
             GB(0, 1.7f, 1.1f).type(G_BLOCK).reload(4.0f).stats(1.1f, 0.7f, 8).bsize(1.3f).life(1.3f).recoil(1.5f).sub(SUB_MEDBOX).heal()}));
        t.fov = 1.1f;
        t.hp = 1.15f;
        t.ai = AI_TRAPPER;
    }
    {   // three repair barrels in a fan
        TankDef& t = add(make("Paramedic", 3,
            {GB(0, 1.25f, 1.25f).asp(0.8f).type(G_DECOR),
             GB(26, 1.65f, 0.85f).delay(0.5f).reload(0.9f).stats(1.2f, 0.5f, 1.2f).spread(0.04f).recoil(0.3f).heal(),
             GB(-26, 1.65f, 0.85f).delay(0.5f).reload(0.9f).stats(1.2f, 0.5f, 1.2f).spread(0.04f).recoil(0.3f).heal(),
             GB(0, 1.8f, 0.85f).reload(0.9f).stats(1.2f, 0.5f, 1.2f).spread(0.04f).recoil(0.3f).heal()}));
        t.fov = 1.1f;
        t.hp = 1.15f;
    }
    // --- smasher branch
    {
        TankDef& t = add(make("Mega Smasher", 3, {}));
        t.shape = SH_MEGA;
        t.body = 2.1f;
        t.hp = 1.8f;
        t.speed = 1.15f;
        t.fov = 1.1f;
        t.ai = AI_RAMMER;
        smasherStats(t);
    }
    {
        TankDef& t = add(make("Spike", 3, {}));
        t.shape = SH_SPIKE;
        t.body = 2.7f;
        t.hp = 1.3f;
        t.speed = 1.25f;
        t.fov = 1.05f;
        t.ai = AI_RAMMER;
        smasherStats(t);
    }
    {
        TankDef& t = add(make("Auto-Smasher", 3, {}));
        t.shape = SH_HEX;
        t.body = 1.8f;
        t.hp = 1.5f;
        t.speed = 1.2f;
        t.fov = 1.05f;
        t.ai = AI_RAMMER;
        // No bullet stats to spend on in this line, so the turret's gun is a fixed, stronger one.
        t.turrets.push_back(turret(0, 0, 0.5f, 180, {autoGun().stats(1.25f, 1.0f, 1.3f)}));
        smasherStats(t);
    }
    {
        TankDef& t = add(make("Landmine", 3, {}));
        t.shape = SH_LAND;
        t.body = 2.0f;
        t.hp = 1.5f;
        t.speed = 1.2f;
        t.fov = 1.05f;
        t.ai = AI_RAMMER;
        t.invis = 0.035f;
        smasherStats(t);
    }

    // ------------------------------------------------------------ dominators (Domination mode)
    // How far each kind sees. A player driving one sees exactly this far to the sides, and its
    // shots vanish at that distance.
    const float DOM_NEAR = 560, DOM_FAR = DOM_NEAR * 2;
    {
        // Sees twice as far as the Gunner. Reloads slowly, and its shell destroys any tank it touches.
        TankDef& t = add(make("Destroyer Dominator", 0,
            {GB(0, 1.55f, 0.6f).reload(4.5f).stats(1.3f, 200, 12).recoil(0).spread(0.01f).life(2.2f).leash(DOM_FAR)}));
        t.hidden = true;
        t.shape = SH_DOM;
        npcBody(t, 2.0f, 6, 0.4f);
        t.reach = DOM_FAR;
        t.fov = DOM_FAR * 2 / 1700;
    }
    {
        TankDef& t = add(make("Gunner Dominator", 0,
            // Short-sighted: a storm of hard-hitting bullets that vanish at the edge of its small view.
            {GB(0, 1.45f, 0.2f).ox(-0.24f).reload(0.3f).stats(1.6f, 1.5f, 2.2f).recoil(0).bsize(1.3f).leash(DOM_NEAR),
             GB(0, 1.45f, 0.2f).ox(0.24f).delay(0.33f).reload(0.3f).stats(1.6f, 1.5f, 2.2f).recoil(0).bsize(1.3f).leash(DOM_NEAR),
             GB(0, 1.6f, 0.2f).delay(0.66f).reload(0.3f).stats(1.6f, 1.5f, 2.2f).recoil(0).bsize(1.3f).leash(DOM_NEAR)}));
        t.hidden = true;
        t.shape = SH_DOM;
        npcBody(t, 2.0f, 6, 0.4f);
        t.reach = DOM_NEAR;
        t.fov = DOM_NEAR * 2 / 1700;
    }
    {
        Guns g;
        for (int i = 0; i < 8; i++)
            g.push_back(GB(i * 45.0f, 1.35f, 0.3f).type(G_TRAP).delay((i % 2) * 0.5f).reload(2.4f).stats(1.3f, 1.2f, 8).bsize(1.7f).recoil(0));
        // Sees as far as the Destroyer and never stops laying traps.
        TankDef& t = add(make("Trapper Dominator", 0, g));
        t.hidden = true;
        t.shape = SH_DOM;
        npcBody(t, 2.0f, 6, 0.4f);
        t.reach = DOM_FAR;
        t.fov = DOM_FAR * 2 / 1700;
        t.ai = AI_TRAPPER;
    }

    // ------------------------------------------------------------ other computer-controlled tanks
    {   // sits in a team base and sends swarms at intruders; cannot be damaged
        Guns g;
        for (int i = 0; i < 3; i++)
            g.push_back(GB(i * 120.0f, 1.4f, 0.5f).asp(1.4f).type(G_SWARM).delay(i / 3.0f).reload(0.5f)
                            .stats(1.35f, 0.05f, 3.5f).bsize(1.5f).life(1.6f).recoil(0).knock(32));
        TankDef& t = add(make("Base Protector", 0, g));
        t.hidden = true;
        t.shape = SH_DOM;
        npcBody(t, 1.5f, 1, 0.7f);
        t.godmode = true;
        t.ai = AI_SPIN;
    }
    {   // clears the arena when a round is over; cannot be damaged
        TankDef& t = add(make("Arena Closer", 0,
            {GB(0, 1.75f, 0.8f).reload(0.45f).stats(2.4f, 30, 40).recoil(0).life(1.4f).spread(0.02f)}));
        t.hidden = true;
        npcBody(t, 1.45f, 1, 1.8f);
        t.body = 12;
        t.godmode = true;
    }
    {
        TankDef& t = add(make("Elite Destroyer", 0,
            {GB(60, 1.45f, 0.62f).reload(2.6f).stats(0.9f, 2.4f, 4.5f).recoil(0).life(1.4f),
             GB(180, 1.45f, 0.62f).delay(0.33f).reload(2.6f).stats(0.9f, 2.4f, 4.5f).recoil(0).life(1.4f),
             GB(300, 1.45f, 0.62f).delay(0.66f).reload(2.6f).stats(0.9f, 2.4f, 4.5f).recoil(0).life(1.4f)}));
        t.hidden = true;
        t.shape = SH_TRI;
        t.ai = AI_SPIN;
        npcBody(t, 1.8f, 14, 0.5f);
        t.turrets.push_back(turret(0, 0, 0.3f, 180, {autoGun().stats(1.3f, 0.9f, 1.4f).reload(0.8f)}));
    }
    {
        TankDef& t = add(make("Elite Gunner", 0,
            {GB(180, 1.2f, 0.7f).type(G_TRAP).reload(1.6f).stats(1.1f, 1.4f, 7).bsize(0.9f).recoil(0),
             GB(0, 1.75f, 0.3f).reload(0.4f).stats(1.4f, 0.9f, 1.4f).recoil(0).spread(0.06f).bsize(1.2f)}));
        t.hidden = true;
        t.shape = SH_TRI;
        npcBody(t, 1.75f, 13, 0.5f);
        for (int s = -1; s <= 1; s += 2)
            t.turrets.push_back(turret(s * 120.0f, 0.95f, 0.26f, 115,
                {GB(0, 2.0f, 0.55f).ox(0.36f).reload(0.7f).stats(1.3f, 0.7f, 1.1f).recoil(0),
                 GB(0, 2.0f, 0.55f).ox(-0.36f).delay(0.5f).reload(0.7f).stats(1.3f, 0.7f, 1.1f).recoil(0)}));
    }
    {
        Guns g;
        for (int i = 0; i < 4; i++)
            g.push_back(GB(i * 90.0f, 1.3f, 0.7f).asp(1.35f).type(G_DRONE).reload(1.1f).stats(0.95f, 0.8f, 2.0f).bsize(0.6f).recoil(0).dshape(4));
        TankDef& t = add(make("Summoner", 0, g));
        t.hidden = true;
        t.shape = SH_SQUARE;
        t.maxDrones = 18;
        t.ai = AI_DRONES;
        npcBody(t, 1.75f, 14, 0.5f);
    }
    {
        TankDef& t = add(make("Nest Keeper", 0, {}));
        t.hidden = true;
        t.shape = SH_PENT;
        t.ai = AI_SPIN;
        npcBody(t, 2.15f, 18, 0.45f);
        for (int i = 0; i < 5; i++)
            t.turrets.push_back(turret(36 + i * 72.0f, 0.72f, 0.2f, 110, {autoGun().stats(1.3f, 0.9f, 1.3f).reload(0.9f)}));
        t.turrets.push_back(turret(0, 0, 0.36f, 180, {GB(0, 1.9f, 1.0f).reload(2.2f).stats(1.0f, 2.2f, 4).recoil(0)}));
    }
    {   // Boss Rush: the players' home. A trapper dominator with a healing turret on top.
        Guns g;
        for (int i = 0; i < 8; i++)
            g.push_back(GB(i * 45.0f, 1.35f, 0.3f).type(G_TRAP).delay((i % 2) * 0.5f).reload(2.2f).stats(1.3f, 1.3f, 9).bsize(1.7f).recoil(0));
        TankDef& t = add(make("Sanctuary", 0, g));
        t.hidden = true;
        t.shape = SH_DOM;
        t.ai = AI_TRAPPER;
        npcBody(t, 2.0f, 8, 0.4f);
        t.turrets.push_back(turret(0, 0, 0.42f, 180, {GB(0, 1.7f, 0.8f).reload(0.45f).stats(1.6f, 0.8f, 1.2f).recoil(0).spread(0.03f).life(1.2f).heal()}));
        t.turrets.back().range = 760;
    }
    // Celestials (Boss Rush waves 5, 10, 15): nine-sided giants with rings of turrets.
    auto ring = [](TankDef& t, int count, float dist, float size, float arcDeg, const Guns& guns, float offsetDeg) {
        for (int i = 0; i < count; i++) t.turrets.push_back(turret(offsetDeg + i * 360.0f / count, dist, size, arcDeg, guns));
    };
    // What a celestial carries on the faces of its body.
    auto trapsAround = [](int sides) {
        Guns g;
        for (int i = 0; i < sides; i++)
            g.push_back(GB((i + 0.5f) * 360.0f / sides, 1.12f, 0.3f).type(G_TRAP).delay((i % 3) / 3.0f).reload(2.6f).stats(1.2f, 1.6f, 9).bsize(1.25f).life(0.7f).recoil(0));
        return g;
    };
    auto gunsAround = [](int sides) {  // a ring of rapid-fire barrels
        Guns g;
        for (int i = 0; i < sides; i++)
            g.push_back(GB((i + 0.5f) * 360.0f / sides, 1.2f, 0.16f).delay((i % 3) / 3.0f).reload(0.75f).stats(1.35f, 0.9f, 1.3f).bsize(2.0f).spread(0.12f).recoil(0));
        return g;
    };
    auto dronesAround = [](int sides) {  // drone hatches
        Guns g;
        for (int i = 0; i < sides; i++)
            g.push_back(GB((i + 0.5f) * 360.0f / sides, 1.1f, 0.36f).asp(1.4f).type(G_DRONE).reload(1.4f).stats(1.0f, 0.9f, 2.4f).bsize(0.5f).recoil(0));
        return g;
    };
    auto celestial = [&](const char* name, int sides, const Guns& bodyGuns) -> TankDef& {
        TankDef& t = add(make(name, 0, bodyGuns));
        t.hidden = true;
        t.shape = uint8_t(sides);
        t.ai = AI_SPIN;
        return t;
    };
    {   // Paladin: traps all round, a ring of swarm launchers, and heavy cannons in the middle
        TankDef& t = celestial("Paladin", 9, trapsAround(9));
        npcBody(t, 3.0f, 40, 0.35f);
        ring(t, 7, 0.66f, 0.15f, 100,
             {GB(0, 1.8f, 1.1f).asp(1.6f).type(G_SWARM).reload(1.0f).stats(1.1f, 0.9f, 1.6f).bsize(1.2f).life(1.5f).recoil(0)}, 0);
        ring(t, 5, 0.33f, 0.19f, 120, {GB(0, 1.8f, 1.5f).reload(3.2f).stats(0.9f, 2.8f, 5.5f).bsize(1.0f).recoil(0)}, 36);
    }
    {   // Freyja: bullets all round, a ring of twin guns, and long sniper barrels in the middle
        TankDef& t = celestial("Freyja", 9, gunsAround(9));
        npcBody(t, 3.0f, 44, 0.35f);
        ring(t, 7, 0.66f, 0.15f, 100,
             {GB(0, 2.0f, 0.55f).ox(0.38f).reload(0.6f).stats(1.4f, 0.75f, 1.2f).bsize(1.5f).recoil(0),
              GB(0, 2.0f, 0.55f).ox(-0.38f).delay(0.5f).reload(0.6f).stats(1.4f, 0.75f, 1.2f).bsize(1.5f).recoil(0)}, 0);
        ring(t, 5, 0.33f, 0.19f, 120,
             {GB(0, 3.0f, 0.55f).reload(1.9f).stats(2.1f, 2.0f, 2.8f).bsize(1.6f).spread(0.01f).life(1.4f).recoil(0)}, 36);
        for (size_t k = 7; k < t.turrets.size(); k++) t.turrets[k].range = 820;  // the snipers reach farther
    }
    {   // Zaphkiel: drone hatches all round, a ring of skimmer-missile launchers, and twister launchers in the middle
        TankDef& t = celestial("Zaphkiel", 9, dronesAround(9));
        npcBody(t, 3.0f, 48, 0.35f);
        t.maxDrones = 24;
        ring(t, 7, 0.66f, 0.15f, 100,
             {GB(0, 1.3f, 1.5f).type(G_DECOR), GB(0, 1.8f, 1.1f).reload(2.4f).stats(0.8f, 1.5f, 3.0f).bsize(1.4f).recoil(0).life(1.3f).sub(SUB_SKIMMER)}, 0);
        ring(t, 5, 0.33f, 0.19f, 120,
             {GB(0, 1.3f, 1.4f).asp(1.3f).type(G_DECOR), GB(0, 1.8f, 1.0f).reload(2.8f).stats(0.8f, 1.4f, 3.0f).bsize(1.3f).recoil(0).life(1.3f).sub(SUB_TWISTER)}, 36);
    }
    {   // the final boss of Boss Rush
        TankDef& t = celestial("Kronos", 11, trapsAround(11));
        npcBody(t, 4.0f, 110, 0.3f);
        ring(t, 9, 0.74f, 0.1f, 95,
             {GB(0, 2.0f, 0.5f).ox(0.34f).reload(0.5f).stats(1.5f, 0.85f, 1.3f).bsize(1.7f).recoil(0),
              GB(0, 2.0f, 0.5f).ox(-0.34f).delay(0.5f).reload(0.5f).stats(1.5f, 0.85f, 1.3f).bsize(1.7f).recoil(0)}, 0);
        ring(t, 7, 0.5f, 0.12f, 105, {GB(0, 1.9f, 1.0f).reload(2.4f).stats(0.8f, 1.6f, 3.2f).bsize(1.6f).recoil(0).life(1.3f).sub(SUB_TWISTER)}, 0);
        ring(t, 5, 0.27f, 0.13f, 120, {GB(0, 1.9f, 1.1f).reload(2.6f).stats(0.95f, 2.6f, 5.0f).bsize(1.4f).recoil(0)}, 36);
        for (int i = 0; i < 4; i++)
            t.guns.push_back(GB(i * 90.0f, 1.05f, 0.3f).asp(1.3f).type(G_DRONE).reload(1.1f).stats(1.05f, 1.0f, 2.6f).bsize(0.6f).recoil(0).g);
        t.maxDrones = 22;
    }
    // ------------------------------------------------------------ the rest of the boss roster
    // Elite crashers are pink triangles: faces at 60/180/300 degrees, corners at 0/120/240.
    auto elite = [&](const char* name, const Guns& guns) -> TankDef& {
        TankDef& t = add(make(name, 0, guns));
        t.hidden = true;
        t.shape = SH_TRI;
        npcBody(t, 1.8f, 14, 0.5f);
        return t;
    };
    const Guns twinTurret = {GB(0, 2.0f, 0.55f).ox(0.36f).reload(0.7f).stats(1.3f, 0.7f, 1.1f).recoil(0),
                             GB(0, 2.0f, 0.55f).ox(-0.36f).delay(0.5f).reload(0.7f).stats(1.3f, 0.7f, 1.1f).recoil(0)};
    const Guns eliteAuto = {autoGun().stats(1.3f, 0.9f, 1.4f).reload(0.8f)};
    const Guns skimmerTurret = {GB(0, 1.3f, 1.5f).asp(0.8f).type(G_DECOR),
                                GB(0, 1.85f, 1.1f).reload(2.6f).stats(0.8f, 1.5f, 3.0f).bsize(1.1f).recoil(0).life(1.35f).sub(SUB_SKIMMER)};
    const Guns sniperTurret = {GB(0, 3.0f, 0.55f).reload(1.9f).stats(2.1f, 2.0f, 2.8f).bsize(1.6f).spread(0.01f).life(1.4f).recoil(0)};
    const Guns swarmTurret = {GB(0, 1.8f, 1.0f).asp(1.5f).type(G_SWARM).reload(0.8f).stats(1.1f, 0.85f, 1.6f).bsize(1.2f).life(1.6f).recoil(0)};
    {   // Elite Sprayer: a machine-gun turret on every face
        TankDef& t = elite("Elite Sprayer", {});
        for (int i = 0; i < 3; i++)
            t.turrets.push_back(turret(60 + i * 120.0f, 0.5f, 0.3f, 125,
                {GB(0, 2.1f, 0.55f).reload(0.45f).stats(1.2f, 0.5f, 0.9f).spread(0.1f).recoil(0),
                 GB(0, 1.75f, 0.9f).asp(1.6f).delay(0.5f).reload(0.45f).stats(1.0f, 0.75f, 1.1f).spread(0.45f).recoil(0)}));
        t.turrets.push_back(turret(0, 0, 0.24f, 180, eliteAuto));
        t.ai = AI_SPIN;
    }
    {   // Elite Battleship: swarms from every face, auto-turrets on the corners
        Guns g;
        for (int i = 0; i < 3; i++)
            for (int k = -1; k <= 1; k++)
                g.push_back(GB(60 + i * 120.0f, 1.3f, 0.34f).asp(1.4f).ox(k * 0.42f).type(G_SWARM).delay((k + 1) / 3.0f).reload(1.0f)
                                .stats(1.0f, 0.8f, 1.6f).bsize(1.25f).life(1.6f).recoil(0));
        TankDef& t = elite("Elite Battleship", g);
        for (int i = 0; i < 3; i++) t.turrets.push_back(turret(i * 120.0f, 0.95f, 0.24f, 115, eliteAuto));
        t.ai = AI_DRONES;
    }
    {   // Elite Spawner: keeps a squad of small tanks around it
        Guns g;
        for (int i = 0; i < 3; i++)
            g.push_back(GB(60 + i * 120.0f, 1.35f, 0.75f).type(G_MINION).reload(5).stats(0.8f, 0.6f, 5).bsize(0.55f).recoil(0));
        TankDef& t = elite("Elite Spawner", g);
        t.maxDrones = 6;
        t.ai = AI_DRONES;
        t.turrets.push_back(turret(0, 0, 0.3f, 180, twinTurret));
    }
    {   // Elite Trap Guard: traps from every face, twin guns on the corners
        Guns g;
        for (int i = 0; i < 3; i++)
            g.push_back(GB(60 + i * 120.0f, 1.3f, 0.6f).type(G_TRAP).delay(i / 3.0f).reload(1.1f).stats(1.2f, 1.3f, 7).bsize(1.0f).recoil(0));
        TankDef& t = elite("Elite Trap Guard", g);
        for (int i = 0; i < 3; i++) t.turrets.push_back(turret(i * 120.0f, 0.95f, 0.26f, 115, twinTurret));
        t.ai = AI_SPIN;
    }
    {   // Elite Spinner: spins fast and throws spirals of bullets
        Guns g;
        for (int i = 0; i < 3; i++)
            for (int k = -1; k <= 1; k++)
                g.push_back(GB(60 + i * 120.0f + k * 24.0f, k ? 1.3f : 1.45f, 0.24f).delay((k + 1) / 3.0f).reload(0.5f)
                                .stats(1.2f, 0.7f, 1.1f).bsize(1.5f).spread(0.03f).recoil(0));
        TankDef& t = elite("Elite Spinner", g);
        t.ai = AI_SPIN;
        t.spin = 0.085f;
    }
    {   // Elite Skimmer: an orange triangle with a missile turret on every face
        TankDef& t = elite("Elite Skimmer", {});
        for (int i = 0; i < 3; i++) t.turrets.push_back(turret(60 + i * 120.0f, 0.5f, 0.34f, 125, skimmerTurret));
        npcBody(t, 1.9f, 16, 0.5f);
        t.ai = AI_SPIN;
    }
    {   // Rogue Palisade: a dark hexagon ringed with trap launchers and cannons
        Guns g;
        for (int i = 0; i < 6; i++)
            g.push_back(GB(30 + i * 60.0f, 1.25f, 0.42f).type(G_TRAP).delay((i % 2) * 0.5f).reload(1.5f).stats(1.2f, 1.4f, 8).bsize(1.2f).recoil(0));
        TankDef& t = add(make("Rogue Palisade", 0, g));
        t.hidden = true;
        t.shape = SH_BODY6;
        t.ai = AI_SPIN;
        npcBody(t, 2.3f, 22, 0.4f);
        for (int i = 0; i < 6; i++)
            t.turrets.push_back(turret(i * 60.0f, 0.92f, 0.17f, 100, {GB(0, 1.9f, 1.0f).reload(1.8f).stats(1.0f, 1.5f, 2.2f).recoil(0)}));
    }
    {   // Rogue Armada: a dark heptagon; shotgun blasts from the faces, small missiles from the corners
        Guns g;
        for (int i = 0; i < 7; i++)
            for (int k = 0; k < 3; k++)
                g.push_back(GB((i + 0.5f) * 360.0f / 7, 1.2f, 0.34f - k * 0.03f).delay((i % 2) * 0.5f).reload(2.2f)
                                .stats(1.1f + 0.1f * k, 0.9f, 1.4f).bsize(1.3f).spread(0.45f).recoil(0));
        TankDef& t = add(make("Rogue Armada", 0, g));
        t.hidden = true;
        t.shape = SH_BODY7;
        t.ai = AI_SPIN;
        npcBody(t, 2.5f, 28, 0.38f);
        for (int i = 0; i < 7; i++)
            t.turrets.push_back(turret(i * 360.0f / 7, 0.9f, 0.15f, 95,
                {GB(0, 1.8f, 1.1f).reload(3.0f).stats(0.8f, 1.2f, 2.4f).bsize(1.3f).recoil(0).life(1.2f).sub(SUB_SKIMMER)}));
    }
    {   // Nest Warden: walls itself in with traps while the corners send swarms
        Guns g;
        for (int i = 0; i < 5; i++)
            for (int k = 0; k < 2; k++)
                g.push_back(GB(36 + i * 72.0f, 1.3f - k * 0.18f, 0.4f).type(G_TRAP).delay(k * 0.5f).reload(0.9f)
                                .stats(1.3f, 0.9f, 4).bsize(1.0f).life(0.6f).recoil(0));
        TankDef& t = add(make("Nest Warden", 0, g));
        t.hidden = true;
        t.shape = SH_PENT;
        t.ai = AI_SPIN;
        npcBody(t, 2.3f, 24, 0.42f);
        for (int i = 0; i < 5; i++) t.turrets.push_back(turret(i * 72.0f, 0.9f, 0.17f, 100, swarmTurret));
        t.turrets.push_back(turret(0, 0, 0.36f, 180, {GB(0, 1.9f, 1.0f).reload(2.0f).stats(1.0f, 2.2f, 4).recoil(0)}));
    }
    {   // Nest Guardian: heavy cannons on the faces, twin guns on the corners, a twister launcher on top
        Guns g;
        for (int i = 0; i < 5; i++)
            g.push_back(GB(36 + i * 72.0f, 1.3f, 0.55f).delay(i / 5.0f).reload(3.0f).stats(0.9f, 2.6f, 5).recoil(0).life(1.3f));
        TankDef& t = add(make("Nest Guardian", 0, g));
        t.hidden = true;
        t.shape = SH_PENT;
        t.ai = AI_SPIN;
        npcBody(t, 2.45f, 30, 0.4f);
        for (int i = 0; i < 5; i++) t.turrets.push_back(turret(i * 72.0f, 0.9f, 0.17f, 100, twinTurret));
        t.turrets.push_back(turret(0, 0, 0.38f, 180,
            {GB(0, 1.3f, 1.4f).asp(1.25f).type(G_DECOR),
             GB(0, 1.8f, 1.0f).reload(2.4f).stats(0.8f, 1.5f, 3.0f).bsize(1.2f).recoil(0).life(1.3f).sub(SUB_TWISTER)}));
    }
    // Mysticals: each keeps a cloud of drones shaped like one kind of polygon (the Summoner's are squares).
    auto mystic = [&](const char* name, int shape, int hatches, float faceDeg, const GB& hatch, int cap) -> TankDef& {
        Guns g;
        for (int i = 0; i < hatches; i++) {
            GB h = hatch;
            h.g.ang = (faceDeg + i * 360.0f / hatches) * PI / 180.0f;
            g.push_back(h);
        }
        TankDef& t = add(make(name, 0, g));
        t.hidden = true;
        t.shape = uint8_t(shape);
        t.maxDrones = cap;
        t.ai = AI_DRONES;
        return t;
    };
    npcBody(mystic("Sorcerer", SH_CIRCLE, 3, 0,
                   GB(0, 1.3f, 0.6f).asp(1.35f).type(G_DRONE).reload(0.45f).stats(1.2f, 0.45f, 0.9f).bsize(0.42f).recoil(0).dshape(6), 26),
            1.6f, 12, 0.5f);
    npcBody(mystic("Enchantress", SH_TRI, 3, 60,
                   GB(0, 1.35f, 0.7f).asp(1.35f).type(G_DRONE).reload(1.3f).stats(1.0f, 1.0f, 3.0f).bsize(0.6f).recoil(0).dshape(3), 14),
            1.8f, 15, 0.5f);
    npcBody(mystic("Exorcistor", SH_PENT, 5, 36,
                   GB(0, 1.3f, 0.7f).asp(1.35f).type(G_DRONE).reload(2.6f).stats(0.9f, 1.6f, 6.0f).bsize(0.8f).recoil(0).dshape(5), 10),
            2.0f, 18, 0.45f);
    // Terrestrials: seven-sided, between the elites and the celestials. Traps all round, five turrets on top.
    auto terrestrial = [&](const char* name, float hp, float speed) -> TankDef& {
        TankDef& t = add(make(name, 0, trapsAround(7)));
        t.hidden = true;
        t.shape = SH_BODY7;
        t.ai = AI_SPIN;
        npcBody(t, 2.7f, hp, speed);
        return t;
    };
    {   // Ares: the toughest and the slowest; heavy cannons and a drone escort
        TankDef& t = terrestrial("Ares", 44, 0.3f);
        ring(t, 5, 0.5f, 0.2f, 120, {GB(0, 1.8f, 1.4f).reload(3.0f).stats(0.9f, 2.6f, 5).recoil(0)}, 36);
        for (int i = 0; i < 5; i++)
            t.guns.push_back(GB(i * 72.0f, 1.08f, 0.3f).asp(1.3f).type(G_DRONE).reload(1.4f).stats(1.0f, 0.9f, 2.4f).bsize(0.55f).recoil(0).g);
        t.maxDrones = 10;
    }
    {   // Gersemi: the fastest; it charges straight at you with swarms pouring off it
        TankDef& t = terrestrial("Gersemi", 30, 0.6f);
        t.ai = AI_RAMMER;
        ring(t, 5, 0.5f, 0.2f, 120, swarmTurret, 36);
    }
    {   // Ezekiel: missile launchers
        TankDef& t = terrestrial("Ezekiel", 36, 0.4f);
        ring(t, 5, 0.5f, 0.2f, 120, skimmerTurret, 36);
    }
    {   // Selene: long sniper barrels and an escort of square drones
        TankDef& t = terrestrial("Selene", 34, 0.4f);
        ring(t, 5, 0.5f, 0.2f, 120, sniperTurret, 36);
        for (TurretDef& td : t.turrets) td.range = 820;
        for (int i = 0; i < 5; i++)
            t.guns.push_back(GB(i * 72.0f, 1.08f, 0.3f).asp(1.3f).type(G_DRONE).reload(1.2f).stats(1.0f, 0.9f, 2.2f).bsize(0.55f).recoil(0).dshape(4).g);
        t.maxDrones = 10;
    }
    {   // Eris: swarms that pick their own victims
        TankDef& t = terrestrial("Eris", 32, 0.45f);
        ring(t, 5, 0.5f, 0.2f, 120,
             {GB(0, 1.8f, 0.9f).asp(1.5f).type(G_SWARM).reload(0.7f).stats(1.2f, 0.8f, 1.4f).bsize(1.25f).life(2.2f).recoil(0).indep()}, 36);
    }
    {   // Theia, the yellow celestial: a ring of square-swarm launchers and five very heavy cannons
        TankDef& t = celestial("Theia", 9, trapsAround(9));
        npcBody(t, 3.0f, 46, 0.35f);
        ring(t, 7, 0.66f, 0.15f, 100,
             {GB(0, 1.8f, 1.1f).asp(1.5f).type(G_SWARM).reload(0.8f).stats(1.05f, 0.9f, 1.8f).bsize(1.2f).life(1.7f).recoil(0).dshape(4)}, 0);
        ring(t, 5, 0.33f, 0.19f, 120, {GB(0, 2.1f, 1.5f).reload(3.4f).stats(1.0f, 3.2f, 6.0f).recoil(0).life(1.3f)}, 36);
    }
    {   // Nyx, the pink celestial: a ring of rocket launchers and five wide-mouthed machine guns
        TankDef& t = celestial("Nyx", 9, trapsAround(9));
        npcBody(t, 3.0f, 50, 0.35f);
        ring(t, 7, 0.66f, 0.15f, 100,
             {GB(0, 1.3f, 1.5f).asp(0.75f).type(G_DECOR),
              GB(0, 1.8f, 1.1f).reload(2.6f).stats(0.45f, 1.6f, 3.0f).bsize(1.3f).recoil(0).life(1.4f).sub(SUB_ROCKET)}, 0);
        ring(t, 5, 0.33f, 0.19f, 120, {GB(0, 1.9f, 1.2f).asp(1.7f).reload(0.4f).stats(1.2f, 0.8f, 1.2f).bsize(0.9f).spread(0.5f).recoil(0)}, 36);
    }
    {   // Julius, a rogue celestial: destroyer cannons, twin guns and a squad of small tanks
        TankDef& t = celestial("Julius", 9, trapsAround(9));
        npcBody(t, 3.0f, 52, 0.35f);
        ring(t, 7, 0.66f, 0.15f, 100, {GB(0, 1.9f, 1.3f).reload(2.8f).stats(0.9f, 2.4f, 4.5f).recoil(0)}, 0);
        ring(t, 5, 0.33f, 0.19f, 120, twinTurret, 36);
        for (int i = 0; i < 3; i++)
            t.guns.push_back(GB(i * 120.0f, 1.1f, 0.4f).type(G_MINION).reload(4).stats(0.8f, 0.7f, 6).bsize(0.5f).recoil(0).g);
        t.maxDrones = 6;
    }
    {   // Genghis, a rogue celestial: twin guns, snipers and a horde of armed drones
        TankDef& t = celestial("Genghis", 9, trapsAround(9));
        npcBody(t, 3.0f, 52, 0.35f);
        ring(t, 7, 0.66f, 0.15f, 100, twinTurret, 0);
        ring(t, 5, 0.33f, 0.19f, 120, sniperTurret, 36);
        for (size_t k = 7; k < t.turrets.size(); k++) t.turrets[k].range = 820;
        for (int i = 0; i < 5; i++)
            t.guns.push_back(GB(i * 72.0f, 1.1f, 0.32f).asp(1.3f).type(G_DRONE).reload(1.0f).stats(1.0f, 0.9f, 2.4f).bsize(0.55f).recoil(0).armed().g);
        t.maxDrones = 15;
    }
    {   // Napoleon, a rogue celestial: swarm launchers and blocks that carry their own turret
        TankDef& t = celestial("Napoleon", 9, trapsAround(9));
        npcBody(t, 3.0f, 52, 0.35f);
        ring(t, 7, 0.66f, 0.15f, 100, swarmTurret, 0);
        ring(t, 5, 0.33f, 0.19f, 120,
             {GB(0, 1.3f, 1.4f).type(G_DECOR),
              GB(0, 1.8f, 1.1f).type(G_BLOCK).reload(3.6f).stats(1.2f, 1.0f, 9).bsize(1.2f).life(1.2f).recoil(0).sub(SUB_PILLBOX)}, 36);
    }
    {   // Ragnarok, the stronger eternal and the last boss of Boss Rush
        TankDef& t = celestial("Ragnarok", 11, trapsAround(11));
        npcBody(t, 4.2f, 140, 0.3f);
        ring(t, 9, 0.74f, 0.1f, 95,
             {GB(0, 1.9f, 1.1f).asp(1.5f).type(G_SWARM).reload(1.0f).stats(1.2f, 0.9f, 1.8f).bsize(1.5f).life(1.8f).recoil(0)}, 0);
        ring(t, 7, 0.5f, 0.12f, 105,
             {GB(0, 1.9f, 1.1f).reload(2.4f).stats(0.45f, 1.8f, 3.4f).bsize(1.6f).recoil(0).life(1.5f).sub(SUB_ROCKET)}, 0);
        ring(t, 5, 0.27f, 0.13f, 120, {GB(0, 2.0f, 1.4f).reload(3.2f).stats(1.0f, 3.0f, 6.0f).bsize(1.3f).recoil(0).life(1.3f)}, 36);
        for (int i = 0; i < 4; i++)
            t.guns.push_back(GB(45 + i * 90.0f, 1.05f, 0.3f).asp(1.3f).type(G_DRONE).reload(1.0f).stats(1.1f, 1.1f, 3.0f).bsize(0.6f).recoil(0).armed().g);
        t.maxDrones = 14;
    }
    {   // Legionary Crasher: a giant elite crasher with its own legion of small tanks
        Guns g;
        for (int i = 0; i < 3; i++) {
            float a = 60 + i * 120.0f;
            g.push_back(GB(a, 1.2f, 0.36f).ox(0.42f).reload(2.8f).stats(0.9f, 2.6f, 5).recoil(0).life(1.4f));
            g.push_back(GB(a, 1.2f, 0.36f).ox(-0.42f).delay(0.5f).reload(2.8f).stats(0.9f, 2.6f, 5).recoil(0).life(1.4f));
            g.push_back(GB(a, 1.05f, 0.3f).type(G_MINION).reload(4.5f).stats(0.8f, 0.7f, 6).bsize(0.45f).recoil(0));
        }
        TankDef& t = add(make("Legionary Crasher", 0, g));
        t.hidden = true;
        t.shape = SH_TRI;
        t.ai = AI_SPIN;
        t.spin = 0.012f;
        t.maxDrones = 9;
        npcBody(t, 3.2f, 80, 0.35f);
        for (int i = 0; i < 3; i++) t.turrets.push_back(turret(i * 120.0f, 0.95f, 0.16f, 115, twinTurret));
        for (int i = 0; i < 3; i++)
            t.turrets.push_back(turret(60 + i * 120.0f, 0.35f, 0.16f, 125,
                {GB(0, 1.7f, 0.9f).type(G_TRAP).reload(1.6f).stats(1.2f, 1.3f, 7).bsize(1.3f).recoil(0)}));
    }
    {   // Bob: a rare visitor to Boss Rush. He looks like an oversized Basic. He is not.
        TankDef& t = add(make("Bob", 0, {GB(0, 1.9f, 0.84f).reload(1.4f).stats(1.2f, 3.0f, 5).recoil(0).life(1.4f)}));
        t.hidden = true;
        npcBody(t, 2.0f, 20, 0.5f);
        for (int i = 0; i < 4; i++) t.turrets.push_back(turret(45 + i * 90.0f, 0.55f, 0.2f, 180, eliteAuto));
    }
    {   // Only reachable with the server's developer code. From it a developer can switch to any class,
        // including every computer-controlled one above.
        TankDef& t = add(make("Developer", 0,
            {GB(0, 1.15f, 1.5f).type(G_DECOR),
             GB(0, 2.0f, 1.25f).asp(0.5f).reload(0.6f).stats(1.4f, 1.7f, 2.2f).recoil(0.4f).spread(0.02f).life(1.3f)}));
        t.hidden = true;
        t.fov = 1.35f;
        t.hp = 1.5f;
        t.speed = 1.25f;
    }
    {   // Developer only: a gun emplacement that cannot be damaged and shreds anything in front of it.
        TankDef& t = add(make("Anti-Tank Machine Gun", 0,
            {GB(0, 1.2f, 0.95f).type(G_DECOR),
             GB(0, 1.6f, 0.26f).ox(-0.3f).reload(0.22f).stats(1.9f, 2.4f, 3.5f).recoil(0).spread(0.05f).life(1.4f).bsize(1.25f),
             GB(0, 1.6f, 0.26f).ox(0.3f).delay(0.33f).reload(0.22f).stats(1.9f, 2.4f, 3.5f).recoil(0).spread(0.05f).life(1.4f).bsize(1.25f),
             GB(0, 1.85f, 0.26f).delay(0.66f).reload(0.22f).stats(1.9f, 2.4f, 3.5f).recoil(0).spread(0.05f).life(1.4f).bsize(1.25f)}));
        t.hidden = true;
        t.shape = SH_DOM;
        t.godmode = true;
        t.size = 1.7f;
        t.speed = 0.9f;
        t.fov = 1.4f;
    }

    // ------------------------------------------------------------ upgrade tree
    link("Basic", {"Twin", "Sniper", "Machine Gun", "Flank Guard", "Director", "Pounder", "Trapper", "Desmos", "Smasher", "Healer"});
    link("Twin", {"Double Twin", "Triple Shot", "Gunner", "Hexa Tank", "Helix", "Musket", "Bulwark"});  // the last two at level 45
    link("Sniper", {"Assassin", "Hunter", "Minigun", "Rifle"});
    link("Machine Gun", {"Artillery", "Minigun", "Gunner", "Sprayer"});
    link("Flank Guard", {"Hexa Tank", "Tri-Angle", "Auto-3", "Trap Guard", "Tri-Trapper"});
    link("Director", {"Overseer", "Cruiser", "Underseer", "Spawner", "Manager", "Big Cheese"});  // the last two at level 45
    link("Pounder", {"Destroyer", "Builder", "Artillery", "Launcher", "Volute"});
    link("Trapper", {"Builder", "Tri-Trapper", "Trap Guard"});
    link("Desmos", {"Volute", "Helix"});
    link("Double Twin", {"Triple Twin", "Hewn Double", "Auto-Double", "Bent Double"});
    link("Triple Shot", {"Penta Shot", "Spreadshot", "Bent Hybrid", "Bent Double", "Triplet"});
    link("Gunner", {"Auto-Gunner", "Nailgun", "Machine Gunner", "Overgunner", "Gunner Trapper", "Cyclone", "Battery"});
    link("Hexa Tank", {"Octo Tank", "Cyclone", "Hexa-Trapper"});
    link("Helix", {"Triplex", "Quadruplex"});
    link("Volute", {"Sidewinder"});
    link("Assassin", {"Ranger", "Falcon", "Stalker", "Auto-Assassin", "Single"});
    link("Hunter", {"Predator", "X-Hunter", "Poacher", "Ordnance", "Dual"});
    link("Minigun", {"Streamliner", "Crop Duster", "Barricade", "Vulture"});
    link("Rifle", {"Musket", "Crossbow", "Armsman"});
    link("Artillery", {"Mortar", "Ordnance", "Beekeeper", "Field Gun"});
    link("Sprayer", {"Redistributor", "Phoenix", "Atomizer", "Focal"});
    link("Tri-Angle", {"Fighter", "Booster", "Falcon", "Bomber", "Auto-Tri-Angle", "Surfer", "Eagle", "Phoenix", "Vulture"});
    link("Auto-3", {"Auto-5", "Mega-3", "Auto-4", "Banshee"});
    link("Trap Guard", {"Bushwhacker", "Gunner Trapper", "Bomber", "Conqueror", "Bulwark"});
    link("Tri-Trapper", {"Fortress", "Hexa-Trapper", "Septa-Trapper", "Architect"});
    link("Overseer", {"Overlord", "Overtrapper", "Overgunner", "Banshee", "Auto-Overseer", "Overdrive", "Commander"});
    link("Cruiser", {"Carrier", "Battleship", "Fortress", "Auto-Cruiser", "Commander"});
    link("Underseer", {"Necromancer", "Maleficitor", "Infestor"});
    link("Spawner", {"Factory", "Auto-Spawner"});
    link("Destroyer", {"Conqueror", "Annihilator", "Hybrid", "Constructor"});
    link("Builder", {"Constructor", "Auto-Builder", "Engineer", "Boomer", "Architect", "Conqueror", "Assembler"});
    link("Launcher", {"Skimmer", "Twister", "Swarmer", "Sidewinder", "Field Gun", "Rocketeer"});
    link("Smasher", {"Mega Smasher", "Spike", "Auto-Smasher", "Landmine"});
    link("Healer", {"Medic", "Ambulance", "Surgeon", "Paramedic"});
}
