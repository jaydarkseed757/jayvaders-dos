/*
 * STATS.C -- runtime session statistics + exit summary screen.
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * No floating point. No stdio. All console output via DOS int 21h AH=09h on
 * '$'-terminated buffers; integer-only number formatting with comma grouping.
 */

#include <i86.h>    /* int86, intdos, union REGS, struct SREGS */
#include <dos.h>    /* REGS/SREGS fallbacks                     */
#include <string.h> /* strlen, memset                          */

#include "STATS.H"
#include "SOUND.H"   /* SOUND_* device constants */
#include "TITLE.H"   /* CPU_*   mode constants   */

/* The one and only statistics instance. */
SessionStats g_stats;

/* FPS sampling cursor state. */
static long s_last_ticks;
static long s_last_frames;
static int  s_fps_primed;

/* =========================================================================
 * Low-level I/O helpers (no stdio)
 * =========================================================================*/

/* Print a '$'-terminated string via DOS int 21h AH=09h.
 * Medium model: data is near (DS = DGROUP), so the offset is sufficient. */
static void dos_print(const char *s)
{
    union REGS r;
    r.h.ah = 0x09;
    r.x.dx = (unsigned)s;
    intdos(&r, &r);
}

/* int 16h AH=00h: block for a keypress. */
static void wait_key(void)
{
    union REGS r;
    r.h.ah = 0x00;
    int86(0x16, &r, &r);
}

/* int 21h AH=44h AL=00h: 1 if stdout (handle 1) is a character device. */
static int stdout_is_console(void)
{
    union REGS r;
    r.x.ax = 0x4400;
    r.x.bx = 1;
    intdos(&r, &r);
    if (r.x.cflag) return 1;            /* on error assume console */
    return (r.x.dx & 0x0080) ? 1 : 0;   /* bit 7 set => char device */
}

/* =========================================================================
 * Number formatting (integer only, no printf)
 * =========================================================================*/

/* Unsigned long -> decimal with thousands separators. Returns buf. */
static char *u32_commas(unsigned long v, char *buf)
{
    char tmp[16];
    int  n = 0, p = 0, grp = 0, i;

    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return buf; }

    while (v > 0) {
        tmp[n++] = (char)('0' + (int)(v % 10UL));
        v /= 10UL;
        if (++grp == 3 && v > 0) { tmp[n++] = ','; grp = 0; }
    }
    for (i = n - 1; i >= 0; i--) buf[p++] = tmp[i];
    buf[p] = '\0';
    return buf;
}

/* Signed long -> plain decimal. Returns buf. */
static char *i_to_a(long v, char *buf)
{
    char tmp[16];
    int  n = 0, p = 0, i;
    unsigned long u;

    if (v < 0) { buf[p++] = '-'; u = (unsigned long)(-v); }
    else       u = (unsigned long)v;

    if (u == 0) { tmp[n++] = '0'; }
    while (u > 0) { tmp[n++] = (char)('0' + (int)(u % 10UL)); u /= 10UL; }

    for (i = n - 1; i >= 0; i--) buf[p++] = tmp[i];
    buf[p] = '\0';
    return buf;
}

/* Uppercase hex of a 16-bit value with a trailing 'h' (e.g. 220h). */
static char *u16_hex(unsigned int v, char *buf)
{
    static const char hx[] = "0123456789ABCDEF";
    char tmp[8];
    int  n = 0, p = 0, i;

    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = hx[v & 0xF]; v >>= 4; }
    for (i = n - 1; i >= 0; i--) buf[p++] = tmp[i];
    buf[p++] = 'h';
    buf[p] = '\0';
    return buf;
}

/* "HH:MM:SS" with zero padding. */
static char *fmt_time(int h, int m, int s, char *buf)
{
    buf[0] = (char)('0' + (h / 10) % 10);
    buf[1] = (char)('0' + h % 10);
    buf[2] = ':';
    buf[3] = (char)('0' + (m / 10) % 10);
    buf[4] = (char)('0' + m % 10);
    buf[5] = ':';
    buf[6] = (char)('0' + (s / 10) % 10);
    buf[7] = (char)('0' + s % 10);
    buf[8] = '\0';
    return buf;
}

/* =========================================================================
 * Box drawing
 * =========================================================================*/

#define BOX_W 43   /* interior width between the vertical borders */

/* Active border glyphs (CP437 console, or ASCII fallback when redirected). */
static char s_tl, s_tr, s_bl, s_br, s_ml, s_mr, s_h, s_v;

static void set_charset(int console)
{
    if (console) {
        s_tl = (char)0xC9; s_tr = (char)0xBB;   /* corners top    */
        s_bl = (char)0xC8; s_br = (char)0xBC;   /* corners bottom */
        s_ml = (char)0xCC; s_mr = (char)0xB9;   /* tee separators */
        s_h  = (char)0xCD; s_v  = (char)0xBA;   /* horiz / vert   */
    } else {
        s_tl = s_tr = s_bl = s_br = s_ml = s_mr = '+';
        s_h  = '-';
        s_v  = '|';
    }
}

static void emit_border(char left, char right)
{
    char buf[80];
    int  i, p = 0;
    buf[p++] = left;
    for (i = 0; i < BOX_W; i++) buf[p++] = s_h;
    buf[p++] = right;
    buf[p++] = '\r'; buf[p++] = '\n'; buf[p++] = '$';
    buf[p]   = '\0';
    dos_print(buf);
}

/* Wrap interior text (left-justified, space-padded to BOX_W) in borders. */
static void emit_row(const char *interior)
{
    char buf[80];
    int  len = (int)strlen(interior), i, p = 0;
    buf[p++] = s_v;
    for (i = 0; i < len && i < BOX_W; i++) buf[p++] = interior[i];
    for (; i < BOX_W; i++) buf[p++] = ' ';
    buf[p++] = s_v;
    buf[p++] = '\r'; buf[p++] = '\n'; buf[p++] = '$';
    buf[p]   = '\0';
    dos_print(buf);
}

/* A section header line: "  TEXT". */
static void row_hdr(const char *text)
{
    char buf[80];
    int  p = 0, i, n = (int)strlen(text);
    buf[p++] = ' '; buf[p++] = ' ';
    for (i = 0; i < n && p < BOX_W; i++) buf[p++] = text[i];
    buf[p] = '\0';
    emit_row(buf);
}

/* A label/value row: "  Label            :  value  TAG". */
static void row_kv(const char *label, const char *value, const char *tag)
{
    char buf[80];
    int  p = 0, i, n;

    buf[p++] = ' '; buf[p++] = ' ';
    n = (int)strlen(label);
    for (i = 0; i < n; i++) buf[p++] = label[i];
    while (p < 2 + 18) buf[p++] = ' ';        /* pad label field to 18 */
    buf[p++] = ':'; buf[p++] = ' '; buf[p++] = ' ';

    n = (int)strlen(value);
    for (i = 0; i < n; i++) buf[p++] = value[i];

    if (tag && tag[0]) {
        buf[p++] = ' '; buf[p++] = ' ';
        n = (int)strlen(tag);
        for (i = 0; i < n; i++) buf[p++] = tag[i];
    }
    buf[p] = '\0';
    emit_row(buf);
}

/* Centered single line (used for the header banner). */
static void row_center(const char *text)
{
    char buf[80];
    int  n = (int)strlen(text), pad, p = 0, i;
    if (n > BOX_W) n = BOX_W;
    pad = (BOX_W - n) / 2;
    for (i = 0; i < pad; i++) buf[p++] = ' ';
    for (i = 0; i < n; i++)   buf[p++] = text[i];
    buf[p] = '\0';
    emit_row(buf);
}

/* =========================================================================
 * Public API
 * =========================================================================*/

void stats_read_pit(long *ticks)
{
    union REGS r;
    r.h.ah = 0x00;
    int86(0x1A, &r, &r);                       /* CX:DX = tick count */
    *ticks = ((long)r.x.cx << 16) | (long)(unsigned)r.x.dx;
}

void stats_init(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
    stats_read_pit(&g_stats.start_ticks);
    g_stats.fps_min = 999;
    s_last_ticks    = 0;
    s_last_frames   = 0;
    s_fps_primed    = 0;
}

void stats_sample_fps(int wave)
{
    long now, elapsed;
    int  fps;

    stats_read_pit(&now);

    if (!s_fps_primed) {
        s_last_ticks  = now;
        s_last_frames = g_stats.frames_rendered;
        s_fps_primed  = 1;
        return;
    }

    elapsed = now - s_last_ticks;
    if (elapsed < 18) return;                  /* ~1 s at 18.2 Hz */

    /* A long gap means a blocking sequence (wave clear / death pause) ran
     * between samples -- rebaseline without recording a bogus low reading. */
    if (elapsed >= 36) {
        s_last_ticks  = now;
        s_last_frames = g_stats.frames_rendered;
        return;
    }

    fps = (int)(g_stats.frames_rendered - s_last_frames);
    g_stats.fps_accumulator += fps;
    g_stats.fps_sample_count++;
    if (wave > 0 && fps < g_stats.fps_min) {
        g_stats.fps_min      = fps;
        g_stats.fps_min_wave = wave;
    }

    s_last_ticks  = now;
    s_last_frames = g_stats.frames_rendered;
}

void stats_snapshot_memory(unsigned int sprite_kb, unsigned int pcm_kb,
                           unsigned int near_kb, unsigned int peak_kb)
{
    union REGS r;

    g_stats.sprite_cache_kb = sprite_kb;
    g_stats.pcm_samples_kb  = pcm_kb;
    g_stats.near_data_kb    = near_kb;
    g_stats.peak_heap_kb    = peak_kb;

    /* int 21h AH=48h, BX=FFFF: allocation fails, BX = largest free block
     * in paragraphs (16 bytes each). */
    r.h.ah = 0x48;
    r.x.bx = 0xFFFF;
    intdos(&r, &r);
    g_stats.conventional_free_kb =
        (unsigned int)(((unsigned long)r.x.bx * 16UL) / 1024UL);
}

unsigned int stats_near_estimate(void)
{
    /* Best-effort: dominant near/static (DGROUP) consumers are the program
     * stack (8 KB), the SB DMA buffer (~4 KB), this stats block, and assorted
     * module statics. Reported as an approximation. */
    unsigned long bytes = (unsigned long)sizeof(SessionStats)
                        + 8192UL    /* stack          */
                        + 4096UL    /* SB DMA buffer  */
                        + 2048UL;   /* misc statics   */
    return (unsigned int)((bytes + 1023UL) / 1024UL);
}

/* ----- display helpers ----- */

static const char *cpu_name(int cpu_mode)
{
    switch (cpu_mode) {
    case CPU_386: return "386 DX/16";
    case CPU_486: return "486 DX/33";
    default:      return "Pentium 66";
    }
}

static const char *sound_name(int sound_mode)
{
    switch (sound_mode) {
    case SOUND_SPK: return "PC Speaker";
    case SOUND_SB:  return "Sound Blaster";
    case SOUND_SBP: return "Sound Blaster Pro";
    default:        return "Disabled";
    }
}

void stats_display(int cpu_mode, int sound_mode,
                   int sound_port, int sound_irq, int sound_dma)
{
    union REGS r;
    char vbuf[40], tbuf[16], kbuf[24];
    long elapsed, seconds;
    int  hours, minutes, secs;
    int  avg_fps, accuracy, shield_pct;

    /* --- derived values (integer only) --- */
    elapsed = g_stats.exit_ticks - g_stats.start_ticks;
    if (elapsed < 0) elapsed = 0;
    seconds = (elapsed * 10L) / 182L;
    hours   = (int)(seconds / 3600L);
    minutes = (int)((seconds % 3600L) / 60L);
    secs    = (int)(seconds % 60L);

    avg_fps = (g_stats.fps_sample_count > 0)
            ? (int)(g_stats.fps_accumulator / (long)g_stats.fps_sample_count)
            : 0;

    accuracy = (g_stats.shots_fired > 0)
             ? (int)(((long)g_stats.invaders_killed * 100L) / g_stats.shots_fired)
             : 0;

    shield_pct = (g_stats.shield_pixels_total > 0)
               ? (int)(((long)g_stats.shield_pixels_eroded * 100L)
                       / (long)g_stats.shield_pixels_total)
               : 0;

    /* --- text mode + charset --- */
    set_charset(stdout_is_console());
    r.x.ax = 0x0003;            /* int 10h: set 80x25 text mode */
    int86(0x10, &r, &r);

    /* ----- SESSION ----- */
    emit_border(s_tl, s_tr);
    row_center("SPACE INVADERS  EXIT STATS");
    emit_border(s_ml, s_mr);
    row_hdr("SESSION");
    row_kv("Run time",        fmt_time(hours, minutes, secs, tbuf), 0);
    row_kv("Frames rendered", u32_commas((unsigned long)g_stats.frames_rendered, vbuf), 0);
    row_kv("Average FPS",     i_to_a((long)avg_fps, vbuf), 0);
    {
        char wtxt[24];
        char fps_lo[16];
        if (g_stats.fps_min >= 999) {
            row_kv("Lowest FPS", "n/a", 0);
        } else {
            int q = 0, i, n;
            i_to_a((long)g_stats.fps_min, fps_lo);
            n = (int)strlen(fps_lo);
            for (i = 0; i < n; i++) wtxt[q++] = fps_lo[i];
            wtxt[q++] = ' '; wtxt[q++] = '(';
            wtxt[q++] = 'w'; wtxt[q++] = 'a'; wtxt[q++] = 'v'; wtxt[q++] = 'e'; wtxt[q++] = ' ';
            { char nb[8]; int j, m; i_to_a((long)g_stats.fps_min_wave, nb);
              m = (int)strlen(nb); for (j = 0; j < m; j++) wtxt[q++] = nb[j]; }
            wtxt[q++] = ')'; wtxt[q] = '\0';
            row_kv("Lowest FPS", wtxt, 0);
        }
    }

    /* ----- GAMEPLAY ----- */
    emit_border(s_ml, s_mr);
    row_hdr("GAMEPLAY");
    row_kv("Games played",   i_to_a((long)g_stats.games_played, vbuf),
           (g_stats.games_played >= 5) ? "HOOKED!" : 0);
    row_kv("Waves cleared",  i_to_a((long)g_stats.waves_cleared, vbuf), 0);
    row_kv("Invaders killed", i_to_a((long)g_stats.invaders_killed, vbuf),
           (g_stats.invaders_killed == 0) ? "JUST WATCHING" : 0);
    row_kv("UFOs hit",       i_to_a((long)g_stats.ufos_hit, vbuf),
           (g_stats.ufos_hit == 0) ? "MISSED 'EM" : 0);
    row_kv("Shots fired",    u32_commas((unsigned long)g_stats.shots_fired, vbuf), 0);
    { char pb[8]; i_to_a((long)accuracy, pb); strcat(pb, "%");
      row_kv("Accuracy", pb, (accuracy >= 50) ? "CRACK SHOT!" : 0); }
    row_kv("Bombs dodged",   u32_commas((unsigned long)g_stats.bombs_dodged, vbuf),
           (g_stats.bombs_dodged > 200L) ? "UNTOUCHABLE" : 0);
    { char pb[8]; i_to_a((long)shield_pct, pb); strcat(pb, "%");
      row_kv("Shields eroded", pb, 0); }
    row_kv("Best score",     i_to_a((long)g_stats.best_score, vbuf), 0);
    row_kv("Extra lives earned", i_to_a((long)g_stats.extra_lives_earned, vbuf), 0);

    /* ----- SOUND ----- */
    emit_border(s_ml, s_mr);
    row_hdr("SOUND");
    row_kv("Device", sound_name(sound_mode), 0);
    if (sound_mode == SOUND_SPK || sound_mode == SOUND_NONE) {
        row_kv("Port / IRQ / DMA", "N/A", 0);
    } else {
        char ln[32]; int p = 0, i, n;
        u16_hex((unsigned int)sound_port, kbuf);
        n = (int)strlen(kbuf); for (i = 0; i < n; i++) ln[p++] = kbuf[i];
        ln[p++] = ' '; ln[p++] = ' ';
        i_to_a((long)sound_irq, kbuf);
        n = (int)strlen(kbuf); for (i = 0; i < n; i++) ln[p++] = kbuf[i];
        ln[p++] = ' '; ln[p++] = ' ';
        i_to_a((long)sound_dma, kbuf);
        n = (int)strlen(kbuf); for (i = 0; i < n; i++) ln[p++] = kbuf[i];
        ln[p] = '\0';
        row_kv("Port / IRQ / DMA", ln, 0);
    }

    /* ----- MEMORY ----- */
    emit_border(s_ml, s_mr);
    row_hdr("MEMORY");
    { i_to_a((long)g_stats.conventional_free_kb, vbuf); strcat(vbuf, "kb");
      row_kv("Conventional free", vbuf, 0); }
    { i_to_a((long)g_stats.sprite_cache_kb, vbuf); strcat(vbuf, "kb");
      row_kv("Sprite cache", vbuf, 0); }
    { i_to_a((long)g_stats.pcm_samples_kb, vbuf); strcat(vbuf, "kb");
      row_kv("PCM samples", vbuf, 0); }
    { i_to_a((long)g_stats.near_data_kb, vbuf); strcat(vbuf, "kb");
      row_kv("Near data used", vbuf, 0); }
    { i_to_a((long)g_stats.peak_heap_kb, vbuf); strcat(vbuf, "kb");
      row_kv("Peak heap used", vbuf, 0); }

    /* ----- PERFORMANCE ----- */
    emit_border(s_ml, s_mr);
    row_hdr("PERFORMANCE");
    row_kv("CPU mode",       cpu_name(cpu_mode), 0);
    row_kv("Compiler target", "Watcom -4 medium", 0);
    row_kv("Vsync misses",   i_to_a((long)g_stats.vsync_misses, vbuf),
           (g_stats.vsync_misses == 0) ? "ROCK SOLID" : 0);
    row_kv("Sprite cache hits", u32_commas((unsigned long)g_stats.sprite_cache_hits, vbuf), 0);
    row_kv("March events",   u32_commas((unsigned long)g_stats.march_events, vbuf), 0);

    /* ----- BUILD ----- */
    emit_border(s_ml, s_mr);
    row_hdr("BUILD");
    row_kv("Version",   BUILD_VERSION, 0);
    row_kv("Build date", BUILD_DATE, 0);
    row_kv("Compiler",  BUILD_COMPILER, 0);
    emit_border(s_bl, s_br);

    dos_print("\r\n  Press any key to exit.\r\n$");
    wait_key();
}
