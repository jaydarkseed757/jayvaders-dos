#ifndef STATS_H
#define STATS_H

/*
 * STATS.H -- runtime session statistics + exit summary screen.
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * One global SessionStats g_stats accumulates counters via one-line hooks in
 * the render / gameplay code. stats_display() draws a text-mode summary on
 * exit (called from the atexit cleanup, after text mode is restored).
 *
 * No floating point, no stdio -- all output via DOS int 21h AH=09h.
 */

/* -------------------------------------------------------------------------
 * Build info (baked in at compile time)
 * -------------------------------------------------------------------------*/
#define BUILD_VERSION  "1.0.0"
#define BUILD_DATE     __DATE__
#define BUILD_COMPILER "Open Watcom 2.0"
#define BUILD_MODEL    "Medium -4"

/* -------------------------------------------------------------------------
 * Session statistics
 * -------------------------------------------------------------------------*/
typedef struct {
    /* Session */
    long         frames_rendered;
    long         start_ticks;       /* PIT ticks at launch */
    long         exit_ticks;        /* PIT ticks at exit */
    int          fps_min;           /* lowest FPS recorded */
    int          fps_min_wave;      /* wave number when dip occurred */
    long         fps_accumulator;   /* sum of per-second frame counts */
    int          fps_sample_count;  /* number of samples taken */

    /* Gameplay */
    int          games_played;
    int          waves_cleared;
    int          invaders_killed;
    int          ufos_hit;
    long         shots_fired;
    long         bombs_dodged;      /* bombs that exit bottom without hitting player */
    int          extra_lives_earned;
    int          best_score;
    int          shield_pixels_eroded;
    int          shield_pixels_total;

    /* Performance */
    long         vsync_misses;      /* frames that overran one vsync period */
    long         sprite_cache_hits; /* every memcpy blit increments this */
    long         march_events;      /* every invader grid step increments this */

    /* Memory -- populated at exit before display */
    unsigned int conventional_free_kb;
    unsigned int sprite_cache_kb;
    unsigned int pcm_samples_kb;
    unsigned int near_data_kb;
    unsigned int peak_heap_kb;
} SessionStats;

extern SessionStats g_stats;

/* -------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/

/* Zero counters; record start_ticks; seed fps_min sentinel. */
void stats_init(void);

/* Read the BIOS/PIT tick counter (int 1Ah AH=00h) into *ticks. */
void stats_read_pit(long *ticks);

/* Call once per frame; records a sample roughly once per second. */
void stats_sample_fps(int wave);

/* Store memory KB figures; query conventional free RAM. Call before display. */
void stats_snapshot_memory(unsigned int sprite_kb, unsigned int pcm_kb,
                           unsigned int near_kb, unsigned int peak_kb);

/* Best-effort estimate of near/static (DGROUP) data footprint, in KB. */
unsigned int stats_near_estimate(void);

/* Draw the exit summary. Call after EGA mode released, before returning. */
void stats_display(int cpu_mode, int sound_mode,
                   int sound_port, int sound_irq, int sound_dma);

#endif /* STATS_H */
