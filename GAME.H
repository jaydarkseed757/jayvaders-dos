#ifndef GAME_H
#define GAME_H

/*
 * GAME.H -- full arcade game loop (build step 8).
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * GameSettings is populated by apply_cpu_mode() in INVADERS.C and passed to
 * game_run(). GAME.C owns all gameplay state for one full game session.
 */

#include "TITLE.H"   /* CPU_*, SHIELD_* constants; pulls in HISCORE.H */

/* -------------------------------------------------------------------------
 * Bomb type identifiers (used in bomb_sequence and s_bomb_type[])
 * -------------------------------------------------------------------------*/
#define BOMB_ROLLING   0   /* random column, rolling animation              */
#define BOMB_PLUNGER   1   /* targets player X, plunger animation           */
#define BOMB_SQUIGGLY  2   /* purely random, most dangerous                 */

/* -------------------------------------------------------------------------
 * Per-CPU-mode settings (populated by apply_cpu_mode in INVADERS.C)
 * -------------------------------------------------------------------------*/
typedef struct {
    int grid_cols;          /* 8 (386) or 11 (486/P66)                     */
    int grid_rows;          /* 4 (386/486) or 5 (P66)                      */
    int max_bombs;          /* max simultaneous bombs: 1/3/5                */
    int ufo_enabled;        /* 0 = no UFO (386)                            */
    int ufo_interval;       /* frames between UFO appearances               */
    int shield_mode;        /* SHIELD_STAGED or SHIELD_PIXEL                */
    int explosion_frames;   /* player explosion frames: 2/4/8               */
    int title_starfield;    /* STAR_NONE/STATIC/SCROLL                      */
    int march_accel;        /* D2: index offset into march_delay_table      */
    int invaders_per_frame; /* D1: sequential cursor stride per frame       */
    int bomb_types;         /* 1=ROLLING only, 2=+PLUNGER, 3=+SQUIGGLY    */
    int extra_life_score;   /* score threshold for bonus life               */
    int ufo_shot_trigger;   /* shot count at which first UFO appears        */
} GameSettings;

/* -------------------------------------------------------------------------
 * Per-wave parameters
 * -------------------------------------------------------------------------*/
typedef struct {
    int grid_start_row;     /* pixel y of top invader row at wave start     */
    int bomb_speed;         /* pixels per frame, downward                   */
    int bomb_fire_rate;     /* minimum frames between bomb launches         */
    int march_step;         /* 8-pixel units per march tick sideways        */
    int ufo_shot_trigger;   /* wave override for UFO shot trigger           */
} WaveConfig;

/* -------------------------------------------------------------------------
 * Public API (called by INVADERS.C)
 * -------------------------------------------------------------------------*/

/*
 * Run one full game until game over (including hi-score entry if qualified).
 * Requires EGA mode 0Dh active, inp_install() called, and g_cache loaded.
 *
 * settings : populated GameSettings for the current CPU mode.
 * hiscore  : current best score to show in the HUD.
 * cpu_mode : CPU_386/486/P66, forwarded to hiscore_check_rank/hiscore_entry.
 *
 * Returns the final score.
 */
long game_run(const GameSettings *settings, long hiscore, int cpu_mode);

#endif /* GAME_H */
