/*
 * Rendering core: textures, unit primitives compiled into display lists,
 * camera matrices, billboards and the 2D (rdpq) helpers used by the HUD.
 */
#include "game.h"

camera_t g_cam;

static GLuint textures[TEX_COUNT];
static GLuint prim_lists;
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

static uint32_t mb_color = 0;   /* mesh builder: 0 = keep the current color */

static void put_vtx(vtx_t v)
{
    if (mb_color) gl_color(mb_color);
    glNormal3f(v.n.x, v.n.y, v.n.z);
    glTexCoord2f(v.u, v.v);
    glVertex3f(v.p.x, v.p.y, v.p.z);
}

/* Emit a triangle, fixing the winding so that it faces along its normals. */
static void emit_tri(vtx_t a, vtx_t b, vtx_t c)
{
    vec3_t fn = v3_cross(v3_sub(b.p, a.p), v3_sub(c.p, a.p));
    vec3_t avg = v3_add(v3_add(a.n, b.n), c.n);
    if (v3_dot(fn, avg) < 0) { vtx_t t = b; b = c; c = t; }
    put_vtx(a); put_vtx(b); put_vtx(c);
}

static void emit_quad(vtx_t a, vtx_t b, vtx_t c, vtx_t d)
{
    emit_tri(a, b, c);
    emit_tri(a, c, d);
}

static void build_cube(void)
{
    static const float N[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    glBegin(GL_TRIANGLES);
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
    glEnd();
}

static void build_sphere(void)
{
    const int SEG = 8, RING = 5;
    glBegin(GL_TRIANGLES);
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
    glEnd();
}

static void build_cylinder(void)
{
    const int SEG = 6;
    glBegin(GL_TRIANGLES);
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
    glEnd();
}

static void build_cone(void)
{
    const int SEG = 6;
    glBegin(GL_TRIANGLES);
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
    glEnd();
}

/* cone with the tip cut off: radius 0.5 at y=0, 0.25 at y=1 (robes, skirts) */
static void build_frustum(void)
{
    const int SEG = 8;
    const float rb = 0.5f, rt = 0.25f;
    glBegin(GL_TRIANGLES);
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
    glEnd();
}

static void build_disc(void)
{
    const int SEG = 12;
    vec3_t up = v3(0, 1, 0);
    glBegin(GL_TRIANGLES);
    for (int s = 0; s < SEG; s++) {
        float b0 = TAU_F * s / SEG, b1 = TAU_F * (s + 1) / SEG;
        emit_tri(V(v3(0, 0, 0), up, 0.5f, 0.5f),
                 V(v3(cosf(b0) * 0.5f, 0, sinf(b0) * 0.5f), up, 0.5f + cosf(b0) * 0.5f, 0.5f + sinf(b0) * 0.5f),
                 V(v3(cosf(b1) * 0.5f, 0, sinf(b1) * 0.5f), up, 0.5f + cosf(b1) * 0.5f, 0.5f + sinf(b1) * 0.5f));
    }
    glEnd();
}

static void build_octa(void)
{
    vec3_t top = v3(0, 0.5f, 0), bot = v3(0, -0.5f, 0);
    vec3_t ring[4] = { v3(0.5f, 0, 0), v3(0, 0, 0.5f), v3(-0.5f, 0, 0), v3(0, 0, -0.5f) };
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < 4; i++) {
        vec3_t a = ring[i], b = ring[(i + 1) & 3];
        vec3_t nt = v3_norm(v3_add(v3_add(a, b), top));
        vec3_t nb = v3_norm(v3_add(v3_add(a, b), bot));
        emit_tri(V(a, nt, 0, 1), V(b, nt, 1, 1), V(top, nt, 0.5f, 0));
        emit_tri(V(a, nb, 0, 0), V(b, nb, 1, 0), V(bot, nb, 0.5f, 1));
    }
    glEnd();
}

/* ------------------------------------------------------------------ */
/* Mesh builder (used to compile props into display lists)             */
/* ------------------------------------------------------------------ */

void mb_begin(uint32_t color) { mb_color = color; glBegin(GL_TRIANGLES); }
void mb_set_color(uint32_t color) { mb_color = color; }
void mb_end(void) { glEnd(); mb_color = 0; }

static vec3_t rot3(vec3_t v, float rx, float ry, float rz)
{
    /* R = Ry * Rx * Rz, matching glRotate(ry,Y) glRotate(rx,X) glRotate(rz,Z) */
    float c, s, x, y, z;
    c = cosf(rz); s = sinf(rz); x = v.x * c - v.y * s; y = v.x * s + v.y * c; v.x = x; v.y = y;
    c = cosf(rx); s = sinf(rx); y = v.y * c - v.z * s; z = v.y * s + v.z * c; v.y = y; v.z = z;
    c = cosf(ry); s = sinf(ry); x = v.x * c + v.z * s; z = -v.x * s + v.z * c; v.x = x; v.z = z;
    return v;
}

void mb_box_rot(vec3_t c, vec3_t half, float rx, float ry, float rz, float ts)
{
    vec3_t ax[3] = { rot3(v3(1, 0, 0), rx, ry, rz), rot3(v3(0, 1, 0), rx, ry, rz), rot3(v3(0, 0, 1), rx, ry, rz) };
    float h[3] = { half.x, half.y, half.z };
    for (int a = 0; a < 3; a++) {
        int b = (a + 1) % 3, d = (a + 2) % 3;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            vec3_t n = v3_scale(ax[a], (float)sgn);
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

    prim_lists = glGenLists(PRIM_COUNT);
    void (*builders[PRIM_COUNT])(void) = { build_cube, build_sphere, build_cylinder, build_cone, build_disc, build_octa, build_frustum };
    for (int i = 0; i < PRIM_COUNT; i++) {
        glNewList(prim_lists + i, GL_COMPILE);
        builders[i]();
        glEndList();
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
    glCallList(prim_lists + p);
}

void gfx_part(prim_t p, uint32_t color, float x, float y, float z, float sx, float sy, float sz)
{
    gl_color(color);
    glPushMatrix();
    glTranslatef(x, y, z);
    glScalef(sx, sy, sz);
    glCallList(prim_lists + p);
    glPopMatrix();
}

void gfx_part_rot(prim_t p, uint32_t color, float x, float y, float z,
                  float rx, float ry, float rz, float sx, float sy, float sz)
{
    gl_color(color);
    glPushMatrix();
    glTranslatef(x, y, z);
    if (ry != 0) glRotatef(ry * RAD2DEG, 0, 1, 0);
    if (rx != 0) glRotatef(rx * RAD2DEG, 1, 0, 0);
    if (rz != 0) glRotatef(rz * RAD2DEG, 0, 0, 1);
    glScalef(sx, sy, sz);
    glCallList(prim_lists + p);
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
    glBegin(GL_TRIANGLES);
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
    glTexCoord2f(0, 1); glVertex3f(a.x, a.y, a.z);
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
    glCallList(prim_lists + PRIM_DISC);
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
