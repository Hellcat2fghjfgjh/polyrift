// One Room = one running arena (a game mode with its own map, entities, bots and players).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "defs.hpp"
#include "util.hpp"

enum Mode {
    M_FFA, M_MAZE, M_TDM2, M_TDM4, M_DOM, M_RUSH, M_MAZEDOM,
    M_SANDBOX,  // free levels, any class, spawn shapes and bosses
    M_TAG,      // four teams; a tank that is destroyed joins the team that destroyed it
    M_BR,       // Battle Royale: no respawns, a shrinking safe circle, the last tank alive wins
    M_MOTHER,   // Mothership: each team protects one big drone tank
    M_KOTH,     // King of the Hill: hold the middle of the map to score
    M_SIEGE,    // Siege: Blue must take Green's dominators before the clock runs out
    M_CTF,      // Capture the Flag, two teams
    M_ENDLESS,  // Boss Rush without a last wave
    M_COUNT
};
constexpr int RUSH_WAVES = 20;
// --- numbers of the newer modes (all times in seconds)
constexpr int BR_LOBBY = 40;          // Battle Royale: time to level up before the battle (tanks cannot hurt each other yet)
constexpr int BR_SHRINK = 240;        // ...and how long the safe circle takes to close completely
constexpr int KOTH_GOAL = 150;        // King of the Hill: points to win (one point a second while a team holds the hill alone)
constexpr float KOTH_RADIUS = 430;
constexpr int SIEGE_TIME = 9 * 60;    // Siege: the attackers' clock...
constexpr int SIEGE_BONUS = 90;       // ...and what each captured dominator adds to it
constexpr int CTF_GOAL = 3;           // Capture the Flag: captures to win
constexpr int CTF_TIME = 15 * 60;     // ...or the higher score when this runs out (a tie plays on until the next capture)
constexpr int CTF_RETURN = 20;        // a dropped flag goes home by itself after this long
constexpr int MOTHER_TIME = 15 * 60;  // Mothership: if both are still alive by then, the healthier one wins
constexpr float WAVE_FREQ = 0.22f;    // Desmos-style bullets: how fast they swing from side to side (radians per tick)
enum Kind : uint8_t { K_TANK = 0, K_BULLET = 1, K_TRAP = 2, K_DRONE = 3, K_FOOD = 4 };

// Team Polygon: the one team for everything that belongs to no player team. The shapes, the
// bosses, uncaptured (neutral) dominators, fallen (neutral) sanctuaries and the arena closers that
// end a round are all on it, so none of them fight each other or break shapes, and what they fire
// passes through the others unharmed. They only fight the players. There is no separate "neutral"
// team: an entity is on a player team (0 and up) or on Team Polygon.
constexpr int TEAM_FOOD = -2;
constexpr int TEAM_BOSS = TEAM_FOOD;
constexpr int TICK_RATE = 30;
constexpr int MAX_LEVEL = 45;
constexpr int MAX_SKILL = 9;
// Spawn protection: three seconds for everyone, and for as long as the tank neither moves nor
// fires, up to twenty. Firing ends it at once. Self-destruct only works while it lasts.
constexpr int SPAWN_PROTECT = 3 * TICK_RATE, SPAWN_PROTECT_STILL = 20 * TICK_RATE;
constexpr float BULLET_SPEED = 10;  // units per tick, before gun and skill multipliers
// Bump this (and PROTO in public/game.js) whenever the binary packet layout changes, so browsers
// holding an old copy of the client reload themselves instead of misreading packets.
constexpr int PROTOCOL_VERSION = 9;
constexpr size_t CHAT_MAX_CHARS = 80;  // longest chat line, in characters

struct Client;

struct BotBrain {
    uint32_t target = 0;
    bool targetIsFood = false;
    Vec2 goal;
    int goalTimer = 0;
    float strafe = 1;
    int strafeTimer = 0;
    int offset = 0;       // de-synchronises thinking between bots
    float aimNoise = 0;
    int weights[S_COUNT] = {0};  // skill build preference (easy bots: loose weights; others: the points of a preset build)
    uint8_t level = 1;    // difficulty: 0 easy, 1 normal, 2 hard
    int goalDef = -1;     // the level-45 class this bot is working towards (-1 = whatever comes)
    int build = -1;       // index into BOT_BUILDS (ai.cpp), -1 = not chosen yet
    Vec2 dodge;           // sidestep away from an incoming shot...
    int dodgeUntil = 0;   // ...until this tick
};

// Who drives a tank. Everything except R_PLAYER is computer-controlled scenery with its own rules.
enum Role : uint8_t { R_PLAYER = 0, R_DOM, R_PROTECTOR, R_CLOSER, R_BOSS, R_SANCT, R_MOTHER };

struct TurretState {
    float ang = 0;         // world angle
    uint32_t tgt = 0;
    std::vector<float> t;  // per-gun cooldown in ticks
};

// Extra state carried only by tanks (players, bots, dominators and the other computer-controlled tanks).
struct TankData {
    std::string name;
    Client* client = nullptr;
    bool bot = false;
    int slot = -1;  // bot slot index
    int def = 0;
    double score = 0;
    int level = 1;
    int skills[S_COUNT] = {0};
    int skillPts = 0;
    std::vector<float> gunT;  // per-gun cooldown in ticks
    bool wasFiring = false, wasAlt = false;  // was firing last tick: main guns, secondary guns
    Vec2 move;                // desired movement direction
    Vec2 aim{1, 0};           // aim point relative to the tank
    bool fire = false, alt = false;
    int invuln = 0;           // spawn protection ticks
    bool stirred = false;     // has moved or fired since spawning (a tank that has not keeps its protection longer)
    int lastHurt = -100000;
    int drones = 0;
    int kills = 0, assists = 0, bossKills = 0, polys = 0;  // this life: shown on the kill bar and the death screen
    int spawnTick = 0;
    int dom = -1;             // dominator index, -1 for ordinary tanks
    uint8_t role = R_PLAYER;
    bool invincible = false;  // base protectors and arena closers
    float accel = 0;          // movement of computer-controlled tanks
    float alpha = 1;          // 0 = invisible (Stalker, Landmine...)
    bool lvlKey = false;      // the level-up key is held
    bool manual = false;      // override: auto-turrets and drones stop acting on their own
    bool reverse = false;     // "reverse tank": the body faces away from the cursor
    bool zooming = false;     // alt-fire zoom (hunter classes): direction is fixed when the key goes down
    Vec2 zoomDir;
    float zoomAmt = 0;        // 0..1, eases in and out
    std::vector<uint32_t> traps;  // classes with a trap limit: the traps this tank has out, oldest first
    bool god = false;         // Sandbox: the player has switched damage off for this tank
    std::vector<TurretState> tur;
    BotBrain ai;
};

struct Entity {
    uint32_t id = 0;
    Kind kind = K_FOOD;
    uint8_t shape = 0;  // food type, or polygon sides for traps/drones
    uint8_t color = 0;
    int team = 0;
    Vec2 pos, vel;
    float angle = 0, spin = 0;
    float size = 10;
    float hp = 1, maxHp = 1, shield = 0, maxShield = 0;
    float damage = 1;      // contact damage per tick against bodies
    float projDamage = 1;  // contact damage per tick against projectiles
    float resist = 1;      // multiplier on incoming damage
    float mass = 1, pushf = 1, damp = 0.9f;
    int life = -1;         // ticks left, -1 = forever
    uint32_t owner = 0;    // tank that fired this
    uint32_t lastHitBy = 0;
    int lastHitTeam = TEAM_FOOD;
    int lastHitTick = -100000;
    const char* cause = nullptr;  // non-tank cause of the last damage
    int hitTick = -1000;
    int xp = 0;            // food: score it is worth; bosses: the reward shared between whoever damaged them
    // Who has damaged this tank or polygon, and how much. Score is shared out by damage dealt,
    // and whoever helped without landing the last hit gets an assist.
    struct Hit { uint32_t by; float dmg; int tick; };
    std::vector<Hit> hits;
    uint8_t tier = 0;      // food: T_NORMAL .. T_OMEGA (crashers: 0 small, 1 large)
    uint8_t mut = 0;       // food: Mutation
    bool crasher = false;  // food that hunts tanks
    bool merge = false;    // blocks that fuse with their owner's other blocks (Assembler)
    bool ghost = false;    // arena closers and their shots: maze walls and team bases do not stop them
    bool dead = false;
    bool nest = false;     // food that belongs to the central nest
    bool counted = false;  // drone that counts against its owner's limit
    float maxSpeed = 0;    // drones
    uint32_t tgt = 0;      // drones: current auto-target
    bool indep = false;    // drones: ignore the owner's cursor
    uint8_t arm = 0;       // drones: 1 = minion gun, 2 = light gun
    float gunT = 0;        // drones: cooldown of that gun
    int sub = -1;          // projectiles: index into SUBS (guns carried by the projectile)
    float subT[4] = {0, 0, 0, 0};
    bool boom = false;     // blocks that fly back towards their tank
    bool heal = false;     // bullets that repair team-mates
    float leash = 0;       // vanishes this far from `from`
    Vec2 from;             // where it was fired
    float knock = 0;       // shove given to the tank it hits
    float wave = 0;        // bullets: sideways weave speed
    Vec2 wperp;            // bullets: weave axis
    int age = 0;           // ticks since creation
    int gx0 = 0, gy0 = 0;  // spatial grid origin cell (for pair de-duplication)
    std::unique_ptr<TankData> tank;
};

// A connected browser. Owned by main.cpp; rooms only keep pointers.
struct Client {
    void* conn = nullptr;
    class Room* room = nullptr;
    std::string name;
    uint32_t tankId = 0;
    int team = -1;
    Vec2 cam;
    float fov = 2000;
    double keepScore = 0;   // score carried over to the next life
    uint32_t spectate = 0;
    bool played = false;    // has spawned at least once (menu spectators don't count as players)
    bool dev = false;       // entered the developer code: spawns as Developer and may pick any class
    int devTries = 0;       // wrong codes sent on this connection
    // Controlling a dominator: the player's own tank is put away and comes back when they let go.
    bool inDom = false;
    double domScore = 0;
    int domDef = 0, domPts = 0;
    int domSkills[S_COUNT] = {0};
    int domKills = 0, domAssists = 0, domBossKills = 0, domPolys = 0;
    float sandTokens = 6;   // Sandbox: allowance for spawn commands, refilled over time
    int sandTick = 0;
    // Chat: a small allowance that refills over time, and the last line said (repeats are refused).
    float chatTokens = 3;
    int chatTick = 0, chatWarnTick = -100000, lastChatTick = -100000;
    std::string lastChat;
};

class Room {
public:
    Room(Mode mode, uint32_t seed);                  // an arena that always plays one mode
    Room(std::vector<Mode> options, uint32_t seed);  // a slot: every new game picks one of these modes at random
    ~Room();

    // Set by the host: deliver a packet to a client.
    std::function<void(Client&, const std::string&, bool binary)> send;

    void addClient(Client* c);
    void removeClient(Client* c);
    void onMessage(Client& c, const uint8_t* data, size_t n);
    void tick();
    void announce(const std::string& text) { sendMsg(nullptr, text); }
    void setBots(int n);        // how many bots fill the arena when it is empty (call before the first tick)
    int players() const;        // clients with a live tank
    int maxPlayers = 40;        // spawn requests beyond this are refused
    int freeLevel = MAX_LEVEL;  // holding the level-up key raises a tank to this level for free (0 = off)
    std::string devCode;        // code that unlocks the Developer tank (empty = nobody can)
    int bossEvery = 6 * 60 * TICK_RATE;  // ticks between boss visits (0 = no bosses)
    int closeEvery = 0;         // close and restart the arena after this many ticks (0 = never)
    // Multiplier on the mutation odds in defs.cpp (0 = no mutated polygons). Set mutationDefault before
    // creating a Room so that the polygons it starts with already follow it.
    static inline double mutationDefault = 100;
    double mutationScale = mutationDefault;
    // Chat between players (--no-chat turns it off). Like mutationDefault, set chatDefault before creating a Room.
    static inline bool chatDefault = true;
    bool chat = chatDefault;
    // How well the bots play: 0 easy, 1 normal, 2 hard, 3 mixed (every bot rolls its own). --bot-level sets the default.
    static inline int botLevelDefault = 1;
    int botLevel = botLevelDefault;
    void closeArena(const std::string& reason);  // send in the arena closers, then start a fresh round
    static std::vector<int> bossDefs();           // the class index of every boss (Sandbox offers them in its panel)
    float waveAimFix(const Entity& e, float dist) const;  // bots: angle to aim off by so a weaving bullet (Desmos line) arrives on target
    bool closing() const { return closeTick_ >= 0; }

    Mode mode = M_FFA;
    std::vector<Mode> options;  // what this slot may turn into when a game ends
    std::string slot;           // name of the slot (the WebSocket path); set by the host
    std::string key, title;     // current mode
    bool domMode() const { return mode == M_DOM || mode == M_MAZEDOM; }   // classic Domination rules
    bool rushMode() const { return mode == M_RUSH || mode == M_ENDLESS; }  // sanctuaries and boss waves
    // Modes with no ending of their own: only these are restarted by the --arena-minutes timer.
    bool openEnded() const { return mode == M_FFA || mode == M_MAZE || mode == M_TDM2 || mode == M_TDM4 || mode == M_TAG; }
    bool bountyMode() const { return !rushMode() && mode != M_SANDBOX && !(mode == M_BR && !brBegun_); }
    float W = 5000, H = 5000;
    int tickNo = 0;
    double startScore = 0;  // everyone spawns with at least this much score (--start-level)
    std::vector<Client*> clients;
    std::vector<Entity*> ents;

    // Stats helpers used by the self-test.
    int statKills = 0, statCaptures = 0, statRounds = 0, statBosses = 0, statBossKills = 0, statWins = 0;
    int statChats = 0;
    int statAssists = 0, statMutations = 0, statEvolved = 0, statCrashers = 0, statMerges = 0, statGroups = 0;
    int statDeaths = 0, statCrasherDeaths = 0;
    int statBounties = 0, statTagged = 0, statFlagCaps = 0, statFlagGrabs = 0, statDodges = 0, statTrapCaps = 0;
    std::vector<int> statClassKills, statClassDeaths;  // per class index: tanks destroyed by it, and times it was destroyed by a tank
    int bestWave() const { return bestWave_; }
    int wave() const { return wave_; }
    bool battleBegun() const { return brBegun_; }
    int countKind(Kind k) const;
    std::string summary() const;

private:
    struct Base { float x, y, w, h; int team; };
    struct Dom { Vec2 pos; uint32_t ent = 0; };
    // Capture the Flag: a flag is mode state, not an entity. It rides on its carrier, lies where the
    // carrier died, or stands at home.
    struct Flag { Vec2 home, pos; int team = 0; uint32_t carrier = 0; bool atHome = true; int dropTick = 0; };
    struct BotSlot { std::string name; int team = -1; uint32_t ent = 0; int respawnAt = 0; double keep = 0; };

    // --- setup (room.cpp)
    void configure(Mode m);  // map size, teams, maze and bot list for a mode
    void populate();         // dominators, sanctuaries, protectors, shapes
    void makeBots(int n);
    Vec2 domSpot(int i) const;
    void generateMaze();
    void setupTeams();
    void setupDominators();
    void setupProtectors();
    Entity* spawnNpc(const char* def, Role role, int team, uint8_t color, Vec2 pos, float size, float hp);
    void spawnBoss();
    Entity* makeBoss(const char* kind, Vec2 pos, float hpScale);
    std::string bossLabel(const Entity& e) const;  // "Vexa the Elite Gunner", or just "Paladin"
    void setupSanctuaries();
    void rushLogic();   // Boss Rush: waves, victory, defeat
    void spawnWave();
    int sanctuariesAlive() const;  // sanctuaries the players currently hold
    bool sanctuaryHeld(const Dom& d) const;
    uint32_t pickAlly(const Entity& self, Vec2 from, float range, float dir, float arc);  // most hurt team-mate nearby
    void toggleDominator(Client& c);   // the "control a dominator" key (also drives the Mothership)
    void releaseDominator(Client& c);  // give the dominator back to the computer and respawn the player's tank
    void resetArena();
    // --- the newer modes (modes.cpp)
    void win(const std::string& text, const std::string& sub, int color);  // announce a result; the arena then closes
    void bountyLogic();        // once a second: who is the leader, and what are they worth
    std::vector<int> teamMembers() const;  // Tag: tanks, waiting bots and dead players on each team
    void tagLogic();
    void tagConvert(Entity& victim, Entity& killer);
    void brLogic();
    bool inZone(Vec2 p, float margin = 0) const;  // Battle Royale: inside the safe circle?
    void setupMotherships();
    void motherThink(Entity& e);
    void motherLogic();
    void kothLogic();
    void setupSiege();
    void siegeLogic();
    void siegeCapture(Entity& dom, Entity* killer);
    void setupFlags();
    void ctfLogic();
    void sendHome(Flag& f);
    const Flag* carriedBy(uint32_t tank) const;
    void sandboxCommand(Client& c, ByteReader& r);
    void giveStarters(Entity& e);  // Necromancer line: top up to the class's starter drones
    void setLevel(Entity& e, int level);  // Sandbox: put a tank at a level, skill points handed back

    // --- entities (room.cpp)
    Entity* create(Kind k);
    Entity* get(uint32_t id) const;
    void flushPending();
    Entity* spawnTank(const std::string& name, int team, double score, Client* c, int slot);
    void spawnFood(bool nest);
    void setFood(Entity& e, int type, int tier, int mut, bool crasher);  // (re)apply a polygon's stats
    void evolveFood();   // once a second: a few polygons grow into the next shape or size
    struct Share { Entity* tank; float frac; };
    std::vector<Share> damageShares(const Entity& e, int window) const;  // living players and their part of the damage
    void refresh(Entity& e);
    void setDef(Entity& e, int def);
    void addScore(Entity& e, double s);
    std::vector<int> upgradeOptions(const TankData& t) const;
    Vec2 spawnPoint(int team);
    bool openSpot(Vec2 p, float r) const;
    Vec2 randomOpenPoint(float r);
    int smallestTeam() const;

    // --- simulation (room.cpp)
    void buildGrid();
    template <class F> void query(Vec2 p, float r, F&& fn);
    void thinkTank(Entity& e);
    void thinkDrone(Entity& e);
    void updateGuns(Entity& e);
    void fireGun(Entity& e, int gi);
    void updateTurrets(Entity& e);
    void thinkProjectile(Entity& e);
    Entity* launch(Entity& from, Entity* tank, const GunDef& g, Vec2 origin, float ang, float scale);
    bool targetable(const Entity& x, int team) const;
    uint32_t pickTarget(Vec2 from, int team, float range, float dir, float arc, bool foodToo);
    void collide();
    void collidePair(Entity& a, Entity& b);
    void hurt(Entity& e, float amount, Entity* src, const char* cause);
    void integrate();
    bool wallAt(int cx, int cy) const;
    bool resolveWalls(Entity& e);
    bool los(Vec2 a, Vec2 b) const;
    void reap();
    void onTankDeath(Entity& e);
    void sweep();
    void manageFood();
    void manageBots();
    void modeLogic();

    // --- AI (ai.cpp)
    void botThink(Entity& e);
    void domThink(Entity& e);
    void protectorThink(Entity& e);
    void closerThink(Entity& e);
    void bossThink(Entity& e);
    void crasherThink(Entity& e);
    void botSpend(Entity& e);
    void botPlan(Entity& e);      // choose a difficulty, a class to aim for and a skill build
    int botUpgrade(const TankData& t, const std::vector<int>& opts);  // which of the offered classes to take
    bool botDodge(Entity& e, Vec2& mv);  // sidestep an incoming shot
    bool reaches(int from, int to) const;  // can class `from` upgrade (in any number of steps) into `to`?
    Vec2 pathStep(Vec2 from, Vec2 to, float radius);
    Vec2 pickGoal(Entity& e);
    float bulletSpeedOf(const Entity& e) const;

    // --- protocol (proto.cpp)
    void sendInit(Client& c);
    void sendUpdates();
    void sendBoards();
    void sendMsg(Client* c, const std::string& text);  // nullptr = everyone
    void sendBanner(const std::string& text, const std::string& sub, int color);  // big centred announcement
    void sendChat(const Entity& from, const std::string& name, const std::string& text);  // a player's line, to everyone in the arena
    uint8_t colorFor(const Entity& e, int viewerTeam) const;
    static uint8_t teamColor(int team);
    static const char* teamName(int team);

    Rng rng_;
    std::vector<Entity*> pending_;
    std::unordered_map<uint32_t, Entity*> byId_;
    uint32_t nextId_ = 1;

    // maze
    int cols_ = 0, rows_ = 0;
    float cell_ = 200;
    std::vector<uint8_t> walls_;
    std::string mazeStyle_;  // name of the wall layout rolled for this game

    // teams / domination
    int teamCount_ = 0;
    std::vector<Base> bases_;
    std::vector<Dom> doms_;
    std::vector<Dom> sancts_;           // Boss Rush sanctuaries
    std::vector<uint32_t> waveBosses_;  // Boss Rush: bosses of the current wave
    int wave_ = 0, waveNextTick_ = -1;
    int allLostTick_ = -1;  // Boss Rush: when the last sanctuary fell (-1 = at least one stands)

    // bosses and arena closing
    std::vector<uint32_t> bossEnts_;  // the bosses visiting right now (one, two or three arrive together)
    std::vector<int> bossBag_;        // shuffled list the next visitors are drawn from, so every boss turns up
    int nextBossTick_ = 0, bossSpawnTick_ = 0, nextBossCount_ = 1;
    bool bossWarned_ = false;
    std::vector<const char*> rushCel_, rushTerr_;  // Boss Rush: this game's order of celestials and terrestrials
    int closeAt_ = -1;      // a win was announced: close the arena at this tick
    int closeTick_ = -1;    // when the closers arrived (-1 = arena open)
    int reopenTick_ = -1;   // when the fresh round starts
    int openedTick_ = 0;

    // --- state of the newer modes
    bool noBases_ = false;      // a team mode without bases (Tag, Mothership)
    uint32_t leaderId_ = 0;     // bounty: the tank with the highest score...
    double leaderBounty_ = 0;   // ...and the extra score for destroying it
    bool tagSeen_[4] = {false, false, false, false};  // Tag: teams that have had a member (an emptied team is out)
    bool brBegun_ = false;      // Battle Royale: the battle is on (no more spawns)
    int brStartTick_ = -1;      // when it begins (-1 = nobody has joined yet)
    int brBeganTick_ = 0, brStarters_ = 0;
    Vec2 brCentre_;
    float brRadius_ = 1e9f, brRadius0_ = 1e9f;
    uint32_t brClosers_[4] = {0, 0, 0, 0};
    std::vector<Dom> mothers_;  // Mothership: one per team, in team order
    Vec2 hill_;                 // King of the Hill
    int hillScore_[4] = {0, 0, 0, 0}, hillOwner_ = -1;  // owner: a team, -1 nobody, -2 contested
    int siegeEnd_ = 0, siegeTaken_ = 0;  // Siege: the tick the attackers' time runs out; dominators taken
    std::vector<Flag> flags_;   // Capture the Flag
    int ctfScore_[2] = {0, 0}, ctfEnd_ = 0;
    bool ctfOvertime_ = false;
    int bestWave_ = 0;          // Endless Boss Rush: the furthest wave reached since the server started
    int sandBots_ = 0;          // Sandbox: bots asked for with the panel

    // population
    std::vector<BotSlot> bots_;
    int botOverride_ = -1;  // bot count chosen by the host (-1 = each mode's own default)
    int foodCount_ = 0, nestCount_ = 0;
    int foodTarget_ = 300, nestTarget_ = 0;
    int ffaTeamSeq_ = 1000;

    // spatial hash
    float gcell_ = 120;
    int gcols_ = 0, grows_ = 0;
    std::vector<std::vector<Entity*>> grid_;
    std::vector<int> bfs_;  // scratch for pathfinding
};

// Visit every live entity whose circle touches the circle (p, r). Uses the grid built at tick start.
template <class F>
void Room::query(Vec2 p, float r, F&& fn) {
    int x0 = std::clamp(int(std::floor((p.x - r) / gcell_)), 0, gcols_ - 1);
    int x1 = std::clamp(int(std::floor((p.x + r) / gcell_)), 0, gcols_ - 1);
    int y0 = std::clamp(int(std::floor((p.y - r) / gcell_)), 0, grows_ - 1);
    int y1 = std::clamp(int(std::floor((p.y + r) / gcell_)), 0, grows_ - 1);
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++)
            for (Entity* e : grid_[size_t(cy) * gcols_ + cx]) {
                if (e->dead || e->hp <= 0) continue;
                if (std::max(e->gx0, x0) != cx || std::max(e->gy0, y0) != cy) continue;  // big entities: visit once
                float rr = r + e->size;
                if ((e->pos - p).len2() <= rr * rr) fn(*e);
            }
}

double levelScore(int level);  // total score needed to reach a level
