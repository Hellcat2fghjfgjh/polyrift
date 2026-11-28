// Data definitions: tank classes (guns, upgrade tree), food polygons, colours.
// Everything about a tank class lives in defs.cpp, so adding a class is one block of data.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "util.hpp"

enum GunType : uint8_t {
    G_BULLET = 0, G_TRAP = 1, G_DRONE = 2, G_BLOCK = 3, G_SWARM = 4,
    G_DECOR = 5,   // drawn but never fires
    G_MINION = 6   // spawns small tanks that shoot for you (Spawner, Factory)
};

// Geometry is in units of the tank's radius, angles in radians.
struct GunDef {
    float ang = 0;       // direction relative to the tank's facing
    float len = 1.9f;    // distance from tank centre to muzzle
    float w = 0.84f;     // barrel width at the base
    float asp = 1;       // muzzle width / base width (>1 flares out)
    float ox = 0;        // sideways offset
    float delay = 0;     // firing phase, 0..1 of the reload cycle
    float reload = 1;    // multiplier on the base reload time
    GunType type = G_BULLET;
    float speed = 1, dmg = 1, hp = 1;  // projectile stat multipliers
    float spread = 0.05f;              // random aim error (radians, total)
    float recoil = 1;
    float bsize = 1;                   // projectile size multiplier
    float life = 1;                    // projectile lifetime multiplier
    float wave = 0;      // bullets weave sideways (Desmos family); the sign picks which side they start towards
    int sub = -1;        // index into SUBS: the projectile carries guns of its own (missiles, pillboxes)
    bool indep = false;  // drones that ignore the cursor and hunt on their own
    bool armed = false;  // drones with a small gun of their own
    bool boom = false;   // blocks that curve back towards the tank (Boomer)
    bool heal = false;   // bullets that repair team-mates they touch (Healer, Sanctuary)
    float leash = 0;     // the projectile vanishes this far from where it was fired (0 = no limit)
    float knock = 0;     // shoves the tank it hits this hard, then is used up (Base Protector)
    bool merge = false;  // blocks that fuse with the same tank's other blocks when they touch (Assembler)
    uint8_t dshape = 0;  // drones: body drawn for them (0 = by class, 4 = square, 5 = pentagon, 6 = egg)
    bool alt = false;    // a secondary gun: fires on alt-fire (right-click or Shift), not with the main guns or autofire
};

// An auto-turret mounted on a tank: it picks its own targets and fires by itself.
struct TurretDef {
    float ang = 0;       // where it is mounted: direction relative to the tank's facing...
    float dist = 0;      // ...and distance from the centre, in tank radii (0 = on top)
    float size = 0.5f;   // turret radius in tank radii; its guns are measured in turret radii
    float arc = PI;      // how far it can turn away from its mount direction (PI = all the way round)
    float range = 620;
    std::vector<GunDef> guns;
};

// Guns carried by a projectile.
struct SubDef {
    std::vector<GunDef> guns;  // at most 4, measured in projectile radii
    float spin = 0;            // radians per tick (Twister)
    float accel = 0;           // forward push per tick (rockets)
    bool aim = false;          // turn towards the nearest enemy and only fire at it (pillbox)
    int delay = 6;             // ticks after launch before the guns start
};

enum BodyShape : uint8_t {
    SH_CIRCLE = 0, SH_TRI = 3, SH_SQUARE = 4, SH_PENT = 5,  // 3/4/5: the body itself is a polygon
    SH_HEX = 6, SH_MEGA = 7, SH_LAND = 8, SH_SPIKE = 12, SH_DOM = 20,
    SH_NONA = 9, SH_HENDECA = 11,  // the body is a 9- or 11-sided polygon (celestials, the eternals)
    // More polygon bodies for bosses. 6, 7 and 8 above are smasher rings, so these are offset by 20.
    SH_BODY6 = 26, SH_BODY7 = 27, SH_BODY8 = 28
};
enum AiStyle : uint8_t { AI_NORMAL, AI_SNIPER, AI_RAMMER, AI_DRONES, AI_TRAPPER, AI_SPIN };
// How a class is played. Worked out for every class when it is registered (classify() in defs.cpp);
// bots use it to choose their skill build (BOT_BUILDS in ai.cpp).
enum BotStyle : uint8_t {
    BS_BALANCED, BS_GLASS, BS_SNIPER, BS_SPRAY, BS_DRONES, BS_TRAPPER, BS_RAMMER, BS_HEAVY, BS_BRAWLER, BS_HEALER, BS_COUNT
};

struct TankDef {
    std::string name;
    int tier = 0;
    int minLevel = 0;
    std::vector<GunDef> guns;
    std::vector<TurretDef> turrets;
    std::vector<int> upgrades;
    uint8_t shape = SH_CIRCLE;
    float speed = 1, hp = 1, fov = 1, body = 1, size = 1;
    int maxDrones = 0;
    uint8_t necro = 0;    // bit mask of food types this class turns into drones when it destroys them
    float invis = 0;      // fades out by this much per tick while standing still and not firing
    bool godmode = false; // cannot be damaged when a developer drives it (protector, closer)
    float reach = 820;    // dominators: how far they can see a target
    float zoom = 0;       // holding alt-fire slides the camera this far towards where the tank is aiming
    bool altFire = false; // has a secondary gun (GunDef::alt); worked out when the class is registered
    bool hidden = false;  // not part of the upgrade tree (dominators, protectors, closers, bosses)
    AiStyle ai = AI_NORMAL;
    // Skill points: how many each stat takes and which stats the class has at all (bit i = Skill i).
    // The Smasher line has five stats that go up to 12, and its Reload stat is "Engine Acceleration".
    int statCap = 9;
    uint16_t statMask = 0x3ff;
    bool engine = false;
    float spin = 0.025f;  // bosses that spin instead of aiming: radians per tick
    int maxTraps = 0;     // at most this many traps or blocks out at once; the oldest goes when another is laid (0 = no limit)
    int starter = 0;      // drones a tank is given the moment it becomes this class (Necromancer line)
    // Worked out when the class is registered, for the bots:
    float thrust = 0;     // how hard its main guns push it forwards when fired (rear thrusters: Tri-Angle line)
    float wave = 0;       // weave of its forward barrels (Desmos line), 0 = bullets fly straight
    bool waveSym = false; // the forward barrels weave to both sides (Helix, Triplex)
    uint8_t style = BS_BALANCED;  // BotStyle
};

struct FoodDef {
    const char* name;
    uint8_t sides;  // 0 = circle
    float size, hp, dmg;
    int xp;
    uint8_t color;
};

// Every polygon comes in four sizes...
enum FoodTier : uint8_t { T_NORMAL, T_BETA, T_ALPHA, T_OMEGA, T_COUNT };
struct TierDef {
    const char* prefix;  // "" for the normal size
    float size, hp, dmg, xp;  // multipliers on the FoodDef
};
// ...and, very rarely, as a mutation: tougher, worth far more, and drawn differently.
enum Mutation : uint8_t { MU_NONE, MU_SHINY, MU_LEGENDARY, MU_SHADOW, MU_RAINBOW, MU_TRANS, MU_POLY, MU_COUNT };
struct MutationDef {
    const char* prefix;
    uint8_t color;    // 255 = keep the polygon's own colour
    float hp, xp;     // multipliers
    double oneIn;     // chance of 1 in this many polygons, before the server's --mutations scale
};
constexpr int FOOD_XP_CAP = 5000000;

// Colour indices shared with the client.
enum Color : uint8_t {
    C_BLUE = 0, C_GREEN = 1, C_RED = 2, C_PURPLE = 3,
    C_SQUARE = 4, C_TRIANGLE = 5, C_PENTAGON = 6, C_WALL = 7, C_EGG = 8, C_NEUTRAL = 9,
    C_ELITE = 10, C_CLOSER = 11, C_FINAL = 12,
    C_HEXAGON = 13, C_SHINY = 14, C_LEGENDARY = 15, C_SHADOW = 16,
    C_RAINBOW = 17, C_TRANS = 18, C_POLY = 19,  // drawn specially by the client (cycling, striped, 3D)
    C_ROGUE = 20, C_COUNT = 21
};

enum Skill {
    S_BODY, S_HEALTH, S_BSPEED, S_BHEALTH, S_BPEN, S_BDMG, S_RELOAD, S_MOVE, S_REGEN, S_SHIELD, S_COUNT
};
extern const char* const SKILL_NAMES[S_COUNT];

enum FoodType { F_EGG, F_SQUARE, F_TRIANGLE, F_PENTAGON, F_HEXAGON, F_COUNT };
extern const FoodDef FOODS[F_COUNT];
extern const TierDef TIERS[T_COUNT];
extern const MutationDef MUTATIONS[MU_COUNT];
// "Shiny Beta Pentagon", "Crasher"... The pointer stays valid for the life of the program.
const char* foodName(int type, int tier, int mut, bool crasher);

extern std::vector<TankDef> TANKS;
extern std::vector<SubDef> SUBS;
void buildTankDefs();
int tankIndex(const std::string& name);  // -1 if unknown
