/*
 * TITLE.C -- title screen, attract mode, high score display.
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * Dependencies: EGA.C, SPRITES.C, CACHE.C
 * Keyboard: kbhit()/getch() from <conio.h> (BIOS buffered -- fine for menus).
 */

#include <conio.h>
#include "EGA.H"
#include "SPRITES.H"
#include "CACHE.H"
#include "TITLE.H"

/* -------------------------------------------------------------------------
 * Attract loop timing (~70 Hz vsync)
 * -------------------------------------------------------------------------*/
#define FRAMES_TITLE    350   /* ~5 s  */
#define FRAMES_HISCORE  350   /* ~5 s  */
#define FRAMES_DEMO    1050   /* ~15 s */
#define FRAMES_CYCLE   (FRAMES_TITLE + FRAMES_HISCORE + FRAMES_DEMO)

/* -------------------------------------------------------------------------
 * Screen layout Y positions
 * -------------------------------------------------------------------------*/
#define LOGO_SPACE_Y      8
#define LOGO_INV_Y       40
#define MARCH_INV_Y      90
#define PRESS_FIRE_Y    120
#define HISCORE_Y       150
#define DEMO_PLAYER_Y   170

/* -------------------------------------------------------------------------
 * Marching invader (in title phase)
 * -------------------------------------------------------------------------*/
#define MARCH_INV_X_MIN  16
#define MARCH_INV_X_MAX 296
/* Logical step per frame. Note: ega_blit_planar requires byte-aligned X, so
 * draw_march/draw_demo_player mask with ~7 -- visible motion is quantized to
 * 8-px jumps (every 4th frame at speed 2). Arcade-style stepping; intended. */
#define MARCH_INV_SPEED   2
#define MARCH_INV_ANIM_PERIOD 8

/* -------------------------------------------------------------------------
 * Blink (PRESS FIRE TO START)
 * -------------------------------------------------------------------------*/
#define BLINK_PERIOD 35   /* frames per half-cycle */

/* -------------------------------------------------------------------------
 * Palette cycling (logo shimmer on EGA_CYAN index)
 * -------------------------------------------------------------------------*/
static const unsigned char s_cyan_cycle[] = { 3, 11, 3, 11, 3, 27 };
#define CYAN_CYCLE_LEN  6
#define CYAN_CYCLE_PERIOD 6   /* frames between steps */

/* -------------------------------------------------------------------------
 * Starfield
 * -------------------------------------------------------------------------*/
#define NUM_STARS 64

static int           s_star_x[NUM_STARS];
static int           s_star_y[NUM_STARS];
static int           s_star_speed[NUM_STARS];   /* 1 or 2 (STAR_SCROLL parallax) */
static unsigned char s_star_drawn[NUM_STARS];   /* 1 = white pixel on screen now */
static int           s_star_phase;              /* current phase, for star_blocked() */

/* -------------------------------------------------------------------------
 * Hi-score column positions
 * -------------------------------------------------------------------------*/
#define HISCORE_COL0_X  16
#define HISCORE_COL1_X 112
#define HISCORE_COL2_X 208
#define HISCORE_ROW0_Y  32   /* first data row (below header) */
#define HISCORE_ROW_H    9   /* pixels per row (8px font + 1 gap) */

/* -------------------------------------------------------------------------
 * Demo grid layout
 * -------------------------------------------------------------------------*/
#define DEMO_GRID_ROWS   5
#define DEMO_GRID_COLS  11
#define DEMO_GRID_X     16
#define DEMO_GRID_Y     20
#define DEMO_CELL_W     16   /* cell stride (INV_W=11 + 5 gap) -- byte aligned */
#define DEMO_CELL_H     12   /* cell height stride (INV_H=8 + 4 gap) */

/* -------------------------------------------------------------------------
 * Invader blit width in bytes (x must be multiple of 8; INV_W=11 -> 2 bytes)
 * -------------------------------------------------------------------------*/
#define INV_W_BYTES   2      /* ceil(INV_W/8) */
#define UFO_W_BYTES   2      /* UFO_W=16 -> 2 bytes */
#define PLAYER_W_BYTES 2     /* ceil(PLAYER_W/8) -> 2 bytes */
#define FONT_W_BYTES   1     /* FONT_W=8 -> 1 byte */

/* -------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------*/
static int s_inv_x;          /* marching invader X position */
static int s_inv_dir;        /* +1 right, -1 left */
static int s_inv_anim;       /* 0 or 1 -- current animation frame */
static int s_inv_prev_x;     /* last blit X (for dirty rect) */

static int s_demo_plr_x;     /* oscillating player X in demo phase */
static int s_demo_plr_dir;   /* +1 right, -1 left */
static int s_demo_plr_prev;  /* previous X for dirty rect */

/* -------------------------------------------------------------------------
 * Simple LCG (no stdlib rand, no float)
 * -------------------------------------------------------------------------*/
static unsigned long lcg_next(unsigned long prev)
{
    return (prev * 1103515245UL + 12345UL) & 0x7FFFFFFFUL;
}

/* -------------------------------------------------------------------------
 * Stars
 * -------------------------------------------------------------------------*/
static void init_stars(void)
{
    unsigned long rng = 12345UL;
    int i;
    for (i = 0; i < NUM_STARS; i++) {
        rng = lcg_next(rng);
        s_star_x[i] = (int)(rng % SCREEN_WIDTH);
        rng = lcg_next(rng);
        s_star_y[i] = (int)(rng % SCREEN_HEIGHT);
        rng = lcg_next(rng);
        s_star_speed[i] = (int)(rng % 2) + 1;  /* 1 or 2 */
        s_star_drawn[i] = 0;
    }
}

/*
 * Returns 1 if (x,y) falls within a horizontal band reserved for static art
 * in the current phase. Stars are suppressed there so scrolling never punches
 * holes in, or scatters dots over, the logo / text / sprites underneath.
 * Band-only (X ignored) -- the reserved rows are otherwise clear.
 */
static int star_blocked(int y)
{
    switch (s_star_phase) {
    case 0: /* title: logo, marching invader, press-fire, hi-score line */
        if (y >= LOGO_SPACE_Y && y < LOGO_INV_Y + FONT_H * 3) return 1;
        if (y >= MARCH_INV_Y   && y < MARCH_INV_Y + INV_H)    return 1;
        if (y >= PRESS_FIRE_Y  && y < PRESS_FIRE_Y + FONT_H)  return 1;
        if (y >= HISCORE_Y     && y < HISCORE_Y + FONT_H)     return 1;
        return 0;
    case 1: /* hi-score screen: title + header + 10 rows */
        if (y >= LOGO_SPACE_Y &&
            y < HISCORE_ROW0_Y + HISCORE_PER_MODE * HISCORE_ROW_H) return 1;
        return 0;
    default: /* demo: frozen grid + oscillating player */
        if (y >= DEMO_GRID_Y &&
            y < DEMO_GRID_Y + DEMO_GRID_ROWS * DEMO_CELL_H)       return 1;
        if (y >= DEMO_PLAYER_Y && y < DEMO_PLAYER_Y + PLAYER_H)   return 1;
        return 0;
    }
}

static void draw_stars(void)
{
    int i;
    for (i = 0; i < NUM_STARS; i++) {
        if (!star_blocked(s_star_y[i])) {
            ega_put_pixel(s_star_x[i], s_star_y[i], EGA_WHITE);
            s_star_drawn[i] = 1;
        } else {
            s_star_drawn[i] = 0;
        }
    }
}

static void update_stars_scroll(void)
{
    int i;
    for (i = 0; i < NUM_STARS; i++) {
        if (s_star_drawn[i])
            ega_put_pixel(s_star_x[i], s_star_y[i], EGA_BLACK);
        s_star_y[i] += s_star_speed[i];
        if (s_star_y[i] >= SCREEN_HEIGHT)
            s_star_y[i] = 0;
        if (!star_blocked(s_star_y[i])) {
            ega_put_pixel(s_star_x[i], s_star_y[i], EGA_WHITE);
            s_star_drawn[i] = 1;
        } else {
            s_star_drawn[i] = 0;
        }
    }
}

/* -------------------------------------------------------------------------
 * Text rendering
 * Fonts are 8×8; bit 7 = leftmost pixel of each row.
 * -------------------------------------------------------------------------*/
static void draw_text(int x, int y, const char *s, unsigned char color)
{
    int glyph, row, bit, cx;
    unsigned char rowbits;

    for (; *s; s++, x += FONT_W) {
        glyph = spr_char_to_glyph((unsigned char)*s);
        if (glyph < 0) continue;
        for (row = 0; row < FONT_H; row++) {
            rowbits = g_font[glyph * FONT_H + row];
            for (bit = 7; bit >= 0; bit--) {
                if (rowbits & (1 << bit)) {
                    cx = x + (7 - bit);
                    ega_put_pixel(cx, y + row, color);
                }
            }
        }
    }
}

/*
 * draw_text_large -- render text with pixel scaling.
 * Each set pixel becomes a (scale × scale) filled rectangle.
 */
static void draw_text_large(int x, int y, const char *s,
                             unsigned char color, int scale)
{
    int glyph, row, bit, cx, cy;
    unsigned char rowbits;

    for (; *s; s++, x += FONT_W * scale) {
        glyph = spr_char_to_glyph((unsigned char)*s);
        if (glyph < 0) continue;
        for (row = 0; row < FONT_H; row++) {
            rowbits = g_font[glyph * FONT_H + row];
            cy = y + row * scale;
            for (bit = 7; bit >= 0; bit--) {
                if (rowbits & (1 << bit)) {
                    cx = x + (7 - bit) * scale;
                    ega_fill_rect(cx, cy, scale, scale, color);
                }
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Logo (SPACE / INVADERS) with drop shadow
 * -------------------------------------------------------------------------*/
static void draw_logo(void)
{
    /* Shadow first, then color on top */
    draw_text_large(10, LOGO_SPACE_Y + 2, "SPACE",    EGA_BLACK,   3);
    draw_text_large(10, LOGO_SPACE_Y,     "SPACE",    EGA_CYAN,    3);
    draw_text_large(10, LOGO_INV_Y + 2,   "INVADERS", EGA_BLACK,   3);
    draw_text_large(10, LOGO_INV_Y,       "INVADERS", EGA_MAGENTA, 3);
}

/* -------------------------------------------------------------------------
 * PRESS FIRE TO START  (blink text)
 * -------------------------------------------------------------------------*/
#define PRESS_FIRE_X  48   /* rough centering for "PRESS FIRE TO START" */

static void draw_press_fire(void)
{
    draw_text(PRESS_FIRE_X, PRESS_FIRE_Y, "PRESS FIRE TO START", EGA_WHITE);
}

static void erase_press_fire(void)
{
    ega_fill_rect(PRESS_FIRE_X, PRESS_FIRE_Y,
                  19 * FONT_W, FONT_H, EGA_BLACK);
}

/* -------------------------------------------------------------------------
 * High score display on title screen (best score, bottom row)
 * -------------------------------------------------------------------------*/
#define HISCORE_LABEL_X  48

static void draw_hiscore_line(long score)
{
    /* "HIGH SCORE  XXXXXX" -- format score as 6 digits */
    static char buf[32];
    char *p;
    long v;
    int i;

    /* manual itoa (no stdlib, no printf, no float) */
    p = buf + 31;
    *p = '\0';
    v = score;
    if (v < 0) v = 0;
    for (i = 0; i < 6; i++) {
        *--p = (char)('0' + (v % 10));
        v /= 10;
    }
    /* prepend label */
    {
        static char line[32];
        int j = 0, k = 0;
        const char *label = "HIGH SCORE  ";
        while (label[k]) line[j++] = label[k++];
        k = 0;
        while (p[k]) line[j++] = p[k++];
        line[j] = '\0';
        draw_text(HISCORE_LABEL_X, HISCORE_Y, line, EGA_WHITE);
    }
}

/* -------------------------------------------------------------------------
 * Marching invader (title phase)
 * -------------------------------------------------------------------------*/
static void init_march(void)
{
    s_inv_x      = MARCH_INV_X_MIN;
    s_inv_dir    = 1;
    s_inv_anim   = 0;
    s_inv_prev_x = MARCH_INV_X_MIN;
}

static void draw_march(void)
{
    /* x rounded down to nearest 8 for byte alignment */
    int bx = s_inv_x & ~7;
    ega_blit_planar(bx, MARCH_INV_Y, INV_W_BYTES, INV_H,
                    g_cache.inv_a[s_inv_anim]);
}

static void erase_march(void)
{
    int bx = s_inv_prev_x & ~7;
    ega_fill_rect(bx, MARCH_INV_Y, INV_W_BYTES * 8, INV_H, EGA_BLACK);
}

static void update_march(int frame)
{
    s_inv_prev_x = s_inv_x;
    s_inv_x += s_inv_dir * MARCH_INV_SPEED;
    if (s_inv_x >= MARCH_INV_X_MAX) { s_inv_x = MARCH_INV_X_MAX; s_inv_dir = -1; }
    if (s_inv_x <= MARCH_INV_X_MIN) { s_inv_x = MARCH_INV_X_MIN; s_inv_dir =  1; }
    if ((frame % MARCH_INV_ANIM_PERIOD) == 0)
        s_inv_anim ^= 1;
}

/* -------------------------------------------------------------------------
 * Palette cycling (logo shimmer)
 * -------------------------------------------------------------------------*/
static void update_palette_cycle(int frame)
{
    int idx = (frame / CYAN_CYCLE_PERIOD) % CYAN_CYCLE_LEN;
    ega_set_palette(EGA_CYAN, s_cyan_cycle[idx]);
}

/* -------------------------------------------------------------------------
 * Phase 1: full high-score screen
 * -------------------------------------------------------------------------*/
static void draw_hiscore_screen(int cpu_mode, const HiScoreEntry *scores)
{
    static const char *headers[3] = { "386", "486", "P66" };
    static const int col_x[3] = { HISCORE_COL0_X, HISCORE_COL1_X, HISCORE_COL2_X };
    int mode, row;
    unsigned char hdr_color;
    char scorebuf[16];
    char linebuf[16];

    ega_clear(EGA_BLACK);

    draw_text(80, 8, "HIGH SCORES", EGA_YELLOW);

    for (mode = 0; mode < 3; mode++) {
        hdr_color = (mode == cpu_mode) ? EGA_YELLOW : EGA_WHITE;
        draw_text(col_x[mode], 20, headers[mode], hdr_color);

        for (row = 0; row < HISCORE_PER_MODE; row++) {
            const HiScoreEntry *e;
            unsigned char row_color;
            long v;
            char *p;
            int i, j, k;

            if (scores == 0) break;

            e = &scores[mode * HISCORE_PER_MODE + row];
            row_color = (mode == cpu_mode) ? EGA_YELLOW : EGA_WHITE;

            /* format score 6 digits */
            p = scorebuf + 15;
            *p = '\0';
            v = e->score;
            if (v < 0) v = 0;
            for (i = 0; i < 6; i++) {
                *--p = (char)('0' + (v % 10));
                v /= 10;
            }

            /* build "AAA XXXXXX" */
            j = 0;
            for (k = 0; k < HISCORE_INITIALS_LEN - 1 && e->initials[k]; k++)
                linebuf[j++] = e->initials[k];
            linebuf[j++] = ' ';
            for (k = 0; p[k]; k++)
                linebuf[j++] = p[k];
            linebuf[j] = '\0';

            draw_text(col_x[mode],
                      HISCORE_ROW0_Y + row * HISCORE_ROW_H,
                      linebuf, row_color);
        }
    }
}

/* -------------------------------------------------------------------------
 * Phase 2: demo screen -- frozen invader grid + oscillating player
 * -------------------------------------------------------------------------*/
static void draw_demo_grid(void)
{
    /* Row types: top 2 rows = inv_a, middle 2 = inv_b, bottom = inv_c */
    static const int row_type[DEMO_GRID_ROWS] = { 0, 0, 1, 1, 2 };
    int gr, gc;
    int bx, by;
    unsigned char far *frame;

    for (gr = 0; gr < DEMO_GRID_ROWS; gr++) {
        by = DEMO_GRID_Y + gr * DEMO_CELL_H;
        for (gc = 0; gc < DEMO_GRID_COLS; gc++) {
            bx = (DEMO_GRID_X + gc * DEMO_CELL_W) & ~7;
            switch (row_type[gr]) {
                case 0: frame = g_cache.inv_a[0]; break;
                case 1: frame = g_cache.inv_b[0]; break;
                default: frame = g_cache.inv_c[0]; break;
            }
            ega_blit_planar(bx, by, INV_W_BYTES, INV_H, frame);
        }
    }
}

static void init_demo_player(void)
{
    s_demo_plr_x    = 16;
    s_demo_plr_dir  = 1;
    s_demo_plr_prev = 16;
}

static void erase_demo_player(void)
{
    int bx = s_demo_plr_prev & ~7;
    ega_fill_rect(bx, DEMO_PLAYER_Y, PLAYER_W_BYTES * 8, PLAYER_H, EGA_BLACK);
}

static void draw_demo_player(void)
{
    int bx = s_demo_plr_x & ~7;
    ega_blit_planar(bx, DEMO_PLAYER_Y, PLAYER_W_BYTES, PLAYER_H,
                    g_cache.player);
}

static void update_demo_player(void)
{
    s_demo_plr_prev = s_demo_plr_x;
    s_demo_plr_x   += s_demo_plr_dir * 2;
    if (s_demo_plr_x >= SCREEN_WIDTH - PLAYER_W - 16) s_demo_plr_dir = -1;
    if (s_demo_plr_x <= 16)                           s_demo_plr_dir =  1;
}

/* -------------------------------------------------------------------------
 * Fire key detection (BIOS buffered -- OK for title)
 * -------------------------------------------------------------------------*/
static int key_fire(void)
{
    int ch;
    if (!kbhit()) return 0;
    ch = getch();
    if (ch == ' ' || ch == '\r' || ch == 27) return 1;
    /* absorb extended scan codes */
    if (ch == 0 && kbhit()) getch();
    return 0;
}

/* -------------------------------------------------------------------------
 * Draw title phase static elements
 * -------------------------------------------------------------------------*/
static void draw_title_phase(int cpu_mode, int starfield,
                             const HiScoreEntry *scores)
{
    s_star_phase = 0;
    ega_clear(EGA_BLACK);
    if (starfield != STAR_NONE) draw_stars();
    draw_logo();
    draw_press_fire();

    /* show best score for current cpu_mode */
    if (scores)
        draw_hiscore_line(scores[cpu_mode * HISCORE_PER_MODE].score);

    init_march();
    draw_march();
}

/* -------------------------------------------------------------------------
 * title_run() -- main entry point
 * -------------------------------------------------------------------------*/
void title_run(int cpu_mode, int starfield, const HiScoreEntry *scores)
{
    int phase;           /* 0=title, 1=hiscore, 2=demo */
    int phase_frame;
    int total_frame;
    int phase_limit;
    int blink_prev;

    init_stars();
    draw_title_phase(cpu_mode, starfield, scores);

    phase       = 0;
    phase_frame = 0;
    total_frame = 0;
    phase_limit = FRAMES_TITLE;
    blink_prev  = 1;

    for (;;) {
        ega_wait_vsync();

        if (key_fire()) {
            ega_set_palette(EGA_CYAN, 3);   /* restore default cyan */
            return;
        }

        /* ----- per-frame updates ----- */

        if (starfield == STAR_SCROLL)
            update_stars_scroll();

        if (phase == 0) {
            /* marching invader */
            erase_march();
            update_march(total_frame);
            draw_march();

            /* blink */
            {
                int blink_on = ((total_frame / BLINK_PERIOD) & 1) == 0;
                if (blink_on != blink_prev) {
                    if (blink_on)
                        draw_press_fire();
                    else
                        erase_press_fire();
                    blink_prev = blink_on;
                }
            }

            update_palette_cycle(total_frame);

        } else if (phase == 2) {
            /* demo: oscillate player */
            erase_demo_player();
            update_demo_player();
            draw_demo_player();
        }

        /* ----- phase transition ----- */
        phase_frame++;
        total_frame++;

        if (phase_frame >= phase_limit) {
            phase_frame = 0;
            phase = (phase + 1) % 3;

            /* NULL scores hides the hi-score phase: title <-> demo only */
            if (phase == 1 && scores == 0)
                phase = 2;

            s_star_phase = phase;

            if (phase == 0) {
                phase_limit = FRAMES_TITLE;
                draw_title_phase(cpu_mode, starfield, scores);
                blink_prev = 1;
            } else if (phase == 1) {
                phase_limit = FRAMES_HISCORE;
                ega_set_palette(EGA_CYAN, 3);   /* restore cyan for hi-score */
                draw_hiscore_screen(cpu_mode, scores);
                if (starfield != STAR_NONE) draw_stars();
            } else {
                phase_limit = FRAMES_DEMO;
                ega_clear(EGA_BLACK);
                if (starfield != STAR_NONE) draw_stars();
                draw_demo_grid();
                init_demo_player();
                draw_demo_player();
            }
        }
    }
}

/* =========================================================================
 * TITLE_TEST -- standalone compile/visual test
 *
 * Compile:
 *   wcc -ml -zf -DTITLE_TEST TITLE.C EGA.OBJ SPRITES.OBJ CACHE.OBJ
 *   wlink file TITLE,EGA,SPRITES,CACHE name TITLETEST
 * =========================================================================*/
#ifdef TITLE_TEST

#include <stdlib.h>

int main(void)
{
    HiScoreEntry scores[3 * HISCORE_PER_MODE];
    static const char *names[10] = {
        "HAL", "ACE", "JET", "MAX", "REX",
        "ZAP", "VIC", "BOB", "DOC", "FOX"
    };
    int i, m;

    /* build fake score table */
    for (m = 0; m < 3; m++) {
        for (i = 0; i < HISCORE_PER_MODE; i++) {
            HiScoreEntry *e = &scores[m * HISCORE_PER_MODE + i];
            e->initials[0] = names[i][0];
            e->initials[1] = names[i][1];
            e->initials[2] = names[i][2];
            e->initials[3] = '\0';
            e->score    = (long)(9000 - i * 500 + m * 100);
            e->cpu_mode = m;
        }
    }

    if (cache_sprites_valid())
        cache_load_sprites();
    else {
        if (cache_generate_sprites() != 0) {
            puts("cache_generate_sprites failed");
            return 1;
        }
        cache_save_sprites();
    }

    ega_set_mode();
    title_run(CPU_486, STAR_SCROLL, scores);
    ega_set_text_mode();
    cache_free();
    return 0;
}

#endif /* TITLE_TEST */
