#ifndef SOUND_H
#define SOUND_H

/*
 * SOUND.H -- PC Speaker / Sound Blaster / SB Pro audio layer (build step 6).
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * Three device tiers: SOUND_SPK (always), SOUND_SB (8-bit mono DMA),
 * SOUND_SBP (8-bit stereo DMA, 22050 Hz).
 *
 * SOUNDS.DAT I/O: cache_generate_sounds / cache_save_sounds / cache_load_sounds
 * implement the stubs declared in CACHE.H.
 */

/* -------------------------------------------------------------------------
 * Device types
 * -------------------------------------------------------------------------*/
#define SOUND_NONE  0
#define SOUND_SPK   1
#define SOUND_SB    2
#define SOUND_SBP   3

/* -------------------------------------------------------------------------
 * Sound event IDs
 * -------------------------------------------------------------------------*/
#define SND_MARCH0       0   /* march beat, note 0 (lowest)  */
#define SND_MARCH1       1
#define SND_MARCH2       2
#define SND_MARCH3       3   /* march beat, note 3 (highest) */
#define SND_FIRE         4
#define SND_INV_EXPLODE  5
#define SND_PLR_DEATH    6
#define SND_UFO_PASS     7
#define SND_UFO_HIT      8
#define SND_SHIELD_HIT   9
#define SND_LEVEL_CLEAR 10
#define SND_TITLE       11
#define SND_COUNT       12

/* -------------------------------------------------------------------------
 * Playback priority (lower number = higher priority)
 * -------------------------------------------------------------------------*/
#define SPRI_CRITICAL  1   /* death, level clear */
#define SPRI_HIGH      2   /* fire, UFO hit      */
#define SPRI_MED       3   /* invader explosion  */
#define SPRI_LOW       4   /* shield hit         */
#define SPRI_BG        5   /* march, UFO, title  */

/* -------------------------------------------------------------------------
 * Hardware configuration (filled by sound_detect)
 * -------------------------------------------------------------------------*/
typedef struct {
    int  device;       /* SOUND_NONE / SPK / SB / SBP */
    int  port;         /* SB base I/O (e.g. 0x220)    */
    int  irq;          /* SB IRQ line (e.g. 5)         */
    int  dma;          /* SB DMA channel (0-3)         */
    int  sample_rate;  /* 11025 (SB) or 22050 (SBP)   */
} SoundConfig;

/* -------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/

/* Probe hardware; fills *cfg; returns device type (SOUND_*). */
int  sound_detect(SoundConfig *cfg);

/* Install IRQ handler (SB/SBP) or prime speaker state. sound_detect first. */
void sound_init(const SoundConfig *cfg);

/* Restore IRQ vector; silence speaker / DSP. */
void sound_shutdown(void);

/* Queue a sound event. screen_x 0-319 for SBP stereo pan (160 = center). */
void sound_play(int event, int priority, int screen_x);

/* Stop all output immediately. */
void sound_stop(void);

/* Per-frame tick: advance PC Speaker note timer; service looping sounds. */
void sound_update(void);

/* -------------------------------------------------------------------------
 * SOUNDS.DAT I/O -- also the real implementations of the CACHE.H stubs
 * -------------------------------------------------------------------------*/
int  cache_generate_sounds(void);
int  cache_save_sounds(void);
int  cache_load_sounds(void);
void sound_free(void);

#endif /* SOUND_H */
