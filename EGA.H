#ifndef EGA_H
#define EGA_H

/*
 * EGA.H -- EGA hardware display layer constants and prototypes.
 * Open Watcom wcc, medium model (-ml), 16-bit real mode DOS.
 */

/* -------------------------------------------------------------------------
 * Screen geometry
 * -------------------------------------------------------------------------*/
#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  200
#define SCREEN_BYTES   40       /* bytes per scanline per plane (320/8)    */
#define PLANE_SIZE     8000     /* bytes per plane (SCREEN_BYTES * 200)    */
#define EGA_SEGMENT    0xA000U  /* VRAM segment for mode 0Dh               */

/* -------------------------------------------------------------------------
 * Video mode constants (INT 10h AL values)
 * -------------------------------------------------------------------------*/
#define EGA_MODE   0x0D  /* 320x200, 16 colour, planar  */
#define TEXT_MODE  0x03  /* 80x25 text, restore on exit */

/* -------------------------------------------------------------------------
 * I/O ports
 * -------------------------------------------------------------------------*/
#define SEQ_INDEX    0x3C4   /* Sequencer index register           */
#define SEQ_DATA     0x3C5   /* Sequencer data register            */
#define GC_INDEX     0x3CE   /* Graphics Controller index register */
#define GC_DATA      0x3CF   /* Graphics Controller data register  */
#define AC_INDEX     0x3C0   /* Attribute Controller index + data  */
#define AC_READ      0x3C1   /* Attribute Controller read-back     */
#define STATUS_REG   0x3DA   /* Input Status 1; reading resets AC flip-flop */
#define MISC_OUTPUT  0x3C2   /* Miscellaneous Output register      */

/* -------------------------------------------------------------------------
 * Sequencer register indices
 * -------------------------------------------------------------------------*/
#define SEQ_MAP_MASK  0x02   /* Map Mask: bitmask of writable planes (0-3) */

/* -------------------------------------------------------------------------
 * Graphics Controller register indices
 * -------------------------------------------------------------------------*/
#define GC_SET_RESET         0x00
#define GC_ENABLE_SET_RESET  0x01
#define GC_COLOR_COMPARE     0x02
#define GC_DATA_ROTATE       0x03
#define GC_READ_MAP          0x04
#define GC_MODE              0x05
#define GC_MISC              0x06
#define GC_COLOR_DONT_CARE   0x07
#define GC_BIT_MASK          0x08

/* -------------------------------------------------------------------------
 * EGA palette colour indices (see ARCHITECTURE.md colour table)
 * -------------------------------------------------------------------------*/
#define EGA_BLACK    0
#define EGA_GREEN    2
#define EGA_CYAN     3
#define EGA_RED      4
#define EGA_MAGENTA  5
#define EGA_WHITE    7
#define EGA_YELLOW   14

/* -------------------------------------------------------------------------
 * Dirty-rectangle infrastructure
 * -------------------------------------------------------------------------*/
#define MAX_DIRTY_RECTS 64

typedef struct {
    int x, y, w, h;
} DirtyRect;

/* -------------------------------------------------------------------------
 * Prototypes
 * -------------------------------------------------------------------------*/

/* Mode / sync */
void ega_set_mode(void);
void ega_set_text_mode(void);
void ega_wait_vsync(void);

/* Plane helper (exported for CACHE/SPRITES direct plane access) */
void ega_set_plane(unsigned char mask);

/* Pixel ops */
void          ega_put_pixel(int x, int y, unsigned char color);
unsigned char ega_get_pixel(int x, int y);

/* Fills */
void ega_clear(unsigned char color);
void ega_fill_rect(int x, int y, int w, int h, unsigned char color);

/* Palette */
void ega_set_palette(unsigned char index, unsigned char value);
void ega_set_all_palette(const unsigned char *pal16);
void ega_palette_enable(void);

/*
 * Planar blit -- copies a pre-generated sprite frame to VRAM.
 *
 * Source layout (plane-major, row-major within each plane):
 *   [0 .. h*w_bytes)             plane 0
 *   [h*w_bytes .. 2*h*w_bytes)   plane 1
 *   [2*h*w_bytes .. 3*h*w_bytes) plane 2
 *   [3*h*w_bytes .. 4*h*w_bytes) plane 3
 *
 * x MUST be a multiple of 8 (byte-aligned).
 * w_bytes = pixel width / 8.
 * CACHE.C stores frames in this layout.
 */
void ega_blit_planar(int x, int y, int w_bytes, int h,
                     unsigned char far *src);

/* Dirty-rectangle tracking (mechanism only; erase policy lives in game loop) */
void       ega_dirty_reset(void);
void       ega_dirty_add(int x, int y, int w, int h);
int        ega_dirty_count(void);
DirtyRect *ega_dirty_get(int i);

#endif /* EGA_H */
