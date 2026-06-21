/*
 * GAME.C -- full arcade game loop (build step 8).
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * Implements the complete Space Invaders game:
 *   - Invader grid (8×4 / 11×4 / 11×5 per CPU mode)
 *   - March timing (march_delay_table + D1 cursor + D2 accel)
 *   - Bombs (sequence + D5 substitution), player bullet, UFO
 *   - Shields (SHIELD_STAGED 4-state / SHIELD_PIXEL per-pixel)
 *   - Collision detection, scoring, wave progression, death sequences
 *   - Hi-score check and initials entry after game over
 *
 * All timing via EGA vsync (port 3DAh). No floating point. No blocking delays.
 */

#include <stdlib.h>   /* abs() */
#include <string.h>   /* memset, memcpy */
#include <stdio.h>    /* sprintf (score draw) */
#include "EGA.H"
#include "SPRITES.H"
#include "CACHE.H"
#include "INPUT.H"
#include "SOUND.H"
#include "HISCORE.H"
#include "GAME.H"
#include "STATS.H"

/* =========================================================================
 * Screen layout constants
 * =========================================================================*/
#define HUD_Y        2     /* pixel y of score / hi-score / wave text    */
#define UFO_Y       14     /* pixel y of UFO flight path                 */
#define PLAYER_Y   176     /* pixel y of player cannon top               */
#define SHIELD_Y   152     /* pixel y of shield bunkers top              */
#define GROUND_Y   185     /* pixel y of ground divider line             */
#define LIVES_Y    189     /* pixel y of lives icon row                  */

/* Invader grid geometry */
#define INV_STEP_X  16     /* pixel columns per invader cell (byte-aligned) */
#define INV_STEP_Y  16     /* pixel rows per invader cell                   */

/* Byte widths for ega_blit_planar */
#define INV_W_BYTES    2   /* ceil(INV_W=11 / 8) */
#define PLAYER_W_BYTES 2   /* ceil(PLAYER_W=13 / 8) */
#define UFO_W_BYTES    2   /* ceil(UFO_W=16 / 8) */
#define BULLET_W_BYTES 1
#define BOMB_W_BYTES   1   /* ceil(BOMB_W=3 / 8) */
#define LIVES_W_BYTES  2

/* Four shield x-positions (24px wide, 8-pixel boundary aligned) */
static const int s_shield_x[4] = { 32, 96, 160, 224 };

/* =========================================================================
 * Module state
 * =========================================================================*/

static GameSettings s_gs;
static WaveConfig   s_wc;
static int          s_wave;
static long         s_score;
static long         s_hiscore;
static int          s_cpu_mode;
static int          s_lives;
static int          s_extra_life_awarded;
static int          s_game_over;
static int          s_wave_clear;
static int          s_score_dirty;

/* --- March ----------------------------------------------------------------*/
/* Frames between march steps, indexed by (55 - count)/5 + march_accel.
 * Entries correspond to ~55,50,45,40,35,30,25,20,15,10,5,1 invaders.     */
static const int s_march_delay[12] = {
    48, 44, 40, 36, 32, 28, 24, 20, 16, 12, 8, 1
};
static int s_march_timer;
static int s_march_dir;    /* +1 = right, -1 = left */
static int s_march_note;   /* 0-3, march beat index */

/* --- Invader grid ---------------------------------------------------------*/
#define GRID_ROWS_MAX   5
#define GRID_COLS_MAX  11

static unsigned int s_alive[GRID_ROWS_MAX];              /* bit i = col i alive */
static int          s_inv_anim[GRID_ROWS_MAX][GRID_COLS_MAX]; /* 0 or 1 */
static int          s_grid_base_x; /* pixel x of column 0, wave start      */
static int          s_grid_x;      /* current march offset from base (px)   */
static int          s_grid_y;      /* pixel y of top row                    */
static int          s_inv_cursor;  /* D1: flat index into grid              */
static int          s_inv_total;   /* living invader count                  */

/* Invader explosion overlay (one at a time) */
static int s_exp_row;    /* -1 = none */
static int s_exp_col;
static int s_exp_timer;

/* --- Player ---------------------------------------------------------------*/
static int s_player_x;      /* pixel x, byte-aligned                       */
static int s_plr_dead;      /* non-zero while exploding                    */
static int s_plr_frame;     /* explosion frame displayed (0..explo_frames) */
static int s_plr_exp_timer; /* frames remaining at current explosion frame */

/* --- Bullet (player) ------------------------------------------------------*/
static int s_bullet_x;   /* -1 = no bullet on screen                      */
static int s_bullet_y;
static int s_shot_count;  /* total shots fired this game (UFO trigger)     */

/* --- Bombs ----------------------------------------------------------------*/
#define MAX_BOMBS_ABS 5
static int s_bomb_x[MAX_BOMBS_ABS];    /* -1 = inactive                    */
static int s_bomb_y[MAX_BOMBS_ABS];
static int s_bomb_type[MAX_BOMBS_ABS]; /* BOMB_ROLLING/PLUNGER/SQUIGGLY    */
static int s_bomb_frame[MAX_BOMBS_ABS];/* animation frame 0-3              */
static int s_bomb_seq_idx;             /* next index into bomb_sequence[]   */
static int s_bomb_fire_timer;          /* cooldown between bomb launches    */

/* Firing sequence (D5: inactive types substituted with BOMB_ROLLING). */
static const int s_bomb_seq[6] = {
    BOMB_ROLLING, BOMB_SQUIGGLY, BOMB_ROLLING,
    BOMB_PLUNGER, BOMB_ROLLING,  BOMB_SQUIGGLY
};

/* --- UFO ------------------------------------------------------------------*/
static int s_ufo_x;         /* -1 = absent; otherwise pixel x              */
static int s_ufo_dir;       /* +1 or -1                                    */
static int s_ufo_exp_timer; /* >0 = explosion flash in progress            */
static int s_ufo_exp_x;     /* byte-aligned x where UFO was hit            */
static int s_ufo_score_val; /* score from this UFO hit                     */
static int s_ufo_frame_timer; /* countdown for next UFO appearance         */

/* UFO score by shot_count % 15 */
static const int s_ufo_scores[15] = {
    300, 300, 300,
    100, 100, 100,
    800, 800, 800,
    100, 100,
    300, 300,
    200, 200
};

/* --- Shields --------------------------------------------------------------*/
static int          s_shield_dmg[4];             /* STAGED: 0-3 damage state */
static unsigned long s_shield_pix[4][SHIELD_H];  /* PIXEL: 24-bit row mask   */
static int          s_shield_dirty[4];           /* redraw flag              */

/* =========================================================================
 * Local text drawing (mirrors TITLE.C / HISCORE.C pattern)
 * =========================================================================*/
static void game_draw_text(int x, int y, const char *s, unsigned char color)
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

/* Draw a non-negative long right-justified in a fixed 7-char field. */
static void game_draw_score_val(int x, int y, long v, unsigned char color)
{
    char buf[12];
    int  i = 11, len, pad;
    buf[11] = '\0';
    if (v == 0) { buf[--i] = '0'; }
    else { while (v > 0 && i > 0) { buf[--i] = (char)('0' + (int)(v % 10L)); v /= 10L; } }
    len = 11 - i;
    pad = 7 - len;
    x  += pad * FONT_W;
    game_draw_text(x, y, buf + i, color);
}

/* =========================================================================
 * Invader type helpers
 * =========================================================================*/

/* Row 0 = top. Returns 0=TypeA/cyan/30, 1=TypeB/magenta/20, 2=TypeC/green/10 */
static int inv_type(int row)
{
    if (s_gs.grid_rows == 5) {
        /* 5-row: top 1 = A, next 2 = B, bottom 2 = C */
        if (row == 0)                return 0;
        if (row <= 2)                return 1;
        return 2;
    } else {
        /* 4-row: top 1 = A, next 2 = B, bottom 1 = C */
        if (row == 0)                return 0;
        if (row <= 2)                return 1;
        return 2;
    }
}

static int inv_score(int row)
{
    int t = inv_type(row);
    if (t == 0) return 30;
    if (t == 1) return 20;
    return 10;
}

static unsigned char far *inv_frame_ptr(int row, int frame)
{
    int t = inv_type(row);
    if (t == 0) return g_cache.inv_a[frame & 1];
    if (t == 1) return g_cache.inv_b[frame & 1];
    return            g_cache.inv_c[frame & 1];
}

/* =========================================================================
 * Section 1 — Wave setup
 * =========================================================================*/

static void start_wave(int wave)
{
    static const WaveConfig wc_table[6] = {
        { 32, 1, 48, 2, 23 },
        { 40, 1, 44, 2, 23 },
        { 48, 2, 40, 3, 23 },
        { 56, 2, 36, 3, 23 },
        { 64, 2, 30, 4, 20 },
        { 72, 3, 24, 4, 18 }
    };
    int widx = (wave >= 6) ? 5 : wave - 1;
    int r, c, mask;

    s_wc = wc_table[widx];

    /* Invader grid base x: center the grid, byte-align. */
    s_grid_base_x = (SCREEN_WIDTH - s_gs.grid_cols * INV_STEP_X) / 2;
    s_grid_base_x &= ~7;
    s_grid_x      = 0;
    s_grid_y      = s_wc.grid_start_row;
    s_march_dir   = 1;
    s_march_timer = s_march_delay[0];
    s_march_note  = 0;
    s_inv_cursor  = 0;
    s_inv_total   = s_gs.grid_cols * s_gs.grid_rows;

    /* Populate alive bitmasks. */
    mask = 0;
    for (c = 0; c < s_gs.grid_cols; c++) mask |= (1 << c);
    for (r = 0; r < s_gs.grid_rows; r++) {
        s_alive[r] = (unsigned int)mask;
        for (c = 0; c < s_gs.grid_cols; c++) s_inv_anim[r][c] = 0;
    }
    for (r = s_gs.grid_rows; r < GRID_ROWS_MAX; r++) s_alive[r] = 0;

    /* Invader explosion cleared. */
    s_exp_row = s_exp_col = -1;
    s_exp_timer = 0;

    /* Player centred. */
    s_player_x = ((SCREEN_WIDTH - PLAYER_W) / 2) & ~7;
    s_plr_dead = s_plr_frame = s_plr_exp_timer = 0;

    /* Bullet / bombs cleared. */
    s_bullet_x = -1;
    s_bomb_seq_idx = 0;
    s_bomb_fire_timer = 0;
    for (r = 0; r < MAX_BOMBS_ABS; r++) s_bomb_x[r] = -1;

    /* UFO. */
    s_ufo_x = -1;
    s_ufo_dir = 1;
    s_ufo_exp_timer = 0;
    s_ufo_exp_x = -UFO_W;
    s_ufo_frame_timer = s_gs.ufo_interval;

    /* Shields: reset only on wave 1. */
    if (wave == 1) {
        int i;
        for (i = 0; i < 4; i++) {
            s_shield_dmg[i] = 0;
            for (r = 0; r < SHIELD_H; r++)
                s_shield_pix[i][r] = 0x00FFFFFFUL;
            s_shield_dirty[i] = 1;
            g_stats.shield_pixels_total += SHIELD_W * SHIELD_H;
        }
    }

    s_wave_clear  = 0;
    s_score_dirty = 1;
}

/* =========================================================================
 * Section 2 — March delay calculation (D2)
 * =========================================================================*/

static int calc_march_delay(void)
{
    int count = s_inv_total;
    int idx;

    if (count <= 0)  count = 1;
    if (count >= 55) idx = 0;
    else             idx = (55 - count + 4) / 5;
    if (idx > 11) idx = 11;

    /* D2: march_accel shifts the index (faster speed). */
    idx += s_gs.march_accel;
    if (idx > 11) idx = 11;

    /* Last-life boost. */
    if (s_lives == 1 && idx < 11) idx++;

    return s_march_delay[idx];
}

/* =========================================================================
 * Section 3 — Shield hit helpers
 * =========================================================================*/

static int shield_at(int x, int y, int *si)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (x >= s_shield_x[i] && x < s_shield_x[i] + SHIELD_W &&
            y >= SHIELD_Y       && y < SHIELD_Y + SHIELD_H) {
            *si = i;
            return 1;
        }
    }
    return 0;
}

/* Player bullet erodes from bottom of shield upward. Returns 1 if it hit a
 * shield (caller consumes the bullet), 0 otherwise. */
static int shield_bullet_erode(int bx, int by)
{
    int si;
    if (!shield_at(bx, by, &si)) return 0;

    if (s_gs.shield_mode == SHIELD_PIXEL) {
        int row = by - SHIELD_Y;
        int bit = bx - s_shield_x[si];
        if (row >= 0 && row < SHIELD_H && bit >= 0 && bit < SHIELD_W) {
            if (s_shield_pix[si][row] & (1UL << (SHIELD_W - 1 - bit)))
                g_stats.shield_pixels_eroded++;
            s_shield_pix[si][row] &= ~(1UL << (SHIELD_W - 1 - bit));
            /* Erode adjacent pixels for a realistic 3-pixel wide bullet. */
            if (bit > 0) {
                if (s_shield_pix[si][row] & (1UL << (SHIELD_W - bit)))
                    g_stats.shield_pixels_eroded++;
                s_shield_pix[si][row] &= ~(1UL << (SHIELD_W - bit));
            }
            if (bit < SHIELD_W-1) {
                if (s_shield_pix[si][row] & (1UL << (SHIELD_W - 2 - bit)))
                    g_stats.shield_pixels_eroded++;
                s_shield_pix[si][row] &= ~(1UL << (SHIELD_W - 2 - bit));
            }
        }
    } else {
        if (s_shield_dmg[si] < 3) s_shield_dmg[si]++;
    }
    s_shield_dirty[si] = 1;
    sound_play(SND_SHIELD_HIT, SPRI_LOW, s_shield_x[si] + SHIELD_W / 2);
    return 1;
}

/* Invader bomb erodes from top of shield downward. Returns 1 if it hit a
 * shield (caller consumes the bomb), 0 otherwise. */
static int shield_bomb_erode(int bx, int by)
{
    int si;
    if (!shield_at(bx, by, &si)) return 0;

    if (s_gs.shield_mode == SHIELD_PIXEL) {
        int row = by - SHIELD_Y;
        int bit = bx - s_shield_x[si];
        if (row >= 0 && row < SHIELD_H && bit >= 0 && bit < SHIELD_W) {
            if (s_shield_pix[si][row] & (1UL << (SHIELD_W - 1 - bit)))
                g_stats.shield_pixels_eroded++;
            s_shield_pix[si][row] &= ~(1UL << (SHIELD_W - 1 - bit));
            if (bit > 0) {
                if (s_shield_pix[si][row] & (1UL << (SHIELD_W - bit)))
                    g_stats.shield_pixels_eroded++;
                s_shield_pix[si][row] &= ~(1UL << (SHIELD_W - bit));
            }
            if (bit < SHIELD_W-1) {
                if (s_shield_pix[si][row] & (1UL << (SHIELD_W - 2 - bit)))
                    g_stats.shield_pixels_eroded++;
                s_shield_pix[si][row] &= ~(1UL << (SHIELD_W - 2 - bit));
            }
        }
    } else {
        if (s_shield_dmg[si] < 3) s_shield_dmg[si]++;
    }
    s_shield_dirty[si] = 1;
    sound_play(SND_SHIELD_HIT, SPRI_LOW, s_shield_x[si] + SHIELD_W / 2);
    return 1;
}

/* Invader marching through a shield row destroys it. */
static void check_invaders_in_shields(void)
{
    int r, c, px, py, i;
    for (r = 0; r < s_gs.grid_rows; r++) {
        for (c = 0; c < s_gs.grid_cols; c++) {
            if (!(s_alive[r] & (1 << c))) continue;
            px = s_grid_base_x + s_grid_x + c * INV_STEP_X;
            py = s_grid_y + r * INV_STEP_Y;
            /* If invader's bottom row overlaps shield band, destroy that shield. */
            if (py + INV_H > SHIELD_Y && py < SHIELD_Y + SHIELD_H) {
                for (i = 0; i < 4; i++) {
                    if (px + INV_W > s_shield_x[i] &&
                        px < s_shield_x[i] + SHIELD_W) {
                        s_shield_dmg[i] = 3;
                        if (s_gs.shield_mode == SHIELD_PIXEL) {
                            int row;
                            for (row = 0; row < SHIELD_H; row++)
                                s_shield_pix[i][row] = 0;
                        }
                        s_shield_dirty[i] = 1;
                    }
                }
            }
        }
    }
}

/* =========================================================================
 * Section 4 — Draw routines
 * =========================================================================*/

static void draw_invaders(void)
{
    int r, c, px, py, f;
    unsigned char far *src;

    for (r = 0; r < s_gs.grid_rows; r++) {
        for (c = 0; c < s_gs.grid_cols; c++) {
            if (!(s_alive[r] & (1 << c))) continue;
            px  = s_grid_base_x + s_grid_x + c * INV_STEP_X;
            py  = s_grid_y + r * INV_STEP_Y;
            f   = s_inv_anim[r][c];
            src = inv_frame_ptr(r, f);
            ega_dirty_add(px, py, INV_W_BYTES * 8, INV_H);
            ega_blit_planar(px, py, INV_W_BYTES, INV_H, src);
        }
    }

    /* Invader explosion overlay */
    if (s_exp_timer > 0) {
        px = s_grid_base_x + s_grid_x + s_exp_col * INV_STEP_X;
        py = s_grid_y + s_exp_row * INV_STEP_Y;
        ega_dirty_add(px, py, INV_W_BYTES * 8, INV_H);
        ega_blit_planar(px, py, INV_W_BYTES, INV_H, g_cache.inv_explode);
    }
}

static void draw_player(void)
{
    if (s_plr_dead) {
        unsigned char far *src = g_cache.plr_explode[s_plr_frame];
        ega_dirty_add(s_player_x, PLAYER_Y, PLAYER_W_BYTES * 8, PLAYER_H);
        ega_blit_planar(s_player_x, PLAYER_Y, PLAYER_W_BYTES, PLAYER_H, src);
    } else {
        ega_dirty_add(s_player_x, PLAYER_Y, PLAYER_W_BYTES * 8, PLAYER_H);
        ega_blit_planar(s_player_x, PLAYER_Y, PLAYER_W_BYTES, PLAYER_H,
                        g_cache.player);
    }
}

static void draw_bullet(void)
{
    if (s_bullet_x < 0) return;
    ega_dirty_add(s_bullet_x, s_bullet_y, BULLET_W_BYTES * 8, BULLET_H);
    ega_blit_planar(s_bullet_x, s_bullet_y, BULLET_W_BYTES, BULLET_H,
                    g_cache.bullet);
}

static void draw_bombs(void)
{
    int i, bx, by;
    unsigned char far *src;
    for (i = 0; i < s_gs.max_bombs; i++) {
        if (s_bomb_x[i] < 0) continue;
        bx = s_bomb_x[i] & ~7;   /* byte-align for blit */
        by = s_bomb_y[i];
        switch (s_bomb_type[i]) {
        case BOMB_ROLLING:   src = g_cache.bomb_rolling[s_bomb_frame[i]];   break;
        case BOMB_PLUNGER:   src = g_cache.bomb_plunger[s_bomb_frame[i]];   break;
        default:             src = g_cache.bomb_squiggly[s_bomb_frame[i]];  break;
        }
        ega_dirty_add(bx, by, BOMB_W_BYTES * 8, BOMB_H);
        ega_blit_planar(bx, by, BOMB_W_BYTES, BOMB_H, src);
    }
}

static void draw_ufo(void)
{
    if (s_ufo_exp_timer > 0) {
        /* Score flash: draw explosion frame at the position of the hit. */
        ega_dirty_add(s_ufo_exp_x, UFO_Y, UFO_W_BYTES * 8, UFO_H);
        ega_blit_planar(s_ufo_exp_x, UFO_Y, UFO_W_BYTES, UFO_H, g_cache.ufo[1]);
    } else if (s_ufo_x >= 0) {
        int blit_x = s_ufo_x & ~7;
        ega_dirty_add(blit_x, UFO_Y, UFO_W_BYTES * 8, UFO_H);
        ega_blit_planar(blit_x, UFO_Y, UFO_W_BYTES, UFO_H, g_cache.ufo[0]);
    }
}

static void draw_shields(void)
{
    int i, r, bit;
    for (i = 0; i < 4; i++) {
        if (!s_shield_dirty[i]) continue;
        /* Erase old shield area first. */
        ega_fill_rect(s_shield_x[i], SHIELD_Y, SHIELD_W, SHIELD_H, EGA_BLACK);
        if (s_gs.shield_mode == SHIELD_STAGED) {
            if (s_shield_dmg[i] < 3) {
                ega_blit_planar(s_shield_x[i], SHIELD_Y,
                                (SHIELD_W + 7) / 8, SHIELD_H,
                                g_cache.shield[s_shield_dmg[i]]);
            }
            /* State 3 = destroyed: already erased above. */
        } else {
            /* SHIELD_PIXEL: draw bit by bit */
            for (r = 0; r < SHIELD_H; r++) {
                unsigned long mask = s_shield_pix[i][r];
                for (bit = SHIELD_W - 1; bit >= 0; bit--) {
                    if (mask & (1UL << (SHIELD_W - 1 - bit)))
                        ega_put_pixel(s_shield_x[i] + bit, SHIELD_Y + r, EGA_GREEN);
                }
            }
        }
        s_shield_dirty[i] = 0;
    }
}

static void draw_ground(void)
{
    ega_fill_rect(0, GROUND_Y, SCREEN_WIDTH, 1, EGA_WHITE);
}

static void draw_hud(void)
{
    char buf[12];
    int  i, ix;

    /* Clear HUD rows */
    ega_fill_rect(0, HUD_Y, SCREEN_WIDTH, FONT_H, EGA_BLACK);
    ega_fill_rect(0, LIVES_Y, SCREEN_WIDTH, LIVES_H + 2, EGA_BLACK);

    /* Wave number top-left */
    buf[0] = 'W';
    buf[1] = (char)('0' + (s_wave / 10 % 10));
    buf[2] = (char)('0' + s_wave % 10);
    buf[3] = '\0';
    if (s_wave < 10) { buf[0] = 'W'; buf[1] = (char)('0' + s_wave); buf[2] = '\0'; }
    game_draw_text(0, HUD_Y, buf, EGA_WHITE);

    /* Score top-center */
    game_draw_text(112, HUD_Y, "SCORE", EGA_WHITE);
    game_draw_score_val(112, HUD_Y + FONT_H + 2, s_score, EGA_WHITE);

    /* Hi-score top-right */
    {
        long hi = (s_score > s_hiscore) ? s_score : s_hiscore;
        game_draw_text(216, HUD_Y, "HI", EGA_WHITE);
        game_draw_score_val(216, HUD_Y + FONT_H + 2, hi, EGA_WHITE);
    }

    /* Lives icons bottom-left */
    ix = 0;
    for (i = 0; i < s_lives && i < 6; i++) {
        ega_blit_planar(ix, LIVES_Y, LIVES_W_BYTES, LIVES_H, g_cache.lives_icon);
        ix += LIVES_W + 4;
    }
}

/* =========================================================================
 * Section 5 — Collision detection
 * =========================================================================*/

static int boxes_overlap(int ax, int ay, int aw, int ah,
                         int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

/* Returns 1 if bullet hit an invader (removes it). */
static int check_bullet_vs_invaders(void)
{
    int r, c, px, py;
    if (s_bullet_x < 0) return 0;
    for (r = 0; r < s_gs.grid_rows; r++) {
        for (c = 0; c < s_gs.grid_cols; c++) {
            if (!(s_alive[r] & (1 << c))) continue;
            px = s_grid_base_x + s_grid_x + c * INV_STEP_X;
            py = s_grid_y + r * INV_STEP_Y;
            if (boxes_overlap(s_bullet_x, s_bullet_y, BULLET_W, BULLET_H,
                              px, py, INV_W, INV_H)) {
                /* Hit */
                s_alive[r] &= ~(1u << c);
                s_inv_total--;
                g_stats.invaders_killed++;
                s_exp_row = r; s_exp_col = c; s_exp_timer = 10;
                s_score += inv_score(r);
                s_score_dirty = 1;
                if (s_score >= s_gs.extra_life_score && !s_extra_life_awarded) {
                    s_lives++;
                    s_extra_life_awarded = 1;
                    g_stats.extra_lives_earned++;
                    s_score_dirty = 1;
                }
                if (s_inv_total <= 0) s_wave_clear = 1;
                s_bullet_x = -1;
                sound_play(SND_INV_EXPLODE, SPRI_MED,
                           s_grid_base_x + s_grid_x + c * INV_STEP_X);
                return 1;
            }
        }
    }
    return 0;
}

static int check_bullet_vs_ufo(void)
{
    int blit_x;
    if (s_bullet_x < 0 || s_ufo_x < 0) return 0;
    blit_x = s_ufo_x & ~7;
    if (boxes_overlap(s_bullet_x, s_bullet_y, BULLET_W, BULLET_H,
                      blit_x, UFO_Y, UFO_W, UFO_H)) {
        s_ufo_score_val = s_ufo_scores[s_shot_count % 15];
        s_score += s_ufo_score_val;
        g_stats.ufos_hit++;
        s_score_dirty = 1;
        s_ufo_exp_timer = 30;
        s_ufo_exp_x = blit_x;
        s_ufo_x = -1;
        s_bullet_x = -1;
        sound_play(SND_UFO_HIT, SPRI_HIGH, blit_x + UFO_W / 2);
        return 1;
    }
    return 0;
}

static void check_bullet_vs_bombs(void)
{
    int i;
    if (s_bullet_x < 0) return;
    for (i = 0; i < s_gs.max_bombs; i++) {
        if (s_bomb_x[i] < 0) continue;
        if (boxes_overlap(s_bullet_x, s_bullet_y, BULLET_W, BULLET_H,
                          s_bomb_x[i], s_bomb_y[i], BOMB_W, BOMB_H)) {
            s_bomb_x[i] = -1;
            s_bullet_x  = -1;
            return;
        }
    }
}

static void check_bullet_vs_shields(void)
{
    if (s_bullet_x < 0) return;
    if (shield_bullet_erode(s_bullet_x, s_bullet_y))
        s_bullet_x = -1;
}

static void check_bomb_vs_player(int slot)
{
    if (s_bomb_x[slot] < 0 || s_plr_dead) return;
    if (boxes_overlap(s_bomb_x[slot], s_bomb_y[slot], BOMB_W, BOMB_H,
                      s_player_x, PLAYER_Y, PLAYER_W, PLAYER_H)) {
        s_bomb_x[slot] = -1;
        /* Trigger player death */
        s_plr_dead = 1;
        s_plr_frame = 0;
        s_plr_exp_timer = 6;
        sound_play(SND_PLR_DEATH, SPRI_CRITICAL, 160);
    }
}

/* =========================================================================
 * Section 6 — D1 sequential invader cursor
 * =========================================================================*/

static int is_bottom_of_col(int row, int col)
{
    int r;
    for (r = row + 1; r < s_gs.grid_rows; r++)
        if (s_alive[r] & (1 << col)) return 0;
    return 1;
}

/* D5: resolve bomb type — substitute BOMB_ROLLING for inactive types. */
static int resolve_bomb_type(int raw)
{
    if (raw == BOMB_SQUIGGLY && s_gs.bomb_types < 3) return BOMB_ROLLING;
    if (raw == BOMB_PLUNGER  && s_gs.bomb_types < 2) return BOMB_ROLLING;
    return raw;
}

static int active_bomb_count(void)
{
    int i, n = 0;
    for (i = 0; i < s_gs.max_bombs; i++) if (s_bomb_x[i] >= 0) n++;
    return n;
}

static void maybe_fire_bomb(int row, int col)
{
    int slot, type, btype, inv_px;

    if (s_bomb_fire_timer > 0) return;
    if (active_bomb_count() >= s_gs.max_bombs) return;

    /* Find free slot */
    for (slot = 0; slot < s_gs.max_bombs; slot++)
        if (s_bomb_x[slot] < 0) break;
    if (slot >= s_gs.max_bombs) return;

    type  = s_bomb_seq[s_bomb_seq_idx % 6];
    btype = resolve_bomb_type(type);
    s_bomb_seq_idx++;

    inv_px = s_grid_base_x + s_grid_x + col * INV_STEP_X;

    /* PLUNGER: only fire if roughly above player. */
    if (btype == BOMB_PLUNGER) {
        if (abs(inv_px + INV_W / 2 - (s_player_x + PLAYER_W / 2)) > 40)
            return;
    }

    s_bomb_x[slot]    = inv_px + INV_W / 2 - BOMB_W / 2;
    s_bomb_y[slot]    = s_grid_y + row * INV_STEP_Y + INV_H;
    s_bomb_type[slot] = btype;
    s_bomb_frame[slot]= 0;
    s_bomb_fire_timer = s_wc.bomb_fire_rate;
}

static void process_inv_cursor(void)
{
    int processed = 0;
    int flat = s_inv_cursor;
    int max_flat = s_gs.grid_rows * s_gs.grid_cols;
    int r, c, tries = max_flat * 2;

    while (processed < s_gs.invaders_per_frame && tries-- > 0) {
        if (flat >= max_flat) flat = 0;
        r = flat / s_gs.grid_cols;
        c = flat % s_gs.grid_cols;
        flat++;
        if (r >= s_gs.grid_rows) continue;
        if (!(s_alive[r] & (1 << c))) continue;

        /* Toggle animation frame. */
        s_inv_anim[r][c] ^= 1;

        /* Fire opportunity. */
        if (s_bomb_fire_timer <= 0 && is_bottom_of_col(r, c))
            maybe_fire_bomb(r, c);

        processed++;
    }
    s_inv_cursor = flat;
    if (s_inv_cursor >= max_flat) s_inv_cursor = 0;
}

/* =========================================================================
 * Section 7 — Per-frame update functions
 * =========================================================================*/

static void update_march(void)
{
    int r, c, leftmost, rightmost, new_x, left_px, right_px;

    s_march_timer--;
    if (s_march_timer > 0) return;

    g_stats.march_events++;   /* one grid march step */

    /* March beat sound. */
    sound_play(SND_MARCH0 + (s_march_note & 3), SPRI_BG, 160);
    s_march_note = (s_march_note + 1) & 3;

    /* Find alive column span. */
    leftmost  = s_gs.grid_cols;
    rightmost = -1;
    for (r = 0; r < s_gs.grid_rows; r++) {
        for (c = 0; c < s_gs.grid_cols; c++) {
            if (!(s_alive[r] & (1 << c))) continue;
            if (c < leftmost)  leftmost  = c;
            if (c > rightmost) rightmost = c;
        }
    }
    if (rightmost < 0) { s_march_timer = 1; return; } /* all dead */

    new_x    = s_grid_x + s_march_dir * s_wc.march_step * 8;
    left_px  = s_grid_base_x + new_x + leftmost  * INV_STEP_X;
    right_px = s_grid_base_x + new_x + rightmost * INV_STEP_X + INV_W - 1;

    if (right_px >= SCREEN_WIDTH || left_px < 0) {
        /* Boundary hit: step down and reverse. */
        s_march_dir = -s_march_dir;
        s_grid_y   += s_wc.march_step * 8;

        /* Ground invasion → instant game over. */
        if (s_grid_y + s_gs.grid_rows * INV_STEP_Y >= PLAYER_Y)
            s_game_over = 1;

        check_invaders_in_shields();
    } else {
        s_grid_x = new_x;
    }

    s_march_timer = calc_march_delay();
}

static void update_player(void)
{
    int spd = 2;
    if (inp_pressed(KEY_LEFT)) {
        s_player_x -= spd;
        if (s_player_x < 0) s_player_x = 0;
        s_player_x &= ~7;
    }
    if (inp_pressed(KEY_RIGHT)) {
        s_player_x += spd;
        if (s_player_x > SCREEN_WIDTH - PLAYER_W)
            s_player_x = SCREEN_WIDTH - PLAYER_W;
        s_player_x &= ~7;
    }
    if (inp_just_pressed(KEY_FIRE) && s_bullet_x < 0) {
        s_bullet_x = (s_player_x + PLAYER_W / 2) & ~7;
        s_bullet_y = PLAYER_Y - BULLET_H;
        s_shot_count++;
        g_stats.shots_fired++;
        sound_play(SND_FIRE, SPRI_HIGH, s_bullet_x);
    }
}

static void update_bullet(void)
{
    if (s_bullet_x < 0) return;

    s_bullet_y -= 4;  /* 4 px/frame upward */
    if (s_bullet_y < 0) { s_bullet_x = -1; return; }

    /* Collision checks (in priority order). */
    if (check_bullet_vs_ufo())        return;
    if (check_bullet_vs_invaders())   return;
    check_bullet_vs_shields();
    if (s_bullet_x < 0) return;
    check_bullet_vs_bombs();
}

static void update_bombs(void)
{
    int i, bx, by;

    for (i = 0; i < s_gs.max_bombs; i++) {
        if (s_bomb_x[i] < 0) continue;

        s_bomb_y[i] += s_wc.bomb_speed;
        s_bomb_frame[i] = (s_bomb_frame[i] + 1) & 3;

        bx = s_bomb_x[i];
        by = s_bomb_y[i];

        /* Off bottom of screen. */
        if (by >= PLAYER_Y + PLAYER_H) {
            s_bomb_x[i] = -1;
            g_stats.bombs_dodged++;
            continue;
        }

        /* vs player */
        check_bomb_vs_player(i);
        if (s_bomb_x[i] < 0) continue;

        /* vs shields */
        if (shield_bomb_erode(bx, by)) { s_bomb_x[i] = -1; continue; }
    }

    if (s_bomb_fire_timer > 0) s_bomb_fire_timer--;
}

static void update_ufo(void)
{
    if (!s_gs.ufo_enabled) return;

    /* Explosion countdown */
    if (s_ufo_exp_timer > 0) {
        s_ufo_exp_timer--;
        return;
    }

    if (s_ufo_x < 0) {
        /* Check trigger conditions */
        int trigger = (s_shot_count == s_gs.ufo_shot_trigger) ||
                      (s_shot_count > s_gs.ufo_shot_trigger &&
                       (s_shot_count - s_gs.ufo_shot_trigger) % 15 == 0);
        if (!trigger) return;

        /* Spawn UFO */
        s_ufo_dir = (s_ufo_dir == 1) ? -1 : 1;  /* alternate direction */
        if (s_ufo_dir == 1)
            s_ufo_x = -UFO_W;
        else
            s_ufo_x = SCREEN_WIDTH;
        sound_play(SND_UFO_PASS, SPRI_BG, SCREEN_WIDTH / 2);
        return;
    }

    /* Move */
    s_ufo_x += s_ufo_dir * 2;

    /* Exit screen */
    if (s_ufo_x > SCREEN_WIDTH || s_ufo_x < -UFO_W) {
        s_ufo_x = -1;
    }
}

static void update_plr_explosion(void)
{
    s_plr_exp_timer--;
    if (s_plr_exp_timer > 0) return;

    s_plr_frame++;
    if (s_plr_frame < s_gs.explosion_frames) {
        s_plr_exp_timer = 6;
        return;
    }

    /* Explosion finished. */
    s_lives--;
    s_score_dirty = 1;

    if (s_lives <= 0) {
        s_game_over = 1;
        return;
    }

    /* Brief pause before respawn (~70 frames = 1s). */
    {
        int i;
        for (i = 0; i < 70; i++) ega_wait_vsync();
    }

    /* Respawn. */
    s_player_x = ((SCREEN_WIDTH - PLAYER_W) / 2) & ~7;
    s_plr_dead = s_plr_frame = s_plr_exp_timer = 0;
    s_bullet_x = -1;

    /* Clear any bombs to prevent immediate re-death. */
    { int j; for (j = 0; j < MAX_BOMBS_ABS; j++) s_bomb_x[j] = -1; }

    /* Redraw lives. */
    draw_hud();
}

/* =========================================================================
 * Section 8 — Erase dirty rects
 * =========================================================================*/

static void erase_dirty(void)
{
    int i, n = ega_dirty_count();
    DirtyRect *dr;
    for (i = 0; i < n; i++) {
        dr = ega_dirty_get(i);
        ega_fill_rect(dr->x, dr->y, dr->w, dr->h, EGA_BLACK);
    }
    ega_dirty_reset();
}

/* =========================================================================
 * Section 9 — Wave clear and game over sequences
 * =========================================================================*/

static void wave_clear_sequence(void)
{
    int i;
    sound_play(SND_LEVEL_CLEAR, SPRI_CRITICAL, 160);
    for (i = 0; i < 140; i++) ega_wait_vsync();  /* ~2s */
}

static void game_over_sequence(void)
{
    int rank, i;

    /* "GAME OVER" centred in red. */
    ega_fill_rect(80, 88, 160, FONT_H + 4, EGA_BLACK);
    game_draw_text(96, 92, "GAME OVER", EGA_RED);

    /* ~2s pause. */
    for (i = 0; i < 140; i++) ega_wait_vsync();

    /* Hi-score check. */
    rank = hiscore_check_rank(s_score, s_cpu_mode);
    if (rank >= 0)
        hiscore_entry(rank, s_score, s_cpu_mode);
}

/* =========================================================================
 * Section 10 — Main entry point
 * =========================================================================*/

long game_run(const GameSettings *settings, long hiscore, int cpu_mode)
{
    s_gs      = *settings;
    s_hiscore  = hiscore;
    s_cpu_mode = cpu_mode;
    s_score    = 0;
    s_lives    = 3;
    s_wave     = 1;
    s_shot_count = 0;
    s_extra_life_awarded = 0;
    s_game_over  = 0;
    s_wave_clear = 0;

    g_stats.games_played++;

    start_wave(1);

    /* Initial draw. */
    ega_clear(EGA_BLACK);
    draw_ground();
    draw_shields();
    draw_hud();
    s_score_dirty = 0;

    ega_dirty_reset();

    while (!s_game_over) {
        /* If retrace is already underway after a full frame of work, the
         * previous frame overran its vsync window -- count a miss. */
        if (ega_vsync_active()) g_stats.vsync_misses++;
        ega_wait_vsync();
        inp_clear_edge();

        /* ESC quits to DOS (cleanup() then shows the stats screen). */
        if (inp_pressed(KEY_ESC)) { g_quit_requested = 1; return s_score; }

        stats_sample_fps(s_wave);
        if (s_score > g_stats.best_score) g_stats.best_score = (int)s_score;

        if (!s_plr_dead) {
            update_player();
            update_bullet();
            update_bombs();
            update_ufo();
            update_march();
            process_inv_cursor();
        } else {
            /* Freeze: only advance explosion timer. */
            update_plr_explosion();
        }

        /* Erase previous frame dirty rects, then redraw. */
        erase_dirty();
        draw_invaders();
        draw_player();
        draw_bullet();
        draw_bombs();
        draw_ufo();
        draw_shields();
        if (s_score_dirty) {
            draw_hud();
            s_score_dirty = 0;
        }

        sound_update();

        if (s_wave_clear && !s_game_over) {
            wave_clear_sequence();
            s_wave++;
            g_stats.waves_cleared++;
            start_wave(s_wave);
            ega_clear(EGA_BLACK);
            draw_ground();
            draw_shields();
            draw_hud();
            s_score_dirty = 0;
            ega_dirty_reset();
        }
    }

    game_over_sequence();
    return s_score;
}

/* =========================================================================
 * GAME_TEST -- text-mode self-test; validates settings tables and D2 math.
 *
 * Compile:
 *   wcc -ml -zf -DGAME_TEST GAME.C
 *   wcc -ml -zf STATS.C        (GAME now references g_stats)
 *   wlink system dos file { GAME.obj STATS.obj } name GAMETEST
 * =========================================================================*/
#ifdef GAME_TEST

#include <stdio.h>

static void test_settings(const GameSettings *gs, const char *name)
{
    int i;
    printf("--- %s ---\n", name);
    printf("  grid %dx%d  bombs_max=%d  ufo=%d  accel=%d  ipf=%d  btypes=%d\n",
           gs->grid_cols, gs->grid_rows, gs->max_bombs,
           gs->ufo_enabled, gs->march_accel, gs->invaders_per_frame, gs->bomb_types);
    printf("  March delays at counts 55,30,10,1:\n    ");
    {
        int counts[4] = { 55, 30, 10, 1 };
        for (i = 0; i < 4; i++) {
            int cnt = counts[i];
            int idx = (cnt >= 55) ? 0 : (55 - cnt + 4) / 5;
            if (idx > 11) idx = 11;
            idx += gs->march_accel;
            if (idx > 11) idx = 11;
            printf("count=%2d->delay=%2d  ", cnt,
                   s_march_delay[idx < 0 ? 0 : idx > 11 ? 11 : idx]);
        }
    }
    printf("\n");

    /* Bomb sequence resolution */
    printf("  Bomb sequence (D5):\n    ");
    {
        int seq[6] = { BOMB_ROLLING, BOMB_SQUIGGLY, BOMB_ROLLING,
                       BOMB_PLUNGER, BOMB_ROLLING, BOMB_SQUIGGLY };
        const char *names[3] = { "ROLLING", "PLUNGER", "SQUIGGLY" };
        for (i = 0; i < 6; i++) {
            int raw = seq[i];
            int resolved = raw;
            if (raw == BOMB_SQUIGGLY && gs->bomb_types < 3) resolved = BOMB_ROLLING;
            if (raw == BOMB_PLUNGER  && gs->bomb_types < 2) resolved = BOMB_ROLLING;
            printf("%s->%s  ", names[raw], names[resolved]);
        }
    }
    printf("\n");
}

int main(void)
{
    static const GameSettings gs_386 = {
        8, 4, 1, 0, 0, SHIELD_STAGED, 2, STAR_NONE, 0, 1, 1, 2000, 30
    };
    static const GameSettings gs_486 = {
        11, 4, 3, 1, 800, SHIELD_STAGED, 4, STAR_STATIC, 1, 2, 2, 1500, 23
    };
    static const GameSettings gs_p66 = {
        11, 5, 5, 1, 500, SHIELD_PIXEL, 8, STAR_SCROLL, 2, 55, 3, 1500, 23
    };

    puts("GAME_TEST");
    puts("---------");
    test_settings(&gs_386, "CPU_386");
    test_settings(&gs_486, "CPU_486");
    test_settings(&gs_p66, "CPU_P66");
    puts("Done.");
    return 0;
}

#endif /* GAME_TEST */
