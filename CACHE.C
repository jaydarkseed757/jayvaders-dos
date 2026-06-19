/*
 * CACHE.C -- sprite pre-generation pipeline (build step 3).
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * First run : converts SPRITES.C raw bitmaps to EGA plane-major format,
 *             writes SPRITES.DAT.
 * Later runs: loads SPRITES.DAT into near heap, wires g_cache far pointers
 *             so the game loop can call ega_blit_planar() without recalculating.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "EGA.H"
#include "SPRITES.H"
#include "CACHE.H"

#define DAT_VERSION_HI  0x00
#define DAT_VERSION_LO  0x01

/* -------------------------------------------------------------------------
 * Module state
 * -------------------------------------------------------------------------*/

SpriteCache g_cache;

/* Near pointer kept for fwrite / free; g_cache.data is the far-cast copy. */
static unsigned char *s_buf = NULL;

/* -------------------------------------------------------------------------
 * Row -> plane-byte converters
 * -------------------------------------------------------------------------*/

/*
 * Converts one row of a sprite (≤ 16 pixels wide) to w_bytes plane bytes.
 * bit_row: bit (w-1) = leftmost pixel, bit 0 = rightmost.
 * color_bit: 1 = copy bitmap into out[], 0 = write zeros.
 * Plane-major layout requires calling once per plane per row.
 */
static void row_to_plane_bytes(unsigned int bit_row, int w, int w_bytes,
                                int color_bit, unsigned char *out)
{
    unsigned int mask_w, aligned;
    int shift, b;

    if (color_bit == 0) {
        for (b = 0; b < w_bytes; b++) out[b] = 0;
        return;
    }

    mask_w  = (w < 16) ? ((1U << w) - 1U) : 0xFFFFU;
    shift   = w_bytes * 8 - w;           /* left-align pixels in byte sequence */
    aligned = (bit_row & mask_w) << shift;

    for (b = 0; b < w_bytes; b++) {
        out[b] = (unsigned char)((aligned >> ((w_bytes - 1 - b) * 8)) & 0xFF);
    }
}

/* Converts one 24-bit shield row to 3 plane bytes (bit 23 = leftmost pixel). */
static void shield_row_to_plane_bytes(unsigned long bit_row, int color_bit,
                                      unsigned char *out)
{
    if (color_bit == 0) {
        out[0] = out[1] = out[2] = 0;
        return;
    }
    out[0] = (unsigned char)((bit_row >> 16) & 0xFF);
    out[1] = (unsigned char)((bit_row >>  8) & 0xFF);
    out[2] = (unsigned char)(bit_row & 0xFF);
}

/* -------------------------------------------------------------------------
 * Sprite / shield / font encoders
 * -------------------------------------------------------------------------*/

/*
 * Encodes all frames of sprite s into *pp in plane-major order, advancing *pp.
 * Layout per frame: [plane0 rows][plane1 rows][plane2 rows][plane3 rows].
 * EGA color bit k == 1 -> plane k carries the bitmap; 0 -> plane k is zeros.
 */
static void cache_encode_sprite(const RawSprite *s, unsigned char **pp)
{
    int w_bytes, f, k, r, color_bit;

    w_bytes = (s->w + 7) / 8;

    for (f = 0; f < s->frames; f++) {
        for (k = 0; k < 4; k++) {
            color_bit = (s->color >> k) & 1;
            for (r = 0; r < s->h; r++) {
                row_to_plane_bytes(s->rows[f * s->h + r], s->w,
                                   w_bytes, color_bit, *pp);
                *pp += w_bytes;
            }
        }
    }
}

/* Encodes all 4 shield states. EGA_GREEN=2=0010 -> plane 1 carries bitmap. */
static void cache_encode_shields(unsigned char **pp)
{
    int state, k, r, color_bit;

    for (state = 0; state < SHIELD_STATES; state++) {
        for (k = 0; k < 4; k++) {
            color_bit = (EGA_GREEN >> k) & 1;
            for (r = 0; r < SHIELD_H; r++) {
                shield_row_to_plane_bytes(g_shield[state * SHIELD_H + r],
                                          color_bit, *pp);
                *pp += 3;
            }
        }
    }
}

/* Encodes all 36 font glyphs. EGA_WHITE=7=0111 -> planes 0-2 carry bitmap. */
static void cache_encode_font(unsigned char **pp)
{
    int glyph, k, r, color_bit;

    for (glyph = 0; glyph < FONT_GLYPHS; glyph++) {
        for (k = 0; k < 4; k++) {
            color_bit = (EGA_WHITE >> k) & 1;
            for (r = 0; r < FONT_H; r++) {
                **pp = color_bit ? g_font[glyph * FONT_H + r] : 0;
                (*pp)++;
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Pointer setup -- identical for generate and load paths
 * -------------------------------------------------------------------------*/

/*
 * Walks buf in the fixed buffer layout order and wires all g_cache far pointers.
 * Must be kept in exact sync with the encode order in cache_generate_sprites().
 */
static void cache_setup_ptrs(unsigned char *buf)
{
    unsigned char *p;
    const RawSprite *s;
    int frame_sz, i;

    p = buf;

    /* Invaders: 2 frames each */
    s = spr_get(SPR_INV_A);
    frame_sz = 4 * s->h * ((s->w + 7) / 8);
    for (i = 0; i < s->frames; i++) {
        g_cache.inv_a[i] = (unsigned char far *)p;
        p += frame_sz;
    }

    s = spr_get(SPR_INV_B);
    frame_sz = 4 * s->h * ((s->w + 7) / 8);
    for (i = 0; i < s->frames; i++) {
        g_cache.inv_b[i] = (unsigned char far *)p;
        p += frame_sz;
    }

    s = spr_get(SPR_INV_C);
    frame_sz = 4 * s->h * ((s->w + 7) / 8);
    for (i = 0; i < s->frames; i++) {
        g_cache.inv_c[i] = (unsigned char far *)p;
        p += frame_sz;
    }

    /* Single-frame sprites */
    s = spr_get(SPR_INV_EXPLODE);
    frame_sz = 4 * s->h * ((s->w + 7) / 8);
    g_cache.inv_explode = (unsigned char far *)p;
    p += frame_sz;

    s = spr_get(SPR_PLAYER);
    frame_sz = 4 * s->h * ((s->w + 7) / 8);
    g_cache.player = (unsigned char far *)p;
    p += frame_sz;

    /* Player explosion: 8 frames */
    s = spr_get(SPR_PLR_EXPLODE);
    frame_sz = 4 * s->h * ((s->w + 7) / 8);
    for (i = 0; i < s->frames; i++) {
        g_cache.plr_explode[i] = (unsigned char far *)p;
        p += frame_sz;
    }

    /* UFO: 2 frames -- [0]=normal, [1]=explosion (both from SPR_UFO) */
    s = spr_get(SPR_UFO);
    frame_sz = 4 * s->h * ((s->w + 7) / 8);
    for (i = 0; i < s->frames; i++) {
        g_cache.ufo[i] = (unsigned char far *)p;
        p += frame_sz;
    }

    /* Bullet: 1 frame */
    s = spr_get(SPR_BULLET);
    frame_sz = 4 * s->h * ((s->w + 7) / 8);
    g_cache.bullet = (unsigned char far *)p;
    p += frame_sz;

    /* Bombs: 4 frames each */
    s = spr_get(SPR_BOMB_ROLLING);
    frame_sz = 4 * s->h * ((s->w + 7) / 8);
    for (i = 0; i < s->frames; i++) {
        g_cache.bomb_rolling[i] = (unsigned char far *)p;
        p += frame_sz;
    }

    s = spr_get(SPR_BOMB_PLUNGER);
    frame_sz = 4 * s->h * ((s->w + 7) / 8);
    for (i = 0; i < s->frames; i++) {
        g_cache.bomb_plunger[i] = (unsigned char far *)p;
        p += frame_sz;
    }

    s = spr_get(SPR_BOMB_SQUIGGLY);
    frame_sz = 4 * s->h * ((s->w + 7) / 8);
    for (i = 0; i < s->frames; i++) {
        g_cache.bomb_squiggly[i] = (unsigned char far *)p;
        p += frame_sz;
    }

    /* Lives icon: 1 frame */
    s = spr_get(SPR_LIVES_ICON);
    frame_sz = 4 * s->h * ((s->w + 7) / 8);
    g_cache.lives_icon = (unsigned char far *)p;
    p += frame_sz;

    /* Shields: 4 states x (4 planes x SHIELD_H rows x 3 bytes) */
    for (i = 0; i < SHIELD_STATES; i++) {
        g_cache.shield[i] = (unsigned char far *)p;
        p += 4 * SHIELD_H * 3;
    }

    /* Font: 36 glyphs x (4 planes x FONT_H rows x 1 byte) */
    for (i = 0; i < FONT_GLYPHS; i++) {
        g_cache.font[i] = (unsigned char far *)p;
        p += 4 * FONT_H;
    }
}

/* -------------------------------------------------------------------------
 * Total size (must mirror encode + setup order exactly)
 * -------------------------------------------------------------------------*/

static unsigned long compute_total_size(void)
{
    const RawSprite *s;
    unsigned long total;

    total = 0;

    s = spr_get(SPR_INV_A);
    total += (unsigned long)s->frames * 4 * s->h * ((s->w + 7) / 8);

    s = spr_get(SPR_INV_B);
    total += (unsigned long)s->frames * 4 * s->h * ((s->w + 7) / 8);

    s = spr_get(SPR_INV_C);
    total += (unsigned long)s->frames * 4 * s->h * ((s->w + 7) / 8);

    s = spr_get(SPR_INV_EXPLODE);
    total += (unsigned long)4 * s->h * ((s->w + 7) / 8);

    s = spr_get(SPR_PLAYER);
    total += (unsigned long)4 * s->h * ((s->w + 7) / 8);

    s = spr_get(SPR_PLR_EXPLODE);
    total += (unsigned long)s->frames * 4 * s->h * ((s->w + 7) / 8);

    s = spr_get(SPR_UFO);
    total += (unsigned long)s->frames * 4 * s->h * ((s->w + 7) / 8);

    s = spr_get(SPR_BULLET);
    total += (unsigned long)4 * s->h * ((s->w + 7) / 8);

    s = spr_get(SPR_BOMB_ROLLING);
    total += (unsigned long)s->frames * 4 * s->h * ((s->w + 7) / 8);

    s = spr_get(SPR_BOMB_PLUNGER);
    total += (unsigned long)s->frames * 4 * s->h * ((s->w + 7) / 8);

    s = spr_get(SPR_BOMB_SQUIGGLY);
    total += (unsigned long)s->frames * 4 * s->h * ((s->w + 7) / 8);

    s = spr_get(SPR_LIVES_ICON);
    total += (unsigned long)4 * s->h * ((s->w + 7) / 8);

    total += (unsigned long)SHIELD_STATES * 4 * SHIELD_H * 3;
    total += (unsigned long)FONT_GLYPHS   * 4 * FONT_H;

    return total;
}

/* -------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

int cache_sprites_valid(void)
{
    FILE *fp;
    unsigned char hdr[8];
    int ok;

    fp = fopen("SPRITES.DAT", "rb");
    if (!fp) return 0;

    ok = (fread(hdr, 1, 8, fp) == 8) &&
         (hdr[0] == 'S') && (hdr[1] == 'P') &&
         (hdr[2] == DAT_VERSION_HI) && (hdr[3] == DAT_VERSION_LO);

    fclose(fp);
    return ok;
}

int cache_generate_sprites(void)
{
    unsigned long total;
    unsigned char *p;

    total  = compute_total_size();
    s_buf  = (unsigned char *)malloc((size_t)total);
    if (!s_buf) return -1;

    memset(s_buf, 0, (size_t)total);

    p = s_buf;
    cache_encode_sprite(spr_get(SPR_INV_A),         &p);
    cache_encode_sprite(spr_get(SPR_INV_B),         &p);
    cache_encode_sprite(spr_get(SPR_INV_C),         &p);
    cache_encode_sprite(spr_get(SPR_INV_EXPLODE),   &p);
    cache_encode_sprite(spr_get(SPR_PLAYER),        &p);
    cache_encode_sprite(spr_get(SPR_PLR_EXPLODE),   &p);
    cache_encode_sprite(spr_get(SPR_UFO),           &p); /* frames 0+1 incl. explosion */
    /* SPR_UFO_EXPLODE deliberately skipped -- it is frame 1 of SPR_UFO */
    cache_encode_sprite(spr_get(SPR_BULLET),        &p);
    cache_encode_sprite(spr_get(SPR_BOMB_ROLLING),  &p);
    cache_encode_sprite(spr_get(SPR_BOMB_PLUNGER),  &p);
    cache_encode_sprite(spr_get(SPR_BOMB_SQUIGGLY), &p);
    cache_encode_sprite(spr_get(SPR_LIVES_ICON),    &p);
    cache_encode_shields(&p);
    cache_encode_font(&p);

    g_cache.data      = (unsigned char far *)s_buf;
    g_cache.data_size = total;
    cache_setup_ptrs(s_buf);

    return 0;
}

int cache_save_sprites(void)
{
    FILE *fp;
    unsigned char hdr[8];
    unsigned long sz;

    if (!s_buf || g_cache.data_size == 0) return -1;

    sz     = g_cache.data_size;
    hdr[0] = 'S';
    hdr[1] = 'P';
    hdr[2] = DAT_VERSION_HI;
    hdr[3] = DAT_VERSION_LO;
    hdr[4] = (unsigned char)(sz         & 0xFF);
    hdr[5] = (unsigned char)((sz >>  8) & 0xFF);
    hdr[6] = (unsigned char)((sz >> 16) & 0xFF);
    hdr[7] = (unsigned char)((sz >> 24) & 0xFF);

    fp = fopen("SPRITES.DAT", "wb");
    if (!fp) return -1;

    if (fwrite(hdr,  1, 8,            fp) != 8 ||
        fwrite(s_buf, 1, (size_t)sz,  fp) != (size_t)sz) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

int cache_load_sprites(void)
{
    FILE *fp;
    unsigned char hdr[8];
    unsigned long sz;

    fp = fopen("SPRITES.DAT", "rb");
    if (!fp) return -1;

    if (fread(hdr, 1, 8, fp) != 8     ||
        hdr[0] != 'S' || hdr[1] != 'P' ||
        hdr[2] != DAT_VERSION_HI || hdr[3] != DAT_VERSION_LO) {
        fclose(fp);
        return -1;
    }

    sz = (unsigned long)hdr[4]          |
         ((unsigned long)hdr[5] <<  8)  |
         ((unsigned long)hdr[6] << 16)  |
         ((unsigned long)hdr[7] << 24);

    if (sz == 0 || sz > 65535UL) { fclose(fp); return -1; }

    s_buf = (unsigned char *)malloc((size_t)sz);
    if (!s_buf) { fclose(fp); return -1; }

    if (fread(s_buf, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        free(s_buf);
        s_buf = NULL;
        return -1;
    }
    fclose(fp);

    g_cache.data      = (unsigned char far *)s_buf;
    g_cache.data_size = sz;
    cache_setup_ptrs(s_buf);

    return 0;
}

void cache_free(void)
{
    if (s_buf) {
        free(s_buf);
        s_buf = NULL;
    }
    memset(&g_cache, 0, sizeof(g_cache));
}

/* -------------------------------------------------------------------------
 * Sound stubs (filled in by SOUND.C, build step 6)
 * -------------------------------------------------------------------------*/

int cache_generate_sounds(void) { return 0; }
int cache_save_sounds(void)     { return 0; }
int cache_load_sounds(void)     { return 0; }

/* -------------------------------------------------------------------------
 * Test harness
 * -------------------------------------------------------------------------*/

#ifdef CACHE_TEST
#include <conio.h>

int main(void)
{
    printf("Generating sprites...\n");
    if (cache_generate_sprites() != 0) {
        puts("FAILED: cache_generate_sprites");
        return 1;
    }
    printf("Buffer: %lu bytes\n", g_cache.data_size);

    printf("Saving SPRITES.DAT...\n");
    if (cache_save_sprites() != 0) {
        puts("FAILED: cache_save_sprites");
        cache_free();
        return 1;
    }

    cache_free();

    printf("Loading SPRITES.DAT...\n");
    if (cache_load_sprites() != 0) {
        puts("FAILED: cache_load_sprites");
        return 1;
    }
    printf("Loaded %lu bytes OK\n", g_cache.data_size);

    ega_set_mode();

    /* Invader A both frames at top-left */
    ega_blit_planar(0,  0, (INV_W + 7) / 8, INV_H, g_cache.inv_a[0]);
    ega_blit_planar(16, 0, (INV_W + 7) / 8, INV_H, g_cache.inv_a[1]);

    /* Font glyph '0' */
    ega_blit_planar(0, 16, (FONT_W + 7) / 8, FONT_H, g_cache.font[0]);

    /* Shield state 0 (3 bytes wide) */
    ega_blit_planar(0, 32, 3, SHIELD_H, g_cache.shield[0]);

    getch();
    ega_set_text_mode();
    cache_free();
    return 0;
}
#endif /* CACHE_TEST */
