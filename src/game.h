/*
 * Mushoku Tensei 64 - unofficial Nintendo 64 fan game.
 * Shared types, math helpers and the interfaces between modules.
 */
#ifndef GAME_H
#define GAME_H

#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define PI_F  3.14159265f
#define TAU_F 6.28318531f
#define RAD2DEG (180.0f / PI_F)

/* ------------------------------------------------------------------ */
/* Math                                                                */
/* ------------------------------------------------------------------ */

typedef struct { float x, y, z; } vec3_t;

static inline vec3_t v3(float x, float y, float z) { return (vec3_t){x, y, z}; }
static inline vec3_t v3_add(vec3_t a, vec3_t b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline vec3_t v3_sub(vec3_t a, vec3_t b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline vec3_t v3_scale(vec3_t a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(vec3_t a, vec3_t b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float v3_len(vec3_t a) { return sqrtf(v3_dot(a, a)); }
static inline vec3_t v3_norm(vec3_t a) {
    float l = v3_len(a);
    return l > 1e-5f ? v3_scale(a, 1.0f / l) : v3(0, 0, 0);
}
static inline vec3_t v3_lerp(vec3_t a, vec3_t b, float t) {
    return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}
static inline vec3_t v3_cross(vec3_t a, vec3_t b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float approachf(float v, float target, float step) {
    if (v < target) return fminf(v + step, target);
    return fmaxf(v - step, target);
}
static inline float wrap_angle(float a) {
    while (a > PI_F) a -= TAU_F;
    while (a < -PI_F) a += TAU_F;
    return a;
}
static inline float angle_lerp(float a, float b, float t) { return a + wrap_angle(b - a) * t; }
static inline float dist_xz(vec3_t a, vec3_t b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}
static inline float smoothstepf(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
static inline float frand(void) { return (rand() & 0xFFFF) / 65535.0f; }
static inline float frand_range(float a, float b) { return a + (b - a) * frand(); }
static inline float yaw_towards(vec3_t from, vec3_t to) { return atan2f(to.x - from.x, to.z - from.z); }

/* Colors are packed 0xRRGGBBAA */
static inline void gl_color(uint32_t c) {
    glColor4ub(c >> 24, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}
static inline color_t rgba(uint32_t c) {
    return RGBA32(c >> 24, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}
uint32_t color_mul(uint32_t c, float k);
uint32_t color_mix(uint32_t a, uint32_t b, float t);

/* ------------------------------------------------------------------ */
/* Frame / input                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    float time;     /* seconds since boot */
    float dt;       /* seconds since last frame (clamped) */
    joypad_buttons_t pressed, held, released;
    joypad_inputs_t in;
    float stick_x, stick_y; /* normalized -1..1 with deadzone */
    uint32_t frame;
} frame_t;
extern frame_t g_frame;

/* ------------------------------------------------------------------ */
/* Graphics (gfx.c)                                                    */
/* ------------------------------------------------------------------ */

typedef enum {
    TEX_GRASS, TEX_DIRT, TEX_WOOD, TEX_STONE, TEX_ROOF, TEX_BARK,
    TEX_LEAVES, TEX_PLASTER, TEX_WATER, TEX_GLOW, TEX_COUNT,
    TEX_NONE = 255
} tex_id_t;

enum { FONT_BODY = 1, FONT_OUTLINE = 2, FONT_TITLE = 3 };
/* font styles (same ids registered on every font) */
enum { STYLE_WHITE, STYLE_GOLD, STYLE_GRAY, STYLE_BLUE, STYLE_RED, STYLE_GREEN, STYLE_PINK, STYLE_DARK };

typedef enum { PRIM_CUBE, PRIM_SPHERE, PRIM_CYLINDER, PRIM_CONE, PRIM_DISC, PRIM_OCTA, PRIM_FRUSTUM, PRIM_SBOX, PRIM_QUAD, PRIM_COUNT } prim_t;

typedef struct {
    vec3_t pos, target;
    float yaw, pitch, dist;
    float shake;
    bool override;          /* cutscene camera */
    vec3_t ov_pos, ov_target;
    float fov;
    /* derived each frame */
    vec3_t fwd, right, up;
    float viewproj[16];
} camera_t;
extern camera_t g_cam;

void gfx_init(void);
void gfx_bind(tex_id_t t);              /* TEX_NONE disables texturing */
void gfx_prim(prim_t p);                /* draw unit primitive at current matrix */
void gfx_part(prim_t p, uint32_t color, float x, float y, float z, float sx, float sy, float sz);
void gfx_part_rot(prim_t p, uint32_t color, float x, float y, float z,
                  float rx, float ry, float rz, float sx, float sy, float sz);
void gfx_begin_frame_3d(uint32_t clear_color);
void gfx_setup_camera(void);
bool gfx_project(vec3_t p, float *sx, float *sy);
bool gfx_visible(vec3_t p, float radius, float max_dist);
void gfx_billboard(vec3_t p, float size, uint32_t color);
void gfx_billboards_begin(void);
void gfx_billboards_end(void);
void gfx_shadow(vec3_t p, float radius, float ground_y);
void gfx_end_frame_3d(void);

/*
 * Display lists. These are libdragon RSP command blocks recorded from GL
 * calls. We keep the block pointers ourselves instead of going through
 * glGenLists/glCallList ids (the id map misbehaved with hundreds of lists).
 */
typedef rspq_block_t *dlist_t;
static inline void dl_begin(void) { rspq_block_begin(); }
static inline dlist_t dl_end(void) { return rspq_block_end(); }
static inline void dl_call(dlist_t l) { if (l) rspq_block_run(l); }
void dl_free(dlist_t l);    /* released a few frames later */

/* interleaved vertex used for indexed drawing */
typedef struct { float p[3]; float t[2]; float n[3]; uint32_t c; } gfx_vtx_t;
void gfx_draw_indexed(const void *verts, int nv, const uint16_t *idx, int ni, bool color, bool normals);

/* mesh builder for compiling static props into display lists */
void mb_begin(uint32_t color);       /* color 0: use the current GL color */
void mb_set_color(uint32_t color);
void mb_end(void);
void mb_box(vec3_t mn, vec3_t mx, float ts);
void mb_box_rot(vec3_t center, vec3_t half, float rx, float ry, float rz, float ts);
void mb_cyl(vec3_t base, float r, float h, int seg, float ts, bool caps);
void mb_cone(vec3_t base, float r, float h, int seg, float ts);
void mb_blob(vec3_t center, vec3_t radii, int seg, int rings, float ts);
void mb_tri(vec3_t a, vec3_t b, vec3_t c, vec3_t outward, float ts);
void mb_quad(vec3_t a, vec3_t b, vec3_t c, vec3_t d, vec3_t outward, float ts);
void mb_prim(prim_t p, uint32_t color, float x, float y, float z, float rx, float ry, float rz,
             float sx, float sy, float sz);       /* unit primitive with a local transform */
void mb_base(float x, float y, float z, float ry, float scale);   /* transform for everything emitted */
void mb_base_identity(void);
void mb_bake_lighting(bool on, vec3_t sun_dir, uint32_t sun, uint32_t ambient);
int  mb_emitted(void);
void mb_skip_bottoms(bool on);
void mb_set_dry(bool on);             /* count geometry without issuing GL calls */        /* drop downward faces (they rest on the ground) */

/* 2D helpers, call after gfx_end_frame_3d */
void ui_rect(int x0, int y0, int x1, int y1, uint32_t color);      /* opaque */
void ui_rect_alpha(int x0, int y0, int x1, int y1, uint32_t color);/* translucent */
void ui_circle(int cx, int cy, uint32_t color, bool ring);
void ui_text(int font, int style, int x, int y, const char *s);
void ui_text_center(int font, int style, int y, const char *s);
void ui_text_box(int font, int style, int x, int y, int w, int max_chars, const char *s);
void ui_logo(int x, int y);
void ui_fade(float alpha, uint32_t color);

/* ------------------------------------------------------------------ */
/* Character / creature models (models.c)                              */
/* ------------------------------------------------------------------ */

typedef enum {
    MDL_RUDEUS, MDL_ROXY, MDL_PAUL, MDL_ZENITH, MDL_LILIA, MDL_SYLPHIE,
    MDL_SYLPHIE_OLDER, MDL_RUIJERD, MDL_VILLAGER_M, MDL_VILLAGER_F,
    MDL_BULLY_A, MDL_BULLY_B, MDL_BULLY_C, MDL_RUDEUS_OLDER,
    MDL_HUMAN_COUNT
} human_model_t;

typedef enum { ANIM_IDLE, ANIM_WALK, ANIM_RUN, ANIM_SWING, ANIM_CAST, ANIM_HURT, ANIM_WAVE, ANIM_SIT } anim_t;

void models_init(void);
void draw_human(human_model_t m, vec3_t pos, float yaw, anim_t anim, float t, float flash);
void draw_wolf(vec3_t pos, float yaw, float t, bool moving, float flash, float scale);
void draw_boar(vec3_t pos, float yaw, float t, bool moving, float flash, float scale, bool boss);
void draw_target(vec3_t pos, float yaw, float flash);
void draw_wisp(vec3_t pos, float t, float flash);
void draw_crystal(vec3_t pos, float t, float flash, float scale);

/* ------------------------------------------------------------------ */
/* World (world.c)                                                     */
/* ------------------------------------------------------------------ */

typedef enum { MAP_VILLAGE, MAP_FOREST, MAP_PLATEAU, MAP_DEMON, MAP_COUNT } map_id_t;

typedef enum {
    PROP_HOUSE, PROP_HOUSE_BIG, PROP_TREE, PROP_PINE, PROP_BIGTREE, PROP_ROCK,
    PROP_FENCE, PROP_WELL, PROP_BRAZIER, PROP_BOULDER, PROP_GATE, PROP_CART,
    PROP_CRATE, PROP_SIGN, PROP_DEADTREE, PROP_CRAG, PROP_HAY, PROP_BENCH, PROP_COUNT
} prop_type_t;

typedef struct {
    uint8_t type;
    uint8_t flags;      /* PF_* */
    float x, y, z;
    float rot;          /* radians */
    float scale;
    float t;            /* per-prop timer for effects */
} prop_t;

#define PF_LIT      (1 << 0)   /* brazier burning */
#define PF_BROKEN   (1 << 1)   /* boulder destroyed */
#define PF_NOCOLL   (1 << 2)   /* no collision */

#define MAP_HALF 48.0f
#define WATER_NONE -100.0f

typedef struct {
    map_id_t id;
    const char *name;
    uint32_t sky_top, sky_horizon, fog_color;
    float fog_start, fog_end;
    uint32_t sun_color, ambient;
    vec3_t sun_dir;
    float water_level;
    bool storm;      /* rain + lightning */
    float storm_t;
    float lightning;
} world_t;
extern world_t g_world;

void world_init(void);
void world_load(map_id_t id);
void world_update(float dt);
void world_render(void);
void world_render_sky(void);
void world_render_fx(void);            /* rain etc, after opaque geometry */
uint32_t world_clear_color(void);
void world_restore_ambient(void);        /* undo a hit-flash ambient boost */
float world_height(float x, float z);
bool world_collide(vec3_t *pos, float radius);       /* push out of solids, returns true if hit */
bool world_solid_at(vec3_t p, float radius);          /* projectile test */
int  world_projectile_hit(vec3_t p, float radius, int spell); /* special interactions */
int  world_count_props(prop_type_t type, uint8_t flag_mask, uint8_t flag_value);
prop_t *world_find_prop(prop_type_t type, int index);
void world_set_palette_calamity(bool on);

/* ------------------------------------------------------------------ */
/* Player (player.c)                                                   */
/* ------------------------------------------------------------------ */

typedef enum { SPELL_WATER, SPELL_FIRE, SPELL_STONE, SPELL_HEAL, SPELL_COUNT } spell_id_t;

typedef struct {
    vec3_t pos, vel;
    float yaw;
    float hp, max_hp;
    float mp, max_mp;
    bool on_ground;
    float anim_t;
    float speed;
    float attack_t;
    float cast_t;
    int   charge_spell;     /* -1 when not charging */
    float charge;
    float hurt_t;
    float heal_fx;
    bool  locked;           /* no control (cutscene / dialog) */
    int   lock_target;      /* enemy index or -1 */
    bool  older;            /* chapter 5 model */
    float no_mp_flash;
} player_t;
extern player_t g_player;

extern const char *const SPELL_NAMES[SPELL_COUNT];
extern const uint32_t SPELL_COLORS[SPELL_COUNT];
extern const float SPELL_COST[SPELL_COUNT];

void player_reset(vec3_t pos, float yaw);
void player_update(float dt);
void player_render(void);
void player_damage(float amount, vec3_t from);
void camera_update(float dt);
void camera_snap(void);

/* ------------------------------------------------------------------ */
/* Combat: enemies, projectiles, particles (combat.c)                  */
/* ------------------------------------------------------------------ */

typedef enum { EN_TARGET, EN_BULLY, EN_WOLF, EN_BOAR, EN_GREATBOAR, EN_WISP, EN_CRYSTAL, EN_CORE, EN_COUNT } enemy_type_t;

typedef struct {
    bool active;
    uint8_t type;
    uint8_t state;
    uint8_t variant;
    vec3_t pos, vel, home;
    float yaw;
    float hp, max_hp;
    float t, anim_t, hurt_t, atk_cd;
    float radius;
} enemy_t;

#define MAX_ENEMIES 24
extern enemy_t g_enemies[MAX_ENEMIES];

int  enemy_spawn(enemy_type_t type, float x, float z, uint8_t variant);
void enemies_clear(void);
int  enemies_alive(int type);         /* -1 = any type */
int  enemies_killed_total(void);
void enemies_reset_kill_count(void);
int  enemy_find_target(vec3_t from, float yaw, float range);
bool enemy_hit(int idx, float dmg, vec3_t from, int spell);

typedef enum { PRJ_WATER, PRJ_FIRE, PRJ_STONE, PRJ_MUD, PRJ_BOLT, PRJ_BOSS } prj_type_t;
void projectile_spawn(prj_type_t type, vec3_t pos, vec3_t vel, bool from_player, float power, int homing_target);
void projectiles_clear(void);

void particles_burst(vec3_t pos, uint32_t color, int count, float speed, float size, float life, float gravity);
void particle_spawn(vec3_t pos, vec3_t vel, uint32_t color, float size, float life, float gravity);
void particles_clear(void);

void combat_update(float dt);
void combat_render(void);
void combat_render_fx(void);

/* ------------------------------------------------------------------ */
/* NPCs + story (story.c)                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    /* Chapter 1: A New Life */
    ST_INTRO, ST_TALK_ZENITH, ST_MEET_ROXY, ST_TARGETS, ST_BRAZIERS, ST_BOULDER,
    /* Chapter 2: Sylphiette */
    ST_GO_HILL, ST_BULLIES, ST_TALK_SYLPHIE, ST_RETURN_ZENITH,
    /* Chapter 3: Beyond the Door */
    ST_ROXY_EXAM, ST_FEAR, ST_CUMULONIMBUS, ST_FAREWELL,
    /* Chapter 4: Fittoa Forest */
    ST_TALK_PAUL, ST_FOREST, ST_BOSS_BOAR, ST_FOREST_DONE,
    /* Chapter 5: The Mana Calamity */
    ST_CALAMITY, ST_CRYSTALS, ST_CORE, ST_DEMON_WAKE, ST_RUIJERD, ST_THE_END,
    ST_COUNT
} story_step_t;

void story_init(void);
void story_new_game(void);
void story_start_from_save(void);
void story_update(float dt);
void story_render(void);
void story_render_ui(void);
bool story_try_interact(void);       /* A pressed near something */
const char *story_objective(void);
const char *story_chapter_name(void);
int  story_chapter(void);
void story_on_enemy_killed(int type);
void story_on_player_dead(void);
bool story_wants_credits(void);
const char *story_interact_hint(void);
void story_respawn(void);
void story_render_fx(void);           /* NPC shadows */
bool story_card_active(void);
void story_debug_start(int step);     /* test builds: jump straight to a story step */

/* ------------------------------------------------------------------ */
/* Dialogue / HUD / menus (ui.c)                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *speaker;   /* NULL for narration */
    const char *text;
} dline_t;

typedef void (*dialog_done_fn)(void);

void dialog_start(const dline_t *lines, int count, dialog_done_fn done);
bool dialog_active(void);
void dialog_update(float dt);
void dialog_render(void);

void hud_banner(const char *title, const char *subtitle, float seconds);
void hud_toast(const char *text, float seconds);
void hud_update(float dt);
void hud_render(void);
void hud_boss_bar(const char *name, float frac);   /* call each frame while boss alive */

/* ------------------------------------------------------------------ */
/* Audio (audio.c)                                                     */
/* ------------------------------------------------------------------ */

typedef enum { MUS_NONE, MUS_TITLE, MUS_VILLAGE, MUS_LESSON, MUS_BATTLE, MUS_BOSS, MUS_SORROW, MUS_FANFARE, MUS_COUNT } music_id_t;
typedef enum {
    SFX_CURSOR, SFX_CONFIRM, SFX_JUMP, SFX_SWING, SFX_WATER, SFX_FIRE, SFX_STONE, SFX_HEAL,
    SFX_HIT, SFX_HURT, SFX_EXPLODE, SFX_TEXT, SFX_THUNDER, SFX_CHARGE, SFX_LEARN, SFX_STEP, SFX_FAIL,
    SFX_COUNT
} sfx_id_t;

void sound_init(void);
void music_play(music_id_t m);
music_id_t music_current(void);
void sfx_play(sfx_id_t s);

/* ------------------------------------------------------------------ */
/* Save (save.c)                                                       */
/* ------------------------------------------------------------------ */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  step;
    uint8_t  spells;     /* bitmask of known spells */
    uint8_t  max_hp;
    uint8_t  cleared;    /* game finished at least once */
    uint8_t  pad[2];
    uint8_t  checksum;
} save_t;
extern save_t g_save;

void save_init(void);
bool save_available(void);
bool save_exists(void);
bool save_load(void);
bool save_write(void);
void save_reset(void);

/* ------------------------------------------------------------------ */
/* Main (main.c)                                                       */
/* ------------------------------------------------------------------ */

typedef enum { GS_TITLE, GS_PLAY, GS_PAUSE, GS_GAMEOVER, GS_CREDITS } game_state_t;
extern game_state_t g_state;
void game_fade_to(void (*callback)(void));   /* fade out, run callback, fade in */
bool game_fading(void);
void game_rumble(float seconds);

#endif
