/*
 * Low-poly characters and creatures, assembled from the unit primitives in
 * gfx.c with simple hierarchical (joint) animation - the classic early N64 look.
 */
#include "game.h"

enum { HS_SHORT, HS_SPIKY, HS_LONG, HS_BRAIDS, HS_PONY, HS_BUN, HS_BALD };
enum { OUT_TUNIC, OUT_ROBE, OUT_DRESS, OUT_MAID, OUT_VEST };
enum { HAT_NONE, HAT_WITCH, HAT_BAND };
enum { W_NONE, W_STAFF, W_STAFF_GEM, W_SWORD, W_SPEAR };

typedef struct {
    uint32_t skin, hair, top, bottom, shoes, accent, eyes;
    uint8_t hair_style, outfit, hat, weapon;
    float height;
    bool elf_ears;
    bool gem;
} human_desc_t;

static const human_desc_t HUMANS[MDL_HUMAN_COUNT] = {
    [MDL_RUDEUS]        = { 0xF2C9A2FF, 0x5C3A22FF, 0x6F7F4CFF, 0x4A3B2AFF, 0x3A2A1EFF, 0xD8D0B8FF, 0x2A2030FF, HS_SHORT,  OUT_ROBE,  HAT_NONE,  W_STAFF,     1.12f, false, false },
    [MDL_RUDEUS_OLDER]  = { 0xF2C9A2FF, 0x5C3A22FF, 0x585E6EFF, 0x3A3540FF, 0x2E2620FF, 0xC8C0A8FF, 0x2A2030FF, HS_SHORT,  OUT_ROBE,  HAT_NONE,  W_STAFF_GEM, 1.40f, false, false },
    [MDL_ROXY]          = { 0xF4D6C0FF, 0x3F6FC8FF, 0x6B4A33FF, 0x4A3426FF, 0x3A2A1EFF, 0xF0EEE6FF, 0x2848A0FF, HS_BRAIDS, OUT_ROBE,  HAT_WITCH, W_STAFF,     1.36f, false, false },
    [MDL_PAUL]          = { 0xE8BC94FF, 0x8C6A3AFF, 0xC9B48AFF, 0x3C3A48FF, 0x4A3220FF, 0x5A4632FF, 0x2A2030FF, HS_SPIKY,  OUT_VEST,  HAT_NONE,  W_SWORD,     1.80f, false, false },
    [MDL_ZENITH]        = { 0xF6D8C0FF, 0xE6CF78FF, 0xE6E6EEFF, 0x6C8CC8FF, 0x6A5040FF, 0x6C8CC8FF, 0x3A60A0FF, HS_LONG,   OUT_DRESS, HAT_NONE,  W_NONE,      1.66f, false, false },
    [MDL_LILIA]         = { 0xF0CCAEFF, 0x3A2A22FF, 0x2A2A36FF, 0x2A2A36FF, 0x1E1E24FF, 0xF2F2F2FF, 0x2A2030FF, HS_BUN,    OUT_MAID,  HAT_NONE,  W_NONE,      1.64f, false, false },
    [MDL_SYLPHIE]       = { 0xF6DCC8FF, 0x58C88AFF, 0xF0EEE4FF, 0x6B5A44FF, 0x4A3A2AFF, 0xF0EEE4FF, 0xB06030FF, HS_SHORT,  OUT_TUNIC, HAT_NONE,  W_NONE,      1.08f, true,  false },
    [MDL_SYLPHIE_OLDER] = { 0xF6DCC8FF, 0x58C88AFF, 0xF0EEE4FF, 0x4E7A5EFF, 0x4A3A2AFF, 0x4E7A5EFF, 0xB06030FF, HS_PONY,   OUT_DRESS, HAT_NONE,  W_NONE,      1.36f, true,  false },
    [MDL_RUIJERD]       = { 0xE4E0D4FF, 0xE4E0D4FF, 0x3A3430FF, 0x2A2622FF, 0x1E1A16FF, 0xC8C4B8FF, 0xC03030FF, HS_BALD,   OUT_VEST,  HAT_BAND,  W_SPEAR,     1.88f, false, true  },
    [MDL_VILLAGER_M]    = { 0xE8BC94FF, 0x6A4A2AFF, 0x9A7A5AFF, 0x4A4A3AFF, 0x3A2A1EFF, 0x7A5A3AFF, 0x2A2030FF, HS_SHORT,  OUT_TUNIC, HAT_NONE,  W_NONE,      1.74f, false, false },
    [MDL_VILLAGER_F]    = { 0xF0CCAEFF, 0xB07040FF, 0xB86A5AFF, 0xB86A5AFF, 0x4A3A2AFF, 0xEDE4D0FF, 0x2A2030FF, HS_BUN,    OUT_MAID,  HAT_NONE,  W_NONE,      1.62f, false, false },
    [MDL_BULLY_A]       = { 0xE8BC94FF, 0x2A2420FF, 0xA83A3AFF, 0x4A3B2AFF, 0x3A2A1EFF, 0xA83A3AFF, 0x2A2030FF, HS_SPIKY,  OUT_TUNIC, HAT_NONE,  W_NONE,      1.18f, false, false },
    [MDL_BULLY_B]       = { 0xF0CCAEFF, 0xC86A2AFF, 0x3A5AA8FF, 0x4A3B2AFF, 0x3A2A1EFF, 0x3A5AA8FF, 0x2A2030FF, HS_SHORT,  OUT_TUNIC, HAT_NONE,  W_NONE,      1.10f, false, false },
    [MDL_BULLY_C]       = { 0xE0B088FF, 0x6A4A2AFF, 0xC8A83AFF, 0x4A3B2AFF, 0x3A2A1EFF, 0xC8A83AFF, 0x2A2030FF, HS_SHORT,  OUT_TUNIC, HAT_NONE,  W_NONE,      1.05f, false, false },
};

static float cur_flash;

static uint32_t C(uint32_t c)
{
    if (cur_flash <= 0) return c;
    return color_mix(c, 0xFFFFFFFF, clampf(cur_flash, 0, 1) * 0.8f);
}

static void part(prim_t p, uint32_t col, float x, float y, float z, float sx, float sy, float sz)
{
    gfx_part(p, C(col), x, y, z, sx, sy, sz);
}

static void part_r(prim_t p, uint32_t col, float x, float y, float z, float rx, float ry, float rz,
                   float sx, float sy, float sz)
{
    gfx_part_rot(p, C(col), x, y, z, rx, ry, rz, sx, sy, sz);
}

/* A limb hanging from a joint, rotated around X (forward swing) and Z (spread). */
static void limb(uint32_t col, float jx, float jy, float jz, float swing, float spread,
                 float w, float len, float d)
{
    glPushMatrix();
    glTranslatef(jx, jy, jz);
    if (spread != 0) glRotatef(spread * RAD2DEG, 0, 0, 1);
    if (swing != 0) glRotatef(swing * RAD2DEG, 1, 0, 0);
    part(PRIM_CUBE, col, 0, -len * 0.5f, 0, w, len, d);
    glPopMatrix();
}

void models_init(void) { }

static void draw_weapon(const human_desc_t *d, float arm_len, float H, anim_t anim)
{
    switch (d->weapon) {
    case W_STAFF:
    case W_STAFF_GEM: {
        /* staff held in the fist, mostly vertical */
        glPushMatrix();
        glTranslatef(0, -arm_len, 0.02f);
        glRotatef(anim == ANIM_CAST ? 0 : -80, 1, 0, 0);
        float len = H * 0.95f;
        part(PRIM_CYLINDER, 0x7A5432FF, 0, -len * 0.35f, 0, 0.06f, len, 0.06f);
        uint32_t gem = d->weapon == W_STAFF_GEM ? 0x50A0FFFF : 0xE0C070FF;
        part(PRIM_SPHERE, gem, 0, len * 0.65f + 0.03f, 0, 0.13f, 0.13f, 0.13f);
        glPopMatrix();
        break;
    }
    case W_SWORD:
        glPushMatrix();
        glTranslatef(0, -arm_len, 0.02f);
        glRotatef(-80, 1, 0, 0);
        part(PRIM_CUBE, 0x5A4030FF, 0, 0.0f, 0, 0.06f, 0.2f, 0.06f);
        part(PRIM_CUBE, 0xB0A060FF, 0, 0.11f, 0, 0.26f, 0.05f, 0.07f);
        part(PRIM_CUBE, 0xD8DCE8FF, 0, 0.55f, 0, 0.08f, 0.8f, 0.03f);
        glPopMatrix();
        break;
    case W_SPEAR:
        glPushMatrix();
        glTranslatef(0, -arm_len, 0.02f);
        glRotatef(-80, 1, 0, 0);
        part(PRIM_CYLINDER, 0xE8E6DCFF, 0, -0.9f, 0, 0.06f, 2.4f, 0.06f);
        part(PRIM_CONE, 0xF4F4FFFF, 0, 1.5f, 0, 0.12f, 0.35f, 0.12f);
        part(PRIM_CONE, 0xF4F4FFFF, 0.09f, 1.4f, 0, 0.06f, 0.2f, 0.06f);
        part(PRIM_CONE, 0xF4F4FFFF, -0.09f, 1.4f, 0, 0.06f, 0.2f, 0.06f);
        glPopMatrix();
        break;
    default:
        break;
    }
}

void draw_human(human_model_t m, vec3_t pos, float yaw, anim_t anim, float t, float flash)
{
    const human_desc_t *d = &HUMANS[m];
    const float H = d->height;
    const bool child = H < 1.3f;
    cur_flash = flash;

    /* proportions */
    const float hip_y = H * (child ? 0.40f : 0.47f);
    const float leg_len = hip_y;
    const float torso_h = H * (child ? 0.28f : 0.31f);
    const float torso_w = H * 0.27f, torso_d = H * 0.16f;
    const float head = H * (child ? 0.30f : 0.22f);
    const float head_y = hip_y + torso_h + head * 0.52f;
    const float sh_y = hip_y + torso_h - 0.04f;
    const float arm_len = H * (child ? 0.33f : 0.36f);
    const float limb_w = H * 0.085f;

    /* animation */
    float leg_sw = 0, arm_sw = 0, bob = 0, lean = 0;
    float r_arm = 0, l_arm = 0, r_spread = 0, l_spread = 0;
    switch (anim) {
    case ANIM_WALK: leg_sw = sinf(t) * 0.55f; bob = fabsf(cosf(t)) * 0.03f; break;
    case ANIM_RUN:  leg_sw = sinf(t) * 0.85f; bob = fabsf(cosf(t)) * 0.06f; lean = 0.12f; break;
    case ANIM_IDLE: bob = sinf(t * 0.5f) * 0.008f; break;
    case ANIM_HURT: lean = -0.3f; r_spread = -0.5f; l_spread = 0.5f; break;
    default: break;
    }
    arm_sw = -leg_sw * 0.8f;
    r_arm = arm_sw; l_arm = -arm_sw;
    if (anim == ANIM_SWING) {
        /* t: 0..1 progress of an overhead strike */
        float k = clampf(t, 0, 1);
        r_arm = -2.6f + k * 3.2f;
        lean = 0.15f * k;
    } else if (anim == ANIM_CAST) {
        r_arm = -1.45f; l_arm = -0.4f; l_spread = 0.2f;
    } else if (anim == ANIM_WAVE) {
        r_arm = -2.8f; r_spread = -0.3f + sinf(t * 6.0f) * 0.35f;
    }

    glPushMatrix();
    glTranslatef(pos.x, pos.y + bob, pos.z);
    glRotatef(yaw * RAD2DEG, 0, 1, 0);
    if (lean != 0) glRotatef(lean * RAD2DEG, 1, 0, 0);

    /* legs + shoes */
    for (int side = -1; side <= 1; side += 2) {
        float sw = side < 0 ? leg_sw : -leg_sw;
        glPushMatrix();
        glTranslatef(side * torso_w * 0.24f, hip_y, 0);
        glRotatef(sw * RAD2DEG, 1, 0, 0);
        part(PRIM_CUBE, d->bottom, 0, -leg_len * 0.45f, 0, limb_w * 1.1f, leg_len * 0.9f, limb_w * 1.15f);
        part(PRIM_CUBE, d->shoes, 0, -leg_len + 0.04f, 0.03f, limb_w * 1.2f, 0.09f, limb_w * 1.7f);
        glPopMatrix();
    }

    /* lower outfit */
    switch (d->outfit) {
    case OUT_ROBE:
        part(PRIM_FRUSTUM, d->top, 0, hip_y * 0.25f, 0, torso_w * 1.5f, hip_y * 0.85f, torso_d * 2.2f);
        break;
    case OUT_DRESS:
        part(PRIM_FRUSTUM, d->bottom, 0, hip_y * 0.12f, 0, torso_w * 1.9f, hip_y * 0.95f, torso_d * 2.8f);
        break;
    case OUT_MAID:
        part(PRIM_FRUSTUM, d->bottom, 0, hip_y * 0.15f, 0, torso_w * 1.8f, hip_y * 0.92f, torso_d * 2.6f);
        part(PRIM_CUBE, d->accent, 0, hip_y * 0.55f, torso_d * 0.62f, torso_w * 0.8f, hip_y * 0.7f, 0.02f);
        break;
    default:
        part(PRIM_CUBE, d->bottom, 0, hip_y - 0.02f, 0, torso_w * 1.02f, 0.12f, torso_d * 1.05f);
        break;
    }

    /* torso */
    part(PRIM_CUBE, d->top, 0, hip_y + torso_h * 0.5f, 0, torso_w, torso_h, torso_d);
    if (d->outfit == OUT_VEST)
        part(PRIM_CUBE, d->accent, 0, hip_y + torso_h * 0.5f, 0, torso_w * 1.06f, torso_h * 0.8f, torso_d * 1.1f);
    if (d->outfit == OUT_ROBE)
        part(PRIM_CUBE, d->accent, 0, sh_y, 0.01f, torso_w * 0.9f, 0.06f, torso_d * 1.15f);
    /* belt */
    part(PRIM_CUBE, 0x3A2A1AFF, 0, hip_y + 0.02f, 0, torso_w * 1.04f, 0.05f, torso_d * 1.08f);

    /* arms (+ weapon in right hand) */
    for (int side = -1; side <= 1; side += 2) {
        bool right = side < 0;   /* model faces +Z, so -X is its right hand */
        float sw = right ? r_arm : l_arm;
        float sp = right ? r_spread : l_spread;
        glPushMatrix();
        glTranslatef(side * (torso_w * 0.5f + limb_w * 0.55f), sh_y, 0);
        if (sp != 0) glRotatef(sp * RAD2DEG * side * -1.0f, 0, 0, 1);
        glRotatef(sw * RAD2DEG, 1, 0, 0);
        uint32_t sleeve = d->outfit == OUT_VEST ? d->top : (d->outfit == OUT_MAID ? d->top : d->top);
        part(PRIM_CUBE, sleeve, 0, -arm_len * 0.4f, 0, limb_w, arm_len * 0.8f, limb_w);
        part(PRIM_CUBE, d->skin, 0, -arm_len * 0.9f, 0, limb_w * 0.9f, arm_len * 0.22f, limb_w * 0.9f);
        if (right) draw_weapon(d, arm_len, H, anim);
        glPopMatrix();
    }

    /* head */
    part(PRIM_SPHERE, d->skin, 0, head_y, 0, head * 0.9f, head, head * 0.92f);
    /* eyes */
    float ey = head_y + head * 0.02f, ez = head * 0.42f, ex = head * 0.18f;
    part(PRIM_CUBE, d->eyes, ex, ey, ez, head * 0.1f, head * 0.16f, 0.03f);
    part(PRIM_CUBE, d->eyes, -ex, ey, ez, head * 0.1f, head * 0.16f, 0.03f);
    if (d->elf_ears) {
        part_r(PRIM_CONE, d->skin, head * 0.45f, head_y, 0, 0, 0, -1.3f, 0.07f, head * 0.35f, 0.05f);
        part_r(PRIM_CONE, d->skin, -head * 0.45f, head_y, 0, 0, 0, 1.3f, 0.07f, head * 0.35f, 0.05f);
    }
    if (d->gem)
        part(PRIM_OCTA, 0xFF3030FF, 0, head_y + head * 0.28f, head * 0.44f, 0.07f, 0.09f, 0.05f);

    /* hair */
    if (d->hair_style != HS_BALD) {
        part(PRIM_SPHERE, d->hair, 0, head_y + head * 0.1f, -head * 0.08f, head * 0.98f, head * 0.92f, head);
        switch (d->hair_style) {
        case HS_SPIKY:
            for (int i = -1; i <= 1; i++)
                part_r(PRIM_CONE, d->hair, i * head * 0.25f, head_y + head * 0.3f, -head * 0.2f,
                       -1.0f, 0, i * 0.4f, head * 0.3f, head * 0.45f, head * 0.3f);
            break;
        case HS_LONG:
            part(PRIM_CUBE, d->hair, 0, head_y - head * 0.45f, -head * 0.32f, head * 0.85f, head * 1.3f, head * 0.3f);
            break;
        case HS_BRAIDS:
            for (int s = -1; s <= 1; s += 2) {
                part(PRIM_CYLINDER, d->hair, s * head * 0.42f, head_y - head * 1.25f, -head * 0.1f,
                     head * 0.2f, head * 1.25f, head * 0.2f);
                part(PRIM_SPHERE, d->accent, s * head * 0.42f, head_y - head * 1.27f, -head * 0.1f,
                     head * 0.16f, head * 0.12f, head * 0.16f);
            }
            break;
        case HS_PONY:
            part_r(PRIM_CONE, d->hair, 0, head_y + head * 0.15f, -head * 0.5f, 2.6f, 0, 0,
                   head * 0.35f, head * 1.0f, head * 0.35f);
            break;
        case HS_BUN:
            part(PRIM_SPHERE, d->hair, 0, head_y + head * 0.25f, -head * 0.5f, head * 0.45f, head * 0.45f, head * 0.45f);
            break;
        default:
            break;
        }
    }

    switch (d->hat) {
    case HAT_WITCH:
        part(PRIM_DISC, 0x4A3222FF, 0, head_y + head * 0.4f, -head * 0.05f, head * 2.3f, 1, head * 2.3f);
        part_r(PRIM_CONE, 0x5A3E2BFF, 0, head_y + head * 0.38f, -head * 0.08f, -0.25f, 0, 0,
               head * 1.05f, head * 1.25f, head * 1.05f);
        part(PRIM_CYLINDER, 0xD8C890FF, 0, head_y + head * 0.4f, -head * 0.06f, head * 1.07f, head * 0.12f, head * 1.07f);
        break;
    case HAT_BAND:
        part(PRIM_CYLINDER, 0x6A2A2AFF, 0, head_y + head * 0.12f, 0, head * 0.95f, head * 0.12f, head * 0.97f);
        break;
    default:
        break;
    }

    glPopMatrix();
    cur_flash = 0;
}

/* ------------------------------------------------------------------ */
/* Creatures                                                           */
/* ------------------------------------------------------------------ */

void draw_wolf(vec3_t pos, float yaw, float t, bool moving, float flash, float scale)
{
    cur_flash = flash;
    const uint32_t fur = 0x76767EFF, dark = 0x4A4A54FF, belly = 0xB8B8C0FF;
    float sw = moving ? sinf(t) * 0.6f : 0;
    glPushMatrix();
    glTranslatef(pos.x, pos.y, pos.z);
    glRotatef(yaw * RAD2DEG, 0, 1, 0);
    glScalef(scale, scale, scale);

    part(PRIM_CUBE, fur, 0, 0.62f, 0, 0.42f, 0.4f, 1.0f);
    part(PRIM_CUBE, belly, 0, 0.47f, 0.05f, 0.34f, 0.12f, 0.8f);
    part(PRIM_CUBE, dark, 0, 0.84f, -0.05f, 0.2f, 0.08f, 0.8f);
    /* head */
    part(PRIM_CUBE, fur, 0, 0.82f, 0.62f, 0.36f, 0.34f, 0.36f);
    part(PRIM_CUBE, belly, 0, 0.74f, 0.86f, 0.2f, 0.16f, 0.28f);
    part(PRIM_CUBE, 0x202020FF, 0, 0.78f, 1.0f, 0.08f, 0.07f, 0.04f);
    part(PRIM_CUBE, 0xFFD040FF, 0.1f, 0.88f, 0.8f, 0.06f, 0.05f, 0.03f);
    part(PRIM_CUBE, 0xFFD040FF, -0.1f, 0.88f, 0.8f, 0.06f, 0.05f, 0.03f);
    part(PRIM_CONE, dark, 0.11f, 0.98f, 0.55f, 0.12f, 0.2f, 0.08f);
    part(PRIM_CONE, dark, -0.11f, 0.98f, 0.55f, 0.12f, 0.2f, 0.08f);
    /* tail */
    part_r(PRIM_CYLINDER, fur, 0, 0.7f, -0.48f, -2.2f + sinf(t * 2) * 0.2f, 0, 0, 0.12f, 0.5f, 0.12f);
    /* legs */
    for (int i = 0; i < 4; i++) {
        float lx = (i & 1) ? 0.14f : -0.14f;
        float lz = (i & 2) ? 0.36f : -0.36f;
        float s = ((i == 0 || i == 3) ? sw : -sw);
        limb(dark, lx, 0.48f, lz, s, 0, 0.11f, 0.48f, 0.12f);
    }
    glPopMatrix();
    cur_flash = 0;
}

void draw_boar(vec3_t pos, float yaw, float t, bool moving, float flash, float scale, bool boss)
{
    cur_flash = flash;
    uint32_t hide = boss ? 0x5A2A22FF : 0x6B4A30FF;
    uint32_t mane = boss ? 0x2A1410FF : 0x3E2A1CFF;
    uint32_t eye = boss ? 0xFF3020FF : 0x201010FF;
    float sw = moving ? sinf(t) * 0.5f : 0;
    glPushMatrix();
    glTranslatef(pos.x, pos.y, pos.z);
    glRotatef(yaw * RAD2DEG, 0, 1, 0);
    glScalef(scale, scale, scale);

    part(PRIM_SPHERE, hide, 0, 0.62f, 0, 0.8f, 0.72f, 1.25f);
    for (int i = 0; i < 4; i++)
        part_r(PRIM_CONE, mane, 0, 0.92f, 0.3f - i * 0.2f, -0.4f, 0, 0, 0.14f, 0.22f, 0.14f);
    /* head */
    part(PRIM_CUBE, hide, 0, 0.56f, 0.64f, 0.48f, 0.42f, 0.42f);
    part_r(PRIM_CYLINDER, 0xC08070FF, 0, 0.5f, 0.8f, 1.5708f, 0, 0, 0.24f, 0.14f, 0.2f);
    part(PRIM_CUBE, eye, 0.14f, 0.66f, 0.84f, 0.07f, 0.06f, 0.03f);
    part(PRIM_CUBE, eye, -0.14f, 0.66f, 0.84f, 0.07f, 0.06f, 0.03f);
    /* tusks */
    part_r(PRIM_CONE, 0xF0ECDCFF, 0.18f, 0.44f, 0.86f, 0.9f, 0, -0.4f, 0.07f, 0.28f, 0.07f);
    part_r(PRIM_CONE, 0xF0ECDCFF, -0.18f, 0.44f, 0.86f, 0.9f, 0, 0.4f, 0.07f, 0.28f, 0.07f);
    /* ears */
    part(PRIM_CONE, mane, 0.18f, 0.76f, 0.56f, 0.12f, 0.18f, 0.08f);
    part(PRIM_CONE, mane, -0.18f, 0.76f, 0.56f, 0.12f, 0.18f, 0.08f);
    /* legs */
    for (int i = 0; i < 4; i++) {
        float lx = (i & 1) ? 0.22f : -0.22f;
        float lz = (i & 2) ? 0.38f : -0.38f;
        float s = ((i == 0 || i == 3) ? sw : -sw);
        limb(mane, lx, 0.38f, lz, s, 0, 0.15f, 0.38f, 0.16f);
    }
    part_r(PRIM_CYLINDER, mane, 0, 0.7f, -0.6f, -2.0f, 0, 0, 0.06f, 0.25f, 0.06f);
    glPopMatrix();
    cur_flash = 0;
}

void draw_target(vec3_t pos, float yaw, float flash)
{
    cur_flash = flash;
    glPushMatrix();
    glTranslatef(pos.x, pos.y, pos.z);
    glRotatef(yaw * RAD2DEG, 0, 1, 0);
    part(PRIM_CYLINDER, 0x7A5432FF, 0, 0, 0, 0.14f, 1.7f, 0.14f);
    part(PRIM_CYLINDER, 0xD8C070FF, 0, 0.6f, 0, 0.55f, 0.75f, 0.45f);
    part(PRIM_CUBE, 0xD8C070FF, 0, 1.05f, 0, 0.9f, 0.14f, 0.18f);
    part(PRIM_SPHERE, 0xD8C070FF, 0, 1.6f, 0, 0.42f, 0.42f, 0.42f);
    /* painted target on the chest */
    part_r(PRIM_DISC, 0xF4F0E8FF, 0, 0.95f, 0.24f, 1.5708f, 0, 0, 0.42f, 1, 0.42f);
    part_r(PRIM_DISC, 0xD03030FF, 0, 0.95f, 0.25f, 1.5708f, 0, 0, 0.28f, 1, 0.28f);
    part_r(PRIM_DISC, 0xF4F0E8FF, 0, 0.95f, 0.26f, 1.5708f, 0, 0, 0.14f, 1, 0.14f);
    glPopMatrix();
    cur_flash = 0;
}

void draw_wisp(vec3_t pos, float t, float flash)
{
    cur_flash = flash;
    glDisable(GL_LIGHTING);
    float p = 1.0f + sinf(t * 5.0f) * 0.1f;
    part(PRIM_SPHERE, 0xC070FFFF, pos.x, pos.y, pos.z, 0.55f * p, 0.55f * p, 0.55f * p);
    part(PRIM_SPHERE, 0xFFE8FFFF, pos.x, pos.y, pos.z, 0.3f, 0.3f, 0.3f);
    for (int i = 0; i < 3; i++) {
        float a = t * 3.0f + i * TAU_F / 3;
        part(PRIM_OCTA, 0xE0A0FFFF, pos.x + cosf(a) * 0.6f, pos.y + sinf(a * 2) * 0.15f, pos.z + sinf(a) * 0.6f,
             0.16f, 0.22f, 0.16f);
    }
    glEnable(GL_LIGHTING);
    cur_flash = 0;
}

void draw_crystal(vec3_t pos, float t, float flash, float scale)
{
    cur_flash = flash;
    glPushMatrix();
    glTranslatef(pos.x, pos.y + sinf(t * 1.7f) * 0.15f, pos.z);
    glRotatef(t * 40.0f, 0, 1, 0);
    glScalef(scale, scale, scale);
    part(PRIM_OCTA, 0x9A50E0FF, 0, 1.2f, 0, 1.0f, 2.0f, 1.0f);
    glDisable(GL_LIGHTING);
    part(PRIM_OCTA, 0xF0C8FFFF, 0, 1.2f, 0, 0.45f, 1.1f, 0.45f);
    glEnable(GL_LIGHTING);
    for (int i = 0; i < 4; i++) {
        float a = i * TAU_F / 4 + t;
        part(PRIM_OCTA, 0x7A40C0FF, cosf(a) * 0.9f, 0.4f, sinf(a) * 0.9f, 0.3f, 0.6f, 0.3f);
    }
    glPopMatrix();
    cur_flash = 0;
}
