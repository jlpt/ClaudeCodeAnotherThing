/*
 * Enemies and their AI, spell/enemy projectiles, and the particle system.
 */
#include "game.h"

enemy_t g_enemies[MAX_ENEMIES];
static int kill_count;

enum { ES_IDLE, ES_CHASE, ES_WINDUP, ES_ATTACK, ES_RECOVER, ES_FLEE, ES_DYING, ES_STOMP };

typedef struct { float hp, radius, speed, damage, sight; } enemy_info_t;

static const enemy_info_t INFO[EN_COUNT] = {
    [EN_TARGET]    = { 1,  0.6f, 0.0f, 0.0f, 0 },
    [EN_BULLY]     = { 2,  0.45f, 3.6f, 0.5f, 18 },
    [EN_WOLF]      = { 3,  0.7f, 5.2f, 1.0f, 17 },
    [EN_BOAR]      = { 5,  0.95f, 4.0f, 1.5f, 18 },
    [EN_GREATBOAR] = { 28, 2.3f, 4.5f, 2.0f, 40 },
    [EN_WISP]      = { 2,  0.55f, 3.2f, 1.0f, 22 },
    [EN_CRYSTAL]   = { 6,  1.0f, 0.0f, 0.0f, 0 },
    [EN_CORE]      = { 36, 2.4f, 0.0f, 1.5f, 60 },
};

/* ------------------------------------------------------------------ */
/* Particles                                                           */
/* ------------------------------------------------------------------ */

typedef struct { vec3_t pos, vel; float life, max_life, size, gravity; uint32_t color; } particle_t;
#define MAX_PARTICLES 140
static particle_t parts[MAX_PARTICLES];
static int part_next;

void particle_spawn(vec3_t pos, vec3_t vel, uint32_t color, float size, float life, float gravity)
{
    /* ring buffer: when full, the oldest particle is replaced */
    particle_t *p = &parts[part_next];
    part_next = (part_next + 1) % MAX_PARTICLES;
    p->pos = pos; p->vel = vel; p->color = color; p->size = size;
    p->life = p->max_life = life; p->gravity = gravity;
}

void particles_burst(vec3_t pos, uint32_t color, int count, float speed, float size, float life, float gravity)
{
    for (int i = 0; i < count; i++) {
        vec3_t d = v3_norm(v3(frand_range(-1, 1), frand_range(-0.3f, 1), frand_range(-1, 1)));
        particle_spawn(pos, v3_scale(d, speed * frand_range(0.4f, 1.0f)), color,
                       size * frand_range(0.7f, 1.3f), life * frand_range(0.6f, 1.0f), gravity);
    }
}

void particles_clear(void)
{
    memset(parts, 0, sizeof(parts));
}

/* ------------------------------------------------------------------ */
/* Projectiles                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    bool active, from_player;
    uint8_t type;
    vec3_t pos, vel;
    float radius, damage, life, gravity, power, spin;
    int target;
} projectile_t;

#define MAX_PROJECTILES 32
static projectile_t prj[MAX_PROJECTILES];

static const uint32_t PRJ_COLORS[] = { 0x60B8FFFF, 0xFF8A30FF, 0xA89878FF, 0x7A5A3AFF, 0xC070FFFF, 0xFF50C0FF };

void projectile_spawn(prj_type_t type, vec3_t pos, vec3_t vel, bool from_player, float power, int homing_target)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectile_t *p = &prj[i];
        if (p->active) continue;
        memset(p, 0, sizeof(*p));
        p->active = true;
        p->type = type;
        p->from_player = from_player;
        p->pos = pos;
        p->vel = vel;
        p->power = power;
        p->target = homing_target;
        p->life = 1.8f;
        switch (type) {
        case PRJ_WATER: p->radius = 0.22f + power * 0.1f; p->damage = 1.0f + power * 0.8f; break;
        case PRJ_FIRE:  p->radius = 0.22f + power * 0.1f; p->damage = 1.4f + power * 1.0f; break;
        case PRJ_STONE: p->radius = 0.15f + power * 0.06f; p->damage = 1.6f + power * 1.2f; p->life = 1.2f; break;
        case PRJ_MUD:   p->radius = 0.22f; p->damage = 0.5f; p->gravity = 14.0f; p->life = 2.5f; break;
        case PRJ_BOLT:  p->radius = 0.25f; p->damage = 1.0f; p->life = 3.0f; break;
        case PRJ_BOSS:  p->radius = 0.4f; p->damage = 1.5f; p->life = 4.0f; break;
        }
        return;
    }
}

void projectiles_clear(void)
{
    memset(prj, 0, sizeof(prj));
}

static int prj_spell(const projectile_t *p)
{
    return p->type == PRJ_WATER ? SPELL_WATER : (p->type == PRJ_FIRE ? SPELL_FIRE : (p->type == PRJ_STONE ? SPELL_STONE : -1));
}

static void prj_burst(projectile_t *p)
{
    uint32_t c = PRJ_COLORS[p->type];
    particles_burst(p->pos, c, 8 + (int)p->power * 3, 4.0f, 0.22f + p->radius * 0.4f, 0.45f, p->type == PRJ_FIRE ? -3.0f : 6.0f);
    if (p->type == PRJ_WATER) particles_burst(p->pos, 0xE0F4FFFF, 6, 3.0f, 0.15f, 0.4f, 9.0f);
    p->active = false;
}

static void update_projectiles(float dt)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectile_t *p = &prj[i];
        if (!p->active) continue;
        p->life -= dt;
        if (p->life <= 0) { prj_burst(p); continue; }

        if (p->target >= 0) {
            enemy_t *e = &g_enemies[p->target];
            if (e->active && e->hp > 0) {
                float speed = v3_len(p->vel);
                vec3_t want = v3_scale(v3_norm(v3_sub(v3_add(e->pos, v3(0, e->radius * 0.8f, 0)), p->pos)), speed);
                p->vel = v3_lerp(p->vel, want, clampf(6.0f * dt, 0, 1));
            }
        }
        if (p->type == PRJ_BOLT) {
            vec3_t want = v3_scale(v3_norm(v3_sub(v3_add(g_player.pos, v3(0, 0.8f, 0)), p->pos)), 9.0f);
            p->vel = v3_lerp(p->vel, want, clampf(0.8f * dt, 0, 1));
        }
        p->vel.y -= p->gravity * dt;
        p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
        p->spin += dt * 12.0f;

        /* trails */
        if (p->type == PRJ_FIRE && frand() < 0.8f)
            particle_spawn(p->pos, v3(frand_range(-0.5f, 0.5f), 1.0f, frand_range(-0.5f, 0.5f)),
                           frand() < 0.5f ? 0xFFB040FF : 0xFF5010FF, p->radius * 1.2f, 0.3f, -1.0f);
        else if (p->type == PRJ_WATER && frand() < 0.5f)
            particle_spawn(p->pos, v3(0, -0.5f, 0), 0xA0D8FFC0, p->radius * 0.7f, 0.3f, 4.0f);
        else if ((p->type == PRJ_BOLT || p->type == PRJ_BOSS) && frand() < 0.6f)
            particle_spawn(p->pos, v3(0, 0, 0), PRJ_COLORS[p->type] & 0xFFFFFFA0, p->radius, 0.25f, 0);

        if (p->from_player) {
            bool hit = false;
            for (int k = 0; k < MAX_ENEMIES && !hit; k++) {
                enemy_t *e = &g_enemies[k];
                if (!e->active || e->hp <= 0 || e->state == ES_DYING) continue;
                vec3_t c = v3_add(e->pos, v3(0, e->radius * 0.8f, 0));
                float r = e->radius + p->radius;
                if (e->type == EN_TARGET) c.y = e->pos.y + 0.9f;
                if (v3_dot(v3_sub(c, p->pos), v3_sub(c, p->pos)) < r * r) {
                    enemy_hit(k, p->damage, p->pos, prj_spell(p));
                    hit = true;
                }
            }
            if (hit) { prj_burst(p); continue; }
            int w = world_projectile_hit(p->pos, p->radius, prj_spell(p));
            if (w) { prj_burst(p); continue; }
        } else {
            vec3_t pc = v3_add(g_player.pos, v3(0, 0.7f, 0));
            float r = 0.5f + p->radius;
            if (v3_dot(v3_sub(pc, p->pos), v3_sub(pc, p->pos)) < r * r) {
                player_damage(p->damage, p->pos);
                prj_burst(p);
                continue;
            }
        }
        if (world_solid_at(p->pos, p->radius * 0.5f)) prj_burst(p);
    }
}

/* ------------------------------------------------------------------ */
/* Enemies                                                             */
/* ------------------------------------------------------------------ */

int enemy_spawn(enemy_type_t type, float x, float z, uint8_t variant)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &g_enemies[i];
        if (e->active) continue;
        memset(e, 0, sizeof(*e));
        e->active = true;
        e->type = type;
        e->variant = variant;
        e->pos = v3(x, world_height(x, z), z);
        e->home = e->pos;
        e->hp = e->max_hp = INFO[type].hp;
        e->radius = INFO[type].radius;
        e->yaw = frand() * TAU_F;
        e->atk_cd = 1.0f + frand();
        e->anim_t = frand() * 10;
        if (type == EN_WISP) e->pos.y += 2.0f;
        if (type == EN_CORE) e->pos.y += 6.0f;
        if (type == EN_TARGET) e->yaw = yaw_towards(e->pos, v3(-19, 0, 5)) + PI_F;
        return i;
    }
    return -1;
}

void enemies_clear(void)
{
    memset(g_enemies, 0, sizeof(g_enemies));
}

int enemies_alive(int type)
{
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g_enemies[i].active && g_enemies[i].state != ES_DYING && g_enemies[i].state != ES_FLEE &&
            (type < 0 || g_enemies[i].type == type)) n++;
    return n;
}

int enemies_killed_total(void) { return kill_count; }
void enemies_reset_kill_count(void) { kill_count = 0; }

int enemy_find_target(vec3_t from, float yaw, float range)
{
    int best = -1;
    float best_score = 1e9f;
    vec3_t f = v3(sinf(yaw), 0, cosf(yaw));
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &g_enemies[i];
        if (!e->active || e->hp <= 0 || e->state == ES_DYING || e->state == ES_FLEE) continue;
        vec3_t d = v3_sub(e->pos, from);
        d.y = 0;
        float dist = v3_len(d);
        if (dist > range) continue;
        float facing = dist > 0.1f ? v3_dot(v3_scale(d, 1.0f / dist), f) : 1.0f;
        float score = dist * (1.6f - facing);
        if (score < best_score) { best_score = score; best = i; }
    }
    return best;
}

static void enemy_die(enemy_t *e)
{
    e->state = ES_DYING;
    e->t = 0.45f;
    uint32_t c = 0xFFFFFFFF;
    switch (e->type) {
    case EN_TARGET:    c = 0xD8C070FF; break;
    case EN_WOLF:      c = 0x8A8A94FF; break;
    case EN_BOAR:      c = 0x7A5A3AFF; break;
    case EN_GREATBOAR: c = 0xA04030FF; break;
    case EN_WISP: case EN_CRYSTAL: case EN_CORE: c = 0xD090FFFF; break;
    }
    particles_burst(v3_add(e->pos, v3(0, e->radius, 0)), c, 16 + (int)(e->radius * 8), 5.0f, 0.3f + e->radius * 0.15f, 0.8f, 6.0f);
    particles_burst(v3_add(e->pos, v3(0, e->radius, 0)), 0xFFFFFFC0, 8, 2.0f, 0.5f + e->radius * 0.2f, 0.7f, -1.0f);
    sfx_play(e->type == EN_GREATBOAR || e->type == EN_CORE ? SFX_EXPLODE : SFX_HIT);
    if (e->type == EN_GREATBOAR || e->type == EN_CORE) { g_cam.shake = 1.0f; game_rumble(0.6f); }
    kill_count++;
    story_on_enemy_killed(e->type);
}

bool enemy_hit(int idx, float dmg, vec3_t from, int spell)
{
    enemy_t *e = &g_enemies[idx];
    if (!e->active || e->hp <= 0 || e->state == ES_DYING || e->state == ES_FLEE) return false;
    /* elemental quirks */
    if (e->type == EN_WISP && spell == SPELL_WATER) dmg *= 1.5f;
    if (e->type == EN_BOAR && spell == SPELL_FIRE) dmg *= 1.3f;
    if (e->type == EN_CRYSTAL && spell == SPELL_STONE) dmg *= 1.5f;
    e->hp -= dmg;
    e->hurt_t = 0.25f;
    sfx_play(SFX_HIT);

    if (e->type != EN_TARGET && e->type != EN_CRYSTAL && e->type != EN_CORE && e->type != EN_GREATBOAR) {
        vec3_t d = v3_sub(e->pos, from);
        d.y = 0;
        d = v3_norm(d);
        e->vel = v3_add(e->vel, v3_scale(d, 5.0f));
        if (e->state == ES_WINDUP) { e->state = ES_RECOVER; e->t = 0.5f; }
    }
    if (e->hp <= 0) {
        e->hp = 0;
        if (e->type == EN_BULLY) {
            e->hp = 0.01f;           /* runs off crying instead of dying */
            e->state = ES_FLEE;
            e->t = 2.6f;
            kill_count++;
            story_on_enemy_killed(EN_BULLY);
        } else {
            enemy_die(e);
        }
    }
    return true;
}

static void move_enemy(enemy_t *e, vec3_t want_vel, float accel, float dt)
{
    e->vel.x = approachf(e->vel.x, want_vel.x, accel * dt);
    e->vel.z = approachf(e->vel.z, want_vel.z, accel * dt);
    vec3_t np = v3_add(e->pos, v3_scale(v3(e->vel.x, 0, e->vel.z), dt));
    world_collide(&np, e->radius * 0.8f);
    np.y = world_height(np.x, np.z);
    e->pos = np;
}

static void contact_damage(enemy_t *e, float extra)
{
    float r = e->radius + 0.45f + extra;
    if (dist_xz(e->pos, g_player.pos) < r && fabsf(e->pos.y - g_player.pos.y) < 1.5f + e->radius)
        player_damage(INFO[e->type].damage, e->pos);
}

static void ai_charger(enemy_t *e, float dt, float dist, vec3_t to_player, bool boss)
{
    const enemy_info_t *in = &INFO[e->type];
    float face = atan2f(to_player.x, to_player.z);
    switch (e->state) {
    case ES_IDLE: {
        /* wander around home */
        if (e->t <= 0) { e->t = 2.0f + frand() * 2.0f; e->yaw = frand() * TAU_F; }
        vec3_t home_d = v3_sub(e->home, e->pos);
        bool far = v3_len(home_d) > 6.0f;
        float y = far ? atan2f(home_d.x, home_d.z) : e->yaw;
        vec3_t v = e->t > 1.0f ? v3(sinf(y) * 1.4f, 0, cosf(y) * 1.4f) : v3(0, 0, 0);
        if (far) e->yaw = y;
        move_enemy(e, v, 6.0f, dt);
        if (dist < in->sight) { e->state = ES_CHASE; e->t = 0; }
        break;
    }
    case ES_CHASE: {
        e->yaw = angle_lerp(e->yaw, face, clampf(6.0f * dt, 0, 1));
        float spd = in->speed;
        move_enemy(e, v3(sinf(e->yaw) * spd, 0, cosf(e->yaw) * spd), 12.0f, dt);
        float attack_range = e->type == EN_WOLF ? 3.2f : (boss ? 14.0f : 9.0f);
        if (dist < attack_range && e->atk_cd <= 0) { e->state = ES_WINDUP; e->t = e->type == EN_WOLF ? 0.45f : 0.8f; }
        if (boss && e->atk_cd <= 0 && dist < 7.0f && frand() < 0.5f) { e->state = ES_STOMP; e->t = 1.1f; e->vel.y = 9.0f; }
        if (dist > in->sight * 1.6f) e->state = ES_IDLE;
        break;
    }
    case ES_WINDUP:
        e->yaw = angle_lerp(e->yaw, face, clampf(10.0f * dt, 0, 1));
        move_enemy(e, v3(0, 0, 0), 20.0f, dt);
        if (e->type != EN_WOLF && frand() < 0.4f)
            particle_spawn(v3(e->pos.x, e->pos.y + 0.1f, e->pos.z), v3(frand_range(-1, 1), 1.5f, frand_range(-1, 1)),
                           0xA89070C0, 0.3f * e->radius, 0.5f, 3.0f);
        if (e->t <= 0) {
            e->state = ES_ATTACK;
            e->t = e->type == EN_WOLF ? 0.32f : (boss ? 1.5f : 1.1f);
            float spd = e->type == EN_WOLF ? 12.0f : (boss ? 15.0f : 13.0f);
            e->vel = v3(sinf(e->yaw) * spd, 0, cosf(e->yaw) * spd);
            if (boss) { sfx_play(SFX_STEP); g_cam.shake = 0.4f; }
        }
        break;
    case ES_ATTACK: {
        vec3_t before = e->pos;
        vec3_t np = v3_add(e->pos, v3_scale(v3(e->vel.x, 0, e->vel.z), dt));
        bool bumped = world_collide(&np, e->radius * 0.8f);
        np.y = world_height(np.x, np.z);
        e->pos = np;
        contact_damage(e, 0.2f);
        if (boss && frand() < 0.5f)
            particle_spawn(v3(before.x, before.y + 0.2f, before.z), v3(0, 2, 0), 0xA89070C0, 0.8f, 0.6f, 2.0f);
        if (e->t <= 0 || bumped) {
            e->state = ES_RECOVER;
            e->t = boss ? 1.4f : (e->type == EN_WOLF ? 0.7f : 1.2f);
            e->atk_cd = boss ? 1.0f : 1.4f + frand();
            e->vel = v3(0, 0, 0);
            if (bumped && boss) { g_cam.shake = 0.6f; sfx_play(SFX_EXPLODE); }
        }
        break;
    }
    case ES_STOMP: {
        e->vel.y -= 22.0f * dt;
        e->pos.y += e->vel.y * dt;
        float g = world_height(e->pos.x, e->pos.z);
        if (e->pos.y <= g && e->vel.y < 0) {
            e->pos.y = g;
            e->state = ES_RECOVER;
            e->t = 1.2f;
            e->atk_cd = 1.5f;
            g_cam.shake = 0.9f;
            game_rumble(0.3f);
            sfx_play(SFX_EXPLODE);
            for (int i = 0; i < 16; i++) {
                float a = i * TAU_F / 16;
                particle_spawn(v3(e->pos.x + cosf(a) * 2.0f, g + 0.3f, e->pos.z + sinf(a) * 2.0f),
                               v3(cosf(a) * 9.0f, 1.0f, sinf(a) * 9.0f), 0xB8A080FF, 0.7f, 0.55f, 0);
            }
            if (dist < 6.5f && g_player.on_ground) player_damage(INFO[e->type].damage, e->pos);
        }
        break;
    }
    case ES_RECOVER:
        move_enemy(e, v3(0, 0, 0), 8.0f, dt);
        if (e->t <= 0) e->state = ES_CHASE;
        break;
    }
}

static void ai_bully(enemy_t *e, float dt, float dist, vec3_t to_player)
{
    float face = atan2f(to_player.x, to_player.z);
    if (e->state == ES_FLEE) {
        float away = face + PI_F;
        e->yaw = angle_lerp(e->yaw, away, clampf(8 * dt, 0, 1));
        move_enemy(e, v3(sinf(e->yaw) * 6.0f, 0, cosf(e->yaw) * 6.0f), 20.0f, dt);
        if (e->t <= 0) e->active = false;
        return;
    }
    /* keep a taunting distance, strafe, and lob mud */
    float want = 7.0f;
    float side = (e->variant & 1) ? 1.0f : -1.0f;
    vec3_t radial = v3_scale(v3_norm(v3(to_player.x, 0, to_player.z)), (dist - want) * 0.9f);
    vec3_t tang = v3_scale(v3(to_player.z, 0, -to_player.x), side * 0.25f / fmaxf(dist, 0.1f) * 6.0f);
    vec3_t v = v3_add(radial, tang);
    float l = v3_len(v);
    if (l > INFO[EN_BULLY].speed) v = v3_scale(v, INFO[EN_BULLY].speed / l);
    move_enemy(e, v, 10.0f, dt);
    e->yaw = angle_lerp(e->yaw, face, clampf(8 * dt, 0, 1));
    if (e->atk_cd <= 0 && dist < 14.0f) {
        e->atk_cd = 2.2f + frand() * 1.2f;
        vec3_t from = v3_add(e->pos, v3(0, 1.0f, 0));
        float t = dist / 9.0f;
        vec3_t vel = v3(to_player.x / t, 0.5f * 14.0f * t, to_player.z / t);
        projectile_spawn(PRJ_MUD, from, vel, false, 1, -1);
        e->anim_t = 0;
        e->t = 0.35f;   /* throw pose */
    }
}

static int count_type(int type)
{
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g_enemies[i].active && g_enemies[i].type == type && g_enemies[i].state != ES_DYING) n++;
    return n;
}

static void ai_wisp(enemy_t *e, float dt, float dist, vec3_t to_player)
{
    e->anim_t += dt;
    float orbit = e->anim_t * 0.6f + e->variant;
    vec3_t goal = v3_add(g_player.pos, v3(cosf(orbit) * 6.5f, 2.4f, sinf(orbit) * 6.5f));
    vec3_t d = v3_sub(goal, e->pos);
    if (dist > 22.0f) d = v3_sub(v3_add(e->home, v3(0, 2, 0)), e->pos);
    vec3_t v = v3_scale(d, 1.2f);
    float l = v3_len(v);
    if (l > INFO[EN_WISP].speed) v = v3_scale(v, INFO[EN_WISP].speed / l);
    e->vel = v3_lerp(e->vel, v, clampf(3 * dt, 0, 1));
    e->pos = v3_add(e->pos, v3_scale(e->vel, dt));
    float g = world_height(e->pos.x, e->pos.z) + 1.2f;
    if (e->pos.y < g) e->pos.y = g;
    if (e->atk_cd <= 0 && dist < 16.0f) {
        e->atk_cd = 2.4f + frand() * 1.5f;
        vec3_t dir = v3_norm(v3_sub(v3_add(g_player.pos, v3(0, 0.8f, 0)), e->pos));
        projectile_spawn(PRJ_BOLT, e->pos, v3_scale(dir, 8.0f), false, 1, -1);
        sfx_play(SFX_CHARGE);
    }
}

static void ai_crystal(enemy_t *e, float dt)
{
    if (e->atk_cd <= 0) {
        e->atk_cd = 7.0f + frand() * 3.0f;
        if (count_type(EN_WISP) < 4 && dist_xz(e->pos, g_player.pos) < 30) {
            int w = enemy_spawn(EN_WISP, e->pos.x + frand_range(-2, 2), e->pos.z + frand_range(-2, 2), (uint8_t)(frand() * 6));
            if (w >= 0) particles_burst(g_enemies[w].pos, 0xE0A0FFFF, 12, 3.0f, 0.3f, 0.6f, 0);
        }
    }
}

static void ai_core(enemy_t *e, float dt, float dist)
{
    e->anim_t += dt;
    float base = world_height(e->home.x, e->home.z) + 6.0f;
    e->pos.y = base + sinf(e->anim_t * 1.3f) * 0.6f;
    bool enraged = e->hp < e->max_hp * 0.5f;
    if (e->atk_cd > 0 || dist > 40.0f) return;

    vec3_t src = v3_add(e->pos, v3(0, 1.2f, 0));
    int pattern = e->variant % 3;
    e->variant++;
    e->atk_cd = enraged ? 1.7f : 2.7f;
    sfx_play(SFX_CHARGE);
    if (pattern == 0) {
        /* ring of bolts that sweeps across the arena */
        int n = enraged ? 14 : 10;
        float off = e->anim_t;
        for (int i = 0; i < n; i++) {
            float a = off + i * TAU_F / n;
            vec3_t v = v3(cosf(a) * 7.0f, -2.4f, sinf(a) * 7.0f);
            projectile_spawn(PRJ_BOSS, src, v, false, 1, -1);
        }
    } else if (pattern == 1) {
        vec3_t aim = v3_norm(v3_sub(v3_add(g_player.pos, v3(0, 0.8f, 0)), src));
        float base_yaw = atan2f(aim.x, aim.z);
        int shots = enraged ? 5 : 3;
        for (int i = 0; i < shots; i++) {
            float y = base_yaw + (i - (shots - 1) * 0.5f) * 0.22f;
            vec3_t v = v3(sinf(y) * 11.0f * sqrtf(1 - aim.y * aim.y), aim.y * 11.0f, cosf(y) * 11.0f * sqrtf(1 - aim.y * aim.y));
            projectile_spawn(PRJ_BOSS, src, v, false, 1, -1);
        }
    } else if (count_type(EN_WISP) < 3) {
        for (int i = 0; i < 2; i++) {
            int w = enemy_spawn(EN_WISP, e->pos.x + frand_range(-4, 4), e->pos.z + frand_range(-4, 4), (uint8_t)(frand() * 6));
            if (w >= 0) particles_burst(g_enemies[w].pos, 0xE0A0FFFF, 12, 3.0f, 0.3f, 0.6f, 0);
        }
    }
}

static void update_enemies(float dt)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &g_enemies[i];
        if (!e->active) continue;
        e->t -= dt;
        e->hurt_t = fmaxf(0, e->hurt_t - dt);
        e->atk_cd -= dt;
        if (e->state == ES_DYING) {
            if (e->t <= 0) e->active = false;
            continue;
        }
        vec3_t to_player = v3_sub(g_player.pos, e->pos);
        to_player.y = 0;
        float dist = v3_len(to_player);
        bool player_ok = g_player.hp > 0 && !g_player.locked;

        switch (e->type) {
        case EN_TARGET:
            break;
        case EN_BULLY:
            if (player_ok || e->state == ES_FLEE) ai_bully(e, dt, dist, to_player);
            break;
        case EN_WOLF:
        case EN_BOAR:
            if (player_ok) ai_charger(e, dt, dist, to_player, false);
            break;
        case EN_GREATBOAR:
            if (player_ok) ai_charger(e, dt, dist, to_player, true);
            break;
        case EN_WISP:
            if (player_ok) ai_wisp(e, dt, dist, to_player);
            break;
        case EN_CRYSTAL:
            e->anim_t += dt;
            if (player_ok) ai_crystal(e, dt);
            break;
        case EN_CORE:
            if (player_ok) ai_core(e, dt, dist);
            else e->anim_t += dt;
            break;
        }

        if (e->type != EN_WISP && e->type != EN_CORE && e->type != EN_CRYSTAL) {
            float sp = sqrtf(e->vel.x * e->vel.x + e->vel.z * e->vel.z);
            e->anim_t += sp * dt * (e->type == EN_GREATBOAR ? 1.0f : 2.5f);
        }

        /* keep enemies from stacking on top of each other */
        for (int k = i + 1; k < MAX_ENEMIES; k++) {
            enemy_t *o = &g_enemies[k];
            if (!o->active || o->state == ES_DYING || o->type == EN_CORE || e->type == EN_CORE) continue;
            float r = (e->radius + o->radius) * 0.8f;
            float dx = o->pos.x - e->pos.x, dz = o->pos.z - e->pos.z;
            float d2 = dx * dx + dz * dz;
            if (d2 < r * r && d2 > 1e-6f) {
                float d = sqrtf(d2), push = (r - d) * 0.5f;
                if (e->type != EN_TARGET && e->type != EN_CRYSTAL) { e->pos.x -= dx / d * push; e->pos.z -= dz / d * push; }
                if (o->type != EN_TARGET && o->type != EN_CRYSTAL) { o->pos.x += dx / d * push; o->pos.z += dz / d * push; }
            }
        }
    }
}

static void update_particles(float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &parts[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->vel.y -= p->gravity * dt;
        p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
    }
}

void combat_update(float dt)
{
    update_enemies(dt);
    update_projectiles(dt);
    update_particles(dt);
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

static float enemy_flash(const enemy_t *e)
{
    if (e->state == ES_DYING) return 1.0f;
    return e->hurt_t > 0 ? 1.0f : 0.0f;
}

void combat_render(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &g_enemies[i];
        if (!e->active) continue;
        if (!gfx_visible(e->pos, e->radius * 2 + 1, g_world.fog_end + 2)) continue;
        float fl = enemy_flash(e);
        float sp = sqrtf(e->vel.x * e->vel.x + e->vel.z * e->vel.z);
        bool moving = sp > 0.3f;
        vec3_t pos = e->pos;
        float shrink = e->state == ES_DYING ? clampf(e->t / 0.45f, 0.05f, 1) : 1.0f;
        switch (e->type) {
        case EN_TARGET:
            if (e->state == ES_DYING) pos.y -= (1 - shrink) * 1.5f;
            draw_target(pos, e->yaw, fl);
            break;
        case EN_BULLY: {
            anim_t a = e->state == ES_FLEE ? ANIM_RUN : (e->t > 0 ? ANIM_SWING : (moving ? ANIM_WALK : ANIM_IDLE));
            float t = a == ANIM_SWING ? 1.0f - e->t / 0.35f : e->anim_t;
            draw_human(MDL_BULLY_A + e->variant % 3, pos, e->yaw, a, t, fl);
            break;
        }
        case EN_WOLF:
            draw_wolf(pos, e->yaw, e->anim_t, moving, fl, shrink);
            break;
        case EN_BOAR:
            draw_boar(pos, e->yaw, e->anim_t, moving, fl, shrink, false);
            break;
        case EN_GREATBOAR:
            draw_boar(pos, e->yaw, e->anim_t, moving || e->state == ES_WINDUP, fl, 2.6f * shrink, true);
            break;
        case EN_WISP:
            draw_wisp(pos, e->anim_t, fl);
            break;
        case EN_CRYSTAL:
            draw_crystal(pos, e->anim_t, fl, shrink);
            break;
        case EN_CORE:
            draw_crystal(v3_sub(pos, v3(0, 2.6f, 0)), e->anim_t * 1.5f, fl, 2.2f * shrink);
            break;
        }
    }

    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectile_t *p = &prj[i];
        if (!p->active) continue;
        float d = p->radius * 2;
        switch (p->type) {
        case PRJ_WATER:
            gfx_part(PRIM_SPHERE, 0x70C0FFFF, p->pos.x, p->pos.y, p->pos.z, d, d * 0.9f, d);
            break;
        case PRJ_FIRE:
            glDisable(GL_LIGHTING);
            gfx_part(PRIM_SPHERE, 0xFFB040FF, p->pos.x, p->pos.y, p->pos.z, d, d, d);
            glEnable(GL_LIGHTING);
            break;
        case PRJ_STONE: {
            vec3_t v = v3_norm(p->vel);
            gfx_part_rot(PRIM_CONE, 0x9A9080FF, p->pos.x, p->pos.y, p->pos.z,
                         acosf(clampf(v.y, -1, 1)), atan2f(v.x, v.z), p->spin, d, d * 2.0f, d);
            break;
        }
        case PRJ_MUD:
            gfx_part(PRIM_SPHERE, 0x6A4A2AFF, p->pos.x, p->pos.y, p->pos.z, d, d, d);
            break;
        default:
            glDisable(GL_LIGHTING);
            gfx_part(PRIM_OCTA, p->type == PRJ_BOSS ? 0xFF70D0FF : 0xD090FFFF, p->pos.x, p->pos.y, p->pos.z, d, d * 1.4f, d);
            glEnable(GL_LIGHTING);
            break;
        }
    }
}

void combat_render_fx(void)
{
    /* blob shadows */
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    gfx_bind(TEX_NONE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    gfx_shadow(g_player.pos, 0.45f, world_height(g_player.pos.x, g_player.pos.z));
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &g_enemies[i];
        if (!e->active || e->type == EN_TARGET) continue;
        if (!gfx_visible(e->pos, 3, 40)) continue;
        float r = e->type == EN_GREATBOAR ? 2.2f : (e->type == EN_CORE ? 2.5f : e->radius);
        gfx_shadow(e->pos, r, world_height(e->pos.x, e->pos.z));
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    /* glows + particles */
    gfx_billboards_begin();
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectile_t *p = &prj[i];
        if (!p->active) continue;
        uint32_t c = PRJ_COLORS[p->type];
        gfx_billboard(p->pos, p->radius * 2.4f, (c & 0xFFFFFF00) | 0x90);
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &g_enemies[i];
        if (!e->active) continue;
        if (e->type == EN_WISP) gfx_billboard(e->pos, 1.3f, 0xC070FF90);
        if (e->type == EN_CRYSTAL) gfx_billboard(v3_add(e->pos, v3(0, 1.2f, 0)), 2.4f, 0xB070FF70);
        if (e->type == EN_CORE) {
            float pulse = 1.0f + sinf(e->anim_t * 4.0f) * 0.15f;
            gfx_billboard(e->pos, 5.0f * pulse, 0xFFB0F0A0);
            gfx_billboard(e->pos, 9.0f * pulse, 0xC060FF50);
        }
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &parts[i];
        if (p->life <= 0) continue;
        if (!gfx_visible(p->pos, 1, 50)) continue;
        float k = p->life / p->max_life;
        uint32_t a = (uint32_t)((p->color & 0xFF) * k);
        gfx_billboard(p->pos, p->size * (0.6f + 0.4f * k), (p->color & 0xFFFFFF00) | a);
    }
    gfx_billboards_end();
    glEnable(GL_LIGHTING);
}
