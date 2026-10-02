/*
 * Dialogue boxes (typewriter text), the in-game HUD with OoT-style
 * C-button spell slots, banners, toasts and the boss health bar.
 */
#include "game.h"

/* ------------------------------------------------------------------ */
/* Dialogue                                                            */
/* ------------------------------------------------------------------ */

static const dline_t *d_lines;
static int d_count, d_index;
static float d_chars;
static int d_last_blip;
static dialog_done_fn d_done;
static bool d_active;
static float d_open_t;

void dialog_start(const dline_t *lines, int count, dialog_done_fn done)
{
    d_lines = lines;
    d_count = count;
    d_index = 0;
    d_chars = 0;
    d_last_blip = 0;
    d_done = done;
    d_active = true;
    d_open_t = 0;
    g_player.charge_spell = -1;
}

bool dialog_active(void) { return d_active; }

void dialog_update(float dt)
{
    if (!d_active) return;
    d_open_t += dt;
    const char *txt = d_lines[d_index].text;
    int len = (int)strlen(txt);
    d_chars += dt * 48.0f;
    if ((int)d_chars / 3 != d_last_blip && (int)d_chars < len) {
        d_last_blip = (int)d_chars / 3;
        sfx_play(SFX_TEXT);
    }
    if (d_open_t < 0.15f) return;   /* swallow the button press that opened the box */
    if (g_frame.pressed.a || g_frame.pressed.b) {
        if ((int)d_chars < len) {
            d_chars = (float)len;
        } else {
            d_index++;
            d_chars = 0;
            d_last_blip = 0;
            sfx_play(SFX_CURSOR);
            if (d_index >= d_count) {
                d_active = false;
                dialog_done_fn cb = d_done;
                d_done = NULL;
                if (cb) cb();
            }
        }
    }
}

static int speaker_style(const char *name)
{
    if (!name) return STYLE_GRAY;
    if (!strncmp(name, "Roxy", 4)) return STYLE_BLUE;
    if (!strncmp(name, "Sylph", 5)) return STYLE_GREEN;
    if (!strncmp(name, "Rudeus", 6)) return STYLE_GOLD;
    if (!strncmp(name, "Ruijerd", 7)) return STYLE_RED;
    if (!strncmp(name, "Zenith", 6) || !strncmp(name, "Lilia", 5)) return STYLE_PINK;
    return STYLE_WHITE;
}

void dialog_render(void)
{
    if (!d_active) return;
    const dline_t *l = &d_lines[d_index];
    const int x0 = 14, y0 = 158, x1 = SCREEN_W - 14, y1 = 230;
    ui_rect_alpha(x0, y0, x1, y1, 0x0C1028E0);
    ui_rect(x0, y0, x1, y0 + 1, 0xC8A860FF);
    ui_rect(x0, y1 - 1, x1, y1, 0xC8A860FF);
    ui_rect(x0, y0, x0 + 1, y1, 0xC8A860FF);
    ui_rect(x1 - 1, y0, x1, y1, 0xC8A860FF);
    if (l->speaker) {
        int w = 12 + (int)strlen(l->speaker) * 7;
        ui_rect_alpha(x0 + 8, y0 - 12, x0 + 8 + w, y0 + 2, 0x0C1028F0);
        ui_rect(x0 + 8, y0 - 12, x0 + 8 + w, y0 - 11, 0xC8A860FF);
        ui_text(FONT_BODY, speaker_style(l->speaker), x0 + 14, y0 - 1, l->speaker);
    }
    bool thought = l->text[0] == '(';
    ui_text_box(FONT_BODY, thought ? STYLE_GRAY : (l->speaker ? STYLE_WHITE : STYLE_GOLD),
                x0 + 12, y0 + 18, x1 - x0 - 24, (int)d_chars, l->text);
    if ((int)d_chars >= (int)strlen(l->text) && fmodf(g_frame.time, 0.8f) < 0.5f) {
        ui_circle(x1 - 16, y1 - 12, 0x4060E0FF, false);
        ui_text(FONT_BODY, STYLE_WHITE, x1 - 20, y1 - 8, "A");
    }
}

/* ------------------------------------------------------------------ */
/* HUD                                                                 */
/* ------------------------------------------------------------------ */

static const char *banner_title, *banner_sub;
static float banner_t, banner_len;
static char toast_buf[96];
static float toast_t;
static const char *boss_name;
static float boss_frac;
static int boss_frames;
static float shown_hp = -1;

void hud_banner(const char *title, const char *subtitle, float seconds)
{
    banner_title = title;
    banner_sub = subtitle;
    banner_t = banner_len = seconds;
}

void hud_toast(const char *text, float seconds)
{
    strncpy(toast_buf, text, sizeof(toast_buf) - 1);
    toast_buf[sizeof(toast_buf) - 1] = 0;
    toast_t = seconds;
}

void hud_boss_bar(const char *name, float frac)
{
    boss_name = name;
    boss_frac = frac;
    boss_frames = 2;
}

void hud_update(float dt)
{
    banner_t = fmaxf(0, banner_t - dt);
    toast_t = fmaxf(0, toast_t - dt);
    if (shown_hp < 0) shown_hp = g_player.hp;
    shown_hp = approachf(shown_hp, g_player.hp, dt * 3.0f);
}

static void draw_bar(int x, int y, int w, int h, float frac, uint32_t fill, uint32_t back)
{
    ui_rect(x - 1, y - 1, x + w + 1, y + h + 1, 0x101018FF);
    ui_rect(x, y, x + w, y + h, back);
    int fw = (int)(w * clampf(frac, 0, 1));
    if (fw > 0) {
        ui_rect(x, y, x + fw, y + h, fill);
        ui_rect(x, y, x + fw, y + 1, color_mix(fill, 0xFFFFFFFF, 0.45f));
    }
}

static void draw_cbuttons(void)
{
    /* OoT-style diamond of C buttons in the top-right corner */
    static const int pos[4][2] = { { 282, 40 }, { 298, 56 }, { 314, 40 }, { 298, 24 } }; /* L D R U */
    static const int spell[4] = { SPELL_WATER, SPELL_FIRE, SPELL_STONE, SPELL_HEAL };
    static const char *label[4] = { "<", "v", ">", "^^" };   /* ^ is an rdpq_text escape */
    for (int i = 0; i < 4; i++) {
        int s = spell[i];
        bool known = (g_save.spells >> s) & 1;
        bool charging = g_player.charge_spell == s;
        int cx = pos[i][0] - 8, cy = pos[i][1];
        uint32_t ring = charging ? 0xFFFFFFFF : 0xF0D030FF;
        ui_circle(cx, cy, known ? SPELL_COLORS[s] : 0x30303880, false);
        ui_circle(cx, cy, ring, true);
        if (!known) ui_text(FONT_OUTLINE, STYLE_GOLD, cx - 3, cy + 4, label[i]);
    }
    if (g_player.charge_spell >= 0) {
        int s = g_player.charge_spell;
        float c = g_player.charge;
        int lvl = c < 0.35f ? 1 : (c < 1.1f ? 2 : 3);
        char buf[40];
        snprintf(buf, sizeof(buf), "%s  Lv%d", SPELL_NAMES[s], lvl);
        rdpq_text_print(&(rdpq_textparms_t){ .style_id = STYLE_WHITE, .width = 150, .align = ALIGN_RIGHT },
                        FONT_OUTLINE, SCREEN_W - 166, 84, buf);
    }
}

void hud_render(void)
{
    bool in_dialog = dialog_active();

    /* life gems */
    ui_text(FONT_OUTLINE, STYLE_RED, 18, 26, "HP");
    int n = (int)ceilf(g_player.max_hp);
    for (int i = 0; i < n; i++) {
        float fill = clampf(shown_hp - i, 0, 1);
        int x = 44 + i * 14, y = 21;
        ui_circle(x, y, 0x40101880, false);
        if (fill > 0.01f) ui_circle(x, y, color_mix(0x701020FF, 0xFF3048FF, fill), false);
        ui_circle(x, y, 0xF0E0C0FF, true);
    }
    /* mana bar */
    ui_text(FONT_OUTLINE, STYLE_BLUE, 18, 44, "MP");
    uint32_t mpc = g_player.no_mp_flash > 0 && fmodf(g_frame.time, 0.2f) < 0.1f ? 0xFF4040FF : 0x3A7AF0FF;
    draw_bar(40, 36, 84, 6, g_player.mp / g_player.max_mp, mpc, 0x182040FF);

    draw_cbuttons();

    /* Z-target reticle */
    if (g_player.lock_target >= 0) {
        enemy_t *e = &g_enemies[g_player.lock_target];
        float sx, sy;
        if (e->active && gfx_project(v3_add(e->pos, v3(0, e->radius * 1.6f + 0.6f, 0)), &sx, &sy)) {
            float bob = sinf(g_frame.time * 8.0f) * 2.0f;
            ui_circle((int)sx, (int)(sy + bob), 0xFFE040FF, true);
            ui_rect((int)sx - 1, (int)(sy + bob) + 9, (int)sx + 1, (int)(sy + bob) + 14, 0xFFE040FF);
        }
    }

    /* objective line */
    const char *obj = story_objective();
    if (obj && obj[0] && !in_dialog) {
        rdpq_text_print(&(rdpq_textparms_t){ .style_id = STYLE_WHITE, .width = 230, .wrap = WRAP_WORD },
                        FONT_OUTLINE, 16, SCREEN_H - 26, obj);
    }

    /* contextual A action */
    const char *hint = story_interact_hint();
    if (hint && !in_dialog && !g_player.locked) {
        ui_circle(262, SCREEN_H - 30, 0x3050E0FF, false);
        ui_text(FONT_OUTLINE, STYLE_WHITE, 258, SCREEN_H - 26, "A");
        ui_text(FONT_OUTLINE, STYLE_WHITE, 274, SCREEN_H - 26, hint);
    }

    if (boss_frames > 0) {
        boss_frames--;
        ui_text_center(FONT_OUTLINE, STYLE_RED, 196, boss_name);
        draw_bar(60, 202, 200, 6, boss_frac, 0xE03040FF, 0x301018FF);
    }

    if (toast_t > 0) {
        ui_rect_alpha(40, 120, SCREEN_W - 40, 138, 0x00000090);
        ui_text_center(FONT_BODY, STYLE_WHITE, 133, toast_buf);
    }

    if (banner_t > 0 && banner_title) {
        float k = fminf(1.0f, fminf(banner_t, banner_len - banner_t) * 2.0f);
        int a = (int)(k * 200);
        ui_rect_alpha(0, 62, SCREEN_W, 106, 0x08081000 | a);
        if (k > 0.5f) {
            ui_text_center(FONT_TITLE, STYLE_GOLD, 84, banner_title);
            if (banner_sub) ui_text_center(FONT_OUTLINE, STYLE_WHITE, 100, banner_sub);
        }
    }
}
