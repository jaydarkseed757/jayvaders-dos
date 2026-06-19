#ifndef SPRITES_H
#define SPRITES_H

/*
 * SPRITES.H -- Raw arcade bitmap descriptors.
 *
 * SPRITES.C holds the static pixel data.  CACHE.C consumes it to build
 * SPRITES.DAT in EGA plane-major format.  Nothing here touches hardware.
 *
 * Row encoding for RawSprite:
 *   unsigned int rows[frames * h]
 *   bit (w-1) = leftmost pixel, bit 0 = rightmost pixel.
 *   Bits above (w-1) are zero.
 *
 * Shield rows use unsigned long (24 pixels wide):
 *   bit 23 = leftmost, bit 0 = rightmost.
 *
 * Font glyphs use unsigned char (8 pixels wide):
 *   bit 7 = leftmost, bit 0 = rightmost.
 */

/* -------------------------------------------------------------------------
 * Sprite dimensions
 * -------------------------------------------------------------------------*/
#define INV_W        11    /* invader pixel width  */
#define INV_H         8    /* invader pixel height */
#define PLAYER_W     13    /* player ship width    */
#define PLAYER_H      8
#define UFO_W        16    /* UFO width            */
#define UFO_H         7
#define BULLET_W      1
#define BULLET_H      8
#define BOMB_W        3    /* all bomb types       */
#define BOMB_H        8
#define SHIELD_W     24    /* bunker width (D4)    */
#define SHIELD_H     16
#define LIVES_W      11    /* HUD lives icon       */
#define LIVES_H       5
#define FONT_W        8
#define FONT_H        8

/* -------------------------------------------------------------------------
 * Sprite IDs (indices into g_sprites[])
 * -------------------------------------------------------------------------*/
#define SPR_INV_A          0   /* squid,   top rows,    cyan    */
#define SPR_INV_B          1   /* crab,    middle rows, magenta */
#define SPR_INV_C          2   /* octopus, bot row,     green   */
#define SPR_INV_EXPLODE    3   /* shared invader explosion frame */
#define SPR_PLAYER         4   /* player cannon                  */
#define SPR_PLR_EXPLODE    5   /* player explosion (up to 8 fr)  */
#define SPR_UFO            6   /* mystery ship                   */
#define SPR_UFO_EXPLODE    7
#define SPR_BULLET         8   /* player bullet                  */
#define SPR_BOMB_ROLLING   9
#define SPR_BOMB_PLUNGER  10
#define SPR_BOMB_SQUIGGLY 11
#define SPR_LIVES_ICON    12   /* small cannon for HUD           */
#define SPR_COUNT         13

#define SHIELD_STATES     4    /* staged damage states           */
#define FONT_GLYPHS      36   /* '0'-'9' then 'A'-'Z'           */

/* -------------------------------------------------------------------------
 * Raw sprite descriptor (for CACHE.C)
 * -------------------------------------------------------------------------*/
typedef struct {
    int                  w;       /* pixel width                         */
    int                  h;       /* pixel height                        */
    int                  frames;  /* animation frames in rows[]          */
    unsigned char        color;   /* EGA colour index                    */
    const unsigned int  *rows;    /* [frames * h] row bitmasks           */
} RawSprite;

/* -------------------------------------------------------------------------
 * Exported data (defined in SPRITES.C)
 * -------------------------------------------------------------------------*/
extern const RawSprite   g_sprites[SPR_COUNT];

/* Shield: 4 damage states × 16 rows, each row a 24-bit unsigned long.
 * g_shield[state * SHIELD_H + row], bit 23 = leftmost pixel. */
extern const unsigned long g_shield[SHIELD_STATES * SHIELD_H];

/* Font: 36 glyphs × 8 rows.
 * g_font[glyph * FONT_H + row], bit 7 = leftmost pixel.
 * Glyph 0-9 = digits '0'-'9'; glyph 10-35 = letters 'A'-'Z'. */
extern const unsigned char g_font[FONT_GLYPHS * FONT_H];

/* -------------------------------------------------------------------------
 * Access helpers
 * -------------------------------------------------------------------------*/

/* Returns pointer to first row of the requested sprite ID. */
const RawSprite *spr_get(int id);

/*
 * Map a printable character to a font glyph index.
 * Accepts '0'-'9' and 'A'-'Z' (upper-case only).
 * Returns -1 for unsupported characters.
 */
int spr_char_to_glyph(int ch);

#endif /* SPRITES_H */
