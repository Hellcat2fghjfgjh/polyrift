// Core simulation: entities, movement, guns, collisions, damage, score, maps.
#include "room.hpp"

#include <chrono>
#include <cstdio>
#include <deque>
#include <initializer_list>

namespace {
const float BASE_SIZE = 20;       // tank radius at level 1
const float BASE_ACCEL = 0.55f;   // units / tick^2
const float BULLET_DMG = 4;       // contact damage per tick
const float BULLET_HP = 6;
const int BULLET_LIFE = 60;       // ticks
const float BASE_RELOAD = 18;     // ticks between shots
const float RECOIL = 0.8f;
// Health and shield of player tanks coming back (see the end of Room::thinkTank).
const float REGEN_RATE = 0.00025f;    // part of max health regained every tick, before the Regeneration stat
const float REGEN_REST = 3;           // ...times this after ten seconds without damage
const int SHIELD_DELAY = 90;          // ticks without damage before the shield starts to refill...
const int SHIELD_DELAY_STEP = 4;      // ...less this many per point of Shield Regeneration
const float SHIELD_RATE = 0.005f;     // part of the shield refilled every tick...
const float SHIELD_RATE_STEP = 0.001f;  // ...plus this per point of Shield Regeneration

const char* const BOT_NAMES[] = {
    "Quill", "Ember", "Nimbus", "Tarn", "Vexel", "Orbit", "Sable", "Juno", "Pico", "Rook", "Halcyon", "Mako",
    "Fennec", "Zephyr", "Cinder", "Lumen", "Basalt", "Kestrel", "Onyx", "Piper", "Dax", "Wren", "Sprocket",
    "Tundra", "Argo", "Pebble", "Hex", "Moxie", "Flint", "Yarrow", "Drift", "Nova", "Bramble", "Cobalt",
    "Tully", "Vesper", "Gizmo", "Rune", "Skipper", "Atlas"};
const int BOT_NAME_COUNT = int(sizeof(BOT_NAMES) / sizeof(BOT_NAMES[0]));
}  // namespace

// Developer-code guessing protection, shared by every arena: after too many wrong codes in a
// short time nobody can log in as a developer for a while.
namespace {
struct DevGuard {
    int fails = 0;
    std::chrono::steady_clock::time_point windowStart, lockedUntil;
} g_devGuard;

bool sameCode(const std::string& a, const std::string& b) {  // compare without stopping at the first difference
    unsigned diff = unsigned(a.size() ^ b.size());
    for (size_t i = 0; i < a.size(); i++) diff |= unsigned(a[i] ^ b[i % std::max<size_t>(1, b.size())]);
    return diff == 0;
}
}  // namespace

double levelScore(int level) {
    if (level <= 1) return 0;
    return std::floor(1.3 * std::pow(double(level - 1), 2.6));
}

// ============================================================ setup
Room::Room(std::vector<Mode> opts, uint32_t seed) : rng_(seed) {
    options = std::move(opts);
    if (options.empty()) options.push_back(M_FFA);
    configure(options[size_t(rng_.irange(0, int(options.size()) - 1))]);
    populate();
    flushPending();
}

Room::Room(Mode m, uint32_t seed) : Room(std::vector<Mode>{m}, seed) {}

// Everything that depends on the mode: map size, teams, maze, bot list. Used when the arena is
// created and again whenever a slot switches mode for a new game (all entities are gone by then).
void Room::configure(Mode m) {
    mode = m;
    cols_ = rows_ = 0;
    teamCount_ = 0;
    nestTarget_ = 0;
    noBases_ = false;
    walls_.clear();
    bases_.clear();
    // every mode starts from a clean slate
    leaderId_ = 0;
    leaderBounty_ = 0;
    for (bool& b : tagSeen_) b = false;
    brBegun_ = false;
    brStartTick_ = -1;
    brBeganTick_ = brStarters_ = 0;
    brRadius_ = brRadius0_ = 1e9f;
    for (uint32_t& id : brClosers_) id = 0;
    mothers_.clear();
    for (int& s : hillScore_) s = 0;
    hillOwner_ = -1;
    siegeEnd_ = siegeTaken_ = 0;
    flags_.clear();
    ctfScore_[0] = ctfScore_[1] = 0;
    ctfEnd_ = 0;
    ctfOvertime_ = false;
    int botCount = 14;
    switch (m) {
        case M_FFA:  key = "ffa";  title = "Free For All"; W = H = 5000; foodTarget_ = 340; nestTarget_ = 22; break;
        case M_MAZE: key = "maze"; title = "Maze"; cols_ = rows_ = 28; cell_ = 200; W = H = 5600; foodTarget_ = 300; break;
        case M_TDM2: key = "2tdm"; title = "2 Teams"; W = H = 5000; teamCount_ = 2; foodTarget_ = 340; nestTarget_ = 22; break;
        case M_TDM4: key = "4tdm"; title = "4 Teams"; W = H = 5400; teamCount_ = 4; foodTarget_ = 380; nestTarget_ = 22; botCount = 16; break;
        case M_DOM:  key = "dom";  title = "Domination"; W = H = 5400; teamCount_ = 2; foodTarget_ = 360; nestTarget_ = 14; break;
        case M_RUSH: key = "rush"; title = "Boss Rush"; W = H = 5400; teamCount_ = 1; foodTarget_ = 160; botCount = 12; break;
        case M_MAZEDOM:
            key = "mazedom"; title = "Maze Domination"; cols_ = rows_ = 28; cell_ = 200; W = H = 5600; teamCount_ = 2; foodTarget_ = 300;
            break;
        case M_SANDBOX: key = "sandbox"; title = "Sandbox"; W = H = 4200; foodTarget_ = 220; nestTarget_ = 12; botCount = 0; break;
        case M_TAG:     key = "tag"; title = "Tag"; W = H = 5200; teamCount_ = 4; noBases_ = true; foodTarget_ = 360; nestTarget_ = 22; botCount = 16; break;
        case M_BR:      key = "br"; title = "Battle Royale"; W = H = 5000; foodTarget_ = 340; nestTarget_ = 22; botCount = 15; break;
        case M_MOTHER:  key = "mother"; title = "Mothership"; W = H = 5400; teamCount_ = 2; noBases_ = true; foodTarget_ = 360; nestTarget_ = 14; break;
        case M_KOTH:    key = "koth"; title = "King of the Hill"; W = H = 5000; teamCount_ = 2; foodTarget_ = 340; break;
        case M_SIEGE:   key = "siege"; title = "Siege"; W = H = 5400; teamCount_ = 2; foodTarget_ = 360; break;
        case M_CTF:     key = "ctf"; title = "Capture the Flag"; W = H = 5000; teamCount_ = 2; foodTarget_ = 340; nestTarget_ = 14; break;
        case M_ENDLESS: key = "endless"; title = "Endless Boss Rush"; W = H = 5400; teamCount_ = 1; foodTarget_ = 160; botCount = 12; break;
        case M_COUNT: break;
    }
    hill_ = {W / 2, H / 2};
    brCentre_ = {W / 2, H / 2};
    gcols_ = int(std::ceil(W / gcell_)) + 1;
    grows_ = int(std::ceil(H / gcell_)) + 1;
    grid_.assign(size_t(gcols_) * grows_, {});
    setupTeams();
    if (cols_) generateMaze();  // after the teams: bases and dominator sites are kept clear of walls
    nextBossTick_ = -1;
    // Sandbox has the bots its players ask for; everywhere else the host's --bots or the mode's own number.
    makeBots(mode == M_SANDBOX ? sandBots_ : botOverride_ >= 0 ? botOverride_ : botCount);
}

void Room::populate() {
    if (domMode()) setupDominators();
    if (mode == M_SIEGE) setupSiege();
    if (rushMode()) setupSanctuaries();
    if (mode == M_MOTHER) setupMotherships();
    if (mode == M_CTF) setupFlags();
    setupProtectors();
    while (foodCount_ < foodTarget_) spawnFood(false);
    while (nestCount_ < nestTarget_) spawnFood(true);
}

void Room::setBots(int n) {
    botOverride_ = n;
    if (mode != M_SANDBOX) makeBots(n);
}

void Room::makeBots(int n) {
    bots_.clear();
    int nameStart = rng_.irange(0, BOT_NAME_COUNT - 1);
    for (int i = 0; i < n; i++) {
        BotSlot s;
        s.name = BOT_NAMES[(nameStart + i) % BOT_NAME_COUNT];
        if (i >= BOT_NAME_COUNT) s.name += " " + std::to_string(i / BOT_NAME_COUNT + 1);
        s.respawnAt = tickNo + rng_.irange(0, 60);
        bots_.push_back(s);
    }
}

int Room::players() const {
    int n = 0;
    for (const Client* c : clients)
        if (c->tankId) n++;
    return n;
}

Room::~Room() {
    for (Entity* e : ents) delete e;
    for (Entity* e : pending_) delete e;
}

// Random wall segments, then keep only the largest connected open region so nobody spawns in a sealed pocket.
void Room::generateMaze() {
    const int n = cols_ * rows_;
    // Cells that must stay open and reachable: team bases and dominator sites (Maze Domination).
    std::vector<uint8_t> keep(size_t(n), 0);
    auto reserve = [&](float x0, float y0, float x1, float y1) {
        int cx0 = std::clamp(int(std::floor(x0 / cell_)), 0, cols_ - 1), cx1 = std::clamp(int(std::floor(x1 / cell_)), 0, cols_ - 1);
        int cy0 = std::clamp(int(std::floor(y0 / cell_)), 0, rows_ - 1), cy1 = std::clamp(int(std::floor(y1 / cell_)), 0, rows_ - 1);
        for (int cy = cy0; cy <= cy1; cy++)
            for (int cx = cx0; cx <= cx1; cx++) keep[size_t(cy * cols_ + cx)] = 1;
    };
    for (const Base& b : bases_) reserve(b.x - 150, b.y - 150, b.x + b.w + 150, b.y + b.h + 150);
    if (domMode())
        for (int i = 0; i < 4; i++) reserve(domSpot(i).x - 330, domSpot(i).y - 330, domSpot(i).x + 330, domSpot(i).y + 330);

    // The wall randomizer: every game rolls one of these layouts, so no two mazes feel the same.
    //   segments = thin wall pieces (how many, as a percentage of the number of cells; shortest and longest piece in cells)
    //   blocks   = solid squares (how many, smallest and largest side in cells)
    struct Style { const char* name; int segPct, lenMin, lenMax, blocks, blockMin, blockMax; };
    static const Style styles[] = {
        {"Classic", 8, 2, 6, 7, 2, 2},    {"Open field", 4, 2, 5, 4, 2, 2},  {"Tight", 10, 2, 5, 5, 2, 2},
        {"Corridors", 3, 6, 11, 2, 2, 2}, {"Blocks", 2, 2, 3, 13, 2, 3},     {"Pillars", 14, 1, 1, 3, 2, 2},
        {"Fortresses", 3, 3, 6, 5, 3, 4}};
    const int styleCount = int(sizeof(styles) / sizeof(styles[0]));
    // Team mazes are mirrored left to right so neither side gets the easier half; free-for-all mazes sometimes are.
    const bool mirror = teamCount_ > 0 || rng_.chance(0.3f);

    for (int attempt = 0; attempt < 120; attempt++) {
        const Style& st = styles[rng_.irange(0, styleCount - 1)];  // a layout that cannot be made fair is simply re-rolled
        walls_.assign(n, 0);
        int segs = n * st.segPct / 100;
        for (int s = 0; s < segs; s++) {
            bool horiz = rng_.chance(0.5f);
            int len = rng_.irange(st.lenMin, st.lenMax);
            int x = rng_.irange(0, cols_ - 1), y = rng_.irange(0, rows_ - 1);
            for (int k = 0; k < len; k++) {
                int cx = horiz ? x + k : x, cy = horiz ? y : y + k;
                if (cx < cols_ && cy < rows_) walls_[cy * cols_ + cx] = 1;
            }
        }
        for (int b = 0; b < st.blocks; b++) {
            int size = rng_.irange(st.blockMin, st.blockMax);
            int x = rng_.irange(0, cols_ - size), y = rng_.irange(0, rows_ - size);
            for (int dy = 0; dy < size; dy++)
                for (int dx = 0; dx < size; dx++) walls_[(y + dy) * cols_ + x + dx] = 1;
        }
        if (mirror)
            for (int y = 0; y < rows_; y++)
                for (int x = 0; x < cols_ / 2; x++) walls_[y * cols_ + (cols_ - 1 - x)] = walls_[y * cols_ + x];
        for (int i = 0; i < n; i++)
            if (keep[size_t(i)]) walls_[i] = 0;
        mazeStyle_ = std::string(st.name) + (mirror ? " (mirrored)" : "");
        std::vector<int> comp(n, -1);
        int best = -1, bestSize = 0, compCount = 0;
        for (int start = 0; start < n; start++) {
            if (walls_[start] || comp[start] >= 0) continue;
            int id = compCount++, size = 0;
            std::deque<int> q{start};
            comp[start] = id;
            while (!q.empty()) {
                int c = q.front();
                q.pop_front();
                size++;
                int cx = c % cols_, cy = c / cols_;
                const int nx[4] = {cx + 1, cx - 1, cx, cx}, ny[4] = {cy, cy, cy + 1, cy - 1};
                for (int k = 0; k < 4; k++) {
                    if (nx[k] < 0 || ny[k] < 0 || nx[k] >= cols_ || ny[k] >= rows_) continue;
                    int o = ny[k] * cols_ + nx[k];
                    if (walls_[o] || comp[o] >= 0) continue;
                    comp[o] = id;
                    q.push_back(o);
                }
            }
            if (size > bestSize) { bestSize = size; best = id; }
        }
        bool reachable = true;  // every base and dominator must be in the one big open region
        for (int i = 0; i < n; i++)
            if (keep[size_t(i)] && comp[i] != best) reachable = false;
        for (int i = 0; i < n; i++)
            if (!walls_[i] && comp[i] != best) walls_[i] = 1;
        if (reachable && bestSize >= n * 50 / 100) return;
    }
    walls_.assign(n, 0);  // could not make a fair maze: play this round without walls
    mazeStyle_ = "No walls";
}

// Where the four dominators stand. In a maze they sit in the middle of a cell.
Vec2 Room::domSpot(int i) const {
    const float fx[4] = {0.32f, 0.68f, 0.32f, 0.68f}, fy[4] = {0.32f, 0.32f, 0.68f, 0.68f};
    Vec2 p{W * fx[i], H * fy[i]};
    if (cols_) {
        p.x = (std::floor(p.x / cell_) + 0.5f) * cell_;
        p.y = (std::floor(p.y / cell_) + 0.5f) * cell_;
    }
    return p;
}

void Room::setupTeams() {
    if (noBases_) return;  // Tag and Mothership: teams without a base
    if (teamCount_ == 2) {
        float bw = W * 0.11f, bh = H * 0.3f;
        bases_.push_back({0, (H - bh) / 2, bw, bh, 0});
        bases_.push_back({W - bw, (H - bh) / 2, bw, bh, 1});
    } else if (teamCount_ == 4) {
        float s = W * 0.14f;
        bases_.push_back({0, 0, s, s, 0});
        bases_.push_back({W - s, H - s, s, s, 1});
        bases_.push_back({W - s, 0, s, s, 2});
        bases_.push_back({0, H - s, s, s, 3});
    }
}

// Computer-controlled tanks (dominators, protectors, closers, bosses) share this skeleton:
// fixed size and health, never level up, and by default cannot be pushed around.
Entity* Room::spawnNpc(const char* def, Role role, int team, uint8_t color, Vec2 pos, float size, float hp) {
    Entity* e = create(K_TANK);
    e->tank = std::make_unique<TankData>();
    TankData& t = *e->tank;
    t.role = role;
    t.def = std::max(0, tankIndex(def));
    t.name = def;
    t.spawnTick = tickNo;
    e->team = team;
    e->color = color;
    e->pos = pos;
    e->size = size;
    e->hp = e->maxHp = hp;
    e->damage = 5;
    e->projDamage = 4;
    e->mass = 1e9f;
    e->pushf = 0;
    e->damp = 0;
    return e;
}

void Room::setupDominators() {
    const char* types[4] = {"Destroyer Dominator", "Gunner Dominator", "Trapper Dominator", "Gunner Dominator"};
    int shift = rng_.irange(0, 3);
    for (int i = 0; i < 4; i++) {
        // Until a team captures it, a dominator is on Team Polygon: it shoots tanks, never shapes or bosses.
        Entity* e = spawnNpc(types[(i + shift) % 4], R_DOM, TEAM_FOOD, C_NEUTRAL, domSpot(i), 62, 1800);
        e->tank->dom = i;
        e->tank->name = "Dominator";
        doms_.push_back({e->pos, e->id});
    }
}

// Boss Rush: four sanctuaries around the middle of the map. Everyone is on one team, spawns beside a
// sanctuary and loses the game when the last one falls.
void Room::setupSanctuaries() {
    sancts_.clear();
    const float fx[4] = {0.38f, 0.62f, 0.38f, 0.62f}, fy[4] = {0.38f, 0.38f, 0.62f, 0.62f};
    for (int i = 0; i < 4; i++) {
        Entity* e = spawnNpc("Sanctuary", R_SANCT, 0, teamColor(0), {W * fx[i], H * fy[i]}, 62, 6000);
        e->damage = 6;
        sancts_.push_back({e->pos, e->id});
    }
}

// A sanctuary is never removed. When it is destroyed it turns yellow and stops working; the players
// get it back by destroying the yellow one, like capturing a dominator.
bool Room::sanctuaryHeld(const Dom& d) const {
    Entity* e = get(d.ent);
    return e && e->team == 0;
}

int Room::sanctuariesAlive() const {
    int n = 0;
    for (const Dom& d : sancts_)
        if (sanctuaryHeld(d)) n++;
    return n;
}

// Every team base gets invulnerable protectors that chase intruders away with drone swarms.
void Room::setupProtectors() {
    for (const Base& b : bases_) {
        std::vector<Vec2> spots;
        if (b.h > b.w * 1.6f) {
            spots.push_back({b.x + b.w / 2, b.y + b.h * 0.25f});
            spots.push_back({b.x + b.w / 2, b.y + b.h * 0.75f});
        } else {
            spots.push_back({b.x + b.w / 2, b.y + b.h / 2});
        }
        for (Vec2 p : spots) {
            Entity* e = spawnNpc("Base Protector", R_PROTECTOR, b.team, teamColor(b.team), p, 46, 1);
            e->tank->invincible = true;
            e->angle = rng_.range(0, TAU);
        }
    }
}

// ============================================================ entity management
Entity* Room::create(Kind k) {
    Entity* e = new Entity();
    e->id = nextId_++;
    e->kind = k;
    byId_[e->id] = e;
    pending_.push_back(e);
    return e;
}

Entity* Room::get(uint32_t id) const {
    if (!id) return nullptr;
    auto it = byId_.find(id);
    return (it == byId_.end() || it->second->dead) ? nullptr : it->second;
}

void Room::flushPending() {
    for (Entity* e : pending_) ents.push_back(e);
    pending_.clear();
}

void Room::sweep() {
    size_t w = 0;
    for (size_t i = 0; i < ents.size(); i++) {
        Entity* e = ents[i];
        if (e->dead) {
            byId_.erase(e->id);
            delete e;
        } else {
            ents[w++] = e;
        }
    }
    ents.resize(w);
}

int Room::countKind(Kind k) const {
    int n = 0;
    for (Entity* e : ents)
        if (!e->dead && e->kind == k) n++;
    return n;
}

bool Room::wallAt(int cx, int cy) const {
    if (!cols_ || cx < 0 || cy < 0 || cx >= cols_ || cy >= rows_) return false;
    return walls_[size_t(cy) * cols_ + cx] != 0;
}

bool Room::openSpot(Vec2 p, float r) const {
    if (p.x < r || p.y < r || p.x > W - r || p.y > H - r) return false;
    if (!cols_) return true;
    int x0 = int(std::floor((p.x - r) / cell_)), x1 = int(std::floor((p.x + r) / cell_));
    int y0 = int(std::floor((p.y - r) / cell_)), y1 = int(std::floor((p.y + r) / cell_));
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++)
            if (wallAt(cx, cy)) return false;
    return true;
}

Vec2 Room::randomOpenPoint(float r) {
    for (int i = 0; i < 300; i++) {
        Vec2 p{rng_.range(r, W - r), rng_.range(r, H - r)};
        if (openSpot(p, r)) return p;
    }
    return {W / 2, H / 2};
}

Vec2 Room::spawnPoint(int team) {
    if (mode == M_MOTHER && team >= 0 && team < int(mothers_.size())) {  // beside the team's Mothership
        if (Entity* m = get(mothers_[size_t(team)].ent))
            for (int tries = 0; tries < 16; tries++) {
                Vec2 p = m->pos + fromAngle(rng_.range(0, TAU), m->size + rng_.range(150, 360));
                if (openSpot(p, 50)) return p;
            }
    }
    if (rushMode()) {  // beside a sanctuary that is still standing
        std::vector<Vec2> alive;
        for (const Dom& d : sancts_)
            if (sanctuaryHeld(d)) alive.push_back(d.pos);
        if (!alive.empty()) return alive[size_t(rng_.irange(0, int(alive.size()) - 1))] + fromAngle(rng_.range(0, TAU), rng_.range(130, 260));
    }
    for (const Base& b : bases_)
        if (b.team == team) return {rng_.range(b.x + 50, b.x + b.w - 50), rng_.range(b.y + 50, b.y + b.h - 50)};
    // Free-for-all: pick the candidate that is farthest from every other tank. Teams without a
    // base (Tag) do the same, but only enemies count. Battle Royale spawns inside the safe circle.
    Vec2 best{W / 2, H / 2};
    float bestD = -1;
    for (int i = 0; i < 14; i++) {
        Vec2 p = randomOpenPoint(50);
        if (mode == M_BR && !inZone(p, 120)) continue;
        float d = 1e12f;
        for (Entity* e : ents)
            if (!e->dead && e->kind == K_TANK && !(teamCount_ && e->team == team)) d = std::min(d, (e->pos - p).len2());
        if (d > bestD) { bestD = d; best = p; }
    }
    return best;
}

int Room::smallestTeam() const {
    if (!teamCount_) return -1;
    if (mode == M_TAG) {  // the smallest team that is still in the game (a team that has lost everyone is out)
        std::vector<int> n = teamMembers();
        int best = -1;
        for (int i = 0; i < teamCount_; i++) {
            if (tagSeen_[i] && n[size_t(i)] == 0) continue;
            if (best < 0 || n[size_t(i)] < n[size_t(best)]) best = i;
        }
        return std::max(best, 0);
    }
    std::vector<int> n(teamCount_, 0);
    for (const BotSlot& b : bots_)
        if (b.ent && b.team >= 0 && b.team < teamCount_) n[b.team]++;
    for (const Client* c : clients)
        if (c->played && c->team >= 0 && c->team < teamCount_) n[c->team]++;
    int best = 0;
    for (int i = 1; i < teamCount_; i++)
        if (n[i] < n[best]) best = i;
    return best;
}

// Recompute a tank's derived stats from its class, level and skill points.
void Room::refresh(Entity& e) {
    TankData& t = *e.tank;
    if (t.role != R_PLAYER) return;
    const TankDef& d = TANKS[t.def];
    float lv = float(t.level - 1);
    e.size = BASE_SIZE * (1 + 0.012f * lv) * d.size;
    float oldMax = e.maxHp;
    t.invincible = d.godmode || t.god;
    e.maxHp = (100 + 2 * lv) * (1 + 0.18f * t.skills[S_HEALTH]) * d.hp;
    e.hp = oldMax > 0 ? e.hp / oldMax * e.maxHp : e.maxHp;
    e.maxShield = (20 + 0.5f * lv) * (1 + 0.3f * t.skills[S_SHIELD]);
    e.shield = std::min(e.shield, e.maxShield);
    e.damage = 3 * (1 + 0.2f * t.skills[S_BODY]) * d.body;
    e.projDamage = 3 * (1 + 0.04f * t.skills[S_BODY]);  // how fast this body wears bullets down
    e.mass = e.size * e.size * (1 + 0.08f * t.skills[S_BODY]);
}

void Room::setDef(Entity& e, int def) {
    TankData& t = *e.tank;
    t.def = def;
    t.gunT.clear();
    t.tur.clear();
    t.alpha = 1;
    t.zooming = false;
    t.zoomAmt = 0;
    t.wasFiring = t.wasAlt = false;
    t.traps.clear();
    const TankDef& d = TANKS[t.def];
    // The tank keeps its drones through a class change, up to what the new class may have. They
    // take on the habits of the new class's own drones (do they follow the cursor, do they carry a
    // gun). A drone of another kind altogether (a Spawner's small tanks, say) is let go.
    const GunDef* hatch = nullptr;  // what the new class launches (for the Necromancer line: what it converts shapes into)
    for (const GunDef& g : d.guns)
        if (g.type == G_DRONE || g.type == G_MINION) { hatch = &g; break; }
    int kept = 0;
    auto keep = [&](Entity* o) {
        if (o->kind != K_DRONE || o->owner != e.id || !o->counted || o->hp <= 0) return;
        const bool minion = o->arm == 1;
        if (!hatch || kept >= d.maxDrones || minion != (hatch->type == G_MINION)) {
            o->hp = 0;
            o->lastHitBy = 0;
            o->counted = false;
            return;
        }
        kept++;
        o->indep = hatch->indep;
        if (!minion) {
            o->arm = hatch->armed ? 2 : 0;
            if (hatch->armed) o->shape = 2;
            else if (o->shape == 2) o->shape = 3;
        }
    };
    for (Entity* o : ents)
        if (!o->dead) keep(o);
    for (Entity* o : pending_) keep(o);
    t.drones = kept;
    // The new class may have other stats or another cap (the Smasher line): points that no longer
    // fit are handed back to be spent again.
    for (int i = 0; i < S_COUNT; i++) {
        int allowed = ((d.statMask >> i) & 1) ? d.statCap : 0;
        if (t.skills[i] > allowed) {
            t.skillPts += t.skills[i] - allowed;
            t.skills[i] = allowed;
        }
    }
    refresh(e);
    giveStarters(e);
}

// The Necromancer line cannot launch drones (it converts the squares it destroys), so a tank that
// has just become one would have nothing to fight with. It is given a few to start from.
void Room::giveStarters(Entity& e) {
    TankData& t = *e.tank;
    const TankDef& d = TANKS[t.def];
    if (t.role != R_PLAYER || d.starter <= 0 || d.guns.empty()) return;
    for (int i = t.drones; i < std::min(d.starter, d.maxDrones); i++) {
        float a = TAU * float(i) / float(d.starter) + e.angle;
        Entity* dr = launch(e, &e, d.guns[0], e.pos + fromAngle(a, e.size * 2.2f), a, 1);
        dr->pos = e.pos + fromAngle(a, e.size * 2.2f);
        dr->vel = {0, 0};
        dr->size = FOODS[F_SQUARE].size * 0.95f;
        dr->mass = dr->size * dr->size;
        dr->shape = FOODS[F_SQUARE].sides;
    }
}

// Sandbox: put a tank at any level. Its skill points are handed back so the build can be redone.
void Room::setLevel(Entity& e, int level) {
    TankData& t = *e.tank;
    level = std::clamp(level, 1, MAX_LEVEL);
    t.score = levelScore(level);
    t.level = level;
    int pts = 0;
    for (int lv = 2; lv <= level; lv++)
        if (lv <= 40 || lv % 2 == 0 || lv == MAX_LEVEL) pts++;
    for (int& s : t.skills) s = 0;
    t.skillPts = pts;
    if (TANKS[t.def].minLevel > level && !TANKS[t.def].hidden) setDef(e, 0);  // too low for this class: back to Basic
    refresh(e);
}

void Room::addScore(Entity& e, double s) {
    TankData& t = *e.tank;
    if (t.role != R_PLAYER && t.client && s > 0) t.client->domScore += s;  // a player is driving it (dominator, Mothership): they keep what it earns
    if (t.role != R_PLAYER || s <= 0) return;
    t.score += s;
    bool up = false;
    while (t.level < MAX_LEVEL && t.score >= levelScore(t.level + 1)) {
        t.level++;
        // One point per level up to 40, then every second level: 42 points in total at level 45.
        if (t.level <= 40 || t.level % 2 == 0 || t.level == MAX_LEVEL) t.skillPts++;
        up = true;
    }
    if (up) refresh(e);
}

std::vector<int> Room::upgradeOptions(const TankData& t) const {
    std::vector<int> out;
    if (t.role != R_PLAYER) return out;
    for (int i : TANKS[t.def].upgrades)
        if (TANKS[i].minLevel <= t.level) out.push_back(i);
    return out;
}

Entity* Room::spawnTank(const std::string& name, int team, double score, Client* c, int slot) {
    Entity* e = create(K_TANK);
    e->tank = std::make_unique<TankData>();
    TankData& t = *e->tank;
    t.name = name;
    t.client = c;
    t.bot = (c == nullptr);
    t.slot = slot;
    t.spawnTick = tickNo;
    t.invuln = SPAWN_PROTECT;
    e->team = teamCount_ ? team : ffaTeamSeq_++;
    if (mode == M_TAG && e->team >= 0 && e->team < 4) tagSeen_[e->team] = true;
    e->color = teamColor(e->team);
    e->damp = 0.9f;
    e->pushf = 1;
    e->pos = spawnPoint(e->team);
    e->angle = rng_.range(-PI, PI);
    e->maxHp = 0;
    refresh(*e);
    addScore(*e, std::max(score, startScore));
    e->hp = e->maxHp;
    e->shield = e->maxShield;
    if (t.bot) {
        BotBrain& b = t.ai;
        b.offset = rng_.irange(0, 63);
        b.strafe = rng_.chance(0.5f) ? 1.f : -1.f;
        const int base[S_COUNT] = {0, 3, 2, 3, 3, 5, 5, 3, 1, 2};
        for (int i = 0; i < S_COUNT; i++) b.weights[i] = base[i] + rng_.irange(0, 2);
        botPlan(*e);  // difficulty, the class to work towards, and the skill build that suits it
    }
    return e;
}

// (Re)apply the stats of a polygon: its shape, its size (normal, beta, alpha, omega), its mutation
// and whether it is a crasher. Health keeps its fraction, so this also serves when a polygon evolves.
void Room::setFood(Entity& e, int type, int tier, int mut, bool crasher) {
    const FoodDef& f = FOODS[type];
    const TierDef& td = TIERS[tier];
    const MutationDef& m = MUTATIONS[mut];
    float frac = e.maxHp > 0 ? e.hp / e.maxHp : 1;
    float size, hp, dmg, xp;
    if (crasher) {  // two sizes only
        size = tier ? 22.f : 13.f;
        hp = tier ? 110.f : 24.f;
        dmg = tier ? 2.8f : 1.8f;
        xp = tier ? 90.f : 25.f;
    } else {
        size = f.size * td.size;
        hp = f.hp * td.hp;
        dmg = f.dmg * td.dmg;
        xp = f.xp * td.xp;
    }
    e.shape = uint8_t(type);
    e.tier = uint8_t(tier);
    e.mut = uint8_t(mut);
    e.crasher = crasher;
    e.color = m.color != 255 ? m.color : crasher ? uint8_t(C_ELITE) : f.color;
    e.size = size;
    e.maxHp = hp * m.hp;
    e.hp = e.maxHp * frac;
    e.damage = e.projDamage = dmg;
    e.xp = int(std::min(double(FOOD_XP_CAP), double(xp) * m.xp));
    e.mass = size * size * 1.5f;
}

void Room::spawnFood(bool nest) {
    float u = rng_.uni() * 100;
    int type;
    if (nest) type = u < 86 ? F_PENTAGON : F_HEXAGON;
    else if (cols_) type = u < 14 ? F_EGG : u < 61 ? F_SQUARE : u < 84.5f ? F_TRIANGLE : u < 97.9f ? F_PENTAGON : F_HEXAGON;
    else type = u < 15 ? F_EGG : u < 64.5f ? F_SQUARE : u < 88 ? F_TRIANGLE : u < 97.9f ? F_PENTAGON : F_HEXAGON;  // 1 hexagon in 48

    // Size: every shape has a beta, an alpha and an omega. The nest grows the big ones, and a maze
    // (which has no nest) gets a few more than an open map.
    static const float sizeOdds[3][3] = {{3, 0.4f, 0.04f}, {8, 1.2f, 0.15f}, {19, 3, 0.4f}};  // % beta, alpha, omega
    const float* so = sizeOdds[nest ? 2 : cols_ ? 1 : 0];
    float v = rng_.uni() * 100;
    int tier = v < so[2] ? T_OMEGA : v < so[2] + so[1] ? T_ALPHA : v < so[2] + so[1] + so[0] ? T_BETA : T_NORMAL;

    // A maze cell is 200 across: nothing bigger than an Alpha Pentagon fits between its walls.
    while (cols_ && tier > T_NORMAL && FOODS[type].size * TIERS[tier].size > 92) tier--;

    // One polygon in twenty is a crasher (more of them guard the nest). They come small or large.
    bool crasher = rng_.chance(nest ? 0.12f : 0.047f);
    if (crasher) {
        type = F_TRIANGLE;
        tier = rng_.chance(0.2f) ? 1 : 0;
    }

    // Mutation: rarest first, each with its own odds.
    int mut = MU_NONE;
    if (mutationScale > 0) {
        double r = (double(rng_.gen()) + 0.5) / 4294967296.0;
        for (int m = MU_COUNT - 1; m > MU_NONE; m--) {
            double p = mutationScale / MUTATIONS[m].oneIn;
            if (r < p) { mut = m; break; }
            r -= p;
        }
    }

    float radius = crasher ? 22 : FOODS[type].size * TIERS[tier].size;
    Vec2 p;
    if (nest) {
        p = Vec2{W / 2, H / 2} + fromAngle(rng_.range(0, TAU), std::sqrt(rng_.uni()) * W * 0.1f);
    } else {
        for (int tries = 0; tries < 30; tries++) {
            p = randomOpenPoint(radius + 6);
            bool bad = false;
            for (const Base& b : bases_)
                if (p.x > b.x - 60 - radius && p.x < b.x + b.w + 60 + radius && p.y > b.y - 60 - radius && p.y < b.y + b.h + 60 + radius) bad = true;
            for (const Dom& d : doms_)
                if ((d.pos - p).len2() < (220 + radius) * (220 + radius)) bad = true;
            for (const Dom& d : sancts_)
                if ((d.pos - p).len2() < (220 + radius) * (220 + radius)) bad = true;
            if (!bad) break;
        }
    }
    Entity* e = create(K_FOOD);
    e->team = TEAM_FOOD;
    e->pos = p;
    e->maxHp = 0;
    setFood(*e, type, tier, mut, crasher);
    e->hp = e->maxHp;
    e->angle = rng_.range(0, TAU);
    e->spin = rng_.range(-0.012f, 0.012f);
    e->vel = fromAngle(rng_.range(0, TAU), 0.3f);
    e->nest = nest;
    (nest ? nestCount_ : foodCount_)++;
    if (crasher) statCrashers++;
    if (mut) statMutations++;
}

// Polygons that are left alone slowly grow: egg, square, triangle, pentagon, then the bigger sizes
// (hexagons only grow in size). A mutated polygon grows much faster, so a shiny egg rarely stays
// an egg. Called once a second.
void Room::evolveFood() {
    for (Entity* e : ents) {
        if (e->dead || e->hp <= 0 || e->kind != K_FOOD || e->crasher || e->age < 40 * TICK_RATE) continue;
        if (!rng_.chance(e->mut ? 0.02f : 0.001f)) continue;
        int type = e->shape, tier = e->tier;
        if (type < F_PENTAGON) type++;
        else if (tier < (e->nest || e->mut ? T_OMEGA : T_ALPHA)) tier++;
        else continue;
        float radius = FOODS[type].size * TIERS[tier].size;
        if (cols_ && radius > 92) continue;              // too big for a maze corridor
        if (!openSpot(e->pos, radius * 0.8f)) continue;  // no room to grow here (maze walls)
        setFood(*e, type, tier, e->mut, false);
        statEvolved++;
    }
}

// The living players that damaged `e` in the last `window` ticks, each with their part of all the
// damage it took in that time. Damage by computer-controlled tanks counts towards the total, so
// finishing off something a boss nearly killed pays only for the part you did.
std::vector<Room::Share> Room::damageShares(const Entity& e, int window) const {
    std::vector<Share> out;
    float total = 0;
    for (const Entity::Hit& h : e.hits)
        if (tickNo - h.tick <= window) total += h.dmg;
    if (total <= 0) return out;
    for (const Entity::Hit& h : e.hits) {
        if (tickNo - h.tick > window) continue;
        Entity* k = get(h.by);
        if (!k || k == &e || !k->tank) continue;
        const TankData& kt = *k->tank;
        if (kt.role != R_PLAYER && !kt.client) continue;  // computer-controlled, unless a player is driving it
        out.push_back({k, h.dmg / total});
    }
    return out;
}

// ============================================================ per-tick logic
void Room::tick() {
    tickNo++;
    buildGrid();
    for (Entity* e : ents) {
        if (e->dead || e->hp <= 0) continue;
        if (e->kind == K_TANK) thinkTank(*e);
        else if (e->kind == K_DRONE) thinkDrone(*e);
        else if (e->kind == K_FOOD) { if (e->crasher) crasherThink(*e); }
        else if (e->sub >= 0 || e->boom) thinkProjectile(*e);
    }
    flushPending();
    collide();
    integrate();
    reap();
    manageFood();
    if (tickNo % TICK_RATE == 7) evolveFood();
    manageBots();
    modeLogic();
    flushPending();
    sendUpdates();
    if (tickNo % TICK_RATE == 0) sendBoards();
    sweep();
}

void Room::buildGrid() {
    for (auto& c : grid_) c.clear();
    for (Entity* e : ents) {
        if (e->dead) continue;
        int x0 = std::clamp(int(std::floor((e->pos.x - e->size) / gcell_)), 0, gcols_ - 1);
        int x1 = std::clamp(int(std::floor((e->pos.x + e->size) / gcell_)), 0, gcols_ - 1);
        int y0 = std::clamp(int(std::floor((e->pos.y - e->size) / gcell_)), 0, grows_ - 1);
        int y1 = std::clamp(int(std::floor((e->pos.y + e->size) / gcell_)), 0, grows_ - 1);
        e->gx0 = x0;
        e->gy0 = y0;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) grid_[size_t(y) * gcols_ + x].push_back(e);
    }
}

void Room::thinkTank(Entity& e) {
    TankData& t = *e.tank;
    int since = tickNo - t.lastHurt;
    if (t.role != R_PLAYER) {
        switch (t.role) {
            case R_DOM:
                if (t.client) {  // a player has taken control: they aim and fire, it still cannot move
                    if (t.aim.len2() > 1) e.angle = t.aim.angle();
                    if (TANKS[t.def].ai == AI_TRAPPER) t.fire = true;  // the trapper never stops
                } else {
                    domThink(e);
                }
                break;
            case R_SANCT:  // spins and lays traps like a trapper dominator; a fallen (yellow) one does nothing
                if (e.team == 0) domThink(e);
                else t.fire = false;
                break;
            case R_PROTECTOR: protectorThink(e); break;
            case R_CLOSER: closerThink(e); break;
            case R_MOTHER:
                if (t.client) {  // a player drives it: they steer, aim and command the drones
                    if (t.aim.len2() > 1) e.angle = t.aim.angle();
                } else {
                    motherThink(e);
                }
                break;
            default: bossThink(e); break;
        }
        if (t.accel > 0) {
            Vec2 mv = t.move;
            if (mv.len2() > 1) mv = mv.norm();
            e.vel += mv * t.accel;
        }
        if (t.role == R_SANCT && e.team != 0) return;  // fallen sanctuary: no traps, no healing, no repair
        updateGuns(e);
        updateTurrets(e);
        // (Siege dominators mend at a quarter of the usual rate: the attackers are on a clock.)
        if (t.role == R_DOM && since > 150) e.hp = std::min(e.maxHp, e.hp + e.maxHp * (mode == M_SIEGE ? 0.0003f : 0.0012f));
        if (t.role == R_BOSS && since > 450) e.hp = std::min(e.maxHp, e.hp + e.maxHp * 0.0003f);
        if (t.role == R_SANCT && since > 300) e.hp = std::min(e.maxHp, e.hp + e.maxHp * 0.0004f);
        // (A Mothership never mends by itself: only its team's Healers can repair it.)
        return;
    }
    if (t.bot) botThink(e);
    // Level-up key: free levels up to the server's limit (Sandbox: always to the top).
    const int freeCap = mode == M_SANDBOX ? MAX_LEVEL : freeLevel;
    if (t.lvlKey && t.level < freeCap && tickNo % 2 == 0) addScore(e, levelScore(t.level + 1) - t.score);
    if (t.invuln > 0) {
        const bool fired = (t.fire || (TANKS[t.def].altFire && t.alt)) && tickNo - t.spawnTick > 10;  // (the click on Play does not count)
        if (fired || t.move.len2() > 0.01f) t.stirred = true;
        t.invuln--;
        if (fired) t.invuln = 0;
        // A tank that has not moved or fired yet stays protected past the three seconds, so its
        // player has time to look around (and to self-destruct for another spawn) before committing.
        else if (t.invuln == 0 && !t.stirred && tickNo - t.spawnTick < SPAWN_PROTECT_STILL) t.invuln = 1;
    }
    const TankDef& d = TANKS[t.def];
    // "Reverse tank" turns the body to face away from the cursor; everything else still follows the cursor.
    if (t.aim.len2() > 1) e.angle = t.reverse ? wrapAngle(t.aim.angle() + PI) : t.aim.angle();
    Vec2 mv = t.move;
    if (mv.len2() > 1) mv = mv.norm();
    // The Smasher line has Engine Acceleration (its Reload slot) in place of Movement Speed.
    float drive = d.engine ? 1 + 0.05f * t.skills[S_RELOAD] : 1 + 0.06f * t.skills[S_MOVE];
    float accel = BASE_ACCEL * d.speed * drive * (1 - 0.004f * (t.level - 1));
    if (carriedBy(e.id)) accel *= 0.85f;  // a flag is heavy
    e.vel += mv * accel;
    if (d.zoom > 0 && t.alt) {  // hunter classes: alt-fire slides the view towards where the tank was aiming
        if (!t.zooming) {
            t.zooming = true;
            t.zoomDir = t.aim.norm();
        }
        t.zoomAmt = std::min(1.f, t.zoomAmt + 0.1f);
    } else {
        t.zooming = false;
        t.zoomAmt = std::max(0.f, t.zoomAmt - 0.14f);
    }
    if (d.invis > 0) {  // fade out while standing still and holding fire
        bool active = t.fire || (d.altFire && t.alt) || mv.len2() > 0.01f || t.invuln > 0;
        t.alpha = active ? std::min(1.f, t.alpha + 0.12f) : std::max(0.f, t.alpha - d.invis);
    } else {
        t.alpha = 1;
    }
    updateGuns(e);
    updateTurrets(e);

    // The shield comes back after a pause without damage; health trickles back, faster when left
    // alone for ten seconds. With Shield Regeneration at 9 the shield starts to refill after 1.8
    // seconds and takes about 2.4 more; health returns at about 4% a second, 12% when left alone.
    int regen = t.skills[S_REGEN];
    if (since > SHIELD_DELAY - SHIELD_DELAY_STEP * regen && e.shield < e.maxShield)
        e.shield = std::min(e.maxShield, e.shield + e.maxShield * (SHIELD_RATE + SHIELD_RATE_STEP * regen));
    float rate = REGEN_RATE * (1 + 0.5f * regen);
    if (since > 300) rate *= REGEN_REST;
    if (e.hp < e.maxHp) e.hp = std::min(e.maxHp, e.hp + e.maxHp * rate);
}

static bool spawnerGun(GunType t) { return t == G_DRONE || t == G_MINION; }

void Room::updateGuns(Entity& e) {
    TankData& t = *e.tank;
    const TankDef& d = TANKS[t.def];
    if (t.gunT.size() != d.guns.size()) t.gunT.assign(d.guns.size(), 0.f);
    float base = BASE_RELOAD * std::pow(0.93f, float(t.skills[S_RELOAD]));
    // Two triggers: the main guns follow the fire button, secondary guns (GunDef::alt) follow
    // alt-fire. Each group starts its own burst, with the barrels staggered by their delay.
    const bool firing[2] = {t.fire && t.invuln == 0, t.alt && t.invuln == 0};
    const bool was[2] = {t.wasFiring, t.wasAlt};
    for (int k = 0; k < 2; k++) {
        if (!firing[k] || was[k]) continue;
        bool ready = true, any = false;
        for (size_t i = 0; i < d.guns.size(); i++) {
            const GunDef& g = d.guns[i];
            if (spawnerGun(g.type) || g.type == G_DECOR || int(g.alt) != k) continue;
            any = true;
            if (t.gunT[i] > 0) ready = false;
        }
        if (any && ready)
            for (size_t i = 0; i < d.guns.size(); i++)
                if (!spawnerGun(d.guns[i].type) && int(d.guns[i].alt) == k) t.gunT[i] = d.guns[i].delay * base * d.guns[i].reload;
    }
    t.wasFiring = firing[0];
    t.wasAlt = firing[1];
    for (size_t i = 0; i < d.guns.size(); i++) {
        const GunDef& g = d.guns[i];
        if (g.type == G_DECOR) continue;
        if (t.gunT[i] > 0) t.gunT[i] -= 1;
        // Drone and minion launchers work on their own until the tank has its full complement.
        // Necromancer-style classes never launch anything: they convert the shapes they destroy.
        bool want = spawnerGun(g.type) ? (!d.necro && t.drones < d.maxDrones) : firing[g.alt ? 1 : 0];
        if (want && t.gunT[i] <= 0) {
            fireGun(e, int(i));
            t.gunT[i] += base * g.reload;
        } else if (!want && t.gunT[i] < 0) {
            t.gunT[i] = 0;
        }
    }
}

void Room::fireGun(Entity& e, int gi) {
    const GunDef& g = TANKS[e.tank->def].guns[gi];
    launch(e, &e, g, e.pos, e.angle, e.size);
    e.vel -= fromAngle(e.angle + g.ang) * (RECOIL * g.recoil);
}

// Create whatever a gun fires. `from` is the thing holding the gun (a tank, a turret's tank, a missile...),
// `tank` is the tank that gets the credit and whose skill points apply (may be null).
Entity* Room::launch(Entity& from, Entity* tank, const GunDef& g, Vec2 origin, float ang, float scale) {
    static const int noSkills[S_COUNT] = {0};
    TankData* td = (tank && tank->tank) ? tank->tank.get() : nullptr;
    const int* sk = td ? td->skills : noSkills;
    float ga = ang + g.ang;
    Vec2 fwd = fromAngle(ga), side{-fwd.y, fwd.x};
    Vec2 dir = fromAngle(ga + (rng_.uni() - 0.5f) * g.spread);

    Kind kind = (g.type == G_BULLET) ? K_BULLET : (g.type == G_TRAP || g.type == G_BLOCK) ? K_TRAP : K_DRONE;
    Entity* b = create(kind);
    b->team = from.team;
    b->color = from.color;
    b->owner = tank ? tank->id : 0;
    b->size = std::max(3.f, g.w * 0.5f * scale * g.bsize);
    b->pos = origin + fwd * (g.len * scale) + side * (g.ox * scale);
    b->angle = dir.angle();
    b->from = origin;
    b->leash = g.leash;
    b->knock = g.knock;
    b->ghost = from.ghost;
    float sp = BULLET_SPEED * g.speed * (1 + 0.06f * sk[S_BSPEED]);
    b->damage = b->projDamage = BULLET_DMG * g.dmg * (1 + 0.11f * sk[S_BDMG]);
    b->hp = b->maxHp = BULLET_HP * g.hp * (1 + 0.08f * sk[S_BHEALTH]);
    b->resist = 1.f / (1 + 0.08f * sk[S_BPEN]);
    switch (g.type) {
        case G_BULLET:
            b->vel = dir * sp + from.vel * 0.3f;
            b->damp = 1;
            b->life = int(BULLET_LIFE * g.life);
            b->pushf = 0;
            b->mass = b->size * b->size * 0.6f;
            b->heal = g.heal;
            if (g.wave != 0) {
                // Alt-fire mirrors the weave of the Desmos family: the shot starts towards the other side.
                b->wave = g.wave * BULLET_SPEED * (td && td->alt ? -1.f : 1.f);
                b->wperp = {-dir.y, dir.x};
            }
            break;
        case G_TRAP:
        case G_BLOCK:
            b->shape = g.type == G_TRAP ? 3 : 4;
            b->vel = dir * (sp * 1.35f) + from.vel * 0.3f;
            b->damp = 0.935f;
            b->life = int((g.type == G_TRAP ? 12 : 18) * TICK_RATE * g.life);
            b->pushf = 0.35f;
            b->mass = b->size * b->size * 2;
            b->spin = 0;
            b->boom = g.boom;
            b->merge = g.merge;
            // Classes with a trap limit (Hexa-Trapper, Septa-Trapper): laying one more than the
            // limit removes the oldest.
            if (td && TANKS[td->def].maxTraps > 0) {
                std::vector<uint32_t>& own = td->traps;
                own.erase(std::remove_if(own.begin(), own.end(), [&](uint32_t id) { Entity* o = get(id); return !o || o->hp <= 0; }), own.end());
                own.push_back(b->id);
                while (int(own.size()) > TANKS[td->def].maxTraps) {
                    if (Entity* old = get(own.front())) { old->hp = 0; old->lastHitBy = 0; }
                    own.erase(own.begin());
                    statTrapCaps++;
                }
            }
            break;
        default:  // drones, swarm drones and minions
            b->shape = (td && TANKS[td->def].shape == SH_SQUARE) ? 4 : 3;
            if (g.dshape) b->shape = g.dshape;  // 4 square, 5 pentagon, 6 egg (the mysticals' drones)
            if (g.type == G_MINION) { b->shape = 1; b->arm = 1; }
            else if (g.armed) { b->shape = 2; b->arm = 2; }
            b->indep = g.indep;
            b->vel = dir * (sp * 0.6f);
            b->damp = 1;
            b->maxSpeed = sp * 0.85f;
            b->pushf = 1;
            b->mass = b->size * b->size;
            if (spawnerGun(g.type) && td) {
                b->counted = true;
                td->drones++;
            } else {
                b->life = int(60 * g.life);  // swarm drones are short-lived instead of capped
            }
            break;
    }
    if (g.sub >= 0 && g.sub < int(SUBS.size()) && kind != K_DRONE) {
        const SubDef& s = SUBS[size_t(g.sub)];
        b->sub = g.sub;
        b->spin = s.spin;
        float base = BASE_RELOAD * std::pow(0.93f, float(sk[S_RELOAD]));
        for (size_t i = 0; i < s.guns.size() && i < 4; i++) b->subT[i] = s.guns[i].delay * base * s.guns[i].reload;
    }
    return b;
}

// Can something on `team` shoot at x? Spawn-protected, invulnerable and invisible tanks are left alone.
bool Room::targetable(const Entity& x, int team) const {
    if (x.dead || x.hp <= 0 || x.team == team) return false;
    if (x.kind == K_FOOD) return true;
    if (x.kind != K_TANK) return false;
    const TankData& t = *x.tank;
    if (t.role == R_SANCT && x.team != 0 && team != 0) return false;  // a fallen sanctuary only matters to the players
    return t.invuln == 0 && !t.invincible && t.alpha >= 0.3f;
}

// Nearest visible enemy tank around `from` (optionally only inside an arc); shapes are a last resort.
uint32_t Room::pickTarget(Vec2 from, int team, float range, float dir, float arc, bool foodToo) {
    uint32_t best = 0;
    float bestD = 1e12f;
    query(from, range, [&](Entity& x) {
        if (x.kind != K_TANK && !(foodToo && x.kind == K_FOOD)) return;
        if (!targetable(x, team)) return;
        Vec2 to = x.pos - from;
        if (arc < PI - 0.01f && std::fabs(wrapAngle(to.angle() - dir)) > arc) return;
        float d = to.len() + (x.kind == K_FOOD ? range : 0);
        if (d < bestD && los(from, x.pos)) { bestD = d; best = x.id; }
    });
    return best;
}

// The most badly hurt team-mate tank in range (never the healer itself).
uint32_t Room::pickAlly(const Entity& self, Vec2 from, float range, float dir, float arc) {
    uint32_t best = 0;
    float bestFrac = 0.97f;
    query(from, range, [&](Entity& x) {
        if (x.kind != K_TANK || x.team != self.team || x.id == self.id || x.tank->invincible) return;
        if (self.owner && x.id == self.owner) return;  // a repair block cannot mend the tank that laid it
        Vec2 to = x.pos - from;
        if (arc < PI - 0.01f && std::fabs(wrapAngle(to.angle() - dir)) > arc) return;
        float frac = x.hp / std::max(1.f, x.maxHp);
        if (frac < bestFrac && los(from, x.pos)) { bestFrac = frac; best = x.id; }
    });
    return best;
}

// Auto-turrets: each one tracks its own target. While the player holds fire, turrets that can
// reach the cursor aim there instead.
void Room::updateTurrets(Entity& e) {
    TankData& t = *e.tank;
    const TankDef& d = TANKS[t.def];
    if (d.turrets.empty()) return;
    float base = BASE_RELOAD * std::pow(0.93f, float(d.engine ? 0 : t.skills[S_RELOAD]));
    if (t.tur.size() != d.turrets.size()) {
        t.tur.assign(d.turrets.size(), TurretState());
        for (size_t k = 0; k < d.turrets.size(); k++) {
            t.tur[k].ang = e.angle + d.turrets[k].ang;
            for (const GunDef& g : d.turrets[k].guns) t.tur[k].t.push_back(g.delay * base * g.reload);
        }
    }
    for (size_t k = 0; k < d.turrets.size(); k++) {
        const TurretDef& td = d.turrets[k];
        TurretState& ts = t.tur[k];
        float mountDir = e.angle + td.ang;
        Vec2 mp = e.pos + fromAngle(mountDir) * (td.dist * e.size);
        float range = td.range * (t.role == R_BOSS ? 1.4f : TANKS[t.def].fov);
        const bool healer = !td.guns.empty() && td.guns[0].heal;
        if ((tickNo + int(e.id) + int(k) * 2) % 6 == 0)
            ts.tgt = t.manual ? 0 : healer ? pickAlly(e, mp, range, mountDir, td.arc)
                                           : pickTarget(mp, e.team, range, mountDir, td.arc, t.role == R_PLAYER);
        Entity* tg = get(ts.tgt);
        if (tg && healer) {  // a healing turret keeps on a friend until it is patched up
            if (tg->team != e.team || tg->hp >= tg->maxHp) tg = nullptr;
        } else if (tg && (t.manual || !targetable(*tg, e.team))) {
            tg = nullptr;  // override: no targets of their own
        }

        bool want = false;
        float desired = mountDir;
        // With override on, every turret follows the cursor; otherwise only ones that fire bullets do
        // (trap-laying turrets place their traps by themselves).
        if (t.role == R_PLAYER && t.fire && !td.guns.empty() && (t.manual || td.guns[0].type == G_BULLET)) {
            Vec2 a = e.pos + t.aim - mp;
            if (a.len2() > 1 && std::fabs(wrapAngle(a.angle() - mountDir)) <= td.arc) {
                desired = a.angle();
                want = true;
            }
        }
        if (!want && tg) {
            Vec2 to = tg->pos - mp;
            float bs = BULLET_SPEED * (td.guns.empty() ? 1.f : td.guns[0].speed) * (1 + 0.06f * t.skills[S_BSPEED]);
            Vec2 at = tg->pos + tg->vel * std::min(to.len() / bs, 40.f) - mp;
            if (std::fabs(wrapAngle(at.angle() - mountDir)) <= td.arc) {
                desired = at.angle();
                want = true;
            }
        }
        ts.ang = wrapAngle(ts.ang + clampf(wrapAngle(desired - ts.ang), -0.24f, 0.24f));
        bool aimed = want && t.invuln == 0 && std::fabs(wrapAngle(desired - ts.ang)) < 0.3f;
        for (size_t i = 0; i < td.guns.size(); i++) {
            const GunDef& g = td.guns[i];
            float hold = g.delay * base * g.reload;  // idle guns wait at their stagger offset
            if (aimed) {
                ts.t[i] -= 1;
                if (ts.t[i] <= 0) {
                    launch(e, &e, g, mp, ts.ang, e.size * td.size);
                    ts.t[i] += base * g.reload;
                }
            } else {
                ts.t[i] = std::max(ts.t[i] - 1, hold);
            }
        }
    }
}

// Missiles, pillboxes and boomerang blocks.
void Room::thinkProjectile(Entity& e) {
    Entity* o = get(e.owner);
    if (e.boom && o && e.age > 16 && e.age < 75) e.vel += (o->pos - e.pos).norm() * 0.55f;
    if (e.sub < 0) return;
    const SubDef& s = SUBS[size_t(e.sub)];
    bool fire = true;
    if (s.aim) {
        const bool healer = !s.guns.empty() && s.guns[0].heal;  // the Surgeon's blocks look for hurt friends instead
        if ((tickNo + int(e.id)) % 6 == 0)
            e.tgt = healer ? pickAlly(e, e.pos, 470, 0, PI) : pickTarget(e.pos, e.team, 470, 0, PI, false);
        Entity* tg = get(e.tgt);
        if (tg && (healer ? (tg->team == e.team && tg->hp > 0 && tg->hp < tg->maxHp) : targetable(*tg, e.team))) {
            float want = (tg->pos + tg->vel * 8 - e.pos).angle();
            e.angle = wrapAngle(e.angle + clampf(wrapAngle(want - e.angle), -0.25f, 0.25f));
            fire = std::fabs(wrapAngle(want - e.angle)) < 0.3f;
        } else {
            fire = false;
        }
    } else if (s.spin == 0 && e.vel.len2() > 0.01f) {
        e.angle = e.vel.angle();
    }
    if (s.accel > 0) e.vel += fromAngle(e.angle) * s.accel;
    if (!o || !o->tank || e.age < s.delay) return;
    float base = BASE_RELOAD * std::pow(0.93f, float(o->tank->skills[S_RELOAD]));
    for (size_t i = 0; i < s.guns.size() && i < 4; i++) {
        const GunDef& g = s.guns[i];
        if (e.subT[i] > 0) e.subT[i] -= 1;
        if (fire && e.subT[i] <= 0) {
            launch(e, o, g, e.pos, e.angle, e.size);
            e.subT[i] += base * g.reload;
        } else if (!fire && e.subT[i] < 0) {
            e.subT[i] = 0;
        }
    }
}

// Guns carried by minions (index 0) and by armed drones (index 1).
static const GunDef& droneArm(int arm) {
    static GunDef guns[2];
    static bool built = false;
    if (!built) {
        built = true;
        guns[0].len = 1.7f; guns[0].w = 0.8f; guns[0].reload = 1.3f; guns[0].speed = 1.0f; guns[0].dmg = 0.45f; guns[0].hp = 0.7f;
        guns[1].len = 0.4f; guns[1].w = 0.6f; guns[1].reload = 1.7f; guns[1].speed = 1.0f; guns[1].dmg = 0.3f; guns[1].hp = 0.5f;
    }
    return guns[arm == 1 ? 0 : 1];
}

void Room::thinkDrone(Entity& e) {
    Entity* o = get(e.owner);
    if (!o || !o->tank) {
        e.hp = 0;
        e.lastHitBy = 0;
        return;
    }
    const TankData& t = *o->tank;
    Vec2 aimPt = o->pos + t.aim;
    Vec2 target, shootAt;
    bool have = false, slowNear = false, shoot = false;
    if (!e.indep && t.fire) {  // attract to the cursor
        target = shootAt = aimPt;
        have = shoot = true;
    } else if (!e.indep && t.alt) {  // repel from the cursor
        target = e.pos + (e.pos - aimPt).norm() * 300;
        have = true;
    } else {
        if (t.manual) {
            e.tgt = 0;  // override: drones wait for orders instead of picking fights
        } else if ((tickNo + int(e.id)) % 6 == 0) {
            Vec2 centre = e.counted ? o->pos : e.pos;
            float bestD = 1e12f;
            e.tgt = 0;
            query(centre, e.counted ? 420.f : 350.f, [&](Entity& x) {
                if (x.kind != K_TANK && x.kind != K_FOOD) return;
                if (!targetable(x, e.team) || !los(e.pos, x.pos)) return;
                float d = (x.pos - e.pos).len2();
                if (d < bestD) { bestD = d; e.tgt = x.id; }
            });
        }
        Entity* g = get(e.tgt);
        if (g && g->team != e.team) {
            target = shootAt = g->pos;
            have = shoot = true;
        } else if (e.counted) {  // idle: orbit the owner
            float a = tickNo * 0.03f + float(e.id % 16) * (TAU / 16);
            target = o->pos + fromAngle(a, o->size * 3.2f + 30);
            have = slowNear = true;
        }
    }
    if (have) {
        Vec2 to = target - e.pos;
        float dist = to.len();
        float sp = e.maxSpeed * ((slowNear && dist < 60) ? dist / 60 : 1);
        Vec2 want = to.norm() * sp;
        if (e.arm == 1 && !slowNear && dist < 200) {  // minions keep their distance and circle
            Vec2 n = to.norm();
            want = (Vec2{-n.y, n.x} * 0.8f - n * 0.3f) * sp;
        }
        e.vel += (want - e.vel) * 0.14f;
    }
    if (e.vel.len2() > 0.01f) e.angle = e.vel.angle();
    if (e.arm) {
        if (e.gunT > 0) e.gunT -= 1;
        if (shoot) {
            Vec2 to = shootAt - e.pos;
            float a = to.angle();
            if (e.arm == 1) e.angle = a;
            if (e.gunT <= 0 && to.len2() < 650 * 650 && to.len2() > 4) {
                const GunDef& g = droneArm(e.arm);
                launch(e, o, g, e.pos, a, e.size);
                e.gunT = BASE_RELOAD * std::pow(0.93f, float(t.skills[S_RELOAD])) * g.reload;
            }
        }
    }
}

void Room::collide() {
    for (int cy = 0; cy < grows_; cy++)
        for (int cx = 0; cx < gcols_; cx++) {
            std::vector<Entity*>& v = grid_[size_t(cy) * gcols_ + cx];
            size_t n = v.size();
            for (size_t i = 0; i + 1 < n; i++)
                for (size_t j = i + 1; j < n; j++) {
                    Entity* a = v[i];
                    Entity* b = v[j];
                    if (std::max(a->gx0, b->gx0) != cx || std::max(a->gy0, b->gy0) != cy) continue;
                    collidePair(*a, *b);
                }
        }
}

void Room::collidePair(Entity& a, Entity& b) {
    if (a.dead || b.dead || a.hp <= 0 || b.hp <= 0) return;
    bool same = a.team == b.team;
    Vec2 d = b.pos - a.pos;
    float r = a.size + b.size;
    float d2 = d.len2();
    if (same && (a.heal || b.heal)) {  // healing bullets repair hurt team-mates (never the tank that fired them)
        Entity& shot = a.heal ? a : b;
        Entity& friendTank = a.heal ? b : a;
        if (d2 < r * r && friendTank.kind == K_TANK && friendTank.id != shot.owner && friendTank.hp < friendTank.maxHp) {
            float before = friendTank.hp;
            friendTank.hp = std::min(friendTank.maxHp, friendTank.hp + shot.damage * 5);
            shot.hp -= shot.maxHp * 0.34f;  // used up after three ticks of contact
            shot.lastHitBy = 0;
            // Healers earn their score by healing. Only another player's damage counts, not a base
            // protector's or a sanctuary's upkeep.
            if (Entity* o = get(shot.owner))
                if (o->tank && friendTank.tank->role == R_PLAYER) addScore(*o, (friendTank.hp - before) * 0.5);
        }
        return;
    }
    // Assembler: two of the same tank's blocks that touch become one bigger, tougher block.
    if (same && a.merge && b.merge && a.kind == K_TRAP && b.kind == K_TRAP && a.owner && a.owner == b.owner && d2 < r * r) {
        Entity& big = a.size >= b.size ? a : b;
        Entity& small = &big == &a ? b : a;
        Entity* o = get(a.owner);
        float cap = o ? o->size * 2.2f : 60;
        if (big.size < cap) {
            float wa = big.size * big.size, wb = small.size * small.size;
            big.pos = (big.pos * wa + small.pos * wb) * (1 / (wa + wb));
            big.vel = (big.vel * wa + small.vel * wb) * (1 / (wa + wb));
            big.size = std::min(cap, std::sqrt(wa + wb));
            big.maxHp += small.maxHp * 0.9f;
            big.hp += small.hp * 0.9f;
            big.damage += small.damage * 0.15f;
            big.projDamage = big.damage;
            big.life = std::max(big.life, small.life);
            big.mass = big.size * big.size * 2;
            small.hp = 0;
            small.lastHitBy = 0;
            statMerges++;
            return;
        }
    }
    // Friendly things only shove their own kind (tanks, drones, traps, food); bullets ignore friends entirely.
    if (same && (a.kind != b.kind || a.kind == K_BULLET)) return;
    if (d2 >= r * r) return;

    // Battle Royale: until the battle begins the tanks (and what they fire) cannot hurt each other.
    const bool truce = mode == M_BR && !brBegun_ && a.team >= 0 && b.team >= 0;
    if (!same && !truce) {
        bool inv = (a.tank && a.tank->invuln > 0) || (b.tank && b.tank->invuln > 0);
        if (!inv) {
            bool aBody = a.kind == K_TANK || a.kind == K_FOOD;
            bool bBody = b.kind == K_TANK || b.kind == K_FOOD;
            float da = (aBody ? b.damage : b.projDamage) * a.resist;
            float db = (bBody ? a.damage : a.projDamage) * b.resist;
            // If one side dies part-way through the tick, the other only takes that fraction of the damage.
            float ea = a.hp + a.shield, eb = b.hp + b.shield;
            float f = 1;
            if (da > ea) f = ea / da;
            if (db > eb) f = std::min(f, eb / db);
            hurt(a, da * f, &b, nullptr);
            hurt(b, db * f, &a, nullptr);
            // Knock-back projectiles (base protectors): one big shove away from whoever fired, then gone.
            for (int k = 0; k < 2; k++) {
                Entity& shot = k ? b : a;
                Entity& hit = k ? a : b;
                if (shot.knock <= 0 || hit.kind != K_TANK) continue;
                Entity* o = get(shot.owner);
                Vec2 away = ((o ? hit.pos - o->pos : hit.pos - shot.pos)).norm();
                hit.vel += away * (shot.knock * hit.pushf);
                shot.hp = 0;
                shot.lastHitBy = 0;
            }
        }
    }
    float dist = std::sqrt(d2);
    Vec2 n = dist > 0.01f ? d * (1 / dist) : fromAngle(rng_.range(0, TAU));
    float k = std::min(r - dist, 14.f) * 0.2f;
    float ms = a.mass + b.mass;
    a.vel -= n * (k * (b.mass / ms) * a.pushf);
    b.vel += n * (k * (a.mass / ms) * b.pushf);
}

void Room::hurt(Entity& e, float amount, Entity* src, const char* cause) {
    if (amount <= 0) return;
    if (e.tank) {
        if (e.tank->invuln > 0 || e.tank->invincible) return;
        // Only the players can knock down a fallen sanctuary (that is how they restore it).
        if (e.tank->role == R_SANCT && e.team != 0 && !(src && src->team == 0)) return;
        e.tank->lastHurt = tickNo;
        if (e.tank->alpha < 1) e.tank->alpha = std::min(1.f, e.tank->alpha + 0.4f);  // getting hit gives you away
    }
    e.hitTick = tickNo;
    if (src) {
        uint32_t by = src->kind == K_TANK ? src->id : src->owner;
        if (by) {
            e.lastHitBy = by;
            e.lastHitTeam = src->team;
            e.lastHitTick = tickNo;
            // Remember who did how much: score is shared by damage dealt (tanks and polygons only).
            if (e.kind == K_TANK || e.kind == K_FOOD) {
                float dealt = std::min(amount, e.hp + e.shield);
                Entity::Hit* slot = nullptr;
                for (Entity::Hit& h : e.hits)
                    if (h.by == by) { slot = &h; break; }
                if (!slot) {
                    if (e.hits.size() < 12) {
                        e.hits.push_back({by, 0, tickNo});
                        slot = &e.hits.back();
                    } else {  // full: the entry nobody has added to for longest makes room
                        slot = &e.hits[0];
                        for (Entity::Hit& h : e.hits)
                            if (h.tick < slot->tick) slot = &h;
                        *slot = {by, 0, tickNo};
                    }
                }
                slot->dmg += dealt;
                slot->tick = tickNo;
            }
        } else if (src->kind == K_FOOD) {
            e.cause = foodName(src->shape, src->tier, src->mut, src->crasher);
        }
    } else if (cause) {
        e.cause = cause;
    }
    if (e.shield > 0) {
        float s = std::min(e.shield, amount);
        e.shield -= s;
        amount -= s;
    }
    e.hp -= amount;
    if (e.hp < 0.001f) e.hp = 0;
}

void Room::integrate() {
    for (Entity* e : ents) {
        if (e->dead || e->hp <= 0) continue;
        e->pos += e->vel;
        e->vel *= e->damp;
        e->angle += e->spin;
        if (e->wave != 0) e->pos += e->wperp * (e->wave * std::cos(e->age * WAVE_FREQ));
        e->age++;
        if ((e->life > 0 && --e->life == 0) || (e->leash > 0 && (e->pos - e->from).len2() > e->leash * e->leash)) {
            e->hp = 0;  // ran out of time, or left the view of whatever fired it
            e->lastHitBy = 0;
            continue;
        }
        if (e->kind != K_BULLET) {
            if (e->pos.x < 0) { e->pos.x = 0; e->vel.x = std::max(e->vel.x, 0.f); }
            if (e->pos.y < 0) { e->pos.y = 0; e->vel.y = std::max(e->vel.y, 0.f); }
            if (e->pos.x > W) { e->pos.x = W; e->vel.x = std::min(e->vel.x, 0.f); }
            if (e->pos.y > H) { e->pos.y = H; e->vel.y = std::min(e->vel.y, 0.f); }
        } else if (e->pos.x < -120 || e->pos.y < -120 || e->pos.x > W + 120 || e->pos.y > H + 120) {
            e->hp = 0;
            e->lastHitBy = 0;
            continue;
        }
        // Arena closers (and what they fire) pass through maze walls and ignore bases: nowhere is safe.
        if (e->ghost) {
            // Battle Royale: the closers work outside the safe circle, and their shots stop at its edge.
            if (mode == M_BR && !closing() && e->kind == K_BULLET && inZone(e->pos, -10)) { e->hp = 0; e->lastHitBy = 0; }
            continue;
        }
        if (cols_ && resolveWalls(*e) && e->kind == K_BULLET) {
            e->hp = 0;
            e->lastHitBy = 0;
            continue;
        }
        if (!bases_.empty() && e->kind != K_FOOD) {
            for (const Base& b : bases_) {
                if (b.team == e->team) continue;
                if (e->pos.x < b.x || e->pos.y < b.y || e->pos.x > b.x + b.w || e->pos.y > b.y + b.h) continue;
                if (e->kind == K_TANK) {  // bases burn intruders and shove them back out
                    if (e->tank->role == R_PLAYER) hurt(*e, e->maxHp * 0.03f + 1, nullptr, "the enemy base");
                    e->vel += (e->pos - Vec2{b.x + b.w / 2, b.y + b.h / 2}).norm() * 1.2f;
                } else {
                    e->hp = 0;
                    e->lastHitBy = 0;
                }
            }
        }
    }
}

// Push a circle out of any wall cells it overlaps. Returns true if it touched a wall.
bool Room::resolveWalls(Entity& e) {
    bool touched = false;
    int x0 = int(std::floor((e.pos.x - e.size) / cell_)), x1 = int(std::floor((e.pos.x + e.size) / cell_));
    int y0 = int(std::floor((e.pos.y - e.size) / cell_)), y1 = int(std::floor((e.pos.y + e.size) / cell_));
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++) {
            if (!wallAt(cx, cy)) continue;
            float rx = cx * cell_, ry = cy * cell_;
            float nx = clampf(e.pos.x, rx, rx + cell_), ny = clampf(e.pos.y, ry, ry + cell_);
            float dx = e.pos.x - nx, dy = e.pos.y - ny;
            float d2 = dx * dx + dy * dy;
            if (d2 >= e.size * e.size) continue;
            touched = true;
            if (d2 > 1e-4f) {
                float d = std::sqrt(d2);
                Vec2 n{dx / d, dy / d};
                e.pos += n * (e.size - d);
                float vn = e.vel.x * n.x + e.vel.y * n.y;
                if (vn < 0) e.vel -= n * vn;
            } else {
                // Centre is inside the wall: leave through the nearest side that opens onto free space.
                float best = 1e9f;
                Vec2 out{0, 0};
                const float dist[4] = {e.pos.x - rx, rx + cell_ - e.pos.x, e.pos.y - ry, ry + cell_ - e.pos.y};
                const Vec2 dir[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                const int ox[4] = {-1, 1, 0, 0}, oy[4] = {0, 0, -1, 1};
                for (int k = 0; k < 4; k++) {
                    float cost = dist[k] + (wallAt(cx + ox[k], cy + oy[k]) ? 1000.f : 0.f);
                    if (cost < best) { best = cost; out = dir[k] * (dist[k] + e.size); }
                }
                e.pos += out;
                e.vel = {0, 0};
            }
        }
    return touched;
}

// Is the straight line between two points free of maze walls?
bool Room::los(Vec2 a, Vec2 b) const {
    if (!cols_) return true;
    Vec2 d = b - a;
    int n = int(d.len() / (cell_ * 0.2f)) + 1;
    for (int i = 0; i <= n; i++) {
        Vec2 p = a + d * (float(i) / n);
        if (wallAt(int(std::floor(p.x / cell_)), int(std::floor(p.y / cell_)))) return false;
    }
    return true;
}

void Room::reap() {
    for (size_t i = 0; i < ents.size(); i++) {
        Entity* e = ents[i];
        if (e->dead || e->hp > 0) continue;
        switch (e->kind) {
            case K_TANK:
                onTankDeath(*e);
                break;
            case K_FOOD: {
                Entity* k = get(e->lastHitBy);
                // The polygon's score goes to everyone who damaged it, in proportion.
                for (const Share& sh : damageShares(*e, 1 << 30)) addScore(*sh.tank, e->xp * double(sh.frac));
                if (k && k->tank) {
                    k->tank->polys++;  // the one who finished it counts it as destroyed
                    if (e->mut && k->tank->role == R_PLAYER) {  // a rare find: tell the finder, and everyone if it is rarer than shiny
                        std::string what = foodName(e->shape, e->tier, e->mut, e->crasher);
                        what = (std::string("AEIOU").find(what[0]) != std::string::npos ? "an " : "a ") + what;  // "an Octahedron"
                        if (e->mut > MU_SHINY) sendMsg(nullptr, k->tank->name + " destroyed " + what + "!");
                        else if (k->tank->client) sendMsg(k->tank->client, "You destroyed " + what + "!");
                    }
                    // Necromancer-style classes turn the shape into a drone instead of just destroying it.
                    const TankDef& kd = TANKS[k->tank->def];
                    if (!e->crasher && e->tier == T_NORMAL && ((kd.necro >> e->shape) & 1) && k->tank->drones < kd.maxDrones && !kd.guns.empty()) {
                        Entity* dr = launch(*k, k, kd.guns[0], e->pos, e->angle, 1);
                        dr->pos = e->pos;
                        dr->vel = {0, 0};
                        dr->size = e->size * 0.95f;
                        dr->mass = dr->size * dr->size;
                        dr->shape = FOODS[e->shape].sides;
                        if (e->shape == F_TRIANGLE) { dr->hp = dr->maxHp = dr->hp * 1.6f; dr->damage *= 1.3f; }
                    }
                }
                (e->nest ? nestCount_ : foodCount_)--;
                e->dead = true;
                break;
            }
            case K_DRONE:
                if (e->counted) {
                    Entity* o = get(e->owner);
                    if (o && o->tank && o->tank->drones > 0) o->tank->drones--;
                }
                e->dead = true;
                break;
            default:
                e->dead = true;
        }
    }
}

void Room::onTankDeath(Entity& e) {
    TankData& t = *e.tank;
    Entity* k = (tickNo - e.lastHitTick < 300) ? get(e.lastHitBy) : nullptr;
    if (k == &e) k = nullptr;

    if (t.dom >= 0) {
        if (Client* driver = t.client) {
            releaseDominator(*driver);
            sendMsg(driver, "Your dominator was destroyed. You are back in your own tank.");
        }
        if (mode == M_SIEGE) {  // Siege has its own rules: a dominator the attackers destroy is theirs for good
            siegeCapture(e, k);
            return;
        }
        // A held dominator falls back to contested; a contested one goes to whoever finished it.
        bool recent = tickNo - e.lastHitTick < 300;
        int newTeam = (e.team >= 0 || !recent || e.lastHitTeam < 0) ? TEAM_FOOD : e.lastHitTeam;  // neutral = Team Polygon
        if (e.team >= 0) sendMsg(nullptr, std::string("A ") + teamName(e.team) + " dominator is being contested!");
        else if (newTeam >= 0) sendMsg(nullptr, std::string("A dominator was captured by ") + teamName(newTeam) + "!");
        e.team = newTeam;
        e.color = newTeam >= 0 ? teamColor(newTeam) : uint8_t(C_NEUTRAL);
        e.hp = e.maxHp;
        e.lastHitBy = 0;
        e.lastHitTick = -100000;
        e.hits.clear();
        t.ai.target = 0;
        if (k && k->tank) addScore(*k, 600);
        statCaptures++;
        if (newTeam >= 0 && closeAt_ < 0 && closeTick_ < 0) {
            bool all = true;
            for (const Dom& d : doms_) {
                Entity* de = get(d.ent);
                if (!de || de->team != newTeam) all = false;
            }
            if (all) {  // victory: announce it, then the arena closers end the round
                sendBanner(std::string(teamName(newTeam)) + " HAS WON THE GAME!", "They hold all four dominators. The arena is closing.",
                           teamColor(newTeam));
                sendMsg(nullptr, std::string(teamName(newTeam)) + " holds every dominator and wins the game!");
                closeAt_ = tickNo + 5 * TICK_RATE;
                statWins++;
            }
        }
        return;
    }
    if (t.role == R_MOTHER) {  // Mothership mode: the team that loses its Mothership loses the game
        if (Client* driver = t.client) {
            releaseDominator(*driver);
            sendMsg(driver, "Your Mothership was destroyed. You are back in your own tank.");
        }
        for (Entity* o : ents)
            if (!o->dead && o->kind == K_DRONE && o->owner == e.id) { o->hp = 0; o->dead = true; }
        for (const Share& sh : damageShares(e, 600 * TICK_RATE)) addScore(*sh.tank, e.xp * double(sh.frac));
        if (k && k->tank && k->tank->role == R_PLAYER) {
            k->tank->bossKills++;
            sendMsg(nullptr, k->tank->name + " destroyed the " + teamName(e.team) + " Mothership!");
        }
        const int winner = e.team == 0 ? 1 : 0;
        win(std::string(teamName(winner)) + " HAS WON THE GAME!", std::string("The ") + teamName(e.team) + " Mothership was destroyed. The arena is closing.",
            teamColor(winner));
        e.dead = true;
        return;
    }
    if (t.role == R_BOSS) {
        for (Entity* o : ents)
            if (!o->dead && o->kind == K_DRONE && o->owner == e.id) { o->hp = 0; o->dead = true; }
        const std::string label = bossLabel(e);
        Entity* killer = (k && k->tank && k->tank->role == R_PLAYER) ? k : nullptr;
        // The reward is shared by damage dealt. Whoever did a real part of the work without landing
        // the last hit gets an assist.
        for (const Share& sh : damageShares(e, 180 * TICK_RATE)) {
            addScore(*sh.tank, e.xp * double(sh.frac));
            TankData& st = *sh.tank->tank;
            if (sh.tank == killer || st.role != R_PLAYER || sh.frac < 0.05f) continue;
            st.assists++;
            statAssists++;
            if (st.client) sendMsg(st.client, "You assisted in killing " + label + ".");
        }
        if (killer) {
            killer->tank->bossKills++;
            if (killer->tank->client) sendMsg(killer->tank->client, "You killed " + label + ".");
            sendMsg(nullptr, killer->tank->name + " has defeated " + label + "!");
        } else {
            sendMsg(nullptr, label + " has been destroyed.");
        }
        statBossKills++;
        bossEnts_.erase(std::remove(bossEnts_.begin(), bossEnts_.end(), e.id), bossEnts_.end());
        if (bossEnts_.empty()) {  // the last of the visitors is gone: the next visit can be scheduled
            bossWarned_ = false;
            nextBossTick_ = tickNo + int(bossEvery * rng_.range(0.8f, 1.2f));
        }
        e.dead = true;
        return;
    }
    if (t.role == R_SANCT) {
        e.lastHitBy = 0;
        e.lastHitTick = -100000;
        t.ai.target = 0;
        t.fire = false;
        if (e.team == 0) {  // destroyed: it turns yellow, joins Team Polygon and must be won back
            e.team = TEAM_FOOD;
            e.color = C_NEUTRAL;
            e.hits.clear();
            e.maxHp = 6000;  // winning it back takes as much work as it took to destroy
            e.hp = e.maxHp;
            int left = sanctuariesAlive();
            sendMsg(nullptr, left > 0 ? "A sanctuary has fallen! " + std::to_string(left) + " left. Destroy the yellow sanctuary to restore it."
                                      : "The last sanctuary has fallen!");
        } else {  // the players destroyed the yellow one: it is theirs again
            e.team = 0;
            e.color = teamColor(0);
            e.maxHp = 6000;
            e.hp = e.maxHp;
            if (k && k->tank && k->tank->role == R_PLAYER) {
                addScore(*k, 5000);
                sendMsg(nullptr, k->tank->name + " has restored a sanctuary!");
            } else {
                sendMsg(nullptr, "A sanctuary has been restored!");
            }
        }
        return;
    }
    if (t.role != R_PLAYER) {  // protectors and closers cannot die, but never leave a husk behind
        e.dead = true;
        return;
    }

    std::string killerName, killerTank, helpers;
    Entity* killer = (k && k->tank && k->tank->role == R_PLAYER) ? k : nullptr;
    // What this tank was worth is shared between the players who damaged it in the last half
    // minute, by damage dealt. The one who landed the last hit gets the kill, the others an assist.
    const double pool = std::max(20.0, std::min(t.score, 24000.0) * 0.5);
    const std::string victim = t.name + "'s " + TANKS[t.def].name;
    bool killerPaid = false;
    int nHelpers = 0;
    for (const Share& sh : damageShares(e, 30 * TICK_RATE)) {
        addScore(*sh.tank, pool * double(sh.frac));
        if (sh.tank == killer) { killerPaid = true; continue; }
        TankData& st = *sh.tank->tank;
        if (st.role != R_PLAYER || sh.frac < 0.1f) continue;
        st.assists++;
        statAssists++;
        if (st.client) sendMsg(st.client, "You assisted in killing " + victim + ".");
        if (nHelpers++ < 3) helpers += std::string(helpers.empty() ? "" : ",") + "\"" + jsonEscape(st.name) + "\"";
    }
    if (k && k->tank) {
        killerName = k->tank->role == R_BOSS ? bossLabel(*k) : k->tank->name;
        if (killer) {
            killerTank = TANKS[k->tank->def].name;
            if (!killerPaid) addScore(*k, 20);
            k->tank->kills++;
            statKills++;
            if (statClassKills.size() != TANKS.size()) { statClassKills.assign(TANKS.size(), 0); statClassDeaths.assign(TANKS.size(), 0); }
            statClassKills[size_t(k->tank->def)]++;   // for --sim: which classes win their fights
            statClassDeaths[size_t(t.def)]++;
            if (k->tank->client) sendMsg(k->tank->client, "You killed " + victim + ".");
        }
    } else if (e.cause) {
        killerName = e.cause;
        if (killerName.find("Crasher") != std::string::npos) statCrasherDeaths++;
    }
    statDeaths++;
    for (Entity* o : ents)
        if (!o->dead && o->kind == K_DRONE && o->owner == e.id) { o->hp = 0; o->dead = true; }

    // The bounty on the leader: whoever destroys the top tank is paid extra, shared by damage like the kill itself.
    std::string note;  // one more line for the death screen
    if (killer && e.id == leaderId_ && leaderBounty_ > 0 && bountyMode()) {
        for (const Share& sh : damageShares(e, 30 * TICK_RATE)) addScore(*sh.tank, leaderBounty_ * double(sh.frac));
        char amount[32];
        std::snprintf(amount, sizeof(amount), "%.0f", leaderBounty_);
        sendMsg(nullptr, killer->tank->name + " claimed the bounty on " + t.name + " (+" + amount + ")");
        note = std::string("You were the leader: there was a bounty of ") + amount + " on you.";
        statBounties++;
        leaderId_ = 0;
        leaderBounty_ = 0;
    }
    // Tag: the loser changes sides.
    if (mode == M_TAG && killer && killer->team != e.team && killer->team >= 0 && closeAt_ < 0) {
        tagConvert(e, *killer);
        note = std::string("You were tagged: you now play for ") + teamName(killer->team) + ".";
    }
    // Battle Royale: there is no way back in, so say how far the tank got.
    if (mode == M_BR && brBegun_) {
        int left = 0;
        for (Entity* o : ents)
            if (!o->dead && o != &e && o->hp > 0 && o->kind == K_TANK && o->tank->role == R_PLAYER) left++;
        note = "You placed " + std::to_string(left + 1) + " of " + std::to_string(std::max(brStarters_, left + 1)) + ". The next battle starts when this one is over.";
    }

    double keep = std::min(std::floor(t.score * 0.33), levelScore(30));
    if (t.client) {
        Client& c = *t.client;
        c.tankId = 0;
        c.keepScore = keep;
        c.spectate = k ? k->id : 0;
        char buf[240];
        std::snprintf(buf, sizeof(buf), "\",\"score\":%.0f,\"level\":%d,\"kills\":%d,\"assists\":%d,\"bosses\":%d,\"polys\":%d,\"time\":%d,\"helpers\":[",
                      t.score, t.level, t.kills, t.assists, t.bossKills, t.polys, (tickNo - t.spawnTick) / TICK_RATE);
        if (send)
            send(c, "{\"t\":\"dead\",\"killer\":\"" + jsonEscape(killerName) + "\",\"killerTank\":\"" +
                        jsonEscape(killerTank) + "\",\"tank\":\"" + jsonEscape(TANKS[t.def].name) + buf + helpers + "],\"note\":\"" +
                        jsonEscape(note) + "\"}", false);
    }
    if (t.slot >= 0 && t.slot < int(bots_.size())) {
        bots_[t.slot].ent = 0;
        bots_[t.slot].respawnAt = tickNo + 60 + rng_.irange(0, 90);
        bots_[t.slot].keep = keep;
    }
    e.dead = true;
}

void Room::manageFood() {
    for (int i = 0; i < 3 && foodCount_ < foodTarget_; i++) spawnFood(false);
    if (nestCount_ < nestTarget_ && tickNo % 20 == 0) spawnFood(true);
}

// Bots keep the arena lively when it is quiet and step aside as real players arrive:
// every player who has joined takes one bot's place (the bot simply isn't respawned).
void Room::manageBots() {
    if (closing()) return;  // nobody joins a closed arena
    if (rushMode() && allLostTick_ >= 0) return;  // Boss Rush: no sanctuary, no respawn
    if (mode == M_BR && brBegun_) return;         // Battle Royale: nobody comes back once the battle is on
    int playing = 0, active = 0;
    for (const Client* c : clients)
        if (c->played) playing++;
    for (const BotSlot& s : bots_)
        if (s.ent) active++;
    // (Sandbox bots were asked for by the players, so they do not step aside.)
    int allowed = mode == M_SANDBOX ? int(bots_.size()) : std::max(0, int(bots_.size()) - playing);
    for (size_t i = 0; i < bots_.size() && active < allowed; i++) {
        BotSlot& s = bots_[i];
        if (s.ent == 0 && tickNo >= s.respawnAt) {
            // Tag: a bot stays on the team that tagged it. Everywhere else teams are evened out at every respawn.
            bool keepTeam = mode == M_TAG && s.team >= 0 && s.team < teamCount_;
            if (keepTeam && tagSeen_[s.team] && teamMembers()[size_t(s.team)] == 0) keepTeam = false;  // its team is out
            if (!keepTeam) s.team = smallestTeam();
            Entity* e = spawnTank(s.name, s.team, s.keep, nullptr, int(i));
            s.ent = e->id;
            active++;
        }
    }
}

void Room::modeLogic() {
    // --- closing the arena: after a Domination win, or on the optional timer
    if (closeTick_ < 0) {
        if (closeAt_ >= 0 && tickNo >= closeAt_) closeArena("");
        // the timer only applies to modes that have no ending of their own
        else if (closeAt_ < 0 && closeEvery > 0 && openEnded() && tickNo - openedTick_ >= closeEvery) {
            if (mode == M_TAG) {  // Tag: when time runs out, the biggest team has won
                std::vector<int> n = teamMembers();
                int best = 0;
                for (int i = 1; i < teamCount_; i++)
                    if (n[size_t(i)] > n[size_t(best)]) best = i;
                win(std::string(teamName(best)) + " HAS WON THE GAME!", "Time is up, and they are the biggest team. The arena is closing.", teamColor(best));
            } else {
                closeArena("Time is up. A new round starts once the arena is clear.");
            }
        }
    }
    if (closeTick_ >= 0) {
        if (reopenTick_ >= 0) {
            if (tickNo >= reopenTick_) resetArena();
            return;
        }
        int alive = 0;
        for (Entity* e : ents)
            if (!e->dead && e->kind == K_TANK && e->tank->role == R_PLAYER) alive++;
        if (alive == 0 && tickNo - closeTick_ > 3 * TICK_RATE) {
            reopenTick_ = tickNo + 4 * TICK_RATE;
        } else if (tickNo - closeTick_ > 40 * TICK_RATE) {  // the closers were too slow: finish the job
            for (Entity* e : ents)
                if (!e->dead && e->kind == K_TANK && e->tank->role == R_PLAYER) {
                    e->tank->invuln = 0;
                    e->hp = 0;
                    e->lastHitBy = 0;
                    e->lastHitTick = -100000;
                    e->cause = "the arena closing";
                }
        }
        return;
    }

    if (rushMode()) {  // bosses come in waves here, not one at a time
        rushLogic();
        return;
    }
    if (tickNo % TICK_RATE == 0) bountyLogic();
    switch (mode) {
        case M_TAG: tagLogic(); break;
        case M_MOTHER: motherLogic(); break;
        case M_KOTH: kothLogic(); break;
        case M_SIEGE: siegeLogic(); break;
        case M_CTF: ctfLogic(); break;
        case M_BR: brLogic(); return;  // no boss visits: the circle is trouble enough
        case M_SANDBOX:                // bosses only come when a player spawns one
            bossEnts_.erase(std::remove_if(bossEnts_.begin(), bossEnts_.end(), [&](uint32_t id) { return get(id) == nullptr; }), bossEnts_.end());
            return;
        default: break;
    }

    // --- bosses drop in every so often: usually one, sometimes two or three together
    if (bossEvery > 0) {
        if (nextBossTick_ < 0) nextBossTick_ = tickNo + bossEvery / 2;
        bossEnts_.erase(std::remove_if(bossEnts_.begin(), bossEnts_.end(), [&](uint32_t id) { return get(id) == nullptr; }), bossEnts_.end());
        if (bossEnts_.empty()) {
            if (!bossWarned_ && tickNo >= nextBossTick_ - 8 * TICK_RATE) {
                bossWarned_ = true;
                // A crowded arena can take more than one visitor at a time.
                int tanks = 0;
                for (Entity* e : ents)
                    if (!e->dead && e->kind == K_TANK && e->tank->role == R_PLAYER) tanks++;
                float u = rng_.uni();
                nextBossCount_ = tanks >= 8 ? (u < 0.1f ? 3 : u < 0.35f ? 2 : 1) : (tanks >= 4 && u < 0.15f) ? 2 : 1;
                sendMsg(nullptr, nextBossCount_ > 1 ? "Several big things are approaching the arena..." : "Something big is approaching the arena...");
            }
            if (tickNo >= nextBossTick_) spawnBoss();
        } else if (tickNo - bossSpawnTick_ > 8 * 60 * TICK_RATE) {  // nobody could beat them: they wander off
            std::string who;
            for (size_t i = 0; i < bossEnts_.size(); i++) {
                Entity* boss = get(bossEnts_[i]);
                who += std::string(i == 0 ? "" : i + 1 == bossEnts_.size() ? " and " : ", ") + bossLabel(*boss);
                boss->dead = true;
            }
            sendMsg(nullptr, who + (bossEnts_.size() > 1 ? " have left the arena." : " has left the arena."));
            bossEnts_.clear();
            bossWarned_ = false;
            nextBossTick_ = tickNo + bossEvery;
        }
    }
}

namespace {
// Size, health, speed and reward of every boss (its weapons are its class in defs.cpp). The
// reward is shared between the players who damaged it. `named` bosses get a name of their own.
struct BossKind { const char* name; uint8_t color; float size, hp, accel; int reward; bool named; float body; };
const BossKind BOSS_KINDS[] = {
    // --- visitors to every arena
    {"Elite Destroyer", C_ELITE, 56, 5200, 0.2f, 30000, true, 7},   {"Elite Gunner", C_ELITE, 54, 4600, 0.2f, 30000, true, 7},
    {"Elite Sprayer", C_ELITE, 55, 4800, 0.2f, 30000, true, 7},     {"Elite Battleship", C_ELITE, 56, 5000, 0.2f, 30000, true, 7},
    {"Elite Spawner", C_ELITE, 56, 5000, 0.2f, 30000, true, 7},     {"Elite Trap Guard", C_ELITE, 55, 5200, 0.2f, 30000, true, 7},
    {"Elite Spinner", C_ELITE, 54, 4600, 0.22f, 30000, true, 7},    {"Elite Skimmer", C_TRIANGLE, 58, 6000, 0.2f, 36000, true, 7},
    {"Rogue Palisade", C_ROGUE, 70, 8000, 0.17f, 45000, true, 7},   {"Summoner", C_SQUARE, 54, 5000, 0.2f, 32000, true, 7},
    {"Nest Keeper", C_PENTAGON, 66, 6500, 0.2f, 38000, true, 7},
    // --- Boss Rush only
    {"Sorcerer", C_EGG, 50, 4200, 0.22f, 30000, true, 7},           {"Enchantress", C_TRIANGLE, 54, 5400, 0.2f, 34000, true, 7},
    {"Exorcistor", C_PENTAGON, 60, 6800, 0.18f, 40000, true, 7},    {"Nest Warden", C_PENTAGON, 70, 8500, 0.18f, 46000, true, 7},
    {"Nest Guardian", C_PENTAGON, 74, 10500, 0.17f, 52000, true, 7}, {"Rogue Armada", C_ROGUE, 76, 11000, 0.16f, 55000, true, 7},
    {"Ares", C_PURPLE, 84, 17000, 0.11f, 75000, false, 8},          {"Gersemi", C_GREEN, 84, 11000, 0.3f, 70000, false, 14},
    {"Ezekiel", C_TRIANGLE, 84, 14000, 0.14f, 70000, false, 8},     {"Selene", C_SQUARE, 84, 13000, 0.14f, 70000, false, 8},
    {"Eris", C_ELITE, 84, 12500, 0.15f, 70000, false, 8},
    {"Paladin", C_PENTAGON, 96, 20000, 0.13f, 100000, false, 9},    {"Freyja", C_SHINY, 96, 24000, 0.13f, 100000, false, 9},
    {"Zaphkiel", C_TRIANGLE, 96, 28000, 0.13f, 100000, false, 9},   {"Theia", C_SQUARE, 96, 26000, 0.13f, 100000, false, 9},
    {"Nyx", C_ELITE, 96, 27000, 0.13f, 100000, false, 9},
    {"Julius", C_ROGUE, 96, 28000, 0.13f, 130000, false, 9},        {"Genghis", C_ROGUE, 96, 28000, 0.13f, 130000, false, 9},
    {"Napoleon", C_ROGUE, 96, 28000, 0.13f, 130000, false, 9},
    {"Legionary Crasher", C_ELITE, 110, 30000, 0.12f, 150000, false, 10},
    {"Kronos", C_FINAL, 128, 70000, 0.11f, 250000, false, 10},      {"Ragnarok", C_LEGENDARY, 136, 80000, 0.1f, 350000, false, 10},
    {"Bob", C_RED, 60, 9000, 0.2f, 50000, false, 7}};
const int BOSS_REGULAR = 11;  // the first eleven visit every arena

const BossKind& bossKind(const char* name) {
    for (const BossKind& k : BOSS_KINDS)
        if (std::string(k.name) == name) return k;
    std::fprintf(stderr, "room: unknown boss '%s'\n", name);
    return BOSS_KINDS[0];
}

const char* const ELITES[] = {"Elite Destroyer", "Elite Gunner", "Elite Sprayer", "Elite Battleship",
                              "Elite Spawner", "Elite Trap Guard", "Elite Spinner"};
const char* const CELESTIALS[] = {"Paladin", "Freyja", "Zaphkiel", "Theia", "Nyx"};
const char* const TERRESTRIALS[] = {"Ares", "Gersemi", "Ezekiel", "Selene", "Eris"};
const char* const ROGUE_CELESTIALS[] = {"Julius", "Genghis", "Napoleon"};

// Names for the bosses that have no name of their own (elites, nesters, mysticals, rogues).
const char* const BOSS_NAMES[] = {
    "Vexa", "Torrin", "Oberon", "Maelis", "Karn", "Isolde", "Brakka", "Zuri", "Halvard", "Nyssa", "Corvin", "Thessaly",
    "Garrick", "Orla", "Dravik", "Seraphine", "Ulric", "Petra", "Mordan", "Lysa", "Fenwick", "Astrid", "Borin", "Calla",
    "Dunstan", "Elara", "Hesper", "Ivo", "Junia", "Lorcan", "Mirela", "Nadir", "Odalys", "Phaedra", "Roan", "Sunniva"};
const int BOSS_NAME_COUNT = int(sizeof(BOSS_NAMES) / sizeof(BOSS_NAMES[0]));
}  // namespace

// "Vexa the Elite Gunner" for a boss with a name of its own, otherwise just "Paladin".
std::string Room::bossLabel(const Entity& e) const {
    const std::string& cls = TANKS[e.tank->def].name;
    return e.tank->name == cls ? cls : e.tank->name + " the " + cls;
}

Entity* Room::makeBoss(const char* kind, Vec2 pos, float hpScale) {
    const BossKind& k = bossKind(kind);
    Entity* e = spawnNpc(k.name, R_BOSS, TEAM_BOSS, k.color, pos, k.size, k.hp * hpScale);
    e->damage = k.body;
    e->projDamage = 5;
    e->mass = k.size * k.size * 5;
    e->pushf = 0.2f;
    e->damp = 0.9f;
    e->xp = int(k.reward * clampf(hpScale, 0.3f, 1.5f));  // shared out by damage when it dies
    e->tank->accel = k.accel;
    e->tank->ai.offset = rng_.irange(0, 63);
    if (k.named) {  // a name no other boss in the arena is using
        for (int tries = 0; tries < 8; tries++) {
            const char* n = BOSS_NAMES[rng_.irange(0, BOSS_NAME_COUNT - 1)];
            bool taken = false;
            for (Entity* o : ents)
                if (!o->dead && o->kind == K_TANK && o->tank->role == R_BOSS && o->tank->name == n) taken = true;
            for (Entity* o : pending_)
                if (o->kind == K_TANK && o != e && o->tank->name == n) taken = true;
            e->tank->name = n;
            if (!taken) break;
        }
    }
    statBosses++;
    return e;
}

void Room::spawnBoss() {
    // Arrive somewhere open, away from bases and dominators and as far from the tanks as possible.
    Vec2 best{W / 2, H / 2};
    float bestD = -1;
    for (int i = 0; i < 24; i++) {
        Vec2 p = randomOpenPoint(110);
        bool bad = false;
        for (const Base& b : bases_)
            if (p.x > b.x - 700 && p.x < b.x + b.w + 700 && p.y > b.y - 700 && p.y < b.y + b.h + 700) bad = true;
        for (const Dom& d : doms_)
            if ((d.pos - p).len2() < 500 * 500) bad = true;
        if (bad) continue;
        float d = 1e12f;
        for (Entity* e : ents)
            if (!e->dead && e->kind == K_TANK) d = std::min(d, (e->pos - p).len2());
        if (d > bestD) { bestD = d; best = p; }
    }
    int count = std::clamp(nextBossCount_, 1, 3);
    nextBossCount_ = 1;
    std::string who;
    for (int i = 0; i < count; i++) {
        if (bossBag_.empty()) {  // every boss turns up once before any of them comes back
            for (int k = 0; k < BOSS_REGULAR; k++) bossBag_.push_back(k);
            for (int k = BOSS_REGULAR - 1; k > 0; k--) std::swap(bossBag_[size_t(k)], bossBag_[size_t(rng_.irange(0, k))]);
        }
        const BossKind& kind = BOSS_KINDS[bossBag_.back()];
        bossBag_.pop_back();
        Vec2 p = best;
        for (int tries = 0; i > 0 && tries < 12; tries++) {  // companions arrive beside the first
            Vec2 q = best + fromAngle(rng_.range(0, TAU), rng_.range(260, 420));
            if (openSpot(q, kind.size + 20)) { p = q; break; }
        }
        Entity* e = makeBoss(kind.name, p, 1);
        bossEnts_.push_back(e->id);
        who += std::string(i == 0 ? "" : i + 1 == count ? " and " : ", ") + bossLabel(*e);
    }
    if (count > 1) statGroups++;
    bossSpawnTick_ = tickNo;
    sendMsg(nullptr, who + (count > 1 ? " have arrived! Destroy them for a huge reward." : " has arrived! Destroy it for a huge reward."));
}

// Boss Rush: 20 waves. Clearing a wave starts the next one; clearing the last wins the game.
// Losing every sanctuary loses it. Either way the arena closes and a new game begins.
void Room::rushLogic() {
    waveBosses_.erase(std::remove_if(waveBosses_.begin(), waveBosses_.end(), [&](uint32_t id) { return get(id) == nullptr; }),
                      waveBosses_.end());
    if (closeAt_ >= 0) return;  // the result has been announced
    // Losing: every sanctuary yellow for 30 seconds in a row. Nobody can respawn during that time,
    // so the tanks still alive have to win one back.
    if (sanctuariesAlive() == 0) {
        if (allLostTick_ < 0) {
            allLostTick_ = tickNo;
            sendBanner("ALL SANCTUARIES LOST", "Restore one within 30 seconds or the game is lost. Nobody can respawn until then.", C_NEUTRAL);
        } else if (tickNo - allLostTick_ >= 30 * TICK_RATE) {
            if (mode == M_ENDLESS)  // Endless has no victory: how far the players got is the result
                sendBanner("THE SANCTUARIES HAVE FALLEN", "You cleared " + std::to_string(std::max(0, wave_ - 1)) + " waves. Best on this server: " +
                                                              std::to_string(bestWave_) + ". The arena is closing.", C_NEUTRAL);
            else
                sendBanner("DEFEAT", "No sanctuary was restored in time (wave " + std::to_string(std::max(1, wave_)) + "). The arena is closing.", C_RED);
            closeAt_ = tickNo + 6 * TICK_RATE;
            return;
        }
    } else if (allLostTick_ >= 0) {
        allLostTick_ = -1;
        sendMsg(nullptr, "A sanctuary stands again. The fight goes on!");
    }
    if (!waveBosses_.empty()) return;
    if (mode == M_ENDLESS && wave_ > bestWave_) {  // a wave cleared: Endless keeps the server's record
        bestWave_ = wave_;
        if (wave_ > 1) sendMsg(nullptr, "New record for this server: " + std::to_string(wave_) + " waves cleared!");
    }
    if (mode == M_RUSH && wave_ >= RUSH_WAVES) {
        sendBanner("VICTORY!", "All " + std::to_string(RUSH_WAVES) + " waves are cleared. The arena is closing.", C_GREEN);
        closeAt_ = tickNo + 6 * TICK_RATE;
        statWins++;
        return;
    }
    if (waveNextTick_ < 0) {
        waveNextTick_ = tickNo + (wave_ == 0 ? 12 : 8) * TICK_RATE;
        if (wave_ > 0) sendMsg(nullptr, "Wave " + std::to_string(wave_) + " cleared! The next wave arrives in 8 seconds.");
    } else if (tickNo >= waveNextTick_) {
        waveNextTick_ = -1;
        wave_++;
        spawnWave();
    }
}

void Room::spawnWave() {
    int players = 0;
    for (Entity* e : ents)
        if (!e->dead && e->kind == K_TANK && e->tank->role == R_PLAYER) players++;
    // Endless Boss Rush plays the same twenty waves over and over. Every lap the bosses have twice
    // the health of the lap before and three more lesser bosses come along (eight at most).
    const int lap = mode == M_ENDLESS ? (wave_ - 1) / RUSH_WAVES : 0;
    const int w = mode == M_ENDLESS ? (wave_ - 1) % RUSH_WAVES + 1 : wave_;  // where we are in the twenty
    const float lapMul = std::pow(2.f, float(lap));
    // Bosses toughen up as the waves go on, and a crowded arena gets a little extra.
    float crowd = clampf(0.45f + 0.045f * players, 0.5f, 1.4f);
    float hpScale = (0.3f + 0.03f * w) * crowd * lapMul;
    int escorts = 1 + (w - 1) / 4 + (players >= 10 ? 1 : 0) + (players >= 20 ? 1 : 0) + 3 * lap;
    auto edge = [&]() -> Vec2 {
        switch (rng_.irange(0, 3)) {
            case 0: return {rng_.range(300, W - 300), 200};
            case 1: return {rng_.range(300, W - 300), H - 200};
            case 2: return {200, rng_.range(300, H - 300)};
            default: return {W - 200, rng_.range(300, H - 300)};
        }
    };
    if (w == 1 || rushCel_.empty()) {  // a new game (or a new lap): deal the celestials and terrestrials
        rushCel_.assign(std::begin(CELESTIALS), std::end(CELESTIALS));
        rushTerr_.assign(std::begin(TERRESTRIALS), std::end(TERRESTRIALS));
        for (size_t k = rushCel_.size() - 1; k > 0; k--) std::swap(rushCel_[k], rushCel_[size_t(rng_.irange(0, int(k)))]);
        for (size_t k = rushTerr_.size() - 1; k > 0; k--) std::swap(rushTerr_[k], rushTerr_[size_t(rng_.irange(0, int(k)))]);
    }
    // The big ones:  5, 10, 15 a celestial · 8 a terrestrial · 12 the Legionary Crasher · 14 two
    // terrestrials · 17 a rogue celestial · 19 Kronos · 20 Ragnarok.
    std::vector<const char*> specials;
    const char* what = "";
    switch (w) {
        case 5: case 10: case 15: specials = {rushCel_[size_t(w / 5 - 1)]}; what = "A CELESTIAL APPROACHES"; break;
        case 8: specials = {rushTerr_[0]}; what = "A TERRESTRIAL APPROACHES"; break;
        case 12: specials = {"Legionary Crasher"}; what = "THE LEGION MARCHES"; break;
        case 14: specials = {rushTerr_[1], rushTerr_[2]}; what = "TERRESTRIALS APPROACH"; break;
        case 17: specials = {ROGUE_CELESTIALS[rng_.irange(0, 2)]}; what = "A ROGUE CELESTIAL APPROACHES"; break;
        case 19: specials = {"Kronos"}; what = "AN ETERNAL AWAKENS"; break;
        case RUSH_WAVES: specials = {"Ragnarok"}; what = "THE FINAL BOSS"; break;
        default: break;
    }
    if (!specials.empty()) {
        escorts = std::max(1, w / 5 - 1) + 3 * lap;  // one to three lesser bosses come along
        std::string names;
        // Kronos is no longer the last boss, so it arrives weakened; Ragnarok after it is at full strength.
        float big = w == 19 ? 0.55f : (w == 8 || w == 14) ? 0.85f : 1.f;
        for (size_t i = 0; i < specials.size(); i++) {
            waveBosses_.push_back(makeBoss(specials[i], edge(), crowd * big * lapMul)->id);
            names += std::string(i ? " and " : "") + specials[i];
        }
        sendBanner("WAVE " + std::to_string(wave_), std::string(what) + ": " + names, bossKind(specials[0]).color);
    }
    // Lesser bosses: elite crashers at first, then the strange ones, the mysticals and the nesters.
    std::vector<const char*> pool(std::begin(ELITES), std::end(ELITES));
    if (w >= 3 || lap) pool.insert(pool.end(), {"Elite Skimmer", "Summoner", "Nest Keeper", "Rogue Palisade"});
    if (w >= 6 || lap) pool.insert(pool.end(), {"Sorcerer", "Enchantress", "Exorcistor", "Nest Warden"});
    if (w >= 9 || lap) pool.insert(pool.end(), {"Nest Guardian", "Rogue Armada"});
    for (size_t k = pool.size() - 1; k > 0; k--) std::swap(pool[k], pool[size_t(rng_.irange(0, int(k)))]);
    escorts = std::min(escorts, 8);
    for (int i = 0; i < escorts; i++) {
        // As escorts, the bigger lesser bosses are trimmed to an elite's league: here it is their weapons that differ.
        const char* kind = pool[size_t(i) % pool.size()];
        waveBosses_.push_back(makeBoss(kind, edge(), hpScale * std::min(1.f, 6500.f / bossKind(kind).hp))->id);
    }
    if (wave_ >= 2 && rng_.chance(0.03f)) {  // a rare guest
        waveBosses_.push_back(makeBoss("Bob", edge(), std::max(hpScale, 0.5f))->id);
        sendMsg(nullptr, "Bob has joined the wave. Nobody invited Bob.");
    }
    sendMsg(nullptr, "Wave " + std::to_string(wave_) + (mode == M_ENDLESS ? "" : " of " + std::to_string(RUSH_WAVES)) + ": " +
                         std::to_string(waveBosses_.size()) + (waveBosses_.size() == 1 ? " boss is coming!" : " bosses are coming!"));
}

// Invulnerable closers sweep the map; once every player and bot is gone the arena starts afresh.
void Room::closeArena(const std::string& reason) {
    if (closeTick_ >= 0) return;
    closeTick_ = tickNo;
    closeAt_ = -1;
    reopenTick_ = -1;
    for (Client* c : clients)
        if (c->inDom) releaseDominator(*c);  // nobody sits out the closing inside a dominator
    sendBanner("ARENA CLOSED", reason.empty() ? "No players may join. Arena closers are on their way." : reason, C_CLOSER);
    sendMsg(nullptr, "Arena closed: no players may join.");
    int tanks = 0;
    for (Entity* e : ents)
        if (!e->dead && e->kind == K_TANK && e->tank->role == R_PLAYER) tanks++;
    int n = std::clamp(2 + tanks / 5, 3, 6);
    for (int i = 0; i < n; i++) {
        Vec2 p;
        switch (i % 4) {
            case 0: p = {rng_.range(100, W - 100), 80}; break;
            case 1: p = {rng_.range(100, W - 100), H - 80}; break;
            case 2: p = {80, rng_.range(100, H - 100)}; break;
            default: p = {W - 80, rng_.range(100, H - 100)}; break;
        }
        // Team Polygon: their shots go straight through shapes, bosses and neutral dominators to the tanks.
        Entity* e = spawnNpc("Arena Closer", R_CLOSER, TEAM_FOOD, C_CLOSER, p, 44, 1);
        e->ghost = true;
        e->tank->invincible = true;
        e->tank->accel = 1.05f;
        e->tank->ai.offset = i * 3;
        e->damage = 45;
        e->projDamage = 45;
        e->damp = 0.9f;
    }
}

// Throw everything away and start a new round: fresh map furniture, scores back to zero.
void Room::resetArena() {
    for (Entity* e : ents) e->dead = true;
    for (Entity* e : pending_) e->dead = true;
    foodCount_ = nestCount_ = 0;
    doms_.clear();
    sancts_.clear();
    waveBosses_.clear();
    wave_ = 0;
    waveNextTick_ = -1;
    allLostTick_ = -1;
    bossEnts_.clear();
    bossWarned_ = false;
    nextBossTick_ = -1;
    nextBossCount_ = 1;
    rushCel_.clear();
    rushTerr_.clear();
    closeTick_ = reopenTick_ = closeAt_ = -1;
    openedTick_ = tickNo;
    statRounds++;
    // A slot with several modes picks one at random for the new game.
    configure(options[size_t(rng_.irange(0, int(options.size()) - 1))]);
    populate();
    for (Client* c : clients) {
        c->tankId = 0;
        c->inDom = false;
        c->keepScore = 0;
        c->spectate = 0;
        c->team = -1;      // teams are dealt again at the next spawn (the mode may have changed)
        c->played = false;
        c->cam = {W / 2, H / 2};
        sendInit(*c);      // new mode, new map
    }
    std::string sub = "The arena is open again.";
    switch (mode) {
        case M_DOM: case M_MAZEDOM: sub = "Capture all four dominators to win."; break;
        case M_RUSH: sub = "Defend the sanctuaries through 20 waves of bosses."; break;
        case M_ENDLESS: sub = "The waves never end. How many can you clear?"; break;
        case M_TAG: sub = "Destroy a tank and it joins your team. Tag everyone to win."; break;
        case M_BR: sub = "No respawns. Stay inside the circle. The last tank alive wins."; break;
        case M_MOTHER: sub = "Protect your Mothership and destroy theirs."; break;
        case M_KOTH: sub = "Hold the hill in the middle. First team to " + std::to_string(KOTH_GOAL) + " points wins."; break;
        case M_SIEGE: sub = "Blue attacks, Green defends: take all three dominators before time runs out."; break;
        case M_CTF: sub = "Bring the enemy flag back to your own stand. First to " + std::to_string(CTF_GOAL) + " wins."; break;
        case M_SANDBOX: sub = "Free levels, any class, and a panel to spawn shapes and bosses."; break;
        default: break;
    }
    if (cols_) sub += " Maze layout: " + mazeStyle_ + ".";
    sendBanner("NEW GAME: " + title, sub, C_GREEN);
}

// ============================================================ clients
void Room::addClient(Client* c) {
    c->room = this;
    c->tankId = 0;
    c->keepScore = 0;
    c->spectate = 0;
    c->played = false;
    c->dev = false;
    c->devTries = 0;
    c->inDom = false;
    c->team = -1;  // chosen at first spawn so menu spectators don't skew team balance
    c->cam = {W / 2, H / 2};
    clients.push_back(c);
    sendInit(*c);
}

void Room::removeClient(Client* c) {
    if (Entity* e = get(c->tankId)) {
        e->tank->client = nullptr;
        if (e->tank->role == R_PLAYER) {
            e->hp = 0;  // reaped on the next tick, no kill credit
            e->lastHitBy = 0;
            e->lastHitTick = -100000;
            e->cause = nullptr;
        } else {  // they were driving a dominator: it simply goes back to the computer
            e->tank->fire = false;
            e->tank->ai.target = 0;
        }
    }
    c->inDom = false;
    clients.erase(std::remove(clients.begin(), clients.end(), c), clients.end());
    c->room = nullptr;
}

void Room::onMessage(Client& c, const uint8_t* data, size_t n) {
    ByteReader r(data, n);
    uint32_t op = r.u8();
    if (!r.ok) return;
    Entity* e = get(c.tankId);
    switch (op) {
        case 0: {  // spawn: name
            if (e) return;
            if (closing()) {
                if (send) send(c, "{\"t\":\"closed\"}", false);
                return;
            }
            if (rushMode() && closeAt_ < 0 && allLostTick_ >= 0) {  // nowhere to spawn until a sanctuary is restored
                if (send) send(c, "{\"t\":\"nospawn\"}", false);
                return;
            }
            if (mode == M_BR && brBegun_) {  // Battle Royale: watch this one, play the next
                if (send) send(c, "{\"t\":\"nospawn\",\"why\":\"The battle has begun. You can watch it and join the next one.\"}", false);
                return;
            }
            if (players() >= maxPlayers) {
                if (send) send(c, "{\"t\":\"full\"}", false);
                return;
            }
            std::string name = sanitizeName(r.rest());
            if (name.empty()) name = "Unnamed";
            c.name = name;
            if (!c.played) {
                c.team = smallestTeam();
                c.played = true;
            }
            Entity* t = spawnTank(name, c.team, c.dev ? std::max(c.keepScore, levelScore(MAX_LEVEL)) : c.keepScore, &c, -1);
            if (c.dev) setDef(*t, tankIndex("Developer"));
            c.tankId = t->id;
            c.keepScore = 0;
            c.spectate = 0;
            break;
        }
        case 1: {  // input: flags, aim offset
            uint32_t flags = r.u8();
            float dx = r.f32(), dy = r.f32();
            if (!r.ok || !e || !std::isfinite(dx) || !std::isfinite(dy)) return;
            TankData& t = *e->tank;
            t.move = {float((flags >> 3) & 1) - float((flags >> 2) & 1), float((flags >> 1) & 1) - float(flags & 1)};
            t.aim = {clampf(dx, -5000, 5000), clampf(dy, -5000, 5000)};
            t.fire = (flags & 16) != 0;
            t.alt = (flags & 32) != 0;
            t.lvlKey = (flags & 64) != 0;
            t.manual = (flags & 128) != 0;
            // An optional second flag byte (newer clients): bit 0 = reverse tank.
            uint32_t more = r.left() ? r.u8() : 0;
            t.reverse = (more & 1) != 0 && t.role == R_PLAYER;
            break;
        }
        case 2: {  // spend a skill point
            uint32_t i = r.u8();
            if (!r.ok || !e || i >= S_COUNT || e->tank->role != R_PLAYER) return;
            TankData& t = *e->tank;
            const TankDef& d = TANKS[t.def];  // each class has its own stats and cap (the Smasher line: five stats, 12 each)
            if (t.skillPts > 0 && ((d.statMask >> i) & 1) && t.skills[i] < d.statCap) {
                t.skills[i]++;
                t.skillPts--;
                refresh(*e);
            }
            break;
        }
        case 3: {  // upgrade class
            uint32_t def = r.u16();
            if (!r.ok || !e || e->tank->role != R_PLAYER) return;
            if (c.dev) {  // developers may become anything, computer-controlled classes included
                if (def < TANKS.size()) {
                    setDef(*e, int(def));
                    e->hp = e->maxHp;
                }
                break;
            }
            if (mode == M_SANDBOX) {  // Sandbox: any playable class at any level
                if (def < TANKS.size() && !TANKS[def].hidden) {
                    setDef(*e, int(def));
                    e->hp = e->maxHp;
                }
                break;
            }
            for (int o : upgradeOptions(*e->tank))
                if (uint32_t(o) == def) {
                    setDef(*e, o);
                    break;
                }
            break;
        }
        case 5: {  // developer code
            std::string code = r.rest();
            auto now = std::chrono::steady_clock::now();
            bool locked = now < g_devGuard.lockedUntil;
            bool ok = !c.dev && !devCode.empty() && !locked && c.devTries < 5 && code.size() <= 64 && sameCode(code, devCode);
            if (c.dev) ok = true;
            if (ok) {
                c.dev = true;
            } else if (!devCode.empty() && !locked) {
                c.devTries++;
                if (now - g_devGuard.windowStart > std::chrono::minutes(10)) {
                    g_devGuard.windowStart = now;
                    g_devGuard.fails = 0;
                }
                if (++g_devGuard.fails >= 20) {  // someone is guessing: shut the door for ten minutes
                    g_devGuard.fails = 0;
                    g_devGuard.lockedUntil = now + std::chrono::minutes(10);
                    std::printf("[dev] too many wrong developer codes; developer login is locked for 10 minutes\n");
                }
            }
            if (send) send(c, ok ? "{\"t\":\"dev\",\"ok\":1}" : "{\"t\":\"dev\",\"ok\":0}", false);
            if (ok && e && e->tank->role == R_PLAYER) {  // already playing: switch straight away
                addScore(*e, levelScore(MAX_LEVEL) - e->tank->score);
                setDef(*e, tankIndex("Developer"));
            }
            break;
        }
        case 6:  // take control of one of your team's dominators, or let go of it
            toggleDominator(c);
            break;
        case 7: {  // self-destruct: only while the tank is still spawn-protected, so it can undo a bad
                   // spawn but never get a tank out of a fight, deny a kill or serve as a quick trip home
            if (!e || e->tank->role != R_PLAYER || closing() || e->tank->invuln <= 0) return;
            e->tank->invuln = 0;
            e->hp = 0;
            e->cause = "self-destruct";
            break;
        }
        case 8: {  // latency probe: send the client's number straight back
            uint32_t v = r.u32();
            if (r.ok && send) send(c, "{\"t\":\"pong\",\"v\":" + std::to_string(v) + "}", false);
            break;
        }
        case 9: {  // chat: one line, shown above the sender's tank and in the chat list of everyone in the arena
            if (!chat || !e || e->tank->client != &c) return;  // only from a tank that is in the arena
            std::string raw = r.rest();
            if (raw.size() > 4 * CHAT_MAX_CHARS) return;
            std::string text = sanitizeChat(raw, CHAT_MAX_CHARS);
            if (text.empty()) return;
            // Three lines in a row, then one every two seconds; saying the same thing again takes ten seconds.
            if (tickNo < c.chatTick) c.chatTick = c.chatWarnTick = c.lastChatTick = -100000;  // a new arena counts from zero
            c.chatTokens = std::min(3.f, c.chatTokens + float(std::min(tickNo - c.chatTick, 100000)) / float(2 * TICK_RATE));
            c.chatTick = tickNo;
            const bool repeat = text == c.lastChat && tickNo - c.lastChatTick < 10 * TICK_RATE;
            if (repeat || c.chatTokens < 1) {
                if (tickNo - c.chatWarnTick >= TICK_RATE) {
                    c.chatWarnTick = tickNo;
                    sendMsg(&c, repeat ? "You have just said that" : "You are sending messages too fast");
                }
                return;
            }
            c.chatTokens -= 1;
            c.lastChat = text;
            c.lastChatTick = tickNo;
            statChats++;
            sendChat(*e, c.name, text);
            break;
        }
        case 10:  // Sandbox: spawn shapes and bosses, set a level, add bots...
            sandboxCommand(c, r);
            break;
        default:  // 4 = keep-alive ping; the network layer has already noted the activity
            break;
    }
}

// Domination: a player may sit in one of their team's dominators. Their own tank is put away
// (class, skills and score are remembered) and comes back at the base when they let go.
void Room::toggleDominator(Client& c) {
    if (c.inDom) {
        releaseDominator(c);
        sendMsg(&c, "You are back in your own tank.");
        return;
    }
    Entity* e = get(c.tankId);
    if (!e || (doms_.empty() && mothers_.empty()) || closing() || e->tank->role != R_PLAYER) return;
    TankData& t = *e->tank;
    const bool ship = !mothers_.empty();  // Mothership mode: the seat is the team's Mothership
    if (tickNo - t.lastHurt < 5 * TICK_RATE) {
        sendMsg(&c, ship ? "You cannot take over the Mothership while under fire." : "You cannot take control of a dominator while under fire.");
        return;
    }
    Entity* best = nullptr;
    float bestD = 1e30f;
    for (const std::vector<Dom>* seats : {&doms_, &mothers_})
        for (const Dom& d : *seats) {
            Entity* de = get(d.ent);
            if (!de || de->hp <= 0 || de->team != e->team || de->tank->client) continue;
            if (de->tank->role != R_DOM && de->tank->role != R_MOTHER) continue;
            float dd = (de->pos - e->pos).len2();
            if (dd < bestD) { bestD = dd; best = de; }
        }
    if (!best) {
        sendMsg(&c, ship ? "A team-mate is already driving your Mothership." : "Your team has no free dominator to control. Capture one first.");
        return;
    }
    c.domScore = t.score;
    c.domDef = t.def;
    c.domPts = t.skillPts;
    for (int i = 0; i < S_COUNT; i++) c.domSkills[i] = t.skills[i];
    c.domKills = t.kills;
    c.domAssists = t.assists;
    c.domBossKills = t.bossKills;
    c.domPolys = t.polys;
    t.client = nullptr;
    e->dead = true;  // put away quietly: no death screen, no kill credit (its drones notice and vanish)
    best->tank->client = &c;
    best->tank->fire = false;
    best->tank->move = {};
    c.tankId = best->id;
    c.inDom = true;
    c.spectate = 0;
    sendMsg(&c, best->tank->role == R_MOTHER ? "You are driving the Mothership: move, aim and click to send its drones. Press H to leave it."
                                             : "You are controlling a dominator. Press H to leave it.");
}

void Room::releaseDominator(Client& c) {
    if (!c.inDom) return;
    c.inDom = false;
    if (Entity* d = get(c.tankId))
        if ((d->tank->role == R_DOM || d->tank->role == R_MOTHER) && d->tank->client == &c) {
            d->tank->client = nullptr;
            d->tank->fire = d->tank->alt = false;
            d->tank->move = {};
            d->tank->ai.target = 0;
        }
    Entity* e = spawnTank(c.name, c.team, c.domScore, &c, -1);
    TankData& t = *e->tank;
    setDef(*e, std::clamp(c.domDef, 0, int(TANKS.size()) - 1));
    for (int i = 0; i < S_COUNT; i++) t.skills[i] = c.domSkills[i];
    t.skillPts = c.domPts;
    t.kills = c.domKills;
    t.assists = c.domAssists;
    t.bossKills = c.domBossKills;
    t.polys = c.domPolys;
    refresh(*e);
    e->hp = e->maxHp;
    e->shield = e->maxShield;
    c.tankId = e->id;
}

std::string Room::summary() const {
    int tanks = 0, maxLevel = 0;
    double top = 0;
    std::string topName, topTank;
    for (Entity* e : ents) {
        if (e->dead || e->kind != K_TANK || e->tank->role != R_PLAYER) continue;
        tanks++;
        maxLevel = std::max(maxLevel, e->tank->level);
        if (e->tank->score >= top) { top = e->tank->score; topName = e->tank->name; topTank = TANKS[e->tank->def].name; }
    }
    char buf[400];
    std::snprintf(buf, sizeof(buf),
                  "%-5s tick %6d | ents %4zu  tanks %2d  food %3d  bullets %3d  traps %3d  drones %3d | top %s (%s) %.0f, max lvl %d",
                  key.c_str(), tickNo, ents.size(), tanks, countKind(K_FOOD), countKind(K_BULLET), countKind(K_TRAP),
                  countKind(K_DRONE), topName.c_str(), topTank.c_str(), top, maxLevel);
    std::string s = buf;
    s += " | kills " + std::to_string(statKills) + ", assists " + std::to_string(statAssists) + ", bosses " + std::to_string(statBossKills) +
         "/" + std::to_string(statBosses) + " (" + std::to_string(statGroups) + " groups), rounds " + std::to_string(statRounds);
    s += "\n      polygons: " + std::to_string(statCrashers) + " crashers, " + std::to_string(statMutations) + " mutated, " +
         std::to_string(statEvolved) + " evolved | " + std::to_string(statMerges) + " block merges | deaths " +
         std::to_string(statDeaths) + " (" + std::to_string(statCrasherDeaths) + " to crashers)";
    s += " | bounties " + std::to_string(statBounties) + ", dodges " + std::to_string(statDodges) + ", traps over the limit " + std::to_string(statTrapCaps);
    if (rushMode())
        s += " | wave " + std::to_string(wave_) + (mode == M_RUSH ? "/" + std::to_string(RUSH_WAVES) : " (best " + std::to_string(bestWave_) + ")") +
             ", bosses alive " + std::to_string(waveBosses_.size()) +
             ", sanctuaries " + std::to_string(sanctuariesAlive()) + ", wins " + std::to_string(statWins);
    if (mode == M_TAG) {
        s += " | tagged " + std::to_string(statTagged) + ", wins " + std::to_string(statWins) + ", teams:";
        for (int n : teamMembers()) s += " " + std::to_string(n);
    }
    if (mode == M_BR) s += std::string(" | battle ") + (brBegun_ ? "on" : "not begun") + ", wins " + std::to_string(statWins);
    if (mode == M_KOTH) s += " | hill " + std::to_string(hillScore_[0]) + ":" + std::to_string(hillScore_[1]) + ", wins " + std::to_string(statWins);
    if (mode == M_CTF)
        s += " | flags taken " + std::to_string(statFlagGrabs) + ", captured " + std::to_string(statFlagCaps) + ", score " + std::to_string(ctfScore_[0]) +
             ":" + std::to_string(ctfScore_[1]) + ", wins " + std::to_string(statWins);
    if (mode == M_SIEGE) s += " | dominators taken " + std::to_string(siegeTaken_) + "/3, wins " + std::to_string(statWins);
    if (mode == M_MOTHER) {
        s += " | wins " + std::to_string(statWins) + ", motherships:";
        for (const Dom& d : mothers_) {
            Entity* e = get(d.ent);
            s += " " + (e ? std::to_string(int(100 * e->hp / e->maxHp)) + "%" : std::string("-"));
        }
    }
    if (!doms_.empty()) {
        s += " | captures " + std::to_string(statCaptures) + ", wins " + std::to_string(statWins) + ", doms:";
        for (const Dom& d : doms_) {
            Entity* e = get(d.ent);
            s += std::string(" ") + (e ? (e->team >= 0 ? teamName(e->team) : "-") : "?");
        }
    }
    return s;
}

// ============================================================ Sandbox
std::vector<int> Room::bossDefs() {
    std::vector<int> out;
    for (const BossKind& k : BOSS_KINDS) out.push_back(std::max(0, tankIndex(k.name)));
    return out;
}

// Message 10: what the Sandbox panel sends.  cmd, two small arguments, and a point relative to the tank.
//    1 spawn a polygon   a = shape, b = size (bits 0-1), crasher (bit 2), mutation (bits 3-5)
//    2 spawn a boss      a = its place in BOSS_KINDS
//    3 natural polygons  a = 1 on, 0 off (off also clears the map)
//    4 clear the bosses
//    5 set my level      a = level
//    6 bots              a = 1 one more, 0 one fewer
//    7 switch damage to my tank off or on
//    8 next bot difficulty (easy, normal, hard, mixed)
//    9 repair my tank
void Room::sandboxCommand(Client& c, ByteReader& r) {
    if (mode != M_SANDBOX || closing()) return;
    Entity* e = get(c.tankId);
    if (!e || e->tank->role != R_PLAYER) return;
    const uint32_t cmd = r.u8(), a = r.u8(), b = r.u8();
    const float dx = r.f32(), dy = r.f32();
    if (!r.ok || !std::isfinite(dx) || !std::isfinite(dy)) return;
    // Six commands at once, then three a second: enough for play, too few to flood the arena.
    if (tickNo < c.sandTick) c.sandTick = tickNo;
    c.sandTokens = std::min(6.f, c.sandTokens + float(tickNo - c.sandTick) / 10.f);
    c.sandTick = tickNo;
    if (c.sandTokens < 1) return;
    c.sandTokens -= 1;
    Vec2 at = e->pos + Vec2{clampf(dx, -1600, 1600), clampf(dy, -1600, 1600)};
    at.x = clampf(at.x, 60, W - 60);
    at.y = clampf(at.y, 60, H - 60);
    TankData& t = *e->tank;
    switch (cmd) {
        case 1: {
            if (foodCount_ + nestCount_ >= 700) { sendMsg(&c, "There are too many polygons already."); return; }
            const bool crasher = (b & 4) != 0;
            const int type = crasher ? int(F_TRIANGLE) : std::min(int(a), int(F_COUNT) - 1);
            const int tier = crasher ? int(b & 1) : int(b & 3);
            const int mut = std::min(int((b >> 3) & 7), int(MU_COUNT) - 1);
            Entity* f = create(K_FOOD);
            f->team = TEAM_FOOD;
            f->pos = at;
            f->maxHp = 0;
            setFood(*f, type, tier, mut, crasher);
            f->hp = f->maxHp;
            f->angle = rng_.range(0, TAU);
            f->spin = rng_.range(-0.012f, 0.012f);
            foodCount_++;
            break;
        }
        case 2: {
            const int kinds = int(sizeof(BOSS_KINDS) / sizeof(BOSS_KINDS[0]));
            if (int(a) >= kinds) return;
            bossEnts_.erase(std::remove_if(bossEnts_.begin(), bossEnts_.end(), [&](uint32_t id) { return get(id) == nullptr; }), bossEnts_.end());
            if (bossEnts_.size() >= 6) { sendMsg(&c, "Six bosses at a time is the limit."); return; }
            Entity* boss = makeBoss(BOSS_KINDS[a].name, at, 1);
            bossEnts_.push_back(boss->id);
            bossSpawnTick_ = tickNo;
            sendMsg(nullptr, t.name + " summoned " + bossLabel(*boss) + ".");
            break;
        }
        case 3: {
            const bool on = a != 0;
            foodTarget_ = on ? 220 : 0;
            nestTarget_ = on ? 12 : 0;
            if (!on) {
                for (Entity* f : ents)
                    if (f->kind == K_FOOD) f->dead = true;
                for (Entity* f : pending_)
                    if (f->kind == K_FOOD) f->dead = true;
                foodCount_ = nestCount_ = 0;
            }
            sendMsg(nullptr, on ? t.name + " switched the natural polygons back on." : t.name + " cleared the polygons.");
            break;
        }
        case 4:
            for (uint32_t id : bossEnts_)
                if (Entity* boss = get(id)) boss->dead = true;  // (its drones notice and vanish)
            bossEnts_.clear();
            sendMsg(nullptr, t.name + " sent the bosses away.");
            break;
        case 5:
            setLevel(*e, int(a));
            break;
        case 6: {
            if (a) {
                if (bots_.size() >= 12) { sendMsg(&c, "Twelve bots is the limit."); return; }
                BotSlot s;
                s.name = BOT_NAMES[(size_t(rng_.irange(0, BOT_NAME_COUNT - 1)) + bots_.size()) % BOT_NAME_COUNT];
                s.respawnAt = tickNo + 15;
                bots_.push_back(s);
            } else if (!bots_.empty()) {
                if (Entity* bot = get(bots_.back().ent)) {
                    bot->tank->invuln = 0;
                    bot->hp = 0;
                    bot->lastHitBy = 0;
                    bot->lastHitTick = -100000;
                    bot->cause = nullptr;
                }
                bots_.pop_back();
            }
            sandBots_ = int(bots_.size());
            sendMsg(nullptr, "Bots in the Sandbox: " + std::to_string(sandBots_));
            break;
        }
        case 7:
            t.god = !t.god;
            refresh(*e);
            sendMsg(&c, t.god ? "Nothing can damage your tank now." : "Your tank can be damaged again.");
            break;
        case 8: {
            static const char* const names[4] = {"easy", "normal", "hard", "mixed"};
            botLevel = (botLevel + 1) % 4;
            for (Entity* o : ents)
                if (!o->dead && o->kind == K_TANK && o->tank->bot) o->tank->ai.level = uint8_t(botLevel < 3 ? botLevel : rng_.irange(0, 2));
            sendMsg(nullptr, std::string("Bot difficulty: ") + names[botLevel]);
            break;
        }
        case 9:
            e->hp = e->maxHp;
            e->shield = e->maxShield;
            break;
        default: break;
    }
}
