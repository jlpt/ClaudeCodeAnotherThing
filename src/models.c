/*
 * Low-poly characters and creatures, assembled from the unit primitives in
 * gfx.c with simple hierarchical (joint) animation - the classic early N64 look.
 *
 * Each model is built once at boot into rigid segments (body, arms, legs),
 * each a single display list with vertex colors, so animating a character
 * costs only a handful of matrix uploads per frame.
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

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void P(prim_t p, uint32_t col, float x, float y, float z, float sx, float sy, float sz)
{
    mb_prim(p, col, x, y, z, 0, 0, 0, sx, sy, sz);
}

static void PR(prim_t p, uint32_t col, float x, float y, float z, float rx, float ry, float rz,
               float sx, float sy, float sz)
{
    mb_prim(p, col, x, y, z, rx, ry, rz, sx, sy, sz);
}

static void begin_list(void)
{
    dl_begin();
    mb_begin(0xFFFFFFFF);
}

static dlist_t end_list(void)
{
    mb_end();
    return dl_end();
}

/* glMultMatrix(T * Ry * Rx * Rz * S) in one upload */
static void mult_trs(float x, float y, float z, float rx, float ry, float rz, float sc)
{
    float cx = cosf(rx), sx = sinf(rx), cy = cosf(ry), sy = sinf(ry), cz = cosf(rz), sz = sinf(rz);
    float r00 = cy * cz + sy * sx * sz, r01 = -cy * sz + sy * sx * cz, r02 = sy * cx;
    float r10 = cx * sz,                r11 = cx * cz,                 r12 = -sx;
    float r20 = -sy * cz + cy * sx * sz, r21 = sy * sz + cy * sx * cz, r22 = cy * cx;
    const GLfloat m[16] = {
        r00 * sc, r10 * sc, r20 * sc, 0,
        r01 * sc, r11 * sc, r21 * sc, 0,
        r02 * sc, r12 * sc, r22 * sc, 0,
        x, y, z, 1,
    };
    glMultMatrixf(m);
}

/* limb joint: T * Rz(spread) * Rx(swing) */
static void mult_joint(float x, float y, float z, float rz, float rx)
{
    float cx = cosf(rx), sx = sinf(rx), cz = cosf(rz), sz = sinf(rz);
    const GLfloat m[16] = {
        cz, sz, 0, 0,
        -sz * cx, cz * cx, sx, 0,
        sz * sx, -cz * sx, cx, 0,
        x, y, z, 1,
    };
    glMultMatrixf(m);
}

static void flash_begin(float flash)
{
    if (flash <= 0) return;
    float k = 0.6f + flash * 0.6f;
    GLfloat amb[4] = { k, k, k, 1 };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
}

static void flash_end(float flash)
{
    if (flash > 0) world_restore_ambient();
}

/* ------------------------------------------------------------------ */
/* Humans                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    dlist_t body, leg, arm_l, arm_r, arm_r_cast;
    float hip_y, leg_x, sh_y, arm_x;
} human_lists_t;

static human_lists_t HL[MDL_HUMAN_COUNT];

static void emit_weapon(const human_desc_t *d, float arm_len, float H, bool cast)
{
    /* the weapon hangs from the fist: (0, -arm_len) in arm space, tilted forward */
    float tilt = cast ? 0.0f : -80.0f * PI_F / 180.0f;
    float ct = cosf(tilt), st = sinf(tilt);
    /* place a part at (u along weapon axis) relative to the fist */
    #define AT(u) 0.0f, -arm_len + (u) * ct, 0.02f + (u) * st
    switch (d->weapon) {
    case W_STAFF:
    case W_STAFF_GEM: {
        float len = H * 0.95f;
        PR(PRIM_CYLINDER, 0x7A5432FF, AT(-len * 0.35f), tilt, 0, 0, 0.06f, len, 0.06f);
        uint32_t gem = d->weapon == W_STAFF_GEM ? 0x50A0FFFF : 0xE0C070FF;
        P(PRIM_OCTA, gem, AT(len * 0.65f + 0.04f), 0.14f, 0.18f, 0.14f);
        break;
    }
    case W_SWORD:
        PR(PRIM_CUBE, 0x5A4030FF, AT(0.0f), tilt, 0, 0, 0.06f, 0.2f, 0.06f);
        PR(PRIM_CUBE, 0xB0A060FF, AT(0.11f), tilt, 0, 0, 0.26f, 0.05f, 0.07f);
        PR(PRIM_CUBE, 0xD8DCE8FF, AT(0.55f), tilt, 0, 0, 0.08f, 0.8f, 0.03f);
        break;
    case W_SPEAR:
        PR(PRIM_CYLINDER, 0xE8E6DCFF, AT(-0.9f), tilt, 0, 0, 0.06f, 2.4f, 0.06f);
        PR(PRIM_CONE, 0xF4F4FFFF, AT(1.5f), tilt, 0, 0, 0.12f, 0.35f, 0.12f);
        break;
    default:
        break;
    }
    #undef AT
}

static void build_human(human_model_t mdl)
{
    const human_desc_t *d = &HUMANS[mdl];
    human_lists_t *L = &HL[mdl];
    const float H = d->height;
    const bool child = H < 1.3f;
    const float hip_y = H * (child ? 0.40f : 0.47f);
    const float leg_len = hip_y;
    const float torso_h = H * (child ? 0.28f : 0.31f);
    const float torso_w = H * 0.27f, torso_d = H * 0.16f;
    const float head = H * (child ? 0.30f : 0.22f);
    const float head_y = hip_y + torso_h + head * 0.52f;
    const float sh_y = hip_y + torso_h - 0.04f;
    const float arm_len = H * (child ? 0.33f : 0.36f);
    const float limb_w = H * 0.085f;

    L->hip_y = hip_y;
    L->leg_x = torso_w * 0.24f;
    L->sh_y = sh_y;
    L->arm_x = torso_w * 0.5f + limb_w * 0.55f;

    /* body: everything that moves with the root */
    begin_list();
    switch (d->outfit) {
    case OUT_ROBE:
        P(PRIM_FRUSTUM, d->top, 0, hip_y * 0.25f, 0, torso_w * 1.5f, hip_y * 0.85f, torso_d * 2.2f);
        break;
    case OUT_DRESS:
        P(PRIM_FRUSTUM, d->bottom, 0, hip_y * 0.12f, 0, torso_w * 1.9f, hip_y * 0.95f, torso_d * 2.8f);
        break;
    case OUT_MAID:
        P(PRIM_FRUSTUM, d->bottom, 0, hip_y * 0.15f, 0, torso_w * 1.8f, hip_y * 0.92f, torso_d * 2.6f);
        P(PRIM_CUBE, d->accent, 0, hip_y * 0.55f, torso_d * 0.62f, torso_w * 0.8f, hip_y * 0.7f, 0.02f);
        break;
    default:
        P(PRIM_CUBE, d->bottom, 0, hip_y - 0.02f, 0, torso_w * 1.02f, 0.12f, torso_d * 1.05f);
        break;
    }
    P(PRIM_SBOX, d->top, 0, hip_y + torso_h * 0.5f, 0, torso_w, torso_h, torso_d);
    if (d->outfit == OUT_VEST)
        P(PRIM_SBOX, d->accent, 0, hip_y + torso_h * 0.5f, 0, torso_w * 1.06f, torso_h * 0.8f, torso_d * 1.1f);
    if (d->outfit == OUT_ROBE)
        P(PRIM_SBOX, d->accent, 0, sh_y, 0.01f, torso_w * 0.9f, 0.06f, torso_d * 1.15f);
    P(PRIM_SBOX, 0x3A2A1AFF, 0, hip_y + 0.02f, 0, torso_w * 1.04f, 0.05f, torso_d * 1.08f);

    P(PRIM_SPHERE, d->skin, 0, head_y, 0, head * 0.9f, head, head * 0.92f);
    float ey = head_y + head * 0.02f, ez = head * 0.42f, ex = head * 0.18f;
    P(PRIM_QUAD, d->eyes, ex, ey, ez + 0.012f, head * 0.1f, head * 0.16f, 1);
    P(PRIM_QUAD, d->eyes, -ex, ey, ez + 0.012f, head * 0.1f, head * 0.16f, 1);
    if (d->elf_ears) {
        PR(PRIM_CONE, d->skin, head * 0.45f, head_y, 0, 0, 0, -1.3f, 0.07f, head * 0.35f, 0.05f);
        PR(PRIM_CONE, d->skin, -head * 0.45f, head_y, 0, 0, 0, 1.3f, 0.07f, head * 0.35f, 0.05f);
    }
    if (d->gem) P(PRIM_OCTA, 0xFF3030FF, 0, head_y + head * 0.28f, head * 0.44f, 0.07f, 0.09f, 0.05f);

    if (d->hair_style != HS_BALD) {
        P(PRIM_SPHERE, d->hair, 0, head_y + head * 0.1f, -head * 0.08f, head * 0.98f, head * 0.92f, head);
        switch (d->hair_style) {
        case HS_SPIKY:
            for (int i = -1; i <= 1; i++)
                PR(PRIM_CONE, d->hair, i * head * 0.25f, head_y + head * 0.3f, -head * 0.2f,
                   -1.0f, 0, i * 0.4f, head * 0.3f, head * 0.45f, head * 0.3f);
            break;
        case HS_LONG:
            P(PRIM_CUBE, d->hair, 0, head_y - head * 0.45f, -head * 0.32f, head * 0.85f, head * 1.3f, head * 0.3f);
            break;
        case HS_BRAIDS:
            for (int sd = -1; sd <= 1; sd += 2) {
                P(PRIM_CYLINDER, d->hair, sd * head * 0.42f, head_y - head * 1.25f, -head * 0.1f,
                  head * 0.2f, head * 1.25f, head * 0.2f);
                P(PRIM_CUBE, d->accent, sd * head * 0.42f, head_y - head * 1.27f, -head * 0.1f,
                  head * 0.16f, head * 0.1f, head * 0.16f);
            }
            break;
        case HS_PONY:
            PR(PRIM_CONE, d->hair, 0, head_y + head * 0.15f, -head * 0.5f, 2.6f, 0, 0,
               head * 0.35f, head * 1.0f, head * 0.35f);
            break;
        case HS_BUN:
            P(PRIM_SPHERE, d->hair, 0, head_y + head * 0.25f, -head * 0.5f, head * 0.45f, head * 0.45f, head * 0.45f);
            break;
        default:
            break;
        }
    }
    switch (d->hat) {
    case HAT_WITCH:
        P(PRIM_DISC, 0x4A3222FF, 0, head_y + head * 0.4f, -head * 0.05f, head * 2.3f, 1, head * 2.3f);
        PR(PRIM_CONE, 0x5A3E2BFF, 0, head_y + head * 0.38f, -head * 0.08f, -0.25f, 0, 0,
           head * 1.05f, head * 1.25f, head * 1.05f);
        P(PRIM_CYLINDER, 0xD8C890FF, 0, head_y + head * 0.4f, -head * 0.06f, head * 1.07f, head * 0.12f, head * 1.07f);
        break;
    case HAT_BAND:
        P(PRIM_CYLINDER, 0x6A2A2AFF, 0, head_y + head * 0.12f, 0, head * 0.95f, head * 0.12f, head * 0.97f);
        break;
    default:
        break;
    }
    L->body = end_list();

    /* leg in hip-joint space */
    begin_list();
    P(PRIM_SBOX, d->bottom, 0, -leg_len * 0.45f, 0, limb_w * 1.1f, leg_len * 0.9f, limb_w * 1.15f);
    P(PRIM_SBOX, d->shoes, 0, -leg_len + 0.04f, 0.03f, limb_w * 1.2f, 0.09f, limb_w * 1.7f);
    L->leg = end_list();

    /* arms in shoulder-joint space */
    for (int k = 0; k < 3; k++) {
        dlist_t l;
        begin_list();
        P(PRIM_SBOX, d->top, 0, -arm_len * 0.4f, 0, limb_w, arm_len * 0.8f, limb_w);
        P(PRIM_SBOX, d->skin, 0, -arm_len * 0.9f, 0, limb_w * 0.9f, arm_len * 0.22f, limb_w * 0.9f);
        if (k > 0) emit_weapon(d, arm_len, H, k == 2);
        l = end_list();
        if (k == 0) L->arm_l = l; else if (k == 1) L->arm_r = l; else L->arm_r_cast = l;
    }
}

void draw_human(human_model_t m, vec3_t pos, float yaw, anim_t anim, float t, float flash)
{
    const human_lists_t *L = &HL[m];

    float leg_sw = 0, bob = 0, lean = 0;
    float r_arm = 0, l_arm = 0, r_spread = 0, l_spread = 0;
    switch (anim) {
    case ANIM_WALK: leg_sw = sinf(t) * 0.55f; bob = fabsf(cosf(t)) * 0.03f; break;
    case ANIM_RUN:  leg_sw = sinf(t) * 0.85f; bob = fabsf(cosf(t)) * 0.06f; lean = 0.12f; break;
    case ANIM_IDLE: bob = sinf(t * 0.5f) * 0.008f; break;
    case ANIM_HURT: lean = -0.3f; r_spread = -0.5f; l_spread = 0.5f; break;
    default: break;
    }
    r_arm = -leg_sw * 0.8f;
    l_arm = leg_sw * 0.8f;
    if (anim == ANIM_SWING) {
        float k = clampf(t, 0, 1);
        r_arm = -2.6f + k * 3.2f;
        lean = 0.15f * k;
    } else if (anim == ANIM_CAST) {
        r_arm = -1.45f; l_arm = -0.4f; l_spread = 0.2f;
    } else if (anim == ANIM_WAVE) {
        r_arm = -2.8f; r_spread = -0.3f + sinf(t * 6.0f) * 0.35f;
    }

    flash_begin(flash);
    glPushMatrix();
    mult_trs(pos.x, pos.y + bob, pos.z, lean, yaw, 0, 1.0f);
    dl_call(L->body);
    for (int side = -1; side <= 1; side += 2) {
        glPushMatrix();
        mult_joint(side * L->leg_x, L->hip_y, 0, 0, side < 0 ? leg_sw : -leg_sw);
        dl_call(L->leg);
        glPopMatrix();
    }
    /* the model faces +Z, so its right hand is on -X */
    glPushMatrix();
    mult_joint(-L->arm_x, L->sh_y, 0, r_spread, r_arm);
    dl_call(anim == ANIM_CAST ? L->arm_r_cast : L->arm_r);
    glPopMatrix();
    glPushMatrix();
    mult_joint(L->arm_x, L->sh_y, 0, -l_spread, l_arm);
    dl_call(L->arm_l);
    glPopMatrix();
    glPopMatrix();
    flash_end(flash);
}

/* ------------------------------------------------------------------ */
/* Creatures                                                           */
/* ------------------------------------------------------------------ */

static dlist_t wolf_body, wolf_leg, wolf_tail;
static dlist_t boar_body[2], boar_leg[2], boar_tail[2];
static dlist_t target_list;

static void build_creatures(void)
{
    const uint32_t fur = 0x76767EFF, dark = 0x4A4A54FF, belly = 0xB8B8C0FF;
    begin_list();
    P(PRIM_CUBE, fur, 0, 0.62f, 0, 0.42f, 0.4f, 1.0f);
    P(PRIM_CUBE, belly, 0, 0.47f, 0.05f, 0.34f, 0.12f, 0.8f);
    P(PRIM_CUBE, dark, 0, 0.84f, -0.05f, 0.2f, 0.08f, 0.8f);
    P(PRIM_CUBE, fur, 0, 0.82f, 0.62f, 0.36f, 0.34f, 0.36f);
    P(PRIM_CUBE, belly, 0, 0.74f, 0.86f, 0.2f, 0.16f, 0.28f);
    P(PRIM_CUBE, 0x202020FF, 0, 0.78f, 1.0f, 0.08f, 0.07f, 0.04f);
    P(PRIM_QUAD, 0xFFD040FF, 0.1f, 0.88f, 0.805f, 0.06f, 0.05f, 1);
    P(PRIM_QUAD, 0xFFD040FF, -0.1f, 0.88f, 0.805f, 0.06f, 0.05f, 1);
    P(PRIM_CONE, dark, 0.11f, 0.98f, 0.55f, 0.12f, 0.2f, 0.08f);
    P(PRIM_CONE, dark, -0.11f, 0.98f, 0.55f, 0.12f, 0.2f, 0.08f);
    wolf_body = end_list();
    begin_list();
    P(PRIM_SBOX, dark, 0, -0.24f, 0, 0.11f, 0.48f, 0.12f);
    wolf_leg = end_list();
    begin_list();
    P(PRIM_CYLINDER, fur, 0, 0, 0, 0.12f, 0.5f, 0.12f);
    wolf_tail = end_list();

    for (int boss = 0; boss < 2; boss++) {
        uint32_t hide = boss ? 0x5A2A22FF : 0x6B4A30FF;
        uint32_t mane = boss ? 0x2A1410FF : 0x3E2A1CFF;
        uint32_t eye = boss ? 0xFF3020FF : 0x201010FF;
        begin_list();
        P(PRIM_SPHERE, hide, 0, 0.62f, 0, 0.8f, 0.72f, 1.25f);
        for (int i = 0; i < 4; i++)
            PR(PRIM_CONE, mane, 0, 0.92f, 0.3f - i * 0.2f, -0.4f, 0, 0, 0.14f, 0.22f, 0.14f);
        P(PRIM_CUBE, hide, 0, 0.56f, 0.64f, 0.48f, 0.42f, 0.42f);
        PR(PRIM_CYLINDER, 0xC08070FF, 0, 0.5f, 0.8f, 1.5708f, 0, 0, 0.24f, 0.14f, 0.2f);
        P(PRIM_QUAD, eye, 0.14f, 0.66f, 0.855f, 0.07f, 0.06f, 1);
        P(PRIM_QUAD, eye, -0.14f, 0.66f, 0.855f, 0.07f, 0.06f, 1);
        PR(PRIM_CONE, 0xF0ECDCFF, 0.18f, 0.44f, 0.86f, 0.9f, 0, -0.4f, 0.07f, 0.28f, 0.07f);
        PR(PRIM_CONE, 0xF0ECDCFF, -0.18f, 0.44f, 0.86f, 0.9f, 0, 0.4f, 0.07f, 0.28f, 0.07f);
        P(PRIM_CONE, mane, 0.18f, 0.76f, 0.56f, 0.12f, 0.18f, 0.08f);
        P(PRIM_CONE, mane, -0.18f, 0.76f, 0.56f, 0.12f, 0.18f, 0.08f);
        boar_body[boss] = end_list();
        begin_list();
        P(PRIM_SBOX, mane, 0, -0.19f, 0, 0.15f, 0.38f, 0.16f);
        boar_leg[boss] = end_list();
        begin_list();
        P(PRIM_CYLINDER, mane, 0, 0, 0, 0.06f, 0.25f, 0.06f);
        boar_tail[boss] = end_list();
    }

    begin_list();
    P(PRIM_CYLINDER, 0x7A5432FF, 0, 0, 0, 0.14f, 1.7f, 0.14f);
    P(PRIM_CYLINDER, 0xD8C070FF, 0, 0.6f, 0, 0.55f, 0.75f, 0.45f);
    P(PRIM_CUBE, 0xD8C070FF, 0, 1.05f, 0, 0.9f, 0.14f, 0.18f);
    P(PRIM_SPHERE, 0xD8C070FF, 0, 1.6f, 0, 0.42f, 0.42f, 0.42f);
    PR(PRIM_DISC, 0xF4F0E8FF, 0, 0.95f, 0.24f, 1.5708f, 0, 0, 0.42f, 1, 0.42f);
    PR(PRIM_DISC, 0xD03030FF, 0, 0.95f, 0.25f, 1.5708f, 0, 0, 0.28f, 1, 0.28f);
    PR(PRIM_DISC, 0xF4F0E8FF, 0, 0.95f, 0.26f, 1.5708f, 0, 0, 0.14f, 1, 0.14f);
    target_list = end_list();
}

void models_init(void)
{
    for (int m = 0; m < MDL_HUMAN_COUNT; m++) build_human((human_model_t)m);
    build_creatures();
}

static void draw_quadruped(dlist_t body, dlist_t leg, dlist_t tail, vec3_t pos, float yaw, float scale,
                           float t, bool moving, float flash, float leg_dx, float leg_dz, float hip_y,
                           vec3_t tail_at, float tail_rx)
{
    float sw = moving ? sinf(t) * 0.55f : 0;
    flash_begin(flash);
    if (scale != 1.0f) glEnable(GL_NORMALIZE);
    glPushMatrix();
    mult_trs(pos.x, pos.y, pos.z, 0, yaw, 0, scale);
    dl_call(body);
    for (int i = 0; i < 4; i++) {
        float lx = (i & 1) ? leg_dx : -leg_dx;
        float lz = (i & 2) ? leg_dz : -leg_dz;
        glPushMatrix();
        mult_joint(lx, hip_y, lz, 0, (i == 0 || i == 3) ? sw : -sw);
        dl_call(leg);
        glPopMatrix();
    }
    glPushMatrix();
    mult_joint(tail_at.x, tail_at.y, tail_at.z, 0, tail_rx);
    dl_call(tail);
    glPopMatrix();
    glPopMatrix();
    if (scale != 1.0f) glDisable(GL_NORMALIZE);
    flash_end(flash);
}

void draw_wolf(vec3_t pos, float yaw, float t, bool moving, float flash, float scale)
{
    draw_quadruped(wolf_body, wolf_leg, wolf_tail, pos, yaw, scale, t, moving, flash,
                   0.14f, 0.36f, 0.48f, v3(0, 0.7f, -0.48f), -2.2f + sinf(t * 2) * 0.2f);
}

void draw_boar(vec3_t pos, float yaw, float t, bool moving, float flash, float scale, bool boss)
{
    int b = boss ? 1 : 0;
    draw_quadruped(boar_body[b], boar_leg[b], boar_tail[b], pos, yaw, scale, t, moving, flash,
                   0.22f, 0.38f, 0.38f, v3(0, 0.7f, -0.6f), -2.0f);
}

void draw_target(vec3_t pos, float yaw, float flash)
{
    flash_begin(flash);
    glPushMatrix();
    mult_trs(pos.x, pos.y, pos.z, 0, yaw, 0, 1.0f);
    dl_call(target_list);
    glPopMatrix();
    flash_end(flash);
}

void draw_wisp(vec3_t pos, float t, float flash)
{
    glDisable(GL_LIGHTING);
    float p = 1.0f + sinf(t * 5.0f) * 0.1f;
    uint32_t outer = flash > 0 ? 0xFFE0FFFF : 0xC070FFFF;
    gfx_part(PRIM_SPHERE, outer, pos.x, pos.y, pos.z, 0.55f * p, 0.55f * p, 0.55f * p);
    for (int i = 0; i < 3; i++) {
        float a = t * 3.0f + i * TAU_F / 3;
        gfx_part(PRIM_OCTA, 0xE0A0FFFF, pos.x + cosf(a) * 0.6f, pos.y + sinf(a * 2) * 0.15f, pos.z + sinf(a) * 0.6f,
                 0.16f, 0.22f, 0.16f);
    }
    glEnable(GL_LIGHTING);
}

void draw_crystal(vec3_t pos, float t, float flash, float scale)
{
    flash_begin(flash);
    glPushMatrix();
    mult_trs(pos.x, pos.y + sinf(t * 1.7f) * 0.15f, pos.z, 0, t * 0.7f, 0, scale);
    gfx_part(PRIM_OCTA, 0x9A50E0FF, 0, 1.2f, 0, 1.0f, 2.0f, 1.0f);
    for (int i = 0; i < 4; i++) {
        float a = i * TAU_F / 4 + t;
        gfx_part(PRIM_OCTA, 0x7A40C0FF, cosf(a) * 0.9f, 0.4f, sinf(a) * 0.9f, 0.3f, 0.6f, 0.3f);
    }
    glPopMatrix();
    flash_end(flash);
}
