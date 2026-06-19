/*
 * HISCORE.C -- high score table management and EGA initials entry (build step 7).
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * Three separate leaderboards (CPU_386, CPU_486, CPU_P66), 10 entries each.
 * Persisted to HISCORES.DAT as a raw HiScoreTable binary with XOR checksum.
 * If the file is absent or the checksum fails, defaults are installed silently.
 *
 * hiscore_entry() runs in EGA mode 0Dh (called after ega_set_mode()).
 * Font rendering mirrors TITLE.C: g_font[] via spr_char_to_glyph().
 */

#include <stdio.h>
#include <string.h>
#include "EGA.H"
#include "SPRITES.H"    /* g_font[], spr_char_to_glyph(), FONT_W, FONT_H */
#include "INPUT.H"
#include "HISCORE.H"

/* =========================================================================
 * Module state
 * =========================================================================*/
static HiScoreTable s_table;

/* =========================================================================
 * Section 1 -- Compiled-in default table
 * =========================================================================*/
static const HiScoreEntry s_defaults[30] = {
    /* CPU_386 (mode 0), descending score */
    { "HAL", 29500, 0 }, { "SID", 18200, 0 }, { "C64",  9999, 0 },
    { "REX",  8800, 0 }, { "TOM",  7200, 0 }, { "ZAP",  6100, 0 },
    { "ACE",  5400, 0 }, { "VIC",  4300, 0 }, { "RJC",  3800, 0 },
    { "JAY",  2500, 0 },
    /* CPU_486 (mode 1) */
    { "JAY", 61800, 1 }, { "ACE", 54200, 1 }, { "REX", 43100, 1 },
    { "TOM", 38800, 1 }, { "ZAP", 29500, 1 }, { "VIC", 22300, 1 },
    { "HAL", 18200, 1 }, { "SID", 14500, 1 }, { "C64",  9999, 1 },
    { "RJC",  7200, 1 },
    /* CPU_P66 (mode 2) */
    { "JAY", 99999, 2 }, { "ACE", 87500, 2 }, { "RJC", 72300, 2 },
    { "TOM", 61800, 2 }, { "ZAP", 54200, 2 }, { "VIC", 43100, 2 },
    { "REX", 38800, 2 }, { "HAL", 29500, 2 }, { "SID", 18200, 2 },
    { "C64",  9999, 2 }
};

/* =========================================================================
 * Section 2 -- Checksum
 * =========================================================================*/
static unsigned int compute_checksum(void)
{
    const unsigned char *p = (const unsigned char *)s_table.entries;
    unsigned int n   = (unsigned int)sizeof(s_table.entries);
    unsigned int sum = 0;
    unsigned int i;
    for (i = 0; i < n; i++)
        sum ^= p[i];
    return sum;
}

/* =========================================================================
 * Section 3 -- Load / Save / Reset
 * =========================================================================*/

static void install_defaults(void)
{
    s_table.magic   = 0x5349u;
    s_table.version = 1u;
    memcpy(s_table.entries, s_defaults, sizeof(s_defaults));
    s_table.checksum = compute_checksum();
}

void hiscore_init(void)
{
    FILE *f;
    int  ok = 0;

    f = fopen("HISCORES.DAT", "rb");
    if (f) {
        if (fread(&s_table, 1, sizeof(s_table), f) == sizeof(s_table)) {
            if (s_table.magic   == 0x5349u &&
                s_table.version == 1u      &&
                s_table.checksum == compute_checksum()) {
                ok = 1;
            }
        }
        fclose(f);
    }

    if (!ok) {
        install_defaults();
        hiscore_save();
    }
}

int hiscore_save(void)
{
    FILE *f;

    s_table.checksum = compute_checksum();
    f = fopen("HISCORES.DAT", "wb");
    if (!f) return 0;
    fwrite(&s_table, 1, sizeof(s_table), f);
    fclose(f);
    return 1;
}

void hiscore_reset(void)
{
    install_defaults();
    hiscore_save();
}

/* =========================================================================
 * Section 4 -- Ranking
 * =========================================================================*/

int hiscore_check_rank(long score, int cpu_mode)
{
    const HiScoreEntry *base;
    int i;

    if (cpu_mode < 0 || cpu_mode >= 3) return -1;
    base = s_table.entries + cpu_mode * HISCORE_PER_MODE;
    for (i = 0; i < HISCORE_PER_MODE; i++) {
        if (score > base[i].score)
            return i;
    }
    return -1;
}

const HiScoreEntry *hiscore_get_table(void)
{
    return s_table.entries;
}

const HiScoreEntry *hiscore_get_mode(int cpu_mode)
{
    if (cpu_mode < 0 || cpu_mode >= 3) return s_table.entries;
    return s_table.entries + cpu_mode * HISCORE_PER_MODE;
}

/* =========================================================================
 * Section 5 -- Insert into table (static helper)
 * =========================================================================*/

static void insert_score(int rank, long score, int cpu_mode,
                         const char *initials)
{
    HiScoreEntry *base = s_table.entries + cpu_mode * HISCORE_PER_MODE;
    int i;

    /* Shift entries rank..HISCORE_PER_MODE-2 down one slot. */
    for (i = HISCORE_PER_MODE - 1; i > rank; i--)
        base[i] = base[i - 1];

    strncpy(base[rank].initials, initials, 3);
    base[rank].initials[3] = '\0';
    base[rank].score    = score;
    base[rank].cpu_mode = cpu_mode;
}

/* =========================================================================
 * Section 6 -- EGA text drawing helpers
 *
 * Font: 8×8 glyphs from g_font[] in SPRITES.H.
 * Mirrors TITLE.C draw_text() exactly.
 * =========================================================================*/

static void hs_draw_text(int x, int y, const char *s, unsigned char color)
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

/* Integer-to-decimal string, right-aligned in 'width' chars, space-padded. */
static void hs_draw_score(int x, int y, long score, unsigned char color)
{
    char buf[12];
    int  i, len, pad;
    long v = score;
    const int width = 7;

    /* Convert to string (score is always >= 0 in this game). */
    i = 11;
    buf[i] = '\0';
    if (v == 0) {
        buf[--i] = '0';
    } else {
        while (v > 0 && i > 0) {
            buf[--i] = (char)('0' + (int)(v % 10));
            v /= 10;
        }
    }
    len = 11 - i;           /* digits in buf+i */
    pad = width - len;

    for (; pad > 0; pad--, x += FONT_W)
        ;                   /* skip leading space (blank = no pixel) */
    hs_draw_text(x, y, buf + i, color);
}

/* =========================================================================
 * Section 7 -- Hi-score table display (EGA mode)
 * =========================================================================*/

#define HS_TITLE_Y   20
#define HS_HEADER_Y  36
#define HS_ROW0_Y    48    /* first data row */
#define HS_ROW_H     12    /* pixels per row (8 glyph + 4 gap) */

#define HS_COL_RANK  42
#define HS_COL_NAME  70
#define HS_COL_SCORE 120

static const char *mode_label[3] = { "386", "486", "P66" };

/* Draw the full 10-entry table for cpu_mode; highlight_rank = -1 for none. */
static void draw_hiscore_table(int cpu_mode, int highlight_rank)
{
    const HiScoreEntry *base;
    char hdr[32];
    char rank_buf[4];
    int i, y;
    unsigned char col;

    if (cpu_mode < 0 || cpu_mode >= 3) cpu_mode = 0;
    base = s_table.entries + cpu_mode * HISCORE_PER_MODE;

    ega_clear(EGA_BLACK);

    /* Title: "HIGH SCORES -- 386" */
    hdr[0] = '\0';
    strcat(hdr, "HIGH SCORES -- ");
    strcat(hdr, mode_label[cpu_mode]);
    hs_draw_text(HS_COL_RANK, HS_TITLE_Y, hdr, EGA_YELLOW);

    /* Column headers */
    hs_draw_text(HS_COL_RANK,  HS_HEADER_Y, "#",     EGA_WHITE);
    hs_draw_text(HS_COL_NAME,  HS_HEADER_Y, "NAME",  EGA_WHITE);
    hs_draw_text(HS_COL_SCORE, HS_HEADER_Y, "SCORE", EGA_WHITE);

    for (i = 0; i < HISCORE_PER_MODE; i++) {
        y   = HS_ROW0_Y + i * HS_ROW_H;
        col = (i == highlight_rank) ? EGA_CYAN : EGA_WHITE;

        /* Rank number */
        rank_buf[0] = (char)('0' + (i + 1) / 10 % 10);
        rank_buf[1] = (char)('0' + (i + 1) % 10);
        rank_buf[2] = '\0';
        if (i + 1 < 10) {
            rank_buf[0] = ' ';
        }
        hs_draw_text(HS_COL_RANK, y, rank_buf, col);

        /* Initials */
        hs_draw_text(HS_COL_NAME, y, base[i].initials, col);

        /* Score */
        hs_draw_score(HS_COL_SCORE, y, base[i].score, col);
    }
}

/* =========================================================================
 * Section 8 -- Initials entry screen
 * =========================================================================*/

#define ENTRY_GAMEOVER_Y   30
#define ENTRY_YOUARE_Y     50
#define ENTRY_SLOTS_Y      90
#define ENTRY_HELP_Y      130
#define ENTRY_SLOT_X0     120   /* left slot pixel x */
#define ENTRY_SLOT_STEP    20   /* pixels between slots */

/* Draw or erase the three letter slots. */
static void draw_slots(const char letters[3], int active_slot)
{
    int i;
    unsigned char col;

    for (i = 0; i < 3; i++) {
        col = (i == active_slot) ? EGA_YELLOW : EGA_WHITE;
        /* Erase old glyph (black rect behind each slot). */
        ega_fill_rect(ENTRY_SLOT_X0 + i * ENTRY_SLOT_STEP,
                      ENTRY_SLOTS_Y, FONT_W, FONT_H, EGA_BLACK);
        /* Draw letter. */
        {
            char ch[2];
            ch[0] = letters[i];
            ch[1] = '\0';
            hs_draw_text(ENTRY_SLOT_X0 + i * ENTRY_SLOT_STEP,
                         ENTRY_SLOTS_Y, ch, col);
        }
        /* Underline active slot. */
        if (i == active_slot) {
            ega_fill_rect(ENTRY_SLOT_X0 + i * ENTRY_SLOT_STEP,
                          ENTRY_SLOTS_Y + FONT_H + 1, FONT_W, 1, EGA_YELLOW);
        } else {
            ega_fill_rect(ENTRY_SLOT_X0 + i * ENTRY_SLOT_STEP,
                          ENTRY_SLOTS_Y + FONT_H + 1, FONT_W, 1, EGA_BLACK);
        }
    }
}

/* Build "YOU ARE #X!" string where X is 1-based rank. */
static void make_youare(char *buf, int rank)
{
    const char *pre = "YOU ARE #";
    int r = rank + 1;
    int i = 0;
    while (pre[i]) { buf[i] = pre[i]; i++; }
    if (r >= 10) buf[i++] = (char)('0' + r / 10);
    buf[i++] = (char)('0' + r % 10);
    buf[i++] = '!';
    buf[i]   = '\0';
}

void hiscore_entry(int rank, long score, int cpu_mode)
{
    char  letters[3];
    int   slot;
    char  youare[16];
    int   i;
    int   done;

    letters[0] = 'A';
    letters[1] = 'A';
    letters[2] = 'A';
    slot = 0;
    done = 0;

    make_youare(youare, rank);

    /* Initial screen layout. */
    ega_clear(EGA_BLACK);
    hs_draw_text(104, ENTRY_GAMEOVER_Y, "GAME OVER", EGA_RED);
    hs_draw_text(80,  ENTRY_YOUARE_Y,   youare,      EGA_YELLOW);
    hs_draw_text(48,  ENTRY_HELP_Y,
                 "UP/DN:CYCLE  FIRE:NEXT", EGA_WHITE);
    draw_slots(letters, slot);

    while (!done) {
        ega_wait_vsync();
        inp_clear_edge();

        /* Cycle current letter UP. */
        if (inp_just_pressed(KEY_UP)) {
            letters[slot]++;
            if (letters[slot] > 'Z') letters[slot] = 'A';
            draw_slots(letters, slot);
        }
        /* Cycle current letter DOWN. */
        else if (inp_just_pressed(KEY_DOWN)) {
            letters[slot]--;
            if (letters[slot] < 'A') letters[slot] = 'Z';
            draw_slots(letters, slot);
        }
        /* Move left between slots. */
        else if (inp_just_pressed(KEY_LEFT)) {
            if (slot > 0) {
                slot--;
                draw_slots(letters, slot);
            }
        }
        /* Move right / confirm slot. */
        else if (inp_just_pressed(KEY_RIGHT) ||
                 inp_just_pressed(KEY_FIRE)  ||
                 inp_just_pressed(KEY_ENTER)) {
            if (slot < 2) {
                slot++;
                draw_slots(letters, slot);
            } else {
                done = 1;
            }
        }
    }

    /* Insert into table and persist. */
    insert_score(rank, score, cpu_mode, letters);
    hiscore_save();

    /* Show updated table with new entry highlighted for ~5s (~350 vsyncs). */
    draw_hiscore_table(cpu_mode, rank);
    for (i = 0; i < 350; i++)
        ega_wait_vsync();
}

/* =========================================================================
 * HISCORE_TEST -- text-mode self-test; no EGA required.
 *
 * Compile:
 *   wcc -ml -zf -DHISCORE_TEST HISCORE.C SPRITES.C
 *   wlink system dos file { HISCORE.obj SPRITES.obj } name HITEST
 *
 * (SPRITES.C provides spr_char_to_glyph and g_font; EGA functions are not
 *  called in HISCORE_TEST mode so EGA.C is not needed.)
 * =========================================================================*/
#ifdef HISCORE_TEST

#include <stdio.h>

static const char *mode_names[3] = { "386", "486", "P66" };

static void print_table(void)
{
    int m, i;
    const HiScoreEntry *e;

    for (m = 0; m < 3; m++) {
        printf("--- CPU %s ---\n", mode_names[m]);
        e = hiscore_get_mode(m);
        for (i = 0; i < HISCORE_PER_MODE; i++)
            printf("  %2d. %-3s %6ld\n", i + 1, e[i].initials, e[i].score);
    }
}

int main(void)
{
    int rank;

    puts("HISCORE TEST");
    puts("------------");

    puts("hiscore_init()...");
    hiscore_init();
    puts("Table after init:");
    print_table();

    /* Rank check: 15000 on 386 should be rank 2 (between SID=18200 and C64=9999). */
    rank = hiscore_check_rank(15000L, 0);
    printf("\nhiscore_check_rank(15000, 386) = %d  (expect 2)\n", rank);

    rank = hiscore_check_rank(0L, 0);
    printf("hiscore_check_rank(0, 386) = %d  (expect -1)\n", rank);

    rank = hiscore_check_rank(99999L, 1);
    printf("hiscore_check_rank(99999, 486) = %d  (expect 0)\n", rank);

    puts("\nhiscore_reset()...");
    hiscore_reset();
    puts("Table after reset:");
    print_table();
    puts("Reset OK");

    return 0;
}

#endif /* HISCORE_TEST */
