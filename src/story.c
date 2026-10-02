/*
 * Story: NPCs, quest steps, dialogue scripts, map transitions and the
 * special scenes (the gate, Cumulonimbus, the Mana Calamity).
 *
 * This is an unofficial fan game. All dialogue is original writing that
 * follows the broad beats of the early Mushoku Tensei story.
 */
#include "game.h"

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define TALK(arr, done) dialog_start(arr, COUNT(arr), done)

/* ------------------------------------------------------------------ */
/* NPCs                                                                */
/* ------------------------------------------------------------------ */

enum {
    NPC_ZENITH, NPC_PAUL, NPC_LILIA, NPC_ROXY, NPC_SYLPHIE, NPC_FARMER, NPC_WIFE,
    NPC_ELDER, NPC_RUIJERD, NPC_BULLY1, NPC_BULLY2, NPC_BULLY3, NPC_COUNT
};

typedef struct {
    bool active, has_goal, follow, fading, talkable;
    human_model_t model;
    vec3_t pos, goal;
    float yaw, anim_t, fade;
    anim_t pose;
    bool walking;
} npc_t;

static npc_t npcs[NPC_COUNT];

static void npc_place(int id, human_model_t m, float x, float z, float yaw)
{
    npc_t *n = &npcs[id];
    memset(n, 0, sizeof(*n));
    n->active = true;
    n->talkable = true;
    n->model = m;
    n->pos = v3(x, world_height(x, z), z);
    n->yaw = yaw;
    n->fade = 1;
    n->pose = ANIM_IDLE;
    n->anim_t = frand() * 5;
}

static void npc_walk(int id, float x, float z)
{
    npcs[id].goal = v3(x, 0, z);
    npcs[id].has_goal = true;
    npcs[id].follow = false;
}

static void update_npcs(float dt)
{
    for (int i = 0; i < NPC_COUNT; i++) {
        npc_t *n = &npcs[i];
        if (!n->active) continue;
        vec3_t tgt = n->pos;
        float stop = 0.15f, speed = 3.2f;
        if (n->follow) {
            tgt = g_player.pos;
            stop = 2.4f;
            if (dist_xz(n->pos, g_player.pos) > 6) speed = 6.5f;
        } else if (n->has_goal) {
            tgt = n->goal;
        }
        vec3_t d = v3_sub(tgt, n->pos);
        d.y = 0;
        float dist = v3_len(d);
        n->walking = false;
        if ((n->follow || n->has_goal) && dist > stop) {
            vec3_t step = v3_scale(d, fminf(speed * dt, dist - stop) / dist);
            n->pos = v3_add(n->pos, step);
            n->yaw = angle_lerp(n->yaw, atan2f(d.x, d.z), clampf(10 * dt, 0, 1));
            n->walking = true;
            world_collide(&n->pos, 0.35f);
        } else if (n->has_goal && !n->follow) {
            n->has_goal = false;
        }
        n->pos.y = world_height(n->pos.x, n->pos.z);
        if (!n->walking && dist_xz(n->pos, g_player.pos) < 5.0f && n->pose == ANIM_IDLE)
            n->yaw = angle_lerp(n->yaw, yaw_towards(n->pos, g_player.pos), clampf(4 * dt, 0, 1));
        n->anim_t += dt * (n->walking ? speed * 2.4f : 1.0f);
        if (n->fading) {
            n->fade -= dt * 0.4f;
            if (n->fade <= 0) n->active = false;
        }
        /* the player can't walk through people */
        float r = 0.75f;
        float px = g_player.pos.x - n->pos.x, pz = g_player.pos.z - n->pos.z;
        float p2 = px * px + pz * pz;
        if (p2 < r * r && p2 > 1e-5f) {
            float pd = sqrtf(p2);
            g_player.pos.x = n->pos.x + px / pd * r;
            g_player.pos.z = n->pos.z + pz / pd * r;
        }
    }
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static story_step_t step;
static float step_t, sub_t;
static int sub;
static int forest_kills, bully_kills, crystals_left;
static float fear, charge, charge_wobble;
static float white;            /* white-out overlay */
static float vignette;
static bool credits_ready;
static float gate_toast_cd;
static char obj_buf[96], banner_buf[48], sub_buf[64];

/* full-screen narration cards */
static const char *const *card_pages;
static int card_count, card_page;
static float card_chars, card_alpha;
static void (*card_done)(void);

static void show_card(const char *const *pages, int count, void (*done)(void))
{
    card_pages = pages;
    card_count = count;
    card_page = 0;
    card_chars = 0;
    card_done = done;
    g_player.locked = true;
}

static void set_step(story_step_t s)
{
    debugf("story step %d -> %d\n", (int)step, (int)s);
    step = s;
    step_t = 0;
    sub = 0;
    sub_t = 0;
    g_save.step = s;
}

int story_chapter(void)
{
    if (step <= ST_BOULDER) return 1;
    if (step <= ST_RETURN_ZENITH) return 2;
    if (step <= ST_FAREWELL) return 3;
    if (step <= ST_FOREST_DONE) return 4;
    return 5;
}

const char *story_chapter_name(void)
{
    static const char *const NAMES[] = { "", "A New Life", "Sylphiette", "Beyond the Door", "Fittoa Forest", "The Mana Calamity" };
    return NAMES[story_chapter()];
}

static void chapter_card(void)
{
    snprintf(banner_buf, sizeof(banner_buf), "Chapter %d", story_chapter());
    hud_banner(banner_buf, story_chapter_name(), 4.0f);
}

static void autosave(story_step_t s)
{
    story_step_t keep = step;
    g_save.step = s;
    if (save_write()) hud_toast("Progress saved.", 2.0f);
    g_save.step = keep;
}

static void learn_spell(int s, const char *button)
{
    g_save.spells |= 1 << s;
    music_play(MUS_FANFARE);
    snprintf(banner_buf, sizeof(banner_buf), "Learned %s!", SPELL_NAMES[s]);
    snprintf(sub_buf, sizeof(sub_buf), "%s to cast  -  hold to charge", button);
    hud_banner(banner_buf, s == SPELL_HEAL ? "C-Up to heal  -  hold for more" : sub_buf, 4.5f);
}

/* ------------------------------------------------------------------ */
/* Dialogue scripts                                                    */
/* ------------------------------------------------------------------ */

static const char *const INTRO[] = {
    "In my last life, I was a thirty-four-year-old shut-in. I hid from the world behind a locked door and let every chance pass me by.",
    "That life ended on a rainy afternoon, in front of a speeding truck.",
    "When I opened my eyes again, I was a baby in a world of swords and sorcery. My new name was Rudeus Greyrat.",
    "This time, I'll do it right. This time, I'm going to live with everything I've got.",
};

static const char *const YEARS_LATER[] = {
    "Five years passed.",
    "Rudeus left Buena to tutor a noble's daughter in the city of Roa, and returned home whenever he could.",
    "Then, one morning, a strange light appeared in the sky over Fittoa...",
};

static const char *const ENDING[] = {
    "And so, in a single flash of light, the people of Fittoa were scattered across the world.",
    "Far from home, Rudeus's long journey back across the Demon Continent was only beginning.",
    "To be continued...",
};

static const dline_t D_ZENITH_1[] = {
    { "Zenith", "Rudy! There you are. Guess what? Your father and I found you a magic tutor!" },
    { "Zenith", "You've been sneaking into the study to read that magic textbook every day, haven't you? Mommy noticed." },
    { "Rudeus", "(She noticed... I thought I was being careful.)" },
    { "Zenith", "Your tutor should be arriving at the south gate any minute. Go and greet her politely, okay?" },
};
static const dline_t D_ZENITH_IDLE[] = { { "Zenith", "Study hard, Rudy! Just... try not to blow a hole in the house again." } };
static const dline_t D_ZENITH_LATER[] = { { "Zenith", "Your father is so proud of you. He just has a funny way of showing it." } };

static const dline_t D_ZENITH_HEAL[] = {
    { "Zenith", "Oh! And who's this cute little one?" },
    { "Sylphie", "I-I'm Sylphie. Nice to meet you!" },
    { "Zenith", "Rudy, you made a friend! Mommy's so happy she could cry." },
    { "Zenith", "Then I have a present for you too. Before I married your father, I was the healer in his adventuring party." },
    { "Zenith", "Healing magic. Rest your hand on the wound, and wish for it to mend. Like this." },
};

static const dline_t D_PAUL_YARD[] = {
    { "Paul", "Hah! Rudy! Want to swing a sword with your old man? ...Magic, magic, always magic." },
    { "Paul", "Fine, fine. But a man should know how to fight up close, too. Press B to give that staff a swing!" },
};
static const dline_t D_PAUL_QUEST[] = {
    { "Paul", "Rudy. You've grown. Roxy says you're a Water Saint now. A Saint! At your age!" },
    { "Paul", "Monsters have been pouring out of Fittoa Forest. Wolves, boars... and something bigger." },
    { "Paul", "I'm the knight of this village, but I can't be everywhere at once. Will you help me thin them out?" },
    { "Rudeus", "Leave it to me, Father." },
    { "Paul", "That's my boy. The forest is past the west sign. Don't do anything reckless!" },
};
static const dline_t D_PAUL_WAIT[] = { { "Paul", "The forest is down the west road. If you get hurt, use that healing magic your mother taught you!" } };
static const dline_t D_PAUL_FOREST[] = {
    { "Paul", "Rudy! I heard the crash all the way from the road! Are you hurt?" },
    { "Paul", "...Wait. You took down THAT thing? By yourself?" },
    { "Rudeus", "Roxy taught me well. And I had a pretty good swordsman to learn from, too." },
    { "Paul", "Heh. Flattery, huh? You definitely get that from me." },
    { "Paul", "Come on, let's go home. Your mother's making stew tonight." },
};

static const dline_t D_LILIA_1[] = { { "Lilia", "Young master, please don't track mud into the house. I just finished the floors." } };
static const dline_t D_LILIA_2[] = { { "Lilia", "Young master... you've become quite the gentleman. Lady Zenith is very proud." } };
static const dline_t D_FARMER_1[] = { { "Farmer", "Morning, little Rudeus! The wheat's coming in nicely this year. Fittoa's the breadbasket of Asura, you know." } };
static const dline_t D_FARMER_2[] = { { "Farmer", "Heard you blasted that boulder to gravel. My old back thanks you!" } };
static const dline_t D_WIFE_1[] = { { "Villager", "The well water is so clear today. Buena is such a peaceful place." } };
static const dline_t D_WIFE_2[] = { { "Villager", "Rudeus! What's happening to the sky?! The children are terrified!" } };
static const dline_t D_ELDER[] = { { "Elder", "Buena may be small, but it's a good village. Look after it when you're grown, lad." } };

static const dline_t D_ROXY_MEET[] = {
    { "Roxy", "Excuse me. I'm looking for the Greyrat residence. I've been hired as a magic tutor." },
    { "Rudeus", "That's my house! I'm Rudeus Greyrat. Nice to meet you, Teacher." },
    { "Roxy", "...You're the student? You can't be older than five." },
    { "Roxy", "Ahem. My name is Roxy Migurdia. Age doesn't matter, as long as you're serious." },
    { "Roxy", "We'll start with the basics: Water Ball. Gather mana in your palm, give it a shape, and release it." },
    { "Roxy", "Normally you recite an incantation first. 'Gather, O water, at my hand...' and so on." },
};
static const dline_t D_ROXY_TARGETS[] = { { "Roxy", "There are three straw targets in your family's yard. Hit all of them with Water Ball. Use Z to lock on!" } };
static const dline_t D_ROXY_TARGETS_DONE[] = {
    { "Roxy", "Hold on. You didn't chant at all. Not a single word!" },
    { "Rudeus", "Is... that strange?" },
    { "Roxy", "Casting without an incantation is something even I can't do. You might be a genius, Rudy." },
    { "Roxy", "Then let's move quickly. Next is Fire Ball. The three braziers in the village square need lighting." },
};
static const dline_t D_ROXY_BRAZIERS_DONE[] = {
    { "Roxy", "Perfect control. The flames didn't even scorch the stone." },
    { "Roxy", "Earth magic next: Stone Cannon. Harden the stone, spin it, and fire it like an arrow." },
    { "Roxy", "A boulder rolled onto the north path to the hill. Clear it, and the villagers will thank you." },
};
static const dline_t D_ROXY_BOULDER_DONE[] = {
    { "Roxy", "Splendid. You learn faster than anyone I've ever taught, Rudy." },
    { "Roxy", "That's enough for today. Why not take a walk? The view from the great tree on the hill is lovely." },
};
static const dline_t D_ROXY_TIP[] = {
    { "Roxy", "Remember: tap a C button for a quick spell, or hold it to gather more mana for a stronger one." },
    { "Roxy", "Hold Z to lock on to a target. Your spells will curve toward it." },
};
static const dline_t D_ROXY_IDLE[] = { { "Roxy", "Rudy, you're doing wonderfully. Have you been practicing with Sylphie?" } };
static const dline_t D_ROXY_EXAM[] = {
    { "Roxy", "Rudy. It's time for your final exam. If you pass, you'll be a Water Saint." },
    { "Roxy", "We'll hold it on the plateau outside the village, where nobody can get hurt." },
    { "Roxy", "Shall we go? Just follow the road out through the gate." },
};
static const dline_t D_FEAR_START[] = {
    { "Rudeus", "(...Outside. Past the gate.)" },
    { "Rudeus", "(My legs won't move. It's just like back then, standing in front of that door.)" },
    { "Rudeus", "(The voices outside. The laughing. I never stepped through that door again...)" },
    { "Roxy", "...Rudy? Your hand is shaking. It's alright. I'm right here with you." },
};
static const dline_t D_FEAR_DONE[] = {
    { "Rudeus", "(...I did it. I'm outside.)" },
    { "Rudeus", "(The sky is so big. Why was I ever so afraid of it?)" },
    { "Roxy", "Well done. Let's go, Rudy." },
};
static const dline_t D_PLATEAU[] = {
    { "Roxy", "This is the place. Your exam is the Saint-tier water spell: Cumulonimbus." },
    { "Roxy", "It calls down a thunderstorm over an entire region. It takes an enormous amount of mana." },
    { "Roxy", "Hold R to gather mana. When the storm is at its peak, release! Pour in too much, and it will scatter." },
};
static const dline_t D_CUMULO_DONE[] = {
    { "Roxy", "Unbelievable... A full storm, and you're not even out of breath." },
    { "Roxy", "Rudeus Greyrat. As of today, you are a Water Saint. There's nothing left for me to teach you." },
    { "Rudeus", "Teacher..." },
    { "Roxy", "Then it's my turn to graduate, too. I'm going to travel and train again. You showed me how much I still have to learn." },
    { "Roxy", "Thank you, Rudy. Teaching you was the happiest time I've had in a very long while." },
    { "Rudeus", "(Roxy pulled me out of that room. I'll never forget it. Not in this life.)" },
};

static const dline_t D_HILL[] = {
    { "Somal", "Go home, green-hair! Green hair means demon blood!" },
    { "Sylphie", "I-I'm not a demon... Please, stop it..." },
    { "Rudeus", "(Three kids ganging up on one little girl. Not on my watch.)" },
    { "Rudeus", "Hey! Three against one? Leave her alone!" },
    { "Somal", "Huh? It's the Greyrat kid! Get him!" },
};
static const dline_t D_SYLPHIE[] = {
    { "Sylphie", "Th-thank you... You didn't have to do that." },
    { "Sylphie", "Everyone says my hair makes me a demon. Aren't you scared of me?" },
    { "Rudeus", "Why would I be? It's a pretty color. Like new leaves in spring." },
    { "Sylphie", "P-pretty...?" },
    { "Rudeus", "I'm Rudeus. Call me Rudy." },
    { "Sylphie", "I'm Sylphiette... Then you can call me Sylphie!" },
    { "Sylphie", "Rudy... will you teach me magic someday? I want to be strong too." },
    { "Rudeus", "Sure. Let's practice together every day." },
};
static const dline_t D_SYLPHIE_IDLE[] = { { "Sylphie", "Rudy! Watch! I can make a tiny Water Ball now! ...Well, almost." } };

static const dline_t D_BOSS_BOAR[] = {
    { "Rudeus", "(That should be the last of them...)" },
    { "Rudeus", "(...The ground is shaking!?)" },
};
static const dline_t D_CALAMITY[] = {
    { "Sylphie", "Rudy! You came back! Look at the sky... it's been glowing all morning!" },
    { "Sylphie", "Crystals started falling all over the village. They give off strange mana, and monsters keep crawling out of them!" },
    { "Rudeus", "Get everyone inside, Sylphie. I'll destroy the crystals." },
    { "Sylphie", "Be careful, Rudy! Please!" },
};
static const dline_t D_SYLPHIE_SHELTER[] = { { "Sylphie", "Everyone's inside. Rudy, the crystals...!" } };
static const dline_t D_CORE_INTRO[] = { { "Rudeus", "(The crystals are gone, but the light... it's coming down!)" } };
static const dline_t D_CORE_DOWN[] = {
    { "Rudeus", "(It's breaking apart... no, it's growing! The light is swallowing everything!)" },
    { "Rudeus", "(Sylphie...! Father! Mother...!)" },
};
static const dline_t D_DEMON_WAKE[] = {
    { "Rudeus", "(Where... am I? The ground is red, and the air smells like iron.)" },
    { "Rudeus", "(This isn't Fittoa. This isn't anywhere I know.)" },
};
static const dline_t D_RUIJERD[] = {
    { "Ruijerd", "You're awake. Good. You were lying out here, alone." },
    { "Rudeus", "(That red jewel on his forehead... a Superd!? The demon race from the old stories...)" },
    { "Ruijerd", "...You're afraid of me. That's fine. Most people are." },
    { "Ruijerd", "I am Ruijerd Superdia. I will not harm a child. A warrior protects children." },
    { "Ruijerd", "This is the Demon Continent. Your home must be very far away." },
    { "Rudeus", "Then... will you help me get back?" },
    { "Ruijerd", "I will. No matter how far it is." },
};

static const dline_t D_SIGN_GATE[] = { { NULL, "BUENA VILLAGE  -  Fittoa Region, Asura Kingdom." } };
static const dline_t D_SIGN_WEST[] = { { NULL, "WEST: Fittoa Forest. Beware of monsters." } };
static const dline_t D_SIGN_FOREST[] = { { NULL, "EAST: Buena Village." } };

/* ------------------------------------------------------------------ */
/* Scene setup                                                         */
/* ------------------------------------------------------------------ */

static const float TARGET_POS[3][2] = { { -24, 17 }, { -19, 20 }, { -14, 17 } };
static const float FOREST_SPAWNS[8][3] = {
    { -10, -20, EN_WOLF }, { -24, 6, EN_WOLF }, { 18, -18, EN_WOLF }, { -4, 24, EN_WOLF },
    { 14, 20, EN_WOLF }, { -22, -26, EN_BOAR }, { -30, 22, EN_BOAR }, { 10, -30, EN_BOAR },
};
static const float CRYSTAL_POS[4][2] = { { -22, -10 }, { 22, -6 }, { -20, 28 }, { 18, 30 } };

static void village_npcs(void)
{
    bool calamity = step >= ST_CALAMITY;
    if (!calamity) {
        npc_place(NPC_ZENITH, MDL_ZENITH, -12.2f, 7.6f, PI_F / 2);
        npc_place(NPC_LILIA, MDL_LILIA, -12.5f, 11.0f, PI_F / 2);
        npc_place(NPC_FARMER, MDL_VILLAGER_M, 30, 0, -PI_F / 2);
        npc_place(NPC_WIFE, MDL_VILLAGER_F, 3.5f, 10.0f, 0);
        npc_place(NPC_ELDER, MDL_VILLAGER_M, -5, 27, PI_F / 2);
        if (step >= ST_TALK_PAUL) {
            npc_place(NPC_PAUL, MDL_PAUL, -36, 3, PI_F / 2);
        } else {
            npc_place(NPC_PAUL, MDL_PAUL, -29, 12, PI_F / 2);
            npcs[NPC_PAUL].pose = ANIM_SWING;
        }
    } else {
        npc_place(NPC_WIFE, MDL_VILLAGER_F, 3.5f, 10.0f, 0);
    }

    switch (step) {
    case ST_MEET_ROXY:
        npc_place(NPC_ROXY, MDL_ROXY, 0, 38, PI_F);
        break;
    case ST_TARGETS:
        npc_place(NPC_ROXY, MDL_ROXY, -19, 23.5f, PI_F);
        break;
    case ST_BRAZIERS:
        npc_place(NPC_ROXY, MDL_ROXY, 4, 14, PI_F);
        break;
    case ST_BOULDER:
        npc_place(NPC_ROXY, MDL_ROXY, 4, -16, PI_F);
        break;
    case ST_GO_HILL: case ST_BULLIES: case ST_TALK_SYLPHIE:
        npc_place(NPC_SYLPHIE, MDL_SYLPHIE, 0.5f, -35.5f, 0);
        if (step == ST_GO_HILL) {
            static const float B[3][3] = { { -2.4f, -33.0f, 2.5f }, { 2.8f, -33.5f, -2.4f }, { 0.3f, -31.6f, PI_F } };
            for (int i = 0; i < 3; i++) {
                npc_place(NPC_BULLY1 + i, MDL_BULLY_A + i, B[i][0], B[i][1], B[i][2]);
                npcs[NPC_BULLY1 + i].pose = ANIM_SWING;
                npcs[NPC_BULLY1 + i].talkable = false;
            }
        }
        break;
    case ST_RETURN_ZENITH:
        npc_place(NPC_SYLPHIE, MDL_SYLPHIE, g_player.pos.x + 1.5f, g_player.pos.z + 1.5f, 0);
        npcs[NPC_SYLPHIE].follow = true;
        break;
    case ST_ROXY_EXAM: case ST_FEAR:
        npc_place(NPC_ROXY, MDL_ROXY, -1.6f, 37.5f, PI_F);
        npc_place(NPC_SYLPHIE, MDL_SYLPHIE, -8.5f, 13.0f, PI_F / 2);
        break;
    case ST_TALK_PAUL: case ST_FOREST: case ST_BOSS_BOAR: case ST_FOREST_DONE:
        npc_place(NPC_SYLPHIE, MDL_SYLPHIE, -8.5f, 13.0f, PI_F / 2);
        break;
    case ST_CALAMITY: case ST_CRYSTALS: case ST_CORE:
        npc_place(NPC_SYLPHIE, MDL_SYLPHIE_OLDER, 3, 13, PI_F);
        break;
    default:
        break;
    }
}

static void village_enemies(void)
{
    switch (step) {
    case ST_TARGETS:
        for (int i = 0; i < 3; i++) enemy_spawn(EN_TARGET, TARGET_POS[i][0], TARGET_POS[i][1], 0);
        break;
    case ST_BULLIES:
        for (int i = bully_kills; i < 3; i++) enemy_spawn(EN_BULLY, -2 + i * 2.0f, -32.0f, (uint8_t)i);
        break;
    case ST_CRYSTALS:
        for (int i = 0; i < crystals_left; i++) enemy_spawn(EN_CRYSTAL, CRYSTAL_POS[i][0], CRYSTAL_POS[i][1], 0);
        enemy_spawn(EN_WISP, -8, -2, 1);
        enemy_spawn(EN_WISP, 8, 20, 3);
        break;
    case ST_CORE:
        enemy_spawn(EN_CORE, 0, -8, 0);
        break;
    default:
        break;
    }
}

static map_id_t pending_map;
static float pending_x, pending_z, pending_yaw;

static void setup_scene(void)
{
    memset(npcs, 0, sizeof(npcs));
    enemies_clear();
    projectiles_clear();
    particles_clear();
    g_player.older = step >= ST_CALAMITY;

    switch (g_world.id) {
    case MAP_VILLAGE: {
        prop_t *b = world_find_prop(PROP_BOULDER, 0);
        if (b && step > ST_BOULDER) b->flags |= PF_BROKEN;
        if (step > ST_BRAZIERS && step < ST_CALAMITY)
            for (int i = 0; i < 3; i++) { prop_t *p = world_find_prop(PROP_BRAZIER, i); if (p) p->flags |= PF_LIT; }
        village_npcs();
        village_enemies();
        if (step == ST_BULLIES || step == ST_CRYSTALS) music_play(MUS_BATTLE);
        else if (step == ST_CORE) music_play(MUS_BOSS);
        else if (step >= ST_TARGETS && step <= ST_BOULDER) music_play(MUS_LESSON);
        else if (step == ST_INTRO) music_play(MUS_SORROW);
        else if (step >= ST_CALAMITY) music_play(MUS_BATTLE);
        else music_play(MUS_VILLAGE);
        break;
    }
    case MAP_FOREST:
        if (step == ST_FOREST) {
            for (int i = forest_kills; i < 8; i++)
                enemy_spawn((enemy_type_t)FOREST_SPAWNS[i][2], FOREST_SPAWNS[i][0], FOREST_SPAWNS[i][1], 0);
            music_play(MUS_BATTLE);
        } else if (step == ST_BOSS_BOAR) {
            enemy_spawn(EN_GREATBOAR, 0, -8, 0);
            music_play(MUS_BOSS);
        } else {
            music_play(MUS_VILLAGE);
        }
        break;
    case MAP_PLATEAU:
        npc_place(NPC_ROXY, MDL_ROXY, g_player.pos.x - 1.5f, g_player.pos.z - 1.0f, PI_F);
        npc_walk(NPC_ROXY, -2.5f, 3.0f);
        music_play(MUS_LESSON);
        break;
    case MAP_DEMON:
        npc_place(NPC_RUIJERD, MDL_RUIJERD, 1.5f, -5.0f, 0);
        music_play(MUS_SORROW);
        break;
    default:
        break;
    }
}

static void enter_map(map_id_t m, float x, float z, float yaw)
{
    bool calamity = step >= ST_CALAMITY && step <= ST_CORE;
    bool changed = m != g_world.id;
    world_set_palette_calamity(calamity);
    world_load(m);
    player_reset(v3(x, 0, z), yaw);
    camera_snap();
    g_cam.override = false;
    setup_scene();
    if (changed || calamity) hud_banner(g_world.name, NULL, 2.5f);
}

static void do_enter_pending(void)
{
    enter_map(pending_map, pending_x, pending_z, pending_yaw);
}

static void travel(map_id_t m, float x, float z, float yaw)
{
    pending_map = m; pending_x = x; pending_z = z; pending_yaw = yaw;
    g_player.locked = true;
    game_fade_to(do_enter_pending);
}

static void default_entry(float *x, float *z, float *yaw, map_id_t *m)
{
    *m = MAP_VILLAGE; *x = 0; *z = 14; *yaw = PI_F;
    switch (step) {
    case ST_INTRO: case ST_TALK_ZENITH: *x = -9.0f; *z = 4.0f; *yaw = -PI_F / 2; break;
    case ST_GO_HILL: *x = 1.5f; *z = -14; *yaw = PI_F; break;
    case ST_ROXY_EXAM: case ST_FEAR: *x = 0; *z = 30; *yaw = 0; break;
    case ST_CUMULONIMBUS: case ST_FAREWELL: *m = MAP_PLATEAU; *x = 0; *z = 38; *yaw = PI_F; break;
    case ST_TALK_PAUL: *x = 0; *z = 34; *yaw = PI_F; break;
    case ST_FOREST: case ST_BOSS_BOAR: case ST_FOREST_DONE: *m = MAP_FOREST; *x = 40; *z = 0; *yaw = -PI_F / 2; break;
    case ST_CALAMITY: *x = 0; *z = 30; *yaw = PI_F; break;
    case ST_DEMON_WAKE: case ST_RUIJERD: case ST_THE_END: *m = MAP_DEMON; *x = 0; *z = 22; *yaw = PI_F; break;
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* Step transitions (dialogue callbacks)                               */
/* ------------------------------------------------------------------ */

static void after_intro(void);
static void begin_chapter_1(void)
{
    g_cam.override = false;
    g_player.locked = false;
    set_step(ST_TALK_ZENITH);
    enter_map(MAP_VILLAGE, -9.0f, 4.0f, -PI_F / 2);
    chapter_card();
}
static void after_intro(void) { game_fade_to(begin_chapter_1); }

static void after_zenith_1(void)
{
    set_step(ST_MEET_ROXY);
    npc_place(NPC_ROXY, MDL_ROXY, 0, 38, PI_F);
}

static void after_roxy_targets_tip(void)
{
    set_step(ST_TARGETS);
    npc_walk(NPC_ROXY, -19, 23.5f);
    for (int i = 0; i < 3; i++) enemy_spawn(EN_TARGET, TARGET_POS[i][0], TARGET_POS[i][1], 0);
    music_play(MUS_LESSON);
}
static void after_roxy_meet(void)
{
    learn_spell(SPELL_WATER, "C-Left");
    TALK(D_ROXY_TARGETS, after_roxy_targets_tip);
}

static void after_targets(void)
{
    learn_spell(SPELL_FIRE, "C-Down");
    set_step(ST_BRAZIERS);
    npc_walk(NPC_ROXY, 4, 14);
}

static void after_braziers(void)
{
    learn_spell(SPELL_STONE, "C-Right");
    set_step(ST_BOULDER);
    npc_walk(NPC_ROXY, 4, -16);
}

static void after_boulder(void)
{
    autosave(ST_GO_HILL);
    set_step(ST_GO_HILL);
    npcs[NPC_ROXY].fading = true;
    npc_walk(NPC_ROXY, -12, -6);
    npc_place(NPC_SYLPHIE, MDL_SYLPHIE, 0.5f, -35.5f, 0);
    static const float B[3][3] = { { -2.4f, -33.0f, 2.5f }, { 2.8f, -33.5f, -2.4f }, { 0.3f, -31.6f, PI_F } };
    for (int i = 0; i < 3; i++) {
        npc_place(NPC_BULLY1 + i, MDL_BULLY_A + i, B[i][0], B[i][1], B[i][2]);
        npcs[NPC_BULLY1 + i].pose = ANIM_SWING;
        npcs[NPC_BULLY1 + i].talkable = false;
    }
    music_play(MUS_VILLAGE);
    chapter_card();
}

static void after_hill(void)
{
    set_step(ST_BULLIES);
    bully_kills = 0;
    for (int i = 0; i < 3; i++) {
        npc_t *b = &npcs[NPC_BULLY1 + i];
        enemy_spawn(EN_BULLY, b->pos.x, b->pos.z, (uint8_t)i);
        b->active = false;
    }
    music_play(MUS_BATTLE);
}

static void after_sylphie(void)
{
    set_step(ST_RETURN_ZENITH);
    npcs[NPC_SYLPHIE].follow = true;
}

static void after_zenith_heal(void)
{
    learn_spell(SPELL_HEAL, "C-Up");
    autosave(ST_ROXY_EXAM);
    set_step(ST_ROXY_EXAM);
    npcs[NPC_SYLPHIE].follow = false;
    npc_walk(NPC_SYLPHIE, -8.5f, 13.0f);
    npc_place(NPC_ROXY, MDL_ROXY, -1.6f, 37.5f, PI_F);
    chapter_card();
}

static void after_roxy_exam(void) { set_step(ST_FEAR); }

static void after_fear_start(void) { sub = 1; sub_t = 0; fear = 0; }

static void go_plateau(void)
{
    set_step(ST_CUMULONIMBUS);
    enter_map(MAP_PLATEAU, 0, 38, PI_F);
    g_player.locked = false;
}
static void after_fear_done(void) { game_fade_to(go_plateau); }

static void after_plateau(void) { sub = 1; sub_t = 0; charge = 0; }

static void after_cumulo(void)
{
    set_step(ST_FAREWELL);
    npc_walk(NPC_ROXY, -6, 46);
    npcs[NPC_ROXY].fading = true;
    music_play(MUS_SORROW);
}

static void begin_chapter_4(void)
{
    set_step(ST_TALK_PAUL);
    enter_map(MAP_VILLAGE, 0, 34, PI_F);
    g_player.locked = false;
    chapter_card();
}

static void after_paul_quest(void)
{
    set_step(ST_FOREST);
    forest_kills = 0;
}

static void after_boss_boar_intro(void)
{
    set_step(ST_BOSS_BOAR);
    int b = enemy_spawn(EN_GREATBOAR, 0, -8, 0);
    if (b >= 0) particles_burst(g_enemies[b].pos, 0xA89070FF, 30, 6.0f, 0.8f, 1.2f, 4.0f);
    g_cam.shake = 1.0f;
    music_play(MUS_BOSS);
    hud_banner("Great Boar", "Lord of Fittoa Forest", 3.0f);
}

static void begin_chapter_5(void)
{
    set_step(ST_CALAMITY);
    crystals_left = 4;
    enter_map(MAP_VILLAGE, 0, 30, PI_F);
    g_player.locked = false;
    chapter_card();
}
static void after_years_later(void) { game_fade_to(begin_chapter_5); }
static void show_years_later(void)
{
    enemies_clear();
    show_card(YEARS_LATER, COUNT(YEARS_LATER), after_years_later);
}

static void after_paul_forest(void)
{
    autosave(ST_CALAMITY);
    set_step(ST_FOREST_DONE);
    sub = 1;
    game_fade_to(show_years_later);
}

static void after_calamity(void)
{
    set_step(ST_CRYSTALS);
    crystals_left = 4;
    npc_walk(NPC_SYLPHIE, 10, 22);
    npcs[NPC_SYLPHIE].fading = true;
    for (int i = 0; i < 4; i++) enemy_spawn(EN_CRYSTAL, CRYSTAL_POS[i][0], CRYSTAL_POS[i][1], 0);
}

static void after_core_intro(void)
{
    set_step(ST_CORE);
    int c = enemy_spawn(EN_CORE, 0, -8, 0);
    if (c >= 0) particles_burst(g_enemies[c].pos, 0xFFB0F0FF, 40, 8.0f, 0.6f, 1.4f, 0);
    music_play(MUS_BOSS);
    hud_banner("Mana Core", "Heart of the Calamity", 3.0f);
}

static void after_core_down(void) { sub = 2; sub_t = 0; }

static void after_ending_card(void) { credits_ready = true; }
static void show_ending(void)
{
    g_save.cleared = 1;
    autosave(ST_CALAMITY);
    show_card(ENDING, COUNT(ENDING), after_ending_card);
}
static void after_ruijerd(void)
{
    set_step(ST_THE_END);
    music_play(MUS_TITLE);
    game_fade_to(show_ending);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void story_init(void)
{
    memset(npcs, 0, sizeof(npcs));
}

void story_new_game(void)
{
    save_reset();
    forest_kills = bully_kills = 0;
    crystals_left = 4;
    credits_ready = false;
    white = 0;
    g_player.hp = 0;
    g_player.mp = 0;
    set_step(ST_INTRO);
    enter_map(MAP_VILLAGE, -7.5f, 5.5f, -PI_F / 2);
    g_player.locked = true;
    g_cam.override = true;
    show_card(INTRO, COUNT(INTRO), after_intro);
}

void story_start_from_save(void)
{
    if (!save_load()) { story_new_game(); return; }
    forest_kills = bully_kills = 0;
    crystals_left = 4;
    credits_ready = false;
    white = 0;
    g_player.hp = 0;
    g_player.mp = 0;
    step = (story_step_t)g_save.step;
    set_step(step);
    float x, z, yaw;
    map_id_t m;
    default_entry(&x, &z, &yaw, &m);
    enter_map(m, x, z, yaw);
    chapter_card();
}

void story_debug_start(int s)
{
    save_reset();
    g_save.step = (uint8_t)s;
    g_save.spells = s >= ST_RETURN_ZENITH ? 0xF : (s >= ST_BOULDER ? 0x7 : (s >= ST_BRAZIERS ? 0x3 : (s >= ST_TARGETS ? 0x1 : 0)));
    forest_kills = bully_kills = 0;
    crystals_left = 4;
    credits_ready = false;
    white = 0;
    g_player.hp = 0;
    g_player.mp = 0;
    if (s == ST_INTRO) { story_new_game(); return; }
    set_step((story_step_t)s);
    float x, z, yaw;
    map_id_t m;
    default_entry(&x, &z, &yaw, &m);
    enter_map(m, x, z, yaw);
    chapter_card();
}

void story_respawn(void)
{
    float x, z, yaw;
    map_id_t m;
    if (step == ST_FEAR) sub = 0;
    if (step == ST_CUMULONIMBUS) sub = 0;
    if (step == ST_CORE) sub = 0;
    default_entry(&x, &z, &yaw, &m);
    g_player.hp = g_player.max_hp;
    g_player.mp = g_player.max_mp;
    white = 0;
    enter_map(m, x, z, yaw);
}

bool story_wants_credits(void) { return credits_ready; }

void story_on_player_dead(void)
{
    g_player.locked = true;
}

void story_on_enemy_killed(int type)
{
    switch (type) {
    case EN_TARGET:
        if (step == ST_TARGETS && enemies_alive(EN_TARGET) == 0) { sub = 1; sub_t = 0; }
        break;
    case EN_BULLY:
        if (step == ST_BULLIES) bully_kills++;
        break;
    case EN_WOLF: case EN_BOAR:
        if (step == ST_FOREST) forest_kills++;
        break;
    case EN_GREATBOAR:
        if (step == ST_BOSS_BOAR) { sub = 1; sub_t = 0; music_play(MUS_VILLAGE); }
        break;
    case EN_CRYSTAL:
        if (step == ST_CRYSTALS && crystals_left > 0) crystals_left--;
        break;
    case EN_CORE:
        if (step == ST_CORE) { sub = 1; sub_t = 0; music_play(MUS_NONE); }
        break;
    }
}

const char *story_objective(void)
{
    switch (step) {
    case ST_TALK_ZENITH:   return "Talk to Mother in front of the house.";
    case ST_MEET_ROXY:     return "Greet your new tutor at the south gate.";
    case ST_TARGETS:       return "Hit the 3 straw targets in the yard with Water Ball (C-Left).";
    case ST_BRAZIERS:      return "Light the 3 braziers in the square with Fire Ball (C-Down).";
    case ST_BOULDER:       return "Break the boulder on the north path with Stone Cannon (C-Right).";
    case ST_GO_HILL:       return "Visit the great tree on the northern hill.";
    case ST_BULLIES:       return "Drive off the bullies!";
    case ST_TALK_SYLPHIE:  return "Talk to the green-haired girl.";
    case ST_RETURN_ZENITH: return "Go home and introduce your new friend to Mother.";
    case ST_ROXY_EXAM:     return "Roxy is waiting at the south gate.";
    case ST_FEAR:          return sub == 0 ? "Step through the south gate." : "";
    case ST_CUMULONIMBUS:  return sub == 0 ? "Climb to the top of the plateau." : "";
    case ST_TALK_PAUL:     return "Father is waiting on the west road.";
    case ST_FOREST:
        snprintf(obj_buf, sizeof(obj_buf), "Clear the monsters in Fittoa Forest (%d/8).", forest_kills);
        return obj_buf;
    case ST_BOSS_BOAR:     return "Defeat the Great Boar!";
    case ST_CALAMITY:      return "Find Sylphie in the village square.";
    case ST_CRYSTALS:
        snprintf(obj_buf, sizeof(obj_buf), "Destroy the mana crystals (%d left).", crystals_left);
        return obj_buf;
    case ST_CORE:          return sub == 0 ? "Destroy the Mana Core!" : "";
    case ST_DEMON_WAKE:    return "Look around.";
    case ST_RUIJERD:       return "Talk to the stranger.";
    default:               return "";
    }
}

/* ------------------------------------------------------------------ */
/* Interaction                                                         */
/* ------------------------------------------------------------------ */

static int find_npc(void)
{
    int best = -1;
    float bd = 2.8f;
    vec3_t f = v3(sinf(g_player.yaw), 0, cosf(g_player.yaw));
    for (int i = 0; i < NPC_COUNT; i++) {
        npc_t *n = &npcs[i];
        if (!n->active || !n->talkable || n->fading) continue;
        vec3_t d = v3_sub(n->pos, g_player.pos);
        d.y = 0;
        float dist = v3_len(d);
        if (dist > bd) continue;
        if (dist > 0.5f && v3_dot(v3_scale(d, 1 / dist), f) < -0.1f) continue;
        bd = dist;
        best = i;
    }
    return best;
}

static prop_t *find_sign(void)
{
    for (int i = 0; ; i++) {
        prop_t *p = world_find_prop(PROP_SIGN, i);
        if (!p) return NULL;
        if (dist_xz(v3(p->x, 0, p->z), g_player.pos) < 2.2f) return p;
    }
}

static void talk(int id)
{
    npc_t *n = &npcs[id];
    n->yaw = yaw_towards(n->pos, g_player.pos);
    g_player.yaw = yaw_towards(g_player.pos, n->pos);
    switch (id) {
    case NPC_ZENITH:
        if (step == ST_TALK_ZENITH) TALK(D_ZENITH_1, after_zenith_1);
        else if (step == ST_RETURN_ZENITH) TALK(D_ZENITH_HEAL, after_zenith_heal);
        else if (step < ST_RETURN_ZENITH) TALK(D_ZENITH_IDLE, NULL);
        else TALK(D_ZENITH_LATER, NULL);
        break;
    case NPC_PAUL:
        if (step == ST_TALK_PAUL) TALK(D_PAUL_QUEST, after_paul_quest);
        else if (step == ST_FOREST_DONE && g_world.id == MAP_FOREST) TALK(D_PAUL_FOREST, after_paul_forest);
        else if (step >= ST_FOREST) TALK(D_PAUL_WAIT, NULL);
        else TALK(D_PAUL_YARD, NULL);
        break;
    case NPC_LILIA:
        if (step < ST_GO_HILL) TALK(D_LILIA_1, NULL); else TALK(D_LILIA_2, NULL);
        break;
    case NPC_FARMER:
        if (step <= ST_BOULDER) TALK(D_FARMER_1, NULL); else TALK(D_FARMER_2, NULL);
        break;
    case NPC_WIFE:
        if (step >= ST_CALAMITY) TALK(D_WIFE_2, NULL); else TALK(D_WIFE_1, NULL);
        break;
    case NPC_ELDER:
        TALK(D_ELDER, NULL);
        break;
    case NPC_ROXY:
        if (step == ST_MEET_ROXY) TALK(D_ROXY_MEET, after_roxy_meet);
        else if (step == ST_ROXY_EXAM) TALK(D_ROXY_EXAM, after_roxy_exam);
        else if (step >= ST_TARGETS && step <= ST_BOULDER) TALK(D_ROXY_TIP, NULL);
        else TALK(D_ROXY_IDLE, NULL);
        break;
    case NPC_SYLPHIE:
        if (step == ST_TALK_SYLPHIE) TALK(D_SYLPHIE, after_sylphie);
        else if (step == ST_CALAMITY) TALK(D_CALAMITY, after_calamity);
        else if (step >= ST_CALAMITY) TALK(D_SYLPHIE_SHELTER, NULL);
        else TALK(D_SYLPHIE_IDLE, NULL);
        break;
    case NPC_RUIJERD:
        TALK(D_RUIJERD, after_ruijerd);
        break;
    }
}

bool story_try_interact(void)
{
    if (dialog_active() || g_player.locked) return false;
    int id = find_npc();
    if (id >= 0) { talk(id); return true; }
    prop_t *s = find_sign();
    if (s) {
        if (g_world.id == MAP_FOREST) TALK(D_SIGN_FOREST, NULL);
        else if (s->x < -20) TALK(D_SIGN_WEST, NULL);
        else TALK(D_SIGN_GATE, NULL);
        return true;
    }
    return false;
}

const char *story_interact_hint(void)
{
    if (find_npc() >= 0) return "Talk";
    if (find_sign()) return "Read";
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Per-frame logic                                                     */
/* ------------------------------------------------------------------ */

static void update_card(float dt)
{
    if (!card_pages) return;
    card_alpha = fminf(1, card_alpha + dt * 2);
    card_chars += dt * 40;
    int len = (int)strlen(card_pages[card_page]);
    if (g_frame.pressed.a || g_frame.pressed.start) {
        if ((int)card_chars < len) card_chars = (float)len;
        else {
            sfx_play(SFX_CURSOR);
            card_page++;
            card_chars = 0;
            if (card_page >= card_count) {
                card_pages = NULL;
                card_alpha = 0;
                if (card_done) card_done();
            }
        }
    }
}

static void check_exits(void)
{
    gate_toast_cd = fmaxf(0, gate_toast_cd - g_frame.dt);
    vec3_t *p = &g_player.pos;
    if (g_world.id == MAP_VILLAGE) {
        if (p->z > 39.6f && fabsf(p->x) < 3.4f) {
            if (step == ST_FEAR && sub == 0) {
                g_player.locked = true;
                g_player.vel = v3(0, 0, 0);
                p->z = 40.0f;
                g_player.yaw = 0;
                npc_walk(NPC_ROXY, -1.2f, 41.0f);
                TALK(D_FEAR_START, after_fear_start);
            } else if (step != ST_FEAR) {
                p->z = 39.5f;
                if (gate_toast_cd <= 0) {
                    hud_toast(step < ST_FEAR ? "Rudeus doesn't feel ready to leave the village..." : "No need to leave the village right now.", 2.0f);
                    gate_toast_cd = 2.5f;
                }
            }
        }
        if (p->x < -44.0f && fabsf(p->z - 2) < 8) {
            if (step >= ST_FOREST && step <= ST_BOSS_BOAR) {
                travel(MAP_FOREST, 41, 0.5f, -PI_F / 2);
            } else {
                p->x = -43.9f;
                if (gate_toast_cd <= 0) {
                    hud_toast("Father says the forest is too dangerous for now.", 2.0f);
                    gate_toast_cd = 2.5f;
                }
            }
        }
    } else if (g_world.id == MAP_FOREST) {
        if (p->x > 44.5f && step != ST_BOSS_BOAR && step != ST_FOREST_DONE) travel(MAP_VILLAGE, -41, 2.5f, PI_F / 2);
        else if (p->x > 44.5f) p->x = 44.4f;
    }
}

void story_update(float dt)
{
    step_t += dt;
    sub_t += dt;
    update_npcs(dt);
    update_card(dt);
    vignette = approachf(vignette, (step == ST_FEAR && sub == 1) ? 1.0f : 0.0f, dt * 2);
    if (white > 0 && !(step == ST_CORE && sub == 2)) white = fmaxf(0, white - dt * 0.5f);
    if (dialog_active() || card_pages || game_fading()) return;

    switch (step) {
    case ST_INTRO: {
        /* slow pan over the village behind the narration */
        float a = step_t * 0.05f;
        g_cam.override = true;
        g_cam.ov_target = v3(-6, 3, 4);
        g_cam.ov_pos = v3(-6 + cosf(a) * 26, 12, 4 + sinf(a) * 26);
        break;
    }
    case ST_MEET_ROXY:
        if (dist_xz(g_player.pos, npcs[NPC_ROXY].pos) < 3.0f && step_t > 0.5f) talk(NPC_ROXY);
        break;
    case ST_TARGETS:
        if (sub == 1 && sub_t > 0.8f) { sub = 2; TALK(D_ROXY_TARGETS_DONE, after_targets); }
        break;
    case ST_BRAZIERS:
        if (world_count_props(PROP_BRAZIER, PF_LIT, PF_LIT) >= 3) {
            if (sub == 0) { sub = 1; sub_t = 0; }
            else if (sub_t > 1.0f) TALK(D_ROXY_BRAZIERS_DONE, after_braziers);
        }
        break;
    case ST_BOULDER: {
        prop_t *b = world_find_prop(PROP_BOULDER, 0);
        if (b && (b->flags & PF_BROKEN)) {
            if (sub == 0) { sub = 1; sub_t = 0; }
            else if (sub_t > 1.2f) {
                hud_banner("Chapter 1 Complete", NULL, 2.5f);
                music_play(MUS_FANFARE);
                TALK(D_ROXY_BOULDER_DONE, after_boulder);
            }
        }
        break;
    }
    case ST_GO_HILL:
        if (dist_xz(g_player.pos, v3(0, 0, -35)) < 12.5f) {
            g_player.yaw = yaw_towards(g_player.pos, npcs[NPC_SYLPHIE].pos);
            TALK(D_HILL, after_hill);
        }
        break;
    case ST_BULLIES:
        if (bully_kills >= 3 && enemies_alive(-1) == 0) {
            if (sub == 0) { sub = 1; sub_t = 0; }
            else if (sub_t > 2.0f) { set_step(ST_TALK_SYLPHIE); music_play(MUS_VILLAGE); }
        }
        break;
    case ST_FEAR:
        if (sub == 1) {
            if (g_frame.pressed.a) {
                fear += 0.085f;
                g_player.pos.z += 0.12f;
                g_player.anim_t += 1.2f;
                sfx_play(SFX_STEP);
                game_rumble(0.05f);
            }
            fear = fmaxf(0, fear - dt * 0.14f);
            if (fear >= 1.0f) { sub = 2; TALK(D_FEAR_DONE, after_fear_done); }
        }
        break;
    case ST_CUMULONIMBUS:
        if (sub == 0) {
            if (dist_xz(g_player.pos, v3(0, 0, 0)) < 7.5f) {
                g_player.yaw = yaw_towards(g_player.pos, npcs[NPC_ROXY].pos);
                TALK(D_PLATEAU, after_plateau);
            }
        } else if (sub == 1) {
            g_player.locked = true;
            g_player.charge_spell = -1;
            float value = charge + charge_wobble;
            if (g_frame.held.r) {
                charge += dt * 0.36f;
                charge_wobble = sinf(sub_t * 7.0f) * 0.045f * charge;
                if (frand() < 0.6f)
                    particle_spawn(v3_add(g_player.pos, v3(frand_range(-1.5f, 1.5f), 0.2f, frand_range(-1.5f, 1.5f))),
                                   v3(0, 4.0f + charge * 4, 0), 0x70B8FFFF, 0.25f, 0.9f, 0);
                if ((int)(sub_t * 4) != (int)((sub_t - dt) * 4)) sfx_play(SFX_CHARGE);
                if (value > 1.0f) {
                    sfx_play(SFX_FAIL);
                    hud_toast("The mana scattered! Try again.", 2.0f);
                    particles_burst(v3_add(g_player.pos, v3(0, 1, 0)), 0x70B8FFFF, 20, 6, 0.3f, 0.6f, 4);
                    charge = 0;
                }
            } else if (charge > 0.05f) {
                if (value >= 0.78f && value <= 0.97f) {
                    sub = 2; sub_t = 0;
                    g_world.storm = true;
                    g_world.storm_t = 0;
                    g_world.lightning = 1.0f;
                    sfx_play(SFX_THUNDER);
                    g_cam.shake = 0.8f;
                    game_rumble(0.4f);
                } else {
                    sfx_play(SFX_FAIL);
                    hud_toast("Not enough mana gathered... try again.", 2.0f);
                }
                charge = 0;
                charge_wobble = 0;
            }
        } else if (sub == 2 && sub_t > 3.5f) {
            sub = 3;
            TALK(D_CUMULO_DONE, after_cumulo);
        }
        break;
    case ST_FAREWELL:
        g_player.locked = false;
        if (step_t > 6.0f && sub == 0) {
            sub = 1;
            hud_banner("Chapter 3 Complete", NULL, 2.5f);
            autosave(ST_TALK_PAUL);
            g_player.locked = true;
        }
        if (sub == 1 && step_t > 8.0f) { sub = 2; game_fade_to(begin_chapter_4); }
        break;
    case ST_FOREST:
        if (forest_kills >= 8 && enemies_alive(-1) == 0) {
            if (sub == 0) { sub = 1; sub_t = 0; }
            else if (sub_t > 1.0f) TALK(D_BOSS_BOAR, after_boss_boar_intro);
        }
        break;
    case ST_BOSS_BOAR:
        for (int i = 0; i < MAX_ENEMIES; i++)
            if (g_enemies[i].active && g_enemies[i].type == EN_GREATBOAR && g_enemies[i].hp > 0)
                hud_boss_bar("GREAT BOAR", g_enemies[i].hp / g_enemies[i].max_hp);
        if (sub == 1 && sub_t > 1.5f) {
            set_step(ST_FOREST_DONE);
            npc_place(NPC_PAUL, MDL_PAUL, 30, 1, -PI_F / 2);
            npc_walk(NPC_PAUL, g_player.pos.x + 2.0f, g_player.pos.z);
            hud_banner("Chapter 4 Complete", NULL, 2.5f);
        }
        break;
    case ST_FOREST_DONE:
        if (sub == 0 && npcs[NPC_PAUL].active && (!npcs[NPC_PAUL].has_goal || step_t > 7.0f) && step_t > 1.0f) talk(NPC_PAUL);
        break;
    case ST_CALAMITY:
        if (dist_xz(g_player.pos, npcs[NPC_SYLPHIE].pos) < 5.0f) talk(NPC_SYLPHIE);
        break;
    case ST_CRYSTALS:
        if (crystals_left == 0 && enemies_alive(EN_CRYSTAL) == 0) {
            if (sub == 0) {
                sub = 1; sub_t = 0;
                for (int i = 0; i < MAX_ENEMIES; i++)
                    if (g_enemies[i].active && g_enemies[i].type == EN_WISP) enemy_hit(i, 99, g_enemies[i].pos, -1);
            } else if (sub_t > 1.5f) {
                TALK(D_CORE_INTRO, after_core_intro);
            }
        }
        break;
    case ST_CORE:
        for (int i = 0; i < MAX_ENEMIES; i++)
            if (g_enemies[i].active && g_enemies[i].type == EN_CORE && g_enemies[i].hp > 0)
                hud_boss_bar("MANA CORE", g_enemies[i].hp / g_enemies[i].max_hp);
        if (sub == 1 && sub_t > 1.2f) {
            sub = 4;
            for (int i = 0; i < MAX_ENEMIES; i++)
                if (g_enemies[i].active && g_enemies[i].type == EN_WISP) enemy_hit(i, 99, g_enemies[i].pos, -1);
            g_player.locked = true;
            TALK(D_CORE_DOWN, after_core_down);
        } else if (sub == 2) {
            white = fminf(1.0f, sub_t / 2.5f);
            g_cam.shake = 0.3f;
            if (sub_t > 3.0f) {
                sub = 3;
                set_step(ST_DEMON_WAKE);
                enter_map(MAP_DEMON, 0, 22, PI_F);
                white = 1.0f;
                g_player.locked = true;
            }
        }
        break;
    case ST_DEMON_WAKE:
        g_player.locked = step_t < 2.5f;
        if (sub == 0 && step_t > 2.5f) { sub = 1; TALK(D_DEMON_WAKE, NULL); }
        if (sub == 1 && dist_xz(g_player.pos, npcs[NPC_RUIJERD].pos) < 6.0f) { set_step(ST_RUIJERD); talk(NPC_RUIJERD); }
        break;
    default:
        break;
    }

    if (!g_player.locked) check_exits();
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

void story_render(void)
{
    for (int i = 0; i < NPC_COUNT; i++) {
        npc_t *n = &npcs[i];
        if (!n->active) continue;
        if (n->fading && n->fade < 0.5f && fmodf(g_frame.time, 0.2f) < 0.1f) continue;
        if (!gfx_visible(n->pos, 2, g_world.fog_end)) continue;
        anim_t a = n->walking ? ANIM_WALK : n->pose;
        float t = n->anim_t;
        if (a == ANIM_SWING) t = fmodf(n->anim_t * 0.8f, 1.0f);
        if (i == NPC_SYLPHIE && step == ST_GO_HILL) a = ANIM_HURT;
        draw_human(n->model, n->pos, n->yaw, a, t, 0);
    }
    /* the gathering storm over the plateau */
    if (step == ST_CUMULONIMBUS && sub == 1 && charge > 0.02f) {
        gfx_billboards_begin();
        for (int i = 0; i < 10; i++) {
            float a = g_frame.time * (0.6f + i * 0.05f) + i * 0.63f;
            float r = 4.0f + i * 0.7f;
            vec3_t c = v3_add(g_player.pos, v3(cosf(a) * r, 11.0f + sinf(a * 2) * 0.6f, sinf(a) * r));
            uint32_t col = color_mix(0x9098A8FF, 0x404858FF, charge);
            gfx_billboard(c, 3.0f + charge * 4.0f, (col & 0xFFFFFF00) | (uint32_t)(charge * 220));
        }
        gfx_billboards_end();
        glEnable(GL_LIGHTING);
        glEnable(GL_FOG);
        gfx_bind(TEX_NONE);
    }
}

void story_render_fx(void)
{
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    gfx_bind(TEX_NONE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (int i = 0; i < NPC_COUNT; i++) {
        npc_t *n = &npcs[i];
        if (!n->active || !gfx_visible(n->pos, 1, 40)) continue;
        gfx_shadow(n->pos, 0.45f, n->pos.y);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void story_render_ui(void)
{
    if (vignette > 0.01f) {
        float beat = 0.5f + 0.5f * sinf(g_frame.time * 9.0f);
        float k = vignette * (1.0f - fear * 0.8f);
        ui_fade(k * (0.45f + beat * 0.1f), 0x100008FF);
        if (step == ST_FEAR && sub == 1 && !dialog_active()) {
            ui_text_center(FONT_TITLE, STYLE_WHITE, 70, "Mash A to take a step!");
            int w = (int)(180 * clampf(fear, 0, 1));
            ui_rect(70, 84, 250, 92, 0x202028FF);
            ui_rect(70, 84, 70 + w, 92, 0xF0C860FF);
        }
    }

    if (step == ST_CUMULONIMBUS && sub == 1 && !dialog_active()) {
        ui_text_center(FONT_OUTLINE, STYLE_WHITE, 60, "Hold R to gather mana - release in the gold zone!");
        const int x0 = 90, x1 = 230, y0 = 70, y1 = 80;
        ui_rect(x0 - 2, y0 - 2, x1 + 2, y1 + 2, 0x101018FF);
        ui_rect(x0, y0, x1, y1, 0x203048FF);
        int z0 = x0 + (int)((x1 - x0) * 0.78f), z1 = x0 + (int)((x1 - x0) * 0.97f);
        ui_rect(z0, y0, z1, y1, 0x806020FF);
        float v = clampf(charge + charge_wobble, 0, 1);
        ui_rect(x0, y0 + 2, x0 + (int)((x1 - x0) * v), y1 - 2, v > 0.97f ? 0xFF5050FF : 0x70B8FFFF);
        ui_rect(z0, y0 - 3, z0 + 1, y1 + 3, 0xFFE070FF);
        ui_rect(z1, y0 - 3, z1 + 1, y1 + 3, 0xFFE070FF);
    }

    if (white > 0.01f) ui_fade(white, 0xFFFFFFFF);

    if (card_pages) {
        ui_fade(step == ST_INTRO ? 0.82f : 0.92f, 0x06040CFF);
        rdpq_text_print(&(rdpq_textparms_t){
            .style_id = STYLE_WHITE, .width = 250, .height = 150, .align = ALIGN_CENTER,
            .valign = VALIGN_CENTER, .wrap = WRAP_WORD, .max_chars = (int16_t)card_chars, .line_spacing = 4,
        }, FONT_BODY, 35, 45, card_pages[card_page]);
        if ((int)card_chars >= (int)strlen(card_pages[card_page]) && fmodf(g_frame.time, 0.8f) < 0.5f)
            ui_text(FONT_BODY, STYLE_GOLD, 286, 222, "A");
    }
}

bool story_card_active(void) { return card_pages != NULL; }
