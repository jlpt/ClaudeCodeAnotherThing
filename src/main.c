/*
 * Mushoku Tensei 64 - an unofficial Nintendo 64 fan game.
 *
 * Boot, main loop and the top-level screens (title, pause, game over,
 * credits). Runs natively on N64 hardware via libdragon.
 */
#include "game.h"
#ifdef PERF_LOG
#include <rspq_profile.h>
#endif

frame_t g_frame;
game_state_t g_state;

static float fade = 1.0f;          /* 1 = black */
static int fade_dir = -1;          /* +1 fading out, -1 fading in */
static void (*fade_cb)(void);
static float rumble_t;
static int menu_sel;
static float state_t;
static float dead_t;
static bool has_save;
static float stick_repeat;

/* ------------------------------------------------------------------ */
/* Helpers used by other modules                                       */
/* ------------------------------------------------------------------ */

void game_fade_to(void (*callback)(void))
{
    if (fade_dir == 1) return;
    fade_cb = callback;
    fade_dir = 1;
}

bool game_fading(void) { return fade_dir != 0; }

void game_rumble(float seconds)
{
    if (!joypad_get_rumble_supported(JOYPAD_PORT_1)) return;
    joypad_set_rumble_active(JOYPAD_PORT_1, true);
    rumble_t = fmaxf(rumble_t, seconds);
}

static void update_fade(float dt)
{
    if (fade_dir == 1) {
        fade += dt * 2.8f;
        if (fade >= 1.0f) {
            fade = 1.0f;
            fade_dir = -1;
            void (*cb)(void) = fade_cb;
            fade_cb = NULL;
            if (cb) cb();
        }
    } else if (fade_dir == -1) {
        fade -= dt * 2.0f;
        if (fade <= 0) { fade = 0; fade_dir = 0; }
    }
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

#ifdef TEST_INPUT
#include "test_input.h"
#endif

static void read_input(void)
{
    joypad_poll();
    g_frame.pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    g_frame.held = joypad_get_buttons_held(JOYPAD_PORT_1);
    g_frame.released = joypad_get_buttons_released(JOYPAD_PORT_1);
    g_frame.in = joypad_get_inputs(JOYPAD_PORT_1);
#ifdef TEST_INPUT
    test_input_apply();
#endif
    float sx = g_frame.in.stick_x / 80.0f, sy = g_frame.in.stick_y / 80.0f;
    float mag = sqrtf(sx * sx + sy * sy);
    if (mag < 0.12f) {
        sx = sy = 0;
    } else {
        float k = clampf((mag - 0.12f) / 0.88f, 0, 1) / mag;
        sx *= k;
        sy *= k;
    }
    g_frame.stick_x = sx;
    g_frame.stick_y = sy;
}

/* menu navigation: d-pad or stick, with auto-repeat */
static int menu_nav(void)
{
    int d = 0;
    if (g_frame.pressed.d_up) d = -1;
    if (g_frame.pressed.d_down) d = 1;
    stick_repeat -= g_frame.dt;
    if (fabsf(g_frame.stick_y) > 0.5f) {
        if (stick_repeat <= 0) { d = g_frame.stick_y > 0 ? -1 : 1; stick_repeat = 0.28f; }
    } else {
        stick_repeat = 0;
    }
    if (d) sfx_play(SFX_CURSOR);
    return d;
}

/* ------------------------------------------------------------------ */
/* Scene rendering                                                     */
/* ------------------------------------------------------------------ */

#ifdef PERF_LOG
static uint64_t perf_sec[8], perf_last;
static void perf_mark(int i)
{
    rspq_wait();
    uint64_t t = get_ticks();
    perf_sec[i] += t - perf_last;
    perf_last = t;
}
#else
#define perf_mark(i) ((void)0)
#endif

static void render_scene(bool actors)
{
#ifdef PERF_LOG
    perf_last = get_ticks();
#endif
    gfx_begin_frame_3d(world_clear_color());
    perf_mark(0);
    gfx_setup_camera();
    world_render_sky();
    perf_mark(1);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    world_render();
    perf_mark(2);
    gfx_bind(TEX_NONE);
    glEnable(GL_LIGHTING);
    glEnable(GL_FOG);
#ifdef EXP_NOLIGHT
    glDisable(GL_LIGHTING);
#endif
#ifdef EXP_NOFOG
    glDisable(GL_FOG);
#endif
    if (actors) {
        /* characters are rigid segments at scale 1: no need to renormalize normals */
        glDisable(GL_NORMALIZE);
        story_render();
        player_render();
        glEnable(GL_NORMALIZE);
        combat_render();
    }
    perf_mark(3);
    world_render_fx();
    if (actors) {
        story_render_fx();
        combat_render_fx();
    }
    gfx_end_frame_3d();
    perf_mark(4);
}

static void draw_menu(const char *const *items, int count, int y)
{
    for (int i = 0; i < count; i++) {
        bool sel = i == menu_sel;
        if (sel) {
            ui_rect_alpha(96, y + i * 18 - 12, 224, y + i * 18 + 4, 0x20284880);
            ui_circle(104, y + i * 18 - 4, 0xF0D030FF, false);
        }
        rdpq_text_print(&(rdpq_textparms_t){ .style_id = sel ? STYLE_GOLD : STYLE_WHITE, .width = SCREEN_W, .align = ALIGN_CENTER },
                        FONT_OUTLINE, 0, y + i * 18, items[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Title                                                               */
/* ------------------------------------------------------------------ */

static void goto_title(void)
{
    g_state = GS_TITLE;
    state_t = 0;
    menu_sel = 0;
    has_save = save_exists();
    story_init();
    enemies_clear();
    projectiles_clear();
    particles_clear();
    world_set_palette_calamity(false);
    world_load(MAP_VILLAGE);
    g_player.pos = v3(-6, 0, 4);
    g_cam.override = true;
    music_play(MUS_TITLE);
}

static void start_new(void)
{
    g_state = GS_PLAY;
    story_new_game();
}

static void start_continue(void)
{
    g_state = GS_PLAY;
    story_start_from_save();
}

static void title_update(float dt)
{
    float a = state_t * 0.06f;
    g_cam.ov_target = v3(-4, 4, 2);
    g_cam.ov_pos = v3(-4 + cosf(a) * 30, 15 + sinf(state_t * 0.1f) * 2, 2 + sinf(a) * 30);
    world_update(dt);
    combat_update(dt);
    if (game_fading()) return;
    int count = has_save ? 2 : 1;
    menu_sel = (menu_sel + menu_nav() + count) % count;
    if ((g_frame.pressed.a || g_frame.pressed.start) && state_t > 0.5f) {
        sfx_play(SFX_CONFIRM);
        bool cont = has_save && menu_sel == 0;
        game_fade_to(cont ? start_continue : start_new);
    }
}

static void title_render(void)
{
    render_scene(false);
    ui_rect_alpha(0, 0, SCREEN_W, SCREEN_H, 0x0A081840);
    float bob = sinf(state_t * 1.5f) * 2.0f;
    ui_logo(60, 26 + (int)bob);
    ui_text_center(FONT_TITLE, STYLE_GOLD, 104, "MUSHOKU TENSEI 64");
    ui_text_center(FONT_OUTLINE, STYLE_WHITE, 120, "~ Jobless Reincarnation ~");
    static const char *const WITH_SAVE[] = { "Continue", "New Game" };
    static const char *const NO_SAVE[] = { "New Game" };
    if (fmodf(state_t, 1.0f) < 0.8f || state_t > 3.0f)
        draw_menu(has_save ? WITH_SAVE : NO_SAVE, has_save ? 2 : 1, 162);
    ui_text_center(FONT_OUTLINE, STYLE_GRAY, 218, "Unofficial fan game - not affiliated with the creators");
    ui_text_center(FONT_OUTLINE, STYLE_GRAY, 232, save_available() ? "Saves to cartridge EEPROM" : "No EEPROM: progress will not be saved");
}

/* ------------------------------------------------------------------ */
/* Pause / game over / credits                                         */
/* ------------------------------------------------------------------ */

static void quit_to_title(void) { goto_title(); }

static void pause_update(void)
{
    menu_sel = (menu_sel + menu_nav() + 2) % 2;
    if (g_frame.pressed.start || g_frame.pressed.b) { g_state = GS_PLAY; sfx_play(SFX_CURSOR); return; }
    if (g_frame.pressed.a) {
        sfx_play(SFX_CONFIRM);
        if (menu_sel == 0) g_state = GS_PLAY;
        else game_fade_to(quit_to_title);
    }
}

static void pause_render(void)
{
    ui_rect_alpha(0, 0, SCREEN_W, SCREEN_H, 0x080818C0);
    ui_text_center(FONT_TITLE, STYLE_GOLD, 34, "PAUSED");
    char buf[64];
    snprintf(buf, sizeof(buf), "Chapter %d: %s", story_chapter(), story_chapter_name());
    ui_text_center(FONT_OUTLINE, STYLE_WHITE, 52, buf);
    const char *obj = story_objective();
    if (obj && obj[0])
        rdpq_text_print(&(rdpq_textparms_t){ .style_id = STYLE_GRAY, .width = 260, .align = ALIGN_CENTER, .wrap = WRAP_WORD },
                        FONT_BODY, 30, 68, obj);

    ui_text(FONT_OUTLINE, STYLE_GOLD, 40, 100, "Spells");
    static const char *const BTN[SPELL_COUNT] = { "C-Left", "C-Down", "C-Right", "C-Up" };
    for (int s = 0; s < SPELL_COUNT; s++) {
        bool known = (g_save.spells >> s) & 1;
        int y = 116 + s * 14;
        ui_circle(46, y - 4, known ? SPELL_COLORS[s] : 0x40404880, false);
        ui_text(FONT_BODY, known ? STYLE_WHITE : STYLE_GRAY, 58, y, known ? SPELL_NAMES[s] : "???");
        ui_text(FONT_BODY, STYLE_GRAY, 140, y, BTN[s]);
    }
    ui_text(FONT_OUTLINE, STYLE_GOLD, 196, 100, "Controls");
    static const char *const CTRL[] = {
        "Stick  Move", "A  Jump / Talk", "B  Staff strike", "C  Cast (hold=charge)",
        "Z  Lock on", "R  Recenter camera", "D-Pad  Camera",
    };
    for (int i = 0; i < 7; i++) ui_text(FONT_BODY, STYLE_WHITE, 196, 114 + i * 12, CTRL[i]);

    static const char *const ITEMS[] = { "Resume", "Quit to Title" };
    draw_menu(ITEMS, 2, 210);
}

static void do_respawn(void)
{
    g_state = GS_PLAY;
    story_respawn();
}

static void gameover_update(void)
{
    menu_sel = (menu_sel + menu_nav() + 2) % 2;
    if (game_fading() || state_t < 1.0f) return;
    if (g_frame.pressed.a || g_frame.pressed.start) {
        sfx_play(SFX_CONFIRM);
        game_fade_to(menu_sel == 0 ? do_respawn : quit_to_title);
    }
}

static void gameover_render(void)
{
    float k = clampf(state_t, 0, 1);
    ui_rect_alpha(0, 0, SCREEN_W, SCREEN_H, 0x20000000 | (uint32_t)(k * 190));
    ui_text_center(FONT_TITLE, STYLE_RED, 96, "Rudeus collapsed...");
    ui_text_center(FONT_OUTLINE, STYLE_GRAY, 116, "\"Even if I fall, I'll stand up again.\"");
    static const char *const ITEMS[] = { "Try Again", "Quit to Title" };
    if (state_t > 1.0f) draw_menu(ITEMS, 2, 160);
}

static const char *const CREDITS[] = {
    "$03MUSHOKU TENSEI 64", "An unofficial Nintendo 64 fan game", "",
    "Based on \"Mushoku Tensei: Jobless Reincarnation\"", "by Rifujin na Magonote", "",
    "$03Design, Code, Art & Music", "Claude (Anthropic)", "",
    "$03Built With", "libdragon - the open source N64 SDK", "DejaVu fonts", "",
    "$03Cast", "Rudeus Greyrat", "Roxy Migurdia", "Sylphiette", "Paul & Zenith Greyrat", "Lilia",
    "Ruijerd Superdia", "", "", "Thank you for playing!", "", "", "$03THE END",
};

static void credits_update(float dt)
{
    world_update(dt);
    float a = state_t * 0.04f;
    g_cam.override = true;
    g_cam.ov_target = v3(0, 3, -4);
    g_cam.ov_pos = v3(cosf(a) * 22, 10, -4 + sinf(a) * 22);
    float end = (int)(sizeof(CREDITS) / sizeof(CREDITS[0])) * 16 / 18.0f + 16.0f;
    if ((g_frame.pressed.start && state_t > 3) || state_t > end + 6) {
        if (!game_fading()) game_fade_to(quit_to_title);
    }
}

static void credits_render(void)
{
    render_scene(false);
    ui_rect_alpha(0, 0, SCREEN_W, SCREEN_H, 0x080410A8);
    int n = sizeof(CREDITS) / sizeof(CREDITS[0]);
    float scroll = state_t * 18.0f;
    for (int i = 0; i < n; i++) {
        float y = SCREEN_H + 20 + i * 16 - scroll;
        if (i == n - 1 && y < SCREEN_H / 2) y = SCREEN_H / 2;   /* THE END stays */
        if (y < -10 || y > SCREEN_H + 20) continue;
        const char *s = CREDITS[i];
        bool head = s[0] == '$';
        ui_text_center(head ? FONT_TITLE : FONT_OUTLINE, head ? STYLE_GOLD : STYLE_WHITE, (int)y, head ? s + 3 : s);
    }
}

/* ------------------------------------------------------------------ */
/* Main loop                                                           */
/* ------------------------------------------------------------------ */

static void gl_setup(void)
{
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
#ifndef EXP_NOAA
    glEnable(GL_MULTISAMPLE_ARB);
#endif
    GLfloat spec[4] = { 0, 0, 0, 1 };
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
}

static void play_update(float dt)
{
    if (g_frame.pressed.start && !dialog_active() && !story_card_active() && !game_fading() &&
        !g_player.locked && g_player.hp > 0) {
        g_state = GS_PAUSE;
        menu_sel = 0;
        sfx_play(SFX_CONFIRM);
        return;
    }
    bool was_dialog = dialog_active();
    dialog_update(dt);
    if (was_dialog) {
        /* the button that advanced the text must not also jump or attack */
        g_frame.pressed.a = g_frame.pressed.b = 0;
    }
    player_update(dt);
    story_update(dt);
    if (!dialog_active() && !story_card_active()) combat_update(dt);
    world_update(dt);
    camera_update(dt);
    hud_update(dt);

    if (g_player.hp <= 0) {
        dead_t += dt;
        if (dead_t > 1.8f && !game_fading()) {
            g_state = GS_GAMEOVER;
            state_t = 0;
            menu_sel = 0;
            music_play(MUS_SORROW);
        }
    } else {
        dead_t = 0;
    }
    if (story_wants_credits() && !game_fading() && g_state == GS_PLAY) {
        g_state = GS_CREDITS;
        state_t = 0;
        music_play(MUS_TITLE);
    }
}

int main(void)
{
    debug_init_isviewer();
    debug_init_usblog();
    dfs_init(DFS_DEFAULT_LOCATION);
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);
    rdpq_init();
    gl_init();
    joypad_init();
    sound_init();
    save_init();

    gfx_init();
    gl_setup();
    models_init();
    world_init();
    story_init();
    srand(1234);

#ifdef PERF_LOG
    rspq_profile_start();
#endif
#ifdef TEST_STEP
    g_state = GS_PLAY;
    story_debug_start(TEST_STEP);
#else
    goto_title();
#endif

    uint64_t last = get_ticks();
    float fps_acc = 0;
    int fps_frames = 0;
#ifdef PERF_LOG
    uint64_t perf_upd = 0, perf_cpu = 0, perf_wait = 0;
#endif
    while (1) {
        uint64_t now = get_ticks();
        float dt = (float)(now - last) / (float)TICKS_PER_SECOND;
        last = now;
        dt = clampf(dt, 0.001f, 1.0f / 15.0f);
        g_frame.dt = dt;
        g_frame.time += dt;
        g_frame.frame++;
        state_t += dt;

        fps_acc += dt;
        fps_frames++;
        if (fps_acc >= 5.0f) {
            debugf("fps %.1f state %d step %d t=%.1f pos %.1f %.1f\n", fps_frames / fps_acc, g_state, (int)g_save.step, g_frame.time, g_player.pos.x, g_player.pos.z);
#ifdef PERF_LOG
            debugf("  avg ms: update %.2f  render-cpu %.2f  gpu-wait %.2f\n",
                   TICKS_TO_US(perf_upd) / 1000.0f / fps_frames, TICKS_TO_US(perf_cpu) / 1000.0f / fps_frames,
                   TICKS_TO_US(perf_wait) / 1000.0f / fps_frames);
            debugf("  sections ms: begin %.2f sky %.2f world %.2f actors %.2f fx %.2f\n",
                   TICKS_TO_US(perf_sec[0]) / 1000.0f / fps_frames, TICKS_TO_US(perf_sec[1]) / 1000.0f / fps_frames,
                   TICKS_TO_US(perf_sec[2]) / 1000.0f / fps_frames, TICKS_TO_US(perf_sec[3]) / 1000.0f / fps_frames,
                   TICKS_TO_US(perf_sec[4]) / 1000.0f / fps_frames);
            memset(perf_sec, 0, sizeof(perf_sec));
            rspq_profile_dump();
            rspq_profile_reset();
            perf_upd = perf_cpu = perf_wait = 0;
#endif
            fps_acc = 0;
            fps_frames = 0;
        }
#ifdef PERF_LOG
        uint64_t p0 = get_ticks();
#endif

        read_input();
        if (rumble_t > 0) {
            rumble_t -= dt;
            if (rumble_t <= 0) joypad_set_rumble_active(JOYPAD_PORT_1, false);
        }
        update_fade(dt);

        switch (g_state) {
        case GS_TITLE:    title_update(dt); break;
        case GS_PLAY:     play_update(dt); break;
        case GS_PAUSE:    pause_update(); break;
        case GS_GAMEOVER: gameover_update(); break;
        case GS_CREDITS:  credits_update(dt); break;
        }

#ifdef PERF_LOG
        uint64_t p1 = get_ticks();
        perf_upd += p1 - p0;
#endif
        switch (g_state) {
        case GS_TITLE:
            title_render();
            break;
        case GS_CREDITS:
            credits_render();
            break;
        default:
            render_scene(true);
            if (!story_card_active()) hud_render();
            story_render_ui();
            dialog_render();
            if (g_state == GS_PAUSE) pause_render();
            if (g_state == GS_GAMEOVER) gameover_render();
            break;
        }
        ui_fade(fade, 0x000000FF);
#ifdef PERF_LOG
        uint64_t p2 = get_ticks();
        perf_cpu += p2 - p1;
        rspq_wait();
        perf_wait += get_ticks() - p2;
#endif
        rdpq_detach_show();
#ifdef PERF_LOG
        rspq_profile_next_frame();
#endif
    }
}
