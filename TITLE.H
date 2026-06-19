#ifndef TITLE_H
#define TITLE_H

/*
 * TITLE.H -- title screen, attract mode, high score display.
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * CPU_*, STAR_*, SHIELD_* are defined here for build steps 4-8.
 * They move to INVADERS.H in step 9; TITLE.H will then include it.
 * HiScoreEntry and HISCORE_* moved to HISCORE.H in step 7.
 */

#include "HISCORE.H"

/* CPU modes */
#define CPU_386  0
#define CPU_486  1
#define CPU_P66  2

/* Starfield display modes (GameSettings.title_starfield) */
#define STAR_NONE    0
#define STAR_STATIC  1
#define STAR_SCROLL  2

/* Shield damage modes (GameSettings.shield_mode) */
#define SHIELD_STAGED  0
#define SHIELD_PIXEL   1

/*
 * title_run() -- blocks until the player presses fire (Space/Enter/Esc).
 *
 * cpu_mode  : CPU_386 / CPU_486 / CPU_P66  (selects march speed, column hi)
 * starfield : STAR_NONE / STAR_STATIC / STAR_SCROLL
 * scores    : pointer to HISCORE_PER_MODE * 3 HiScoreEntry, row-major by mode
 *             (mode 0 first, then mode 1, then mode 2).  NULL hides hi-score.
 */
void title_run(int cpu_mode, int starfield, const HiScoreEntry *scores);

#endif /* TITLE_H */
