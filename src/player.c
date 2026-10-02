/*
 * Rudeus: analog movement, jumping, staff strikes, C-button spellcasting
 * (tap = instant incantation-less cast, hold = charged cast), Z-targeting,
 * and the third-person follow camera.
 */
#include "game.h"

player_t g_player;

const char *const SPELL_NAMES[SPELL_COUNT] = { "Water Ball", "Fire Ball", "Stone Cannon", "Healing" };
const uint32_t SPELL_COLORS[SPELL_COUNT] = { 0x58B0FFFF, 0xFF7A30FF, 0xB8A888FF, 0x80F090FF };
const float SPELL_COST[SPELL_COUNT] = { 6, 9, 11, 22 };

#define GRAVITY      26.0f
#define RUN_SPEED    6.8f
#define JUMP_VEL     8.6f
#define RADIUS       0.4f
#define MAX_CHARGE   1.6f

static bool attack_hit_done;
static float recenter_t;
static bool was_charging_any;

void player_reset(vec3_t pos, float yaw)
{
    float max_hp = g_save.max_hp ? g_save.max_hp : 6;
    g_player.pos = pos;
    g_player.pos.y = world_height(pos.x, pos.z);
    g_player.vel = v3(0, 0, 0);
    g_player.yaw = yaw;
    g_player.max_hp = max_hp;
    if (g_player.hp <= 0 || g_player.hp > max_hp) g_player.hp = max_hp;
    g_player.max_mp = 100;
    if (g_player.mp <= 0) g_player.mp = 100;
    g_player.on_ground = true;
    g_player.attack_t = 0;
    g_player.cast_t = 0;
    g_player.charge_spell = -1;
    g_player.charge = 0;
    g_player.hurt_t = 0;
    g_player.lock_target = -1;
    g_player.locked = false;
}

static bool spell_known(int s)
{
    return (g_save.spells >> s) & 1;
}

static vec3_t hand_pos(void)
{
    vec3_t f = v3(sinf(g_player.yaw), 0, cosf(g_player.yaw));
    vec3_t r = v3(-cosf(g_player.yaw), 0, sinf(g_player.yaw));
    float h = g_player.older ? 1.05f : 0.85f;
    return v3_add(v3_add(g_player.pos, v3(0, h, 0)), v3_add(v3_scale(f, 0.55f), v3_scale(r, 0.15f)));
}

static void cast_spell(int s, float charge)
{
    int level = charge < 0.35f ? 1 : (charge < 1.1f ? 2 : 3);
    float cost = SPELL_COST[s] * (level == 1 ? 1.0f : (level == 2 ? 1.6f : 2.4f));
    if (g_player.mp < cost) {
        sfx_play(SFX_FAIL);
        hud_toast("Not enough mana!", 1.2f);
        g_player.no_mp_flash = 1.0f;
        return;
    }
    g_player.mp -= cost;
    g_player.cast_t = 0.3f;

    if (s == SPELL_HEAL) {
        float amount = 1.0f + level * 1.0f;
        g_player.hp = fminf(g_player.max_hp, g_player.hp + amount);
        g_player.heal_fx = 1.0f;
        particles_burst(v3_add(g_player.pos, v3(0, 0.8f, 0)), 0x90FFA0FF, 18 + level * 6, 2.5f, 0.3f, 0.9f, -2.0f);
        sfx_play(SFX_HEAL);
        return;
    }

    vec3_t from = hand_pos();
    vec3_t dir = v3(sinf(g_player.yaw), 0, cosf(g_player.yaw));
    int target = g_player.lock_target;
    if (target >= 0 && g_enemies[target].active) {
        enemy_t *e = &g_enemies[target];
        vec3_t aim = v3_add(e->pos, v3(0, e->radius * 0.8f, 0));
        dir = v3_norm(v3_sub(aim, from));
    } else {
        target = -1;
        dir.y = 0.02f;
    }
    float speed = s == SPELL_WATER ? 17.0f : (s == SPELL_FIRE ? 15.0f : 25.0f);
    prj_type_t type = s == SPELL_WATER ? PRJ_WATER : (s == SPELL_FIRE ? PRJ_FIRE : PRJ_STONE);
    projectile_spawn(type, from, v3_scale(dir, speed), true, (float)level, target);
    sfx_play(s == SPELL_WATER ? SFX_WATER : (s == SPELL_FIRE ? SFX_FIRE : SFX_STONE));
    if (level == 3) game_rumble(0.15f);
}

static void update_spells(float dt)
{
    static const int8_t BUTTON_SPELL[4] = { SPELL_WATER, SPELL_FIRE, SPELL_STONE, SPELL_HEAL };
    bool pressed[4] = { g_frame.pressed.c_left, g_frame.pressed.c_down, g_frame.pressed.c_right, g_frame.pressed.c_up };
    bool held[4] = { g_frame.held.c_left, g_frame.held.c_down, g_frame.held.c_right, g_frame.held.c_up };

    if (g_player.charge_spell < 0) {
        for (int i = 0; i < 4; i++) {
            if (!pressed[i]) continue;
            int s = BUTTON_SPELL[i];
            if (!spell_known(s)) {
                hud_toast("You haven't learned that spell yet.", 1.4f);
                sfx_play(SFX_FAIL);
                break;
            }
            g_player.charge_spell = s;
            g_player.charge = 0;
            break;
        }
        return;
    }

    int s = g_player.charge_spell;
    int btn = s == SPELL_WATER ? 0 : (s == SPELL_FIRE ? 1 : (s == SPELL_STONE ? 2 : 3));
    g_player.charge += dt;
    /* gathering mana: sparks swirl around the hand */
    if (g_player.charge > 0.2f) {
        float a = g_frame.time * 14.0f;
        float rad = 0.25f + fminf(g_player.charge, MAX_CHARGE) * 0.15f;
        vec3_t hp = hand_pos();
        particle_spawn(v3_add(hp, v3(cosf(a) * rad, sinf(a * 1.3f) * rad * 0.5f, sinf(a) * rad)),
                       v3(0, 0.4f, 0), SPELL_COLORS[s], 0.12f + g_player.charge * 0.06f, 0.25f, 0);
        if (!was_charging_any) sfx_play(SFX_CHARGE);
        was_charging_any = true;
    }
    if (!held[btn] || g_player.charge >= MAX_CHARGE + 0.6f) {
        cast_spell(s, g_player.charge);
        g_player.charge_spell = -1;
        g_player.charge = 0;
        was_charging_any = false;
    }
}

static void update_melee(void)
{
    if (g_frame.pressed.b && g_player.attack_t <= 0 && g_player.charge_spell < 0) {
        g_player.attack_t = 0.38f;
        attack_hit_done = false;
        sfx_play(SFX_SWING);
    }
    if (g_player.attack_t > 0 && g_player.attack_t < 0.24f && !attack_hit_done) {
        attack_hit_done = true;
        vec3_t f = v3(sinf(g_player.yaw), 0, cosf(g_player.yaw));
        for (int i = 0; i < MAX_ENEMIES; i++) {
            enemy_t *e = &g_enemies[i];
            if (!e->active || e->hp <= 0) continue;
            vec3_t d = v3_sub(e->pos, g_player.pos);
            d.y = 0;
            float dist = v3_len(d);
            if (dist > 1.7f + e->radius) continue;
            if (dist > 0.3f && v3_dot(v3_scale(d, 1.0f / dist), f) < 0.2f) continue;
            enemy_hit(i, 1.0f, g_player.pos, -1);
        }
    }
}

void player_damage(float amount, vec3_t from)
{
    if (g_player.hurt_t > 0 || g_player.hp <= 0) return;
    g_player.hp -= amount;
    g_player.hurt_t = 1.1f;
    vec3_t d = v3_sub(g_player.pos, from);
    d.y = 0;
    d = v3_norm(d);
    g_player.vel = v3(d.x * 7.0f, 5.0f, d.z * 7.0f);
    g_player.on_ground = false;
    g_player.charge_spell = -1;
    sfx_play(SFX_HURT);
    game_rumble(0.25f);
    g_cam.shake = fmaxf(g_cam.shake, 0.35f);
    if (g_player.hp <= 0) {
        g_player.hp = 0;
        story_on_player_dead();
    }
}

void player_update(float dt)
{
    player_t *p = &g_player;
    p->hurt_t = fmaxf(0, p->hurt_t - dt);
    p->attack_t = fmaxf(0, p->attack_t - dt);
    p->cast_t = fmaxf(0, p->cast_t - dt);
    p->heal_fx = fmaxf(0, p->heal_fx - dt);
    p->no_mp_flash = fmaxf(0, p->no_mp_flash - dt * 2);
    if (p->charge_spell < 0) p->mp = fminf(p->max_mp, p->mp + 7.0f * dt);

    bool control = !p->locked && !dialog_active() && p->hp > 0;
    vec3_t want = v3(0, 0, 0);

    if (control) {
        float sx = g_frame.stick_x, sy = g_frame.stick_y;
        float mag = fminf(1.0f, sqrtf(sx * sx + sy * sy));
        if (mag > 0.01f) {
            vec3_t f = v3(sinf(g_cam.yaw), 0, cosf(g_cam.yaw));
            vec3_t r = v3(-cosf(g_cam.yaw), 0, sinf(g_cam.yaw));
            vec3_t dir = v3_norm(v3_add(v3_scale(f, sy), v3_scale(r, sx)));
            float speed = RUN_SPEED * mag;
            if (p->charge_spell >= 0) speed *= 0.45f;
            if (p->attack_t > 0) speed *= 0.35f;
            want = v3_scale(dir, speed);
            if (p->lock_target < 0)
                p->yaw = angle_lerp(p->yaw, atan2f(dir.x, dir.z), clampf(14.0f * dt, 0, 1));
        }
        if (p->lock_target >= 0 && g_enemies[p->lock_target].active)
            p->yaw = angle_lerp(p->yaw, yaw_towards(p->pos, g_enemies[p->lock_target].pos), clampf(12.0f * dt, 0, 1));

        if (g_frame.pressed.a) {
            if (!story_try_interact() && p->on_ground) {
                p->vel.y = JUMP_VEL;
                p->on_ground = false;
                sfx_play(SFX_JUMP);
            }
        }
        update_melee();
        update_spells(dt);

        /* Z: lock on to the nearest enemy, or recenter the camera */
        if (g_frame.pressed.z) {
            int t = enemy_find_target(p->pos, p->yaw, 18.0f);
            if (t >= 0) { p->lock_target = t; sfx_play(SFX_CURSOR); }
            else recenter_t = 0.35f;
        }
        if (g_frame.pressed.r) recenter_t = 0.35f;
    }
    if (!g_frame.held.z || !control) p->lock_target = -1;
    if (p->lock_target >= 0) {
        enemy_t *e = &g_enemies[p->lock_target];
        if (!e->active || e->hp <= 0 || dist_xz(e->pos, p->pos) > 26) p->lock_target = -1;
    }

    /* horizontal velocity */
    float accel = p->on_ground ? 45.0f : 18.0f;
    if (p->hurt_t > 0.8f) accel = 4.0f;    /* knocked back */
    p->vel.x = approachf(p->vel.x, want.x, accel * dt);
    p->vel.z = approachf(p->vel.z, want.z, accel * dt);
    p->vel.y -= GRAVITY * dt;

    vec3_t np = v3_add(p->pos, v3_scale(p->vel, dt));
    world_collide(&np, RADIUS);
    /* enemies are solid too */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &g_enemies[i];
        if (!e->active || e->hp <= 0) continue;
        float r = e->radius * 0.8f + RADIUS;
        float dx = np.x - e->pos.x, dz = np.z - e->pos.z;
        float d2 = dx * dx + dz * dz;
        if (d2 < r * r && d2 > 1e-6f && fabsf(np.y - e->pos.y) < 2.5f) {
            float d = sqrtf(d2);
            np.x = e->pos.x + dx / d * r;
            np.z = e->pos.z + dz / d * r;
        }
    }

    float ground = world_height(np.x, np.z);
    if (np.y <= ground) {
        np.y = ground;
        if (p->vel.y < 0) p->vel.y = 0;
        p->on_ground = true;
    } else if (p->on_ground && p->vel.y <= 0 && np.y - ground < 0.45f) {
        np.y = ground;   /* stick to slopes when walking down */
        p->vel.y = 0;
    } else {
        p->on_ground = false;
    }
    p->pos = np;

    p->speed = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
    p->anim_t += p->speed * dt * 2.4f;
    if (p->speed < 0.2f) p->anim_t += dt;
}

void player_render(void)
{
    player_t *p = &g_player;
    if (p->hurt_t > 0 && fmodf(p->hurt_t, 0.14f) < 0.05f && p->hp > 0) return;   /* i-frame blink */

    anim_t anim = ANIM_IDLE;
    float t = p->anim_t;
    if (p->hp <= 0) anim = ANIM_HURT;
    else if (p->attack_t > 0) { anim = ANIM_SWING; t = 1.0f - p->attack_t / 0.38f; }
    else if (p->charge_spell >= 0 || p->cast_t > 0) anim = ANIM_CAST;
    else if (p->speed > 4.5f) anim = ANIM_RUN;
    else if (p->speed > 0.3f) anim = ANIM_WALK;

    float flash = p->heal_fx > 0 ? p->heal_fx * 0.4f : 0;
    draw_human(p->older ? MDL_RUDEUS_OLDER : MDL_RUDEUS, p->pos, p->yaw, anim, t, flash);
}

/* ------------------------------------------------------------------ */
/* Camera                                                              */
/* ------------------------------------------------------------------ */

static vec3_t cam_desired(void)
{
    float h = g_player.older ? 1.3f : 1.1f;
    vec3_t tgt = v3_add(g_player.pos, v3(0, h, 0));
    float cp = cosf(g_cam.pitch);
    return v3_sub(tgt, v3(sinf(g_cam.yaw) * cp * g_cam.dist, -sinf(g_cam.pitch) * g_cam.dist, cosf(g_cam.yaw) * cp * g_cam.dist));
}

void camera_snap(void)
{
    g_cam.yaw = g_player.yaw;
    g_cam.dist = 7.0f;
    g_cam.pitch = 0.32f;
    g_cam.target = v3_add(g_player.pos, v3(0, g_player.older ? 1.3f : 1.1f, 0));
    g_cam.pos = cam_desired();
    float gh = world_height(g_cam.pos.x, g_cam.pos.z) + 0.8f;
    if (g_cam.pos.y < gh) g_cam.pos.y = gh;
}

void camera_update(float dt)
{
    g_cam.shake = fmaxf(0, g_cam.shake - dt * 1.5f);
    if (g_cam.override) return;

    bool control = !g_player.locked && !dialog_active();
    if (control) {
        if (g_frame.held.d_left)  g_cam.yaw += 2.2f * dt;
        if (g_frame.held.d_right) g_cam.yaw -= 2.2f * dt;
        if (g_frame.held.d_up)    g_cam.dist = fmaxf(4.0f, g_cam.dist - 6.0f * dt);
        if (g_frame.held.d_down)  g_cam.dist = fminf(11.0f, g_cam.dist + 6.0f * dt);
    }

    if (g_player.lock_target >= 0) {
        enemy_t *e = &g_enemies[g_player.lock_target];
        g_cam.yaw = angle_lerp(g_cam.yaw, yaw_towards(g_player.pos, e->pos), clampf(5.0f * dt, 0, 1));
    } else if (recenter_t > 0) {
        recenter_t -= dt;
        g_cam.yaw = angle_lerp(g_cam.yaw, g_player.yaw, clampf(12.0f * dt, 0, 1));
    } else if (g_player.speed > 0.5f) {
        /* lazily swing behind the player when moving sideways */
        float side = fabsf(sinf(wrap_angle(g_player.yaw - g_cam.yaw)));
        float facing_away = cosf(wrap_angle(g_player.yaw - g_cam.yaw)) > -0.3f ? 1.0f : 0.0f;
        g_cam.yaw = angle_lerp(g_cam.yaw, g_player.yaw, clampf(1.6f * dt * side * facing_away, 0, 1));
    }
    g_cam.yaw = wrap_angle(g_cam.yaw);

    vec3_t want = cam_desired();
    float gh = world_height(want.x, want.z) + 0.8f;
    if (want.y < gh) want.y = gh;
    float k = 1.0f - expf(-10.0f * dt);
    g_cam.pos = v3_lerp(g_cam.pos, want, k);
    vec3_t tgt = v3_add(g_player.pos, v3(0, g_player.older ? 1.3f : 1.1f, 0));
    if (g_player.lock_target >= 0) {
        enemy_t *e = &g_enemies[g_player.lock_target];
        tgt = v3_lerp(tgt, v3_add(e->pos, v3(0, 1.0f, 0)), 0.3f);
    }
    g_cam.target = v3_lerp(g_cam.target, tgt, 1.0f - expf(-14.0f * dt));
}
