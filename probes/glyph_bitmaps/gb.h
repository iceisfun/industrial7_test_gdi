/* glyph_bitmaps: shared declarations (see main.c for the table list and docs/probes/glyph_bitmaps.md). */
#ifndef GB_H
#define GB_H
#include "rec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *face; int alias; int raster; } gb_face;

extern int g_full;                 /* --full (default) vs --quick */
extern const gb_face g_faces[];    /* every face the probe touches */
extern const int g_nfaces;
extern const int H_FULL[13], H_QUICK[3];   /* lfHeight lists (em height, px) */
int gb_face_on(int fi);            /* face enabled in this mode */

/* character sets */
extern const WCHAR g_core[];       /* small core: raw GGO bytes recorded */
extern WCHAR g_all[256]; extern int g_nall;   /* 0x20..0x7E + Latin-1 / punctuation extras */

/* font helpers */
HFONT gb_font(const char *face, int h, int weight, int italic, int underline, int strike, int quality);
void  gb_short_hash(char out[17], const void *p, size_t n);   /* first 64 bits of SHA-256, lowercase hex */
const char *gb_qname(int q);

/* sections */
void sec_ggo(void);        /* ggo.c: gm, gmf, gmu, ggo_all, ggo_* bitmaps */
void sec_text(void);       /* text.c: tx, txb, txprof */
void sec_methods(void);    /* text.c: txm */
void sec_styles(void);     /* text.c: sty, styb */
void sec_fonttab(void);    /* fonttab.c: ffile, ftab, fhead, fhhea, fos2, gasp, eblc, hdmx, vdmx */

/* DIB with a chosen depth: 32 (BGRA), 8 (256-grey palette, index == grey level), 1 (black/white palette) */
typedef struct { HDC dc; HBITMAP bmp, old; uint8_t *bits; int w, h, bpp, stride; } gb_dib;
int  gb_dib_new(gb_dib *d, int w, int h, int bpp);
void gb_dib_fill(gb_dib *d, int white);
void gb_dib_free(gb_dib *d);
/* grey level 0..255 of pixel (x,y) (BT.601 integer for 32bpp, palette index for 8bpp, 0/255 for 1bpp) */
int  gb_dib_grey(const gb_dib *d, int x, int y);
#endif
