/*
 * World: heightmap terrain, static props, sky, water and weather for the
 * four maps (Buena Village, Fittoa Forest, the plateau, the Demon Continent).
 */
#include "game.h"

world_t g_world;

#define GRID        32
#define VERTS       (GRID + 1)
#define CELL        (2.0f * MAP_HALF / GRID)
#define CHUNKS      4
#define CHUNK_CELLS (GRID / CHUNKS)
#define MAX_PROPS   200
#define BOUND       46.0f

static float heights[VERTS][VERTS];
static uint32_t tcolors[VERTS][VERTS];
static dlist_t chunk_lists[CHUNKS * CHUNKS], sky_list;
static bool lists_ready;
static bool lists_dirty;
static bool baked_storm;
static bool calamity;
static tex_id_t ground_tex;
static uint32_t map_seed;

static prop_t props[MAX_PROPS];
static int nprops;

/* ------------------------------------------------------------------ */
/* Prop geometry                                                       */
/* ------------------------------------------------------------------ */

enum { COL_NONE, COL_CIRCLE, COL_BOX, COL_GATE };

typedef struct {
    float cull_r;
    uint8_t col;
    float cr, chx, chz;   /* collider radius / box half extents */
    float top;            /* height of the solid part (projectiles fly over lower things) */
} prop_info_t;

static const prop_info_t PROP_INFO[PROP_COUNT] = {
    [PROP_HOUSE]     = { 4.9f, COL_BOX, 0, 3.15f, 2.65f, 5.3f },
    [PROP_HOUSE_BIG] = { 6.7f, COL_BOX, 0, 4.65f, 3.65f, 7.4f },
    [PROP_TREE]      = { 3.0f, COL_CIRCLE, 0.45f, 0, 0, 6.0f },
    [PROP_PINE]      = { 3.0f, COL_CIRCLE, 0.4f, 0, 0, 6.0f },
    [PROP_BIGTREE]   = { 7.0f, COL_CIRCLE, 1.2f, 0, 0, 12.0f },
    [PROP_ROCK]      = { 1.5f, COL_CIRCLE, 0.85f, 0, 0, 0.9f },
    [PROP_FENCE]     = { 4.2f, COL_BOX, 0, 4.0f, 0.2f, 1.3f },
    [PROP_WELL]      = { 2.0f, COL_CIRCLE, 1.1f, 0, 0, 1.0f },
    [PROP_BRAZIER]   = { 1.0f, COL_CIRCLE, 0.45f, 0, 0, 1.3f },
    [PROP_BOULDER]   = { 3.0f, COL_CIRCLE, 2.45f, 0, 0, 3.4f },
    [PROP_GATE]      = { 4.5f, COL_GATE, 0.35f, 0, 0, 4.2f },
    [PROP_CART]      = { 3.0f, COL_BOX, 0, 1.1f, 1.5f, 1.3f },
    [PROP_CRATE]     = { 1.0f, COL_BOX, 0, 0.55f, 0.55f, 0.9f },
    [PROP_SIGN]      = { 1.0f, COL_CIRCLE, 0.2f, 0, 0, 1.6f },
    [PROP_DEADTREE]  = { 3.0f, COL_CIRCLE, 0.4f, 0, 0, 5.0f },
    [PROP_CRAG]      = { 3.5f, COL_CIRCLE, 1.4f, 0, 0, 5.0f },
    [PROP_HAY]       = { 1.2f, COL_CIRCLE, 0.75f, 0, 0, 1.0f },
    [PROP_BENCH]     = { 1.2f, COL_BOX, 0, 1.0f, 0.3f, 0.6f },
};

/* The texture pass being emitted. Each prop emits only the parts that use
   this texture, so a whole chunk's props can be batched per texture. */
static tex_id_t cur_pass;

static bool part(tex_id_t tex, uint32_t color)
{
    if (tex != cur_pass) return false;
    mb_set_color(color);
    return true;
}

static const tex_id_t PASS_ORDER[] = { TEX_PLASTER, TEX_ROOF, TEX_WOOD, TEX_STONE, TEX_BARK, TEX_LEAVES, TEX_GRASS, TEX_NONE };
#define NPASS ((int)(sizeof(PASS_ORDER) / sizeof(PASS_ORDER[0])))

static void emit_house(float w, float d, float hw, float hr, bool big, uint32_t roof)
{
    const float x = w * 0.5f, z = d * 0.5f;
    if (part(TEX_PLASTER, 0xEEE6D2FF)) {
        mb_box(v3(-x, -1.0f, -z), v3(x, hw, z), 2.0f);
        mb_tri(v3(-x, hw, z), v3(x, hw, z), v3(0, hr, z), v3(0, 0, 1), 2.0f);
        mb_tri(v3(-x, hw, -z), v3(x, hw, -z), v3(0, hr, -z), v3(0, 0, -1), 2.0f);
    }
    if (part(TEX_ROOF, roof)) {
        float ox = x + 0.6f, ey = hw - 0.25f, ry = hr + 0.25f;
        float ang = atan2f(ry - ey, ox);
        float half = sqrtf(ox * ox + (ry - ey) * (ry - ey)) * 0.5f + 0.1f;
        mb_box_rot(v3(-ox * 0.5f, (ey + ry) * 0.5f, 0), v3(half, 0.14f, z + 0.5f), 0, 0, ang, 1.5f);
        mb_box_rot(v3(ox * 0.5f, (ey + ry) * 0.5f, 0), v3(half, 0.14f, z + 0.5f), 0, 0, -ang, 1.5f);
    }
    if (part(TEX_WOOD, 0x5E4029FF)) {
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sz = -1; sz <= 1; sz += 2)
                mb_box(v3(sx * x - 0.18f, -0.5f, sz * z - 0.18f), v3(sx * x + 0.18f, hw, sz * z + 0.18f), 1.0f);
        mb_box(v3(-x - 0.05f, hw * 0.48f, z - 0.05f), v3(x + 0.05f, hw * 0.48f + 0.22f, z + 0.12f), 1.0f);
        mb_box(v3(-x - 0.05f, hw * 0.48f, -z - 0.12f), v3(x + 0.05f, hw * 0.48f + 0.22f, -z + 0.05f), 1.0f);
        mb_set_color(0x7A5234FF);
        mb_box(v3(-0.65f, -0.2f, z), v3(0.65f, 2.1f, z + 0.12f), 1.0f);
        if (big) {
            mb_set_color(0x8A6A48FF);
            mb_box(v3(-2.2f, -0.6f, z), v3(2.2f, 0.15f, z + 2.0f), 1.0f);
            mb_box(v3(-2.0f, 0.1f, z + 1.7f), v3(-1.8f, 2.6f, z + 1.9f), 1.0f);
            mb_box(v3(1.8f, 0.1f, z + 1.7f), v3(2.0f, 2.6f, z + 1.9f), 1.0f);
            mb_box_rot(v3(0, 2.75f, z + 1.0f), v3(2.4f, 0.1f, 1.2f), -0.25f, 0, 0, 1.0f);
        }
    }
    if (part(TEX_NONE, 0x2A3448FF)) {
        float wy0 = hw * 0.2f + 0.4f, wy1 = wy0 + 0.9f;
        for (int row = 0; row < (big ? 2 : 1); row++) {
            float y0 = wy0 + row * hw * 0.48f, y1 = wy1 + row * hw * 0.48f;
            for (int sd = -1; sd <= 1; sd += 2) {
                float cx = sd * x * 0.55f;
                mb_quad(v3(cx - 0.45f, y0, z + 0.02f), v3(cx + 0.45f, y0, z + 0.02f),
                        v3(cx + 0.45f, y1, z + 0.02f), v3(cx - 0.45f, y1, z + 0.02f), v3(0, 0, 1), 1.0f);
                float cz = sd * z * 0.4f;
                mb_quad(v3(x + 0.02f, y0, cz - 0.45f), v3(x + 0.02f, y0, cz + 0.45f),
                        v3(x + 0.02f, y1, cz + 0.45f), v3(x + 0.02f, y1, cz - 0.45f), v3(1, 0, 0), 1.0f);
                mb_quad(v3(-x - 0.02f, y0, cz - 0.45f), v3(-x - 0.02f, y0, cz + 0.45f),
                        v3(-x - 0.02f, y1, cz + 0.45f), v3(-x - 0.02f, y1, cz - 0.45f), v3(-1, 0, 0), 1.0f);
            }
        }
    }
    if (part(TEX_STONE, 0x8A847AFF))
        mb_box(v3(x * 0.45f, hw, -z * 0.5f), v3(x * 0.45f + 0.8f, hr + 0.6f, -z * 0.5f + 0.8f), 1.0f);
}

static void emit_prop(const prop_t *p, uint32_t tint)
{
    switch (p->type) {
    case PROP_HOUSE:     emit_house(6.0f, 5.0f, 3.2f, 5.3f, false, tint); break;
    case PROP_HOUSE_BIG: emit_house(9.0f, 7.0f, 4.4f, 7.4f, true, tint); break;
    case PROP_TREE:
        if (part(TEX_BARK, 0x6A4A30FF)) mb_cyl(v3(0, -0.3f, 0), 0.32f, 3.4f, 4, 1.5f, false);
        if (part(TEX_LEAVES, tint)) mb_blob(v3(0, 4.1f, 0), v3(2.0f, 1.9f, 2.0f), 6, 3, 1.5f);
        break;
    case PROP_PINE:
        if (part(TEX_BARK, 0x5A4030FF)) mb_cyl(v3(0, -0.3f, 0), 0.25f, 1.8f, 4, 1.5f, false);
        if (part(TEX_LEAVES, tint)) {
            mb_cone(v3(0, 1.2f, 0), 1.9f, 2.9f, 5, 1.5f);
            mb_cone(v3(0, 3.0f, 0), 1.3f, 2.6f, 5, 1.5f);
        }
        break;
    case PROP_BIGTREE:
        if (part(TEX_BARK, 0x6A4A30FF)) {
            mb_cyl(v3(0, -0.5f, 0), 0.95f, 6.5f, 7, 2.0f, false);
            mb_box_rot(v3(1.4f, 5.6f, 0), v3(1.6f, 0.25f, 0.25f), 0, 0, 0.6f, 1.0f);
            mb_box_rot(v3(-1.3f, 6.0f, 0.4f), v3(1.5f, 0.22f, 0.22f), 0, 0.3f, -0.6f, 1.0f);
        }
        if (part(TEX_LEAVES, tint)) {
            mb_blob(v3(0, 8.2f, 0), v3(5.0f, 3.2f, 5.0f), 7, 4, 2.0f);
            mb_blob(v3(2.6f, 9.6f, 1.0f), v3(2.8f, 2.2f, 2.8f), 5, 3, 2.0f);
            mb_blob(v3(-2.4f, 9.4f, -1.4f), v3(2.8f, 2.2f, 2.8f), 5, 3, 2.0f);
        }
        break;
    case PROP_ROCK:
        if (part(TEX_STONE, 0x8A8680FF)) mb_blob(v3(0, 0.15f, 0), v3(1.0f, 0.75f, 0.85f), 5, 3, 1.0f);
        break;
    case PROP_FENCE:
        if (part(TEX_WOOD, 0x8A6A44FF)) {
            for (int i = -1; i <= 1; i++)
                mb_box(v3(i * 3.9f - 0.1f, -0.4f, -0.1f), v3(i * 3.9f + 0.1f, 1.35f, 0.1f), 1.0f);
            for (int r = 0; r < 2; r++) {
                float y0 = 0.45f + r * 0.5f, y1 = y0 + 0.15f;
                mb_quad(v3(-4, y0, 0.06f), v3(4, y0, 0.06f), v3(4, y1, 0.06f), v3(-4, y1, 0.06f), v3(0, 0, 1), 1.0f);
                mb_quad(v3(-4, y0, -0.06f), v3(4, y0, -0.06f), v3(4, y1, -0.06f), v3(-4, y1, -0.06f), v3(0, 0, -1), 1.0f);
                mb_quad(v3(-4, y1, -0.06f), v3(4, y1, -0.06f), v3(4, y1, 0.06f), v3(-4, y1, 0.06f), v3(0, 1, 0), 1.0f);
            }
        }
        break;
    case PROP_WELL:
        if (part(TEX_STONE, 0x9A948AFF)) mb_cyl(v3(0, -0.3f, 0), 1.0f, 1.2f, 8, 1.0f, false);
        if (part(TEX_NONE, 0x203048FF)) mb_cyl(v3(0, 0.7f, 0), 0.85f, 0.01f, 8, 1.0f, true);
        if (part(TEX_WOOD, 0x6A4A30FF)) {
            mb_box(v3(-0.95f, 0.5f, -0.1f), v3(-0.75f, 2.6f, 0.1f), 1.0f);
            mb_box(v3(0.75f, 0.5f, -0.1f), v3(0.95f, 2.6f, 0.1f), 1.0f);
            mb_box(v3(-1.0f, 2.1f, -0.06f), v3(1.0f, 2.22f, 0.06f), 1.0f);
        }
        if (part(TEX_ROOF, tint)) {
            mb_box_rot(v3(0, 2.75f, 0.55f), v3(1.3f, 0.08f, 0.7f), 0.55f, 0, 0, 1.0f);
            mb_box_rot(v3(0, 2.75f, -0.55f), v3(1.3f, 0.08f, 0.7f), -0.55f, 0, 0, 1.0f);
        }
        break;
    case PROP_BRAZIER:
        if (part(TEX_STONE, 0x7A7470FF)) {
            mb_box(v3(-0.25f, -0.3f, -0.25f), v3(0.25f, 1.0f, 0.25f), 1.0f);
            mb_cyl(v3(0, 1.0f, 0), 0.5f, 0.28f, 7, 1.0f, false);
        }
        if (part(TEX_NONE, 0x1A1410FF)) mb_cyl(v3(0, 1.18f, 0), 0.42f, 0.01f, 7, 1.0f, true);
        break;
    case PROP_BOULDER:
        if (part(TEX_STONE, 0x8E8678FF)) {
            mb_blob(v3(0, 1.3f, 0), v3(2.5f, 2.1f, 2.2f), 7, 5, 1.2f);
            mb_blob(v3(1.6f, 0.6f, 1.0f), v3(1.1f, 0.9f, 1.0f), 5, 3, 1.2f);
        }
        break;
    case PROP_GATE:
        if (part(TEX_WOOD, 0x6A4A30FF)) {
            mb_box(v3(-3.4f, -0.5f, -0.25f), v3(-2.9f, 4.2f, 0.25f), 1.0f);
            mb_box(v3(2.9f, -0.5f, -0.25f), v3(3.4f, 4.2f, 0.25f), 1.0f);
            mb_box(v3(-4.0f, 3.7f, -0.3f), v3(4.0f, 4.2f, 0.3f), 1.0f);
            mb_set_color(0xA88A5AFF);
            mb_box(v3(-1.6f, 2.8f, -0.1f), v3(1.6f, 3.6f, 0.1f), 1.0f);
        }
        break;
    case PROP_CART:
        if (part(TEX_WOOD, 0x9A7448FF)) {
            mb_box(v3(-0.9f, 0.6f, -1.4f), v3(0.9f, 1.25f, 1.4f), 1.0f);
            mb_box(v3(-0.1f, 0.6f, 1.4f), v3(0.1f, 0.75f, 2.8f), 1.0f);
            mb_set_color(0x5A4030FF);
            for (int sd = -1; sd <= 1; sd += 2) {
                mb_box_rot(v3(sd * 1.0f, 0.55f, 0), v3(0.08f, 0.55f, 0.55f), 0, 0, 0, 1.0f);
                mb_box_rot(v3(sd * 1.0f, 0.55f, 0), v3(0.08f, 0.55f, 0.55f), 0.785f, 0, 0, 1.0f);
            }
        }
        break;
    case PROP_CRATE:
        if (part(TEX_WOOD, 0xA87C4CFF)) mb_box(v3(-0.5f, -0.1f, -0.5f), v3(0.5f, 0.9f, 0.5f), 1.0f);
        break;
    case PROP_SIGN:
        if (part(TEX_WOOD, 0x7A5A38FF)) {
            mb_box(v3(-0.08f, -0.3f, -0.08f), v3(0.08f, 1.5f, 0.08f), 1.0f);
            mb_set_color(0xB08C5CFF);
            mb_box(v3(-0.7f, 1.0f, -0.05f), v3(0.7f, 1.6f, 0.07f), 1.0f);
        }
        break;
    case PROP_DEADTREE:
        if (part(TEX_BARK, 0x4A3A30FF)) {
            mb_cone(v3(0, -0.3f, 0), 0.45f, 5.0f, 5, 1.0f);
            mb_box_rot(v3(0.7f, 2.8f, 0), v3(0.9f, 0.1f, 0.1f), 0, 0, 0.7f, 1.0f);
            mb_box_rot(v3(-0.6f, 3.4f, 0.2f), v3(0.8f, 0.09f, 0.09f), 0, 0.5f, -0.8f, 1.0f);
            mb_box_rot(v3(0.1f, 2.0f, -0.6f), v3(0.7f, 0.08f, 0.08f), 0, 1.6f, 0.5f, 1.0f);
        }
        break;
    case PROP_CRAG:
        if (part(TEX_STONE, tint)) {
            mb_cone(v3(0, -0.5f, 0), 1.6f, 5.5f, 6, 1.5f);
            mb_blob(v3(0.6f, 0.3f, 0.4f), v3(1.3f, 1.0f, 1.2f), 5, 3, 1.5f);
        }
        break;
    case PROP_HAY:
        if (part(TEX_GRASS, 0xE0C060FF)) mb_cyl(v3(0, -0.2f, 0), 0.75f, 1.2f, 7, 1.0f, true);
        break;
    case PROP_BENCH:
        if (part(TEX_WOOD, 0x8A6A44FF)) {
            mb_box(v3(-1.0f, 0.45f, -0.25f), v3(1.0f, 0.6f, 0.25f), 1.0f);
            mb_box(v3(-0.9f, -0.2f, -0.2f), v3(-0.75f, 0.45f, 0.2f), 1.0f);
            mb_box(v3(0.75f, -0.2f, -0.2f), v3(0.9f, 0.45f, 0.2f), 1.0f);
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Noise + map shapes                                                  */
/* ------------------------------------------------------------------ */

static float hash2(int x, int z)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + map_seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFF) / 65535.0f;
}

static float vnoise(float x, float z)
{
    int xi = (int)floorf(x), zi = (int)floorf(z);
    float fx = x - xi, fz = z - zi;
    fx = fx * fx * (3 - 2 * fx);
    fz = fz * fz * (3 - 2 * fz);
    float a = hash2(xi, zi), b = hash2(xi + 1, zi), c = hash2(xi, zi + 1), d = hash2(xi + 1, zi + 1);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fz);
}

static float fbm(float x, float z)
{
    return vnoise(x, z) * 0.55f + vnoise(x * 2.03f + 17, z * 2.03f + 9) * 0.3f + vnoise(x * 4.1f + 3, z * 4.1f + 31) * 0.15f;
}

typedef struct { float x0, z0, x1, z1; } seg_t;

static const seg_t VILLAGE_PATHS[] = {
    { 0, 47, 0, 6 }, { 0, 6, 0, -24 }, { 0, -24, 0, -33 },
    { 0, 4, -47, 2 }, { -8, 4, -13, 4 }, { 0, 6, 24, 6 },
    { 0, 22, -8, 22 }, { 0, 22, 8, 22 }, { 0, 34, -7, 34 }, { 0, 34, 7, 34 },
};
static const seg_t FOREST_PATHS[] = { { 47, 0, 10, 0 }, { 10, 0, 0, 3 } };
static const seg_t PLATEAU_PATHS[] = { { 0, 47, 0, 12 } };
static const seg_t DEMON_PATHS[] = { { 0, 30, 0, -4 } };

static const seg_t *paths;
static int npaths;

static float seg_dist(const seg_t *s, float x, float z)
{
    float dx = s->x1 - s->x0, dz = s->z1 - s->z0;
    float l2 = dx * dx + dz * dz;
    float t = l2 > 0 ? clampf(((x - s->x0) * dx + (z - s->z0) * dz) / l2, 0, 1) : 0;
    float px = s->x0 + dx * t - x, pz = s->z0 + dz * t - z;
    return sqrtf(px * px + pz * pz);
}

static float path_dist(float x, float z)
{
    float d = 1e9f;
    for (int i = 0; i < npaths; i++) d = fminf(d, seg_dist(&paths[i], x, z));
    return d;
}

static float rim(float x, float z, float start, float amount)
{
    float e = fmaxf(fabsf(x), fabsf(z));
    return smoothstepf(start, MAP_HALF, e) * amount;
}

static float shape_height(float x, float z)
{
    float pd = path_dist(x, z);
    switch (g_world.id) {
    case MAP_VILLAGE: {
        float flat = smoothstepf(2.5f, 11.0f, pd);
        float h = (fbm(x / 14.0f, z / 14.0f) - 0.5f) * 3.0f * flat;
        float dh = sqrtf(x * x + (z + 38) * (z + 38));
        h += 6.5f * (1.0f - smoothstepf(4.5f, 17.0f, dh));
        float dp = sqrtf((x + 30) * (x + 30) + (z - 30) * (z - 30));
        h -= 2.8f * (1.0f - smoothstepf(3.0f, 8.5f, dp));
        float r = rim(x, z, 36.0f, 7.0f);
        if (x < -30) r *= smoothstepf(3.0f, 9.0f, fabsf(z - 2));     /* west road */
        if (z > 30) r *= 0.0f;                                       /* south is fenced */
        return h + r;
    }
    case MAP_FOREST: {
        float flat = smoothstepf(2.5f, 9.0f, pd);
        float dc = sqrtf(x * x + z * z);
        flat = fminf(flat, smoothstepf(11.0f, 17.0f, dc));
        float h = (fbm(x / 11.0f, z / 11.0f) - 0.5f) * 7.0f * flat;
        float r = rim(x, z, 34.0f, 9.0f);
        if (x > 30) r *= smoothstepf(3.0f, 9.0f, fabsf(z));
        return h + r;
    }
    case MAP_PLATEAU: {
        float d = sqrtf(x * x + z * z);
        float h = 5.5f * (1.0f - smoothstepf(13.0f, 19.0f, d));
        if (fabsf(x) < 7.0f && z > 10.0f) {
            float ramp = 5.5f * clampf((30.0f - z) / 14.0f, 0, 1);
            float k = 1.0f - smoothstepf(3.0f, 7.0f, fabsf(x));
            h = fmaxf(h, ramp * k + h * (1 - k));
        }
        h += (fbm(x / 9.0f, z / 9.0f) - 0.5f) * 4.0f * smoothstepf(17.0f, 24.0f, d) * smoothstepf(3.0f, 8.0f, pd);
        return h + rim(x, z, 36.0f, 10.0f);
    }
    case MAP_DEMON:
    default: {
        float flat = smoothstepf(3.0f, 10.0f, pd);
        float h = (fbm(x / 10.0f, z / 10.0f) - 0.5f) * 6.0f * flat;
        float dc = sqrtf((x - 14) * (x - 14) + (z + 14) * (z + 14));
        h -= 2.5f * (1.0f - smoothstepf(3.0f, 8.0f, dc));   /* a crater */
        return h + rim(x, z, 34.0f, 8.0f);
    }
    }
}

static uint32_t shape_color(float x, float z, float h)
{
    float n = fbm(x / 6.0f + 50, z / 6.0f + 50);
    float pd = path_dist(x, z);
    uint32_t c;
    switch (g_world.id) {
    case MAP_VILLAGE:
        c = color_mix(0x4E9A38FF, 0x86C050FF, n);
        if (x > 20 && x < 42 && z > -20 && z < 16) {
            float edge = fminf(fminf(x - 20, 42 - x), fminf(z + 20, 16 - z));
            float stripe = ((int)floorf((z + 20) / 3.0f) & 1) ? 1.0f : 0.86f;
            c = color_mix(c, color_mul(0xE0C460FF, stripe), smoothstepf(0, 2.5f, edge) * 0.95f);
        }
        if (h > 4.0f) c = color_mix(c, 0x8CC85AFF, smoothstepf(4.0f, 6.0f, h) * 0.5f);
        {
            float dp = sqrtf((x + 30) * (x + 30) + (z - 30) * (z - 30));
            c = color_mix(c, 0xC8B888FF, 1.0f - smoothstepf(6.5f, 9.0f, dp));
        }
        c = color_mix(c, 0xB49870FF, 1.0f - smoothstepf(1.5f, 3.6f, pd));
        if (h > 3.0f && fmaxf(fabsf(x), fabsf(z)) > 38) c = color_mix(c, 0x3E7A30FF, 0.5f);
        break;
    case MAP_FOREST:
        c = color_mix(0x2E6A26FF, 0x5A9A3AFF, n);
        c = color_mix(c, 0x7AA84AFF, (1.0f - smoothstepf(8.0f, 14.0f, sqrtf(x * x + z * z))) * 0.5f);
        c = color_mix(c, 0x8A7450FF, 1.0f - smoothstepf(1.5f, 3.4f, pd));
        break;
    case MAP_PLATEAU: {
        float d = sqrtf(x * x + z * z);
        c = color_mix(0x8A7A62FF, 0xA8987AFF, n);
        c = color_mix(c, color_mix(0x6E9A48FF, 0x8AB060FF, n), 1.0f - smoothstepf(12.0f, 15.0f, d));
        c = color_mix(c, 0xB8A27AFF, (1.0f - smoothstepf(1.5f, 3.2f, pd)) * 0.8f);
        break;
    }
    case MAP_DEMON:
    default:
        c = color_mix(0x7A3426FF, 0xB0603CFF, n);
        c = color_mix(c, 0x5A2A22FF, smoothstepf(0.62f, 0.75f, fbm(x / 3.0f, z / 3.0f)));
        c = color_mix(c, 0xA87050FF, (1.0f - smoothstepf(1.5f, 3.2f, pd)) * 0.6f);
        break;
    }
    if (calamity) c = color_mix(color_mul(c, 0.8f), 0x7A4A9AFF, 0.25f);
    return c;
}

/* ------------------------------------------------------------------ */
/* Terrain                                                             */
/* ------------------------------------------------------------------ */

float world_height(float x, float z)
{
    float gx = (x + MAP_HALF) / CELL, gz = (z + MAP_HALF) / CELL;
    gx = clampf(gx, 0, GRID - 0.001f);
    gz = clampf(gz, 0, GRID - 0.001f);
    int ix = (int)gx, iz = (int)gz;
    float fx = gx - ix, fz = gz - iz;
    float h00 = heights[iz][ix], h10 = heights[iz][ix + 1];
    float h01 = heights[iz + 1][ix], h11 = heights[iz + 1][ix + 1];
    if (fx >= fz) return h00 + (h10 - h00) * fx + (h11 - h10) * fz;
    return h00 + (h11 - h01) * fx + (h01 - h00) * fz;
}

static uint32_t light_color(uint32_t base, vec3_t n)
{
    float d = fmaxf(0.0f, v3_dot(n, g_world.sun_dir)) * (g_world.storm ? 0.35f : 1.0f);
    uint32_t a = g_world.ambient, s = g_world.sun_color;
    float r = ((a >> 24) + (s >> 24) * d) / 255.0f;
    float g = (((a >> 16) & 0xFF) + ((s >> 16) & 0xFF) * d) / 255.0f;
    float b = (((a >> 8) & 0xFF) + ((s >> 8) & 0xFF) * d) / 255.0f;
    int cr = (int)((base >> 24) * r), cg = (int)(((base >> 16) & 0xFF) * g), cb = (int)(((base >> 8) & 0xFF) * b);
    cr = cr > 255 ? 255 : cr; cg = cg > 255 ? 255 : cg; cb = cb > 255 ? 255 : cb;
    return ((uint32_t)cr << 24) | ((uint32_t)cg << 16) | ((uint32_t)cb << 8) | 0xFF;
}

static void compute_terrain(void)
{
    for (int z = 0; z < VERTS; z++)
        for (int x = 0; x < VERTS; x++)
            heights[z][x] = shape_height(-MAP_HALF + x * CELL, -MAP_HALF + z * CELL);

    for (int z = 0; z < VERTS; z++)
        for (int x = 0; x < VERTS; x++) {
            float hl = heights[z][x > 0 ? x - 1 : x], hr = heights[z][x < GRID ? x + 1 : x];
            float hd = heights[z > 0 ? z - 1 : z][x], hu = heights[z < GRID ? z + 1 : z][x];
            vec3_t n = v3_norm(v3(hl - hr, 2.0f * CELL, hd - hu));
            float wx = -MAP_HALF + x * CELL, wz = -MAP_HALF + z * CELL;
            tcolors[z][x] = light_color(shape_color(wx, wz, heights[z][x]), n);
        }
}

static void build_terrain(void)
{
    /* each chunk is a 9x9 vertex grid drawn with indices, so every vertex is
       transformed once by the RSP instead of once per triangle */
    gfx_bind(ground_tex);
    const int CV = CHUNK_CELLS + 1;
    static gfx_vtx_t verts[(CHUNK_CELLS + 1) * (CHUNK_CELLS + 1)];
    static uint16_t idx[CHUNK_CELLS * CHUNK_CELLS * 6];
    int ni = 0;
    for (int z = 0; z < CHUNK_CELLS; z++)
        for (int x = 0; x < CHUNK_CELLS; x++) {
            uint16_t a = z * CV + x, b = z * CV + x + 1, c = (z + 1) * CV + x, d = (z + 1) * CV + x + 1;
            /* CCW seen from above, split along the (x,z)-(x+1,z+1) diagonal like world_height() */
            idx[ni++] = a; idx[ni++] = d; idx[ni++] = b;
            idx[ni++] = a; idx[ni++] = c; idx[ni++] = d;
        }

    for (int cz = 0; cz < CHUNKS; cz++)
        for (int cx = 0; cx < CHUNKS; cx++) {
            int x0 = cx * CHUNK_CELLS, z0 = cz * CHUNK_CELLS;
            for (int z = 0; z < CV; z++)
                for (int x = 0; x < CV; x++) {
                    gfx_vtx_t *v = &verts[z * CV + x];
                    v->p[0] = -MAP_HALF + (x0 + x) * CELL;
                    v->p[1] = heights[z0 + z][x0 + x];
                    v->p[2] = -MAP_HALF + (z0 + z) * CELL;
                    v->t[0] = (float)x;
                    v->t[1] = (float)z;
                    v->n[0] = 0; v->n[1] = 1; v->n[2] = 0;
                    v->c = tcolors[z0 + z][x0 + x] | 0xFF;
                }
            dl_begin();
            gfx_draw_indexed(verts, CV * CV, idx, ni, true, false);
            chunk_lists[cz * CHUNKS + cx] = dl_end();
        }
    gfx_bind(TEX_NONE);
}

static void build_sky(void)
{
    static const float elev[] = { -0.5f, -0.02f, 0.18f, 0.55f, 1.0f, 1.5708f };
    const int SEG = 10, NR = sizeof(elev) / sizeof(elev[0]);
    dl_begin();
    glBegin(GL_TRIANGLES);
    for (int r = 0; r < NR - 1; r++) {
        for (int s = 0; s < SEG; s++) {
            float a[2] = { elev[r], elev[r + 1] };
            float b[2] = { TAU_F * s / SEG, TAU_F * (s + 1) / SEG };
            vec3_t p[4];
            uint32_t col[4];
            for (int i = 0; i < 4; i++) {
                float e = a[i >> 1], az = b[(i == 1 || i == 2) ? 1 : 0];
                p[i] = v3(cosf(e) * cosf(az) * 90.0f, sinf(e) * 90.0f, cosf(e) * sinf(az) * 90.0f);
                float t = clampf(e / 1.2f, 0, 1);
                col[i] = e < 0 ? g_world.fog_color : color_mix(g_world.sky_horizon, g_world.sky_top, sqrtf(t));
            }
            /* wound so the inside of the dome faces the camera at the origin */
            int order[6] = { 0, 1, 2, 0, 2, 3 };
            for (int k = 0; k < 6; k++) {
                uint32_t c = col[order[k]];
                glColor4ub(c >> 24, (c >> 16) & 0xFF, (c >> 8) & 0xFF, 0xFF);
                glVertex3f(p[order[k]].x, p[order[k]].y, p[order[k]].z);
            }
        }
    }
    glEnd();
    sky_list = dl_end();
}

/* ------------------------------------------------------------------ */
/* Map population                                                      */
/* ------------------------------------------------------------------ */

static prop_t *add_prop(prop_type_t t, float x, float z, float rot, float scale)
{
    if (nprops >= MAX_PROPS) return NULL;
    prop_t *p = &props[nprops++];
    memset(p, 0, sizeof(*p));
    p->type = t; p->x = x; p->z = z; p->rot = rot; p->scale = scale;
    p->y = world_height(x, z);
    if (t == PROP_HOUSE || t == PROP_HOUSE_BIG) {
        /* sit on the lowest corner so nothing floats */
        float hx = PROP_INFO[t].chx, hz = PROP_INFO[t].chz;
        float m = p->y;
        for (int i = 0; i < 4; i++)
            m = fminf(m, world_height(x + ((i & 1) ? hx : -hx), z + ((i & 2) ? hz : -hz)));
        p->y = m;
    }
    return p;
}

static float rng_state;
static float rnd(void)
{
    rng_state = rng_state * 16807.0f;
    rng_state -= floorf(rng_state / 2147483647.0f) * 2147483647.0f;
    return rng_state / 2147483647.0f;
}

static bool spot_clear(float x, float z, float min_prop_dist, float min_path)
{
    if (path_dist(x, z) < min_path) return false;
    for (int i = 0; i < nprops; i++) {
        float dx = props[i].x - x, dz = props[i].z - z;
        float need = min_prop_dist + PROP_INFO[props[i].type].cull_r * 0.5f;
        if (dx * dx + dz * dz < need * need) return false;
    }
    return true;
}

static void scatter(prop_type_t t, int count, float min_dist, float min_path,
                    bool (*allowed)(float x, float z), float smin, float smax)
{
    int placed = 0;
    for (int tries = 0; tries < count * 30 && placed < count; tries++) {
        float x = (rnd() * 2 - 1) * 44.0f, z = (rnd() * 2 - 1) * 44.0f;
        if (allowed && !allowed(x, z)) continue;
        if (!spot_clear(x, z, min_dist, min_path)) continue;
        add_prop(t, x, z, rnd() * TAU_F, smin + (smax - smin) * rnd());
        placed++;
    }
}

static bool village_tree_ok(float x, float z)
{
    if (x > 18 && x < 44 && z > -22 && z < 18) return false;          /* fields */
    if (x * x + (z + 38) * (z + 38) < 14 * 14) return false;          /* hill top */
    if ((x + 30) * (x + 30) + (z - 30) * (z - 30) < 9 * 9) return false; /* pond */
    if (z > 38) return false;
    if (fabsf(x) < 9 && z > -22 && z < 40) return false;              /* village core */
    if (x > -28 && x < -8 && z > -4 && z < 20) return false;           /* Greyrat yard */
    return true;
}

static bool forest_tree_ok(float x, float z)
{
    return x * x + z * z > 13 * 13;
}

static bool plateau_rock_ok(float x, float z)
{
    return x * x + z * z > 20 * 20;
}

static void fence_line(float x0, float x1, float z, float rot)
{
    for (float x = x0 + 4.0f; x <= x1 - 3.99f; x += 8.0f) add_prop(PROP_FENCE, x, z, rot, 1.0f);
}

static void populate_village(void)
{
    add_prop(PROP_HOUSE_BIG, -19, 5, PI_F / 2, 1.0f);         /* Greyrat house */
    add_prop(PROP_HOUSE, 13, -9, -PI_F / 2, 1.0f);
    add_prop(PROP_HOUSE, 13, 20, -PI_F / 2, 1.0f);
    add_prop(PROP_HOUSE, -12, 23, PI_F / 2, 1.0f);
    add_prop(PROP_HOUSE, 12, 33, -PI_F / 2, 1.0f);
    add_prop(PROP_HOUSE, -12, 34, PI_F / 2, 1.0f);
    add_prop(PROP_HOUSE, -14, -12, 0, 1.0f);
    add_prop(PROP_HOUSE, 12, -1, -PI_F / 2, 1.0f);
    add_prop(PROP_HOUSE, 30, 22, PI_F, 1.0f);                   /* farmhouse */

    add_prop(PROP_WELL, 0, 6, 0, 1.0f);
    add_prop(PROP_BRAZIER, -6, 9, 0, 1.0f);
    add_prop(PROP_BRAZIER, 6, 9, 0, 1.0f);
    add_prop(PROP_BRAZIER, 0, -1, 0, 1.0f);
    add_prop(PROP_BOULDER, 0, -23, 0.4f, 1.0f);
    add_prop(PROP_BIGTREE, 0, -39, 0.3f, 1.0f);
    add_prop(PROP_BENCH, 3.5f, -36.0f, 0.2f, 1.0f);
    add_prop(PROP_GATE, 0, 41, 0, 1.0f);
    add_prop(PROP_SIGN, 4.2f, 38.5f, 0, 1.0f);
    add_prop(PROP_SIGN, -42, 5.5f, PI_F / 2, 1.0f);
    add_prop(PROP_CART, 7.5f, 13.0f, 0.3f, 1.0f);
    add_prop(PROP_CRATE, -7.0f, 14.5f, 0.2f, 1.0f);
    add_prop(PROP_CRATE, -8.1f, 14.9f, 0.7f, 1.0f);
    add_prop(PROP_CRATE, 17.0f, 26.5f, 0.1f, 1.0f);
    for (int i = 0; i < 6; i++) add_prop(PROP_HAY, 25.0f + i * 3.0f, -14.0f + (i % 3) * 9.0f, 0, 1.0f);

    /* the hill is fenced off except for the path the boulder blocks */
    fence_line(-46, -2.6f, -21.5f, 0);
    fence_line(2.6f, 46, -21.5f, 0);
    /* southern boundary with the gate */
    fence_line(-46, -3.4f, 41.0f, 0);
    fence_line(3.4f, 46, 41.0f, 0);
    /* field fences */
    fence_line(20, 44, -21.0f, 0);
    for (int i = 0; i < 4; i++) add_prop(PROP_FENCE, 19.5f, -16.0f + i * 8.0f, PI_F / 2, 1.0f);

    for (int i = 0; i < 6; i++) {
        float a = i * TAU_F / 6 + 0.3f;
        add_prop(PROP_ROCK, -30 + cosf(a) * 7.5f, 30 + sinf(a) * 7.5f, a, 0.5f + (i % 3) * 0.2f);
    }
    scatter(PROP_TREE, 26, 3.5f, 4.0f, village_tree_ok, 0.85f, 1.25f);
    scatter(PROP_PINE, 9, 3.5f, 4.0f, village_tree_ok, 0.9f, 1.3f);
    scatter(PROP_ROCK, 8, 3.0f, 3.0f, village_tree_ok, 0.6f, 1.2f);
}

static void populate_forest(void)
{
    scatter(PROP_PINE, 38, 3.0f, 3.5f, forest_tree_ok, 0.9f, 1.5f);
    scatter(PROP_TREE, 16, 3.2f, 3.5f, forest_tree_ok, 0.9f, 1.4f);
    scatter(PROP_ROCK, 12, 2.5f, 3.0f, forest_tree_ok, 0.6f, 1.4f);
    add_prop(PROP_ROCK, -6, -8, 0, 1.4f);
    add_prop(PROP_ROCK, 7, 9, 1, 1.1f);
    add_prop(PROP_SIGN, 40, 4, -PI_F / 2, 1.0f);
}

static void populate_plateau(void)
{
    for (int i = 0; i < 8; i++) {
        float a = i * TAU_F / 8 + 0.2f;
        if (fabsf(wrap_angle(a - PI_F / 2)) < 0.4f) continue;   /* keep the ramp open */
        add_prop(PROP_ROCK, cosf(a) * 15.5f, sinf(a) * 15.5f, a, 0.8f + (i & 1) * 0.5f);
    }
    scatter(PROP_CRAG, 14, 5.0f, 5.0f, plateau_rock_ok, 0.7f, 1.4f);
    scatter(PROP_ROCK, 16, 3.0f, 4.0f, plateau_rock_ok, 0.6f, 1.6f);
    scatter(PROP_PINE, 10, 4.0f, 5.0f, plateau_rock_ok, 0.8f, 1.2f);
}

static bool demon_ok(float x, float z)
{
    return !(fabsf(x) < 7 && z > -10 && z < 32);
}

static void populate_demon(void)
{
    add_prop(PROP_CRAG, 3.5f, -9, 0.4f, 0.8f);
    add_prop(PROP_ROCK, -2.0f, -6.5f, 0.2f, 1.6f);
    scatter(PROP_CRAG, 18, 5.0f, 4.0f, demon_ok, 0.7f, 1.6f);
    scatter(PROP_DEADTREE, 12, 4.0f, 4.0f, demon_ok, 0.8f, 1.3f);
    scatter(PROP_ROCK, 14, 3.0f, 3.0f, demon_ok, 0.6f, 1.5f);
}

/* ------------------------------------------------------------------ */

static void set_map_info(map_id_t id)
{
    world_t *w = &g_world;
    w->id = id;
    w->water_level = WATER_NONE;
    w->storm = false;
    w->storm_t = 0;
    w->lightning = 0;
    w->sun_dir = v3_norm(v3(0.45f, 0.8f, 0.35f));
    switch (id) {
    case MAP_VILLAGE:
        w->name = "Buena Village";
        if (calamity) {
            w->sky_top = 0x1A0A30FF; w->sky_horizon = 0xC0508AFF; w->fog_color = 0x8A4A86FF;
            w->fog_start = 14; w->fog_end = 50;
            w->sun_color = 0xC090E0FF; w->ambient = 0x584468FF;
        } else {
            w->sky_top = 0x3A76D6FF; w->sky_horizon = 0xB0D6F2FF; w->fog_color = 0xBCDAF0FF;
            w->fog_start = 18; w->fog_end = 52;
            w->sun_color = 0xFFF2DCFF; w->ambient = 0x707A8CFF;
        }
        w->water_level = -1.2f;
        break;
    case MAP_FOREST:
        w->name = "Fittoa Forest";
        w->sky_top = 0x3E7AC0FF; w->sky_horizon = 0x9EC2D2FF; w->fog_color = 0x6C8A78FF;
        w->fog_start = 10; w->fog_end = 44;
        w->sun_color = 0xE8F0D0FF; w->ambient = 0x546856FF;
        break;
    case MAP_PLATEAU:
        w->name = "Western Plateau";
        w->sky_top = 0x2A62C4FF; w->sky_horizon = 0xC4DEF2FF; w->fog_color = 0xC2D8EEFF;
        w->fog_start = 22; w->fog_end = 60;
        w->sun_color = 0xFFF4E4FF; w->ambient = 0x707888FF;
        break;
    case MAP_DEMON:
    default:
        w->name = "Demon Continent";
        w->sky_top = 0x3A1426FF; w->sky_horizon = 0xD07050FF; w->fog_color = 0xB06048FF;
        w->fog_start = 16; w->fog_end = 52;
        w->sun_color = 0xFFD0A8FF; w->ambient = 0x6A4444FF;
        w->sun_dir = v3_norm(v3(-0.5f, 0.45f, -0.6f));
        break;
    }
}

void bake_props(void);
static uint32_t prop_tint(const prop_t *p, int idx);

static dlist_t bake_list[CHUNKS * CHUNKS][NPASS];   /* NULL = nothing to draw */
static dlist_t boulder_list;

/* Release all of the current map's display lists; the new ones are recorded
   at the start of the next frame. */
static void free_world_lists(void)
{
    for (int c = 0; c < CHUNKS * CHUNKS; c++) {
        dl_free(chunk_lists[c]);
        chunk_lists[c] = NULL;
        for (int k = 0; k < NPASS; k++) { dl_free(bake_list[c][k]); bake_list[c][k] = NULL; }
    }
    dl_free(sky_list);
    sky_list = NULL;
    dl_free(boulder_list);
    boulder_list = NULL;
}

void world_init(void)
{
}

void world_set_palette_calamity(bool on)
{
    calamity = on;
}

void world_load(map_id_t id)
{
    map_seed = 1000 + id * 77;
    rng_state = 12345.0f + id * 1013.0f;
    set_map_info(id);
    switch (id) {
    case MAP_VILLAGE: paths = VILLAGE_PATHS; npaths = sizeof(VILLAGE_PATHS) / sizeof(seg_t); ground_tex = TEX_GRASS; break;
    case MAP_FOREST:  paths = FOREST_PATHS;  npaths = sizeof(FOREST_PATHS) / sizeof(seg_t);  ground_tex = TEX_GRASS; break;
    case MAP_PLATEAU: paths = PLATEAU_PATHS; npaths = sizeof(PLATEAU_PATHS) / sizeof(seg_t); ground_tex = TEX_DIRT;  break;
    default:          paths = DEMON_PATHS;   npaths = sizeof(DEMON_PATHS) / sizeof(seg_t);   ground_tex = TEX_DIRT;  break;
    }
    free_world_lists();
    compute_terrain();

    nprops = 0;
    switch (id) {
    case MAP_VILLAGE: populate_village(); break;
    case MAP_FOREST:  populate_forest(); break;
    case MAP_PLATEAU: populate_plateau(); break;
    default:          populate_demon(); break;
    }
    /* Display lists are recorded at the start of the next frame, inside the
       GL context: lists recorded outside it came out broken (textures
       sampled along a single axis, occasional RSP crashes). */
    lists_dirty = true;
    baked_storm = false;
}

static void record_world_lists(void)
{
    rspq_wait();
    build_terrain();
    build_sky();
    bake_props();
    lists_ready = true;
    lists_dirty = false;
#ifdef TEST_INPUT
    heap_stats_t hs;
    sys_get_heap_stats(&hs);
    debugf("map %d recorded: heap %d / %d KB\n", g_world.id, hs.used / 1024, hs.total / 1024);
#endif
}

/* ------------------------------------------------------------------ */
/* Collision                                                           */
/* ------------------------------------------------------------------ */

static bool prop_solid(const prop_t *p)
{
    if (p->flags & PF_NOCOLL) return false;
    if (p->type == PROP_BOULDER && (p->flags & PF_BROKEN)) return false;
    return PROP_INFO[p->type].col != COL_NONE;
}

static bool push_circle(vec3_t *pos, float cx, float cz, float r)
{
    float dx = pos->x - cx, dz = pos->z - cz;
    float d2 = dx * dx + dz * dz;
    if (d2 >= r * r) return false;
    float d = sqrtf(d2);
    if (d < 1e-4f) { dx = 1; dz = 0; d = 1; }
    pos->x = cx + dx / d * r;
    pos->z = cz + dz / d * r;
    return true;
}

static bool push_box(vec3_t *pos, const prop_t *p, float hx, float hz, float radius)
{
    float c = cosf(p->rot), s = sinf(p->rot);
    float wx = pos->x - p->x, wz = pos->z - p->z;
    float lx = wx * c - wz * s, lz = wx * s + wz * c;
    hx += radius; hz += radius;
    if (fabsf(lx) >= hx || fabsf(lz) >= hz) return false;
    float px = hx - fabsf(lx), pz = hz - fabsf(lz);
    if (px < pz) lx = lx < 0 ? -hx : hx;
    else         lz = lz < 0 ? -hz : hz;
    pos->x = p->x + lx * c + lz * s;
    pos->z = p->z - lx * s + lz * c;
    return true;
}

bool world_collide(vec3_t *pos, float radius)
{
    bool hit = false;
    for (int i = 0; i < nprops; i++) {
        const prop_t *p = &props[i];
        if (!prop_solid(p)) continue;
        float dx = pos->x - p->x, dz = pos->z - p->z;
        float reach = PROP_INFO[p->type].cull_r * p->scale + radius + 1.0f;
        if (dx * dx + dz * dz > reach * reach) continue;
        const prop_info_t *m = &PROP_INFO[p->type];
        switch (m->col) {
        case COL_CIRCLE:
            hit |= push_circle(pos, p->x, p->z, m->cr * p->scale + radius);
            break;
        case COL_BOX:
            hit |= push_box(pos, p, m->chx * p->scale, m->chz * p->scale, radius);
            break;
        case COL_GATE: {
            float c = cosf(p->rot), s = sinf(p->rot);
            for (int k = -1; k <= 1; k += 2)
                hit |= push_circle(pos, p->x + k * 3.15f * c, p->z - k * 3.15f * s, m->cr + radius);
            break;
        }
        }
    }
    if (g_world.id == MAP_VILLAGE)
        hit |= push_circle(pos, -30, 30, 5.6f + radius);
    if (pos->x < -BOUND) { pos->x = -BOUND; hit = true; }
    if (pos->x > BOUND)  { pos->x = BOUND;  hit = true; }
    if (pos->z < -BOUND) { pos->z = -BOUND; hit = true; }
    if (pos->z > BOUND)  { pos->z = BOUND;  hit = true; }
    return hit;
}

bool world_solid_at(vec3_t pt, float radius)
{
    if (pt.y < world_height(pt.x, pt.z)) return true;
    for (int i = 0; i < nprops; i++) {
        const prop_t *p = &props[i];
        if (!prop_solid(p)) continue;
        const prop_info_t *m = &PROP_INFO[p->type];
        if (pt.y > p->y + m->top * p->scale) continue;
        vec3_t q = pt;
        bool in = false;
        if (m->col == COL_CIRCLE) {
            float r = m->cr * p->scale + radius;
            float dx = q.x - p->x, dz = q.z - p->z;
            in = dx * dx + dz * dz < r * r;
        } else if (m->col == COL_BOX) {
            in = push_box(&q, p, m->chx * p->scale, m->chz * p->scale, radius);
        }
        if (in) return true;
    }
    return false;
}

int world_projectile_hit(vec3_t pt, float radius, int spell)
{
    for (int i = 0; i < nprops; i++) {
        prop_t *p = &props[i];
        if (p->type == PROP_BRAZIER) {
            float dx = pt.x - p->x, dz = pt.z - p->z, dy = pt.y - (p->y + 1.3f);
            if (dx * dx + dz * dz + dy * dy < (0.9f + radius) * (0.9f + radius)) {
                if (spell == SPELL_FIRE && !(p->flags & PF_LIT)) {
                    p->flags |= PF_LIT;
                    particles_burst(v3(p->x, p->y + 1.4f, p->z), 0xFFA030FF, 14, 3.0f, 0.35f, 0.7f, -2.0f);
                    sfx_play(SFX_FIRE);
                    return 1;
                }
                return 4;
            }
        } else if (p->type == PROP_BOULDER && !(p->flags & PF_BROKEN)) {
            float dx = pt.x - p->x, dz = pt.z - p->z;
            if (dx * dx + dz * dz < (2.6f + radius) * (2.6f + radius) && pt.y < p->y + 3.6f) {
                if (spell == SPELL_STONE) {
                    p->flags |= PF_BROKEN;
                    particles_burst(v3(p->x, p->y + 1.2f, p->z), 0x8E8678FF, 30, 7.0f, 0.5f, 1.2f, 12.0f);
                    particles_burst(v3(p->x, p->y + 1.0f, p->z), 0xC8C0B0FF, 16, 4.0f, 0.8f, 1.0f, 2.0f);
                    sfx_play(SFX_EXPLODE);
                    g_cam.shake = 0.6f;
                    return 2;
                }
                return 3;
            }
        }
    }
    return 0;
}

int world_count_props(prop_type_t type, uint8_t mask, uint8_t value)
{
    int n = 0;
    for (int i = 0; i < nprops; i++)
        if (props[i].type == type && (props[i].flags & mask) == value) n++;
    return n;
}

prop_t *world_find_prop(prop_type_t type, int index)
{
    for (int i = 0; i < nprops; i++)
        if (props[i].type == type && index-- == 0) return &props[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Update + render                                                     */
/* ------------------------------------------------------------------ */

void world_update(float dt)
{
    for (int i = 0; i < nprops; i++) {
        prop_t *p = &props[i];
        p->t += dt;
        if (p->type == PROP_BRAZIER && (p->flags & PF_LIT) && p->t > 0.07f) {
            p->t = 0;
            if (dist_xz(v3(p->x, 0, p->z), g_player.pos) < 40) {
                particle_spawn(v3(p->x + frand_range(-0.2f, 0.2f), p->y + 1.3f, p->z + frand_range(-0.2f, 0.2f)),
                               v3(frand_range(-0.3f, 0.3f), frand_range(1.5f, 2.5f), frand_range(-0.3f, 0.3f)),
                               frand() < 0.5f ? 0xFF9020FF : 0xFFD050FF, 0.35f, 0.6f, 0);
            }
        }
        if ((p->type == PROP_HOUSE || p->type == PROP_HOUSE_BIG) && p->t > 0.5f && g_world.id == MAP_VILLAGE) {
            p->t = 0;
            if (dist_xz(v3(p->x, 0, p->z), g_player.pos) < 35) {
                const prop_info_t *m = &PROP_INFO[p->type];
                float lx = m->chx * 0.45f + 0.4f, lz = -m->chz * 0.5f + 0.4f;
                float c = cosf(p->rot), s = sinf(p->rot);
                vec3_t top = v3(p->x + lx * c + lz * s, p->y + m->top + 0.8f, p->z - lx * s + lz * c);
                particle_spawn(top, v3(0.3f, 1.0f, 0.1f), calamity ? 0x806090A0 : 0xD8D8E0A0, 0.6f, 2.5f, -0.1f);
            }
        }
    }

    if (g_world.storm && !baked_storm) {
        /* the sun goes behind the clouds: re-light the terrain and props */
        baked_storm = true;
        free_world_lists();
        compute_terrain();
        lists_dirty = true;
    }
    if (g_world.storm) {
        g_world.storm_t += dt;
        g_world.lightning = fmaxf(0, g_world.lightning - dt * 3.0f);
        if (frand() < dt * 0.35f) {
            g_world.lightning = 1.0f;
            sfx_play(SFX_THUNDER);
            g_cam.shake = fmaxf(g_cam.shake, 0.3f);
        }
    }
}

static void apply_fog(uint32_t fog)
{
    GLfloat fc[4] = { (fog >> 24) / 255.0f, ((fog >> 16) & 0xFF) / 255.0f, ((fog >> 8) & 0xFF) / 255.0f, 1 };
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, g_world.fog_start);
    glFogf(GL_FOG_END, g_world.fog_end);
    glFogfv(GL_FOG_COLOR, fc);
}

static uint32_t storm_mix(uint32_t c, float darken_to_gray)
{
    float k = clampf(g_world.storm_t / 3.0f, 0, 1) * darken_to_gray;
    c = color_mix(c, 0x3A4250FF, k);
    if (g_world.lightning > 0) c = color_mix(c, 0xF0F4FFFF, g_world.lightning * 0.8f);
    return c;
}

uint32_t world_clear_color(void)
{
    return g_world.storm ? storm_mix(g_world.fog_color, 1.0f) : g_world.fog_color;
}

void world_render_sky(void)
{
    if (lists_dirty) record_world_lists();
    vec3_t eye = g_cam.override ? g_cam.ov_pos : g_cam.pos;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    gfx_bind(TEX_NONE);
    glDisable(GL_CULL_FACE);
    /* in a storm the dome fades out and the dark clear color shows through */
    if (!g_world.storm || g_world.storm_t < 0.6f) {
        glPushMatrix();
        glTranslatef(eye.x, eye.y, eye.z);
        dl_call(sky_list);
        glPopMatrix();
    }
    glEnable(GL_CULL_FACE);

    /* sun / clouds as soft billboards */
    gfx_billboards_begin();
    if (!g_world.storm) {
        vec3_t sun = v3_add(eye, v3_scale(g_world.sun_dir, 80.0f));
        gfx_billboard(sun, 9.0f, g_world.id == MAP_DEMON ? 0xFFB070FF : 0xFFF8E0FF);
        gfx_billboard(sun, 18.0f, g_world.id == MAP_DEMON ? 0xFF804060 : 0xFFF0C050);
        uint32_t cloud = calamity ? 0xC080C080 : (g_world.id == MAP_DEMON ? 0xE0A08060 : 0xFFFFFF90);
        for (int i = 0; i < 7; i++) {
            float a = i * 0.9f + g_frame.time * 0.004f;
            vec3_t c = v3_add(eye, v3(cosf(a) * 70.0f, 26.0f + (i % 3) * 6.0f, sinf(a) * 70.0f));
            gfx_billboard(c, 12.0f, cloud);
            gfx_billboard(v3_add(c, v3(5, -1.5f, 2)), 9.0f, cloud);
            gfx_billboard(v3_add(c, v3(-5, -1.0f, -1)), 8.0f, cloud);
        }
    }
    if (calamity) {
        /* the strange light in the sky */
        vec3_t src = v3_add(eye, v3(-20, 60, -50));
        float pulse = 1.0f + sinf(g_frame.time * 2.0f) * 0.1f;
        gfx_billboard(src, 22.0f * pulse, 0xFFFFFFFF);
        gfx_billboard(src, 40.0f * pulse, 0xE0B0FFA0);
    }
    gfx_billboards_end();
    glEnable(GL_DEPTH_TEST);
}

static void render_water(void)
{
    if (g_world.water_level <= WATER_NONE) return;
    float cx = -30, cz = 30, r = 9.0f;
    if (!gfx_visible(v3(cx, g_world.water_level, cz), r * 1.4f, g_world.fog_end)) return;
    float off = g_frame.time * 0.08f;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    gfx_bind(TEX_WATER);
    gl_color(calamity ? 0x8A5AB8B8 : 0x4A90D0B8);
    glBegin(GL_TRIANGLES);
    const int N = 4;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            float x0 = cx - r + 2 * r * i / N, x1 = cx - r + 2 * r * (i + 1) / N;
            float z0 = cz - r + 2 * r * j / N, z1 = cz - r + 2 * r * (j + 1) / N;
            float u0 = (float)i + off, u1 = (float)(i + 1) + off, v0 = (float)j + off * 0.6f, v1 = (float)(j + 1) + off * 0.6f;
            float y = g_world.water_level;
            glTexCoord2f(u0, v0); glVertex3f(x0, y, z0);
            glTexCoord2f(u0, v1); glVertex3f(x0, y, z1);
            glTexCoord2f(u1, v1); glVertex3f(x1, y, z1);
            glTexCoord2f(u0, v0); glVertex3f(x0, y, z0);
            glTexCoord2f(u1, v1); glVertex3f(x1, y, z1);
            glTexCoord2f(u1, v0); glVertex3f(x1, y, z0);
        }
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

static uint32_t prop_tint(const prop_t *p, int idx)
{
    uint32_t c;
    switch (p->type) {
    case PROP_HOUSE: case PROP_HOUSE_BIG: case PROP_WELL: {
        static const uint32_t roofs[] = { 0xA8503AFF, 0x7A4A3AFF, 0x6A5A7AFF, 0xB0683AFF, 0x8A4A2AFF };
        c = p->type == PROP_HOUSE_BIG ? 0x5E6E8AFF : roofs[idx % 5];
        break;
    }
    case PROP_PINE:    c = color_mix(0x2E5E36FF, 0x3E7A40FF, (idx % 7) / 7.0f); break;
    case PROP_BIGTREE: c = 0x5AA040FF; break;
    case PROP_CRAG:    c = g_world.id == MAP_DEMON ? 0x8A4A3AFF : 0x8A8274FF; break;
    default:           c = color_mix(0x3E8A30FF, 0x6AAA3AFF, (idx % 5) / 5.0f); break;
    }
    if (calamity) c = color_mix(c, 0x7A4A9AFF, 0.3f);
    return c;
}

/* Static props are baked per terrain chunk and per texture into display
   lists, with the sun lighting pre-computed into vertex colors. Drawing the
   whole village is then a handful of list calls with no per-prop CPU work. */
static int boulder_idx = -1;

static int chunk_of(float x, float z)
{
    int cx = (int)((x + MAP_HALF) / (CHUNK_CELLS * CELL));
    int cz = (int)((z + MAP_HALF) / (CHUNK_CELLS * CELL));
    cx = cx < 0 ? 0 : (cx >= CHUNKS ? CHUNKS - 1 : cx);
    cz = cz < 0 ? 0 : (cz >= CHUNKS ? CHUNKS - 1 : cz);
    return cz * CHUNKS + cx;
}

static int emit_chunk_pass(int c)
{
    mb_begin(0xFFFFFFFF);
    for (int i = 0; i < nprops; i++) {
        const prop_t *p = &props[i];
        if (p->type == PROP_BOULDER) { boulder_idx = i; continue; }
        if (chunk_of(p->x, p->z) != c) continue;
        mb_base(p->x, p->y, p->z, p->rot, p->scale);
        emit_prop(p, prop_tint(p, i));
    }
    int n = mb_emitted();
    mb_end();
    return n;
}

void bake_props(void)
{
    boulder_idx = -1;

    uint32_t sun = g_world.storm ? color_mul(g_world.sun_color, 0.35f) : g_world.sun_color;
    mb_bake_lighting(true, g_world.sun_dir, sun, g_world.ambient);
    mb_skip_bottoms(true);
    gfx_bind(TEX_STONE);   /* texturing on while recording (see build_terrain) */
    for (int c = 0; c < CHUNKS * CHUNKS; c++) {
        for (int k = 0; k < NPASS; k++) {
            cur_pass = PASS_ORDER[k];
            /* dry run first so that only non-empty lists are created */
            mb_set_dry(true);
            int n = emit_chunk_pass(c);
            mb_set_dry(false);
            if (!n) continue;
            dl_begin();
            emit_chunk_pass(c);
            bake_list[c][k] = dl_end();
        }
    }
    if (boulder_idx >= 0) {
        const prop_t *p = &props[boulder_idx];
        cur_pass = TEX_STONE;
        dl_begin();
        mb_begin(0xFFFFFFFF);
        mb_base(p->x, p->y, p->z, p->rot, p->scale);
        emit_prop(p, 0);
        mb_end();
        boulder_list = dl_end();
    }
    mb_base_identity();
    mb_skip_bottoms(false);
    mb_bake_lighting(false, g_world.sun_dir, 0, 0);
    gfx_bind(TEX_NONE);
}

static bool chunk_visible(int cx, int cz)
{
    float half = CHUNK_CELLS * CELL * 0.5f;
    vec3_t c = v3(-MAP_HALF + (cx * CHUNK_CELLS) * CELL + half, 0, -MAP_HALF + (cz * CHUNK_CELLS) * CELL + half);
    c.y = heights[cz * CHUNK_CELLS + CHUNK_CELLS / 2][cx * CHUNK_CELLS + CHUNK_CELLS / 2] + 2.0f;
    return gfx_visible(c, half * 1.45f + 3.0f, g_world.fog_end);
}

void world_restore_ambient(void)
{
    uint32_t a = g_world.ambient;
    GLfloat amb[4] = { (a >> 24) / 255.0f, ((a >> 16) & 0xFF) / 255.0f, ((a >> 8) & 0xFF) / 255.0f, 1 };
    if (g_world.lightning > 0) for (int i = 0; i < 3; i++) amb[i] = fminf(1.0f, amb[i] + g_world.lightning * 0.6f);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
}

void world_render(void)
{
    uint32_t fog = g_world.storm ? storm_mix(g_world.fog_color, 1.0f) : g_world.fog_color;
    apply_fog(fog);
    glEnable(GL_FOG);

    /* sun light for lit objects (set while the modelview is the view matrix) */
    float sd = g_world.storm ? 0.45f : 1.0f;
    uint32_t s = g_world.sun_color, a = g_world.ambient;
    GLfloat pos[4] = { g_world.sun_dir.x, g_world.sun_dir.y, g_world.sun_dir.z, 0 };
    GLfloat dif[4] = { (s >> 24) / 255.0f * sd, ((s >> 16) & 0xFF) / 255.0f * sd, ((s >> 8) & 0xFF) / 255.0f * sd, 1 };
    GLfloat amb[4] = { (a >> 24) / 255.0f, ((a >> 16) & 0xFF) / 255.0f, ((a >> 8) & 0xFF) / 255.0f, 1 };
    if (g_world.lightning > 0) for (int i = 0; i < 3; i++) amb[i] = fminf(1.0f, amb[i] + g_world.lightning * 0.6f);
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);

    /* terrain and props: lighting is baked into vertex colors */
    glDisable(GL_LIGHTING);
    bool vis[CHUNKS * CHUNKS];
    for (int cz = 0; cz < CHUNKS; cz++)
        for (int cx = 0; cx < CHUNKS; cx++)
            vis[cz * CHUNKS + cx] = chunk_visible(cx, cz);

    gfx_bind(ground_tex);
    for (int c = 0; c < CHUNKS * CHUNKS; c++)
        if (vis[c]) dl_call(chunk_lists[c]);

    for (int k = 0; k < NPASS; k++) {
        bool bound = false;
        for (int c = 0; c < CHUNKS * CHUNKS; c++) {
            if (!vis[c] || !bake_list[c][k]) continue;
            if (!bound) { gfx_bind(PASS_ORDER[k]); bound = true; }
            dl_call(bake_list[c][k]);
        }
    }
    if (boulder_idx >= 0 && !(props[boulder_idx].flags & PF_BROKEN) &&
        gfx_visible(v3(props[boulder_idx].x, props[boulder_idx].y + 1.5f, props[boulder_idx].z), 3.0f, g_world.fog_end)) {
        gfx_bind(TEX_STONE);
        dl_call(boulder_list);
    }
    glEnable(GL_LIGHTING);
    gfx_bind(TEX_NONE);
    render_water();
}

void world_render_fx(void)
{
    if (!g_world.storm) return;
    /* rain streaks around the camera */
    vec3_t eye = g_cam.override ? g_cam.ov_pos : g_cam.pos;
    float k = clampf(g_world.storm_t / 2.0f, 0, 1);
    int n = (int)(70 * k);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    gfx_bind(TEX_NONE);
    gl_color(0xC8D4F0A0);
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < n; i++) {
        float fx = (float)((i * 73) % 97) / 97.0f, fz = (float)((i * 41) % 89) / 89.0f;
        float speed = 18.0f + (i % 5) * 2.0f;
        float y = 12.0f - fmodf(g_frame.time * speed + i * 1.7f, 16.0f);
        vec3_t p = v3(eye.x + (fx - 0.5f) * 24.0f, eye.y + y - 4.0f, eye.z + (fz - 0.5f) * 24.0f);
        vec3_t r = v3_scale(g_cam.right, 0.025f);
        glVertex3f(p.x - r.x, p.y, p.z - r.z);
        glVertex3f(p.x + r.x, p.y, p.z + r.z);
        glVertex3f(p.x + r.x + 0.1f, p.y + 0.9f, p.z + r.z);
        glVertex3f(p.x - r.x, p.y, p.z - r.z);
        glVertex3f(p.x + r.x + 0.1f, p.y + 0.9f, p.z + r.z);
        glVertex3f(p.x - r.x + 0.1f, p.y + 0.9f, p.z - r.z);
    }
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
}
