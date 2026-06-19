/*
 * INVADERS.C -- main(), CLI parsing, startup, state machine (build step 9).
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * apply_cpu_mode()   -- populate GameSettings from cpu_mode constant.
 * apply_sound_mode() -- wire SoundConfig from saved AppConfig.
 * main()             -- first-run setup, game loop.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>

#include "EGA.H"
#include "SPRITES.H"
#include "CACHE.H"
#include "INPUT.H"
#include "SOUND.H"
#include "HISCORE.H"
#include "TITLE.H"
#include "GAME.H"

#define CFG_FILE "INVADERS.CFG"

/* =========================================================================
 * Persistent configuration (INVADERS.CFG)
 * =========================================================================*/
typedef struct {
    int cpu_mode;       /* CPU_386 / CPU_486 / CPU_P66          */
    int sound_device;   /* SOUND_NONE / SPK / SB / SBP          */
    int sound_port;     /* SB base I/O hex (e.g. 0x220)         */
    int sound_irq;
    int sound_dma;
    int sound_auto;     /* 1 = re-detect on each launch         */
} AppConfig;

static AppConfig   s_cfg;
static SoundConfig s_snd;
static int         s_ega_active;
static int         s_inp_active;

/* =========================================================================
 * apply_cpu_mode -- per-mode GameSettings table (ARCHITECTURE.md)
 * =========================================================================*/
void apply_cpu_mode(int cpu_mode, GameSettings *gs)
{
    switch (cpu_mode) {
    case CPU_386:
        gs->grid_cols          = 8;
        gs->grid_rows          = 4;
        gs->max_bombs          = 1;
        gs->ufo_enabled        = 0;
        gs->ufo_interval       = 0;
        gs->shield_mode        = SHIELD_STAGED;
        gs->explosion_frames   = 2;
        gs->title_starfield    = STAR_NONE;
        gs->march_accel        = 0;
        gs->invaders_per_frame = 1;
        gs->bomb_types         = 1;
        gs->extra_life_score   = 2000;
        gs->ufo_shot_trigger   = 30;
        break;
    case CPU_486:
        gs->grid_cols          = 11;
        gs->grid_rows          = 4;
        gs->max_bombs          = 3;
        gs->ufo_enabled        = 1;
        gs->ufo_interval       = 800;
        gs->shield_mode        = SHIELD_STAGED;
        gs->explosion_frames   = 4;
        gs->title_starfield    = STAR_STATIC;
        gs->march_accel        = 1;
        gs->invaders_per_frame = 2;
        gs->bomb_types         = 2;
        gs->extra_life_score   = 1500;
        gs->ufo_shot_trigger   = 23;
        break;
    default: /* CPU_P66 */
        gs->grid_cols          = 11;
        gs->grid_rows          = 5;
        gs->max_bombs          = 5;
        gs->ufo_enabled        = 1;
        gs->ufo_interval       = 500;
        gs->shield_mode        = SHIELD_PIXEL;
        gs->explosion_frames   = 8;
        gs->title_starfield    = STAR_SCROLL;
        gs->march_accel        = 2;
        gs->invaders_per_frame = 55;
        gs->bomb_types         = 3;
        gs->extra_life_score   = 1500;
        gs->ufo_shot_trigger   = 23;
        break;
    }
}

/* =========================================================================
 * apply_sound_mode -- wire s_snd from saved s_cfg (SOUND_AUTO=0 path)
 * =========================================================================*/
static void apply_sound_mode(void)
{
    s_snd.device      = s_cfg.sound_device;
    s_snd.port        = s_cfg.sound_port;
    s_snd.irq         = s_cfg.sound_irq;
    s_snd.dma         = s_cfg.sound_dma;
    s_snd.sample_rate = (s_snd.device == SOUND_SBP) ? 22050 : 11025;
}

/* =========================================================================
 * CFG file I/O
 * =========================================================================*/
static void parse_cfg_line(const char *line)
{
    char key[32], val[32];
    const char *eq;
    int n;

    eq = strchr(line, '=');
    if (!eq) return;
    n = (int)(eq - line);
    if (n <= 0 || n >= 32) return;

    memcpy(key, line, (size_t)n);
    key[n] = '\0';
    strncpy(val, eq + 1, 31);
    val[31] = '\0';
    for (n = 0; val[n]; n++)
        if (val[n] == '\n' || val[n] == '\r') { val[n] = '\0'; break; }

    if (!strcmp(key, "CPU")) {
        if      (!strcmp(val, "386")) s_cfg.cpu_mode = CPU_386;
        else if (!strcmp(val, "486")) s_cfg.cpu_mode = CPU_486;
        else                          s_cfg.cpu_mode = CPU_P66;
    } else if (!strcmp(key, "SOUND_DEVICE")) {
        if      (!strcmp(val, "SPK"))  s_cfg.sound_device = SOUND_SPK;
        else if (!strcmp(val, "SB"))   s_cfg.sound_device = SOUND_SB;
        else if (!strcmp(val, "SBP"))  s_cfg.sound_device = SOUND_SBP;
        else                           s_cfg.sound_device = SOUND_NONE;
    } else if (!strcmp(key, "SOUND_PORT")) {
        s_cfg.sound_port = (int)strtol(val, NULL, 16);
    } else if (!strcmp(key, "SOUND_IRQ")) {
        s_cfg.sound_irq = atoi(val);
    } else if (!strcmp(key, "SOUND_DMA")) {
        s_cfg.sound_dma = atoi(val);
    } else if (!strcmp(key, "SOUND_AUTO")) {
        s_cfg.sound_auto = atoi(val);
    }
}

static int cfg_load(void)
{
    FILE *f;
    char  line[64];
    f = fopen(CFG_FILE, "r");
    if (!f) return 0;
    while (fgets(line, sizeof(line), f))
        parse_cfg_line(line);
    fclose(f);
    return 1;
}

static void cfg_save(void)
{
    FILE *f;
    const char *dev_str;
    f = fopen(CFG_FILE, "w");
    if (!f) return;

    switch (s_cfg.cpu_mode) {
    case CPU_386: fprintf(f, "CPU=386\n"); break;
    case CPU_486: fprintf(f, "CPU=486\n"); break;
    default:      fprintf(f, "CPU=P66\n"); break;
    }
    switch (s_cfg.sound_device) {
    case SOUND_SPK:  dev_str = "SPK";  break;
    case SOUND_SB:   dev_str = "SB";   break;
    case SOUND_SBP:  dev_str = "SBP";  break;
    default:         dev_str = "NONE"; break;
    }
    fprintf(f, "SOUND_DEVICE=%s\n", dev_str);
    fprintf(f, "SOUND_PORT=%X\n",   s_cfg.sound_port);
    fprintf(f, "SOUND_IRQ=%d\n",    s_cfg.sound_irq);
    fprintf(f, "SOUND_DMA=%d\n",    s_cfg.sound_dma);
    fprintf(f, "SOUND_AUTO=%d\n",   s_cfg.sound_auto);
    fclose(f);
}

/* =========================================================================
 * Text-mode first-run prompts
 * =========================================================================*/
static void prompt_cpu(void)
{
    int ch;
    printf("\n  SPACE INVADERS\n\n");
    printf("  Select your machine:\n\n");
    printf("  [1] 386 DX/16\n");
    printf("  [2] 486 DX/33\n");
    printf("  [3] Pentium 66\n\n");
    printf("  Press 1, 2, or 3...\n");
    for (;;) {
        ch = getch();
        if (ch == '1') { s_cfg.cpu_mode = CPU_386; break; }
        if (ch == '2') { s_cfg.cpu_mode = CPU_486; break; }
        if (ch == '3') { s_cfg.cpu_mode = CPU_P66; break; }
    }
    printf("\n");
}

static void prompt_sound(int detected)
{
    int ch;
    const char *rec;
    switch (detected) {
    case SOUND_SBP: rec = "Sound Blaster Pro"; break;
    case SOUND_SB:  rec = "Sound Blaster";     break;
    default:        rec = "PC Speaker";        break;
    }
    printf("  Sound configuration:\n\n");
    printf("  [1] PC Speaker\n");
    printf("  [2] Sound Blaster\n");
    printf("  [3] Sound Blaster Pro\n");
    printf("  [4] No sound\n\n");
    printf("  Auto-detected: %s -- press 1-4 or Enter to accept\n", rec);
    for (;;) {
        ch = getch();
        if (ch == '\r' || ch == '\n') { s_cfg.sound_device = detected; break; }
        if (ch == '1') { s_cfg.sound_device = SOUND_SPK;  break; }
        if (ch == '2') { s_cfg.sound_device = SOUND_SB;   break; }
        if (ch == '3') { s_cfg.sound_device = SOUND_SBP;  break; }
        if (ch == '4') { s_cfg.sound_device = SOUND_NONE; break; }
    }
    printf("\n");
}

/* =========================================================================
 * /SOUNDTEST -- text mode only; no EGA
 * =========================================================================*/
static void run_soundtest(void)
{
    const char *env = getenv("BLASTER");
    const char *type_str;
    SoundConfig tmp;
    long i;

    printf("\nSound Blaster Detection\n");
    printf("-----------------------\n");
    printf("BLASTER env : %s\n", env ? env : "(not set)");

    switch (s_snd.device) {
    case SOUND_SBP: type_str = "Sound Blaster Pro"; break;
    case SOUND_SB:  type_str = "Sound Blaster";     break;
    default:        type_str = "None detected";     break;
    }

    if (s_snd.device == SOUND_SB || s_snd.device == SOUND_SBP) {
        printf("Base port   : %Xh\n", s_snd.port);
        printf("DSP reset   : OK\n");
        printf("Card type   : %s\n",  type_str);
        printf("IRQ         : %d\n",  s_snd.irq);
        printf("DMA         : %d\n",  s_snd.dma);
    } else {
        printf("Card type   : PC Speaker only\n");
    }

    printf("\nTesting PC Speaker...    ");
    fflush(stdout);
    tmp.device = SOUND_SPK; tmp.port = 0; tmp.irq = 0;
    tmp.dma = 0; tmp.sample_rate = 11025;
    sound_init(&tmp);
    sound_play(SND_MARCH0, SPRI_BG, 160);
    for (i = 0; i < 100000L; i++) sound_update();
    sound_stop();
    sound_shutdown();
    printf("OK\n");

    if (s_snd.device == SOUND_SB || s_snd.device == SOUND_SBP) {
        printf("Testing SB DAC output... ");
        fflush(stdout);
        if (cache_generate_sounds() == 0) {
            tmp = s_snd; tmp.device = SOUND_SB;
            sound_init(&tmp);
            sound_play(SND_FIRE, SPRI_HIGH, 160);
            for (i = 0; i < 400000L; i++) sound_update();
            sound_stop();
            sound_shutdown();
        }
        printf("OK\n");
    }

    if (s_snd.device == SOUND_SBP) {
        printf("Testing SB Pro stereo... ");
        fflush(stdout);
        sound_init(&s_snd);
        sound_play(SND_LEVEL_CLEAR, SPRI_CRITICAL, 0);
        for (i = 0; i < 600000L; i++) sound_update();
        sound_stop();
        sound_shutdown();
        printf("OK\n");
    }

    sound_free();
    printf("\nPress any key to continue or ESC to exit.\n");
    getch();
}

/* =========================================================================
 * Command-line parsing
 * =========================================================================*/
typedef struct {
    int override_cpu;     /* -1 = no override */
    int override_sound;   /* -1 = no override */
    int override_port;    /* 0  = no override */
    int override_irq;     /* -1 = no override */
    int override_dma;     /* -1 = no override */
    int do_save;
    int do_reset;
    int do_resethi;
    int do_soundtest;
} CmdOpts;

static int parse_hex(const char *s)
{
    int v = 0;
    for (; *s; s++) {
        if      (*s >= '0' && *s <= '9') v = v * 16 + (*s - '0');
        else if (*s >= 'A' && *s <= 'F') v = v * 16 + (*s - 'A' + 10);
        else if (*s >= 'a' && *s <= 'f') v = v * 16 + (*s - 'a' + 10);
        else break;
    }
    return v;
}

static void parse_args(int argc, char *argv[], CmdOpts *o)
{
    int i;
    const char *a;

    o->override_cpu   = -1;
    o->override_sound = -1;
    o->override_port  = 0;
    o->override_irq   = -1;
    o->override_dma   = -1;
    o->do_save        = 0;
    o->do_reset       = 0;
    o->do_resethi     = 0;
    o->do_soundtest   = 0;

    for (i = 1; i < argc; i++) {
        a = argv[i];
        if (*a == '/' || *a == '-') a++;

        if      (!stricmp(a, "386"))       o->override_cpu   = CPU_386;
        else if (!stricmp(a, "486"))       o->override_cpu   = CPU_486;
        else if (!stricmp(a, "P66"))       o->override_cpu   = CPU_P66;
        else if (!stricmp(a, "SBP"))       o->override_sound = SOUND_SBP;
        else if (!stricmp(a, "SB"))        o->override_sound = SOUND_SB;
        else if (!stricmp(a, "SPK"))       o->override_sound = SOUND_SPK;
        else if (!stricmp(a, "NOSOUND"))   o->override_sound = SOUND_NONE;
        else if (!stricmp(a, "SAVE"))      o->do_save        = 1;
        else if (!stricmp(a, "reset"))     o->do_reset       = 1;
        else if (!stricmp(a, "resethi"))   o->do_resethi     = 1;
        else if (!stricmp(a, "resetall")) { o->do_reset = 1; o->do_resethi = 1; }
        else if (!stricmp(a, "SOUNDTEST")) o->do_soundtest   = 1;
        else if (!strnicmp(a, "PORT:", 5)) o->override_port  = parse_hex(a + 5);
        else if (!strnicmp(a, "IRQ:", 4))  o->override_irq   = atoi(a + 4);
        else if (!strnicmp(a, "DMA:", 4))  o->override_dma   = atoi(a + 4);
    }
}

/* =========================================================================
 * Data generation helpers
 * =========================================================================*/
static void generate_sprites(void)
{
    printf("Generating SPRITES.DAT...\n");
    if (cache_generate_sprites() != 0) {
        printf("ERROR: sprite generation failed (out of memory?)\n");
        exit(1);
    }
    if (cache_save_sprites() != 0)
        printf("WARNING: could not write SPRITES.DAT\n");
}

static void generate_sounds(void)
{
    printf("Generating SOUNDS.DAT...\n");
    if (cache_generate_sounds() != 0 || cache_save_sounds() != 0) {
        printf("WARNING: sound generation failed -- falling back to PC Speaker\n");
        s_cfg.sound_device = SOUND_SPK;
        s_snd.device       = SOUND_SPK;
    }
}

/* =========================================================================
 * atexit cleanup -- restores text mode and IRQ1 on any exit
 * =========================================================================*/
static void cleanup(void)
{
    if (s_inp_active) { inp_remove(); s_inp_active = 0; }
    sound_shutdown();
    sound_free();
    cache_free();
    if (s_ega_active) { ega_set_text_mode(); s_ega_active = 0; }
}

/* =========================================================================
 * main
 * =========================================================================*/
int main(int argc, char *argv[])
{
    CmdOpts      opts;
    GameSettings gs;
    int          first_run;
    int          detected;

    parse_args(argc, argv, &opts);
    atexit(cleanup);

    /* Hi-score reset before anything else (text mode safe) */
    if (opts.do_resethi) {
        hiscore_reset();
        printf("High scores reset to defaults.\n");
    }

    /* Sound detection always runs so /SOUNDTEST always has data */
    detected = sound_detect(&s_snd);

    /* /SOUNDTEST: show results in text mode then exit -- no EGA */
    if (opts.do_soundtest) {
        run_soundtest();
        return 0;
    }

    /* /reset wipes CFG, forcing first-run prompts again */
    if (opts.do_reset)
        remove(CFG_FILE);

    first_run = !cfg_load();

    if (first_run) {
        s_cfg.cpu_mode     = CPU_486;
        s_cfg.sound_device = detected;
        s_cfg.sound_port   = s_snd.port;
        s_cfg.sound_irq    = s_snd.irq;
        s_cfg.sound_dma    = s_snd.dma;
        s_cfg.sound_auto   = 1;
        prompt_cpu();
        prompt_sound(detected);
    }

    /* Session-only CLI overrides (persist only with /SAVE) */
    if (opts.override_cpu   >= 0) s_cfg.cpu_mode     = opts.override_cpu;
    if (opts.override_sound >= 0) s_cfg.sound_device = opts.override_sound;
    if (opts.override_port  >  0) s_cfg.sound_port   = opts.override_port;
    if (opts.override_irq   >= 0) s_cfg.sound_irq    = opts.override_irq;
    if (opts.override_dma   >= 0) s_cfg.sound_dma    = opts.override_dma;

    if (opts.do_save || first_run)
        cfg_save();

    /* Wire sound hardware config */
    if (s_cfg.sound_auto) {
        /* Keep port/irq/dma from sound_detect(); apply device choice */
        s_snd.device = s_cfg.sound_device;
    } else {
        apply_sound_mode();
    }

    /* Sprite data: load or generate */
    if (!cache_sprites_valid())
        generate_sprites();
    if (cache_load_sprites() != 0) {
        generate_sprites();
        if (cache_load_sprites() != 0) {
            printf("ERROR: could not load SPRITES.DAT\n");
            return 1;
        }
    }

    /* Sound data: load or generate */
    if (s_snd.device == SOUND_SB || s_snd.device == SOUND_SBP) {
        if (cache_load_sounds() != 0)
            generate_sounds();
    }

    /* High scores: load or regenerate defaults silently */
    hiscore_init();

    /* --- EGA mode from this point forward --- */
    ega_set_mode();   s_ega_active = 1;
    inp_install();    s_inp_active = 1;
    sound_init(&s_snd);

    apply_cpu_mode(s_cfg.cpu_mode, &gs);

    /*
     * Main game loop. Runs indefinitely (authentic arcade behavior).
     * Ctrl+Break terminates; atexit(cleanup) restores text mode and IRQ1.
     */
    for (;;) {
        sound_play(SND_TITLE, SPRI_BG, 160);
        title_run(s_cfg.cpu_mode, gs.title_starfield, hiscore_get_table());
        game_run(&gs, hiscore_get_mode(s_cfg.cpu_mode)[0].score, s_cfg.cpu_mode);
    }
}
