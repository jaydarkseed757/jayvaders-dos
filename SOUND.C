/*
 * SOUND.C -- PC Speaker / Sound Blaster / SB Pro audio layer (build step 6).
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * Three tiers:
 *   SOUND_SPK  -- PIT counter 2 + gate port 61h; always available.
 *   SOUND_SB   -- 8-bit mono DMA, 11025 Hz; IRQ-driven.
 *   SOUND_SBP  -- 8-bit stereo DMA, 22050 Hz; hardware panning via
 *                 voice register 0x04 (D3).
 *
 * SOUNDS.DAT: implements cache_generate_sounds / cache_save_sounds /
 *             cache_load_sounds (replacing the stubs that were in CACHE.C).
 */

#include <i86.h>    /* inp, outp, FP_SEG, FP_OFF                */
#include <dos.h>    /* _dos_getvect, _dos_setvect               */
#include <stdio.h>  /* FILE, fopen, fread, fwrite, fclose       */
#include <stdlib.h> /* malloc, free                             */
#include <string.h> /* memcpy                                   */
#include "CACHE.H"
#include "SOUND.H"

/* =========================================================================
 * Port constants
 * =========================================================================*/

/* PC Speaker */
#define SPK_PIT_MODE  0x43
#define SPK_PIT_DATA  0x42
#define SPK_GATE      0x61

/* 8259A PIC */
#define PIC_CMD       0x20
#define PIC_EOI       0x20

/* =========================================================================
 * Module state
 * =========================================================================*/

static SoundConfig s_cfg;

/* Priority of the currently playing sound (0 = idle). */
static int s_cur_pri;

/* Queued next SB/SBP event (-1 = none). */
static int  s_next_event;
static int  s_next_pri;
static int  s_next_x;

/* PC Speaker: frames remaining on current note; current event being played. */
static int s_spk_frames;
static int s_spk_event;
static int s_spk_phase;     /* generic per-event phase counter */

/* SB/SBP: saved IRQ vector. */
static void (__interrupt __far *s_old_irq_vec)(void);

/* DMA transfer buffer -- static so physical address is deterministic. */
#define DMA_BUF_SIZE  4096
static unsigned char s_dma_buf[DMA_BUF_SIZE];

/* Current SBP pan position (kept for IRQ re-trigger). */
static int s_cur_screen_x;

/* SOUNDS.DAT PCM pool. */
static unsigned char       *s_snd_buf;
static unsigned long        s_snd_off[SND_COUNT];
static unsigned long        s_snd_len[SND_COUNT];

/* =========================================================================
 * Section 1 -- PC Speaker
 * =========================================================================*/

/* March frequencies (Hz): 4-note authentic Space Invaders sequence. */
static const int s_march_hz[4] = { 160, 130, 100, 80 };

/* Frames per note at ~70 Hz vertical retrace. */
#define MARCH_FRAMES  4

/* Multi-note PC Speaker events: notes per event, Hz arrays. */
static const int s_fire_hz[6]  = { 300, 550, 800, 1100, 1500, 2000 };
static const int s_exp_hz[2]   = { 80, 300 };
static const int s_death_hz[6] = { 800, 650, 500, 380, 270, 180 };
static const int s_ufohit_hz[4]= { 200, 280, 380, 500 };
static const int s_clear_hz[6] = { 330, 440, 523, 659, 784, 1047 };
/* Title: simple 8-note melody loop. */
static const int s_title_hz[8] = { 330, 392, 494, 523, 440, 392, 330, 262 };

#define FIRE_FRAMES    6
#define DEATH_FRAMES  10
#define UFOHIT_FRAMES  4
#define CLEAR_FRAMES   8
#define TITLE_FRAMES  12

static void spk_tone(int freq)
{
    unsigned int div = (unsigned int)(1193180UL / (unsigned long)(unsigned int)freq);
    outp(SPK_PIT_MODE, 0xB6);
    outp(SPK_PIT_DATA, (unsigned char)(div & 0xFF));
    outp(SPK_PIT_DATA, (unsigned char)((div >> 8) & 0xFF));
    outp(SPK_GATE, inp(SPK_GATE) | 0x03);
}

static void spk_silence(void)
{
    outp(SPK_GATE, inp(SPK_GATE) & ~0x03);
}

/* Start a new speaker event. */
static void spk_start(int event)
{
    s_spk_event = event;
    s_spk_phase = 0;
    switch (event) {
    case SND_MARCH0: case SND_MARCH1: case SND_MARCH2: case SND_MARCH3:
        spk_tone(s_march_hz[event - SND_MARCH0]);
        s_spk_frames = MARCH_FRAMES;
        break;
    case SND_FIRE:
        spk_tone(s_fire_hz[0]);
        s_spk_frames = FIRE_FRAMES;
        break;
    case SND_INV_EXPLODE:
        spk_tone(s_exp_hz[0]);
        s_spk_frames = 8;
        break;
    case SND_PLR_DEATH:
        spk_tone(s_death_hz[0]);
        s_spk_frames = DEATH_FRAMES;
        break;
    case SND_UFO_PASS:
        spk_tone(200);
        s_spk_frames = 1;
        break;
    case SND_UFO_HIT:
        spk_tone(s_ufohit_hz[0]);
        s_spk_frames = UFOHIT_FRAMES;
        break;
    case SND_SHIELD_HIT:
        spk_tone(120);
        s_spk_frames = 2;
        break;
    case SND_LEVEL_CLEAR:
        spk_tone(s_clear_hz[0]);
        s_spk_frames = CLEAR_FRAMES;
        break;
    case SND_TITLE:
        spk_tone(s_title_hz[0]);
        s_spk_frames = TITLE_FRAMES;
        break;
    default:
        s_spk_frames = 0;
        break;
    }
}

/* =========================================================================
 * Section 2 -- SB detection
 * =========================================================================*/

static int parse_blaster(SoundConfig *cfg)
{
    char *env;
    char *p;
    int val;

    env = getenv("BLASTER");
    if (!env)
        return 0;

    p = env;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (*p == 'A' || *p == 'a') {
            p++;
            val = 0;
            while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
            /* Treat as hex port (e.g. "220" means 0x220) */
            cfg->port = val;
        } else if (*p == 'I' || *p == 'i') {
            p++;
            val = 0;
            while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
            cfg->irq = val;
        } else if (*p == 'D' || *p == 'd') {
            p++;
            val = 0;
            while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
            cfg->dma = val;
        } else {
            /* Skip unknown token */
            while (*p && *p != ' ' && *p != '\t') p++;
        }
    }
    return (cfg->port > 0) ? 1 : 0;
}

static int sb_dsp_reset(int base)
{
    int i;
    outp(base + 6, 1);
    for (i = 0; i < 100; i++) inp(0x80);   /* ~3.3 us delay per read */
    outp(base + 6, 0);
    for (i = 0; i < 100; i++) {
        if (inp(base + 0xE) & 0x80) {
            if (inp(base + 0xA) == 0xAA)
                return 1;
        }
    }
    return 0;
}

static void sb_write_cmd(int base, unsigned char cmd)
{
    int i;
    for (i = 0; i < 65535; i++) {
        if (!(inp(base + 0xC) & 0x80)) {
            outp(base + 0xC, cmd);
            return;
        }
    }
}

static unsigned char sb_read_data(int base)
{
    int i;
    for (i = 0; i < 65535; i++) {
        if (inp(base + 0xE) & 0x80)
            return (unsigned char)inp(base + 0xA);
    }
    return 0;
}

static void sb_get_version(int base, int *hi, int *lo)
{
    sb_write_cmd(base, 0xE1);
    *hi = (int)sb_read_data(base);
    *lo = (int)sb_read_data(base);
}

int sound_detect(SoundConfig *cfg)
{
    static const int s_scan_ports[4] = { 0x210, 0x220, 0x230, 0x240 };
    int i, hi, lo, found_port;

    cfg->device      = SOUND_SPK;
    cfg->port        = 0;
    cfg->irq         = 5;
    cfg->dma         = 1;
    cfg->sample_rate = 0;

    found_port = 0;

    /* Try BLASTER environment variable first. */
    if (parse_blaster(cfg) && sb_dsp_reset(cfg->port)) {
        found_port = cfg->port;
    } else {
        /* Port scan. */
        cfg->port = 0;
        for (i = 0; i < 4; i++) {
            if (sb_dsp_reset(s_scan_ports[i])) {
                found_port = s_scan_ports[i];
                cfg->port  = found_port;
                break;
            }
        }
    }

    if (!found_port)
        return SOUND_SPK;

    /* Identify card type from DSP version. */
    sb_get_version(found_port, &hi, &lo);
    if (hi >= 3)
        cfg->device = SOUND_SBP;
    else
        cfg->device = SOUND_SB;

    cfg->sample_rate = (cfg->device == SOUND_SBP) ? 22050 : 11025;
    return cfg->device;
}

/* =========================================================================
 * Section 3 -- DMA + SB IRQ
 * =========================================================================*/

/* DMA controller port tables (channels 0-3). */
static const unsigned char s_dma_page_port[4] = { 0x87, 0x83, 0x81, 0x82 };
static const unsigned char s_dma_addr_port[4] = { 0x00, 0x02, 0x04, 0x06 };
static const unsigned char s_dma_cnt_port[4]  = { 0x01, 0x03, 0x05, 0x07 };

static void dma_setup(int channel, unsigned char *buf, unsigned int len)
{
    unsigned long phys = ((unsigned long)FP_SEG(buf) << 4)
                       + (unsigned long)FP_OFF(buf);
    unsigned char page = (unsigned char)((phys >> 16) & 0xFF);
    int ch = channel & 3;

    outp(0x0A, 0x04 | ch);                         /* mask channel          */
    outp(0x0C, 0);                                  /* flip-flop reset       */
    outp(0x0B, 0x48 | ch);                          /* single-cycle read     */
    outp(s_dma_addr_port[ch], (unsigned char)(phys & 0xFF));
    outp(s_dma_addr_port[ch], (unsigned char)((phys >> 8) & 0xFF));
    outp(s_dma_page_port[ch], page);
    outp(s_dma_cnt_port[ch],  (unsigned char)((len - 1) & 0xFF));
    outp(s_dma_cnt_port[ch],  (unsigned char)(((len - 1) >> 8) & 0xFF));
    outp(0x0A, ch);                                 /* unmask channel        */
}

static void sbp_set_pan(int screen_x)
{
    int right = screen_x * 15 / 319;
    int left  = 15 - right;
    outp(s_cfg.port + 0x4, 0x04);
    outp(s_cfg.port + 0x5, (unsigned char)((left << 4) | right));
}

static void sb_set_rate(int base, int rate)
{
    unsigned char tc = (unsigned char)(256 - 1000000 / rate);
    sb_write_cmd(base, 0x40);
    sb_write_cmd(base, tc);
}

static void sb_play_dma(int base, unsigned int len)
{
    sb_write_cmd(base, 0x14);
    sb_write_cmd(base, (unsigned char)((len - 1) & 0xFF));
    sb_write_cmd(base, (unsigned char)(((len - 1) >> 8) & 0xFF));
}

/* Copy event PCM into DMA buffer and start playback. */
static void dma_start_event(int event, int screen_x)
{
    unsigned int copy_len;

    if (!s_snd_buf || event < 0 || event >= SND_COUNT)
        return;

    copy_len = (unsigned int)(s_snd_len[event] < DMA_BUF_SIZE
                              ? s_snd_len[event] : DMA_BUF_SIZE);
    memcpy(s_dma_buf, s_snd_buf + s_snd_off[event], copy_len);

    if (s_cfg.device == SOUND_SBP)
        sbp_set_pan(screen_x);

    dma_setup(s_cfg.dma, s_dma_buf, copy_len);
    sb_set_rate(s_cfg.port, s_cfg.sample_rate);
    sb_play_dma(s_cfg.port, copy_len);
}

/* IRQ vector lookup for IRQ line. */
static int irq_to_int(int irq)
{
    switch (irq) {
    case 2: return 0x0A;
    case 5: return 0x0D;
    case 7: return 0x0F;
    default: return 0x0D;
    }
}

static void __interrupt __far sb_irq_handler(void)
{
    inp(s_cfg.port + 0xE);             /* acknowledge SB interrupt */

    if (s_next_event >= 0) {
        s_cur_pri   = s_next_pri;
        dma_start_event(s_next_event, s_next_x);
        s_next_event = -1;
    } else {
        s_cur_pri = 0;
    }

    outp(PIC_CMD, PIC_EOI);
}

/* =========================================================================
 * Section 4 -- PCM synthesis (integer arithmetic only)
 * =========================================================================*/

/* LCG noise; seed carried across calls for variety. */
static unsigned long s_noise_rng = 12345UL;
static unsigned char noise_byte(void)
{
    s_noise_rng = (s_noise_rng * 1103515245UL + 12345UL) & 0x7FFFFFFFUL;
    return (unsigned char)(s_noise_rng & 0xFF);
}

/* Square wave: 200 (high) or 56 (low). */
static unsigned char square_wave(int phase, int period)
{
    if (period < 1) period = 1;
    return ((phase % period) < (period / 2)) ? 200 : 56;
}

/* Linear-decay envelope around 128 centre. */
static unsigned char decay_env(unsigned char v, int i, int total)
{
    int centre = 128;
    int dist   = (int)v - centre;
    dist = (int)((long)dist * (total - i) / total);
    return (unsigned char)(centre + dist);
}

/* Write n samples of a square wave at freq Hz centred at 128 with decay. */
static void synth_square(unsigned char **pp, int n, int freq, int sample_rate)
{
    int period = (freq > 0) ? sample_rate / freq : 1;
    int i;
    for (i = 0; i < n; i++) {
        unsigned char v = square_wave(i, period);
        *(*pp)++ = decay_env(v, i, n);
    }
}

/* Write all SND_COUNT sounds into *out; record s_snd_off[] and s_snd_len[]. */
static void synthesize_all(unsigned char *out, int sample_rate)
{
    unsigned char *pp = out;
    unsigned long  off = 0;
    int i, n, note, period, freq;

    /* Convenience: samples per ms at this rate. */
#define MS(ms) ((int)((long)(ms) * sample_rate / 1000))

    /* --- SND_MARCH0 .. SND_MARCH3 --- */
    for (note = 0; note < 4; note++) {
        n = MS(30);
        s_snd_off[SND_MARCH0 + note] = off;
        s_snd_len[SND_MARCH0 + note] = (unsigned long)n;
        synth_square(&pp, n, s_march_hz[note], sample_rate);
        off += (unsigned long)n;
    }

    /* --- SND_FIRE: 300→2000 Hz upsweep over 100ms --- */
    {
        n = MS(100);
        s_snd_off[SND_FIRE] = off;
        s_snd_len[SND_FIRE] = (unsigned long)n;
        for (i = 0; i < n; i++) {
            freq   = 300 + (int)((long)(2000 - 300) * i / n);
            period = (freq > 0) ? sample_rate / freq : 1;
            *pp++  = square_wave(i, period);
        }
        off += (unsigned long)n;
    }

    /* --- SND_INV_EXPLODE: noise × decay, 200ms --- */
    {
        n = MS(200);
        s_snd_off[SND_INV_EXPLODE] = off;
        s_snd_len[SND_INV_EXPLODE] = (unsigned long)n;
        for (i = 0; i < n; i++) {
            unsigned char nb = noise_byte();
            *pp++ = decay_env(nb, i, n);
        }
        off += (unsigned long)n;
    }

    /* --- SND_PLR_DEATH: 6 descending notes, ~133ms each, total 800ms --- */
    {
        static const int death_hz[6] = { 800, 650, 500, 380, 270, 180 };
        int seg;
        n = MS(800);
        s_snd_off[SND_PLR_DEATH] = off;
        s_snd_len[SND_PLR_DEATH] = (unsigned long)n;
        seg = n / 6;
        for (note = 0; note < 6; note++) {
            int cnt = (note < 5) ? seg : n - seg * 5;
            synth_square(&pp, cnt, death_hz[note], sample_rate);
        }
        off += (unsigned long)n;
    }

    /* --- SND_UFO_PASS: 200/170 Hz alternating per 30 samples, 500ms --- */
    {
        int seg = MS(30) > 0 ? MS(30) : 1;
        n = MS(500);
        s_snd_off[SND_UFO_PASS] = off;
        s_snd_len[SND_UFO_PASS] = (unsigned long)n;
        for (i = 0; i < n; i++) {
            freq   = ((i / seg) & 1) ? 170 : 200;
            period = sample_rate / freq;
            *pp++  = square_wave(i, period);
        }
        off += (unsigned long)n;
    }

    /* --- SND_UFO_HIT: 4 ascending notes, ~50ms each, total 200ms --- */
    {
        static const int ufohit_hz[4] = { 200, 280, 380, 500 };
        int seg;
        n = MS(200);
        s_snd_off[SND_UFO_HIT] = off;
        s_snd_len[SND_UFO_HIT] = (unsigned long)n;
        seg = n / 4;
        for (note = 0; note < 4; note++) {
            int cnt = (note < 3) ? seg : n - seg * 3;
            synth_square(&pp, cnt, ufohit_hz[note], sample_rate);
        }
        off += (unsigned long)n;
    }

    /* --- SND_SHIELD_HIT: 120 Hz, 20ms --- */
    {
        n = MS(20);
        s_snd_off[SND_SHIELD_HIT] = off;
        s_snd_len[SND_SHIELD_HIT] = (unsigned long)n;
        synth_square(&pp, n, 120, sample_rate);
        off += (unsigned long)n;
    }

    /* --- SND_LEVEL_CLEAR: 6 ascending notes, ~83ms each, 500ms total --- */
    {
        static const int clear_hz[6] = { 330, 440, 523, 659, 784, 1047 };
        int seg;
        n = MS(500);
        s_snd_off[SND_LEVEL_CLEAR] = off;
        s_snd_len[SND_LEVEL_CLEAR] = (unsigned long)n;
        seg = n / 6;
        for (note = 0; note < 6; note++) {
            int cnt = (note < 5) ? seg : n - seg * 5;
            synth_square(&pp, cnt, clear_hz[note], sample_rate);
        }
        off += (unsigned long)n;
    }

    /* --- SND_TITLE: 8-note melody loop, ~250ms per note, 2s total --- */
    {
        static const int title_hz[8] = { 330, 392, 494, 523, 440, 392, 330, 262 };
        int seg;
        n = MS(2000);
        s_snd_off[SND_TITLE] = off;
        s_snd_len[SND_TITLE] = (unsigned long)n;
        seg = n / 8;
        for (note = 0; note < 8; note++) {
            int cnt = (note < 7) ? seg : n - seg * 7;
            synth_square(&pp, cnt, title_hz[note], sample_rate);
        }
        off += (unsigned long)n;
    }

#undef MS
}

/* Compute total PCM bytes for a given sample rate. */
static unsigned long total_pcm_bytes(int sample_rate)
{
#define MS2(ms) ((unsigned long)(ms) * (unsigned long)sample_rate / 1000UL)
    unsigned long t = 0;
    t += MS2(30) * 4;   /* 4 march notes */
    t += MS2(100);      /* fire          */
    t += MS2(200);      /* inv explode   */
    t += MS2(800);      /* plr death     */
    t += MS2(500);      /* ufo pass      */
    t += MS2(200);      /* ufo hit       */
    t += MS2(20);       /* shield hit    */
    t += MS2(500);      /* level clear   */
    t += MS2(2000);     /* title         */
    return t;
#undef MS2
}

/* =========================================================================
 * Section 5 -- SOUNDS.DAT I/O
 * =========================================================================*/

#define DAT_MAGIC0  'S'
#define DAT_MAGIC1  'N'
#define DAT_VER_HI  0x00
#define DAT_VER_LO  0x01

int cache_generate_sounds(void)
{
    unsigned long total;

    if (s_cfg.sample_rate <= 0)
        return 0;

    sound_free();

    total = total_pcm_bytes(s_cfg.sample_rate);
    s_snd_buf = (unsigned char *)malloc((unsigned int)total);
    if (!s_snd_buf)
        return 0;

    synthesize_all(s_snd_buf, s_cfg.sample_rate);
    return 1;
}

int cache_save_sounds(void)
{
    FILE *f;
    unsigned char hdr[8];
    int i;

    if (!s_snd_buf)
        return 0;

    f = fopen("SOUNDS.DAT", "wb");
    if (!f)
        return 0;

    hdr[0] = DAT_MAGIC0;
    hdr[1] = DAT_MAGIC1;
    hdr[2] = DAT_VER_HI;
    hdr[3] = DAT_VER_LO;
    hdr[4] = (unsigned char)(s_cfg.sample_rate & 0xFF);
    hdr[5] = (unsigned char)((s_cfg.sample_rate >> 8) & 0xFF);
    hdr[6] = 0x00;
    hdr[7] = 0x00;
    fwrite(hdr, 1, 8, f);

    for (i = 0; i < SND_COUNT; i++) {
        unsigned char b[4];
        b[0] = (unsigned char)(s_snd_off[i] & 0xFF);
        b[1] = (unsigned char)((s_snd_off[i] >> 8) & 0xFF);
        b[2] = (unsigned char)((s_snd_off[i] >> 16) & 0xFF);
        b[3] = (unsigned char)((s_snd_off[i] >> 24) & 0xFF);
        fwrite(b, 1, 4, f);
    }
    for (i = 0; i < SND_COUNT; i++) {
        unsigned char b[4];
        b[0] = (unsigned char)(s_snd_len[i] & 0xFF);
        b[1] = (unsigned char)((s_snd_len[i] >> 8) & 0xFF);
        b[2] = (unsigned char)((s_snd_len[i] >> 16) & 0xFF);
        b[3] = (unsigned char)((s_snd_len[i] >> 24) & 0xFF);
        fwrite(b, 1, 4, f);
    }

    /* Write the PCM: total is last offset + last len */
    {
        unsigned long total = s_snd_off[SND_COUNT-1] + s_snd_len[SND_COUNT-1];
        fwrite(s_snd_buf, 1, (unsigned int)total, f);
    }

    fclose(f);
    return 1;
}

int cache_load_sounds(void)
{
    FILE *f;
    unsigned char hdr[8];
    int stored_rate;
    int i;
    unsigned long total;

    f = fopen("SOUNDS.DAT", "rb");
    if (!f)
        return 0;

    if (fread(hdr, 1, 8, f) != 8) { fclose(f); return 0; }
    if (hdr[0] != DAT_MAGIC0 || hdr[1] != DAT_MAGIC1) { fclose(f); return 0; }
    if (hdr[2] != DAT_VER_HI  || hdr[3] != DAT_VER_LO)  { fclose(f); return 0; }

    stored_rate = (int)hdr[4] | ((int)hdr[5] << 8);
    if (stored_rate != s_cfg.sample_rate) { fclose(f); return 0; }

    sound_free();

    for (i = 0; i < SND_COUNT; i++) {
        unsigned char b[4];
        if (fread(b, 1, 4, f) != 4) { fclose(f); return 0; }
        s_snd_off[i] = (unsigned long)b[0]
                     | ((unsigned long)b[1] << 8)
                     | ((unsigned long)b[2] << 16)
                     | ((unsigned long)b[3] << 24);
    }
    for (i = 0; i < SND_COUNT; i++) {
        unsigned char b[4];
        if (fread(b, 1, 4, f) != 4) { fclose(f); return 0; }
        s_snd_len[i] = (unsigned long)b[0]
                     | ((unsigned long)b[1] << 8)
                     | ((unsigned long)b[2] << 16)
                     | ((unsigned long)b[3] << 24);
    }

    total = s_snd_off[SND_COUNT-1] + s_snd_len[SND_COUNT-1];
    s_snd_buf = (unsigned char *)malloc((unsigned int)total);
    if (!s_snd_buf) { fclose(f); return 0; }

    if (fread(s_snd_buf, 1, (unsigned int)total, f) != (unsigned int)total) {
        free(s_snd_buf);
        s_snd_buf = NULL;
        fclose(f);
        return 0;
    }

    fclose(f);
    return 1;
}

void sound_free(void)
{
    if (s_snd_buf) {
        free(s_snd_buf);
        s_snd_buf = NULL;
    }
    memset(s_snd_off, 0, sizeof(s_snd_off));
    memset(s_snd_len, 0, sizeof(s_snd_len));
}

/* =========================================================================
 * Section 6 -- Public API
 * =========================================================================*/

void sound_init(const SoundConfig *cfg)
{
    int vec;

    memcpy(&s_cfg, cfg, sizeof(SoundConfig));
    s_cur_pri    = 0;
    s_next_event = -1;
    s_next_pri   = 0;
    s_next_x     = 160;
    s_spk_frames = 0;
    s_spk_event  = -1;
    s_spk_phase  = 0;

    if (cfg->device == SOUND_SB || cfg->device == SOUND_SBP) {
        vec = irq_to_int(cfg->irq);
        s_old_irq_vec = _dos_getvect((unsigned)vec);
        _dos_setvect((unsigned)vec, sb_irq_handler);
        /* Reset DSP so it is ready for playback. */
        sb_dsp_reset(cfg->port);
    }
}

void sound_shutdown(void)
{
    int vec;

    if (s_cfg.device == SOUND_SB || s_cfg.device == SOUND_SBP) {
        /* Silence DSP: speaker off command 0xD3. */
        sb_write_cmd(s_cfg.port, 0xD3);
        vec = irq_to_int(s_cfg.irq);
        _dos_setvect((unsigned)vec, s_old_irq_vec);
    }
    spk_silence();
    s_cur_pri    = 0;
    s_next_event = -1;
    s_spk_frames = 0;
}

void sound_play(int event, int priority, int screen_x)
{
    if (event < 0 || event >= SND_COUNT) return;

    if (s_cfg.device == SOUND_SPK) {
        /* PC Speaker: lower priority number wins. */
        if (s_cur_pri == 0 || priority <= s_cur_pri) {
            s_cur_pri = priority;
            spk_start(event);
        }
        return;
    }

    /* SB / SBP */
    if (s_cur_pri == 0) {
        /* Channel idle -- play immediately. */
        s_cur_pri      = priority;
        s_cur_screen_x = screen_x;
        dma_start_event(event, screen_x);
    } else if (priority < s_cur_pri) {
        /* Higher priority: queue. */
        s_next_event = event;
        s_next_pri   = priority;
        s_next_x     = screen_x;
    }
    /* Equal or lower priority: ignore. */
}

void sound_stop(void)
{
    if (s_cfg.device == SOUND_SB || s_cfg.device == SOUND_SBP) {
        sb_write_cmd(s_cfg.port, 0xD0);    /* pause DMA */
    }
    spk_silence();
    s_cur_pri    = 0;
    s_next_event = -1;
    s_spk_frames = 0;
}

void sound_update(void)
{
    if (s_cfg.device != SOUND_SPK)
        return;

    if (s_spk_frames <= 0)
        return;

    s_spk_frames--;

    if (s_spk_frames > 0)
        return;

    /* Current note expired -- advance phase or silence. */
    s_spk_phase++;

    switch (s_spk_event) {
    case SND_MARCH0: case SND_MARCH1: case SND_MARCH2: case SND_MARCH3:
        spk_silence();
        s_cur_pri = 0;
        break;

    case SND_FIRE:
        if (s_spk_phase < FIRE_FRAMES) {
            spk_tone(s_fire_hz[s_spk_phase]);
            s_spk_frames = 1;
        } else {
            spk_silence();
            s_cur_pri = 0;
        }
        break;

    case SND_INV_EXPLODE:
        if (s_spk_phase < 8) {
            spk_tone(s_exp_hz[s_spk_phase & 1]);
            s_spk_frames = 1;
        } else {
            spk_silence();
            s_cur_pri = 0;
        }
        break;

    case SND_PLR_DEATH:
        if (s_spk_phase < 6) {
            spk_tone(s_death_hz[s_spk_phase]);
            s_spk_frames = DEATH_FRAMES;
        } else {
            spk_silence();
            s_cur_pri = 0;
        }
        break;

    case SND_UFO_PASS:
        /* Loops indefinitely until preempted. */
        spk_tone((s_spk_phase & 1) ? 170 : 200);
        s_spk_frames = 1;
        break;

    case SND_UFO_HIT:
        if (s_spk_phase < 4) {
            spk_tone(s_ufohit_hz[s_spk_phase]);
            s_spk_frames = UFOHIT_FRAMES;
        } else {
            spk_silence();
            s_cur_pri = 0;
        }
        break;

    case SND_SHIELD_HIT:
        spk_silence();
        s_cur_pri = 0;
        break;

    case SND_LEVEL_CLEAR:
        if (s_spk_phase < 6) {
            spk_tone(s_clear_hz[s_spk_phase]);
            s_spk_frames = CLEAR_FRAMES;
        } else {
            spk_silence();
            s_cur_pri = 0;
        }
        break;

    case SND_TITLE:
        /* Loops through 8-note melody indefinitely. */
        spk_tone(s_title_hz[s_spk_phase & 7]);
        s_spk_frames = TITLE_FRAMES;
        break;

    default:
        spk_silence();
        s_cur_pri = 0;
        break;
    }
}

/* =========================================================================
 * SOUND_TEST -- text-mode self-test, no EGA required.
 *
 * Compile:
 *   wcc -ml -zf -DSOUND_TEST SOUND.C
 *   wlink system dos file SOUND.obj name SOUNDTEST
 *
 * Expected output: hardware detection report, then each sound event played.
 * =========================================================================*/
#ifdef SOUND_TEST

#include <conio.h>

static const char *s_snd_names[SND_COUNT] = {
    "SND_MARCH0", "SND_MARCH1", "SND_MARCH2", "SND_MARCH3",
    "SND_FIRE", "SND_INV_EXPLODE", "SND_PLR_DEATH",
    "SND_UFO_PASS", "SND_UFO_HIT", "SND_SHIELD_HIT",
    "SND_LEVEL_CLEAR", "SND_TITLE"
};

/* Spin ~1 second via port 3DAh vsync count (no float, no delay()). */
static void wait_vsyncs(int count)
{
    int i;
    for (i = 0; i < count; i++) {
        while (inp(0x3DA) & 0x08);
        while (!(inp(0x3DA) & 0x08));
    }
}

int main(void)
{
    SoundConfig cfg;
    char *blaster_env;
    int  i, key;
    int  dsp_hi, dsp_lo;
    const char *card_name;
    int  port, reset_ok;

    puts("Sound Blaster Detection");
    puts("-----------------------");

    blaster_env = getenv("BLASTER");
    if (blaster_env)
        printf("BLASTER env : %s\n", blaster_env);
    else
        puts("BLASTER env : not set");

    /* Run detection. */
    sound_detect(&cfg);

    if (cfg.port)
        printf("Base port   : %Xh\n", cfg.port);

    if (cfg.device != SOUND_SPK) {
        reset_ok = sb_dsp_reset(cfg.port);
        printf("DSP reset   : %s\n", reset_ok ? "OK" : "FAIL");

        if (reset_ok) {
            sb_get_version(cfg.port, &dsp_hi, &dsp_lo);
            printf("DSP version : %d.%02d\n", dsp_hi, dsp_lo);
        }
    }

    switch (cfg.device) {
    case SOUND_SPK: card_name = "PC Speaker";       break;
    case SOUND_SB:  card_name = "Sound Blaster";    break;
    case SOUND_SBP: card_name = "Sound Blaster Pro"; break;
    default:        card_name = "None";              break;
    }
    printf("Card type   : %s\n", card_name);

    if (cfg.device != SOUND_SPK) {
        printf("IRQ         : %d\n", cfg.irq);
        printf("DMA         : %d\n", cfg.dma);
    }

    /* Generate sounds. */
    sound_init(&cfg);
    printf("\nGenerating SOUNDS.DAT...");
    if (cache_generate_sounds()) {
        puts(" OK");
        printf("Saving SOUNDS.DAT...");
        puts(cache_save_sounds() ? " OK" : " FAIL");
    } else {
        puts(" FAIL (malloc?)");
    }

    printf("\nTesting PC Speaker...     ");
    spk_tone(440);
    wait_vsyncs(35);
    spk_silence();
    puts("OK");

    if (cfg.device == SOUND_SB || cfg.device == SOUND_SBP) {
        printf("Testing SB DAC output...  ");
        if (s_snd_buf) {
            dma_start_event(SND_FIRE, 160);
            wait_vsyncs(15);
            puts("OK");
        } else {
            puts("SKIP (no PCM buffer)");
        }
        if (cfg.device == SOUND_SBP) {
            printf("Testing SB Pro stereo...  ");
            if (s_snd_buf) {
                sbp_set_pan(80);
                dma_start_event(SND_FIRE, 80);
                wait_vsyncs(15);
                puts("OK");
            } else {
                puts("SKIP (no PCM buffer)");
            }
        }
    }

    puts("\nPress any key to play each sound event, ESC to exit.");

    for (i = 0; i < SND_COUNT; i++) {
        printf("  [%-18s] -- press key...", s_snd_names[i]);
        while (!kbhit());
        key = getch();
        if (key == 27) { puts(""); break; }

        /* Play the event. */
        sound_play(i, SPRI_CRITICAL, 160);
        /* For PC Speaker: tick update for multi-note sounds ~35 frames. */
        if (cfg.device == SOUND_SPK) {
            int f;
            for (f = 0; f < 100; f++) {
                sound_update();
                wait_vsyncs(1);
            }
        } else {
            wait_vsyncs(90);
        }
        sound_stop();
        puts(" OK");
    }

    sound_shutdown();
    sound_free();
    puts("\nDone.");
    return 0;
}

#endif /* SOUND_TEST */
