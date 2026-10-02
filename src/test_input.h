/*
 * Scripted controller input for automated emulator testing.
 * Only compiled with -DTEST_INPUT=<script> (see README "Testing").
 */
#ifndef TEST_INPUT_H
#define TEST_INPUT_H

enum {
    TB_A = 0x8000, TB_B = 0x4000, TB_Z = 0x2000, TB_START = 0x1000,
    TB_DU = 0x0800, TB_DD = 0x0400, TB_DL = 0x0200, TB_DR = 0x0100,
    TB_L = 0x0020, TB_R = 0x0010, TB_CU = 0x0008, TB_CD = 0x0004, TB_CL = 0x0002, TB_CR = 0x0001,
    TB_ADV = 0x0040,   /* pseudo-button: tap A every 0.4s while a dialogue or card is open */
    TB_MASH = 0x0080,  /* pseudo-button: tap A every 0.15s */
};

typedef struct { float t0, t1; uint16_t buttons; int8_t sx, sy; void (*fn)(void); } tin_t;

static void tp_zenith(void)  { g_player.pos = v3(-10.4f, 0, 6.8f); g_player.pos.y = world_height(-10.4f, 6.8f); g_player.yaw = -1.2f; camera_snap(); }
static void tp_gate(void)    { g_player.pos = v3(0, 0, 35.6f); g_player.pos.y = world_height(0, 35.6f); g_player.yaw = 0; camera_snap(); }
static void tp_yard(void)    { g_player.pos = v3(-19, 0, 11.0f); g_player.pos.y = world_height(-19, 11); g_player.yaw = 0; camera_snap(); }
static void tp_wolves(void)  { g_player.pos = v3(-6, 0, -12.0f); g_player.pos.y = world_height(-6, -12); g_player.yaw = PI_F; camera_snap(); }
static void tp_mesa(void)    { g_player.pos = v3(0, 0, 6.0f); g_player.pos.y = world_height(0, 6); g_player.yaw = PI_F; camera_snap(); }
static void tp_square(void)  { g_player.pos = v3(-1.5f, 0, 13.0f); g_player.pos.y = world_height(-1.5f, 13); g_player.yaw = PI_F - 0.6f; camera_snap(); }
static void tp_square2(void) { g_player.pos = v3(3.0f, 0, 3.0f); g_player.pos.y = world_height(3, 3); g_player.yaw = -0.9f; camera_snap(); }
static void tp_boulder(void) { g_player.pos = v3(0, 0, -16.0f); g_player.pos.y = world_height(0, -16); g_player.yaw = PI_F; camera_snap(); }
static void god_mode(void)   { g_player.max_hp = 99; g_player.hp = 99; }

#if TEST_INPUT == 1
/* chapter 1: Zenith, Roxy, targets, braziers, boulder */
static void face(float x, float z) { g_player.yaw = yaw_towards(g_player.pos, v3(x, 0, z)); camera_snap(); }
static void place(float x, float z) { g_player.pos = v3(x, world_height(x, z), z); }
static void tp_br1(void) { place(-2.5f, 9.0f); face(-6, 9); }
static void tp_br2(void) { place(2.5f, 9.0f); face(6, 9); }
static void tp_br3(void) { place(3.0f, -1.0f); face(0, -1); }
static const tin_t SCRIPT[] = {
    { 0.8f, 0.9f, 0, 0, 0, tp_zenith },
    { 1.5f, 1.6f, TB_A },
    { 2.0f, 90.0f, TB_ADV },
    { 9.0f, 9.1f, 0, 0, 0, tp_gate },
    { 20.0f, 20.1f, 0, 0, 0, tp_yard },
    { 21.0f, 22.0f, TB_Z }, { 21.3f, 21.4f, TB_CL },
    { 22.5f, 23.5f, TB_Z }, { 22.8f, 22.9f, TB_CL },
    { 24.0f, 25.0f, TB_Z }, { 24.3f, 24.4f, TB_CL },
    { 25.5f, 26.5f, TB_Z }, { 25.8f, 25.9f, TB_CL },
    { 27.0f, 28.0f, TB_Z }, { 27.3f, 27.4f, TB_CL },
    { 34.0f, 34.1f, 0, 0, 0, tp_br1 }, { 34.5f, 34.6f, TB_CD },
    { 36.0f, 36.1f, 0, 0, 0, tp_br2 }, { 36.5f, 36.6f, TB_CD },
    { 38.0f, 38.1f, 0, 0, 0, tp_br3 }, { 38.5f, 38.6f, TB_CD },
    { 46.0f, 46.1f, 0, 0, 0, tp_boulder }, { 46.5f, 46.6f, TB_CR }, { 48.0f, 49.0f, TB_CR },
};
#elif TEST_INPUT == 4
/* chapters 2 and 3: bullies, Sylphie, healing, the gate, Cumulonimbus */
static void place(float x, float z) { g_player.pos = v3(x, world_height(x, z), z); }
static void face(float x, float z) { g_player.yaw = yaw_towards(g_player.pos, v3(x, 0, z)); camera_snap(); }
static void tp_hill(void)    { god_mode(); place(0, -25); face(0, -35); }
static void tp_sylphie(void) { place(0.5f, -33.6f); face(0.5f, -35.5f); }
static void tp_home(void)    { place(-10.4f, 6.8f); face(-12.2f, 7.6f); }
static void tp_exam(void)    { place(-1.6f, 35.6f); face(-1.6f, 37.5f); }
static void tp_cross(void)   { place(0, 39.7f); face(0, 45); }
static const tin_t SCRIPT[] = {
    { 0.8f, 0.9f, 0, 0, 0, tp_hill },
    { 1.0f, 200.0f, TB_ADV },
    { 6.0f, 7.0f, TB_Z }, { 6.3f, 6.4f, TB_CL }, { 7.5f, 8.5f, TB_Z }, { 7.8f, 7.9f, TB_CL },
    { 9.0f, 10.0f, TB_Z }, { 9.3f, 9.4f, TB_CL }, { 10.5f, 11.5f, TB_Z }, { 10.8f, 10.9f, TB_CL },
    { 12.0f, 13.0f, TB_Z }, { 12.3f, 12.4f, TB_CL }, { 13.5f, 14.5f, TB_Z }, { 13.8f, 13.9f, TB_CL },
    { 15.0f, 16.0f, TB_Z }, { 15.3f, 15.4f, TB_CL }, { 16.5f, 17.5f, TB_Z }, { 16.8f, 16.9f, TB_CL },
    { 18.0f, 19.0f, TB_Z }, { 18.3f, 18.4f, TB_CL }, { 19.5f, 20.5f, TB_Z }, { 19.8f, 19.9f, TB_CL },
    { 24.0f, 24.1f, 0, 0, 0, tp_sylphie }, { 24.5f, 24.6f, TB_A },
    { 34.0f, 34.1f, 0, 0, 0, tp_home }, { 34.5f, 34.6f, TB_A },
    { 44.0f, 44.1f, 0, 0, 0, tp_exam }, { 44.5f, 44.6f, TB_A },
    { 50.0f, 50.1f, 0, 0, 0, tp_cross },
    { 54.0f, 62.0f, TB_MASH },
    { 66.0f, 66.1f, 0, 0, 0, tp_mesa },
    { 72.0f, 74.45f, TB_R },
};
#elif TEST_INPUT == 5
/* map transitions: cycle through every map and variant, twice */
static const int TRANS_STEPS[] = { 12, 15, 1, 21, 18, 16, 12, 19, 1 };
static void go_next(void)
{
    static int n;
    story_debug_start(TRANS_STEPS[n++ % (sizeof(TRANS_STEPS) / sizeof(TRANS_STEPS[0]))]);
}
static const tin_t SCRIPT[] = {
    { 3, 3.1f, 0, 0, 0, go_next }, { 6, 6.1f, 0, 0, 0, go_next }, { 9, 9.1f, 0, 0, 0, go_next },
    { 12, 12.1f, 0, 0, 0, go_next }, { 15, 15.1f, 0, 0, 0, go_next }, { 18, 18.1f, 0, 0, 0, go_next },
    { 21, 21.1f, 0, 0, 0, go_next }, { 24, 24.1f, 0, 0, 0, go_next }, { 27, 27.1f, 0, 0, 0, go_next },
    { 30, 30.1f, 0, 0, 0, go_next }, { 33, 33.1f, 0, 0, 0, go_next }, { 36, 36.1f, 0, 0, 0, go_next },
    { 39, 39.1f, 0, 0, 0, go_next }, { 42, 42.1f, 0, 0, 0, go_next }, { 45, 45.1f, 0, 0, 0, go_next },
    { 48, 48.1f, 0, 0, 0, go_next }, { 51, 51.1f, 0, 0, 0, go_next }, { 54, 54.1f, 0, 0, 0, go_next },
};
#elif TEST_INPUT == 2
/* forest: fight wolves with spells, lock-on and the staff */
static const tin_t SCRIPT[] = {
    { 0.5f, 0.6f, 0, 0, 0, god_mode },
    { 0.8f, 0.9f, 0, 0, 0, tp_wolves },
    { 2.0f, 30.0f, TB_Z },
    { 2.5f, 2.6f, TB_CL }, { 3.5f, 3.6f, TB_CD }, { 4.5f, 5.6f, TB_CR }, { 6.5f, 6.6f, TB_B },
    { 7.0f, 7.1f, TB_CD }, { 8.0f, 8.1f, TB_CL }, { 9.0f, 9.1f, TB_B }, { 10.0f, 11.2f, TB_CD },
    { 12.0f, 12.1f, TB_CL }, { 13.0f, 13.1f, TB_CR }, { 14.0f, 14.1f, TB_B }, { 15.0f, 15.1f, TB_CD },
    { 16.0f, 16.1f, TB_CU }, { 17.0f, 17.1f, TB_CL }, { 18.0f, 18.1f, TB_CR },
};
#elif TEST_INPUT == 3
/* plateau: the Cumulonimbus exam */
static const tin_t SCRIPT[] = {
    { 0.8f, 0.9f, 0, 0, 0, tp_mesa },
    { 2.5f, 2.6f, TB_A }, { 3.5f, 3.6f, TB_A }, { 4.5f, 4.6f, TB_A }, { 5.5f, 5.6f, TB_A }, { 6.5f, 6.6f, TB_A },
    { 7.5f, 7.6f, TB_A },
    { 9.0f, 11.45f, TB_R },
    { 18.0f, 18.1f, TB_A }, { 19.0f, 19.1f, TB_A }, { 20.0f, 20.1f, TB_A }, { 21.0f, 21.1f, TB_A },
};
#elif TEST_INPUT == 9
/* texture check: look straight down at the ground, then straight at a wall */
static void cam_down(void) { g_cam.override = true; g_cam.ov_pos = v3(-6, 9, 4.01f); g_cam.ov_target = v3(-6, 0, 4); }
static void cam_wall(void) { g_cam.override = true; g_cam.ov_pos = v3(-9, 3, 5); g_cam.ov_target = v3(-15.5f, 3, 5); }
static const tin_t SCRIPT[] = { { 0.5f, 0.6f, 0, 0, 0, cam_down }, { 3.0f, 3.1f, 0, 0, 0, cam_wall } };
#else
static const tin_t SCRIPT[] = { { 0, 0, 0 } };
#endif

static void test_input_apply(void)
{
    static uint16_t prev;
    static bool fired[sizeof(SCRIPT) / sizeof(SCRIPT[0])];
    float t = g_frame.time;
    uint16_t held = 0;
    int8_t sx = 0, sy = 0;
    for (unsigned i = 0; i < sizeof(SCRIPT) / sizeof(SCRIPT[0]); i++) {
        const tin_t *e = &SCRIPT[i];
        if (t < e->t0 || t >= e->t1) continue;
        if (e->buttons & TB_ADV) {
            if ((dialog_active() || story_card_active()) && fmodf(t, 0.4f) < 0.1f) held |= TB_A;
            continue;
        }
        if (e->buttons & TB_MASH) {
            if (fmodf(t, 0.15f) < 0.07f) held |= TB_A;
            continue;
        }
        held |= e->buttons;
        if (e->sx || e->sy) { sx = e->sx; sy = e->sy; }
        if (e->fn && !fired[i]) { fired[i] = true; e->fn(); debugf("test t=%.1f event %u -> player %.1f %.1f %.1f\n", t, i, g_player.pos.x, g_player.pos.y, g_player.pos.z); }
    }
    g_frame.held.raw |= held;
    g_frame.pressed.raw |= held & ~prev;
    g_frame.released.raw |= prev & ~held;
    if (sx || sy) { g_frame.in.stick_x = sx; g_frame.in.stick_y = sy; }
    prev = held;
}

#endif
