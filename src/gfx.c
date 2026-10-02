/*
 * Rendering core: textures, unit primitives compiled into display lists,
 * camera matrices, billboards and the 2D (rdpq) helpers used by the HUD.
 */
#include "game.h"

camera_t g_cam;

static GLuint textures[TEX_COUNT];
static dlist_t prim_lists[PRIM_COUNT];
static int bound_tex = -1;
static sprite_t *spr_circle, *spr_ring, *spr_logo;

static const char *const TEX_FILES[TEX_COUNT] = {
    "rom:/tex_grass.sprite", "rom:/tex_dirt.sprite", "rom:/tex_wood.sprite",
    "rom:/tex_stone.sprite", "rom:/tex_roof.sprite", "rom:/tex_bark.sprite",
    "rom:/tex_leaves.sprite", "rom:/tex_plaster.sprite", "rom:/tex_water.sprite",
    "rom:/fx_glow.sprite",
};

uint32_t color_mul(uint32_t c, float k)
{
    int r = (int)((c >> 24) * k), g = (int)(((c >> 16) & 0xFF) * k), b = (int)(((c >> 8) & 0xFF) * k);
    r = r > 255 ? 255 : (r < 0 ? 0 : r);
    g = g > 255 ? 255 : (g < 0 ? 0 : g);
    b = b > 255 ? 255 : (b < 0 ? 0 : b);
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (c & 0xFF);
}

uint32_t color_mix(uint32_t a, uint32_t b, float t)
{
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        out |= ((uint32_t)(ca + (cb - ca) * t) & 0xFF) << sh;
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Primitive construction                                              */
/* ------------------------------------------------------------------ */

typedef struct { vec3_t p, n; float u, v; } vtx_t;

static inline vtx_t V(vec3_t p, vec3_t n, float u, float v) { return (vtx_t){p, n, u, v}; }

/*
 * Mesh batching. Triangles are collected into a vertex/index buffer with
 * duplicate vertices merged, then submitted with glDrawElements so the RSP's
 * vertex cache can reuse transformed vertices (immediate mode would transform
 * three vertices for every triangle). Recorded inside display lists, the
 * vertex data is copied into the list, so the buffers are reused.
 */
typedef gfx_vtx_t mvtx_t;

#define MB_MAX_V   1024
#define MB_MAX_I   3072
#define MB_HASH    2048

static mvtx_t *mb_v;
static uint16_t *mb_i;
static int16_t *mb_hash;
static int mb_nv, mb_ni;
static bool mb_any_color;
static uint32_t mb_color = 0;   /* 0 = use the current GL color when the list runs */

static void mb_reset(void)
{
    if (!mb_v) {
        mb_v = malloc(sizeof(mvtx_t) * MB_MAX_V);
        mb_i = malloc(sizeof(uint16_t) * MB_MAX_I);
        mb_hash = malloc(sizeof(int16_t) * MB_HASH);
    }
    mb_nv = mb_ni = 0;
    mb_any_color = false;
    memset(mb_hash, 0xFF, sizeof(int16_t) * MB_HASH);
}

void gfx_draw_indexed(const void *verts, int nv, const uint16_t *idx, int ni, bool color, bool normals)
{
    /* Normals are always supplied so that only two vertex formats exist
       (with and without per-vertex color): every format switch makes the
       RSP pipeline regenerate its vertex loader. */
    (void)nv; (void)normals;
    const mvtx_t *v = verts;
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(mvtx_t), v[0].p);
    glTexCoordPointer(2, GL_FLOAT, sizeof(mvtx_t), v[0].t);
    glNormalPointer(GL_FLOAT, sizeof(mvtx_t), v[0].n);
    if (color) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(mvtx_t), &v[0].c);
    }
    glDrawElements(GL_TRIANGLES, ni, GL_UNSIGNED_SHORT, idx);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
}

static int mb_total;              /* indices emitted since mb_begin */

/* builder transform: p' = M p + t, n' = N n (N = inverse transpose of M) */
static float xb[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };   /* base (row-major 3x4) */
static float xl[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };   /* local part */
static float xm[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };   /* effective = base * local */
static float xn[9]  = { 1,0,0, 0,1,0, 0,0,1 };
static bool x_ident = true;

/* optional lighting baked into vertex colors */
static bool bake_on;
static vec3_t bake_sun;
static float bake_sun_rgb[3], bake_amb_rgb[3];

static void xf_update(void)
{
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++)
            xm[r * 4 + c] = xb[r * 4 + 0] * xl[0 * 4 + c] + xb[r * 4 + 1] * xl[1 * 4 + c] + xb[r * 4 + 2] * xl[2 * 4 + c];
        xm[r * 4 + 3] = xb[r * 4 + 0] * xl[3] + xb[r * 4 + 1] * xl[7] + xb[r * 4 + 2] * xl[11] + xb[r * 4 + 3];
    }
    /* inverse transpose of the 3x3 part (cofactor matrix; the scale is irrelevant since we normalize) */
    const float a = xm[0], b = xm[1], c = xm[2], d = xm[4], e = xm[5], f = xm[6], g = xm[8], h = xm[9], i = xm[10];
    xn[0] = e * i - f * h; xn[1] = f * g - d * i; xn[2] = d * h - e * g;
    xn[3] = c * h - b * i; xn[4] = a * i - c * g; xn[5] = b * g - a * h;
    xn[6] = b * f - c * e; xn[7] = c * d - a * f; xn[8] = a * e - b * d;
    x_ident = false;
}

static void xf_compose(float *out, float x, float y, float z, float rx, float ry, float rz, float sx, float sy, float sz)
{
    /* T * Ry * Rx * Rz * S, row-major 3x4 */
    float cx = cosf(rx), sxn = sinf(rx), cy = cosf(ry), syn = sinf(ry), cz = cosf(rz), szn = sinf(rz);
    float r[9] = {
        cy * cz + syn * sxn * szn, -cy * szn + syn * sxn * cz, syn * cx,
        cx * szn,                  cx * cz,                    -sxn,
        -syn * cz + cy * sxn * szn, syn * szn + cy * sxn * cz, cy * cx,
    };
    for (int row = 0; row < 3; row++) {
        out[row * 4 + 0] = r[row * 3 + 0] * sx;
        out[row * 4 + 1] = r[row * 3 + 1] * sy;
        out[row * 4 + 2] = r[row * 3 + 2] * sz;
    }
    out[3] = x; out[7] = y; out[11] = z;
}

void mb_base(float x, float y, float z, float ry, float scale)
{
    xf_compose(xb, x, y, z, 0, ry, 0, scale, scale, scale);
    xf_update();
}

void mb_base_identity(void)
{
    static const float I[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
    memcpy(xb, I, sizeof(I));
    memcpy(xl, I, sizeof(I));
    memcpy(xm, I, sizeof(I));
    static const float NI[9] = { 1,0,0, 0,1,0, 0,0,1 };
    memcpy(xn, NI, sizeof(NI));
    x_ident = true;
}

void mb_bake_lighting(bool on, vec3_t sun_dir, uint32_t sun, uint32_t ambient)
{
    bake_on = on;
    bake_sun = v3_norm(sun_dir);
    for (int k = 0; k < 3; k++) {
        bake_sun_rgb[k] = ((sun >> (24 - 8 * k)) & 0xFF) / 255.0f;
        bake_amb_rgb[k] = ((ambient >> (24 - 8 * k)) & 0xFF) / 255.0f;
    }
}

static bool mb_dry;
void mb_set_dry(bool on) { mb_dry = on; }

static void mb_flush(void)
{
    if (mb_ni > 0 && !mb_dry) gfx_draw_indexed(mb_v, mb_nv, mb_i, mb_ni, mb_any_color || bake_on, !bake_on);
    mb_total += mb_ni;
}

int mb_emitted(void) { return mb_total + mb_ni; }

static uint16_t mb_add(const mvtx_t *m)
{
    const uint32_t *w = (const uint32_t *)m;
    uint32_t h = 2166136261u;
    for (unsigned k = 0; k < sizeof(mvtx_t) / 4; k++) h = (h ^ w[k]) * 16777619u;
    h &= MB_HASH - 1;
    while (mb_hash[h] >= 0) {
        if (!memcmp(&mb_v[mb_hash[h]], m, sizeof(mvtx_t))) return (uint16_t)mb_hash[h];
        h = (h + 1) & (MB_HASH - 1);
    }
    mb_v[mb_nv] = *m;
    mb_hash[h] = (int16_t)mb_nv;
    return (uint16_t)mb_nv++;
}

static void put_vtx(vtx_t v)
{
    if (mb_nv + 1 > MB_MAX_V || mb_ni + 1 > MB_MAX_I) mb_flush();
    if (!x_ident) {
        vec3_t p = v.p, n = v.n;
        v.p = v3(xm[0] * p.x + xm[1] * p.y + xm[2] * p.z + xm[3],
                 xm[4] * p.x + xm[5] * p.y + xm[6] * p.z + xm[7],
                 xm[8] * p.x + xm[9] * p.y + xm[10] * p.z + xm[11]);
        v.n = v3_norm(v3(xn[0] * n.x + xn[1] * n.y + xn[2] * n.z,
                         xn[3] * n.x + xn[4] * n.y + xn[5] * n.z,
                         xn[6] * n.x + xn[7] * n.y + xn[8] * n.z));
    }
    uint32_t col = mb_color ? mb_color : 0xFFFFFFFF;
    if (bake_on) {
        float d = fmaxf(0.0f, v3_dot(v.n, bake_sun));
        uint32_t out = col & 0xFF;
        for (int k = 0; k < 3; k++) {
            float ch = ((col >> (24 - 8 * k)) & 0xFF) * (bake_amb_rgb[k] + bake_sun_rgb[k] * d);
            out |= (uint32_t)(ch > 255 ? 255 : ch) << (24 - 8 * k);
        }
        col = out;
        v.n = v3(0, 1, 0);   /* normals are not uploaded for baked meshes */
    }
    mvtx_t m = { { v.p.x, v.p.y, v.p.z }, { v.u, v.v }, { v.n.x, v.n.y, v.n.z }, col };
    if (mb_color) mb_any_color = true;
    mb_i[mb_ni++] = mb_add(&m);
}

/* Emit a triangle, fixing the winding so that it faces along its normals. */
static void emit_tri(vtx_t a, vtx_t b, vtx_t c)
{
    vec3_t fn = v3_cross(v3_sub(b.p, a.p), v3_sub(c.p, a.p));
    vec3_t avg = v3_add(v3_add(a.n, b.n), c.n);
    if (v3_dot(fn, avg) < 0) { vtx_t t = b; b = c; c = t; }
    /* keep the three vertices of a triangle in the same batch */
    if (mb_nv + 3 > MB_MAX_V || mb_ni + 3 > MB_MAX_I) mb_flush();
    put_vtx(a); put_vtx(b); put_vtx(c);
}

static void emit_quad(vtx_t a, vtx_t b, vtx_t c, vtx_t d)
{
    emit_tri(a, b, c);
    emit_tri(a, c, d);
}

static void emit_cube(void)
{
    static const float N[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    for (int f = 0; f < 6; f++) {
        vec3_t n = v3(N[f][0], N[f][1], N[f][2]);
        /* two tangent axes */
        vec3_t t1 = fabsf(n.y) > 0.5f ? v3(1, 0, 0) : v3(0, 1, 0);
        vec3_t t2 = v3_cross(n, t1);
        vec3_t c = v3_scale(n, 0.5f);
        vec3_t p[4];
        for (int i = 0; i < 4; i++) {
            float s1 = (i == 1 || i == 2) ? 0.5f : -0.5f;
            float s2 = (i >= 2) ? 0.5f : -0.5f;
            p[i] = v3_add(c, v3_add(v3_scale(t1, s1), v3_scale(t2, s2)));
        }
        emit_quad(V(p[0], n, 0, 0), V(p[1], n, 1, 0), V(p[2], n, 1, 1), V(p[3], n, 0, 1));
    }
}

static void emit_sphere(void)
{
    const int SEG = 7, RING = 4;
    for (int r = 0; r < RING; r++) {
        float a0 = PI_F * r / RING - PI_F / 2, a1 = PI_F * (r + 1) / RING - PI_F / 2;
        for (int s = 0; s < SEG; s++) {
            float b0 = TAU_F * s / SEG, b1 = TAU_F * (s + 1) / SEG;
            vec3_t n00 = v3(cosf(a0) * cosf(b0), sinf(a0), cosf(a0) * sinf(b0));
            vec3_t n01 = v3(cosf(a0) * cosf(b1), sinf(a0), cosf(a0) * sinf(b1));
            vec3_t n10 = v3(cosf(a1) * cosf(b0), sinf(a1), cosf(a1) * sinf(b0));
            vec3_t n11 = v3(cosf(a1) * cosf(b1), sinf(a1), cosf(a1) * sinf(b1));
            float u0 = 2.0f * s / SEG, u1 = 2.0f * (s + 1) / SEG;
            float v0 = (float)r / RING, v1 = (float)(r + 1) / RING;
            vtx_t A = V(v3_scale(n00, 0.5f), n00, u0, v0);
            vtx_t B = V(v3_scale(n01, 0.5f), n01, u1, v0);
            vtx_t C = V(v3_scale(n11, 0.5f), n11, u1, v1);
            vtx_t D = V(v3_scale(n10, 0.5f), n10, u0, v1);
            if (r == 0)              emit_tri(A, C, D);
            else if (r == RING - 1)  emit_tri(A, B, C);
            else                     emit_quad(A, B, C, D);
        }
    }
}

static void emit_cylinder(void)
{
    const int SEG = 6;
    for (int s = 0; s < SEG; s++) {
        float b0 = TAU_F * s / SEG, b1 = TAU_F * (s + 1) / SEG;
        vec3_t n0 = v3(cosf(b0), 0, sinf(b0)), n1 = v3(cosf(b1), 0, sinf(b1));
        vec3_t p0 = v3(n0.x * 0.5f, 0, n0.z * 0.5f), p1 = v3(n1.x * 0.5f, 0, n1.z * 0.5f);
        vec3_t q0 = v3(p0.x, 1, p0.z), q1 = v3(p1.x, 1, p1.z);
        float u0 = 2.0f * s / SEG, u1 = 2.0f * (s + 1) / SEG;
        emit_quad(V(p0, n0, u0, 1), V(p1, n1, u1, 1), V(q1, n1, u1, 0), V(q0, n0, u0, 0));
        /* caps */
        vec3_t up = v3(0, 1, 0), dn = v3(0, -1, 0);
        emit_tri(V(v3(0, 1, 0), up, 0.5f, 0.5f), V(q0, up, 0.5f + n0.x * 0.5f, 0.5f + n0.z * 0.5f),
                 V(q1, up, 0.5f + n1.x * 0.5f, 0.5f + n1.z * 0.5f));
        emit_tri(V(v3(0, 0, 0), dn, 0.5f, 0.5f), V(p0, dn, 0.5f + n0.x * 0.5f, 0.5f + n0.z * 0.5f),
                 V(p1, dn, 0.5f + n1.x * 0.5f, 0.5f + n1.z * 0.5f));
    }
}

static void emit_cone(void)
{
    const int SEG = 6;
    for (int s = 0; s < SEG; s++) {
        float b0 = TAU_F * s / SEG, b1 = TAU_F * (s + 1) / SEG, bm = (b0 + b1) * 0.5f;
        vec3_t n0 = v3_norm(v3(cosf(b0), 0.5f, sinf(b0)));
        vec3_t n1 = v3_norm(v3(cosf(b1), 0.5f, sinf(b1)));
        vec3_t nm = v3_norm(v3(cosf(bm), 0.5f, sinf(bm)));
        vec3_t p0 = v3(cosf(b0) * 0.5f, 0, sinf(b0) * 0.5f), p1 = v3(cosf(b1) * 0.5f, 0, sinf(b1) * 0.5f);
        float u0 = 2.0f * s / SEG, u1 = 2.0f * (s + 1) / SEG;
        emit_tri(V(p0, n0, u0, 1), V(p1, n1, u1, 1), V(v3(0, 1, 0), nm, (u0 + u1) * 0.5f, 0));
        vec3_t dn = v3(0, -1, 0);
        emit_tri(V(v3(0, 0, 0), dn, 0.5f, 0.5f), V(p0, dn, 0.5f + cosf(b0) * 0.5f, 0.5f + sinf(b0) * 0.5f),
                 V(p1, dn, 0.5f + cosf(b1) * 0.5f, 0.5f + sinf(b1) * 0.5f));
    }
}

/* cone with the tip cut off: radius 0.5 at y=0, 0.25 at y=1 (robes, skirts) */
static void emit_frustum(void)
{
    const int SEG = 8;
    const float rb = 0.5f, rt = 0.25f;
    for (int s = 0; s < SEG; s++) {
        float b0 = TAU_F * s / SEG, b1 = TAU_F * (s + 1) / SEG;
        vec3_t n0 = v3_norm(v3(cosf(b0), rb - rt, sinf(b0)));
        vec3_t n1 = v3_norm(v3(cosf(b1), rb - rt, sinf(b1)));
        vec3_t p0 = v3(cosf(b0) * rb, 0, sinf(b0) * rb), p1 = v3(cosf(b1) * rb, 0, sinf(b1) * rb);
        vec3_t q0 = v3(cosf(b0) * rt, 1, sinf(b0) * rt), q1 = v3(cosf(b1) * rt, 1, sinf(b1) * rt);
        float u0 = 2.0f * s / SEG, u1 = 2.0f * (s + 1) / SEG;
        emit_quad(V(p0, n0, u0, 1), V(p1, n1, u1, 1), V(q1, n1, u1, 0), V(q0, n0, u0, 0));
        vec3_t up = v3(0, 1, 0), dn = v3(0, -1, 0);
        emit_tri(V(v3(0, 1, 0), up, 0.5f, 0.5f), V(q0, up, 0, 0), V(q1, up, 1, 0));
        emit_tri(V(v3(0, 0, 0), dn, 0.5f, 0.5f), V(p0, dn, 0, 1), V(p1, dn, 1, 1));
    }
}

/* smooth-shaded box: 8 shared vertices instead of 24 (limbs, torsos) */
static void emit_sbox(void)
{
    vtx_t c[8];
    for (int i = 0; i < 8; i++) {
        vec3_t p = v3((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f);
        c[i] = V(p, v3_norm(p), (i & 1) ? 1.0f : 0.0f, (i & 2) ? 0.0f : 1.0f);
    }
    static const uint8_t F[6][4] = { {1,3,7,5}, {0,4,6,2}, {2,6,7,3}, {0,1,5,4}, {4,5,7,6}, {0,2,3,1} };
    for (int f = 0; f < 6; f++) emit_quad(c[F[f][0]], c[F[f][1]], c[F[f][2]], c[F[f][3]]);
}

/* unit quad in the XY plane facing +Z (eyes, decals) */
static void emit_quadp(void)
{
    vec3_t n = v3(0, 0, 1);
    emit_quad(V(v3(-0.5f, -0.5f, 0), n, 0, 1), V(v3(0.5f, -0.5f, 0), n, 1, 1),
              V(v3(0.5f, 0.5f, 0), n, 1, 0), V(v3(-0.5f, 0.5f, 0), n, 0, 0));
}

static void emit_disc(void)
{
    const int SEG = 8;
    vec3_t up = v3(0, 1, 0);
    for (int s = 0; s < SEG; s++) {
        float b0 = TAU_F * s / SEG, b1 = TAU_F * (s + 1) / SEG;
        emit_tri(V(v3(0, 0, 0), up, 0.5f, 0.5f),
                 V(v3(cosf(b0) * 0.5f, 0, sinf(b0) * 0.5f), up, 0.5f + cosf(b0) * 0.5f, 0.5f + sinf(b0) * 0.5f),
                 V(v3(cosf(b1) * 0.5f, 0, sinf(b1) * 0.5f), up, 0.5f + cosf(b1) * 0.5f, 0.5f + sinf(b1) * 0.5f));
    }
}

static void emit_octa(void)
{
    vec3_t top = v3(0, 0.5f, 0), bot = v3(0, -0.5f, 0);
    vec3_t ring[4] = { v3(0.5f, 0, 0), v3(0, 0, 0.5f), v3(-0.5f, 0, 0), v3(0, 0, -0.5f) };
    mb_reset();
    for (int i = 0; i < 4; i++) {
        vec3_t a = ring[i], b = ring[(i + 1) & 3];
        vec3_t nt = v3_norm(v3_add(v3_add(a, b), top));
        vec3_t nb = v3_norm(v3_add(v3_add(a, b), bot));
        emit_tri(V(a, nt, 0, 1), V(b, nt, 1, 1), V(top, nt, 0.5f, 0));
        emit_tri(V(a, nb, 0, 0), V(b, nb, 1, 0), V(bot, nb, 0.5f, 1));
    }
}

/* ------------------------------------------------------------------ */
/* Mesh builder (used to compile props into display lists)             */
/* ------------------------------------------------------------------ */

void mb_begin(uint32_t color) { mb_reset(); mb_total = 0; mb_color = color; }

static void (*const PRIM_EMIT[PRIM_COUNT])(void) = {
    emit_cube, emit_sphere, emit_cylinder, emit_cone, emit_disc, emit_octa, emit_frustum, emit_sbox, emit_quadp,
};

/* emit a unit primitive with a local transform (relative to the base) */
void mb_prim(prim_t p, uint32_t color, float x, float y, float z, float rx, float ry, float rz,
             float sx, float sy, float sz)
{
    xf_compose(xl, x, y, z, rx, ry, rz, sx, sy, sz);
    xf_update();
    uint32_t keep = mb_color;
    mb_color = color;
    PRIM_EMIT[p]();
    mb_color = keep;
    static const float I[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
    memcpy(xl, I, sizeof(I));
    xf_update();
}
void mb_set_color(uint32_t color) { mb_color = color; }
void mb_end(void) { mb_flush(); mb_color = 0; }

static vec3_t rot3(vec3_t v, float rx, float ry, float rz)
{
    /* R = Ry * Rx * Rz, matching glRotate(ry,Y) glRotate(rx,X) glRotate(rz,Z) */
    float c, s, x, y, z;
    c = cosf(rz); s = sinf(rz); x = v.x * c - v.y * s; y = v.x * s + v.y * c; v.x = x; v.y = y;
    c = cosf(rx); s = sinf(rx); y = v.y * c - v.z * s; z = v.y * s + v.z * c; v.y = y; v.z = z;
    c = cosf(ry); s = sinf(ry); x = v.x * c + v.z * s; z = -v.x * s + v.z * c; v.x = x; v.z = z;
    return v;
}

static bool skip_bottoms;
void mb_skip_bottoms(bool on) { skip_bottoms = on; }

void mb_box_rot(vec3_t c, vec3_t half, float rx, float ry, float rz, float ts)
{
    vec3_t ax[3] = { rot3(v3(1, 0, 0), rx, ry, rz), rot3(v3(0, 1, 0), rx, ry, rz), rot3(v3(0, 0, 1), rx, ry, rz) };
    float h[3] = { half.x, half.y, half.z };
    for (int a = 0; a < 3; a++) {
        int b = (a + 1) % 3, d = (a + 2) % 3;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            vec3_t n = v3_scale(ax[a], (float)sgn);
            if (skip_bottoms && n.y < -0.9f) continue;   /* faces the ground: never visible */
            vec3_t fc = v3_add(c, v3_scale(n, h[a]));
            vec3_t eb = v3_scale(ax[b], h[b]), ed = v3_scale(ax[d], h[d]);
            float ub = 2 * h[b] / ts, ud = 2 * h[d] / ts;
            vtx_t p0 = V(v3_sub(v3_sub(fc, eb), ed), n, 0, ud);
            vtx_t p1 = V(v3_sub(v3_add(fc, eb), ed), n, ub, ud);
            vtx_t p2 = V(v3_add(v3_add(fc, eb), ed), n, ub, 0);
            vtx_t p3 = V(v3_add(v3_sub(fc, eb), ed), n, 0, 0);
            emit_quad(p0, p1, p2, p3);
        }
    }
}

void mb_box(vec3_t mn, vec3_t mx, float ts)
{
    vec3_t c = v3_scale(v3_add(mn, mx), 0.5f);
    vec3_t h = v3_scale(v3_sub(mx, mn), 0.5f);
    mb_box_rot(c, h, 0, 0, 0, ts);
}

void mb_cyl(vec3_t base, float r, float h, int seg, float ts, bool caps)
{
    for (int s = 0; s < seg; s++) {
        float b0 = TAU_F * s / seg, b1 = TAU_F * (s + 1) / seg;
        vec3_t n0 = v3(cosf(b0), 0, sinf(b0)), n1 = v3(cosf(b1), 0, sinf(b1));
        vec3_t p0 = v3_add(base, v3_scale(n0, r)), p1 = v3_add(base, v3_scale(n1, r));
        vec3_t q0 = v3(p0.x, p0.y + h, p0.z), q1 = v3(p1.x, p1.y + h, p1.z);
        float u0 = TAU_F * r * s / seg / ts, u1 = TAU_F * r * (s + 1) / seg / ts;
        emit_quad(V(p0, n0, u0, h / ts), V(p1, n1, u1, h / ts), V(q1, n1, u1, 0), V(q0, n0, u0, 0));
        if (caps) {
            vec3_t up = v3(0, 1, 0);
            emit_tri(V(v3(base.x, base.y + h, base.z), up, 0.5f, 0.5f),
                     V(q0, up, 0.5f + n0.x * r / ts, 0.5f + n0.z * r / ts),
                     V(q1, up, 0.5f + n1.x * r / ts, 0.5f + n1.z * r / ts));
        }
    }
}

void mb_cone(vec3_t base, float r, float h, int seg, float ts)
{
    for (int s = 0; s < seg; s++) {
        float b0 = TAU_F * s / seg, b1 = TAU_F * (s + 1) / seg, bm = (b0 + b1) * 0.5f;
        vec3_t n0 = v3_norm(v3(cosf(b0) * h, r, sinf(b0) * h));
        vec3_t n1 = v3_norm(v3(cosf(b1) * h, r, sinf(b1) * h));
        vec3_t nm = v3_norm(v3(cosf(bm) * h, r, sinf(bm) * h));
        vec3_t p0 = v3(base.x + cosf(b0) * r, base.y, base.z + sinf(b0) * r);
        vec3_t p1 = v3(base.x + cosf(b1) * r, base.y, base.z + sinf(b1) * r);
        float u0 = TAU_F * r * s / seg / ts, u1 = TAU_F * r * (s + 1) / seg / ts;
        emit_tri(V(p0, n0, u0, h / ts), V(p1, n1, u1, h / ts),
                 V(v3(base.x, base.y + h, base.z), nm, (u0 + u1) * 0.5f, 0));
    }
}

void mb_blob(vec3_t c, vec3_t rad, int seg, int rings, float ts)
{
    for (int r = 0; r < rings; r++) {
        float a0 = PI_F * r / rings - PI_F / 2, a1 = PI_F * (r + 1) / rings - PI_F / 2;
        for (int s = 0; s < seg; s++) {
            float b0 = TAU_F * s / seg, b1 = TAU_F * (s + 1) / seg;
            vec3_t n[4] = {
                v3(cosf(a0) * cosf(b0), sinf(a0), cosf(a0) * sinf(b0)),
                v3(cosf(a0) * cosf(b1), sinf(a0), cosf(a0) * sinf(b1)),
                v3(cosf(a1) * cosf(b1), sinf(a1), cosf(a1) * sinf(b1)),
                v3(cosf(a1) * cosf(b0), sinf(a1), cosf(a1) * sinf(b0)),
            };
            vtx_t q[4];
            for (int i = 0; i < 4; i++) {
                vec3_t p = v3(c.x + n[i].x * rad.x, c.y + n[i].y * rad.y, c.z + n[i].z * rad.z);
                vec3_t nn = v3_norm(v3(n[i].x / rad.x, n[i].y / rad.y, n[i].z / rad.z));
                float u = (i == 0 || i == 3 ? b0 : b1) * rad.x / ts;
                float v = (i < 2 ? a0 : a1) * rad.y / ts;
                q[i] = V(p, nn, u, v);
            }
            if (r == 0)               emit_tri(q[0], q[2], q[3]);
            else if (r == rings - 1)  emit_tri(q[0], q[1], q[2]);
            else                      emit_quad(q[0], q[1], q[2], q[3]);
        }
    }
}

void mb_tri(vec3_t a, vec3_t b, vec3_t c, vec3_t outward, float ts)
{
    vec3_t n = v3_norm(v3_cross(v3_sub(b, a), v3_sub(c, a)));
    if (v3_dot(n, outward) < 0) n = v3_scale(n, -1);
    /* planar mapping on the dominant plane */
    #define UV(p) (fabsf(n.x) > 0.7f ? (p).z : (p).x) / ts, -(fabsf(n.y) > 0.7f ? (p).z : (p).y) / ts
    emit_tri(V(a, n, UV(a)), V(b, n, UV(b)), V(c, n, UV(c)));
    #undef UV
}

void mb_quad(vec3_t a, vec3_t b, vec3_t c, vec3_t d, vec3_t outward, float ts)
{
    mb_tri(a, b, c, outward, ts);
    mb_tri(a, c, d, outward, ts);
}

/* ------------------------------------------------------------------ */

void gfx_init(void)
{
    glGenTextures(TEX_COUNT, textures);
    for (int i = 0; i < TEX_COUNT; i++) {
        sprite_t *spr = sprite_load(TEX_FILES[i]);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        if (i == TEX_GLOW) {
            glSpriteTextureN64(GL_TEXTURE_2D, spr, NULL);
        } else {
            glSpriteTextureN64(GL_TEXTURE_2D, spr, &(rdpq_texparms_t){
                .s.repeats = REPEAT_INFINITE, .t.repeats = REPEAT_INFINITE });
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    for (int i = 0; i < PRIM_COUNT; i++) {
        dl_begin();
        mb_begin(0);
        PRIM_EMIT[i]();
        mb_end();
        prim_lists[i] = dl_end();
    }

    spr_circle = sprite_load("rom:/ui_circle.sprite");
    spr_ring = sprite_load("rom:/ui_ring.sprite");
    spr_logo = sprite_load("rom:/logo_kanji.sprite");

    static const uint32_t style_colors[] = {
        0xFFFFFFFF, 0xFFD860FF, 0xA8A8B8FF, 0x78C8FFFF, 0xFF6060FF, 0x80E880FF, 0xFFA8D8FF, 0x302838FF,
    };
    const char *font_files[] = { "rom:/body.font64", "rom:/outline.font64", "rom:/title.font64" };
    for (int f = 0; f < 3; f++) {
        rdpq_font_t *fnt = rdpq_font_load(font_files[f]);
        for (int s = 0; s < (int)(sizeof(style_colors) / sizeof(style_colors[0])); s++) {
            rdpq_font_style(fnt, s, &(rdpq_fontstyle_t){
                .color = rgba(style_colors[s]),
                .outline_color = RGBA32(16, 8, 24, 255),
            });
        }
        rdpq_text_register_font(FONT_BODY + f, fnt);
    }

    g_cam.fov = 60.0f;
    g_cam.dist = 7.0f;
    g_cam.pitch = 0.32f;
}

/* libdragon's GL pipeline records each vertex-loader command into a slot of
   fixed size but fills only the instructions it needs; the RSP copies the
   whole slot into IMEM and runs it. rspq clears its own ring buffers, but not
   the memory of a block, so once the heap hands out memory of a freed block
   the leftover words run as RSP code (this crashed every map change).
   Block memory comes from malloc_uncached, which the Makefile wraps
   (--wrap=malloc_uncached) so that it returns zeroed memory: unused slots
   stay NOPs. */
void *__real_malloc_uncached(size_t size);
void *__wrap_malloc_uncached(size_t size)
{
    void *p = __real_malloc_uncached(size);
    if (p) {
        void *c = CachedAddr(p);
        memset(c, 0, size);
        data_cache_hit_writeback_invalidate(c, size);
    }
    return p;
}

/* Freed lists are parked until the start of a later frame and released there
   in one go, behind a single rspq_wait(). */
#ifndef DL_FREE_DELAY
#define DL_FREE_DELAY 2
#endif
#define DL_GRAVE 256
static dlist_t grave[DL_GRAVE];
static uint32_t grave_frame[DL_GRAVE];
static int ngrave;

static void dl_collect(bool all)
{
    int keep = 0;
    bool waited = false;
    for (int i = 0; i < ngrave; i++) {
        if (all || g_frame.frame - grave_frame[i] >= DL_FREE_DELAY) {
            if (!waited) { rspq_wait(); waited = true; }
            rspq_block_free(grave[i]);
        } else {
            grave[keep] = grave[i];
            grave_frame[keep++] = grave_frame[i];
        }
    }
    ngrave = keep;
}

void dl_free(dlist_t l)
{
    if (!l) return;
    if (ngrave == DL_GRAVE) dl_collect(true);
    grave[ngrave] = l;
    grave_frame[ngrave++] = g_frame.frame;
}

void gfx_bind(tex_id_t t)
{
    if ((int)t == bound_tex) return;
    if (t == TEX_NONE) {
        glDisable(GL_TEXTURE_2D);
    } else {
        if (bound_tex == -1 || bound_tex == TEX_NONE) glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, textures[t]);
    }
    bound_tex = t;
}

void gfx_prim(prim_t p)
{
    dl_call(prim_lists[p]);
}

void gfx_part(prim_t p, uint32_t color, float x, float y, float z, float sx, float sy, float sz)
{
    gl_color(color);
    /* translate + scale in a single matrix upload */
    const GLfloat m[16] = { sx, 0, 0, 0,  0, sy, 0, 0,  0, 0, sz, 0,  x, y, z, 1 };
    glPushMatrix();
    glMultMatrixf(m);
    dl_call(prim_lists[p]);
    glPopMatrix();
}

void gfx_part_rot(prim_t p, uint32_t color, float x, float y, float z,
                  float rx, float ry, float rz, float sx, float sy, float sz)
{
    gl_color(color);
    /* M = T * Ry * Rx * Rz * S, built on the CPU and uploaded once */
    float cx = cosf(rx), sxn = sinf(rx), cy = cosf(ry), syn = sinf(ry), cz = cosf(rz), szn = sinf(rz);
    float r00 = cy * cz + syn * sxn * szn, r01 = -cy * szn + syn * sxn * cz, r02 = syn * cx;
    float r10 = cx * szn,                  r11 = cx * cz,                    r12 = -sxn;
    float r20 = -syn * cz + cy * sxn * szn, r21 = syn * szn + cy * sxn * cz, r22 = cy * cx;
    const GLfloat m[16] = {
        r00 * sx, r10 * sx, r20 * sx, 0,
        r01 * sy, r11 * sy, r21 * sy, 0,
        r02 * sz, r12 * sz, r22 * sz, 0,
        x, y, z, 1,
    };
    glPushMatrix();
    glMultMatrixf(m);
    dl_call(prim_lists[p]);
    glPopMatrix();
}

/* ------------------------------------------------------------------ */
/* Camera matrices                                                     */
/* ------------------------------------------------------------------ */

static void mat_mul(float *out, const float *a, const float *b)
{
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k];
            r[c * 4 + rr] = s;
        }
    memcpy(out, r, sizeof(r));
}

void gfx_begin_frame_3d(uint32_t clear_color)
{
    if (ngrave) dl_collect(false);
    surface_t *disp = display_get();
    rdpq_attach(disp, display_get_zbuf());
    gl_context_begin();
    bound_tex = -1;
    glClearColor((clear_color >> 24) / 255.0f, ((clear_color >> 16) & 0xFF) / 255.0f,
                 ((clear_color >> 8) & 0xFF) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void gfx_setup_camera(void)
{
    vec3_t eye = g_cam.override ? g_cam.ov_pos : g_cam.pos;
    vec3_t ctr = g_cam.override ? g_cam.ov_target : g_cam.target;
    if (g_cam.shake > 0) {
        eye.x += frand_range(-1, 1) * g_cam.shake * 0.25f;
        eye.y += frand_range(-1, 1) * g_cam.shake * 0.25f;
    }

    float aspect = (float)SCREEN_W / SCREEN_H;
    float n = 0.4f, f = 140.0f;
    float ft = 1.0f / tanf(g_cam.fov * 0.5f * PI_F / 180.0f);
    float proj[16] = {
        ft / aspect, 0, 0, 0,
        0, ft, 0, 0,
        0, 0, (f + n) / (n - f), -1,
        0, 0, 2 * f * n / (n - f), 0,
    };

    vec3_t F = v3_norm(v3_sub(ctr, eye));
    vec3_t S = v3_norm(v3_cross(F, v3(0, 1, 0)));
    vec3_t U = v3_cross(S, F);
    float view[16] = {
        S.x, U.x, -F.x, 0,
        S.y, U.y, -F.y, 0,
        S.z, U.z, -F.z, 0,
        -v3_dot(S, eye), -v3_dot(U, eye), v3_dot(F, eye), 1,
    };
    g_cam.fwd = F; g_cam.right = S; g_cam.up = U;
    mat_mul(g_cam.viewproj, proj, view);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view);
}

bool gfx_project(vec3_t p, float *sx, float *sy)
{
    const float *m = g_cam.viewproj;
    float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w < 0.1f) return false;
    *sx = (x / w * 0.5f + 0.5f) * SCREEN_W;
    *sy = (1.0f - (y / w * 0.5f + 0.5f)) * SCREEN_H;
    return true;
}

bool gfx_visible(vec3_t p, float radius, float max_dist)
{
    vec3_t eye = g_cam.override ? g_cam.ov_pos : g_cam.pos;
    vec3_t d = v3_sub(p, eye);
    float along = v3_dot(d, g_cam.fwd);
    if (along < -radius) return false;
    float dist2 = v3_dot(d, d);
    if (dist2 > (max_dist + radius) * (max_dist + radius)) return false;
    /* rough side culling: horizontal FOV ~ 80 degrees incl. margin */
    float side = fabsf(v3_dot(d, g_cam.right));
    if (side > along * 0.95f + radius * 1.5f + 2.0f) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* Billboards / shadows                                                */
/* ------------------------------------------------------------------ */

void gfx_billboards_begin(void)
{
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    gfx_bind(TEX_GLOW);
    glBegin(GL_QUADS);
}

void gfx_billboard(vec3_t p, float size, uint32_t color)
{
    vec3_t r = v3_scale(g_cam.right, size), u = v3_scale(g_cam.up, size);
    vec3_t a = v3_sub(v3_sub(p, r), u), b = v3_sub(v3_add(p, r), u);
    vec3_t c = v3_add(v3_add(p, r), u), d = v3_add(v3_sub(p, r), u);
    gl_color(color);
    glTexCoord2f(0, 1); glVertex3f(a.x, a.y, a.z);
    glTexCoord2f(1, 1); glVertex3f(b.x, b.y, b.z);
    glTexCoord2f(1, 0); glVertex3f(c.x, c.y, c.z);
    glTexCoord2f(0, 0); glVertex3f(d.x, d.y, d.z);
}

void gfx_billboards_end(void)
{
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

void gfx_shadow(vec3_t p, float radius, float ground_y)
{
    float h = p.y - ground_y;
    float k = clampf(1.0f - h * 0.15f, 0.3f, 1.0f);
    gl_color(0x00000000 | (uint32_t)(110 * k));
    glPushMatrix();
    glTranslatef(p.x, ground_y + 0.06f, p.z);
    glScalef(radius * 2 * k, 1, radius * 2 * k);
    dl_call(prim_lists[PRIM_DISC]);
    glPopMatrix();
}

void gfx_end_frame_3d(void)
{
    gl_context_end();
}

/* ------------------------------------------------------------------ */
/* 2D helpers                                                          */
/* ------------------------------------------------------------------ */

void ui_rect(int x0, int y0, int x1, int y1, uint32_t color)
{
    rdpq_set_mode_fill(rgba(color));
    rdpq_fill_rectangle(x0, y0, x1, y1);
}

void ui_rect_alpha(int x0, int y0, int x1, int y1, uint32_t color)
{
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(rgba(color));
    rdpq_fill_rectangle(x0, y0, x1, y1);
}

void ui_circle(int cx, int cy, uint32_t color, bool ring)
{
    sprite_t *s = ring ? spr_ring : spr_circle;
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_set_prim_color(rgba(color));
    rdpq_sprite_blit(s, cx - s->width / 2, cy - s->height / 2, NULL);
}

void ui_logo(int x, int y)
{
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);
    rdpq_sprite_blit(spr_logo, x, y, NULL);
}

void ui_fade(float alpha, uint32_t color)
{
    if (alpha <= 0.004f) return;
    if (alpha >= 0.996f) {
        ui_rect(0, 0, SCREEN_W, SCREEN_H, color | 0xFF);
        return;
    }
    ui_rect_alpha(0, 0, SCREEN_W, SCREEN_H, (color & 0xFFFFFF00) | (uint32_t)(alpha * 255));
}

void ui_text(int font, int style, int x, int y, const char *s)
{
    rdpq_text_print(&(rdpq_textparms_t){ .style_id = style }, font, x, y, s);
}

void ui_text_center(int font, int style, int y, const char *s)
{
    rdpq_text_print(&(rdpq_textparms_t){ .style_id = style, .width = SCREEN_W, .align = ALIGN_CENTER },
                    font, 0, y, s);
}

void ui_text_box(int font, int style, int x, int y, int w, int max_chars, const char *s)
{
    rdpq_text_print(&(rdpq_textparms_t){
        .style_id = style, .width = w, .wrap = WRAP_WORD, .max_chars = max_chars, .line_spacing = 2,
    }, font, x, y, s);
}
