#ifndef HISCORE_H
#define HISCORE_H

/*
 * HISCORE.H -- high score table management and EGA initials entry (build step 7).
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * Three separate leaderboards (one per CPU mode), 10 entries each.
 * Stored in HISCORES.DAT as a raw HiScoreTable struct with XOR checksum.
 * On missing/bad file: silently regenerate from compiled-in defaults.
 *
 * HiScoreEntry and HISCORE_* constants moved here from TITLE.H (step 7).
 */

/* -------------------------------------------------------------------------
 * Table dimensions
 * -------------------------------------------------------------------------*/
#define HISCORE_INITIALS_LEN  4   /* 3 chars + NUL */
#define HISCORE_PER_MODE     10   /* entries per CPU mode */

/* -------------------------------------------------------------------------
 * Data structures
 * -------------------------------------------------------------------------*/
typedef struct {
    char  initials[HISCORE_INITIALS_LEN];
    long  score;
    int   cpu_mode;
} HiScoreEntry;

typedef struct {
    unsigned int  magic;        /* 0x5349 ('S','I') */
    unsigned int  version;      /* 1 */
    HiScoreEntry  entries[30];  /* 10 per mode; mode 0 first, then 1, then 2 */
    unsigned int  checksum;     /* XOR across all bytes of entries[] */
} HiScoreTable;

/* -------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/

/* Load HISCORES.DAT; on missing/bad checksum, silently install defaults. */
void hiscore_init(void);

/* Write current table to HISCORES.DAT. Returns 1 on success, 0 on I/O error. */
int  hiscore_save(void);

/* Overwrite table with compiled-in defaults and save. */
void hiscore_reset(void);

/*
 * Return 0-based rank if score qualifies for cpu_mode's leaderboard, -1 if not.
 * Rank 0 = best score. The player must beat an existing entry to qualify.
 */
int  hiscore_check_rank(long score, int cpu_mode);

/*
 * Run the EGA-mode initials entry screen, then show updated table for ~5s.
 * Requires EGA mode 0Dh to be active (call ega_set_mode first) and
 * inp_install() to have been called.
 *
 * rank:     0-based rank from hiscore_check_rank.
 * score:    player's score.
 * cpu_mode: CPU_386 / CPU_486 / CPU_P66.
 */
void hiscore_entry(int rank, long score, int cpu_mode);

/* Pointer to the flat 30-entry array (row-major by mode, 10 entries each). */
const HiScoreEntry *hiscore_get_table(void);

/* Pointer to the 10 entries for cpu_mode (0-2). */
const HiScoreEntry *hiscore_get_mode(int cpu_mode);

#endif /* HISCORE_H */
