#ifndef CACHE_H
#define CACHE_H

/*
 * CACHE.H -- sprite pre-generation pipeline.
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 */

#include "SPRITES.H"

typedef struct {
    unsigned char far *data;        /* base of malloc'd buffer          */
    unsigned long      data_size;   /* total bytes in buffer            */
    unsigned char far *inv_a[2];
    unsigned char far *inv_b[2];
    unsigned char far *inv_c[2];
    unsigned char far *inv_explode;
    unsigned char far *player;
    unsigned char far *plr_explode[8];
    unsigned char far *ufo[2];          /* [0]=normal  [1]=explosion        */
    unsigned char far *bullet;
    unsigned char far *bomb_rolling[4];
    unsigned char far *bomb_plunger[4];
    unsigned char far *bomb_squiggly[4];
    unsigned char far *lives_icon;
    unsigned char far *shield[SHIELD_STATES];
    unsigned char far *font[FONT_GLYPHS];
} SpriteCache;

extern SpriteCache g_cache;

/* Returns 1 if SPRITES.DAT has a valid header, 0 otherwise. */
int  cache_sprites_valid(void);

/* Converts raw SPRITES.C bitmaps to EGA plane-major format in near heap. */
int  cache_generate_sprites(void);  /* 0=ok  -1=malloc fail */

/* Writes SPRITES.DAT; call after cache_generate_sprites. */
int  cache_save_sprites(void);      /* 0=ok  -1=file error  */

/* Reads SPRITES.DAT and wires all g_cache pointers. */
int  cache_load_sprites(void);      /* 0=ok  -1=error       */

/* Frees the buffer and zeros all g_cache pointers. */
void cache_free(void);

/* Sound stubs -- real implementation in SOUND.C (build step 6). */
int  cache_generate_sounds(void);
int  cache_save_sounds(void);
int  cache_load_sounds(void);

#endif /* CACHE_H */
