/*
 * EGA.C -- EGA hardware display layer
 *
 * Targets Open Watcom wcc, medium memory model (-ml), 16-bit real mode DOS.
 * All timing via vertical retrace (port 3DAh). No floating point. No libs.
 *
 * Planar blit layout expected by ega_blit_planar(): see EGA.H comment.
 */

#include <i86.h>    /* int86, union REGS, MK_FP */
#include <conio.h>  /* inp, outp                */
#include "EGA.H"
#include "STATS.H"  /* g_stats frame / vsync / blit counters */

/* VRAM base as a far pointer -- A000:0000 in real mode */
#define VRAM  ((unsigned char far *)MK_FP(EGA_SEGMENT, 0))

/* =========================================================================
 * Static dirty-rect table
 * =========================================================================*/

static DirtyRect s_dirty[MAX_DIRTY_RECTS];
static int       s_dirty_count = 0;

/* =========================================================================
 * Mode / sync
 * =========================================================================*/

void ega_set_mode(void) {
    union REGS r;
    r.w.ax = 0x000D;       /* INT 10h AH=00h AL=0Dh: set EGA mode 0Dh */
    int86(0x10, &r, &r);

    /* Bring GC and Sequencer to a clean write-mode-0 state */
    outp(SEQ_INDEX, SEQ_MAP_MASK);       outp(SEQ_DATA, 0x0F); /* all planes */
    outp(GC_INDEX, GC_SET_RESET);        outp(GC_DATA,  0x00);
    outp(GC_INDEX, GC_ENABLE_SET_RESET); outp(GC_DATA,  0x00);
    outp(GC_INDEX, GC_DATA_ROTATE);      outp(GC_DATA,  0x00);
    outp(GC_INDEX, GC_MODE);             outp(GC_DATA,  0x00); /* write mode 0 */
    outp(GC_INDEX, GC_BIT_MASK);         outp(GC_DATA,  0xFF);
    outp(GC_INDEX, GC_READ_MAP);         outp(GC_DATA,  0x00); /* read plane 0 */
}

void ega_set_text_mode(void) {
    union REGS r;
    r.w.ax = 0x0003;       /* INT 10h AH=00h AL=03h: set text mode 03h */
    int86(0x10, &r, &r);
}

/*
 * Lock to the start of vertical retrace (~70 Hz).
 * First drain any in-progress retrace, then wait for the next rising edge.
 * This prevents frame tearing and gives a stable 70 Hz game tick.
 */
void ega_wait_vsync(void) {
    while ( inp(STATUS_REG) & 0x08) ;   /* wait until NOT in vsync */
    while (!(inp(STATUS_REG) & 0x08)) ; /* wait until vsync begins  */
    g_stats.frames_rendered++;
}

/* Non-blocking probe: 1 if the CRTC is currently in vertical retrace.
 * The game loop polls this at the top of a frame -- if a retrace is already
 * underway after a full frame of work, that frame overran its vsync window. */
int ega_vsync_active(void) {
    return (inp(STATUS_REG) & 0x08) ? 1 : 0;
}

/* =========================================================================
 * Plane helper
 * =========================================================================*/

void ega_set_plane(unsigned char mask) {
    outp(SEQ_INDEX, SEQ_MAP_MASK);
    outp(SEQ_DATA,  mask & 0x0F);
}

/* =========================================================================
 * Pixel ops
 *
 * EGA planar layout: pixel (x,y) occupies bit (7 - x%8) of byte
 * (y*40 + x/8) in each of the 4 planes.  Bit 7 is the leftmost pixel
 * in a byte group; bit 0 is the rightmost.
 * =========================================================================*/

void ega_put_pixel(int x, int y, unsigned char color) {
    unsigned int  offset;
    unsigned char bit_mask;
    unsigned char far *vram = VRAM;

    /* Use unsigned comparison to reject negatives and out-of-range together */
    if ((unsigned int)x >= SCREEN_WIDTH || (unsigned int)y >= SCREEN_HEIGHT)
        return;

    offset   = (unsigned int)y * SCREEN_BYTES + (unsigned int)(x >> 3);
    bit_mask = (unsigned char)(0x80u >> (x & 7));

    /* Write-mode 0 + Set/Reset: set all 4 plane bits for this pixel to color */
    outp(SEQ_INDEX, SEQ_MAP_MASK);       outp(SEQ_DATA,  0x0F);
    outp(GC_INDEX, GC_SET_RESET);        outp(GC_DATA,   color & 0x0F);
    outp(GC_INDEX, GC_ENABLE_SET_RESET); outp(GC_DATA,   0x0F);
    outp(GC_INDEX, GC_BIT_MASK);         outp(GC_DATA,   bit_mask);

    (void) vram[offset];   /* read to load all four plane latches */
    vram[offset] = 0;      /* write triggers Set/Reset -- value is ignored */

    outp(GC_INDEX, GC_BIT_MASK);         outp(GC_DATA, 0xFF);
    outp(GC_INDEX, GC_ENABLE_SET_RESET); outp(GC_DATA, 0x00);
}

/*
 * Read a pixel's 4-bit colour by reading each plane in turn.
 * Slow -- for off-screen checks, not the game render path.
 */
unsigned char ega_get_pixel(int x, int y) {
    unsigned int  offset;
    unsigned char color, bit_pos;
    int           plane;
    unsigned char far *vram = VRAM;

    if ((unsigned int)x >= SCREEN_WIDTH || (unsigned int)y >= SCREEN_HEIGHT)
        return 0;

    offset  = (unsigned int)y * SCREEN_BYTES + (unsigned int)(x >> 3);
    bit_pos = (unsigned char)(7 - (x & 7));
    color   = 0;

    for (plane = 0; plane < 4; plane++) {
        outp(GC_INDEX, GC_READ_MAP); outp(GC_DATA, (unsigned char)plane);
        if (vram[offset] & (1 << bit_pos))
            color |= (unsigned char)(1 << plane);
    }

    outp(GC_INDEX, GC_READ_MAP); outp(GC_DATA, 0x00); /* restore plane 0 */
    return color;
}

/* =========================================================================
 * Fills
 * =========================================================================*/

/*
 * Fill the entire screen with a single colour.
 * With Enable Set/Reset = 0x0F and Bit Mask = 0xFF every bit in every plane
 * is driven directly by the Set/Reset register -- no read-modify-write needed.
 */
void ega_clear(unsigned char color) {
    unsigned int i;
    unsigned char far *vram = VRAM;

    outp(SEQ_INDEX, SEQ_MAP_MASK);       outp(SEQ_DATA, 0x0F);
    outp(GC_INDEX, GC_SET_RESET);        outp(GC_DATA,  color & 0x0F);
    outp(GC_INDEX, GC_ENABLE_SET_RESET); outp(GC_DATA,  0x0F);
    outp(GC_INDEX, GC_BIT_MASK);         outp(GC_DATA,  0xFF);

    for (i = 0; i < PLANE_SIZE; i++)
        vram[i] = 0;   /* Set/Reset provides the actual plane data */

    outp(GC_INDEX, GC_ENABLE_SET_RESET); outp(GC_DATA, 0x00);
}

/*
 * Fill a rectangle with a solid colour.
 *
 * Handles byte-boundary edge alignment: for bytes that are only partially
 * covered, the Bit Mask register protects the unaffected pixels and the
 * latch-read preserves them.  Fully covered bytes skip the latch concern
 * but we still read for consistency -- it costs one memory cycle per byte
 * and keeps the code unconditional.
 *
 * Left  byte mask: 0xFF >> (x  % 8)        -- zeros bits left of x
 * Right byte mask: 0xFF << (7 - (x2 % 8))  -- zeros bits right of x2
 * Combined mask for a full byte: 0xFF.
 */
void ega_fill_rect(int x, int y, int w, int h, unsigned char color) {
    int          row, col, x2;
    int          byte_l, byte_r;
    unsigned int row_off;
    unsigned char left_mask, right_mask, mask;
    unsigned char far *vram = VRAM;

    /* Clip to screen */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_WIDTH)  w = SCREEN_WIDTH  - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    x2     = x + w - 1;
    byte_l = x  >> 3;
    byte_r = x2 >> 3;

    outp(SEQ_INDEX, SEQ_MAP_MASK);       outp(SEQ_DATA, 0x0F);
    outp(GC_INDEX, GC_SET_RESET);        outp(GC_DATA,  color & 0x0F);
    outp(GC_INDEX, GC_ENABLE_SET_RESET); outp(GC_DATA,  0x0F);

    for (row = y; row < y + h; row++) {
        row_off = (unsigned int)row * SCREEN_BYTES;

        for (col = byte_l; col <= byte_r; col++) {
            left_mask  = (col == byte_l)
                       ? (unsigned char)(0xFF >> (x & 7))
                       : (unsigned char)0xFF;
            right_mask = (col == byte_r)
                       ? (unsigned char)(0xFF << (7 - (x2 & 7)))
                       : (unsigned char)0xFF;
            mask = left_mask & right_mask;

            outp(GC_INDEX, GC_BIT_MASK); outp(GC_DATA, mask);
            (void) vram[row_off + col];  /* load latches for partial bytes */
            vram[row_off + col] = 0;
        }
    }

    outp(GC_INDEX, GC_BIT_MASK);         outp(GC_DATA, 0xFF);
    outp(GC_INDEX, GC_ENABLE_SET_RESET); outp(GC_DATA, 0x00);
}

/* =========================================================================
 * Palette (Attribute Controller)
 *
 * The AC uses a single port (3C0h) for both index and data writes, selected
 * by an internal flip-flop.  Reading port 3DAh resets the flip-flop to
 * index mode.  After palette programming, writing 0x20 to 3C0h re-enables
 * the video output (bit 5 = Palette Address Source).
 * =========================================================================*/

void ega_set_palette(unsigned char index, unsigned char value) {
    inp(STATUS_REG);               /* reset flip-flop to index mode */
    outp(AC_INDEX, index & 0x1F); /* write palette register index  */
    outp(AC_INDEX, value & 0x3F); /* write palette register value  */
    outp(AC_INDEX, 0x20);         /* re-enable video output         */
}

void ega_set_all_palette(const unsigned char *pal16) {
    int i;
    inp(STATUS_REG);
    for (i = 0; i < 16; i++) {
        outp(AC_INDEX, (unsigned char)i);
        outp(AC_INDEX, pal16[i] & 0x3F);
    }
    outp(AC_INDEX, 0x20);
}

/* Re-assert video enable after manual AC manipulation (e.g. colour cycling). */
void ega_palette_enable(void) {
    inp(STATUS_REG);
    outp(AC_INDEX, 0x20);
}

/* =========================================================================
 * Planar blit
 *
 * Copies a pre-generated plane-major sprite frame directly to VRAM with
 * one plane written at a time (Map Mask selects the target plane).
 * No Set/Reset -- raw CPU data written in write mode 0.
 *
 * This is the hot path for gameplay rendering.  Source must be in a far
 * memory buffer (use _fmalloc in CACHE.C).  x must be byte-aligned.
 * =========================================================================*/
void ega_blit_planar(int x, int y, int w_bytes, int h,
                     unsigned char far *src)
{
    int          plane, row, i;
    unsigned int plane_stride;
    unsigned int src_plane_off, src_row_off, dst_off;
    unsigned char far *vram = VRAM;

    if (w_bytes <= 0 || h <= 0) return;
    /* Reject out-of-bounds blits -- callers are responsible for clipping */
    if ((unsigned int)x >= SCREEN_WIDTH)  return;
    if ((unsigned int)y >= SCREEN_HEIGHT) return;
    if (x + w_bytes * 8 > SCREEN_WIDTH)  return;
    if (y + h > SCREEN_HEIGHT)           return;

    g_stats.sprite_cache_hits++;   /* one cached-frame blit to EGA memory */

    plane_stride = (unsigned int)h * (unsigned int)w_bytes;

    /* Raw CPU data, write mode 0, all bits, no Set/Reset */
    outp(GC_INDEX, GC_ENABLE_SET_RESET); outp(GC_DATA, 0x00);
    outp(GC_INDEX, GC_BIT_MASK);         outp(GC_DATA, 0xFF);
    outp(GC_INDEX, GC_DATA_ROTATE);      outp(GC_DATA, 0x00);
    outp(GC_INDEX, GC_MODE);             outp(GC_DATA, 0x00);

    for (plane = 0; plane < 4; plane++) {
        outp(SEQ_INDEX, SEQ_MAP_MASK);
        outp(SEQ_DATA, (unsigned char)(1 << plane));

        src_plane_off = (unsigned int)plane * plane_stride;

        for (row = 0; row < h; row++) {
            dst_off     = (unsigned int)(y + row) * SCREEN_BYTES
                        + (unsigned int)(x >> 3);
            src_row_off = src_plane_off
                        + (unsigned int)row * (unsigned int)w_bytes;
            for (i = 0; i < w_bytes; i++)
                vram[dst_off + i] = src[src_row_off + i];
        }
    }

    outp(SEQ_INDEX, SEQ_MAP_MASK); outp(SEQ_DATA, 0x0F); /* restore */
}

/* =========================================================================
 * Dirty-rectangle tracking
 *
 * The game loop calls ega_dirty_add() for every sprite it moves, then at
 * the start of the next frame calls ega_fill_rect() on each recorded rect
 * to erase the old position before drawing the new one.
 * ega_dirty_reset() clears the list at the end of each frame.
 * =========================================================================*/

void ega_dirty_reset(void) {
    s_dirty_count = 0;
}

void ega_dirty_add(int x, int y, int w, int h) {
    DirtyRect *r;

    /* Clip to screen before storing */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_WIDTH)  w = SCREEN_WIDTH  - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0)            return;
    if (s_dirty_count >= MAX_DIRTY_RECTS) return; /* table full, silently drop */

    r = &s_dirty[s_dirty_count++];
    r->x = x; r->y = y; r->w = w; r->h = h;
}

int ega_dirty_count(void) {
    return s_dirty_count;
}

DirtyRect *ega_dirty_get(int i) {
    if (i < 0 || i >= s_dirty_count) return 0;
    return &s_dirty[i];
}

/* =========================================================================
 * Visual self-test  (compile with -DEGA_TEST)
 *
 * Standalone smoke test -- verifies the EGA layer visually in DOSBox-X
 * before any other module exists.  Build with:
 *   wcc -ml -zf -DEGA_TEST EGA.C
 *   wcc -ml -zf STATS.C        (EGA now references g_stats)
 *   wlink system dos file { EGA.obj STATS.obj } name EGATEST.EXE
 *
 * Expected output:
 *   - 16 colour bars filling the screen
 *   - A white diagonal line across the lower half
 *   - A 3-second palette colour cycle on bar 3 (cyan)
 *   - Clean return to text mode on keypress
 * =========================================================================*/
#ifdef EGA_TEST

int main(void) {
    int           i, bar_h;
    unsigned char cycle_val;

    /*
     * Standard EGA attribute controller palette values for the 16 default
     * colours.  Written as 6-bit IRGB values accepted by the AC registers.
     */
    static const unsigned char default_pal[16] = {
        0x00, 0x01, 0x02, 0x03,  /*  0  1  2  3  black blue green cyan  */
        0x04, 0x05, 0x14, 0x07,  /*  4  5  6  7  red magenta brown lgray */
        0x38, 0x39, 0x3A, 0x3B,  /*  8  9 10 11  dgray bblue bgreen bcyan */
        0x3C, 0x3D, 0x3E, 0x3F   /* 12 13 14 15  bred bmag byellow white  */
    };

    ega_set_mode();
    ega_set_all_palette(default_pal);

    /* 16 colour bars, 12 pixels tall each (covers 192 of 200 rows) */
    bar_h = 12;
    for (i = 0; i < 16; i++)
        ega_fill_rect(0, i * bar_h, SCREEN_WIDTH, bar_h, (unsigned char)i);

    /* White diagonal -- one pixel per step, 2px down per step */
    for (i = 0; i < 100; i++)
        ega_put_pixel(i * 3, 128 + i, EGA_WHITE);

    /* Palette colour cycle on index 3 (cyan), ~3 seconds at 70 Hz */
    cycle_val = 0x03;
    for (i = 0; i < 210; i++) {
        ega_wait_vsync();
        cycle_val = (unsigned char)((cycle_val + 1) & 0x3F);
        ega_set_palette(3, cycle_val);
    }

    getch();         /* wait for keypress before restoring text mode */

    ega_set_text_mode();
    return 0;
}

#endif /* EGA_TEST */
