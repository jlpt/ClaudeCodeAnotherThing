/*
 * Audio: a tiny integer chiptune synthesizer running in the N64 AI buffer
 * callback. Four music voices (pulse / triangle / noise) play songs written
 * in MML, two more voices play pitch-swept sound effects.
 *
 * All music here is original, composed for this fan game.
 */
#include "game.h"

#define RATE        22050
#define CHUNK       32          /* envelope / sequencer granularity (samples) */
#define NCH         4           /* music channels */
#define NSFX        2
#define TICKS_WHOLE 192         /* ticks per whole note */

enum { W_SQ50, W_SQ25, W_SQ12, W_TRI, W_SAW, W_NOISE };

typedef struct {
    uint8_t wave;
    uint8_t amp;        /* base amplitude, 0-255 */
    uint16_t decay;     /* per-chunk envelope multiplier, /4096 */
    uint16_t sustain;   /* envelope floor 0-65535 */
} instr_t;

static const instr_t INSTR[8] = {
    { W_SQ50,  150, 4076, 22000 },   /* @0 lead */
    { W_SQ25,  150, 4082, 20000 },   /* @1 soft lead */
    { W_TRI,   255, 4090, 40000 },   /* @2 bass */
    { W_SQ12,  110, 4040, 0 },       /* @3 arpeggio pluck */
    { W_NOISE, 110, 3750, 0 },       /* @4 hi-hat */
    { W_NOISE, 150, 3960, 0 },       /* @5 snare */
    { W_NOISE, 190, 3900, 0 },       /* @6 kick */
    { W_SAW,   90,  4092, 45000 },   /* @7 pad */
};

typedef struct { uint8_t note, instr, vol; uint16_t len; } ev_t;
typedef struct { ev_t *ev; int count; } track_t;
typedef struct { track_t ch[NCH]; uint16_t bpm; bool loop; } song_t;

typedef struct {
    uint32_t phase, inc;
    int32_t inc_delta;      /* sfx pitch sweep per chunk */
    uint32_t env;           /* 0..65535 */
    uint16_t decay, sustain;
    uint8_t wave, amp, vol;
    uint16_t lfsr;
    bool on, released;
    int32_t chunks_left;    /* sfx duration */
} voice_t;

typedef struct {
    const ev_t *ev;
    int count, pos;
    int32_t ticks_left, gate_left;
    bool done;
} cursor_t;

static song_t songs[MUS_COUNT];
static uint32_t note_inc[128];

static volatile int cur_song = MUS_NONE;
static int resume_song = MUS_NONE;
static cursor_t cur[NCH];
static voice_t mv[NCH], sv[NSFX];
static uint32_t tick_acc, ticks_per_chunk;   /* 16.16 fixed point */
static int chunk_left;
static int next_sfx;

/* ------------------------------------------------------------------ */
/* MML parser (runs once at boot)                                      */
/* ------------------------------------------------------------------ */

static int parse_num(const char **p, int def)
{
    if (**p < '0' || **p > '9') return def;
    int n = 0;
    while (**p >= '0' && **p <= '9') n = n * 10 + (*(*p)++ - '0');
    return n;
}

static int parse_track(const char *mml, ev_t *out, int max, int *bpm)
{
    static const int8_t SEMI[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* a..g */
    int n = 0, oct = 4, deflen = 8, vol = 12, instr = 0;
    const char *loop_start = NULL;
    int loop_count = 0;
    const char *p = mml;
    while (*p && n < max) {
        char c = *p++;
        if (c >= 'a' && c <= 'g') {
            int semi = SEMI[c - 'a'];
            if (*p == '+' || *p == '#') { semi++; p++; }
            else if (*p == '-') { semi--; p++; }
            int len = parse_num(&p, deflen);
            int ticks = TICKS_WHOLE / len;
            if (*p == '.') { ticks += ticks / 2; p++; }
            int note = 12 * (oct + 1) + semi;
            out[n++] = (ev_t){ (uint8_t)clampf(note, 1, 127), (uint8_t)instr, (uint8_t)vol, (uint16_t)ticks };
        } else if (c == 'r') {
            int len = parse_num(&p, deflen);
            int ticks = TICKS_WHOLE / len;
            if (*p == '.') { ticks += ticks / 2; p++; }
            out[n++] = (ev_t){ 0, (uint8_t)instr, 0, (uint16_t)ticks };
        } else if (c == 'o') oct = parse_num(&p, 4);
        else if (c == '>') oct++;
        else if (c == '<') oct--;
        else if (c == 'l') deflen = parse_num(&p, 8);
        else if (c == 'v') vol = parse_num(&p, 12);
        else if (c == '@') instr = parse_num(&p, 0) & 7;
        else if (c == 't') *bpm = parse_num(&p, 120);
        else if (c == '[') { loop_start = p; loop_count = -1; }
        else if (c == ']') {
            if (loop_count < 0) loop_count = parse_num(&p, 2) - 1;
            else { while (*p >= '0' && *p <= '9') p++; }
            if (loop_count > 0 && loop_start) {
                loop_count--;
                p = loop_start;   /* the repeat count after ']' is skipped on later passes */
            } else {
                loop_start = NULL;
            }
        }
    }
    return n;
}

static void build_song(music_id_t id, bool loop, const char *c0, const char *c1, const char *c2, const char *c3)
{
    const char *src[NCH] = { c0, c1, c2, c3 };
    int bpm = 120;
    songs[id].loop = loop;
    for (int i = 0; i < NCH; i++) {
        if (!src[i]) { songs[id].ch[i].count = 0; continue; }
        ev_t *tmp = malloc(sizeof(ev_t) * 1024);
        int n = parse_track(src[i], tmp, 1024, &bpm);
        songs[id].ch[i].ev = realloc(tmp, sizeof(ev_t) * (n ? n : 1));
        songs[id].ch[i].count = n;
    }
    songs[id].bpm = bpm;
}

/* ------------------------------------------------------------------ */
/* Songs (original compositions)                                       */
/* ------------------------------------------------------------------ */

static void build_songs(void)
{
    /* Title: a gentle waltz in D major */
    build_song(MUS_TITLE, true,
        "t84 @1 v11 o5 l8"
        "f+4. e d4 | e4 c+ d e4 | f+4. a b4 | a2. |"
        "g4. f+ e4 | f+4 d4 <a4> | e4 f+ g a4 | e2. |"
        "f+4. e d4 | e4 c+ d e4 | f+4. a b4 | >c+2< a4 |"
        "b4. a g4 | a4. g e4 | f+2. | d2. |",
        "@2 v12 o3 l4"
        "d a a | <a >e e | <b >f+ f+ | <f+ >c+ c+ |"
        "<g >d d | d a a | e b b | <a >e e |"
        "d a a | <a >e e | <b >f+ f+ | <f+ >c+ c+ |"
        "<g >d d | <a >e e | d a a | d a >d< |",
        "@3 v6 o4 l8"
        "d f+ a f+ d f+ | c+ e a e c+ e | d f+ b f+ d f+ | c+ f+ a f+ c+ f+ |"
        "d g b g d g | d f+ a f+ d f+ | e g b g e g | c+ e a e c+ e |"
        "d f+ a f+ d f+ | c+ e a e c+ e | d f+ b f+ d f+ | c+ f+ a f+ c+ f+ |"
        "d g b g d g | c+ e a e c+ e | d f+ a f+ d f+ | d f+ a f+ d f+ |",
        NULL);

    /* Buena Village: cheerful, G major */
    build_song(MUS_VILLAGE, true,
        "t112 @1 v12 o4 l8"
        "b4 a g a4 b4 | g4 e4 e2 | e4 g a >c4< b a | a4. f+ d2 |"
        "b4 a g a4 b4 | >d4< b g e2 | e4 a g f+4 a4 | g2 r4 d4 |"
        "e4 e f+ g4 e4 | f+4 f+ g a4 f+4 | b4 a b >d4< b4 | g2 e4 g4 |"
        ">c4< b a g4 e4 | f+4 a g f+4 d4 | g4 b a g4 d4 | a2 r4 d4 |",
        "@2 v13 o3 l4"
        "g >d< g >d< | e b e b | c g c g | d a d a |"
        "g >d< g >d< | e b e b | a >e< d a | g >d< g d |"
        "c g c g | d a d a | <b >f+ <b >f+ | e b e b |"
        "c g c g | d a d f+ | g >d< g b | d a >c< a |",
        "@3 v6 o4 l16"
        "[g b >d< b]4 [e g b g]4 [e g >c< g]4 [d f+ a f+]4"
        "[g b >d< b]4 [e g b g]4 [e a >c< a]2 [d f+ a f+]2 [g b >d< b]4"
        "[e g >c< g]4 [d f+ a f+]4 [d f+ b f+]4 [e g b g]4"
        "[e g >c< g]4 [d f+ a f+]4 [g b >d< b]4 [d f+ a f+]4",
        "v9 l8 [@4 o7 c @4 o7 c @5 o5 c @4 o7 c]32");

    /* Roxy's lessons: warm, F major */
    build_song(MUS_LESSON, true,
        "t96 @1 v12 o4 l8"
        "a4. b- >c4< a4 | g4. a g4 e4 | f4. g a4 >d4< | >c2< b-4 a4 |"
        "a4. b- >c4 f4< | >e4. d c2< | >d4 c< b- a4 g4 | f2. r4 |",
        "@2 v12 o3 l4"
        "f >c< f >c< | c g c g | d a d a | <b- >f <b- >f |"
        "f >c< f >c< | c g c g | <b- >f c g | f >c< f r |",
        "@3 v6 o4 l8"
        "[f a >c< a]2 [e g >c< g]2 [d f a f]2 [d f b- f]2"
        "[f a >c< a]2 [e g >c< g]2 d f b- f e g >c< g [f a >c< a]2",
        NULL);

    /* Battle: driving, D minor */
    build_song(MUS_BATTLE, true,
        "t144 @0 v11 o4 l8"
        "d4 f a >d4< a f | e f g a a4 g f | f4 d <b- >d4 f4 | e4 c <g >c4 e4 |"
        "d4 f a >d4< a >c< | >d e f e d c d4< | >d4 c< b- a4 g4 | a4 e c+ <a2> |",
        "@2 v13 o2 l8"
        "[d d >d< d]4 [<b- b- >b- <b- >]2 [c c >c< c]2"
        "[d d >d< d]4 [<b- b- >b- <b- >]2 [<a a >a <a >]2",
        "@3 v6 o4 l16"
        "[d f a f]8 [d f b- f]4 [e g >c< g]4"
        "[d f a f]8 [d f b- f]4 [c+ e a e]4",
        "v10 l8 [@6 o3 c @4 o7 c @5 o5 c @4 o7 c]16");

    /* Boss: urgent, E minor */
    build_song(MUS_BOSS, true,
        "t160 @0 v11 o4 l8"
        "e g b >e4< b g e | e g >c4< g e c e | f+ a >d4< a f+ d f+ | d+ f+ b4 a f+ d+4 |"
        ">e4 d< b >c4< b g | a4 g e g4 >c4< | >c4< b a g4 a4 | b2 d+4 f+4 |",
        "@2 v13 o2 l8"
        "[e e >e< e]2 [c c >c< c]2 [d d >d< d]2 < [b b >b< b]2 >"
        "[e e >e< e]2 [c c >c< c]2 [a a >a< a]2 < [b b >b< b]2 >",
        "@3 v6 o4 l16"
        "[e g b g]4 [e g >c< g]4 [d f+ a f+]4 [d+ f+ b f+]4"
        "[e g b g]4 [e g >c< g]4 [c e a e]4 [d+ f+ b f+]4",
        "v11 l8 [@6 o3 c @4 o7 c @5 o5 c @6 o3 c]16");

    /* Farewells and far-away places: A minor */
    build_song(MUS_SORROW, true,
        "t72 @1 v11 o4 l8"
        "e4. d c4 <a4> | c4. d e4 f4 | g4. f e4 c4 | d2 <b4> d4 |"
        "c4 d e f4 a4 | g4. f d4 <b4> | c4 <b a> c4 e4 | a2 r2 |",
        "@2 v11 o3 l2"
        "<a >e | f c | c g | <g >d | f c | <g >d | <a >e | <a1> |",
        "@3 v5 o4 l8"
        "[a >c e c<]2 [f a >c< a]2 [e g >c< g]2 [d g b g]2"
        "[f a >c< a]2 [d g b g]2 [a >c e c<]2 [a >c e c<]2",
        NULL);

    /* Spell learned / chapter clear jingle (does not loop) */
    build_song(MUS_FANFARE, false,
        "t132 @1 v13 o5 l16 c e g >c8< g8 >c4. r8",
        "@2 v12 o3 l8 c g >c< g c4. r8",
        "@3 v7 o4 l16 [c e g >c<]3 c4. r8",
        "v10 l8 @5 o5 c @5 o5 c @6 o3 c4. r8");
}

/* ------------------------------------------------------------------ */
/* Synthesis (runs under the AI interrupt)                             */
/* ------------------------------------------------------------------ */

static inline int32_t wave_sample(voice_t *v)
{
    uint32_t ph = v->phase;
    uint32_t old = ph;
    v->phase = ph + v->inc;
    switch (v->wave) {
    case W_SQ50: return (ph >> 31) ? 8192 : -8192;
    case W_SQ25: return (ph >> 30) == 0 ? 8192 : -8192;
    case W_SQ12: return (ph >> 29) == 0 ? 8192 : -8192;
    case W_TRI: {
        int32_t t = (int32_t)(ph >> 16);
        t = t < 32768 ? t * 2 - 32768 : (65535 - t) * 2 - 32768;
        return t >> 2;
    }
    case W_SAW: return ((int32_t)(ph >> 16) - 32768) >> 2;
    default:
        if (v->phase < old) {
            uint16_t bit = ((v->lfsr >> 0) ^ (v->lfsr >> 1)) & 1;
            v->lfsr = (v->lfsr >> 1) | (bit << 14);
        }
        return (v->lfsr & 1) ? 8192 : -8192;
    }
}

static void note_on(voice_t *v, const ev_t *e)
{
    const instr_t *in = &INSTR[e->instr];
    v->wave = in->wave;
    v->amp = in->amp;
    v->decay = in->decay;
    v->sustain = in->sustain;
    v->vol = e->vol;
    v->inc = note_inc[e->note];
    if (in->wave == W_NOISE) v->inc <<= 3;
    v->env = 65535;
    v->on = true;
    v->released = false;
    if (!v->lfsr) v->lfsr = 0x4001;
}

static void start_cursors(const song_t *s)
{
    for (int c = 0; c < NCH; c++) {
        cur[c] = (cursor_t){ s->ch[c].ev, s->ch[c].count, 0, 1, 0, false };
        mv[c].on = false;
    }
    ticks_per_chunk = (uint32_t)((uint64_t)CHUNK * s->bpm * 48 * 65536 / (60 * RATE));
    tick_acc = 0;
}

static void seq_tick(void)
{
    const song_t *s = &songs[cur_song];
    bool all_done = true;
    for (int c = 0; c < NCH; c++) {
        cursor_t *k = &cur[c];
        if (!k->ev || k->count == 0 || k->done) continue;
        all_done = false;
        if (--k->gate_left == 0) mv[c].released = true;
        if (--k->ticks_left > 0) continue;
        if (k->pos >= k->count) {
            if (s->loop) k->pos = 0;
            else { k->done = true; mv[c].released = true; continue; }
        }
        const ev_t *e = &k->ev[k->pos++];
        k->ticks_left = e->len;
        k->gate_left = e->len - e->len / 8;
        if (e->note) note_on(&mv[c], e);
        else mv[c].released = true;
    }
    if (all_done && !s->loop) {
        /* one-shot jingle finished: go back to the previous song */
        int r = resume_song;
        cur_song = MUS_NONE;
        if (r != MUS_NONE) {
            start_cursors(&songs[r]);
            cur_song = r;
        }
    }
}

static void engine_chunk(void)
{
    if (cur_song != MUS_NONE) {
        tick_acc += ticks_per_chunk;
        while (tick_acc >= 65536) {
            tick_acc -= 65536;
            seq_tick();
            if (cur_song == MUS_NONE) break;
        }
    }
    for (int c = 0; c < NCH; c++) {
        voice_t *v = &mv[c];
        if (!v->on) continue;
        if (v->released) {
            v->env = (v->env * 3700) >> 12;
            if (v->env < 64) v->on = false;
        } else if (v->env > v->sustain) {
            v->env = (v->env * v->decay) >> 12;
            if (v->env < v->sustain) v->env = v->sustain;
        }
    }
    for (int i = 0; i < NSFX; i++) {
        voice_t *v = &sv[i];
        if (!v->on) continue;
        v->inc = (uint32_t)((int32_t)v->inc + v->inc_delta);
        if (--v->chunks_left <= 0) {
            v->env = (v->env * 3500) >> 12;
            if (v->env < 64) v->on = false;
        }
    }
}

static void audio_fill(short *buf, size_t numsamples)
{
    size_t i = 0;
    while (i < numsamples) {
        if (chunk_left <= 0) { engine_chunk(); chunk_left = CHUNK; }
        size_t n = numsamples - i;
        if (n > (size_t)chunk_left) n = chunk_left;
        for (size_t k = 0; k < n; k++) {
            int32_t mix = 0;
            for (int c = 0; c < NCH; c++) {
                voice_t *v = &mv[c];
                if (!v->on) continue;
                int32_t s = wave_sample(v);
                mix += (((s * (int32_t)(v->env >> 8)) >> 8) * v->amp * v->vol) >> 12;
            }
            for (int c = 0; c < NSFX; c++) {
                voice_t *v = &sv[c];
                if (!v->on) continue;
                int32_t s = wave_sample(v);
                mix += (((s * (int32_t)(v->env >> 8)) >> 8) * v->amp * v->vol) >> 12;
            }
            mix = (mix * 3) >> 1;
            if (mix > 32767) mix = 32767;
            if (mix < -32768) mix = -32768;
            buf[(i + k) * 2] = (short)mix;
            buf[(i + k) * 2 + 1] = (short)mix;
        }
        i += n;
        chunk_left -= n;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void sound_init(void)
{
    for (int n = 0; n < 128; n++) {
        double f = 440.0 * pow(2.0, (n - 69) / 12.0);
        note_inc[n] = (uint32_t)(f * 4294967296.0 / RATE);
    }
    build_songs();
    audio_init(RATE, AUDIO_DEFAULT_LATENCY);
    audio_set_buffer_callback(audio_fill);
}

music_id_t music_current(void)
{
    return cur_song == MUS_FANFARE ? (music_id_t)resume_song : (music_id_t)cur_song;
}

void music_play(music_id_t m)
{
    if (m == cur_song) return;
    if (m != MUS_FANFARE && cur_song == MUS_FANFARE) {
        /* let the jingle finish, then switch */
        resume_song = m;
        return;
    }
    disable_interrupts();
    if (m == MUS_FANFARE) resume_song = cur_song;
    for (int c = 0; c < NCH; c++) mv[c].on = false;
    if (m == MUS_NONE) {
        cur_song = MUS_NONE;
    } else {
        start_cursors(&songs[m]);
        cur_song = m;
    }
    enable_interrupts();
}

typedef struct { uint8_t wave, n0, n1, vol; uint16_t ms; } sfx_def_t;

static const sfx_def_t SFX[SFX_COUNT] = {
    [SFX_CURSOR]  = { W_SQ25, 84, 84, 9, 50 },
    [SFX_CONFIRM] = { W_SQ25, 76, 88, 10, 120 },
    [SFX_JUMP]    = { W_SQ50, 58, 72, 7, 140 },
    [SFX_SWING]   = { W_NOISE, 100, 80, 7, 110 },
    [SFX_WATER]   = { W_SQ12, 82, 62, 10, 200 },
    [SFX_FIRE]    = { W_NOISE, 80, 56, 12, 320 },
    [SFX_STONE]   = { W_NOISE, 68, 44, 13, 180 },
    [SFX_HEAL]    = { W_SQ25, 72, 96, 9, 420 },
    [SFX_HIT]     = { W_NOISE, 84, 62, 12, 90 },
    [SFX_HURT]    = { W_SQ50, 64, 46, 11, 260 },
    [SFX_EXPLODE] = { W_NOISE, 64, 22, 15, 650 },
    [SFX_TEXT]    = { W_SQ25, 86, 86, 4, 20 },
    [SFX_THUNDER] = { W_NOISE, 44, 18, 15, 1200 },
    [SFX_CHARGE]  = { W_SQ12, 60, 86, 6, 350 },
    [SFX_LEARN]   = { W_SQ25, 72, 84, 10, 500 },
    [SFX_STEP]    = { W_NOISE, 44, 30, 12, 120 },
    [SFX_FAIL]    = { W_SQ50, 50, 44, 9, 220 },
};

void sfx_play(sfx_id_t s)
{
    const sfx_def_t *d = &SFX[s];
    int chunks = d->ms * RATE / 1000 / CHUNK;
    if (chunks < 1) chunks = 1;
    uint32_t i0 = note_inc[d->n0], i1 = note_inc[d->n1];
    if (d->wave == W_NOISE) { i0 <<= 3; i1 <<= 3; }
    disable_interrupts();
    voice_t *v = &sv[next_sfx];
    next_sfx = (next_sfx + 1) % NSFX;
    v->wave = d->wave;
    v->amp = 180;
    v->vol = d->vol;
    v->inc = i0;
    v->inc_delta = ((int32_t)i1 - (int32_t)i0) / chunks;
    v->env = 65535;
    v->chunks_left = chunks;
    v->on = true;
    if (!v->lfsr) v->lfsr = 0x2A01;
    enable_interrupts();
}
